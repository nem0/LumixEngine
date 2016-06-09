#include "audio/audio_scene.h"
#include "audio/clip_manager.h"
#include "asset_browser.h"
#include "engine/blob.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/default_allocator.h"
#include "engine/fixed_array.h"
#include "engine/fs/file_system.h"
#include "engine/fs/os_file.h"
#include "engine/input_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/mt/thread.h"
#include "engine/path_utils.h"
#include "engine/profiler.h"
#include "engine/quat.h"
#include "engine/resource_manager.h"
#include "engine/system.h"
#include "engine/timer.h"
#include "engine/debug/debug.h"
#include "editor/gizmo.h"
#include "editor/entity_groups.h"
#include "editor/entity_template_system.h"
#include "editor/render_interface.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "log_ui.h"
#include "metadata.h"
#include "imgui/imgui.h"
#include "platform_interface.h"
#include "profiler_ui.h"
#include "property_grid.h"
#include "settings.h"
#include "studio_app.h"
#include "utils.h"
#include <SDL.h>
#include <SDL_syswm.h>


class StudioAppImpl* g_app;


class StudioAppImpl : public StudioApp
{
public:
	StudioAppImpl()
		: m_is_entity_list_opened(true)
		, m_finished(false)
		, m_is_entity_template_list_opened(false)
		, m_selected_template_name(m_allocator)
		, m_profiler_ui(nullptr)
		, m_asset_browser(nullptr)
		, m_property_grid(nullptr)
		, m_actions(m_allocator)
		, m_metadata(m_allocator)
		, m_is_welcome_screen_opened(true)
		, m_editor(nullptr)
		, m_settings(m_allocator)
		, m_plugins(m_allocator)
	{
		m_drag_data = { DragData::NONE, nullptr, 0 };
		m_confirm_load = m_confirm_new = m_confirm_exit = false;
		m_exit_code = 0;
		g_app = this;
		m_template_name[0] = '\0';
		m_open_filter[0] = '\0';
		init();
	}


	~StudioAppImpl()
	{
		m_allocator.deallocate(m_drag_data.data);
		shutdown();
		g_app = nullptr;
	}


	Lumix::Array<Action*>& getActions() override
	{
		return m_actions;
	}


	void autosave()
	{
		m_time_to_autosave = float(m_settings.m_autosave_time);
		if (!m_editor->getUniversePath().isValid()) return;
		if (m_editor->isGameMode()) return;

		char filename[Lumix::MAX_PATH_LENGTH];
		Lumix::copyString(filename, m_editor->getUniversePath().c_str());
		Lumix::catString(filename, "_autosave.unv");

		m_editor->saveUniverse(Lumix::Path(filename), false);
	}


	void guiBeginFrame()
	{
		PROFILE_FUNCTION();

		ImGuiIO& io = ImGui::GetIO();
		int w, h;
		SDL_GetWindowSize(m_window, &w, &h);
		io.DisplaySize = ImVec2((float)w, (float)h);
		io.DeltaTime = m_engine->getLastTimeDelta();
		io.KeyShift = ((SDL_GetModState() & KMOD_SHIFT) != 0);
		io.KeyCtrl = ((SDL_GetModState() & KMOD_CTRL) != 0);
		io.KeyAlt = ((SDL_GetModState() & KMOD_ALT) != 0);

		ImGui::NewFrame();
		
		if (m_drag_data.type == DragData::PATH)
		{
			ImGui::BeginTooltip();
			char tmp[Lumix::MAX_PATH_LENGTH];
			Lumix::PathUtils::getFilename(tmp, Lumix::lengthOf(tmp), (const char*)m_drag_data.data);
			ImGui::Text("%s", tmp);
			ImGui::EndTooltip();
		}
	}


	float showMainToolbar(float menu_height)
	{
		bool any_icon = false;
		for (auto* action : m_actions)
		{
			if (action->is_in_toolbar)
			{
				any_icon = true;
				break;
			}
		}
		if (!any_icon) return menu_height;

		auto frame_padding = ImGui::GetStyle().FramePadding;
		float padding = frame_padding.y * 2;
		ImVec4 active_color = ImGui::GetStyle().Colors[ImGuiCol_ButtonActive];
		ImVec4 inactive_color(0, 0, 0, 0);
		if (ImGui::BeginToolbar(
				"main_toolbar", ImVec2(1, menu_height), ImVec2(ImGui::GetIO().DisplaySize.x, 24 + padding)))
		{
			auto& render_interface = *m_editor->getRenderInterface();
			ImVec2 icon_size(24, 24);

			for (int i = 0; i < m_actions.size(); ++i)
			{
				if (i > 0) ImGui::SameLine();
				if (m_actions[i]->is_in_toolbar)
				{
					if (ImGui::ImageButton(m_actions[i]->icon, icon_size))
					{
						m_actions[i]->func.invoke();
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", m_actions[i]->label);
					}
				}
			}
		}
		ImGui::EndToolbar();
		return menu_height + 24 + padding;
	}


	void guiEndFrame()
	{
		if (m_is_welcome_screen_opened)
		{
			showWelcomeScreen();
		}
		else
		{
			float menu_height = showMainMenu();
			float toolbar_bottom = showMainToolbar(menu_height);
			if (ImGui::GetIO().DisplaySize.y > 0)
			{
				auto pos = ImVec2(0, toolbar_bottom);
				auto size = ImGui::GetIO().DisplaySize;
				size.y -= pos.y;
				ImGui::RootDock(pos, size);
			}
			m_profiler_ui->onGUI();
			m_asset_browser->onGUI();
			m_log_ui->onGUI();
			m_property_grid->onGUI();
			showEntityList();
			showEntityTemplateList();
			for (auto* plugin : m_plugins)
			{
				plugin->onWindowGUI();
			}
			m_settings.onGUI(&m_actions[0], m_actions.size());
		}
		ImGui::Render();

		if (ImGui::GetIO().MouseReleased[0])
		{
			m_allocator.deallocate(m_drag_data.data);
			m_drag_data.data = nullptr;
			m_drag_data.size = 0;
			m_drag_data.type = DragData::NONE;
		}
	}

	void update()
	{
		PROFILE_FUNCTION();
		guiBeginFrame();

		float time_delta = m_editor->getEngine().getLastTimeDelta();

		m_time_to_autosave -= time_delta;
		if (m_time_to_autosave < 0) autosave();

		m_editor->setMouseSensitivity(m_settings.m_mouse_sensitivity_x, m_settings.m_mouse_sensitivity_y);
		m_editor->update();
		m_engine->update(*m_editor->getUniverse());

		for (auto* plugin : m_plugins)
		{
			plugin->update(time_delta);
		}
		m_asset_browser->update();
		m_log_ui->update(time_delta);

		guiEndFrame();
	}


