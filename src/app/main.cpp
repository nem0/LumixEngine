#include "engine/allocators.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/debug.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/geometry.h"
#include "engine/input_system.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/thread.h"
#include "engine/universe.h"
#include "gui/gui_system.h"
#include "lua_script/lua_script_system.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"

using namespace Lumix;

static const ComponentType ENVIRONMENT_TYPE = Reflection::getComponentType("environment");
static const ComponentType LUA_SCRIPT_TYPE = Reflection::getComponentType("lua_script");

struct GUIInterface : GUISystem::Interface {
	Pipeline* getPipeline() override { return pipeline; }
	Vec2 getPos() const override { return Vec2(0); }
	Vec2 getSize() const override { return size; }
	void setCursor(OS::CursorType type) override { OS::setCursor(type); }
	void enableCursor(bool enable) override { OS::showCursor(enable); }

	Vec2 size;
	Pipeline* pipeline;
};

#if 1 // set to 0 to build minimal lunex example

struct Runner final
{
	Runner() 
		: m_allocator(m_main_allocator) 
	{
		if (!JobSystem::init(OS::getCPUsCount(), m_allocator)) {
			logError("Failed to initialize job system.");
		}
	}

	~Runner() {
		JobSystem::shutdown();
		ASSERT(!m_universe); 
	}

	void onResize() {
		if (!m_engine.get()) return;
		if (m_engine->getWindowHandle() == OS::INVALID_WINDOW) return;

		const OS::Rect r = OS::getWindowClientRect(m_engine->getWindowHandle());
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

		m_renderer = static_cast<Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));
		PipelineResource* pres = m_engine->getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
		m_pipeline = Pipeline::create(*m_renderer, pres, "APP", m_engine->getAllocator());

		while (m_engine->getFileSystem().hasWork()) {
			OS::sleep(100);
			m_engine->getFileSystem().processCallbacks();
		}

		m_pipeline->setUniverse(m_universe);
	}

	void initDemoScene() {
		const EntityRef env = m_universe->createEntity({0, 0, 0}, Quat::IDENTITY);
		m_universe->createComponent(ENVIRONMENT_TYPE, env);
		m_universe->createComponent(LUA_SCRIPT_TYPE, env);
		
		RenderScene* render_scene = (RenderScene*)m_universe->getScene(crc32("renderer"));
		Environment& environment = render_scene->getEnvironment(env);
		environment.diffuse_intensity = 3;
		
		Quat rot;
		rot.fromEuler(Vec3(degreesToRadians(45.f), 0, 0));
		m_universe->setRotation(env, rot);
		
		LuaScriptScene* lua_scene = (LuaScriptScene*)m_universe->getScene(crc32("lua_script"));
		lua_scene->addScript(env, 0);
		lua_scene->setScriptPath(env, 0, Path("pipelines/atmo.lua"));
	}

	bool loadUniverse(const char* path) {
		FileSystem& fs = m_engine->getFileSystem();
		OutputMemoryStream data(m_allocator);
		if (!fs.getContentSync(Path(path), Ref(data))) return false;

		InputMemoryStream tmp(data);
		EntityMap entity_map(m_allocator);
		struct Header {
			u32 magic;
			i32 version;
			u32 hash;
			u32 engine_hash;
		} header;

		tmp.read(Ref(header));

		m_universe->setName("main");
		if (!m_engine->deserialize(*m_universe, tmp, Ref(entity_map))) {
			logError("Failed to deserialize universes/main/entities.unv");
			return false;
		}
		return true;
	}

	void onInit() {
		Engine::InitArgs init_data;

		if (OS::fileExists("main.pak")) {
			init_data.file_system = FileSystem::createPacked("main.pak", m_allocator);
		}

		m_engine = Engine::create(static_cast<Engine::InitArgs&&>(init_data), m_allocator);

		m_universe = &m_engine->createUniverse(true);
		initRenderPipeline();
		
		auto* gui = static_cast<GUISystem*>(m_engine->getPluginManager().getPlugin("gui"));
		m_gui_interface.pipeline = m_pipeline.get();
		gui->setInterface(&m_gui_interface);

		if (!loadUniverse("universes/main/entities.unv")) {
			initDemoScene();
		}
		while (m_engine->getFileSystem().hasWork()) {
			OS::sleep(10);
			m_engine->getFileSystem().processCallbacks();
		}

		OS::showCursor(false);
		onResize();
		m_engine->startGame(*m_universe);
	}

	void shutdown() {
		m_engine->destroyUniverse(*m_universe);
		auto* gui = static_cast<GUISystem*>(m_engine->getPluginManager().getPlugin("gui"));
		gui->setInterface(nullptr);
		m_pipeline.reset();
		m_engine.reset();
		m_universe = nullptr;
	}

	void onEvent(const OS::Event& event) {
		if (m_engine.get()) {
			InputSystem& input = m_engine->getInputSystem();
			input.injectEvent(event, 0, 0);
		}
		switch (event.type) {
			case OS::Event::Type::QUIT:
			case OS::Event::Type::WINDOW_CLOSE: 
				m_finished = true;
				break;
			case OS::Event::Type::WINDOW_MOVE:
			case OS::Event::Type::WINDOW_SIZE:
				onResize();
				break;
		}
	}

	void onIdle() {
		m_engine->update(*m_universe);

		EntityPtr camera = m_pipeline->getScene()->getActiveCamera();
		if (camera.isValid()) {
			int w = m_viewport.w;
			int h = m_viewport.h;
			m_viewport = m_pipeline->getScene()->getCameraViewport((EntityRef)camera);
			m_viewport.w = w;
			m_viewport.h = h;
		}

		m_pipeline->setViewport(m_viewport);
		m_pipeline->render(false);
		m_renderer->frame();
	}

	DefaultAllocator m_main_allocator;
	Debug::Allocator m_allocator;
	UniquePtr<Engine> m_engine;
	Renderer* m_renderer = nullptr;
	Universe* m_universe = nullptr;
	UniquePtr<Pipeline> m_pipeline;
	Viewport m_viewport;
	bool m_finished = false;
	GUIInterface m_gui_interface;
};

