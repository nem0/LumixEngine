#include "studio_app.h"
#include "asset_browser.h"
#include "audio/audio_scene.h"
#include "audio/clip_manager.h"
#include "editor/gizmo.h"
#include "editor/prefab_system.h"
#include "editor/render_interface.h"
#include "editor/world_editor.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/debug/debug.h"
#include "engine/default_allocator.h"
#include "engine/engine.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/file_system.h"
#include "engine/fs/os_file.h"
#include "engine/input_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/mt/thread.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/quat.h"
#include "engine/resource_manager.h"
#include "engine/system.h"
#include "engine/timer.h"
#include "engine/universe/universe.h"
#include "imgui/imgui.h"
#include "log_ui.h"
#include "metadata.h"
#include "platform_interface.h"
#include "profiler_ui.h"
#include "property_grid.h"
#include "settings.h"
#include "utils.h"
#include <SDL.h>
#include <SDL_syswm.h>


namespace Lumix
{


struct LuaPlugin : public StudioApp::IPlugin
{
	LuaPlugin(StudioApp& app, const char* src, const char* filename)
		: editor(app.getWorldEditor())
	{
		L = lua_newthread(editor.getEngine().getState()); // [thread]
		thread_ref = luaL_ref(editor.getEngine().getState(), LUA_REGISTRYINDEX); // []

		lua_newtable(L); // [env]
		 // reference environment
		lua_pushvalue(L, -1); // [env, env]
		env_ref = luaL_ref(L, LUA_REGISTRYINDEX); // [env]

		// environment's metatable & __index
		lua_pushvalue(L, -1); // [env, env]
		lua_setmetatable(L, -2); // [env]
		lua_pushglobaltable(L); // [evn, _G]
		lua_setfield(L, -2, "__index");  // [env]

		bool errors = luaL_loadbuffer(L, src, stringLength(src), filename) != LUA_OK; // [env, func]

		lua_pushvalue(L, -2); // [env, func, env]
		lua_setupvalue(L, -2, 1); // function's environment [env, func]

		errors = errors || lua_pcall(L, 0, 0, 0) != LUA_OK; // [env]
		if (errors)
		{
			g_log_error.log("Editor") << filename << ": " << lua_tostring(L, -1);
			lua_pop(L, 1);
		}
		lua_pop(L, 1); // []

		const char* name = "LuaPlugin";
		if (lua_getglobal(L, "plugin_name") == LUA_TSTRING)
		{
			name = lua_tostring(L, -1);
		}

		Action* action = LUMIX_NEW(editor.getAllocator(), Action)(name, name, name);
		action->func.bind<LuaPlugin, &LuaPlugin::onAction>(this);
		app.addWindowAction(action);
		m_is_open = false;

		lua_pop(L, 1); // plugin_name
	}


	~LuaPlugin()
	{
		lua_State* L = editor.getEngine().getState();
		luaL_unref(L, LUA_REGISTRYINDEX, env_ref);
		luaL_unref(L, LUA_REGISTRYINDEX, thread_ref);
	}


	const char* getName() const override { return "lua_script"; }


	void onAction()
	{
		m_is_open = !m_is_open;
	}


	void onWindowGUI() override
	{
		if (!m_is_open) return;
		if (lua_getglobal(L, "onGUI") == LUA_TFUNCTION)
		{
			if (lua_pcall(L, 0, 0, 0) != LUA_OK)
			{
				g_log_error.log("Editor") << "LuaPlugin:" << lua_tostring(L, -1);
				lua_pop(L, 1);
			}
		}
		else
		{
			lua_pop(L, 1);
		}
	}

