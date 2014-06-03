#include "editor_server.h"

#include <gl/GL.h>

#include "core/array.h"
#include "core/blob.h"
#include "core/crc32.h"
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
#include "core/MT/mutex.h"
#include "core/MT/task.h"
#include "core/Net/tcp_acceptor.h"
#include "core/Net/tcp_stream.h"
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
#include "core/MT/task.h"
#include "core/net/tcp_acceptor.h"
#include "core/net/tcp_stream.h"
#include "script/script_system.h"
#include "universe/component_event.h"
#include "universe/entity_destroyed_event.h"
#include "universe/entity_moved_event.h"
#include "universe/universe.h"


namespace Lux
{


struct EditorIconHit
{
	EditorIcon* m_icon;
	float m_t;
};


struct ClientMessageType
{
	enum 
	{
		POINTER_DOWN = 1,
		POINTER_MOVE,
		POINTER_UP,
		PROPERTY_SET,
		MOVE_CAMERA,			// 5
		SAVE,
		LOAD,
		ADD_COMPONENT = 8,
		GET_PROPERTIES = 9,
		REMOVE_COMPONENT = 10,
		ADD_ENTITY,				// 11
		TOGGLE_GAME_MODE,		// 12
		GET_POSITION,			// 13
		SET_POSITION,			// 14
		REMOVE_ENTITY,			// 15
		SET_EDIT_MODE,			// 16
								// 17
								// 18
		NEW_UNIVERSE = 19,		// 19
		LOOK_AT_SELECTED = 20,	// 20
		STOP_GAME_MODE,			// 21
	};
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



struct MouseButton
{
	enum Value
	{
		LEFT,
		MIDDLE,
		RIGHT
	};
};


class MessageTask : public MT::Task
{
	public:
		virtual int task() override;

		struct EditorServerImpl* m_server;
		Net::TCPAcceptor m_acceptor;
		Net::TCPStream* m_stream;
		bool m_is_finished;
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

		EditorServerImpl();
		~EditorServerImpl();

		bool create(const char* base_path);
		void destroy();
		void onPointerDown(int x, int y, MouseButton::Value button);
		void onPointerMove(int x, int y, int relx, int rely);
		void onPointerUp(int x, int y, MouseButton::Value button);
		void selectEntity(Entity e);
		void navigate(float forward, float right, int fast);
		void writeString(const char* str);
		void setProperty(void* data, int size);
		void createUniverse(bool create_scene);
		void renderIcons(IRenderDevice& render_device);
		void renderScene(IRenderDevice& render_device);
		void renderPhysics();
		void save(const char* path);
		void load(const char* path);
		void loadMap(FS::IFile* file, bool success, FS::FileSystem& fs);
		void addComponent(uint32_t type_crc);
		void sendComponent(uint32_t type_crc);
		void removeComponent(uint32_t type_crc);
		void sendEntityPosition(int uid);
		void setEntityPosition(int uid, float* pos);
		void addEntity();
		void removeEntity();
		void toggleGameMode();
		void stopGameMode();
		void lookAtSelected();
		void newUniverse();
		void logMessage(int32_t type, const char* system, const char* msg);
		Entity& getSelectedEntity() { return m_selected_entity; }
		bool isGameMode() const { return m_is_game_mode; }
		void save(FS::IFile& file, const char* path);
		void load(FS::IFile& file, const char* path);
		void onMessage(void* msgptr, int size);
		EditorIconHit raycastEditorIcons(const Vec3& origin, const Vec3& dir);

		const IPropertyDescriptor& getPropertyDescriptor(uint32_t type, uint32_t name_hash);
		void registerProperties();
		void rotateCamera(int x, int y);
		void onEvent(Event& evt);
		void onComponentEvent(Event& e);
		void destroyUniverse();
		bool loadPlugin(const char* plugin_name);
		void onLogInfo(const char* system, const char* message);
		void onLogWarning(const char* system, const char* message);
		void onLogError(const char* system, const char* message);
		void sendMessage(const uint8_t* data, int32_t length);

