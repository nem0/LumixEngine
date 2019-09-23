#include "../ffr/ffr.h"
#include "animation/animation.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/property_grid.h"
#include "editor/render_interface.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/geometry.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/lumix.h"
#include "engine/mt/atomic.h"
#include "engine/mt/thread.h"
#include "engine/os.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/prefab.h"
#include "engine/profiler.h"
#include "engine/queue.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/universe/universe.h"
#include "fbx_importer.h"
#include "game_view.h"
#include "renderer/culling_system.h"
#include "renderer/ffr/ffr.h"
#include "renderer/font.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/particle_system.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include "scene_view.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#if defined _MSC_VER && _MSC_VER == 1900 
#pragma warning(disable : 4312)
#endif
#include "imgui/imgui_freetype.h"
#include "stb/stb_image.h"
#include "stb/stb_image_resize.h"
#include "terrain_editor.h"
#include <cmft/clcontext.h>
#include <cmft/cubemapfilter.h>
#include <nvtt.h>
#include <stddef.h>


using namespace Lumix;


static const ComponentType PARTICLE_EMITTER_TYPE = Reflection::getComponentType("particle_emitter");
static const ComponentType TERRAIN_TYPE = Reflection::getComponentType("terrain");
static const ComponentType CAMERA_TYPE = Reflection::getComponentType("camera");
static const ComponentType DECAL_TYPE = Reflection::getComponentType("decal");
static const ComponentType POINT_LIGHT_TYPE = Reflection::getComponentType("point_light");
static const ComponentType ENVIRONMENT_TYPE = Reflection::getComponentType("environment");
static const ComponentType MODEL_INSTANCE_TYPE = Reflection::getComponentType("model_instance");
static const ComponentType TEXT_MESH_TYPE = Reflection::getComponentType("text_mesh");
static const ComponentType ENVIRONMENT_PROBE_TYPE = Reflection::getComponentType("environment_probe");


static bool saveAsDDS(const char* path, const u8* data, int w, int h) {
	ASSERT(data);
	OS::OutputFile file;
	if (!file.open(path)) return false;
	
	nvtt::Context context;
		
	nvtt::InputOptions input;
	input.setMipmapGeneration(true);
	input.setAlphaMode(nvtt::AlphaMode_Transparency);
	input.setNormalMap(false);
	input.setTextureLayout(nvtt::TextureType_2D, w, h);
	input.setMipmapData(data, w, h);
		
	nvtt::OutputOptions output;
	output.setSrgbFlag(false);
	struct : nvtt::OutputHandler {
		bool writeData(const void * data, int size) override { return dst->write(data, size); }
		void beginImage(int size, int width, int height, int depth, int face, int miplevel) override {}
		void endImage() override {}

		OS::OutputFile* dst;
	} output_handler;
	output_handler.dst = &file;
	output.setOutputHandler(&output_handler);

	nvtt::CompressionOptions compression;
	compression.setFormat(nvtt::Format_DXT5);
	compression.setQuality(nvtt::Quality_Fastest);

	if (!context.process(input, compression, output)) {
		file.close();
		return false;
	}
	file.close();
	return true;
}


struct FontPlugin final : public AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	FontPlugin(StudioApp& app) 
		: m_app(app) 
	{
		app.getAssetCompiler().registerExtension("ttf", FontResource::TYPE); 
	}
	
	bool compile(const Path& src) override
	{
		return m_app.getAssetCompiler().copyCompile(src);
	}

	void onGUI(Span<Resource*> resources) override {}
	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Font"; }

	ResourceType getResourceType() const override { return FontResource::TYPE; }

	StudioApp& m_app;
};


struct PipelinePlugin final : AssetCompiler::IPlugin
{
	explicit PipelinePlugin(StudioApp& app)
		: m_app(app)
	{}

	bool compile(const Path& src) override
	{
		return m_app.getAssetCompiler().copyCompile(src);
	}

	StudioApp& m_app;
};


struct ParticleEmitterPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	explicit ParticleEmitterPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("par", ParticleEmitterResource::TYPE);
	}


	bool compile(const Path& src) override
	{
		return m_app.getAssetCompiler().copyCompile(src);
	}
	
	
	void onGUI(Span<Resource*> resources) override {}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Particle Emitter"; }
	ResourceType getResourceType() const override { return ParticleEmitterResource::TYPE; }


	StudioApp& m_app;
};


