#include "audio/audio_scene.h"
#include "audio/clip_manager.h"
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
#include "debug/debug.h"
#include "editor/gizmo.h"
#include "editor/entity_groups.h"
#include "editor/entity_template_system.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "engine/plugin_manager.h"
#include "game_view.h"
#include "import_asset_dialog.h"
#include "log_ui.h"
#include "metadata.h"
#include "imgui/imgui.h"
#include "platform_interface.h"
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
#include "shader_editor.h"
#include "studio_app.h"
#include "utils.h"

#include <bgfx/bgfx.h>


static void imGuiCallback(ImDrawData* draw_data);


class StudioAppImpl* g_app;


class StudioAppImpl : public StudioApp
{
public:
	StudioAppImpl()
		: m_is_entity_list_opened(true)
		, m_finished(false)
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
		, m_shader_editor(nullptr)
		, m_editor(nullptr)
		, m_settings(m_allocator)
		, m_plugins(m_allocator)
	{
		g_app = this;
		m_template_name[0] = '\0';
		init();
	}


	~StudioAppImpl()
	{
		shutdown();
		g_app = nullptr;
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


	AssetBrowser* getAssetBrowser() override
	{
		return m_asset_browser;
	}

	
	PropertyGrid* getPropertyGrid() override
	{
		return m_property_grid;
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

			if (ImGui::Button("Close")) m_is_welcome_screen_opened = false;
		}
		ImGui::End();
	}


	void onGUI()
	{
		PROFILE_FUNCTION();

		if (!m_gui_pipeline_source->isReady()) return;

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
			m_profiler_ui->onGUI();
			m_asset_browser->onGUI();
			m_log_ui->onGUI();
			m_import_asset_dialog->onGUI();
			m_property_grid->onGUI();
			showEntityList();
			showEntityTemplateList();
			m_sceneview.onGUI();
			m_gameview.onGui();
			m_shader_editor->onGUI();
			for (auto* plugin : m_plugins)
			{
				plugin->onWindowGUI();
			}
			m_settings.onGUI(&m_actions[0], m_actions.size());
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

	void undo() { m_shader_editor->isFocused() ? m_shader_editor->undo() : m_editor->undo(); }
	void redo() { m_shader_editor->isFocused() ? m_shader_editor->redo() :m_editor->redo(); }
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
		if (PlatformInterface::getOpenFilename(filename, Lumix::lengthOf(filename), "JSON files\0*.json\0"))
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
					ImGui::MenuItem("Log", nullptr, &m_log_ui->m_is_opened);
					ImGui::MenuItem("Profiler", nullptr, &m_profiler_ui->m_is_opened);
					ImGui::MenuItem("Properties", nullptr, &m_property_grid->m_is_opened);
					ImGui::MenuItem("Settings", nullptr, &m_settings.m_is_opened);
					ImGui::MenuItem("Shader editor", nullptr, &m_shader_editor->m_is_opened);
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
				ImGui::EndMenu();
			}
			StringBuilder<100> stats("");
			if (m_engine->getFileSystem().hasWork()) stats << "Loading... | ";
			stats << "FPS: ";
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
			int scroll_to = -1;

