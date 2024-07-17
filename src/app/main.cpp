#if 1 // set to 0 to build minimal lunex example

#include "core/atomic.h"
#include "core/command_line_parser.h"
#include "core/debug.h"
#include "core/default_allocator.h"
#include "engine/file_system.h"
#include "core/geometry.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/os.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/thread.h"
#include "engine/engine.h"
#include "engine/input_system.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "gui/gui_system.h"
#include "lua_script/lua_script.h"
#include "lua_script/lua_script_system.h"
#include "renderer/pipeline.h"
#include "renderer/render_module.h"
#include "renderer/renderer.h"

#ifdef __linux__
	#define STB_IMAGE_IMPLEMENTATION
	#include "stb/stb_image.h"
#endif

using namespace Lumix;

static const ComponentType ENVIRONMENT_TYPE = reflection::getComponentType("environment");
static const ComponentType LUA_SCRIPT_TYPE = reflection::getComponentType("lua_script");

struct GUIInterface : GUISystem::Interface {
	Pipeline* getPipeline() override { return pipeline; }
	Vec2 getPos() const override { return Vec2(0); }
	Vec2 getSize() const override { return size; }
	void setCursor(os::CursorType type) override { os::setCursor(type); }
	void enableCursor(bool enable) override { os::showCursor(enable); }

	Vec2 size;
	Pipeline* pipeline;
};


struct Runner final
{
	Runner() 
		: m_allocator(m_main_allocator) 
	{
		if (!jobs::init(os::getCPUsCount(), m_allocator)) {
			logError("Failed to initialize job system.");
		}
	}

	~Runner() {
		jobs::shutdown();
		ASSERT(!m_world); 
	}

	void onResize() {
		if (!m_engine.get()) return;
		if (m_engine->getMainWindow() == os::INVALID_WINDOW) return;

		const os::Rect r = os::getWindowClientRect(m_engine->getMainWindow());
		m_viewport.w = r.width;
		m_viewport.h = r.height;
		m_gui_interface.size = Vec2((float)r.width, (float)r.height);
	}

	void initRenderPipeline() {
		m_viewport.fov = degreesToRadians(60.f);
		m_viewport.far = 10'000.f;
		m_viewport.is_ortho = false;
		m_viewport.near = 0.1f;
		m_viewport.pos = {0, 0, 0};
		m_viewport.rot = Quat::IDENTITY;

		m_renderer = static_cast<Renderer*>(m_engine->getSystemManager().getSystem("renderer"));
		LuaScript* pres = m_engine->getResourceManager().load<LuaScript>(Path("pipelines/main.lua"));
		m_pipeline = Pipeline::create(*m_renderer, pres, "APP");

		while (m_engine->getFileSystem().hasWork()) {
			os::sleep(100);
			m_engine->getFileSystem().processCallbacks();
		}

		m_pipeline->setWorld(m_world);
	}

	void initDemoScene() {
		const EntityRef env = m_world->createEntity({0, 0, 0}, Quat::IDENTITY);
		m_world->createComponent(ENVIRONMENT_TYPE, env);
		m_world->createComponent(LUA_SCRIPT_TYPE, env);
		
		RenderModule* render_module = (RenderModule*)m_world->getModule("renderer");
		Environment& environment = render_module->getEnvironment(env);
		environment.direct_intensity = 3;
		
		Quat rot;
		rot.fromEuler(Vec3(degreesToRadians(45.f), 0, 0));
		m_world->setRotation(env, rot);
		
		LuaScriptModule* lua_module = (LuaScriptModule*)m_world->getModule("lua_script");
		lua_module->addScript(env, 0);
		lua_module->setScriptPath(env, 0, Path("pipelines/atmo.lua"));
	}

	bool loadWorld(const char* path) {
		FileSystem& fs = m_engine->getFileSystem();
		OutputMemoryStream data(m_allocator);
		if (!fs.getContentSync(Path(path), data)) return false;

		InputMemoryStream blob(data);
		EntityMap entity_map(m_allocator);

		WorldVersion editor_version;
		if (!m_world->deserialize(blob, entity_map, editor_version)) {
			logError("Failed to deserialize ", path);
			return false;
		}
		return true;
	}