struct MaterialPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	explicit MaterialPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("mat", Material::TYPE);
	}

	bool canCreateResource() const override { return true; }
	const char* getFileDialogFilter() const override { return "Material\0*.mat\0"; }
	const char* getFileDialogExtensions() const override { return "mat"; }
	const char* getDefaultExtension() const override { return "mat"; }

	bool createResource(const char* path) override {
		OS::OutputFile file;
		WorldEditor& editor = m_app.getWorldEditor();
		if (!file.open(path)) {
			logError("Renderer") << "Failed to create " << path;
			return false;
		}

		file << "shader \"/pipelines/standard.shd\"";
		file.close();
		return true;
	}

	bool compile(const Path& src) override
	{
		return m_app.getAssetCompiler().copyCompile(src);
	}


	void saveMaterial(Material* material)
	{
		if (OutputMemoryStream* file = m_app.getAssetBrowser().beginSaveResource(*material)) {
			bool success = true;
			if (!material->save(*file))
			{
				success = false;
				logError("Editor") << "Could not save file " << material->getPath().c_str();
			}
			m_app.getAssetBrowser().endSaveResource(*material, *file, success);
		}
	}

	void onGUIMultiple(Span<Resource*> resources) {
		if (ImGui::Button("Open in external editor")) {
			for (Resource* res : resources) {
				m_app.getAssetBrowser().openInExternalEditor(res);
			}
		}

		for (Resource* res : resources) {
			if (!res->isReady()) {
				ImGui::Text("%s is not ready", res->getPath().c_str());
				return;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Save")) {
			for (Resource* res : resources) {
				saveMaterial(static_cast<Material*>(res));
			}
		}

		char buf[MAX_PATH_LENGTH];
		auto* first = static_cast<Material*>(resources[0]);

		bool same_shader = true;
		for (Resource* res : resources) {
			if (static_cast<Material*>(res)->getShader() != first->getShader()) {
				same_shader = false;
			}
		}

		if (same_shader) {
			copyString(buf, first->getShader() ? first->getShader()->getPath().c_str() : "");
		}
		else {
			copyString(buf, "<different values>");
		}

		if (m_app.getAssetBrowser().resourceInput("Shader", "shader", Span(buf), Shader::TYPE)) {
			for (Resource* res : resources) {
				static_cast<Material*>(res)->setShader(Path(buf));
			}
		}

		if (!same_shader) return;

	}

	void onGUI(Span<Resource*> resources) override {
		if (resources.length() > 1) {
			onGUIMultiple(resources);
			return;
		}

		Material* material = static_cast<Material*>(resources[0]);
		if (ImGui::Button("Open in external editor")) m_app.getAssetBrowser().openInExternalEditor(material);
		if (!material->isReady()) return;

		if (ImGui::Button("Save")) saveMaterial(material);
		ImGui::SameLine();

		auto* plugin = m_app.getWorldEditor().getEngine().getPluginManager().getPlugin("renderer");
		auto* renderer = static_cast<Renderer*>(plugin);

		int alpha_cutout_define = renderer->getShaderDefineIdx("ALPHA_CUTOUT");

		bool b = material->isBackfaceCulling();
		if (ImGui::Checkbox("Backface culling", &b)) material->enableBackfaceCulling(b);

		if (material->getShader() 
			&& material->getShader()->isReady() 
			&& material->getShader()->hasDefine(alpha_cutout_define))
		{
			b = material->isDefined(alpha_cutout_define);
			if (ImGui::Checkbox("Is alpha cutout", &b)) material->setDefine(alpha_cutout_define, b);
			if (b) {
				float tmp = material->getAlphaRef();
				if (ImGui::DragFloat("Alpha reference value", &tmp, 0.01f, 0.0f, 1.0f)) {
					material->setAlphaRef(tmp);
				}
			}
		}

		Vec4 color = material->getColor();
		if (ImGui::ColorEdit4("Color", &color.x)) {
			material->setColor(color);
		}

		float roughness = material->getRoughness();
		if (ImGui::DragFloat("Roughness", &roughness, 0.01f, 0.0f, 1.0f)) {
			material->setRoughness(roughness);
		}

		float metallic = material->getMetallic();
		if (ImGui::DragFloat("Metallic", &metallic, 0.01f, 0.0f, 1.0f)) {
			material->setMetallic(metallic);
		}

		float emission = material->getEmission();
		if (ImGui::DragFloat("Emission", &emission, 0.01f, 0.0f)) {
			material->setEmission(emission);
		}

		char buf[MAX_PATH_LENGTH];
		copyString(buf, material->getShader() ? material->getShader()->getPath().c_str() : "");
		if (m_app.getAssetBrowser().resourceInput("Shader", "shader", Span(buf), Shader::TYPE)) {
			material->setShader(Path(buf));
		}

		const char* current_layer_name = renderer->getLayerName(material->getLayer());
		if (ImGui::BeginCombo("Layer", current_layer_name)) {
			for (u8 i = 0, c = renderer->getLayersCount(); i < c; ++i) {
				const char* name = renderer->getLayerName(i);
				if (ImGui::Selectable(name)) {
					material->setLayer(i);
				}
			}
			ImGui::EndCombo();
		}

		for (u32 i = 0; material->getShader() && i < material->getShader()->m_texture_slot_count; ++i) {
			auto& slot = material->getShader()->m_texture_slots[i];
			Texture* texture = material->getTexture(i);
			copyString(buf, texture ? texture->getPath().c_str() : "");
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
			bool is_node_open = ImGui::TreeNodeEx((const void*)(intptr_t)(i + 1),
				ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_Framed,
				"%s",
				"");
			ImGui::PopStyleColor(4);
			ImGui::SameLine();
			if (m_app.getAssetBrowser().resourceInput(
					slot.name, StaticString<30>("", (u64)&slot), Span(buf), Texture::TYPE))
			{
				material->setTexturePath(i, Path(buf));
			}
			if (!texture && is_node_open) {
				ImGui::TreePop();
				continue;
			}

			if (is_node_open) {
				ImGui::Image((void*)(uintptr_t)texture->handle.value, ImVec2(96, 96));

				for (int i = 0; i < Material::getCustomFlagCount(); ++i) {
					bool b = material->isCustomFlag(1 << i);
					if (ImGui::Checkbox(Material::getCustomFlagName(i), &b)) {
						if (b)
							material->setCustomFlag(1 << i);
						else
							material->unsetCustomFlag(1 << i);
					}
				}

				ImGui::TreePop();
			}
		}

		if (material->getShader() && material->isReady()) {
			const Shader* shader = material->getShader();
			for (int i = 0; i < shader->m_uniforms.size(); ++i) {
				const Shader::Uniform& shader_uniform = shader->m_uniforms[i];
				Material::Uniform* uniform = material->findUniform(shader_uniform.name_hash);
				if (!uniform) continue;

				switch (shader_uniform.type) {
					case Shader::Uniform::FLOAT:
						if (ImGui::DragFloat(shader_uniform.name, &uniform->float_value)) {
							material->updateRenderData(false);
						}
						break;
					case Shader::Uniform::VEC3:
						if (ImGui::DragFloat3(shader_uniform.name, uniform->vec3)) {
							material->updateRenderData(false);
						}
						break;
					case Shader::Uniform::VEC4:
						if (ImGui::DragFloat4(shader_uniform.name, uniform->vec4)) {
							material->updateRenderData(false);
						}
						break;
					case Shader::Uniform::VEC2:
						if (ImGui::DragFloat2(shader_uniform.name, uniform->vec2)) {
							material->updateRenderData(false);
						}
						break;
					case Shader::Uniform::COLOR:
						if (ImGui::ColorEdit3(shader_uniform.name, uniform->vec3)) {
							material->updateRenderData(false);
						}
						break;
					default: ASSERT(false); break;
				}
			}
			
			if (ImGui::CollapsingHeader("Defines")) {
				for (int i = 0; i < renderer->getShaderDefinesCount(); ++i) {
					const char* define = renderer->getShaderDefine(i);
					if (!shader->hasDefine(i)) continue;
					bool value = material->isDefined(i);

					auto isBuiltinDefine = [](const char* define) {
						const char* BUILTIN_DEFINES[] = {"HAS_SHADOWMAP", "ALPHA_CUTOUT", "SKINNED"};
						for (const char* builtin_define : BUILTIN_DEFINES)
						{
							if (equalStrings(builtin_define, define)) return true;
						}
						return false;
					};

					bool is_texture_define = material->isTextureDefine(i);
					if (!is_texture_define && !isBuiltinDefine(define) && ImGui::Checkbox(define, &value)) {
						material->setDefine(i, value);
					}
				}
			}

		}
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Material"; }
	ResourceType getResourceType() const override { return Material::TYPE; }


	StudioApp& m_app;
};


struct ModelPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	struct Meta
	{
		float scale = 1;
		bool split = false;
	};

	explicit ModelPlugin(StudioApp& app)
		: m_app(app)
		, m_mesh(INVALID_ENTITY)
		, m_pipeline(nullptr)
		, m_universe(nullptr)
		, m_is_mouse_captured(false)
		, m_tile(app.getWorldEditor().getAllocator())
		, m_fbx_importer(app.getAssetCompiler(), app.getWorldEditor().getEngine().getFileSystem(), app.getWorldEditor().getAllocator())
	{
		app.getAssetCompiler().registerExtension("fbx", Model::TYPE);
		createPreviewUniverse();
		createTileUniverse();
		m_viewport.is_ortho = false;
		m_viewport.fov = degreesToRadians(60.f);
		m_viewport.near = 0.1f;
		m_viewport.far = 10000.f;
	}


	~ModelPlugin()
	{
		JobSystem::wait(m_subres_signal);
		auto& engine = m_app.getWorldEditor().getEngine();
		engine.destroyUniverse(*m_universe);
		Pipeline::destroy(m_pipeline);
		engine.destroyUniverse(*m_tile.universe);
		Pipeline::destroy(m_tile.pipeline);
	}


	Meta getMeta(const Path& path) const
	{
		Meta meta;
		m_app.getAssetCompiler().getMeta(path, [&](lua_State* L){
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "scale", &meta.scale);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "split", &meta.split);
		});
		return meta;
	}

	void addSubresources(AssetCompiler& compiler, const char* path) override {
		compiler.addResource(Model::TYPE, path);

		const Meta meta = getMeta(Path(path));
		struct JobData {
			ModelPlugin* plugin;
			StaticString<MAX_PATH_LENGTH> path;
			Meta meta;
		};
		JobData* data = LUMIX_NEW(m_app.getWorldEditor().getAllocator(), JobData);
		data->plugin = this;
		data->path = path;
		data->meta = meta;
		JobSystem::runEx(data, [](void* ptr) {
			JobData* data = (JobData*)ptr;
			ModelPlugin* plugin = data->plugin;
			WorldEditor& editor = plugin->m_app.getWorldEditor();
			FileSystem& fs = editor.getEngine().getFileSystem();
			FBXImporter importer(plugin->m_app.getAssetCompiler(), fs, editor.getAllocator());
			AssetCompiler& compiler = plugin->m_app.getAssetCompiler();

			const char* path = data->path[0] == '/' ? data->path.data + 1 : data->path;
			importer.setSource(path, true);

			if(data->meta.split) {
				const Array<FBXImporter::ImportMesh>& meshes = importer.getMeshes();
				for (int i = 0; i < meshes.size(); ++i) {
					char mesh_name[256];
					importer.getImportMeshName(meshes[i], mesh_name);
					StaticString<MAX_PATH_LENGTH> tmp(mesh_name, ".fbx:", path);
					compiler.addResource(Model::TYPE, tmp);
				}
			}

			const Array<FBXImporter::ImportAnimation>& animations = importer.getAnimations();
			for (const FBXImporter::ImportAnimation& anim : animations) {
				StaticString<MAX_PATH_LENGTH> tmp(anim.name, ".ani:", path);
				compiler.addResource(Animation::TYPE, tmp);
			}

			LUMIX_DELETE(editor.getAllocator(), data);
		}, &m_subres_signal, JobSystem::INVALID_HANDLE, 2);			
	}

	static const char* getResourceFilePath(const char* str)
	{
		const char* c = str;
		while (*c && *c != ':') ++c;
		return *c != ':' ? str : c + 1;
	}

	bool compile(const Path& src) override
	{
		ASSERT(PathUtils::hasExtension(src.c_str(), "fbx"));
		const char* filepath = getResourceFilePath(src.c_str());
		FBXImporter::ImportConfig cfg;
		const Meta meta = getMeta(Path(filepath));
		cfg.mesh_scale = meta.scale;
		const PathUtils::FileInfo src_info(filepath);
		m_fbx_importer.setSource(filepath, false);
		if (m_fbx_importer.getMeshes().empty() && m_fbx_importer.getAnimations().empty()) {
			if (m_fbx_importer.getOFBXScene()->getMeshCount() > 0) {
				logError("Editor") << "No meshes with materials found in " << src;
			}
			else {
				logError("Editor") << "No meshes or animations found in " << src;
			}
		}

		const StaticString<32> hash_str("", src.getHash());
		if (meta.split) {
			//cfg.origin = FBXImporter::ImportConfig::Origin::CENTER;
			const Array<FBXImporter::ImportMesh>& meshes = m_fbx_importer.getMeshes();
			m_fbx_importer.writeSubmodels(filepath, cfg);
			m_fbx_importer.writePrefab(filepath, cfg);
		}
		m_fbx_importer.writeModel(src.c_str(), cfg);
		m_fbx_importer.writeMaterials(filepath, cfg);
		m_fbx_importer.writeAnimations(filepath, cfg);
		return true;
	}


	void createTileUniverse()
	{
		Engine& engine = m_app.getWorldEditor().getEngine();
		m_tile.universe = &engine.createUniverse(false);
		Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
		PipelineResource* pres = engine.getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
		m_tile.pipeline = Pipeline::create(*renderer, pres, "", engine.getAllocator());

		RenderScene* render_scene = (RenderScene*)m_tile.universe->getScene(MODEL_INSTANCE_TYPE);
		const EntityRef env_probe = m_tile.universe->createEntity({0, 0, 0}, Quat::IDENTITY);
		m_tile.universe->createComponent(ENVIRONMENT_PROBE_TYPE, env_probe);
		render_scene->setEnvironmentProbeRadius(env_probe, 1e3);

		Matrix mtx;
		mtx.lookAt({10, 10, 10}, Vec3::ZERO, {0, 1, 0});
		const EntityRef light_entity = m_tile.universe->createEntity({10, 10, 10}, mtx.getRotation());
		m_tile.universe->createComponent(ENVIRONMENT_TYPE, light_entity);
		render_scene->getEnvironment(light_entity).m_diffuse_intensity = 1;
		render_scene->getEnvironment(light_entity).m_indirect_intensity = 1;
		
		m_tile.pipeline->setScene(render_scene);
	}


	void createPreviewUniverse()
	{
		auto& engine = m_app.getWorldEditor().getEngine();
		m_universe = &engine.createUniverse(false);
		auto* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));
		PipelineResource* pres = engine.getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
		m_pipeline = Pipeline::create(*renderer, pres, "PREVIEW",  engine.getAllocator());

		const EntityRef mesh_entity = m_universe->createEntity({0, 0, 0}, {0, 0, 0, 1});
		auto* render_scene = static_cast<RenderScene*>(m_universe->getScene(MODEL_INSTANCE_TYPE));
		m_mesh = mesh_entity;
		m_universe->createComponent(MODEL_INSTANCE_TYPE, mesh_entity);

		const EntityRef env_probe = m_universe->createEntity({0, 0, 0}, Quat::IDENTITY);
		m_universe->createComponent(ENVIRONMENT_PROBE_TYPE, env_probe);
		render_scene->setEnvironmentProbeRadius(env_probe, 1e3);

		Matrix mtx;
		mtx.lookAt({10, 10, 10}, Vec3::ZERO, {0, 1, 0});
		const EntityRef light_entity = m_universe->createEntity({0, 0, 0}, mtx.getRotation());
		m_universe->createComponent(ENVIRONMENT_TYPE, light_entity);
		render_scene->getEnvironment(light_entity).m_diffuse_intensity = 1;
		render_scene->getEnvironment(light_entity).m_indirect_intensity = 1;

		m_pipeline->setScene(render_scene);
	}


	void showPreview(Model& model)
	{
		auto* render_scene = static_cast<RenderScene*>(m_universe->getScene(MODEL_INSTANCE_TYPE));
		if (!render_scene) return;
		if (!model.isReady()) return;
		if (!m_mesh.isValid()) return;

		if (render_scene->getModelInstanceModel((EntityRef)m_mesh) != &model)
		{
			render_scene->setModelInstancePath((EntityRef)m_mesh, model.getPath());
			AABB aabb = model.getAABB();

			const Vec3 center = (aabb.max + aabb.min) * 0.5f;
			m_viewport.pos = DVec3(0)  + center + Vec3(1, 1, 1) * (aabb.max - aabb.min).length();
			m_viewport.rot = Quat::vec3ToVec3({0, 0, 1}, {1, 1, 1});
		}
		ImVec2 image_size(ImGui::GetContentRegionAvailWidth(), ImGui::GetContentRegionAvailWidth());

		m_viewport.w = (int)image_size.x;
		m_viewport.h = (int)image_size.y;
		m_pipeline->setViewport(m_viewport);
		m_pipeline->render(false);
		m_preview = m_pipeline->getOutput();
		ImGui::Image((void*)(uintptr_t)m_preview.value, image_size);
		bool mouse_down = ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1);
		if (m_is_mouse_captured && !mouse_down)
		{
			m_is_mouse_captured = false;
			OS::showCursor(true);
			OS::setMouseScreenPos(m_captured_mouse_x, m_captured_mouse_y);
		}

		if (ImGui::GetIO().MouseClicked[1] && ImGui::IsItemHovered()) ImGui::OpenPopup("PreviewPopup");

		if (ImGui::BeginPopup("PreviewPopup"))
		{
			if (ImGui::Selectable("Save preview"))
			{
				model.getResourceManager().load(model);
				renderTile(&model, &m_viewport.pos, &m_viewport.rot);
			}
			ImGui::EndPopup();
		}

		if (ImGui::IsItemHovered() && mouse_down)
		{
			auto delta = m_app.getMouseMove();

			if (!m_is_mouse_captured)
			{
				m_is_mouse_captured = true;
				OS::showCursor(false);
				const OS::Point p = OS::getMouseScreenPos();
				m_captured_mouse_x = p.x;
				m_captured_mouse_y = p.y;
			}

			if (delta.x != 0 || delta.y != 0)
			{
				const Vec2 MOUSE_SENSITIVITY(50, 50);
				DVec3 pos = m_viewport.pos;
				Quat rot = m_viewport.rot;

				float yaw = -signum(delta.x) * (::powf(abs((float)delta.x / MOUSE_SENSITIVITY.x), 1.2f));
				Quat yaw_rot(Vec3(0, 1, 0), yaw);
				rot = yaw_rot * rot;
				rot.normalize();

				Vec3 pitch_axis = rot.rotate(Vec3(1, 0, 0));
				float pitch =
					-signum(delta.y) * (::powf(abs((float)delta.y / MOUSE_SENSITIVITY.y), 1.2f));
				Quat pitch_rot(pitch_axis, pitch);
				rot = pitch_rot * rot;
				rot.normalize();

				Vec3 dir = rot.rotate(Vec3(0, 0, 1));
				Vec3 origin = (model.getAABB().max + model.getAABB().min) * 0.5f;

				float dist = (origin - pos.toFloat()).length();
				pos = DVec3(0) + origin + dir * dist;

				m_viewport.rot = rot;
				m_viewport.pos = pos;
			}
		}
	}


	void onGUI(Span<Resource*> resources) override
	{
		if (resources.length() > 1) return;

		auto* model = static_cast<Model*>(resources[0]);

		if (model->isReady()) {
			ImGui::LabelText("Bounding radius", "%f", model->getBoundingRadius());

			auto* lods = model->getLODs();
			if (lods[0].to_mesh >= 0 && !model->isFailure())
			{
				ImGui::Separator();
				ImGui::Columns(4);
				ImGui::Text("LOD");
				ImGui::NextColumn();
				ImGui::Text("Distance");
				ImGui::NextColumn();
				ImGui::Text("# of meshes");
				ImGui::NextColumn();
				ImGui::Text("# of triangles");
				ImGui::NextColumn();
				ImGui::Separator();
				int lod_count = 1;
				for (int i = 0; i < Model::MAX_LOD_COUNT && lods[i].to_mesh >= 0; ++i)
				{
					ImGui::PushID(i);
					ImGui::Text("%d", i);
					ImGui::NextColumn();
					if (lods[i].distance == FLT_MAX)
					{
						ImGui::Text("Infinite");
					}
					else
					{
						float dist = sqrt(lods[i].distance);
						if (ImGui::DragFloat("", &dist))
						{
							lods[i].distance = dist * dist;
						}
					}
					ImGui::NextColumn();
					ImGui::Text("%d", lods[i].to_mesh - lods[i].from_mesh + 1);
					ImGui::NextColumn();
					int tri_count = 0;
					for (int j = lods[i].from_mesh; j <= lods[i].to_mesh; ++j)
					{
						int indices_count = model->getMesh(j).indices.size() >> 1;
						if (!model->getMesh(j).flags.isSet(Mesh::Flags::INDICES_16_BIT)) {
							indices_count >>= 1;
						}
						tri_count += indices_count / 3;

					}

					ImGui::Text("%d", tri_count);
					ImGui::NextColumn();
					++lod_count;
					ImGui::PopID();
				}

				ImGui::Columns(1);
			}

			ImGui::Separator();
			for (int i = 0; i < model->getMeshCount(); ++i)
			{
				auto& mesh = model->getMesh(i);
				if (ImGui::TreeNode(&mesh, "%s", mesh.name.length() > 0 ? mesh.name.c_str() : "N/A"))
				{
					ImGui::LabelText("Triangle count", "%d", (mesh.indices.size() >> (mesh.areIndices16() ? 1 : 2))/ 3);
					ImGui::LabelText("Material", "%s", mesh.material->getPath().c_str());
					ImGui::SameLine();
					if (ImGui::Button("->"))
					{
						m_app.getAssetBrowser().selectResource(mesh.material->getPath(), true, false);
					}
					ImGui::TreePop();
				}
			}

			ImGui::LabelText("Bone count", "%d", model->getBoneCount());
			if (model->getBoneCount() > 0 && ImGui::CollapsingHeader("Bones")) {
				ImGui::Columns(3);
				for (int i = 0; i < model->getBoneCount(); ++i)
				{
					ImGui::Text("%s", model->getBone(i).name.c_str());
					ImGui::NextColumn();
					Vec3 pos = model->getBone(i).transform.pos;
					ImGui::Text("%f; %f; %f", pos.x, pos.y, pos.z);
					ImGui::NextColumn();
					Quat rot = model->getBone(i).transform.rot;
					ImGui::Text("%f; %f; %f; %f", rot.x, rot.y, rot.z, rot.w);
					ImGui::NextColumn();
				}
			}
		}

		if (ImGui::CollapsingHeader("Import")) {
			AssetCompiler& compiler = m_app.getAssetCompiler();
			if(m_meta_res != model->getPath().getHash()) {
				m_meta = getMeta(model->getPath());
				m_meta_res = model->getPath().getHash();
			}
			ImGui::InputFloat("Scale", &m_meta.scale);
			ImGui::Checkbox("Split", &m_meta.split);
			if (ImGui::Button("Apply")) {
				StaticString<256> src("scale = ", m_meta.scale, "\nsplit = ", m_meta.split ? "true\n" : "false\n");
				compiler.updateMeta(model->getPath(), src);
				if (compiler.compile(model->getPath())) {
					model->getResourceManager().reload(*model);
				}
			}
		}

		showPreview(*model);
	}

	Meta m_meta;
	u32 m_meta_res = 0;

	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Model"; }
	ResourceType getResourceType() const override { return Model::TYPE; }


	void pushTileQueue(const Path& path)
	{
		ASSERT(!m_tile.queue.full());
		WorldEditor& editor = m_app.getWorldEditor();
		Engine& engine = editor.getEngine();
		ResourceManagerHub& resource_manager = engine.getResourceManager();

		Resource* resource;
		if (PathUtils::hasExtension(path.c_str(), "fab")) {
			resource = resource_manager.load<PrefabResource>(path);
		}
		else {
			resource = resource_manager.load<Model>(path);
		}
		m_tile.queue.push(resource);
	}


	void popTileQueue()
	{
		m_tile.queue.pop();
		if (m_tile.paths.empty()) return;

		Path path = m_tile.paths.back();
		m_tile.paths.pop();
		pushTileQueue(path);
	}
	
	static void destroyEntityRecursive(Universe& universe, EntityPtr entity)
	{
		if (!entity.isValid()) return;
			
		EntityRef e = (EntityRef)entity;
		destroyEntityRecursive(universe, universe.getFirstChild(e));
		destroyEntityRecursive(universe, universe.getNextSibling(e));

		universe.destroyEntity(e);
	}

	void update() override
	{
		if (m_tile.frame_countdown >= 0) {
			--m_tile.frame_countdown;
			if (m_tile.frame_countdown == -1) {
				destroyEntityRecursive(*m_tile.universe, (EntityRef)m_tile.entity);
				Engine& engine = m_app.getWorldEditor().getEngine();
				FileSystem& fs = engine.getFileSystem();
				StaticString<MAX_PATH_LENGTH> path(fs.getBasePath(), ".lumix/asset_tiles/", m_tile.path_hash, ".dds");
				saveAsDDS(path, &m_tile.data[0], AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE);
				memset(m_tile.data.begin(), 0, m_tile.data.byte_size());
				Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
				renderer->destroy(m_tile.texture);
				m_tile.entity = INVALID_ENTITY;
			}
			return;
		}

		if (m_tile.entity.isValid()) return;
		if (m_tile.queue.empty()) return;

		Resource* resource = m_tile.queue.front();
		if (resource->isFailure()) {
			logError("Editor") << "Failed to load " << resource->getPath();
			popTileQueue();
			return;
		}
		if (!resource->isReady()) return;

		popTileQueue();

		if (resource->getType() == Model::TYPE) {
			renderTile((Model*)resource, nullptr, nullptr);
		}
		else if (resource->getType() == PrefabResource::TYPE) {
			renderTile((PrefabResource*)resource);
		}
		else {
			ASSERT(false);
		}
	}


	void renderTile(PrefabResource* prefab)
	{
		Engine& engine = m_app.getWorldEditor().getEngine();
		RenderScene* render_scene = (RenderScene*)m_tile.universe->getScene(MODEL_INSTANCE_TYPE);
		if (!render_scene) return;

		Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
		if (!renderer) return;

		EntityPtr mesh_entity = m_tile.universe->instantiatePrefab(*prefab, DVec3(0), Quat::IDENTITY, 1);
		if (!mesh_entity.isValid()) return;

		if (!render_scene->getUniverse().hasComponent((EntityRef)mesh_entity, MODEL_INSTANCE_TYPE)) return;

		Model* model = render_scene->getModelInstanceModel((EntityRef)mesh_entity);
		if (!model) return;

		m_tile.path_hash = prefab->getPath().getHash();
		prefab->getResourceManager().unload(*prefab);
		m_tile.entity = mesh_entity;
		model->onLoaded<ModelPlugin, &ModelPlugin::renderPrefabSecondStage>(this);
	}


	void renderPrefabSecondStage(Resource::State old_state, Resource::State new_state, Resource& resource)
	{
		Engine& engine = m_app.getWorldEditor().getEngine();

		RenderScene* render_scene = (RenderScene*)m_tile.universe->getScene(MODEL_INSTANCE_TYPE);
		if (!render_scene) return;

		Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
		if (!renderer) return;

		if (new_state != Resource::State::READY) return;
		Model* model = (Model*)&resource;
		if (!model->isReady()) return;

		AABB aabb = model->getAABB();

		Vec3 center = (aabb.max + aabb.min) * 0.5f;
		Vec3 eye = center + Vec3(1, 1, 1) * (aabb.max - aabb.min).length() / SQRT2;
		Matrix mtx;
		mtx.lookAt(eye, center, Vec3(-1, 1, -1).normalized());
		mtx.inverse();
		Viewport viewport;
		viewport.is_ortho = false;
		viewport.far = 10000.f;
		viewport.near = 0.1f;
		viewport.fov = degreesToRadians(60.f);
		viewport.h = AssetBrowser::TILE_SIZE;
		viewport.w = AssetBrowser::TILE_SIZE;
		viewport.pos = DVec3(eye.x, eye.y, eye.z);
		viewport.rot = mtx.getRotation();
		m_tile.pipeline->setViewport(viewport);
		m_tile.pipeline->render(false);
		
		struct Cmd : Renderer::RenderJob
		{
			void setup() override {}
			void execute() override
			{
				PROFILE_FUNCTION();
				ffr::getTextureImage(texture, mem.size, mem.data);
			}

			Renderer::MemRef mem;
			ffr::TextureHandle texture;
		};

		m_tile.texture = ffr::allocTextureHandle(); 

		Cmd* cmd = LUMIX_NEW(renderer->getAllocator(), Cmd);
		cmd->texture = m_tile.pipeline->getOutput();
		m_tile.data.resize(AssetBrowser::TILE_SIZE * AssetBrowser::TILE_SIZE * 4);
		cmd->mem.data = &m_tile.data[0];
		cmd->mem.size = m_tile.data.size() * sizeof(&m_tile.data[0]);
		cmd->mem.own = false;
		renderer->queue(cmd, 0);
		m_tile.frame_countdown = 2;
	}


	void renderTile(Model* model, const DVec3* in_pos, const Quat* in_rot)
	{
		Engine& engine = m_app.getWorldEditor().getEngine();
		RenderScene* render_scene = (RenderScene*)m_tile.universe->getScene(MODEL_INSTANCE_TYPE);
		if (!render_scene) return;

		Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
		if (!renderer) return;

		EntityRef mesh_entity = m_tile.universe->createEntity({ 0, 0, 0 }, { 0, 0, 0, 1 });
		m_tile.universe->createComponent(MODEL_INSTANCE_TYPE, mesh_entity);

		render_scene->setModelInstancePath(mesh_entity, model->getPath());
		AABB aabb = model->getAABB();

		Matrix mtx;
		Vec3 center = (aabb.max + aabb.min) * 0.5f;
		Vec3 eye = center + Vec3(1, 1, 1) * (aabb.max - aabb.min).length() / SQRT2;
		mtx.lookAt(eye, center, Vec3(-1, 1, -1).normalized());
		mtx.inverse();
		Viewport viewport;
		viewport.is_ortho = false;
		viewport.far = 10000.f;
		viewport.near = 0.1f;
		viewport.fov = degreesToRadians(60.f);
		viewport.h = AssetBrowser::TILE_SIZE;
		viewport.w = AssetBrowser::TILE_SIZE;
		viewport.pos = in_pos ? *in_pos : DVec3(eye.x, eye.y, eye.z);
		viewport.rot = in_rot ? *in_rot : mtx.getRotation();
		m_tile.pipeline->setViewport(viewport);
		m_tile.pipeline->render(false);

		struct Cmd : Renderer::RenderJob
		{
			void setup() override {}
			void execute() override
			{
				PROFILE_FUNCTION();
				ffr::getTextureImage(texture, mem.size, mem.data);
			}

			Renderer::MemRef mem;
			ffr::TextureHandle texture;
		};

		m_tile.texture = ffr::allocTextureHandle(); 
		

		Cmd* cmd = LUMIX_NEW(renderer->getAllocator(), Cmd);
		cmd->texture = m_tile.pipeline->getOutput();
		m_tile.data.resize(AssetBrowser::TILE_SIZE * AssetBrowser::TILE_SIZE * 4);
		cmd->mem.data = &m_tile.data[0];
		cmd->mem.size = m_tile.data.size() * sizeof(&m_tile.data[0]);
		cmd->mem.own = false;
		renderer->queue(cmd, 0);
		m_tile.entity = mesh_entity;
		m_tile.frame_countdown = 2;
		m_tile.path_hash = model->getPath().getHash();
		model->getResourceManager().unload(*model);
	}


	bool createTile(const char* in_path, const char* out_path, ResourceType type) override
	{
		FileSystem& fs = m_app.getWorldEditor().getEngine().getFileSystem();
		if (type == Material::TYPE) return fs.copyFile("models/editor/tile_material.dds", out_path);
		if (type == Shader::TYPE) return fs.copyFile("models/editor/tile_shader.dds", out_path);

		if (type != Model::TYPE && type != PrefabResource::TYPE) return false;

		Path path(in_path);

		if (!m_tile.queue.full())
		{
			pushTileQueue(path);
			return true;
		}

		m_tile.paths.push(path);
		return true;
	}


	struct TileData
	{
		TileData(IAllocator& allocator)
			: data(allocator)
			, queue(allocator)
			, paths(allocator)
		{
		}

		Universe* universe = nullptr;
		Pipeline* pipeline = nullptr;
		EntityPtr entity = INVALID_ENTITY;
		int frame_countdown = -1;
		u32 path_hash;
		Array<u8> data;
		ffr::TextureHandle texture = ffr::INVALID_TEXTURE;
		Queue<Resource*, 8> queue;
		Array<Path> paths;
	} m_tile;
	

	StudioApp& m_app;
	ffr::TextureHandle m_preview;
	Universe* m_universe;
	Viewport m_viewport;
	Pipeline* m_pipeline;
	EntityPtr m_mesh = INVALID_ENTITY;
	bool m_is_mouse_captured;
	int m_captured_mouse_x;
	int m_captured_mouse_y;
	FBXImporter m_fbx_importer;
	JobSystem::SignalHandle m_subres_signal = JobSystem::INVALID_HANDLE;
};