		MT::Mutex m_universe_mutex;
		MT::Mutex m_send_mutex;
		Gizmo m_gizmo;
		Entity m_selected_entity;
		Blob m_stream;
		Map<uint32_t, Array<IPropertyDescriptor*> > m_component_properties;
		Map<uint32_t, IPlugin*> m_creators;
		MouseMode::Value m_mouse_mode;
		Array<EditorIcon*> m_editor_icons;
		bool m_is_game_mode;
		FS::IFile* m_game_mode_file;
		MessageTask* m_message_task;
		Engine m_engine;
		EditorServer* m_owner;
		Entity m_camera;

		FS::FileSystem* m_file_system;
		FS::TCPFileServer m_tpc_file_server;
		FS::DiskFileDevice m_disk_file_device;
		FS::MemoryFileDevice m_mem_file_device;
		FS::TCPFileDevice m_tcp_file_device;
		IRenderDevice* m_edit_view_render_device;
		bool m_toggle_game_mode_requested;
};


static const uint32_t renderable_type = crc32("renderable");
static const uint32_t camera_type = crc32("camera");
static const uint32_t light_type = crc32("light");
static const uint32_t script_type = crc32("script");
static const uint32_t animable_type = crc32("animable");


void EditorServer::render(IRenderDevice& render_device)
{
	m_impl->renderScene(render_device);
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
	if(m_impl->m_is_game_mode)
	{
		m_impl->m_engine.update();
	}
	m_impl->m_engine.getFileSystem().updateAsyncTransactions();
}


bool EditorServer::create(const char* base_path)
{
	m_impl = LUX_NEW(EditorServerImpl)();
	m_impl->m_owner = this;
	
	if(!m_impl->create(base_path))
	{
		LUX_DELETE(m_impl);
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
		LUX_DELETE(m_impl);
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
			LUX_DELETE(iter.second()[i]);
		}
		++iter;
	}
}


void EditorServerImpl::registerProperties()
{
	m_component_properties[camera_type].push(LUX_NEW(PropertyDescriptor<RenderScene>)(crc32("slot"), &RenderScene::getCameraSlot, &RenderScene::setCameraSlot, IPropertyDescriptor::STRING));
	m_component_properties[camera_type].push(LUX_NEW(PropertyDescriptor<RenderScene>)(crc32("fov"), &RenderScene::getCameraFOV, &RenderScene::setCameraFOV));
	m_component_properties[camera_type].push(LUX_NEW(PropertyDescriptor<RenderScene>)(crc32("near"), &RenderScene::getCameraNearPlane, &RenderScene::setCameraNearPlane));
	m_component_properties[camera_type].push(LUX_NEW(PropertyDescriptor<RenderScene>)(crc32("far"), &RenderScene::getCameraFarPlane, &RenderScene::setCameraFarPlane));
	m_component_properties[renderable_type].push(LUX_NEW(PropertyDescriptor<RenderScene>)(crc32("source"), &RenderScene::getRenderablePath, &RenderScene::setRenderablePath, IPropertyDescriptor::FILE));
	/*m_component_properties[renderable_type].push(LUX_NEW(PropertyDescriptor<Renderer>)(crc32("visible"), &Renderer::getVisible, &Renderer::setVisible));
	m_component_properties[renderable_type].push(LUX_NEW(PropertyDescriptor<Renderer>)(crc32("cast shadows"), &Renderer::getCastShadows, &Renderer::setCastShadows));
	m_component_properties[point_light_type].push(LUX_NEW(PropertyDescriptor<Renderer>)(crc32("fov"), &Renderer::getLightFov, &Renderer::setLightFov));
	m_component_properties[point_light_type].push(LUX_NEW(PropertyDescriptor<Renderer>)(crc32("radius"), &Renderer::getLightRadius, &Renderer::setLightRadius));
	*/m_component_properties[script_type].push(LUX_NEW(PropertyDescriptor<ScriptSystem>)(crc32("source"), &ScriptSystem::getScriptPath, &ScriptSystem::setScriptPath, IPropertyDescriptor::FILE));
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
		Component camera_cmp = m_camera.getComponent(camera_type);
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
			selectEntity(hit.m_renderable.entity);
			m_mouse_mode = EditorServerImpl::MouseMode::TRANSFORM;
			m_gizmo.startTransform(m_camera.getComponent(camera_type), x, y, Gizmo::TransformMode::CAMERA_XZ);
		}
	}
}


