#include "asset_browser.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/default_allocator.h"
#include "core/input_system.h"
#include "core/mt/thread.h"
#include "core/path_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/system.h"
#include "core/timer.h"
#include "debug/allocator.h"
#include "editor/gizmo.h"
#include "editor/entity_template_system.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "engine/plugin_manager.h"
#include "hierarchy_ui.h"
#include "import_asset_dialog.h"
#include "log_ui.h"
#include "profiler_ui.h"
#include "property_grid.h"
#include "renderer/frame_buffer.h"
#include "renderer/material.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include "renderer/transient_geometry.h"
#include "ocornut-imgui/imgui.h"
#include "scene_view.h"
#include "shader_compiler.h"
#include "universe/hierarchy.h"
#include "utils.h"

#include <bgfx.h>
#include <cstdio>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <mmsystem.h>

// http://prideout.net/blog/?p=36


void imGuiCallback(ImDrawData* draw_data);
LRESULT WINAPI msgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


class Context
{
public:
	Context()
		: m_is_entity_list_shown(true)
		, m_finished(false)
		, m_is_style_editor_shown(false)
		, m_import_asset_dialog(nullptr)
		, m_shader_compiler(nullptr)
		, m_is_wireframe(false)
		, m_is_entity_template_list_opened(false)
		, m_selected_template_name(m_allocator)
		, m_is_gameview_opened(true)
		, m_profiler_ui(nullptr)
		, m_asset_browser(nullptr)
		, m_property_grid(nullptr)
	{
		m_entity_list_filter[0] = '\0';
	}