struct TexturePlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	struct Meta
	{
		enum WrapMode : u32 {
			REPEAT,
			CLAMP
		};
		enum Filter : u32 {
			LINEAR,
			POINT
		};
		bool srgb = false;
		bool is_normalmap = false;
		WrapMode wrap_mode_u = WrapMode::REPEAT;
		WrapMode wrap_mode_v = WrapMode::REPEAT;
		WrapMode wrap_mode_w = WrapMode::REPEAT;
		Filter filter = Filter::LINEAR;
	};

	explicit TexturePlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("png", Texture::TYPE);
		app.getAssetCompiler().registerExtension("jpg", Texture::TYPE);
		app.getAssetCompiler().registerExtension("tga", Texture::TYPE);
		app.getAssetCompiler().registerExtension("dds", Texture::TYPE);
		app.getAssetCompiler().registerExtension("raw", Texture::TYPE);
	}


	~TexturePlugin() {
		PluginManager& plugin_manager = m_app.getWorldEditor().getEngine().getPluginManager();
		auto* renderer = (Renderer*)plugin_manager.getPlugin("renderer");
		if(m_texture_view.isValid()) {
			renderer->destroy(m_texture_view);
		}
	}


	struct TextureTileJob
	{
		TextureTileJob(FileSystem& filesystem, IAllocator& allocator) 
			: m_allocator(allocator) 
			, m_filesystem(filesystem)
		{}

		void execute() {
			IAllocator& allocator = m_allocator;

			u32 hash = crc32(m_in_path);
			StaticString<MAX_PATH_LENGTH> out_path(".lumix/asset_tiles/", hash, ".dds");
			Array<u8> resized_data(allocator);
			resized_data.resize(AssetBrowser::TILE_SIZE * AssetBrowser::TILE_SIZE * 4);
			if (PathUtils::hasExtension(m_in_path, "dds")) {
				OS::InputFile file;
				if (!file.open(m_in_path)) {
					m_filesystem.copyFile("models/editor/tile_texture.dds", out_path);
					logError("Editor") << "Failed to load " << m_in_path;
					return;
				}
				Array<u8> data(allocator);
				data.resize((int)file.size());
				file.read(&data[0], data.size());
				file.close();

				nvtt::Surface surface;
				if (!surface.load(m_in_path, data.begin(), data.byte_size())) {
					logError("Editor") << "Failed to load " << m_in_path;
					m_filesystem.copyFile("models/editor/tile_texture.dds", out_path);
					return;
				}

				Array<u8> decompressed(allocator);
				const int w = surface.width();
				const int h = surface.height();
				decompressed.resize(4 * w * h);
				for (int c = 0; c < 4; ++c) {
					const float* data = surface.channel(c);
					for (int j = 0; j < h; ++j) {
						for (int i = 0; i < w; ++i) {
							const u8 p = u8(data[j * w + i] * 255.f + 0.5f);
							decompressed[(j * w + i) * 4 + c] = p;
						}
					}
				}

				stbir_resize_uint8((const u8*)decompressed.begin(),
					w,
					h,
					0,
					&resized_data[0],
					AssetBrowser::TILE_SIZE,
					AssetBrowser::TILE_SIZE,
					0,
					4);
			}
			else
			{
				int image_comp;
				int w, h;
				auto data = stbi_load(m_in_path, &w, &h, &image_comp, 4);
				if (!data)
				{
					logError("Editor") << "Failed to load " << m_in_path;
					m_filesystem.copyFile("models/editor/tile_texture.dds", out_path);
					return;
				}
				stbir_resize_uint8(data,
					w,
					h,
					0,
					&resized_data[0],
					AssetBrowser::TILE_SIZE,
					AssetBrowser::TILE_SIZE,
					0,
					4);
				stbi_image_free(data);
			}

			if (!saveAsDDS(m_out_path, &resized_data[0], AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE)) {
				logError("Editor") << "Failed to save " << m_out_path;
			}
		}

		static void execute(void* data) {
			PROFILE_FUNCTION();
			TextureTileJob* that = (TextureTileJob*)data;
			that->execute();
			LUMIX_DELETE(that->m_allocator, that);
		}

		IAllocator& m_allocator;
		FileSystem& m_filesystem;
		StaticString<MAX_PATH_LENGTH> m_in_path; 
		StaticString<MAX_PATH_LENGTH> m_out_path; 
	};


	bool createTile(const char* in_path, const char* out_path, ResourceType type) override
	{
		if (type == Texture::TYPE) {
			IAllocator& allocator = m_app.getWorldEditor().getAllocator();
			FileSystem& fs = m_app.getWorldEditor().getEngine().getFileSystem();
			auto* job = LUMIX_NEW(allocator, TextureTileJob)(fs, allocator);
			job->m_in_path = fs.getBasePath();
			job->m_in_path << in_path;
			job->m_out_path = fs.getBasePath();
			job->m_out_path << out_path;
			JobSystem::SignalHandle signal = JobSystem::INVALID_HANDLE;
			JobSystem::runEx(job, &TextureTileJob::execute, &signal, m_tile_signal, JobSystem::getWorkersCount() - 1);
			m_tile_signal = signal;
			return true;
		}
		return false;
	}


	bool compileImage(const Array<u8>& src_data, OutputMemoryStream& dst, const Meta& meta)
	{
		PROFILE_FUNCTION();
		int w, h, comps;
		stbi_uc* data = stbi_load_from_memory(src_data.begin(), src_data.byte_size(), &w, &h, &comps, 4);
		if (!data) return false;

		dst.write("dds", 3);
		u32 flags = meta.srgb ? (u32)Texture::Flags::SRGB : 0;
		flags |= meta.wrap_mode_u == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_U : 0;
		flags |= meta.wrap_mode_v == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_V : 0;
		flags |= meta.wrap_mode_w == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_W : 0;
		flags |= meta.filter == Meta::Filter::POINT ? (u32)Texture::Flags::POINT : 0;
		dst.write(&flags, sizeof(flags));

		nvtt::Context context;
		
		const bool has_alpha = comps == 4;
		nvtt::InputOptions input;
		input.setMipmapGeneration(true);
		input.setAlphaMode(has_alpha ? nvtt::AlphaMode_Transparency : nvtt::AlphaMode_None);
		input.setNormalMap(meta.is_normalmap);
		input.setTextureLayout(nvtt::TextureType_2D, w, h);
		input.setMipmapData(data, w, h);
		stbi_image_free(data);
		
		nvtt::OutputOptions output;
		output.setSrgbFlag(meta.srgb);
		struct : nvtt::OutputHandler {
			bool writeData(const void * data, int size) override { return dst->write(data, size); }
			void beginImage(int size, int width, int height, int depth, int face, int miplevel) override {}
			void endImage() override {}

			OutputMemoryStream* dst;
		} output_handler;
		output_handler.dst = &dst;
		output.setOutputHandler(&output_handler);

		nvtt::CompressionOptions compression;
		compression.setFormat(meta.is_normalmap ? nvtt::Format_DXT5n : (has_alpha ? nvtt::Format_DXT5 :  nvtt::Format_DXT1));
		compression.setQuality(nvtt::Quality_Normal);

		if (!context.process(input, compression, output)) {
			return false;
		}
		return true;
	}


	Meta getMeta(const Path& path) const
	{
		Meta meta;
		m_app.getAssetCompiler().getMeta(path, [&meta](lua_State* L){
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "srgb", &meta.srgb);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "normalmap", &meta.is_normalmap);
			char tmp[32];
			if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "filter", Span(tmp))) {
				if (stricmp(tmp, "point") == 0) {
					meta.filter = Meta::Filter::POINT;
				}
				else {
					meta.filter = Meta::Filter::LINEAR;
				}
			}
			if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "wrap_mode_u", Span(tmp))) {
				meta.wrap_mode_u = stricmp(tmp, "repeat") == 0 ? Meta::WrapMode::REPEAT : Meta::WrapMode::CLAMP;
			}
			if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "wrap_mode_v", Span(tmp))) {
				meta.wrap_mode_v = stricmp(tmp, "repeat") == 0 ? Meta::WrapMode::REPEAT : Meta::WrapMode::CLAMP;
			}
			if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "wrap_mode_w", Span(tmp))) {
				meta.wrap_mode_w = stricmp(tmp, "repeat") == 0 ? Meta::WrapMode::REPEAT : Meta::WrapMode::CLAMP;
			}
		});
		return meta;
	}


	bool compile(const Path& src) override
	{
		char ext[4] = {};
		PathUtils::getExtension(Span(ext), Span(src.c_str(), src.length()));

		FileSystem& fs = m_app.getWorldEditor().getEngine().getFileSystem();
		Array<u8> src_data(m_app.getWorldEditor().getAllocator());
		if (!fs.getContentSync(src, Ref(src_data))) return false;
		
		OutputMemoryStream out(m_app.getWorldEditor().getAllocator());
		Meta meta = getMeta(src);
		if (equalStrings(ext, "dds") || equalStrings(ext, "raw") || equalStrings(ext, "tga")) {
			out.write(ext, sizeof(ext) - 1);
			u32 flags = meta.srgb ? (u32)Texture::Flags::SRGB : 0;
			flags |= meta.wrap_mode_u == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_U : 0;
			flags |= meta.wrap_mode_v == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_V : 0;
			flags |= meta.wrap_mode_w == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_W : 0;
			flags |= meta.filter == Meta::Filter::POINT ? (u32)Texture::Flags::POINT : 0;
			out.write(flags);
			out.write(src_data.begin(), src_data.byte_size());
		}
		else if(equalStrings(ext, "jpg")) {
			compileImage(src_data, out, meta);
		}
		else if(equalStrings(ext, "png")) {
			compileImage(src_data, out, meta);
		}
		else {
			ASSERT(false);
		}

		return m_app.getAssetCompiler().writeCompiledResource(src.c_str(), Span((u8*)out.getData(), (i32)out.getPos()));
	}

	const char* toString(Meta::Filter filter) {
		switch (filter) {
			case Meta::Filter::POINT: return "point";
			case Meta::Filter::LINEAR: return "linear";
			default: ASSERT(false); return "linear";
		}
	}

	const char* toString(Meta::WrapMode wrap) {
		switch (wrap) {
			case Meta::WrapMode::CLAMP: return "clamp";
			case Meta::WrapMode::REPEAT: return "repeat";
			default: ASSERT(false); return "repeat";
		}
	}

	void onGUI(Span<Resource*> resources) override
	{
		if(resources.length() > 1) return;

		auto* texture = static_cast<Texture*>(resources[0]);

		ImGui::LabelText("Size", "%dx%d", texture->width, texture->height);
		ImGui::LabelText("Mips", "%d", texture->mips);
		if (texture->bytes_per_pixel > 0) ImGui::LabelText("BPP", "%d", texture->bytes_per_pixel);
		if (texture->handle.isValid()) {
			ImVec2 texture_size(200, 200);
			if (texture->width > texture->height) {
				texture_size.y = texture_size.x * texture->height / texture->width;
			}
			else {
				texture_size.x = texture_size.y * texture->width / texture->height;
			}

			if (texture != m_texture) {
				m_texture = texture;
				PluginManager& plugin_manager = m_app.getWorldEditor().getEngine().getPluginManager();
				auto* renderer = (Renderer*)plugin_manager.getPlugin("renderer");
				renderer->runInRenderThread(this, [](Renderer& r, void* ptr){
					TexturePlugin* p = (TexturePlugin*)ptr;
					if (!p->m_texture_view.isValid()) {
						p->m_texture_view = ffr::allocTextureHandle();
					}
					ffr::createTextureView(p->m_texture_view, p->m_texture->handle);
				});
			}

			ImGui::Image((void*)(uintptr_t)m_texture_view.value, texture_size);

			if (ImGui::Button("Open")) m_app.getAssetBrowser().openInExternalEditor(texture);
		}

		if (ImGui::CollapsingHeader("Import")) {
			AssetCompiler& compiler = m_app.getAssetCompiler();
			
			if(texture->getPath().getHash() != m_meta_res) {
				m_meta = getMeta(texture->getPath());
				m_meta_res = texture->getPath().getHash();
			}
			
			ImGui::Checkbox("SRGB", &m_meta.srgb);
			ImGui::Checkbox("Is normalmap", &m_meta.is_normalmap);
			ImGui::Combo("U Wrap mode", (int*)&m_meta.wrap_mode_u, "Repeat\0Clamp\0");
			ImGui::Combo("V Wrap mode", (int*)&m_meta.wrap_mode_v, "Repeat\0Clamp\0");
			ImGui::Combo("W Wrap mode", (int*)&m_meta.wrap_mode_w, "Repeat\0Clamp\0");
			ImGui::Combo("Filter", (int*)&m_meta.wrap_mode_w, "Trilinear\0Bilinear\0Point\0");

			if (ImGui::Button("Apply")) {
				const StaticString<512> src("srgb = ", m_meta.srgb ? "true" : "false"
					, "\nnormalmap = ", m_meta.is_normalmap ? "true" : "false"
					, "\nwrap_mode_u = \"", toString(m_meta.wrap_mode_u), "\""
					, "\nwrap_mode_v = \"", toString(m_meta.wrap_mode_v), "\""
					, "\nwrap_mode_w = \"", toString(m_meta.wrap_mode_w), "\""
					, "\nfilter = \"", toString(m_meta.filter), "\""
				);
				compiler.updateMeta(texture->getPath(), src);
				if (compiler.compile(texture->getPath())) {
					texture->getResourceManager().reload(*texture);
				}
			}
		}
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Texture"; }
	ResourceType getResourceType() const override { return Texture::TYPE; }


	StudioApp& m_app;
	Texture* m_texture;
	ffr::TextureHandle m_texture_view = ffr::INVALID_TEXTURE;
	JobSystem::SignalHandle m_tile_signal = JobSystem::INVALID_HANDLE;
	Meta m_meta;
	u32 m_meta_res = 0;
};