void EditorServerImpl::onPointerMove(int x, int y, int relx, int rely)
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
				
				Gizmo::TransformOperation tmode = GetKeyState(VK_MENU) & 0x8000 ? Gizmo::TransformOperation::ROTATE : Gizmo::TransformOperation::TRANSLATE;
				int flags = GetKeyState(VK_LCONTROL) & 0x8000 ? Gizmo::Flags::FIXED_STEP : 0;
				m_gizmo.transform(m_camera.getComponent(camera_type), tmode, x, y, relx, rely, flags);
			}
			break;
	}
}


void EditorServerImpl::onPointerUp(int, int, MouseButton::Value)
{
	m_mouse_mode = EditorServerImpl::MouseMode::NONE;
}


void EditorServerImpl::save(const char* path)
{
	g_log_info.log("editor server", "saving universe %s...", path);
	FS::FileSystem& fs = m_engine.getFileSystem();
	FS::IFile* file = fs.open(fs.getDefaultDevice(), path, FS::Mode::OPEN_OR_CREATE | FS::Mode::WRITE);
	save(*file, path);
	fs.close(file);
}


void EditorServerImpl::save(FS::IFile& file, const char* path)
{
	JsonSerializer serializer(file, JsonSerializer::WRITE, path);
	m_engine.serialize(serializer);
	g_log_info.log("editor server", "universe saved");
}


void EditorServerImpl::addEntity()
{
	Entity e = m_engine.getUniverse()->createEntity();
	e.setPosition(m_camera.getPosition() + m_camera.getRotation() * Vec3(0, 0, -2));
	selectEntity(e);
	EditorIcon* er = LUX_NEW(EditorIcon)();
	er->create(m_engine, *m_engine.getRenderScene(), m_selected_entity, Component::INVALID);
	m_editor_icons.push(er);
}


int MessageTask::task()
{
	m_is_finished = false;
	m_acceptor.start("127.0.0.1", 10013);
	m_stream = m_acceptor.accept();
	Array<uint8_t> data;
	data.resize(5);
	while(!m_is_finished)
	{
		if(m_stream->read(&data[0], 5))
		{
			int length = *(int*)&data[0];
			if(length > 0)
			{
				data.resize(length);
				m_stream->read(&data[0], length);
				MT::Lock lock(m_server->m_universe_mutex);
				m_server->onMessage(&data[0], data.size());
			}
		}
	}
	return 1;
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
		m_engine.getScriptSystem().start();
		m_is_game_mode = true;
	}
}


void EditorServerImpl::stopGameMode()
{
	m_is_game_mode = false;
	m_engine.getScriptSystem().stop();
	m_game_mode_file->seek(FS::SeekMode::BEGIN, 0);
	load(*m_game_mode_file, "GameMode");
	m_engine.getFileSystem().close(m_game_mode_file);
	m_game_mode_file = NULL;
}


void EditorServerImpl::sendComponent(uint32_t type_crc)
{
	if(m_selected_entity.isValid())
	{
		m_stream.flush();
		m_stream.write(ServerMessageType::PROPERTY_LIST);
		const Entity::ComponentList& cmps = m_selected_entity.getComponents();
		for(int i = 0; i < cmps.size(); ++i)
		{
			if(cmps[i].type == type_crc)
			{
				Array<IPropertyDescriptor*>& props = m_component_properties[cmps[i].type];
				m_stream.write(props.size());
				m_stream.write(type_crc);
				for(int j = 0; j < props.size(); ++j)
				{
					m_stream.write(props[j]->getNameHash());
					props[j]->get(cmps[i], m_stream);
				}
				break;
			}
		}
		sendMessage(m_stream.getBuffer(), m_stream.getBufferSize());
	}
}


