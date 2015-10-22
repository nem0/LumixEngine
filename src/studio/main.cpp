#include "asset_browser.h"
#include "core/blob.h"
#include "core/command_line_parser.h"
#include "core/crc32.h"
#include "core/default_allocator.h"
#include "core/fs/file_system.h"
#include "core/input_system.h"
#include "core/log.h"
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
#include "game_view.h"
#include "hierarchy_ui.h"
#include "import_asset_dialog.h"
#include "log_ui.h"
#include "metadata.h"
#include "ocornut-imgui/imgui.h"
#include "profiler_ui.h"
#include "property_grid.h"
#include "renderer/frame_buffer.h"
#include "renderer/material.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include "renderer/transient_geometry.h"
#include "scene_view.h"
#include "settings.h"
#include "shader_compiler.h"
#include "universe/hierarchy.h"
#include "utils.h"

#include <bgfx/bgfx.h>
#include <cstdio>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <mmsystem.h>

// http://prideout.net/blog/?p=36


void imGuiCallback(ImDrawData* draw_data);
LRESULT WINAPI msgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


class StudioApp
{
public:
	StudioApp()
		: m_is_entity_list_opened(true)
		, m_finished(false)
		, m_is_style_editor_opened(false)
		, m_import_asset_dialog(nullptr)
		, m_shader_compiler(nullptr)
		, m_is_wireframe(false)
		, m_is_entity_template_list_opened(false)
		, m_selected_template_name(m_allocator)
		, m_profiler_ui(nullptr)
		, m_asset_browser(nullptr)
		, m_property_grid(nullptr)
		, m_actions(m_allocator)
		, m_metadata(m_allocator)
		, m_gui_pipeline(nullptr)
		, m_is_welcome_screen_opened(true)
		, m_is_mouse_tracked(false)
	{
		m_entity_list_search[0] = '\0';
		m_template_name[0] = '\0';
	}


	void autosave()
	{
		m_time_to_autosave = float(m_settings.m_autosave_time);
		if (!m_editor->getUniversePath().isValid()) return;

		char filename[Lumix::MAX_PATH_LENGTH];
		Lumix::copyString(filename, m_editor->getUniversePath().c_str());
		Lumix::catString(filename, "_autosave.unv");

		m_editor->saveUniverse(Lumix::Path(filename), false);
	}


	void update()
	{
		PROFILE_FUNCTION();
		float time_delta = m_editor->getEngine().getLastTimeDelta();

		m_time_to_autosave -= time_delta;
		if (m_time_to_autosave < 0) autosave();

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


	void showWelcomeScreen()
	{
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
								 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
		RECT client_rect;
		GetClientRect(m_hwnd, &client_rect);
		ImVec2 size(float(client_rect.right - client_rect.left),
			float(client_rect.bottom - client_rect.top));
		if (ImGui::Begin("Welcome", nullptr, size, -1, flags))
		{
			ImGui::Text("Welcome to Lumix Studio");

			ImVec2 half_size = ImGui::GetContentRegionAvail();
			half_size.x = half_size.x * 0.5f - ImGui::GetStyle().FramePadding.x;
			half_size.y *= 0.75f;
			auto right_pos = ImGui::GetCursorPos();
			right_pos.x += half_size.x + ImGui::GetStyle().FramePadding.x;
			if (ImGui::BeginChild("left", half_size, true))
			{
				if (ImGui::Button("New Universe")) m_is_welcome_screen_opened = false;

				ImGui::Separator();
				ImGui::Text("Open universe:");
				ImGui::Indent();
				auto& universes = m_asset_browser->getResources(AssetBrowser::UNIVERSE);
				for (auto& univ : universes)
				{
					if (ImGui::MenuItem(univ.c_str()))
					{
						m_editor->loadUniverse(univ);
						setTitle(univ.c_str());
						m_is_welcome_screen_opened = false;
					}
				}
				ImGui::Unindent();
			}
			ImGui::EndChild();

			ImGui::SetCursorPos(right_pos);

			if (ImGui::BeginChild("right", half_size, true))
			{
				ImGui::Text("Version 0.17. - News");
				ImGui::BulletText("Painting entities - align with normal");
				ImGui::BulletText("Painting entities - random x and z rotation");
				ImGui::BulletText("Lua properties with types");
				ImGui::BulletText("Moving the Light Texel-Sized Increments");
				ImGui::BulletText("Terrain brush for removing entities");
				ImGui::BulletText("Improved shadows on terrain");
				ImGui::BulletText("Fog height");
				ImGui::BulletText("Bitmap to heightmap convertor");
				ImGui::BulletText("LOD preview");
				ImGui::BulletText("New gizmo");
				ImGui::BulletText("Orbit camera");
				ImGui::BulletText("Welcome screen");
				ImGui::BulletText("Visualization of physical contorller");
				ImGui::BulletText("Game view fixed");
				ImGui::Separator();
				if (ImGui::Button("Download new version"))
				{
					Lumix::shellExecuteOpen(
						"https://github.com/nem0/lumixengine_data/archive/master.zip");
				}

				if (ImGui::Button("Show major releases"))
				{
					Lumix::shellExecuteOpen("https://github.com/nem0/LumixEngine/releases");
				}

				if (ImGui::Button("Show latest commits"))
				{
					Lumix::shellExecuteOpen("https://github.com/nem0/LumixEngine/commits/master");
				}

				if (ImGui::Button("Show issues"))
				{
					Lumix::shellExecuteOpen("https://github.com/nem0/lumixengine/issues");
				}

			}
			ImGui::EndChild();

			if (ImGui::Button("Close")) m_is_welcome_screen_opened = false;
		}
		ImGui::End();
	}


	void onGUI()
	{
		PROFILE_FUNCTION();

		if (!m_gui_pipeline_source->isReady()) return;

		ImGuiIO& io = ImGui::GetIO();
		RECT rect;
		GetClientRect(m_hwnd, &rect);
		io.DisplaySize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));
		io.DeltaTime = m_engine->getLastTimeDelta();
		io.KeyCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		io.KeyShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		io.KeyAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
		io.KeysDown[VK_MENU] = io.KeyAlt;
		io.KeysDown[VK_SHIFT] = io.KeyShift;
		io.KeysDown[VK_CONTROL] = io.KeyCtrl;