struct ShaderPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	explicit ShaderPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("shd", Shader::TYPE);
	}


	void findIncludes(const char* path)
	{
		lua_State* L = luaL_newstate();
		luaL_openlibs(L);

		OS::InputFile file;
		if (!file.open(path[0] == '/' ? path + 1 : path)) return;
		
		IAllocator& allocator = m_app.getWorldEditor().getAllocator();
		Array<u8> content(allocator);
		content.resize((int)file.size());
		file.read(content.begin(), content.byte_size());
		file.close();

		struct Context {
			const char* path;
			ShaderPlugin* plugin;
			u8* content;
			u32 content_len;
			int idx;
		} ctx = { path, this, content.begin(), content.byte_size(), 0 };

		lua_pushlightuserdata(L, &ctx);
		lua_setfield(L, LUA_GLOBALSINDEX, "this");

		auto include = [](lua_State* L) -> int {
			lua_getfield(L, LUA_GLOBALSINDEX, "this");
			Context* that = LuaWrapper::toType<Context*>(L, -1);
			lua_pop(L, 1);
			const char* path = LuaWrapper::checkArg<const char*>(L, 1);
			that->plugin->m_app.getAssetCompiler().registerDependency(Path(that->path), Path(path));
			return 0;
		};

		lua_pushcclosure(L, include, 0);
		lua_setfield(L, LUA_GLOBALSINDEX, "include");

		static const char* preface = 
			"local new_g = setmetatable({include = include}, {__index = function() return function() end end })\n"
			"setfenv(1, new_g)\n";

		auto reader = [](lua_State* L, void* data, size_t* size) -> const char* {
			Context* ctx = (Context*)data;
			++ctx->idx;
			switch(ctx->idx) {
				case 1: 
					*size = stringLength(preface);
					return preface;
				case 2: 
					*size = ctx->content_len;
					return (const char*)ctx->content;
				default:
					*size = 0;
					return nullptr;
			}
		};

		if (lua_load(L, reader, &ctx, path) != 0) {
			logError("Engine") << path << ": " << lua_tostring(L, -1);
			lua_pop(L, 2);
			lua_close(L);
			return;
		}

		if (lua_pcall(L, 0, 0, -2) != 0) {
			logError("Engine") << lua_tostring(L, -1);
			lua_pop(L, 2);
			lua_close(L);
			return;
		}
		lua_pop(L, 1);
		lua_close(L);
	}

	void addSubresources(AssetCompiler& compiler, const char* path) override {
		compiler.addResource(Shader::TYPE, path);
		findIncludes(path);
	}

	bool compile(const Path& src) override
	{
		return m_app.getAssetCompiler().copyCompile(src);
	}


	void onGUI(Span<Resource*> resources) override
	{
		if(resources.length() > 1) return;

		auto* shader = static_cast<Shader*>(resources[0]);
		char basename[MAX_PATH_LENGTH];
		PathUtils::getBasename(Span(basename), shader->getPath().c_str());
		if (ImGui::Button("Open in external editor"))
		{
			m_app.getAssetBrowser().openInExternalEditor(shader->getPath().c_str());
		}

		if (shader->m_texture_slot_count > 0 &&
			ImGui::CollapsingHeader(
				"Texture slots", nullptr, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
		{
			for (u32 i = 0; i < shader->m_texture_slot_count; ++i)
			{
				auto& slot = shader->m_texture_slots[i];
				ImGui::Text("%s", slot.name);
			}
		}
		if (!shader->m_uniforms.empty() &&
			ImGui::CollapsingHeader("Uniforms", nullptr, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
		{
			ImGui::Columns(2);
			ImGui::Text("name");
			ImGui::NextColumn();
			ImGui::Text("type");
			ImGui::NextColumn();
			ImGui::Separator();
			for (int i = 0; i < shader->m_uniforms.size(); ++i)
			{
				auto& uniform = shader->m_uniforms[i];
				ImGui::Text("%s", uniform.name);
				ImGui::NextColumn();
				switch (uniform.type)
				{
					case Shader::Uniform::COLOR: ImGui::Text("Color"); break;
					case Shader::Uniform::FLOAT: ImGui::Text("Float"); break;
					case Shader::Uniform::INT: ImGui::Text("Int"); break;
					case Shader::Uniform::MATRIX4: ImGui::Text("Matrix 4x4"); break;
					case Shader::Uniform::VEC4: ImGui::Text("Vector4"); break;
					case Shader::Uniform::VEC3: ImGui::Text("Vector3"); break;
					case Shader::Uniform::VEC2: ImGui::Text("Vector2"); break;
					default: ASSERT(false); break;
				}
				ImGui::NextColumn();
			}
			ImGui::Columns(1);
		}
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Shader"; }
	ResourceType getResourceType() const override { return Shader::TYPE; }


	StudioApp& m_app;
};


struct EnvironmentProbePlugin final : public PropertyGrid::IPlugin
{
	static constexpr int TEXTURE_SIZE = 1024;


	explicit EnvironmentProbePlugin(StudioApp& app)
		: m_app(app)
		, m_data(app.getWorldEditor().getAllocator())
		, m_probes(app.getWorldEditor().getAllocator())
	{
		WorldEditor& world_editor = app.getWorldEditor();
		Engine& engine = world_editor.getEngine();
		PluginManager& plugin_manager = engine.getPluginManager();
		Renderer* renderer = static_cast<Renderer*>(plugin_manager.getPlugin("renderer"));
		IAllocator& allocator = world_editor.getAllocator();
		PipelineResource* pres = engine.getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
		m_pipeline = Pipeline::create(*renderer, pres, "PROBE", allocator);

		m_cl_context = nullptr; // cmft::clLoad() > 0 ? cmft::clInit() : nullptr;
	}


	~EnvironmentProbePlugin()
	{
		if (m_cl_context)
		{
			cmft::clDestroy(m_cl_context);
			cmft::clUnload();
		}
		Pipeline::destroy(m_pipeline);
	}


	bool saveCubemap(u64 probe_guid, const u8* data, int texture_size, const char* postfix)
	{
		ASSERT(data);
		const char* base_path = m_app.getWorldEditor().getEngine().getFileSystem().getBasePath();
		StaticString<MAX_PATH_LENGTH> path(base_path, "universes/", m_app.getWorldEditor().getUniverse()->getName());
		if (!OS::makePath(path) && !OS::dirExists(path)) {
			logError("Editor") << "Failed to create " << path;
		}
		path << "/probes_tmp/";
		if (!OS::makePath(path) && !OS::dirExists(path)) {
			logError("Editor") << "Failed to create " << path;
		}
		path << probe_guid << postfix << ".dds";
		OS::OutputFile file;
		if (!file.open(path)) {
			logError("Editor") << "Failed to create " << path;
			return false;
		}

		nvtt::Context context;
		
		nvtt::InputOptions input;
		input.setMipmapGeneration(true);
		input.setAlphaMode(nvtt::AlphaMode_None);
		input.setNormalMap(false);
		input.setTextureLayout(nvtt::TextureType_Cube, texture_size, texture_size);
		for (int i = 0; i < 6; ++i) {
			const int step = texture_size * texture_size * 4;
			input.setMipmapData(data + step * i, texture_size, texture_size, 1, i);
		}
		
		nvtt::OutputOptions output;
		output.setSrgbFlag(false);
		struct : nvtt::OutputHandler {
			bool writeData(const void * data, int size) override { return dst->write(data, size); }
			void beginImage(int size, int width, int height, int depth, int face, int miplevel) override {}
			void endImage() override {}

			OS::OutputFile* dst;
		} output_handler;
		output_handler.dst = &file;
		output.setOutputHandler(&output_handler);

		nvtt::CompressionOptions compression;
		compression.setFormat(nvtt::Format_DXT1);
		compression.setQuality(nvtt::Quality_Fastest);

		if (!context.process(input, compression, output)) {
			file.close();
			return false;
		}
		file.close();
		return true;
	}


	void flipY(u32* data, int texture_size)
	{
		for (int y = 0; y < texture_size / 2; ++y)
		{
			for (int x = 0; x < texture_size; ++x)
			{
				u32 t = data[x + y * texture_size];
				data[x + y * texture_size] = data[x + (texture_size - y - 1) * texture_size];
				data[x + (texture_size - y - 1) * texture_size] = t;
			}
		}
	}


	void flipX(u32* data, int texture_size)
	{
		for (int y = 0; y < texture_size; ++y)
		{
			u32* tmp = (u32*)&data[y * texture_size];
			for (int x = 0; x < texture_size / 2; ++x)
			{
				u32 t = tmp[x];
				tmp[x] = tmp[texture_size - x - 1];
				tmp[texture_size - x - 1] = t;
			}
		}
	}


	void generateCubemaps(bool bounce) {
		ASSERT(!m_in_progress);
		ASSERT(m_probes.empty());

		// TODO block user interaction
		Universe* universe = m_app.getWorldEditor().getUniverse();
		if (universe->getName()[0] == '\0') {
			logError("Editor") << "Universe must be saved before environment probe can be generated.";
			return;
		}
		
		m_pipeline->define("PROBE_BOUNCE", bounce);

		auto* scene = static_cast<RenderScene*>(universe->getScene(ENVIRONMENT_PROBE_TYPE));
		const Span<EntityRef> probes = scene->getAllEnvironmentProbes();
		m_probes.reserve(probes.length());
		for (const EntityRef& p : probes) {
			m_probes.push(p);
		}
	}


	void generateCubemap(EntityRef entity)
	{
		ASSERT(!m_in_progress);

		Universe* universe = m_app.getWorldEditor().getUniverse();
		if (universe->getName()[0] == '\0') {
			logError("Editor") << "Universe must be saved before environment probe can be generated.";
			return;
		}

		m_in_progress = true;
		MT::memoryBarrier();

		WorldEditor& world_editor = m_app.getWorldEditor();
		Engine& engine = world_editor.getEngine();
		auto& plugin_manager = engine.getPluginManager();
		IAllocator& allocator = engine.getAllocator();
		auto* scene = static_cast<RenderScene*>(universe->getScene(CAMERA_TYPE));
		const EnvironmentProbe& probe = scene->getEnvironmentProbe(entity);

		const DVec3 probe_position = universe->getPosition(entity);
		Viewport viewport;
		viewport.is_ortho = false;
		viewport.fov = degreesToRadians(90.f);
		viewport.near = 0.1f;
		viewport.far = probe.radius;
		viewport.w = TEXTURE_SIZE;
		viewport.h = TEXTURE_SIZE;

		m_pipeline->setScene(scene);
		m_pipeline->setViewport(viewport);

		Renderer* renderer = static_cast<Renderer*>(plugin_manager.getPlugin("renderer"));
		Vec3 dirs[] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
		Vec3 ups[] = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, 1, 0}};
		Vec3 ups_opengl[] = { { 0, -1, 0 },{ 0, -1, 0 },{ 0, 0, 1 },{ 0, 0, -1 },{ 0, -1, 0 },{ 0, -1, 0 } };

		m_data.resize(6 * TEXTURE_SIZE * TEXTURE_SIZE * 4);

		const bool ndc_bottom_left = ffr::isOriginBottomLeft();
		for (int i = 0; i < 6; ++i) {
			Vec3 side = crossProduct(ndc_bottom_left ? ups_opengl[i] : ups[i], dirs[i]);
			Matrix mtx = Matrix::IDENTITY;
			mtx.setZVector(dirs[i]);
			mtx.setYVector(ndc_bottom_left ? ups_opengl[i] : ups[i]);
			mtx.setXVector(side);
			viewport.pos = probe_position;
			viewport.rot = mtx.getRotation();
			m_pipeline->setViewport(viewport);
			m_pipeline->render(false);

			const ffr::TextureHandle res = m_pipeline->getOutput();
			ASSERT(res.isValid());
			renderer->getTextureImage(res, TEXTURE_SIZE * TEXTURE_SIZE * 4, &m_data[i * TEXTURE_SIZE * TEXTURE_SIZE * 4]);
		}

		renderer->frame();
		renderer->frame();

		if (!ndc_bottom_left) {
			for (int i = 0; i < 6; ++i) {
				u32* tmp = (u32*)&m_data[i * TEXTURE_SIZE * TEXTURE_SIZE * 4];
				if (i == 2 || i == 3) {
					flipY(tmp, TEXTURE_SIZE);
				}
				else {
					flipX(tmp, TEXTURE_SIZE);
				}
			}
		}

		m_irradiance_size = 32;
		m_radiance_size = 128;
		m_reflection_size = TEXTURE_SIZE;

		if (probe.flags.isSet(EnvironmentProbe::OVERRIDE_GLOBAL_SIZE)) {
			m_irradiance_size = probe.irradiance_size;
			m_radiance_size = probe.radiance_size;
			// TODO the size of m_data should be m_reflection_size^2 instead of TEXTURE_SIZE^2
			m_reflection_size = probe.reflection_size;
		}
		m_save_reflection = probe.flags.isSet(EnvironmentProbe::REFLECTION);
		m_probe_guid = probe.guid;

		JobSystem::run(this, [](void* ptr) {
			((EnvironmentProbePlugin*)ptr)->processData();
		}, &m_signal);
	}


	void update() override
	{
		if (m_reload_probes && !m_in_progress) {
			m_reload_probes = false;
			Universe* universe = m_app.getWorldEditor().getUniverse();
			const char* universe_name = universe->getName();
			auto* scene = static_cast<RenderScene*>(universe->getScene(ENVIRONMENT_PROBE_TYPE));
			const Span<EntityRef> probes = scene->getAllEnvironmentProbes();
			
			auto move = [universe_name](u64 guid, const char* postfix){
				const StaticString<MAX_PATH_LENGTH> tmp_path("universes/", universe_name, "/probes_tmp/", guid, postfix, ".dds");
				const StaticString<MAX_PATH_LENGTH> path("universes/", universe_name, "/probes/", guid, postfix, ".dds");
				if (!OS::fileExists(tmp_path)) return;
				if (!OS::moveFile(tmp_path, path)) {
					logError("Editor") << "Failed to move file " << tmp_path;
				}
			};

			for (EntityRef e : probes) {
				const EnvironmentProbe& probe = scene->getEnvironmentProbe(e);
				move(probe.guid, "");
				move(probe.guid, "_radiance");
				move(probe.guid, "_irradiance");
			}
		}
		else if (!m_probes.empty() && !m_in_progress) {
			const EntityRef e = m_probes.back();
			m_probes.pop();
			generateCubemap(e);

			if (m_probes.empty()) m_reload_probes = true;
		}
	}


	void processData()
	{
		cmft::Image image;	
		cmft::Image irradiance;

		cmft::imageCreate(image, TEXTURE_SIZE, TEXTURE_SIZE, 0x303030ff, 1, 6, cmft::TextureFormat::RGBA8);
		cmft::imageFromRgba32f(image, cmft::TextureFormat::RGBA8);
		copyMemory(image.m_data, &m_data[0], m_data.size());
		cmft::imageToRgba32f(image);

		{
			PROFILE_BLOCK("radiance filter");
			cmft::imageRadianceFilter(
				image
				, 128
				, cmft::LightingModel::BlinnBrdf
				, false
				, 1
				, 10
				, 1
				, cmft::EdgeFixup::None
				, m_cl_context ? 0 : MT::getCPUsCount()
				, m_cl_context
			);
		}

		{
			PROFILE_BLOCK("irradiance filter");
			cmft::imageIrradianceFilterSh(irradiance, 32, image);
		}

		cmft::imageFromRgba32f(image, cmft::TextureFormat::RGBA8);
		cmft::imageFromRgba32f(irradiance, cmft::TextureFormat::RGBA8);

		for (int i = 3; i < m_data.size(); i += 4) m_data[i] = 0xff; 
		saveCubemap(m_probe_guid, (u8*)irradiance.m_data, m_irradiance_size, "_irradiance");
		saveCubemap(m_probe_guid, (u8*)image.m_data, m_radiance_size, "_radiance");
		if (m_save_reflection) {
			saveCubemap(m_probe_guid, &m_data[0], m_reflection_size, "");
		}

		MT::memoryBarrier();
		m_in_progress = false;
	}


	void onGUI(PropertyGrid& grid, ComponentUID cmp) override
	{
		if (cmp.type != ENVIRONMENT_PROBE_TYPE) return;

		const EntityRef e = (EntityRef)cmp.entity;
		auto* scene = static_cast<RenderScene*>(cmp.scene);
		auto* texture = scene->getEnvironmentProbeTexture(e);
		if (texture)
		{
			ImGui::LabelText("Reflection path", "%s", texture->getPath().c_str());
			if (ImGui::Button("View reflection")) m_app.getAssetBrowser().selectResource(texture->getPath(), true, false);
		}
		texture = scene->getEnvironmentProbeIrradiance(e);
		if (texture)
		{
			ImGui::LabelText("Irradiance path", "%s", texture->getPath().c_str());
			if (ImGui::Button("View irradiance")) m_app.getAssetBrowser().selectResource(texture->getPath(), true, false);
		}
		texture = scene->getEnvironmentProbeRadiance(e);
		if (texture)
		{
			ImGui::LabelText("Radiance path", "%s", texture->getPath().c_str());
			if (ImGui::Button("View radiance")) m_app.getAssetBrowser().selectResource(texture->getPath(), true, false);
		}
		if (m_in_progress) ImGui::Text("Generating...");
		else {
			if (ImGui::Button("Generate")) generateCubemaps(false);
			if (ImGui::Button("Add bounce")) generateCubemaps(true);
		}
	}


	StudioApp& m_app;
	Pipeline* m_pipeline;
	
	Array<u8> m_data;
	bool m_in_progress = false;
	bool m_reload_probes = false;
	int m_irradiance_size;
	int m_radiance_size;
	int m_reflection_size;
	bool m_save_reflection;
	u64 m_probe_guid;
	Array<EntityRef> m_probes;

	JobSystem::SignalHandle m_signal = JobSystem::INVALID_HANDLE;


	cmft::ClContext* m_cl_context;
};


struct TerrainPlugin final : public PropertyGrid::IPlugin
{
	explicit TerrainPlugin(StudioApp& app)
		: m_app(app)
	{
		WorldEditor& editor = app.getWorldEditor();
		m_terrain_editor = LUMIX_NEW(editor.getAllocator(), TerrainEditor)(editor, app);
	}


	~TerrainPlugin() { LUMIX_DELETE(m_app.getWorldEditor().getAllocator(), m_terrain_editor); }


	void onGUI(PropertyGrid& grid, ComponentUID cmp) override
	{
		if (cmp.type != TERRAIN_TYPE) return;

		m_terrain_editor->setComponent(cmp);
		m_terrain_editor->onGUI();
	}


	StudioApp& m_app;
	TerrainEditor* m_terrain_editor;
};


struct RenderInterfaceImpl final : public RenderInterface
{
	RenderInterfaceImpl(WorldEditor& editor, Pipeline& pipeline, Renderer& renderer)
		: m_pipeline(pipeline)
		, m_editor(editor)
		, m_models(editor.getAllocator())
		, m_textures(editor.getAllocator())
		, m_renderer(renderer)
	{
		m_model_index = 0;
		auto& rm = m_editor.getEngine().getResourceManager();
		
		Path shader_path("pipelines/debug_shape.shd");
		m_shader = rm.load<Shader>(shader_path);
		
		Path font_path("editor/fonts/OpenSans-Regular.ttf");
		m_font_res = rm.load<FontResource>(font_path);
		m_font = m_font_res->addRef(16);

		editor.universeCreated().bind<RenderInterfaceImpl, &RenderInterfaceImpl::onUniverseCreated>(this);
		editor.universeDestroyed().bind<RenderInterfaceImpl, &RenderInterfaceImpl::onUniverseDestroyed>(this);
	}


	~RenderInterfaceImpl()
	{
		m_shader->getResourceManager().unload(*m_shader);
		m_font_res->getResourceManager().unload(*m_font_res);

		m_editor.universeCreated().unbind<RenderInterfaceImpl, &RenderInterfaceImpl::onUniverseCreated>(this);
		m_editor.universeDestroyed().unbind<RenderInterfaceImpl, &RenderInterfaceImpl::onUniverseDestroyed>(this);
	}


	void addText2D(float x, float y, u32 color, const char* text) override
	{
		if (m_font) m_pipeline.getDraw2D().addText(*m_font, {x, y}, Color(color), text);
	}


	void addRect2D(const Vec2& a, const Vec2& b, u32 color) override
	{
		m_pipeline.getDraw2D().addRect(a, b, *(Color*)&color, 1);
	}


	void addRectFilled2D(const Vec2& a, const Vec2& b, u32 color) override
	{
		m_pipeline.getDraw2D().addRectFilled(a, b, *(Color*)&color);
	}


	DVec3 getClosestVertex(Universe* universe, EntityRef entity, const DVec3& wpos) override
	{
		const Transform tr = universe->getTransform(entity);
		const Vec3 lpos = tr.rot.conjugated() * (wpos - tr.pos).toFloat();
		auto* scene = (RenderScene*)universe->getScene(MODEL_INSTANCE_TYPE);
		if (!universe->hasComponent(entity, MODEL_INSTANCE_TYPE)) return wpos;

		Model* model = scene->getModelInstanceModel(entity);

		float min_dist_squared = FLT_MAX;
		Vec3 closest_vertex = lpos;
		auto processVertex = [&](const Vec3& vertex) {
			float dist_squared = (vertex - lpos).squaredLength();
			if (dist_squared < min_dist_squared)
			{
				min_dist_squared = dist_squared;
				closest_vertex = vertex;
			}
		};

		for (int i = 0, c = model->getMeshCount(); i < c; ++i)
		{
			Mesh& mesh = model->getMesh(i);

			if (mesh.areIndices16())
			{
				const u16* indices = (const u16*)&mesh.indices[0];
				for (int i = 0, c = mesh.indices.size() >> 1; i < c; ++i)
				{
					Vec3 vertex = mesh.vertices[indices[i]];
					processVertex(vertex);
				}
			}
			else
			{
				const u32* indices = (const u32*)&mesh.indices[0];
				for (int i = 0, c = mesh.indices.size() >> 2; i < c; ++i)
				{
					Vec3 vertex = mesh.vertices[indices[i]];
					processVertex(vertex);
				}
			}
		}
		return tr.pos + tr.rot * closest_vertex;
	}


	ImFont* addFont(const char* filename, int size) override
	{
		ImGuiIO& io = ImGui::GetIO();
		ImFont* font = io.Fonts->AddFontFromFileTTF(filename, (float)size);

		Engine& engine = m_editor.getEngine();
		unsigned char* pixels;
		int width, height;

		ImGuiFreeType::BuildFontAtlas(ImGui::GetIO().Fonts);
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		Material* material = engine.getResourceManager().load<Material>(Path("pipelines/imgui/imgui.mat"));

		Texture* old_texture = material->getTexture(0);
		PluginManager& plugin_manager = engine.getPluginManager();
		Texture* texture = LUMIX_NEW(engine.getAllocator(), Texture)(
			Path("font"), *engine.getResourceManager().get(Texture::TYPE), m_renderer, engine.getAllocator());

		texture->create(width, height, ffr::TextureFormat::RGBA8, pixels, width * height * 4);
		material->setTexture(0, texture);
		if (old_texture)
		{
			old_texture->destroy();
			LUMIX_DELETE(engine.getAllocator(), old_texture);
		}

		return font;
	}


	ModelHandle loadModel(Path& path) override
	{
		m_models.insert(m_model_index, m_editor.getEngine().getResourceManager().load<Model>(path));
		++m_model_index;
		return m_model_index - 1;
	}


	bool saveTexture(Engine& engine, const char* path_cstr, const void* pixels, int w, int h, bool upper_left_origin) override
	{
		Path path(path_cstr);
		OS::OutputFile file;
		if (!file.open(path_cstr)) return false;

		if (!Texture::saveTGA(&file, w, h, 4, (const u8*)pixels, upper_left_origin, path, engine.getAllocator())) {
			file.close();
			return false;
		}

		file.close();
		return true;
	}


	ImTextureID createTexture(const char* name, const void* pixels, int w, int h) override
	{
		Engine& engine = m_editor.getEngine();
		auto& rm = engine.getResourceManager();
		auto& allocator = m_editor.getAllocator();

		Texture* texture = LUMIX_NEW(allocator, Texture)(Path(name), *rm.get(Texture::TYPE), m_renderer, allocator);
		texture->create(w, h, ffr::TextureFormat::RGBA8, pixels, w * h * 4);
		m_textures.insert(&texture->handle, texture);
		return (ImTextureID)(uintptr_t)texture->handle.value;
	}


	void destroyTexture(ImTextureID handle) override
	{
		auto& allocator = m_editor.getAllocator();
		auto iter = m_textures.find(handle);
		if (iter == m_textures.end()) return;
		auto* texture = iter.value();
		m_textures.erase(iter);
		texture->destroy();
		LUMIX_DELETE(allocator, texture);
	}


	bool isValid(ImTextureID texture) override
	{
		return texture && ((ffr::TextureHandle*)texture)->isValid();
	}


	ImTextureID loadTexture(const Path& path) override
	{
		auto& rm = m_editor.getEngine().getResourceManager();
		auto* texture = rm.load<Texture>(path);
		m_textures.insert(&texture->handle, texture);
		return &texture->handle;
	}


	void unloadTexture(ImTextureID handle) override
	{
		auto iter = m_textures.find(handle);
		if (iter == m_textures.end()) return;
		auto* texture = iter.value();
		texture->getResourceManager().unload(*texture);
		m_textures.erase(iter);
	}


	void addDebugCross(const DVec3& pos, float size, u32 color) override
	{
		m_render_scene->addDebugCross(pos, size, color);
	}


	WorldEditor::RayHit castRay(const DVec3& origin, const Vec3& dir, EntityPtr ignored) override
	{
		const RayCastModelHit hit = m_render_scene->castRay(origin, dir, ignored);

		return {hit.is_hit, hit.t, hit.entity, hit.origin + hit.dir * hit.t};
	}


	void addDebugLine(const DVec3& from, const DVec3& to, u32 color) override
	{
		m_render_scene->addDebugLine(from, to, color);
	}


	void addDebugCube(const DVec3& minimum, const DVec3& maximum, u32 color) override
	{
		m_render_scene->addDebugCube(minimum, maximum, color);
	}


	void addDebugCube(const DVec3& pos, const Vec3& dir, const Vec3& up, const Vec3& right, u32 color) override
	{
		m_render_scene->addDebugCube(pos, dir, right, up, color);
	}


	AABB getEntityAABB(Universe& universe, EntityRef entity, const DVec3& base) override
	{
		AABB aabb;

		if (universe.hasComponent(entity, MODEL_INSTANCE_TYPE)) {
			Model* model = m_render_scene->getModelInstanceModel(entity);
			if (!model) return aabb;

			aabb = model->getAABB();
			aabb.transform(universe.getRelativeMatrix(entity, base));

			return aabb;
		}

		Vec3 pos = (universe.getPosition(entity) - base).toFloat();
		aabb.set(pos, pos);

		return aabb;
	}


	void unloadModel(ModelHandle handle) override
	{
		auto* model = m_models[handle];
		model->getResourceManager().unload(*model);
		m_models.erase(handle);
	}


	Vec2 getCameraScreenSize(EntityRef entity) override { return m_render_scene->getCameraScreenSize(entity); }


	float getCameraOrthoSize(EntityRef entity) override { return m_render_scene->getCamera(entity).ortho_size; }


	bool isCameraOrtho(EntityRef entity) override { return m_render_scene->getCamera(entity).is_ortho; }


	float getCameraFOV(EntityRef entity) override { return m_render_scene->getCamera(entity).fov; }


	float castRay(ModelHandle model, const Vec3& origin, const Vec3& dir, const Pose* pose) override
	{
		RayCastModelHit hit = m_models[model]->castRay(origin, dir, pose);
		return hit.is_hit ? hit.t : -1;
	}


	void renderModel(ModelHandle model, const Matrix& mtx) override
	{
		if (!m_pipeline.isReady() || !m_models[model]->isReady()) return;

		m_pipeline.renderModel(*m_models[model], mtx);
	}


	void onUniverseCreated()
	{
		m_render_scene = static_cast<RenderScene*>(m_editor.getUniverse()->getScene(MODEL_INSTANCE_TYPE));
	}


	void onUniverseDestroyed() { m_render_scene = nullptr; }


	Vec3 getModelCenter(EntityRef entity) override
	{
		if (!m_render_scene->getUniverse().hasComponent(entity, MODEL_INSTANCE_TYPE)) return Vec3::ZERO;
		Model* model = m_render_scene->getModelInstanceModel(entity);
		if (!model) return Vec3(0, 0, 0);
		return (model->getAABB().min + model->getAABB().max) * 0.5f;
	}


	Path getModelInstancePath(EntityRef entity) override { return m_render_scene->getModelInstancePath(entity); }


	ShiftedFrustum getFrustum(EntityRef camera, const Vec2& viewport_min, const Vec2& viewport_max) override
	{
		return m_render_scene->getCameraFrustum(camera, viewport_min, viewport_max);
	}


	void getRenderables(Array<EntityRef>& entities, const ShiftedFrustum& frustum) override
	{
		for (int i = 0; i < (int)RenderableTypes::COUNT; ++i) {
			CullResult* renderables = m_render_scene->getRenderables(frustum, (RenderableTypes)i);
			while (renderables) {
				for (u32 i = 0; i < renderables->header.count; ++i) {
					entities.push(renderables->entities[i]);
				}
				renderables = renderables->header.next;
			}
		}
	}


	WorldEditor& m_editor;
	Shader* m_shader;
	FontResource* m_font_res;
	Font* m_font;
	RenderScene* m_render_scene;
	Renderer& m_renderer;
	Pipeline& m_pipeline;
	HashMap<int, Model*> m_models;
	HashMap<void*, Texture*> m_textures;
	int m_model_index;
};


struct EditorUIRenderPlugin final : public StudioApp::GUIPlugin
{
	struct RenderCommand : Renderer::RenderJob
	{
		struct CmdList
		{
			CmdList(IAllocator& allocator) : commands(allocator) {}

			Renderer::MemRef idx_buffer;
			Renderer::MemRef vtx_buffer;

			Array<ImDrawCmd> commands;
		};


		RenderCommand(IAllocator& allocator)
			: allocator(allocator)
			, command_lists(allocator)
		{
		}

		void setup() override
		{
			PROFILE_FUNCTION();
			PluginManager& plugin_manager = plugin->m_engine.getPluginManager();
			renderer = (Renderer*)plugin_manager.getPlugin("renderer");

			const ImDrawData* draw_data = ImGui::GetDrawData();
			if (!draw_data) return;

			command_lists.reserve(draw_data->CmdListsCount);
			for (int i = 0; i < draw_data->CmdListsCount; ++i) {
				ImDrawList* cmd_list = draw_data->CmdLists[i];
				CmdList& out_cmd_list = command_lists.emplace(allocator);

				out_cmd_list.idx_buffer = renderer->copy(&cmd_list->IdxBuffer[0], cmd_list->IdxBuffer.size() * sizeof(cmd_list->IdxBuffer[0]));
				out_cmd_list.vtx_buffer = renderer->copy(&cmd_list->VtxBuffer[0], cmd_list->VtxBuffer.size() * sizeof(cmd_list->VtxBuffer[0]));
			
				out_cmd_list.commands.resize(cmd_list->CmdBuffer.size());
				for (int i = 0, c = out_cmd_list.commands.size(); i < c; ++i) {
					out_cmd_list.commands[i] = cmd_list->CmdBuffer[i];
				}
			}
			
			init_render = !plugin->m_program.isValid();
			
			if (init_render) {
				plugin->m_index_buffer = ffr::allocBufferHandle();
				plugin->m_vertex_buffer = ffr::allocBufferHandle();
				plugin->m_uniform_buffer = ffr::allocBufferHandle();
				plugin->m_program = ffr::allocProgramHandle();
			}
		
			width = plugin->m_width;
			height = plugin->m_height;
			default_texture = &plugin->m_texture;
			vb = plugin->m_vertex_buffer;
			ib = plugin->m_index_buffer;
			ub = plugin->m_uniform_buffer;
			program = plugin->m_program;
		}


		void draw(const CmdList& cmd_list)
		{
			const u32 num_indices = cmd_list.idx_buffer.size / sizeof(ImDrawIdx);
			const u32 num_vertices = cmd_list.vtx_buffer.size / sizeof(ImDrawVert);

			const bool use_big_buffers = num_vertices * sizeof(ImDrawVert) > 256 * 1024 ||
				num_indices * sizeof(ImDrawIdx) > 256 * 1024;
			ffr::useProgram(program);

			ffr::BufferHandle big_ib, big_vb;
			if (use_big_buffers) {
				big_vb = ffr::allocBufferHandle();
				big_ib = ffr::allocBufferHandle();
				ffr::createBuffer(big_vb, (u32)ffr::BufferFlags::IMMUTABLE, num_vertices * sizeof(ImDrawVert), cmd_list.vtx_buffer.data);
				ffr::createBuffer(big_ib, (u32)ffr::BufferFlags::IMMUTABLE, num_indices * sizeof(ImDrawIdx), cmd_list.idx_buffer.data);
				ffr::bindVertexBuffer(0, big_vb, 0, sizeof(ImDrawVert));
				ffr::bindIndexBuffer(big_ib);
			}
			else {
				ffr::update(ib, cmd_list.idx_buffer.data, num_indices * sizeof(ImDrawIdx));
				ffr::update(vb, cmd_list.vtx_buffer.data, num_vertices * sizeof(ImDrawVert));

				ffr::bindVertexBuffer(0, vb, 0, sizeof(ImDrawVert));
				ffr::bindIndexBuffer(ib);
			}
			renderer->free(cmd_list.vtx_buffer);
			renderer->free(cmd_list.idx_buffer);
			u32 elem_offset = 0;
			const ImDrawCmd* pcmd_begin = cmd_list.commands.begin();
			const ImDrawCmd* pcmd_end = cmd_list.commands.end();
			// TODO enable only when dc.textures[0].value != m_scene_view.getTextureHandle().value);
			const u64 blend_state = ffr::getBlendStateBits(ffr::BlendFactors::SRC_ALPHA, ffr::BlendFactors::ONE_MINUS_SRC_ALPHA, ffr::BlendFactors::SRC_ALPHA, ffr::BlendFactors::ONE_MINUS_SRC_ALPHA);
			ffr::setState((u64)ffr::StateFlags::SCISSOR_TEST | blend_state);
			for (const ImDrawCmd* pcmd = pcmd_begin; pcmd != pcmd_end; pcmd++)
			{
				ASSERT(!pcmd->UserCallback);
				if (0 == pcmd->ElemCount) continue;

				/*			auto material = m_material;
							u64 render_states = material->getRenderStates();
							if (&m_scene_view.getTextureHandle() == &texture_id || &m_game_view.getTextureHandle() ==
				   &texture_id)
							{
								render_states &= ~BGFX_STATE_BLEND_MASK;
							}
				*/

				ffr::TextureHandle tex = pcmd->TextureId ? ffr::TextureHandle{(u32)(intptr_t)pcmd->TextureId} : *default_texture;
				if (!tex.isValid()) tex = *default_texture;
				ffr::bindTextures(&tex, 0, 1);

				const u32 h = u32(minimum(pcmd->ClipRect.w, 65535.0f) - maximum(pcmd->ClipRect.y, 0.0f));

				if(ffr::isOriginBottomLeft()) {
					ffr::scissor(u32(maximum(pcmd->ClipRect.x, 0.0f)),
						height - u32(maximum(pcmd->ClipRect.y, 0.0f)) - h,
						u32(minimum(pcmd->ClipRect.z, 65535.0f) - maximum(pcmd->ClipRect.x, 0.0f)),
						u32(minimum(pcmd->ClipRect.w, 65535.0f) - maximum(pcmd->ClipRect.y, 0.0f)));
				}
				else {
					ffr::scissor(u32(maximum(pcmd->ClipRect.x, 0.0f)),
						u32(maximum(pcmd->ClipRect.y, 0.0f)),
						u32(minimum(pcmd->ClipRect.z, 65535.0f) - maximum(pcmd->ClipRect.x, 0.0f)),
						u32(minimum(pcmd->ClipRect.w, 65535.0f) - maximum(pcmd->ClipRect.y, 0.0f)));
				}

				ffr::drawElements(elem_offset * sizeof(u32), pcmd->ElemCount, ffr::PrimitiveType::TRIANGLES, ffr::DataType::U32);
		
				elem_offset += pcmd->ElemCount;
			}
			if(use_big_buffers) {
				ffr::destroy(big_ib);
				ffr::destroy(big_vb);
			}
			else {
				ib_offset += num_indices;
				vb_offset += num_vertices;
			}
		}


		void execute() override
		{
			PROFILE_FUNCTION();

			if(init_render) {
				ffr::createBuffer(ub, (u32)ffr::BufferFlags::UNIFORM_BUFFER, 256, nullptr);
				ffr::createBuffer(ib, 0, 256 * 1024, nullptr);
				ffr::createBuffer(vb, 0, 256 * 1024, nullptr);
				ffr::ShaderType types[] = {ffr::ShaderType::VERTEX, ffr::ShaderType::FRAGMENT};
				ffr::VertexDecl decl;
				decl.addAttribute(0, 0, 2, ffr::AttributeType::FLOAT, 0);
				decl.addAttribute(1, 8, 2, ffr::AttributeType::FLOAT, 0);
				decl.addAttribute(2, 16, 4, ffr::AttributeType::U8, ffr::Attribute::NORMALIZED);

				const char* vs =
					R"#(
					layout(location = 0) in vec2 a_pos;
					layout(location = 1) in vec2 a_uv;
					layout(location = 2) in vec4 a_color;
					layout(location = 0) out vec4 v_color;
					layout(location = 1) out vec2 v_uv;
					layout (std140, binding = 4) uniform IMGUIState {
						mat2x3 u_canvas_mtx;
					};
					void main() {
						v_color = a_color;
						v_uv = a_uv;
						vec2 p = vec3(a_pos, 1) * u_canvas_mtx;
						gl_Position = vec4(p.xy, 0, 1);
					})#";
				const char* fs = 
					R"#(
					layout(location = 0) in vec4 v_color;
					layout(location = 1) in vec2 v_uv;
					layout(location = 0) out vec4 o_color;
					uniform sampler2D u_texture;
					void main() {
						vec4 tc = textureLod(u_texture, v_uv, 0);
						o_color.rgb = pow(tc.rgb, vec3(1/2.2)) * v_color.rgb;
						o_color.a = v_color.a * tc.a;
					})#";
				const char* srcs[] = {vs, fs};
				ffr::createProgram(program, decl, srcs, types, 2, nullptr, 0, "imgui shader");
			}

			ffr::pushDebugGroup("imgui");
			ffr::setFramebuffer(nullptr, 0, 0);

			const float clear_color[] = {0.2f, 0.2f, 0.2f, 1.f};
			ffr::clear((u32)ffr::ClearFlags::COLOR | (u32)ffr::ClearFlags::DEPTH, clear_color, 1.0);

			ffr::viewport(0, 0, width, height);
			const bool is_dx = ffr::getBackend() == ffr::Backend::DX11;
			const Vec4 canvas_mtx[] = {
				Vec4(2.f / width, 0, -1, 0),
				Vec4(0, -2.f / height, 1, 0)
			};
			ffr::update(ub, canvas_mtx, sizeof(canvas_mtx));
			ffr::bindUniformBuffer(4, ub, 0, sizeof(canvas_mtx));

			vb_offset = 0;
			ib_offset = 0;
			for(const CmdList& cmd_list : command_lists) {
				draw(cmd_list);
			}

			ffr::popDebugGroup();
		}
		
		Renderer* renderer;
		const ffr::TextureHandle* default_texture;
		u32 width, height;
		Array<CmdList> command_lists;
		u32 ib_offset;
		u32 vb_offset;
		IAllocator& allocator;
		ffr::BufferHandle ib;
		ffr::BufferHandle vb;
		ffr::BufferHandle ub;
		ffr::ProgramHandle program;
		bool init_render;
		EditorUIRenderPlugin* plugin;
	};


	EditorUIRenderPlugin(StudioApp& app, SceneView& scene_view, GameView& game_view)
		: m_app(app)
		, m_scene_view(scene_view)
		, m_game_view(game_view)
		, m_width(-1)
		, m_height(-1)
		, m_engine(app.getWorldEditor().getEngine())
		, m_index_buffer(ffr::INVALID_BUFFER)
		, m_vertex_buffer(ffr::INVALID_BUFFER)
		, m_uniform_buffer(ffr::INVALID_BUFFER)
		, m_program(ffr::INVALID_PROGRAM)
	{
		WorldEditor& editor = app.getWorldEditor();

		PluginManager& plugin_manager = m_engine.getPluginManager();
		Renderer* renderer = (Renderer*)plugin_manager.getPlugin("renderer");

		const OS::Point size = OS::getWindowClientSize(m_app.getWindow());
		m_width = size.x;
		m_height = size.y;
		renderer->resize(m_width, m_height);

		unsigned char* pixels;
		int width, height;
		ImGuiFreeType::BuildFontAtlas(ImGui::GetIO().Fonts);
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

		const Renderer::MemRef mem = renderer->copy(pixels, width * height * 4);
		m_texture = renderer->createTexture(width, height, 1, ffr::TextureFormat::RGBA8, 0, mem, "editor_font_atlas");

		IAllocator& allocator = editor.getAllocator();
		RenderInterface* render_interface =
			LUMIX_NEW(allocator, RenderInterfaceImpl)(editor, *scene_view.getPipeline(), *renderer);
		editor.setRenderInterface(render_interface);
	}


	~EditorUIRenderPlugin()
	{
		PluginManager& plugin_manager = m_engine.getPluginManager();
		Renderer* renderer = (Renderer*)plugin_manager.getPlugin("renderer");
		WorldEditor& editor = m_app.getWorldEditor();
		shutdownImGui();
	}


	void onWindowGUI() override {}


	const char* getName() const override { return "editor_ui_render"; }


	void shutdownImGui()
	{
		ImGui::DestroyContext();
	}


	void guiEndFrame() override
	{
		const ImDrawData* draw_data = ImGui::GetDrawData();

		const OS::Point size = OS::getWindowClientSize(m_app.getWindow());
		if (size.x != m_width || size.y != m_height) {
			m_width = size.x;
			m_height = size.y;
			auto& plugin_manager = m_app.getWorldEditor().getEngine().getPluginManager();
			auto* renderer = static_cast<Renderer*>(plugin_manager.getPlugin("renderer"));
			if (renderer) renderer->resize(m_width, m_height);
		}

		Renderer* renderer = static_cast<Renderer*>(m_engine.getPluginManager().getPlugin("renderer"));
		RenderCommand* cmd = LUMIX_NEW(renderer->getAllocator(), RenderCommand)(renderer->getAllocator());
		cmd->plugin = this;
		
		renderer->queue(cmd, 0);
		renderer->frame();
	}


	int m_width;
	int m_height;
	StudioApp& m_app;
	Engine& m_engine;
	SceneView& m_scene_view;
	GameView& m_game_view;
	ffr::TextureHandle m_texture;
	ffr::BufferHandle m_index_buffer;
	ffr::BufferHandle m_vertex_buffer;
	ffr::BufferHandle m_uniform_buffer;
	ffr::ProgramHandle m_program;
};


