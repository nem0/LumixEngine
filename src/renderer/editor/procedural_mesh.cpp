#include "procedural_mesh.h"
#include "animation/animation.h"
#include "../../../external/evox/capi.h"
#include "../../../external/evox/arena.h"
#include "core/log.h"
#include "core/math.h"
#include "core/profiler.h"
#include "core/stream.h"
#include "editor/action.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/editor_asset.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "engine/component_types.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "imgui/IconsFontAwesome5.h"
#include "renderer/editor/model_importer.h"
#include "renderer/editor/model_meta.h"
#include "renderer/editor/world_viewer.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_module.h"

namespace Lumix {

namespace {

// Memory layout of `core:procedural_geom.Mesh` as returned by `main`.
struct ExArray {
	ex_slice values;
	i64 size;
};

struct ExMesh {
	ExArray vertices;
	ExArray indices;
	i32 topology;
};

static_assert(sizeof(ProceduralMesh::Vertex) == 44);
static_assert(sizeof(ExArray) == 24);
static_assert(sizeof(ExMesh) == 56);

static constexpr u32 MAX_VERTICES = 1'000'000;
static constexpr u32 MAX_INDICES = 3'000'000;

const char* callResultName(ex_call_result result) {
	switch (result) {
		case EX_CALL_RESULT_OK: return "ok";
		case EX_CALL_RESULT_SUSPENDED: return "suspended";
		case EX_CALL_RESULT_FUNCTION_NOT_FOUND: return "function not found";
		case EX_CALL_RESULT_INVALID_ARGUMENT: return "invalid argument";
		case EX_CALL_RESULT_INVALID_STATE: return "invalid state";
		case EX_CALL_RESULT_ALREADY_EXECUTING: return "already executing";
		case EX_CALL_RESULT_NOT_SUSPENDED: return "not suspended";
		case EX_CALL_RESULT_NOT_RESUMABLE: return "not resumable";
		case EX_CALL_RESULT_OUT_OF_MEMORY: return "out of memory";
		case EX_CALL_RESULT_RUNTIME_ERROR: return "runtime error";
		case EX_CALL_RESULT_DIVISION_BY_ZERO: return "division by zero";
		case EX_CALL_RESULT_MODULO_BY_ZERO: return "modulo by zero";
		case EX_CALL_RESULT_INDEX_OUT_OF_BOUNDS: return "index out of bounds";
		case EX_CALL_RESULT_INVALID_FUNCTION_CALL: return "invalid function call";
		case EX_CALL_RESULT_PANIC: return "panic";
		case EX_CALL_RESULT_INVALID_YIELD_VALUE: return "invalid yield value";
		case EX_CALL_RESULT_STACK_OVERFLOW: return "stack overflow";
		case EX_CALL_RESULT_CALL_DEPTH: return "call depth exceeded";
	}
	return "unknown error";
}

bool isFinite(float value) {
	u32 bits;
	memcpy(&bits, &value, sizeof(bits));
	return (bits & 0x7f800000) != 0x7f800000;
}

bool isRelativePath(StringView path) {
	if (path.empty() || path.size() >= MAX_PATH || path[0] == '/' || path[0] == '\\') return false;
	for (u32 i = 0; i < path.size(); ++i) {
		if (!path[i] || path[i] == ':') return false;
		if (path[i] == '.' && i + 1 < path.size() && path[i + 1] == '.' && (!i || path[i - 1] == '/' || path[i - 1] == '\\') && (i + 2 == path.size() || path[i + 2] == '/' || path[i + 2] == '\\'))
			return false;
	}
	return true;
}

// Natives of the units a recipe may import. They need a world, so they are not available when compiling a resource.
void unavailableNative(ex_runtime*, ex_call_frame frame) {
	static const char message[] = "Function is not available in procedural mesh recipes";
	*frame.panic = {message, (i64)sizeof(message) - 1};
}

struct Recipe {
	Recipe(FileSystem& fs, const Path& path, ProceduralMesh& result, String& error, IAllocator& allocator, AssetCompiler* dependencies)
		: fs(fs)
		, path(path)
		, result(result)
		, error(error)
		, allocator(allocator)
		, dependencies(dependencies)
		, sources(allocator)
	{
		ex_default_arena_create(&host.arena);
		host.print = [](void* userdata, ex_string_view message) { ((String*)userdata)->append(StringView(message.begin, message.length)); };
		host.diagnostics_userdata = &error;
	}

