#include "core/blob.h"
#include "core/crc32.h"
#include "core/default_allocator.h"
#include "core/input_system.h"
#include "core/resource_manager.h"
#include "debug/allocator.h"
#include "engine.h"
#include "engine/plugin_manager.h"
#include "renderer/material.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include "renderer/transient_geometry.h"
#include "ocornut-imgui/imgui.h"

#include <bgfx.h>
#include <cstdio>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


// http://prideout.net/blog/?p=36


void imGuiCallback(ImDrawData* draw_data);


class Context
{
public:
	Context()
		: m_allocator(m_main_allocator)
	{
	}


	void onGUI()
	{
		ImGuiIO& io = ImGui::GetIO();

		RECT rect;
		GetClientRect(m_hwnd, &rect);
		io.DisplaySize = ImVec2((float)(rect.right - rect.left),
								(float)(rect.bottom - rect.top));

		io.DeltaTime = m_engine->getLastTimeDelta();

		io.KeyCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		io.KeyShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		io.KeyAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
		// io.KeysDown : filled by WM_KEYDOWN/WM_KEYUP events
		// io.MousePos : filled by WM_MOUSEMOVE events
		// io.MouseDown : filled by WM_*BUTTON* events
		// io.MouseWheel : filled by WM_MOUSEWHEEL events

		SetCursor(io.MouseDrawCursor ? NULL : LoadCursor(NULL, IDC_ARROW));

		ImGui::NewFrame();

		ImGui::ShowTestWindow();

		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Exit"))
				{
					PostQuitMessage(0);
				}
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}

		ImGui::Render();
	}


	void shutdown()
	{
		shutdownImGui();

		m_engine->stopGame(*m_universe_context);
		m_engine->destroyUniverse(*m_universe_context);
		Lumix::PipelineInstance::destroy(m_pipeline);
		m_pipeline_source->getResourceManager()
			.get(Lumix::ResourceManager::PIPELINE)
			->unload(*m_pipeline_source);
		Lumix::Engine::destroy(m_engine);
		m_engine = nullptr;
		m_pipeline = nullptr;
		m_pipeline_source = nullptr;
	}


	void shutdownImGui()
	{
		ImGui::Shutdown();
		
		Lumix::Texture* texture = m_material->getTexture(0);
		m_material->setTexture(0, nullptr);
		texture->destroy();
		m_allocator.deleteObject(texture);

		m_material->getResourceManager()
			.get(Lumix::ResourceManager::MATERIAL)
			->unload(*m_material);
	}


	void initIMGUI(HWND hwnd)
	{
		m_hwnd = hwnd;
		m_decl.begin()
			.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
			.end();

		ImGuiIO& io = ImGui::GetIO();
		io.KeyMap[ImGuiKey_Tab] = VK_TAB;
		io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
		io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
		io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
		io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
		io.KeyMap[ImGuiKey_PageUp] = VK_PRIOR;
		io.KeyMap[ImGuiKey_PageDown] = VK_NEXT;
		io.KeyMap[ImGuiKey_Home] = VK_HOME;
		io.KeyMap[ImGuiKey_End] = VK_END;
		io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
		io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
		io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
		io.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
		io.KeyMap[ImGuiKey_A] = 'A';
		io.KeyMap[ImGuiKey_C] = 'C';
		io.KeyMap[ImGuiKey_V] = 'V';
		io.KeyMap[ImGuiKey_X] = 'X';
		io.KeyMap[ImGuiKey_Y] = 'Y';
		io.KeyMap[ImGuiKey_Z] = 'Z';

		io.RenderDrawListsFn = imGuiCallback;
		io.ImeWindowHandle = hwnd;

		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		m_material = static_cast<Lumix::Material*>(
			m_engine->getResourceManager()
				.get(Lumix::ResourceManager::MATERIAL)
				->load(Lumix::Path("models/imgui.mat")));

		Lumix::Texture* texture = m_allocator.newObject<Lumix::Texture>(
			Lumix::Path("font"), m_engine->getResourceManager(), m_allocator);

		texture->create(width, height, pixels);
		m_material->setTexture(0, texture);
	}


	void init(HWND win)
	{
		Lumix::Renderer::setInitData(win);
		m_engine = Lumix::Engine::create(nullptr, m_allocator);
		m_engine->loadPlugin("renderer.dll");
		m_engine->loadPlugin("lua_script.dll");
		m_engine->loadPlugin("animation.dll");
		m_engine->loadPlugin("physics.dll");

		m_pipeline_source = static_cast<Lumix::Pipeline*>(
			m_engine->getResourceManager()
				.get(Lumix::ResourceManager::PIPELINE)
				->load(Lumix::Path("pipelines/game_view.lua")));
		m_pipeline = Lumix::PipelineInstance::create(*m_pipeline_source,
													 m_engine->getAllocator());

		m_universe_context = &m_engine->createUniverse();
		ASSERT(m_universe_context);

		auto fp = fopen("main.unv", "rb");
		fseek(fp, 0, SEEK_END);
		auto l = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		Lumix::Array<char*> b(m_allocator);
		b.resize(l);
		fread(&b[0], 1, l, fp);
		fclose(fp);

		Lumix::InputBlob blob(&b[0], b.size());

		uint32_t engine_hash;
		blob.read(engine_hash);
		uint32_t editor_hash;
		blob.read(editor_hash);
		m_engine->deserialize(*m_universe_context, blob);
		m_pipeline->setScene((Lumix::RenderScene*)m_universe_context->getScene(
			Lumix::crc32("renderer")));

		RECT rect;
		GetClientRect(win, &rect);
		m_pipeline->resize(rect.right, rect.bottom);

		initIMGUI(win);
	}

	HWND m_hwnd;
	bgfx::VertexDecl m_decl;
	Lumix::Material* m_material;
	Lumix::UniverseContext* m_universe_context;
	Lumix::Engine* m_engine;
	Lumix::Pipeline* m_pipeline_source;
	Lumix::PipelineInstance* m_pipeline;
	Lumix::DefaultAllocator m_main_allocator;
	Lumix::Debug::Allocator m_allocator;
};


