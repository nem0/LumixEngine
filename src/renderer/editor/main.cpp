#include "lumix.h"
#include "core/crc32.h"
#include "core/FS/disk_file_device.h"
#include "core/FS/file_system.h"
#include "core/FS/os_file.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/lua_wrapper.h"
#include "core/path_utils.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
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


static const uint32 TEXTURE_HASH = ResourceManager::TEXTURE;
static const uint32 SHADER_HASH = ResourceManager::SHADER;
static const uint32 MATERIAL_HASH = crc32("MATERIAL");


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
		char tmp_path[MAX_PATH_LENGTH];
		strcpy(tmp_path, material->getPath().c_str());
		strcat(tmp_path, ".tmp");
		FS::IFile* file = fs.open(fs.getDefaultDevice(),
			Path(tmp_path),
			FS::Mode::CREATE_AND_WRITE);
		if (file)
		{
			DefaultAllocator allocator;
			JsonSerializer serializer(
				*file, JsonSerializer::AccessMode::WRITE, material->getPath(), allocator);
			if (!material->save(serializer))
			{
				g_log_error.log("Editor") << "Error saving " << material->getPath().c_str();
			}
			fs.close(*file);

			auto& engine = m_app.getWorldEditor()->getEngine();
			StaticString<MAX_PATH_LENGTH> src_full_path("");
			StaticString<MAX_PATH_LENGTH> dest_full_path("");
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
		else
		{
			g_log_error.log("Editor") << "Could not save file " << material->getPath().c_str();
		}
	}


	bool onGUI(Resource* resource, uint32 type) override
	{
		if (type != MATERIAL_HASH) return false;

		auto* material = static_cast<Material*>(resource);

		if (ImGui::Button("Save")) saveMaterial(material);
		ImGui::SameLine();
		if (ImGui::Button("Open in external editor")) m_app.getAssetBrowser()->openInExternalEditor(material);

		bool b;
		auto* plugin = m_app.getWorldEditor()->getEngine().getPluginManager().getPlugin("renderer");
		auto* renderer = static_cast<Renderer*>(plugin);

		int alpha_cutout_define = renderer->getShaderDefineIdx("ALPHA_CUTOUT");
		
		int layer_count = material->getLayerCount();
		if (ImGui::DragInt("Layers", &layer_count))
		{
			material->setLayerCount(layer_count);
		}
		
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

		char buf[256];
		copyString(buf, material->getShader() ? material->getShader()->getPath().c_str() : "");
		if (m_app.getAssetBrowser()->resourceInput("Shader", "shader", buf, sizeof(buf), SHADER_HASH))
		{
			material->setShader(Path(buf));
		}

		for (int i = 0; i < material->getShader()->getTextureSlotCount(); ++i)
		{
			auto& slot = material->getShader()->getTextureSlot(i);
			auto* texture = material->getTexture(i);
			copyString(buf, texture ? texture->getPath().c_str() : "");
			if (m_app.getAssetBrowser()->resourceInput(
				slot.m_name, StaticString<30>("", (uint64)&slot), buf, sizeof(buf), TEXTURE_HASH))
			{
				material->setTexturePath(i, Path(buf));
			}
			if (!texture) continue;

			ImGui::SameLine();
			StaticString<100> popup_name("pu", (uint64)texture, slot.m_name);
			if (ImGui::Button(StaticString<100>("Advanced###adv", (uint64)texture, slot.m_name)))
			{
				ImGui::OpenPopup(popup_name);
			}

			if (ImGui::BeginPopup(popup_name))
			{
				bool is_srgb = (texture->getFlags() & BGFX_TEXTURE_SRGB) != 0;
				if (ImGui::Checkbox("SRGB", &is_srgb))
				{
					texture->setFlag(BGFX_TEXTURE_SRGB, is_srgb);
				}
				bool u_clamp = (texture->getFlags() & BGFX_TEXTURE_U_CLAMP) != 0;
				if (ImGui::Checkbox("u clamp", &u_clamp))
				{
					texture->setFlag(BGFX_TEXTURE_U_CLAMP, u_clamp);
				}
				bool v_clamp = (texture->getFlags() & BGFX_TEXTURE_V_CLAMP) != 0;
				if (ImGui::Checkbox("v clamp", &v_clamp))
				{
					texture->setFlag(BGFX_TEXTURE_V_CLAMP, v_clamp);
				}
				bool min_point = (texture->getFlags() & BGFX_TEXTURE_MIN_POINT) != 0;
				if (ImGui::Checkbox("Min point", &min_point))
				{
					texture->setFlag(BGFX_TEXTURE_MIN_POINT, min_point);
				}
				bool mag_point = (texture->getFlags() & BGFX_TEXTURE_MAG_POINT) != 0;
				if (ImGui::Checkbox("Mag point", &mag_point))
				{
					texture->setFlag(BGFX_TEXTURE_MAG_POINT, mag_point);
				}
				if (slot.m_is_atlas)
				{
					int size = texture->getAtlasSize() - 2;
					const char values[] = { '2', 'x', '2', 0, '3', 'x', '3', 0, '4', 'x', '4', 0, 0 };
					if (ImGui::Combo(StaticString<30>("Atlas size###", i), &size, values))
					{
						texture->setAtlasSize(size + 2);
					}
				}
				ImGui::EndPopup();

			}

		}

		auto* shader = material->getShader();
		if(shader && material->isReady())
		{
			for(int i = 0; i < shader->getUniformCount(); ++i)
			{
				auto& uniform = material->getUniform(i);
				auto& shader_uniform = shader->getUniform(i);
				switch(shader_uniform.type)
				{
					case Shader::Uniform::FLOAT:
						if (ImGui::DragFloat(shader_uniform.name, &uniform.float_value))
						{
							material->createCommandBuffer();
						}
						break;
					case Shader::Uniform::VEC3:
						if(ImGui::DragFloat3(shader_uniform.name, uniform.vec3))
						{
							material->createCommandBuffer();
						}
						break;
					case Shader::Uniform::COLOR:
						if(ImGui::ColorEdit3(shader_uniform.name, uniform.vec3))
						{
							material->createCommandBuffer();
						}
						if(ImGui::BeginPopupContextItem(StaticString<50>(shader_uniform.name, "pu")))
						{
							if(ImGui::ColorPicker(uniform.vec3, false))
							{
								material->createCommandBuffer();
							}
							ImGui::EndPopup();
						}
						break;
					case Shader::Uniform::TIME: break;
					default: ASSERT(false); break;
				}
			}
		}
		for (int i = 0; i < 32; ++i)
		{
			if (material->isCustomFlag(1 << i))
			{
				ImGui::LabelText("Custom flag", Material::getCustomFlagName(i));
			}
		}

		return true;
	}


	void onResourceUnloaded(Resource* resource) override
	{
	}


	const char* getName() const override
	{
		return "Material";
	}


	bool hasResourceManager(uint32 type) const override
	{
		return type == MATERIAL_HASH;
	}


	uint32 getResourceType(const char* ext) override
	{
		if (compareString(ext, "mat") == 0) return MATERIAL_HASH;
		return 0;
	}


	StudioApp& m_app;
};