	~Recipe() {
		if (task) ex_task_destroy(task);
		if (runtime) ex_runtime_destroy(runtime);
		if (bytecode) ex_bytecode_destroy(bytecode);
		if (module) ex_module_destroy(module);
		ex_default_arena_destroy(&host.arena);
	}

	static int resolveImport(void* userdata, ex_string_view name, ex_string_view, ex_string_view* source) {
		Recipe& self = *(Recipe*)userdata;
		const StringView requested(name.begin, name.length);
		Path file;
		if (startsWith(requested, "core:")) {
			const StringView unit = requested.withoutLeft(5);
			file = endsWith(unit, ".evox") ? Path("engine/scripts/core/", unit) : Path("engine/scripts/core/", unit, ".evox");
		} else {
			file = endsWith(requested, ".evox") ? Path(requested) : Path(requested, ".evox");
		}

		OutputMemoryStream& bytes = self.sources.emplace(self.allocator);
		if (!isRelativePath(file) || !self.fs.getContentSync(file, bytes)) {
			self.error.append("Could not read recipe input (invalid path or missing file): ", file, "\n");
			self.sources.pop();
			return 0;
		}
		if (self.dependencies) self.dependencies->registerDependency(self.path, file);

		*source = {(const char*)bytes.data(), (i64)bytes.size()};
		return 1;
	}

	static ex_native_fn resolveNativeFunction(ex_runtime*, ex_native_function_desc function, void* userdata) {
		const StringView unit(function.unit_path.begin, function.unit_path.length);
		if (unit == "core:procedural_geom" || unit == "core:entity") return &unavailableNative;
		((Recipe*)userdata)->error.append("Unsupported procedural mesh native: ", unit, ".", StringView(function.name.begin, function.name.length), "\n");
		return nullptr;
	}

	bool execute(StringView source) {
		module = ex_module_create(&host);
		if (!module) return false;
		if (ex_module_compile(module, {source.data, (i64)source.size()}, {path.c_str(), (i64)stringLength(path.c_str())}, &resolveImport, this) != EX_RESULT_OK) return false;

		ex_bytecode_compile_options compile_options = { true };
		bytecode = ex_bytecode_compile(module, &host, &compile_options);
		if (!bytecode) {
			error.append("Could not compile procedural mesh bytecode");
			return false;
		}

		runtime = ex_runtime_create(bytecode, &host);
		if (!runtime) return false;
		if (ex_runtime_set_native_resolver(runtime, &resolveNativeFunction, this) != EX_RESULT_OK) return false;

		task = ex_task_create(runtime);
		if (!task) {
			error.append("Could not create procedural mesh task");
			return false;
		}

		const ex_call_result status = ex_call(task, {"main", 4}, nullptr, 0);
		if (status != EX_CALL_RESULT_OK) {
			error.append("Procedural mesh main failed: ", callResultName(status));
			for (u32 i = 0; i < ex_debug_stack_depth(task); ++i) {
				ex_debug_location location = {};
				if (ex_debug_frame_location(task, i, &location) != EX_RESULT_OK) continue;

				const StaticString<32> line(location.line + 1);
				error.append("\n  at ", StringView(location.source_name.begin, location.source_name.length), ":", line);
			}
			return false;
		}

		u32 size = 0;
		const void* output = ex_task_result(task, &size);
		return acceptResult(output, size);
	}

