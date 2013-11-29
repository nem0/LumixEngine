#include "editor_server.h"
#include "graphics/renderer.h"
#include "universe/universe.h"
#include "core/matrix.h"
#include "core/memory_stream.h"
#include "core/vector.h"
#include "core/map.h"
#include "core/crc32.h"
#include "property_descriptor.h"
#include "gizmo.h"
#include "editor_icon.h"
#include "universe/component_event.h"
#include "core/raw_file_stream.h"
#include "core/json_serializer.h"
#include "graphics/renderer.h"
#include "universe/universe.h"
#include "universe/entity_moved_event.h"
#include "universe/entity_destroyed_event.h"
#include "script\script_system.h"
#include <Windows.h>
#include <shellapi.h>
#include "core/mutex.h"
#include "core/ifilesystem.h"
#include "platform/tcp_filesystem.h"
#include "platform/input_system.h"
#include <Windows.h>
#include <gl/GL.h>
#include "Horde3DUtils.h"
#include "animation/animation_system.h"
#include "editor/iplugin.h"


namespace Lux
{


H3DNode g_node[16];


class EditorServer;


struct MouseButton
{
	enum Value
	{
		LEFT,
		MIDDLE,
		RIGHT
	};
};
	

class EditorServer
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
	public:
		typedef void (__stdcall *ResponceCallback)(const void*, int);

	public:
		EditorServer();

		bool create(HWND hwnd, const char* base_path);
		void destroy();
		void update();
		void onPointerDown(int x, int y, MouseButton::Value button);
		void onPointerMove(int x, int y, int relx, int rely);
		void onPointerUp(int x, int y, MouseButton::Value button);
		void selectEntity(Entity e);
		void navigate(float forward, float right, int fast);
		void writeString(const char* str);
		void setProperty(void* data, int size);
		void onResize(int w, int h) { m_renderer->onResize(w, h); }
		void setResponseCallback(ResponceCallback callback) { m_response_callback = callback; }
		void createUniverse(bool create_scene, const char* base_path);
		void renderScene();
		void save(const char path[]);
		void load(const char path[]);
		void addComponent(uint32_t type_crc);
		void sendComponent(uint32_t type_crc);
		void removeComponent(uint32_t type_crc);
		void sendEntityPosition(int uid);
		void setEntityPosition(int uid, float* pos);
		void addEntity();
		void removeEntity();
		void runGameMode();
		void editScript();
		void reloadScript(const char* path);
		void lookAtSelected();
		void newUniverse();
		Entity& getSelectedEntity() { return m_selected_entity; }
		Mutex& getMessageMutex() { return m_message_mutex; }
		bool isGameMode() const { return m_is_game_mode; }
		void save(IStream& stream);
		void load(IStream& stream);

private:
		const PropertyDescriptor& getPropertyDescriptor(uint32_t type, const char* name);
		H3DNode castRay(int x, int y, Vec3& hit_pos, char* name, int max_name_size, H3DNode gizmo_node);
		void registerProperties();
		void rotateCamera(int x, int y);
		void onEvent(Event& evt);
		void destroyUniverse();
		void gameModeLoop();
		bool loadPlugin(const char* plugin_name);

		static void onEvent(void* data, Event& evt);

	private:
		Gizmo m_gizmo;
		Entity m_selected_entity;
		Renderer* m_renderer;
		AnimationSystem* m_animation_system;
		ScriptSystem* m_script_system;
		Universe* m_universe;
		vector<IPlugin*> m_plugins;
		ResponceCallback m_response_callback;
		MemoryStream m_stream;
		map<uint32_t, vector<PropertyDescriptor> > m_component_properties;
		map<uint32_t, IPlugin*> m_creators;
		MouseMode::Value m_mouse_mode;
		vector<EditorIcon*>	m_editor_icons;
		HWND m_hwnd;
		bool m_is_game_mode;
		Quat m_camera_rot;
		Vec3 m_camera_pos;
		Mutex m_message_mutex;
		TCPFileSystem m_fs;
};


static const uint32_t renderable_type = crc32("renderable");
static const uint32_t physical_type = crc32("physical");
static const uint32_t camera_type = crc32("camera");
static const uint32_t point_light_type = crc32("point_light");
static const uint32_t physical_controller_type = crc32("physical_controller");
static const uint32_t script_type = crc32("script");
static const uint32_t animable_type = crc32("animable");


