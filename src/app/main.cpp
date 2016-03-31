#include "core/FS/file_system.h"
#include "core/blob.h"
#include "core/command_line_parser.h"
#include "core/crc32.h"
#include "core/fs/disk_file_device.h"
#include "core/fs/file_system.h"
#include "core/fs/memory_file_device.h"
#include "core/fs/pack_file_device.h"
#include "core/log.h"
#include "core/lua_wrapper.h"
#include "core/mt/thread.h"
#include "core/path_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/system.h"
#include "debug/debug.h"
#include "editor/gizmo.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include "universe/universe.h"
#include <cstdio>
#include <windows.h>


class App
{
public:
	App()
	{
		m_universe = nullptr;
		m_exit_code = 0;
	}


	~App() { ASSERT(!m_universe); }


	HWND createWindow()
	{
		HINSTANCE hInst = GetModuleHandle(NULL);
		WNDCLASSEX wnd;
		wnd = {};
		wnd.cbSize = sizeof(wnd);
		wnd.style = CS_HREDRAW | CS_VREDRAW;
		wnd.lpfnWndProc = DefWindowProc;
		wnd.hInstance = hInst;
		wnd.hIcon = LoadIcon(NULL, IDI_APPLICATION);
		wnd.hCursor = LoadCursor(NULL, IDC_ARROW);
		wnd.lpszClassName = "App";
		wnd.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
		RegisterClassExA(&wnd);
		auto hwnd = CreateWindowA("App", "App", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 800, 600, NULL, NULL, hInst, 0);

		m_hwnd = hwnd;
		return hwnd;
	}


	void init()
	{
		Lumix::copyString(m_pipeline_path, "piplines/app.lua");
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

		auto hwnd = createWindow();

		Lumix::g_log_info.getCallback().bind<outputToVS>();
		Lumix::g_log_warning.getCallback().bind<outputToVS>();
		Lumix::g_log_error.getCallback().bind<outputToVS>();

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
		platform_data.window_handle = hwnd;
		m_engine->setPlatformData(platform_data);

		m_engine->getPluginManager().load("renderer");
		m_engine->getPluginManager().load("animation");
		m_engine->getPluginManager().load("audio");
		m_engine->getPluginManager().load("lua_script");
		m_engine->getPluginManager().load("physics");
		Lumix::Renderer* renderer = static_cast<Lumix::Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));
		m_pipeline = Lumix::Pipeline::create(*renderer, Lumix::Path(m_pipeline_path), m_engine->getAllocator());
		m_pipeline->load();

		m_universe = &m_engine->createUniverse();
		m_pipeline->setScene((Lumix::RenderScene*)m_universe->getScene(Lumix::crc32("renderer")));
		m_pipeline->setViewport(0, 0, 600, 400);
		renderer->resize(600, 400);

		registerLuaAPI();
	}


	void startupScriptLoaded(Lumix::FS::IFile& file, bool success)
	{
		if (!success)
		{
			Lumix::g_log_error.log("App") << "Could not open " << m_startup_script_path;
			return;
		}

		m_engine->runScript((const char*)file.getBuffer(), file.size(), m_startup_script_path);
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
	}


	int getExitCode() const { return m_exit_code; }


	void handleRawInput(LPARAM lParam) {}


	void handleEvents()
	{
		MSG msg;
		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			switch (msg.message)
			{
				case WM_QUIT: m_finished = true; break;
				case WM_INPUT: handleRawInput(msg.lParam); break;
			}
		}
	}


	static void outputToVS(const char* system, const char* message)
	{
		char tmp[2048];
		Lumix::copyString(tmp, system);
		Lumix::catString(tmp, " : ");
		Lumix::catString(tmp, message);
		Lumix::catString(tmp, "\r");

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


	void frame()
	{
		m_engine->update(*m_universe);
		m_pipeline->setViewport(0, 0, 600, 400);
		m_pipeline->render();
		auto* renderer = m_engine->getPluginManager().getPlugin("renderer");
		static_cast<Lumix::Renderer*>(renderer)->frame();
		m_engine->getFileSystem().updateAsyncTransactions();
		Lumix::MT::sleep(100);
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
	bool m_finished;
	int m_exit_code;
	HWND m_hwnd;
	char m_startup_script_path[Lumix::MAX_PATH_LENGTH];
	char m_pipeline_path[Lumix::MAX_PATH_LENGTH];
};


INT WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, INT)
{
	App app;
	app.init();
	app.run();
	app.shutdown();
	return app.getExitCode();
}
