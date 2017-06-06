#include "editor/asset_browser.h"
#include "editor/ieditor_command.h"
#include "editor/platform_interface.h"
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
#include "engine/input_system.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/lumix.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "game_view.h"
#include "import_asset_dialog.h"
#include "renderer/frame_buffer.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/model_manager.h"
#include "renderer/particle_system.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include "scene_view.h"
#include "shader_compiler.h"
#include "shader_editor.h"
#include "terrain_editor.h"
#include <SDL.h>
#include <cmath>
#include <cmft/cubemapfilter.h>
#include <crnlib.h>

using namespace Lumix;


static const ComponentType PARTICLE_EMITTER_TYPE = PropertyRegister::getComponentType("particle_emitter");
static const ComponentType TERRAIN_TYPE = PropertyRegister::getComponentType("terrain");
static const ComponentType CAMERA_TYPE = PropertyRegister::getComponentType("camera");
static const ComponentType DECAL_TYPE = PropertyRegister::getComponentType("decal");
static const ComponentType POINT_LIGHT_TYPE = PropertyRegister::getComponentType("point_light");
static const ComponentType GLOBAL_LIGHT_TYPE = PropertyRegister::getComponentType("global_light");
static const ComponentType MODEL_INSTANCE_TYPE = PropertyRegister::getComponentType("renderable");
static const ComponentType ENVIRONMENT_PROBE_TYPE = PropertyRegister::getComponentType("environment_probe");
static const ResourceType MATERIAL_TYPE("material");
static const ResourceType SHADER_TYPE("shader");
static const ResourceType TEXTURE_TYPE("texture");
static const ResourceType MODEL_TYPE("model");


struct MaterialPlugin LUMIX_FINAL : public AssetBrowser::IPlugin
{
	explicit MaterialPlugin(StudioApp& app)
		: m_app(app)
	{
	}


	bool acceptExtension(const char* ext, ResourceType type) const override
	{
		return type == MATERIAL_TYPE && equalStrings(ext, "mat");
	}


	void saveMaterial(Material* material)
	{
		FS::FileSystem& fs = m_app.getWorldEditor()->getEngine().getFileSystem();
		// use temporary because otherwise the material is reloaded during saving
		StaticString<MAX_PATH_LENGTH> tmp_path(material->getPath().c_str(), ".tmp");
		FS::IFile* file = fs.open(fs.getDefaultDevice(), Path(tmp_path), FS::Mode::CREATE_AND_WRITE);
		if (!file)
		{
			g_log_error.log("Editor") << "Could not save file " << material->getPath().c_str();
			return;
		}

		auto& allocator = m_app.getWorldEditor()->getAllocator();
		JsonSerializer serializer(*file, JsonSerializer::AccessMode::WRITE, material->getPath(), allocator);
		if (!material->save(serializer))
		{
			g_log_error.log("Editor") << "Could not save file " << material->getPath().c_str();
			fs.close(*file);
			return;
		}
		fs.close(*file);

		auto& engine = m_app.getWorldEditor()->getEngine();
		StaticString<MAX_PATH_LENGTH> src_full_path;
		StaticString<MAX_PATH_LENGTH> dest_full_path;
		if (engine.getPatchFileDevice())
		{
			src_full_path << engine.getPatchFileDevice()->getBasePath() << tmp_path;
			dest_full_path << engine.getPatchFileDevice()->getBasePath() << material->getPath().c_str();
		}
		if (!engine.getPatchFileDevice() || !PlatformInterface::fileExists(src_full_path))
		{
			src_full_path.data[0] = 0;
			dest_full_path.data[0] = 0;
			src_full_path << engine.getDiskFileDevice()->getBasePath() << tmp_path;
			dest_full_path << engine.getDiskFileDevice()->getBasePath() << material->getPath().c_str();
		}

		PlatformInterface::deleteFile(dest_full_path);

		if (!PlatformInterface::moveFile(src_full_path, dest_full_path))
		{
			g_log_error.log("Editor") << "Could not save file " << material->getPath().c_str();
		}
	}


	bool onGUI(Resource* resource, ResourceType type) override
	{
		if (type != MATERIAL_TYPE) return false;

		auto* material = static_cast<Material*>(resource);

		if (ImGui::Button("Save")) saveMaterial(material);
		ImGui::SameLine();
		if (ImGui::Button("Open in external editor")) m_app.getAssetBrowser()->openInExternalEditor(material);

		bool b;
		auto* plugin = m_app.getWorldEditor()->getEngine().getPluginManager().getPlugin("renderer");
		auto* renderer = static_cast<Renderer*>(plugin);

		int alpha_cutout_define = renderer->getShaderDefineIdx("ALPHA_CUTOUT");
		
		int render_layer = material->getRenderLayer();
		auto getter = [](void* data, int idx, const char** out) -> bool {
			auto* renderer = (Renderer*)data;
			*out = renderer->getLayerName(idx);
			return true;
		};
		if (ImGui::Combo("Render Layer", &render_layer, getter, renderer, renderer->getLayersCount()))
		{
			material->setRenderLayer(render_layer);
		}

		b = material->isBackfaceCulling();
		if (ImGui::Checkbox("Backface culling", &b)) material->enableBackfaceCulling(b);

		if (material->hasDefine(alpha_cutout_define))
		{
			b = material->isDefined(alpha_cutout_define);
			if (ImGui::Checkbox("Is alpha cutout", &b)) material->setDefine(alpha_cutout_define, b);
			if(b)
			{
				float tmp = material->getAlphaRef();
				if(ImGui::DragFloat("Alpha reference value", &tmp, 0.01f, 0.0f, 1.0f))
				{
					material->setAlphaRef(tmp);
				}
			}
		}

		Vec3 color = material->getColor();
		if (ImGui::ColorEdit3("Color", &color.x))
		{
			material->setColor(color);
		}
		if (ImGui::BeginPopupContextItem("color_pu"))
		{
			if(ImGui::ColorPicker(&color.x, false))
			{
				material->setColor(color);
			}
			ImGui::EndPopup();
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

		char buf[MAX_PATH_LENGTH];
		copyString(buf, material->getShader() ? material->getShader()->getPath().c_str() : "");
		if (m_app.getAssetBrowser()->resourceInput("Shader", "shader", buf, sizeof(buf), SHADER_TYPE))
		{
			material->setShader(Path(buf));
		}

		for (int i = 0; i < material->getShader()->m_texture_slot_count; ++i)
		{
			auto& slot = material->getShader()->m_texture_slots[i];
			auto* texture = material->getTexture(i);
			copyString(buf, texture ? texture->getPath().c_str() : "");
			if (m_app.getAssetBrowser()->resourceInput(
					slot.name, StaticString<30>("", (u64)&slot), buf, sizeof(buf), TEXTURE_TYPE))
			{
				material->setTexturePath(i, Path(buf));
			}
			if (!texture) continue;

			ImGui::SameLine();
			StaticString<50> popup_name("pu", (u64)texture, slot.name);
			StaticString<50> label("Advanced###adv", (u64)texture, slot.name);
			if (ImGui::Button(label)) ImGui::OpenPopup(popup_name);

			if (ImGui::BeginPopup(popup_name))
			{
				static const struct { const char* name; u32 value; u32 unset_flag; } FLAGS[] = {
					{"SRGB", BGFX_TEXTURE_SRGB, 0},
					{"u clamp", BGFX_TEXTURE_U_CLAMP, 0},
					{"v clamp", BGFX_TEXTURE_V_CLAMP, 0},
					{"Min point", BGFX_TEXTURE_MIN_POINT, BGFX_TEXTURE_MIN_ANISOTROPIC},
					{"Mag point", BGFX_TEXTURE_MAG_POINT, BGFX_TEXTURE_MAG_ANISOTROPIC},
					{"Min anisotropic", BGFX_TEXTURE_MIN_ANISOTROPIC, BGFX_TEXTURE_MIN_POINT},
					{"Mag anisotropic", BGFX_TEXTURE_MAG_ANISOTROPIC, BGFX_TEXTURE_MAG_POINT}};

				for (auto& flag : FLAGS)
				{
					bool b = (texture->bgfx_flags & flag.value) != 0;
					if (ImGui::Checkbox(flag.name, &b))
					{
						ImGui::CloseCurrentPopup();
						if (flag.unset_flag)
						{
							texture->setFlag(flag.unset_flag, false);
						}
						texture->setFlag(flag.value, b);
					}
				}

				ImGui::EndPopup();
			}
		}

		auto* shader = material->getShader();
		if(shader && material->isReady())
		{
			for(int i = 0; i < shader->m_uniforms.size(); ++i)
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
						if (ImGui::BeginPopupContextItem(StaticString<40>(shader_uniform.name, "pu")))
						{
							if (ImGui::ColorPicker(uniform.vec3, false)) material->createCommandBuffer();
							ImGui::EndPopup();
						}
						break;
					case Shader::Uniform::TIME: break;
					default: ASSERT(false); break;
				}
			}

			int layers_count = material->getLayersCount();
			if (ImGui::DragInt("Layers count", &layers_count, 1, 0, 256))
			{
				material->setLayersCount(layers_count);
			}
			
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
			}

			if (Material::getCustomFlagCount() > 0 && ImGui::CollapsingHeader("Flags"))
			{
				for (int i = 0; i < Material::getCustomFlagCount(); ++i)
				{
					bool b = material->isCustomFlag(1 << i);
					if (ImGui::Checkbox(Material::getCustomFlagName(i), &b))
					{
						if (b) material->setCustomFlag(1 << i);
						else material->unsetCustomFlag(1 << i);
					}
				}
			}
		}
		return true;
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Material"; }
	bool hasResourceManager(ResourceType type) const override { return type == MATERIAL_TYPE; }


	ResourceType getResourceType(const char* ext) override
	{
		return equalStrings(ext, "mat") ? MATERIAL_TYPE : INVALID_RESOURCE_TYPE;
	}


	StudioApp& m_app;
};


struct ModelPlugin LUMIX_FINAL : public AssetBrowser::IPlugin
{
	explicit ModelPlugin(StudioApp& app)
		: m_app(app)
	{
		m_camera_cmp = INVALID_COMPONENT;
		m_camera_entity = INVALID_ENTITY;
		m_mesh = INVALID_COMPONENT;
		m_pipeline = nullptr;
		m_universe = nullptr;
		m_is_mouse_captured = false;

		createPreviewUniverse();
	}