void EditorServer::registerProperties()
{
	m_component_properties[crc32("renderable")].push_back(PropertyDescriptor("path", (PropertyDescriptor::Getter)&Renderer::getMesh, (PropertyDescriptor::Setter)&Renderer::setMesh, PropertyDescriptor::FILE));
	//m_component_properties[crc32("physical")].push_back(PropertyDescriptor("source", (PropertyDescriptor::Getter)&PhysicsScene::getShapeSource, (PropertyDescriptor::Setter)&PhysicsScene::setShapeSource, PropertyDescriptor::FILE));
	//m_component_properties[crc32("physical")].push_back(PropertyDescriptor("dynamic", (PropertyDescriptor::BoolGetter)&PhysicsScene::getIsDynamic, (PropertyDescriptor::BoolSetter)&PhysicsScene::setIsDynamic));
	m_component_properties[crc32("point_light")].push_back(PropertyDescriptor("fov", (PropertyDescriptor::DecimalGetter)&Renderer::getLightFov, (PropertyDescriptor::DecimalSetter)&Renderer::setLightFov));
	m_component_properties[crc32("point_light")].push_back(PropertyDescriptor("radius", (PropertyDescriptor::DecimalGetter)&Renderer::getLightRadius, (PropertyDescriptor::DecimalSetter)&Renderer::setLightRadius));
	m_component_properties[crc32("script")].push_back(PropertyDescriptor("path", (PropertyDescriptor::Getter)&ScriptSystem::getScriptPath, (PropertyDescriptor::Setter)&ScriptSystem::setScriptPath, PropertyDescriptor::FILE));
}


void EditorServer::onPointerDown(int x, int y, MouseButton::Value button)
{
	if(button == MouseButton::RIGHT)
	{
		m_mouse_mode = EditorServer::MouseMode::NAVIGATE;
	}
	else if(button == MouseButton::LEFT)
	{
		Vec3 hit_pos;
		char node_name[20];
		H3DNode node = castRay(x, y, hit_pos, node_name, 20, m_gizmo.getNode());
		Component r = m_renderer->getRenderable(*m_universe, node);
		if(node == m_gizmo.getNode() && m_selected_entity.isValid())
		{
			m_mouse_mode = EditorServer::MouseMode::TRANSFORM;
			if(node_name[0] == 'x')
			{
				m_gizmo.startTransform(x, y, Gizmo::TransformMode::X, m_renderer);
			}
			else if(node_name[0] == 'y')
			{
				m_gizmo.startTransform(x, y, Gizmo::TransformMode::Y, m_renderer);
			}
			else if(node_name[0] == 'z')
			{
				m_gizmo.startTransform(x, y, Gizmo::TransformMode::Z, m_renderer);
			}
		}
		else
		{
			if(!r.isValid())
			{
				for(int i = 0, c = m_editor_icons.size(); i < c; ++i)
				{
					if(m_editor_icons[i]->getHandle() == node)
					{
						selectEntity(m_editor_icons[i]->getEntity());
						m_mouse_mode = EditorServer::MouseMode::TRANSFORM;
						m_gizmo.startTransform(x, y, Gizmo::TransformMode::CAMERA_XZ, m_renderer);
						break;
					}
				}
			}
			else
			{
				selectEntity(r.entity);
				m_mouse_mode = EditorServer::MouseMode::TRANSFORM;
				m_gizmo.startTransform(x, y, Gizmo::TransformMode::CAMERA_XZ, m_renderer);
			}
		}
	}
}


void EditorServer::onPointerMove(int x, int y, int relx, int rely)
{
	switch(m_mouse_mode)
	{
		case EditorServer::MouseMode::NAVIGATE:
			{
				rotateCamera(relx, rely);
			}
			break;
		case EditorServer::MouseMode::TRANSFORM:
			{
				
				Gizmo::TransformOperation tmode = GetKeyState(VK_MENU) & 0x8000 ? Gizmo::TransformOperation::ROTATE : Gizmo::TransformOperation::TRANSLATE;
				int flags = GetKeyState(VK_LCONTROL) & 0x8000 ? Gizmo::Flags::FIXED_STEP : 0;
				m_gizmo.transform(tmode, x, y, relx, rely, m_renderer, flags);
			}
			break;
	}
}


void EditorServer::onPointerUp(int x, int y, MouseButton::Value button)
{
	m_mouse_mode = EditorServer::MouseMode::NONE;
}


void EditorServer::save(const char path[])
{
	RawFileStream stream;
	stream.create(path, RawFileStream::Mode::WRITE);
	save(stream);
	stream.destroy();
}

void EditorServer::save(IStream& stream)
{
	JsonSerializer serializer(stream, JsonSerializer::WRITE);
	m_universe->serialize(serializer);
	m_script_system->serialize(serializer);
	m_renderer->serialize(serializer);
	for(int i = 0; i < m_plugins.size(); ++i)
	{
		m_plugins[i]->serialize(serializer);
	}
	m_animation_system->serialize(serializer);
	serializer.serialize("cam_pos_x", m_camera_pos.x);
	serializer.serialize("cam_pos_y", m_camera_pos.y);
	serializer.serialize("cam_pos_z", m_camera_pos.z);
	serializer.serialize("cam_rot_x", m_camera_rot.x);
	serializer.serialize("cam_rot_y", m_camera_rot.y);
	serializer.serialize("cam_rot_z", m_camera_rot.z);
	serializer.serialize("cam_rot_w", m_camera_rot.w);
}