struct GizmoPlugin final : public WorldEditor::Plugin
{
	void showPointLightGizmo(ComponentUID light)
	{
		RenderScene* scene = static_cast<RenderScene*>(light.scene);
		Universe& universe = scene->getUniverse();

		const float range = scene->getLightRange((EntityRef)light.entity);

		const DVec3 pos = universe.getPosition((EntityRef)light.entity);
		scene->addDebugSphere(pos, range, 0xff0000ff);
	}


	static Vec3 minCoords(const Vec3& a, const Vec3& b)
	{
		return Vec3(minimum(a.x, b.x), minimum(a.y, b.y), minimum(a.z, b.z));
	}


	static Vec3 maxCoords(const Vec3& a, const Vec3& b)
	{
		return Vec3(maximum(a.x, b.x), maximum(a.y, b.y), maximum(a.z, b.z));
	}


	void showGlobalLightGizmo(ComponentUID light)
	{
		RenderScene* scene = static_cast<RenderScene*>(light.scene);
		const Universe& universe = scene->getUniverse();
		const EntityRef entity = (EntityRef)light.entity;
		const DVec3 pos = universe.getPosition(entity);

		const Vec3 dir = universe.getRotation(entity).rotate(Vec3(0, 0, 1));
		const Vec3 right = universe.getRotation(entity).rotate(Vec3(1, 0, 0));
		const Vec3 up = universe.getRotation(entity).rotate(Vec3(0, 1, 0));

		scene->addDebugLine(pos, pos + dir, 0xff0000ff);
		scene->addDebugLine(pos + right, pos + dir + right, 0xff0000ff);
		scene->addDebugLine(pos - right, pos + dir - right, 0xff0000ff);
		scene->addDebugLine(pos + up, pos + dir + up, 0xff0000ff);
		scene->addDebugLine(pos - up, pos + dir - up, 0xff0000ff);

		scene->addDebugLine(pos + right + up, pos + dir + right + up, 0xff0000ff);
		scene->addDebugLine(pos + right - up, pos + dir + right - up, 0xff0000ff);
		scene->addDebugLine(pos - right - up, pos + dir - right - up, 0xff0000ff);
		scene->addDebugLine(pos - right + up, pos + dir - right + up, 0xff0000ff);

		scene->addDebugSphere(pos - dir, 0.1f, 0xff0000ff);
	}