Context g_context;


static void imGuiCallback(ImDrawData* draw_data)
{
	if (!g_context.m_material || !g_context.m_material->isReady())
	{
		return;
	}

	const float width = ImGui::GetIO().DisplaySize.x;
	const float height = ImGui::GetIO().DisplaySize.y;

	Lumix::Matrix ortho;
	ortho.setOrtho(0.0f, width, 0.0f, height, -1.0f, 1.0f);

	g_context.m_pipeline->setViewProjection(ortho, (int)width, (int)height);

	for (int32_t ii = 0; ii < draw_data->CmdListsCount; ++ii)
	{
		ImDrawList* cmd_list = draw_data->CmdLists[ii];

		Lumix::TransientGeometry geom(&cmd_list->VtxBuffer[0],
									  cmd_list->VtxBuffer.size(),
									  g_context.m_decl,
									  &cmd_list->IdxBuffer[0],
									  cmd_list->IdxBuffer.size());

		if (geom.getNumVertices() < 0)
		{
			break;
		}

		uint32_t elem_offset = 0;
		const ImDrawCmd* pcmd_begin = cmd_list->CmdBuffer.begin();
		const ImDrawCmd* pcmd_end = cmd_list->CmdBuffer.end();
		for (const ImDrawCmd* pcmd = pcmd_begin; pcmd != pcmd_end; pcmd++)
		{
			if (pcmd->UserCallback)
			{
				pcmd->UserCallback(cmd_list, pcmd);
				elem_offset += pcmd->ElemCount;
				continue;
			}
			if (0 == pcmd->ElemCount)
			{
				continue;
			}

			g_context.m_pipeline->setScissor(
				uint16_t(Lumix::Math::maxValue(pcmd->ClipRect.x, 0.0f)),
				uint16_t(Lumix::Math::maxValue(pcmd->ClipRect.y, 0.0f)),
				uint16_t(Lumix::Math::minValue(pcmd->ClipRect.z, 65535.0f) -
						 Lumix::Math::maxValue(pcmd->ClipRect.x, 0.0f)),
				uint16_t(Lumix::Math::minValue(pcmd->ClipRect.w, 65535.0f) -
						 Lumix::Math::maxValue(pcmd->ClipRect.y, 0.0f)));
			
				g_context.m_pipeline->render(
					geom, elem_offset, pcmd->ElemCount, *g_context.m_material);

			elem_offset += pcmd->ElemCount;
		}
	}
}


LRESULT WINAPI msgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	int x = LOWORD(lParam);
	int y = HIWORD(lParam);
	static int old_x = x;
	static int old_y = y;
	if (!g_context.m_pipeline)
	{
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}

	switch (msg)
	{
		case WM_SIZE:
		{
			uint32_t width = ((int)(short)LOWORD(lParam));
			uint32_t height = ((int)(short)HIWORD(lParam));
			g_context.m_pipeline->resize(width, height);
		}
		break;
		case WM_ERASEBKGND:
			return 1;
		case WM_LBUTTONUP:
			ImGui::GetIO().MouseDown[0] = false;
			break;
		case WM_LBUTTONDOWN:
			ImGui::GetIO().MouseDown[0] = true;
			break;
		case WM_RBUTTONDOWN:
			ImGui::GetIO().MouseDown[1] = true;
			break;
		case WM_RBUTTONUP:
			ImGui::GetIO().MouseDown[1] = false;
			break;
		case WM_MOUSEMOVE:
		{
			auto& input_system = g_context.m_engine->getInputSystem();
			input_system.injectMouseXMove(float(old_x - x));
			input_system.injectMouseYMove(float(old_y - y));
			old_x = x;
			old_y = y;

			{
				ImGuiIO& io = ImGui::GetIO();
				io.MousePos.x = (float)x;
				io.MousePos.y = (float)y;
			}
		}
		break;
		case WM_CHAR:
			ImGui::GetIO().AddInputCharacter((ImWchar)wParam);
			break;
		case WM_KEYUP:
			ImGui::GetIO().KeysDown[wParam] = false;
			break;
		case WM_KEYDOWN:
		{
			ImGui::GetIO().KeysDown[wParam] = true;
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

	g_context.init(hwnd);
	SetWindowTextA(hwnd, "Lumix Sample app");

	while (g_context.m_engine->getResourceManager().isLoading())
	{
		g_context.m_engine->update(*g_context.m_universe_context);
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
			Lumix::Renderer* renderer = static_cast<Lumix::Renderer*>(
				g_context.m_engine->getPluginManager().getPlugin("renderer"));
			g_context.m_engine->update(*g_context.m_universe_context);
			g_context.m_pipeline->render();
			g_context.onGUI();
			renderer->frame();
		}
	}

	g_context.shutdown();

	UnregisterClassA(szName, wnd.hInstance);

	return 0;
}