	WorldEditor& editor;
	lua_State* L;
	int thread_ref;
	int env_ref;
	bool m_is_open;
};


class StudioAppImpl LUMIX_FINAL : public StudioApp
{
public:
	StudioAppImpl()
		: m_is_entity_list_open(true)
		, m_is_save_as_dialog_open(false)
		, m_finished(false)
		, m_deferred_game_mode_exit(false)
		, m_profiler_ui(nullptr)
		, m_asset_browser(nullptr)
		, m_property_grid(nullptr)
		, m_actions(m_allocator)
		, m_window_actions(m_allocator)
		, m_toolbar_actions(m_allocator)
		, m_metadata(m_allocator)
		, m_is_welcome_screen_open(true)
		, m_is_pack_data_dialog_open(false)
		, m_editor(nullptr)
		, m_settings(*this)
		, m_plugins(m_allocator)
		, m_add_cmp_plugins(m_allocator)
		, m_component_labels(m_allocator)
		, m_confirm_load(false)
		, m_confirm_new(false)
		, m_confirm_exit(false)
		, m_exit_code(0)
		, m_allocator(m_main_allocator)
		, m_universes(m_allocator)
		, m_events(m_allocator)
	{
		m_add_cmp_root.label[0] = '\0';
		m_template_name[0] = '\0';
		m_open_filter[0] = '\0';
		SDL_SetMainReady();
		SDL_Init(SDL_INIT_VIDEO);

		checkWorkingDirector();
		
		char current_dir[MAX_PATH_LENGTH];
		PlatformInterface::getCurrentDirectory(current_dir, lengthOf(current_dir));

		char data_dir_path[MAX_PATH_LENGTH] = {};
		checkDataDirCommandLine(data_dir_path, lengthOf(data_dir_path));
		m_engine = Engine::create(current_dir, data_dir_path, nullptr, m_allocator);
		createLua();

		m_window = SDL_CreateWindow("Lumix Studio", 0, 0, 800, 600, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
		PlatformInterface::setWindow(m_window);
		SDL_SysWMinfo window_info;
		SDL_VERSION(&window_info.version);
		SDL_GetWindowWMInfo(m_window, &window_info);
		Engine::PlatformData platform_data = {};
		#ifdef _WIN32
			platform_data.window_handle = window_info.info.win.window;
		#elif defined(__linux__)
			platform_data.window_handle = (void*)(uintptr_t)window_info.info.x11.window;
			platform_data.display = window_info.info.x11.display;
		#endif
		m_engine->setPlatformData(platform_data);

		m_editor = WorldEditor::create(current_dir, *m_engine, m_allocator);
		m_settings.m_editor = m_editor;
		scanUniverses();
		loadUserPlugins();
		addActions();

		m_asset_browser = LUMIX_NEW(m_allocator, AssetBrowser)(*this);
		m_property_grid = LUMIX_NEW(m_allocator, PropertyGrid)(*this);
		m_profiler_ui = ProfilerUI::create(*m_engine);
		m_log_ui = LUMIX_NEW(m_allocator, LogUI)(m_editor->getAllocator());

		ImGui::CreateContext();
		loadSettings();
		initIMGUI();
		#ifdef _WIN32
			ImGui::GetIO().ImeWindowHandle = window_info.info.win.window;
		#endif

		m_custom_pivot_action = LUMIX_NEW(m_editor->getAllocator(), Action)(
			"Set Custom Pivot", "Set Custom Pivot", "set_custom_pivot", SDLK_v, -1, -1);
		m_custom_pivot_action->is_global = false;
		addAction(m_custom_pivot_action);

		if (!m_metadata.load()) g_log_info.log("Editor") << "Could not load metadata";

		setStudioApp();
		loadIcons();
		loadSettings();
		loadUniverseFromCommandLine();
		findLuaPlugins("plugins/lua/");

		m_asset_browser->onInitFinished();
		m_sleep_when_inactive = shouldSleepWhenInactive();
	}


	~StudioAppImpl()
	{
		saveSettings();
		unloadIcons();

		while (m_editor->getEngine().getFileSystem().hasWork())
		{
			m_editor->getEngine().getFileSystem().updateAsyncTransactions();
		}

		m_editor->newUniverse();

		destroyAddCmpTreeNode(m_add_cmp_root.child);

		for (auto* plugin : m_plugins)
		{
			LUMIX_DELETE(m_editor->getAllocator(), plugin);
		}
		m_plugins.clear();

		for (auto* i : m_add_cmp_plugins)
		{
			LUMIX_DELETE(m_editor->getAllocator(), i);
		}
		m_add_cmp_plugins.clear();

		for (auto* a : m_actions)
		{
			LUMIX_DELETE(m_editor->getAllocator(), a);
		}
		m_actions.clear();

		ProfilerUI::destroy(*m_profiler_ui);
		LUMIX_DELETE(m_allocator, m_asset_browser);
		LUMIX_DELETE(m_allocator, m_property_grid);
		LUMIX_DELETE(m_allocator, m_log_ui);
		WorldEditor::destroy(m_editor, m_allocator);
		Engine::destroy(m_engine, m_allocator);
		m_engine = nullptr;
		m_editor = nullptr;

		SDL_DestroyWindow(m_window);
		SDL_Quit();
	}


	bool makeFile(const char* path, const char* content) override
	{
		FS::OsFile file;
		if (!file.open(path, FS::Mode::CREATE_AND_WRITE)) return false;
		bool success = file.writeText(content);
		file.close();
		return success;
	}


	void destroyAddCmpTreeNode(AddCmpTreeNode* node)
	{
		if (!node) return;
		destroyAddCmpTreeNode(node->child);
		destroyAddCmpTreeNode(node->next);
		LUMIX_DELETE(m_allocator, node);
	}


	const char* getComponentTypeName(ComponentType cmp_type) const override
	{
		auto iter = m_component_labels.find(cmp_type);
		if (iter == m_component_labels.end()) return "Unknown";
		return iter.value().c_str();
	}


	const AddCmpTreeNode& getAddComponentTreeRoot() const override { return m_add_cmp_root; }


	void addPlugin(IAddComponentPlugin& plugin)
	{
		int i = 0;
		while (i < m_add_cmp_plugins.size() && compareString(plugin.getLabel(), m_add_cmp_plugins[i]->getLabel()) > 0)
		{
			++i;
		}
		m_add_cmp_plugins.insert(i, &plugin);

		auto* node = LUMIX_NEW(m_allocator, AddCmpTreeNode);
		copyString(node->label, plugin.getLabel());
		node->plugin = &plugin;
		insertAddCmpNode(m_add_cmp_root, node);
	}


	static void insertAddCmpNodeOrdered(AddCmpTreeNode& parent, AddCmpTreeNode* node)
	{
		if (!parent.child)
		{
			parent.child = node;
			return;
		}
		if (compareString(parent.child->label, node->label) > 0)
		{
			node->next = parent.child;
			parent.child = node;
			return;
		}
		auto* i = parent.child;
		while (i->next && compareString(i->next->label, node->label) < 0)
		{
			i = i->next;
		}
		node->next = i->next;
		i->next = node;
	}


	void insertAddCmpNode(AddCmpTreeNode& parent, AddCmpTreeNode* node)
	{
		for (auto* i = parent.child; i; i = i->next)
		{
			if (!i->plugin && startsWith(node->label, i->label))
			{
				insertAddCmpNode(*i, node);
				return;
			}
		}
		const char* rest = node->label + stringLength(parent.label);
		if (parent.label[0] != '\0') ++rest; // include '/'
		const char* slash = findSubstring(rest, "/");
		if (!slash)
		{
			insertAddCmpNodeOrdered(parent, node);
			return;
		}
		auto* new_group = LUMIX_NEW(m_allocator, AddCmpTreeNode);
		copyNString(new_group->label, (int)sizeof(new_group->label), node->label, int(slash - node->label));
		insertAddCmpNodeOrdered(parent, new_group);
		insertAddCmpNode(*new_group, node);
	}


	void registerComponentWithResource(const char* type,
		const char* label,
		ResourceType resource_type,
		const Reflection::PropertyBase& property) override
	{
		struct Plugin LUMIX_FINAL : public IAddComponentPlugin
		{
			void onGUI(bool create_entity, bool from_filter) override
			{
				ImGui::SetNextWindowSize(ImVec2(300, 300));
				const char* last = reverseFind(label, nullptr, '/');
				if (!ImGui::BeginMenu(last && !from_filter ? last + 1 : label)) return;
				char buf[MAX_PATH_LENGTH];
				bool create_empty = ImGui::Selectable("Empty", false);
				if (asset_browser->resourceList(buf, lengthOf(buf), resource_type, 0) || create_empty)
				{
					if (create_entity)
					{
						Entity entity = editor->addEntity();
						editor->selectEntities(&entity, 1, false);
					}

					editor->addComponent(type);
					if (!create_empty)
					{
						editor->setProperty(type,
							-1,
							*property,
							&editor->getSelectedEntities()[0],
							editor->getSelectedEntities().size(),
							buf,
							stringLength(buf) + 1);
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndMenu();
			}


			const char* getLabel() const override
			{
				return label;
			}

			PropertyGrid* property_grid;
			AssetBrowser* asset_browser;
			WorldEditor* editor;
			ComponentType type;
			ResourceType resource_type;
			const Reflection::PropertyBase* property;
			char label[50];
		};

		auto& allocator = m_editor->getAllocator();
		auto* plugin = LUMIX_NEW(allocator, Plugin);
		plugin->property_grid = m_property_grid;
		plugin->asset_browser = m_asset_browser;
		plugin->type = Reflection::getComponentType(type);
		plugin->editor = m_editor;
		plugin->property = &property;
		plugin->resource_type = resource_type;
		copyString(plugin->label, label);
		addPlugin(*plugin);

		m_component_labels.insert(plugin->type, string(label, m_allocator));
	}


	void registerComponent(const char* id, IAddComponentPlugin& plugin) override
	{
		addPlugin(plugin);
		m_component_labels.insert(Reflection::getComponentType(id), string(plugin.getLabel(), m_allocator));
	}


	void registerComponent(const char* type, const char* label) override
	{
		struct Plugin LUMIX_FINAL : public IAddComponentPlugin
		{
			void onGUI(bool create_entity, bool from_filter) override
			{
				const char* last = reverseFind(label, nullptr, '/');
				if (ImGui::Selectable(last && !from_filter ? last + 1 : label))
				{
					if (create_entity)
					{
						Entity entity = editor->addEntity();
						editor->selectEntities(&entity, 1, false);
					}

					editor->addComponent(type);
				}
			}


			const char* getLabel() const override
			{
				return label;
			}

			WorldEditor* editor;
			PropertyGrid* property_grid;
			ComponentType type;
			char label[64];
		};

		auto& allocator = m_editor->getAllocator();
		auto* plugin = LUMIX_NEW(allocator, Plugin);
		plugin->property_grid = m_property_grid;
		plugin->editor = m_editor;
		plugin->type = Reflection::getComponentType(type);
		copyString(plugin->label, label);
		addPlugin(*plugin);

		m_component_labels.insert(plugin->type, string(label, m_allocator));
	}




	const Array<Action*>& getActions() override
	{
		return m_actions;
	}


	Array<Action*>& getToolbarActions() override
	{
		return m_toolbar_actions;
	}


	void guiBeginFrame() const
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
		ImGui::PushFont(m_font);
	}


	float showMainToolbar(float menu_height)
	{
		if (m_toolbar_actions.empty()) return menu_height;

		auto frame_padding = ImGui::GetStyle().FramePadding;
		float padding = frame_padding.y * 2;
		ImVec2 toolbar_size(ImGui::GetIO().DisplaySize.x, 24 + padding);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
		if (ImGui::BeginToolbar("main_toolbar", ImVec2(1, menu_height), toolbar_size))
		{
			for (auto* action : m_toolbar_actions)
			{
				action->toolbarButton();
			}
		}
		ImGui::EndToolbar();
		ImGui::PopStyleVar();
		return menu_height + 24 + padding;
	}


	void guiEndFrame()
	{
		if (m_is_welcome_screen_open)
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
			onEntityListGUI();
			onSaveAsDialogGUI();
			for (auto* plugin : m_plugins)
			{
				plugin->onWindowGUI();
			}
			m_settings.onGUI();
			onPackDataGUI();
		}
		ImGui::PopFont();
		ImGui::Render();
	}

	void update()
	{
		PROFILE_FUNCTION();
		guiBeginFrame();

		float time_delta = m_editor->getEngine().getLastTimeDelta();

		ImGuiIO& io = ImGui::GetIO();
		if (!io.KeyShift)
		{
			m_editor->setSnapMode(false, false);
		}
		else if (io.KeyCtrl)
		{
			m_editor->setSnapMode(io.KeyShift, io.KeyCtrl);
		}
		if (m_custom_pivot_action->isActive())
		{
			m_editor->setCustomPivot();
		}

		m_editor->setMouseSensitivity(m_settings.m_mouse_sensitivity.x, m_settings.m_mouse_sensitivity.y);
		m_editor->update();
		m_engine->update(*m_editor->getUniverse());

		if (m_deferred_game_mode_exit)
		{
			m_deferred_game_mode_exit = false;
			m_editor->toggleGameMode();
		}

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
		ImGui::SetNextWindowSize(size);
		if (ImGui::Begin("Welcome", nullptr, flags))
		{
			ImGui::Text("Welcome to Lumix Studio");

			ImVec2 half_size = ImGui::GetContentRegionAvail();
			half_size.x = half_size.x * 0.5f - ImGui::GetStyle().FramePadding.x;
			half_size.y *= 0.75f;
			auto right_pos = ImGui::GetCursorPos();
			right_pos.x += half_size.x + ImGui::GetStyle().FramePadding.x;
			if (ImGui::BeginChild("left", half_size, true))
			{
				if (ImGui::Button("New Universe")) m_is_welcome_screen_open = false;

				ImGui::Separator();
				ImGui::Text("Open universe:");
				ImGui::Indent();
				for (auto& univ : m_universes)
				{
					if (ImGui::MenuItem(univ.data))
					{
						m_editor->loadUniverse(univ.data);
						setTitle(univ.data);
						m_is_welcome_screen_open = false;
					}
				}
				ImGui::Unindent();
			}
			ImGui::EndChild();

			ImGui::SetCursorPos(right_pos);

			if (ImGui::BeginChild("right", half_size, true))
			{
				ImGui::Text("Using NVidia PhysX");

				if (ImGui::Button("Wiki"))
				{
					PlatformInterface::shellExecuteOpen("https://github.com/nem0/LumixEngine/wiki", nullptr);
				}

				if (ImGui::Button("Download new version"))
				{
					PlatformInterface::shellExecuteOpen("https://github.com/nem0/lumixengine_data/archive/master.zip", nullptr);
				}

				if (ImGui::Button("Show major releases"))
				{
					PlatformInterface::shellExecuteOpen("https://github.com/nem0/LumixEngine/releases", nullptr);
				}

				if (ImGui::Button("Show latest commits"))
				{
					PlatformInterface::shellExecuteOpen("https://github.com/nem0/LumixEngine/commits/master", nullptr);
				}

				if (ImGui::Button("Show issues"))
				{
					PlatformInterface::shellExecuteOpen("https://github.com/nem0/lumixengine/issues", nullptr);
				}
			}
			ImGui::EndChild();
		}
		ImGui::End();
	}


	void setTitle(const char* title) const
	{
		char tmp[100];
		copyString(tmp, "Lumix Studio - ");
		catString(tmp, title);
		SDL_SetWindowTitle(m_window, tmp);
	}


	static void getShortcut(const Action& action, char* buf, int max_size)
	{
		buf[0] = 0;
		for (int i = 0; i < lengthOf(action.shortcut); ++i)
		{
			const char* str = SDL_GetKeyName(SDL_GetKeyFromScancode((SDL_Scancode)action.shortcut[i]));
			if (str[0] == 0) return;
			if (i > 0) catString(buf, max_size, " - ");
			catString(buf, max_size, str);
		}
	}


	void doMenuItem(Action& a, bool enabled) const
	{
		char buf[20];
		getShortcut(a, buf, sizeof(buf));
		if (ImGui::MenuItem(a.label_short, buf, a.is_selected.invoke(), enabled))
		{
			a.func.invoke();
		}
	}


	void save()
	{
		if (m_editor->isGameMode())
		{
			g_log_error.log("Editor") << "Could not save while the game is running";
			return;
		}

		if (m_editor->getUniverse()->getName()[0])
		{
			m_editor->saveUniverse(m_editor->getUniverse()->getName(), true);
		}
		else
		{
			saveAs();
		}
	}


	void onSaveAsDialogGUI()
	{
		if (!m_is_save_as_dialog_open) return;
		
		if (ImGui::Begin("Save Universe As", &m_is_save_as_dialog_open))
		{
			static char name[64] = "";
			ImGui::InputText("Name", name, lengthOf(name));
			if (ImGui::Button("Save"))
			{
				m_is_save_as_dialog_open = false;
				setTitle(name);
				m_editor->saveUniverse(name, true);
			}
			ImGui::SameLine();
			if (ImGui::Button("Close")) m_is_save_as_dialog_open = false;
		}
		ImGui::End();
	}


	void saveAs()
	{
		if (m_editor->isGameMode())
		{
			g_log_error.log("Editor") << "Could not save while the game is running";
			return;
		}

		m_is_save_as_dialog_open = true;
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
		}
	}


