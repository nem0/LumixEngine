#include "engine/blob.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/debug/debug.h"
#include "engine/engine.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/file_system.h"
#include "engine/fs/memory_file_device.h"
#include "engine/fs/pack_file_device.h"
#include "engine/input_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/mt/thread.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/profiler.h"
#include "engine/system.h"
#include "engine/timer.h"
#include "engine/universe/universe.h"
#include "gui/gui_system.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include <cstdio>
#include <SDL.h>
#include <SDL_syswm.h>
#ifdef _WIN32
	#include <windows.h>
#endif


namespace Lumix
{


struct GUIInterface : GUISystem::Interface
{
	GUIInterface() = default;

	Pipeline* getPipeline() override { return pipeline; }
	Vec2 getPos() const override { return Vec2(0, 0); }

	void enableCursor(bool enable) override
	{
		SDL_ShowCursor(enable);
		SDL_SetRelativeMouseMode(!enable ? SDL_TRUE : SDL_FALSE);
	}


	Pipeline* pipeline;
};


class App
{
public:
	App()
		: m_allocator(m_main_allocator)
		, m_window_mode(false)
		, m_universe(nullptr)
		, m_exit_code(0)
		, m_pipeline(nullptr)
		, m_finished(false)
	{
		m_frame_timer = Timer::create(m_allocator);
		ASSERT(!s_instance);
		s_instance = this;
	}


	~App()
	{
		Timer::destroy(m_frame_timer);
		ASSERT(!m_universe);
		s_instance = nullptr;
	}


	void onResize() const
	{
		int w, h;
		SDL_GetWindowSize(m_window, &w, &h);
		m_pipeline->resize(w, h);
		Renderer* renderer = (Renderer*)m_engine->getPluginManager().getPlugin("renderer");
		renderer->resize(w, h);
	}


	void init()
	{
		copyString(m_pipeline_path, "pipelines/main.pln");
		m_pipeline_define = "APP";
		copyString(m_startup_script_path, "startup.lua");
		char cmd_line[1024];
		getCommandLine(cmd_line, lengthOf(cmd_line));
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

		u32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
		if (!m_window_mode) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

		g_log_info.getCallback().bind<outputToConsole>();
		g_log_warning.getCallback().bind<outputToConsole>();
		g_log_error.getCallback().bind<outputToConsole>();

		enableCrashReporting(false);

		m_file_system = FS::FileSystem::create(m_allocator);

		m_mem_file_device = LUMIX_NEW(m_allocator, FS::MemoryFileDevice)(m_allocator);
		char current_dir[MAX_PATH_LENGTH];
		#ifdef _WIN32
			GetCurrentDirectory(sizeof(current_dir), current_dir); 
		#else
			current_dir[0] = '\0';
		#endif
		m_disk_file_device = LUMIX_NEW(m_allocator, FS::DiskFileDevice)("disk", current_dir, m_allocator);
		m_pack_file_device = LUMIX_NEW(m_allocator, FS::PackFileDevice)(m_allocator);

		m_file_system->mount(m_mem_file_device);
		m_file_system->mount(m_disk_file_device);
		m_file_system->mount(m_pack_file_device);
		m_pack_file_device->mount("data.pak");
		m_file_system->setDefaultDevice("memory:disk:pack");
		m_file_system->setSaveGameDevice("memory:disk");

		m_engine = Engine::create(current_dir, "", m_file_system, m_allocator);
		m_window = SDL_CreateWindow("Lumix App", 0, 0, 600, 400, flags);
		if (!m_window_mode) SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
		SDL_SysWMinfo window_info;
		SDL_VERSION(&window_info.version);
		SDL_GetWindowWMInfo(m_window, &window_info);
		Engine::PlatformData platform_data = {};
		#ifdef _WIN32
			platform_data.window_handle = window_info.info.win.window;
		#elif defined(__linux__)
			platform_data.window_handle = (void*)(uintptr_t)window_info.info.x11.window;
			platform_data.display = window_info.info.x11.display;
		#else
			#error PLATFORM_NOT_SUPPORTED
		#endif
		m_engine->setPlatformData(platform_data);

		m_engine->getPluginManager().load("renderer");
		m_engine->getPluginManager().load("animation");
		m_engine->getPluginManager().load("audio");
		m_engine->getPluginManager().load("navigation");
		m_engine->getPluginManager().load("lua_script");
		m_engine->getPluginManager().load("physics");
		m_engine->getPluginManager().load("gui");
		#ifdef LUMIXENGINE_PLUGINS
			const char* plugins[] = { LUMIXENGINE_PLUGINS };
			for (auto plugin : plugins)
			{
				m_engine->getPluginManager().load(plugin);
			}
		#endif
		m_engine->getInputSystem().enable(true);
		Renderer* renderer = static_cast<Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));
		m_pipeline = Pipeline::create(*renderer, Path(m_pipeline_path), m_pipeline_define, "main", m_engine->getAllocator());
		m_pipeline->load();
		renderer->setMainPipeline(m_pipeline);

