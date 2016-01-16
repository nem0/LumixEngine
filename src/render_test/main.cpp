#include "core/FS/file_system.h"
#include "core/FS/ifile.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "debug/debug.h"
#include "core/log.h"
#include "core/mt/thread.h"
#include "core/path_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/gizmo.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include <Windows.h>
#include <cstdio>


class App
{
public:
	App()
		: m_tests(m_allocator)
	{
		m_current_test = -1;
		m_is_test_universe_loaded = false;
		m_universe_context = nullptr;
	}


	~App() { ASSERT(!m_universe_context); }


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
		if (Lumix::crc32((const uint8_t*)blob.getData() + sizeof(header),
				blob.getSize() - sizeof(header)) != header.hash)
		{
			Lumix::g_log_error.log("render_test") << "Universe corrupted";
			return;
		}
		bool deserialize_succeeded = m_engine->deserialize(*m_universe_context, blob);
		m_is_test_universe_loaded = true;
		if (!deserialize_succeeded)
		{
			Lumix::g_log_error.log("render_test") << "Failed to deserialize universe";
		}
	}


	static LRESULT WINAPI msgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}


	HWND createWindow()
	{
		HINSTANCE hInst = GetModuleHandle(NULL);
		WNDCLASSEX wnd;
		memset(&wnd, 0, sizeof(wnd));
		wnd.cbSize = sizeof(wnd);
		wnd.style = CS_HREDRAW | CS_VREDRAW;
		wnd.lpfnWndProc = msgProc;
		wnd.hInstance = hInst;
		wnd.hIcon = LoadIcon(NULL, IDI_APPLICATION);
		wnd.hCursor = LoadCursor(NULL, IDC_ARROW);
		wnd.lpszClassName = "render_test";
		wnd.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
		RegisterClassExA(&wnd);
		auto hwnd = CreateWindowA("render_test",
			"render_test",
			WS_OVERLAPPEDWINDOW | WS_VISIBLE,
			0,
			0,
			800,
			600,
			NULL,
			NULL,
			hInst,
			0);

		m_hwnd = hwnd;
		return hwnd;
	}


	void init()
	{
		auto hwnd = createWindow();
		Lumix::g_log_info.getCallback().bind<outputToVS>();
		Lumix::g_log_warning.getCallback().bind<outputToVS>();
		Lumix::g_log_error.getCallback().bind<outputToVS>();

		Lumix::g_log_info.getCallback().bind<outputToConsole>();
		Lumix::g_log_warning.getCallback().bind<outputToConsole>();
		Lumix::g_log_error.getCallback().bind<outputToConsole>();

		Lumix::enableCrashReporting(false);

		m_engine = Lumix::Engine::create(NULL, m_allocator);

		m_engine->getPluginManager().load("renderer.dll");
		m_engine->getPluginManager().load("animation.dll");
		m_engine->getPluginManager().load("audio.dll");
		m_engine->getPluginManager().load("lua_script.dll");
		m_engine->getPluginManager().load("physics.dll");
		Lumix::Renderer* renderer =
			static_cast<Lumix::Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));
		m_pipeline = Lumix::Pipeline::create(
			*renderer, Lumix::Path("pipelines/render_test.lua"), m_engine->getAllocator());
		m_pipeline->load();

		m_universe_context = &m_engine->createUniverse();
		m_pipeline->setScene(
			(Lumix::RenderScene*)m_universe_context->getScene(Lumix::crc32("renderer")));
		m_pipeline->setViewport(0, 0, 600, 400);
		renderer->resize(600, 400);

		enumerateTests();
	}


	void shutdown()
	{
		m_engine->destroyUniverse(*m_universe_context);
		Lumix::Pipeline::destroy(m_pipeline);
		Lumix::Engine::destroy(m_engine, m_allocator);
		m_engine = nullptr;
		m_pipeline = nullptr;
		m_universe_context = nullptr;
	}


	void handleEvents()
	{
		MSG msg;
		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT)
			{
				m_finished = true;
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


	void enumerateTests()
	{
		WIN32_FIND_DATAA data;
		char buf[100];
		GetCurrentDirectory(100, buf);

		auto handle = FindFirstFile(".\\render_tests\\*.unv", &data);

		auto push_test = [&](const char* name) {
			char basename[Lumix::MAX_PATH_LENGTH];
			Lumix::PathUtils::getBasename(basename, Lumix::lengthOf(basename), name);
			auto& test = m_tests.emplace();
			Lumix::copyString(test.path, "render_tests/");
			Lumix::catString(test.path, basename);
			test.failed = false;
		};

		if (handle != INVALID_HANDLE_VALUE)
		{
			push_test(data.cFileName);
			while (FindNextFile(handle, &data))
			{
				push_test(data.cFileName);
			}
			FindClose(handle);
		}
		Lumix::g_log_info.log("render_test") << "Found " << m_tests.size() << " tests";
	}


	bool nextTest()
	{
		Lumix::FS::FileSystem& fs = m_engine->getFileSystem();

		bool can_do_next_test = m_current_test == -1 ||
								(!m_engine->getFileSystem().hasWork() && m_is_test_universe_loaded);
		if (can_do_next_test)
		{
			char path[Lumix::MAX_PATH_LENGTH];
			if (m_current_test >= 0)
			{
				Lumix::Renderer* renderer = static_cast<Lumix::Renderer*>(
					m_engine->getPluginManager().getPlugin("renderer"));

				Lumix::copyString(path, sizeof(path), m_tests[m_current_test].path);
				Lumix::catString(path, sizeof(path), "_res.tga");
				m_pipeline->setViewport(0, 0, 600, 400);
				m_pipeline->render();
				renderer->makeScreenshot(Lumix::Path(path));
				renderer->frame();
				m_pipeline->setViewport(0, 0, 600, 400);
				m_pipeline->render();
				renderer->frame();

				char path_preimage[Lumix::MAX_PATH_LENGTH];
				Lumix::copyString(path_preimage, sizeof(path), m_tests[m_current_test].path);
				Lumix::catString(path_preimage, sizeof(path), ".tga");

				auto file1 = fs.open(fs.getDefaultDevice(),
					Lumix::Path(path),
					Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
				auto file2 = fs.open(fs.getDefaultDevice(),
					Lumix::Path(path_preimage),
					Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
				if(!file1)
				{
					if (file2) fs.close(*file2);
					Lumix::g_log_error.log("render_test") << "Failed to open " << path;
				}
				else if(!file2)
				{
					fs.close(*file1);
					Lumix::g_log_error.log("render_test") << "Failed to open " << path_preimage;
				}
				else
				{
					unsigned int difference = Lumix::Texture::compareTGA(m_allocator, file1, file2, 10);
					Lumix::g_log_info.log("render_test") << "Difference between " << path << " and "
						<< path_preimage << " is " << difference;
					fs.close(*file1);
					fs.close(*file2);
					m_tests[m_current_test].failed = difference > 100;
				}
			}

			++m_current_test;
			if (m_current_test < m_tests.size())
			{
				Lumix::copyString(path, sizeof(path), m_tests[m_current_test].path);
				Lumix::catString(path, sizeof(path), ".unv");
				Lumix::g_log_info.log("render_test") << "Loading " << path << "...";
				Lumix::FS::ReadCallback file_read_cb;
				file_read_cb.bind<App, &App::universeFileLoaded>(this);
				fs.openAsync(fs.getDefaultDevice(),
					Lumix::Path(path),
					Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ,
					file_read_cb);
				m_is_test_universe_loaded = false;
				return true;
			}
			return false;
		}
		return true;
	}


	void run()
	{
		m_finished = false;
		while (!m_finished)
		{
			m_engine->update(*m_universe_context);
			m_pipeline->setViewport(0, 0, 600, 400);
			m_pipeline->render();
			auto* renderer = m_engine->getPluginManager().getPlugin("renderer");
			static_cast<Lumix::Renderer*>(renderer)->frame();
			if (!m_engine->getFileSystem().hasWork())
			{
				if (!nextTest()) return;
			}
			m_engine->getFileSystem().updateAsyncTransactions();
			Lumix::MT::sleep(100);
			handleEvents();
		}
		int failed_count = getFailedCount();
		if (failed_count)
		{
			Lumix::g_log_info.log("render_test") << failed_count << " tests failed";
		}
	}


	int getFailedCount() const
	{
		int count = 0;
		for (auto& test : m_tests)
		{
			if (test.failed) ++count;
		}
		return count;
	}


private:
	struct Test
	{
		char path[Lumix::MAX_PATH_LENGTH];
		bool failed;
	};

	Lumix::DefaultAllocator m_allocator;
	Lumix::Engine* m_engine;
	Lumix::UniverseContext* m_universe_context;
	Lumix::Pipeline* m_pipeline;
	Lumix::Array<Test> m_tests;
	int m_current_test;
	bool m_is_test_universe_loaded;
	bool m_finished;
	HWND m_hwnd;
};


INT WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, INT)
{
	App app;
	app.init();
	app.run();
	int failed_count = app.getFailedCount();
	app.shutdown();
	return failed_count;
}