int main(int args, char* argv[])
{
	Profiler::setThreadName("Main thread");
	struct Data {
		Data() : semaphore(0, 1) {}
		Runner app;
		Semaphore semaphore;
	} data;

	JobSystem::runEx(&data, [](void* ptr) {
		Data* data = (Data*)ptr;

		data->app.onInit();
		while(!data->app.m_finished) {
			OS::Event e;
			while(OS::getEvent(Ref(e))) {
				data->app.onEvent(e);
			}
			data->app.onIdle();
		}

		data->app.shutdown();

		data->semaphore.signal();
	}, nullptr, JobSystem::INVALID_HANDLE, 0);
	
	PROFILE_BLOCK("sleeping");
	data.semaphore.wait();

	return 0;
}

#else

int main(int args, char* argv[]) {
	OS::WindowHandle win = OS::createWindow({});

	DefaultAllocator allocator;
	gpu::preinit(allocator, false);
	gpu::init(win, gpu::InitFlags::NONE);
	gpu::ProgramHandle shader = gpu::allocProgramHandle();

	const gpu::ShaderType types[] = {gpu::ShaderType::VERTEX, gpu::ShaderType::FRAGMENT};
	const char* srcs[] = {
		"void main() { gl_Position = vec4(gl_VertexID & 1, (gl_VertexID >> 1) & 1, 0, 1); }",
		"layout(location = 0) out vec4 color; void main() { color = vec4(1, 0, 1, 1); }",
	};
	gpu::createProgram(shader, {}, srcs, types, 2, nullptr, 0, "shader");

	bool finished = false;
	while (!finished) {
		OS::Event e;
		while (OS::getEvent(Ref(e))) {
			switch (e.type) {
				case OS::Event::Type::WINDOW_CLOSE:
				case OS::Event::Type::QUIT: finished = true; break;
			}
		}

		gpu::setFramebuffer(nullptr, 0, gpu::INVALID_TEXTURE, gpu::FramebufferFlags::NONE);
		const float clear_col[] = {0, 0, 0, 1};
		gpu::clear(gpu::ClearFlags::COLOR | gpu::ClearFlags::DEPTH, clear_col, 0);
		gpu::useProgram(shader);
		gpu::setState(gpu::StateFlags::NONE);
		gpu::drawArrays(0, 3, gpu::PrimitiveType::TRIANGLES);

		u32 frame = gpu::swapBuffers();
		gpu::waitFrame(frame);
	}

	gpu::shutdown();

	OS::destroyWindow(win);
}

#endif