	~ModelPlugin()
	{
		auto& engine = m_app.getWorldEditor()->getEngine();
		engine.destroyUniverse(*m_universe);
		Pipeline::destroy(m_pipeline);
	}


	bool acceptExtension(const char* ext, ResourceType type) const override
	{
		return type == MODEL_TYPE && equalStrings(ext, "msh");
	}


	void createPreviewUniverse()
	{
		auto& engine = m_app.getWorldEditor()->getEngine();
		m_universe = &engine.createUniverse(false);
		auto* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));
		m_pipeline = Pipeline::create(*renderer, Path("pipelines/main.lua"), engine.getAllocator());
		m_pipeline->load();

		auto mesh_entity = m_universe->createEntity({0, 0, 0}, {0, 0, 0, 1});
		auto* render_scene = static_cast<RenderScene*>(m_universe->getScene(MODEL_INSTANCE_TYPE));
		m_mesh = render_scene->createComponent(MODEL_INSTANCE_TYPE, mesh_entity);

		auto light_entity = m_universe->createEntity({0, 0, 0}, {0, 0, 0, 1});
		auto light_cmp = render_scene->createComponent(GLOBAL_LIGHT_TYPE, light_entity);
		render_scene->setGlobalLightIntensity(light_cmp, 1);

		m_camera_entity = m_universe->createEntity({0, 0, 0}, {0, 0, 0, 1});
		m_camera_cmp = render_scene->createComponent(CAMERA_TYPE, m_camera_entity);
		render_scene->setCameraSlot(m_camera_cmp, "editor");

		m_pipeline->setScene(render_scene);
	}


	void showPreview(Model& model)
	{
		auto& engine = m_app.getWorldEditor()->getEngine();
		auto* render_scene = static_cast<RenderScene*>(m_universe->getScene(MODEL_INSTANCE_TYPE));
		if (!render_scene) return;
		if (!model.isReady()) return;

		if (render_scene->getModelInstanceModel(m_mesh) != &model)
		{
			render_scene->setModelInstancePath(m_mesh, model.getPath());
			AABB aabb = model.getAABB();

			m_universe->setRotation(m_camera_entity, {0, 0, 0, 1});
			m_universe->setPosition(m_camera_entity,
				{(aabb.max.x + aabb.min.x) * 0.5f,
					(aabb.max.y + aabb.min.y) * 0.5f,
					aabb.max.z + aabb.max.x - aabb.min.x});
		}
		ImVec2 image_size(ImGui::GetContentRegionAvailWidth(), ImGui::GetContentRegionAvailWidth());

		m_pipeline->setViewport(0, 0, (int)image_size.x, (int)image_size.y);
		m_pipeline->render();

		auto content_min = ImGui::GetCursorScreenPos();
		ImVec2 content_max(content_min.x + image_size.x, content_min.y + image_size.y);
		ImGui::Image(&m_pipeline->getFramebuffer("default")->getRenderbuffer(0).m_handle, image_size);
		bool mouse_down = ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1);
		if (m_is_mouse_captured && !mouse_down)
		{
			m_is_mouse_captured = false;
			SDL_ShowCursor(1);
			SDL_SetRelativeMouseMode(SDL_FALSE);
			SDL_WarpMouseInWindow(nullptr, m_captured_mouse_x, m_captured_mouse_y);
		}
		
		if (ImGui::IsItemHovered() && mouse_down)
		{
			auto& input = engine.getInputSystem();
			auto delta = Vec2(input.getMouseXMove(), input.getMouseYMove());

			if (!m_is_mouse_captured)
			{
				m_is_mouse_captured = true;
				SDL_ShowCursor(0);
				SDL_SetRelativeMouseMode(SDL_TRUE);
				SDL_GetMouseState(&m_captured_mouse_x, &m_captured_mouse_y);
			}

			if (delta.x != 0 || delta.y != 0)
			{
				const Vec2 MOUSE_SENSITIVITY(50, 50);
				Vec3 pos = m_universe->getPosition(m_camera_entity);
				Quat rot = m_universe->getRotation(m_camera_entity);
				Quat old_rot = rot;

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

				float dist = (origin - pos).length();
				pos = origin + dir * dist;

				m_universe->setRotation(m_camera_entity, rot);
				m_universe->setPosition(m_camera_entity, pos);
			}

		}
	}


	bool onGUI(Resource* resource, ResourceType type) override
	{
		if (type != MODEL_TYPE) return false;

		auto* model = static_cast<Model*>(resource);

		showPreview(*model);

		ImGui::LabelText("Bone count", "%d", model->getBoneCount());
		if (model->getBoneCount() > 0 && ImGui::CollapsingHeader("Bones"))
		{
			ImGui::Columns(3);
			for (int i = 0; i < model->getBoneCount(); ++i)
			{
				ImGui::Text("%s", model->getBone(i).name.c_str());
				ImGui::NextColumn();
				auto pos = model->getBone(i).transform.pos;
				ImGui::Text("%f; %f; %f", pos.x, pos.y, pos.z);
				ImGui::NextColumn();
				auto rot = model->getBone(i).transform.rot;
				ImGui::Text("%f; %f; %f; %f", rot.x, rot.y, rot.z, rot.w);
				ImGui::NextColumn();
			}
		}

		ImGui::LabelText("Bounding radius", "%f", model->getBoundingRadius());

		auto* lods = model->getLODs();
		if (lods[0].to_mesh >= 0 && !model->isFailure())
		{
			ImGui::Separator();
			ImGui::Columns(4);
			ImGui::Text("LOD"); ImGui::NextColumn();
			ImGui::Text("Distance"); ImGui::NextColumn();
			ImGui::Text("# of meshes"); ImGui::NextColumn();
			ImGui::Text("# of triangles"); ImGui::NextColumn();
			ImGui::Separator();
			int lod_count = 1;
			bool is_infinite_lod = false;
			for (int i = 0; i < Model::MAX_LOD_COUNT && lods[i].to_mesh >= 0; ++i)
			{
				ImGui::PushID(i);
				ImGui::Text("%d", i); ImGui::NextColumn();
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
				ImGui::Text("%d", lods[i].to_mesh - lods[i].from_mesh + 1); ImGui::NextColumn();
				int tri_count = 0;
				for (int j = lods[i].from_mesh; j <= lods[i].to_mesh; ++j)
				{
					tri_count += model->getMesh(j).indices_count / 3;
				}

				ImGui::Text("%d", tri_count); ImGui::NextColumn();
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
				ImGui::LabelText("Triangle count", "%d", mesh.indices_count / 3);
				ImGui::LabelText("Material", "%s", mesh.material->getPath().c_str());
				ImGui::SameLine();
				if (ImGui::Button("->"))
				{
					m_app.getAssetBrowser()->selectResource(mesh.material->getPath(), true);
				}
				ImGui::TreePop();
			}
		}

		return true;
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Model"; }
	bool hasResourceManager(ResourceType type) const override { return type == MODEL_TYPE; }
	
	
	ResourceType getResourceType(const char* ext) override
	{
		return equalStrings(ext, "msh") ? MODEL_TYPE : INVALID_RESOURCE_TYPE;
	}


	StudioApp& m_app;
	Universe* m_universe;
	Pipeline* m_pipeline;
	ComponentHandle m_mesh;
	Entity m_camera_entity;
	ComponentHandle m_camera_cmp;
	bool m_is_mouse_captured;
	int m_captured_mouse_x;
	int m_captured_mouse_y;
};


struct TexturePlugin LUMIX_FINAL : public AssetBrowser::IPlugin
{
	explicit TexturePlugin(StudioApp& app)
		: m_app(app)
	{
	}


	bool acceptExtension(const char* ext, ResourceType type) const override { return false; }


	bool onGUI(Resource* resource, ResourceType type) override
	{
		if (type != TEXTURE_TYPE) return false;

		auto* texture = static_cast<Texture*>(resource);
		if (texture->isFailure())
		{
			ImGui::Text("Texture failed to load.");
			return true;
		}

		ImGui::LabelText("Size", "%dx%d", texture->width, texture->height);
		ImGui::LabelText("Mips", "%d", texture->mips);
		if (texture->bytes_per_pixel > 0) ImGui::LabelText("BPP", "%d", texture->bytes_per_pixel);
		if (texture->is_cubemap)
		{
			ImGui::Text("Cubemap");
			return true;
		}

		if (bgfx::isValid(texture->handle))
		{
			ImVec2 texture_size(200, 200);
			if (texture->width > texture->height)
			{
				texture_size.y = texture_size.x * texture->height / texture->width;
			}
			else
			{
				texture_size.x = texture_size.y * texture->width / texture->height;
			}

			ImGui::Image(&texture->handle, texture_size);
		
			if (ImGui::Button("Open")) m_app.getAssetBrowser()->openInExternalEditor(resource);
		}
		return true;
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Texture"; }
	bool hasResourceManager(ResourceType type) const override { return type == TEXTURE_TYPE; }


	ResourceType getResourceType(const char* ext) override
	{
		if (equalStrings(ext, "tga")) return TEXTURE_TYPE;
		if (equalStrings(ext, "dds")) return TEXTURE_TYPE;
		if (equalStrings(ext, "raw")) return TEXTURE_TYPE;
		return INVALID_RESOURCE_TYPE;
	}

	StudioApp& m_app;
};


struct ShaderPlugin LUMIX_FINAL : public AssetBrowser::IPlugin
{
	explicit ShaderPlugin(StudioApp& app)
		: m_app(app)
	{
	}


	bool acceptExtension(const char* ext, ResourceType type) const override
	{
		return type == SHADER_TYPE && equalStrings("shd", ext);
	}