		SetCursor(io.MouseDrawCursor ? NULL : LoadCursor(NULL, IDC_ARROW));

		ImGui::NewFrame();

		if (m_is_welcome_screen_opened)
		{
			showWelcomeScreen();
		}
		else
		{
			showMainMenu();
			m_profiler_ui->onGUI();
			m_asset_browser->onGUI();
			m_log_ui->onGUI();
			m_import_asset_dialog->onGUI();
			m_property_grid->onGUI();
			showEntityList();
			showEntityTemplateList();
			m_sceneview.onGUI();
			m_hierarchy_ui.onGUI();
			m_gameview.onGui();
			if (m_is_style_editor_opened) ImGui::ShowStyleEditor();
			m_settings.onGUI(&m_actions[0], m_actions.size());
		}

		ImGui::Render();
	}


	void setTitle(const char* title)
	{
		char tmp[100];
		Lumix::copyString(tmp, "Lumix Studio - ");
		Lumix::catString(tmp, title);
		SetWindowTextA(m_hwnd, tmp);
	}


	static void getShortcut(const Action& action, char* buf, int max_size)
	{
		buf[0] = 0;
		for (int i = 0; i < Lumix::lengthOf(action.shortcut); ++i)
		{
			char str[30];
			getKeyName(action.shortcut[i], str, Lumix::lengthOf(str));
			if (str[0] == 0) return;
			if (i > 0) Lumix::catString(buf, max_size, " - ");
			Lumix::catString(buf, max_size, str);
		}
	}


	void doMenuItem(Action& a, bool selected, bool enabled)
	{
		char buf[20];
		getShortcut(a, buf, sizeof(buf));
		if (ImGui::MenuItem(a.label, buf, selected, enabled))
		{
			a.func.invoke();
		}
	}


	void save()
	{
		m_time_to_autosave = float(m_settings.m_autosave_time);
		if (m_editor->getUniversePath().isValid())
		{
			m_editor->saveUniverse(m_editor->getUniversePath(), true);
		}
		else
		{
			char filename[Lumix::MAX_PATH_LENGTH];
			if (Lumix::getSaveFilename(
				filename, sizeof(filename), "Universes\0*.unv\0", "unv"))
			{
				m_editor->saveUniverse(Lumix::Path(filename), true);
				setTitle(filename);
			}
		}

	}


	void saveAs()
	{
		m_time_to_autosave = float(m_settings.m_autosave_time);
		char filename[Lumix::MAX_PATH_LENGTH];
		if (Lumix::getSaveFilename(filename, sizeof(filename), "Universes\0*.unv\0", "unv"))
		{
			m_editor->saveUniverse(Lumix::Path(filename), true);
		}
	}


	void exit() { PostQuitMessage(0); }

	void newUniverse()
	{
		m_editor->newUniverse();
		m_time_to_autosave = float(m_settings.m_autosave_time);
	}

	void undo() { m_editor->undo(); }
	void redo() { m_editor->redo(); }
	void copy() { m_editor->copyEntity(); }
	void paste() { m_editor->pasteEntity(); }
	void toggleOrbitCamera() { m_editor->setOrbitCamera(!m_editor->isOrbitCamera()); }
	void togglePivotMode() { m_editor->getGizmo().togglePivot(); }
	void toggleCoordSystem() { m_editor->getGizmo().toggleCoordSystem(); }
	void createEntity() { m_editor->addEntity(); }
	void showEntities() { m_editor->showEntities(); }
	void hideEntities() { m_editor->hideEntities(); }
	void toggleMeasure() { m_editor->toggleMeasure(); }
	void snapDown() { m_editor->snapDown(); }
	void lookAtSelected() { m_editor->lookAtSelected(); }
	void toggleStats() { m_gui_pipeline->toggleStats(); }

	void autosnapDown() 
	{
		auto& gizmo = m_editor->getGizmo();
		gizmo.setAutosnapDown(!gizmo.isAutosnapDown());
	}

	void toggleGizmoMode() 
	{
		auto& gizmo = m_editor->getGizmo();
		if (gizmo.getMode() == Lumix::Gizmo::Mode::TRANSLATE)
		{
			gizmo.setMode(Lumix::Gizmo::Mode::ROTATE);
		}
		else
		{
			gizmo.setMode(Lumix::Gizmo::Mode::TRANSLATE);
		}
	}

	
	void setWireframe()
	{
		m_is_wireframe = !m_is_wireframe;
		m_sceneview.setWireframe(m_is_wireframe);
	}


	void destroyEntity()
	{
		auto& selected_entities = m_editor->getSelectedEntities();
		if (selected_entities.empty()) return;
		m_editor->destroyEntities(&selected_entities[0], selected_entities.size());
	}


	void loadAndExecuteCommands()
	{
		char filename[Lumix::MAX_PATH_LENGTH];
		if (Lumix::getOpenFilename(filename, Lumix::lengthOf(filename), "JSON files\0*.json\0"))
		{
			m_editor->executeUndoStack(Lumix::Path(filename));
		}
	}


	void saveUndoStack()
	{
		char filename[Lumix::MAX_PATH_LENGTH];
		if (Lumix::getSaveFilename(filename, Lumix::lengthOf(filename), "JSON files\0*.json\0", "json"))
		{
			m_editor->saveUndoStack(Lumix::Path(filename));
		}
	}


	template <void (StudioApp::*func)()>
	void addAction(const char* label, const char* name)
	{
		auto* a = LUMIX_NEW(m_editor->getAllocator(), Action)(label, name);
		a->func.bind<StudioApp, func>(this);
		m_actions.push(a);
	}


	template <void (StudioApp::*func)()>
	void addAction(const char* label, const char* name, int shortcut0, int shortcut1, int shortcut2)
	{
		auto* a = LUMIX_NEW(m_editor->getAllocator(), Action)(
			label, name, shortcut0, shortcut1, shortcut2);
		a->func.bind<StudioApp, func>(this);
		m_actions.push(a);
	}



	Action& getAction(const char* name)
	{
		for (auto* a : m_actions)
		{
			if (strcmp(a->name, name) == 0) return *a;
		}
		ASSERT(false);
		return *m_actions[0];
	}

	
	void showMainMenu()
	{
		bool is_any_entity_selected = !m_editor->getSelectedEntities().empty();
		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				doMenuItem(getAction("newUniverse"), false, true);
				if (ImGui::BeginMenu("Open"))
				{
					auto& universes = m_asset_browser->getResources(AssetBrowser::UNIVERSE);
					for (auto& univ : universes)
					{
						if (ImGui::MenuItem(univ.c_str()))
						{
							m_time_to_autosave = float(m_settings.m_autosave_time);
							m_editor->loadUniverse(univ);
							setTitle(univ.c_str());
						}
					}
					ImGui::EndMenu();
				}
				doMenuItem(getAction("save"), false, true);
				doMenuItem(getAction("saveAs"), false, true);
				doMenuItem(getAction("exit"), false, true);

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Edit"))
			{
				doMenuItem(getAction("undo"), false, m_editor->canUndo());
				doMenuItem(getAction("redo"), false, m_editor->canRedo());
				ImGui::Separator();
				doMenuItem(getAction("copy"), false, is_any_entity_selected);
				doMenuItem(getAction("paste"), false, m_editor->canPasteEntity());
				ImGui::Separator();
				doMenuItem(getAction("orbitCamera"),
					m_editor->isOrbitCamera(),
					is_any_entity_selected || m_editor->isOrbitCamera());
				doMenuItem(getAction("toggleGizmoMode"), false, is_any_entity_selected);
				doMenuItem(getAction("togglePivotMode"), false, is_any_entity_selected);
				doMenuItem(getAction("toggleCoordSystem"), false, is_any_entity_selected);
				if (ImGui::BeginMenu("Select"))
				{
					if (ImGui::MenuItem("Same mesh", nullptr, nullptr, is_any_entity_selected))
						m_editor->selectEntitiesWithSameMesh();
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Entity"))
			{
				doMenuItem(getAction("createEntity"), false, true);
				doMenuItem(getAction("destroyEntity"), false, is_any_entity_selected);

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
				
				doMenuItem(getAction("showEntities"), false, is_any_entity_selected);
				doMenuItem(getAction("hideEntities"), false, is_any_entity_selected);
				ImGui::EndMenu();
			}


			if (ImGui::BeginMenu("Tools"))
			{
				doMenuItem(getAction("toggleGameMode"), m_editor->isGameMode(), true);
				doMenuItem(getAction("toggleMeasure"), m_editor->isMeasureToolActive(), true);
				doMenuItem(getAction("snapDown"), false, is_any_entity_selected);
				doMenuItem(getAction("autosnapDown"), m_editor->getGizmo().isAutosnapDown(), true);
				if (ImGui::MenuItem("Save commands")) saveUndoStack();
				if (ImGui::MenuItem("Load commands")) loadAndExecuteCommands();

				ImGui::MenuItem("Import asset", nullptr, &m_import_asset_dialog->m_is_opened);
				ImGui::EndMenu();
			}


			if (ImGui::BeginMenu("View"))
			{
				doMenuItem(getAction("lookAtSelected"), false, is_any_entity_selected);
				doMenuItem(getAction("setWireframe"), m_is_wireframe, true);
				doMenuItem(getAction("toggleStats"), false, true);
				if (ImGui::BeginMenu("Windows"))
				{
					ImGui::MenuItem("Asset browser", nullptr, &m_asset_browser->m_is_opened);
					ImGui::MenuItem("Entity list", nullptr, &m_is_entity_list_opened);
					ImGui::MenuItem("Entity templates", nullptr, &m_is_entity_template_list_opened);
					ImGui::MenuItem("Game view", nullptr, &m_gameview.m_is_opened);
					ImGui::MenuItem("Hierarchy", nullptr, &m_hierarchy_ui.m_is_opened);
					ImGui::MenuItem("Log", nullptr, &m_log_ui->m_is_opened);
					ImGui::MenuItem("Profiler", nullptr, &m_profiler_ui->m_is_opened);
					ImGui::MenuItem("Properties", nullptr, &m_property_grid->m_is_opened);
					ImGui::MenuItem("Settings", nullptr, &m_settings.m_is_opened);
					ImGui::MenuItem("Style editor", nullptr, &m_is_style_editor_opened);
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


	void toggleGameMode() { m_editor->toggleGameMode(); }


	void showEntityTemplateList()
	{
		if (!m_is_entity_template_list_opened) return;

		if (ImGui::Begin("Entity templates", &m_is_entity_template_list_opened))
		{
			if (m_editor->getSelectedEntities().size() == 1)
			{
				ImGui::InputText("Template name", m_template_name, Lumix::lengthOf(m_template_name));

				if (ImGui::Button("Create from selected"))
				{
					auto entity = m_editor->getSelectedEntities()[0];
					auto& system = m_editor->getEntityTemplateSystem();
					system.createTemplateFromEntity(m_template_name, entity);
				}
				ImGui::Separator();
			}
			ImGui::Text("Templates:");
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
		if (!m_is_entity_list_opened) return;

		if (ImGui::Begin("Entity list", &m_is_entity_list_opened))
		{
			if (ImGui::Button("Create entity"))
			{
				m_editor->addEntity();
			}
			auto* universe = m_editor->getUniverse();
			int scroll_to = -1;

			if (ImGui::InputText("Search", m_entity_list_search, sizeof(m_entity_list_search)))
			{
				for (int i = 0, c = universe->getEntityCount(); i < c; ++i)
				{
					static char buf[1024];
					auto entity = universe->getEntityFromDenseIdx(i);
					getEntityListDisplayName(*m_editor, buf, sizeof(buf), entity);
					if (Lumix::stristr(buf, m_entity_list_search) != nullptr)
					{
						scroll_to = i;
						break;
					}
				}
			}
			ImGui::Separator();

			struct ListBoxData
			{
				Lumix::WorldEditor* m_editor;
				Lumix::Universe* universe;
				char buffer[1024];
				static bool itemsGetter(void* data, int idx, const char** txt)
				{
					auto* d = static_cast<ListBoxData*>(data);
					auto entity = d->universe->getEntityFromDenseIdx(idx);
					getEntityListDisplayName(*d->m_editor, d->buffer, sizeof(d->buffer), entity);
					*txt = d->buffer;
					return true;
				}
			};
			ListBoxData data;
			data.universe = universe;
			data.m_editor = m_editor;
			static int current_item;
			if (ImGui::ListBox("Entities",
					&current_item,
					scroll_to,
					&ListBoxData::itemsGetter,
					&data,
					universe->getEntityCount(),
					15))
			{
				auto e = universe->getEntityFromDenseIdx(current_item);
				m_editor->selectEntities(&e, 1);
			};
		}
		ImGui::End();
	}


	void saveSettings()
	{
		m_settings.m_is_asset_browser_opened = m_asset_browser->m_is_opened;
		m_settings.m_is_entity_list_opened = m_is_entity_list_opened;
		m_settings.m_is_entity_template_list_opened = m_is_entity_template_list_opened;
		m_settings.m_is_gameview_opened = m_gameview.m_is_opened;
		m_settings.m_is_hierarchy_opened = m_hierarchy_ui.m_is_opened;
		m_settings.m_is_log_opened = m_log_ui->m_is_opened;
		m_settings.m_is_profiler_opened = m_profiler_ui->m_is_opened;
		m_settings.m_is_properties_opened = m_property_grid->m_is_opened;
		m_settings.m_is_style_editor_opened = m_is_style_editor_opened;

		m_settings.save(&m_actions[0], m_actions.size());

		if (!m_metadata.save())
		{
			Lumix::g_log_warning.log("studio") << "Could not save metadata";
		}
	}


	void shutdown()
	{
		saveSettings();

		for (auto* a : m_actions)
		{
			m_editor->getAllocator().deleteObject(a);
		}
		m_actions.clear();

		shutdownImGui();

		delete m_profiler_ui;
		delete m_asset_browser;
		delete m_log_ui;
		delete m_property_grid;
		delete m_import_asset_dialog;
		delete m_shader_compiler;
		Lumix::WorldEditor::destroy(m_editor, m_allocator);
		m_sceneview.shutdown();
		m_gameview.shutdown();
		Lumix::PipelineInstance::destroy(m_gui_pipeline);
		m_gui_pipeline_source->getResourceManager()
			.get(Lumix::ResourceManager::PIPELINE)
			->unload(*m_gui_pipeline_source);
		Lumix::Engine::destroy(m_engine, m_allocator);
		m_engine = nullptr;
		m_gui_pipeline = nullptr;
		m_gui_pipeline_source = nullptr;
		m_editor = nullptr;

		UnregisterClassA("lmxa", m_instance);
	}


	void shutdownImGui()
	{
		ImGui::Shutdown();

		Lumix::Texture* texture = m_material->getTexture(0);
		m_material->setTexture(0, nullptr);
		texture->destroy();
		m_allocator.deleteObject(texture);

		m_material->getResourceManager().get(Lumix::ResourceManager::MATERIAL)->unload(*m_material);
	}


	void trackMouse()
	{
		TRACKMOUSEEVENT track_event;
		track_event.cbSize = sizeof(TRACKMOUSEEVENT);
		track_event.dwFlags = TME_LEAVE;
		track_event.hwndTrack = m_hwnd;
		m_is_mouse_tracked = TrackMouseEvent(&track_event) == TRUE;
	}


	void initIMGUI()
	{
		trackMouse();

		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->AddFontFromFileTTF("editor/VeraMono.ttf", 13);

		m_decl.begin()
			.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
			.end();

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
		io.ImeWindowHandle = m_hwnd;

		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		auto* material_manager =
			m_engine->getResourceManager().get(Lumix::ResourceManager::MATERIAL);
		auto* resource = material_manager->load(Lumix::Path("models/imgui.mat"));
		m_material = static_cast<Lumix::Material*>(resource);

		Lumix::Texture* texture = LUMIX_NEW(m_allocator, Lumix::Texture)(
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
		m_gameview.setScene(scene);
	}


	void onUniverseDestroyed()
	{
		m_sceneview.setScene(nullptr);
		m_gui_pipeline->setScene(nullptr);
		m_gameview.setScene(nullptr);
	}


	void loadSettings()
	{
		m_settings.load(&m_actions[0], m_actions.size());

		m_asset_browser->m_is_opened = m_settings.m_is_asset_browser_opened;
		m_is_entity_list_opened = m_settings.m_is_entity_list_opened;
		m_is_entity_template_list_opened = m_settings.m_is_entity_template_list_opened;
		m_gameview.m_is_opened = m_settings.m_is_gameview_opened;
		m_hierarchy_ui.m_is_opened = m_settings.m_is_hierarchy_opened;
		m_log_ui->m_is_opened = m_settings.m_is_log_opened;
		m_profiler_ui->m_is_opened = m_settings.m_is_profiler_opened;
		m_property_grid->m_is_opened = m_settings.m_is_properties_opened;
		m_is_style_editor_opened = m_settings.m_is_style_editor_opened;

		if (m_settings.m_is_maximized)
		{
			ShowWindow(m_hwnd, SW_MAXIMIZE);
		}
		else if (m_settings.m_window.w > 0)
		{
			MoveWindow(m_hwnd,
				m_settings.m_window.x,
				m_settings.m_window.y,
				m_settings.m_window.w,
				m_settings.m_window.h,
				FALSE);
		}
	}


	void addActions()
	{
		addAction<&StudioApp::newUniverse>("New", "newUniverse");
		addAction<&StudioApp::save>("Save", "save", VK_CONTROL, 'S', -1);
		addAction<&StudioApp::saveAs>("Save As", "saveAs", VK_CONTROL, VK_SHIFT, 'S');
		addAction<&StudioApp::exit>("Exit", "exit", VK_CONTROL, 'X', -1);

		addAction<&StudioApp::redo>("Redo", "redo", VK_CONTROL, VK_SHIFT, 'Z');
		addAction<&StudioApp::undo>("Undo", "undo", VK_CONTROL, 'Z', -1);
		addAction<&StudioApp::copy>("Copy", "copy", VK_CONTROL, 'C', -1);
		addAction<&StudioApp::paste>("Paste", "paste", VK_CONTROL, 'V', -1);
		addAction<&StudioApp::toggleOrbitCamera>("Orbit camera", "orbitCamera");
		addAction<&StudioApp::toggleGizmoMode>("Translate/Rotate", "toggleGizmoMode");
		addAction<&StudioApp::togglePivotMode>("Center/Pivot", "togglePivotMode");
		addAction<&StudioApp::toggleCoordSystem>("Local/Global", "toggleCoordSystem");

		addAction<&StudioApp::createEntity>("Create", "createEntity");
		addAction<&StudioApp::destroyEntity>("Destroy", "destroyEntity", VK_DELETE, -1, -1);
		addAction<&StudioApp::showEntities>("Show", "showEntities");
		addAction<&StudioApp::hideEntities>("Hide", "hideEntities");

		addAction<&StudioApp::toggleGameMode>("Game Mode", "toggleGameMode");
		addAction<&StudioApp::toggleMeasure>("Toggle measure", "toggleMeasure");
		addAction<&StudioApp::autosnapDown>("Autosnap down", "autosnapDown");
		addAction<&StudioApp::snapDown>("Snap down", "snapDown");
		addAction<&StudioApp::lookAtSelected>("Look at selected", "lookAtSelected");

		addAction<&StudioApp::setWireframe>("Wireframe", "setWireframe");
		addAction<&StudioApp::toggleStats>("Stats", "toggleStats"); 
	}


	void loadUserPlugins()
	{
		char cmd_line[2048];
		Lumix::getCommandLine(cmd_line, Lumix::lengthOf(cmd_line));

		Lumix::CommandLineParser parser(cmd_line);
		auto& plugin_manager = m_editor->getEngine().getPluginManager();
		while (parser.next())
		{
			if (!parser.currentEquals("-plugin")) continue;
			if (!parser.next()) break;
	
			char tmp[Lumix::MAX_PATH_LENGTH];
			parser.getCurrent(tmp, Lumix::lengthOf(tmp));
			bool loaded = plugin_manager.load(tmp) != nullptr;
			if (!loaded)
			{
				Lumix::g_log_error.log("init") << "Could not load plugin " << tmp
											   << " requested by command line";
			}
		}
	}


	void processSystemEvents()
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


	void run()
	{
		Lumix::Timer* timer = Lumix::Timer::create(m_allocator);
		while (!m_finished)
		{
			{
				timer->tick();
				PROFILE_BLOCK("all");
				float frame_time;
				{
					PROFILE_BLOCK("tick");
					processSystemEvents();
					update();
					frame_time = timer->tick();
				}

				if (frame_time < 1 / 60.0f)
				{
					PROFILE_BLOCK("sleep");
					Lumix::MT::sleep(uint32_t(1000 / 60.0f - frame_time * 1000));
				}
			}
			Lumix::g_profiler.frame();
			Lumix::g_profiler.checkRecording();
		}
		Lumix::Timer::destroy(timer);
	}


	void checkWorkingDirector()
	{
		if (!Lumix::dirExists("shaders"))
		{
			Lumix::messageBox("Shaders directory not found, please check working directory.");
		}
		else if (!Lumix::dirExists("bin"))
		{
			Lumix::messageBox("Bin directory not found, please check working directory.");
		}
		else if (!Lumix::dirExists("pipelines"))
		{
			Lumix::messageBox("Pipelines directory not found, please check working directory.");
		}
	}


	void init(HINSTANCE hInst)
	{
		checkWorkingDirector();

		m_instance = hInst;

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
		m_hwnd = CreateWindowA(
			"lmxa", "lmxa", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 800, 600, NULL, NULL, hInst, 0);
		ASSERT(m_hwnd);
		SetWindowTextA(m_hwnd, "Lumix Studio");

		Lumix::Renderer::setInitData(m_hwnd);
		m_engine = Lumix::Engine::create(nullptr, m_allocator);
		char current_dir[MAX_PATH];
		GetCurrentDirectory(sizeof(current_dir), current_dir);
		m_editor = Lumix::WorldEditor::create(current_dir, *m_engine, m_allocator);
		loadUserPlugins();

		addActions();

		m_asset_browser = new AssetBrowser(*m_editor, m_metadata);
		m_property_grid = new PropertyGrid(*m_editor, *m_asset_browser, m_actions);
		auto engine_allocator = static_cast<Lumix::Debug::Allocator*>(&m_engine->getAllocator());
		m_profiler_ui = new ProfilerUI(engine_allocator, &m_engine->getResourceManager());
		m_log_ui = new LogUI(m_editor->getAllocator());
		m_import_asset_dialog = new ImportAssetDialog(*m_editor, m_metadata);
		m_shader_compiler = new ShaderCompiler(*m_editor, *m_log_ui);
		m_hierarchy_ui.setWorldEditor(*m_editor);

		m_editor->universeCreated().bind<StudioApp, &StudioApp::onUniverseCreated>(this);
		m_editor->universeDestroyed().bind<StudioApp, &StudioApp::onUniverseDestroyed>(this);

		auto* pipeline_manager =
			m_engine->getResourceManager().get(Lumix::ResourceManager::PIPELINE);

		m_gui_pipeline_source = static_cast<Lumix::Pipeline*>(
			pipeline_manager->load(Lumix::Path("pipelines/imgui.lua")));
		m_gui_pipeline =
			Lumix::PipelineInstance::create(*m_gui_pipeline_source, m_engine->getAllocator());

		m_sceneview.init(*m_editor, m_actions);
		m_gameview.init(m_hwnd, *m_editor);

		RECT rect;
		GetClientRect(m_hwnd, &rect);
		m_gui_pipeline->setViewport(0, 0, rect.right, rect.bottom);
		auto& plugin_manager = m_editor->getEngine().getPluginManager();
		auto* renderer = static_cast<Lumix::Renderer*>(plugin_manager.getPlugin("renderer"));
		renderer->resize(rect.right, rect.bottom);
		onUniverseCreated();
		initIMGUI();

		loadSettings();

		if (!m_metadata.load()) Lumix::g_log_info.log("studio") << "Could not load metadata";
		timeBeginPeriod(1);

		RAWINPUTDEVICE Rid;
		Rid.usUsagePage = 0x01;
		Rid.usUsage = 0x02;
		Rid.dwFlags = 0;   
		Rid.hwndTarget = 0;
		RegisterRawInputDevices(&Rid, 1, sizeof(Rid));
	}

	void checkShortcuts()
	{
		if (ImGui::IsAnyItemActive()) return;

		bool* keysDown = ImGui::GetIO().KeysDown;
		for (auto* a : m_actions)
		{
			if (!a->is_global || a->shortcut[0] == -1) continue;

			for (int i = 0; i < Lumix::lengthOf(a->shortcut) + 1; ++i)
			{
				if (a->shortcut[i] == -1 || i == Lumix::lengthOf(a->shortcut))
				{
					a->func.invoke();
					return;
				}

				if (!keysDown[a->shortcut[i]]) break;
			}
		}
	}


	void onWindowTransformed()
	{
		RECT rect;
		GetWindowRect(m_hwnd, &rect);
		m_settings.m_window.x = rect.left;
		m_settings.m_window.y = rect.top;
		m_settings.m_window.w = rect.right - rect.left;
		m_settings.m_window.h = rect.bottom - rect.top;
		
		WINDOWPLACEMENT wndpl;
		wndpl.length = sizeof(wndpl);
		if (GetWindowPlacement(m_hwnd, &wndpl))
		{
			m_settings.m_is_maximized = wndpl.showCmd == SW_MAXIMIZE;
		}
	}


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
			m_editor->getEngine().getInputSystem().injectMouseXMove(float(raw->data.mouse.lLastX));
			m_editor->getEngine().getInputSystem().injectMouseYMove(float(raw->data.mouse.lLastY));
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
			case WM_INPUT: handleRawInput(lParam); break;
			case WM_CLOSE: PostQuitMessage(0); break;
			case WM_MOVE: onWindowTransformed(); break;
			case WM_SIZE:
			{
				onWindowTransformed();

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
				if (!m_sceneview.onMouseDown(old_x, old_y, Lumix::MouseButton::LEFT) &&
					!m_gameview.isMouseCaptured())
				{
					ImGui::GetIO().MouseDown[0] = true;
				}
				break;
			case WM_RBUTTONDOWN:
				if (!m_sceneview.onMouseDown(old_x, old_y, Lumix::MouseButton::RIGHT) &&
					!m_gameview.isMouseCaptured())
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
				if (!m_is_mouse_tracked)
				{
					trackMouse();
				}

				if (!m_gameview.isMouseCaptured())
				{
					POINT p;
					p.x = x;
					p.y = y;
					ClientToScreen(m_hwnd, &p);

					m_sceneview.onMouseMove(old_x, old_y, x - old_x, y - old_y);

					old_x = x;
					old_y = y;

					ImGuiIO& io = ImGui::GetIO();
					io.MousePos.x = (float)x;
					io.MousePos.y = (float)y;
				}
			}
			break;
			case WM_MOUSELEAVE:
				clearInputs();
				break;
			case WM_CHAR: ImGui::GetIO().AddInputCharacter((ImWchar)wParam); break;
			case WM_KEYUP: ImGui::GetIO().KeysDown[wParam] = false; break;
			case WM_SYSKEYDOWN: ImGui::GetIO().KeysDown[wParam] = true; break;
			case WM_SYSKEYUP: ImGui::GetIO().KeysDown[wParam] = false; break;
			case WM_KEYDOWN:
			{
				ImGui::GetIO().KeysDown[wParam] = true;
				checkShortcuts();
				break;
			}
		}

		return DefWindowProc(hWnd, msg, wParam, lParam);
	}


	void clearInputs()
	{
		m_is_mouse_tracked = false;
		auto& io = ImGui::GetIO();
		io.KeyAlt = false;
		io.KeyCtrl = false;
		io.KeyShift = false;
		memset(io.KeysDown, 0, sizeof(io.KeysDown));
		memset(io.MouseDown, 0, sizeof(io.MouseDown));
	}


	Lumix::DefaultAllocator m_allocator;
	HWND m_hwnd;
	HINSTANCE m_instance;
	bgfx::VertexDecl m_decl;
	Lumix::Material* m_material;
	Lumix::Engine* m_engine;

	SceneView m_sceneview;
	GameView m_gameview;

	Lumix::Pipeline* m_gui_pipeline_source;
	Lumix::PipelineInstance* m_gui_pipeline;

	float m_time_to_autosave;
	Lumix::Array<Action*> m_actions;
	Lumix::WorldEditor* m_editor;
	AssetBrowser* m_asset_browser;
	PropertyGrid* m_property_grid;
	LogUI* m_log_ui;
	ProfilerUI* m_profiler_ui;
	ImportAssetDialog* m_import_asset_dialog;
	ShaderCompiler* m_shader_compiler;
	Lumix::string m_selected_template_name;
	HierarchyUI m_hierarchy_ui;
	Settings m_settings;
	Metadata m_metadata;
	char m_entity_list_search[100];
	char m_template_name[100];

	bool m_finished;

	bool m_is_welcome_screen_opened;
	bool m_is_entity_list_opened;
	bool m_is_entity_template_list_opened;
	bool m_is_style_editor_opened;
	bool m_is_wireframe;
	bool m_is_mouse_tracked;
};


StudioApp* g_app;


static void imGuiCallback(ImDrawData* draw_data)
{
	PROFILE_FUNCTION();
	if (!g_app->m_material || !g_app->m_material->isReady()) return;
	if (!g_app->m_material->getTexture(0)) return;

	const float width = ImGui::GetIO().DisplaySize.x;
	const float height = ImGui::GetIO().DisplaySize.y;

	Lumix::Matrix ortho;
	ortho.setOrtho(0.0f, width, 0.0f, height, -1.0f, 1.0f);

	g_app->m_gui_pipeline->setViewProjection(ortho, (int)width, (int)height);

	for (int32_t ii = 0; ii < draw_data->CmdListsCount; ++ii)
	{
		ImDrawList* cmd_list = draw_data->CmdLists[ii];

		Lumix::TransientGeometry geom(&cmd_list->VtxBuffer[0],
			cmd_list->VtxBuffer.size(),
			g_app->m_decl,
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

			g_app->m_gui_pipeline->setScissor(
				uint16_t(Lumix::Math::maxValue(pcmd->ClipRect.x, 0.0f)),
				uint16_t(Lumix::Math::maxValue(pcmd->ClipRect.y, 0.0f)),
				uint16_t(Lumix::Math::minValue(pcmd->ClipRect.z, 65535.0f) -
						 Lumix::Math::maxValue(pcmd->ClipRect.x, 0.0f)),
				uint16_t(Lumix::Math::minValue(pcmd->ClipRect.w, 65535.0f) -
						 Lumix::Math::maxValue(pcmd->ClipRect.y, 0.0f)));

			g_app->m_gui_pipeline->render(geom,
				elem_offset,
				pcmd->ElemCount,
				*g_app->m_material,
				pcmd->TextureId ? (bgfx::TextureHandle*)pcmd->TextureId
								: &g_app->m_material->getTexture(0)->getTextureHandle());

			elem_offset += pcmd->ElemCount;
		}
	}
}


LRESULT WINAPI msgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return g_app->windowProc(hWnd, msg, wParam, lParam);
}


INT WINAPI WinMain(HINSTANCE hInst, HINSTANCE ignoreMe0, LPSTR ignoreMe1, INT ignoreMe2)
{
	StudioApp app;
	g_app = &app;

	app.init(hInst);
	app.run();
	app.shutdown();
	
	g_app = nullptr;

	return 0;
}