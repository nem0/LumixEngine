#include "world_editor.h"

#include "animation/animation_system.h"
#include "core/array.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/delegate_list.h"
#include "core/fs/file_system.h"
#include "core/fs/memory_file_device.h"
#include "core/fs/disk_file_device.h"
#include "core/fs/tcp_file_device.h"
#include "core/fs/tcp_file_server.h"
#include "core/fs/ifile.h"
#include "core/input_system.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/map.h"
#include "core/matrix.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/editor_icon.h"
#include "editor/entity_template_system.h"
#include "editor/gizmo.h"
#include "editor/property_descriptor.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "engine/plugin_manager.h"
#include "graphics/irender_device.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "graphics/texture.h"
#include "core/input_system.h"
#include "core/mt/mutex.h"
#include "script/script_system.h"
#include "universe/universe.h"


namespace Lumix
{


struct EditorIconHit
{
	EditorIcon* m_icon;
	float m_t;
};


static const uint32_t RENDERABLE_HASH = crc32("renderable");
static const uint32_t CAMERA_HASH = crc32("camera");
static const uint32_t LIGHT_HASH = crc32("light");
static const uint32_t SCRIPT_HASH = crc32("script");
static const uint32_t ANIMABLE_HASH = crc32("animable");
static const uint32_t TERRAIN_HASH = crc32("terrain");


struct WorldEditorImpl : public WorldEditor
{
	public:

		virtual const char* getBasePath() override
		{
			return m_base_path.c_str();
		}


		virtual Engine& getEngine() override
		{
			return m_engine;
		}


		virtual void registerProperty(const char* component_type, IPropertyDescriptor* descriptor) override
		{
			ASSERT(descriptor);
			m_component_properties[crc32(component_type)].push(descriptor);
		}


		virtual void registerCreator(uint32_t type, IPlugin& creator) override
		{
			m_creators.insert(type, &creator);
		}


		virtual void tick() override
		{
			if (m_toggle_game_mode_requested)
			{
				toggleGameMode();
				m_toggle_game_mode_requested = false;
			}
			PROFILE_FUNCTION();
			m_engine.update(m_is_game_mode);
			m_engine.getFileSystem().updateAsyncTransactions();
		}

	
		virtual ~WorldEditorImpl()
		{
			auto iter = m_component_properties.begin();
			auto end = m_component_properties.end();
			while (iter != end)
			{
				for (int i = 0, c = iter.second().size(); i < c; ++i)
				{
					LUMIX_DELETE(iter.second()[i]);
				}
				++iter;
			}
		}