	bool onGUI(Resource* resource, ResourceType type) override
	{
		if (type != SHADER_TYPE) return false;

		auto* shader = static_cast<Shader*>(resource);
		char basename[MAX_PATH_LENGTH];
		PathUtils::getBasename(basename, lengthOf(basename), resource->getPath().c_str());
		StaticString<MAX_PATH_LENGTH> path("/pipelines/", basename, "/", basename);
		if (ImGui::Button("Open vertex shader"))
		{
			path << "_vs.sc";
			m_app.getAssetBrowser()->openInExternalEditor(path);
		}
		ImGui::SameLine();
		if (ImGui::Button("Open fragment shader"))
		{
			path << "_fs.sc";
			m_app.getAssetBrowser()->openInExternalEditor(path);
		}

		if (shader->m_texture_slot_count > 0 &&
			ImGui::CollapsingHeader(
				"Texture slots", nullptr, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
		{
			ImGui::Columns(2);
			ImGui::Text("name");
			ImGui::NextColumn();
			ImGui::Text("uniform");
			ImGui::NextColumn();
			ImGui::Separator();
			for (int i = 0; i < shader->m_texture_slot_count; ++i)
			{
				auto& slot = shader->m_texture_slots[i];
				ImGui::Text("%s", slot.name);
				ImGui::NextColumn();
				ImGui::Text("%s", slot.uniform);
				ImGui::NextColumn();
			}
			ImGui::Columns(1);
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
					case Shader::Uniform::VEC3: ImGui::Text("Vector3"); break;
					case Shader::Uniform::VEC2: ImGui::Text("Vector2"); break;
					default: ASSERT(false); break;
				}
				ImGui::NextColumn();
			}
			ImGui::Columns(1);
		}
		return true;
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Shader"; }
	bool hasResourceManager(ResourceType type) const override { return type == SHADER_TYPE; }
	
	
	ResourceType getResourceType(const char* ext) override
	{
		return equalStrings(ext, "shd") ? SHADER_TYPE : INVALID_RESOURCE_TYPE;
	}


	StudioApp& m_app;
};


struct EnvironmentProbePlugin LUMIX_FINAL : public PropertyGrid::IPlugin
{
	explicit EnvironmentProbePlugin(StudioApp& app)
		: m_app(app)
	{
		auto* world_editor = app.getWorldEditor();
		auto& plugin_manager = world_editor->getEngine().getPluginManager();
		Renderer*  renderer = static_cast<Renderer*>(plugin_manager.getPlugin("renderer"));
		auto& allocator = world_editor->getAllocator();
		Path pipeline_path("pipelines/probe.lua");
		m_pipeline = Pipeline::create(*renderer, pipeline_path, allocator);
		m_pipeline->load();
	}


	~EnvironmentProbePlugin()
	{
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
		comp_params.m_num_helper_threads = 3;
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
		const char* base_path = m_app.getWorldEditor()->getEngine().getDiskFileDevice()->getBasePath();
		StaticString<MAX_PATH_LENGTH> path(base_path, "universes/", m_app.getWorldEditor()->getUniverse()->getName());
		if (!PlatformInterface::makePath(path) && !PlatformInterface::dirExists(path))
		{
			g_log_error.log("Editor") << "Failed to create " << path;
		}
		path << "/probes/";
		if (!PlatformInterface::makePath(path) && !PlatformInterface::dirExists(path))
		{
			g_log_error.log("Editor") << "Failed to create " << path;
		}
		u64 probe_guid = ((RenderScene*)cmp.scene)->getEnvironmentProbeGUID(cmp.handle);
		path << probe_guid << postfix << ".dds";
		auto& allocator = m_app.getWorldEditor()->getAllocator();
		if (!file.open(path, FS::Mode::CREATE_AND_WRITE, allocator))
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
		static const int TEXTURE_SIZE = 1024;

		Universe* universe = m_app.getWorldEditor()->getUniverse();
		if (universe->getName()[0] == '\0')
		{
			g_log_error.log("Editor") << "Universe must be saved before environment probe can be generated.";
			return;
		}

		WorldEditor* world_editor = m_app.getWorldEditor();
		Engine& engine = world_editor->getEngine();
		auto& plugin_manager = engine.getPluginManager();
		IAllocator& allocator = engine.getAllocator();

		Vec3 probe_position = universe->getPosition(cmp.entity);
		auto* scene = static_cast<RenderScene*>(universe->getScene(CAMERA_TYPE));
		ComponentHandle camera_cmp = scene->getCameraInSlot("probe");
		if (!camera_cmp.isValid()) return;

		Entity camera_entity = scene->getCameraEntity(camera_cmp);
		scene->setCameraFOV(camera_cmp, Math::degreesToRadians(90));

		m_pipeline->setScene(scene);
		m_pipeline->setViewport(0, 0, TEXTURE_SIZE, TEXTURE_SIZE);

		Renderer* renderer = static_cast<Renderer*>(plugin_manager.getPlugin("renderer"));

		Vec3 dirs[] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
		Vec3 ups[] = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, 1, 0}};
		Vec3 ups_opengl[] = { { 0, -1, 0 },{ 0, -1, 0 },{ 0, 0, 1 },{ 0, 0, -1 },{ 0, -1, 0 },{ 0, -1, 0 } };

		Array<u8> data(allocator);
		data.resize(6 * TEXTURE_SIZE * TEXTURE_SIZE * 4);
		bgfx::TextureHandle texture =
			bgfx::createTexture2D(TEXTURE_SIZE, TEXTURE_SIZE, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_READ_BACK);
		renderer->frame(false); // submit
		renderer->frame(false); // wait for gpu

		bool is_opengl = renderer->isOpenGL();
		for (int i = 0; i < 6; ++i)
		{
			Matrix mtx = Matrix::IDENTITY;
			mtx.setTranslation(probe_position);
			Vec3 side = crossProduct(is_opengl ? ups_opengl[i] : ups[i], dirs[i]);
			mtx.setZVector(dirs[i]);
			mtx.setYVector(is_opengl ? ups_opengl[i] : ups[i]);
			mtx.setXVector(side);
			universe->setMatrix(camera_entity, mtx);
			m_pipeline->render();

			renderer->viewCounterAdd();
			bgfx::touch(renderer->getViewCounter());
			bgfx::setViewName(renderer->getViewCounter(), "probe_blit");
			auto* default_framebuffer = m_pipeline->getFramebuffer("default");
			bgfx::TextureHandle color_renderbuffer = default_framebuffer->getRenderbufferHandle(0);
			bgfx::blit(renderer->getViewCounter(), texture, 0, 0, color_renderbuffer);

			renderer->viewCounterAdd();
			bgfx::setViewName(renderer->getViewCounter(), "probe_read");
			bgfx::readTexture(texture, &data[i * TEXTURE_SIZE * TEXTURE_SIZE * 4]);
			bgfx::touch(renderer->getViewCounter());
			renderer->frame(false); // submit
			renderer->frame(false); // wait for gpu

			if (is_opengl) continue;

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
		cmft::Image image;
		cmft::Image irradiance;

		cmft::imageCreate(image, TEXTURE_SIZE, TEXTURE_SIZE, 0x303030ff, 1, 6, cmft::TextureFormat::RGBA8);
		cmft::imageFromRgba32f(image, cmft::TextureFormat::RGBA8);
		copyMemory(image.m_data, &data[0], data.size());
		cmft::imageToRgba32f(image);
		
		cmft::imageIrradianceFilterSh(irradiance, 32, image);

		cmft::imageRadianceFilter(
			image
			, 128
			, cmft::LightingModel::BlinnBrdf
			, false
			, 1
			, 10
			, 1
			, cmft::EdgeFixup::None
			, 0xff
		);

		cmft::imageFromRgba32f(image, cmft::TextureFormat::RGBA8);
		cmft::imageFromRgba32f(irradiance, cmft::TextureFormat::RGBA8);
		saveCubemap(cmp, (u8*)irradiance.m_data, 32, "_irradiance");
		saveCubemap(cmp, (u8*)image.m_data, 128, "_radiance");
		saveCubemap(cmp, &data[0], TEXTURE_SIZE, "");
		bgfx::destroyTexture(texture);
		
		scene->reloadEnvironmentProbe(cmp.handle);
	}


	void onGUI(PropertyGrid& grid, ComponentUID cmp) override
	{
		if (cmp.type != ENVIRONMENT_PROBE_TYPE) return;

		auto* scene = static_cast<RenderScene*>(cmp.scene);
		auto* texture = scene->getEnvironmentProbeTexture(cmp.handle);
		ImGui::LabelText("Path", "%s", texture->getPath().c_str());
		if (ImGui::Button("View")) m_app.getAssetBrowser()->selectResource(texture->getPath(), true);
		ImGui::SameLine();
		if (ImGui::Button("Generate")) generateCubemap(cmp);
	}


	StudioApp& m_app;
	Pipeline* m_pipeline;
};


struct EmitterPlugin LUMIX_FINAL : public PropertyGrid::IPlugin
{
	explicit EmitterPlugin(StudioApp& app)
		: m_app(app)
	{
		m_particle_emitter_updating = true;
		m_particle_emitter_timescale = 1.0f;
	}


	void onGUI(PropertyGrid& grid, ComponentUID cmp) override
	{
		if (cmp.type != PARTICLE_EMITTER_TYPE) return;
		
		ImGui::Separator();
		ImGui::Checkbox("Update", &m_particle_emitter_updating);
		auto* scene = static_cast<RenderScene*>(cmp.scene);
		ImGui::SameLine();
		if (ImGui::Button("Reset")) scene->resetParticleEmitter(cmp.handle);

		if (m_particle_emitter_updating)
		{
			ImGui::DragFloat("Timescale", &m_particle_emitter_timescale, 0.01f, 0.01f, 10000.0f);
			float time_delta = m_app.getWorldEditor()->getEngine().getLastTimeDelta();
			scene->updateEmitter(cmp.handle, time_delta * m_particle_emitter_timescale);
			scene->getParticleEmitter(cmp.handle)->drawGizmo(*m_app.getWorldEditor(), *scene);
		}
	}


	StudioApp& m_app;
	float m_particle_emitter_timescale;
	bool m_particle_emitter_updating;
};


struct TerrainPlugin LUMIX_FINAL : public PropertyGrid::IPlugin
{
	explicit TerrainPlugin(StudioApp& app)
		: m_app(app)
	{
		auto& editor = *app.getWorldEditor();
		m_terrain_editor = LUMIX_NEW(editor.getAllocator(), TerrainEditor)(editor, app);
	}