void EditorServer::addEntity()
{
	Entity e = m_universe->createEntity();
	e.setPosition(m_camera_pos	+ m_camera_rot * Vec3(0, 0, -2));
	selectEntity(e);
	EditorIcon* er = new EditorIcon();
	er->create(m_selected_entity, Component::INVALID);
	m_editor_icons.push_back(er);
	/*** this is here because camera render node does not exists untitle pipeline resource is loaded, do this properly*/
	Matrix mtx;
	m_camera_rot.toMatrix(mtx);
	mtx.setTranslation(m_camera_pos);
	m_renderer->setCameraMatrix(mtx);	
	/***/
}


void EditorServer::gameModeLoop()
{
	bool finished = false;
	bool mousemove_handling = false;
	InputSystem input_system;
	input_system.create();
	input_system.addAction(crc32("up"), InputSystem::DOWN, 'W');
	input_system.addAction(crc32("down"), InputSystem::DOWN, 'S');
	input_system.addAction(crc32("left"), InputSystem::DOWN, 'A');
	input_system.addAction(crc32("right"), InputSystem::DOWN, 'D');
	input_system.addAction(crc32("select"), InputSystem::DOWN, 'Q');
	
	m_script_system->setInputSystem(&input_system);
	while(!finished)
	{
		{
			Lock lock(m_message_mutex);
			MSG msg;
			while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) > 0)
			{
				 TranslateMessage(&msg);
				 //DispatchMessage(&msg);
				 switch(msg.message)
				 {
					case WM_PAINT:
						DispatchMessage(&msg);
						break;
					case WM_KEYDOWN:
						if(msg.wParam == VK_ESCAPE)
						{
							finished = true;
						}
						break;
					case WM_MOUSEMOVE:
						{
							if(!mousemove_handling)
							{
								RECT rect;
								GetWindowRect(m_hwnd, &rect);
								int cx = (rect.left + rect.right) >> 1;
								int cy = (rect.bottom + rect.top) >> 1;
								POINT cp;
								cp.x = LOWORD(msg.lParam);
								cp.y = HIWORD(msg.lParam);
								ClientToScreen(m_hwnd, &cp);
								if(cp.x != cx || cp.y != cy)
								{
									SetCursorPos(cx, cy);
								}
								input_system.injectMouseXMove((float)(cp.x - cx));
								input_system.injectMouseYMove((float)(cp.y - cy));
							}
							mousemove_handling = !mousemove_handling;
						}
						break;
					case WM_KEYUP:
						break;
				 }
			}

			float dt = 0.05f;
			m_script_system->update(dt);
			for(int i = 0; i < m_plugins.size(); ++i)
			{
				m_plugins[i]->update(dt);
			}
			input_system.update(dt);
		}
		HDC hdc;
		PAINTSTRUCT ps;
		hdc = BeginPaint(m_hwnd, &ps);
		update();
		renderScene();
		wglSwapLayerBuffers(hdc, WGL_SWAP_MAIN_PLANE);
		EndPaint(m_hwnd, &ps);
	}
	input_system.destroy();
}


void EditorServer::runGameMode()
{
	selectEntity(Entity::INVALID);
	MemoryStream stream;
	save(stream);
	m_is_game_mode = true;
	ShowCursor(FALSE);
	m_gizmo.hide();
	for(int i = 0; i < m_editor_icons.size(); ++i)
	{
		m_editor_icons[i]->hide();
	}
	m_script_system->start();
	gameModeLoop();
	m_script_system->stop();
	m_gizmo.show();
	for(int i = 0; i < m_editor_icons.size(); ++i)
	{
		m_editor_icons[i]->show();
	}
	Matrix mtx;
	m_camera_rot.toMatrix(mtx);
	mtx.setTranslation(m_camera_pos);
	m_renderer->setCameraMatrix(mtx);
	ShowCursor(TRUE);
	m_is_game_mode = false;
	stream.rewindForRead();
	load(stream);
}


void EditorServer::sendComponent(uint32_t type_crc)
{
	if(m_selected_entity.isValid())
	{
		m_stream.flush();
		m_stream.write(2);
		const Entity::ComponentList& cmps = m_selected_entity.getComponents();
		string value;
		for(int i = 0; i < cmps.size(); ++i)
		{
			if(cmps[i].type == type_crc)
			{
				vector<PropertyDescriptor>& props = m_component_properties[cmps[i].type];
				m_stream.write(props.size());
				m_stream.write(type_crc);
				for(int j = 0; j < props.size(); ++j)
				{
					writeString(props[j].getName().c_str());
					props[j].get(cmps[i], value);
					writeString(value.c_str());
					m_stream.write(props[j].getType());
				}
				break;
			}
		}
		m_response_callback(m_stream.getBuffer(), m_stream.getBufferSize());
	}
}