	void update()
	{
		PROFILE_FUNCTION();
		float time_delta = m_editor->getEngine().getLastTimeDelta();

		m_editor->update();
		m_sceneview.update();
		m_engine->update(*m_editor->getUniverseContext());

		m_asset_browser->update();
		m_shader_compiler->update(time_delta);
		m_log_ui->update(time_delta);

		m_gui_pipeline->render();
		onGUI();
		Lumix::Renderer* renderer =
			static_cast<Lumix::Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));
		
		renderer->frame();
	}


	void onGUI()
	{
		PROFILE_FUNCTION();
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

		showMainMenu();

		m_profiler_ui->onGui();
		m_asset_browser->onGui();
		m_log_ui->onGui();
		m_import_asset_dialog->onGui();
		m_property_grid->onGui();
		showEntityList();
		showEntityTemplateList();
		m_sceneview.onGui();
		m_hierarchy_ui.onGui();
		showGameView();
		if (m_is_style_editor_shown) ImGui::ShowStyleEditor();

		ImGui::Render();
	}


	void showGameView()
	{
		if (!m_is_gameview_opened) return;

		if (ImGui::Begin("Game view", &m_is_gameview_opened))
		{
			m_is_gameview_hovered = ImGui::IsWindowHovered();
			auto size = ImGui::GetContentRegionAvail();
			if (size.x > 0 && size.y > 0)
			{
				auto pos = ImGui::GetWindowPos();
				auto cp = ImGui::GetCursorPos();
				int gameview_x = int(pos.x + cp.x);
				int gameview_y = int(pos.y + cp.y);
				m_game_pipeline->setViewport(0, 0, int(size.x), int(size.y));

				auto* fb = m_game_pipeline->getFramebuffer("default");
				m_gameview_texture_handle = fb->getRenderbufferHandle(0);
				ImGui::Image(&m_gameview_texture_handle, size);
				m_game_pipeline->render();
			}
		}
		ImGui::End();
	}


	void showMainMenu()
	{
		bool is_any_entity_selected = !m_editor->getSelectedEntities().empty();
		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("New")) m_editor->newUniverse();
				if (ImGui::BeginMenu("Open"))
				{
					auto& universes =
						m_asset_browser->getResources(AssetBrowser::UNIVERSE);
					for (auto& univ : universes)
					{
						if (ImGui::MenuItem(univ.c_str()))
						{
							m_editor->loadUniverse(univ);
						}
					}
					ImGui::EndMenu();
				}
				if (ImGui::MenuItem("Save", nullptr, nullptr, m_editor->getUniversePath().isValid()))
				{
					m_editor->saveUniverse(m_editor->getUniversePath());
				}
				if (ImGui::MenuItem("Save As"))
				{
					char filename[Lumix::MAX_PATH_LENGTH];
					if (Lumix::getSaveFilename(filename, sizeof(filename), "Universes\0*.unv\0", "unv"))
					{
						m_editor->saveUniverse(Lumix::Path(filename));
					}
				}
				if (ImGui::MenuItem("Exit")) PostQuitMessage(0);

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Edit"))
			{
				if (ImGui::MenuItem("Undo", "Ctrl - Z", nullptr, m_editor->canUndo())) m_editor->undo();
				if (ImGui::MenuItem("Redo", "Ctrl - Shift - Z", nullptr, m_editor->canRedo())) m_editor->redo();
				ImGui::Separator();
				if (ImGui::MenuItem("Copy", "Ctrl - C", nullptr, is_any_entity_selected)) m_editor->copyEntity();
				if (ImGui::MenuItem("Paste", "Ctrl - V", nullptr, m_editor->canPasteEntity())) m_editor->pasteEntity();
				ImGui::Separator();
				if (ImGui::MenuItem("Center/Pivot")) m_editor->getGizmo().togglePivotMode();
				if (ImGui::MenuItem("Local/Global")) m_editor->getGizmo().toggleCoordSystem();
				if (ImGui::BeginMenu("Select"))
				{
					if (ImGui::MenuItem("Same mesh",
						nullptr,
						nullptr,
						is_any_entity_selected))
						m_editor->selectEntitiesWithSameMesh();
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Entity"))
			{
				if (ImGui::MenuItem("Create", "Ctrl - E")) m_editor->addEntity();
				if (ImGui::MenuItem("Remove", "Delete", nullptr, is_any_entity_selected))
				{
					if (!m_editor->getSelectedEntities().empty())
					{
						m_editor->destroyEntities(
							&m_editor->getSelectedEntities()[0],
							m_editor->getSelectedEntities().size());
					}
				}

				if (ImGui::BeginMenu("Create template", is_any_entity_selected))
				{
					static char name[255] = "";
					ImGui::InputText("Name##templatename", name, sizeof(name));
					if (ImGui::Button("Create"))
					{
						auto entity = m_editor->getSelectedEntities()[0];
						auto& system = m_editor->getEntityTemplateSystem();
						system.createTemplateFromEntity(name, entity);
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndMenu();
				}
				if (ImGui::MenuItem("Instantiate template",
					nullptr,
					nullptr,
					m_selected_template_name.length() > 0))
				{
					Lumix::Vec3 pos = m_editor->getCameraRaycastHit();
					m_editor->getEntityTemplateSystem().createInstance(
						m_selected_template_name.c_str(), pos);
				}
				if (ImGui::MenuItem("Show", nullptr, nullptr, is_any_entity_selected)) m_editor->showEntities();
				if (ImGui::MenuItem("Hide", nullptr, nullptr, is_any_entity_selected)) m_editor->hideEntities();
				ImGui::EndMenu();
			}


			if (ImGui::BeginMenu("Tools"))
			{
				bool b = m_editor->isGameMode();
				if (ImGui::MenuItem("Game mode", "Ctrl - P", &b))
				{
					toggleGameMode();
				}


				b = m_editor->isMeasureToolActive();
				if (ImGui::MenuItem("Measure", nullptr, &b))
				{
					m_editor->toggleMeasure();
				}
				if (ImGui::MenuItem("Snap to terrain",
					"Ctrl - T",
					nullptr,
					is_any_entity_selected))
				{
					m_editor->snapToTerrain();
				}
				ImGui::MenuItem("Import asset", nullptr, &m_import_asset_dialog->m_is_opened);
				ImGui::EndMenu();
			}


			if (ImGui::BeginMenu("View"))
			{
				if (ImGui::MenuItem(
					"Look at selected", "Ctrl - F", nullptr, is_any_entity_selected))
				{
					m_editor->lookAtSelected();
				}
				if (ImGui::MenuItem("Wireframe", "Ctrl - W", &m_is_wireframe))
					m_sceneview.setWireframe(m_is_wireframe);
				if (ImGui::MenuItem("Stats")) m_gui_pipeline->toggleStats();
				if (ImGui::BeginMenu("Windows"))
				{
					ImGui::MenuItem("Asset browser", nullptr, &m_asset_browser->m_is_opened);
					ImGui::MenuItem("Entity list", nullptr, &m_is_entity_list_shown);
					ImGui::MenuItem("Entity templates", nullptr, &m_is_entity_template_list_opened);
					ImGui::MenuItem("Game view", nullptr, &m_is_gameview_opened);
					ImGui::MenuItem("Hierarchy", nullptr, &m_hierarchy_ui.m_is_opened);
					ImGui::MenuItem("Log", nullptr, &m_log_ui->m_is_opened);
					ImGui::MenuItem("Profiler", nullptr, &m_profiler_ui->m_is_opened);
					ImGui::MenuItem("Properties", nullptr, &m_property_grid->m_is_opened);
					ImGui::MenuItem("Style editor", nullptr, &m_is_style_editor_shown);
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}
			StringBuilder<100> stats("FPS: ");
			stats << m_engine->getFPS();
			auto stats_size = ImGui::CalcTextSize(stats);
			ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x);
			ImGui::Text(stats);

			ImGui::EndMainMenuBar();
		}
	}


	void toggleGameMode()
	{
		m_editor->toggleGameMode();
	}


	


	void showEntityTemplateList()
	{
		if (!m_is_entity_template_list_opened) return;

		if (ImGui::Begin("Entity templates", &m_is_entity_template_list_opened))
		{
			auto& template_system = m_editor->getEntityTemplateSystem();

			for (auto& template_name : template_system.getTemplateNames())
			{
				bool b = m_selected_template_name == template_name;
				if (ImGui::Selectable(template_name.c_str(), &b))
				{
					m_selected_template_name = template_name;
				}
			}
		}
		ImGui::End();
	}


	void showEntityList()
	{
		if (!m_is_entity_list_shown) return;

		if (ImGui::Begin("Entity list", &m_is_entity_list_shown))
		{
			if (ImGui::Button("Create entity"))
			{
				m_editor->addEntity();
			}
			ImGui::InputText("Filter", m_entity_list_filter, sizeof(m_entity_list_filter));
			ImGui::Separator();

			auto* universe = m_editor->getUniverse();
			auto entity = universe->getFirstEntity();

			if (ImGui::BeginChild("header"))
			{
				while (entity >= 0)
				{
					char buf[1024];
					getEntityListDisplayName(*m_editor, buf, sizeof(buf), entity);

					if (m_entity_list_filter[0] == '\0' || strstr(buf, m_entity_list_filter) != nullptr)
					{
						bool is_selected = m_editor->isEntitySelected(entity);
						if (ImGui::Selectable(buf, &is_selected))
						{
							if (ImGui::GetIO().KeysDown[VK_CONTROL])
							{
								m_editor->addEntityToSelection(entity);
							}
							else
							{
								m_editor->selectEntities(&entity, 1);
							}
						}
					}
					entity = universe->getNextEntity(entity);
				}
			}
			ImGui::EndChild();

		}
		ImGui::End();
	}


	void shutdown()
	{
		shutdownImGui();

		delete m_profiler_ui;
		delete m_asset_browser;
		delete m_log_ui;
		delete m_property_grid;
		delete m_import_asset_dialog;
		delete m_shader_compiler;
		Lumix::WorldEditor::destroy(m_editor, m_allocator);
		m_sceneview.shutdown();
		Lumix::PipelineInstance::destroy(m_gui_pipeline);
		Lumix::PipelineInstance::destroy(m_game_pipeline);
		m_gui_pipeline_source->getResourceManager()
			.get(Lumix::ResourceManager::PIPELINE)
			->unload(*m_gui_pipeline_source);
		m_game_pipeline_source->getResourceManager()
			.get(Lumix::ResourceManager::PIPELINE)
			->unload(*m_game_pipeline_source);
		Lumix::Engine::destroy(m_engine, m_allocator);
		m_engine = nullptr;
		m_gui_pipeline = nullptr;
		m_game_pipeline = nullptr;
		m_gui_pipeline_source = nullptr;
		m_game_pipeline_source = nullptr;
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
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->AddFontFromFileTTF("editor/VeraMono.ttf", 13);

		m_hwnd = hwnd;
		m_decl.begin()
			.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
			.end();

		//ImGuiIO& io = ImGui::GetIO();
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
		auto* scene =
			static_cast<Lumix::RenderScene*>(m_editor->getScene(Lumix::crc32("renderer")));

		m_sceneview.setScene(scene);
		m_gui_pipeline->setScene(scene);
		m_game_pipeline->setScene(scene);
	}


	void onUniverseDestroyed()
	{
		m_sceneview.setScene(nullptr);
		m_gui_pipeline->setScene(nullptr);
		m_game_pipeline->setScene(nullptr);
	}


	void init(HWND win)
	{
		Lumix::Renderer::setInitData(win);
		m_engine = Lumix::Engine::create(nullptr, m_allocator);
		char current_dir[MAX_PATH];
		GetCurrentDirectory(sizeof(current_dir), current_dir);
		m_editor = Lumix::WorldEditor::create(current_dir, *m_engine, m_allocator);
		m_asset_browser = new AssetBrowser(*m_editor);
		m_property_grid = new PropertyGrid(*m_editor, *m_asset_browser);
		auto engine_allocator = static_cast<Lumix::Debug::Allocator*>(&m_engine->getAllocator());
		m_profiler_ui = new ProfilerUI(engine_allocator, &m_engine->getResourceManager());
		m_log_ui = new LogUI(m_editor->getAllocator());
		m_import_asset_dialog = new ImportAssetDialog(*m_editor);
		m_shader_compiler = new ShaderCompiler(*m_editor, *m_log_ui);
		m_hierarchy_ui.setWorldEditor(*m_editor);

		m_editor->universeCreated().bind<Context, &Context::onUniverseCreated>(this);
		m_editor->universeDestroyed().bind<Context, &Context::onUniverseDestroyed>(this);

		auto* pipeline_manager =
			m_engine->getResourceManager().get(Lumix::ResourceManager::PIPELINE);

		m_gui_pipeline_source = static_cast<Lumix::Pipeline*>(
			pipeline_manager->load(Lumix::Path("pipelines/imgui.lua")));
		m_gui_pipeline = Lumix::PipelineInstance::create(*m_gui_pipeline_source,
			m_engine->getAllocator());

		m_sceneview.init(*m_editor);

		m_game_pipeline_source = static_cast<Lumix::Pipeline*>(
			pipeline_manager->load(Lumix::Path("pipelines/game_view.lua")));
		m_game_pipeline = Lumix::PipelineInstance::create(
			*m_game_pipeline_source, m_engine->getAllocator());

		RECT rect;
		GetClientRect(win, &rect);
		m_gui_pipeline->setViewport(0, 0, rect.right, rect.bottom);
		auto& plugin_manager = m_editor->getEngine().getPluginManager();
		auto* renderer = static_cast<Lumix::Renderer*>(plugin_manager.getPlugin("renderer"));
		renderer->resize(rect.right, rect.bottom);
		onUniverseCreated();
		initIMGUI(win);
	}

	void checkShortcuts()
	{
		if (ImGui::IsAnyItemActive()) return;

		if (ImGui::GetIO().KeysDown[VK_DELETE])
		{
			if (!m_editor->getSelectedEntities().empty())
			{
				m_editor->destroyEntities(
					&m_editor->getSelectedEntities()[0],
					m_editor->getSelectedEntities().size());
			}
		}
		if (ImGui::GetIO().KeysDown[VK_CONTROL])
		{
			if (ImGui::GetIO().KeysDown['W'])
			{
				m_is_wireframe = !m_is_wireframe;
				m_sceneview.setWireframe(m_is_wireframe);
			}
			if (ImGui::GetIO().KeysDown['P'])
			{
				toggleGameMode();
			}
			if (ImGui::GetIO().KeysDown['C'])
			{
				m_editor->copyEntity();
			}
			if (ImGui::GetIO().KeysDown['V'])
			{
				m_editor->pasteEntity();
			}
			if (ImGui::GetIO().KeysDown['F'])
			{
				m_editor->lookAtSelected();
			}
			if (ImGui::GetIO().KeysDown['E'])
			{
				m_editor->addEntity();
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


	LRESULT windowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		static int old_x = x;
		static int old_y = y;
		if (!m_gui_pipeline)
		{
			return DefWindowProc(hWnd, msg, wParam, lParam);
		}

		switch (msg)
		{
			case WM_CLOSE: PostQuitMessage(0); break;
			case WM_SIZE:
			{
				uint32_t width = ((int)(short)LOWORD(lParam));
				uint32_t height = ((int)(short)HIWORD(lParam));

				m_gui_pipeline->setViewport(0, 0, width, height);
				auto& plugin_manager = m_editor->getEngine().getPluginManager();
				auto* renderer =
					static_cast<Lumix::Renderer*>(plugin_manager.getPlugin("renderer"));
				renderer->resize(width, height);
			}
			break;
			case WM_MOUSEWHEEL:
				ImGui::GetIO().MouseWheel = GET_WHEEL_DELTA_WPARAM(wParam) / 600.0f;
				break;
			case WM_ERASEBKGND: return 1;
			case WM_LBUTTONUP:
				m_sceneview.onMouseUp(Lumix::MouseButton::LEFT);
				ImGui::GetIO().MouseDown[0] = false;
				break;
			case WM_LBUTTONDOWN:
				if (!m_sceneview.onMouseDown(old_x, old_y, Lumix::MouseButton::LEFT))
				{
					ImGui::GetIO().MouseDown[0] = true;
				}
				break;
			case WM_RBUTTONDOWN:
				if (!m_sceneview.onMouseDown(old_x, old_y, Lumix::MouseButton::RIGHT))
				{
					ImGui::GetIO().MouseDown[1] = true;
				}
				break;
			case WM_RBUTTONUP:
				m_sceneview.onMouseUp(Lumix::MouseButton::RIGHT);
				ImGui::GetIO().MouseDown[1] = false;
				break;
			case WM_MOUSEMOVE:
			{
				m_sceneview.onMouseMove(x, y, x - old_x, y - old_y);

				auto& input_system = m_engine->getInputSystem();
				input_system.injectMouseXMove(float(old_x - x));
				input_system.injectMouseYMove(float(old_y - y));
				old_x = x;
				old_y = y;

				ImGuiIO& io = ImGui::GetIO();
				io.MousePos.x = (float)x;
				io.MousePos.y = (float)y;
			}
			break;
			case WM_CHAR: ImGui::GetIO().AddInputCharacter((ImWchar)wParam); break;
			case WM_KEYUP: ImGui::GetIO().KeysDown[wParam] = false; break;
			case WM_SYSKEYDOWN: ImGui::GetIO().KeysDown[wParam] = true; break;
			case WM_SYSKEYUP: ImGui::GetIO().KeysDown[wParam] = false; break;
			case WM_KEYDOWN:
			{
				ImGui::GetIO().KeysDown[wParam] = true;
				checkShortcuts();
				switch (wParam)
				{
					case VK_OEM_2: // Question Mark / Forward Slash for US Keyboards
						break;
				}
				break;
			}
		}

		return DefWindowProc(hWnd, msg, wParam, lParam);
	}


	HWND m_hwnd;
	HINSTANCE m_instance;
	bgfx::VertexDecl m_decl;
	Lumix::Material* m_material;
	Lumix::Engine* m_engine;

	SceneView m_sceneview;

	Lumix::Pipeline* m_gui_pipeline_source;
	Lumix::PipelineInstance* m_gui_pipeline;

	Lumix::Pipeline* m_game_pipeline_source;
	Lumix::PipelineInstance* m_game_pipeline;
	bgfx::TextureHandle m_gameview_texture_handle;

	Lumix::DefaultAllocator m_allocator;
	Lumix::WorldEditor* m_editor;
	AssetBrowser* m_asset_browser;
	PropertyGrid* m_property_grid;
	LogUI* m_log_ui;
	ProfilerUI* m_profiler_ui;
	ImportAssetDialog* m_import_asset_dialog;
	ShaderCompiler* m_shader_compiler;
	Lumix::string m_selected_template_name;
	HierarchyUI m_hierarchy_ui;
	char m_entity_list_filter[100];

	bool m_finished;

	bool m_is_gameview_hovered;
	bool m_is_gameview_opened;
	bool m_is_entity_list_shown;
	bool m_is_entity_template_list_opened;
	bool m_is_style_editor_shown;
	bool m_is_wireframe;
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

	g_context.m_gui_pipeline->setViewProjection(ortho, (int)width, (int)height);

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

			g_context.m_gui_pipeline->setScissor(
				uint16_t(Lumix::Math::maxValue(pcmd->ClipRect.x, 0.0f)),
				uint16_t(Lumix::Math::maxValue(pcmd->ClipRect.y, 0.0f)),
				uint16_t(Lumix::Math::minValue(pcmd->ClipRect.z, 65535.0f) -
						 Lumix::Math::maxValue(pcmd->ClipRect.x, 0.0f)),
				uint16_t(Lumix::Math::minValue(pcmd->ClipRect.w, 65535.0f) -
						 Lumix::Math::maxValue(pcmd->ClipRect.y, 0.0f)));

			g_context.m_gui_pipeline->render(
				geom, elem_offset, pcmd->ElemCount, *g_context.m_material, (bgfx::TextureHandle*)pcmd->TextureId);

			elem_offset += pcmd->ElemCount;
		}
	}
}