void EditorServerImpl::setEntityPosition(int uid, float* pos)
{
	Entity e(m_engine.getUniverse(), uid);
	e.setPosition(pos[0], pos[1], pos[2]);
}


void EditorServerImpl::sendEntityPosition(int uid)
{
	Entity entity(m_engine.getUniverse(), uid);
	if(entity.isValid())
	{
		m_stream.flush();
		m_stream.write(ServerMessageType::ENTITY_POSITION);
		m_stream.write(entity.index);
		m_stream.write(entity.getPosition().x);
		m_stream.write(entity.getPosition().y);
		m_stream.write(entity.getPosition().z);
		sendMessage(m_stream.getBuffer(), m_stream.getBufferSize());
	}
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
		else if(type_crc == renderable_type)
		{
			m_engine.getRenderScene()->createComponent(renderable_type, m_selected_entity);
		}
		else if(type_crc == light_type)
		{
			m_engine.getRenderScene()->createComponent(light_type, m_selected_entity);
		}
		else if(type_crc == script_type)
		{
			m_engine.getScriptSystem().createScript(m_selected_entity);
		}
		else if (type_crc == camera_type)
		{
			m_engine.getRenderScene()->createComponent(camera_type, m_selected_entity);
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
	/*if(m_selected_entity.isValid())
	{
		Vec3 dir = m_camera_rot * Vec3(0, 0, 1);
		m_camera_pos = m_selected_entity.getPosition() + dir * 10;
		Matrix mtx;
		m_camera_rot.toMatrix(mtx);
		mtx.setTranslation(m_camera_pos);
		m_engine.getRenderer().setCameraMatrix(mtx);
	}*/
}


void EditorServerImpl::removeEntity()
{
	m_engine.getUniverse()->destroyEntity(m_selected_entity);
	selectEntity(Lux::Entity::INVALID);
}


void EditorServerImpl::removeComponent(uint32_t)
{
	/*const Entity::ComponentList& cmps = m_selected_entity.getComponents();
	Component cmp;
	for(int i = 0; i < cmps.size(); ++i)
	{
		if(cmps[i].type == type_crc)
		{
			cmp = cmps[i];
			break;
		}
	}
	if(type_crc == renderable_type)
	{
		m_engine.getRenderer().destroyRenderable(cmp);
	}
	else if(type_crc == point_light_type)
	{
		m_engine.getRenderer().destroyPointLight(cmp);
	}
	else
	{
		ASSERT(false);
	}
	selectEntity(m_selected_entity);*/
}

void EditorServerImpl::load(const char* path)
{
	g_log_info.log("editor server", "loading universe %s...", path);
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
		load(*file, "unknown map"); /// TODO file path
	}

	fs.close(file);
}

void EditorServerImpl::newUniverse()
{
	destroyUniverse();
	createUniverse(false);
	g_log_info.log("editor server", "universe created");
}


void EditorServerImpl::load(FS::IFile& file, const char* path)
{
	g_log_info.log("editor server", "parsing universe...");
	destroyUniverse();
	createUniverse(false);
	JsonSerializer serializer(file, JsonSerializer::READ, path);
	m_engine.deserialize(serializer);
	m_camera = m_engine.getRenderScene()->getCameraInSlot("editor").entity;
	g_log_info.log("editor server", "universe parsed");
}


