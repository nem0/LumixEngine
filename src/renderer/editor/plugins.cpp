#include "engine/lumix.h"
#include "engine/crc32.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/file_system.h"
#include "engine/fs/os_file.h"
#include "engine/input_system.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/path_utils.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "editor/asset_browser.h"
#include "editor/ieditor_command.h"
#include "editor/platform_interface.h"
#include "editor/property_grid.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "engine/property_register.h"
#include "engine/property_descriptor.h"
#include "game_view.h"
#include "editor/render_interface.h"
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
#include "shader_editor.h"
#include "shader_compiler.h"
#include "terrain_editor.h"
#include <cmath>


using namespace Lumix;


static const uint32 PARTICLE_EMITTER_HASH = crc32("particle_emitter");
static const uint32 TERRAIN_HASH = crc32("terrain");
static const uint32 CAMERA_HASH = crc32("camera");
static const uint32 POINT_LIGHT_HASH = crc32("point_light");
static const uint32 GLOBAL_LIGHT_HASH = crc32("global_light");
static const uint32 RENDERABLE_HASH = crc32("renderable");


struct MaterialPlugin : public AssetBrowser::IPlugin
{
	explicit MaterialPlugin(StudioApp& app)
		: m_app(app)
	{
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


	bool onGUI(Resource* resource, uint32 type) override
	{
		if (type != ResourceManager::MATERIAL) return false;

		auto* material = static_cast<Material*>(resource);

		if (ImGui::Button("Save")) saveMaterial(material);
		ImGui::SameLine();
		if (ImGui::Button("Open in external editor")) m_app.getAssetBrowser()->openInExternalEditor(material);

		bool b;
		auto* plugin = m_app.getWorldEditor()->getEngine().getPluginManager().getPlugin("renderer");
		auto* renderer = static_cast<Renderer*>(plugin);

		int alpha_cutout_define = renderer->getShaderDefineIdx("ALPHA_CUTOUT");
		
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

		float shininess = material->getShininess();
		if (ImGui::DragFloat("Shininess", &shininess, 0.1f, 0.0f, 64.0f))
		{
			material->setShininess(shininess);
		}

		char buf[MAX_PATH_LENGTH];
		copyString(buf, material->getShader() ? material->getShader()->getPath().c_str() : "");
		if (m_app.getAssetBrowser()->resourceInput("Shader", "shader", buf, sizeof(buf), ResourceManager::SHADER))
		{
			material->setShader(Path(buf));
		}

		for (int i = 0; i < material->getShader()->m_texture_slot_count; ++i)
		{
			auto& slot = material->getShader()->m_texture_slots[i];
			auto* texture = material->getTexture(i);
			copyString(buf, texture ? texture->getPath().c_str() : "");
			if (m_app.getAssetBrowser()->resourceInput(
					slot.name, StaticString<30>("", (uint64)&slot), buf, sizeof(buf), ResourceManager::TEXTURE))
			{
				material->setTexturePath(i, Path(buf));
			}
			if (!texture) continue;

			ImGui::SameLine();
			StaticString<50> popup_name("pu", (uint64)texture, slot.name);
			StaticString<50> label("Advanced###adv", (uint64)texture, slot.name);
			if (ImGui::Button(label)) ImGui::OpenPopup(popup_name);

			if (ImGui::BeginPopup(popup_name))
			{
				static const struct { const char* name; uint32 value; } FLAGS[] = {
					{"SRGB", BGFX_TEXTURE_SRGB},
					{"u clamp", BGFX_TEXTURE_U_CLAMP},
					{"v clamp", BGFX_TEXTURE_V_CLAMP},
					{"Min point", BGFX_TEXTURE_MIN_POINT},
					{"Mag point", BGFX_TEXTURE_MAG_POINT}};

				for (auto& flag : FLAGS)
				{
					bool b = (texture->bgfx_flags & flag.value) != 0;
					if (ImGui::Checkbox(flag.name, &b))
					{
						ImGui::CloseCurrentPopup();
						texture->setFlag(flag.value, b);
					}
				}

				if (slot.is_atlas)
				{
					int size = texture->atlas_size - 2;
					const char* values = "2x2\0" "3x3\0" "4x4\0";
					if (ImGui::Combo(StaticString<30>("Atlas size###", i), &size, values))
					{
						ImGui::CloseCurrentPopup();
						texture->atlas_size = size + 2;
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
			for (int i = 0; i < 32; ++i)
			{
				if (material->isCustomFlag(1 << i))
				{
					ImGui::LabelText("Custom flag", "%s", Material::getCustomFlagName(i));
				}
			}

			if (ImGui::CollapsingHeader("Layers"))
			{
				for (int i = 0; i < shader->m_combintions.pass_count; ++i)
				{
					int idx = renderer->getPassIdx(shader->m_combintions.passes[i]);
					int layers_count = material->getLayerCount(idx);
					ImGui::DragInt(shader->m_combintions.passes[i], &layers_count, 1, 0, 256);
					material->setLayerCount(idx, layers_count);
				}
			}
			}
		return true;
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Material"; }
	bool hasResourceManager(uint32 type) const override { return type == ResourceManager::MATERIAL; }


	uint32 getResourceType(const char* ext) override
	{
		return equalStrings(ext, "mat") ? ResourceManager::MATERIAL : 0;
	}


	StudioApp& m_app;
};


struct ModelPlugin : public AssetBrowser::IPlugin
{
	struct InsertMeshCommand : public IEditorCommand
	{
		Vec3 m_position;
		Path m_mesh_path;
		Entity m_entity;
		WorldEditor& m_editor;


		explicit InsertMeshCommand(WorldEditor& editor)
			: m_editor(editor)
		{
		}


		InsertMeshCommand(WorldEditor& editor,
			const Vec3& position,
			const Path& mesh_path)
			: m_mesh_path(mesh_path)
			, m_position(position)
			, m_editor(editor)
		{
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("path", m_mesh_path.c_str());
			serializer.beginArray("pos");
			serializer.serializeArrayItem(m_position.x);
			serializer.serializeArrayItem(m_position.y);
			serializer.serializeArrayItem(m_position.z);
			serializer.endArray();
		}


		void deserialize(JsonSerializer& serializer) override
		{
			char path[MAX_PATH_LENGTH];
			serializer.deserialize("path", path, sizeof(path), "");
			m_mesh_path = path;
			serializer.deserializeArrayBegin("pos");
			serializer.deserializeArrayItem(m_position.x, 0);
			serializer.deserializeArrayItem(m_position.y, 0);
			serializer.deserializeArrayItem(m_position.z, 0);
			serializer.deserializeArrayEnd();
		}


		bool execute() override
		{
			static const uint32 RENDERABLE_HASH = crc32("renderable");

			Universe* universe = m_editor.getUniverse();
			m_entity = universe->createEntity(Vec3(0, 0, 0), Quat(0, 0, 0, 1));
			universe->setPosition(m_entity, m_position);
			const Array<IScene*>& scenes = m_editor.getScenes();
			ComponentIndex cmp = -1;
			IScene* scene = nullptr;
			for (int i = 0; i < scenes.size(); ++i)
			{
				cmp = scenes[i]->createComponent(RENDERABLE_HASH, m_entity);

				if (cmp >= 0)
				{
					scene = scenes[i];
					break;
				}
			}
			if (cmp >= 0) static_cast<RenderScene*>(scene)->setRenderablePath(cmp, m_mesh_path);
			return true;
		}


		void undo() override
		{
			const WorldEditor::ComponentList& cmps = m_editor.getComponents(m_entity);
			for (int i = 0; i < cmps.size(); ++i)
			{
				cmps[i].scene->destroyComponent(cmps[i].index, cmps[i].type);
			}
			m_editor.getUniverse()->destroyEntity(m_entity);
			m_entity = INVALID_ENTITY;
		}


		uint32 getType() override
		{
			static const uint32 TYPE = crc32("insert_mesh");
			return TYPE;
		}


		bool merge(IEditorCommand&) override { return false; }
	};


	explicit ModelPlugin(StudioApp& app)
		: m_app(app)
	{
		m_camera_cmp = INVALID_COMPONENT;
		m_camera_entity = INVALID_ENTITY;
		m_mesh = INVALID_COMPONENT;
		m_pipeline = nullptr;
		m_universe = nullptr;
		m_app.getWorldEditor()->registerEditorCommandCreator("insert_mesh", createInsertMeshCommand);

		createPreviewUniverse();
	}


	~ModelPlugin()
	{
		auto& engine = m_app.getWorldEditor()->getEngine();
		engine.destroyUniverse(*m_universe);
		Pipeline::destroy(m_pipeline);
	}


	static IEditorCommand* createInsertMeshCommand(WorldEditor& editor)
	{
		return LUMIX_NEW(editor.getAllocator(), InsertMeshCommand)(editor);
	}


	static void insertInScene(WorldEditor& editor, Model* model)
	{
		auto* command =
			LUMIX_NEW(editor.getAllocator(), InsertMeshCommand)(editor, editor.getCameraRaycastHit(), model->getPath());

		editor.executeCommand(command);
	}


	void createPreviewUniverse()
	{
		auto& engine = m_app.getWorldEditor()->getEngine();
		m_universe = &engine.createUniverse();
		auto* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));
		m_pipeline = Pipeline::create(*renderer, Path("pipelines/main.lua"), engine.getAllocator());
		m_pipeline->load();

		auto mesh_entity = m_universe->createEntity({ 0, 0, 0 }, { 0, 0, 0, 1 });
		auto* render_scene = static_cast<RenderScene*>(m_universe->getScene(crc32("renderer")));
		m_mesh = render_scene->createComponent(crc32("renderable"), mesh_entity);
		
		auto light_entity = m_universe->createEntity({ 0, 0, 0 }, { 0, 0, 0, 1 });
		auto light_cmp = render_scene->createComponent(crc32("global_light"), light_entity);
		render_scene->setGlobalLightIntensity(light_cmp, 0);
		render_scene->setLightAmbientIntensity(light_cmp, 1);
		
		m_camera_entity = m_universe->createEntity({ 0, 0, 0 }, { 0, 0, 0, 1 });
		m_camera_cmp = render_scene->createComponent(crc32("camera"), m_camera_entity);
		render_scene->setCameraSlot(m_camera_cmp, "editor");
		
		m_pipeline->setScene(render_scene);
	}


	void showPreview(Model& model)
	{
		auto& engine = m_app.getWorldEditor()->getEngine();
		auto* render_scene = static_cast<RenderScene*>(m_universe->getScene(crc32("renderer")));
		if (!render_scene) return;
		if (!model.isReady()) return;

		if (render_scene->getRenderableModel(m_mesh) != &model)
		{
			render_scene->setRenderablePath(m_mesh, model.getPath());
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
			PlatformInterface::showCursor(true);
			PlatformInterface::unclipCursor();
		}
		
		if (ImGui::IsItemHovered() && mouse_down)
		{
			auto& input = engine.getInputSystem();
			auto delta = Lumix::Vec2(input.getMouseXMove(), input.getMouseYMove());

			if (!m_is_mouse_captured)
			{
				m_is_mouse_captured = true;
				PlatformInterface::showCursor(false);
			}

			PlatformInterface::clipCursor(content_min.x, content_min.y, content_max.x, content_max.y);

			if (delta.x != 0 || delta.y != 0)
			{
				const Vec2 MOUSE_SENSITIVITY(50, 50);
				Vec3 pos = m_universe->getPosition(m_camera_entity);
				Quat rot = m_universe->getRotation(m_camera_entity);
				Quat old_rot = rot;

				float yaw = -Math::signum(delta.x) * (Math::pow(Math::abs((float)delta.x / MOUSE_SENSITIVITY.x), 1.2f));
				Quat yaw_rot(Vec3(0, 1, 0), yaw);
				rot = rot * yaw_rot;
				rot.normalize();

				Vec3 pitch_axis = rot * Vec3(1, 0, 0);
				float pitch =
					-Math::signum(delta.y) * (Math::pow(Math::abs((float)delta.y / MOUSE_SENSITIVITY.y), 1.2f));
				Quat pitch_rot(pitch_axis, pitch);
				rot = rot * pitch_rot;
				rot.normalize();

				Vec3 dir = rot * Vec3(0, 0, 1);
				Vec3 origin = (model.getAABB().max + model.getAABB().min) * 0.5f;

				float dist = (origin - pos).length();
				pos = origin + dir * dist;

				m_universe->setRotation(m_camera_entity, rot);
				m_universe->setPosition(m_camera_entity, pos);
			}

		}
	}


	bool onGUI(Resource* resource, uint32 type) override
	{
		if (type != ResourceManager::MODEL) return false;

		auto* model = static_cast<Model*>(resource);

		if (ImGui::Button("Insert in scene")) insertInScene(*m_app.getWorldEditor(), model);

		ImGui::LabelText("Bone count", "%d", model->getBoneCount());
		if (model->getBoneCount() > 0 && ImGui::CollapsingHeader("Bones"))
		{
			ImGui::Columns(3);
			for (int i = 0; i < model->getBoneCount(); ++i)
			{
				ImGui::Text("%s", model->getBone(i).name.c_str());
				ImGui::NextColumn();
				auto pos = model->getBone(i).position;
				ImGui::Text("%f; %f; %f", pos.x, pos.y, pos.z);
				ImGui::NextColumn();
				auto rot = model->getBone(i).rotation;
				ImGui::Text("%f; %f; %f; %f", rot.x, rot.y, rot.z, rot.w);
				ImGui::NextColumn();
			}
		}

		ImGui::LabelText("Bounding radius", "%f", model->getBoundingRadius());

		auto* lods = model->getLODs();
		if (lods[0].to_mesh >= 0)
		{
			ImGui::Separator();
			ImGui::Columns(3);
			ImGui::Text("LOD"); ImGui::NextColumn();
			ImGui::Text("Distance"); ImGui::NextColumn();
			ImGui::Text("# of meshes"); ImGui::NextColumn();
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
					m_app.getAssetBrowser()->selectResource(mesh.material->getPath());
				}
				ImGui::TreePop();
			}
		}

		showPreview(*model);

		return true;
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Model"; }
	bool hasResourceManager(uint32 type) const override { return type == ResourceManager::MODEL; }
	uint32 getResourceType(const char* ext) override { return equalStrings(ext, "msh") ? ResourceManager::MODEL : 0; }


	StudioApp& m_app;
	Universe* m_universe;
	Pipeline* m_pipeline;
	ComponentIndex m_mesh;
	Entity m_camera_entity;
	ComponentIndex m_camera_cmp;
	bool m_is_mouse_captured;
};


struct TexturePlugin : public AssetBrowser::IPlugin
{
	explicit TexturePlugin(StudioApp& app)
		: m_app(app)
	{
	}


	bool onGUI(Resource* resource, uint32 type) override
	{
		if (type != ResourceManager::TEXTURE) return false;

		auto* texture = static_cast<Texture*>(resource);
		if (texture->isFailure())
		{
			ImGui::Text("Texture failed to load.");
			return true;
		}

		ImGui::LabelText("Size", "%dx%d", texture->width, texture->height);
		ImGui::LabelText("BPP", "%d", texture->bytes_per_pixel);
		if (texture->is_cubemap)
		{
			ImGui::Text("Cubemap");
			return true;
		}

		m_texture_handle = texture->handle;
		if (bgfx::isValid(m_texture_handle))
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

			ImGui::Image(&m_texture_handle, texture_size);
		
			if (ImGui::Button("Open")) m_app.getAssetBrowser()->openInExternalEditor(resource);
		}
		return true;
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Texture"; }
	bool hasResourceManager(uint32 type) const override { return type == ResourceManager::TEXTURE; }


	uint32 getResourceType(const char* ext) override
	{
		if (equalStrings(ext, "tga")) return ResourceManager::TEXTURE;
		if (equalStrings(ext, "dds")) return ResourceManager::TEXTURE;
		if (equalStrings(ext, "raw")) return ResourceManager::TEXTURE;
		return 0;
	}

	bgfx::TextureHandle m_texture_handle;
	StudioApp& m_app;
};


struct ShaderPlugin : public AssetBrowser::IPlugin
{
	explicit ShaderPlugin(StudioApp& app)
		: m_app(app)
	{
	}


	bool onGUI(Resource* resource, uint32 type) override
	{
		if (type != ResourceManager::SHADER) return false;

		auto* shader = static_cast<Shader*>(resource);
		char basename[MAX_PATH_LENGTH];
		PathUtils::getBasename(basename, lengthOf(basename), resource->getPath().c_str());
		StaticString<MAX_PATH_LENGTH> path("/shaders/", basename);
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

		if (shader->m_texture_slot_count > 0 && ImGui::CollapsingHeader("Texture slots", nullptr, true, true))
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

		if (!shader->m_uniforms.empty() && ImGui::CollapsingHeader("Uniforms", nullptr, true, true))
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
	bool hasResourceManager(uint32 type) const override { return type == ResourceManager::SHADER; }
	uint32 getResourceType(const char* ext) override { return equalStrings(ext, "shd") ? ResourceManager::SHADER : 0; }


	StudioApp& m_app;
};


struct EmitterPlugin : public PropertyGrid::IPlugin
{
	explicit EmitterPlugin(StudioApp& app)
		: m_app(app)
	{
		m_particle_emitter_updating = true;
		m_particle_emitter_timescale = 1.0f;
	}


	void onGUI(PropertyGrid& grid, ComponentUID cmp) override
	{
		if (cmp.type != PARTICLE_EMITTER_HASH) return;
		
		ImGui::Separator();
		ImGui::Checkbox("Update", &m_particle_emitter_updating);
		auto* scene = static_cast<RenderScene*>(cmp.scene);
		ImGui::SameLine();
		if (ImGui::Button("Reset")) scene->resetParticleEmitter(cmp.index);

		if (m_particle_emitter_updating)
		{
			ImGui::DragFloat("Timescale", &m_particle_emitter_timescale, 0.01f, 0.01f, 10000.0f);
			float time_delta = m_app.getWorldEditor()->getEngine().getLastTimeDelta();
			scene->updateEmitter(cmp.index, time_delta * m_particle_emitter_timescale);
			scene->getParticleEmitter(cmp.index)->drawGizmo(*m_app.getWorldEditor(), *scene);
		}
	}


	StudioApp& m_app;
	float m_particle_emitter_timescale;
	bool m_particle_emitter_updating;
};


struct TerrainPlugin : public PropertyGrid::IPlugin
{
	explicit TerrainPlugin(StudioApp& app)
		: m_app(app)
	{
		auto& editor = *app.getWorldEditor();
		m_terrain_editor = LUMIX_NEW(editor.getAllocator(), TerrainEditor)(editor, app.getActions());
	}


	~TerrainPlugin()
	{
		LUMIX_DELETE(m_app.getWorldEditor()->getAllocator(), m_terrain_editor);
	}


	void onGUI(PropertyGrid& grid, ComponentUID cmp) override
	{
		if (cmp.type != TERRAIN_HASH) return;

		m_terrain_editor->setComponent(cmp);
		m_terrain_editor->onGUI();
	}


	StudioApp& m_app;
	TerrainEditor* m_terrain_editor;
};


struct SceneViewPlugin : public StudioApp::IPlugin
{
	struct RenderInterfaceImpl : public RenderInterface
	{
		ModelHandle loadModel(Path& path) override
		{
			auto& rm = m_editor.getEngine().getResourceManager();
			m_models.insert(m_model_index, static_cast<Model*>(rm.get(ResourceManager::MODEL)->load(path)));
			++m_model_index;
			return m_model_index - 1;
		}


		void addDebugCross(const Vec3& pos, float size, uint32 color, float life) override
		{
			m_render_scene->addDebugCross(pos, size, color, life);
		}


		WorldEditor::RayHit castRay(const Vec3& origin, const Vec3& dir, ComponentIndex ignored) override
		{
			auto hit = m_render_scene->castRay(origin, dir, ignored);

			return {hit.m_is_hit, hit.m_t, hit.m_entity, hit.m_origin + hit.m_dir * hit.m_t};
		}


		void getRay(ComponentIndex camera_index, float x, float y, Vec3& origin, Vec3& dir) override
		{
			m_render_scene->getRay(camera_index, x, y, origin, dir);
		}


		void addDebugLine(const Vec3& from, const Vec3& to, uint32 color, float life) override
		{
			m_render_scene->addDebugLine(from, to, color, life);
		}


		void addDebugCube(const Vec3& minimum, const Vec3& maximum, uint32 color, float life) override
		{
			m_render_scene->addDebugCube(minimum, maximum, color, life);
		}


		AABB getEntityAABB(Universe& universe, Entity entity) override
		{
			AABB aabb;
			auto cmp = m_render_scene->getRenderableComponent(entity);
			if (cmp != INVALID_COMPONENT)
			{
				Model* model = m_render_scene->getRenderableModel(cmp);
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
			model->getResourceManager().get(ResourceManager::MODEL)->unload(*model);
			m_models.erase(handle);
		}


		void setCameraSlot(ComponentIndex cmp, const char* slot) override
		{
			m_render_scene->setCameraSlot(cmp, slot);
		}


		ComponentIndex getCameraInSlot(const char* slot) override
		{
			return m_render_scene->getCameraInSlot(slot);
		}


		Entity getCameraEntity(ComponentIndex cmp) override
		{
			return m_render_scene->getCameraEntity(cmp);
		}


		Vec2 getCameraScreenSize(ComponentIndex cmp) override
		{
			return m_render_scene->getCameraScreenSize(cmp);
		}


		float getCameraOrthoSize(ComponentIndex cmp) override
		{
			return m_render_scene->getCameraOrthoSize(cmp);
		}


		bool isCameraOrtho(ComponentIndex cmp) override
		{
			return m_render_scene->isCameraOrtho(cmp);
		}


		float getCameraFOV(ComponentIndex cmp) override
		{
			return m_render_scene->getCameraFOV(cmp);
		}


		float castRay(ModelHandle model, const Vec3& origin, const Vec3& dir, const Matrix& mtx) override
		{
			RayCastModelHit hit = m_models[model]->castRay(origin, dir, mtx);
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
		{
			m_model_index = -1;
			auto& rm = m_editor.getEngine().getResourceManager();
			Path shader_path("shaders/debugline.shd");
			m_shader = static_cast<Shader*>(rm.get(ResourceManager::SHADER)->load(shader_path));

			editor.universeCreated().bind<RenderInterfaceImpl, &RenderInterfaceImpl::onUniverseCreated>(this);
			editor.universeDestroyed().bind<RenderInterfaceImpl, &RenderInterfaceImpl::onUniverseDestroyed>(this);
		}


		~RenderInterfaceImpl()
		{
			auto& rm = m_editor.getEngine().getResourceManager();
			rm.get(ResourceManager::SHADER)->unload(*m_shader);

			m_editor.universeCreated().unbind<RenderInterfaceImpl, &RenderInterfaceImpl::onUniverseCreated>(this);
			m_editor.universeDestroyed().unbind<RenderInterfaceImpl, &RenderInterfaceImpl::onUniverseDestroyed>(this);
		}


		void onUniverseCreated()
		{
			m_render_scene = static_cast<RenderScene*>(m_editor.getUniverse()->getScene(crc32("renderer")));
		}


		void onUniverseDestroyed()
		{
			m_render_scene = nullptr;
		}


		Vec3 getModelCenter(Entity entity) override
		{
			auto cmp = m_render_scene->getRenderableComponent(entity);
			if (cmp == INVALID_COMPONENT) return Vec3(0, 0, 0);
			Model* model = m_render_scene->getRenderableModel(cmp);
			if (!model) return Vec3(0, 0, 0);
			return (model->getAABB().min + model->getAABB().max) * 0.5f;
		}


		void showEntity(Entity entity) override
		{
			ComponentIndex cmp = m_render_scene->getRenderableComponent(entity);
			if (cmp == INVALID_COMPONENT) return;
			m_render_scene->showRenderable(cmp);
		}


		void hideEntity(Entity entity) override
		{
			ComponentIndex cmp = m_render_scene->getRenderableComponent(entity);
			if (cmp == INVALID_COMPONENT) return;
			m_render_scene->hideRenderable(cmp);
		}


		Path getRenderablePath(ComponentIndex cmp) override
		{
			return m_render_scene->getRenderablePath(cmp);
		}


		void render(const Matrix& mtx,
			uint16* indices,
			int indices_count,
			Vertex* vertices,
			int vertices_count,
			bool lines) override
		{
			if (!m_shader->isReady()) return;

			auto& renderer = static_cast<Renderer&>(m_render_scene->getPlugin());
			if (!bgfx::checkAvailTransientBuffers(vertices_count, renderer.getBasicVertexDecl(), indices_count))
			{
				return;
			}
			bgfx::TransientVertexBuffer vertex_buffer;
			bgfx::TransientIndexBuffer index_buffer;
			bgfx::allocTransientVertexBuffer(&vertex_buffer, vertices_count, renderer.getBasicVertexDecl());
			bgfx::allocTransientIndexBuffer(&index_buffer, indices_count);

			copyMemory(vertex_buffer.data, vertices, vertices_count * renderer.getBasicVertexDecl().getStride());
			copyMemory(index_buffer.data, indices, indices_count * sizeof(uint16));

			uint64 flags = BGFX_STATE_DEPTH_TEST_LEQUAL;
			if (lines) flags |= BGFX_STATE_PT_LINES;
			m_pipeline.render(vertex_buffer,
				index_buffer,
				mtx,
				0,
				indices_count,
				flags,
				m_shader->getInstance(0).program_handles[m_pipeline.getPassIdx()]);
		}


		WorldEditor& m_editor;
		Shader* m_shader;
		RenderScene* m_render_scene;
		Pipeline& m_pipeline;
		HashMap<int, Model*> m_models;
		int m_model_index;
	};


	explicit SceneViewPlugin(StudioApp& app)
		: m_app(app)
	{
		auto& editor = *app.getWorldEditor();
		auto& allocator = editor.getAllocator();
		m_action = LUMIX_NEW(allocator, Action)("Scene View", "scene_view");
		m_action->func.bind<SceneViewPlugin, &SceneViewPlugin::onAction>(this);
		m_scene_view.init(*app.getLogUI(), editor, app.getActions());
		m_render_interface = LUMIX_NEW(allocator, RenderInterfaceImpl)(editor, *m_scene_view.getPipeline());
		editor.setRenderInterface(m_render_interface);
		m_app.getAssetBrowser()->resourceChanged().bind<SceneViewPlugin, &SceneViewPlugin::onResourceChanged>(this);
	}


	~SceneViewPlugin()
	{
		m_app.getAssetBrowser()->resourceChanged().unbind<SceneViewPlugin, &SceneViewPlugin::onResourceChanged>(this);
		m_scene_view.shutdown();
	}


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


struct GameViewPlugin : public StudioApp::IPlugin
{
	static GameViewPlugin* s_instance;


	explicit GameViewPlugin(StudioApp& app)
		: m_app(app)
	{
		m_width = m_height = -1;
		auto& editor = *app.getWorldEditor();
		m_engine = &editor.getEngine();
		m_action = LUMIX_NEW(editor.getAllocator(), Action)("Game View", "game_view");
		m_action->func.bind<GameViewPlugin, &GameViewPlugin::onAction>(this);
		m_game_view.m_is_opened = false;
		m_game_view.init(editor);

		auto& plugin_manager = editor.getEngine().getPluginManager();
		auto* renderer = static_cast<Renderer*>(plugin_manager.getPlugin("renderer"));
		Path path("pipelines/imgui.lua");
		m_gui_pipeline = Pipeline::create(*renderer, path, m_engine->getAllocator());
		m_gui_pipeline->load();

		int w = PlatformInterface::getWindowWidth();
		int h = PlatformInterface::getWindowHeight();
		m_gui_pipeline->setViewport(0, 0, w, h);
		renderer->resize(w, h);
		onUniverseCreated();

		s_instance = this;

		unsigned char* pixels;
		int width, height;
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		auto* material_manager = m_engine->getResourceManager().get(ResourceManager::MATERIAL);
		auto* resource = material_manager->load(Path("shaders/imgui.mat"));
		m_material = static_cast<Material*>(resource);

		Texture* texture = LUMIX_NEW(editor.getAllocator(), Texture)(
			Path("font"), m_engine->getResourceManager(), editor.getAllocator());

		texture->create(width, height, pixels);
		m_material->setTexture(0, texture);

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


	void shutdownImGui()
	{
		ImGui::ShutdownDock();
		ImGui::Shutdown();

		Texture* texture = m_material->getTexture(0);
		m_material->setTexture(0, nullptr);
		texture->destroy();
		LUMIX_DELETE(m_app.getWorldEditor()->getAllocator(), texture);

		m_material->getResourceManager().get(ResourceManager::MATERIAL)->unload(*m_material);
	}


	void draw(ImDrawData* draw_data)
	{
		if (!m_gui_pipeline->isReady()) return;
		if (!m_material || !m_material->isReady()) return;
		if (!m_material->getTexture(0)) return;

		int w = PlatformInterface::getWindowWidth();
		int h = PlatformInterface::getWindowHeight();
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

		renderer->frame();
	}


	void onUniverseCreated()
	{
		auto* scene = static_cast<RenderScene*>(m_app.getWorldEditor()->getScene(crc32("renderer")));

		m_gui_pipeline->setScene(scene);
	}


	void onUniverseDestroyed() { m_gui_pipeline->setScene(nullptr); }
	static void imGuiCallback(ImDrawData* draw_data) { s_instance->draw(draw_data); }


	void setGUIProjection()
	{
		float width = ImGui::GetIO().DisplaySize.x;
		float height = ImGui::GetIO().DisplaySize.y;
		Matrix ortho;
		ortho.setOrtho(0.0f, width, 0.0f, height, -1.0f, 1.0f);
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
		if (!bgfx::checkAvailTransientBuffers(num_vertices, decl, num_indices)) return;
		bgfx::allocTransientVertexBuffer(&vertex_buffer, num_vertices, decl);
		bgfx::allocTransientIndexBuffer(&index_buffer, num_indices);

		copyMemory(vertex_buffer.data, &cmd_list->VtxBuffer[0], num_vertices * decl.getStride());
		copyMemory(index_buffer.data, &cmd_list->IdxBuffer[0], num_indices * sizeof(uint16));

		uint32 elem_offset = 0;
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

			m_gui_pipeline->setScissor(uint16(Math::maximum(pcmd->ClipRect.x, 0.0f)),
				uint16(Math::maximum(pcmd->ClipRect.y, 0.0f)),
				uint16(Math::minimum(pcmd->ClipRect.z, 65535.0f) - Math::maximum(pcmd->ClipRect.x, 0.0f)),
				uint16(Math::minimum(pcmd->ClipRect.w, 65535.0f) - Math::maximum(pcmd->ClipRect.y, 0.0f)));

			auto material = m_material;
			int pass_idx = m_gui_pipeline->getPassIdx();
			const auto& texture_id =
				pcmd->TextureId ? *(bgfx::TextureHandle*)pcmd->TextureId : material->getTexture(0)->handle;
			auto texture_uniform = material->getShader()->m_texture_slots[0].uniform_handle;
			m_gui_pipeline->setTexture(0, texture_id, texture_uniform);
			m_gui_pipeline->render(vertex_buffer,
				index_buffer,
				Matrix::IDENTITY,
				elem_offset,
				pcmd->ElemCount,
				material->getRenderStates(),
				material->getShaderInstance().program_handles[pass_idx]);

			elem_offset += pcmd->ElemCount;
		}
	}


	void onAction() { m_game_view.m_is_opened = !m_game_view.m_is_opened; }
	void onWindowGUI() override { m_game_view.onGui(); }

	int m_width;
	int m_height;
	StudioApp& m_app;
	Engine* m_engine;
	Material* m_material;
	Pipeline* m_gui_pipeline;
	GameView m_game_view;
};


GameViewPlugin* GameViewPlugin::s_instance = nullptr;


struct ShaderEditorPlugin : public StudioApp::IPlugin
{
	explicit ShaderEditorPlugin(StudioApp& app)
		: m_shader_editor(app.getWorldEditor()->getAllocator())
		, m_app(app)
	{
		m_action = LUMIX_NEW(app.getWorldEditor()->getAllocator(), Action)("Shader Editor", "shader_editor");
		m_action->func.bind<ShaderEditorPlugin, &ShaderEditorPlugin::onAction>(this);
		m_shader_editor.m_is_opened = false;

		m_compiler = LUMIX_NEW(app.getWorldEditor()->getAllocator(), ShaderCompiler)(app, *app.getLogUI());

		lua_State* L = app.getWorldEditor()->getEngine().getState();
		LuaWrapper::createSystemVariable(L, "Editor", "shader_compiler", m_compiler);
		auto* f =
			&LuaWrapper::wrapMethod<ShaderCompiler, decltype(&ShaderCompiler::compileAll), &ShaderCompiler::compileAll>;
		LuaWrapper::createSystemFunction(L, "Editor", "compileShaders", f);
	}


	~ShaderEditorPlugin() { LUMIX_DELETE(m_app.getWorldEditor()->getAllocator(), m_compiler); }


	void update(float) override { m_compiler->update(); }
	void onAction() { m_shader_editor.m_is_opened = !m_shader_editor.m_is_opened; }
	void onWindowGUI() override { m_shader_editor.onGUI(); }
	bool hasFocus() override { return m_shader_editor.isFocused(); }


	StudioApp& m_app;
	ShaderCompiler* m_compiler;
	ShaderEditor m_shader_editor;
};


struct WorldEditorPlugin : public WorldEditor::Plugin
{
	void showPointLightGizmo(ComponentUID light)
	{
		RenderScene* scene = static_cast<RenderScene*>(light.scene);
		Universe& universe = scene->getUniverse();

		float range = scene->getLightRange(light.index);

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


	void showRenderableGizmo(ComponentUID renderable)
	{
		RenderScene* scene = static_cast<RenderScene*>(renderable.scene);
		Universe& universe = scene->getUniverse();
		Model* model = scene->getRenderableModel(renderable.index);
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
		Matrix mtx = universe.getMatrix(renderable.entity);

		for (int j = 0; j < 8; ++j)
		{
			points[j] = mtx.multiplyPosition(points[j]);
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

		Vec3 dir = universe.getRotation(light.entity) * Vec3(0, 0, 1);
		Vec3 right = universe.getRotation(light.entity) * Vec3(1, 0, 0);
		Vec3 up = universe.getRotation(light.entity) * Vec3(0, 1, 0);

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


	bool showGizmo(ComponentUID cmp) override 
	{
		if (cmp.type == CAMERA_HASH)
		{
			RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
			Universe& universe = scene->getUniverse();
			Vec3 pos = universe.getPosition(cmp.entity);

			float fov = scene->getCameraFOV(cmp.index);
			float near_distance = scene->getCameraNearPlane(cmp.index);
			float far_distance = scene->getCameraFarPlane(cmp.index);
			Vec3 dir = universe.getRotation(cmp.entity) * Vec3(0, 0, -1);
			Vec3 right = universe.getRotation(cmp.entity) * Vec3(1, 0, 0);
			Vec3 up = universe.getRotation(cmp.entity) * Vec3(0, 1, 0);
			float w = scene->getCameraScreenWidth(cmp.index);
			float h = scene->getCameraScreenHeight(cmp.index);
			float ratio = h < 1.0f ? 1 : w / h;

			scene->addDebugFrustum(
				pos, dir, up, fov, ratio, near_distance, far_distance, 0xffff0000, 0);
			return true;
		}
		if (cmp.type == POINT_LIGHT_HASH)
		{
			showPointLightGizmo(cmp);
			return true;
		}
		if (cmp.type == GLOBAL_LIGHT_HASH)
		{
			showGlobalLightGizmo(cmp);
			return true;
		}
		if (cmp.type == RENDERABLE_HASH)
		{
			showRenderableGizmo(cmp);
			return true;
		}
		return false;
	}
};


extern "C" {


LUMIX_STUDIO_ENTRY(renderer)
{
	auto& allocator = app.getWorldEditor()->getAllocator();

	auto* model_plugin = LUMIX_NEW(allocator, ModelPlugin)(app);
	app.getAssetBrowser()->addPlugin(*model_plugin);

	auto* material_plugin = LUMIX_NEW(allocator, MaterialPlugin)(app);
	app.getAssetBrowser()->addPlugin(*material_plugin);

	auto* texture_plugin = LUMIX_NEW(allocator, TexturePlugin)(app);
	app.getAssetBrowser()->addPlugin(*texture_plugin);

	auto* shader_plugin = LUMIX_NEW(allocator, ShaderPlugin)(app);
	app.getAssetBrowser()->addPlugin(*shader_plugin);

	auto* emitter_plugin = LUMIX_NEW(allocator, EmitterPlugin)(app);
	app.getPropertyGrid()->addPlugin(*emitter_plugin);

	auto* terrain_plugin = LUMIX_NEW(allocator, TerrainPlugin)(app);
	app.getPropertyGrid()->addPlugin(*terrain_plugin);

	auto* scene_view_plugin = LUMIX_NEW(allocator, SceneViewPlugin)(app);
	app.addPlugin(*scene_view_plugin);

	auto* import_asset_plugin = LUMIX_NEW(allocator, ImportAssetDialog)(app);
	app.addPlugin(*import_asset_plugin);

	auto* game_view_plugin = LUMIX_NEW(allocator, GameViewPlugin)(app);
	app.addPlugin(*game_view_plugin);

	auto* shader_editor_plugin =
		LUMIX_NEW(allocator, ShaderEditorPlugin)(app);
	app.addPlugin(*shader_editor_plugin);

	auto* world_editor_plugin = LUMIX_NEW(allocator, WorldEditorPlugin)();
	app.getWorldEditor()->addPlugin(*world_editor_plugin);
}


}