	IPlugin* getFocusedPlugin()
	{
		for(IPlugin* plugin : m_plugins)
		{
			if(plugin->hasFocus()) return plugin;
		}
		return nullptr;
	}


	void undo() { m_editor->undo(); }
	void redo() { m_editor->redo(); }
	void copy() { m_editor->copyEntities(); }
	void paste() { m_editor->pasteEntities(); }
	void duplicate() { m_editor->duplicateEntities(); }
	bool isOrbitCamera() { return m_editor->isOrbitCamera(); }
	void toggleOrbitCamera() { m_editor->setOrbitCamera(!m_editor->isOrbitCamera()); }
	void setTopView() { m_editor->setTopView(); }
	void setFrontView() { m_editor->setFrontView(); }
	void setSideView() { m_editor->setSideView(); }
	void setLocalCoordSystem() { m_editor->getGizmo().setLocalCoordSystem(); }
	void setGlobalCoordSystem() { m_editor->getGizmo().setGlobalCoordSystem(); }
	void setPivotOrigin() { m_editor->getGizmo().setPivotOrigin(); }
	void setPivotCenter() { m_editor->getGizmo().setPivotCenter(); }
	void addEntity() { m_editor->addEntity(); }
	void toggleMeasure() { m_editor->toggleMeasure(); }
	void snapDown() { m_editor->snapDown(); }
	void lookAtSelected() { m_editor->lookAtSelected(); }
	void toggleSettings() { m_settings.m_is_open = !m_settings.m_is_open; }
	bool areSettingsOpen() const { return m_settings.m_is_open; }
	void toggleEntityList() { m_is_entity_list_open = !m_is_entity_list_open; }
	bool isEntityListOpen() const { return m_is_entity_list_open; }
	void toggleAssetBrowser() { m_asset_browser->m_is_open = !m_asset_browser->m_is_open; }
	bool isAssetBrowserOpen() const { return m_asset_browser->m_is_open; }
	int getExitCode() const override { return m_exit_code; }
	AssetBrowser& getAssetBrowser() override { ASSERT(m_asset_browser); return *m_asset_browser; }
	PropertyGrid& getPropertyGrid() override { ASSERT(m_property_grid); return *m_property_grid; }
	Metadata& getMetadata() override { return m_metadata; }
	LogUI& getLogUI() override { ASSERT(m_log_ui); return *m_log_ui; }
	void toggleGameMode() { m_editor->toggleGameMode(); }
	void setTranslateGizmoMode() { m_editor->getGizmo().setTranslateMode(); }
	void setRotateGizmoMode() { m_editor->getGizmo().setRotateMode(); }
	void setScaleGizmoMode() { m_editor->getGizmo().setScaleMode(); }


	void makeParent()
	{
		const auto& entities = m_editor->getSelectedEntities();
		ASSERT(entities.size() == 2);
		m_editor->makeParent(entities[0], entities[1]);
	}


	void unparent()
	{
		const auto& entities = m_editor->getSelectedEntities();
		ASSERT(entities.size() == 1);
		m_editor->makeParent(INVALID_ENTITY, entities[0]);
	}


	void savePrefab()
	{
		char filename[MAX_PATH_LENGTH];
		char tmp[MAX_PATH_LENGTH];
		if (PlatformInterface::getSaveFilename(tmp, lengthOf(tmp), "Prefab files\0*.fab\0", "fab"))
		{
			PathUtils::normalize(tmp, filename, lengthOf(tmp));
			const char* base_path = m_engine->getDiskFileDevice()->getBasePath();
			if (startsWith(filename, base_path))
			{
				m_editor->getPrefabSystem().savePrefab(Path(filename + stringLength(base_path)));
			}
			else
			{
				base_path = m_engine->getPatchFileDevice() ? m_engine->getPatchFileDevice()->getBasePath() : nullptr;
				if (base_path && startsWith(filename, base_path))
				{
					m_editor->getPrefabSystem().savePrefab(Path(filename + stringLength(base_path)));
				}
				else
				{
					m_editor->getPrefabSystem().savePrefab(Path(filename));
				}
			}
		}
	}


	void autosnapDown() 
	{
		auto& gizmo = m_editor->getGizmo();
		gizmo.setAutosnapDown(!gizmo.isAutosnapDown());
	}


	void destroySelectedEntity()
	{
		auto& selected_entities = m_editor->getSelectedEntities();
		if (selected_entities.empty()) return;
		m_editor->destroyEntities(&selected_entities[0], selected_entities.size());
	}


	void loadAndExecuteCommands()
	{
		char filename[MAX_PATH_LENGTH];
		if (PlatformInterface::getOpenFilename(filename, lengthOf(filename), "JSON files\0*.json\0", nullptr))
		{
			m_editor->executeUndoStack(Path(filename));
		}
	}


	void saveUndoStack()
	{
		char filename[MAX_PATH_LENGTH];
		if (PlatformInterface::getSaveFilename(filename, lengthOf(filename), "JSON files\0*.json\0", "json"))
		{
			m_editor->saveUndoStack(Path(filename));
		}
	}


	void addWindowAction(Action* action) override
	{
		addAction(action);
		for (int i = 0; i < m_window_actions.size(); ++i)
		{
			if (compareString(m_window_actions[i]->label_long, action->label_long) > 0)
			{
				m_window_actions.insert(i, action);
				return;
			}
		}
		m_window_actions.push(action);
	}


	void addAction(Action* action) override
	{
		for (int i = 0; i < m_actions.size(); ++i)
		{
			if (compareString(m_actions[i]->label_long, action->label_long) > 0)
			{
				m_actions.insert(i, action);
				return;
			}
		}
		m_actions.push(action);
	}


	template <void (StudioAppImpl::*Func)()>
	Action& addAction(const char* label_short, const char* label_long, const char* name)
	{
		auto* a = LUMIX_NEW(m_editor->getAllocator(), Action)(label_short, label_long, name);
		a->func.bind<StudioAppImpl, Func>(this);
		addAction(a);
		return *a;
	}


	template <void (StudioAppImpl::*Func)()>
	void addAction(const char* label_short, const char* label_long, const char* name, int shortcut0, int shortcut1, int shortcut2)
	{
		auto* a = LUMIX_NEW(m_editor->getAllocator(), Action)(
			label_short, label_long, name, shortcut0, shortcut1, shortcut2);
		a->func.bind<StudioAppImpl, Func>(this);
		addAction(a);
	}


	Action* getAction(const char* name) override
	{
		for (auto* a : m_actions)
		{
			if (equalStrings(a->name, name)) return a;
		}
		return nullptr;
	}


	static void showAddComponentNode(const StudioApp::AddCmpTreeNode* node, const char* filter)
	{
		if (!node) return;

		if (filter[0])
		{
			if (!node->plugin) showAddComponentNode(node->child, filter);
			else if (stristr(node->plugin->getLabel(), filter)) node->plugin->onGUI(false, true);
			showAddComponentNode(node->next, filter);
			return;
		}

		if (node->plugin)
		{
			node->plugin->onGUI(true, false);
			showAddComponentNode(node->next, filter);
			return;
		}

		const char* last = reverseFind(node->label, nullptr, '/');
		if (ImGui::BeginMenu(last ? last + 1 : node->label))
		{
			showAddComponentNode(node->child, filter);
			ImGui::EndMenu();
		}
		showAddComponentNode(node->next, filter);
	}


