#include "editor_server.h"

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
#include "editor/editor_icon.h"
#include "editor/gizmo.h"
#include "editor/property_descriptor.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "engine/plugin_manager.h"
#include "graphics/irender_device.h"
#include "graphics/model.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "core/input_system.h"
#include "core/MT/mutex.h"
#include "script/script_system.h"
#include "universe/universe.h"


namespace Lumix
{


struct EditorIconHit
{
	EditorIcon* m_icon;
	float m_t;
};


struct ServerMessageType
{
	enum
	{
		ENTITY_SELECTED = 1,
		PROPERTY_LIST = 2,
		ENTITY_POSITION = 3,
		LOG_MESSAGE = 4,
	};
};



struct EditorServerImpl
{
	public:
		struct MouseMode
		{
			enum Value
			{
				NONE,
				SELECT,
				NAVIGATE,
				TRANSFORM
			};
		};

		EditorServerImpl(EditorServer& server);
		~EditorServerImpl();

		bool create(const char* base_path);
		void destroy();
		void onPointerDown(int x, int y, MouseButton::Value button);
		void onPointerMove(int x, int y, int relx, int rely, int mouse_flags);
		void onPointerUp(int x, int y, MouseButton::Value button);
		void selectEntity(Entity e);
		void navigate(float forward, float right, float speed);
		void writeString(const char* str);
		void setProperty(const char* component, const char* property, const void* data, int size);
		void createUniverse(bool create_scene);
		void renderIcons(IRenderDevice& render_device);
		void renderScene(IRenderDevice& render_device);
		void renderPhysics();
		void save(const Path& path);
		void load(const Path& path);
		void loadMap(FS::IFile* file, bool success, FS::FileSystem& fs);
		void addComponent(uint32_t type_crc);
		void addEntity();
		void removeEntity();
		void toggleGameMode();
		void stopGameMode();
		void lookAtSelected();
		void newUniverse();
		Entity& getSelectedEntity() { return m_selected_entity; }
		bool isGameMode() const { return m_is_game_mode; }
		void save(FS::IFile& file, const char* path);
		void load(FS::IFile& file, const char* path);
		void resetAndLoad(FS::IFile& file, const char* path);
		EditorIconHit raycastEditorIcons(const Vec3& origin, const Vec3& dir);

		const IPropertyDescriptor& getPropertyDescriptor(uint32_t type, uint32_t name_hash);
		void registerProperties();
		void rotateCamera(int x, int y);
		void onEntityDestroyed(Entity& entity);
		void onComponentCreated(Component& cmp);
		void onComponentDestroyed(Component& cmp);
		void destroyUniverse();
		bool loadPlugin(const char* plugin_name);
		void setWireframe(bool is_wireframe);
		void createEditorIcon(const Entity& entity);