bool EditorServerImpl::create(const char* base_path)
{
	m_message_task = LUX_NEW(MessageTask)();
	m_message_task->m_server = this;
	m_message_task->m_stream = NULL;
	m_message_task->create("Message Task");
	m_message_task->run();

	m_file_system = FS::FileSystem::create();
	m_tpc_file_server.start(base_path);

	m_tcp_file_device.connect("127.0.0.1", 10001);

	m_file_system->mount(&m_mem_file_device);
	m_file_system->mount(&m_disk_file_device);
	m_file_system->mount(&m_tcp_file_device);

	m_file_system->setDefaultDevice("memory:tcp");
	m_file_system->setSaveGameDevice("memory:tcp");
	
	g_log_info.getCallback().bind<EditorServerImpl, &EditorServerImpl::onLogInfo>(this);
	g_log_warning.getCallback().bind<EditorServerImpl, &EditorServerImpl::onLogWarning>(this);
	g_log_error.getCallback().bind<EditorServerImpl, &EditorServerImpl::onLogError>(this);

	if(!m_engine.create(base_path, m_file_system, m_owner))
	{
		return false;
	}

	//glPopAttrib();
	
	if(!m_engine.loadPlugin("physics.dll"))
	{
		g_log_info.log("plugins", "physics plugin has not been loaded");
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
	return m_impl->m_camera.getComponent(camera_type);
}


void EditorServerImpl::destroy()
{
	m_message_task->m_is_finished = true;
	LUX_DELETE(m_message_task->m_stream);
	m_message_task->destroy();

	destroyUniverse();
	m_engine.destroy();
	g_log_info.getCallback().unbind<EditorServerImpl, &EditorServerImpl::onLogInfo>(this);
	g_log_warning.getCallback().unbind<EditorServerImpl, &EditorServerImpl::onLogWarning>(this);
	g_log_error.getCallback().unbind<EditorServerImpl, &EditorServerImpl::onLogError>(this);

	m_tcp_file_device.disconnect();
	m_tpc_file_server.stop();
	FS::FileSystem::destroy(m_file_system);
}


void EditorServerImpl::onLogInfo(const char* system, const char* message)
{
	logMessage(0, system, message);
}


void EditorServerImpl::onLogWarning(const char* system, const char* message)
{
	logMessage(1, system, message);
}


void EditorServerImpl::onLogError(const char* system, const char* message)
{
	logMessage(2, system, message);
}


void EditorServerImpl::sendMessage(const uint8_t* data, int32_t length)
{
	if(m_message_task->m_stream)
	{
		MT::Lock lock(m_send_mutex);
		const uint32_t guard = 0x12345678;
		m_message_task->m_stream->write(length);
		m_message_task->m_stream->write(guard);
		m_message_task->m_stream->write(data, length);
	}
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


void EditorServerImpl::renderPhysics()
{
	ASSERT(false);
	/*glMatrixMode(GL_PROJECTION);
	float proj[16];
	h3dGetCameraProjMat(m_engine.getRenderer().getRawCameraNode(), proj);
	glLoadMatrixf(proj);
		
	glMatrixMode(GL_MODELVIEW);
	
	Matrix mtx;
	m_engine.getRenderer().getCameraMatrix(mtx);
	mtx.fastInverse();
	glLoadMatrixf(&mtx.m11);

	glViewport(0, 0, m_engine.getRenderer().getWidth(), m_engine.getRenderer().getHeight());

	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	IPlugin* physics = m_engine.getPluginManager().getPlugin("physics");
	if(physics)
	{
		physics->sendMessage("render");
	}
	/*m_physics_scene->getRawScene()->getNbActors(physx::PxActorTypeSelectionFlag::eRIGID_STATIC);
	const physx::PxRenderBuffer& rb = m_physics_scene->getRawScene()->getRenderBuffer();
	const physx::PxU32 numLines = rb.getNbLines();
	const physx::PxU32 numPoints = rb.getNbPoints();
	const physx::PxU32 numTri = rb.getNbTriangles();
	if(numLines)
	{
		glBegin(GL_LINES);
		const physx::PxDebugLine* PX_RESTRICT lines = rb.getLines();
		for(physx::PxU32 i=0; i<numLines; i++)
		{
			const physx::PxDebugLine& line = lines[i];
			glColor3f(1, 1, 1);				
			glVertex3fv((GLfloat*)&line.pos0);
			glVertex3fv((GLfloat*)&line.pos1);
		}
		glEnd();
	}*/
}


EditorServerImpl::EditorServerImpl()
	: m_universe_mutex(false)
	, m_send_mutex(false)
	, m_toggle_game_mode_requested(false)
{
	m_is_game_mode = false;
	m_selected_entity = Entity::INVALID;
	m_edit_view_render_device = NULL;
}


void EditorServerImpl::navigate(float forward, float right, int fast)
{
	float navigation_speed = (fast ? 0.4f : 0.1f);
	Vec3 pos = m_camera.getPosition();
	Quat rot = m_camera.getRotation();;
	pos += rot * Vec3(0, 0, -1) * forward * navigation_speed;
	pos += rot * Vec3(1, 0, 0) * right * navigation_speed;
	m_camera.setPosition(pos);
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


void EditorServerImpl::setProperty(void* data, int size)
{
	Blob stream;
	stream.create(data, size);
	uint32_t component_type;
	stream.read(component_type);
	Component cmp = Component::INVALID;
	if(m_selected_entity.isValid())
	{
		const Entity::ComponentList& cmps = m_selected_entity.getComponents();
		for(int i = 0; i < cmps.size(); ++i)
		{
			if(cmps[i].type == component_type)
			{
				cmp = cmps[i];
				break;
			}
		}
	}
	if(cmp.isValid())
	{
		uint32_t name_hash;
		stream.read(name_hash);
		const IPropertyDescriptor& cp = getPropertyDescriptor(cmp.type, name_hash);
		cp.set(cmp, stream);
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


void EditorServerImpl::logMessage(int32_t type, const char* system, const char* msg)
{
	m_stream.flush();
	m_stream.write(ServerMessageType::LOG_MESSAGE);
	m_stream.write(type);
	writeString(system);
	writeString(msg);
	sendMessage(m_stream.getBuffer(), m_stream.getBufferSize());
}


void EditorServerImpl::selectEntity(Entity e)
{
	m_selected_entity = e;
	m_gizmo.setEntity(e);
	m_stream.flush();
	m_stream.write(ServerMessageType::ENTITY_SELECTED);
	m_stream.write(e.index);
	if(e.isValid())
	{
		const Entity::ComponentList& cmps = e.getComponents();
		m_stream.write(cmps.size());
		for(int i = 0; i < cmps.size(); ++i)
		{
			m_stream.write(cmps[i].type);
		}
	}
	sendMessage(m_stream.getBuffer(), m_stream.getBufferSize());
}


void EditorServerImpl::onComponentEvent(Event& event)
{
	ASSERT(event.getType() == ComponentEvent::type);
	ComponentEvent& e = static_cast<ComponentEvent&>(event);
	for(int i = 0; i < m_editor_icons.size(); ++i)
	{
		if(m_editor_icons[i]->getEntity() == e.component.entity)
		{
			m_editor_icons[i]->destroy();
			LUX_DELETE(m_editor_icons[i]);
			m_editor_icons.eraseFast(i);
			break;
		}
	}			
	if(e.is_created)
	{
		const Lux::Entity::ComponentList& cmps = e.component.entity.getComponents();
		bool found = false;
		for(int i = 0; i < cmps.size(); ++i)
		{
			if(cmps[i].type == renderable_type)
			{
				found = true;
				break;
			}
		}
		if(!found)
		{
			EditorIcon* er = LUX_NEW(EditorIcon)();
			er->create(m_engine, *m_engine.getRenderScene(), e.component.entity, e.component);
			m_editor_icons.push(er);
		}
	}
	else
	{
		if(e.component.entity.existsInUniverse() &&  e.component.entity.getComponents().empty())
		{
			EditorIcon* er = LUX_NEW(EditorIcon)();
			er->create(m_engine, *m_engine.getRenderScene(), e.component.entity, Component::INVALID);
			m_editor_icons.push(er);
		}
	}
}


void EditorServerImpl::onEvent(Event& evt)
{
	if(evt.getType() == EntityMovedEvent::type)
	{
		sendEntityPosition(static_cast<EntityMovedEvent&>(evt).entity.index);
	}
	else if(evt.getType() == EntityDestroyedEvent::type)
	{
		Entity e = static_cast<EntityDestroyedEvent&>(evt).entity;
		for(int i = 0; i < m_editor_icons.size(); ++i)
		{
			if(m_editor_icons[i]->getEntity() == e)
			{
				m_editor_icons[i]->destroy();
				LUX_DELETE(m_editor_icons[i]);
				m_editor_icons.eraseFast(i);
				break;
			}
		}
	}
}


void EditorServerImpl::destroyUniverse()
{
	for (int i = 0; i < m_editor_icons.size(); ++i)
	{
		m_editor_icons[i]->destroy();
		LUX_DELETE(m_editor_icons[i]);
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


void EditorServerImpl::createUniverse(bool create_basic_entities)
{
	Universe* universe = m_engine.createUniverse();
	if (create_basic_entities)
	{
		m_camera = m_engine.getUniverse()->createEntity();
		m_camera.setPosition(0, 0, -5);
		m_camera.setRotation(Quat(Vec3(0, 1, 0), -Math::PI));
		Component cmp = m_engine.getRenderScene()->createComponent(camera_type, m_camera);
		RenderScene* scene = static_cast<RenderScene*>(cmp.system);
		scene->setCameraSlot(cmp, string("editor"));
	}
	m_gizmo.create(m_engine.getRenderer());
	m_gizmo.setUniverse(universe);
	m_gizmo.hide();

	universe->getEventManager().addListener(EntityMovedEvent::type).bind<EditorServerImpl, &EditorServerImpl::onEvent>(this);
	universe->getEventManager().addListener(ComponentEvent::type).bind<EditorServerImpl, &EditorServerImpl::onComponentEvent>(this);
	universe->getEventManager().addListener(EntityDestroyedEvent::type).bind<EditorServerImpl, &EditorServerImpl::onEvent>(this);

	m_selected_entity = Entity::INVALID;
}


void EditorServerImpl::onMessage(void* msgptr, int size)
{
	int* msg = static_cast<int*>(msgptr);
	float* fmsg = static_cast<float*>(msgptr); 
	switch(msg[0])
	{
		case ClientMessageType::POINTER_DOWN:
			onPointerDown(msg[1], msg[2], (MouseButton::Value)msg[3]);
			break;
		case ClientMessageType::POINTER_MOVE:
			onPointerMove(msg[1], msg[2], msg[3], msg[4]);
			break;
		case ClientMessageType::POINTER_UP:
			onPointerUp(msg[1], msg[2], (MouseButton::Value)msg[3]);
			break;
		case ClientMessageType::PROPERTY_SET:
			setProperty(msg+1, size - 4);
			break;
		case ClientMessageType::MOVE_CAMERA:
			navigate(fmsg[1], fmsg[2], msg[3]);
			break;
		case ClientMessageType::SAVE:
			save(reinterpret_cast<char*>(&msg[1]));
			break;
		case ClientMessageType::LOAD:
			load(reinterpret_cast<char*>(&msg[1]));
			break;
		case ClientMessageType::ADD_COMPONENT:
			addComponent(*reinterpret_cast<uint32_t*>(&msg[1]));
			break;
		case ClientMessageType::GET_PROPERTIES:
			sendComponent(*reinterpret_cast<uint32_t*>(&msg[1]));
			break;
		case ClientMessageType::REMOVE_COMPONENT:
			removeComponent(*reinterpret_cast<uint32_t*>(&msg[1]));
			break;
		case ClientMessageType::ADD_ENTITY:
			addEntity();
			break;
		case ClientMessageType::TOGGLE_GAME_MODE:
			m_toggle_game_mode_requested = true;
			break;
		case ClientMessageType::GET_POSITION:
			sendEntityPosition(getSelectedEntity().index);
			break;
		case ClientMessageType::SET_POSITION:
			setEntityPosition(msg[1], reinterpret_cast<float*>(&msg[2]));
			break;
		case ClientMessageType::REMOVE_ENTITY:
			removeEntity();
			break;
		case ClientMessageType::LOOK_AT_SELECTED:
			lookAtSelected();
			break;
		case ClientMessageType::NEW_UNIVERSE:
			newUniverse();
			break;
		default:
			ASSERT(false); // unknown message
			break;
	}
}



} // !namespace Lux
