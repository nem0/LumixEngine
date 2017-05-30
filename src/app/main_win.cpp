#include "engine/blob.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/debug/debug.h"
#include "engine/engine.h"
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
#include "engine/plugin_manager.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/system.h"
#include "engine/timer.h"
#include "engine/universe/universe.h"
#include "gui/gui_system.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include <cstdio>
#ifdef _MSC_VER
	#include <windows.h>
#endif


namespace Lumix
{


struct GUIInterface : GUISystem::Interface
{
	GUIInterface()
	{
	}

	Pipeline* getPipeline() override { return pipeline; }
	Vec2 getPos() const override { return Vec2(0, 0); }

	void enableCursor(bool enable) override
	{
		if (enable)
		{
			while (ShowCursor(true) < 0);
		}
		else
		{
			while (ShowCursor(false) >= 0);
		}
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


	LRESULT onMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		auto& input_system = m_engine->getInputSystem();
		switch (msg)
		{
			case WM_KILLFOCUS: m_engine->getInputSystem().enable(false); break;
			case WM_SETFOCUS: m_engine->getInputSystem().enable(true); break;
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
			Renderer* renderer =
				static_cast<Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));
			renderer->resize(w, h);
		}
	}


	void createWindow()
	{
		HINSTANCE hInst = GetModuleHandle(NULL);
		WNDCLASSEX wnd;
		wnd = {};
		wnd.cbSize = sizeof(wnd);
		wnd.style = CS_HREDRAW | CS_VREDRAW;
		wnd.lpfnWndProc = msgProc;
		wnd.hInstance = hInst;
		wnd.hIcon = LoadIcon(NULL, IDI_APPLICATION);
		wnd.hCursor = LoadCursor(NULL, IDC_ARROW);
		wnd.lpszClassName = "App";
		wnd.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
		RegisterClassExA(&wnd);

		RECT rect = { 0, 0, 600, 400 };
		AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW | WS_VISIBLE, FALSE);

		m_hwnd = CreateWindowA("App",
			"App",
			WS_OVERLAPPEDWINDOW | WS_VISIBLE,
			0,
			0,
			rect.right - rect.left,
			rect.bottom - rect.top,
			NULL,
			NULL,
			hInst,
			0);

		if(!m_window_mode) setFullscreenBorderless();

		RAWINPUTDEVICE Rid;
		Rid.usUsagePage = 0x01;
		Rid.usUsage = 0x02;
		Rid.dwFlags = 0;
		Rid.hwndTarget = 0;
		RegisterRawInputDevices(&Rid, 1, sizeof(Rid));
	}


	void setFullscreenBorderless()
	{
		HMONITOR hmon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = {sizeof(mi)};
		if (!GetMonitorInfo(hmon, &mi)) return;

		SetWindowLong(m_hwnd, GWL_STYLE, GetWindowLong(m_hwnd, GWL_STYLE) & ~(WS_CAPTION | WS_THICKFRAME));
		SetWindowLong(m_hwnd,
			GWL_EXSTYLE,
			GetWindowLong(m_hwnd, GWL_EXSTYLE) &
				~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
		SetWindowPos(m_hwnd,
			NULL,
			mi.rcMonitor.left,
			mi.rcMonitor.top,
			mi.rcMonitor.right - mi.rcMonitor.left,
			mi.rcMonitor.bottom - mi.rcMonitor.top,
			SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}


	void init()
	{
		copyString(m_pipeline_path, "pipelines/app.lua");
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
			else if(parser.currentEquals("-script"))
			{
				if (!parser.next()) break;

				parser.getCurrent(m_startup_script_path, lengthOf(m_startup_script_path));
			}
		}

		createWindow();

		g_log_info.getCallback().bind<outputToVS>();
		g_log_warning.getCallback().bind<outputToVS>();
		g_log_error.getCallback().bind<outputToVS>();

		g_log_info.getCallback().bind<outputToConsole>();
		g_log_warning.getCallback().bind<outputToConsole>();
		g_log_error.getCallback().bind<outputToConsole>();

		enableCrashReporting(false);

		m_file_system = FS::FileSystem::create(m_allocator);

		m_mem_file_device = LUMIX_NEW(m_allocator, FS::MemoryFileDevice)(m_allocator);
		char current_dir[MAX_PATH_LENGTH];
		GetCurrentDirectory(sizeof(current_dir), current_dir);
		m_disk_file_device = LUMIX_NEW(m_allocator, FS::DiskFileDevice)("disk", current_dir, m_allocator);
		m_pack_file_device = LUMIX_NEW(m_allocator, FS::PackFileDevice)(m_allocator);

		m_file_system->mount(m_mem_file_device);
		m_file_system->mount(m_disk_file_device);
		m_file_system->mount(m_pack_file_device);
		m_pack_file_device->mount("data.pak");
		m_file_system->setDefaultDevice("memory:disk:pack");
		m_file_system->setSaveGameDevice("memory:disk");

		m_engine = Engine::create(current_dir, "", m_file_system, m_allocator);
		Engine::PlatformData platform_data;
		platform_data.window_handle = m_hwnd;
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
		m_pipeline = Pipeline::create(*renderer, Path(m_pipeline_path), m_engine->getAllocator());
		m_pipeline->load();

		while (m_engine->getFileSystem().hasWork())
		{
			MT::sleep(100);
			m_engine->getFileSystem().updateAsyncTransactions();
		}

		m_universe = &m_engine->createUniverse(true);
		m_pipeline->setScene((RenderScene*)m_universe->getScene(crc32("renderer")));
		m_pipeline->setViewport(0, 0, 600, 400);
		renderer->resize(600, 400);

		registerLuaAPI();

		m_gui_interface = LUMIX_NEW(m_allocator, GUIInterface);
		auto* gui_system = static_cast<GUISystem*>(m_engine->getPluginManager().getPlugin("gui"));
		m_gui_interface->pipeline = m_pipeline;

		gui_system->setInterface(m_gui_interface);
		
		while (ShowCursor(false) >= 0);
		onResize();

		runStartupScript();
	}


	void runStartupScript()
	{
		FS::FileSystem& fs = m_engine->getFileSystem();
		FS::IFile* file = fs.open(fs.getDefaultDevice(), Path(m_startup_script_path), FS::Mode::OPEN_AND_READ);
		if (file)
		{
			m_engine->runScript((const char*)file->getBuffer(), (int)file->size(), m_startup_script_path);
		}
		fs.close(*file);
	}



	void registerLuaAPI()
	{
		lua_State* L = m_engine->getState();

		#define REGISTER_FUNCTION(F, name) \
			do { \
				auto* f = &LuaWrapper::wrapMethodClosure<App, decltype(&App::F), &App::F>; \
				LuaWrapper::createSystemClosure(L, "App", this, name, f); \
			} while(false) \

		REGISTER_FUNCTION(loadUniverse, "loadUniverse");
		REGISTER_FUNCTION(setUniverse, "setUniverse");
		REGISTER_FUNCTION(frame, "frame");
		REGISTER_FUNCTION(exit, "exit");
		REGISTER_FUNCTION(isFinished, "isFinished");

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
			input_system.injectMouseXMove(float(raw->data.mouse.lLastX), (float)p.x);
			input_system.injectMouseYMove(float(raw->data.mouse.lLastY), (float)p.y);
		}
	}


	void handleEvents()
	{
		MSG msg;
		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			onMessage(msg.hwnd, msg.message, msg.wParam, msg.lParam);
		}
	}


	static void outputToVS(const char* system, const char* message)
	{
		char tmp[2048];
		copyString(tmp, system);
		catString(tmp, " : ");
		catString(tmp, message);
		catString(tmp, "\r");

		OutputDebugString(tmp);
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
	HWND m_hwnd;

	static App* s_instance;
};


App* App::s_instance = nullptr;


}


INT WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, INT)
{
	Lumix::App app;
	app.init();
	app.run();
	app.shutdown();
	return app.getExitCode();
}

