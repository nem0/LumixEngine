#include "../ffr/ffr.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/property_grid.h"
#include "editor/render_interface.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/file_system.h"
#include "engine/fs/os_file.h"
#include "engine/job_system.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/lumix.h"
#include "engine/mt/atomic.h"
#include "engine/mt/thread.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/prefab.h"
#include "engine/profiler.h"
#include "engine/queue.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/universe/universe.h"
#include "engine/viewport.h"
#include "fbx_importer.h"
#include "game_view.h"
#include "renderer/draw2d.h"
#include "renderer/ffr/ffr.h"
#include "renderer/font_manager.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/particle_system.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include "scene_view.h"
#include "shader_editor.h"
#include "stb/stb_image.h"
#include "stb/stb_image_resize.h"
#include "terrain_editor.h"
#include <cmath>
#include <cmft/clcontext.h>
#include <cmft/cubemapfilter.h>
#include <crnlib.h>
#include <cstddef>


using namespace Lumix;


static const ComponentType PARTICLE_EMITTER_TYPE = Reflection::getComponentType("particle_emitter");
static const ComponentType TERRAIN_TYPE = Reflection::getComponentType("terrain");
static const ComponentType CAMERA_TYPE = Reflection::getComponentType("camera");
static const ComponentType DECAL_TYPE = Reflection::getComponentType("decal");
static const ComponentType POINT_LIGHT_TYPE = Reflection::getComponentType("point_light");
static const ComponentType GLOBAL_LIGHT_TYPE = Reflection::getComponentType("global_light");
static const ComponentType MODEL_INSTANCE_TYPE = Reflection::getComponentType("model_instance");
static const ComponentType TEXT_MESH_TYPE = Reflection::getComponentType("text_mesh");
static const ComponentType ENVIRONMENT_PROBE_TYPE = Reflection::getComponentType("environment_probe");


static bool saveAsDDS(const char* path, const u8* image_data, int image_width, int image_height)
{
	ASSERT(image_data);

	crn_uint32 size;
	crn_comp_params comp_params;
	comp_params.m_file_type = cCRNFileTypeDDS;
	comp_params.m_quality_level = cCRNMaxQualityLevel;
	comp_params.m_dxt_quality = cCRNDXTQualityNormal;
	comp_params.m_dxt_compressor_type = cCRNDXTCompressorCRN;
	comp_params.m_pProgress_func = nullptr;
	comp_params.m_pProgress_func_data = nullptr;
	comp_params.m_num_helper_threads = 3;
	comp_params.m_width = image_width;
	comp_params.m_height = image_height;
	comp_params.m_format = cCRNFmtDXT5;
	comp_params.m_pImages[0][0] = (u32*)image_data;
	crn_mipmap_params mipmap_params;
	mipmap_params.m_mode = cCRNMipModeGenerateMips;

	void* data = crn_compress(comp_params, mipmap_params, size);
	if (!data) return false;

	FS::OsFile file;
	if (file.open(path, FS::Mode::CREATE_AND_WRITE))
	{
		file.write(data, size);
		file.close();
		crn_free_block(data);
		return true;
	}

	crn_free_block(data);
	return false;
}



struct FontPlugin final : public AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	FontPlugin(StudioApp& app) : app(app) 
	{ 
		app.getAssetCompiler().registerExtension("ttf", FontResource::TYPE); 
	}
	
	bool compile(const Path& src) override
	{
		const char* dst_dir = app.getAssetCompiler().getCompiledDir();
		const u32 hash = crc32(src.c_str());

		const StaticString<MAX_PATH_LENGTH> dst(dst_dir, hash, ".res");

		OS::copyFile(src.c_str(), dst);
		return true;
	}

	void onGUI(Resource* resource) override {}
	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Font"; }

	ResourceType getResourceType() const override { return FontResource::TYPE; }

	StudioApp& app;
};


struct PipelinePlugin final : AssetCompiler::IPlugin
{
	explicit PipelinePlugin(StudioApp& app)
		: m_app(app)
	{}

	bool compile(const Path& src) override
	{
		const char* dst_dir = m_app.getAssetCompiler().getCompiledDir();
		const u32 hash = crc32(src.c_str());

		const StaticString<MAX_PATH_LENGTH> dst(dst_dir, hash, ".res");

		OS::copyFile(src.c_str(), dst);
		return true;
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
		const char* dst_dir = m_app.getAssetCompiler().getCompiledDir();
		const u32 hash = crc32(src.c_str());

		const StaticString<MAX_PATH_LENGTH> dst(dst_dir, hash, ".res");

		OS::copyFile(src.c_str(), dst);
		return true;
	}
	
	
	void onGUI(Resource* resource) override
	{
	}


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


	bool compile(const Path& src) override
	{
		const char* dst_dir = m_app.getAssetCompiler().getCompiledDir();
		const u32 hash = crc32(src.c_str());

		const StaticString<MAX_PATH_LENGTH> dst(dst_dir, hash, ".res");

		OS::copyFile(src.c_str(), dst);
		return true;
	}


	void saveMaterial(Material* material)
	{
		if (FS::IFile* file = m_app.getAssetBrowser().beginSaveResource(*material)) {
			bool success = true;
			if (!material->save(*file))
			{
				success = false;
				g_log_error.log("Editor") << "Could not save file " << material->getPath().c_str();
			}
			m_app.getAssetBrowser().endSaveResource(*material, *file, success);
		}
	}