	~TerrainPlugin()
	{
		LUMIX_DELETE(m_app.getWorldEditor()->getAllocator(), m_terrain_editor);
	}


	void onGUI(PropertyGrid& grid, ComponentUID cmp) override
	{
		if (cmp.type != TERRAIN_TYPE) return;

		m_terrain_editor->setComponent(cmp);
		m_terrain_editor->onGUI();
	}


	StudioApp& m_app;
	TerrainEditor* m_terrain_editor;
};


struct SceneViewPlugin LUMIX_FINAL : public StudioApp::IPlugin
{
	struct RenderInterfaceImpl LUMIX_FINAL : public RenderInterface
	{
		ModelHandle loadModel(Path& path) override
		{
			auto& rm = m_editor.getEngine().getResourceManager();
			m_models.insert(m_model_index, static_cast<Model*>(rm.get(MODEL_TYPE)->load(path)));
			++m_model_index;
			return m_model_index - 1;
		}


		ImTextureID loadTexture(const Path& path) override
		{
			auto& rm = m_editor.getEngine().getResourceManager();
			auto* texture = static_cast<Texture*>(rm.get(TEXTURE_TYPE)->load(path));
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


		void addDebugCross(const Vec3& pos, float size, u32 color, float life) override
		{
			m_render_scene->addDebugCross(pos, size, color, life);
		}


		WorldEditor::RayHit castRay(const Vec3& origin, const Vec3& dir, ComponentHandle ignored) override
		{
			auto hit = m_render_scene->castRay(origin, dir, ignored);

			return {hit.m_is_hit, hit.m_t, hit.m_entity, hit.m_origin + hit.m_dir * hit.m_t};
		}


		void getRay(ComponentHandle camera_index, float x, float y, Vec3& origin, Vec3& dir) override
		{
			m_render_scene->getRay(camera_index, x, y, origin, dir);
		}


		void addDebugLine(const Vec3& from, const Vec3& to, u32 color, float life) override
		{
			m_render_scene->addDebugLine(from, to, color, life);
		}


		void addDebugCube(const Vec3& minimum, const Vec3& maximum, u32 color, float life) override
		{
			m_render_scene->addDebugCube(minimum, maximum, color, life);
		}


		AABB getEntityAABB(Universe& universe, Entity entity) override
		{
			AABB aabb;
			auto cmp = m_render_scene->getModelInstanceComponent(entity);
			if (cmp != INVALID_COMPONENT)
			{
				Model* model = m_render_scene->getModelInstanceModel(cmp);
				if (!model) return aabb;

				aabb = model->getAABB();
				aabb.transform(universe.getMatrix(entity));

				return aabb;
			}

			Vec3 pos = universe.getPosition(entity);
			aabb.set(pos, pos);

			return aabb;
		}


		void unloadModel(ModelHandle handle) override
		{
			auto* model = m_models[handle];
			model->getResourceManager().unload(*model);
			m_models.erase(handle);
		}


		void setCameraSlot(ComponentHandle cmp, const char* slot) override
		{
			m_render_scene->setCameraSlot(cmp, slot);
		}


		ComponentHandle getCameraInSlot(const char* slot) override
		{
			return m_render_scene->getCameraInSlot(slot);
		}


		Entity getCameraEntity(ComponentHandle cmp) override
		{
			return m_render_scene->getCameraEntity(cmp);
		}


		Vec2 getCameraScreenSize(ComponentHandle cmp) override
		{
			return m_render_scene->getCameraScreenSize(cmp);
		}


		float getCameraOrthoSize(ComponentHandle cmp) override
		{
			return m_render_scene->getCameraOrthoSize(cmp);
		}


		bool isCameraOrtho(ComponentHandle cmp) override
		{
			return m_render_scene->isCameraOrtho(cmp);
		}


		float getCameraFOV(ComponentHandle cmp) override
		{
			return m_render_scene->getCameraFOV(cmp);
		}


		float castRay(ModelHandle model, const Vec3& origin, const Vec3& dir, const Matrix& mtx, const Pose* pose) override
		{
			RayCastModelHit hit = m_models[model]->castRay(origin, dir, mtx, pose);
			return hit.m_is_hit ? hit.m_t : -1;
		}


		void renderModel(ModelHandle model, const Matrix& mtx) override
		{
			if (!m_pipeline.isReady() || !m_models[model]->isReady()) return;

			m_pipeline.renderModel(*m_models[model], mtx);
		}


		RenderInterfaceImpl(WorldEditor& editor, Pipeline& pipeline)
			: m_pipeline(pipeline)
			, m_editor(editor)
			, m_models(editor.getAllocator())
			, m_textures(editor.getAllocator())
		{
			m_model_index = -1;
			auto& rm = m_editor.getEngine().getResourceManager();
			Path shader_path("pipelines/editor/debugline.shd");
			m_shader = static_cast<Shader*>(rm.get(SHADER_TYPE)->load(shader_path));

			editor.universeCreated().bind<RenderInterfaceImpl, &RenderInterfaceImpl::onUniverseCreated>(this);
			editor.universeDestroyed().bind<RenderInterfaceImpl, &RenderInterfaceImpl::onUniverseDestroyed>(this);
		}


		~RenderInterfaceImpl()
		{
			auto& rm = m_editor.getEngine().getResourceManager();
			rm.get(SHADER_TYPE)->unload(*m_shader);

			m_editor.universeCreated().unbind<RenderInterfaceImpl, &RenderInterfaceImpl::onUniverseCreated>(this);
			m_editor.universeDestroyed().unbind<RenderInterfaceImpl, &RenderInterfaceImpl::onUniverseDestroyed>(this);
		}


		void onUniverseCreated()
		{
			m_render_scene = static_cast<RenderScene*>(m_editor.getUniverse()->getScene(MODEL_INSTANCE_TYPE));
		}


		void onUniverseDestroyed()
		{
			m_render_scene = nullptr;
		}


		Vec3 getModelCenter(Entity entity) override
		{
			auto cmp = m_render_scene->getModelInstanceComponent(entity);
			if (cmp == INVALID_COMPONENT) return Vec3(0, 0, 0);
			Model* model = m_render_scene->getModelInstanceModel(cmp);
			if (!model) return Vec3(0, 0, 0);
			return (model->getAABB().min + model->getAABB().max) * 0.5f;
		}


		void showEntity(Entity entity) override
		{
			ComponentHandle cmp = m_render_scene->getModelInstanceComponent(entity);
			if (cmp == INVALID_COMPONENT) return;
			m_render_scene->showModelInstance(cmp);
		}


		void hideEntity(Entity entity) override
		{
			ComponentHandle cmp = m_render_scene->getModelInstanceComponent(entity);
			if (cmp == INVALID_COMPONENT) return;
			m_render_scene->hideModelInstance(cmp);
		}


		Path getModelInstancePath(ComponentHandle cmp) override
		{
			return m_render_scene->getModelInstancePath(cmp);
		}


		void render(const Matrix& mtx,
			u16* indices,
			int indices_count,
			Vertex* vertices,
			int vertices_count,
			bool lines) override
		{
			if (!m_shader->isReady()) return;

			auto& renderer = static_cast<Renderer&>(m_render_scene->getPlugin());
			if (bgfx::getAvailTransientIndexBuffer(indices_count) < (u32)indices_count) return;
			if (bgfx::getAvailTransientVertexBuffer(vertices_count, renderer.getBasicVertexDecl()) < (u32)vertices_count) return;
			bgfx::TransientVertexBuffer vertex_buffer;
			bgfx::TransientIndexBuffer index_buffer;
			bgfx::allocTransientVertexBuffer(&vertex_buffer, vertices_count, renderer.getBasicVertexDecl());
			bgfx::allocTransientIndexBuffer(&index_buffer, indices_count);

			copyMemory(vertex_buffer.data, vertices, vertices_count * renderer.getBasicVertexDecl().getStride());
			copyMemory(index_buffer.data, indices, indices_count * sizeof(u16));

			u64 flags = BGFX_STATE_DEPTH_TEST_LEQUAL;
			if (lines) flags |= BGFX_STATE_PT_LINES;
			m_pipeline.render(vertex_buffer,
				index_buffer,
				mtx,
				0,
				indices_count,
				flags,
				m_shader->getInstance(0));
		}


		WorldEditor& m_editor;
		Shader* m_shader;
		RenderScene* m_render_scene;
		Pipeline& m_pipeline;
		HashMap<int, Model*> m_models;
		HashMap<void*, Texture*> m_textures;
		int m_model_index;
	};


	explicit SceneViewPlugin(StudioApp& app)
		: m_app(app)
		, m_scene_view(app)
	{
		auto& editor = *app.getWorldEditor();
		auto& allocator = editor.getAllocator();
		Action* action = LUMIX_NEW(allocator, Action)("Scene View", "scene_view");
		action->func.bind<SceneViewPlugin, &SceneViewPlugin::onAction>(this);
		app.addWindowAction(action);
		m_render_interface = LUMIX_NEW(allocator, RenderInterfaceImpl)(editor, *m_scene_view.getPipeline());
		editor.setRenderInterface(m_render_interface);
		m_app.getAssetBrowser()->resourceChanged().bind<SceneViewPlugin, &SceneViewPlugin::onResourceChanged>(this);
	}


	~SceneViewPlugin()
	{
		m_app.getAssetBrowser()->resourceChanged().unbind<SceneViewPlugin, &SceneViewPlugin::onResourceChanged>(this);
		m_scene_view.shutdown();
	}


	const char* getName() const override { return "scene_view"; }


	void onResourceChanged(const Path& path, const char* /*ext*/)
	{
		if (m_scene_view.getPipeline()->getPath() == path) m_scene_view.getPipeline()->load();
	}


	void update(float) override
	{
		m_scene_view.update();
		if(&m_render_interface->m_pipeline == m_scene_view.getPipeline()) return;

		auto& editor = *m_app.getWorldEditor();
		auto& allocator = editor.getAllocator();
		editor.setRenderInterface(nullptr);
		LUMIX_DELETE(allocator, m_render_interface);
		m_render_interface = LUMIX_NEW(allocator, RenderInterfaceImpl)(editor, *m_scene_view.getPipeline());
		editor.setRenderInterface(m_render_interface);
	}