	bool acceptResult(const void* data, u32 size) {
		const ex_type* type = ex_bytecode_runtime_result_type(runtime, {"main", 4});
		const ex_string_view type_name = ex_type_get_name(type);
		if (!data || StringView(type_name.begin, type_name.length) != "core:procedural_geom.Mesh" || size != sizeof(ExMesh)) {
			error.append("Recipe main must return core:procedural_geom.Mesh");
			return false;
		}

		ExMesh mesh;
		memcpy(&mesh, data, sizeof(mesh));
		if (mesh.topology != 0) {
			error.append("Recipe must return a triangle mesh, not a path or points");
			return false;
		}
		if (mesh.vertices.size <= 0 || mesh.vertices.size > MAX_VERTICES || !mesh.vertices.values.data
			|| mesh.indices.size <= 0 || mesh.indices.size > MAX_INDICES || mesh.indices.size % 3 != 0 || !mesh.indices.values.data
			|| mesh.vertices.size > mesh.vertices.values.length || mesh.indices.size > mesh.indices.values.length)
		{
			error.append("Recipe returned invalid mesh (1-1000000 vertices and 3-3000000 indices, multiple of 3, required)");
			return false;
		}

		const u32 vertex_count = (u32)mesh.vertices.size;
		const u32 index_count = (u32)mesh.indices.size;
		result.vertices.resize(vertex_count);
		result.indices.resize(index_count);
		memcpy(result.vertices.begin(), mesh.vertices.values.data, vertex_count * sizeof(ProceduralMesh::Vertex));
		memcpy(result.indices.begin(), mesh.indices.values.data, index_count * sizeof(u32));

		for (u32 index : result.indices) {
			if (index >= vertex_count) {
				error.append("Recipe returned index out of bounds");
				return false;
			}
		}
		const float* floats = (const float*)result.vertices.begin();
		for (u32 i = 0, c = vertex_count * (sizeof(ProceduralMesh::Vertex) / sizeof(float)); i < c; ++i) {
			if (!isFinite(floats[i])) {
				error.append("Recipe returned non-finite vertex data");
				return false;
			}
		}
		return true;
	}

	FileSystem& fs;
	const Path& path;
	ProceduralMesh& result;
	String& error;
	IAllocator& allocator;
	AssetCompiler* dependencies;
	Array<OutputMemoryStream> sources;
	ex_host host = {};
	ex_module* module = nullptr;
	ex_bytecode* bytecode = nullptr;
	ex_runtime* runtime = nullptr;
	ex_task* task = nullptr;
};

// Converts a compiled recipe to the regular model data.
struct ProceduralMeshImporter final : ModelImporter {
	ProceduralMeshImporter(StudioApp& app, const ProceduralMesh& mesh, StringView name)
		: ModelImporter(app)
		, m_mesh(mesh)
		, m_name(name, app.getAllocator())
	{}

	bool parseSimple(const Path& filename) override { return false; }

	bool parse(const Path& filename, const ModelMeta& meta) override {
		ImportMaterial& material = m_materials.emplace(m_allocator);
		material.name = m_name;
		material.diffuse_color = Vec3(1);

		ImportGeometry& geom = m_geometries.emplace(m_allocator);
		geom.vertex_size = sizeof(Vec3) + sizeof(u32) + sizeof(Vec2) + sizeof(u32);
		geom.material_index = 0;
		geom.is_skinned = false;
		geom.flip_handness = false;
		geom.name = m_name;
		geom.attributes.push({ AttributeSemantic::POSITION, gpu::AttributeType::FLOAT, 3 });
		geom.attributes.push({ AttributeSemantic::NORMAL, gpu::AttributeType::I8, 4 });
		geom.attributes.push({ AttributeSemantic::TEXCOORD0, gpu::AttributeType::FLOAT, 2 });
		geom.attributes.push({ AttributeSemantic::TANGENT, gpu::AttributeType::I8, 4 });

		geom.vertex_buffer.resize(m_mesh.vertices.size() * geom.vertex_size);
		u8* out = geom.vertex_buffer.getMutableData();
		for (const ProceduralMesh::Vertex& v : m_mesh.vertices) {
			const u32 normal = packF4u(normalize(v.normal));
			const u32 tangent = packF4u(normalize(v.tangent));
			memcpy(out, &v.position, sizeof(v.position)); out += sizeof(v.position);
			memcpy(out, &normal, sizeof(normal)); out += sizeof(normal);
			memcpy(out, &v.uv, sizeof(v.uv)); out += sizeof(v.uv);
			memcpy(out, &tangent, sizeof(tangent)); out += sizeof(tangent);
		}
		geom.indices.resize(m_mesh.indices.size());
		memcpy(geom.indices.begin(), m_mesh.indices.begin(), m_mesh.indices.byte_size());

		ImportMesh& mesh = m_meshes.emplace(m_allocator);
		mesh.geometry_idx = 0;
		mesh.lod = 0;
		mesh.name = m_name;
		return true;
	}