	void showDecalGizmo(ComponentUID cmp)
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		Universe& universe = scene->getUniverse();
		Vec3 half_extents = scene->getDecalHalfExtents((EntityRef)cmp.entity);
		const RigidTransform tr = universe.getTransform((EntityRef)cmp.entity).getRigidPart();
		const Vec3 x = tr.rot * Vec3(1, 0, 0) * half_extents.x;
		const Vec3 y = tr.rot * Vec3(0, 1, 0) * half_extents.y;
		const Vec3 z = tr.rot * Vec3(0, 0, 1) * half_extents.z;
		scene->addDebugCube(tr.pos, x, y, z, 0xff0000ff);
	}


	void showCameraGizmo(ComponentUID cmp)
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);

		scene->addDebugFrustum(scene->getCameraFrustum((EntityRef)cmp.entity), 0xffff0000);
	}


	bool showGizmo(ComponentUID cmp) override
	{
		if (cmp.type == CAMERA_TYPE)
		{
			showCameraGizmo(cmp);
			return true;
		}
		if (cmp.type == DECAL_TYPE)
		{
			showDecalGizmo(cmp);
			return true;
		}
		if (cmp.type == POINT_LIGHT_TYPE)
		{
			showPointLightGizmo(cmp);
			return true;
		}
		if (cmp.type == ENVIRONMENT_TYPE)
		{
			showGlobalLightGizmo(cmp);
			return true;
		}
		return false;
	}
};