	void loadProject() {
		FileSystem& fs = m_engine->getFileSystem();
		OutputMemoryStream data(m_allocator);
		if (!fs.getContentSync(Path("lumix.prj"), data)) return;

		InputMemoryStream tmp(data);
		const DeserializeProjectResult res = m_engine->deserializeProject(tmp, m_startup_world);
		if (DeserializeProjectResult::SUCCESS != res) {
			logError("Failed to deserialize project file");
		}

		char cmd_line[4096];
		if (os::getCommandLine(cmd_line)) {
			CommandLineParser parser(cmd_line);
			while (parser.next()) {
				if (!parser.currentEquals("-world")) continue;
				if (!parser.next()) break;

				char src[MAX_PATH];
				parser.getCurrent(src, lengthOf(src));
				m_startup_world = src;
				break;
			}
		}
	}

	void onInit() {
		Engine::InitArgs init_data;

		if (os::fileExists("main.pak")) {
			init_data.file_system = FileSystem::createPacked("main.pak", m_allocator);
		}
		init_data.log_path = "lumix_app.log";

		m_engine = Engine::create(static_cast<Engine::InitArgs&&>(init_data), m_allocator);

		os::InitWindowArgs init_window_args;
		init_window_args.name = "Lumix App";
		m_window = os::createWindow(init_window_args);
		m_engine->setMainWindow(m_window);

		m_engine->init();
		
		if (!CommandLineParser::isOn("-window")) {
			os::setFullscreen(m_engine->getMainWindow());
			captureMouse(true);
		}

		m_world = &m_engine->createWorld(true);
		initRenderPipeline();
		
		auto* gui = static_cast<GUISystem*>(m_engine->getSystemManager().getSystem("gui"));
		m_gui_interface.pipeline = m_pipeline.get();
		gui->setInterface(&m_gui_interface);

		loadProject();

		if (!loadWorld(m_startup_world.c_str())) {
			initDemoScene();
		}
		os::showCursor(false);
		while (m_engine->getFileSystem().hasWork()) {
			os::sleep(10);
			m_engine->getFileSystem().processCallbacks();
		}
		m_engine->getFileSystem().processCallbacks();

		os::showCursor(false);
		onResize();
		m_engine->startGame(*m_world);

		os::showWindow(m_window);
	}

	void shutdown() {
		m_engine->destroyWorld(*m_world);
		auto* gui = static_cast<GUISystem*>(m_engine->getSystemManager().getSystem("gui"));
		gui->setInterface(nullptr);
		m_pipeline.reset();
		m_engine.reset();
		m_world = nullptr;
	}

	void captureMouse(bool capture) {
		m_mouse_captured = capture;
		if (m_focused) {
			os::Rect r = os::getWindowScreenRect(m_engine->getMainWindow());
			os::clipCursor(m_engine->getMainWindow(), r);
		}
		else {
			os::clipCursor(os::INVALID_WINDOW, {});
		}
	}

	void onEvent(const os::Event& event) {
		if (m_engine.get()) {
			const bool is_mouse_up = event.type == os::Event::Type::MOUSE_BUTTON && !event.mouse_button.down;
			const bool is_key_up = event.type == os::Event::Type::KEY && !event.key.down;
			if (m_focused || is_mouse_up || is_key_up) {
				InputSystem& input = m_engine->getInputSystem();
				os::Point p = os::toScreen(m_window, 0, 0);
				input.injectEvent(event, p.x, p.y);
			}
		}
		switch (event.type) {
			case os::Event::Type::FOCUS:
				m_focused = event.focus.gained;
				captureMouse(m_focused);
				break;
			case os::Event::Type::QUIT:
			case os::Event::Type::WINDOW_CLOSE: 
				m_finished = true;
				break;
			case os::Event::Type::WINDOW_MOVE:
			case os::Event::Type::WINDOW_SIZE:
				onResize();
				captureMouse(m_focused);
				break;
			default: break;
		}
	}