	void onAction() {}
	void onWindowGUI() override { m_scene_view.onGUI(); }

	StudioApp& m_app;
	SceneView m_scene_view;
	RenderInterfaceImpl* m_render_interface;
};


struct FurPainter LUMIX_FINAL : public WorldEditor::Plugin
{
	FurPainter(StudioApp& _app)
		: app(_app)
		, brush_radius(0.1f)
		, brush_strength(1.0f)
		, enabled(false)
	{
		app.getWorldEditor()->addPlugin(*this);
	}


	void saveTexture()
	{
		WorldEditor& editor = *app.getWorldEditor();
		auto& entities = editor.getSelectedEntities();
		if (entities.empty()) return;

		ComponentUID model_instance = editor.getUniverse()->getComponent(entities[0], MODEL_INSTANCE_TYPE);
		if (!model_instance.isValid()) return;

		RenderScene* scene = static_cast<RenderScene*>(model_instance.scene);
		Model* model = scene->getModelInstanceModel(model_instance.handle);

		if (!model || !model->isReady()) return;

		Texture* texture = model->getMesh(0).material->getTexture(0);
		texture->save();
	}


	struct Vertex
	{
		Vec2 uv;
		Vec3 pos;

		void fixUV(int w, int h)
		{
			if (uv.y < 0) uv.y = 1 + uv.y;
			uv.x *= (float)w;
			uv.y *= (float)h;
		}
	};


	struct Point
	{
		i64 x, y;
	};


	static i64 orient2D(const Point& a, const Point& b, const Point& c)
	{
		return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
	}


	void postprocess()
	{
		WorldEditor& editor = *app.getWorldEditor();
		Universe* universe = editor.getUniverse();
		auto& entities = editor.getSelectedEntities();
		if (entities.empty()) return;

		ComponentUID model_instance = universe->getComponent(entities[0], MODEL_INSTANCE_TYPE);
		if (!model_instance.isValid()) return;

		RenderScene* scene = static_cast<RenderScene*>(model_instance.scene);
		Model* model = scene->getModelInstanceModel(model_instance.handle);

		if (!model || !model->isReady() || model->getMeshCount() < 1) return;
		if (!model->getMesh(0).material) return;

		Texture* texture = model->getMesh(0).material->getTexture(0);
		if (!texture || texture->data.empty()) return;

		u8* mem = (u8*)app.getWorldEditor()->getAllocator().allocate(texture->width * texture->height);

		ASSERT(!texture->data.empty());

		const u16* idx16 = model->getIndices16();
		const u32* idx32 = model->getIndices32();
		const Vec3* vertices = &model->getVertices()[0];
		setMemory(mem, 0, texture->width * texture->height);
		for (int i = 0, c = model->getIndicesCount(); i < c; i += 3)
		{
			u32 idx[3];
			if (idx16)
			{
				idx[0] = idx16[i];
				idx[1] = idx16[i + 1];
				idx[2] = idx16[i + 2];
			}
			else
			{
				idx[0] = idx32[i];
				idx[1] = idx32[i + 1];
				idx[2] = idx32[i + 2];
			}

			Vertex v[] = { { model->getUVs()[idx[0]], vertices[idx[0]] },
			{ model->getUVs()[idx[1]], vertices[idx[1]] },
			{ model->getUVs()[idx[2]], vertices[idx[2]] } };

			Vec3 n = crossProduct(Vec3(v[0].uv, 0) - Vec3(v[1].uv, 0), Vec3(v[2].uv, 0) - Vec3(v[1].uv, 0));
			if (n.z > 0) Math::swap(v[1], v[2]);

			v[0].fixUV(texture->width, texture->height);
			v[1].fixUV(texture->width, texture->height);
			v[2].fixUV(texture->width, texture->height);

			rasterizeTriangle2(texture->width, mem, v);
		}

		u32* data = (u32*)&texture->data[0];
		struct DistanceFieldCell
		{
			u32 distance;
			u32 color;
		};

		Array<DistanceFieldCell> distance_field(app.getWorldEditor()->getAllocator());
		int width = texture->width;
		int height = texture->height;
		distance_field.resize(width * height);

		for (int j = 0; j < height; ++j)
		{
			for (int i = 0; i < width; ++i)
			{
				distance_field[i + j * width].color = data[i + j * width];
				distance_field[i + j * width].distance = 0xffffFFFF;
			}
		}

		for (int j = 1; j < height; ++j)
		{
			for (int i = 1; i < width; ++i)
			{
				int idx = i + j * width;
				if (mem[idx])
				{
					distance_field[idx].distance = 0;
				}
				else
				{
					if (distance_field[idx - 1].distance < distance_field[idx - width].distance)
					{
						distance_field[idx].distance = distance_field[idx - 1].distance + 1;
						distance_field[idx].color = distance_field[idx - 1].color;
					}
					else
					{
						distance_field[idx].distance = distance_field[idx - width].distance + 1;
						distance_field[idx].color = distance_field[idx - width].color;
					}
				}
			}
		}

		for (int j = height - 2; j >= 0; --j)
		{
			for (int i = width - 2; i >= 0; --i)
			{
				int idx = i + j * width;
				if (distance_field[idx + 1].distance < distance_field[idx + width].distance &&
					distance_field[idx + 1].distance < distance_field[idx].distance)
				{
					distance_field[idx].distance = distance_field[idx + 1].distance + 1;
					distance_field[idx].color = distance_field[idx + 1].color;
				}
				else if (distance_field[idx + width].distance < distance_field[idx].distance)
				{
					distance_field[idx].distance = distance_field[idx + width].distance + 1;
					distance_field[idx].color = distance_field[idx + width].color;
				}
			}
		}

		for (int j = 0; j < height; ++j)
		{
			for (int i = 0; i < width; ++i)
			{
				data[i + j * width] = distance_field[i + j*width].color;
			}
		}

		texture->onDataUpdated(0, 0, texture->width, texture->height);
		app.getWorldEditor()->getAllocator().deallocate(mem);
	}


	void rasterizeTriangle2(int width, u8* mem, Vertex v[3]) const
	{
		float squared_radius_rcp = 1.0f / (brush_radius * brush_radius);

		static const i64 substep = 256;
		static const i64 submask = substep - 1;
		static const i64 stepshift = 8;

		Point v0 = { i64(v[0].uv.x * substep), i64(v[0].uv.y * substep) };
		Point v1 = { i64(v[1].uv.x * substep), i64(v[1].uv.y * substep) };
		Point v2 = { i64(v[2].uv.x * substep), i64(v[2].uv.y * substep) };

		i64 minX = Math::minimum(v0.x, v1.x, v2.x);
		i64 minY = Math::minimum(v0.y, v1.y, v2.y);
		i64 maxX = Math::maximum(v0.x, v1.x, v2.x) + substep;
		i64 maxY = Math::maximum(v0.y, v1.y, v2.y) + substep;

		minX = ((minX + submask) & ~submask) - 1;
		minY = ((minY + submask) & ~submask) - 1;

		Point p;
		for (p.y = minY; p.y <= maxY; p.y += substep)
		{
			for (p.x = minX; p.x <= maxX; p.x += substep)
			{
				i64 w0 = orient2D(v1, v2, p);
				i64 w1 = orient2D(v2, v0, p);
				i64 w2 = orient2D(v0, v1, p);

				if (w0 >= 0 && w1 >= 0 && w2 >= 0)
				{
					mem[(p.x >> stepshift) + (p.y >> stepshift) * width] = 1;
				}
			}
		}
	}


	void rasterizeTriangle(Texture* texture, Vertex v[3], const Vec3& center) const
	{
		float squared_radius_rcp = 1.0f / (brush_radius * brush_radius);

		static const i64 substep = 256;
		static const i64 submask = substep - 1;
		static const i64 stepshift = 8;

		Point v0 = {i64(v[0].uv.x * substep), i64(v[0].uv.y * substep)};
		Point v1 = {i64(v[1].uv.x * substep), i64(v[1].uv.y * substep)};
		Point v2 = {i64(v[2].uv.x * substep), i64(v[2].uv.y * substep)};

		i64 minX = Math::minimum(v0.x, v1.x, v2.x);
		i64 minY = Math::minimum(v0.y, v1.y, v2.y);
		i64 maxX = Math::maximum(v0.x, v1.x, v2.x) + substep;
		i64 maxY = Math::maximum(v0.y, v1.y, v2.y) + substep;

		minX = ((minX + submask) & ~submask) - 1;
		minY = ((minY + submask) & ~submask) - 1;

		Point p;
		for (p.y = minY; p.y <= maxY; p.y += substep)
		{
			for (p.x = minX; p.x <= maxX; p.x += substep)
			{
				i64 w0 = orient2D(v1, v2, p);
				i64 w1 = orient2D(v2, v0, p);
				i64 w2 = orient2D(v0, v1, p);

				if (w0 >= 0 && w1 >= 0 && w2 >= 0)
				{
					Vec3 pos =
						(float(w0) * v[0].pos + float(w1) * v[1].pos + float(w2) * v[2].pos) * (1.0f / (w0 + w1 + w2));
					float q = 1 - (center - pos).squaredLength() * squared_radius_rcp;
					if (q <= 0) continue;
						
					u32& val = ((u32*)&texture->data[0])[(p.x >> stepshift) + (p.y >> stepshift) * texture->width];
					float alpha = ((val & 0xff000000) >> 24) / 255.0f;
					alpha = brush_strength * q + alpha * (1 - q);
					val = val & 0x00ffFFFF | (u32)(alpha * 255.0f) << 24;
				}
			}
		}
	}