		while (m_engine->getFileSystem().hasWork())
		{
			MT::sleep(100);
			m_engine->getFileSystem().updateAsyncTransactions();
		}

		m_universe = &m_engine->createUniverse(true);
		m_pipeline->setScene((RenderScene*)m_universe->getScene(crc32("renderer")));
		m_pipeline->resize(600, 400);
		renderer->resize(600, 400);

		registerLuaAPI();

		m_gui_interface = LUMIX_NEW(m_allocator, GUIInterface);
		auto* gui_system = static_cast<GUISystem*>(m_engine->getPluginManager().getPlugin("gui"));
		m_gui_interface->pipeline = m_pipeline;

		gui_system->setInterface(m_gui_interface);
		
		SDL_ShowCursor(false);
		SDL_SetRelativeMouseMode(SDL_TRUE);
		onResize();

		if (!runStartupScript())
		{
			loadUniverse("universes/main.unv");
			while (m_engine->getFileSystem().hasWork()) m_engine->getFileSystem().updateAsyncTransactions();
			m_engine->startGame(*m_universe);
		}
	}


	bool runStartupScript() const
	{
		FS::FileSystem& fs = m_engine->getFileSystem();
		FS::IFile* file = fs.open(fs.getDefaultDevice(), Path(m_startup_script_path), FS::Mode::OPEN_AND_READ);
		if (file)
		{
			m_engine->runScript((const char*)file->getBuffer(), (int)file->size(), m_startup_script_path);
			fs.close(*file);
			return true;
		}
		return false;
	}


	void registerLuaAPI()
	{
		lua_State* L = m_engine->getState();

		#define REGISTER_FUNCTION(F) \
			do { \
				auto* f = &LuaWrapper::wrapMethodClosure<App, decltype(&App::F), &App::F>; \
				LuaWrapper::createSystemClosure(L, "App", this, #F, f); \
			} while(false) \

		REGISTER_FUNCTION(loadUniverse);
		REGISTER_FUNCTION(setUniverse);
		REGISTER_FUNCTION(frame);
		REGISTER_FUNCTION(exit);
		REGISTER_FUNCTION(isFinished);

		#undef REGISTER_FUNCTION

		LuaWrapper::createSystemVariable(L, "App", "universe", m_universe);
	}


	void universeFileLoaded(FS::IFile& file, bool success)
	{
		if (!success)
		{
			g_log_error.log("App") << "Failed to open universe.";
			return;
		}

		ASSERT(file.getBuffer());
		InputBlob blob(file.getBuffer(), (int)file.size());
		#pragma pack(1)
			struct Header
			{
				u32 magic;
				int version;
				u32 hash;
				u32 engine_hash;
			};
		#pragma pack()
		Header header;
		blob.read(header);
		if (crc32((const u8*)blob.getData() + sizeof(header), blob.getSize() - sizeof(header)) != header.hash)
		{
			g_log_error.log("App") << "Universe corrupted";
			return;
		}
		m_engine->destroyUniverse(*m_universe);
		m_universe = &m_engine->createUniverse(true);
		char basename[MAX_PATH_LENGTH];
		PathUtils::getBasename(basename, lengthOf(basename), m_universe_path);
		m_universe->setName(basename);
		m_pipeline->setScene((RenderScene*)m_universe->getScene(crc32("renderer")));
		LuaWrapper::createSystemVariable(m_engine->getState(), "App", "universe", m_universe);
		bool deserialize_succeeded = m_engine->deserialize(*m_universe, blob);
		if (!deserialize_succeeded)
		{
			g_log_error.log("App") << "Failed to deserialize universe";
		}
	}


	void setUniverse(Universe* universe)
	{
		m_engine->destroyUniverse(*m_universe);
		m_universe = universe;
		m_universe->setName("runtime");
		m_pipeline->setScene((RenderScene*)m_universe->getScene(crc32("renderer")));
		LuaWrapper::createSystemVariable(m_engine->getState(), "App", "universe", m_universe);
	}


	void loadUniverse(const char* path)
	{
		copyString(m_universe_path, path);
		auto& fs = m_engine->getFileSystem();
		FS::ReadCallback file_read_cb;
		file_read_cb.bind<App, &App::universeFileLoaded>(this);
		fs.openAsync(fs.getDefaultDevice(), Path(m_universe_path), FS::Mode::OPEN_AND_READ, file_read_cb);
	}


	void shutdown()
	{
		auto* gui_system = static_cast<GUISystem*>(m_engine->getPluginManager().getPlugin("gui"));
		gui_system->setInterface(nullptr);
		LUMIX_DELETE(m_allocator, m_gui_interface);

		m_engine->destroyUniverse(*m_universe);
		FS::FileSystem::destroy(m_file_system);
		LUMIX_DELETE(m_allocator, m_disk_file_device);
		LUMIX_DELETE(m_allocator, m_mem_file_device);
		LUMIX_DELETE(m_allocator, m_pack_file_device);
		Pipeline::destroy(m_pipeline);
		Engine::destroy(m_engine, m_allocator);
		m_engine = nullptr;
		m_pipeline = nullptr;
		m_universe = nullptr;
	}


	int getExitCode() const { return m_exit_code; }


	void handleEvents()
	{
		SDL_Event event;
		InputSystem& input = m_engine->getInputSystem();
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_MOUSEBUTTONDOWN:
				{
					Vec2 rel_mp = {(float)event.button.x, (float)event.button.y};
					InputSystem::Event input_event;
					input_event.type = InputSystem::Event::BUTTON;
					input_event.device = input.getMouseDevice();
					input_event.data.button.key_id = event.button.button;
					input_event.data.button.state = InputSystem::ButtonEvent::DOWN;
					input_event.data.button.x_abs = rel_mp.x;
					input_event.data.button.y_abs = rel_mp.y;
					input.injectEvent(input_event);
				}
				break;
			case SDL_MOUSEBUTTONUP:
				{
					Vec2 rel_mp = { (float)event.button.x, (float)event.button.y };
					InputSystem::Event input_event;
					input_event.type = InputSystem::Event::BUTTON;
					input_event.device = input.getMouseDevice();
					input_event.data.button.key_id = event.button.button;
					input_event.data.button.state = InputSystem::ButtonEvent::UP;
					input_event.data.button.x_abs = rel_mp.x;
					input_event.data.button.y_abs = rel_mp.y;
					input.injectEvent(input_event);
				}
				break;
			case SDL_MOUSEMOTION:
				{
					Vec2 rel_mp = { (float)event.motion.x, (float)event.motion.y };
					InputSystem::Event input_event;
					input_event.type = InputSystem::Event::AXIS;
					input_event.device = input.getMouseDevice();
					input_event.data.axis.x_abs = rel_mp.x;
					input_event.data.axis.y_abs = rel_mp.y;
					input_event.data.axis.x = (float)event.motion.xrel;
					input_event.data.axis.y = (float)event.motion.yrel;
					input.injectEvent(input_event);
				}
				break;
			case SDL_KEYDOWN:
				{
					InputSystem::Event input_event;
					input_event.type = InputSystem::Event::BUTTON;
					input_event.device = input.getKeyboardDevice();
					input_event.data.button.state = InputSystem::ButtonEvent::DOWN;
					input_event.data.button.key_id = event.key.keysym.sym;
					input_event.data.button.scancode = event.key.keysym.scancode;
					input.injectEvent(input_event);
				}
				break;
			case SDL_TEXTINPUT:
				{
					InputSystem::Event input_event;
					input_event.type = InputSystem::Event::TEXT_INPUT;
					input_event.device = input.getKeyboardDevice();
					ASSERT(sizeof(input_event.data.text.text) >= sizeof(event.text.text));
					copyMemory(input_event.data.text.text, event.text.text, sizeof(event.text.text));
					input.injectEvent(input_event);
				}
				break;
			case SDL_KEYUP:
				{
					InputSystem::Event input_event;
					input_event.type = InputSystem::Event::BUTTON;
					input_event.device = input.getKeyboardDevice();
					input_event.data.button.state = InputSystem::ButtonEvent::UP;
					input_event.data.button.key_id = event.key.keysym.sym;
					input_event.data.button.scancode = event.key.keysym.scancode;
					input.injectEvent(input_event);
				}
				break;
			case SDL_WINDOWEVENT:
				switch (event.window.event)
				{
				case SDL_WINDOWEVENT_CLOSE:
					m_finished = true;
					break;
				case SDL_WINDOWEVENT_RESIZED:
				case SDL_WINDOWEVENT_SIZE_CHANGED:
				case SDL_WINDOWEVENT_MOVED:
					onResize();
					break;
				}
				break;
			case SDL_QUIT: m_finished = true; break;
			case SDL_WINDOWEVENT_FOCUS_GAINED: m_engine->getInputSystem().enable(true); break;
			case SDL_WINDOWEVENT_FOCUS_LOST: m_engine->getInputSystem().enable(false); break;
			}
		}
	}



	static void outputToVS(const char* system, const char* message)
	{
		#ifdef _MSC_VER
			char tmp[2048];
			copyString(tmp, system);
			catString(tmp, " : ");
			catString(tmp, message);
			catString(tmp, "\r");

			OutputDebugString(tmp);
		#endif
	}


	static void outputToConsole(const char* system, const char* message) 
	{
		printf("%s: %s\n", system, message); 
	}


	void exit(int exit_code)
	{
		m_finished = true;
		m_exit_code = exit_code;
	}


	bool isFinished() const { return m_finished; }


	void frame()
	{
		float frame_time = m_frame_timer->tick();
		m_engine->update(*m_universe);
		m_pipeline->render();
		auto* renderer = m_engine->getPluginManager().getPlugin("renderer");
		static_cast<Renderer*>(renderer)->frame(false);
		m_engine->getFileSystem().updateAsyncTransactions();
		if (frame_time < 1 / 60.0f)
		{
			PROFILE_BLOCK("sleep");
			MT::sleep(u32(1000 / 60.0f - frame_time * 1000));
		}
		handleEvents();
	}


	void run()
	{
		while (!m_finished)
		{
			frame();
		}
	}


private:
	DefaultAllocator m_main_allocator;
	Debug::Allocator m_allocator;
	Engine* m_engine;
	char m_universe_path[MAX_PATH_LENGTH];
	Universe* m_universe;
	Pipeline* m_pipeline;
	FS::FileSystem* m_file_system;
	FS::MemoryFileDevice* m_mem_file_device;
	FS::DiskFileDevice* m_disk_file_device;
	FS::PackFileDevice* m_pack_file_device;
	Timer* m_frame_timer;
	GUIInterface* m_gui_interface;
	bool m_finished;
	bool m_window_mode;
	int m_exit_code;
	char m_startup_script_path[MAX_PATH_LENGTH];
	char m_pipeline_path[MAX_PATH_LENGTH];
	StaticString<64> m_pipeline_define;
	SDL_Window* m_window;

	static App* s_instance;
};


App* App::s_instance = nullptr;


}

#ifdef _WIN32
INT WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, INT)
#else
int main(int args, char* argv[])
#endif
{
	Lumix::App app;
	app.init();
	app.run();
	app.shutdown();
	return app.getExitCode();
}
