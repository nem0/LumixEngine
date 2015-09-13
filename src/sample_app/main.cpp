#include "core/blob.h"
#include "core/crc32.h"
#include "core/default_allocator.h"
#include "core/input_system.h"
#include "core/path_utils.h"
#include "core/resource_manager.h"
#include "debug/allocator.h"
#include "editor/gizmo.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "engine/plugin_manager.h"
#include "engine/property_descriptor.h"
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

		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("New"))
				{
					m_editor->newUniverse();
				}
				if (ImGui::MenuItem("Open"))
				{
					m_editor->loadUniverse(Lumix::Path("main.unv"));
				}
				if (ImGui::MenuItem("Exit"))
				{
					PostQuitMessage(0);
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Edit"))
			{
				if (ImGui::MenuItem("Undo", "Ctrl - Z"))
				{
					m_editor->undo();
				}
				if (ImGui::MenuItem("Redo", "Ctrl - Shift - Z"))
				{
					m_editor->redo();
				}
				ImGui::EndMenu();
			}
		
			if (ImGui::BeginMenu("Tools"))
			{
				if (ImGui::MenuItem("Snap to terrain", "Ctrl - T"))
				{
					m_editor->snapToTerrain();
				}
				if (ImGui::MenuItem("Look at selected", "Ctrl - F"))
				{
					m_editor->lookAtSelected();
				}
				ImGui::EndMenu();
			}

			ImGui::EndMainMenuBar();
		}

		showPropertyGrid();
		showEntityList();
		showFPS();

		ImGui::Render();
	}


	const char* getComponentTypeName(Lumix::ComponentUID cmp)
	{
		for (int i = 0; i < m_engine->getComponentTypesCount(); ++i)
		{
			if (cmp.type ==
				Lumix::crc32(m_engine->getComponentTypeID(i)))
			{
				return m_engine->getComponentTypeName(i);
			}
		}
		return "Unknown";
	}



	void showProperty(Lumix::IPropertyDescriptor& desc, Lumix::ComponentUID cmp)
	{
		Lumix::OutputBlob stream(m_editor->getAllocator());
		desc.get(cmp, stream);
		Lumix::InputBlob tmp(stream);

		switch (desc.getType())
		{
			case Lumix::IPropertyDescriptor::DECIMAL:
			{
				float f;
				tmp.read(f);
				auto& d = static_cast<Lumix::IDecimalPropertyDescriptor&>(desc);
				if ((d.getMax() - d.getMin()) / d.getStep() <= 100)
				{
					if (ImGui::SliderFloat(desc.getName(), &f, d.getMin(), d.getMax()))
					{
						m_editor->setProperty(cmp.type, -1, desc, &f, sizeof(f));
					}
				}
				else
				{
					if (ImGui::DragFloat(desc.getName(), &f, d.getStep(), d.getMin(), d.getMax()))
					{
						m_editor->setProperty(cmp.type, -1, desc, &f, sizeof(f));
					}
				}
				break;
			}
			case Lumix::IPropertyDescriptor::BOOL:
			{
				bool b;
				tmp.read(b);
				if (ImGui::Checkbox(desc.getName(), &b))
				{
					m_editor->setProperty(cmp.type, -1, desc, &b, sizeof(b));
				}
				break;
			}
			case Lumix::IPropertyDescriptor::COLOR:
			{
				Lumix::Vec3 v;
				tmp.read(v);
				if (ImGui::ColorEdit3(desc.getName(), &v.x))
				{
					m_editor->setProperty(cmp.type, -1, desc, &v, sizeof(v));
				}
				break;
			}
			case Lumix::IPropertyDescriptor::VEC3:
			{
				Lumix::Vec3 v;
				tmp.read(v);
				if (ImGui::DragFloat3(desc.getName(), &v.x))
				{
					m_editor->setProperty(cmp.type, -1, desc, &v, sizeof(v));
				}
				break;
			}
			case Lumix::IPropertyDescriptor::VEC4:
			{
				Lumix::Vec4 v;
				tmp.read(v);
				if (ImGui::DragFloat4(desc.getName(), &v.x))
				{
					m_editor->setProperty(cmp.type, -1, desc, &v, sizeof(v));
				}
				break;
			}
			case Lumix::IPropertyDescriptor::STRING:
			case Lumix::IPropertyDescriptor::FILE:
			case Lumix::IPropertyDescriptor::RESOURCE:
			{
				char buf[1024];
				Lumix::copyString(buf, sizeof(buf), (const char*)stream.getData());
				if (ImGui::InputText(desc.getName(), buf, sizeof(buf)))
				{
					m_editor->setProperty(cmp.type, -1, desc, buf, strlen(buf) + 1);
				}
				break;
			}
			case Lumix::IPropertyDescriptor::ARRAY:
				break;
			default:
				ASSERT(false);
				break;
		}
	}


	void showComponentProperties(Lumix::ComponentUID cmp)
	{
		if (!ImGui::CollapsingHeader(
				getComponentTypeName(cmp), nullptr, true, true))
			return;

		auto& descs = m_engine->getPropertyDescriptors(cmp.type);

		for (auto* desc : descs)
		{
			showProperty(*desc, cmp);
		}
	}


	void showCoreProperties(Lumix::Entity entity)
	{
		char name[256];
		const char* tmp = m_editor->getUniverse()->getEntityName(entity);
		Lumix::copyString(name, sizeof(name), tmp);
		if (ImGui::InputText("name", name, sizeof(name)))
		{
			m_editor->setEntityName(entity, name);
		}

		auto pos = m_editor->getUniverse()->getPosition(entity);
		if (ImGui::DragFloat3("position", &pos.x))
		{
			m_editor->setEntitiesPositions(&entity, &pos, 1);
		}

		auto rot = m_editor->getUniverse()->getRotation(entity);
		if (ImGui::DragFloat4("rotation", &rot.x))
		{
			m_editor->setEntitiesRotations(&entity, &rot, 1);
		}
	}


	void showPropertyGrid()
	{
		auto& ents = m_editor->getSelectedEntities();
		if (ImGui::Begin("Properties") && ents.size() == 1)
		{
			showCoreProperties(ents[0]);


			auto& cmps = m_editor->getComponents(ents[0]);
			for (auto cmp : cmps)
			{
				showComponentProperties(cmp);
			}
		}
		ImGui::End();
	}


	void showFPS()
	{
		ImGui::SetNextWindowPos(ImVec2(10, 30));
		bool opened;
		if (!ImGui::Begin("Example: Fixed Overlay", &opened, ImVec2(0, 0), 0.3f, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::End();
			return;
		}
		ImGui::Text("FPS: (%.1f)", m_engine->getFPS());
		ImGui::End();

	}


	void getEntityListDisplayName(char* buf, int max_size, Lumix::Entity entity)
	{
		const char* name = m_editor->getUniverse()->getEntityName(entity);
		static const uint32_t RENDERABLE_HASH = Lumix::crc32("renderable");
		Lumix::ComponentUID renderable =
			m_editor->getComponent(entity, RENDERABLE_HASH);
		if (renderable.isValid())
		{
			auto* scene = static_cast<Lumix::RenderScene*>(renderable.scene);
			const char* path = scene->getRenderablePath(renderable.index);
			if (path && path[0] != 0)
			{
				char basename[Lumix::MAX_PATH_LENGTH];
				Lumix::copyString(buf, max_size, path);
				Lumix::PathUtils::getBasename(
					basename, Lumix::MAX_PATH_LENGTH, path);
				if (name && name[0] != '\0')
					Lumix::copyString(buf, max_size, name);
				else
					Lumix::toCString(entity, buf, max_size);

				Lumix::catString(buf, max_size, " - ");
				Lumix::catString(buf, max_size, basename);
				return;
			}
		}

		if (name && name[0] != '\0')
		{
			Lumix::copyString(buf, max_size, name);
		}
		else
		{
			Lumix::toCString(entity, buf, max_size);
		}
	}


	void showEntityList()
	{
		if (ImGui::Begin("Entity list"))
		{
			if (ImGui::Button("Create entity"))
			{
				m_editor->addEntity();
			}
			auto* universe = m_editor->getUniverse();
			auto entity = universe->getFirstEntity();
			while (entity >= 0)
			{
				char buf[1024];

				getEntityListDisplayName(buf, sizeof(buf), entity);
				if (ImGui::TreeNode((void*)entity, buf))
				{
					ImGui::TreePop();
				}
				entity = universe->getNextEntity(entity);
			}
		}
		ImGui::End();
	}


	void shutdown()
	{
		shutdownImGui();

		Lumix::WorldEditor::destroy(m_editor);
		Lumix::PipelineInstance::destroy(m_pipeline);
		m_pipeline_source->getResourceManager()
			.get(Lumix::ResourceManager::PIPELINE)
			->unload(*m_pipeline_source);
		Lumix::Engine::destroy(m_engine);
		m_engine = nullptr;
		m_pipeline = nullptr;
		m_pipeline_source = nullptr;
		m_editor = nullptr;
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


	void onUniverseCreated()
	{
		m_pipeline->setScene(static_cast<Lumix::RenderScene*>(
			m_editor->getScene(Lumix::crc32("renderer"))));
	}


	void onUniverseDestroyed()
	{
		m_pipeline->setScene(nullptr);
	}


	void renderGizmos()
	{
		m_editor->renderIcons(*m_pipeline);
		m_editor->getGizmo().updateScale(m_editor->getEditCamera().index);
		m_editor->getGizmo().render(*m_pipeline);
	}


	void init(HWND win)
	{
		Lumix::Renderer::setInitData(win);
		m_engine = Lumix::Engine::create(nullptr, m_allocator);
		m_editor = Lumix::WorldEditor::create("", *m_engine);

		m_editor->universeCreated().bind<Context, &Context::onUniverseCreated>(this);
		m_editor->universeDestroyed().bind<Context, &Context::onUniverseDestroyed>(this);

		m_pipeline_source = static_cast<Lumix::Pipeline*>(
			m_engine->getResourceManager()
				.get(Lumix::ResourceManager::PIPELINE)
				->load(Lumix::Path("pipelines/main.lua")));
		m_pipeline = Lumix::PipelineInstance::create(*m_pipeline_source,
													 m_engine->getAllocator());
		m_pipeline->addCustomCommandHandler("render_gizmos")
			.bind<Context, &Context::renderGizmos>(this);

		RECT rect;
		GetClientRect(win, &rect);
		m_pipeline->resize(rect.right, rect.bottom);

		initIMGUI(win);
	}

	void checkShortcuts()
	{
		if (ImGui::GetIO().KeysDown[VK_CONTROL])
		{
			if (ImGui::GetIO().KeysDown['F'])
			{
				m_editor->lookAtSelected();
			}
			if (ImGui::GetIO().KeysDown['T'])
			{
				m_editor->snapToTerrain();
			}
			if (ImGui::GetIO().KeysDown['Z'])
			{
				if (ImGui::GetIO().KeysDown[VK_SHIFT])
				{
					m_editor->redo();
				}
				else
				{
					m_editor->undo();
				}
			}
		}
	}


	void updateNavigation()
	{
		if (ImGui::IsMouseHoveringAnyWindow()) return;
		float speed = 0.1f;
		if (ImGui::GetIO().KeysDown[VK_SHIFT])
		{
			speed *= 10;
		}
		if (ImGui::GetIO().KeysDown['W'])
		{
			m_editor->navigate(1.0f, 0, speed);
		}
		if (ImGui::GetIO().KeysDown['S'])
		{
			m_editor->navigate(-1.0f, 0, speed);
		}
		if (ImGui::GetIO().KeysDown['A'])
		{
			m_editor->navigate(0.0f, -1.0f, speed);
		}
		if (ImGui::GetIO().KeysDown['D'])
		{
			m_editor->navigate(0.0f, 1.0f, speed);
		}
	}


	HWND m_hwnd;
	bgfx::VertexDecl m_decl;
	Lumix::Material* m_material;
	Lumix::Engine* m_engine;
	Lumix::Pipeline* m_pipeline_source;
	Lumix::PipelineInstance* m_pipeline;
	Lumix::DefaultAllocator m_main_allocator;
	Lumix::Debug::Allocator m_allocator;
	Lumix::WorldEditor* m_editor;
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
		case WM_CLOSE:
			PostQuitMessage(0);
			break;
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
			g_context.m_editor->onMouseUp(old_x, old_y, Lumix::MouseButton::LEFT);
			ImGui::GetIO().MouseDown[0] = false;
			break;
		case WM_LBUTTONDOWN:
			if (!ImGui::IsMouseHoveringAnyWindow())
			{
				g_context.m_editor->onMouseDown(old_x, old_y, Lumix::MouseButton::LEFT);
			}
			ImGui::GetIO().MouseDown[0] = true;
			break;
		case WM_RBUTTONDOWN:
			if (!ImGui::IsMouseHoveringAnyWindow())
			{
				g_context.m_editor->onMouseDown(old_x, old_y, Lumix::MouseButton::RIGHT);
			}
			ImGui::GetIO().MouseDown[1] = true;
			break;
		case WM_RBUTTONUP:
			g_context.m_editor->onMouseUp(old_x, old_y, Lumix::MouseButton::RIGHT);
			ImGui::GetIO().MouseDown[1] = false;
			break;
		case WM_MOUSEMOVE:
		{
			int flags = ImGui::GetIO().KeysDown[VK_MENU]
							? (int)Lumix::WorldEditor::MouseFlags::ALT
							: 0;
			g_context.m_editor->onMouseMove(x, y, x - old_x, y - old_y, flags);
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
		case WM_SYSKEYDOWN:
			ImGui::GetIO().KeysDown[wParam] = true;
			break;
		case WM_SYSKEYUP:
			ImGui::GetIO().KeysDown[wParam] = true;
			break;
		case WM_KEYDOWN:
		{
			ImGui::GetIO().KeysDown[wParam] = true;
			g_context.checkShortcuts();
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
		g_context.m_engine->update(*g_context.m_editor->getUniverseContext());
	}

	bool finished = false;
	while(!finished)
	{
		MSG msg = { 0 };
		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT)
			{
				finished = true;
			}
		}

		g_context.m_editor->update();
		g_context.updateNavigation();
		Lumix::Renderer* renderer = static_cast<Lumix::Renderer*>(
			g_context.m_engine->getPluginManager().getPlugin("renderer"));
		g_context.m_engine->update(*g_context.m_editor->getUniverseContext());
		g_context.m_pipeline->render();
		g_context.onGUI();
		renderer->frame();
	}

	g_context.shutdown();

	UnregisterClassA(szName, wnd.hInstance);

	return 0;
}