void EditorServer::setEntityPosition(int uid, float* pos)
{
	Entity e(m_universe, uid);
	e.setPosition(pos[0], pos[1], pos[2]);
}


void EditorServer::sendEntityPosition(int uid)
{
	Entity entity(m_universe, uid);
	if(entity.isValid())
	{
		m_stream.flush();
		m_stream.write(3);
		m_stream.write(entity.index);
		m_stream.write(entity.getPosition().x);
		m_stream.write(entity.getPosition().y);
		m_stream.write(entity.getPosition().z);
		if(m_response_callback)
			m_response_callback(m_stream.getBuffer(), m_stream.getBufferSize());
	}
}


void EditorServer::addComponent(uint32_t type_crc)
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
			m_renderer->createRenderable(m_selected_entity);
		}
		else if(type_crc == physical_type)
		{
			//m_physics_scene->createActor(m_selected_entity);
		}
		else if(type_crc == point_light_type)
		{
			m_renderer->createPointLight(m_selected_entity);
		}
		else if(type_crc == physical_controller_type)
		{
			//m_physics_scene->createController(m_selected_entity);
		}
		else if(type_crc == script_type)
		{
			m_script_system->createScript(m_selected_entity);
		}
		else if(type_crc == animable_type)
		{
			m_animation_system->createAnimable(m_selected_entity);
		}
		else
		{
			assert(false);
		}
	}
	selectEntity(m_selected_entity);
}


void EditorServer::reloadScript(const char* path)
{
	m_script_system->reloadScript(path);
}


void EditorServer::lookAtSelected()
{
	if(m_selected_entity.isValid())
	{
		Vec3 dir = m_camera_rot * Vec3(0, 0, 1);
		m_camera_pos = m_selected_entity.getPosition() + dir * 10;
		Matrix mtx;
		m_camera_rot.toMatrix(mtx);
		mtx.setTranslation(m_camera_pos);
		m_renderer->setCameraMatrix(mtx);
	}
}


void EditorServer::editScript()
{
	string path;
	const Entity::ComponentList& cmps = m_selected_entity.getComponents();
	for(int i = 0; i < cmps.size(); ++i)
	{
		if(cmps[i].type == script_type)
		{
			m_script_system->getScriptPath(cmps[i], path);
			break;
		}
	}
	
	ShellExecute(NULL, "open", path.c_str(), NULL, NULL, SW_SHOW);
}


void EditorServer::removeEntity()
{
	m_universe->destroyEntity(m_selected_entity);
	selectEntity(Lux::Entity::INVALID);
}


void EditorServer::removeComponent(uint32_t type_crc)
{
	const Entity::ComponentList& cmps = m_selected_entity.getComponents();
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
		m_renderer->destroyRenderable(cmp);
	}
	else if(type_crc == physical_type)
	{
		//m_physics_scene->destroyActor(cmp);
	}
	else if(type_crc == point_light_type)
	{
		m_renderer->destroyPointLight(cmp);
	}
	else
	{
		assert(false);
	}
	selectEntity(m_selected_entity);
}


void loadMap(void* user_data, char* file_data, int length, bool success)
{
	assert(success);
	if(success)
	{
		MemoryStream stream;
		stream.create(file_data, length);
		static_cast<EditorServer*>(user_data)->load(stream);
	}
}


void EditorServer::load(const char path[])
{
	m_fs.openFile(path, &loadMap, this);
}


void EditorServer::newUniverse()
{
	destroyUniverse();
	createUniverse(false, "");
}


void EditorServer::load(IStream& stream)
{
	destroyUniverse();
	createUniverse(false, "");
	JsonSerializer serializer(stream, JsonSerializer::READ);
	m_universe->deserialize(serializer);
	m_script_system->deserialize(serializer);
	m_renderer->deserialize(serializer);
	for(int i = 0; i < m_plugins.size(); ++i)
	{
		m_plugins[i]->deserialize(serializer);
	}
	m_animation_system->deserialize(serializer);
	serializer.deserialize("cam_pos_x", m_camera_pos.x);
	serializer.deserialize("cam_pos_y", m_camera_pos.y);
	serializer.deserialize("cam_pos_z", m_camera_pos.z);
	serializer.deserialize("cam_rot_x", m_camera_rot.x);
	serializer.deserialize("cam_rot_y", m_camera_rot.y);
	serializer.deserialize("cam_rot_z", m_camera_rot.z);
	serializer.deserialize("cam_rot_w", m_camera_rot.w);
	Matrix mtx;
	m_camera_rot.toMatrix(mtx);
	mtx.setTranslation(m_camera_pos);
	m_renderer->setCameraMatrix(mtx);
}