		void registerProperties()
		{
			m_component_properties[CAMERA_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("slot"), &RenderScene::getCameraSlot, &RenderScene::setCameraSlot, IPropertyDescriptor::STRING));
			m_component_properties[CAMERA_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("fov"), &RenderScene::getCameraFOV, &RenderScene::setCameraFOV));
			m_component_properties[CAMERA_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("near"), &RenderScene::getCameraNearPlane, &RenderScene::setCameraNearPlane));
			m_component_properties[CAMERA_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("far"), &RenderScene::getCameraFarPlane, &RenderScene::setCameraFarPlane));
			m_component_properties[RENDERABLE_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("source"), &RenderScene::getRenderablePath, &RenderScene::setRenderablePath, IPropertyDescriptor::FILE));
			m_component_properties[TERRAIN_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("material"), &RenderScene::getTerrainMaterial, &RenderScene::setTerrainMaterial, IPropertyDescriptor::FILE));
			m_component_properties[TERRAIN_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("xz_scale"), &RenderScene::getTerrainXZScale, &RenderScene::setTerrainXZScale));
			m_component_properties[TERRAIN_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("y_scale"), &RenderScene::getTerrainYScale, &RenderScene::setTerrainYScale));
			m_component_properties[TERRAIN_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("grass_mesh"), &RenderScene::getTerrainGrass, &RenderScene::setTerrainGrass, IPropertyDescriptor::FILE));
			/*m_component_properties[renderable_type].push(LUMIX_NEW(PropertyDescriptor<Renderer>)(crc32("visible"), &Renderer::getVisible, &Renderer::setVisible));
			m_component_properties[renderable_type].push(LUMIX_NEW(PropertyDescriptor<Renderer>)(crc32("cast shadows"), &Renderer::getCastShadows, &Renderer::setCastShadows));
			m_component_properties[point_light_type].push(LUMIX_NEW(PropertyDescriptor<Renderer>)(crc32("fov"), &Renderer::getLightFov, &Renderer::setLightFov));
			m_component_properties[point_light_type].push(LUMIX_NEW(PropertyDescriptor<Renderer>)(crc32("radius"), &Renderer::getLightRadius, &Renderer::setLightRadius));
			*/
		}


		EditorIconHit raycastEditorIcons(const Vec3& origin, const Vec3& dir)
		{
			EditorIconHit hit;
			hit.m_t = -1;
			for (int i = 0, c = m_editor_icons.size(); i < c; ++i)
			{
				float t = m_editor_icons[i]->hit(origin, dir);
				if (t >= 0)
				{
					hit.m_icon = m_editor_icons[i];
					hit.m_t = t;
					return hit;
				}
			}
			return hit;
		}


		virtual void onMouseDown(int x, int y, MouseButton::Value button) override
		{
			if (button == MouseButton::RIGHT)
			{
				m_mouse_mode = MouseMode::NAVIGATE;
			}
			else if (button == MouseButton::LEFT)
			{
				Vec3 origin, dir;
				Component camera_cmp = m_camera.getComponent(CAMERA_HASH);
				RenderScene* scene = static_cast<RenderScene*>(camera_cmp.system);
				scene->getRay(camera_cmp, (float)x, (float)y, origin, dir);
				RayCastModelHit hit = scene->castRay(origin, dir, Component::INVALID);
				RayCastModelHit gizmo_hit = m_gizmo.castRay(origin, dir);
				EditorIconHit icon_hit = raycastEditorIcons(origin, dir);
				if (gizmo_hit.m_is_hit && (icon_hit.m_t < 0 || gizmo_hit.m_t < icon_hit.m_t))
				{
					if (m_selected_entity.isValid())
					{
						m_mouse_mode = MouseMode::TRANSFORM;
						if (gizmo_hit.m_mesh->getNameHash() == crc32("x_axis"))
						{
							m_gizmo.startTransform(camera_cmp, x, y, Gizmo::TransformMode::X);
						}
						else if (gizmo_hit.m_mesh->getNameHash() == crc32("y_axis"))
						{
							m_gizmo.startTransform(camera_cmp, x, y, Gizmo::TransformMode::Y);
						}
						else
						{
							m_gizmo.startTransform(camera_cmp, x, y, Gizmo::TransformMode::Z);
						}
					}
				}
				else if (icon_hit.m_t >= 0)
				{
					selectEntity(icon_hit.m_icon->getEntity());
				}
				else if (hit.m_is_hit)
				{
					onEntityMouseDown(hit, x, y);
				}
			}
		}


		virtual void addPlugin(Plugin* plugin) override
		{
			m_plugins.push(plugin);
		}


		void onEntityMouseDown(const RayCastModelHit& hit, int x, int y)
		{
			Entity entity = hit.m_component.entity;
			for (int i = 0; i < m_plugins.size(); ++i)
			{
				if (m_plugins[i]->onEntityMouseDown(hit, x, y))
				{
					m_mouse_handling_plugin = m_plugins[i];
					m_mouse_mode = MouseMode::CUSTOM;
					return;
				}
			}
			selectEntity(entity);
			m_mouse_mode = MouseMode::TRANSFORM;
			m_gizmo.startTransform(m_camera.getComponent(CAMERA_HASH), x, y, Gizmo::TransformMode::CAMERA_XZ);
		}


		virtual void onMouseMove(int x, int y, int relx, int rely, int mouse_flags) override
		{
			switch (m_mouse_mode)
			{
				case MouseMode::CUSTOM:
					{
						if (m_mouse_handling_plugin)
						{
							m_mouse_handling_plugin->onMouseMove(x, y, relx, rely, mouse_flags);
						}
					}
					break;
				case MouseMode::NAVIGATE:
					rotateCamera(relx, rely);
					break;
				case MouseMode::TRANSFORM:
					{
					Gizmo::TransformOperation tmode = mouse_flags & (int)MouseFlags::ALT/*GetKeyState(VK_MENU) & 0x8000*/ ? Gizmo::TransformOperation::ROTATE : Gizmo::TransformOperation::TRANSLATE;
					int flags = mouse_flags & (int)MouseFlags::CONTROL/*GetKeyState(VK_LCONTROL) & 0x8000*/ ? Gizmo::Flags::FIXED_STEP : 0;
					m_gizmo.transform(m_camera.getComponent(CAMERA_HASH), tmode, x, y, relx, rely, flags);
					}
					break;
			}
		}


		virtual void onMouseUp(int x, int y, MouseButton::Value button) override
		{
			if (m_mouse_handling_plugin)
			{
				m_mouse_handling_plugin->onMouseUp(x, y, button);
				m_mouse_handling_plugin = NULL;
			}
			m_mouse_mode = MouseMode::NONE;
		}


		virtual void saveUniverse(const Path& path) override
		{
			g_log_info.log("editor server") << "saving universe " << path.c_str() << "...";
			FS::FileSystem& fs = m_engine.getFileSystem();
			FS::IFile* file = fs.open(fs.getDefaultDevice(), path, FS::Mode::OPEN_OR_CREATE | FS::Mode::WRITE);
			save(*file, path.c_str());
			fs.close(file);
			m_universe_path = path;
		}


		void save(FS::IFile& file, const char* path)
		{
			JsonSerializer serializer(file, JsonSerializer::WRITE, path);
			m_engine.serialize(serializer);
			m_template_system->serialize(serializer);
			g_log_info.log("editor server") << "universe saved";
		}


		virtual void snapToTerrain() override
		{
			if (m_selected_entity.isValid())
			{
				Component renderable = m_selected_entity.getComponent(RENDERABLE_HASH);
				RenderScene* scene = m_engine.getRenderScene();
				RayCastModelHit hit = scene->castRay(m_selected_entity.getPosition(), Vec3(0, -1, 0), renderable);
				if (hit.m_is_hit)
				{
					m_selected_entity.setPosition(hit.m_origin + hit.m_dir * hit.m_t);
				}
			}
		}

		virtual Entity addEntity() override
		{
			Component cmp = m_camera.getComponent(CAMERA_HASH);
			float width;
			float height;
			m_engine.getRenderScene()->getCameraWidth(cmp, width);
			m_engine.getRenderScene()->getCameraHeight(cmp, height);
			return addEntityAt((int)width >> 1, (int)height >> 1);
		}

		virtual Entity addEntityAt(int camera_x, int camera_y) override
		{
			Entity e = m_engine.getUniverse()->createEntity();

			RenderScene* scene = m_engine.getRenderScene();
			Vec3 origin;
			Vec3 dir;
			scene->getRay(m_camera.getComponent(CAMERA_HASH), (float)camera_x, (float)camera_y, origin, dir);
				RayCastModelHit hit = scene->castRay(origin, dir, Component::INVALID);
			if (hit.m_is_hit)
			{
				e.setPosition(hit.m_origin + hit.m_dir * hit.m_t);
			}
			else
			{
				e.setPosition(m_camera.getPosition() + m_camera.getRotation() * Vec3(0, 0, -2));
			}
			selectEntity(e);
			EditorIcon* er = LUMIX_NEW(EditorIcon)();
			er->create(m_engine, *m_engine.getRenderScene(), m_selected_entity);
			m_editor_icons.push(er);
			return e;
		}


		virtual void toggleGameMode() override
		{
			if (m_is_game_mode)
			{
				stopGameMode();
			}
			else
			{
				m_game_mode_file = m_engine.getFileSystem().open("memory", "", FS::Mode::WRITE);
				save(*m_game_mode_file, "GameMode");
				m_is_game_mode = true;
			}
		}


		void stopGameMode()
		{
			m_is_game_mode = false;
			m_game_mode_file->seek(FS::SeekMode::BEGIN, 0);
			load(*m_game_mode_file, "GameMode");
			m_engine.getFileSystem().close(m_game_mode_file);
			m_game_mode_file = NULL;
		}


		virtual EntityTemplateSystem& getEntityTemplateSystem() override
		{
			return *m_template_system;
		}


		virtual void cloneComponent(const Component& src, Entity& entity) override
		{
			Component clone = Component::INVALID;

			IPlugin* plugin = 0;
			if (m_creators.find(src.type, plugin))
			{
				clone = plugin->createComponent(src.type, entity);
			}
			else if (src.type == RENDERABLE_HASH || src.type == TERRAIN_HASH || src.type == CAMERA_HASH || src.type == LIGHT_HASH)
			{
				clone = m_engine.getRenderScene()->createComponent(src.type, entity);
			}
			else
			{
				ASSERT(false);
			}

			const Array<IPropertyDescriptor*>& properties = m_component_properties[src.type];
			Blob stream;
			for (int i = 0; i < properties.size(); ++i)
			{
				stream.clearBuffer();
				properties[i]->get(src, stream);
				stream.rewindForRead();
				properties[i]->set(clone, stream);
			}
		}


		virtual Component addComponent(uint32_t type_crc) override
		{
			Component cmp = Component::INVALID;
			if (m_selected_entity.isValid())
			{
				const Entity::ComponentList& cmps = m_selected_entity.getComponents();
				for (int i = 0; i < cmps.size(); ++i)
				{
					if (cmps[i].type == type_crc)
					{
						return cmps[i];
					}
				}

				IPlugin* plugin = 0;
				if (m_creators.find(type_crc, plugin))
				{
					cmp = plugin->createComponent(type_crc, m_selected_entity);
				}
				else if (type_crc == RENDERABLE_HASH || type_crc == TERRAIN_HASH || type_crc == CAMERA_HASH || type_crc == LIGHT_HASH)
				{
					cmp = m_engine.getRenderScene()->createComponent(type_crc, m_selected_entity);
				}
				else
				{
					ASSERT(false);
				}
				selectEntity(m_selected_entity);
			}
			return cmp;
		}


		virtual void lookAtSelected() override
		{
			if (m_selected_entity.isValid())
			{
				Matrix camera_mtx = m_camera.getMatrix();
				Vec3 dir = camera_mtx * Vec3(0, 0, 1);
				camera_mtx.setTranslation(m_selected_entity.getPosition() + dir * 10);
				m_camera.setMatrix(camera_mtx);
			}
		}


		virtual void loadUniverse(const Path& path) override
		{
			m_universe_path = path;
			g_log_info.log("editor server") << "Loading universe " << path.c_str() << "...";
			FS::FileSystem& fs = m_engine.getFileSystem();
			FS::ReadCallback file_read_cb;
			file_read_cb.bind<WorldEditorImpl, &WorldEditorImpl::loadMap>(this);
			fs.openAsync(fs.getDefaultDevice(), path, FS::Mode::OPEN | FS::Mode::READ, file_read_cb);
		}

		void loadMap(FS::IFile* file, bool success, FS::FileSystem& fs)
		{
			ASSERT(success);
			if (success)
			{
				resetAndLoad(*file, "unknown map"); /// TODO file path
			}

			fs.close(file);
		}

		virtual void getRelativePath(char* relative_path, int max_length, const Path& source) override
		{
			if (strncmp(m_base_path.c_str(), source.c_str(), m_base_path.length()) == 0)
			{
				strncpy(relative_path, source.c_str() + m_base_path.length(), max_length);
			}
			else
			{
				strncpy(relative_path, source.c_str(), max_length);
			}
		}

		virtual void newUniverse() override
		{
			m_universe_path = "";
			destroyUniverse();
			createUniverse(true);
			g_log_info.log("editor server") << "universe created";
		}


		void load(FS::IFile& file, const char* path)
		{
			g_log_info.log("editor server") << "parsing universe...";
			JsonSerializer serializer(file, JsonSerializer::READ, path);
			m_engine.deserialize(serializer);
			m_template_system->deserialize(serializer);
			m_camera = m_engine.getRenderScene()->getCameraInSlot("editor").entity;
			g_log_info.log("editor server") << "universe parsed";

			Universe* universe = m_engine.getUniverse();
			for (int i = 0; i < universe->getEntityCount(); ++i)
			{
				Entity e(universe, i);
				createEditorIcon(e);
			}
		}


		void createEditorIcon(const Entity& entity)
		{
			const Entity::ComponentList& cmps = entity.getComponents();

			bool found_renderable = false;
			for (int i = 0; i < cmps.size(); ++i)
			{
				if (cmps[i].type == RENDERABLE_HASH)
				{
					found_renderable = true;
					break;
				}
			}
			if (!found_renderable)
			{
				EditorIcon* er = LUMIX_NEW(EditorIcon)();
				er->create(m_engine, *m_engine.getRenderScene(), entity);
				m_editor_icons.push(er);
			}
		}

		void resetAndLoad(FS::IFile& file, const char* path)
		{
			destroyUniverse();
			createUniverse(false);
			load(file, path);
		}


		bool create(const char* base_path)
		{
			m_file_system = FS::FileSystem::create();
			m_tpc_file_server.start(base_path);
			m_base_path = base_path;

			m_tcp_file_device.connect("127.0.0.1", 10001);

			m_file_system->mount(&m_mem_file_device);
			m_file_system->mount(&m_disk_file_device);
			m_file_system->mount(&m_tcp_file_device);

			m_file_system->setDefaultDevice("memory:disk");
			m_file_system->setSaveGameDevice("memory:disk");

			if (!m_engine.create(base_path, m_file_system, this))
			{
				return false;
			}

			//glPopAttrib();

			if (!m_engine.loadPlugin("physics.dll"))
			{
				g_log_info.log("plugins") << "physics plugin has not been loaded";
			}
			if (!m_engine.loadPlugin("script.dll"))
			{
				g_log_info.log("plugins") << "script plugin has not been loaded";
			}
			/*if(!m_engine.loadPlugin("navigation.dll"))
			{
			g_log_info.log("plugins", "navigation plugin has not been loaded");
			}*/

			registerProperties();
			createUniverse(true);
			m_template_system = EntityTemplateSystem::create(*this);

			return true;
		}


		virtual Gizmo& getGizmo() override
		{
			return m_gizmo;
		}


		virtual FS::TCPFileServer& getTCPFileServer() override
		{
			return m_tpc_file_server;
		}


		virtual Component getEditCamera() const override
		{
			return m_camera.getComponent(CAMERA_HASH);
		}


		void destroy()
		{
			destroyUniverse();
			EntityTemplateSystem::destroy(m_template_system);
			m_engine.destroy();

			m_tcp_file_device.disconnect();
			m_tpc_file_server.stop();
			FS::FileSystem::destroy(m_file_system);
		}


		virtual void setWireframe(bool is_wireframe) override
		{
			m_engine.getRenderer().setEditorWireframe(is_wireframe);
		}


		virtual void renderIcons(IRenderDevice& render_device) override
		{
			PROFILE_FUNCTION();
			for (int i = 0, c = m_editor_icons.size(); i < c; ++i)
			{
				m_editor_icons[i]->render(&m_engine.getRenderer(), render_device);
			}

		}

		virtual void render(IRenderDevice& render_device) override
		{
			PROFILE_FUNCTION();
			m_engine.getRenderer().render(render_device);
		}


		WorldEditorImpl()
			: m_universe_mutex(false)
			, m_toggle_game_mode_requested(false)
		{
			m_mouse_handling_plugin = NULL;
			m_is_game_mode = false;
			m_selected_entity = Entity::INVALID;
			m_edit_view_render_device = NULL;
			m_universe_path = "";
			m_terrain_brush_size = 10;
			m_terrain_brush_strength = 0.01f;
		}


		virtual void navigate(float forward, float right, float speed) override
		{
			Vec3 pos = m_camera.getPosition();
			Quat rot = m_camera.getRotation();;
			pos += rot * Vec3(0, 0, -1) * forward * speed;
			pos += rot * Vec3(1, 0, 0) * right * speed;
			m_camera.setPosition(pos);
		}


		virtual Entity getSelectedEntity() const override
		{
			return m_selected_entity;
		}


		virtual const IPropertyDescriptor& getPropertyDescriptor(uint32_t type, uint32_t name_hash) override
		{
			Array<IPropertyDescriptor*>& props = m_component_properties[type];
			for (int i = 0; i < props.size(); ++i)
			{
				if (props[i]->getNameHash() == name_hash)
				{
					return *props[i];
				}
			}
			ASSERT(false);
			return *m_component_properties[type][0];
		}


		virtual void setProperty(const char* component, const char* property, const void* data, int size) override
		{
			if (m_selected_entity.isValid())
			{
				Blob stream;
				stream.create(data, size);
				uint32_t component_type = crc32(component);
				Component cmp = m_selected_entity.getComponent(component_type);

				if (cmp.isValid())
				{
					uint32_t template_hash = m_template_system->getTemplate(m_selected_entity);
					uint32_t property_name_hash = crc32(property);
					const IPropertyDescriptor& cp = getPropertyDescriptor(cmp.type, property_name_hash);
					if (template_hash)
					{
						const Array<Entity>& entities = m_template_system->getInstances(template_hash);
						for (int i = 0, c = entities.size(); i < c; ++i)
						{
							stream.rewindForRead();
							const Entity::ComponentList& cmps = entities[i].getComponents();
							for (int j = 0, cj = cmps.size(); j < cj; ++j)
							{
								if (cmps[j].type == cmp.type)
								{
									cp.set(cmps[j], stream);
									break;
								}
							}
						}
					}
					else
					{
						cp.set(cmp, stream);
					}
				}
			}
		}


		void rotateCamera(int x, int y)
		{
			Vec3 pos = m_camera.getPosition();
			Quat rot = m_camera.getRotation();

			Quat yaw_rot(Vec3(0, 1, 0), -x / 200.0f);
			rot = rot * yaw_rot;
			rot.normalize();

			Vec3 axis = rot * Vec3(1, 0, 0);
			Quat pitch_rot(axis, -y / 200.0f);
			rot = rot * pitch_rot;
			rot.normalize();

			Matrix camera_mtx;
			rot.toMatrix(camera_mtx);

			camera_mtx.setTranslation(pos);
			m_camera.setMatrix(camera_mtx);
		}

	
		void selectEntity(Entity e) 
		{
			m_selected_entity = e;
			m_gizmo.setEntity(e);
			m_entity_selected.invoke(e);
		}


		void onComponentCreated(Component& cmp)
		{
			for (int i = 0; i < m_editor_icons.size(); ++i)
			{
				if (m_editor_icons[i]->getEntity() == cmp.entity)
				{
					m_editor_icons[i]->destroy();
					LUMIX_DELETE(m_editor_icons[i]);
					m_editor_icons.eraseFast(i);
					break;
				}
			}
			createEditorIcon(cmp.entity);
		}


		void onComponentDestroyed(Component& cmp)
		{
			for (int i = 0; i < m_editor_icons.size(); ++i)
			{
				if (m_editor_icons[i]->getEntity() == cmp.entity)
				{
					m_editor_icons[i]->destroy();
					LUMIX_DELETE(m_editor_icons[i]);
					m_editor_icons.eraseFast(i);
					break;
				}
			}
			if (cmp.entity.existsInUniverse() && cmp.entity.getComponents().empty())
			{
				EditorIcon* er = LUMIX_NEW(EditorIcon)();
				er->create(m_engine, *m_engine.getRenderScene(), cmp.entity);
				m_editor_icons.push(er);
			}
		}


		void onEntityDestroyed(Entity& entity)
		{
			for (int i = 0; i < m_editor_icons.size(); ++i)
			{
				if (m_editor_icons[i]->getEntity() == entity)
				{
					m_editor_icons[i]->destroy();
					LUMIX_DELETE(m_editor_icons[i]);
					m_editor_icons.eraseFast(i);
					break;
				}
			}
		}


		void destroyUniverse()
		{
			m_universe_destroyed.invoke();
			for (int i = 0; i < m_editor_icons.size(); ++i)
			{
				m_editor_icons[i]->destroy();
				LUMIX_DELETE(m_editor_icons[i]);
			}
			selectEntity(Entity::INVALID);
			m_camera = Entity::INVALID;
			m_editor_icons.clear();
			m_gizmo.setUniverse(NULL);
			m_gizmo.destroy();
			m_engine.destroyUniverse();
		}


		virtual void setEditViewRenderDevice(IRenderDevice& render_device) override
		{
			m_edit_view_render_device = &render_device;
		}


		virtual Path getUniversePath() const override
		{
			return m_universe_path;
		}
	

		virtual DelegateList<void()>& universeCreated() override
		{
			return m_universe_created;
		}


		virtual DelegateList<void(Entity&)>& entitySelected() override
		{
			return m_entity_selected;
		}


		virtual DelegateList<void()>& universeDestroyed() override
		{
			return m_universe_destroyed;
		}



		void createUniverse(bool create_basic_entities)
		{
			Universe* universe = m_engine.createUniverse();
			if (create_basic_entities)
			{
				m_camera = m_engine.getUniverse()->createEntity();
				m_camera.setPosition(0, 0, -5);
				m_camera.setRotation(Quat(Vec3(0, 1, 0), -Math::PI));
				Component cmp = m_engine.getRenderScene()->createComponent(CAMERA_HASH, m_camera);
				RenderScene* scene = static_cast<RenderScene*>(cmp.system);
				scene->setCameraSlot(cmp, string("editor"));
			}
			m_gizmo.create(m_engine.getRenderer());
			m_gizmo.setUniverse(universe);
			m_gizmo.hide();

			universe->componentCreated().bind<WorldEditorImpl, &WorldEditorImpl::onComponentCreated>(this);
			universe->componentDestroyed().bind<WorldEditorImpl, &WorldEditorImpl::onComponentDestroyed>(this);
			universe->entityDestroyed().bind<WorldEditorImpl, &WorldEditorImpl::onEntityDestroyed>(this);

			m_selected_entity = Entity::INVALID;
			m_universe_created.invoke();

		}

	private:
		struct MouseMode
		{
			enum Value
			{
				NONE,
				SELECT,
				NAVIGATE,
				TRANSFORM,
				CUSTOM
			};
		};

		MT::Mutex m_universe_mutex;
		Gizmo m_gizmo;
		Entity m_selected_entity;
		Map<uint32_t, Array<IPropertyDescriptor*> > m_component_properties;
		Map<uint32_t, IPlugin*> m_creators;
		MouseMode::Value m_mouse_mode;
		Array<EditorIcon*> m_editor_icons;
		bool m_is_game_mode;
		FS::IFile* m_game_mode_file;
		Engine m_engine;
		Entity m_camera;
		DelegateList<void()> m_universe_destroyed;
		DelegateList<void()> m_universe_created;
		DelegateList<void(Entity&)> m_entity_selected;

		FS::FileSystem* m_file_system;
		FS::TCPFileServer m_tpc_file_server;
		FS::DiskFileDevice m_disk_file_device;
		FS::MemoryFileDevice m_mem_file_device;
		FS::TCPFileDevice m_tcp_file_device;
		IRenderDevice* m_edit_view_render_device;
		bool m_toggle_game_mode_requested;
		Path m_universe_path;
		Path m_base_path;
		int m_terrain_brush_size;
		float m_terrain_brush_strength;
		Array<Plugin*> m_plugins;
		Plugin* m_mouse_handling_plugin;
		EntityTemplateSystem* m_template_system;
};


WorldEditor* WorldEditor::create(const char* base_path)
{
	WorldEditorImpl* impl = LUMIX_NEW(WorldEditorImpl)();

	if (!impl->create(base_path))
	{
		LUMIX_DELETE(impl);
		return NULL;
	}

	return impl;
}


void WorldEditor::destroy(WorldEditor* server)
{
	static_cast<WorldEditorImpl*>(server)->destroy();
	LUMIX_DELETE(server);
}


} // !namespace Lumix