	void showWelcomeScreen()
	{
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
								 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
		int w, h;
		SDL_GetWindowSize(m_window, &w, &h);
		ImVec2 size((float)w, (float)h);
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
				auto& universes = m_asset_browser->getResources(0);
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
				if (ImGui::Button("Wiki"))
				{
					PlatformInterface::shellExecuteOpen("https://github.com/nem0/LumixEngine/wiki");
				}

				if (ImGui::Button("Download new version"))
				{
					PlatformInterface::shellExecuteOpen("https://github.com/nem0/lumixengine_data/archive/master.zip");
				}

				if (ImGui::Button("Show major releases"))
				{
					PlatformInterface::shellExecuteOpen("https://github.com/nem0/LumixEngine/releases");
				}

				if (ImGui::Button("Show latest commits"))
				{
					PlatformInterface::shellExecuteOpen("https://github.com/nem0/LumixEngine/commits/master");
				}

				if (ImGui::Button("Show issues"))
				{
					PlatformInterface::shellExecuteOpen("https://github.com/nem0/lumixengine/issues");
				}
				ImGui::Separator();

				ImGui::Text("Version 0.22. - News");
				ImGui::BulletText("default studio settings");
				ImGui::BulletText("navigation");
				ImGui::BulletText("merge meshes during import");
				ImGui::BulletText("advanced CPU profiler view");
				ImGui::BulletText("patch file device");
				ImGui::BulletText("pack file device");
				ImGui::BulletText("ask to save before quit / new / open");
				ImGui::BulletText("new terrian painting features");
				ImGui::BulletText("16bit mesh indices");
				ImGui::BulletText("distance per grass type");
				ImGui::BulletText("lua's require goes through engine");
				ImGui::BulletText("game packing");
				ImGui::Text("Version 0.21. - News");
				ImGui::BulletText("copy / paste multiple entities at once");
				ImGui::BulletText("stencil support");
				ImGui::BulletText("unlimited light intensity");
				ImGui::BulletText("alpha test reference value editable");
				ImGui::BulletText("panning");
				ImGui::BulletText("multiple script components in one entity");
				ImGui::BulletText("errors messages are more visible");
				ImGui::BulletText("plugins can be static libraries");
				ImGui::BulletText("multipass materials");
				ImGui::BulletText("several data sources");
				ImGui::BulletText("editor GUI can be created from lua script");
				ImGui::BulletText("DXT1 for images without alpha");
				ImGui::BulletText("import dialog - several new features, improved UX");
				ImGui::BulletText("show / hide, freeze / unfreeze group");
				ImGui::BulletText("pipeline can be reloaded in runtime");
				ImGui::BulletText("postprocess effect framework");
				ImGui::Separator();
				ImGui::Text("Version 0.20. - News");
				ImGui::BulletText("Deferred rendering");
				ImGui::BulletText("HDR");
				ImGui::BulletText("New editor skin");
				ImGui::BulletText("Top, front, size view");
				ImGui::BulletText("Editor does not depend on plugins");
				ImGui::BulletText("Editor scripting");
				ImGui::BulletText("Scale mesh on import, flip Y/Z axis");
				ImGui::BulletText("Multiple gizmos when editing emitters");
				ImGui::BulletText("Improved color picker");
				ImGui::BulletText("Close notification button");
				ImGui::BulletText("Entity look at");
				ImGui::BulletText("Mesh and material decoupled");
				ImGui::BulletText("Simple animable component");
				ImGui::Separator();
				ImGui::Text("Version 0.19. - News");
				ImGui::BulletText("Editor UI - docking");
				ImGui::BulletText("Physics - layers");
				ImGui::BulletText("File system UI");
				ImGui::BulletText("Particle system player");
				ImGui::BulletText("Particle system using bezier curves");
				ImGui::BulletText("Bezier curves in GUI");
				ImGui::Separator();
				ImGui::Text("Version 0.18. - News");
				ImGui::BulletText("Collision events are sent to scripts");
				ImGui::BulletText("Multithread safe profiler");
				ImGui::BulletText("XBox Controller support");
				ImGui::BulletText("Each script component has its own environment");
				ImGui::BulletText("Pipeline's features can be enabled/disabled in GUI");
				ImGui::BulletText("Shader editor");
				ImGui::BulletText("Audio system");
				ImGui::BulletText("Basic particle system");
			}
			ImGui::EndChild();
		}
		ImGui::End();
	}


	void setTitle(const char* title)
	{
		char tmp[100];
		Lumix::copyString(tmp, "Lumix Studio - ");
		Lumix::catString(tmp, title);
		SDL_SetWindowTitle(m_window, tmp);
	}


	static void getShortcut(const Action& action, char* buf, int max_size)
	{
		buf[0] = 0;
		for (int i = 0; i < Lumix::lengthOf(action.shortcut); ++i)
		{
			const char* str = SDL_GetKeyName(SDL_GetKeyFromScancode((SDL_Scancode)action.shortcut[i]));
			if (str[0] == 0) return;
			if (i > 0) Lumix::catString(buf, max_size, " - ");
			Lumix::catString(buf, max_size, str);
		}
	}


	void doMenuItem(Action& a, bool enabled)
	{
		char buf[20];
		getShortcut(a, buf, sizeof(buf));
		if (ImGui::MenuItem(a.label, buf, a.is_selected.invoke(), enabled))
		{
			a.func.invoke();
		}
	}


	void save()
	{
		if (m_editor->isGameMode())
		{
			Lumix::g_log_error.log("Editor") << "Could not save while the game is running";
			return;
		}

		m_time_to_autosave = float(m_settings.m_autosave_time);
		if (m_editor->getUniversePath().isValid())
		{
			m_editor->saveUniverse(m_editor->getUniversePath(), true);
		}
		else
		{
			char filename[Lumix::MAX_PATH_LENGTH];
			if (PlatformInterface::getSaveFilename(filename, sizeof(filename), "Universes\0*.unv\0", "unv"))
			{
				m_editor->saveUniverse(Lumix::Path(filename), true);
				setTitle(filename);
			}
		}
	}


	void saveAs()
	{
		if (m_editor->isGameMode())
		{
			Lumix::g_log_error.log("Editor") << "Could not save while the game is running";
			return;
		}

		m_time_to_autosave = float(m_settings.m_autosave_time);
		char filename[Lumix::MAX_PATH_LENGTH];
		if (PlatformInterface::getSaveFilename(filename, sizeof(filename), "Universes\0*.unv\0", "unv"))
		{
			m_editor->saveUniverse(Lumix::Path(filename), true);
		}
	}


	void exit()
	{
		if (m_editor->isUniverseChanged())
		{
			m_confirm_exit = true;
		}
		else
		{
			m_finished = true;
		}
	}


	void newUniverse() 
	{ 
		if (m_editor->isUniverseChanged())
		{
			m_confirm_new = true;
		}
		else
		{
			m_editor->newUniverse();
			m_time_to_autosave = float(m_settings.m_autosave_time);
		}
	}


	bool hasPluginFocus()
	{
		for(auto* plugin : m_plugins)
		{
			if(plugin->hasFocus()) return true;
		}
		return false;
	}


	void undo() { if (!hasPluginFocus()) m_editor->undo(); }
	void redo() { if (!hasPluginFocus()) m_editor->redo(); }
	void copy() { m_editor->copyEntities(); }
	void paste() { m_editor->pasteEntities(); }
	bool isOrbitCamera() { return m_editor->isOrbitCamera(); }
	void toggleOrbitCamera() { m_editor->setOrbitCamera(!m_editor->isOrbitCamera()); }
	void setTopView() { m_editor->setTopView(); }
	void setFrontView() { m_editor->setFrontView(); }
	void setSideView() { m_editor->setSideView(); }
	void setLocalCoordSystem() { m_editor->getGizmo().setLocalCoordSystem(); }
	void setGlobalCoordSystem() { m_editor->getGizmo().setGlobalCoordSystem(); }
	void setPivotOrigin() { m_editor->getGizmo().setPivotOrigin(); }
	void setPivotCenter() { m_editor->getGizmo().setPivotCenter(); }
	void createEntity() { m_editor->addEntity(); }
	void showEntities() { m_editor->showSelectedEntities(); }
	void hideEntities() { m_editor->hideSelectedEntities(); }
	void toggleMeasure() { m_editor->toggleMeasure(); }
	void snapDown() { m_editor->snapDown(); }
	void lookAtSelected() { m_editor->lookAtSelected(); }
	int getExitCode() const override { return m_exit_code; }
	AssetBrowser* getAssetBrowser() override { return m_asset_browser; }
	PropertyGrid* getPropertyGrid() override { return m_property_grid; }
	Metadata* getMetadata() override { return &m_metadata; }
	LogUI* getLogUI() override { return m_log_ui; }
	void toggleGameMode() { m_editor->toggleGameMode(); }
	void setTranslateGizmoMode() { m_editor->getGizmo().setTranslateMode(); }
	void setRotateGizmoMode() { m_editor->getGizmo().setRotateMode(); }


	void autosnapDown() 
	{
		auto& gizmo = m_editor->getGizmo();
		gizmo.setAutosnapDown(!gizmo.isAutosnapDown());
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
		if (PlatformInterface::getOpenFilename(filename, Lumix::lengthOf(filename), "JSON files\0*.json\0", nullptr))
		{
			m_editor->executeUndoStack(Lumix::Path(filename));
		}
	}


	void saveUndoStack()
	{
		char filename[Lumix::MAX_PATH_LENGTH];
		if (PlatformInterface::getSaveFilename(filename, Lumix::lengthOf(filename), "JSON files\0*.json\0", "json"))
		{
			m_editor->saveUndoStack(Lumix::Path(filename));
		}
	}


	template <void (StudioAppImpl::*func)()>
	Action& addAction(const char* label, const char* name)
	{
		auto* a = LUMIX_NEW(m_editor->getAllocator(), Action)(label, name);
		a->func.bind<StudioAppImpl, func>(this);
		m_actions.push(a);
		return *a;
	}


	template <void (StudioAppImpl::*func)(), bool (StudioAppImpl::*selected_func)()>
	void addSelectableAction(const char* label, const char* name)
	{
		auto* a = LUMIX_NEW(m_editor->getAllocator(), Action)(label, name);
		a->func.bind<StudioAppImpl, func>(this);
		a->is_selected.bind<StudioAppImpl, selected_func>(this);
		m_actions.push(a);
	}



	template <void (StudioAppImpl::*func)()>
	void addAction(const char* label, const char* name, int shortcut0, int shortcut1, int shortcut2)
	{
		auto* a = LUMIX_NEW(m_editor->getAllocator(), Action)(
			label, name, shortcut0, shortcut1, shortcut2);
		a->func.bind<StudioAppImpl, func>(this);
		m_actions.push(a);
	}


	Action& getAction(const char* name) override
	{
		for (auto* a : m_actions)
		{
			if (Lumix::equalStrings(a->name, name)) return *a;
		}
		ASSERT(false);
		return *m_actions[0];
	}

	
	void entityMenu()
	{
		if (!ImGui::BeginMenu("Entity")) return;

		bool is_any_entity_selected = !m_editor->getSelectedEntities().empty();
		doMenuItem(getAction("createEntity"), true);
		doMenuItem(getAction("destroyEntity"), is_any_entity_selected);

		if (ImGui::BeginMenu("Create template", is_any_entity_selected))
		{
			static char name[255] = "";
			ImGui::InputText("Name###templatename", name, sizeof(name));
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
				m_selected_template_name.c_str(), pos, Lumix::Quat(0, 0, 0, 1), 1);
		}

		doMenuItem(getAction("showEntities"), is_any_entity_selected);
		doMenuItem(getAction("hideEntities"), is_any_entity_selected);
		ImGui::EndMenu();
	}


	void editMenu()
	{
		if (!ImGui::BeginMenu("Edit")) return;

		bool is_any_entity_selected = !m_editor->getSelectedEntities().empty();
		doMenuItem(getAction("undo"), m_editor->canUndo());
		doMenuItem(getAction("redo"), m_editor->canRedo());
		ImGui::Separator();
		doMenuItem(getAction("copy"), is_any_entity_selected);
		doMenuItem(getAction("paste"), m_editor->canPasteEntities());
		ImGui::Separator();
		doMenuItem(getAction("orbitCamera"), is_any_entity_selected || m_editor->isOrbitCamera());
		doMenuItem(getAction("setTranslateGizmoMode"), true);
		doMenuItem(getAction("setRotateGizmoMode"), true);
		doMenuItem(getAction("setPivotCenter"), true);
		doMenuItem(getAction("setPivotOrigin"), true);
		doMenuItem(getAction("setLocalCoordSystem"), true);
		doMenuItem(getAction("setGlobalCoordSystem"), true);
		if (ImGui::BeginMenu("View", true))
		{
			doMenuItem(getAction("viewTop"), true);
			doMenuItem(getAction("viewFront"), true);
			doMenuItem(getAction("viewSide"), true);
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}


	void fileMenu()
	{
		if (!ImGui::BeginMenu("File")) return;

		doMenuItem(getAction("newUniverse"), true);
		if (ImGui::BeginMenu("Open"))
		{
			ImGui::InputText("Filter", m_open_filter, sizeof(m_open_filter));
			auto& universes = m_asset_browser->getResources(0);
			for (auto& univ : universes)
			{
				if ((m_open_filter[0] == '\0' || Lumix::stristr(univ.c_str(), m_open_filter)) &&
					ImGui::MenuItem(univ.c_str()))
				{
					if (m_editor->isUniverseChanged())
					{
						Lumix::copyString(m_universe_to_load, univ.c_str());
						m_confirm_load = true;
					}
					else
					{
						m_time_to_autosave = float(m_settings.m_autosave_time);
						m_editor->loadUniverse(univ);
						setTitle(univ.c_str());
					}
				}
			}
			ImGui::EndMenu();
		}
		doMenuItem(getAction("save"), !m_editor->isGameMode());
		doMenuItem(getAction("saveAs"), !m_editor->isGameMode());
		doMenuItem(getAction("exit"), true);
		ImGui::EndMenu();
	}


	void toolsMenu()
	{
		if (!ImGui::BeginMenu("Tools")) return;

		bool is_any_entity_selected = !m_editor->getSelectedEntities().empty();
		doMenuItem(getAction("lookAtSelected"), is_any_entity_selected);
		doMenuItem(getAction("toggleGameMode"), true);
		doMenuItem(getAction("toggleMeasure"), true);
		doMenuItem(getAction("snapDown"), is_any_entity_selected);
		doMenuItem(getAction("autosnapDown"), true);
		if (ImGui::MenuItem("Save commands")) saveUndoStack();
		if (ImGui::MenuItem("Load commands")) loadAndExecuteCommands();
		if (ImGui::MenuItem("Pack data")) packData();
		ImGui::EndMenu();
	}


	void viewMenu()
	{
		if (!ImGui::BeginMenu("View")) return;

		ImGui::MenuItem("Asset browser", nullptr, &m_asset_browser->m_is_opened);
		ImGui::MenuItem("Entity list", nullptr, &m_is_entity_list_opened);
		ImGui::MenuItem("Entity templates", nullptr, &m_is_entity_template_list_opened);
		ImGui::MenuItem("Log", nullptr, &m_log_ui->m_is_opened);
		ImGui::MenuItem("Profiler", nullptr, &m_profiler_ui->m_is_opened);
		ImGui::MenuItem("Properties", nullptr, &m_property_grid->m_is_opened);
		ImGui::MenuItem("Settings", nullptr, &m_settings.m_is_opened);
		ImGui::Separator();
		for (auto* plugin : m_plugins)
		{
			if (plugin->m_action)
			{
				doMenuItem(*plugin->m_action, true);
			}
		}
		ImGui::EndMenu();
	}


	float showMainMenu()
	{
		bool is_any_entity_selected = !m_editor->getSelectedEntities().empty();
		if (m_confirm_exit)
		{
			ImGui::OpenPopup("confirm_exit");
			m_confirm_exit = false;
		}
		if (ImGui::BeginPopupModal("confirm_exit"))
		{
			ImGui::Text("All unsaved changes will be lost, do you want to continue?");
			if (ImGui::Button("Continue"))
			{
				m_finished = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if (m_confirm_new)
		{
			ImGui::OpenPopup("confirm_new");
			m_confirm_new = false;
		}
		if (ImGui::BeginPopupModal("confirm_new"))
		{
			ImGui::Text("All unsaved changes will be lost, do you want to continue?");
			if (ImGui::Button("Continue"))
			{
				m_editor->newUniverse();
				m_time_to_autosave = float(m_settings.m_autosave_time);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		if (m_confirm_load)
		{
			ImGui::OpenPopup("confirm_load");
			m_confirm_load = false;
		}
		if(ImGui::BeginPopupModal("confirm_load"))
		{
			ImGui::Text("All unsaved changes will be lost, do you want to continue?");
			if (ImGui::Button("Continue"))
			{
				m_time_to_autosave = float(m_settings.m_autosave_time);
				m_editor->loadUniverse(Lumix::Path(m_universe_to_load));
				setTitle(m_universe_to_load);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		float menu_height = 0;
		if (ImGui::BeginMainMenuBar())
		{
			fileMenu();
			editMenu();
			entityMenu();
			toolsMenu();
			viewMenu();

			Lumix::StaticString<200> stats("");
			if (m_engine->getFileSystem().hasWork()) stats << "Loading... | ";
			stats << "FPS: ";
			stats << m_engine->getFPS();
			if ((SDL_GetWindowFlags(m_window) & SDL_WINDOW_INPUT_FOCUS) == 0) stats << " - inactive window";
			auto stats_size = ImGui::CalcTextSize(stats);
			ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x);
			ImGui::Text("%s", (const char*)stats);

			if (m_log_ui->getUnreadErrorCount() == 1)
			{
				ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x);
				auto error_stats_size = ImGui::CalcTextSize("1 error | ");
				ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x - error_stats_size.x);
				ImGui::TextColored(ImVec4(1, 0, 0, 1), "1 error | ");
			}
			else if (m_log_ui->getUnreadErrorCount() > 1)
			{
				Lumix::StaticString<50> error_stats("", m_log_ui->getUnreadErrorCount(), " errors | ");
				ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x);
				auto error_stats_size = ImGui::CalcTextSize(error_stats);
				ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x - error_stats_size.x);
				ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", (const char*)error_stats);
			}
			menu_height = ImGui::GetWindowSize().y;
			ImGui::EndMainMenuBar();
		}
		return menu_height;
	}


	void showEntityTemplateList()
	{
		if (ImGui::BeginDock("Entity Templates", &m_is_entity_template_list_opened))
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
		ImGui::EndDock();
	}


	void showEntityList()
	{
		if (ImGui::BeginDock("Entity List", &m_is_entity_list_opened))
		{
			auto* universe = m_editor->getUniverse();

			auto& groups = m_editor->getEntityGroups();
			static char group_name[20] = "";
			ImGui::InputText("New group name", group_name, Lumix::lengthOf(group_name));
			if(ImGui::Button("Create group"))
			{
				if(group_name[0] == 0)
				{
					Lumix::g_log_error.log("Editor") << "Group name can not be empty";
				}
				else if(groups.getGroup(group_name) != -1)
				{
					Lumix::g_log_error.log("Editor") << "Group with name " << group_name << " already exists";
				}
				else
				{
					groups.createGroup(group_name);
				}
				group_name[0] = 0;
			}
			ImGui::Separator();

			for(int i = 0; i < groups.getGroupCount(); ++i)
			{
				auto* name = groups.getGroupName(i);
				if(ImGui::TreeNode(name, "%s (%d)", name, groups.getGroupEntitiesCount(i)))
				{
					struct ListBoxData
					{
						Lumix::WorldEditor* m_editor;
						Lumix::Universe* universe;
						Lumix::EntityGroups* groups;
						int group;
						char buffer[1024];
						static bool itemsGetter(void* data, int idx, const char** txt)
						{
							auto* d = static_cast<ListBoxData*>(data);
							auto* entities = d->groups->getGroupEntities(d->group);
							getEntityListDisplayName(*d->m_editor, d->buffer, sizeof(d->buffer), entities[idx]);
							*txt = d->buffer;
							return true;
						}
					};
					ListBoxData data;
					data.universe = universe;
					data.m_editor = m_editor;
					data.group = i;
					data.groups = &groups;
					int current_item = -1;
					if(ImGui::ListBox("Entities",
						&current_item,
						&ListBoxData::itemsGetter,
						&data,
						groups.getGroupEntitiesCount(i),
						15))
					{
						auto e = groups.getGroupEntities(i)[current_item];
						m_editor->selectEntities(&e, 1);
					};

					if(groups.getGroupCount() == 1)
					{
						ImGui::Text("Can not delete - at least one group must exists");
					}
					else if(ImGui::Button("Delete group"))
					{
						groups.deleteGroup(i);
					}

					if(ImGui::Button("Select all entities in group"))
					{
						m_editor->selectEntities(groups.getGroupEntities(i), groups.getGroupEntitiesCount(i));
					}

					if(ImGui::Button("Assign selected entities to group"))
					{
						auto& selected = m_editor->getSelectedEntities();
						for(auto e : selected)
						{
							groups.setGroup(e, i);
						}
					}

					if (ImGui::Button("Hide all"))
					{
						m_editor->hideEntities(groups.getGroupEntities(i), groups.getGroupEntitiesCount(i));
					}

					if (ImGui::Button("Show all"))
					{
						m_editor->showEntities(groups.getGroupEntities(i), groups.getGroupEntitiesCount(i));
					}

					if (groups.isGroupFrozen(i) && ImGui::Button("Unfreeze"))
					{
						groups.freezeGroup(i, false);
					}
					else if (!groups.isGroupFrozen(i) && ImGui::Button("Freeze"))
					{
						groups.freezeGroup(i, true);
					}

					ImGui::TreePop();
				}
			}
		}
		ImGui::EndDock();
	}


	void startDrag(DragData::Type type, const void* data, int size) override
	{
		m_allocator.deallocate(m_drag_data.data);

		m_drag_data.type = type;
		if (size > 0)
		{
			m_drag_data.data = m_allocator.allocate(size);
			Lumix::copyMemory(m_drag_data.data, data, size);
			m_drag_data.size = size;
		}
		else
		{
			m_drag_data.data = nullptr;
			m_drag_data.size = 0;
		}
	}


	DragData getDragData() override
	{
		return m_drag_data;
	}


	void saveSettings()
	{
		m_settings.m_is_asset_browser_opened = m_asset_browser->m_is_opened;
		m_settings.m_is_entity_list_opened = m_is_entity_list_opened;
		m_settings.m_is_entity_template_list_opened = m_is_entity_template_list_opened;
		m_settings.m_is_log_opened = m_log_ui->m_is_opened;
		m_settings.m_is_profiler_opened = m_profiler_ui->m_is_opened;
		m_settings.m_is_properties_opened = m_property_grid->m_is_opened;
		m_settings.m_mouse_sensitivity_x = m_editor->getMouseSensitivity().x;
		m_settings.m_mouse_sensitivity_y = m_editor->getMouseSensitivity().y;

		m_settings.save(&m_actions[0], m_actions.size());

		if (!m_metadata.save())
		{
			Lumix::g_log_warning.log("Editor") << "Could not save metadata";
		}
	}


	void shutdown()
	{
		saveSettings();
		unloadIcons();

		while (m_editor->getEngine().getFileSystem().hasWork())
		{
			m_editor->getEngine().getFileSystem().updateAsyncTransactions();
		}

		m_editor->newUniverse();

		for (auto* plugin : m_plugins)
		{
			LUMIX_DELETE(m_editor->getAllocator(), plugin);
		}
		m_plugins.clear();

		for (auto* a : m_actions)
		{
			LUMIX_DELETE(m_editor->getAllocator(), a);
		}
		m_actions.clear();

		ProfilerUI::destroy(*m_profiler_ui);
		LUMIX_DELETE(m_allocator, m_asset_browser);
		LUMIX_DELETE(m_allocator, m_property_grid);
		LUMIX_DELETE(m_allocator, m_log_ui);
		Lumix::WorldEditor::destroy(m_editor, m_allocator);
		Lumix::Engine::destroy(m_engine, m_allocator);
		m_engine = nullptr;
		m_editor = nullptr;

		SDL_DestroyWindow(m_window);
		SDL_Quit();
	}


	void initIMGUI()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->AddFontFromFileTTF("bin/VeraMono.ttf", 13);

		io.KeyMap[ImGuiKey_Tab] = SDLK_TAB;
		io.KeyMap[ImGuiKey_LeftArrow] = SDL_SCANCODE_LEFT;
		io.KeyMap[ImGuiKey_RightArrow] = SDL_SCANCODE_RIGHT;
		io.KeyMap[ImGuiKey_UpArrow] = SDL_SCANCODE_UP;
		io.KeyMap[ImGuiKey_DownArrow] = SDL_SCANCODE_DOWN;
		io.KeyMap[ImGuiKey_PageUp] = SDL_SCANCODE_PAGEUP;
		io.KeyMap[ImGuiKey_PageDown] = SDL_SCANCODE_PAGEDOWN;
		io.KeyMap[ImGuiKey_Home] = SDL_SCANCODE_HOME;
		io.KeyMap[ImGuiKey_End] = SDL_SCANCODE_END;
		io.KeyMap[ImGuiKey_Delete] = SDLK_DELETE;
		io.KeyMap[ImGuiKey_Backspace] = SDLK_BACKSPACE;
		io.KeyMap[ImGuiKey_Enter] = SDLK_RETURN;
		io.KeyMap[ImGuiKey_Escape] = SDLK_ESCAPE;
		io.KeyMap[ImGuiKey_A] = SDLK_a;
		io.KeyMap[ImGuiKey_C] = SDLK_c;
		io.KeyMap[ImGuiKey_V] = SDLK_v;
		io.KeyMap[ImGuiKey_X] = SDLK_x;
		io.KeyMap[ImGuiKey_Y] = SDLK_y;
		io.KeyMap[ImGuiKey_Z] = SDLK_z;
	}


	void loadSettings()
	{
		char cmd_line[2048];
		Lumix::getCommandLine(cmd_line, Lumix::lengthOf(cmd_line));

		Lumix::CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (!parser.currentEquals("-no_crash_report")) continue;

			m_settings.m_force_no_crash_report = true;
			break;
		}

		m_settings.load(&m_actions[0], m_actions.size());

		m_asset_browser->m_is_opened = m_settings.m_is_asset_browser_opened;
		m_is_entity_list_opened = m_settings.m_is_entity_list_opened;
		m_is_entity_template_list_opened = m_settings.m_is_entity_template_list_opened;
		m_log_ui->m_is_opened = m_settings.m_is_log_opened;
		m_profiler_ui->m_is_opened = m_settings.m_is_profiler_opened;
		m_property_grid->m_is_opened = m_settings.m_is_properties_opened;

		if (m_settings.m_is_maximized)
		{
			SDL_MaximizeWindow(m_window);
		}
		else if (m_settings.m_window.w > 0)
		{
			SDL_SetWindowPosition(m_window, m_settings.m_window.x, m_settings.m_window.y);
			SDL_SetWindowSize(m_window, m_settings.m_window.w, m_settings.m_window.h);
		}
	}


	void addActions()
	{
		addAction<&StudioAppImpl::newUniverse>("New", "newUniverse");
		addAction<&StudioAppImpl::save>("Save", "save", KMOD_CTRL, 'S', -1);
		addAction<&StudioAppImpl::saveAs>("Save As",
			"saveAs",
			KMOD_CTRL,
			KMOD_SHIFT,
			'S');
		addAction<&StudioAppImpl::exit>("Exit", "exit", KMOD_CTRL, 'X', -1);

		addAction<&StudioAppImpl::redo>("Redo",
			"redo",
			KMOD_CTRL,
			KMOD_SHIFT,
			'Z');
		addAction<&StudioAppImpl::undo>("Undo", "undo", KMOD_CTRL, 'Z', -1);
		addAction<&StudioAppImpl::copy>("Copy", "copy", KMOD_CTRL, 'C', -1);
		addAction<&StudioAppImpl::paste>("Paste", "paste", KMOD_CTRL, 'V', -1);
		addSelectableAction<&StudioAppImpl::toggleOrbitCamera, &StudioAppImpl::isOrbitCamera>(
			"Orbit camera", "orbitCamera");
		addAction<&StudioAppImpl::setTranslateGizmoMode>("Translate", "setTranslateGizmoMode")
			.is_selected.bind<Lumix::Gizmo, &Lumix::Gizmo::isTranslateMode>(&m_editor->getGizmo());
		addAction<&StudioAppImpl::setRotateGizmoMode>("Rotate", "setRotateGizmoMode")
			.is_selected.bind<Lumix::Gizmo, &Lumix::Gizmo::isRotateMode>(&m_editor->getGizmo());
		addAction<&StudioAppImpl::setTopView>("Top", "viewTop");
		addAction<&StudioAppImpl::setFrontView>("Front", "viewFront");
		addAction<&StudioAppImpl::setSideView>("Side", "viewSide");
		addAction<&StudioAppImpl::setLocalCoordSystem>("Local", "setLocalCoordSystem")
			.is_selected.bind<Lumix::Gizmo, &Lumix::Gizmo::isLocalCoordSystem>(&m_editor->getGizmo());
		addAction<&StudioAppImpl::setGlobalCoordSystem>("Global", "setGlobalCoordSystem")
			.is_selected.bind<Lumix::Gizmo, &Lumix::Gizmo::isGlobalCoordSystem>(&m_editor->getGizmo());
		addAction<&StudioAppImpl::setPivotCenter>("Center", "setPivotCenter")
			.is_selected.bind<Lumix::Gizmo, &Lumix::Gizmo::isPivotCenter>(&m_editor->getGizmo());
		addAction<&StudioAppImpl::setPivotOrigin>("Origin", "setPivotOrigin")
			.is_selected.bind<Lumix::Gizmo, &Lumix::Gizmo::isPivotOrigin>(&m_editor->getGizmo());

		addAction<&StudioAppImpl::createEntity>("Create", "createEntity");
		addAction<&StudioAppImpl::destroyEntity>("Destroy", "destroyEntity", SDLK_DELETE, -1, -1);
		addAction<&StudioAppImpl::showEntities>("Show", "showEntities");
		addAction<&StudioAppImpl::hideEntities>("Hide", "hideEntities");

		addAction<&StudioAppImpl::toggleGameMode>("Game Mode", "toggleGameMode")
			.is_selected.bind<Lumix::WorldEditor, &Lumix::WorldEditor::isGameMode>(m_editor);
		addAction<&StudioAppImpl::toggleMeasure>("Toggle measure", "toggleMeasure")
			.is_selected.bind<Lumix::WorldEditor, &Lumix::WorldEditor::isMeasureToolActive>(m_editor);
		addAction<&StudioAppImpl::autosnapDown>("Autosnap down", "autosnapDown")
			.is_selected.bind<Lumix::Gizmo, &Lumix::Gizmo::isAutosnapDown>(&m_editor->getGizmo());
		addAction<&StudioAppImpl::snapDown>("Snap down", "snapDown");
		addAction<&StudioAppImpl::lookAtSelected>("Look at selected", "lookAtSelected");
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
				Lumix::g_log_error.log("Editor") << "Could not load plugin " << tmp
											   << " requested by command line";
			}
		}
	}


	void loadUniverseFromCommandLine()
	{
		char cmd_line[2048];
		char path[Lumix::MAX_PATH_LENGTH];
		Lumix::getCommandLine(cmd_line, Lumix::lengthOf(cmd_line));

		Lumix::CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (!parser.currentEquals("-open")) continue;
			if (!parser.next()) break;

			parser.getCurrent(path, Lumix::lengthOf(path));
			Lumix::Path tmp(path);
			m_editor->loadUniverse(tmp);
			setTitle(path);
			m_is_welcome_screen_opened = false;
			break;
		}
	}


	static void checkDataDirCommandLine(char* dir, int max_size)
	{
		char cmd_line[2048];
		Lumix::getCommandLine(cmd_line, Lumix::lengthOf(cmd_line));

		Lumix::CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (!parser.currentEquals("-data_dir")) continue;
			if (!parser.next()) break;

			parser.getCurrent(dir, max_size);
			break;
		}
	}


	void addPlugin(IPlugin& plugin) override
	{
		m_plugins.push(&plugin);
		if (plugin.m_action)
		{
			m_actions.push(plugin.m_action);
		}
	}


	void removePlugin(IPlugin& plugin) override
	{
		m_plugins.eraseItemFast(&plugin);
	}


	void setStudioApp()
	{
		auto& plugin_manager = m_editor->getEngine().getPluginManager();
		#ifdef STATIC_PLUGINS
			for (auto* plugin : plugin_manager.getPlugins())
			{
				StudioApp::StaticPluginRegister::create(plugin->getName(), *this);
			}
		#else
			for (auto* lib : plugin_manager.getLibraries())
			{
				auto* f = (void (*)(StudioApp&))Lumix::getLibrarySymbol(lib, "setStudioApp");
				if (f) f(*this);
			}
		#endif
	}


	void runScript(const char* src, const char* script_name) override
	{
		lua_State* L = m_engine->getState();
		bool errors =
			luaL_loadbuffer(L, src, Lumix::stringLength(src), script_name) != LUA_OK;
		errors = errors || lua_pcall(L, 0, 0, 0) != LUA_OK;
		if (errors)
		{
			Lumix::g_log_error.log("Editor") << script_name << ": " << lua_tostring(L, -1);
			lua_pop(L, 1);
		}
	}


	void LUA_createEntityTemplate(Lumix::Entity entity, const char* name)
	{
		m_editor->getEntityTemplateSystem().createTemplateFromEntity(name, entity);
	}


	void LUA_exit(int exit_code)
	{
		m_finished = true;
		m_exit_code = exit_code;
	}


	bool LUA_runTest(const char* undo_stack_path, const char* result_universe_path)
	{
		return m_editor->runTest(Lumix::Path(undo_stack_path), Lumix::Path(result_universe_path));
	}

	
	void createLua()
	{
		lua_State* L = m_engine->getState();

		Lumix::LuaWrapper::createSystemVariable(L, "Editor", "editor", this);

		#define REGISTER_FUNCTION(F) \
			do { \
				auto* f = &Lumix::LuaWrapper::wrapMethod<StudioAppImpl, decltype(&StudioAppImpl::LUA_##F), &StudioAppImpl::LUA_##F>; \
				Lumix::LuaWrapper::createSystemFunction(L, "Editor", #F, f); \
			} while(false) \

		REGISTER_FUNCTION(runTest);
		REGISTER_FUNCTION(exit);
		REGISTER_FUNCTION(createEntityTemplate);

		#undef REGISTER_FUNCTION
	}


	void checkScriptCommandLine()
	{
		char command_line[1024];
		Lumix::getCommandLine(command_line, Lumix::lengthOf(command_line));
		Lumix::CommandLineParser parser(command_line);
		while (parser.next())
		{
			if (parser.currentEquals("-run_script"))
			{
				if (!parser.next()) break;
				char tmp[Lumix::MAX_PATH_LENGTH];
				parser.getCurrent(tmp, Lumix::lengthOf(tmp));
				Lumix::FS::OsFile file;
				if (file.open(tmp, Lumix::FS::Mode::OPEN_AND_READ, m_allocator))
				{
					auto size = file.size();
					auto* src = (char*)m_allocator.allocate(size + 1);
					file.read(src, size);
					src[size] = 0;
					runScript((const char*)src, tmp);
					m_allocator.deallocate(src);
					file.close();
				}
				else
				{
					Lumix::g_log_error.log("Editor") << "Could not open " << tmp;
				}
				break;
			}
		}
	}


	static bool includeFileInPack(const char* filename)
	{
		if (filename[0] == '.') return false;
		if (Lumix::compareStringN("bin/", filename, 4) == 0) return false;
		if (Lumix::compareStringN("bin32/", filename, 4) == 0) return false;
		if (Lumix::equalStrings("data.pak", filename)) return false;
		if (Lumix::equalStrings("error.log", filename)) return false;
		return true;
	}


	static bool includeDirInPack(const char* filename)
	{
		if (filename[0] == '.') return false;
		if (Lumix::compareStringN("bin", filename, 4) == 0) return false;
		if (Lumix::compareStringN("bin32", filename, 4) == 0) return false;
		return true;
	}


	#pragma pack(1)
	struct PackFileInfo
	{
		Lumix::uint32 hash;
		Lumix::uint64 offset;
		Lumix::uint64 size;

		using Path = Lumix::FixedArray<char, Lumix::MAX_PATH_LENGTH>;
	};
	#pragma pack()


	void packDataScan(const char* dir_path, Lumix::Array<PackFileInfo>& infos, Lumix::Array<PackFileInfo::Path>& paths)
	{
		auto* iter = PlatformInterface::createFileIterator(dir_path, m_allocator);
		PlatformInterface::FileInfo info;
		while (PlatformInterface::getNextFile(iter, &info))
		{
			char normalized_path[Lumix::MAX_PATH_LENGTH];
			Lumix::PathUtils::normalize(info.filename, normalized_path, Lumix::lengthOf(normalized_path));
			if (info.is_directory)
			{
				if (!includeDirInPack(normalized_path)) continue;

				char dir[Lumix::MAX_PATH_LENGTH] = {0};
				if (dir_path[0] != '.') Lumix::copyString(dir, dir_path);
				Lumix::catString(dir, info.filename);
				Lumix::catString(dir, "/");
				packDataScan(dir, infos, paths);
				continue;
			}

			if(!includeFileInPack(normalized_path)) continue;

			auto& out_path = paths.emplace();
			if(dir_path[0] == '.')
			{
				Lumix::copyString(out_path.data(), out_path.size(), normalized_path);
			}
			else
			{
				Lumix::copyString(out_path.data(), out_path.size(), dir_path);
				Lumix::catString(out_path.data(), out_path.size(), normalized_path);
			}
			auto& out_info = infos.emplace();
			out_info.hash = Lumix::crc32(out_path.data());
			out_info.size = PlatformInterface::getFileSize(out_path.data());
			out_info.offset = ~0UL;
		}
	}


	void packData()
	{
		char dest[Lumix::MAX_PATH_LENGTH];
		char dest_dir[Lumix::MAX_PATH_LENGTH];
		if (!PlatformInterface::getOpenDirectory(dest_dir, Lumix::lengthOf(dest_dir), ".")) return;

		static const char* OUT_FILENAME = "data.pak";
		Lumix::catString(dest_dir, "/");
		Lumix::copyString(dest, dest_dir);
		Lumix::catString(dest, OUT_FILENAME);
		Lumix::Array<PackFileInfo> infos(m_allocator);
		Lumix::Array<PackFileInfo::Path> paths(m_allocator);
		infos.reserve(10000);
		paths.reserve(10000);
		packDataScan("./", infos, paths);
		if (infos.empty())
		{
			Lumix::g_log_error.log("Editor") << "No files found while trying to create " << dest;
			return;
		}

		Lumix::FS::OsFile file;
		if (!file.open(dest, Lumix::FS::Mode::CREATE_AND_WRITE, m_allocator))
		{
			Lumix::g_log_error.log("Editor") << "Could not create " << dest;
			return;
		}

		int count = infos.size();
		file.write(&count, sizeof(count));
		Lumix::uint64 offset = sizeof(count) + sizeof(infos[0]) * count;
		for (auto& info : infos)
		{
			info.offset = offset;
			offset += info.size;
		}
		file.write(&infos[0], sizeof(infos[0]) * count);

		for (auto& path : paths)
		{
			Lumix::FS::OsFile src;
			size_t src_size = PlatformInterface::getFileSize(path.data());
			if (!src.open(path.data(), Lumix::FS::Mode::OPEN_AND_READ, m_allocator))
			{
				file.close();
				Lumix::g_log_error.log("Editor") << "Could not open " << path.data();
				return;
			}
			Lumix::uint8 buf[4096];
			for (; src_size > 0; src_size -= Lumix::Math::minimum(sizeof(buf), src_size))
			{
				size_t batch_size = Lumix::Math::minimum(sizeof(buf), src_size);
				if (!src.read(buf, batch_size))
				{
					file.close();
					Lumix::g_log_error.log("Editor") << "Could not read " << path.data();
					return;
				}
				file.write(buf, batch_size);
			}
			src.close();
		}

		file.close();

		const char* bin_files[] = {
			"app.exe",
			"assimp.dll",
			"nvToolsExt64_1.dll",
			"PhysX3CharacterKinematicCHECKED_x64.dll",
			"PhysX3CHECKED_x64.dll",
			"PhysX3CommonCHECKED_x64.dll",
			"PhysX3CookingCHECKED_x64.dll"
		};
		for(auto& file : bin_files)
		{
			Lumix::StaticString<Lumix::MAX_PATH_LENGTH> tmp(dest_dir, file);
			Lumix::StaticString<Lumix::MAX_PATH_LENGTH> src("bin/", file);
			if (!Lumix::copyFile(src, tmp))
			{
				Lumix::g_log_error.log("Editor") << "Failed to copy " << src << " to " << tmp;
			}
		}
		Lumix::StaticString<Lumix::MAX_PATH_LENGTH> tmp(dest_dir);
		tmp << "startup.lua";
		if (!Lumix::copyFile("startup.lua", tmp))
		{
			Lumix::g_log_error.log("Editor") << "Failed to copy startup.lua to " << tmp;
		}
	}


	void processSystemEvents()
	{
		SDL_Event event;
		auto& io = ImGui::GetIO();
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
				case SDL_WINDOWEVENT:
					switch (event.window.event)
					{
						case SDL_WINDOWEVENT_MOVED:
						case SDL_WINDOWEVENT_SIZE_CHANGED:
						{
							int x, y, w, h;
							SDL_GetWindowSize(m_window, &w, &h);
							SDL_GetWindowPosition(m_window, &x, &y);
							onWindowTransformed(x, y, w, h);
						}
						break;
						case SDL_WINDOWEVENT_CLOSE: exit(); break;
					}
					break;
				case SDL_QUIT: exit(); break;
				case SDL_MOUSEBUTTONDOWN:
					m_editor->setAdditiveSelection(io.KeyCtrl);
					m_editor->setSnapMode(io.KeyShift);
					switch (event.button.button)
					{
						case SDL_BUTTON_LEFT: io.MouseDown[0] = true; break;
						case SDL_BUTTON_RIGHT: io.MouseDown[1] = true; break;
						case SDL_BUTTON_MIDDLE: io.MouseDown[2] = true; break;
					}
					break;
				case SDL_MOUSEBUTTONUP:
					switch (event.button.button)
					{
						case SDL_BUTTON_LEFT: io.MouseDown[0] = false; break;
						case SDL_BUTTON_RIGHT: io.MouseDown[1] = false; break;
						case SDL_BUTTON_MIDDLE: io.MouseDown[2] = false; break;
					}
					break;
				case SDL_MOUSEMOTION:
				{
					auto& input_system = m_editor->getEngine().getInputSystem();
					input_system.injectMouseXMove(float(event.motion.xrel));
					input_system.injectMouseYMove(float(event.motion.yrel));
					if (SDL_GetRelativeMouseMode() == SDL_FALSE)
					{
						io.MousePos.x = (float)event.motion.x;
						io.MousePos.y = (float)event.motion.y;
					}
				}
				break;
				case SDL_TEXTINPUT: io.AddInputCharactersUTF8(event.text.text); break;
				case SDL_KEYDOWN:
				case SDL_KEYUP:
				{
					int key = event.key.keysym.sym & ~SDLK_SCANCODE_MASK;
					io.KeysDown[key] = (event.type == SDL_KEYDOWN);
					io.KeyShift = ((SDL_GetModState() & KMOD_SHIFT) != 0);
					io.KeyCtrl = ((SDL_GetModState() & KMOD_CTRL) != 0);
					io.KeyAlt = ((SDL_GetModState() & KMOD_ALT) != 0);
					checkShortcuts();
				}
				break;
				case SDL_MOUSEWHEEL:
				{
					io.MouseWheel = float(event.wheel.x != 0 ? event.wheel.x : event.wheel.y);
					break;
				}
			}
		}
	}


	void run() override
	{
		checkScriptCommandLine();

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
					if (!m_finished) update();
					frame_time = timer->tick();
				}

				float wanted_fps = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_INPUT_FOCUS) != 0 ? 60.0f : 5.0f;
				if (frame_time < 1 / wanted_fps)
				{
					PROFILE_BLOCK("sleep");
					Lumix::MT::sleep(Lumix::uint32(1000 / wanted_fps - frame_time * 1000));
				}
			}
			Lumix::Profiler::frame();
		}
		Lumix::Timer::destroy(timer);
	}


	void checkWorkingDirector()
	{
		if (!PlatformInterface::dirExists("shaders"))
		{
			Lumix::messageBox("Shaders directory not found, please check working directory.");
		}
		else if (!PlatformInterface::dirExists("bin"))
		{
			Lumix::messageBox("Bin directory not found, please check working directory.");
		}
		else if (!PlatformInterface::dirExists("pipelines"))
		{
			Lumix::messageBox("Pipelines directory not found, please check working directory.");
		}
	}


	void unloadIcons()
	{
		auto& render_interface = *m_editor->getRenderInterface();
		for (auto* action : m_actions)
		{
			render_interface.unloadTexture(action->icon);
		}
	}


	void loadIcons()
	{
		auto& render_interface = *m_editor->getRenderInterface();
		for (auto* action : m_actions)
		{
			char tmp[Lumix::MAX_PATH_LENGTH];
			action->getIconPath(tmp, Lumix::lengthOf(tmp));
			if (PlatformInterface::fileExists(tmp))
			{
				action->icon = render_interface.loadTexture(Lumix::Path(tmp));
			}
			else
			{
				action->icon = nullptr;
			}
		}
	}


	void init()
	{
		SDL_SetMainReady();
		SDL_Init(SDL_INIT_VIDEO);

		checkWorkingDirector();
		m_window = SDL_CreateWindow("Lumix Studio", 0, 0, 800, 600, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
		
		char current_dir[Lumix::MAX_PATH_LENGTH];
		PlatformInterface::getCurrentDirectory(current_dir, Lumix::lengthOf(current_dir));

		char data_dir_path[Lumix::MAX_PATH_LENGTH] = {};
		checkDataDirCommandLine(data_dir_path, Lumix::lengthOf(data_dir_path));
		m_engine = Lumix::Engine::create(current_dir, data_dir_path, nullptr, m_allocator);
		createLua();

		SDL_SysWMinfo window_info;
		SDL_VERSION(&window_info.version);
		SDL_GetWindowWMInfo(m_window, &window_info);
		Lumix::Engine::PlatformData platform_data = {};
		#ifdef _WIN32
			platform_data.window_handle = window_info.info.win.window;
			ImGui::GetIO().ImeWindowHandle = window_info.info.win.window;
		#elif defined(__linux__)
			platform_data.window_handle = (void*)(uintptr_t)window_info.info.x11.window;
			platform_data.display = window_info.info.x11.display;
		#endif
		m_engine->setPlatformData(platform_data);

		m_editor = Lumix::WorldEditor::create(current_dir, *m_engine, m_allocator);
		m_settings.m_editor = m_editor;
		loadUserPlugins();

		addActions();

		m_asset_browser = LUMIX_NEW(m_allocator, AssetBrowser)(*this);
		m_property_grid = LUMIX_NEW(m_allocator, PropertyGrid)(*m_editor, *m_asset_browser, m_actions);
		auto engine_allocator = static_cast<Lumix::Debug::Allocator*>(&m_engine->getAllocator());
		m_profiler_ui = ProfilerUI::create(*m_engine);
		m_log_ui = LUMIX_NEW(m_allocator, LogUI)(m_editor->getAllocator());

		initIMGUI();

		if (!m_metadata.load()) Lumix::g_log_info.log("Editor") << "Could not load metadata";

		setStudioApp();
		loadIcons();
		loadSettings();
		loadUniverseFromCommandLine();
	}


	void checkShortcuts()
	{
		if (ImGui::IsAnyItemActive()) return;

		int key_count;
		auto* state = SDL_GetKeyboardState(&key_count);
		Lumix::uint32 pressed_modifiers = SDL_GetModState() & (KMOD_CTRL | KMOD_ALT | KMOD_SHIFT);
		for (auto* a : m_actions)
		{
			if (!a->is_global || a->shortcut[0] == -1) continue;

			Lumix::uint8 action_modifiers = 0;
			for (int i = 0; i < Lumix::lengthOf(a->shortcut) + 1; ++i)
			{
				if ((i == Lumix::lengthOf(a->shortcut) || a->shortcut[i] == -1) &&
					action_modifiers == pressed_modifiers)
				{
					a->func.invoke();
					return;
				}

				if (i == Lumix::lengthOf(a->shortcut)) break;
				if (a->shortcut[i] == -1) break;
				if (a->shortcut[i] >= key_count) break;
				if (!state[a->shortcut[i]]) break;
				if (a->shortcut[i] == SDL_SCANCODE_LCTRL) action_modifiers |= KMOD_LCTRL;
				else if (a->shortcut[i] == SDL_SCANCODE_LALT) action_modifiers |= KMOD_LALT;
				else if (a->shortcut[i] == SDL_SCANCODE_LSHIFT) action_modifiers |= KMOD_LSHIFT;
				else if (a->shortcut[i] == SDL_SCANCODE_RCTRL) action_modifiers |= KMOD_RCTRL;
				else if (a->shortcut[i] == SDL_SCANCODE_RALT) action_modifiers |= KMOD_RALT;
				else if (a->shortcut[i] == SDL_SCANCODE_RSHIFT) action_modifiers |= KMOD_RSHIFT;
			}
		}
	}


	void onWindowTransformed(int x, int y, int width, int height)
	{
		if (height == 0) return;

		m_settings.m_window.x = x;
		m_settings.m_window.y = y;
		m_settings.m_window.w = width;
		m_settings.m_window.h = height;
		m_settings.m_is_maximized = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) != 0;
	}


	void clearInputs()
	{
		auto& io = ImGui::GetIO();
		io.KeyAlt = false;
		io.KeyCtrl = false;
		io.KeyShift = false;
		memset(io.KeysDown, 0, sizeof(io.KeysDown));
		memset(io.MouseDown, 0, sizeof(io.MouseDown));
	}
	

	SDL_Window* getWindow() override
	{
		return m_window;
	}


	Lumix::WorldEditor* getWorldEditor() override
	{
		return m_editor;
	}


	Lumix::DefaultAllocator m_allocator;
	Lumix::Engine* m_engine;
	SDL_Window* m_window;

	float m_time_to_autosave;
	Lumix::Array<Action*> m_actions;
	Lumix::Array<IPlugin*> m_plugins;
	Lumix::WorldEditor* m_editor;
	bool m_confirm_exit;
	bool m_confirm_load;
	bool m_confirm_new;
	char m_universe_to_load[Lumix::MAX_PATH_LENGTH];
	AssetBrowser* m_asset_browser;
	PropertyGrid* m_property_grid;
	LogUI* m_log_ui;
	ProfilerUI* m_profiler_ui;
	Lumix::string m_selected_template_name;
	Settings m_settings;
	Metadata m_metadata;
	char m_template_name[100];
	char m_open_filter[64];

	bool m_finished;
	int m_exit_code;

	bool m_is_welcome_screen_opened;
	bool m_is_entity_list_opened;
	bool m_is_entity_template_list_opened;
	DragData m_drag_data;
};