LRESULT WINAPI msgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return g_context.windowProc(hWnd, msg, wParam, lParam);
}


INT WINAPI WinMain(HINSTANCE hInst,
				   HINSTANCE ignoreMe0,
				   LPSTR ignoreMe1,
				   INT ignoreMe2)
{
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
	ASSERT(hwnd);
	SetWindowTextA(hwnd, "Lumix Studio");
	ShowWindow(hwnd, SW_MAXIMIZE);
	g_context.m_instance = hInst;
	g_context.init(hwnd);
	timeBeginPeriod(1);

	while (g_context.m_engine->getResourceManager().isLoading())
	{
		g_context.m_engine->update(*g_context.m_editor->getUniverseContext());
	}

	
	Lumix::Timer* timer = Lumix::Timer::create(g_context.m_allocator);
	while (!g_context.m_finished)
	{
		float frame_time;
		{
			PROFILE_BLOCK("tick");
			MSG msg = {0};
			while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);

				if (msg.message == WM_QUIT)
				{
					g_context.m_finished = true;
				}
			}

			g_context.update();
			frame_time = timer->tick();
		}
		
		if (frame_time < 0.016f)
		{
			PROFILE_BLOCK("sleep");
			Lumix::MT::sleep(uint32_t(16 - frame_time * 1000));
		}
		timer->tick();
		Lumix::g_profiler.frame();
	}

	Lumix::Timer::destroy(timer);
	g_context.shutdown();
	UnregisterClassA("lmxa", wnd.hInstance);

	return 0;
}