void EditorServer::destroy()
{
	m_message_mutex.destroy();
	m_fs.destroy();
	// TODO
}


bool EditorServer::create(HWND hwnd, const char* base_path)
{
	m_message_mutex.create();
	m_fs.create();
	m_hwnd = hwnd;
	m_renderer = new Renderer();
	m_animation_system = new AnimationSystem();
	RECT rect;
	m_script_system = new ScriptSystem();
	m_script_system->setRenderer(m_renderer);
	GetWindowRect(hwnd, &rect);
	if(!m_renderer->create(&m_fs, rect.right - rect.left, rect.bottom - rect.top, base_path))
	{
		return false;
	}
	if(!m_animation_system->create())
	{
		return false;
	}
	glPopAttrib();
	
	loadPlugin("physics.dll");
	loadPlugin("navigation.dll");

	EditorIcon::createResources(base_path);

	createUniverse(true, base_path);
	m_gizmo.create(base_path);
	m_gizmo.hide();
	registerProperties();
	//m_navigation.load("models/level2/level2.pda");
	
	return true;
}


bool EditorServer::loadPlugin(const char* plugin_name)
{
	typedef IPlugin* (*PluginCreator)();
	HMODULE lib = LoadLibrary(TEXT(plugin_name));
	if(lib)
	{
		PluginCreator creator = (PluginCreator)GetProcAddress(lib, TEXT("createPlugin"));
		if(creator)
		{
			IPlugin* plugin = creator();
			if(!plugin->create(m_component_properties, m_creators))
			{
				delete plugin;
				assert(false);
				return false;
			}
			m_plugins.push_back(plugin);
			return true;
		}
	}
	return false;
}


void EditorServer::update()
{
	m_fs.processLoaded();
}


void EditorServer::renderScene()
{
	static float t = 0;
	static long last_tick = GetTickCount();
	long tick = GetTickCount();
	t += 30.0f * (tick - last_tick) / 1000.0f;
	m_animation_system->update(30.0f * (tick - last_tick) / 1000.0f);
	last_tick = tick;
/*	for(int i = 0; i< 16; ++i)
	{
		h3dSetModelAnimParams(g_node[i], 0, t, 1.0f );
	}*/
	if(!m_is_game_mode)
	{
		if(m_selected_entity.isValid())
		{
			m_gizmo.updateScale(m_renderer);
		}
		for(int i = 0, c = m_editor_icons.size(); i < c; ++i)
		{
			m_editor_icons[i]->update(m_renderer);
		}
	}
	m_renderer->renderScene();

	/*if(!m_is_game_mode)
	{
		renderPhysics();
	}*/

	m_renderer->endFrame();
		
	//m_navigation.draw();
}


/*void EditorServer::renderPhysics()
{
	glMatrixMode(GL_PROJECTION);
	float proj[16];
	h3dGetCameraProjMat(m_renderer->getRawCameraNode(), proj);
	glLoadMatrixf(proj);
		
	glMatrixMode(GL_MODELVIEW);
	
	Matrix mtx;
	m_renderer->getCameraMatrix(mtx);
	mtx.fastInverse();
	glLoadMatrixf(&mtx.m11);

	glViewport(0, 0, m_renderer->getWidth(), m_renderer->getHeight());

	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	int nbac = m_physics_scene->getRawScene()->getNbActors(physx::PxActorTypeSelectionFlag::eRIGID_STATIC);
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
	}
}*/


EditorServer::EditorServer()
{
	m_is_game_mode = false;
	m_response_callback = 0;
	m_selected_entity = Entity::INVALID;
}


void EditorServer::navigate(float forward, float right, int fast)
{
	float navigation_speed = (fast ? 0.4f : 0.1f);
	m_camera_pos += m_camera_rot * Vec3(0, 0, 1) * -forward * navigation_speed;
	m_camera_pos += m_camera_rot * Vec3(1, 0, 0) * right * navigation_speed;
	Matrix mtx;
	m_camera_rot.toMatrix(mtx);
	mtx.setTranslation(m_camera_pos);
	m_renderer->setCameraMatrix(mtx);
}


const PropertyDescriptor& EditorServer::getPropertyDescriptor(uint32_t type, const char* name)
{
	vector<PropertyDescriptor>& props = m_component_properties[type];
	for(int i = 0; i < props.size(); ++i)
	{
		if(props[i].getName() == name)
		{
			return props[i];
		}
	}
	assert(false);
	return m_component_properties[type][0];
}