	void onIdle() {
		if (m_mouse_captured) {
			os::Rect r = os::getWindowScreenRect(m_engine->getMainWindow());
			os::clipCursor(m_engine->getMainWindow(), r);
		}

		m_engine->update(*m_world);

		EntityPtr camera = m_pipeline->getModule()->getActiveCamera();
		if (camera.isValid()) {
			int w = m_viewport.w;
			int h = m_viewport.h;
			m_viewport = m_pipeline->getModule()->getCameraViewport((EntityRef)camera);
			m_viewport.w = w;
			m_viewport.h = h;
		}

		m_pipeline->setViewport(m_viewport);
		m_pipeline->render(false);
		m_renderer->frame();
	}

	DefaultAllocator m_main_allocator;
	debug::Allocator m_allocator;
	UniquePtr<Engine> m_engine;
	Renderer* m_renderer = nullptr;
	World* m_world = nullptr;
	UniquePtr<Pipeline> m_pipeline;
	Path m_startup_world;
	os::WindowHandle m_window = os::INVALID_WINDOW;

	Viewport m_viewport;
	bool m_finished = false;
	bool m_focused = true;
	bool m_mouse_captured = false;
	GUIInterface m_gui_interface;
};

int main(int args, char* argv[])
{
	profiler::setThreadName("Main thread");
	struct Data {
		Data() : semaphore(0, 1) {}
		Runner app;
		Semaphore semaphore;
	} data;

	jobs::runEx(&data, [](void* ptr) {
		Data* data = (Data*)ptr;

		data->app.onInit();
		while(!data->app.m_finished) {
			os::Event e;
			while(os::getEvent(e)) {
				data->app.onEvent(e);
			}
			data->app.onIdle();
		}

		data->app.shutdown();

		data->semaphore.signal();
	}, nullptr, 0);
	
	PROFILE_BLOCK("sleeping");
	data.semaphore.wait();

	return 0;
}

#else

#include "core/default_allocator.h"
#include "core/os.h"
#include "renderer/gpu/gpu.h"

using namespace Lumix;

int main(int args, char* argv[]) {
	os::init();
	// create window
	os::WindowHandle win = os::createWindow({ .width = 640, .height = 480 });
	
	// init GPU
	DefaultAllocator allocator;
	gpu::preinit(allocator, false);
	gpu::init(win, gpu::InitFlags::NONE);
	gpu::ProgramHandle shader = gpu::allocProgramHandle();

	// create shader
	const gpu::ShaderType types[] = {gpu::ShaderType::VERTEX, gpu::ShaderType::FRAGMENT};
	const char* srcs[] = {
		"void main() { gl_Position = vec4(gl_VertexID & 1, (gl_VertexID >> 1) & 1, 0, 1); }",
		"layout(location = 0) out vec4 color; void main() { color = vec4(1, 0, 1, 1); }",
	};
	gpu::VertexDecl decl(gpu::PrimitiveType::TRIANGLES);
	gpu::createProgram(shader, gpu::StateFlags::NONE, decl, srcs, types, 2, nullptr, 0, "shader");

	// main loop
	bool finished = false;
	while (!finished) {
		// process OS events
		os::Event e;
		while (os::getEvent(e)) {
			switch (e.type) {
				case os::Event::Type::WINDOW_CLOSE:
				case os::Event::Type::QUIT: finished = true; break;
				default: break;
			}
		}

		// draw
		os::Rect rect = os::getWindowClientRect(win);
		gpu::viewport(0, 0, rect.width, rect.height);
		gpu::setFramebuffer(nullptr, 0, gpu::INVALID_TEXTURE, gpu::FramebufferFlags::NONE);
		const float clear_col[] = {0.1f, 0.1f, 0.1f, 1};
		gpu::clear(gpu::ClearFlags::COLOR | gpu::ClearFlags::DEPTH, clear_col, 0);
		gpu::useProgram(shader);
		gpu::drawArrays(0, 3);

		u32 frame = gpu::swapBuffers();
		gpu::waitFrame(frame);
	}

	// shutdown
	gpu::shutdown();

	os::destroyWindow(win);
}

#endif