struct AddTerrainComponentPlugin final : public StudioApp::IAddComponentPlugin
{
	explicit AddTerrainComponentPlugin(StudioApp& _app)
		: app(_app)
	{
	}


	bool createHeightmap(const char* material_path, int size)
	{
		char normalized_material_path[MAX_PATH_LENGTH];
		PathUtils::normalize(material_path, Span(normalized_material_path));

		PathUtils::FileInfo info(normalized_material_path);
		StaticString<MAX_PATH_LENGTH> hm_path(info.m_dir, info.m_basename, ".raw");
		OS::OutputFile file;
		if (!file.open(hm_path))
		{
			logError("Editor") << "Failed to create heightmap " << hm_path;
			return false;
		}
		else
		{
			u16 tmp = 0xffff >> 1;
			for (int i = 0; i < size * size; ++i)
			{
				file.write(&tmp, sizeof(tmp));
			}
			file.close();
		}

		if (!file.open(normalized_material_path))
		{
			logError("Editor") << "Failed to create material " << normalized_material_path;
			OS::deleteFile(hm_path);
			return false;
		}

		file << R"#(
			shader "pipelines/terrain.shd"
			texture ")#";
		file << info.m_basename;
		file << R"#(.raw"
			texture "/textures/common/white.tga"
			texture ""
			texture ""
		)#";