static const uint32 MODEL_HASH = ResourceManager::MODEL;


struct ModelPlugin : public AssetBrowser::IPlugin
{
	struct InsertMeshCommand : public IEditorCommand
	{
		Vec3 m_position;
		Path m_mesh_path;
		Entity m_entity;
		WorldEditor& m_editor;

		Entity getEntity() const { return m_entity; }
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
			if (cmp >= 0)
			{
				static_cast<RenderScene*>(scene)->setRenderablePath(cmp, m_mesh_path);
			}
			return true;
		}


		void undo() override
		{
			const WorldEditor::ComponentList& cmps =
				m_editor.getComponents(m_entity);
			for (int i = 0; i < cmps.size(); ++i)
			{
				cmps[i].scene->destroyComponent(cmps[i].index, cmps[i].type);
			}
			m_editor.getUniverse()->destroyEntity(m_entity);
			m_entity = INVALID_ENTITY;
		}


		uint32 getType() override
		{
			static const uint32 type = crc32("insert_mesh");
			return type;
		}


		bool merge(IEditorCommand&) override
		{
			return false;
		}


	};


	explicit ModelPlugin(StudioApp& app)
		: m_app(app)
	{
		m_app.getWorldEditor()->registerEditorCommandCreator("insert_mesh", createInsertMeshCommand);
	}


	static IEditorCommand* createInsertMeshCommand(WorldEditor& editor)
	{
		return LUMIX_NEW(editor.getAllocator(), InsertMeshCommand)(editor);
	}


	static void insertInScene(WorldEditor& editor, Model* model)
	{
		auto* command = LUMIX_NEW(editor.getAllocator(), InsertMeshCommand)(
			editor, editor.getCameraRaycastHit(), model->getPath());

		editor.executeCommand(command);
	}


	bool onGUI(Resource* resource, uint32 type) override
	{
		if (type != MODEL_HASH) return false;

		auto* model = static_cast<Model*>(resource);
		if (ImGui::Button("Insert in scene"))
		{
			insertInScene(*m_app.getWorldEditor(), model);
		}

		ImGui::LabelText("Bone count", "%d", model->getBoneCount());
		if (model->getBoneCount() > 0 && ImGui::CollapsingHeader("Bones"))
		{
			ImGui::Columns(3);
			for (int i = 0; i < model->getBoneCount(); ++i)
			{
				ImGui::Text(model->getBone(i).name.c_str());
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
			for (int i = 0; i < Model::MAX_LOD_COUNT - 1 && lods[i + 1].to_mesh >= 0; ++i)
			{
				ImGui::PushID(i);
				ImGui::Text("%d", i); ImGui::NextColumn();
				float dist = sqrt(lods[i].distance);
				if (ImGui::DragFloat("", &dist))
				{
					lods[i].distance = dist * dist;
				}
				ImGui::NextColumn();
				ImGui::Text("%d", lods[i].to_mesh - lods[i].from_mesh + 1); ImGui::NextColumn();
				++lod_count;
				ImGui::PopID();
			}

			ImGui::Text("%d", lod_count - 1); ImGui::NextColumn();
			ImGui::Text("INFINITE"); ImGui::NextColumn();
			ImGui::Text("%d", lods[lod_count - 1].to_mesh - lods[lod_count - 1].from_mesh + 1);
			ImGui::Columns(1);
		}

		ImGui::Separator();
		for (int i = 0; i < model->getMeshCount(); ++i)
		{
			auto& mesh = model->getMesh(i);
			if (ImGui::TreeNode(&mesh, mesh.name.length() > 0 ? mesh.name.c_str() : "N/A"))
			{
				ImGui::LabelText("Triangle count", "%d", mesh.indices_count / 3);
				ImGui::LabelText("Material", mesh.material->getPath().c_str());
				ImGui::SameLine();
				if (ImGui::Button("->"))
				{
					m_app.getAssetBrowser()->selectResource(mesh.material->getPath());
				}
				ImGui::TreePop();
			}
		}
		return true;
	}


	void onResourceUnloaded(Resource* resource) override
	{
	}


	const char* getName() const override
	{
		return "Model";
	}


	bool hasResourceManager(uint32 type) const override
	{
		return type == MODEL_HASH;
	}


	uint32 getResourceType(const char* ext) override
	{
		if (compareString(ext, "msh") == 0) return MODEL_HASH;
		return 0;
	}


	StudioApp& m_app;
};