	void onCreateEntityWithComponentGUI()
	{
		doMenuItem(*getAction("createEntity"), true);
		ImGui::Separator();
		ImGui::LabellessInputText("Filter", m_component_filter, sizeof(m_component_filter));
		showAddComponentNode(m_add_cmp_root.child, m_component_filter);
	}

	
	void entityMenu()
	{
		if (!ImGui::BeginMenu("Entity")) return;

		const auto& selected_entities = m_editor->getSelectedEntities();
		bool is_any_entity_selected = !selected_entities.empty();
		if (ImGui::BeginMenu("Create"))
		{
			onCreateEntityWithComponentGUI();
			ImGui::EndMenu();
		}
		doMenuItem(*getAction("destroyEntity"), is_any_entity_selected);
		doMenuItem(*getAction("savePrefab"), selected_entities.size() == 1);
		doMenuItem(*getAction("makeParent"), selected_entities.size() == 2);
		bool can_unparent = selected_entities.size() == 1 && m_editor->getUniverse()->getParent(selected_entities[0]).isValid();
		doMenuItem(*getAction("unparent"), can_unparent);
		ImGui::EndMenu();
	}


	void editMenu()
	{
		if (!ImGui::BeginMenu("Edit")) return;

		bool is_any_entity_selected = !m_editor->getSelectedEntities().empty();
		doMenuItem(*getAction("undo"), m_editor->canUndo());
		doMenuItem(*getAction("redo"), m_editor->canRedo());
		ImGui::Separator();
		doMenuItem(*getAction("copy"), is_any_entity_selected);
		doMenuItem(*getAction("paste"), m_editor->canPasteEntities());
		doMenuItem(*getAction("duplicate"), is_any_entity_selected);
		ImGui::Separator();
		doMenuItem(*getAction("orbitCamera"), is_any_entity_selected || m_editor->isOrbitCamera());
		doMenuItem(*getAction("setTranslateGizmoMode"), true);
		doMenuItem(*getAction("setRotateGizmoMode"), true);
		doMenuItem(*getAction("setScaleGizmoMode"), true);
		doMenuItem(*getAction("setPivotCenter"), true);
		doMenuItem(*getAction("setPivotOrigin"), true);
		doMenuItem(*getAction("setLocalCoordSystem"), true);
		doMenuItem(*getAction("setGlobalCoordSystem"), true);
		if (ImGui::BeginMenu("View", true))
		{
			doMenuItem(*getAction("viewTop"), true);
			doMenuItem(*getAction("viewFront"), true);
			doMenuItem(*getAction("viewSide"), true);
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}


	void fileMenu()
	{
		if (!ImGui::BeginMenu("File")) return;

		doMenuItem(*getAction("newUniverse"), true);
		if (ImGui::BeginMenu("Open"))
		{
			ImGui::LabellessInputText("Filter", m_open_filter, sizeof(m_open_filter));
			for (auto& univ : m_universes)
			{
				if ((m_open_filter[0] == '\0' || stristr(univ.data, m_open_filter)) &&
					ImGui::MenuItem(univ.data))
				{
					if (m_editor->isUniverseChanged())
					{
						copyString(m_universe_to_load, univ.data);
						m_confirm_load = true;
					}
					else
					{
						m_editor->loadUniverse(univ.data);
						setTitle(univ.data);
					}
				}
			}
			ImGui::EndMenu();
		}
		doMenuItem(*getAction("save"), !m_editor->isGameMode());
		doMenuItem(*getAction("saveAs"), !m_editor->isGameMode());
		doMenuItem(*getAction("exit"), true);
		ImGui::EndMenu();
	}


	void toolsMenu()
	{
		if (!ImGui::BeginMenu("Tools")) return;

		bool is_any_entity_selected = !m_editor->getSelectedEntities().empty();
		doMenuItem(*getAction("lookAtSelected"), is_any_entity_selected);
		doMenuItem(*getAction("toggleGameMode"), true);
		doMenuItem(*getAction("toggleMeasure"), true);
		doMenuItem(*getAction("snapDown"), is_any_entity_selected);
		doMenuItem(*getAction("autosnapDown"), true);
		if (ImGui::MenuItem("Save commands")) saveUndoStack();
		if (ImGui::MenuItem("Load commands")) loadAndExecuteCommands();
		doMenuItem(*getAction("pack_data"), true);
		ImGui::EndMenu();
	}


	void viewMenu()
	{
		if (!ImGui::BeginMenu("View")) return;

		ImGui::MenuItem("Asset browser", nullptr, &m_asset_browser->m_is_open);
		doMenuItem(*getAction("entityList"), true);
		ImGui::MenuItem("Log", nullptr, &m_log_ui->m_is_open);
		ImGui::MenuItem("Profiler", nullptr, &m_profiler_ui->m_is_open);
		ImGui::MenuItem("Properties", nullptr, &m_property_grid->m_is_open);
		doMenuItem(*getAction("settings"), true);
		ImGui::Separator();
		for (Action* action : m_window_actions)
		{
			doMenuItem(*action, true);
		}
		ImGui::EndMenu();
	}


	float showMainMenu()
	{
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
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		if (m_confirm_load)
		{
			ImGui::OpenPopup("Confirm");
			m_confirm_load = false;
		}
		if(ImGui::BeginPopupModal("Confirm"))
		{
			ImGui::Text("All unsaved changes will be lost, do you want to continue?");
			if (ImGui::Button("Continue"))
			{
				m_editor->loadUniverse(m_universe_to_load);
				setTitle(m_universe_to_load);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		float menu_height = 0;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
		if (ImGui::BeginMainMenuBar())
		{
			fileMenu();
			editMenu();
			entityMenu();
			toolsMenu();
			viewMenu();

			StaticString<200> stats("");
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
				StaticString<50> error_stats("", m_log_ui->getUnreadErrorCount(), " errors | ");
				ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x);
				auto error_stats_size = ImGui::CalcTextSize(error_stats);
				ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x - error_stats_size.x);
				ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", (const char*)error_stats);
			}
			menu_height = ImGui::GetWindowSize().y;
			ImGui::EndMainMenuBar();
		}
		ImGui::PopStyleVar();
		return menu_height;
	}


	void showHierarchy(Entity entity, const Array<Entity>& selected_entities)
	{
		char buffer[1024];
		Universe* universe = m_editor->getUniverse();
		getEntityListDisplayName(*m_editor, buffer, sizeof(buffer), entity);
		bool selected = selected_entities.indexOf(entity) >= 0;
		ImGui::PushID(entity.index);
		if (ImGui::Selectable(buffer, &selected))
		{
			m_editor->selectEntities(&entity, 1, true);
		}
		if (ImGui::IsMouseReleased(1) && ImGui::IsItemHovered())
		{
			ImGui::OpenPopup("entity_context_menu");
		}
		if (ImGui::BeginPopup("entity_context_menu"))
		{
			if (ImGui::MenuItem("Create child"))
			{
				m_editor->beginCommandGroup(crc32("create_child_entity"));
				Entity child = m_editor->addEntity();
				m_editor->makeParent(entity, child);
				Vec3 pos = m_editor->getUniverse()->getPosition(entity);
				m_editor->setEntitiesPositions(&child, &pos, 1);
				m_editor->endCommandGroup();
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
		if (ImGui::BeginDragDropSource())
		{
			ImGui::Text("%s", buffer);
			ImGui::SetDragDropPayload("entity", &entity, sizeof(entity));
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (auto* payload = ImGui::AcceptDragDropPayload("entity"))
			{
				Entity dropped_entity = *(Entity*)payload->Data;
				if (dropped_entity != entity)
				{
					m_editor->makeParent(entity, dropped_entity);
					return;
				}
			}

			ImGui::EndDragDropTarget();
		}

		ImGui::Indent();
		for (Entity e = universe->getFirstChild(entity); e.isValid(); e = universe->getNextSibling(e))
		{
			showHierarchy(e, selected_entities);
		}
		ImGui::Unindent();
	}


	void onEntityListGUI()
	{
		PROFILE_FUNCTION();
		const Array<Entity>& entities = m_editor->getSelectedEntities();
		static char filter[64] = "";
		if (ImGui::BeginDock("Entity List", &m_is_entity_list_open))
		{
			auto* universe = m_editor->getUniverse();
			ImGui::LabellessInputText("Filter", filter, sizeof(filter));

			if (ImGui::BeginChild("entities"))
			{
				ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() - ImGui::GetStyle().FramePadding.x);
				if (filter[0] == '\0')
				{
					for (Entity e = universe->getFirstEntity(); e.isValid(); e = universe->getNextEntity(e))
					{
						if (!universe->getParent(e).isValid())
						{
							showHierarchy(e, entities);
						}
					}
				}
				else
				{
					for (Entity e = universe->getFirstEntity(); e.isValid(); e = universe->getNextEntity(e))
					{
						char buffer[1024];
						getEntityListDisplayName(*m_editor, buffer, sizeof(buffer), e);
						if (stristr(buffer, filter) == nullptr) continue;
						bool selected = entities.indexOf(e) >= 0;
						if (ImGui::Selectable(buffer, &selected))
						{
							m_editor->selectEntities(&e, 1, true);
						}
						if (ImGui::BeginDragDropSource())
						{
							ImGui::Text("%s", buffer);
							ImGui::SetDragDropPayload("entity", &e, sizeof(e));
							ImGui::EndDragDropSource();
						}
					}
				}
				ImGui::PopItemWidth();
			}
			ImGui::EndChild();
			// TODO uncomment once it's fixed in imgui
			/*if (ImGui::BeginDragDropTarget())
			{
				if (auto* payload = ImGui::AcceptDragDropPayload("entity"))
				{
					Entity dropped_entity = *(Entity*)payload->Data;
					m_editor->makeParent(INVALID_ENTITY, dropped_entity);
				}
				ImGui::EndDragDropTarget();
			}*/
		}
		ImGui::EndDock();
	}


	void dummy() {}


	void setFullscreen(bool fullscreen) override
	{
		SDL_SetWindowFullscreen(m_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
	}


	void saveSettings()
	{
		m_settings.m_is_asset_browser_open = m_asset_browser->m_is_open;
		m_settings.m_asset_browser_left_column_width = m_asset_browser->m_left_column_width;
		m_settings.m_is_entity_list_open = m_is_entity_list_open;
		m_settings.m_is_log_open = m_log_ui->m_is_open;
		m_settings.m_is_profiler_open = m_profiler_ui->m_is_open;
		m_settings.m_is_properties_open = m_property_grid->m_is_open;
		m_settings.m_mouse_sensitivity.x = m_editor->getMouseSensitivity().x;
		m_settings.m_mouse_sensitivity.y = m_editor->getMouseSensitivity().y;

		m_settings.save();

		if (!m_metadata.save())
		{
			g_log_warning.log("Editor") << "Could not save metadata";
		}
	}


	void initIMGUI()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.NavFlags |= ImGuiNavFlags_EnableKeyboard;
		float ddpi;
		float font_scale = 1;
		if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) == 0) font_scale = ddpi / 96;
		m_font = io.Fonts->AddFontFromFileTTF("ui/fonts/OpenSans-Regular.ttf", (float)m_settings.m_font_size * font_scale);
		m_bold_font = io.Fonts->AddFontFromFileTTF("ui/fonts/OpenSans-Bold.ttf", (float)m_settings.m_font_size * font_scale);