	void paint(Texture* texture, Model* model, const Vec3& hit) const
	{
		ASSERT(!texture->data.empty());

		const u16* idx16 = model->getIndices16();
		const u32* idx32 = model->getIndices32();
		const Vec3* vertices = &model->getVertices()[0];
		Vec2 min((float)texture->width, (float)texture->height);
		Vec2 max(0, 0);
		int tri_count = 0;
		for (int i = 0, c = model->getIndicesCount(); i < c; i += 3)
		{
			u32 idx[3];
			if (idx16)
			{
				idx[0] = idx16[i];
				idx[1] = idx16[i + 1];
				idx[2] = idx16[i + 2];
			}
			else
			{
				idx[0] = idx32[i];
				idx[1] = idx32[i + 1];
				idx[2] = idx32[i + 2];
			}

			if (Math::getSphereTriangleIntersection(
				hit, brush_radius, vertices[idx[0]], vertices[idx[1]], vertices[idx[2]]))
			{
				Vertex v[] = {{model->getUVs()[idx[0]], vertices[idx[0]]},
					{model->getUVs()[idx[1]], vertices[idx[1]]},
					{model->getUVs()[idx[2]], vertices[idx[2]]}};

				Vec3 n = crossProduct(Vec3(v[0].uv, 0) - Vec3(v[1].uv, 0), Vec3(v[2].uv, 0) - Vec3(v[1].uv, 0));
				if (n.z > 0) Math::swap(v[1], v[2]);

				v[0].fixUV(texture->width, texture->height);
				v[1].fixUV(texture->width, texture->height);
				v[2].fixUV(texture->width, texture->height);

				min.x = Math::minimum(min.x, v[0].uv.x, v[1].uv.x, v[2].uv.x);
				max.x = Math::maximum(max.x, v[0].uv.x, v[1].uv.x, v[2].uv.x);

				min.y = Math::minimum(min.y, v[0].uv.y, v[1].uv.y, v[2].uv.y);
				max.y = Math::maximum(max.y, v[0].uv.y, v[1].uv.y, v[2].uv.y);

				++tri_count;
				rasterizeTriangle(texture, v, hit);
			}
		}

		if (tri_count > 0) texture->onDataUpdated((int)min.x, (int)min.y, int(max.x - min.x), int(max.y - min.y));
	}


	bool onEntityMouseDown(const WorldEditor::RayHit& hit, int x, int y) override
	{
		auto& ents = app.getWorldEditor()->getSelectedEntities();
		
		if (enabled && ents.size() == 1 && ents[0] == hit.entity)
		{
			onMouseMove(x, y, 0, 0);
			return true;
		}
		return false;
	}


	void onMouseMove(int x, int y, int, int) override
	{
		WorldEditor& editor = *app.getWorldEditor();
		Universe* universe = editor.getUniverse();
		auto& entities = editor.getSelectedEntities();
		if (entities.empty()) return;
		if (!editor.isMouseDown(MouseButton::LEFT)) return;

		ComponentUID model_instance = universe->getComponent(entities[0], MODEL_INSTANCE_TYPE);
		if (!model_instance.isValid()) return;

		RenderScene* scene = static_cast<RenderScene*>(model_instance.scene);
		Model* model = scene->getModelInstanceModel(model_instance.handle);

		if (!model || !model->isReady() || model->getMeshCount() < 1) return;
		if (!model->getMesh(0).material) return;

		Texture* texture = model->getMesh(0).material->getTexture(0);
		if (!texture || texture->data.empty()) return;

		Pose* pose = scene->getPose(model_instance.handle);

		Vec3 origin, dir;
		scene->getRay(editor.getEditCamera().handle, (float)x, (float)y, origin, dir);
		RayCastModelHit hit = model->castRay(origin, dir, universe->getMatrix(entities[0]), pose);
		if (!hit.m_is_hit) return;

		Vec3 hit_pos = hit.m_origin + hit.m_t * hit.m_dir;
		hit_pos = universe->getTransform(entities[0]).inverted().transform(hit_pos);

		paint(texture, model, hit_pos);
	}


	float brush_radius;
	float brush_strength;
	StudioApp& app;
	bool enabled;
};


struct FurPainterPlugin LUMIX_FINAL : public StudioApp::IPlugin
{
	explicit FurPainterPlugin(StudioApp& _app)
		: app(_app)
		, is_opened(false)
	{
		fur_painter = LUMIX_NEW(app.getWorldEditor()->getAllocator(), FurPainter)(_app);
		Action* action = LUMIX_NEW(app.getWorldEditor()->getAllocator(), Action)("Fur Painter", "fur_painter");
		action->func.bind<FurPainterPlugin, &FurPainterPlugin::onAction>(this);
		action->is_selected.bind<FurPainterPlugin, &FurPainterPlugin::isOpened>(this);
		app.addWindowAction(action);
	}


	const char* getName() const override { return "fur_painter"; }


	bool isOpened() const { return is_opened; }
	void onAction() { is_opened = !is_opened; }


	void onWindowGUI() override
	{
		if (ImGui::BeginDock("Fur painter", &is_opened))
		{
			ImGui::Checkbox("Enabled", &fur_painter->enabled);
			if (!fur_painter->enabled) goto end;


			WorldEditor& editor = *app.getWorldEditor();
			const auto& entities = editor.getSelectedEntities();
			if (entities.empty())
			{
				ImGui::Text("No entity selected.");
				goto end;
			}
			Universe* universe = editor.getUniverse();
			RenderScene* scene = static_cast<RenderScene*>(universe->getScene(MODEL_INSTANCE_TYPE));
			ComponentUID model_instance = universe->getComponent(entities[0], MODEL_INSTANCE_TYPE);

			if (!model_instance.isValid())
			{
				ImGui::Text("Entity does not have model_instance component.");
				goto end;
			}

			Model* model = scene->getModelInstanceModel(model_instance.handle);
			if (!model)
			{
				ImGui::Text("Entity does not have model.");
				goto end;
			}

			if (model->isFailure())
			{
				ImGui::Text("Model failed to load.");
				goto end;
			}
			else if (model->isEmpty())
			{
				ImGui::Text("Model is not loaded.");
				goto end;
			}

			if(model->getMeshCount() < 1 || !model->getMesh(0).material)
			{
				ImGui::Text("Model file is invalid.");
				goto end;
			}

			Texture* texture = model->getMesh(0).material->getTexture(0);
			if (!texture)
			{
				ImGui::Text("Missing texture.");
				goto end;
			}

			if(!endsWith(texture->getPath().c_str(), ".tga"))
			{
				ImGui::Text("Only TGA can be painted");
				goto end;
			}

			if (texture->data.empty())
			{
				texture->addDataReference();
				texture->getResourceManager().reload(*texture);
				goto end;
			}

			ImGui::DragFloat("Brush radius", &fur_painter->brush_radius);
			ImGui::DragFloat("Brush strength", &fur_painter->brush_strength, 0.01f, 0.0f, 1.0f);
			if (ImGui::Button("Save texture")) fur_painter->saveTexture();
			ImGui::SameLine();
			if (ImGui::Button("Postprocess")) fur_painter->postprocess();

			drawGizmo();
		}
		
		end:
			ImGui::EndDock();
	}


	void drawGizmo()
	{
		if (!fur_painter->enabled) return;

		WorldEditor& editor = *app.getWorldEditor();
		auto& entities = editor.getSelectedEntities();
		if (entities.empty()) return;

		ComponentUID model_instance = editor.getUniverse()->getComponent(entities[0], MODEL_INSTANCE_TYPE);
		if (!model_instance.isValid()) return;

		RenderScene* scene = static_cast<RenderScene*>(model_instance.scene);
		Model* model = scene->getModelInstanceModel(model_instance.handle);

		if (!model || !model->isReady() || model->getMeshCount() < 1) return;
		if (!model->getMesh(0).material) return;

		Texture* texture = model->getMesh(0).material->getTexture(0);
		if (!texture || texture->data.empty()) return;

		const Pose* pose = scene->getPose(model_instance.handle);

		Vec3 origin, dir;
		scene->getRay(editor.getEditCamera().handle, editor.getMouseX(), editor.getMouseY(), origin, dir);
		RayCastModelHit hit = model->castRay(origin, dir, editor.getUniverse()->getMatrix(entities[0]), pose);
		if (!hit.m_is_hit) return;

		Vec3 hit_pos = hit.m_origin + hit.m_t * hit.m_dir;
		scene->addDebugSphere(hit_pos, fur_painter->brush_radius, 0xffffFFFF, 0);
	}


	FurPainter* fur_painter;
	bool is_opened;
	StudioApp& app;
};


struct GameViewPlugin LUMIX_FINAL : public StudioApp::IPlugin
{
	static GameViewPlugin* s_instance;


	explicit GameViewPlugin(StudioApp& app, SceneViewPlugin& scene_view_plugin)
		: m_app(app)
		, m_game_view(app)
		, m_scene_view(scene_view_plugin.m_scene_view)
		, m_width(-1)
		, m_height(-1)
	{
		auto& editor = *app.getWorldEditor();
		m_engine = &editor.getEngine();
		Action* action = LUMIX_NEW(editor.getAllocator(), Action)("Game View", "game_view");
		action->func.bind<GameViewPlugin, &GameViewPlugin::onAction>(this);
		action->is_selected.bind<GameViewPlugin, &GameViewPlugin::isOpened>(this);
		app.addWindowAction(action);
		m_game_view.m_is_opened = false;
		m_game_view.init(editor);

		auto& plugin_manager = editor.getEngine().getPluginManager();
		auto* renderer = static_cast<Renderer*>(plugin_manager.getPlugin("renderer"));
		Path path("pipelines/imgui/imgui.lua");
		m_gui_pipeline = Pipeline::create(*renderer, path, m_engine->getAllocator());
		m_gui_pipeline->load();

		int w, h;
		SDL_GetWindowSize(m_app.getWindow(), &w, &h);
		m_gui_pipeline->setViewport(0, 0, w, h);
		renderer->resize(w, h);
		onUniverseCreated();

		s_instance = this;

		unsigned char* pixels;
		int width, height;
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		auto* material_manager = m_engine->getResourceManager().get(MATERIAL_TYPE);
		auto* resource = material_manager->load(Path("pipelines/imgui/imgui.mat"));
		m_material = static_cast<Material*>(resource);

		Texture* old_texture = m_material->getTexture(0);
		Texture* texture = LUMIX_NEW(editor.getAllocator(), Texture)(
			Path("font"), *m_engine->getResourceManager().get(TEXTURE_TYPE), editor.getAllocator());

		texture->create(width, height, pixels);
		m_material->setTexture(0, texture);
		if (old_texture)
		{
			old_texture->destroy();
			LUMIX_DELETE(m_engine->getAllocator(), old_texture);
		}

		ImGui::GetIO().RenderDrawListsFn = imGuiCallback;

		editor.universeCreated().bind<GameViewPlugin, &GameViewPlugin::onUniverseCreated>(this);
		editor.universeDestroyed().bind<GameViewPlugin, &GameViewPlugin::onUniverseDestroyed>(this);
	}