struct TexturePlugin : public AssetBrowser::IPlugin
{
	explicit TexturePlugin(StudioApp& app)
		: m_app(app)
	{
	}


	bool onGUI(Resource* resource, uint32 type) override
	{
		if (type != TEXTURE_HASH) return false;

		auto* texture = static_cast<Texture*>(resource);
		if (texture->isFailure())
		{
			ImGui::Text("Texture failed to load");
			return true;
		}

		ImGui::LabelText("Size", "%dx%d", texture->getWidth(), texture->getHeight());
		ImGui::LabelText("BPP", "%d", texture->getBytesPerPixel());
		m_texture_handle = texture->getTextureHandle();
		if (bgfx::isValid(m_texture_handle))
		{
			ImGui::Image(&m_texture_handle, ImVec2(200, 200));
			if (ImGui::Button("Open"))
			{
				m_app.getAssetBrowser()->openInExternalEditor(resource);
			}
		}
		return true;
	}


	void onResourceUnloaded(Resource* resource) override
	{
	}


	const char* getName() const override
	{
		return "Texture";
	}


	bool hasResourceManager(uint32 type) const override
	{
		return type == TEXTURE_HASH;
	}


	uint32 getResourceType(const char* ext) override
	{
		if (compareString(ext, "tga") == 0) return TEXTURE_HASH;
		if (compareString(ext, "dds") == 0) return TEXTURE_HASH;
		if (compareString(ext, "raw") == 0) return TEXTURE_HASH;
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
		if (type != SHADER_HASH) return false;
		auto* shader = static_cast<Shader*>(resource);
		char basename[MAX_PATH_LENGTH];
		PathUtils::getBasename(
			basename, lengthOf(basename), resource->getPath().c_str());
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

		if (shader->getTextureSlotCount() > 0 && ImGui::CollapsingHeader("Texture slots", nullptr, true, true))
		{
			ImGui::Columns(2);
			ImGui::Text("name");
			ImGui::NextColumn();
			ImGui::Text("uniform");
			ImGui::NextColumn();
			ImGui::Separator();
			for (int i = 0; i < shader->getTextureSlotCount(); ++i)
			{
				auto& slot = shader->getTextureSlot(i);
				ImGui::Text(slot.m_name);
				ImGui::NextColumn();
				ImGui::Text(slot.m_uniform);
				ImGui::NextColumn();
			}
			ImGui::Columns(1);
		}

		if(shader->getUniformCount() > 0 && ImGui::CollapsingHeader("Uniforms", nullptr, true, true))
		{
			ImGui::Columns(2);
			ImGui::Text("name");
			ImGui::NextColumn();
			ImGui::Text("type");
			ImGui::NextColumn();
			ImGui::Separator();
			for(int i = 0; i < shader->getUniformCount(); ++i)
			{
				auto& uniform = shader->getUniform(i);
				ImGui::Text(uniform.name);
				ImGui::NextColumn();
				switch(uniform.type)
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


	void onResourceUnloaded(Resource* resource) override
	{
	}


	const char* getName() const override
	{
		return "Shader";
	}


	bool hasResourceManager(uint32 type) const override
	{
		return type == SHADER_HASH;
	}


	uint32 getResourceType(const char* ext) override
	{
		if (compareString(ext, "shd") == 0) return SHADER_HASH;
		return 0;
	}


	StudioApp& m_app;
};


static const uint32 PARTICLE_EMITTER_HASH = crc32("particle_emitter");


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


static const uint32 TERRAIN_HASH = crc32("terrain");


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
			onUniverseCreated();
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


		void render(const Matrix& mtx,
			uint16* indices,
			int indices_count,
			Vertex* vertices,
			int vertices_count,
			bool lines) override
		{
			if (!m_shader->isReady()) return;

			auto& renderer = static_cast<Renderer&>(m_render_scene->getPlugin());
			if (!bgfx::checkAvailTransientBuffers(
					vertices_count, renderer.getBasicVertexDecl(), indices_count))
			{
				return;
			}
			bgfx::TransientVertexBuffer vertex_buffer;
			bgfx::TransientIndexBuffer index_buffer;
			bgfx::allocTransientVertexBuffer(
				&vertex_buffer, vertices_count, renderer.getBasicVertexDecl());
			bgfx::allocTransientIndexBuffer(&index_buffer, indices_count);

			copyMemory(vertex_buffer.data,
				vertices,
				vertices_count * renderer.getBasicVertexDecl().getStride());
			copyMemory(index_buffer.data, indices, indices_count * sizeof(uint16));
			
			uint64 flags = BGFX_STATE_DEPTH_TEST_LEQUAL;
			if (lines) flags |= BGFX_STATE_PT_LINES;
			m_pipeline.render(vertex_buffer,
				index_buffer,
				mtx,
				0,
				indices_count,
				flags,
				m_shader->getInstance(0).m_program_handles[m_pipeline.getPassIdx()]);
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


	void onResourceChanged(const Path& path, const char* ext)
	{
		if (m_scene_view.getPipeline()->getPath() == path)
		{
			m_scene_view.getPipeline()->load();
		}
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


struct MeshMergerPlugin : public StudioApp::IPlugin
{
	explicit MeshMergerPlugin(StudioApp& _app)
		: app(_app)
		, models(_app.getWorldEditor()->getAllocator())
	{
		m_is_window_opened = false;
		m_action =
			LUMIX_NEW(app.getWorldEditor()->getAllocator(), Action)("Mesh Merger", "mesh_merger");
		m_action->func.bind<MeshMergerPlugin, &MeshMergerPlugin::onAction>(this);
		auto& engine = app.getWorldEditor()->getEngine();
		auto* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));
		model_manager = &renderer->getModelManager();
		output[0] = 0;
		for (int i = 0; i < lengthOf(lods); ++i) lods[i] = (float)i;
	}


	~MeshMergerPlugin()
	{
		for (auto* model : models)
		{
			model_manager->unload(*model);
		}
	}


	void onAction() { m_is_window_opened = !m_is_window_opened; }


	enum class VertexAttributeDef : uint32
	{
		POSITION,
		FLOAT1,
		FLOAT2,
		FLOAT3,
		FLOAT4,
		INT1,
		INT2,
		INT3,
		INT4,
		SHORT2,
		SHORT4,
		BYTE4,
		NONE
	};


	static void writeAttribute(const char* attribute_name,
		VertexAttributeDef attribute_type,
		FS::OsFile& file)
	{
		uint32 length = stringLength(attribute_name);
		file.write((const char*)&length, sizeof(length));
		file.write(attribute_name, length);

		uint32 type = (uint32)attribute_type;
		file.write((const char*)&type, sizeof(type));
	}


	int getAttributeArrayOffset(Mesh& mesh)
	{
		int offset = 0;
		for (auto& model : models)
		{
			for (int i = 0; i < model->getMeshCount(); ++i)
			{
				auto& tmp = model->getMesh(i);
				if (&tmp == &mesh) return offset;
				offset += tmp.attribute_array_size;
			}
		}
		return offset;
	}


	int getIndicesOffset(Mesh& mesh)
	{
		int offset = 0;
		for (auto& model : models)
		{
			for (int i = 0; i < model->getMeshCount(); ++i)
			{
				auto& tmp = model->getMesh(i);
				if (&tmp == &mesh) return offset + tmp.indices_offset;
			}
			offset += model->getIndices().size();
		}
		return offset;
	}


	void writeMeshes(FS::OsFile& file)
	{
		Array<Mesh*> meshes(app.getWorldEditor()->getAllocator());
		int mesh_count = 0;
		for (auto* model : models)
		{
			mesh_count += model->getMeshCount();
		}
		file.write((const char*)&mesh_count, sizeof(mesh_count));
		for (auto* model : models)
		{
			for (int i = 0; i < model->getMeshCount(); ++i)
			{
				auto& engine_mesh = model->getMesh(i);
				int vertex_size = engine_mesh.vertex_def.getStride();
				char material_name[MAX_PATH_LENGTH];
				PathUtils::getBasename(material_name,
					lengthOf(material_name),
					engine_mesh.material->getPath().c_str());
				int32 length = stringLength(material_name);
				file.write((const char*)&length, sizeof(length));
				file.write(material_name, length);

				int32 attribute_array_offset = getAttributeArrayOffset(engine_mesh);
				file.write((const char*)&attribute_array_offset, sizeof(attribute_array_offset));
				int32 attribute_array_size = engine_mesh.attribute_array_size;
				attribute_array_offset += attribute_array_size;
				file.write((const char*)&attribute_array_size, sizeof(attribute_array_size));

				int32 indices_offset = getIndicesOffset(engine_mesh);
				file.write((const char*)&indices_offset, sizeof(indices_offset));
				indices_offset += engine_mesh.indices_count;
				int mesh_tri_count = engine_mesh.indices_count / 3;
				file.write((const char*)&mesh_tri_count, sizeof(mesh_tri_count));

				length = engine_mesh.name.length();

				file.write((const char*)&length, sizeof(length));
				file.write((const char*)engine_mesh.name.c_str(), length);

				int32 attribute_count = 3;
				if (engine_mesh.vertex_def.has(bgfx::Attrib::Color0)) ++attribute_count;
				if (engine_mesh.vertex_def.has(bgfx::Attrib::Tangent)) ++attribute_count;

				file.write((const char*)&attribute_count, sizeof(attribute_count));

				writeAttribute("in_position", VertexAttributeDef::POSITION, file);
				if (engine_mesh.vertex_def.has(bgfx::Attrib::Color0))
				{
					writeAttribute("in_colors", VertexAttributeDef::BYTE4, file);
				}
				writeAttribute("in_normal", VertexAttributeDef::BYTE4, file);
				if (engine_mesh.vertex_def.has(bgfx::Attrib::Tangent))
				{
					writeAttribute("in_tangents", VertexAttributeDef::BYTE4, file);
				}
				writeAttribute("in_tex_coords", VertexAttributeDef::FLOAT2, file);
			}
		}
	}


	bool writeGeometry(FS::OsFile& file)
	{
		int32 indices_count = 0;
		int32 vertices_size = 0;
		for (auto& model : models)
		{
			indices_count += model->getIndices().size();
			for (int i = 0; i < model->getMeshCount(); ++i)
			{
				auto& engine_mesh = model->getMesh(i);
				vertices_size += engine_mesh.attribute_array_size;
			}
		}
		file.write((const char*)&indices_count, sizeof(indices_count));

		int32 polygon_idx = 0;
		int indices_offset = 0;
		for (auto& model : models)
		{
			const int* indices = &model->getIndices()[0];
			for (int j = 0; j < model->getIndices().size(); ++j)
			{
				int tmp = indices_offset + indices[j];
				file.write(&tmp, sizeof(tmp));
			}
		}

		file.write((const char*)&vertices_size, sizeof(vertices_size));
		for (auto& model : models)
		{
			auto& allocator = app.getWorldEditor()->getAllocator();
			auto& fs = app.getWorldEditor()->getEngine().getFileSystem();
			auto in_file = fs.open(fs.getDiskDevice(), model->getPath(), FS::Mode::OPEN_AND_READ);
			if (!in_file)
			{
				g_log_error.log("Renderer") << "Failed to open \"" << model->getPath() << "\"";
				return false;
			}

			Model::FileHeader header;
			in_file->read(&header, sizeof(Model::FileHeader));
			if (header.m_version != (uint32)Model::FileVersion::FIRST + 1)
			{
				g_log_error.log("Renderer") << model->getPath().c_str()
											<< " has unsupported version";
				return false;
			}

			int object_count = 0;
			in_file->read(&object_count, sizeof(object_count));
			for (int i = 0; i < object_count; ++i)
			{
				int32 str_size;
				in_file->read(&str_size, sizeof(str_size));
				char dummy[MAX_PATH_LENGTH];
				in_file->read(dummy, str_size);

				int32 idummy[4];
				in_file->read(idummy, sizeof(idummy));

				in_file->read(&str_size, sizeof(str_size));
				in_file->read(dummy, str_size);

				uint32 attribute_count;
				in_file->read(&attribute_count, sizeof(attribute_count));
				for (uint32 i = 0; i < attribute_count; ++i)
				{
					char tmp[50];
					uint32 len;
					in_file->read(&len, sizeof(len));
					in_file->read(tmp, len);
					uint32 type;
					in_file->read(&type, sizeof(type));
				}
			}
			int32 indices_count;
			in_file->read(&indices_count, sizeof(indices_count));
			in_file->seek(FS::SeekMode::CURRENT, indices_count * sizeof(int32));
			int32 in_vertices_size;
			in_file->read(&in_vertices_size, sizeof(in_vertices_size));
			char buf[4096];
			while (in_vertices_size)
			{
				int size = Math::minimum(in_vertices_size, lengthOf(buf));
				in_file->read(buf, size);
				file.write(buf, size);
				in_vertices_size -= size;
			}

			fs.close(*in_file);
		}
		return true;
	}


	void writeLODs(FS::OsFile& file)
	{
		int lod_count = models.size();
		if (lods[lod_count - 1] < 10e9)
		{
			lods[lod_count] = FLT_MAX;
			++lod_count;
		}
		file.write((const char*)&lod_count, sizeof(lod_count));
		int32 to_mesh = -1;
		for (int i = 0; i < lod_count; ++i)
		{
			to_mesh += i < models.size() ? models[i]->getMeshCount() : 0;
			file.write((const char*)&to_mesh, sizeof(to_mesh));
			float squared_dist = lods[i] * lods[i];
			file.write((const char*)&squared_dist, sizeof(squared_dist));
		}
	}


	bool check()
	{
		for (auto* model : models)
		{
			if (model->getBoneCount() > 0)
			{
				g_log_error.log("Renderer") << "Skinned meshes are not supported";
				return false;
			}
		}
		return true;
	}


	void merge()
	{
		if (output[0] == 0) return;
		if (!check()) return;

		FS::OsFile file;
		if (!file.open(
				output, FS::Mode::CREATE_AND_WRITE, app.getWorldEditor()->getAllocator()))
		{
			g_log_error.log("Renderer") << "Failed to save \"" << output << "\"";
			return;
		}

		Model::FileHeader header;
		header.m_magic = Model::FILE_MAGIC;
		header.m_version = (uint32)Model::FileVersion::FIRST;

		file.write(&header, sizeof(header));
		writeMeshes(file);
		if (!writeGeometry(file))
		{
			file.close();
			return;
		}
		int32 bone_count = 0;
		file.write((const char*)&bone_count, sizeof(bone_count));
		writeLODs(file);

		auto* disk_device = app.getWorldEditor()->getEngine().getDiskFileDevice();
		auto* patch_device = app.getWorldEditor()->getEngine().getPatchFileDevice();
		char dir[MAX_PATH_LENGTH];
		PathUtils::getDir(dir, lengthOf(dir), output);
		for (auto& model : models)
		{
			for (int i = 0; i < model->getMeshCount(); ++i)
			{
				auto& engine_mesh = model->getMesh(i);
				char src[MAX_PATH_LENGTH];
				char dest[MAX_PATH_LENGTH];
				if (patch_device)
				{
					copyString(src, patch_device->getBasePath());
					catString(src, engine_mesh.material->getPath().c_str());
					copyString(dest, dir);
					char mat_basename[MAX_PATH_LENGTH];
					PathUtils::getBasename(mat_basename, lengthOf(mat_basename), engine_mesh.material->getPath().c_str());
					catString(dest, mat_basename);
					catString(dest, ".mat");
				}
				if (!patch_device || !PlatformInterface::copyFile(src, dest))
				{
					copyString(src, disk_device->getBasePath());
					catString(src, engine_mesh.material->getPath().c_str());
					if (!PlatformInterface::copyFile(src, dest))
					{
						g_log_error.log("Renderer") << "Failed to copy " << engine_mesh.material->getPath();
					}
				}
			}
		}

		file.close();
	}


	void onWindowGUI() override
	{
		if (ImGui::BeginDock("Mesh Merger", &m_is_window_opened))
		{
			ImGui::InputText("Output", output, sizeof(output));
			ImGui::SameLine();
			if (ImGui::Button("...###browseoutput"))
			{
				auto* base_path =
					app.getWorldEditor()->getEngine().getDiskFileDevice()->getBasePath();
				PlatformInterface::getSaveFilename(output, sizeof(output), base_path, "msh");
			}
			if (ImGui::Button("Merge")) merge();

			if (ImGui::CollapsingHeader("Sources", nullptr, true, true))
			{
				char buf[MAX_PATH_LENGTH];
				ImGui::Columns(2);
				if (!models.empty())
				{
					ImGui::Text("Model");
					ImGui::NextColumn();
					ImGui::Text("Distance");
					ImGui::NextColumn();
				}
				for (int i = 0; i < models.size(); ++i)
				{
					auto& model = models[i];
					ImGui::PushID(model);
					buf[0] = 0;
					copyString(buf, model->getPath().c_str());
					if (app.getAssetBrowser()->resourceInput(
							"Model", "model", buf, lengthOf(buf), ResourceManager::MODEL))
					{
						if (model) model_manager->unload(*model);
						if (buf[0] != 0)
						{
							model = static_cast<Model*>(model_manager->load(Path(buf)));
						}
						else
						{
							models.erase(i);
							ImGui::PopID();
							break;
						}
					}
					ImGui::NextColumn();
					if (lods[i] < 10e9)
					{
						ImGui::DragFloat("",
							&lods[i],
							1,
							i > 0 ? lods[i - 1] : 0,
							i < models.size() - 1 ? lods[i + 1] : 10e8f);
						ImGui::SameLine();
					}
					if (i == models.size() - 1)
					{
						bool b = lods[i] > 10e9;
						if (ImGui::Checkbox("Infinite", &b))
						{
							lods[i] = b ? FLT_MAX : 0;
						}
					}
					ImGui::NextColumn();
					ImGui::PopID();
				}
				ImGui::Columns();
				buf[0] = 0;
				ImGui::PushID(models.size());
				if (app.getAssetBrowser()->resourceInput(
						"Model", "model", buf, lengthOf(buf), ResourceManager::MODEL))
				{
					auto& model = models.emplace();
					model = static_cast<Model*>(model_manager->load(Path(buf)));
				}
				ImGui::PopID();
			}
		}
		ImGui::EndDock();
	}


	bool hasFocus() override { return false; }


	void update(float) override {}


	StudioApp& app;
	bool m_is_window_opened;
	Array<Model*> models;
	float lods[16];
	char output[MAX_PATH_LENGTH];
	ModelManager* model_manager;
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
		auto* material_manager =
			m_engine->getResourceManager().get(ResourceManager::MATERIAL);
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
		if(!m_material || !m_material->isReady()) return;
		if(!m_material->getTexture(0)) return;

		int w = PlatformInterface::getWindowWidth();
		int h = PlatformInterface::getWindowHeight();
		if (w != m_width || h != m_height)
		{
			m_width = w;
			m_height = h;
			auto& plugin_manager = m_app.getWorldEditor()->getEngine().getPluginManager();
			auto* renderer =
				static_cast<Renderer*>(plugin_manager.getPlugin("renderer"));
			if (renderer) renderer->resize(m_width, m_height);
		}

		m_gui_pipeline->render();
		setGUIProjection();

		for(int i = 0; i < draw_data->CmdListsCount; ++i)
		{
			ImDrawList* cmd_list = draw_data->CmdLists[i];
			drawGUICmdList(cmd_list);
		}

		Renderer* renderer =
			static_cast<Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));

		renderer->frame();
	}