void EditorServer::setProperty(void* data, int size)
{
	MemoryStream stream;
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
		char tmp[255];
		int len;
		stream.read(len);
		stream.read(tmp, len);
		tmp[len] = 0;
		const PropertyDescriptor& cp = getPropertyDescriptor(cmp.type, tmp);
		stream.read(len);
		stream.read(tmp, len);
		tmp[len] = 0;
		cp.set(cmp, string(tmp));
	}
}


void EditorServer::rotateCamera(int x, int y)
{
	Quat yaw_rot(Vec3(0, 1, 0), -x / 200.0f);
	m_camera_rot = m_camera_rot * yaw_rot;
	m_camera_rot.normalize();

	Matrix mtx;
	m_camera_rot.toMatrix(mtx);
	Vec3 axis = mtx.getXVector();
	axis.y = 0;
	axis.normalize();
	Quat pitch_rot(axis, -y / 200.0f);
	m_camera_rot = m_camera_rot * pitch_rot;
	m_camera_rot.normalize();

	Matrix camera_mtx;
	m_camera_rot.toMatrix(camera_mtx);
	camera_mtx.setTranslation(m_camera_pos);
	m_renderer->setCameraMatrix(camera_mtx);
}


H3DNode EditorServer::castRay(int x, int y, Vec3& hit_pos, char* name, int max_name_size, H3DNode gizmo_node)
{
	Vec3 origin, dir;
	m_renderer->getRay(x, y, origin, dir);
	
	if(h3dCastRay(H3DRootNode, origin.x, origin.y, origin.z, dir.x, dir.y, dir.z, 0) == 0)
	{
		return 0;
	}
	else
	{
		H3DNode node = 0;
		int i = 0;
		while(h3dGetCastRayResult(i, &node, 0, &hit_pos.x))
		{
			H3DNode original_node = node;
			while(node && h3dGetNodeType(node) != H3DNodeTypes::Model)
			{
				node = h3dGetNodeParent(node);
			}
			if(node == gizmo_node)
			{
				const char* node_name = h3dGetNodeParamStr(original_node, H3DNodeParams::NameStr);
				strcpy_s(name, max_name_size, node_name);
				return node;
			}
			++i;
		}
		if(h3dGetCastRayResult(0, &node, 0, &hit_pos.x))
		{
			const char* node_name = h3dGetNodeParamStr(node, H3DNodeParams::NameStr);
			strcpy_s(name, max_name_size, node_name);
			while(node && h3dGetNodeType(node) != H3DNodeTypes::Model)
			{
				node = h3dGetNodeParent(node);
			}
			return node;
		}
	}
	return 0;
}

void EditorServer::writeString(const char* str)
{
	int len = strlen(str);
	m_stream.write(len);
	m_stream.write(str, len);
}



void EditorServer::selectEntity(Entity e)
{
	m_selected_entity = e;
	m_gizmo.setEntity(e);
	if(e.isValid())
	{
		m_stream.flush();
		m_stream.write(1);
		m_stream.write(e.index);
		const Entity::ComponentList& cmps = e.getComponents();
		m_stream.write(cmps.size());
		for(int i = 0; i < cmps.size(); ++i)
		{
			m_stream.write(cmps[i].type);
		}
		m_response_callback(m_stream.getBuffer(), m_stream.getBufferSize());
	}
	else
	{
		m_stream.flush();
		m_stream.write(1);
		m_stream.write(-1);
		m_response_callback(m_stream.getBuffer(), m_stream.getBufferSize());
	}
}


extern "C" LUX_ENGINE_API void __stdcall luxServerResize(HWND hwnd, void* ptr)
{
	EditorServer* server = static_cast<EditorServer*>(ptr);
	RECT rect;
	GetWindowRect(hwnd, &rect);
	server->onResize(rect.right - rect.left, rect.bottom - rect.top);
}


extern "C" LUX_ENGINE_API void __stdcall luxServerDraw(HWND hwnd, void* ptr)
{
	EditorServer* server = static_cast<EditorServer*>(ptr);
	PAINTSTRUCT ps;
	HDC hdc;
	hdc = BeginPaint(hwnd, &ps);
	server->renderScene();
	wglSwapLayerBuffers(hdc, WGL_SWAP_MAIN_PLANE);
	EndPaint(hwnd, &ps);
}


extern "C" LUX_ENGINE_API void __stdcall luxServerUpdate(void* ptr)
{
	EditorServer* server = static_cast<EditorServer*>(ptr);
	server->update();
}


void EditorServer::onEvent(void* data, Event& evt)
{
	static_cast<EditorServer*>(data)->onEvent(evt);
}


