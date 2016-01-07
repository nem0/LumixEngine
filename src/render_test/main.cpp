#include "core/blob.h"
#include "core/crc32.h"
#include "core/FS/file_system.h"
#include "core/FS/ifile.h"
#include "core/log.h"
#include "core/path_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/world_editor.h"
#include "editor/gizmo.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include <Windows.h>


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
		Lumix::InputBlob blob(file.getBuffer(), file.size());
		uint32_t hash = 0;
		blob.read(hash);
		uint32_t engine_hash = 0;
		blob.read(engine_hash);
		if (Lumix::crc32((const uint8_t*)blob.getData() + sizeof(hash),
				blob.getSize() - sizeof(hash)) != hash)
		{
			ASSERT(false);
			return;
		}
		bool deserialize_succeeded = m_engine->deserialize(*m_universe_context, blob);
		m_is_test_universe_loaded = true;
		ASSERT(deserialize_succeeded);
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
		auto hwnd = CreateWindowA(
			"render_test", "render_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 800, 600, NULL, NULL, hInst, 0);

		Lumix::Renderer::setInitData(hwnd);

		m_hwnd = hwnd;
		return hwnd;
	}


	void init()
	{
		auto hwnd = createWindow();
		m_engine = Lumix::Engine::create(NULL, m_allocator);
		m_engine->getPluginManager().load("renderer.dll");
		m_engine->getPluginManager().load("animation.dll");
		m_engine->getPluginManager().load("audio.dll");
		m_engine->getPluginManager().load("lua_script.dll");
		m_engine->getPluginManager().load("physics.dll");
		Lumix::Pipeline* pipeline_object =
			static_cast<Lumix::Pipeline*>(m_engine->getResourceManager()
											  .get(Lumix::ResourceManager::PIPELINE)
											  ->load(Lumix::Path("pipelines/imgui.lua")));
		ASSERT(pipeline_object);
		if (pipeline_object)
		{
			m_pipeline =
				Lumix::PipelineInstance::create(*pipeline_object, m_engine->getAllocator());
		}

		m_universe_context = &m_engine->createUniverse();
		m_pipeline->setScene(
			(Lumix::RenderScene*)m_universe_context->getScene(Lumix::crc32("renderer")));
		m_pipeline->setViewport(0, 0, 600, 400);
		Lumix::Renderer* renderer = static_cast<Lumix::Renderer*>(
			m_engine->getPluginManager().getPlugin("renderer"));
		renderer->resize(600, 400);

		enumerateTests();
	}


	void shutdown()
	{
		if (m_pipeline)
		{
			Lumix::PipelineInstance::destroy(m_pipeline);
		}
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


	void enumerateTests()
	{
		WIN32_FIND_DATAA data;
		char buf[100];
		GetCurrentDirectory(100, buf);

		auto handle = FindFirstFile(".\\render_tests\\*.unv", &data);
		
		auto push_test = [&](const char* name) 
		{
			char basename[Lumix::MAX_PATH_LENGTH];
			Lumix::PathUtils::getBasename(basename, Lumix::lengthOf(basename), name);
			char tmp[Lumix::MAX_PATH_LENGTH];
			Lumix::copyString(tmp, "render_tests/");
			Lumix::catString(tmp, basename);
			m_tests.push(Lumix::string(tmp, m_allocator));
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
				TODO("TODO");
				return true;
				Lumix::Renderer* renderer = static_cast<Lumix::Renderer*>(
					m_engine->getPluginManager().getPlugin("renderer"));

				Lumix::copyString(path, sizeof(path), m_tests[m_current_test].c_str());
				Lumix::catString(path, sizeof(path), "_res.tga");
				renderer->makeScreenshot(Lumix::Path(path));

				char path_preimage[Lumix::MAX_PATH_LENGTH];
				Lumix::copyString(
					path_preimage, sizeof(path), m_tests[m_current_test].c_str());
				Lumix::catString(path_preimage, sizeof(path), ".tga");

				auto file1 = fs.open(
					fs.getDefaultDevice(), Lumix::Path(path), Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
				auto file2 = fs.open(fs.getDefaultDevice(),
					Lumix::Path(path_preimage),
					Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
				unsigned int diference = Lumix::Texture::compareTGA(m_allocator, file1, file2, 10);
				fs.close(*file1);
				fs.close(*file2);
				ASSERT(diference < 100);
			}

			++m_current_test;
			if (m_current_test < m_tests.size())
			{
				Lumix::copyString(path, sizeof(path), m_tests[m_current_test].c_str());
				Lumix::catString(path, sizeof(path), ".unv");
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
			static_cast<Lumix::Renderer*>(m_engine->getPluginManager().getPlugin("renderer"))->frame();
			if (!m_engine->getFileSystem().hasWork())
			{
				if (!nextTest()) return;
			}
			m_engine->getFileSystem().updateAsyncTransactions();
			handleEvents();
		}
	}

private:
	Lumix::DefaultAllocator m_allocator;
	Lumix::Engine* m_engine;
	Lumix::UniverseContext* m_universe_context;
	Lumix::PipelineInstance* m_pipeline;
	Lumix::Array<Lumix::string> m_tests;
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
	app.shutdown();
	return 0;
}