	void onUniverseCreated()
	{
		auto* scene =
			static_cast<RenderScene*>(m_app.getWorldEditor()->getScene(crc32("renderer")));

		m_gui_pipeline->setScene(scene);
	}


	void onUniverseDestroyed()
	{
		m_gui_pipeline->setScene(nullptr);
	}


	static void imGuiCallback(ImDrawData* draw_data)
	{
		s_instance->draw(draw_data);
	}


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
		Renderer* renderer =
			static_cast<Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));

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
		for(const ImDrawCmd* pcmd = pcmd_begin; pcmd != pcmd_end; pcmd++)
		{
			if(pcmd->UserCallback)
			{
				pcmd->UserCallback(cmd_list, pcmd);
				elem_offset += pcmd->ElemCount;
				continue;
			}

			if(0 == pcmd->ElemCount) continue;

			m_gui_pipeline->setScissor(
				uint16(Math::maximum(pcmd->ClipRect.x, 0.0f)),
				uint16(Math::maximum(pcmd->ClipRect.y, 0.0f)),
				uint16(Math::minimum(pcmd->ClipRect.z, 65535.0f) -
				Math::maximum(pcmd->ClipRect.x, 0.0f)),
				uint16(Math::minimum(pcmd->ClipRect.w, 65535.0f) -
				Math::maximum(pcmd->ClipRect.y, 0.0f)));

			auto material = m_material;
			int pass_idx = m_gui_pipeline->getPassIdx();
			const auto& texture_id = pcmd->TextureId
				? *(bgfx::TextureHandle*)pcmd->TextureId
				: material->getTexture(0)->getTextureHandle();
			auto texture_uniform = material->getShader()->getTextureSlot(0).m_uniform_handle;
			m_gui_pipeline->setTexture(0, texture_id, texture_uniform);
			m_gui_pipeline->render(vertex_buffer,
				index_buffer,
				Matrix::IDENTITY,
				elem_offset,
				pcmd->ElemCount,
				material->getRenderStates(),
				material->getShaderInstance().m_program_handles[pass_idx]);

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

		m_compiler =
			LUMIX_NEW(app.getWorldEditor()->getAllocator(), ShaderCompiler)(app, *app.getLogUI());
		
		lua_State* L = app.getWorldEditor()->getEngine().getState();
		LuaWrapper::createSystemVariable(L, "Editor", "shader_compiler", m_compiler);
		auto* f = &LuaWrapper::wrapMethod<ShaderCompiler,
			decltype(&ShaderCompiler::compileAll),
			&ShaderCompiler::compileAll>;
		LuaWrapper::createSystemFunction(L, "Editor", "compileShaders", f);
	}


	void update(float) override
	{
		m_compiler->update();
	}


	~ShaderEditorPlugin()
	{
		LUMIX_DELETE(m_app.getWorldEditor()->getAllocator(), m_compiler);
	}


	void onAction() { m_shader_editor.m_is_opened = !m_shader_editor.m_is_opened; }
	void onWindowGUI() override { m_shader_editor.onGUI(); }
	bool hasFocus() override { return m_shader_editor.isFocused(); }


	StudioApp& m_app;
	ShaderCompiler* m_compiler;
	ShaderEditor m_shader_editor;
};


