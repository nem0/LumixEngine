#include "engine/blob.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/file_system.h"
#include "engine/fs/file_system.h"
#include "engine/fs/memory_file_device.h"
#include "engine/fs/pack_file_device.h"
#include "engine/input_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/mt/thread.h"
#include "engine/path_utils.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/system.h"
#include "engine/timer.h"
#include "engine/debug/debug.h"
#include "editor/gizmo.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include "engine/universe/universe.h"
#include <cstdio>
#include <X11/Xlib.h>

struct App
{
	App()
	{
		m_universe = nullptr;
		m_exit_code = 0;
		m_frame_timer = Lumix::Timer::create(m_allocator);
		ASSERT(!s_instance);
		s_instance = this;
		m_pipeline = nullptr;
	}


	~App()
	{
		Lumix::Timer::destroy(m_frame_timer);
		ASSERT(!m_universe);
		s_instance = nullptr;
	}

	/*
	LRESULT onMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		switch (msg)
		{
			case WM_CLOSE: PostQuitMessage(0); break;
			case WM_MOVE:
			case WM_SIZE: onResize(); break;
			case WM_QUIT: m_finished = true; break;
			case WM_INPUT: handleRawInput(lparam); break;
		}
		return DefWindowProc(hwnd, msg, wparam, lparam);
	}


	static LRESULT CALLBACK msgProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		if (!s_instance || !s_instance->m_pipeline) return DefWindowProc(hwnd, msg, wparam, lparam);

		return s_instance->onMessage(hwnd, msg, wparam, lparam);
	}


	void onResize()
	{
		RECT rect;
		RECT screen_rect;
		GetClientRect(m_hwnd, &rect);
		GetWindowRect(m_hwnd, &screen_rect);
		int w = rect.right - rect.left;
		int h = rect.bottom - rect.top;
		if (w > 0)
		{
			ClipCursor(&screen_rect);
			m_pipeline->setViewport(0, 0, w, h);
			Lumix::Renderer* renderer =
				static_cast<Lumix::Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));
			renderer->resize(w, h);
		}
	}
*/

	bool createWindow()
	{
		m_display = XOpenDisplay(nullptr);
		if (!m_display) return false;
		
		int s = DefaultScreen(m_display);
		m_window = XCreateSimpleWindow(m_display, RootWindow(m_display, s), 10, 10, 100, 100, 1
			, BlackPixel(m_display, s), WhitePixel(m_display, s));
		XSelectInput(m_display, m_window, ExposureMask | KeyPressMask);
		XMapWindow(m_display, m_window);
		return true;
	}

	void init()
	{
		Lumix::copyString(m_pipeline_path, "pipelines/app.lua");
		Lumix::copyString(m_startup_script_path, "startup.lua");
		char cmd_line[1024];
		Lumix::getCommandLine(cmd_line, Lumix::lengthOf(cmd_line));
		Lumix::CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (parser.currentEquals("-pipeline"))
			{
				if (!parser.next()) break;

				parser.getCurrent(m_pipeline_path, Lumix::lengthOf(m_pipeline_path));
			}
			else if(parser.currentEquals("-script"))
			{
				if (!parser.next()) break;

				parser.getCurrent(m_startup_script_path, Lumix::lengthOf(m_startup_script_path));
			}
		}

		createWindow();

		Lumix::g_log_info.getCallback().bind<outputToConsole>();
		Lumix::g_log_warning.getCallback().bind<outputToConsole>();
		Lumix::g_log_error.getCallback().bind<outputToConsole>();

		Lumix::enableCrashReporting(false);

		m_file_system = Lumix::FS::FileSystem::create(m_allocator);

		m_mem_file_device = LUMIX_NEW(m_allocator, Lumix::FS::MemoryFileDevice)(m_allocator);
		m_disk_file_device = LUMIX_NEW(m_allocator, Lumix::FS::DiskFileDevice)("disk", "", m_allocator);
		m_pack_file_device = LUMIX_NEW(m_allocator, Lumix::FS::PackFileDevice)(m_allocator);