void EditorServer::onEvent(Event& evt)
{
	if(evt.getType() == ComponentEvent::type)
	{
		ComponentEvent& e = static_cast<ComponentEvent&>(evt);
		for(int i = 0; i < m_editor_icons.size(); ++i)
		{
			if(m_editor_icons[i]->getEntity() == e.component.entity)
			{
				m_editor_icons[i]->destroy();
				m_editor_icons.eraseFast(i);
				break;
			}
		}			
		if(e.is_created)
		{
			Lux::Entity::ComponentList cmps = e.component.entity.getComponents();
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
				EditorIcon* er = new EditorIcon();
				er->create(e.component.entity, e.component);
				m_editor_icons.push_back(er);
			}
		}
		else
		{
			if(e.component.entity.existsInUniverse() &&  e.component.entity.getComponents().empty())
			{
				EditorIcon* er = new EditorIcon();
				er->create(e.component.entity, Component::INVALID);
				m_editor_icons.push_back(er);
			}
		}
	}
	else if(evt.getType() == EntityMovedEvent::type)
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
				m_editor_icons.eraseFast(i);
				break;
			}
		}
	}
}


void EditorServer::destroyUniverse()
{
	for(int i = 0; i < m_editor_icons.size(); ++i)
	{
		m_editor_icons[i]->destroy();
	}

	m_gizmo.hide();
	m_editor_icons.clear();
	m_renderer->setUniverse(0);
	m_animation_system->setUniverse(0);
	m_script_system->setUniverse(0);
	m_gizmo.setUniverse(0);
	for(int i = 0; i < m_plugins.size(); ++i)
	{
		m_plugins[i]->onDestroyUniverse(*m_universe);
	}
	m_universe->destroy();
	delete m_universe;
	m_universe = 0;
}

void EditorServer::createUniverse(bool create_scene, const char* base_path)
{
	m_universe = new Universe();
	for(int i = 0; i < m_plugins.size(); ++i)
	{
		m_plugins[i]->onCreateUniverse(*m_universe);
	}
	m_universe->create();
	m_universe->getEventManager()->registerListener(EntityMovedEvent::type, this, &EditorServer::onEvent);
	m_universe->getEventManager()->registerListener(ComponentEvent::type, this, &EditorServer::onEvent);
	m_universe->getEventManager()->registerListener(EntityDestroyedEvent::type, this, &EditorServer::onEvent);

	m_renderer->setUniverse(m_universe);
	m_animation_system->setUniverse(m_universe);
	m_script_system->setUniverse(m_universe);
	if(create_scene)
	{
		Quat q(Vec3(0, 1, 0), 3.14159265f);
	
		/*Entity e = m_universe->createEntity();
		e.setPosition(0, 0, -5);
		Component renderable = m_renderer->createRenderable(m_universe->createEntity());
		m_renderer->setMesh(renderable, "models/draha/draha.scene.xml");
		
		for(int i = 0; i < 16; ++i)
		{
			Entity e = m_universe->createEntity();
			e.setPosition(0, 0, 0);
			Component renderable = m_renderer->createRenderable(e);
			m_renderer->setMesh(renderable, "models/zebra/zebra_run.scene.xml");
			
			Component animable = m_animation_system->createAnimable(e);	
			m_animation_system->playAnimation(animable, "models/zebra/zebra_run.anim");
			
			g_node[i] = m_renderer->getMeshNode(renderable);
			h3dSetNodeTransform(g_node[i], 0 + (float)i, 0, 10, 0, 0, 0, 0.01f, 0.01f, 0.01f);
		}

		e = m_universe->createEntity();
		e.setPosition(0, 0, -5);
		e.setRotation(q.x, q.y, q.z, q.w);
		Component light2 = m_renderer->createPointLight(e);
		*/
		m_camera_pos.set(0, 0, 2);
		m_camera_rot = Quat(0, 0, 0, 1);
		Matrix mtx;
		m_camera_rot.toMatrix(mtx);
		mtx.setTranslation(m_camera_pos);
		m_renderer->setCameraMatrix(mtx);
	}
	m_gizmo.setUniverse(m_universe);
	m_selected_entity = Entity::INVALID;
}


extern "C" LUX_ENGINE_API void* __stdcall luxServerInit(HWND hwnd, const char* base_path)
{
	PAINTSTRUCT ps;
	HDC hdc;
	hdc = BeginPaint(hwnd, &ps);
	PIXELFORMATDESCRIPTOR pfd = { 
    sizeof(PIXELFORMATDESCRIPTOR),  //  size of this pfd  
    1,                     // version number  
    PFD_DRAW_TO_WINDOW |   // support window  
    PFD_SUPPORT_OPENGL |   // support OpenGL  
    PFD_DOUBLEBUFFER,      // double buffered  
    PFD_TYPE_RGBA,         // RGBA type  
    24,                    // 24-bit color depth  
    0, 0, 0, 0, 0, 0,      // color bits ignored  
    0,                     // no alpha buffer  
    0,                     // shift bit ignored  
    0,                     // no accumulation buffer  
    0, 0, 0, 0,            // accum bits ignored  
    32,                    // 32-bit z-buffer      
    0,                     // no stencil buffer  
    0,                     // no auxiliary buffer  
    PFD_MAIN_PLANE,        // main layer  
    0,                     // reserved  
    0, 0, 0                // layer masks ignored  
    }; 
	int pixelformat = ChoosePixelFormat(hdc, &pfd);
	SetPixelFormat(hdc, pixelformat, &pfd);
	HGLRC hglrc = wglCreateContext(hdc);
	wglMakeCurrent(hdc, hglrc);
	EndPaint(hwnd, &ps);
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	EditorServer* server = new EditorServer;
	if(!server->create(hwnd, base_path))
	{
		delete server;
		return NULL;
	}

	return server;
}