			auto& groups = m_editor->getEntityGroups();
			static char group_name[20] = "";
			ImGui::InputText("New group name", group_name, Lumix::lengthOf(group_name));
			if(ImGui::Button("Create group"))
			{
				if(group_name[0] == 0)
				{
					Lumix::g_log_error.log("editor") << "Group name can not be empty";
				}
				else if(groups.getGroup(group_name) != -1)
				{
					Lumix::g_log_error.log("editor") << "Group with name " << group_name << " already exists";
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
		m_settings.m_is_gameview_opened = m_gameview.m_is_opened;
		m_settings.m_is_log_opened = m_log_ui->m_is_opened;
		m_settings.m_is_profiler_opened = m_profiler_ui->m_is_opened;
		m_settings.m_is_properties_opened = m_property_grid->m_is_opened;
		m_settings.m_is_shader_editor_opened = m_shader_editor->m_is_opened;

		m_settings.save(&m_actions[0], m_actions.size());

		if (!m_metadata.save())
		{
			Lumix::g_log_warning.log("studio") << "Could not save metadata";
		}
	}


	void shutdown()
	{
		for (auto* plugin : m_plugins)
		{
			LUMIX_DELETE(m_editor->getAllocator(), plugin);
		}
		m_plugins.clear();

		saveSettings();

		for (auto* a : m_actions)
		{
			LUMIX_DELETE(m_editor->getAllocator(), a);
		}
		m_actions.clear();

		shutdownImGui();

		ProfilerUI::destroy(*m_profiler_ui);
		LUMIX_DELETE(m_allocator, m_asset_browser);
		LUMIX_DELETE(m_allocator, m_log_ui);
		LUMIX_DELETE(m_allocator, m_property_grid);
		LUMIX_DELETE(m_allocator, m_import_asset_dialog);
		LUMIX_DELETE(m_allocator, m_shader_compiler);
		LUMIX_DELETE(m_allocator, m_shader_editor);
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
		m_shader_editor = nullptr;

		PlatformInterface::shutdown();
	}


	void shutdownImGui()
	{
		ImGui::Shutdown();

		Lumix::Texture* texture = m_material->getTexture(0);
		m_material->setTexture(0, nullptr);
		texture->destroy();
		LUMIX_DELETE(m_allocator, texture);

		m_material->getResourceManager().get(Lumix::ResourceManager::MATERIAL)->unload(*m_material);
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

		io.RenderDrawListsFn = ::imGuiCallback;

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

		ImGui::GetStyle().WindowFillAlphaDefault = 1.0f;
	}


	void onUniverseCreated()
	{
		auto* scene =
			static_cast<Lumix::RenderScene*>(m_editor->getScene(Lumix::crc32("renderer")));

		m_gui_pipeline->setScene(scene);
	}


	void onUniverseDestroyed()
	{
		m_gui_pipeline->setScene(nullptr);
	}


	void loadSettings()
	{
		m_settings.load(&m_actions[0], m_actions.size());

		m_asset_browser->m_is_opened = m_settings.m_is_asset_browser_opened;
		m_is_entity_list_opened = m_settings.m_is_entity_list_opened;
		m_is_entity_template_list_opened = m_settings.m_is_entity_template_list_opened;
		m_gameview.m_is_opened = m_settings.m_is_gameview_opened;
		m_log_ui->m_is_opened = m_settings.m_is_log_opened;
		m_profiler_ui->m_is_opened = m_settings.m_is_profiler_opened;
		m_property_grid->m_is_opened = m_settings.m_is_properties_opened;
		m_shader_editor->m_is_opened = m_settings.m_is_shader_editor_opened;

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

		addAction<&StudioAppImpl::setWireframe>("Wireframe", "setWireframe");
		addAction<&StudioAppImpl::toggleStats>("Stats", "toggleStats"); 
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
		for (auto* lib : plugin_manager.getLibraries())
		{
			auto* f = (void (*)(StudioApp&))Lumix::getLibrarySymbol(lib, "setStudioApp");
			if (f) f(*this);
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
					m_finished = !PlatformInterface::processSystemEvents();
					update();
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

			if (m_app->m_gameview.isMouseCaptured()) return;

			m_app->m_sceneview.onMouseMove(x, y, rel_x, rel_y);

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
					if (!m_app->m_sceneview.onMouseDown(
							m_mouse_x, m_mouse_y, Lumix::MouseButton::LEFT) &&
						!m_app->m_gameview.isMouseCaptured())
					{
						ImGui::GetIO().MouseDown[0] = true;
					}
					break;
				case PlatformInterface::SystemEventHandler::MouseButton::RIGHT:
					if(!m_app->m_sceneview.onMouseDown(
						m_mouse_x, m_mouse_y, Lumix::MouseButton::RIGHT) &&
						!m_app->m_gameview.isMouseCaptured())
					{
						ImGui::GetIO().MouseDown[1] = true;
					}
					break;
				case PlatformInterface::SystemEventHandler::MouseButton::MIDDLE:
					if(!m_app->m_sceneview.onMouseDown(
						m_mouse_x, m_mouse_y, Lumix::MouseButton::MIDDLE) &&
						!m_app->m_gameview.isMouseCaptured())
					{
						ImGui::GetIO().MouseDown[2] = true;
					}
					break;
			}
		}


		void onMouseButtonUp(MouseButton button) override
		{
			switch (button)
			{
				case PlatformInterface::SystemEventHandler::MouseButton::LEFT:
					m_app->m_sceneview.onMouseUp(Lumix::MouseButton::LEFT);
					ImGui::GetIO().MouseDown[0] = false;
					break;
				case PlatformInterface::SystemEventHandler::MouseButton::RIGHT:
					m_app->m_sceneview.onMouseUp(Lumix::MouseButton::RIGHT);
					ImGui::GetIO().MouseDown[1] = false;
					break;
				case PlatformInterface::SystemEventHandler::MouseButton::MIDDLE:
					m_app->m_sceneview.onMouseUp(Lumix::MouseButton::MIDDLE);
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
		checkWorkingDirector();
		m_handler.m_app = this;
		PlatformInterface::createWindow(nullptr);

		m_engine = Lumix::Engine::create(nullptr, m_allocator);
		Lumix::Engine::PlatformData platform_data;
		platform_data.window_handle = PlatformInterface::getWindowHandle();
		m_engine->setPlatformData(platform_data);
		char current_dir[Lumix::MAX_PATH_LENGTH];
		PlatformInterface::getCurrentDirectory(current_dir, Lumix::lengthOf(current_dir));
		m_editor = Lumix::WorldEditor::create(current_dir, *m_engine, m_allocator);
		loadUserPlugins();

		addActions();

		m_asset_browser = LUMIX_NEW(m_allocator, AssetBrowser)(*m_editor, m_metadata);
		m_property_grid = LUMIX_NEW(m_allocator, PropertyGrid)(*m_editor, *m_asset_browser, m_actions);
		auto engine_allocator = static_cast<Lumix::Debug::Allocator*>(&m_engine->getAllocator());
		m_profiler_ui = ProfilerUI::create(*m_engine);
		m_log_ui = LUMIX_NEW(m_allocator, LogUI)(m_editor->getAllocator());
		m_import_asset_dialog = LUMIX_NEW(m_allocator, ImportAssetDialog)(*m_editor, m_metadata);
		m_shader_compiler = LUMIX_NEW(m_allocator, ShaderCompiler)(*m_editor, *m_log_ui);
		m_shader_editor = LUMIX_NEW(m_allocator, ShaderEditor)(m_editor->getAllocator());

		m_editor->universeCreated().bind<StudioAppImpl, &StudioAppImpl::onUniverseCreated>(this);
		m_editor->universeDestroyed().bind<StudioAppImpl, &StudioAppImpl::onUniverseDestroyed>(this);

		auto* pipeline_manager =
			m_engine->getResourceManager().get(Lumix::ResourceManager::PIPELINE);

		m_gui_pipeline_source = static_cast<Lumix::Pipeline*>(
			pipeline_manager->load(Lumix::Path("pipelines/imgui.lua")));
		m_gui_pipeline =
			Lumix::PipelineInstance::create(*m_gui_pipeline_source, m_engine->getAllocator());

		m_sceneview.init(*m_editor, m_actions);
		m_gameview.init(*m_editor);

		int w = PlatformInterface::getWindowWidth();
		int h = PlatformInterface::getWindowHeight();
		m_gui_pipeline->setViewport(0, 0, w, h);
		auto& plugin_manager = m_editor->getEngine().getPluginManager();
		auto* renderer = static_cast<Lumix::Renderer*>(plugin_manager.getPlugin("renderer"));
		renderer->resize(w, h);
		onUniverseCreated();
		initIMGUI();

		PlatformInterface::setSystemEventHandler(&m_handler);
		loadSettings();

		if (!m_metadata.load()) Lumix::g_log_info.log("studio") << "Could not load metadata";

		void registerProperties(Lumix::WorldEditor&);
		registerProperties(*m_editor);

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

		if (m_gui_pipeline) m_gui_pipeline->setViewport(0, 0, width, height);
		if (!m_editor) return;

		auto& plugin_manager = m_editor->getEngine().getPluginManager();
		auto* renderer =
			static_cast<Lumix::Renderer*>(plugin_manager.getPlugin("renderer"));
		if(renderer) renderer->resize(width, height);
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


	void setGUIProjection()
	{
		float width = ImGui::GetIO().DisplaySize.x;
		float height = ImGui::GetIO().DisplaySize.y;
		Lumix::Matrix ortho;
		ortho.setOrtho(0.0f, width, 0.0f, height, -1.0f, 1.0f);
		m_gui_pipeline->setViewProjection(ortho, (int)width, (int)height);
	}


	void drawGUICmdList(ImDrawList* cmd_list)
	{
		Lumix::Renderer* renderer =
			static_cast<Lumix::Renderer*>(m_engine->getPluginManager().getPlugin("renderer"));

		Lumix::TransientGeometry geom(&cmd_list->VtxBuffer[0],
			cmd_list->VtxBuffer.size(),
			renderer->getBasic2DVertexDecl(),
			&cmd_list->IdxBuffer[0],
			cmd_list->IdxBuffer.size());

		if (geom.getNumVertices() < 0) return;

		Lumix::uint32 elem_offset = 0;
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

			if (0 == pcmd->ElemCount) continue;

			m_gui_pipeline->setScissor(
				Lumix::uint16(Lumix::Math::maxValue(pcmd->ClipRect.x, 0.0f)),
				Lumix::uint16(Lumix::Math::maxValue(pcmd->ClipRect.y, 0.0f)),
				Lumix::uint16(Lumix::Math::minValue(pcmd->ClipRect.z, 65535.0f) -
				Lumix::Math::maxValue(pcmd->ClipRect.x, 0.0f)),
				Lumix::uint16(Lumix::Math::minValue(pcmd->ClipRect.w, 65535.0f) -
				Lumix::Math::maxValue(pcmd->ClipRect.y, 0.0f)));

			auto material = m_material;
			int pass_idx = m_gui_pipeline->getPassIdx();
			const auto& texture_id = pcmd->TextureId
				? *(bgfx::TextureHandle*)pcmd->TextureId
				: material->getTexture(0)->getTextureHandle();
			auto texture_uniform = material->getShader()->getTextureSlot(0).m_uniform_handle;
			m_gui_pipeline->setTexture(0, texture_id, texture_uniform);
			m_gui_pipeline->render(geom,
				Lumix::Matrix::IDENTITY,
				elem_offset,
				pcmd->ElemCount,
				material->getRenderStates(),
				material->getShaderInstance().m_program_handles[pass_idx]);

			elem_offset += pcmd->ElemCount;
		}
	}


	void imGuiCallback(ImDrawData* draw_data)
	{
		PROFILE_FUNCTION();
		if (!m_material || !m_material->isReady()) return;
		if (!m_material->getTexture(0)) return;

		setGUIProjection();

		for (int i = 0; i < draw_data->CmdListsCount; ++i)
		{
			ImDrawList* cmd_list = draw_data->CmdLists[i];
			drawGUICmdList(cmd_list);
		}
	}


	Lumix::WorldEditor* getWorldEditor() override
	{
		return m_editor;
	}


	Lumix::DefaultAllocator m_allocator;
	Lumix::Material* m_material;
	Lumix::Engine* m_engine;

	SceneView m_sceneview;
	GameView m_gameview;

	Lumix::Pipeline* m_gui_pipeline_source;
	Lumix::PipelineInstance* m_gui_pipeline;

	float m_time_to_autosave;
	Lumix::Array<Action*> m_actions;
	Lumix::Array<IPlugin*> m_plugins;
	Lumix::WorldEditor* m_editor;
	AssetBrowser* m_asset_browser;
	PropertyGrid* m_property_grid;
	LogUI* m_log_ui;
	ProfilerUI* m_profiler_ui;
	ImportAssetDialog* m_import_asset_dialog;
	ShaderCompiler* m_shader_compiler;
	Lumix::string m_selected_template_name;
	Settings m_settings;
	Metadata m_metadata;
	ShaderEditor* m_shader_editor;
	char m_template_name[100];

	bool m_finished;

	bool m_is_welcome_screen_opened;
	bool m_is_entity_list_opened;
	bool m_is_entity_template_list_opened;
	bool m_is_wireframe;
};


static void imGuiCallback(ImDrawData* draw_data)
{
	g_app->imGuiCallback(draw_data);
}


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