		m_file_system->mount(m_mem_file_device);
		m_file_system->mount(m_disk_file_device);
		m_file_system->mount(m_pack_file_device);
		m_pack_file_device->mount("data.pak");
		m_file_system->setDefaultDevice("memory:disk:pack");
		m_file_system->setSaveGameDevice("memory:disk");

		m_engine = Lumix::Engine::create("", "", m_file_system, m_allocator);
		Lumix::Engine::PlatformData platform_data;
		platform_data.window_handle = (void*)(uintptr_t)m_window;
		platform_data.display = m_display;
		m_engine->setPlatformData(platform_data);

		m_engine->getPluginManager().load("renderer");
		m_engine->getPluginManager().load("animation");
		m_engine->getPluginManager().load("audio");
		m_engine->getPluginManager().load("lua_script");
		m_engine->getPluginManager().load("physics");
		m_engine->getInputSystem().enable(true);
		Lumix::Renderer* renderer = static_cast<Lumix::Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));
		m_pipeline = Lumix::Pipeline::create(*renderer, Lumix::Path(m_pipeline_path), m_engine->getAllocator());
		m_pipeline->load();

		while (m_engine->getFileSystem().hasWork())
		{
			Lumix::MT::sleep(100);
			m_engine->getFileSystem().updateAsyncTransactions();
		}

		m_universe = &m_engine->createUniverse();
		m_pipeline->setScene((Lumix::RenderScene*)m_universe->getScene(Lumix::crc32("renderer")));
		m_pipeline->setViewport(0, 0, 600, 400);
		renderer->resize(600, 400);

		registerLuaAPI();

		//while (ShowCursor(false) >= 0); // TODO
		// onResize(); // TODO
	}


	void startupScriptLoaded(Lumix::FS::IFile& file, bool success)
	{
		if (!success)
		{
			Lumix::g_log_error.log("App") << "Could not open " << m_startup_script_path;
			return;
		}

		m_engine->runScript((const char*)file.getBuffer(), (int)file.size(), m_startup_script_path);
	}


	void registerLuaAPI()
	{
		lua_State* L = m_engine->getState();

		#define REGISTER_FUNCTION(F, name) \
			do { \
				auto* f = &Lumix::LuaWrapper::wrapMethod<App, decltype(&App::F), &App::F>; \
				Lumix::LuaWrapper::createSystemFunction(L, "App", name, f); \
			} while(false) \

		REGISTER_FUNCTION(loadUniverse, "loadUniverse");
		REGISTER_FUNCTION(frame, "frame");
		REGISTER_FUNCTION(exit, "exit");

		#undef REGISTER_FUNCTION

		Lumix::LuaWrapper::createSystemVariable(L, "App", "instance", this);
		Lumix::LuaWrapper::createSystemVariable(L, "App", "universe", m_universe);

		auto& fs = m_engine->getFileSystem();
		Lumix::FS::ReadCallback cb;
		cb.bind<App, &App::startupScriptLoaded>(this);
		fs.openAsync(fs.getDefaultDevice(), Lumix::Path(m_startup_script_path), Lumix::FS::Mode::OPEN_AND_READ, cb);
	}


	void universeFileLoaded(Lumix::FS::IFile& file, bool success)
	{
		ASSERT(success);
		if (!success) return;

		ASSERT(file.getBuffer());
		Lumix::InputBlob blob(file.getBuffer(), (int)file.size());
		#pragma pack(1)
			struct Header
			{
				Lumix::uint32 magic;
				int version;
				Lumix::uint32 hash;
				Lumix::uint32 engine_hash;
			};
		#pragma pack()
		Header header;
		blob.read(header);
		if (Lumix::crc32((const uint8_t*)blob.getData() + sizeof(header), blob.getSize() - sizeof(header)) !=
			header.hash)
		{
			Lumix::g_log_error.log("App") << "Universe corrupted";
			return;
		}
		bool deserialize_succeeded = m_engine->deserialize(*m_universe, blob);
		if (!deserialize_succeeded)
		{
			Lumix::g_log_error.log("App") << "Failed to deserialize universe";
		}
	}


	void loadUniverse(const char* path)
	{
		auto& fs = m_engine->getFileSystem();
		Lumix::FS::ReadCallback file_read_cb;
		file_read_cb.bind<App, &App::universeFileLoaded>(this);
		fs.openAsync(fs.getDefaultDevice(), Lumix::Path(path), Lumix::FS::Mode::OPEN_AND_READ, file_read_cb);
	}


	void shutdown()
	{
		m_engine->destroyUniverse(*m_universe);
		Lumix::FS::FileSystem::destroy(m_file_system);
		LUMIX_DELETE(m_allocator, m_disk_file_device);
		LUMIX_DELETE(m_allocator, m_mem_file_device);
		LUMIX_DELETE(m_allocator, m_pack_file_device);
		Lumix::Pipeline::destroy(m_pipeline);
		Lumix::Engine::destroy(m_engine, m_allocator);
		m_engine = nullptr;
		m_pipeline = nullptr;
		m_universe = nullptr;
		
		XCloseDisplay(m_display);
	}
	

	int getExitCode() const { return m_exit_code; }

	/*
	void handleRawInput(LPARAM lParam)
	{
		UINT dwSize;
		char data[sizeof(RAWINPUT) * 10];

		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
		if (dwSize > sizeof(data)) return;

		if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, data, &dwSize, sizeof(RAWINPUTHEADER)) !=
			dwSize) return;

		RAWINPUT* raw = (RAWINPUT*)data;
		if (raw->header.dwType == RIM_TYPEMOUSE &&
			raw->data.mouse.usFlags == MOUSE_MOVE_RELATIVE)
		{
			POINT p;
			GetCursorPos(&p);
			ScreenToClient(m_hwnd, &p);
			auto& input_system = m_engine->getInputSystem();
			input_system.injectMouseXMove(float(raw->data.mouse.lLastX));
			input_system.injectMouseYMove(float(raw->data.mouse.lLastY));
		}
	}
*/

	void handleEvents()
	{
		XEvent e;
		while (XPending(m_display) > 0)
		{
			XNextEvent(m_display, &e);
			if (e.type == KeyPress) break;
		}
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

	
	void frame()
	{
		float frame_time = m_frame_timer->tick();
		m_engine->update(*m_universe);
		m_pipeline->render();
		auto* renderer = m_engine->getPluginManager().getPlugin("renderer");
		static_cast<Lumix::Renderer*>(renderer)->frame();
		m_engine->getFileSystem().updateAsyncTransactions();
		if (frame_time < 1 / 60.0f)
		{
			PROFILE_BLOCK("sleep");
			Lumix::MT::sleep(Lumix::uint32(1000 / 60.0f - frame_time * 1000));
		}
		handleEvents();
	}

	void run()
	{
		m_finished = false;
		while (!m_finished)
		{
			frame();
		}
	}
	

private:
	Lumix::DefaultAllocator m_allocator;
	Lumix::Engine* m_engine;
	Lumix::Universe* m_universe;
	Lumix::Pipeline* m_pipeline;
	Lumix::FS::FileSystem* m_file_system;
	Lumix::FS::MemoryFileDevice* m_mem_file_device;
	Lumix::FS::DiskFileDevice* m_disk_file_device;
	Lumix::FS::PackFileDevice* m_pack_file_device;
	Lumix::Timer* m_frame_timer;
	bool m_finished;
	int m_exit_code;
	char m_startup_script_path[Lumix::MAX_PATH_LENGTH];
	char m_pipeline_path[Lumix::MAX_PATH_LENGTH];
	Display* m_display;
	Window m_window;

	static App* s_instance;
};


App* App::s_instance = nullptr;


int main(int argc, char* argv[])
{
	Lumix::setCommandLine(argc, argv);
	App app;
	app.init();
	app.run();
	app.shutdown();
	return app.getExitCode();
}