		file.close();
		return true;
	}


	void onGUI(bool create_entity, bool from_filter) override
	{
		WorldEditor& editor = app.getWorldEditor();

		ImGui::SetNextWindowSize(ImVec2(300, 300));
		if (!ImGui::BeginMenu("Terrain")) return;
		char buf[MAX_PATH_LENGTH];
		AssetBrowser& asset_browser = app.getAssetBrowser();
		bool new_created = false;
		if (ImGui::BeginMenu("New"))
		{
			static int size = 1024;
			ImGui::InputInt("Size", &size);
			if (ImGui::Button("Create"))
			{
				char save_filename[MAX_PATH_LENGTH];
				if (OS::getSaveFilename(Span(save_filename), "Material\0*.mat\0", "mat")) {
					editor.makeRelative(Span(buf), save_filename);
					new_created = createHeightmap(buf, size);
				}
			}
			ImGui::EndMenu();
		}
		bool create_empty = ImGui::Selectable("Empty", false);
		static u32 selected_res_hash = 0;
		if (asset_browser.resourceList(Span(buf), Ref(selected_res_hash), Material::TYPE, 0, false) || create_empty || new_created)
		{
			if (create_entity)
			{
				EntityRef entity = editor.addEntity();
				editor.selectEntities(&entity, 1, false);
			}
			if (editor.getSelectedEntities().empty()) return;
			EntityRef entity = editor.getSelectedEntities()[0];

			if (!editor.getUniverse()->hasComponent(entity, TERRAIN_TYPE))
			{
				editor.addComponent(TERRAIN_TYPE);
			}

			if (!create_empty)
			{
				auto* prop = Reflection::getProperty(TERRAIN_TYPE, "Material");
				editor.setProperty(TERRAIN_TYPE, -1, *prop, &entity, 1, buf, stringLength(buf) + 1);
			}

			ImGui::CloseCurrentPopup();
		}
		ImGui::EndMenu();
	}


	const char* getLabel() const override { return "Render / Terrain"; }


	StudioApp& app;
};


struct StudioAppPlugin : StudioApp::IPlugin
{
	StudioAppPlugin(StudioApp& app)
		: m_app(app)
	{
	}


	const char* getName() const override { return "renderer"; }


	void init() override
	{
		IAllocator& allocator = m_app.getWorldEditor().getAllocator();

		m_app.registerComponent("camera", "Render / Camera");
		m_app.registerComponent("environment", "Render / Environment");

		m_app.registerComponentWithResource(
			"model_instance", "Render / Mesh", Model::TYPE, *Reflection::getProperty(MODEL_INSTANCE_TYPE, "Source"));
		m_app.registerComponentWithResource("particle_emitter",
			"Render / Particle emitter",
			ParticleEmitterResource::TYPE,
			*Reflection::getProperty(PARTICLE_EMITTER_TYPE, "Resource"));
		m_app.registerComponent("point_light", "Render / Point light");
		m_app.registerComponent("decal", "Render / Decal");
		m_app.registerComponent("bone_attachment", "Render / Bone attachment");
		m_app.registerComponent("environment_probe", "Render / Environment probe");
		m_app.registerComponentWithResource(
			"text_mesh", "Render / Text 3D", FontResource::TYPE, *Reflection::getProperty(TEXT_MESH_TYPE, "Font"));

		m_add_terrain_plugin = LUMIX_NEW(allocator, AddTerrainComponentPlugin)(m_app);
		m_app.registerComponent("terrain", *m_add_terrain_plugin);

		AssetCompiler& asset_compiler = m_app.getAssetCompiler();

		m_shader_plugin = LUMIX_NEW(allocator, ShaderPlugin)(m_app);
		const char* shader_exts[] = {"shd", nullptr};
		asset_compiler.addPlugin(*m_shader_plugin, shader_exts);

		m_texture_plugin = LUMIX_NEW(allocator, TexturePlugin)(m_app);
		const char* texture_exts[] = { "png", "jpg", "dds", "tga", "raw", nullptr};
		asset_compiler.addPlugin(*m_texture_plugin, texture_exts);

		m_pipeline_plugin = LUMIX_NEW(allocator, PipelinePlugin)(m_app);
		const char* pipeline_exts[] = {"pln", nullptr};
		asset_compiler.addPlugin(*m_pipeline_plugin, pipeline_exts);

		m_particle_emitter_plugin = LUMIX_NEW(allocator, ParticleEmitterPlugin)(m_app);
		const char* particle_emitter_exts[] = {"par", nullptr};
		asset_compiler.addPlugin(*m_particle_emitter_plugin, particle_emitter_exts);

		m_material_plugin = LUMIX_NEW(allocator, MaterialPlugin)(m_app);
		const char* material_exts[] = {"mat", nullptr};
		asset_compiler.addPlugin(*m_material_plugin, material_exts);

		m_model_plugin = LUMIX_NEW(allocator, ModelPlugin)(m_app);
		const char* model_exts[] = {"fbx", nullptr};
		asset_compiler.addPlugin(*m_model_plugin, model_exts);

		m_font_plugin = LUMIX_NEW(allocator, FontPlugin)(m_app);
		const char* fonts_exts[] = {"ttf", nullptr};
		asset_compiler.addPlugin(*m_font_plugin, fonts_exts);
		
		AssetBrowser& asset_browser = m_app.getAssetBrowser();
		asset_browser.addPlugin(*m_model_plugin);
		asset_browser.addPlugin(*m_particle_emitter_plugin);
		asset_browser.addPlugin(*m_material_plugin);
		asset_browser.addPlugin(*m_font_plugin);
		asset_browser.addPlugin(*m_shader_plugin);
		asset_browser.addPlugin(*m_texture_plugin);

		m_env_probe_plugin = LUMIX_NEW(allocator, EnvironmentProbePlugin)(m_app);
		m_terrain_plugin = LUMIX_NEW(allocator, TerrainPlugin)(m_app);
		PropertyGrid& property_grid = m_app.getPropertyGrid();
		property_grid.addPlugin(*m_env_probe_plugin);
		property_grid.addPlugin(*m_terrain_plugin);

		m_scene_view = LUMIX_NEW(allocator, SceneView)(m_app);
		m_game_view = LUMIX_NEW(allocator, GameView)(m_app);
		m_editor_ui_render_plugin = LUMIX_NEW(allocator, EditorUIRenderPlugin)(m_app, *m_scene_view, *m_game_view);
		m_app.addPlugin(*m_scene_view);
		m_app.addPlugin(*m_game_view);
		m_app.addPlugin(*m_editor_ui_render_plugin);

		m_gizmo_plugin = LUMIX_NEW(allocator, GizmoPlugin)();
		m_app.getWorldEditor().addPlugin(*m_gizmo_plugin);
	}


	~StudioAppPlugin()
	{
		IAllocator& allocator = m_app.getWorldEditor().getAllocator();

		AssetBrowser& asset_browser = m_app.getAssetBrowser();
		asset_browser.removePlugin(*m_model_plugin);
		asset_browser.removePlugin(*m_particle_emitter_plugin);
		asset_browser.removePlugin(*m_material_plugin);
		asset_browser.removePlugin(*m_font_plugin);
		asset_browser.removePlugin(*m_texture_plugin);
		asset_browser.removePlugin(*m_shader_plugin);

		AssetCompiler& asset_compiler = m_app.getAssetCompiler();
		asset_compiler.removePlugin(*m_font_plugin);
		asset_compiler.removePlugin(*m_shader_plugin);
		asset_compiler.removePlugin(*m_texture_plugin);
		asset_compiler.removePlugin(*m_model_plugin);
		asset_compiler.removePlugin(*m_material_plugin);
		asset_compiler.removePlugin(*m_particle_emitter_plugin);
		asset_compiler.removePlugin(*m_pipeline_plugin);

		LUMIX_DELETE(allocator, m_model_plugin);
		LUMIX_DELETE(allocator, m_material_plugin);
		LUMIX_DELETE(allocator, m_particle_emitter_plugin);
		LUMIX_DELETE(allocator, m_pipeline_plugin);
		LUMIX_DELETE(allocator, m_font_plugin);
		LUMIX_DELETE(allocator, m_texture_plugin);
		LUMIX_DELETE(allocator, m_shader_plugin);

		PropertyGrid& property_grid = m_app.getPropertyGrid();

		property_grid.removePlugin(*m_env_probe_plugin);
		property_grid.removePlugin(*m_terrain_plugin);

		LUMIX_DELETE(allocator, m_env_probe_plugin);
		LUMIX_DELETE(allocator, m_terrain_plugin);

		m_app.removePlugin(*m_scene_view);
		m_app.removePlugin(*m_game_view);
		m_app.removePlugin(*m_editor_ui_render_plugin);

		LUMIX_DELETE(allocator, m_scene_view);
		LUMIX_DELETE(allocator, m_game_view);
		LUMIX_DELETE(allocator, m_editor_ui_render_plugin);

		m_app.getWorldEditor().removePlugin(*m_gizmo_plugin);
		LUMIX_DELETE(allocator, m_gizmo_plugin);
	}


	StudioApp& m_app;
	AddTerrainComponentPlugin* m_add_terrain_plugin;
	ModelPlugin* m_model_plugin;
	MaterialPlugin* m_material_plugin;
	ParticleEmitterPlugin* m_particle_emitter_plugin;
	PipelinePlugin* m_pipeline_plugin;
	FontPlugin* m_font_plugin;
	TexturePlugin* m_texture_plugin;
	ShaderPlugin* m_shader_plugin;
	EnvironmentProbePlugin* m_env_probe_plugin;
	TerrainPlugin* m_terrain_plugin;
	SceneView* m_scene_view;
	GameView* m_game_view;
	EditorUIRenderPlugin* m_editor_ui_render_plugin;
	GizmoPlugin* m_gizmo_plugin;
};


LUMIX_STUDIO_ENTRY(renderer)
{
	auto& allocator = app.getWorldEditor().getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}