		MT::Mutex m_universe_mutex;
		Gizmo m_gizmo;
		Entity m_selected_entity;
		Blob m_stream;
		Map<uint32_t, Array<IPropertyDescriptor*> > m_component_properties;
		Map<uint32_t, IPlugin*> m_creators;
		MouseMode::Value m_mouse_mode;
		Array<EditorIcon*> m_editor_icons;
		bool m_is_game_mode;
		FS::IFile* m_game_mode_file;
		Engine m_engine;
		EditorServer& m_owner;
		Entity m_camera;
		DelegateList<void ()> m_universe_destroyed;
		DelegateList<void ()> m_universe_created;
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
};


static const uint32_t RENDERABLE_HASH = crc32("renderable");
static const uint32_t CAMERA_HASH = crc32("camera");
static const uint32_t LIGHT_HASH = crc32("light");
static const uint32_t SCRIPT_HASH = crc32("script");
static const uint32_t ANIMABLE_HASH = crc32("animable");
static const uint32_t TERRAIN_HASH = crc32("terrain");


void EditorServer::render(IRenderDevice& render_device)
{
	m_impl->renderScene(render_device);
}


const char* EditorServer::getBasePath()
{
	return m_impl->m_base_path.c_str();
}


void EditorServer::renderIcons(IRenderDevice& render_device)
{
	PROFILE_FUNCTION();
	m_impl->renderIcons(render_device);
}



Engine& EditorServer::getEngine()
{
	return m_impl->m_engine;
}


void EditorServer::registerProperty(const char* component_type, IPropertyDescriptor* descriptor)
{
	ASSERT(descriptor);
	m_impl->m_component_properties[crc32(component_type)].push(descriptor);
}


void EditorServer::registerCreator(uint32_t type, IPlugin& creator)
{
	m_impl->m_creators.insert(type, &creator);
}


void EditorServer::tick()
{
	if(m_impl->m_toggle_game_mode_requested)
	{
		m_impl->toggleGameMode();
		m_impl->m_toggle_game_mode_requested = false;
	}
	PROFILE_FUNCTION();
	m_impl->m_engine.update(m_impl->m_is_game_mode);
	m_impl->m_engine.getFileSystem().updateAsyncTransactions();
}


bool EditorServer::create(const char* base_path)
{
	m_impl = LUMIX_NEW(EditorServerImpl)(*this);
	
	if(!m_impl->create(base_path))
	{
		LUMIX_DELETE(m_impl);
		m_impl = NULL;
		return false;
	}

	return true;
}


void EditorServer::destroy()
{
	if (m_impl)
	{
		m_impl->destroy();
		LUMIX_DELETE(m_impl);
		m_impl = NULL;
	}
}


EditorServerImpl::~EditorServerImpl()
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


void EditorServerImpl::registerProperties()
{
	m_component_properties[CAMERA_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("slot"), &RenderScene::getCameraSlot, &RenderScene::setCameraSlot, IPropertyDescriptor::STRING));
	m_component_properties[CAMERA_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("fov"), &RenderScene::getCameraFOV, &RenderScene::setCameraFOV));
	m_component_properties[CAMERA_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("near"), &RenderScene::getCameraNearPlane, &RenderScene::setCameraNearPlane));
	m_component_properties[CAMERA_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("far"), &RenderScene::getCameraFarPlane, &RenderScene::setCameraFarPlane));
	m_component_properties[RENDERABLE_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("source"), &RenderScene::getRenderablePath, &RenderScene::setRenderablePath, IPropertyDescriptor::FILE));
	m_component_properties[TERRAIN_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("material"), &RenderScene::getTerrainMaterial, &RenderScene::setTerrainMaterial, IPropertyDescriptor::FILE));
	m_component_properties[TERRAIN_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("xz_scale"), &RenderScene::getTerrainXZScale, &RenderScene::setTerrainXZScale));
	m_component_properties[TERRAIN_HASH].push(LUMIX_NEW(PropertyDescriptor<RenderScene>)(crc32("y_scale"), &RenderScene::getTerrainYScale, &RenderScene::setTerrainYScale));
	/*m_component_properties[renderable_type].push(LUMIX_NEW(PropertyDescriptor<Renderer>)(crc32("visible"), &Renderer::getVisible, &Renderer::setVisible));
	m_component_properties[renderable_type].push(LUMIX_NEW(PropertyDescriptor<Renderer>)(crc32("cast shadows"), &Renderer::getCastShadows, &Renderer::setCastShadows));
	m_component_properties[point_light_type].push(LUMIX_NEW(PropertyDescriptor<Renderer>)(crc32("fov"), &Renderer::getLightFov, &Renderer::setLightFov));
	m_component_properties[point_light_type].push(LUMIX_NEW(PropertyDescriptor<Renderer>)(crc32("radius"), &Renderer::getLightRadius, &Renderer::setLightRadius));
	*/
}