	~GameViewPlugin()
	{
		Pipeline::destroy(m_gui_pipeline);
		auto& editor = *m_app.getWorldEditor();
		editor.universeCreated().unbind<GameViewPlugin, &GameViewPlugin::onUniverseCreated>(this);
		editor.universeDestroyed().unbind<GameViewPlugin, &GameViewPlugin::onUniverseDestroyed>(this);
		shutdownImGui();
		m_game_view.shutdown();
	}


	const char* getName() const override { return "game_view"; }


	bool isOpened() const { return m_game_view.m_is_opened; }


	void shutdownImGui()
	{
		ImGui::ShutdownDock();
		ImGui::Shutdown();

		Texture* texture = m_material->getTexture(0);
		m_material->setTexture(0, nullptr);
		texture->destroy();
		LUMIX_DELETE(m_app.getWorldEditor()->getAllocator(), texture);

		m_material->getResourceManager().unload(*m_material);
	}


	void draw(ImDrawData* draw_data)
	{
		if (!m_gui_pipeline->isReady()) return;
		if (!m_material || !m_material->isReady()) return;
		if (!m_material->getTexture(0)) return;

		int w, h;
		SDL_GetWindowSize(m_app.getWindow(), &w, &h);
		if (w != m_width || h != m_height)
		{
			m_width = w;
			m_height = h;
			auto& plugin_manager = m_app.getWorldEditor()->getEngine().getPluginManager();
			auto* renderer = static_cast<Renderer*>(plugin_manager.getPlugin("renderer"));
			if (renderer) renderer->resize(m_width, m_height);
		}

		m_gui_pipeline->render();
		setGUIProjection();

		for (int i = 0; i < draw_data->CmdListsCount; ++i)
		{
			ImDrawList* cmd_list = draw_data->CmdLists[i];
			drawGUICmdList(cmd_list);
		}

		Renderer* renderer = static_cast<Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));

		renderer->frame(false);
	}


	void onUniverseCreated()
	{
		auto* universe = m_app.getWorldEditor()->getUniverse();
		auto* scene = static_cast<RenderScene*>(universe->getScene(MODEL_INSTANCE_TYPE));

		m_gui_pipeline->setScene(scene);
	}


	void onUniverseDestroyed() { m_gui_pipeline->setScene(nullptr); }
	static void imGuiCallback(ImDrawData* draw_data) { s_instance->draw(draw_data); }


	void setGUIProjection()
	{
		float width = ImGui::GetIO().DisplaySize.x;
		float height = ImGui::GetIO().DisplaySize.y;
		Matrix ortho;
		bool is_opengl = bgfx::getRendererType() == bgfx::RendererType::OpenGL ||
						 bgfx::getRendererType() == bgfx::RendererType::OpenGLES;
		ortho.setOrtho(0.0f, width, height, 0.0f, -1.0f, 1.0f, is_opengl);
		m_gui_pipeline->setViewport(0, 0, (int)width, (int)height);
		m_gui_pipeline->setViewProjection(ortho, (int)width, (int)height);
	}


	void drawGUICmdList(ImDrawList* cmd_list)
	{
		Renderer* renderer = static_cast<Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));

		int num_indices = cmd_list->IdxBuffer.size();
		int num_vertices = cmd_list->VtxBuffer.size();
		auto& decl = renderer->getBasic2DVertexDecl();
		bgfx::TransientVertexBuffer vertex_buffer;
		bgfx::TransientIndexBuffer index_buffer;
		if (bgfx::getAvailTransientIndexBuffer(num_indices) < (u32)num_indices) return;
		if (bgfx::getAvailTransientVertexBuffer(num_vertices, decl) < (u32)num_vertices) return;
		bgfx::allocTransientVertexBuffer(&vertex_buffer, num_vertices, decl);
		bgfx::allocTransientIndexBuffer(&index_buffer, num_indices);

		copyMemory(vertex_buffer.data, &cmd_list->VtxBuffer[0], num_vertices * decl.getStride());
		copyMemory(index_buffer.data, &cmd_list->IdxBuffer[0], num_indices * sizeof(u16));

		u32 elem_offset = 0;
		const ImDrawCmd* pcmd_begin = cmd_list->CmdBuffer.begin();
		const ImDrawCmd* pcmd_end = cmd_list->CmdBuffer.end();
		for (const ImDrawCmd* pcmd = pcmd_begin; pcmd != pcmd_end; pcmd++)
		{
			if (pcmd->UserCallback)
			{
				pcmd->UserCallback(cmd_list, pcmd);
				elem_offset += pcmd->ElemCount;
				continue;
			}

			if (0 == pcmd->ElemCount) continue;

			m_gui_pipeline->setScissor(u16(Math::maximum(pcmd->ClipRect.x, 0.0f)),
				u16(Math::maximum(pcmd->ClipRect.y, 0.0f)),
				u16(Math::minimum(pcmd->ClipRect.z, 65535.0f) - Math::maximum(pcmd->ClipRect.x, 0.0f)),
				u16(Math::minimum(pcmd->ClipRect.w, 65535.0f) - Math::maximum(pcmd->ClipRect.y, 0.0f)));

			auto material = m_material;
			const auto& texture_id =
				pcmd->TextureId ? *(bgfx::TextureHandle*)pcmd->TextureId : material->getTexture(0)->handle;
			auto texture_uniform = material->getShader()->m_texture_slots[0].uniform_handle;
			u64 render_states = material->getRenderStates();
			if (&m_scene_view.getTextureHandle() == &texture_id)
			{
				render_states &= ~BGFX_STATE_BLEND_MASK;
			}
			m_gui_pipeline->setTexture(0, texture_id, texture_uniform);
			m_gui_pipeline->render(vertex_buffer,
				index_buffer,
				Matrix::IDENTITY,
				elem_offset,
				pcmd->ElemCount,
				render_states,
				material->getShaderInstance());

			elem_offset += pcmd->ElemCount;
		}
	}


	void onAction() { m_game_view.m_is_opened = !m_game_view.m_is_opened; }
	void onWindowGUI() override { m_game_view.onGUI(); }

	int m_width;
	int m_height;
	StudioApp& m_app;
	Engine* m_engine;
	Material* m_material;
	Pipeline* m_gui_pipeline;
	GameView m_game_view;
	SceneView& m_scene_view;
};


GameViewPlugin* GameViewPlugin::s_instance = nullptr;


struct ShaderEditorPlugin LUMIX_FINAL : public StudioApp::IPlugin
{
	explicit ShaderEditorPlugin(StudioApp& app)
		: m_shader_editor(app.getWorldEditor()->getAllocator())
		, m_app(app)
	{
		Action* action = LUMIX_NEW(app.getWorldEditor()->getAllocator(), Action)("Shader Editor", "shaderEditor");
		action->func.bind<ShaderEditorPlugin, &ShaderEditorPlugin::onAction>(this);
		action->is_selected.bind<ShaderEditorPlugin, &ShaderEditorPlugin::isOpened>(this);
		app.addWindowAction(action);
		m_shader_editor.m_is_opened = false;

		m_compiler = LUMIX_NEW(app.getWorldEditor()->getAllocator(), ShaderCompiler)(app, *app.getLogUI());

		lua_State* L = app.getWorldEditor()->getEngine().getState();
		auto* f =
			&LuaWrapper::wrapMethodClosure<ShaderCompiler, decltype(&ShaderCompiler::makeUpToDate), &ShaderCompiler::makeUpToDate>;
		LuaWrapper::createSystemClosure(L, "Editor", m_compiler, "compileShaders", f);
	}


	~ShaderEditorPlugin() { LUMIX_DELETE(m_app.getWorldEditor()->getAllocator(), m_compiler); }


	const char* getName() const override { return "shader_editor"; }
	void update(float) override { m_compiler->update(); }
	void onAction() { m_shader_editor.m_is_opened = !m_shader_editor.m_is_opened; }
	void onWindowGUI() override { m_shader_editor.onGUI(); }
	bool hasFocus() override { return m_shader_editor.isFocused(); }
	bool isOpened() const { return m_shader_editor.m_is_opened; }

	StudioApp& m_app;
	ShaderCompiler* m_compiler;
	ShaderEditor m_shader_editor;
};


struct WorldEditorPlugin LUMIX_FINAL : public WorldEditor::Plugin
{
	void showPointLightGizmo(ComponentUID light)
	{
		RenderScene* scene = static_cast<RenderScene*>(light.scene);
		Universe& universe = scene->getUniverse();

		float range = scene->getLightRange(light.handle);

		Vec3 pos = universe.getPosition(light.entity);
		scene->addDebugSphere(pos, range, 0xff0000ff, 0);
	}


	static Vec3 minCoords(const Vec3& a, const Vec3& b)
	{
		return Vec3(Math::minimum(a.x, b.x),
			Math::minimum(a.y, b.y),
			Math::minimum(a.z, b.z));
	}


	static Vec3 maxCoords(const Vec3& a, const Vec3& b)
	{
		return Vec3(Math::maximum(a.x, b.x),
			Math::maximum(a.y, b.y),
			Math::maximum(a.z, b.z));
	}


	void showModelInstanceGizmo(ComponentUID model_instance)
	{
		RenderScene* scene = static_cast<RenderScene*>(model_instance.scene);
		Universe& universe = scene->getUniverse();
		Model* model = scene->getModelInstanceModel(model_instance.handle);
		Vec3 points[8];
		if (!model) return;

		const AABB& aabb = model->getAABB();
		points[0] = aabb.min;
		points[7] = aabb.max;
		points[1].set(points[0].x, points[0].y, points[7].z);
		points[2].set(points[0].x, points[7].y, points[0].z);
		points[3].set(points[0].x, points[7].y, points[7].z);
		points[4].set(points[7].x, points[0].y, points[0].z);
		points[5].set(points[7].x, points[0].y, points[7].z);
		points[6].set(points[7].x, points[7].y, points[0].z);
		Matrix mtx = universe.getMatrix(model_instance.entity);

		for (int j = 0; j < 8; ++j)
		{
			points[j] = mtx.transform(points[j]);
		}

		Vec3 this_min = points[0];
		Vec3 this_max = points[0];

		for (int j = 0; j < 8; ++j)
		{
			this_min = minCoords(points[j], this_min);
			this_max = maxCoords(points[j], this_max);
		}

		scene->addDebugCube(this_min, this_max, 0xffff0000, 0);
	}


