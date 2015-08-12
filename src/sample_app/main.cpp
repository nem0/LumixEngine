#include "core/blob.h"
#include "core/crc32.h"
#include "core/default_allocator.h"
#include "core/input_system.h"
#include "core/resource_manager.h"
#include "debug/allocator.h"
#include "engine.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"

#include <cstdio>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


// http://prideout.net/blog/?p=36


struct Context
{
	Context()
		: m_allocator(m_main_allocator)
	{
	}

	Lumix::UniverseContext* m_universe_context;
	Lumix::Engine* m_engine;
	Lumix::Pipeline* m_pipeline_source;
	Lumix::PipelineInstance* m_pipeline;
	Lumix::DefaultAllocator m_main_allocator;
	Lumix::Debug::Allocator m_allocator;
};


Context g_context;


void init(HWND win)
{
	Lumix::Renderer::setInitData(win);
	g_context.m_engine = Lumix::Engine::create(nullptr, g_context.m_allocator);
	g_context.m_engine->loadPlugin("lua_script.dll");
	g_context.m_engine->loadPlugin("animation.dll");
	g_context.m_engine->loadPlugin("physics.dll");

	g_context.m_pipeline_source = static_cast<Lumix::Pipeline*>(
		g_context.m_engine->getResourceManager()
			.get(Lumix::ResourceManager::PIPELINE)
			->load(Lumix::Path("pipelines/game_view.lua")));
	g_context.m_pipeline = Lumix::PipelineInstance::create(
		*g_context.m_pipeline_source, g_context.m_engine->getAllocator());

	g_context.m_universe_context = &g_context.m_engine->createUniverse();
	ASSERT(g_context.m_universe_context);

	auto fp = fopen("main.unv", "rb");
	fseek(fp, 0, SEEK_END);
	auto l = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	Lumix::Array<char*> b(g_context.m_allocator);
	b.resize(l);
	fread(&b[0], 1, l, fp);
	fclose(fp);

	Lumix::InputBlob blob(&b[0], b.size());

	uint32_t engine_hash;
	blob.read(engine_hash);
	uint32_t editor_hash;
	blob.read(editor_hash);
	g_context.m_engine->deserialize(*g_context.m_universe_context, blob);
	g_context.m_pipeline->setScene(
		(Lumix::RenderScene*)g_context.m_universe_context->getScene(
			Lumix::crc32("renderer")));

	g_context.m_pipeline->resize(800, 600);
}


LRESULT WINAPI msgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	int x = LOWORD(lParam);
	int y = HIWORD(lParam);
	static int old_x = x;
	static int old_y = y;
	switch (msg)
	{
		case WM_ERASEBKGND:
			return 1;
		case WM_LBUTTONUP:
			break;
		case WM_LBUTTONDOWN:
			break;
		case WM_MOUSEMOVE:
		{
			auto& input_system = g_context.m_engine->getInputSystem();
			input_system.injectMouseXMove((old_x - x) / 1.0f);
			input_system.injectMouseYMove((old_y - y) / 1.0f);
			old_x = x;
			old_y = y;
		}
		break;

		case WM_KEYDOWN:
		{
			switch (wParam)
			{
				case VK_ESCAPE:
					PostQuitMessage(0);
					break;
				case VK_OEM_2: // Question Mark / Forward Slash for US Keyboards
					break;
			}
			break;
		}
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}


INT WINAPI WinMain(HINSTANCE hInst,
				   HINSTANCE ignoreMe0,
				   LPSTR ignoreMe1,
				   INT ignoreMe2)
{
	LPCSTR szName = "Lumix Sample App";
	WNDCLASSEX wnd;
	memset(&wnd, 0, sizeof(wnd));
	wnd.cbSize = sizeof(wnd);
	wnd.style = CS_HREDRAW | CS_VREDRAW;
	wnd.lpfnWndProc = msgProc;
	wnd.hInstance = hInst;
	wnd.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wnd.hCursor = LoadCursor(NULL, IDC_ARROW);
	wnd.lpszClassName = "lmxa";
	wnd.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	auto x = RegisterClassExA(&wnd);

	HWND hwnd = CreateWindowA("lmxa",
							  "lmxa",
							  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
							  0,
							  0,
							  800,
							  600,
							  NULL,
							  NULL,
							  hInst,
							  0);
	auto e = GetLastError();
	ASSERT(hwnd);

	init(hwnd);
	SetWindowTextA(hwnd, "Lumix Sample app");

	while (g_context.m_engine->getResourceManager().isLoading())
	{
		g_context.m_engine->update(*g_context.m_universe_context, 1.0f, -1.0f);
	}

	g_context.m_engine->startGame(*g_context.m_universe_context);

	MSG msg = {0};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			g_context.m_engine->update(*g_context.m_universe_context, 1.0f, -1.0f);
			g_context.m_pipeline->render();
			g_context.m_engine->getRenderer().frame();
		}
	}

	g_context.m_engine->stopGame(*g_context.m_universe_context);

	g_context.m_engine->destroyUniverse(*g_context.m_universe_context);

	Lumix::PipelineInstance::destroy(g_context.m_pipeline);
	g_context.m_pipeline_source->getResourceManager()
		.get(Lumix::ResourceManager::PIPELINE)
		->unload(*g_context.m_pipeline_source);

	Lumix::Engine::destroy(g_context.m_engine);

	UnregisterClassA(szName, wnd.hInstance);

	return 0;
}