static const uint32 CAMERA_HASH = crc32("camera");
static const uint32 POINT_LIGHT_HASH = crc32("point_light");
static const uint32 GLOBAL_LIGHT_HASH = crc32("global_light");
static const uint32 RENDERABLE_HASH = crc32("renderable");


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
			float w = scene->getCameraWidth(cmp.index);
			float h = scene->getCameraHeight(cmp.index);
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

	auto* material_plugin = LUMIX_NEW(allocator, MaterialPlugin)(app);
	app.getAssetBrowser()->addPlugin(*material_plugin);

	auto* model_plugin = LUMIX_NEW(allocator, ModelPlugin)(app);
	app.getAssetBrowser()->addPlugin(*model_plugin);

	auto* texture_plugin = LUMIX_NEW(allocator, TexturePlugin)(app);
	app.getAssetBrowser()->addPlugin(*texture_plugin);

	auto* shader_plugin = LUMIX_NEW(allocator, ShaderPlugin)(app);
	app.getAssetBrowser()->addPlugin(*shader_plugin);

	auto* emitter_plugin = LUMIX_NEW(allocator, EmitterPlugin)(app);
	app.getPropertyGrid()->addPlugin(*emitter_plugin);

	auto* terrain_plugin = LUMIX_NEW(allocator, TerrainPlugin)(app);
	app.getPropertyGrid()->addPlugin(*terrain_plugin);

	auto* mesh_merger_plugin = LUMIX_NEW(allocator, MeshMergerPlugin)(app);
	app.addPlugin(*mesh_merger_plugin);

	auto* scene_view_plugin = LUMIX_NEW(allocator, SceneViewPlugin)(app);
	app.addPlugin(*scene_view_plugin);

	auto* game_view_plugin = LUMIX_NEW(allocator, GameViewPlugin)(app);
	app.addPlugin(*game_view_plugin);

	auto* shader_editor_plugin =
		LUMIX_NEW(allocator, ShaderEditorPlugin)(app);
	app.addPlugin(*shader_editor_plugin);

	auto* world_editor_plugin = LUMIX_NEW(allocator, WorldEditorPlugin)();
	app.getWorldEditor()->addPlugin(*world_editor_plugin);
}


}