static size_t alignMask(size_t _value, size_t _mask)
{
	return (_value + _mask) & ((~0) & (~_mask));
}


static void* alignPtr(void* _ptr, size_t _align)
{
	union { void* ptr; size_t addr; } un;
	un.ptr = _ptr;
	size_t mask = _align - 1;
	size_t aligned = alignMask(un.addr, mask);
	un.addr = aligned;
	return un.ptr;
}


StudioApp* StudioApp::create()
{
	static char buf[sizeof(StudioAppImpl) * 2];
	return new (Lumix::NewPlaceholder(), alignPtr(buf, ALIGN_OF(StudioAppImpl))) StudioAppImpl;
}


void StudioApp::destroy(StudioApp& app)
{
	app.~StudioApp();
}


static StudioApp::StaticPluginRegister* s_first_plugin = nullptr;


StudioApp::StaticPluginRegister::StaticPluginRegister(const char* name, Creator creator)
{
	this->creator = creator;
	this->name = name;
	next = s_first_plugin;
	s_first_plugin = this;
}


void StudioApp::StaticPluginRegister::create(const char* name, StudioApp& app)
{
	auto* i = s_first_plugin;
	while (i)
	{
		if (Lumix::equalStrings(name, i->name))
		{
			i->creator(app);
			return;
		}
		i = i->next;
	}
}