namespace MessageType
{
	enum 
	{
		POINTER_DOWN = 1,
		POINTER_MOVE,
		POINTER_UP,
		PROPERTY_SET,
		MOVE_CAMERA,		// 5
		SAVE,
		LOAD,
		ADD_COMPONENT = 8,	
		GET_PROPERTIES = 9,
		REMOVE_COMPONENT = 10,
		ADD_ENTITY,				// 11
		RUN_GAME_MODE,			// 12
		GET_POSITION,			// 13
		SET_POSITION,			// 14
		REMOVE_ENTITY,			// 15
		SET_EDIT_MODE,			// 16
		EDIT_SCRIPT,			// 17
		RELOAD_SCRIPT,			// 18
		NEW_UNIVERSE,			// 19
		LOOK_AT_SELECTED = 20,	// 20

	};
}


extern "C" LUX_ENGINE_API void __stdcall luxServerSetCallback(void* ptr, void* callback_ptr)
{
	EditorServer* server = static_cast<EditorServer*>(ptr);
	server->setResponseCallback(static_cast<EditorServer::ResponceCallback>(callback_ptr));
}

extern "C" LUX_ENGINE_API void __stdcall luxServerMessage(void* ptr, void* msgptr, int size)
{
	EditorServer* server = static_cast<EditorServer*>(ptr);
	bool locked = false;
	if(server->isGameMode())
	{
		server->getMessageMutex().lock();
		locked = true;
	}
	int* msg = static_cast<int*>(msgptr);
	float* fmsg = static_cast<float*>(msgptr); 
	switch(msg[0])
	{
		case MessageType::POINTER_DOWN:
			server->onPointerDown(msg[1], msg[2], (MouseButton::Value)msg[3]);
			break;
		case MessageType::POINTER_MOVE:
			server->onPointerMove(msg[1], msg[2], msg[3], msg[4]);
			break;
		case MessageType::POINTER_UP:
			server->onPointerUp(msg[1], msg[2], (MouseButton::Value)msg[3]);
			break;
		case MessageType::PROPERTY_SET:
			server->setProperty(msg+1, size - 4);
			break;
		case MessageType::MOVE_CAMERA:
			server->navigate(fmsg[1], fmsg[2], msg[3]);
			break;
		case MessageType::SAVE:
			server->save(reinterpret_cast<char*>(&msg[1]));
			break;
		case MessageType::LOAD:
			server->load(reinterpret_cast<char*>(&msg[1]));
			break;
		case MessageType::ADD_COMPONENT:
			server->addComponent(*reinterpret_cast<uint32_t*>(&msg[1]));
			break;
		case MessageType::GET_PROPERTIES:
			server->sendComponent(*reinterpret_cast<uint32_t*>(&msg[1]));
			break;
		case MessageType::REMOVE_COMPONENT:
			server->removeComponent(*reinterpret_cast<uint32_t*>(&msg[1]));
			break;
		case MessageType::ADD_ENTITY:
			server->addEntity();
			break;
		case MessageType::RUN_GAME_MODE:
			server->runGameMode();
			break;
		case MessageType::GET_POSITION:
			server->sendEntityPosition(server->getSelectedEntity().index);
			break;
		case MessageType::SET_POSITION:
			server->setEntityPosition(msg[1], reinterpret_cast<float*>(&msg[2]));
			break;
		case MessageType::REMOVE_ENTITY:
			server->removeEntity();
			break;
		case MessageType::EDIT_SCRIPT:
			server->editScript();
			break;
		case MessageType::RELOAD_SCRIPT:
			server->reloadScript(reinterpret_cast<char*>(&msg[1]));
			break;
		case MessageType::LOOK_AT_SELECTED:
			server->lookAtSelected();
			break;
		case MessageType::NEW_UNIVERSE:
			server->newUniverse();
			break;
		default:
			assert(false); // unknown message
			break;
	}
	if(locked)
	{
		server->getMessageMutex().unlock();
	}
}


} // !namespace Lux