	void showGlobalLightGizmo(ComponentUID light)
	{
		RenderScene* scene = static_cast<RenderScene*>(light.scene);
		Universe& universe = scene->getUniverse();
		Vec3 pos = universe.getPosition(light.entity);

		Vec3 dir = universe.getRotation(light.entity).rotate(Vec3(0, 0, 1));
		Vec3 right = universe.getRotation(light.entity).rotate(Vec3(1, 0, 0));
		Vec3 up = universe.getRotation(light.entity).rotate(Vec3(0, 1, 0));

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
		Vec3 scale = scene->getDecalScale(cmp.handle);
		Matrix mtx = universe.getMatrix(cmp.entity);
		scene->addDebugCube(mtx.getTranslation(),
			mtx.getXVector() * scale.x,
			mtx.getYVector() * scale.y,
			mtx.getZVector() * scale.z,
			0xff0000ff,
			0);
	}


	void showCameraGizmo(ComponentUID cmp)
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		Universe& universe = scene->getUniverse();
		Vec3 pos = universe.getPosition(cmp.entity);

		bool is_ortho = scene->isCameraOrtho(cmp.handle);
		float near_distance = scene->getCameraNearPlane(cmp.handle);
		float far_distance = scene->getCameraFarPlane(cmp.handle);
		Vec3 dir = universe.getRotation(cmp.entity).rotate(Vec3(0, 0, -1));
		Vec3 right = universe.getRotation(cmp.entity).rotate(Vec3(1, 0, 0));
		Vec3 up = universe.getRotation(cmp.entity).rotate(Vec3(0, 1, 0));
		float w = scene->getCameraScreenWidth(cmp.handle);
		float h = scene->getCameraScreenHeight(cmp.handle);
		float ratio = h < 1.0f ? 1 : w / h;

		if (is_ortho)
		{
			float ortho_size = scene->getCameraOrthoSize(cmp.handle);
			Vec3 center = pos;
			center += (far_distance - near_distance) * dir * 0.5f;
			scene->addDebugCube(center,
				(far_distance - near_distance) * dir * 0.5f,
				ortho_size * up,
				ortho_size * ratio * right,
				0xffff0000,
				0);
		}
		else
		{
			float fov = scene->getCameraFOV(cmp.handle);
			scene->addDebugFrustum(pos, dir, up, fov, ratio, near_distance, far_distance, 0xffff0000, 0);
		}
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
		if (cmp.type == MODEL_INSTANCE_TYPE)
		{
			showModelInstanceGizmo(cmp);
			return true;
		}
		return false;
	}
};


struct AddTerrainComponentPlugin LUMIX_FINAL : public StudioApp::IAddComponentPlugin
{
	AddTerrainComponentPlugin(StudioApp& _app)
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
		auto& allocator = app.getWorldEditor()->getAllocator();
		if (!file.open(hm_path, FS::Mode::CREATE_AND_WRITE, allocator))
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

		if (!file.open(normalized_material_path, FS::Mode::CREATE_AND_WRITE, allocator))
		{
			g_log_error.log("Editor") << "Failed to create material " << normalized_material_path;
			PlatformInterface::deleteFile(hm_path);
			return false;
		}

		file.writeText("{ \"shader\" : \"pipelines/terrain/terrain.shd\", \
			\"texture\" : {\"source\" : \"");
		file.writeText(info.m_basename);
		file.writeText(".raw\", \"keep_data\" : true}, \
			\"texture\" : {\"source\" : \"/models/utils/white.tga\", \
			\"u_clamp\" : true, \"v_clamp\" : true, \
			\"min_filter\" : \"point\", \"mag_filter\" : \"point\", \"keep_data\" : true}, \
			\"texture\" : {\"source\" : \"\", \"srgb\" : true}, \
			\"texture\" : {\"source\" : \"\", \"srgb\" : true, \"keep_data\" : true}, \
			\"texture\" : {\"source\" : \"/models/utils/white.tga\", \"srgb\" : true}, \
			\"texture\" : {\"source\" : \"\"}, \
			\"uniforms\" : [\
				{\"name\" : \"detail_texture_distance\", \"float_value\" : 80.0}, \
				{ \"name\" : \"texture_scale\", \"float_value\" : 1.0 }], \
			\"metallic\" : 0.06, \"roughness\" : 0.9, \"alpha_ref\" : 0.3 }"
		);

		file.close();
		return true;
	}


	void onGUI(bool create_entity, bool from_filter) override
	{
		auto& editor = *app.getWorldEditor();

		ImGui::SetNextWindowSize(ImVec2(300, 300));
		if (!ImGui::BeginMenu("Terrain")) return;
		char buf[MAX_PATH_LENGTH];
		auto* asset_browser = app.getAssetBrowser();
		bool new_created = false;
		if (ImGui::BeginMenu("New"))
		{
			static int size = 1024;
			ImGui::InputInt("Size", &size);
			if (ImGui::Button("Create"))
			{
				char save_filename[MAX_PATH_LENGTH];
				if (PlatformInterface::getSaveFilename(save_filename, lengthOf(save_filename), "Material\0*.mat\0", "mat"))
				{
					editor.makeRelative(buf, lengthOf(buf), save_filename);
					new_created = createHeightmap(buf, size);
				}
			}
			ImGui::EndMenu();
		}
		bool create_empty = ImGui::Selectable("Empty", false);
		if (asset_browser->resourceList(buf, lengthOf(buf), MATERIAL_TYPE, 0) || create_empty || new_created)
		{
			if (create_entity)
			{
				Entity entity = editor.addEntity();
				editor.selectEntities(&entity, 1);
			}
			if (editor.getSelectedEntities().empty()) return;
			Entity entity = editor.getSelectedEntities()[0];

			if (!editor.getUniverse()->hasComponent(entity, TERRAIN_TYPE))
			{
				editor.addComponent(TERRAIN_TYPE);
			}

			auto& allocator = editor.getAllocator();
			auto* render_scene = static_cast<RenderScene*>(editor.getUniverse()->getScene(TERRAIN_TYPE));
			ComponentHandle cmp = editor.getUniverse()->getComponent(entity, TERRAIN_TYPE).handle;

			if (!create_empty)
			{
				auto* desc = PropertyRegister::getDescriptor(TERRAIN_TYPE, crc32("Material"));
				editor.setProperty(TERRAIN_TYPE, -1, *desc, &entity, 1, buf, stringLength(buf));
			}

			ImGui::CloseCurrentPopup();
		}
		ImGui::EndMenu();
	}


	const char* getLabel() const override { return "Render/Terrain"; }


	StudioApp& app;
};



extern "C" {


LUMIX_STUDIO_ENTRY(renderer)
{
	auto& allocator = app.getWorldEditor()->getAllocator();

	Model::force_keep_skin = true;

	app.registerComponent("camera", "Render/Camera");
	app.registerComponent("global_light", "Render/Global light");
	app.registerComponentWithResource("renderable", "Render/Mesh", MODEL_TYPE, "Source");
	app.registerComponentWithResource("particle_emitter", "Render/Particle emitter/Emitter", MATERIAL_TYPE, "Material");
	app.registerComponent("particle_emitter_spawn_shape", "Render/Particle emitter/Spawn shape");
	app.registerComponent("particle_emitter_alpha", "Render/Particle emitter/Alpha");
	app.registerComponent("particle_emitter_plane", "Render/Particle emitter/Plane");
	app.registerComponent("particle_emitter_force", "Render/Particle emitter/Force");
	app.registerComponent("particle_emitter_attractor", "Render/Particle emitter/Attractor");
	app.registerComponent("particle_emitter_subimage", "Render/Particle emitter/Subimage");
	app.registerComponent("particle_emitter_linear_movement", "Render/Particle emitter/Linear movement");
	app.registerComponent("particle_emitter_random_rotation", "Render/Particle emitter/Random rotation");
	app.registerComponent("particle_emitter_size", "Render/Particle emitter/Size");
	app.registerComponent("point_light", "Render/Point light");
	app.registerComponent("decal", "Render/Decal");
	app.registerComponent("bone_attachment", "Render/Bone attachment");
	app.registerComponent("environment_probe", "Render/Environment probe");

	auto* add_terrain_plugin = LUMIX_NEW(allocator, AddTerrainComponentPlugin)(app);
	app.registerComponent("terrain", *add_terrain_plugin);

	auto& asset_browser = *app.getAssetBrowser();
	asset_browser.addPlugin(*LUMIX_NEW(allocator, ModelPlugin)(app));
	asset_browser.addPlugin(*LUMIX_NEW(allocator, MaterialPlugin)(app));
	asset_browser.addPlugin(*LUMIX_NEW(allocator, TexturePlugin)(app));
	asset_browser.addPlugin(*LUMIX_NEW(allocator, ShaderPlugin)(app));

	auto& property_grid = *app.getPropertyGrid();
	property_grid.addPlugin(*LUMIX_NEW(allocator, EmitterPlugin)(app));
	property_grid.addPlugin(*LUMIX_NEW(allocator, EnvironmentProbePlugin)(app));
	property_grid.addPlugin(*LUMIX_NEW(allocator, TerrainPlugin)(app));

	auto* scene_view_plugin = LUMIX_NEW(allocator, SceneViewPlugin)(app);
	app.addPlugin(*scene_view_plugin);
	app.addPlugin(*LUMIX_NEW(allocator, ImportAssetDialog)(app));
	app.addPlugin(*LUMIX_NEW(allocator, GameViewPlugin)(app, *scene_view_plugin));
	app.addPlugin(*LUMIX_NEW(allocator, FurPainterPlugin)(app));
	app.addPlugin(*LUMIX_NEW(allocator, ShaderEditorPlugin)(app));

	app.getWorldEditor()->addPlugin(*LUMIX_NEW(allocator, WorldEditorPlugin)());
}


}