	void fillTracks(const ImportAnimation& anim, Array<Array<Key>>& tracks, u32 from_frame, u32 num_frames) const override {}

	const ProceduralMesh& m_mesh;
	String m_name;
};

const char DEFAULT_RECIPE[] = R"(import "core:procedural_geom"
import "core:vec3"

// Procedural mesh recipe. main must return the mesh to be compiled to a model.
fn main() : Mesh {
    var body = cube(1.0, 1.0, 1.0);
    var top = translate(sphere(0.5, 32, 16), Vec3 { 0.0, 0.75, 0.0 });
    return merge(body, top);
}
)";

// ProceduralMeshEditor must be the first base, UniquePtr deletes the plugin through a pointer to it
struct Plugin final : ProceduralMeshEditor, EditorAssetPlugin {
	struct Window final : AssetEditorWindow {
		Window(const Path& path, StudioApp& app, IAllocator& allocator)
			: AssetEditorWindow(app)
			, m_allocator(allocator)
			, m_path(path)
			, m_viewer(app)
		{
			Engine& engine = app.getEngine();
			m_resource = engine.getResourceManager().load<Model>(path);
			auto* render_module = static_cast<RenderModule*>(m_viewer.m_world->getModule(types::model_instance));
			render_module->setModelInstancePath(*m_viewer.m_mesh, path);

			OutputMemoryStream source(m_allocator);
			if (engine.getFileSystem().getContentSync(path, source)) {
				m_editor = createEvoxCodeEditor(app);
				m_editor->setText(StringView((const char*)source.data(), (u32)source.size()));
				m_editor->focus();
			}
			else {
				logError("Failed to read ", path);
			}
		}

		~Window() { m_resource->decRefCount(); }

		void save() {
			if (m_editor) {
				OutputMemoryStream source(m_allocator);
				m_editor->serializeText(source);
				m_app.getAssetBrowser().saveResource(m_path, source);
			}
			m_dirty = false;
		}

		void fileChangedExternally() override { m_external_change.onFileChanged(); }

		void windowGUI() override {
			if (m_editor) m_external_change.gui(m_app, m_path, *m_editor, m_dirty);
			CommonActions& actions = m_app.getCommonActions();
			if (ImGui::BeginMenuBar()) {
				if (actions.save.iconButton(m_dirty, &m_app)) save();
				if (actions.open_externally.iconButton(true, &m_app)) m_app.getAssetBrowser().openInExternalEditor(m_resource);
				if (actions.view_in_browser.iconButton(true, &m_app)) m_app.getAssetBrowser().locate(*m_resource);
				ImGui::EndMenuBar();
			}

			if (!ImGui::BeginTable("tab", 2, ImGuiTableFlags_Resizable)) return;
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			if (m_editor && m_editor->gui("procedural_mesh_editor", ImVec2(0, 0), m_app.getMonospaceFont(), m_app.getDefaultFont())) m_dirty = true;

			ImGui::TableNextColumn();
			if (m_resource->isEmpty()) {
				ImGui::TextUnformatted("Loading...");
			}
			else if (!m_resource->isReady()) {
				ImGui::TextUnformatted("Failed to compile. See log for more info.");
			}
			else {
				if (ImGui::Checkbox("Wireframe", &m_wireframe)) setWireframe();
				ImGui::SameLine();
				if (ImGui::Button("Reset camera")) m_viewer.resetCamera(*m_resource);
				// recompiling the recipe replaces the materials, so apply the checkbox again
				if (m_resource->getMeshCount() > 0 && m_resource->getMeshMaterial(0).material != m_wireframe_material) setWireframe();
				statsGUI();
				if (!m_init) {
					m_init = true;
					m_viewer.resetCamera(*m_resource);
				}
				m_viewer.gui();
			}
			ImGui::EndTable();
		}