		m_font->DisplayOffset.y = 0;
		m_bold_font->DisplayOffset.y = 0;

		io.KeyMap[ImGuiKey_Space] = SDL_SCANCODE_SPACE;
		io.KeyMap[ImGuiKey_Tab] = SDL_SCANCODE_TAB;
		io.KeyMap[ImGuiKey_LeftArrow] = SDL_SCANCODE_LEFT;
		io.KeyMap[ImGuiKey_RightArrow] = SDL_SCANCODE_RIGHT;
		io.KeyMap[ImGuiKey_UpArrow] = SDL_SCANCODE_UP;
		io.KeyMap[ImGuiKey_DownArrow] = SDL_SCANCODE_DOWN;
		io.KeyMap[ImGuiKey_PageUp] = SDL_SCANCODE_PAGEUP;
		io.KeyMap[ImGuiKey_PageDown] = SDL_SCANCODE_PAGEDOWN;
		io.KeyMap[ImGuiKey_Home] = SDL_SCANCODE_HOME;
		io.KeyMap[ImGuiKey_End] = SDL_SCANCODE_END;
		io.KeyMap[ImGuiKey_Delete] = SDL_SCANCODE_DELETE;
		io.KeyMap[ImGuiKey_Backspace] = SDL_SCANCODE_BACKSPACE;
		io.KeyMap[ImGuiKey_Enter] = SDL_SCANCODE_RETURN;
		io.KeyMap[ImGuiKey_Escape] = SDL_SCANCODE_ESCAPE;
		io.KeyMap[ImGuiKey_A] = SDL_SCANCODE_A;
		io.KeyMap[ImGuiKey_C] = SDL_SCANCODE_C;
		io.KeyMap[ImGuiKey_V] = SDL_SCANCODE_V;
		io.KeyMap[ImGuiKey_X] = SDL_SCANCODE_X;
		io.KeyMap[ImGuiKey_Y] = SDL_SCANCODE_Y;
		io.KeyMap[ImGuiKey_Z] = SDL_SCANCODE_Z;

		ImGuiStyle& style = ImGui::GetStyle();
		style.FramePadding.y = 0;
		style.ItemSpacing.y = 2;
		style.ItemInnerSpacing.x = 2;
	}


	void loadSettings()
	{
		char cmd_line[2048];
		getCommandLine(cmd_line, lengthOf(cmd_line));

		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (!parser.currentEquals("-no_crash_report")) continue;

			m_settings.m_force_no_crash_report = true;
			break;
		}

		m_settings.load();

		m_asset_browser->m_is_open = m_settings.m_is_asset_browser_open;
		m_asset_browser->m_left_column_width = m_settings.m_asset_browser_left_column_width;
		m_is_entity_list_open = m_settings.m_is_entity_list_open;
		m_log_ui->m_is_open = m_settings.m_is_log_open;
		m_profiler_ui->m_is_open = m_settings.m_is_profiler_open;
		m_property_grid->m_is_open = m_settings.m_is_properties_open;

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
		addAction<&StudioAppImpl::newUniverse>("New", "New universe", "newUniverse");
		addAction<&StudioAppImpl::save>("Save", "Save universe", "save", SDLK_LCTRL, 'S', -1);
		addAction<&StudioAppImpl::saveAs>("Save As", "Save universe as", "saveAs", SDLK_LCTRL, SDLK_LSHIFT, 'S');
		addAction<&StudioAppImpl::exit>("Exit", "Exit Studio", "exit", SDLK_LCTRL, 'X', -1);
		addAction<&StudioAppImpl::redo>("Redo", "Redo scene action", "redo", SDLK_LCTRL, SDLK_LSHIFT, 'Z');
		addAction<&StudioAppImpl::undo>("Undo", "Undo scene action", "undo", SDLK_LCTRL, 'Z', -1);
		addAction<&StudioAppImpl::copy>("Copy", "Copy entity", "copy", SDLK_LCTRL, 'C', -1);
		addAction<&StudioAppImpl::paste>("Paste", "Paste entity", "paste", SDLK_LCTRL, 'V', -1);
		addAction<&StudioAppImpl::duplicate>("Duplicate", "Duplicate entity", "duplicate", SDLK_LCTRL, 'D', -1);
		addAction<&StudioAppImpl::toggleOrbitCamera>("Orbit camera", "Orbit camera", "orbitCamera")
			.is_selected.bind<StudioAppImpl, &StudioAppImpl::isOrbitCamera>(this);
		addAction<&StudioAppImpl::setTranslateGizmoMode>("Translate", "Set translate mode", "setTranslateGizmoMode")
			.is_selected.bind<Gizmo, &Gizmo::isTranslateMode>(&m_editor->getGizmo());
		addAction<&StudioAppImpl::setRotateGizmoMode>("Rotate", "Set rotate mode", "setRotateGizmoMode")
			.is_selected.bind<Gizmo, &Gizmo::isRotateMode>(&m_editor->getGizmo());
		addAction<&StudioAppImpl::setScaleGizmoMode>("Scale", "Set scale mode", "setScaleGizmoMode")
			.is_selected.bind<Gizmo, &Gizmo::isScaleMode>(&m_editor->getGizmo());
		addAction<&StudioAppImpl::setTopView>("Top", "Set top camera view", "viewTop");
		addAction<&StudioAppImpl::setFrontView>("Front", "Set front camera view", "viewFront");
		addAction<&StudioAppImpl::setSideView>("Side", "Set side camera view", "viewSide");
		addAction<&StudioAppImpl::setLocalCoordSystem>("Local", "Set local transform system", "setLocalCoordSystem")
			.is_selected.bind<Gizmo, &Gizmo::isLocalCoordSystem>(&m_editor->getGizmo());
		addAction<&StudioAppImpl::setGlobalCoordSystem>("Global", "Set global transform system", "setGlobalCoordSystem")
			.is_selected.bind<Gizmo, &Gizmo::isGlobalCoordSystem>(&m_editor->getGizmo());
		addAction<&StudioAppImpl::setPivotCenter>("Center", "Set center transform system", "setPivotCenter")
			.is_selected.bind<Gizmo, &Gizmo::isPivotCenter>(&m_editor->getGizmo());
		addAction<&StudioAppImpl::setPivotOrigin>("Pivot", "Set pivot transform system", "setPivotOrigin")
			.is_selected.bind<Gizmo, &Gizmo::isPivotOrigin>(&m_editor->getGizmo());

		addAction<&StudioAppImpl::addEntity>("Create empty", "Create empty entity", "createEntity");
		addAction<&StudioAppImpl::destroySelectedEntity>("Destroy", "Destroy entity", "destroyEntity", SDLK_DELETE, -1, -1);
		addAction<&StudioAppImpl::savePrefab>("Save prefab", "Save selected entities as prefab", "savePrefab");
		addAction<&StudioAppImpl::makeParent>("Make parent", "Make entity parent", "makeParent");
		addAction<&StudioAppImpl::unparent>("Unparent", "Unparent entity", "unparent");

