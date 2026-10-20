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

struct ExPart {
	ex_slice material;
	u32 lod;
	u32 first_index;
	u32 index_count;
};

struct ExMesh {
	ExArray vertices;
	ExArray indices;
	i32 topology;
	ExArray parts;
};

static_assert(sizeof(ProceduralMesh::Vertex) == 44);
static_assert(sizeof(ExPart) == 32);
static_assert(sizeof(ExArray) == 24);
static_assert(sizeof(ExMesh) == 80);

static constexpr u32 MAX_VERTICES = 1'000'000;
static constexpr u32 MAX_INDICES = 3'000'000;
// Material names become material files next to the recipe, so keep the number small.
static constexpr u32 MAX_MATERIALS = 64;
static constexpr u32 MAX_MATERIAL_NAME = 64;
// Matches the number of LOD levels of models.
static constexpr u32 MAX_LODS = 4;
static constexpr u32 NO_VERTEX = 0xffFFffFF;

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

	static Path importToPath(StringView requested) {
		if (startsWith(requested, "core:")) {
			const StringView unit = requested.withoutLeft(5);
			return endsWith(unit, ".evox") ? Path("engine/scripts/core/", unit) : Path("engine/scripts/core/", unit, ".evox");
		}
		return endsWith(requested, ".evox") ? Path(requested) : Path(requested, ".evox");
	}

	static int resolveImport(void* userdata, ex_string_view name, ex_string_view, ex_string_view* source) {
		Recipe& self = *(Recipe*)userdata;
		const Path file = importToPath(StringView(name.begin, name.length));

		// registered before the file is read: a missing import is a dependency too, creating or moving the file must rebuild the recipe
		if (self.dependencies) self.dependencies->registerDependency(self.path, file);
		OutputMemoryStream& bytes = self.sources.emplace(self.allocator);
		if (!isRelativePath(file) || !self.fs.getContentSync(file, bytes)) {
			self.error.append("Could not read recipe input (invalid path or missing file): ", file, "\n");
			self.sources.pop();
			return 0;
		}

		*source = {(const char*)bytes.data(), (i64)bytes.size()};
		return 1;
	}

	static ex_native_fn resolveNativeFunction(ex_runtime*, ex_native_function_desc function, void* userdata) {
		const StringView unit(function.unit_path.begin, function.unit_path.length);
		if (unit == "core:procedural_geom" || unit == "core:entity") return &unavailableNative;
		((Recipe*)userdata)->error.append("Unsupported procedural mesh native: ", unit, ".", StringView(function.name.begin, function.name.length), "\n");
		return nullptr;
	}

	bool execute(StringView source, u32 lod_count) {
		module = ex_module_create(&host);
		if (!module) return false;
		if (ex_module_compile(module, {source.data, (i64)source.size()}, {path.c_str(), (i64)stringLength(path.c_str())}, &resolveImport, this) != EX_RESULT_OK) return false;

		ex_bytecode_compile_options compile_options = { true };
		bytecode = ex_bytecode_compile(module, &host, &compile_options);
		if (!bytecode) {
			error.append("Could not compile procedural mesh bytecode");
			return false;
		}

		host.native_resolver = &resolveNativeFunction;
		host.native_resolver_userdata = this;
		runtime = ex_runtime_create(bytecode, &host);
		if (!runtime) return false;

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
		return acceptResult(output, size, lod_count);
	}

	bool acceptResult(const void* data, u32 size, u32 lod_count) {
		const ex_string_view name = {"main", 4};
		const ex_type* type = ex_bytecode_runtime_result_type(runtime, name);
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

		const i64 part_count = mesh.parts.size;
		if (part_count < 0 || part_count > MAX_INDICES / 3 || (part_count > 0 && (!mesh.parts.values.data || mesh.parts.values.length < part_count))) {
			error.append("Recipe returned invalid parts");
			return false;
		}
		if (part_count == 0) {
			const ProceduralMesh::Part all = {0, 0, 0, index_count};
			result.parts.push(all);
		}
		else {
			const ExPart* parts = (const ExPart*)mesh.parts.values.data;
			for (i64 i = 0; i < part_count; ++i) {
				const ExPart& part = parts[i];
				const i32 material = findMaterial(part.material);
				if (material < 0 || part.lod >= lod_count) {
					error.append("Recipe returned invalid part (a material name of 1-64 characters without / \\ : . and a level below lod_count in the meta file)");
					return false;
				}
				if (part.first_index > index_count || part.index_count > index_count - part.first_index || part.index_count % 3 != 0) {
					error.append("Recipe returned invalid part (an index range of whole triangles within the mesh)");
					return false;
				}
				const ProceduralMesh::Part converted = {(u32)material, part.lod, part.first_index, part.index_count};
				result.parts.push(converted);
			}
		}
		if (!result.hasLod(0)) {
			error.append("Recipe must return triangles for level 0");
			return false;
		}
		return true;
	}

	// Returns the index of the material name in the recipe, adding it if it is new. Returns -1 if the name is invalid.
	i32 findMaterial(ex_slice name) {
		if (name.length == 0) return 0;
		if (name.length < 0 || name.length > MAX_MATERIAL_NAME || !name.data) return -1;
		const StringView view((const char*)name.data, (u32)name.length);
		for (u32 c = 0; c < view.size(); ++c) {
			if (view[c] == '/' || view[c] == '\\' || view[c] == ':' || view[c] == '.' || (u8)view[c] < 32) return -1;
		}
		for (u32 i = 1, c = (u32)result.material_names.size(); i < c; ++i) {
			if (StringView(result.material_names[i].c_str()) == view) return (i32)i;
		}
		if (result.material_names.size() >= (i32)MAX_MATERIALS) return -1;
		result.material_names.push(String(view, allocator));
		return (i32)result.material_names.size() - 1;
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

// Converts a compiled recipe to the regular model data. Each material of each level becomes a mesh.
struct ProceduralMeshImporter final : ModelImporter {
	ProceduralMeshImporter(StudioApp& app, const ProceduralMesh& mesh, StringView name)
		: ModelImporter(app)
		, m_mesh(mesh)
		, m_name(name, app.getAllocator())
	{}

	bool parseSimple(const Path& filename) override { return false; }

	bool parse(const Path& filename, const ModelMeta& meta) override {
		const u32 material_count = (u32)m_mesh.material_names.size();
		const u32 lod_count = m_mesh.getLodCount();
		// material of each name, created when the name is first used
		Array<u32> materials(m_allocator);
		materials.resize(material_count);
		for (u32& material : materials) material = NO_VERTEX;

		for (u32 lod = 0; lod < lod_count; ++lod) {
			// levels without parts are generated from level 0 by the model importer, if autolodN is set
			for (u32 material = 0; material < material_count; ++material) {
				Array<u32> indices(m_allocator);
				for (const ProceduralMesh::Part& part : m_mesh.parts) {
					if (part.lod != lod || part.material != material) continue;
					for (u32 i = 0; i < part.index_count; ++i) indices.push(m_mesh.indices[part.first_index + i]);
				}
				if (!indices.empty()) addMesh(lod, material, indices, materials);
			}
		}
		return true;
	}

	// Creates the geometry and mesh of one material of a level. Only the vertices used by the material are kept.
	void addMesh(u32 lod, u32 material, const Array<u32>& indices, Array<u32>& materials) {
		// the default material is named after the recipe, others after the recipe and their name
		String name(m_name, m_allocator);
		if (material > 0) name.append("_", m_mesh.material_names[material].c_str());
		if (materials[material] == NO_VERTEX) {
			ImportMaterial& import_material = m_materials.emplace(m_allocator);
			import_material.name = name;
			import_material.diffuse_color = Vec3(1);
			materials[material] = (u32)m_materials.size() - 1;
		}
		if (lod > 0) {
			const StaticString<16> suffix("_lod", lod);
			name.append(suffix);
		}

		const u32 vertex_count = (u32)m_mesh.vertices.size();
		Array<u32> remap(m_allocator);
		remap.resize(vertex_count);
		for (u32& r : remap) r = NO_VERTEX;
		Array<u32> used(m_allocator);
		for (u32 index : indices) {
			if (remap[index] != NO_VERTEX) continue;
			remap[index] = (u32)used.size();
			used.push(index);
		}

		ImportGeometry& geom = m_geometries.emplace(m_allocator);
		geom.vertex_size = sizeof(Vec3) + sizeof(u32) + sizeof(Vec2) + sizeof(u32);
		geom.material_index = materials[material];
		geom.is_skinned = false;
		geom.flip_handness = false;
		geom.name = name;
		geom.attributes.push({ AttributeSemantic::POSITION, gpu::AttributeType::FLOAT, 3 });
		geom.attributes.push({ AttributeSemantic::NORMAL, gpu::AttributeType::I8, 4 });
		geom.attributes.push({ AttributeSemantic::TEXCOORD0, gpu::AttributeType::FLOAT, 2 });
		geom.attributes.push({ AttributeSemantic::TANGENT, gpu::AttributeType::I8, 4 });

		geom.vertex_buffer.resize(used.size() * geom.vertex_size);
		u8* out = geom.vertex_buffer.getMutableData();
		for (u32 index : used) {
			const ProceduralMesh::Vertex& v = m_mesh.vertices[index];
			const u32 normal = packF4u(normalize(v.normal));
			const u32 tangent = packF4u(normalize(v.tangent));
			memcpy(out, &v.position, sizeof(v.position)); out += sizeof(v.position);
			memcpy(out, &normal, sizeof(normal)); out += sizeof(normal);
			memcpy(out, &v.uv, sizeof(v.uv)); out += sizeof(v.uv);
			memcpy(out, &tangent, sizeof(tangent)); out += sizeof(tangent);
		}
		geom.indices.resize((u32)indices.size());
		for (u32 i = 0, c = (u32)indices.size(); i < c; ++i) geom.indices[i] = remap[indices[i]];

		ImportMesh& mesh = m_meshes.emplace(m_allocator);
		mesh.geometry_idx = (u32)m_geometries.size() - 1;
		mesh.lod = lod;
		mesh.name = name;
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
			, m_meta(allocator)
		{
			Engine& engine = app.getEngine();
			m_resource = engine.getResourceManager().load<Model>(path);
			m_meta.load(path, app);
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
			if (m_meta_dirty) {
				OutputMemoryStream blob(m_allocator);
				m_meta.serialize(blob, m_path);
				m_app.getAssetCompiler().updateMeta(m_path, blob);
				m_meta_dirty = false;
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
				metaGUI();
			}
			else {
				if (ImGui::Checkbox("Wireframe", &m_wireframe)) setWireframe();
				ImGui::SameLine();
				if (ImGui::Button("Reset camera")) m_viewer.resetCamera(*m_resource);
				// recompiling the recipe replaces the materials, so apply the checkbox again
				if (m_resource->getMeshCount() > 0 && m_resource->getMeshMaterial(0).material != m_wireframe_material) setWireframe();
				metaGUI();
				if (ImGui::CollapsingHeader("Info")) infoGUI();
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

		// Same information as the Info section of the model editor (bones left out, a generated mesh has none).
		void infoGUI() {
			if (!m_resource->isReady()) {
				ImGui::TextUnformatted("Failed to load.");
				return;
			}

			ImGuiEx::Label("Bounding radius (from origin)");
			ImGui::Text("%f", m_resource->getOriginBoundingRadius());
			ImGuiEx::Label("Bounding radius (from center)");
			ImGui::Text("%f", m_resource->getCenterBoundingRadius());

			if (m_resource->getMeshCount() <= 0) return;

			ImGui::SeparatorText("LODs");
			const LODMeshIndices* lods = m_resource->getLODIndices();
			float* distances = m_resource->getLODDistances();
			if (lods[0].to >= 0 && !m_resource->isFailure() && ImGui::BeginTable("lodtbl", 5, ImGuiTableFlags_Resizable)) {
				ImGui::TableSetupColumn("LOD");
				ImGui::TableSetupColumn("Distance");
				ImGui::TableSetupColumn("# of meshes");
				ImGui::TableSetupColumn("# of triangles");
				ImGui::TableSetupColumn("# of vertices");
				ImGui::TableHeadersRow();

				for (i32 i = 0; i < Model::MAX_LOD_COUNT && lods[i].to >= 0; ++i) {
					ImGui::PushID(i);
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text("%d", i);
					ImGui::TableNextColumn();
					float dist = sqrtf(distances[i]);
					if (ImGui::DragFloat("", &dist)) distances[i] = dist * dist;
					ImGui::TableNextColumn();
					ImGui::Text("%d", lods[i].to - lods[i].from + 1);
					ImGui::TableNextColumn();
					i32 tri_count = 0;
					i32 num_vertices = 0;
					for (i32 j = lods[i].from; j <= lods[i].to; ++j) {
						const Mesh& mesh = m_resource->getMesh(j);
						const i32 indices_count = (i32)mesh.indices.size() >> (mesh.areIndices16() ? 1 : 2);
						tri_count += indices_count / 3;
						num_vertices += (i32)mesh.vertices.size();
					}
					ImGui::Text("%d", tri_count);
					ImGui::TableNextColumn();
					ImGui::Text("%d", num_vertices);
					ImGui::PopID();
				}
				ImGui::EndTable();
			}

			ImGui::SeparatorText("Meshes");
			if (ImGui::BeginTable("mshtbl", 3, ImGuiTableFlags_Resizable)) {
				ImGui::TableSetupColumn("Name");
				ImGui::TableSetupColumn("Triangles");
				ImGui::TableSetupColumn("Material");
				ImGui::TableHeadersRow();

				const float go_to_w = ImGui::CalcTextSize(ICON_FA_BULLSEYE).x;
				for (i32 i = 0; i < m_resource->getMeshCount(); ++i) {
					ImGui::PushID(i);
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					const Mesh& mesh = m_resource->getMesh(i);
					const MeshMaterial& mesh_mat = m_resource->getMeshMaterial(i);
					ImGuiEx::TextUnformatted(mesh.name);
					ImGui::TableNextColumn();
					ImGui::Text("%d", ((i32)mesh.indices.size() >> (mesh.areIndices16() ? 1 : 2)) / 3);
					ImGui::TableNextColumn();
					const float w = ImGui::GetContentRegionAvail().x - go_to_w;
					ImGuiEx::TextClipped(mesh_mat.material->getPath().c_str(), w);
					ImGui::SameLine();
					if (ImGuiEx::IconButton(ICON_FA_BULLSEYE, "Go to")) {
						m_app.getAssetBrowser().openEditor(mesh_mat.material->getPath());
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}

		// Import settings stored in the .meta file next to the recipe. Only what applies to a generated static mesh is shown.
		void metaGUI() {
			if (!ImGui::CollapsingHeader("Import settings")) return;
			bool changed = false;

			ImGuiEx::Label("Bake vertex AO");
			changed |= ImGui::Checkbox("##vrtxao", &m_meta.bake_vertex_ao);
			if (m_meta.bake_vertex_ao) {
				ImGuiEx::Label("Min bake vertex AO");
				changed |= ImGui::DragFloat("##minvrtxao", &m_meta.min_bake_vertex_ao, 0.01f, 0, 1);
			}
			ImGuiEx::Label("Culling scale");
			changed |= ImGui::InputFloat("##cull_scale", &m_meta.culling_scale);

			ImGuiEx::Label("Origin");
			if (ImGui::BeginCombo("##origin", ModelMeta::toUIString(m_meta.origin))) {
				const ModelMeta::Origin origins[] = { ModelMeta::Origin::SOURCE, ModelMeta::Origin::CENTER, ModelMeta::Origin::CENTER_XZ, ModelMeta::Origin::BOTTOM };
				for (ModelMeta::Origin o : origins) {
					if (ImGui::Selectable(ModelMeta::toUIString(o))) {
						m_meta.origin = o;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}

			ImGuiEx::Label("Physics");
			if (ImGui::BeginCombo("##phys", ModelMeta::toUIString(m_meta.physics))) {
				const ModelMeta::Physics physics[] = { ModelMeta::Physics::NONE, ModelMeta::Physics::CONVEX, ModelMeta::Physics::TRIMESH };
				for (ModelMeta::Physics p : physics) {
					if (ImGui::Selectable(ModelMeta::toUIString(p))) {
						m_meta.physics = p;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}

			ImGui::SeparatorText("LODs");
			ImGuiEx::Label("LOD count");
			if (ImGui::SliderInt("##lodcount", (i32*)&m_meta.lod_count, 1, 4)) {
				for (u32 i = 1; i < 4; ++i) {
					m_meta.lods_distances[i] = maximum(m_meta.lods_distances[i - 1] + 0.01f, m_meta.lods_distances[i]);
				}
				changed = true;
			}

			if (ImGui::BeginTable("lods", 4, ImGuiTableFlags_BordersOuter)) {
				ImGui::TableSetupColumn("LOD", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
				ImGui::TableSetupColumn("Distance");
				ImGui::TableSetupColumn("Auto LOD", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
				ImGui::TableSetupColumn("% triangles", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
				ImGui::TableHeadersRow();

				for (u32 i = 0; i < m_meta.lod_count; ++i) {
					ImGui::PushID(i);
					ImGui::TableNextColumn();
					ImGui::Text("%d", i);

					ImGui::TableNextColumn();
					ImGui::SetNextItemWidth(-1);
					if (ImGui::DragFloat("##lod", &m_meta.lods_distances[i], 1, 0, FLT_MAX, "%.1f")) {
						m_meta.lods_distances[0] = maximum(0.f, m_meta.lods_distances[0]);
						for (u32 j = 1; j < 4; ++j) {
							m_meta.lods_distances[j] = maximum(m_meta.lods_distances[j - 1] + 0.01f, m_meta.lods_distances[j]);
						}
						changed = true;
					}

					// level 0 is never generated, the recipe has to return it
					ImGui::TableNextColumn();
					bool autolod = m_meta.autolod_mask & (1 << i);
					if (i > 0) {
						ImGui::SetNextItemWidth(-1);
						if (ImGui::Checkbox("##auto_lod", &autolod)) {
							m_meta.autolod_mask &= ~(1 << i);
							if (autolod) m_meta.autolod_mask |= 1 << i;
							changed = true;
						}
					}

					ImGui::TableNextColumn();
					if (i > 0 && autolod) {
						ImGui::SetNextItemWidth(-1);
						float f = m_meta.autolod_coefs[i] * 100;
						if (ImGui::DragFloat("##lodcoef", &f, 1, 0, 100, "%.1f", ImGuiSliderFlags_AlwaysClamp)) {
							m_meta.autolod_coefs[i] = f * 0.01f;
							changed = true;
						}
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}

			if (changed) {
				m_meta_dirty = true;
				m_dirty = true;
			}
		}

		const Path& getPath() override { return m_path; }
		const char* getName() const override { return "procedural mesh editor"; }

		IAllocator& m_allocator;
		Path m_path;
		Model* m_resource;
		WorldViewer m_viewer;
		ModelMeta m_meta;
		bool m_meta_dirty = false;
		UniquePtr<CodeEditor> m_editor;
		const Material* m_wireframe_material = nullptr;
		bool m_wireframe = false;
		ExternalFileChangeDialog m_external_change;
		bool m_init = false;
	};

	Plugin(StudioApp& app)
		: EditorAssetPlugin("Procedural mesh", PROCEDURAL_MESH_EXTENSION, Model::TYPE, app, app.getAllocator())
	{}

	// Like Evox scripts, a recipe's imports are found by parsing it when it is scanned, not only while it compiles: a compile that fails
	// (or never ran) stops at the first broken import and would leave the later ones unregistered.
	void addSubresources(AssetCompiler& compiler, const Path& path, AtomicI32& signal) override {
		EditorAssetPlugin::addSubresources(compiler, path, signal);

		OutputMemoryStream content(m_app.getAllocator());
		if (!m_app.getEngine().getFileSystem().getContentSync(path, content)) return;

		ex_host host = {};
		ex_default_arena_create(&host.arena);
		ex_module* module = ex_module_create(&host);
		if (module) {
			ex_module_parse(module, {(const char*)content.data(), (i64)content.size()}, {path.c_str(), (i64)stringLength(path.c_str())});
			ex_unit* unit = ex_module_get_unit(module, 0);
			for (int i = 0, count = ex_unit_get_import_count(unit); i < count; ++i) {
				const ex_string_view value = ex_unit_get_import_path(unit, i);
				const StringView import_path(value.begin, value.length);
				if (startsWith(import_path, "std:")) continue;
				compiler.registerDependency(path, Recipe::importToPath(import_path));
			}
			ex_module_destroy(module);
		}
		ex_default_arena_destroy(&host.arena);
	}

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

bool compileProceduralMesh(FileSystem& fs, StringView source, const Path& path, ProceduralMesh& result, String& error, IAllocator& allocator, AssetCompiler* dependencies, u32 lod_count) {
	result.vertices.clear();
	result.indices.clear();
	result.parts.clear();
	result.material_names.clear();
	error = "";
	if (lod_count < 1 || lod_count > MAX_LODS) {
		error.append("LOD count must be 1-4");
		return false;
	}
	// the default material is always at index 0
	result.material_names.push(String(StringView(""), allocator));
	Recipe recipe(fs, path, result, error, allocator, dependencies);
	const bool success = recipe.execute(source, lod_count);
	if (!success) {
		result.vertices.clear();
		result.indices.clear();
		result.parts.clear();
		result.material_names.clear();
	}
	return success;
}

bool compileProceduralMeshModel(StudioApp& app, const Path& src, StringView source) {
	IAllocator& allocator = app.getAllocator();
	ModelMeta meta(allocator);
	meta.load(src, app);
	if (meta.lod_count < 1 || meta.lod_count > MAX_LODS) {
		logError(src, ": lod_count in the meta file must be 1-4");
		return false;
	}

	AssetCompiler& compiler = app.getAssetCompiler();
	// editing the meta file rebuilds the model
	compiler.registerDependency(src, Path(src, ".meta"));

	ProceduralMesh mesh(allocator);
	String error(allocator);
	if (!compileProceduralMesh(app.getEngine().getFileSystem(), source, src, mesh, error, allocator, &compiler, meta.lod_count)) {
		logError(src, ": ", error);
		return false;
	}

	// a level the recipe has parts for replaces the generated one
	for (u32 lod = 1; lod < meta.lod_count; ++lod) {
		if (mesh.hasLod(lod)) {
			meta.autolod_mask = (u8)(meta.autolod_mask & ~(1 << lod));
		}
		else if (!(meta.autolod_mask & (1 << lod))) {
			logError(src, ": LOD levels must have parts in the recipe or be enabled by autolodN in the meta file");
			return false;
		}
	}

	ProceduralMeshImporter importer(app, mesh, Path::getBasename(src));
	return importer.parse(src, meta) && importer.write(src, meta);
}

UniquePtr<ProceduralMeshEditor> ProceduralMeshEditor::create(StudioApp& app) {
	return UniquePtr<Plugin>::create(app.getAllocator(), app);
}

} // namespace Lumix