		void setWireframe() {
			for (i32 i = 0, c = m_resource->getMeshCount(); i < c; ++i) {
				m_resource->getMeshMaterial(i).material->setWireframe(m_wireframe);
			}
			m_wireframe_material = m_resource->getMeshCount() > 0 ? m_resource->getMeshMaterial(0).material : nullptr;
		}

		void statsGUI() {
			u32 vertices = 0, triangles = 0;
			for (i32 i = 0, c = m_resource->getMeshCount(); i < c; ++i) {
				const Mesh& mesh = m_resource->getMesh(i);
				vertices += (u32)mesh.vertices.size();
				triangles += mesh.indices_count / 3;
			}
			const AABB& aabb = m_resource->getAABB();
			const Vec3 size = aabb.max - aabb.min;
			ImGui::Text("Vertices: %u   Triangles: %u   Size: %.2f x %.2f x %.2f m", vertices, triangles, size.x, size.y, size.z);
		}

		const Path& getPath() override { return m_path; }
		const char* getName() const override { return "procedural mesh editor"; }

		IAllocator& m_allocator;
		Path m_path;
		Model* m_resource;
		WorldViewer m_viewer;
		UniquePtr<CodeEditor> m_editor;
		const Material* m_wireframe_material = nullptr;
		bool m_wireframe = false;
		ExternalFileChangeDialog m_external_change;
		bool m_init = false;
	};

	Plugin(StudioApp& app)
		: EditorAssetPlugin("Procedural mesh", PROCEDURAL_MESH_EXTENSION, Model::TYPE, app, app.getAllocator())
	{}

	bool compile(const Path& src) override {
		PROFILE_FUNCTION();
		OutputMemoryStream source(m_app.getAllocator());
		if (!m_app.getEngine().getFileSystem().getContentSync(src, source)) {
			logError("Failed to read ", src);
			return false;
		}
		return compileProceduralMeshModel(m_app, src, StringView((const char*)source.data(), (u32)source.size()));
	}

	void createResource(OutputMemoryStream& blob) override { blob.write(DEFAULT_RECIPE, sizeof(DEFAULT_RECIPE) - 1); }
	const char* getIcon() const override { return ICON_FA_CUBE; }

	void openEditor(const Path& path) override {
		IAllocator& allocator = m_app.getAllocator();
		UniquePtr<Window> win = UniquePtr<Window>::create(allocator, path, m_app, allocator);
		m_app.getAssetBrowser().addWindow(win.move());
	}
};

} // anonymous namespace

bool compileProceduralMesh(FileSystem& fs, StringView source, const Path& path, ProceduralMesh& result, String& error, IAllocator& allocator, AssetCompiler* dependencies) {
	result.vertices.clear();
	result.indices.clear();
	error = "";
	Recipe recipe(fs, path, result, error, allocator, dependencies);
	const bool success = recipe.execute(source);
	if (!success) {
		result.vertices.clear();
		result.indices.clear();
	}
	return success;
}

bool compileProceduralMeshModel(StudioApp& app, const Path& src, StringView source) {
	IAllocator& allocator = app.getAllocator();
	ProceduralMesh mesh(allocator);
	String error(allocator);
	if (!compileProceduralMesh(app.getEngine().getFileSystem(), source, src, mesh, error, allocator, &app.getAssetCompiler())) {
		logError(src, ": ", error);
		return false;
	}

	ProceduralMeshImporter importer(app, mesh, Path::getBasename(src));
	ModelMeta meta(allocator);
	return importer.parse(src, meta) && importer.write(src, meta);
}

UniquePtr<ProceduralMeshEditor> ProceduralMeshEditor::create(StudioApp& app) {
	return UniquePtr<Plugin>::create(app.getAllocator(), app);
}

} // namespace Lumix
