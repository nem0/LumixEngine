#include "audio/audio_scene.h"
#include "audio/clip_manager.h"
#include "asset_browser.h"
#include "core/blob.h"
#include "core/command_line_parser.h"
#include "core/crc32.h"
#include "core/default_allocator.h"
#include "core/fs/file_system.h"
#include "core/fs/os_file.h"
#include "core/input_system.h"
#include "core/log.h"
#include "core/lua_wrapper.h"
#include "core/mt/thread.h"
#include "core/path_utils.h"
#include "core/profiler.h"
#include "core/quat.h"
#include "core/resource_manager.h"
#include "core/system.h"
#include "core/timer.h"
#include "debug/debug.h"
#include "editor/gizmo.h"
#include "editor/entity_groups.h"
#include "editor/entity_template_system.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "engine/plugin_manager.h"
#include "import_asset_dialog.h"
#include "log_ui.h"
#include "metadata.h"
#include "imgui/imgui.h"
#include "platform_interface.h"
#include "profiler_ui.h"
#include "property_grid.h"
#include "settings.h"
#include "studio_app.h"
#include "utils.h"


class StudioAppImpl* g_app;


class StudioAppImpl : public StudioApp
{
public:
	StudioAppImpl()
		: m_is_entity_list_opened(true)
		, m_finished(false)
		, m_import_asset_dialog(nullptr)
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
		m_exit_code = 0;
		g_app = this;
		m_template_name[0] = '\0';
		init();
	}


	~StudioAppImpl()
	{
		shutdown();
		g_app = nullptr;
	}


	Lumix::Array<Action*>& getActions() override
	{
		return m_actions;
	}


	lua_State* getLuaState() const override
	{
		return m_lua_state;
	}


	int getExitCode() const override { return m_exit_code; }


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

		m_editor->setMouseSensitivity(m_settings.m_mouse_sensitivity_x, m_settings.m_mouse_sensitivity_y);
		m_editor->update();
		m_engine->update(*m_editor->getUniverse());

		for (auto* plugin : m_plugins)
		{
			plugin->update(time_delta);
		}
		m_asset_browser->update();
		m_log_ui->update(time_delta);

		onGUI();

		m_engine->getInputSystem().clear();
	}


	AssetBrowser* getAssetBrowser() override
	{
		return m_asset_browser;
	}


	PropertyGrid* getPropertyGrid() override
	{
		return m_property_grid;
	}


	LogUI* getLogUI() override
	{
		return m_log_ui;
	}


	void showWelcomeScreen()
	{
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
								 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
		ImVec2 size((float)PlatformInterface::getWindowWidth(),
			(float)PlatformInterface::getWindowHeight());
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
					PlatformInterface::shellExecuteOpen(
						"https://github.com/nem0/LumixEngine/wiki");
				}

				if (ImGui::Button("Download new version"))
				{
					PlatformInterface::shellExecuteOpen(
						"https://github.com/nem0/lumixengine_data/archive/master.zip");
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
				ImGui::Separator();
				ImGui::Text("Version 0.17. - News");
				ImGui::BulletText("Back button in the asset browser");
				ImGui::BulletText("Grass culling");
				ImGui::BulletText("Importing compressed embedded textures");
				ImGui::BulletText("Euler angles");
				ImGui::BulletText("Textures relative to root");
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

			}
			ImGui::EndChild();
		}
		ImGui::End();
	}


	void onGUI()
	{
		PROFILE_FUNCTION();

		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)PlatformInterface::getWindowWidth(),
			(float)PlatformInterface::getWindowHeight());
		io.DeltaTime = m_engine->getLastTimeDelta();
		io.KeyCtrl = PlatformInterface::isPressed((int)PlatformInterface::Keys::CONTROL);
		io.KeyShift = PlatformInterface::isPressed((int)PlatformInterface::Keys::SHIFT);
		io.KeyAlt = PlatformInterface::isPressed((int)PlatformInterface::Keys::ALT);
		io.KeysDown[(int)PlatformInterface::Keys::ALT] = io.KeyAlt;
		io.KeysDown[(int)PlatformInterface::Keys::SHIFT] = io.KeyShift;
		io.KeysDown[(int)PlatformInterface::Keys::CONTROL] = io.KeyCtrl;

		PlatformInterface::setCursor(io.MouseDrawCursor ? PlatformInterface::Cursor::NONE
														: PlatformInterface::Cursor::DEFAULT);

		ImGui::NewFrame();

		if (m_is_welcome_screen_opened)
		{
			showWelcomeScreen();
		}
		else
		{
			showMainMenu();
			if (ImGui::GetIO().DisplaySize.y > 0)
			{
				auto pos = ImVec2(0, ImGui::GetWindowFontSize() + ImGui::GetStyle().FramePadding.y * 2);
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
			m_import_asset_dialog->onGUI();
		}

		ImGui::Render();
	}


	void setTitle(const char* title)
	{
		char tmp[100];
		Lumix::copyString(tmp, "Lumix Studio - ");
		Lumix::catString(tmp, title);
		PlatformInterface::setWindowTitle(tmp);
	}


	static void getShortcut(const Action& action, char* buf, int max_size)
	{
		buf[0] = 0;
		for (int i = 0; i < Lumix::lengthOf(action.shortcut); ++i)
		{
			char str[30];
			PlatformInterface::getKeyName(action.shortcut[i], str, Lumix::lengthOf(str));
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
			if (PlatformInterface::getSaveFilename(
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
		if (PlatformInterface::getSaveFilename(filename, sizeof(filename), "Universes\0*.unv\0", "unv"))
		{
			m_editor->saveUniverse(Lumix::Path(filename), true);
		}
	}


	void exit() { m_finished = true; }

	void newUniverse()
	{
		m_editor->newUniverse();
		m_time_to_autosave = float(m_settings.m_autosave_time);
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
	void toggleOrbitCamera() { m_editor->setOrbitCamera(!m_editor->isOrbitCamera()); }
	void setTopView() { m_editor->setTopView(); }
	void setFrontView() { m_editor->setFrontView(); }
	void setSideView() { m_editor->setSideView(); }
	void togglePivotMode() { m_editor->getGizmo().togglePivot(); }
	void toggleCoordSystem() { m_editor->getGizmo().toggleCoordSystem(); }
	void createEntity() { m_editor->addEntity(); }
	void showEntities() { m_editor->showEntities(); }
	void hideEntities() { m_editor->hideEntities(); }
	void toggleMeasure() { m_editor->toggleMeasure(); }
	void snapDown() { m_editor->snapDown(); }
	void lookAtSelected() { m_editor->lookAtSelected(); }

	void autosnapDown() 
	{
		auto& gizmo = m_editor->getGizmo();
		gizmo.setAutosnapDown(!gizmo.isAutosnapDown());
	}

	void toggleGizmoMode() 
	{
		auto& gizmo = m_editor->getGizmo();
		gizmo.toggleMode();
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
		if (PlatformInterface::getSaveFilename(
				filename, Lumix::lengthOf(filename), "JSON files\0*.json\0", "json"))
		{
			m_editor->saveUndoStack(Lumix::Path(filename));
		}
	}


	template <void (StudioAppImpl::*func)()>
	void addAction(const char* label, const char* name)
	{
		auto* a = LUMIX_NEW(m_editor->getAllocator(), Action)(label, name);
		a->func.bind<StudioAppImpl, func>(this);
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


	Action& getAction(const char* name)
	{
		for (auto* a : m_actions)
		{
			if (Lumix::compareString(a->name, name) == 0) return *a;
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
					auto& universes = m_asset_browser->getResources(0);
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
				doMenuItem(getAction("paste"), false, m_editor->canPasteEntities());
				ImGui::Separator();
				doMenuItem(getAction("orbitCamera"),
					m_editor->isOrbitCamera(),
					is_any_entity_selected || m_editor->isOrbitCamera());
				doMenuItem(getAction("toggleGizmoMode"), false, is_any_entity_selected);
				doMenuItem(getAction("togglePivotMode"), false, is_any_entity_selected);
				doMenuItem(getAction("toggleCoordSystem"), false, is_any_entity_selected);
				if (ImGui::BeginMenu("View", true))
				{
					doMenuItem(getAction("viewTop"), false, true);
					doMenuItem(getAction("viewFront"), false, true);
					doMenuItem(getAction("viewSide"), false, true);
					ImGui::EndMenu();
				}
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
						m_selected_template_name.c_str(), pos, Lumix::Quat(0, 0, 0, 1));
				}

				doMenuItem(getAction("showEntities"), false, is_any_entity_selected);
				doMenuItem(getAction("hideEntities"), false, is_any_entity_selected);
				ImGui::EndMenu();
			}


			if (ImGui::BeginMenu("Tools"))
			{
				doMenuItem(getAction("lookAtSelected"), false, is_any_entity_selected);
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
						doMenuItem(*plugin->m_action, false, true);
					}
				}
				ImGui::EndMenu();
			}
			StringBuilder<200> stats("");
			if (m_engine->getFileSystem().hasWork()) stats << "Loading... | ";
			stats << "FPS: ";
			stats << m_engine->getFPS();
			auto stats_size = ImGui::CalcTextSize(stats);
			ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x);
			ImGui::Text(stats);

			if (m_log_ui->getUnreadErrorCount() == 1)
			{
				ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x);
				auto error_stats_size = ImGui::CalcTextSize("1 error | ");
				ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x - error_stats_size.x);
				ImGui::TextColored(ImVec4(1, 0, 0, 1), "1 error | ");
			}
			else if (m_log_ui->getUnreadErrorCount() > 1)
			{
				StringBuilder<50> error_stats("", m_log_ui->getUnreadErrorCount(), " errors | ");
				ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x);
				auto error_stats_size = ImGui::CalcTextSize(error_stats);
				ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x - error_stats_size.x);
				ImGui::TextColored(ImVec4(1, 0, 0, 1), error_stats);
			}

			ImGui::EndMainMenuBar();
		}
	}


	void toggleGameMode() { m_editor->toggleGameMode(); }


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

					ImGui::TreePop();
				}
			}
		}
		ImGui::EndDock();
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

		while (m_editor->getEngine().getFileSystem().hasWork())
		{
			m_editor->getEngine().getFileSystem().updateAsyncTransactions();
		}

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
		LUMIX_DELETE(m_allocator, m_import_asset_dialog);
		LUMIX_DELETE(m_allocator, m_log_ui);
		Lumix::WorldEditor::destroy(m_editor, m_allocator);
		Lumix::Engine::destroy(m_engine, m_allocator);
		m_engine = nullptr;
		m_editor = nullptr;

		PlatformInterface::shutdown();
	}


	void initIMGUI()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->AddFontFromFileTTF("bin/VeraMono.ttf", 13);

		io.KeyMap[ImGuiKey_Tab] = (int)PlatformInterface::Keys::TAB;
		io.KeyMap[ImGuiKey_LeftArrow] = (int)PlatformInterface::Keys::LEFT;
		io.KeyMap[ImGuiKey_RightArrow] = (int)PlatformInterface::Keys::RIGHT;
		io.KeyMap[ImGuiKey_UpArrow] = (int)PlatformInterface::Keys::UP;
		io.KeyMap[ImGuiKey_DownArrow] = (int)PlatformInterface::Keys::DOWN;
		io.KeyMap[ImGuiKey_PageUp] = (int)PlatformInterface::Keys::PAGE_UP;
		io.KeyMap[ImGuiKey_PageDown] = (int)PlatformInterface::Keys::PAGE_DOWN;
		io.KeyMap[ImGuiKey_Home] = (int)PlatformInterface::Keys::HOME;
		io.KeyMap[ImGuiKey_End] = (int)PlatformInterface::Keys::END;
		io.KeyMap[ImGuiKey_Delete] = (int)PlatformInterface::Keys::DEL;
		io.KeyMap[ImGuiKey_Backspace] = (int)PlatformInterface::Keys::BACKSPACE;
		io.KeyMap[ImGuiKey_Enter] = (int)PlatformInterface::Keys::ENTER;
		io.KeyMap[ImGuiKey_Escape] = (int)PlatformInterface::Keys::ESCAPE;
		io.KeyMap[ImGuiKey_A] = 'A';
		io.KeyMap[ImGuiKey_C] = 'C';
		io.KeyMap[ImGuiKey_V] = 'V';
		io.KeyMap[ImGuiKey_X] = 'X';
		io.KeyMap[ImGuiKey_Y] = 'Y';
		io.KeyMap[ImGuiKey_Z] = 'Z';
	}


	void loadSettings()
	{
		m_settings.load(&m_actions[0], m_actions.size());

		m_asset_browser->m_is_opened = m_settings.m_is_asset_browser_opened;
		m_is_entity_list_opened = m_settings.m_is_entity_list_opened;
		m_is_entity_template_list_opened = m_settings.m_is_entity_template_list_opened;
		m_log_ui->m_is_opened = m_settings.m_is_log_opened;
		m_profiler_ui->m_is_opened = m_settings.m_is_profiler_opened;
		m_property_grid->m_is_opened = m_settings.m_is_properties_opened;

		if (m_settings.m_is_maximized)
		{
			PlatformInterface::maximizeWindow();
		}
		else if (m_settings.m_window.w > 0)
		{
			PlatformInterface::moveWindow(m_settings.m_window.x,
				m_settings.m_window.y,
				m_settings.m_window.w,
				m_settings.m_window.h);
		}
	}


	void addActions()
	{
		addAction<&StudioAppImpl::newUniverse>("New", "newUniverse");
		addAction<&StudioAppImpl::save>("Save", "save", (int)PlatformInterface::Keys::CONTROL, 'S', -1);
		addAction<&StudioAppImpl::saveAs>("Save As",
			"saveAs",
			(int)PlatformInterface::Keys::CONTROL,
			(int)PlatformInterface::Keys::SHIFT,
			'S');
		addAction<&StudioAppImpl::exit>("Exit", "exit", (int)PlatformInterface::Keys::CONTROL, 'X', -1);

		addAction<&StudioAppImpl::redo>("Redo",
			"redo",
			(int)PlatformInterface::Keys::CONTROL,
			(int)PlatformInterface::Keys::SHIFT,
			'Z');
		addAction<&StudioAppImpl::undo>("Undo", "undo", (int)PlatformInterface::Keys::CONTROL, 'Z', -1);
		addAction<&StudioAppImpl::copy>("Copy", "copy", (int)PlatformInterface::Keys::CONTROL, 'C', -1);
		addAction<&StudioAppImpl::paste>(
			"Paste", "paste", (int)PlatformInterface::Keys::CONTROL, 'V', -1);
		addAction<&StudioAppImpl::toggleOrbitCamera>("Orbit camera", "orbitCamera");
		addAction<&StudioAppImpl::toggleGizmoMode>("Translate/Rotate", "toggleGizmoMode");
		addAction<&StudioAppImpl::setTopView>("Top", "viewTop");
		addAction<&StudioAppImpl::setFrontView>("Front", "viewFront");
		addAction<&StudioAppImpl::setSideView>("Side", "viewSide");
		addAction<&StudioAppImpl::togglePivotMode>("Center/Pivot", "togglePivotMode");
		addAction<&StudioAppImpl::toggleCoordSystem>("Local/Global", "toggleCoordSystem");

		addAction<&StudioAppImpl::createEntity>("Create", "createEntity");
		addAction<&StudioAppImpl::destroyEntity>(
			"Destroy", "destroyEntity", (int)PlatformInterface::Keys::DEL, -1, -1);
		addAction<&StudioAppImpl::showEntities>("Show", "showEntities");
		addAction<&StudioAppImpl::hideEntities>("Hide", "hideEntities");

		addAction<&StudioAppImpl::toggleGameMode>("Game Mode", "toggleGameMode");
		addAction<&StudioAppImpl::toggleMeasure>("Toggle measure", "toggleMeasure");
		addAction<&StudioAppImpl::autosnapDown>("Autosnap down", "autosnapDown");
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
			auto* f = (void(*)(StudioApp&))Lumix::getLibrarySymbol(lib, "setStudioApp");
			if (f) f(*this);
		}
		#endif
		
	}


	void runScript(const char* src, const char* script_name) override
	{
		bool errors =
			luaL_loadbuffer(m_lua_state, src, Lumix::stringLength(src), script_name) != LUA_OK;
		errors = errors || lua_pcall(m_lua_state, 0, LUA_MULTRET, 0) != LUA_OK;
		if (errors)
		{
			Lumix::g_log_error.log("Editor") << script_name << ": " << lua_tostring(m_lua_state, -1);
			lua_pop(m_lua_state, 1);
		}
	}


	void LUA_exit(int exit_code)
	{
		m_finished = true;
		m_exit_code = exit_code;
	}


	void LUA_logError(const char* message)
	{
		Lumix::g_log_error.log("Editor") << message;
	}


	void LUA_logInfo(const char* message)
	{
		Lumix::g_log_info.log("Editor") << message;
	}


	bool LUA_runTest(const char* undo_stack_path, const char* result_universe_path)
	{
		return m_editor->runTest(Lumix::Path(undo_stack_path), Lumix::Path(result_universe_path));
	}


	void registerLuaFunction(const char* name, int (*function)(struct lua_State*)) override
	{
		lua_pushcfunction(m_lua_state, function);
		lua_setglobal(m_lua_state, name);
	}


	void registerLuaGlobal(const char* name, void* data) override
	{
		lua_pushlightuserdata(m_lua_state, data);
		lua_setglobal(m_lua_state, name);
	}


	void createLua()
	{
		m_lua_state = luaL_newstate();
		luaL_openlibs(m_lua_state);

		lua_pushlightuserdata(m_lua_state, this);
		lua_setglobal(m_lua_state, "g_editor");

		lua_newtable(m_lua_state);
		lua_pushvalue(m_lua_state, -1);
		lua_setglobal(m_lua_state, "Editor");

		#define REGISTER_FUNCTION(F, name) \
			do { \
				lua_pushvalue(m_lua_state, -1); \
				auto* f = &Lumix::LuaWrapper::wrapMethod<StudioAppImpl, decltype(&StudioAppImpl::F), &StudioAppImpl::F>; \
				lua_pushcfunction(m_lua_state, f); \
				lua_setfield(m_lua_state, -2, name); \
			} while(false) \

		REGISTER_FUNCTION(LUA_runTest, "runTest");
		REGISTER_FUNCTION(LUA_logError, "logError");
		REGISTER_FUNCTION(LUA_logInfo, "logInfo");
		REGISTER_FUNCTION(LUA_exit, "exit");

		#undef REGISTER_FUNCTION

		lua_pop(m_lua_state, 1);
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


	void run()
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
					m_finished = !PlatformInterface::processSystemEvents();
					if (!m_finished) update();
					frame_time = timer->tick();
				}

				float wanted_fps = PlatformInterface::isWindowActive() ? 60.0f : 5.0f;
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


	struct SystemEventHandler : public PlatformInterface::SystemEventHandler
	{
		void onWindowTransformed(int x, int y, int w, int h) override
		{
			m_app->onWindowTransformed(x, y, w, h);
		}


		void onMouseLeftWindow() override { m_app->clearInputs(); }


		void onMouseMove(int x, int y, int rel_x, int rel_y) override
		{
			m_mouse_x = x;
			m_mouse_y = y;
			auto& input_system = m_app->m_editor->getEngine().getInputSystem();
			input_system.injectMouseXMove(float(rel_x));
			input_system.injectMouseYMove(float(rel_y));

			ImGuiIO& io = ImGui::GetIO();
			io.MousePos.x = (float)x;
			io.MousePos.y = (float)y;
		}


		void onMouseWheel(int amount) override
		{
			ImGui::GetIO().MouseWheel = amount / 600.0f;
		}


		void onMouseButtonDown(MouseButton button) override
		{
			switch (button)
			{
				case PlatformInterface::SystemEventHandler::MouseButton::LEFT:
					m_app->m_editor->setAdditiveSelection(ImGui::GetIO().KeyCtrl);
					m_app->m_editor->setSnapMode(ImGui::GetIO().KeyShift);
					ImGui::GetIO().MouseDown[0] = true;
					break;
				case PlatformInterface::SystemEventHandler::MouseButton::RIGHT:
					ImGui::GetIO().MouseDown[1] = true;
					break;
				case PlatformInterface::SystemEventHandler::MouseButton::MIDDLE:
					ImGui::GetIO().MouseDown[2] = true;
					break;
			}
		}


		void onMouseButtonUp(MouseButton button) override
		{
			switch (button)
			{
				case PlatformInterface::SystemEventHandler::MouseButton::LEFT:
					ImGui::GetIO().MouseDown[0] = false;
					break;
				case PlatformInterface::SystemEventHandler::MouseButton::RIGHT:
					ImGui::GetIO().MouseDown[1] = false;
					break;
				case PlatformInterface::SystemEventHandler::MouseButton::MIDDLE:
					ImGui::GetIO().MouseDown[2] = false;
					break;
			}
		}


		void onKeyDown(int key) override
		{
			ImGui::GetIO().KeysDown[key] = true;
			m_app->checkShortcuts();
		}


		void onKeyUp(int key) override
		{
			ImGui::GetIO().KeysDown[key] = false;
		}


		void onChar(int key)
		{
			ImGui::GetIO().AddInputCharacter(key);
		}


		int m_mouse_x;
		int m_mouse_y;
		StudioAppImpl* m_app;
	};


	SystemEventHandler m_handler;


	void init()
	{
		createLua();
		checkWorkingDirector();
		m_handler.m_app = this;
		PlatformInterface::createWindow(nullptr);

		char current_dir[Lumix::MAX_PATH_LENGTH];
		PlatformInterface::getCurrentDirectory(current_dir, Lumix::lengthOf(current_dir));

		char base_path2[Lumix::MAX_PATH_LENGTH] = {};
		checkDataDirCommandLine(base_path2, Lumix::lengthOf(base_path2));
		m_engine = Lumix::Engine::create(current_dir, base_path2, nullptr, m_allocator);
		Lumix::Engine::PlatformData platform_data;
		platform_data.window_handle = PlatformInterface::getWindowHandle();
		m_engine->setPlatformData(platform_data);
		m_editor = Lumix::WorldEditor::create(current_dir, *m_engine, m_allocator);
		loadUserPlugins();

		addActions();

		m_asset_browser = LUMIX_NEW(m_allocator, AssetBrowser)(*m_editor, m_metadata);
		m_property_grid = LUMIX_NEW(m_allocator, PropertyGrid)(*m_editor, *m_asset_browser, m_actions);
		auto engine_allocator = static_cast<Lumix::Debug::Allocator*>(&m_engine->getAllocator());
		m_profiler_ui = ProfilerUI::create(*m_engine);
		m_log_ui = LUMIX_NEW(m_allocator, LogUI)(m_editor->getAllocator());
		m_import_asset_dialog = LUMIX_NEW(m_allocator, ImportAssetDialog)(*m_editor, m_metadata);

		initIMGUI();

		PlatformInterface::setSystemEventHandler(&m_handler);
		loadSettings();

		if (!m_metadata.load()) Lumix::g_log_info.log("Editor") << "Could not load metadata";

		setStudioApp();
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


	void onWindowTransformed(int x, int y, int width, int height)
	{
		if (height == 0) return;

		m_settings.m_window.x = x;
		m_settings.m_window.y = y;
		m_settings.m_window.w = width;
		m_settings.m_window.h = height;
		m_settings.m_is_maximized = PlatformInterface::isMaximized();
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
	

	Lumix::WorldEditor* getWorldEditor() override
	{
		return m_editor;
	}


	Lumix::DefaultAllocator m_allocator;
	Lumix::Engine* m_engine;

	float m_time_to_autosave;
	Lumix::Array<Action*> m_actions;
	Lumix::Array<IPlugin*> m_plugins;
	Lumix::WorldEditor* m_editor;
	AssetBrowser* m_asset_browser;
	PropertyGrid* m_property_grid;
	LogUI* m_log_ui;
	ProfilerUI* m_profiler_ui;
	ImportAssetDialog* m_import_asset_dialog;
	Lumix::string m_selected_template_name;
	Settings m_settings;
	Metadata m_metadata;
	lua_State* m_lua_state;
	char m_template_name[100];

	bool m_finished;
	int m_exit_code;

	bool m_is_welcome_screen_opened;
	bool m_is_entity_list_opened;
	bool m_is_entity_template_list_opened;
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
		if (Lumix::compareString(name, i->name) == 0)
		{
			i->creator(app);
			return;
		}
		i = i->next;
	}
}