EditorIconHit EditorServerImpl::raycastEditorIcons(const Vec3& origin, const Vec3& dir)
{
	EditorIconHit hit;
	hit.m_t = -1;
	for (int i = 0, c = m_editor_icons.size(); i < c;  ++i)
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


void EditorServerImpl::onPointerDown(int x, int y, MouseButton::Value button)
{
	if(button == MouseButton::RIGHT)
	{
		m_mouse_mode = EditorServerImpl::MouseMode::NAVIGATE;
	}
	else if(button == MouseButton::LEFT)
	{
		Vec3 origin, dir;
		Component camera_cmp = m_camera.getComponent(CAMERA_HASH);
		RenderScene* scene = static_cast<RenderScene*>(camera_cmp.system);
		scene->getRay(camera_cmp, (float)x, (float)y, origin, dir);
		RayCastModelHit hit = scene->castRay(origin, dir);
		RayCastModelHit gizmo_hit = m_gizmo.castRay(origin, dir);
		EditorIconHit icon_hit = raycastEditorIcons(origin, dir);
		if (gizmo_hit.m_is_hit && (icon_hit.m_t < 0 || gizmo_hit.m_t < icon_hit.m_t))
		{
			if (m_selected_entity.isValid())
			{
				m_mouse_mode = EditorServerImpl::MouseMode::TRANSFORM;
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
		else if(hit.m_is_hit)
		{
			selectEntity(hit.m_component.entity);
			m_mouse_mode = EditorServerImpl::MouseMode::TRANSFORM;
			m_gizmo.startTransform(m_camera.getComponent(CAMERA_HASH), x, y, Gizmo::TransformMode::CAMERA_XZ);
		}
	}
}


void EditorServerImpl::onPointerMove(int x, int y, int relx, int rely, int mouse_flags)
{
	switch(m_mouse_mode)
	{
		case EditorServerImpl::MouseMode::NAVIGATE:
			{
				rotateCamera(relx, rely);
			}
			break;
		case EditorServerImpl::MouseMode::TRANSFORM:
			{
				Gizmo::TransformOperation tmode = mouse_flags & (int)EditorServer::MouseFlags::ALT/*GetKeyState(VK_MENU) & 0x8000*/ ? Gizmo::TransformOperation::ROTATE : Gizmo::TransformOperation::TRANSLATE;
				int flags = mouse_flags & (int)EditorServer::MouseFlags::CONTROL/*GetKeyState(VK_LCONTROL) & 0x8000*/ ? Gizmo::Flags::FIXED_STEP : 0;
				m_gizmo.transform(m_camera.getComponent(CAMERA_HASH), tmode, x, y, relx, rely, flags);
			}
			break;
	}
}


void EditorServerImpl::onPointerUp(int, int, MouseButton::Value)
{
	m_mouse_mode = EditorServerImpl::MouseMode::NONE;
}


void EditorServerImpl::save(const Path& path)
{
	g_log_info.log("editor server") << "saving universe " << path.c_str() << "...";
	FS::FileSystem& fs = m_engine.getFileSystem();
	FS::IFile* file = fs.open(fs.getDefaultDevice(), path, FS::Mode::OPEN_OR_CREATE | FS::Mode::WRITE);
	save(*file, path.c_str());
	fs.close(file);
	m_universe_path = path;
}


void EditorServerImpl::save(FS::IFile& file, const char* path)
{
	JsonSerializer serializer(file, JsonSerializer::WRITE, path);
	m_engine.serialize(serializer);
	g_log_info.log("editor server") << "universe saved";
}


void EditorServerImpl::addEntity()
{
	Entity e = m_engine.getUniverse()->createEntity();

	RenderScene* scene = m_engine.getRenderScene();
	Vec3 origin = m_camera.getPosition();
	Vec3 dir = -m_camera.getMatrix().getZVector();
	RayCastModelHit hit = scene->castRay(origin, dir);
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
}


void EditorServerImpl::toggleGameMode()
{
	if(m_is_game_mode)
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


void EditorServerImpl::stopGameMode()
{
	m_is_game_mode = false;
	m_game_mode_file->seek(FS::SeekMode::BEGIN, 0);
	load(*m_game_mode_file, "GameMode");
	m_engine.getFileSystem().close(m_game_mode_file);
	m_game_mode_file = NULL;
}


void EditorServerImpl::addComponent(uint32_t type_crc)
{
	if(m_selected_entity.isValid())
	{
		const Entity::ComponentList& cmps = m_selected_entity.getComponents();
		for(int i = 0; i < cmps.size(); ++i)
		{
			if(cmps[i].type == type_crc)
			{
				return;
			}
		}

		IPlugin* plugin = 0;
		if(m_creators.find(type_crc, plugin))
		{
			plugin->createComponent(type_crc, m_selected_entity);
		}
		else if(type_crc == RENDERABLE_HASH || type_crc == TERRAIN_HASH || type_crc == CAMERA_HASH || type_crc == LIGHT_HASH)
		{
			m_engine.getRenderScene()->createComponent(type_crc, m_selected_entity);
		}
		else
		{
			ASSERT(false);
		}
		selectEntity(m_selected_entity);
	}
}


void EditorServerImpl::lookAtSelected()
{
	if(m_selected_entity.isValid())
	{
		Matrix camera_mtx = m_camera.getMatrix();
		Vec3 dir = camera_mtx * Vec3(0, 0, 1);
		camera_mtx.setTranslation(m_selected_entity.getPosition() + dir * 10);
		m_camera.setMatrix(camera_mtx);
	}
}


void EditorServerImpl::removeEntity()
{
	m_engine.getUniverse()->destroyEntity(m_selected_entity);
	selectEntity(Lumix::Entity::INVALID);
}


void EditorServerImpl::load(const Path& path)
{
	m_universe_path = path;
	g_log_info.log("editor server") << "Loading universe " << path.c_str() << "...";
	FS::FileSystem& fs = m_engine.getFileSystem();
	FS::ReadCallback file_read_cb;
	file_read_cb.bind<EditorServerImpl, &EditorServerImpl::loadMap>(this);
	fs.openAsync(fs.getDefaultDevice(), path, FS::Mode::OPEN | FS::Mode::READ, file_read_cb);
}

void EditorServerImpl::loadMap(FS::IFile* file, bool success, FS::FileSystem& fs)
{
	ASSERT(success);
	if(success)
	{
		resetAndLoad(*file, "unknown map"); /// TODO file path
	}

	fs.close(file);
}

void EditorServerImpl::newUniverse()
{
	m_universe_path = "";
	destroyUniverse();
	createUniverse(true);
	g_log_info.log("editor server") << "universe created";
}


void EditorServerImpl::load(FS::IFile& file, const char* path)
{
	g_log_info.log("editor server") << "parsing universe...";
	JsonSerializer serializer(file, JsonSerializer::READ, path);
	m_engine.deserialize(serializer);
	m_camera = m_engine.getRenderScene()->getCameraInSlot("editor").entity;
	g_log_info.log("editor server") << "universe parsed";

	Universe* universe = m_engine.getUniverse();
	for (int i = 0; i < universe->getEntityCount(); ++i)
	{
		Entity e(universe, i);
		createEditorIcon(e);
	}
}


void EditorServerImpl::createEditorIcon(const Entity& entity)
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

void EditorServerImpl::resetAndLoad(FS::IFile& file, const char* path)
{
	destroyUniverse();
	createUniverse(false);
	load(file, path);
}


bool EditorServerImpl::create(const char* base_path)
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

	if(!m_engine.create(base_path, m_file_system, &m_owner))
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

	return true;
}


Gizmo& EditorServer::getGizmo()
{
	return m_impl->m_gizmo;
}


FS::TCPFileServer& EditorServer::getTCPFileServer()
{
	return m_impl->m_tpc_file_server;
}


Component EditorServer::getEditCamera() const
{
	return m_impl->m_camera.getComponent(CAMERA_HASH);
}


void EditorServerImpl::destroy()
{

	destroyUniverse();
	m_engine.destroy();

	m_tcp_file_device.disconnect();
	m_tpc_file_server.stop();
	FS::FileSystem::destroy(m_file_system);
}


void EditorServerImpl::setWireframe(bool is_wireframe)
{
	m_engine.getRenderer().setEditorWireframe(is_wireframe);
}


void EditorServerImpl::renderIcons(IRenderDevice& render_device)
{
	for(int i = 0, c = m_editor_icons.size(); i < c; ++i)
	{
		m_editor_icons[i]->render(&m_engine.getRenderer(), render_device);
	}

}

void EditorServerImpl::renderScene(IRenderDevice& render_device)
{
	PROFILE_FUNCTION();
	m_engine.getRenderer().render(render_device);
}


EditorServerImpl::EditorServerImpl(EditorServer& server)
	: m_universe_mutex(false)
	, m_toggle_game_mode_requested(false)
	, m_owner(server)
{
	m_is_game_mode = false;
	m_selected_entity = Entity::INVALID;
	m_edit_view_render_device = NULL;
	m_universe_path = "";
}


void EditorServerImpl::navigate(float forward, float right, float speed)
{
	Vec3 pos = m_camera.getPosition();
	Quat rot = m_camera.getRotation();;
	pos += rot * Vec3(0, 0, -1) * forward * speed;
	pos += rot * Vec3(1, 0, 0) * right * speed;
	m_camera.setPosition(pos);
}


const IPropertyDescriptor& EditorServer::getPropertyDescriptor(uint32_t type, uint32_t name_hash)
{
	return m_impl->getPropertyDescriptor(type, name_hash);
}


Entity EditorServer::getSelectedEntity() const
{
	return m_impl->m_selected_entity;
}


const IPropertyDescriptor& EditorServerImpl::getPropertyDescriptor(uint32_t type, uint32_t name_hash)
{
	Array<IPropertyDescriptor*>& props = m_component_properties[type];
	for(int i = 0; i < props.size(); ++i)
	{
		if(props[i]->getNameHash() == name_hash)
		{
			return *props[i];
		}
	}
	ASSERT(false);
	return *m_component_properties[type][0];
}


void EditorServerImpl::setProperty(const char* component, const char* property, const void* data, int size)
{
	if (m_selected_entity.isValid())
	{
		Blob stream;
		stream.create(data, size);
		uint32_t component_type = crc32(component);
		Component cmp = m_selected_entity.getComponent(component_type);

		if (cmp.isValid())
		{
			uint32_t name_hash = crc32(property);
			const IPropertyDescriptor& cp = getPropertyDescriptor(cmp.type, name_hash);
			cp.set(cmp, stream);
		}
	}
}


void EditorServerImpl::rotateCamera(int x, int y)
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


void EditorServerImpl::writeString(const char* str)
{
	int32_t len = (int32_t)strlen(str);
	m_stream.write(len);
	m_stream.write(str, len);
}


void EditorServerImpl::selectEntity(Entity e)
{
	m_selected_entity = e;
	m_gizmo.setEntity(e);
	m_entity_selected.invoke(e);
}


void EditorServerImpl::onComponentCreated(Component& cmp)
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


void EditorServerImpl::onComponentDestroyed(Component& cmp)
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
	if(cmp.entity.existsInUniverse() && cmp.entity.getComponents().empty())
	{
		EditorIcon* er = LUMIX_NEW(EditorIcon)();
		er->create(m_engine, *m_engine.getRenderScene(), cmp.entity);
		m_editor_icons.push(er);
	}
}


void EditorServerImpl::onEntityDestroyed(Entity& entity)
{
	for(int i = 0; i < m_editor_icons.size(); ++i)
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


void EditorServerImpl::destroyUniverse()
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


void EditorServer::setEditViewRenderDevice(IRenderDevice& render_device)
{
	m_impl->m_edit_view_render_device = &render_device;
}


void EditorServer::loadUniverse(const Path& path)
{
	m_impl->load(path);
}

void EditorServer::saveUniverse(const Path& path)
{
	m_impl->save(path);
}

void EditorServer::newUniverse()
{
	m_impl->newUniverse();
}

Path EditorServer::getUniversePath() const
{
	return m_impl->m_universe_path;
}

void EditorServer::addComponent(uint32_t type_crc)
{
	m_impl->addComponent(type_crc);
}


void EditorServer::addEntity()
{
	m_impl->addEntity();
}

void EditorServer::toggleGameMode()
{
	m_impl->toggleGameMode();
}

void EditorServer::setWireframe(bool is_wireframe)
{
	m_impl->setWireframe(is_wireframe);
}


void EditorServer::lookAtSelected()
{
	m_impl->lookAtSelected();
}


void EditorServer::navigate(float forward, float right, float speed)
{
	m_impl->navigate(forward, right, speed);
}

void EditorServer::setProperty(const char* component, const char* property, const void* data, int size)
{
	m_impl->setProperty(component, property, data, size);
}


void EditorServer::onMouseDown(int x, int y, MouseButton::Value button)
{
	m_impl->onPointerDown(x, y, button);
}

void EditorServer::onMouseMove(int x, int y, int relx, int rely, int mouse_flags)
{
	m_impl->onPointerMove(x, y, relx, rely, mouse_flags);
}

void EditorServer::onMouseUp(int x, int y, MouseButton::Value button)
{
	m_impl->onPointerUp(x, y, button);
}


DelegateList<void ()>& EditorServer::universeCreated()
{
	return m_impl->m_universe_created;
}


DelegateList<void(Entity&)>& EditorServer::entitySelected()
{
	return m_impl->m_entity_selected;
}


DelegateList<void ()>& EditorServer::universeDestroyed()
{
	return m_impl->m_universe_destroyed;
}



void EditorServerImpl::createUniverse(bool create_basic_entities)
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

	universe->componentCreated().bind<EditorServerImpl, &EditorServerImpl::onComponentCreated>(this);
	universe->componentDestroyed().bind<EditorServerImpl, &EditorServerImpl::onComponentDestroyed>(this);
	universe->entityDestroyed().bind<EditorServerImpl, &EditorServerImpl::onEntityDestroyed>(this);

	m_selected_entity = Entity::INVALID;
	m_universe_created.invoke();

}





} // !namespace Lumix