	void onGUI(Resource* resource) override
	{
		Material* material = static_cast<Material*>(resource);
		if (!material->isReady()) return;

		if (ImGui::Button("Save")) saveMaterial(material);
		ImGui::SameLine();
		if (ImGui::Button("Open in external editor")) m_app.getAssetBrowser().openInExternalEditor(material);

		auto* plugin = m_app.getWorldEditor().getEngine().getPluginManager().getPlugin("renderer");
		auto* renderer = static_cast<Renderer*>(plugin);

		int alpha_cutout_define = renderer->getShaderDefineIdx("ALPHA_CUTOUT");

		bool b = material->isBackfaceCulling();
		if (ImGui::Checkbox("Backface culling", &b)) material->enableBackfaceCulling(b);

		/*if (material->hasDefine(alpha_cutout_define))
		{
			b = material->isDefined(alpha_cutout_define);
			if (ImGui::Checkbox("Is alpha cutout", &b)) material->setDefine(alpha_cutout_define, b);
			if (b)
			{
				float tmp = material->getAlphaRef();
				if (ImGui::DragFloat("Alpha reference value", &tmp, 0.01f, 0.0f, 1.0f))
				{
					material->setAlphaRef(tmp);
				}
			}
		}*/
		// TODO

		Vec4 color = material->getColor();
		if (ImGui::ColorEdit4("Color", &color.x))
		{
			material->setColor(color);
		}

		float roughness = material->getRoughness();
		if (ImGui::DragFloat("Roughness", &roughness, 0.01f, 0.0f, 1.0f))
		{
			material->setRoughness(roughness);
		}

		float metallic = material->getMetallic();
		if (ImGui::DragFloat("Metallic", &metallic, 0.01f, 0.0f, 1.0f))
		{
			material->setMetallic(metallic);
		}

		float emission = material->getEmission();
		if (ImGui::DragFloat("Emission", &emission, 0.01f, 0.0f))
		{
			material->setEmission(emission);
		}

		char buf[MAX_PATH_LENGTH];
		copyString(buf, material->getShader() ? material->getShader()->getPath().c_str() : "");
		if (m_app.getAssetBrowser().resourceInput("Shader", "shader", buf, sizeof(buf), Shader::TYPE))
		{
			material->setShader(Path(buf));
		}

		const char* current_layer_name = renderer->getLayerName(material->getLayer());
		if (ImGui::BeginCombo("Layer", current_layer_name)) {
			for(u8 i = 0, c = renderer->getLayersCount(); i < c; ++i) {
				const char* name = renderer->getLayerName(i);
				if(ImGui::Selectable(name)) {
					material->setLayer(i);
				}
			}
			ImGui::EndCombo();
		}

		for (int i = 0; i < material->getShader()->m_texture_slot_count; ++i)
		{
			auto& slot = material->getShader()->m_texture_slots[i];
			Texture* texture = material->getTexture(i);
			copyString(buf, texture ? texture->getPath().c_str() : "");
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
			bool is_node_open = ImGui::TreeNodeEx((const void*)(intptr_t)i,
				ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_Framed,
				"%s",
				"");
			ImGui::PopStyleColor(4);
			ImGui::SameLine();
			if (m_app.getAssetBrowser().resourceInput(
					slot.name, StaticString<30>("", (u64)&slot), buf, sizeof(buf), Texture::TYPE))
			{
				material->setTexturePath(i, Path(buf));
			}
			if (!texture && is_node_open)
			{
				ImGui::TreePop();
				continue;
			}

			if (is_node_open)
			{
				ImGui::Image((void*)(uintptr_t)texture->handle.value, ImVec2(96, 96));
				// TODO
				/*if (ImGui::CollapsingHeader("Advanced"))
				{
					static const struct
					{
						const char* name;
						u32 value;
						u32 unset_flag;
					} FLAGS[] = {
						{"u clamp", BGFX_TEXTURE_U_CLAMP, 0},
						{"v clamp", BGFX_TEXTURE_V_CLAMP, 0},
						{"Min point", BGFX_TEXTURE_MIN_POINT, BGFX_TEXTURE_MIN_ANISOTROPIC},
						{"Mag point", BGFX_TEXTURE_MAG_POINT, BGFX_TEXTURE_MAG_ANISOTROPIC},
						{"Min anisotropic", BGFX_TEXTURE_MIN_ANISOTROPIC, BGFX_TEXTURE_MIN_POINT},
						{"Mag anisotropic", BGFX_TEXTURE_MAG_ANISOTROPIC, BGFX_TEXTURE_MAG_POINT}
					};

					for (int i = 0; i < lengthOf(FLAGS); ++i)
					{
						auto& flag = FLAGS[i];
						bool b = (texture->flags & flag.value) != 0;
						if (ImGui::Checkbox(flag.name, &b))
						{
							if (flag.unset_flag) texture->setFlag((Texture::Flags)flag.unset_flag, false);
							texture->setFlag((Texture::Flags)flag.value, b);
						}
					}
				}*/
				ImGui::TreePop();
			}
		}

		auto* shader = material->getShader();
		if (shader && material->isReady())
		{
						// TODO
				/*
	for (int i = 0; i < shader->m_uniforms.size(); ++i)
			{
				auto& uniform = material->getUniform(i);
				auto& shader_uniform = shader->m_uniforms[i];
				switch (shader_uniform.type)
				{
					case Shader::Uniform::FLOAT:
						if (ImGui::DragFloat(shader_uniform.name, &uniform.float_value))
						{
							material->createCommandBuffer();
						}
						break;
					case Shader::Uniform::VEC3:
						if (ImGui::DragFloat3(shader_uniform.name, uniform.vec3))
						{
							material->createCommandBuffer();
						}
						break;
					case Shader::Uniform::VEC4:
						if (ImGui::DragFloat4(shader_uniform.name, uniform.vec4))
						{
							material->createCommandBuffer();
						}
						break;
					case Shader::Uniform::VEC2:
						if (ImGui::DragFloat2(shader_uniform.name, uniform.vec2))
						{
							material->createCommandBuffer();
						}
						break;
					case Shader::Uniform::COLOR:
						if (ImGui::ColorEdit3(shader_uniform.name, uniform.vec3))
						{
							material->createCommandBuffer();
						}
						break;
					case Shader::Uniform::TIME: break;
					default: ASSERT(false); break;
				}
			}
			*/
			// TODO
				/*
			if (ImGui::CollapsingHeader("Defines"))
			{
				for (int define_idx = 0; define_idx < renderer->getShaderDefinesCount(); ++define_idx)
				{
					const char* define = renderer->getShaderDefine(define_idx);
					if (!material->hasDefine(define_idx)) continue;
					bool value = material->isDefined(define_idx);

					auto isBuiltinDefine = [](const char* define) {
						const char* BUILTIN_DEFINES[] = {"HAS_SHADOWMAP", "ALPHA_CUTOUT", "SKINNED"};
						for (const char* builtin_define : BUILTIN_DEFINES)
						{
							if (equalStrings(builtin_define, define)) return true;
						}
						return false;
					};

					bool is_texture_define = material->isTextureDefine(define_idx);
					if (!is_texture_define && !isBuiltinDefine(define) && ImGui::Checkbox(define, &value))
					{
						material->setDefine(define_idx, value);
					}
				}
			}*/

			if (Material::getCustomFlagCount() > 0 && ImGui::CollapsingHeader("Flags"))
			{
				for (int i = 0; i < Material::getCustomFlagCount(); ++i)
				{
					bool b = material->isCustomFlag(1 << i);
					if (ImGui::Checkbox(Material::getCustomFlagName(i), &b))
					{
						if (b)
							material->setCustomFlag(1 << i);
						else
							material->unsetCustomFlag(1 << i);
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
		, m_fbx_importer(app.getWorldEditor().getAllocator())
	{
		app.getAssetCompiler().registerExtension("fbx", Model::TYPE);
		createPreviewUniverse();
		createTileUniverse();
		m_viewport.is_ortho = false;
		m_viewport.fov = Math::degreesToRadians(60.f);
		m_viewport.near = 0.1f;
		m_viewport.far = 10000.f;
	}


	~ModelPlugin()
	{
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


	void addSubresources(AssetCompiler& compiler, const char* path, HashMap<ResourceType, Array<Path>, HashFunc<ResourceType>>& subresources) override
	{
		const Meta meta = getMeta(Path(path));
		auto iter = subresources.find(Model::TYPE);
		if (!iter.isValid()) return;
		
		const Path path_obj(path);
		if (iter.value().indexOf(path_obj) >= 0) return;

		iter.value().push(path_obj);

		if(meta.split) {
			m_fbx_importer.setSource(path[0] == '/' ? path + 1 : path);
			const Array<FBXImporter::ImportMesh>& meshes = m_fbx_importer.getMeshes();
			for (int i = 0; i < meshes.size(); ++i) {
				const char* mesh_name = m_fbx_importer.getImportMeshName(meshes[i]);
				StaticString<MAX_PATH_LENGTH> tmp(mesh_name, ":", (path[0] == '/' ? path + 1: path));
				Path subpath_obj(tmp);
				if (iter.value().indexOf(subpath_obj) < 0) iter.value().push(subpath_obj);
			}
		}
		else {
			const Path path_obj(path);
			if (iter.value().indexOf(path_obj) < 0) iter.value().push(path_obj);
		}
	}


	static const char* getResourceFilePath(const char* str)
	{
		const char* c = str;
		while (*c && *c != ':') ++c;
		return *c != ':' ? str : c + 1;
	}

	bool compile(const Path& src) override
	{
		if (PathUtils::hasExtension(src.c_str(), "fbx")) {
			const char* filepath = getResourceFilePath(src.c_str());
			FBXImporter::ImportConfig cfg;
			cfg.output_dir = m_app.getAssetCompiler().getCompiledDir();
			const Meta meta = getMeta(Path(filepath));
			cfg.mesh_scale = meta.scale;
			const PathUtils::FileInfo src_info(filepath);
			m_fbx_importer.setSource(filepath);
			if (m_fbx_importer.getMeshes().empty()) {
				if (m_fbx_importer.getOFBXScene()->getMeshCount() > 0) {
					g_log_error.log("Editor") << "No meshes with materials found in " << src.c_str();
				}
				else {
					g_log_error.log("Editor") << "No meshes foudn in " << src.c_str();
				}
			}

			const StaticString<32> hash_str("", src.getHash());
			if (meta.split) {
				//cfg.origin = FBXImporter::ImportConfig::Origin::CENTER;
				const Array<FBXImporter::ImportMesh>& meshes = m_fbx_importer.getMeshes();
				m_fbx_importer.writeSubmodels(filepath, cfg);
				m_fbx_importer.writePrefab(filepath, cfg);
			}
			m_fbx_importer.writeModel(hash_str, ".res", src.c_str(), cfg);
			m_fbx_importer.writeMaterials(filepath, cfg);
			m_fbx_importer.writeAnimations(filepath, cfg);
			m_fbx_importer.writeTextures(filepath, cfg);
			return true;
		}

		const char* dst_dir = m_app.getAssetCompiler().getCompiledDir();
		const u32 hash = crc32(src.c_str());

		const StaticString<MAX_PATH_LENGTH> dst(dst_dir, hash, ".res");

		OS::copyFile(src.c_str(), dst);
		return true;
	}


	void createTileUniverse()
	{
		Engine& engine = m_app.getWorldEditor().getEngine();
		m_tile.universe = &engine.createUniverse(false);
		Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
		PipelineResource* pres = engine.getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
		m_tile.pipeline = Pipeline::create(*renderer, pres, "", engine.getAllocator());

		Matrix mtx;
		mtx.lookAt({10, 10, 10}, Vec3::ZERO, {0, 1, 0});
		const EntityRef light_entity = m_tile.universe->createEntity({10, 10, 10}, mtx.getRotation());
		RenderScene* render_scene = (RenderScene*)m_tile.universe->getScene(MODEL_INSTANCE_TYPE);
		m_tile.universe->createComponent(GLOBAL_LIGHT_TYPE, light_entity);
		render_scene->setGlobalLightIntensity(light_entity, 1);
		render_scene->setGlobalLightIndirectIntensity(light_entity, 1);

		m_tile.pipeline->setScene(render_scene);
	}


	void createPreviewUniverse()
	{
		auto& engine = m_app.getWorldEditor().getEngine();
		m_universe = &engine.createUniverse(false);
		auto* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));
		PipelineResource* pres = engine.getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
		m_pipeline = Pipeline::create(*renderer, pres, "PREVIEW",  engine.getAllocator());

		auto mesh_entity = m_universe->createEntity({0, 0, 0}, {0, 0, 0, 1});
		auto* render_scene = static_cast<RenderScene*>(m_universe->getScene(MODEL_INSTANCE_TYPE));
		m_mesh = mesh_entity;
		m_universe->createComponent(MODEL_INSTANCE_TYPE, mesh_entity);

		auto light_entity = m_universe->createEntity({0, 0, 0}, {0, 0, 0, 1});
		m_universe->createComponent(GLOBAL_LIGHT_TYPE, light_entity);
		render_scene->setGlobalLightIntensity(light_entity, 1);

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
		m_pipeline->render();
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
				// TODO 
				ASSERT(false);
				/*const Matrix mtx(m_viewport.pos, m_viewport.rot);
				model.getResourceManager().load(model);
				renderTile(&model, &mtx);*/
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

				float yaw = -Math::signum(delta.x) * (Math::pow(Math::abs((float)delta.x / MOUSE_SENSITIVITY.x), 1.2f));
				Quat yaw_rot(Vec3(0, 1, 0), yaw);
				rot = yaw_rot * rot;
				rot.normalize();

				Vec3 pitch_axis = rot.rotate(Vec3(1, 0, 0));
				float pitch =
					-Math::signum(delta.y) * (Math::pow(Math::abs((float)delta.y / MOUSE_SENSITIVITY.y), 1.2f));
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


	void onGUI(Resource* resource) override
	{
		auto* model = static_cast<Model*>(resource);

		if (resource->isReady()) {
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
						m_app.getAssetBrowser().selectResource(mesh.material->getPath(), true);
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
			if(m_meta_res != resource->getPath().getHash()) {
				m_meta = getMeta(resource->getPath());
				m_meta_res = resource->getPath().getHash();
			}
			ImGui::InputFloat("Scale", &m_meta.scale);
			ImGui::Checkbox("Split", &m_meta.split);
			if (ImGui::Button("Apply")) {
				StaticString<256> src("scale = ", m_meta.scale, "\nsplit = ", m_meta.split ? "true\n" : "false\n");
				compiler.updateMeta(resource->getPath(), src);
				if (compiler.compile(resource->getPath())) {
					resource->getResourceManager().reload(*resource);
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


	void update() override
	{
		if (m_tile.frame_countdown >= 0) {
			--m_tile.frame_countdown;
			if (m_tile.frame_countdown == -1) {
				m_tile.universe->destroyEntity((EntityRef)m_tile.mesh_entity);
				StaticString<MAX_PATH_LENGTH> path(".lumix/asset_tiles/", m_tile.path_hash, ".dds");
				saveAsDDS(path, &m_tile.data[0], AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE);
				Engine& engine = m_app.getWorldEditor().getEngine();
				Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
				renderer->destroy(m_tile.texture);
			}
			return;
		}

		if (m_tile.m_entity_in_fly.isValid()) return;
		if (m_tile.queue.empty()) return;

		Resource* resource = m_tile.queue.front();
		if (resource->isFailure()) {
			g_log_error.log("Editor") << "Failed to load " << resource->getPath();
			popTileQueue();
			return;
		}
		if (!resource->isReady()) return;

		popTileQueue();

		if (resource->getType() == Model::TYPE) {
			renderTile((Model*)resource, nullptr);
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
				// TODO
	//ASSERT(false);
/*Engine& engine = m_app.getWorldEditor().getEngine();
		RenderScene* render_scene = (RenderScene*)m_tile.universe->getScene(MODEL_INSTANCE_TYPE);
		if (!render_scene) return;

		Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
		if (!renderer) return;

		EntityPtr mesh_entity = m_tile.universe->instantiatePrefab(*prefab, Vec3::ZERO, Quat::IDENTITY, 1);
		if (!mesh_entity.isValid()) return;

		if (!render_scene->getUniverse().hasComponent((EntityRef)mesh_entity, MODEL_INSTANCE_TYPE)) return;

		Model* model = render_scene->getModelInstanceModel((EntityRef)mesh_entity);
		if (!model) return;

		m_tile.path_hash = prefab->getPath().getHash();
		prefab->getResourceManager().unload(*prefab);
		m_tile.m_entity_in_fly = mesh_entity;
		model->onLoaded<ModelPlugin, &ModelPlugin::renderPrefabSecondStage>(this);*/
	}


	void renderPrefabSecondStage(Resource::State old_state, Resource::State new_state, Resource& resource)
	{
		// TODO
		ASSERT(false);
		/*
Engine& engine = m_app.getWorldEditor().getEngine();
		

		RenderScene* render_scene = (RenderScene*)m_tile.universe->getScene(MODEL_INSTANCE_TYPE);
		if (!render_scene) return;

		Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
		if (!renderer) return;

		Model* model = (Model*)&resource;
		if (!model->isReady()) return;

		AABB aabb = model->getAABB();

		Matrix mtx;
		Vec3 center = (aabb.max + aabb.min) * 0.5f;
		Vec3 eye = center + Vec3(1, 1, 1) * (aabb.max - aabb.min).length() / Math::SQRT2;
		mtx.lookAt(eye, center, Vec3(-1, 1, -1).normalized());
		mtx.inverse();
		m_tile.universe->setMatrix(m_tile.camera_entity, mtx);

		m_tile.pipeline->resize(AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE);
		m_tile.pipeline->render();

		m_tile.texture =
			bgfx::createTexture2D(AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE, false, 1,
bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_READ_BACK); renderer->viewCounterAdd();
		bgfx::touch(renderer->getViewCounter());
		bgfx::setViewName(renderer->getViewCounter(), "billboard_blit");
		bgfx::TextureHandle color_renderbuffer = m_tile.pipeline->getRenderbuffer("default", 0);
		bgfx::blit(renderer->getViewCounter(), m_tile.texture, 0, 0, color_renderbuffer);

		renderer->viewCounterAdd();
		bgfx::setViewName(renderer->getViewCounter(), "billboard_read");
		m_tile.data.resize(AssetBrowser::TILE_SIZE * AssetBrowser::TILE_SIZE * 4);
		bgfx::readTexture(m_tile.texture, &m_tile.data[0]);
		bgfx::touch(renderer->getViewCounter());
		m_tile.universe->destroyEntity(m_tile.m_entity_in_fly);

		m_tile.frame_countdown = 2;
		m_tile.m_entity_in_fly = INVALID_ENTITY;*/
	}


	void renderTile(Model* model, const Matrix* in_mtx)
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
		Vec3 eye = center + Vec3(1, 1, 1) * (aabb.max - aabb.min).length() / Math::SQRT2;
		mtx.lookAt(eye, center, Vec3(-1, 1, -1).normalized());
		mtx.inverse();
		if (in_mtx) {
			mtx = *in_mtx;
		}
		Viewport viewport;
		viewport.is_ortho = false;
		viewport.far = 10000.f;
		viewport.near = 0.1f;
		viewport.fov = Math::degreesToRadians(60.f);
		viewport.h = AssetBrowser::TILE_SIZE;
		viewport.w = AssetBrowser::TILE_SIZE;
		viewport.pos = DVec3(eye.x, eye.y, eye.z);
		viewport.rot = mtx.getRotation();
		m_tile.pipeline->setViewport(viewport);
		m_tile.pipeline->render();

		struct Cmd : Renderer::RenderJob
		{
			void setup() override {}
			void execute() override
			{
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
		renderer->push(cmd);
		m_tile.mesh_entity = mesh_entity;
		m_tile.frame_countdown = 2;
		m_tile.path_hash = model->getPath().getHash();
		model->getResourceManager().unload(*model);
	}


	bool createTile(const char* in_path, const char* out_path, ResourceType type) override
	{
		if (type == Material::TYPE) return OS::copyFile("models/editor/tile_material.dds", out_path);
		if (type == Shader::TYPE) return OS::copyFile("models/editor/tile_shader.dds", out_path);

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
		EntityPtr mesh_entity = INVALID_ENTITY;
		Pipeline* pipeline = nullptr;
		EntityPtr m_entity_in_fly = INVALID_ENTITY;
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
};


struct TexturePlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	struct Meta
	{
		enum WrapMode : int {
			REPEAT,
			CLAMP
		};
		bool srgb = false;
		bool is_normalmap = false;
		WrapMode wrap_mode = WrapMode::REPEAT;
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


	struct TextureTileJob
	{
		TextureTileJob(IAllocator& allocator) : m_allocator(allocator) {}

		void execute() {
			IAllocator& allocator = m_allocator;

			int image_width, image_height;
			u32 hash = crc32(m_in_path);
			StaticString<MAX_PATH_LENGTH> out_path(".lumix/asset_tiles/", hash, ".dds");
			Array<u8> resized_data(allocator);
			resized_data.resize(AssetBrowser::TILE_SIZE * AssetBrowser::TILE_SIZE * 4);
			if (PathUtils::hasExtension(m_in_path, "dds"))
			{
				FS::OsFile file;
				if (!file.open(m_in_path, FS::Mode::OPEN_AND_READ))
				{
					OS::copyFile("models/editor/tile_texture.dds", out_path);
					g_log_error.log("Editor") << "Failed to load " << m_in_path;
					return;
				}
				Array<u8> data(allocator);
				data.resize((int)file.size());
				file.read(&data[0], data.size());
				file.close();

				crn_uint32* raw_img[cCRNMaxFaces * cCRNMaxLevels];
				crn_texture_desc desc;
				bool success = crn_decompress_dds_to_images(&data[0], data.size(), raw_img, desc);
				if (!success)
				{
					OS::copyFile("models/editor/tile_texture.dds", out_path);
					return;
				}
				image_width = desc.m_width;
				image_height = desc.m_height;
				stbir_resize_uint8((u8*)raw_img[0],
					image_width,
					image_height,
					0,
					&resized_data[0],
					AssetBrowser::TILE_SIZE,
					AssetBrowser::TILE_SIZE,
					0,
					4);
				crn_free_all_images(raw_img, desc);
			}
			else
			{
				int image_comp;
				auto data = stbi_load(m_in_path, &image_width, &image_height, &image_comp, 4);
				if (!data)
				{
					g_log_error.log("Editor") << "Failed to load " << m_in_path;
					OS::copyFile("models/editor/tile_texture.dds", out_path);
					return;
				}
				stbir_resize_uint8(data,
					image_width,
					image_height,
					0,
					&resized_data[0],
					AssetBrowser::TILE_SIZE,
					AssetBrowser::TILE_SIZE,
					0,
					4);
				stbi_image_free(data);
			}

			if (!saveAsDDS(m_out_path, &resized_data[0], AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE))
			{
				g_log_error.log("Editor") << "Failed to save " << m_out_path;
			}
		}

		static void execute(void* data) {
			PROFILE_FUNCTION();
			TextureTileJob* that = (TextureTileJob*)data;
			that->execute();
			LUMIX_DELETE(that->m_allocator, that);
		}

		IAllocator& m_allocator;
		StaticString<MAX_PATH_LENGTH> m_in_path; 
		StaticString<MAX_PATH_LENGTH> m_out_path; 
	};


	bool createTile(const char* in_path, const char* out_path, ResourceType type) override
	{
		if (type == Texture::TYPE) {
			IAllocator& allocator = m_app.getWorldEditor().getAllocator();
			auto* job = LUMIX_NEW(allocator, TextureTileJob)(allocator);
			job->m_in_path = in_path;
			job->m_out_path = out_path;
			JobSystem::run(job, &TextureTileJob::execute, nullptr);
			return true;
		}
		return false;
	}


	bool compileJPEG(FS::OsFile& src, FS::OsFile& dst, const Meta& meta)
	{
		Array<u8> tmp(m_app.getWorldEditor().getAllocator());
		tmp.resize((int)src.size());
		if (!src.read(tmp.begin(), tmp.byte_size())) return false;

		int w, h, comps;
		stbi_uc* data = stbi_load_from_memory(tmp.begin(), tmp.byte_size(), &w, &h, &comps, 4);
		if (!data) return false;

		crn_comp_params comp_params;
		comp_params.m_file_type = cCRNFileTypeDDS;
		comp_params.m_quality_level = cCRNMaxQualityLevel;
		comp_params.m_dxt_quality = cCRNDXTQualityNormal;
		comp_params.m_dxt_compressor_type = cCRNDXTCompressorCRN;
		comp_params.m_pProgress_func = nullptr;
		comp_params.m_pProgress_func_data = nullptr;
		comp_params.m_num_helper_threads = 3;
		comp_params.m_width = w;
		comp_params.m_height = h;
		const bool has_alpha = comps == 4;
		comp_params.m_format = meta.is_normalmap ? cCRNFmtDXN_YX : (has_alpha ? cCRNFmtDXT5 : cCRNFmtDXT1);
		comp_params.m_pImages[0][0] = (u32*)data;
		crn_mipmap_params mipmap_params;
		mipmap_params.m_mode = cCRNMipModeGenerateMips;

		u32 compressed_size;
		void* compressed = crn_compress(comp_params, mipmap_params, compressed_size);
		if(!compressed) {
			stbi_image_free(data);
			return false;
		}

		dst.write("dds", 3);
		u32 flags = meta.srgb ? (u32)Texture::Flags::SRGB : 0;
		flags |= meta.wrap_mode == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP : 0;
		dst.write(&flags, sizeof(flags));
		dst.write(compressed, compressed_size);

		stbi_image_free(data);
		crn_free_block(compressed);

		return true;
	}


	Meta getMeta(const Path& path) const
	{
		Meta meta;
		m_app.getAssetCompiler().getMeta(path, [&meta](lua_State* L){
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "srgb", &meta.srgb);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "normalmap", &meta.is_normalmap);
			char tmp[32];
			if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "wrap_mode", tmp, lengthOf(tmp))) {
				meta.wrap_mode = stricmp(tmp, "repeat") == 0 ? Meta::WrapMode::REPEAT : Meta::WrapMode::CLAMP;
			}
		});
		return meta;
	}


	bool compile(const Path& src) override
	{
		char ext[4] = {};
		PathUtils::getExtension(ext, lengthOf(ext), src.c_str());

		const char* dst_dir = m_app.getAssetCompiler().getCompiledDir();
		const u32 hash = crc32(src.c_str());

		const StaticString<MAX_PATH_LENGTH> dst(dst_dir, hash, ".res");

		FS::OsFile srcf;
		FS::OsFile dstf;
		if (!srcf.open(src.c_str(), FS::Mode::OPEN_AND_READ)) return false;
		if (!dstf.open(dst, FS::Mode::CREATE_AND_WRITE)) {
			srcf.close();
			return false;
		}
		
		Meta meta = getMeta(src);
		if (equalStrings(ext, "dds") || equalStrings(ext, "raw") || equalStrings(ext, "tga")) {
			Array<u8> buffer(m_app.getWorldEditor().getAllocator());
			buffer.resize((int)srcf.size());

			srcf.read(buffer.begin(), buffer.byte_size());

			dstf.write(ext, sizeof(ext) - 1);
			u32 flags = meta.srgb ? (u32)Texture::Flags::SRGB : 0;
			flags |= meta.wrap_mode == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP : 0;
			dstf.write(&flags, sizeof(flags));
			dstf.write(buffer.begin(), buffer.byte_size());
		}
		else if(equalStrings(ext, "jpg")) {
			compileJPEG(srcf, dstf, meta);
		}
		else if(equalStrings(ext, "png")) {
			// TODO rename
			compileJPEG(srcf, dstf, meta);
		}
		else {
			ASSERT(false);
		}

		srcf.close();
		dstf.close();

		return true;
	}


	void onGUI(Resource* resource) override
	{
		auto* texture = static_cast<Texture*>(resource);

		ImGui::LabelText("Size", "%dx%d", texture->width, texture->height);
		ImGui::LabelText("Mips", "%d", texture->mips);
		if (texture->bytes_per_pixel > 0) ImGui::LabelText("BPP", "%d", texture->bytes_per_pixel);
		if (texture->is_cubemap) {
			ImGui::Text("Cubemap");
		}
		else if (texture->handle.isValid()) {
			ImVec2 texture_size(200, 200);
			if (texture->width > texture->height)
			{
				texture_size.y = texture_size.x * texture->height / texture->width;
			}
			else
			{
				texture_size.x = texture_size.y * texture->width / texture->height;
			}

			ImGui::Image((void*)(uintptr_t)texture->handle.value, texture_size);

			if (ImGui::Button("Open")) m_app.getAssetBrowser().openInExternalEditor(resource);
		}

		if (ImGui::CollapsingHeader("Import")) {
			AssetCompiler& compiler = m_app.getAssetCompiler();
			
			if(texture->getPath().getHash() != m_meta_res) {
				m_meta = getMeta(resource->getPath());
				m_meta_res = texture->getPath().getHash();
			}
			
			ImGui::Checkbox("SRGB", &m_meta.srgb);
			ImGui::Checkbox("Is normalmap", &m_meta.is_normalmap);
			ImGui::Combo("Wrap mode", (int*)&m_meta.wrap_mode, "Repeat\0Clamp\0");

			if (ImGui::Button("Apply")) {
				const StaticString<256> src("srgb = ", m_meta.srgb ? "true" : "false"
					, "\nnormalmap = ", m_meta.is_normalmap ? "true" : "false"
					, "\nwrap_mode = \"", m_meta.wrap_mode == Meta::WrapMode::REPEAT ? "repeat\"" : "clamp\"");
				compiler.updateMeta(resource->getPath(), src);
				if (compiler.compile(resource->getPath())) {
					resource->getResourceManager().reload(*resource);
				}
			}
		}
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Texture"; }
	ResourceType getResourceType() const override { return Texture::TYPE; }


	StudioApp& m_app;
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

		FS::OsFile file;
		if (!file.open(path[0] == '/' ? path + 1 : path, FS::Mode::OPEN_AND_READ)) return;
		
		IAllocator& allocator = m_app.getWorldEditor().getAllocator();
		Array<u8> content(allocator);
		content.resize((int)file.size());
		file.read(content.begin(), content.byte_size());
		file.close();

		struct Context {
			const char* path;
			ShaderPlugin* plugin;
			u8* content;
			int content_len;
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
			g_log_error.log("Engine") << path << ": " << lua_tostring(L, -1);
			lua_pop(L, 2);
			lua_close(L);
			return;
		}

		if (lua_pcall(L, 0, 0, -2) != 0) {
			g_log_error.log("Engine") << lua_tostring(L, -1);
			lua_pop(L, 2);
			lua_close(L);
			return;
		}
		lua_pop(L, 1);
		lua_close(L);
	}


	void addSubresources(AssetCompiler& compiler, const char* path, HashMap<ResourceType, Array<Path>, HashFunc<ResourceType>>& subresources) override
	{
		const Path path_obj(path);
		auto iter = subresources.find(Shader::TYPE);
		if (!iter.isValid()) return;
		if (iter.value().indexOf(path_obj) < 0) iter.value().push(path_obj);

		findIncludes(path);
	}


	bool compile(const Path& src) override
	{
		const char* dst_dir = m_app.getAssetCompiler().getCompiledDir();
		const u32 hash = crc32(src.c_str());

		const StaticString<MAX_PATH_LENGTH> dst(dst_dir, hash, ".res");

		return OS::copyFile(src.c_str(), dst);
	}


	void onGUI(Resource* resource) override
	{
		auto* shader = static_cast<Shader*>(resource);
		char basename[MAX_PATH_LENGTH];
		PathUtils::getBasename(basename, lengthOf(basename), resource->getPath().c_str());
		if (ImGui::Button("Open in external editor"))
		{
			m_app.getAssetBrowser().openInExternalEditor(resource->getPath().c_str());
		}

		if (shader->m_texture_slot_count > 0 &&
			ImGui::CollapsingHeader(
				"Texture slots", nullptr, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
		{
			for (int i = 0; i < shader->m_texture_slot_count; ++i)
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
					case Shader::Uniform::COLOR: ImGui::Text("color"); break;
					case Shader::Uniform::FLOAT: ImGui::Text("float"); break;
					case Shader::Uniform::INT: ImGui::Text("int"); break;
					case Shader::Uniform::MATRIX4: ImGui::Text("Matrix 4x4"); break;
					case Shader::Uniform::TIME: ImGui::Text("time"); break;
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
	explicit EnvironmentProbePlugin(StudioApp& app)
		: m_app(app)
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


	bool saveCubemap(ComponentUID cmp, const u8* data, int texture_size, const char* postfix)
	{
		crn_uint32 size;
		crn_comp_params comp_params;
		comp_params.m_width = texture_size;
		comp_params.m_height = texture_size;
		comp_params.m_file_type = cCRNFileTypeDDS;
		comp_params.m_format = cCRNFmtDXT1;
		comp_params.m_quality_level = cCRNMinQualityLevel;
		comp_params.m_dxt_quality = cCRNDXTQualitySuperFast;
		comp_params.m_dxt_compressor_type = cCRNDXTCompressorRYG;
		comp_params.m_pProgress_func = nullptr;
		comp_params.m_pProgress_func_data = nullptr;
		comp_params.m_num_helper_threads = MT::getCPUsCount() - 1;
		comp_params.m_faces = 6;
		for (int i = 0; i < 6; ++i)
		{
			comp_params.m_pImages[i][0] = (u32*)&data[i * texture_size * texture_size * 4];
		}
		crn_mipmap_params mipmap_params;
		mipmap_params.m_mode = cCRNMipModeGenerateMips;

		void* compressed_data = crn_compress(comp_params, mipmap_params, size);
		if (!compressed_data)
		{
			g_log_error.log("Editor") << "Failed to compress the probe.";
			return false;
		}

		FS::OsFile file;
		const char* base_path = m_app.getWorldEditor().getEngine().getDiskFileDevice()->getBasePath();
		StaticString<MAX_PATH_LENGTH> path(base_path, "universes/", m_app.getWorldEditor().getUniverse()->getName());
		if (!OS::makePath(path) && !OS::dirExists(path))
		{
			g_log_error.log("Editor") << "Failed to create " << path;
		}
		path << "/probes/";
		if (!OS::makePath(path) && !OS::dirExists(path))
		{
			g_log_error.log("Editor") << "Failed to create " << path;
		}
		u64 probe_guid = ((RenderScene*)cmp.scene)->getEnvironmentProbeGUID((EntityRef)cmp.entity);
		path << probe_guid << postfix << ".dds";
		if (!file.open(path, FS::Mode::CREATE_AND_WRITE))
		{
			g_log_error.log("Editor") << "Failed to create " << path;
			crn_free_block(compressed_data);
			return false;
		}

		file.write((const char*)compressed_data, size);
		file.close();
		crn_free_block(compressed_data);
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


	void generateCubemap(ComponentUID cmp)
	{
		ASSERT(cmp.isValid());
		const EntityRef entity = (EntityRef)cmp.entity;

		static const int TEXTURE_SIZE = 1024;

		Universe* universe = m_app.getWorldEditor().getUniverse();
		if (universe->getName()[0] == '\0') {
			g_log_error.log("Editor") << "Universe must be saved before environment probe can be generated.";
			return;
		}

		WorldEditor& world_editor = m_app.getWorldEditor();
		Engine& engine = world_editor.getEngine();
		auto& plugin_manager = engine.getPluginManager();
		IAllocator& allocator = engine.getAllocator();

		const DVec3 probe_position = universe->getPosition((EntityRef)cmp.entity);
		auto* scene = static_cast<RenderScene*>(universe->getScene(CAMERA_TYPE));
		Viewport viewport;
		viewport.is_ortho = false;
		viewport.fov = Math::degreesToRadians(90.f);
		viewport.near = 0.1f;
		viewport.far = 10000.f;
		viewport.w = TEXTURE_SIZE;
		viewport.h = TEXTURE_SIZE;

		m_pipeline->setScene(scene);
		m_pipeline->setViewport(viewport);

		Renderer* renderer = static_cast<Renderer*>(plugin_manager.getPlugin("renderer"));
		Vec3 dirs[] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
		Vec3 ups[] = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, 1, 0}};
		Vec3 ups_opengl[] = { { 0, -1, 0 },{ 0, -1, 0 },{ 0, 0, 1 },{ 0, 0, -1 },{ 0, -1, 0 },{ 0, -1, 0 } };

		Array<u8> data(allocator);
		data.resize(6 * TEXTURE_SIZE * TEXTURE_SIZE * 4);

		renderer->startCapture();
		for (int i = 0; i < 6; ++i) {
			const bool ndc_bottom_left = ffr::isOriginBottomLeft();
			Vec3 side = crossProduct(ndc_bottom_left ? ups_opengl[i] : ups[i], dirs[i]);
			Matrix mtx = Matrix::IDENTITY;
			mtx.setZVector(dirs[i]);
			mtx.setYVector(ndc_bottom_left ? ups_opengl[i] : ups[i]);
			mtx.setXVector(side);
			viewport.pos = probe_position;
			viewport.rot = mtx.getRotation();
			m_pipeline->setViewport(viewport);
			m_pipeline->render();

			const ffr::TextureHandle res = m_pipeline->getOutput();
			ASSERT(res.isValid());
			renderer->getTextureImage(res, TEXTURE_SIZE * TEXTURE_SIZE * 4, &data[i * TEXTURE_SIZE * TEXTURE_SIZE * 4]);

			if (ndc_bottom_left) continue;

			u32* tmp = (u32*)&data[i * TEXTURE_SIZE * TEXTURE_SIZE * 4];
			if (i == 2 || i == 3)
			{
				flipY(tmp, TEXTURE_SIZE);
			}
			else
			{
				flipX(tmp, TEXTURE_SIZE);
			}
		}
		renderer->stopCapture();
		renderer->frame();
		renderer->frame();

		cmft::Image image;
		cmft::Image irradiance;

		cmft::imageCreate(image, TEXTURE_SIZE, TEXTURE_SIZE, 0x303030ff, 1, 6, cmft::TextureFormat::RGBA8);
		cmft::imageFromRgba32f(image, cmft::TextureFormat::RGBA8);
		copyMemory(image.m_data, &data[0], data.size());
		cmft::imageToRgba32f(image);
		

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

		cmft::imageIrradianceFilterSh(irradiance, 32, image);

		cmft::imageFromRgba32f(image, cmft::TextureFormat::RGBA8);
		cmft::imageFromRgba32f(irradiance, cmft::TextureFormat::RGBA8);

		int irradiance_size = 32;
		int radiance_size = 128;
		int reflection_size = TEXTURE_SIZE;

		if (scene->isEnvironmentProbeCustomSize(entity)) {
			irradiance_size = scene->getEnvironmentProbeIrradianceSize(entity);
			radiance_size = scene->getEnvironmentProbeRadianceSize(entity);
			reflection_size = scene->getEnvironmentProbeReflectionSize(entity);
		}

		for (int i = 3; i < data.size(); i += 4) data[i] = 0xff; 
		saveCubemap(cmp, (u8*)irradiance.m_data, irradiance_size, "_irradiance");
		saveCubemap(cmp, (u8*)image.m_data, radiance_size, "_radiance");
		if (scene->isEnvironmentProbeReflectionEnabled(entity)) {
			saveCubemap(cmp, &data[0], reflection_size, "");
		}

		scene->reloadEnvironmentProbe(entity);
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
			if (ImGui::Button("View reflection")) m_app.getAssetBrowser().selectResource(texture->getPath(), true);
		}
		texture = scene->getEnvironmentProbeIrradiance(e);
		if (texture)
		{
			ImGui::LabelText("Irradiance path", "%s", texture->getPath().c_str());
			if (ImGui::Button("View irradiance")) m_app.getAssetBrowser().selectResource(texture->getPath(), true);
		}
		texture = scene->getEnvironmentProbeRadiance(e);
		if (texture)
		{
			ImGui::LabelText("Radiance path", "%s", texture->getPath().c_str());
			if (ImGui::Button("View radiance")) m_app.getAssetBrowser().selectResource(texture->getPath(), true);
		}
		if (ImGui::Button("Generate")) generateCubemap(cmp);
	}


	StudioApp& m_app;
	Pipeline* m_pipeline;

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

		editor.universeCreated().bind<RenderInterfaceImpl, &RenderInterfaceImpl::onUniverseCreated>(this);
		editor.universeDestroyed().bind<RenderInterfaceImpl, &RenderInterfaceImpl::onUniverseDestroyed>(this);

		m_model_uniform = ffr::allocUniform("u_model", ffr::UniformType::MAT4, 1);
	}


	~RenderInterfaceImpl()
	{
		auto& rm = m_editor.getEngine().getResourceManager();
		rm.get(Shader::TYPE)->unload(*m_shader);

		m_editor.universeCreated().unbind<RenderInterfaceImpl, &RenderInterfaceImpl::onUniverseCreated>(this);
		m_editor.universeDestroyed().unbind<RenderInterfaceImpl, &RenderInterfaceImpl::onUniverseDestroyed>(this);
	}


	void addText2D(float x, float y, float font_size, u32 color, const char* text) override
	{
		Font* font = m_renderer.getFontManager().getDefaultFont();
		m_pipeline.getDraw2D().AddText(font, font_size, {x, y}, color, text);
	}


	void addRect2D(const Vec2& a, const Vec2& b, u32 color) override { m_pipeline.getDraw2D().AddRect(a, b, color); }


	void addRectFilled2D(const Vec2& a, const Vec2& b, u32 color) override
	{
		m_pipeline.getDraw2D().AddRectFilled(a, b, color);
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
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		Material* material = engine.getResourceManager().load<Material>(Path("pipelines/imgui/imgui.mat"));

		Texture* old_texture = material->getTexture(0);
		PluginManager& plugin_manager = engine.getPluginManager();
		Texture* texture = LUMIX_NEW(engine.getAllocator(), Texture)(
			Path("font"), m_renderer, *engine.getResourceManager().get(Texture::TYPE), engine.getAllocator());

		texture->create(width, height, pixels, width * height * 4);
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


	bool saveTexture(Engine& engine, const char* path_cstr, const void* pixels, int w, int h) override
	{
		FS::FileSystem& fs = engine.getFileSystem();
		Path path(path_cstr);
		FS::IFile* file = fs.open(fs.getDefaultDevice(), path, FS::Mode::CREATE_AND_WRITE);
		if (!file) return false;

		if (!Texture::saveTGA(file, w, h, 4, (const u8*)pixels, path, engine.getAllocator()))
		{
			fs.close(*file);
			return false;
		}

		fs.close(*file);
		return true;
	}


	ImTextureID createTexture(const char* name, const void* pixels, int w, int h) override
	{
		Engine& engine = m_editor.getEngine();
		auto& rm = engine.getResourceManager();
		auto& allocator = m_editor.getAllocator();

		Texture* texture = LUMIX_NEW(allocator, Texture)(Path(name), m_renderer, *rm.get(Texture::TYPE), allocator);
		texture->create(w, h, pixels, w * h * 4);
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


	void addDebugCross(const DVec3& pos, float size, u32 color, float life) override
	{
		m_render_scene->addDebugCross(pos, size, color, life);
	}


	WorldEditor::RayHit castRay(const DVec3& origin, const Vec3& dir, EntityPtr ignored) override
	{
		const RayCastModelHit hit = m_render_scene->castRay(origin, dir, ignored);

		return {hit.is_hit, hit.t, hit.entity, hit.origin + hit.dir * hit.t};
	}


	void addDebugLine(const DVec3& from, const DVec3& to, u32 color, float life) override
	{
		m_render_scene->addDebugLine(from, to, color, life);
	}


	void addDebugCube(const DVec3& minimum, const DVec3& maximum, u32 color, float life) override
	{
		m_render_scene->addDebugCube(minimum, maximum, color, life);
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


	float getCameraOrthoSize(EntityRef entity) override { return m_render_scene->getCameraOrthoSize(entity); }


	bool isCameraOrtho(EntityRef entity) override { return m_render_scene->isCameraOrtho(entity); }


	float getCameraFOV(EntityRef entity) override { return m_render_scene->getCameraFOV(entity); }


	float castRay(ModelHandle model, const Vec3& origin, const Vec3& dir, const Pose* pose) override
	{
		RayCastModelHit hit = m_models[model]->castRay(origin, dir, pose);
		return hit.is_hit ? hit.t : -1;
	}


	void renderModel(ModelHandle model, const Matrix& mtx) override
	{
		if (!m_pipeline.isReady() || !m_models[model]->isReady()) return;

		Pipeline::renderModel(*m_models[model], mtx, m_model_uniform);
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


	void getRenderables(Array<EntityRef>& entities, const ShiftedFrustum& frustum, RenderableTypes type) override
	{
		Array<Array<EntityRef>> renderables(m_render_scene->getAllocator());
		m_render_scene->getRenderables(frustum, renderables, type);
		for (const Array<EntityRef>& a : renderables) {
			for (EntityRef b : a) {
				entities.push(b);
			}
		}
	}


	WorldEditor& m_editor;
	Shader* m_shader;
	RenderScene* m_render_scene;
	Renderer& m_renderer;
	Pipeline& m_pipeline;
	ffr::UniformHandle m_model_uniform;
	HashMap<int, Model*> m_models;
	HashMap<void*, Texture*> m_textures;
	int m_model_index;
};


struct RenderStatsPlugin final : public StudioApp::GUIPlugin
{
	struct ProfileTree
	{
		ProfileTree(IAllocator& allocator) : m_nodes(allocator) {}

		struct Node
		{
			StaticString<32> name;
			float times[128];
			u64 query_time;
			int parent = -1;
			int child = -1;
			int sibling = -1;
		};

		int getChild(int parent, const char* name)
		{
			int tmp = 0;
			int* child = parent < 0 ? &tmp : &m_nodes[parent].child;
			if(!m_nodes.empty()) {
				while(*child >= 0) {
					Node& n = m_nodes[*child];
					if(equalStrings(n.name, name)) return *child;
					child = &n.sibling;
				}
			}

			Node& n = m_nodes.emplace();
			n.name = name;
			n.parent = parent;
			*child = m_nodes.size() - 1;
			return *child;
		}

		void add(const Array<Renderer::GPUProfilerQuery>& queries)
		{
			if (queries.empty()) return;

			for (Node& n : m_nodes) {
				n.times[m_frame % lengthOf(n.times)] = 0;
			}

			int current = -1;
			const int start = queries[0].is_end ? 1 : 0;
			for(int i = start, c = queries.size() - 1; i < c; ++i) {
				const Renderer::GPUProfilerQuery& q = queries[i];
				if(q.is_end) {
					Node& n = m_nodes[current];
					const float t = float((q.result - n.query_time) / double(1'000'000));
					n.times[m_frame % lengthOf(n.times)] += t;
					current = m_nodes[current].parent;
					continue;
				}
				
				const int idx = getChild(current, q.name);
				Node& n = m_nodes[idx];
				n.query_time = q.result;
				current = idx;
			}
			++m_frame;
		}

		Array<Node> m_nodes;
		uint m_frame = 0;
	};

	explicit RenderStatsPlugin(StudioApp& app)
		: m_app(app)
		, m_profile_tree(app.getWorldEditor().getAllocator())
	{
		Action* action = LUMIX_NEW(app.getWorldEditor().getAllocator(), Action)(
			"Render Stats", "Toggle render stats", "render_stats");
		action->func.bind<RenderStatsPlugin, &RenderStatsPlugin::onAction>(this);
		action->is_selected.bind<RenderStatsPlugin, &RenderStatsPlugin::isOpen>(this);
		app.addWindowAction(action);
	}


	const char* getName() const override { return "render_stats"; }


	void timingsUI(int idx)
	{
		if (idx == -1) return;
		const ProfileTree::Node& n = m_profile_tree.m_nodes[idx];
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_OpenOnArrow;
		if (n.child < 0) flags |= ImGuiTreeNodeFlags_Leaf;
		const bool open = ImGui::TreeNodeEx(n.name, flags);
		ImGui::NextColumn();
		ImGui::Text("%.2f ms", n.times[(m_profile_tree.m_frame - 1) % lengthOf(n.times)]);
		ImGui::NextColumn();
		if (open) {
			timingsUI(n.child);
			ImGui::TreePop();
		}
		timingsUI(n.sibling);
	}


	void onWindowGUI() override
	{
		if (!m_is_open) return;
		if (ImGui::Begin("Renderer Stats", &m_is_open)) {
			Renderer& renderer = *(Renderer*)m_app.getWorldEditor().getEngine().getPluginManager().getPlugin("renderer");
			Array<Renderer::GPUProfilerQuery> timings(renderer.getAllocator());
			renderer.getGPUTimings(&timings);
			m_profile_tree.add(timings);

			ImGui::Columns(2);
			if (!timings.empty()) {
				timingsUI(0);
			}
			ImGui::Columns();
		}
		ImGui::End();
	}


	bool isOpen() const { return m_is_open; }
	void onAction() { m_is_open = !m_is_open; }


	StudioApp& m_app;
	ProfileTree m_profile_tree;
	bool m_is_open = false;
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
				plugin->m_program = ffr::allocProgramHandle();
			}
		
			width = plugin->m_width;
			height = plugin->m_height;
			default_texture = &plugin->m_texture;
			vb = plugin->m_vertex_buffer;
			ib = plugin->m_index_buffer;
			canvas_size_uniform = plugin->m_canvas_size_uniform;
			texture_uniform = plugin->m_texture_uniform;
			program = plugin->m_program;
		}


		void draw(const CmdList& cmd_list)
		{
			const uint num_indices = cmd_list.idx_buffer.size / sizeof(ImDrawIdx);
			const uint num_vertices = cmd_list.vtx_buffer.size / sizeof(ImDrawVert);

			ffr::VertexDecl decl;
			decl.addAttribute(2, ffr::AttributeType::FLOAT, false, false);
			decl.addAttribute(2, ffr::AttributeType::FLOAT, false, false);
			decl.addAttribute(4, ffr::AttributeType::U8, true, false);

			const bool use_big_buffers = (vb_offset + num_vertices) * sizeof(ImDrawVert) > 1024 * 1024 ||
				(ib_offset + num_indices) * sizeof(ImDrawIdx) > 1024 * 1024;

			ffr::BufferHandle big_ib, big_vb;
			if (use_big_buffers) {
				big_vb = ffr::allocBufferHandle();
				big_ib = ffr::allocBufferHandle();
				ffr::createBuffer(big_vb, (uint)ffr::BufferFlags::DYNAMIC_STORAGE, num_vertices * sizeof(ImDrawVert), cmd_list.vtx_buffer.data);
				ffr::createBuffer(big_ib, (uint)ffr::BufferFlags::DYNAMIC_STORAGE, num_indices * sizeof(ImDrawIdx), cmd_list.idx_buffer.data);
				ffr::setVertexBuffer(&decl, big_vb, 0, nullptr);
				ffr::setIndexBuffer(big_ib);
			}
			else {
				ffr::update(ib
					, cmd_list.idx_buffer.data
					, ib_offset * sizeof(ImDrawIdx)
					, num_indices * sizeof(ImDrawIdx));
				ffr::update(vb
					, cmd_list.vtx_buffer.data
					, vb_offset * sizeof(ImDrawVert)
					, num_vertices * sizeof(ImDrawVert));

				ffr::setVertexBuffer(&decl, vb, vb_offset * sizeof(ImDrawVert), nullptr);
				ffr::setIndexBuffer(ib);
			}
			renderer->free(cmd_list.vtx_buffer);
			renderer->free(cmd_list.idx_buffer);
			u32 elem_offset = 0;
			const ImDrawCmd* pcmd_begin = cmd_list.commands.begin();
			const ImDrawCmd* pcmd_end = cmd_list.commands.end();
			ffr::useProgram(program);
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

				const u32 first_index = elem_offset + (use_big_buffers ? 0 : ib_offset);

				ffr::TextureHandle tex = pcmd->TextureId ? ffr::TextureHandle{(uint)(intptr_t)pcmd->TextureId} : *default_texture;
				if (!tex.isValid()) tex = *default_texture;
				ffr::bindTextures(&tex, 1);

				const uint h = uint(Math::minimum(pcmd->ClipRect.w, 65535.0f) - Math::maximum(pcmd->ClipRect.y, 0.0f));

				ffr::scissor(uint(Math::maximum(pcmd->ClipRect.x, 0.0f)),
					height - uint(Math::maximum(pcmd->ClipRect.y, 0.0f)) - h,
					uint(Math::minimum(pcmd->ClipRect.z, 65535.0f) - Math::maximum(pcmd->ClipRect.x, 0.0f)),
					uint(Math::minimum(pcmd->ClipRect.w, 65535.0f) - Math::maximum(pcmd->ClipRect.y, 0.0f)));

				ffr::drawElements(first_index, pcmd->ElemCount, ffr::PrimitiveType::TRIANGLES, ffr::DataType::UINT32);
		
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
				ffr::createBuffer(ib, (uint)ffr::BufferFlags::DYNAMIC_STORAGE, 1024 * 1024, nullptr);
				ffr::createBuffer(vb, (uint)ffr::BufferFlags::DYNAMIC_STORAGE, 1024 * 1024, nullptr);
				const char* vs =
					R"#(#version 330
					layout(location = 0) in vec2 a_pos;
					layout(location = 1) in vec2 a_uv;
					layout(location = 2) in vec4 a_color;
					out vec4 v_color;
					out vec2 v_uv;
					uniform vec2 u_canvas_size;
					void main() {
						v_color = a_color;
						v_uv = a_uv;
						gl_Position = vec4(a_pos / u_canvas_size * 2 - 1, 0, 1);
						gl_Position.y = -gl_Position.y;
					})#";
				const char* fs = 
					R"#(#version 330
					in vec4 v_color;
					in vec2 v_uv;
					out vec4 o_color;
					uniform sampler2D u_texture;
					void main() {
						vec4 tc = textureLod(u_texture, v_uv, 0);
						o_color.rgb = pow(tc.rgb, vec3(1/2.2)) * v_color.rgb;
						o_color.a = v_color.a * tc.a;
					})#";
				const char* srcs[] = {vs, fs};
				ffr::ShaderType types[] = {ffr::ShaderType::VERTEX, ffr::ShaderType::FRAGMENT};
				ffr::createProgram(program, srcs, types, 2, nullptr, 0, "imgui shader");
			}

			ffr::pushDebugGroup("imgui");
			ffr::setFramebuffer(ffr::INVALID_FRAMEBUFFER, false);

			const float clear_color[] = {0.2f, 0.2f, 0.2f, 1.f};
			ffr::clear((uint)ffr::ClearFlags::COLOR | (uint)ffr::ClearFlags::DEPTH, clear_color, 1.0);

			ffr::viewport(0, 0, width, height);
			const float canvas_size[] = {(float)width, (float)height};
			ffr::setUniform2f(canvas_size_uniform, canvas_size);
			ffr::setUniform1i(texture_uniform, 0);

			vb_offset = 0;
			ib_offset = 0;
			for(const CmdList& cmd_list : command_lists) {
				draw(cmd_list);
			}

			ffr::popDebugGroup();
		}
		
		Renderer* renderer;
		const ffr::TextureHandle* default_texture;
		uint width, height;
		Array<CmdList> command_lists;
		uint ib_offset;
		uint vb_offset;
		IAllocator& allocator;
		ffr::BufferHandle ib;
		ffr::BufferHandle vb;
		ffr::UniformHandle canvas_size_uniform;
		ffr::UniformHandle texture_uniform;
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
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

		const Renderer::MemRef mem = renderer->copy(pixels, width * height * 4);
		m_texture = renderer->createTexture(width, height, 1, ffr::TextureFormat::RGBA8, 0, mem, "editor_font_atlas");

		IAllocator& allocator = editor.getAllocator();
		RenderInterface* render_interface =
			LUMIX_NEW(allocator, RenderInterfaceImpl)(editor, *scene_view.getPipeline(), *renderer);
		editor.setRenderInterface(render_interface);

		m_canvas_size_uniform = ffr::allocUniform("u_canvas_size", ffr::UniformType::VEC2, 1);
		m_texture_uniform = ffr::allocUniform("u_texture", ffr::UniformType::INT, 1);
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
		
		renderer->push(cmd);
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
	ffr::ProgramHandle m_program;
	ffr::UniformHandle m_texture_uniform;
	ffr::UniformHandle m_canvas_size_uniform;
};


struct ShaderEditorPlugin final : public StudioApp::GUIPlugin
{
	explicit ShaderEditorPlugin(StudioApp& app)
		: m_shader_editor(app.getWorldEditor().getAllocator())
		, m_app(app)
	{
		Action* action = LUMIX_NEW(app.getWorldEditor().getAllocator(), Action)(
			"Shader Editor", "Toggle shader editor", "shaderEditor");
		action->func.bind<ShaderEditorPlugin, &ShaderEditorPlugin::onAction>(this);
		action->is_selected.bind<ShaderEditorPlugin, &ShaderEditorPlugin::isOpen>(this);
		app.addWindowAction(action);
		m_shader_editor.m_is_open = false;
	}


	const char* getName() const override { return "shader_editor"; }
	void onAction() { m_shader_editor.m_is_open = !m_shader_editor.m_is_open; }
	void onWindowGUI() override { m_shader_editor.onGUI(); }
	bool hasFocus() override { return m_shader_editor.hasFocus(); }
	bool isOpen() const { return m_shader_editor.m_is_open; }

	StudioApp& m_app;
	ShaderEditor m_shader_editor;
};


struct GizmoPlugin final : public WorldEditor::Plugin
{
	void showPointLightGizmo(ComponentUID light)
	{
		RenderScene* scene = static_cast<RenderScene*>(light.scene);
		Universe& universe = scene->getUniverse();

		const float range = scene->getLightRange((EntityRef)light.entity);

		const DVec3 pos = universe.getPosition((EntityRef)light.entity);
		scene->addDebugSphere(pos, range, 0xff0000ff, 0);
	}


	static Vec3 minCoords(const Vec3& a, const Vec3& b)
	{
		return Vec3(Math::minimum(a.x, b.x), Math::minimum(a.y, b.y), Math::minimum(a.z, b.z));
	}


	static Vec3 maxCoords(const Vec3& a, const Vec3& b)
	{
		return Vec3(Math::maximum(a.x, b.x), Math::maximum(a.y, b.y), Math::maximum(a.z, b.z));
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

		scene->addDebugLine(pos, pos + dir, 0xff0000ff, 0);
		scene->addDebugLine(pos + right, pos + dir + right, 0xff0000ff, 0);
		scene->addDebugLine(pos - right, pos + dir - right, 0xff0000ff, 0);
		scene->addDebugLine(pos + up, pos + dir + up, 0xff0000ff, 0);
		scene->addDebugLine(pos - up, pos + dir - up, 0xff0000ff, 0);

		scene->addDebugLine(pos + right + up, pos + dir + right + up, 0xff0000ff, 0);
		scene->addDebugLine(pos + right - up, pos + dir + right - up, 0xff0000ff, 0);
		scene->addDebugLine(pos - right - up, pos + dir - right - up, 0xff0000ff, 0);
		scene->addDebugLine(pos - right + up, pos + dir - right + up, 0xff0000ff, 0);

		scene->addDebugSphere(pos - dir, 0.1f, 0xff0000ff, 0);
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
		scene->addDebugCube(tr.pos, x, y, z, 0xff0000ff, 0);
	}


	void showCameraGizmo(ComponentUID cmp)
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);

		scene->addDebugFrustum(scene->getCameraFrustum((EntityRef)cmp.entity), 0xffff0000, 0);
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
		if (cmp.type == GLOBAL_LIGHT_TYPE)
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
		PathUtils::normalize(material_path, normalized_material_path, lengthOf(normalized_material_path));

		PathUtils::FileInfo info(normalized_material_path);
		StaticString<MAX_PATH_LENGTH> hm_path(info.m_dir, info.m_basename, ".raw");
		FS::OsFile file;
		if (!file.open(hm_path, FS::Mode::CREATE_AND_WRITE))
		{
			g_log_error.log("Editor") << "Failed to create heightmap " << hm_path;
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

		if (!file.open(normalized_material_path, FS::Mode::CREATE_AND_WRITE))
		{
			g_log_error.log("Editor") << "Failed to create material " << normalized_material_path;
			OS::deleteFile(hm_path);
			return false;
		}

		file.writeText(R"#(
			shader "pipelines/terrain.shd"
			texture ")#");
		file.writeText(info.m_basename);
		file.writeText(R"#(.raw"
			texture "/textures/common/white.tga"
			texture ""
			texture ""
		)#");

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
				if (OS::getSaveFilename(
						save_filename, lengthOf(save_filename), "Material\0*.mat\0", "mat"))
				{
					editor.makeRelative(buf, lengthOf(buf), save_filename);
					new_created = createHeightmap(buf, size);
				}
			}
			ImGui::EndMenu();
		}
		bool create_empty = ImGui::Selectable("Empty", false);
		if (asset_browser.resourceList(buf, lengthOf(buf), Material::TYPE, 0) || create_empty || new_created)
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


	const char* getLabel() const override { return "Render/Terrain"; }


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

		m_app.registerComponent("camera", "Render/Camera");
		m_app.registerComponent("global_light", "Render/Global light");

		m_app.registerComponentWithResource(
			"model_instance", "Render/Mesh", Model::TYPE, *Reflection::getProperty(MODEL_INSTANCE_TYPE, "Source"));
		m_app.registerComponentWithResource("particle_emitter",
			"Render/Particle emitter",
			ParticleEmitterResource::TYPE,
			*Reflection::getProperty(PARTICLE_EMITTER_TYPE, "Resource"));
		m_app.registerComponent("point_light", "Render/Point light");
		m_app.registerComponent("decal", "Render/Decal");
		m_app.registerComponent("bone_attachment", "Render/Bone attachment");
		m_app.registerComponent("environment_probe", "Render/Environment probe");
		m_app.registerComponentWithResource(
			"text_mesh", "Render/Text 3D", FontResource::TYPE, *Reflection::getProperty(TEXT_MESH_TYPE, "Font"));

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
		m_render_stats_plugin = LUMIX_NEW(allocator, RenderStatsPlugin)(m_app);
		m_shader_editor_plugin = LUMIX_NEW(allocator, ShaderEditorPlugin)(m_app);
		m_app.addPlugin(*m_scene_view);
		m_app.addPlugin(*m_game_view);
		m_app.addPlugin(*m_editor_ui_render_plugin);
		m_app.addPlugin(*m_render_stats_plugin);
		m_app.addPlugin(*m_shader_editor_plugin);

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
		m_app.removePlugin(*m_render_stats_plugin);
		m_app.removePlugin(*m_shader_editor_plugin);

		LUMIX_DELETE(allocator, m_scene_view);
		LUMIX_DELETE(allocator, m_game_view);
		LUMIX_DELETE(allocator, m_editor_ui_render_plugin);
		LUMIX_DELETE(allocator, m_render_stats_plugin);
		LUMIX_DELETE(allocator, m_shader_editor_plugin);

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
	RenderStatsPlugin* m_render_stats_plugin;
	ShaderEditorPlugin* m_shader_editor_plugin;
	GizmoPlugin* m_gizmo_plugin;
};


LUMIX_STUDIO_ENTRY(renderer)
{
	auto& allocator = app.getWorldEditor().getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}