		addAction<&StudioAppImpl::toggleGameMode>("Game Mode", "Toggle game mode", "toggleGameMode")
			.is_selected.bind<WorldEditor, &WorldEditor::isGameMode>(m_editor);
		addAction<&StudioAppImpl::toggleMeasure>("Toggle measure", "Toggle measure mode", "toggleMeasure")
			.is_selected.bind<WorldEditor, &WorldEditor::isMeasureToolActive>(m_editor);
		addAction<&StudioAppImpl::autosnapDown>("Autosnap down", "Toggle autosnap down", "autosnapDown")
			.is_selected.bind<Gizmo, &Gizmo::isAutosnapDown>(&m_editor->getGizmo());
		addAction<&StudioAppImpl::snapDown>("Snap down", "Snap entities down", "snapDown");
		addAction<&StudioAppImpl::lookAtSelected>("Look at selected", "Look at selected entity", "lookAtSelected");
		addAction<&StudioAppImpl::toggleAssetBrowser>("Asset Browser", "Toggle asset browser", "assetBrowser")
			.is_selected.bind<StudioAppImpl, &StudioAppImpl::isAssetBrowserOpen>(this);
		addAction<&StudioAppImpl::toggleEntityList>("Entity List", "Toggle entity list", "entityList")
			.is_selected.bind<StudioAppImpl, &StudioAppImpl::isEntityListOpen>(this);
		addAction<&StudioAppImpl::toggleSettings>("Settings", "Toggle settings UI", "settings")
			.is_selected.bind<StudioAppImpl, &StudioAppImpl::areSettingsOpen>(this);
		addAction<&StudioAppImpl::showPackDataDialog>("Pack data", "Pack data", "pack_data");
	}


	void loadUserPlugins()
	{
		char cmd_line[2048];
		getCommandLine(cmd_line, lengthOf(cmd_line));

		CommandLineParser parser(cmd_line);
		auto& plugin_manager = m_editor->getEngine().getPluginManager();
		while (parser.next())
		{
			if (!parser.currentEquals("-plugin")) continue;
			if (!parser.next()) break;

			char tmp[MAX_PATH_LENGTH];
			parser.getCurrent(tmp, lengthOf(tmp));
			bool loaded = plugin_manager.load(tmp) != nullptr;
			if (!loaded)
			{
				g_log_error.log("Editor") << "Could not load plugin " << tmp << " requested by command line";
			}
		}
	}

	bool shouldSleepWhenInactive()
	{
		char cmd_line[2048];
		getCommandLine(cmd_line, lengthOf(cmd_line));

		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (parser.currentEquals("-no_sleep_inactive")) return false;
		}
		return true;
	}


	bool vSync()
	{
		char cmd_line[2048];
		getCommandLine(cmd_line, lengthOf(cmd_line));

		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (parser.currentEquals("-no_vsync")) return false;
		}
		return true;
	}


	void loadUniverseFromCommandLine()
	{
		char cmd_line[2048];
		char path[MAX_PATH_LENGTH];
		getCommandLine(cmd_line, lengthOf(cmd_line));

		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (!parser.currentEquals("-open")) continue;
			if (!parser.next()) break;

			parser.getCurrent(path, lengthOf(path));
			m_editor->loadUniverse(path);
			setTitle(path);
			m_is_welcome_screen_open = false;
			break;
		}
	}


	static void checkDataDirCommandLine(char* dir, int max_size)
	{
		char cmd_line[2048];
		getCommandLine(cmd_line, lengthOf(cmd_line));

		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (!parser.currentEquals("-data_dir")) continue;
			if (!parser.next()) break;

			parser.getCurrent(dir, max_size);
			break;
		}
	}


	IPlugin* getPlugin(const char* name) override
	{
		for (auto* i : m_plugins)
		{
			if (equalStrings(i->getName(), name)) return i;
		}
		return nullptr;
	}


	void addPlugin(IPlugin& plugin) override
	{
		m_plugins.push(&plugin);
		for (auto* i : m_plugins)
		{
			i->pluginAdded(plugin);
			plugin.pluginAdded(*i);
		}
	}


	void removePlugin(IPlugin& plugin) override
	{
		m_plugins.eraseItemFast(&plugin);
	}


	void setStudioApp()
	{
		m_editor->getPrefabSystem().setStudioApp(*this);
		#ifdef STATIC_PLUGINS
			StudioApp::StaticPluginRegister::create(*this);
		#else
			auto& plugin_manager = m_editor->getEngine().getPluginManager();
			for (auto* lib : plugin_manager.getLibraries())
			{
				auto* f = (void (*)(StudioApp&))getLibrarySymbol(lib, "setStudioApp");
				if (f) f(*this);
			}
		#endif
	}


	void runScript(const char* src, const char* script_name) override
	{
		lua_State* L = m_engine->getState();
		bool errors =
			luaL_loadbuffer(L, src, stringLength(src), script_name) != LUA_OK;
		errors = errors || lua_pcall(L, 0, 0, 0) != LUA_OK;
		if (errors)
		{
			g_log_error.log("Editor") << script_name << ": " << lua_tostring(L, -1);
			lua_pop(L, 1);
		}
	}


	void savePrefabAs(const char* path)
	{
		m_editor->getPrefabSystem().savePrefab(Path(path));
	}


	void destroyEntity(Entity e)
	{
		m_editor->destroyEntities(&e, 1);
	}


	void selectEntity(Entity e)
	{
		m_editor->selectEntities(&e, 1, false);
	}


	Entity createEntity()
	{
		return m_editor->addEntity();
	}

	void createComponent(Entity e, int type)
	{
		m_editor->selectEntities(&e, 1, false);
		m_editor->addComponent({type});
	}

	void exitGameMode()
	{
		m_deferred_game_mode_exit = true;
	}


	void exitWithCode(int exit_code)
	{
		m_finished = true;
		m_exit_code = exit_code;
	}


	struct SetPropertyVisitor : public Reflection::IPropertyVisitor
	{
		void visit(const Reflection::Property<int>& prop) override 
		{ 
			if (!equalStrings(property_name, prop.name)) return;
			if (!lua_isinteger(L, -1)) return;

			int val = (int)lua_tointeger(L, -1);
			editor->setProperty(cmp.type, 0, prop, &cmp.entity, 1, &val, sizeof(val));
		}


		// TODO 
		void visit(const Reflection::Property<float>& prop) override { notSupported(prop); }
		void visit(const Reflection::Property<Entity>& prop) override { notSupported(prop); }
		void visit(const Reflection::Property<Int2>& prop) override { notSupported(prop); }
		void visit(const Reflection::Property<Vec2>& prop) override { notSupported(prop); }
		void visit(const Reflection::Property<Vec3>& prop) override { notSupported(prop); }
		void visit(const Reflection::Property<Vec4>& prop) override { notSupported(prop); }


		void visit(const Reflection::Property<const char*>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (!lua_isstring(L, -1)) return;

			const char* str = lua_tostring(L, -1);
			editor->setProperty(cmp.type, 0, prop, &cmp.entity, 1, str, stringLength(str) + 1);
		}


		void visit(const Reflection::Property<Path>& prop) override 
		{ 
			if (!equalStrings(property_name, prop.name)) return;
			if (!lua_isstring(L, -1)) return;

			const char* str = lua_tostring(L, -1);
			editor->setProperty(cmp.type, 0, prop, &cmp.entity, 1, str, stringLength(str) + 1);
		}


		void visit(const Reflection::Property<bool>& prop) override 
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (!lua_isboolean(L, -1)) return;

			bool val = lua_toboolean(L, -1) != 0;
			editor->setProperty(cmp.type, 0, prop, &cmp.entity, 1, &val, sizeof(val));
		}

		void visit(const Reflection::IArrayProperty& prop) override { notSupported(prop); }
		void visit(const Reflection::IEnumProperty& prop) override { notSupported(prop); }
		void visit(const Reflection::IBlobProperty& prop) override { notSupported(prop); }
		void visit(const Reflection::ISampledFuncProperty& prop) override { notSupported(prop); }


		void notSupported(const Reflection::PropertyBase& prop)
		{
			if (!equalStrings(property_name, prop.name)) return;
			g_log_error.log("Lua Script") << "Property " << prop.name << " has unsupported type";
		}


		lua_State* L;
		ComponentUID cmp;
		const char* property_name;
		WorldEditor* editor;
	};


	static int createEntityEx(lua_State* L)
	{
		auto* studio = LuaWrapper::checkArg<StudioAppImpl*>(L, 1);
		LuaWrapper::checkTableArg(L, 2);

		WorldEditor& editor = *studio->m_editor;
		editor.beginCommandGroup(crc32("createEntityEx"));
		Entity e = editor.addEntity();
		editor.selectEntities(&e, 1, false);

		lua_pushvalue(L, 2);
		lua_pushnil(L);
		while (lua_next(L, -2) != 0)
		{
			const char* parameter_name = luaL_checkstring(L, -2);
			if (equalStrings(parameter_name, "position"))
			{
				auto pos = LuaWrapper::toType<Vec3>(L, -1);
				editor.setEntitiesPositions(&e, &pos, 1);
			}
			else if (equalStrings(parameter_name, "rotation"))
			{
				auto rot = LuaWrapper::toType<Quat>(L, -1);
				editor.setEntitiesRotations(&e, &rot, 1);
			}
			else
			{
				ComponentType cmp_type = Reflection::getComponentType(parameter_name);
				editor.addComponent(cmp_type);

				IScene* scene = editor.getUniverse()->getScene(cmp_type);
				if (scene)
				{
					ComponentUID cmp(e, cmp_type, scene);
					const Reflection::ComponentBase* cmp_des = Reflection::getComponent(cmp_type);
					if (cmp.isValid())
					{
						lua_pushvalue(L, -1);
						lua_pushnil(L);
						while (lua_next(L, -2) != 0)
						{
							const char* property_name = luaL_checkstring(L, -2);
							SetPropertyVisitor v;
							v.property_name = property_name;
							v.cmp = cmp;
							v.L = L;
							v.editor = &editor;
							cmp_des->visit(v);

							lua_pop(L, 1);
						}
						lua_pop(L, 1);
					}
				}
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);

		editor.endCommandGroup();
		LuaWrapper::push(L, e);
		return 1;
	}


	static int getResources(lua_State* L)
	{
		auto* studio = LuaWrapper::checkArg<StudioAppImpl*>(L, 1);
		auto* type = LuaWrapper::checkArg<const char*>(L, 2);

		AssetBrowser& browser = studio->getAssetBrowser();
		if (ResourceType(type) == INVALID_RESOURCE_TYPE) return 0;
		auto& resources_paths = browser.getResources(ResourceType(type));

		lua_createtable(L, resources_paths.size(), 0);
		int i = 0;
		for (auto& path : resources_paths)
		{
			LuaWrapper::push(L, path.c_str());
			lua_rawseti(L, -2, i + 1);
			++i;
		}

		return 1;
	}


	void executeUndoStack(const char* path)
	{
		m_editor->executeUndoStack(Path(path));
	}


	void saveUniverseAs(const char* basename, bool save_path)
	{
		m_editor->saveUniverse(basename, save_path);
	}


	void saveUniverse()
	{
		save();
	}


	bool runTest(const char* dir, const char* name)
	{
		return m_editor->runTest(dir, name);
	}


	void createLua()
	{
		lua_State* L = m_engine->getState();

		LuaWrapper::createSystemVariable(L, "Editor", "editor", this);

		#define REGISTER_FUNCTION(F) \
			do { \
				auto f = &LuaWrapper::wrapMethodClosure<StudioAppImpl, decltype(&StudioAppImpl::F), &StudioAppImpl::F>; \
				LuaWrapper::createSystemClosure(L, "Editor", this, #F, f); \
			} while(false) \


		REGISTER_FUNCTION(savePrefabAs);
		REGISTER_FUNCTION(selectEntity);
		REGISTER_FUNCTION(createEntity);
		REGISTER_FUNCTION(createComponent);
		REGISTER_FUNCTION(destroyEntity);
		REGISTER_FUNCTION(newUniverse);
		REGISTER_FUNCTION(saveUniverse);
		REGISTER_FUNCTION(saveUniverseAs);
		REGISTER_FUNCTION(runTest);
		REGISTER_FUNCTION(executeUndoStack);
		REGISTER_FUNCTION(exitWithCode);
		REGISTER_FUNCTION(exitGameMode);

		#undef REGISTER_FUNCTION

		LuaWrapper::createSystemFunction(L, "Editor", "getResources", &getResources);
		LuaWrapper::createSystemFunction(L, "Editor", "createEntityEx", &createEntityEx);
	}


	void checkScriptCommandLine()
	{
		char command_line[1024];
		getCommandLine(command_line, lengthOf(command_line));
		CommandLineParser parser(command_line);
		while (parser.next())
		{
			if (parser.currentEquals("-run_script"))
			{
				if (!parser.next()) break;
				char tmp[MAX_PATH_LENGTH];
				parser.getCurrent(tmp, lengthOf(tmp));
				FS::OsFile file;
				if (file.open(tmp, FS::Mode::OPEN_AND_READ))
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
					g_log_error.log("Editor") << "Could not open " << tmp;
				}
				break;
			}
		}
	}


	static bool includeFileInPack(const char* filename)
	{
		if (filename[0] == '.') return false;
		if (compareStringN("bin/", filename, 4) == 0) return false;
		if (compareStringN("bin32/", filename, 4) == 0) return false;
		if (equalStrings("data.pak", filename)) return false;
		if (equalStrings("error.log", filename)) return false;
		return true;
	}


	static bool includeDirInPack(const char* filename)
	{
		if (filename[0] == '.') return false;
		if (compareStringN("bin", filename, 4) == 0) return false;
		if (compareStringN("bin32", filename, 4) == 0) return false;
		return true;
	}


	#pragma pack(1)
	struct PackFileInfo
	{
		u32 hash;
		u64 offset;
		u64 size;

		char path[MAX_PATH_LENGTH];
	};
	#pragma pack()


	void packDataScan(const char* dir_path, AssociativeArray<u32, PackFileInfo>& infos)
	{
		auto* iter = PlatformInterface::createFileIterator(dir_path, m_allocator);
		PlatformInterface::FileInfo info;
		while (PlatformInterface::getNextFile(iter, &info))
		{
			char normalized_path[MAX_PATH_LENGTH];
			PathUtils::normalize(info.filename, normalized_path, lengthOf(normalized_path));
			if (info.is_directory)
			{
				if (!includeDirInPack(normalized_path)) continue;

				char dir[MAX_PATH_LENGTH] = {0};
				if (dir_path[0] != '.') copyString(dir, dir_path);
				catString(dir, info.filename);
				catString(dir, "/");
				packDataScan(dir, infos);
				continue;
			}

			if(!includeFileInPack(normalized_path)) continue;

			StaticString<MAX_PATH_LENGTH> out_path;
			if(dir_path[0] == '.')
			{
				copyString(out_path.data, normalized_path);
			}
			else
			{
				copyString(out_path.data, dir_path);
				catString(out_path.data, normalized_path);
			}
			u32 hash = crc32(out_path.data);
			if (infos.find(hash) >= 0) continue;

			auto& out_info = infos.emplace(hash);
			copyString(out_info.path, out_path);
			out_info.hash = hash;
			out_info.size = PlatformInterface::getFileSize(out_path.data);
			out_info.offset = ~0UL;
		}
		PlatformInterface::destroyFileIterator(iter);
	}


	void packDataScanResources(AssociativeArray<u32, PackFileInfo>& infos)
	{
		ResourceManager& rm = m_editor->getEngine().getResourceManager();
		for (auto iter = rm.getAll().begin(), end = rm.getAll().end(); iter != end; ++iter)
		{
			const auto& resources = iter.value()->getResourceTable();
			for (Resource* res : resources)
			{
				u32 hash = crc32(res->getPath().c_str());
				auto& out_info = infos.emplace(hash);
				copyString(out_info.path, MAX_PATH_LENGTH, res->getPath().c_str());
				out_info.hash = hash;
				out_info.size = PlatformInterface::getFileSize(res->getPath().c_str());
				out_info.offset = ~0UL;
			}
		}
		packDataScan("pipelines/", infos);
		StaticString<MAX_PATH_LENGTH> unv_path;
		unv_path << "universes/" << m_editor->getUniverse()->getName() << "/";
		packDataScan(unv_path, infos);
		unv_path.data[0] = 0;
		unv_path << "universes/" << m_editor->getUniverse()->getName() << ".unv";
		u32 hash = crc32(unv_path);
		auto& out_info = infos.emplace(hash);
		copyString(out_info.path, MAX_PATH_LENGTH, unv_path);
		out_info.hash = hash;
		out_info.size = PlatformInterface::getFileSize(unv_path);
		out_info.offset = ~0UL;
		
	}


	void showPackDataDialog()
	{
		m_is_pack_data_dialog_open = true;
	}


	void onPackDataGUI()
	{
		if (ImGui::BeginDock("Pack data", &m_is_pack_data_dialog_open))
		{
			ImGui::LabelText("Destination dir", "%s", m_pack.dest_dir.data);
			ImGui::SameLine();
			if (ImGui::Button("Choose dir"))
			{
				if (PlatformInterface::getOpenDirectory(m_pack.dest_dir.data, lengthOf(m_pack.dest_dir.data), "."))
				{
					m_pack.dest_dir << "/";
				}
			}

			ImGui::Combo("Mode", (int*)&m_pack.mode, "All files\0Loaded universe\0");

			if (ImGui::Button("Pack")) packData();
		}
		ImGui::EndDock();
	}


	void packData()
	{
		if (m_pack.dest_dir.empty()) return;

		char dest[MAX_PATH_LENGTH];

		static const char* OUT_FILENAME = "data.pak";
		copyString(dest, m_pack.dest_dir);
		catString(dest, OUT_FILENAME);
		AssociativeArray<u32, PackFileInfo> infos(m_allocator);
		infos.reserve(10000);
		
		switch (m_pack.mode)
		{
			case PackConfig::Mode::ALL_FILES: packDataScan("./", infos); break;
			case PackConfig::Mode::CURRENT_UNIVERSE: packDataScanResources(infos); break;
			default: ASSERT(false); break;
		}
		
		if (infos.size() == 0)
		{
			g_log_error.log("Editor") << "No files found while trying to create " << dest;
			return;
		}

		FS::OsFile file;
		if (!file.open(dest, FS::Mode::CREATE_AND_WRITE))
		{
			g_log_error.log("Editor") << "Could not create " << dest;
			return;
		}

		int count = infos.size();
		file.write(&count, sizeof(count));
		u64 offset = sizeof(count) + (sizeof(u32) + sizeof(u64) * 2) * count;
		for (auto& info : infos)
		{
			info.offset = offset;
			offset += info.size;
		}

		for (auto& info : infos)
		{
			file.write(&info.hash, sizeof(info.hash));
			file.write(&info.offset, sizeof(info.offset));
			file.write(&info.size, sizeof(info.size));
		}

		for (auto& info : infos)
		{
			FS::OsFile src;
			size_t src_size = PlatformInterface::getFileSize(info.path);
			if (!src.open(info.path, FS::Mode::OPEN_AND_READ))
			{
				file.close();
				g_log_error.log("Editor") << "Could not open " << info.path;
				return;
			}
			u8 buf[4096];
			for (; src_size > 0; src_size -= Math::minimum(sizeof(buf), src_size))
			{
				size_t batch_size = Math::minimum(sizeof(buf), src_size);
				if (!src.read(buf, batch_size))
				{
					file.close();
					g_log_error.log("Editor") << "Could not read " << info.path;
					return;
				}
				file.write(buf, batch_size);
			}
			src.close();
		}

		file.close();

		const char* bin_files[] = {
			"app.exe",
			"nvToolsExt64_1.dll",
			"PhysX3CharacterKinematicCHECKED_x64.dll",
			"PhysX3CHECKED_x64.dll",
			"PhysX3CommonCHECKED_x64.dll",
			"PhysX3CookingCHECKED_x64.dll"
			"dbghelp.dll",
			"dbgcore.dll"
		};
		for(auto& file : bin_files)
		{
			StaticString<MAX_PATH_LENGTH> tmp(m_pack.dest_dir, file);
			StaticString<MAX_PATH_LENGTH> src("bin/", file);
			if (!copyFile(src, tmp))
			{
				g_log_error.log("Editor") << "Failed to copy " << src << " to " << tmp;
			}
		}
		StaticString<MAX_PATH_LENGTH> tmp(m_pack.dest_dir);
		tmp << "startup.lua";
		if (!copyFile("startup.lua", tmp))
		{
			g_log_error.log("Editor") << "Failed to copy startup.lua to " << tmp;
		}
	}


	void loadLuaPlugin(const char* dir, const char* filename)
	{
		StaticString<MAX_PATH_LENGTH> path(dir, filename);
		FS::OsFile file;

		if (file.open(path, FS::Mode::OPEN_AND_READ))
		{
			auto size = file.size();
			auto* src = (char*)m_engine->getLIFOAllocator().allocate(size + 1);
			file.read(src, size);
			src[size] = 0;
			
			LuaPlugin* plugin = LUMIX_NEW(m_editor->getAllocator(), LuaPlugin)(*this, src, filename);
			addPlugin(*plugin);

			m_engine->getLIFOAllocator().deallocate(src);
			file.close();
		}
		else
		{
			g_log_warning.log("Editor") << "Failed to open " << path;
		}
	}


	void scanUniverses()
	{
		auto* iter = PlatformInterface::createFileIterator("universes/", m_allocator);
		PlatformInterface::FileInfo info;
		while (PlatformInterface::getNextFile(iter, &info))
		{
			if (info.filename[0] == '.') continue;
			if (!info.is_directory) continue;
			if (startsWith(info.filename, "__")) continue;

			char basename[MAX_PATH_LENGTH];
			PathUtils::getBasename(basename, lengthOf(basename), info.filename);
			m_universes.emplace(basename);
		}
		PlatformInterface::destroyFileIterator(iter);
	}


	void findLuaPlugins(const char* dir)
	{
		auto* iter = PlatformInterface::createFileIterator(dir, m_allocator);
		PlatformInterface::FileInfo info;
		while (PlatformInterface::getNextFile(iter, &info))
		{
			char normalized_path[MAX_PATH_LENGTH];
			PathUtils::normalize(info.filename, normalized_path, lengthOf(normalized_path));
			if (normalized_path[0] == '.') continue;
			if (info.is_directory)
			{
				char dir_path[MAX_PATH_LENGTH] = { 0 };
				if (dir[0] != '.') copyString(dir_path, dir);
				catString(dir_path, info.filename);
				catString(dir_path, "/");
				findLuaPlugins(dir_path);
			}
			else
			{
				char ext[5];
				PathUtils::getExtension(ext, lengthOf(ext), info.filename);
				if (equalStrings(ext, "lua"))
				{
					loadLuaPlugin(dir, info.filename);
				}
			}
		}
		PlatformInterface::destroyFileIterator(iter);
	}


	const SDL_Event* getEvents() const override
	{
		return m_events.empty() ? nullptr : &m_events[0];
	}


	int getEventsCount() const override { return m_events.size(); }


	Vec2 getMouseMove() const override { return m_mouse_move; }


	void processSystemEvents()
	{
		m_mouse_move.set(0, 0);
		m_events.clear();
		SDL_Event event;
		auto& io = ImGui::GetIO();
		while (SDL_PollEvent(&event))
		{
			m_events.push(event);
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
					m_editor->setToggleSelection(io.KeyCtrl);
					m_editor->setSnapMode(io.KeyShift, io.KeyCtrl);
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
					input_system.setCursorPosition({(float)event.motion.x, (float)event.motion.y});
					m_mouse_move += {(float)event.motion.xrel, (float)event.motion.yrel};
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
					int scancode = event.key.keysym.scancode;
					io.KeysDown[scancode] = (event.type == SDL_KEYDOWN);
					if (event.key.keysym.scancode == SDL_SCANCODE_KP_ENTER)
					{
						io.KeysDown[SDLK_RETURN] = (event.type == SDL_KEYDOWN);
					}
					io.KeyShift = ((SDL_GetModState() & KMOD_SHIFT) != 0);
					io.KeyCtrl = ((SDL_GetModState() & KMOD_CTRL) != 0);
					io.KeyAlt = ((SDL_GetModState() & KMOD_ALT) != 0);
					checkShortcuts();
				}
				break;
				case SDL_DROPFILE:
					for (IPlugin* plugin : m_plugins)
					{
						if (plugin->onDropFile(event.drop.file)) break;
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

		Timer* timer = Timer::create(m_allocator);
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

				if (m_sleep_when_inactive && (SDL_GetWindowFlags(m_window) & SDL_WINDOW_INPUT_FOCUS) == 0)
				{
					float wanted_fps = 5.0f;
					if (frame_time < 1 / wanted_fps)
					{
						PROFILE_BLOCK("sleep");
						MT::sleep(u32(1000 / wanted_fps - frame_time * 1000));
					}

				}
			}
			Profiler::frame();
		}
		Timer::destroy(timer);
	}


	static void checkWorkingDirector()
	{
		if (!PlatformInterface::fileExists("../LumixStudio.lnk")) return;

		if (!PlatformInterface::dirExists("bin") && PlatformInterface::dirExists("../bin") &&
			PlatformInterface::dirExists("../pipelines"))
		{
			PlatformInterface::setCurrentDirectory("../");
		}

		if (!PlatformInterface::dirExists("bin"))
		{
			messageBox("Bin directory not found, please check working directory.");
		}
		else if (!PlatformInterface::dirExists("pipelines"))
		{
			messageBox("Pipelines directory not found, please check working directory.");
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
			char tmp[MAX_PATH_LENGTH];
			action->getIconPath(tmp, lengthOf(tmp));
			if (PlatformInterface::fileExists(tmp))
			{
				action->icon = render_interface.loadTexture(Path(tmp));
			}
			else
			{
				action->icon = nullptr;
			}
		}
	}


	void checkShortcuts()
	{
		if (ImGui::IsAnyItemActive()) return;
		IPlugin* plugin = getFocusedPlugin();

		int key_count;
		auto* state = SDL_GetKeyboardState(&key_count);
		u32 pressed_modifiers = SDL_GetModState() & (KMOD_CTRL | KMOD_ALT | KMOD_SHIFT);
		for (auto* a : m_actions)
		{
			if (!a->is_global || a->shortcut[0] == -1) continue;
			if (a->plugin != plugin) continue; 

			u32 action_modifiers = 0;
			for (int i = 0; i < lengthOf(a->shortcut) + 1; ++i)
			{
				if ((i == lengthOf(a->shortcut) || a->shortcut[i] == -1) &&
					action_modifiers == pressed_modifiers)
				{
					a->func.invoke();
					return;
				}

				if (i == lengthOf(a->shortcut)) break;
				if (a->shortcut[i] == -1) break;

				SDL_Scancode scancode = SDL_GetScancodeFromKey(a->shortcut[i]);

				if (scancode >= key_count) break;
				if (!state[scancode]) break;
				if (a->shortcut[i] == SDLK_LCTRL) action_modifiers |= KMOD_LCTRL;
				else if (a->shortcut[i] == SDLK_LALT) action_modifiers |= KMOD_LALT;
				else if (a->shortcut[i] == SDLK_LSHIFT) action_modifiers |= KMOD_LSHIFT;
				else if (a->shortcut[i] == SDLK_RCTRL) action_modifiers |= KMOD_RCTRL;
				else if (a->shortcut[i] == SDLK_RALT) action_modifiers |= KMOD_RALT;
				else if (a->shortcut[i] == SDLK_RSHIFT) action_modifiers |= KMOD_RSHIFT;
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


	static void clearInputs()
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


	WorldEditor& getWorldEditor() override
	{
		ASSERT(m_editor);
		return *m_editor;
	}


	ImFont* getBoldFont() override
	{
		return m_bold_font;
	}


	DefaultAllocator m_main_allocator;
	Debug::Allocator m_allocator;
	Engine* m_engine;
	SDL_Window* m_window;

	Array<Action*> m_actions;
	Array<Action*> m_window_actions;
	Array<Action*> m_toolbar_actions;
	Array<IPlugin*> m_plugins;
	Array<IAddComponentPlugin*> m_add_cmp_plugins;
	Array<StaticString<MAX_PATH_LENGTH>> m_universes;
	AddCmpTreeNode m_add_cmp_root;
	HashMap<ComponentType, string> m_component_labels;
	WorldEditor* m_editor;
	Action* m_custom_pivot_action;
	bool m_confirm_exit;
	bool m_confirm_load;
	bool m_confirm_new;
	char m_universe_to_load[MAX_PATH_LENGTH];
	AssetBrowser* m_asset_browser;
	PropertyGrid* m_property_grid;
	LogUI* m_log_ui;
	ProfilerUI* m_profiler_ui;
	Settings m_settings;
	Metadata m_metadata;
	Array<SDL_Event> m_events;
	Vec2 m_mouse_move;
	char m_template_name[100];
	char m_open_filter[64];
	char m_component_filter[32];

	struct PackConfig
	{
		enum class Mode : int
		{
			ALL_FILES,
			CURRENT_UNIVERSE
		};

		Mode mode;
		StaticString<MAX_PATH_LENGTH> dest_dir;
	};

	PackConfig m_pack;
	bool m_finished;
	bool m_deferred_game_mode_exit;
	int m_exit_code;

	bool m_sleep_when_inactive;
	bool m_is_welcome_screen_open;
	bool m_is_pack_data_dialog_open;
	bool m_is_entity_list_open;
	bool m_is_save_as_dialog_open;
	ImFont* m_font;
	ImFont* m_bold_font;
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
	return new (NewPlaceholder(), alignPtr(buf, ALIGN_OF(StudioAppImpl))) StudioAppImpl;
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


void StudioApp::StaticPluginRegister::create(StudioApp& app)
{
	auto* i = s_first_plugin;
	while (i)
	{
		i->creator(app);
		i = i->next;
	}
}


} // namespace Lumix