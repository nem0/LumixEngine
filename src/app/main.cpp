#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/debug.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/geometry.h"
#include "engine/input_system.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/mt/thread.h"
#include "engine/os.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/profiler.h"
#include "engine/stream.h"
#include "engine/universe/universe.h"
#include "gui/gui_system.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include <cstdio>
#ifdef _WIN32
	#include <Windows.h>
	#undef near
	#undef far
#endif

namespace Lumix
{


struct GUIInterface : GUISystem::Interface
{
	GUIInterface() = default;

	Pipeline* getPipeline() override { return pipeline; }
	Vec2 getPos() const override { return Vec2(0, 0); }
	Vec2 getSize() const override { return size; }

	void enableCursor(bool enable) override
	{
		OS::showCursor(enable);
	}


	Vec2 size;
	Pipeline* pipeline;
};


class Runner : public OS::Interface
{
public:
	Runner()
		: m_allocator(m_main_allocator)
		, m_window_mode(false)
		, m_universe(nullptr)
		, m_exit_code(0)
		, m_pipeline(nullptr)
	{
		ASSERT(!s_instance);
		s_instance = this;
	}


	~Runner()
	{
		ASSERT(!m_universe);
		s_instance = nullptr;
	}


	void onResize()
	{
		if (m_window == OS::INVALID_WINDOW) return;
		const OS::Rect r = OS::getWindowClientRect(m_window);
		m_viewport.w = r.width;
		m_viewport.h = r.height;
	}


	void onInit() override
	{
		JobSystem::init(MT::getCPUsCount(), m_allocator);

		copyString(m_pipeline_path, "pipelines/main.pln");
		m_pipeline_define = "APP";
		copyString(m_startup_script_path, "startup.lua");
		char cmd_line[1024];
		OS::getCommandLine(Span(cmd_line));
		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (parser.currentEquals("-window"))
			{
				m_window_mode = true;
			}
			else if (parser.currentEquals("-pipeline"))
			{
				if (!parser.next()) break;

				parser.getCurrent(m_pipeline_path, lengthOf(m_pipeline_path));
			}
			else if (parser.currentEquals("-pipeline_define"))
			{
				if (!parser.next()) break;

				parser.getCurrent(m_pipeline_define.data, lengthOf(m_pipeline_define.data));
			}
			else if(parser.currentEquals("-script"))
			{
				if (!parser.next()) break;

				parser.getCurrent(m_startup_script_path, lengthOf(m_startup_script_path));
			}
		}

		enableCrashReporting(false);

		char current_dir[MAX_PATH_LENGTH];
		#ifdef _WIN32
			OS::getCurrentDirectory(Span(current_dir)); 
		#else
			current_dir[0] = '\0';
		#endif
		m_engine = Engine::create(current_dir, m_allocator);
		gpu::preinit(m_allocator);
		
		OS::InitWindowArgs create_win_args = {};
		create_win_args.fullscreen = !m_window_mode;
		create_win_args.handle_file_drops = false;
		create_win_args.name = "Lumix App";
		m_window = OS::createWindow(create_win_args);

		#ifdef LUMIXENGINE_PLUGINS
			const char* plugins[] = { LUMIXENGINE_PLUGINS };
			for (auto plugin : plugins)
			{
				m_engine->getPluginManager().load(plugin);
			}
		#endif

		Engine::PlatformData platform_data = {};
		platform_data.window_handle = m_window;
		m_engine->setPlatformData(platform_data);

		m_engine->getPluginManager().initPlugins();
			
		Renderer* renderer = static_cast<Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));
		PipelineResource* pres = m_engine->getResourceManager().load<PipelineResource>(Path(m_pipeline_path));
		m_pipeline = Pipeline::create(*renderer, pres, m_pipeline_define, m_engine->getAllocator());

		while (m_engine->getFileSystem().hasWork())
		{
			MT::sleep(100);
			m_engine->getFileSystem().processCallbacks();
		}

		m_universe = &m_engine->createUniverse(true);
		m_pipeline->setScene((RenderScene*)m_universe->getScene(crc32("renderer")));
		// TODO
		//m_pipeline->resize(600, 400);

		OS::showCursor(false);
		onResize();
	}


	void setUniverse(Universe* universe)
	{
		m_engine->destroyUniverse(*m_universe);
		m_universe = universe;
		m_universe->setName("runtime");
		m_pipeline->setScene((RenderScene*)m_universe->getScene(crc32("renderer")));
		LuaWrapper::createSystemVariable(m_engine->getState(), "App", "universe", m_universe);
	}


	void shutdown()
	{
		m_engine->destroyUniverse(*m_universe);
		Pipeline::destroy(m_pipeline);
		Engine::destroy(m_engine, m_allocator);
		m_engine = nullptr;
		m_pipeline = nullptr;
		m_universe = nullptr;
	}


	int getExitCode() const { return m_exit_code; }


	void onEvent(const OS::Event& event) override
	{
		InputSystem& input = m_engine->getInputSystem();
		input.injectEvent(event);
		switch (event.type) {
			case OS::Event::Type::QUIT:
			case OS::Event::Type::WINDOW_CLOSE: 
				OS::quit();
				break;
			case OS::Event::Type::WINDOW_MOVE:
			case OS::Event::Type::WINDOW_SIZE:
				onResize();
				break;
		}
	}


	static void outputToConsole(const char* system, const char* message) 
	{
		printf("%s: %s\n", system, message); 
	}


	void exit(int exit_code)
	{
		m_exit_code = exit_code;
		OS::quit();
	}


	void onIdle() override
	{
		float frame_time = m_frame_timer.tick();
		m_engine->update(*m_universe);
		m_viewport.fov = degreesToRadians(60.f);
		m_viewport.far = 10'000.f;
		m_viewport.is_ortho = false;
		m_viewport.near = 0.1f;
		m_viewport.pos = {0, 0, 0};
		m_viewport.rot = Quat::IDENTITY;
		m_pipeline->setViewport(m_viewport);
		m_pipeline->render(false);
		auto* renderer = m_engine->getPluginManager().getPlugin("renderer");
		static_cast<Renderer*>(renderer)->frame();
		m_engine->getFileSystem().processCallbacks();
		if (frame_time < 1 / 60.0f) {
			PROFILE_BLOCK("sleep");
			MT::sleep(u32(1000 / 60.0f - frame_time * 1000));
		}
	}


private:
	DefaultAllocator m_main_allocator;
	Debug::Allocator m_allocator;
	Engine* m_engine;
	char m_universe_path[MAX_PATH_LENGTH];
	Universe* m_universe;
	Pipeline* m_pipeline;
	Viewport m_viewport;
	OS::Timer m_frame_timer;
	bool m_window_mode;
	int m_exit_code;
	char m_startup_script_path[MAX_PATH_LENGTH];
	char m_pipeline_path[MAX_PATH_LENGTH];
	StaticString<64> m_pipeline_define;
	OS::WindowHandle m_window = OS::INVALID_WINDOW;

	static Runner* s_instance;
};


Runner* Runner::s_instance = nullptr;


}

#ifdef _WIN32
INT WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, INT)
#else
int main(int args, char* argv[])
#endif
{
	Lumix::Runner app;
	Lumix::OS::run(app);
	app.shutdown();
	return app.getExitCode();
}
