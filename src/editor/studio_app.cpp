#include <imgui/imgui.h>
#include <imgui/imnodes.h>

#include "audio/audio_scene.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/file_system_watcher.h"
#include "editor/gizmo.h"
#include "editor/prefab_system.h"
#include "editor/render_interface.h"
#include "editor/world_editor.h"
#include "engine/allocators.h"
#include "engine/associative_array.h"
#include "engine/atomic.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/debug.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/geometry.h"
#include "engine/input_system.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/lz4.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/universe.h"
#include "log_ui.h"
#include "profiler_ui.h"
#include "property_grid.h"
#include "settings.h"
#include "studio_app.h"
#include "utils.h"

#ifdef _WIN32
	#include "engine/win/simple_win.h"
#endif

namespace Lumix
{

#define LUMIX_EDITOR_PLUGINS_DECLS
#include "engine/plugins.inl"
#undef LUMIX_EDITOR_PLUGINS_DECLS
	

struct TarHeader {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char chksum[8];
	char typeflag;
	char linkname[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char devmajor[8];
	char devminor[8];
	char prefix[155];   
};


#define NO_ICON "     "


struct LuaPlugin : StudioApp::GUIPlugin
{
	LuaPlugin(StudioApp& app, Span<const char> src, const char* filename)
		: m_app(app)
	{
		L = lua_newthread(app.getEngine().getState());						 // [thread]
		m_thread_ref = luaL_ref(app.getEngine().getState(), LUA_REGISTRYINDEX); // []

		lua_newtable(L);						  // [env]
												  // reference environment
		lua_pushvalue(L, -1);					  // [env, env]
		m_env_ref = luaL_ref(L, LUA_REGISTRYINDEX); // [env]

		// environment's metatable & __index
		lua_pushvalue(L, -1);	// [env, env]
		lua_setmetatable(L, -2); // [env]
		lua_pushvalue(L, LUA_GLOBALSINDEX);
		lua_setfield(L, -2, "__index"); // [env]

		bool errors = luaL_loadbuffer(L, src.m_begin, src.length(), filename) != 0; // [env, func]

		lua_pushvalue(L, -2); // [env, func, env]
		lua_setfenv(L, -2);   // function's environment [env, func]

		errors = errors || lua_pcall(L, 0, 0, 0) != 0; // [env]
		if (errors)
		{
			logError(filename, ": ", lua_tostring(L, -1));
			lua_pop(L, 1);
		}

		lua_getfield(L, -1, "plugin_name"); // [env, plugin_name]
		const char* name = "LuaPlugin";
		if (lua_type(L, -1) == LUA_TSTRING) {
			name = lua_tostring(L, -1);
		}

		lua_pop(L, 2); // []

		m_toggle_ui.init(name, name, name, "", true);
		m_toggle_ui.func.bind<&LuaPlugin::onAction>(this);
		app.addWindowAction(&m_toggle_ui);
		m_is_open = false;

		lua_pop(L, 1); // plugin_name
	}


	~LuaPlugin()
	{
		m_app.removeAction(&m_toggle_ui);
		lua_State* L = m_app.getEngine().getState();
		luaL_unref(L, LUA_REGISTRYINDEX, m_env_ref);
		luaL_unref(L, LUA_REGISTRYINDEX, m_thread_ref);
	}


	const char* getName() const override { return "lua_script"; }


	void onAction() { m_is_open = !m_is_open; }


	void onWindowGUI() override
	{
		if (!m_is_open) return;

		lua_rawgeti(L, LUA_REGISTRYINDEX, m_env_ref); // [env]
		lua_getfield(L, -1, "onGUI"); // [env, onGUI]
		if (lua_type(L, -1) == LUA_TFUNCTION) {
			if (lua_pcall(L, 0, 0, 0) != 0) {
				logError("LuaPlugin:", lua_tostring(L, -1));
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
		}
		else {
			lua_pop(L, 2);
		}
	}

	StudioApp& m_app;
	lua_State* L;
	int m_thread_ref;
	int m_env_ref;
	bool m_is_open;
	Action m_toggle_ui;
};


struct StudioAppImpl final : StudioApp
{
	StudioAppImpl()
		: m_is_entity_list_open(true)
		, m_finished(false)
		, m_deferred_game_mode_exit(false)
		, m_actions(m_allocator)
		, m_owned_actions(m_allocator)
		, m_window_actions(m_allocator)
		, m_toolbar_actions(m_allocator)
		, m_is_welcome_screen_open(true)
		, m_is_pack_data_dialog_open(false)
		, m_settings(*this)
		, m_gui_plugins(m_allocator)
		, m_mouse_plugins(m_allocator)
		, m_plugins(m_allocator)
		, m_add_cmp_plugins(m_allocator)
		, m_component_labels(m_allocator)
		, m_component_icons(m_allocator)
		, m_confirm_load(false)
		, m_confirm_new(false)
		, m_confirm_exit(false)
		, m_exit_code(0)
		, m_allocator(m_main_allocator)
		, m_universes(m_allocator)
		, m_events(m_allocator)
		, m_windows(m_allocator)
		, m_deferred_destroy_windows(m_allocator)
	{
		u32 cpus_count = minimum(os::getCPUsCount(), 64);
		u32 workers;
		if (workersCountOption(Ref(workers))) {
			cpus_count = workers;
		}
		if (!JobSystem::init(cpus_count, m_allocator)) {
			logError("Failed to initialize job system.");
		}
	}


	void onEvent(const os::Event& event)
	{
		const bool handle_input = isFocused();
		m_events.push(event);
		switch (event.type) {
			case os::Event::Type::MOUSE_MOVE: break;
			case os::Event::Type::FOCUS: break;
			case os::Event::Type::MOUSE_BUTTON: {
				ImGuiIO& io = ImGui::GetIO();
				m_editor->getView().setSnapMode(io.KeyShift, io.KeyCtrl);
				if (handle_input || !event.mouse_button.down) {
					io.MouseDown[(int)event.mouse_button.button] = event.mouse_button.down;
				}
				break;
			}
			case os::Event::Type::MOUSE_WHEEL:
				if (handle_input) {
					ImGuiIO& io = ImGui::GetIO();
					io.MouseWheel = event.mouse_wheel.amount;
				}
				break;
			case os::Event::Type::WINDOW_SIZE:
				if (ImGui::GetCurrentContext()) {
					ImGuiViewport* vp = ImGui::FindViewportByPlatformHandle(event.window);
					if (vp) vp->PlatformRequestResize = true;
				}
				if (event.window == m_main_window && event.win_size.h > 0 && event.win_size.w > 0) {
					m_settings.m_window.w = event.win_size.w;
					m_settings.m_window.h = event.win_size.h;
					m_settings.m_is_maximized = os::isMaximized(m_main_window);
				}
				break;
			case os::Event::Type::WINDOW_MOVE:
				if (ImGui::GetCurrentContext()) {
					ImGuiViewport* vp = ImGui::FindViewportByPlatformHandle(event.window);
					if (vp) vp->PlatformRequestMove = true;
				}
				if (event.window == m_main_window) {
					m_settings.m_window.x = event.win_move.x;
					m_settings.m_window.y = event.win_move.y;
					m_settings.m_is_maximized = os::isMaximized(m_main_window);
				}
				break;
			case os::Event::Type::WINDOW_CLOSE: {
				ImGuiViewport* vp = ImGui::FindViewportByPlatformHandle(event.window);
				if (vp) vp->PlatformRequestClose = true;
				if (event.window == m_main_window) exit();
				break;
			}
			case os::Event::Type::QUIT:
				exit();
				break;
			case os::Event::Type::CHAR:
				if (handle_input) {
					ImGuiIO& io = ImGui::GetIO();
					char tmp[5] = {};
					memcpy(tmp, &event.text_input.utf8, sizeof(event.text_input.utf8));
					io.AddInputCharactersUTF8(tmp);
				}
				break;
			case os::Event::Type::KEY:
				if (handle_input) {
					ImGuiIO& io = ImGui::GetIO();
					io.KeysDown[(int)event.key.keycode] = event.key.down;
					io.KeyShift = os::isKeyDown(os::Keycode::SHIFT);
					io.KeyCtrl = os::isKeyDown(os::Keycode::CTRL);
					io.KeyAlt = os::isKeyDown(os::Keycode::MENU);

					if (event.key.down && event.key.keycode == os::Keycode::F2) {
						m_is_f2_pressed = true;
					}
					checkShortcuts();
				}
				break;
			case os::Event::Type::DROP_FILE:
				for(int i = 0, c = os::getDropFileCount(event); i < c; ++i) {
					char tmp[MAX_PATH_LENGTH];
					os::getDropFile(event, i, Span(tmp));
					if (!m_asset_browser->onDropFile(tmp)) {
						for (GUIPlugin* plugin : m_gui_plugins) {
							if (plugin->onDropFile(tmp)) break;
						}
					}
				}
				break;
			default: ASSERT(false); break;
		}
	}

	bool isFocused() const {
		const os::WindowHandle focused = os::getFocused();
		const int idx = m_windows.find([focused](os::WindowHandle w){ return w == focused; });
		return idx >= 0;
	}

	void onIdle()
	{
		update();

		if (m_sleep_when_inactive && !isFocused()) {
			const float frame_time = m_inactive_fps_timer.tick();
			const float wanted_fps = 5.0f;

			if (frame_time < 1 / wanted_fps) {
				PROFILE_BLOCK("sleep");
				os::sleep(u32(1000 / wanted_fps - frame_time * 1000));
			}
			m_inactive_fps_timer.tick();
		}

		Profiler::frame();
		m_events.clear();
		m_is_f2_pressed = false;
	}


	void run() override
	{
		Profiler::setThreadName("Main thread");
		Semaphore semaphore(0, 1);
		struct Data {
			StudioAppImpl* that;
			Semaphore* semaphore;
		} data = {this, &semaphore};
		JobSystem::runEx(&data, [](void* ptr) {
			Data* data = (Data*)ptr;
			data->that->onInit();
			while (!data->that->m_finished) {
				os::Event e;
				while(os::getEvent(Ref(e))) {
					data->that->onEvent(e);
				}
				data->that->onIdle();
			}

			data->semaphore->signal();
		}, nullptr, JobSystem::INVALID_HANDLE, 0);
		PROFILE_BLOCK("sleeping");
		semaphore.wait();
	}

	
	static void* imguiAlloc(size_t size, void* user_data) {
		StudioAppImpl* app = (StudioAppImpl*)user_data;
		return app->m_allocator.allocate(size);
	}


	static void imguiFree(void* ptr, void* user_data) {
		StudioAppImpl* app = (StudioAppImpl*)user_data;
		return app->m_allocator.deallocate(ptr);
	}


	void onInit()
	{
		os::Timer init_timer;

		m_add_cmp_root.label[0] = '\0';
		m_template_name[0] = '\0';
		m_open_filter[0] = '\0';

		checkWorkingDirectory();

		char saved_data_dir[MAX_PATH_LENGTH] = {};
		os::InputFile cfg_file;
		if (cfg_file.open(".lumixuser")) {
			if (!cfg_file.read(saved_data_dir, minimum(lengthOf(saved_data_dir), (int)cfg_file.size()))) {
				logError("Failed to read .lumixuser");
			}
			cfg_file.close();
		}

		char current_dir[MAX_PATH_LENGTH];
		os::getCurrentDirectory(Span(current_dir));

		char data_dir[MAX_PATH_LENGTH] = {};
		checkDataDirCommandLine(data_dir, lengthOf(data_dir));

		Engine::InitArgs init_data = {};
		init_data.handle_file_drops = true;
		init_data.window_title = "Lumix Studio";
		init_data.working_dir = data_dir[0] ? data_dir : (saved_data_dir[0] ? saved_data_dir : current_dir);
		m_engine = Engine::create(static_cast<Engine::InitArgs&&>(init_data), m_allocator);
		m_main_window = m_engine->getWindowHandle();
		m_windows.push(m_main_window);

		createLua();
		extractBundled();

		m_editor = WorldEditor::create(*m_engine, m_allocator);
		scanUniverses();
		loadUserPlugins();
		addActions();

		m_asset_compiler = AssetCompiler::create(*this);
		m_asset_browser.create(*this);
		m_property_grid.create(*this);
		m_profiler_ui = ProfilerUI::create(*m_engine);
		m_log_ui.create(m_editor->getAllocator());

		ImGui::SetAllocatorFunctions(imguiAlloc, imguiFree, this);
		ImGui::CreateContext();
		imnodes::Initialize();
		loadSettings();
		initIMGUI();

		m_set_pivot_action.init("Set custom pivot",
			"Set custom pivot",
			"set_custom_pivot",
			"",
			os::Keycode::K,
			0,
			false);
		addAction(&m_set_pivot_action);

		m_reset_pivot_action.init("Reset pivot",
			"Reset pivot",
			"reset_pivot",
			"",
			os::Keycode::K,
			(u8)Action::Modifiers::SHIFT,
			false);
		addAction(&m_reset_pivot_action);

		setStudioApp();
		loadSettings();
		loadUniverseFromCommandLine();
		findLuaPlugins("plugins/lua/");

		m_asset_compiler->onInitFinished();
		m_asset_browser->onInitFinished();
		m_sleep_when_inactive = shouldSleepWhenInactive();

		checkScriptCommandLine();

		logInfo("Startup took ", init_timer.getTimeSinceStart(), " s"); 
	}


	~StudioAppImpl()
	{
		m_asset_browser->releaseResources();
		removeAction(&m_set_pivot_action);
		removeAction(&m_reset_pivot_action);
		m_watched_plugin.watcher.reset();

		saveSettings();

		while (m_engine->getFileSystem().hasWork()) {
			m_engine->getFileSystem().processCallbacks();
		}

		m_editor->newUniverse();

		destroyAddCmpTreeNode(m_add_cmp_root.child);

		for (auto* i : m_plugins) {
			LUMIX_DELETE(m_editor->getAllocator(), i);
		}
		m_plugins.clear();

		for (auto* i : m_gui_plugins) {
			LUMIX_DELETE(m_editor->getAllocator(), i);
		}
		m_gui_plugins.clear();

		PrefabSystem::destroyEditorPlugins(*this);
		ASSERT(m_mouse_plugins.empty());

		for (auto* i : m_add_cmp_plugins)
		{
			LUMIX_DELETE(m_editor->getAllocator(), i);
		}
		m_add_cmp_plugins.clear();

		m_profiler_ui.reset();
		m_asset_browser.destroy();
		m_property_grid.destroy();
		m_log_ui.destroy();
		ASSERT(!m_render_interface);
		m_asset_compiler.reset();
		m_editor.reset();

		for (Action* action : m_owned_actions) {
			removeAction(action);
			LUMIX_DELETE(m_allocator, action);
		}
		m_owned_actions.clear();
		ASSERT(m_actions.empty());
		m_actions.clear();

		m_engine.reset();
		
		JobSystem::shutdown();
	}


	bool makeFile(const char* path, const char* content) override
	{
		os::OutputFile file;
		if (!m_engine->getFileSystem().open(path, Ref(file))) return false;
		file << content;
		file.close();
		return file.isError();
	}


	void destroyAddCmpTreeNode(AddCmpTreeNode* node)
	{
		if (!node) return;
		destroyAddCmpTreeNode(node->child);
		destroyAddCmpTreeNode(node->next);
		LUMIX_DELETE(m_allocator, node);
	}

	const char* getComponentIcon(ComponentType cmp_type) const override
	{
		auto iter = m_component_icons.find(cmp_type);
		if (iter == m_component_icons.end()) return "";
		return iter.value();
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
		copyNString(Span(new_group->label), node->label, int(slash - node->label));
		insertAddCmpNodeOrdered(parent, new_group);
		insertAddCmpNode(*new_group, node);
	}

	void registerComponent(const char* icon, ComponentType cmp_type, const char* label, ResourceType resource_type, const char* property) {
		struct Plugin final : IAddComponentPlugin {
			void onGUI(bool create_entity, bool from_filter) override {
				ImGui::SetNextWindowSize(ImVec2(300, 300));
				const char* last = reverseFind(label, nullptr, '/');
				last = last && !from_filter ? last + 1 : label;
				if (last[0] == ' ') ++last;
				if (!ImGui::BeginMenu(last)) return;
				char buf[MAX_PATH_LENGTH];
				bool create_empty = ImGui::MenuItem("Empty");
				static u32 selected_res_hash = 0;
				if (asset_browser->resourceList(Span(buf), Ref(selected_res_hash), resource_type, 0, true) || create_empty) {
					if (create_entity) {
						EntityRef entity = editor->addEntity();
						editor->selectEntities(Span(&entity, 1), false);
					}

					const Array<EntityRef>& selected_entites = editor->getSelectedEntities();
					editor->addComponent(selected_entites, type);
					if (!create_empty) {
						editor->setProperty(type, "", -1, property, editor->getSelectedEntities(), Path(buf));
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndMenu();
			}


			const char* getLabel() const override { return label; }

			PropertyGrid* property_grid;
			AssetBrowser* asset_browser;
			WorldEditor* editor;
			ComponentType type;
			ResourceType resource_type;
			StaticString<64> property;
			char label[50];
		};

		auto& allocator = m_editor->getAllocator();
		auto* plugin = LUMIX_NEW(allocator, Plugin);
		plugin->property_grid = m_property_grid.get();
		plugin->asset_browser = m_asset_browser.get();
		plugin->type = cmp_type;
		plugin->editor = m_editor.get();
		plugin->property = property;
		plugin->resource_type = resource_type;
		copyString(plugin->label, label);
		addPlugin(*plugin);

		m_component_labels.insert(plugin->type, String(label, m_allocator));
		if (icon && icon[0]) {
			m_component_icons.insert(plugin->type, icon);
		}
	}

	void registerComponent(const char* icon, const char* id, IAddComponentPlugin& plugin) override {
		addPlugin(plugin);
		m_component_labels.insert(reflection::getComponentType(id), String(plugin.getLabel(), m_allocator));
		if (icon && icon[0]) {
			m_component_icons.insert(reflection::getComponentType(id), icon);
		}
	}

	void registerComponent(const char* icon, ComponentType type, const char* label)
	{
		struct Plugin final : IAddComponentPlugin
		{
			void onGUI(bool create_entity, bool from_filter) override
			{
				const char* last = reverseFind(label, nullptr, '/');
				last = last && !from_filter ? last + 1 : label;
				if (last[0] == ' ') ++last;
				if (ImGui::MenuItem(last))
				{
					if (create_entity)
					{
						EntityRef entity = editor->addEntity();
						editor->selectEntities(Span(&entity, 1), false);
					}

					editor->addComponent(editor->getSelectedEntities(), type);
				}
			}

			const char* getLabel() const override { return label; }

			WorldEditor* editor;
			PropertyGrid* property_grid;
			ComponentType type;
			char label[64];
		};

		auto& allocator = m_editor->getAllocator();
		auto* plugin = LUMIX_NEW(allocator, Plugin);
		plugin->property_grid = m_property_grid.get();
		plugin->editor = m_editor.get();
		plugin->type = type;
		copyString(plugin->label, label);
		addPlugin(*plugin);

		m_component_labels.insert(plugin->type, String(label, m_allocator));
		if (icon && icon[0]) {
			m_component_icons.insert(plugin->type, icon);
		}
	}


	const Array<Action*>& getActions() override { return m_actions; }


	Array<Action*>& getToolbarActions() override { return m_toolbar_actions; }


	void guiBeginFrame() const
	{
		PROFILE_FUNCTION();

		ImGuiIO& io = ImGui::GetIO();

		updateIMGUIMonitors();
		const os::Rect rect = os::getWindowClientRect(m_main_window);
		if (rect.width > 0 && rect.height > 0) {
			io.DisplaySize = ImVec2(float(rect.width), float(rect.height));
		}
		else if(io.DisplaySize.x <= 0) {
			io.DisplaySize.x = 800;
			io.DisplaySize.y = 600;
		}
		io.DeltaTime = m_engine->getLastTimeDelta();
		io.KeyShift = os::isKeyDown(os::Keycode::SHIFT);
		io.KeyCtrl = os::isKeyDown(os::Keycode::CTRL);
		io.KeyAlt = os::isKeyDown(os::Keycode::MENU);

		const os::Point cp = os::getMouseScreenPos();
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			io.MousePos.x = (float)cp.x;
			io.MousePos.y = (float)cp.y;
		}
		else {
			const os::Rect screen_rect = os::getWindowScreenRect(m_main_window);
			io.MousePos.x = (float)cp.x - screen_rect.left;
			io.MousePos.y = (float)cp.y - screen_rect.top;
		}

		const ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
		ImGui::NewFrame();
		if (!m_cursor_captured) {
			switch (imgui_cursor) {
				case ImGuiMouseCursor_Arrow: os::setCursor(os::CursorType::DEFAULT); break;
				case ImGuiMouseCursor_ResizeNS: os::setCursor(os::CursorType::SIZE_NS); break;
				case ImGuiMouseCursor_ResizeEW: os::setCursor(os::CursorType::SIZE_WE); break;
				case ImGuiMouseCursor_ResizeNWSE: os::setCursor(os::CursorType::SIZE_NWSE); break;
				case ImGuiMouseCursor_TextInput: os::setCursor(os::CursorType::TEXT_INPUT); break;
				default: os::setCursor(os::CursorType::DEFAULT); break;
			}
		}
		ImGui::PushFont(m_font);
	}


	void guiEndFrame()
	{
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
									ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
									ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | 
									ImGuiWindowFlags_NoDocking;
		const os::Rect main_win_rect = os::getWindowClientRect(m_main_window);
		const bool has_viewports = ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable;
		const os::Point p = has_viewports ? os::toScreen(m_main_window, main_win_rect.left, main_win_rect.top) : os::Point{0, 0};
		if (m_is_welcome_screen_open) {
			if (main_win_rect.width > 0 && main_win_rect.height > 0) {
				ImGui::SetNextWindowSize(ImVec2((float)main_win_rect.width, (float)main_win_rect.height));
				ImGui::SetNextWindowPos(ImVec2((float)p.x, (float)p.y));
			    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
				ImGui::Begin("MainDockspace", nullptr, flags);
				ImGui::PopStyleVar();
				ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
				ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_KeepAliveOnly);
				ImGui::End();
				showWelcomeScreen();
			}
		}
		else {
			if (main_win_rect.width > 0 && main_win_rect.height > 0) {
				ImGui::SetNextWindowSize(ImVec2((float)main_win_rect.width, (float)main_win_rect.height));
				ImGui::SetNextWindowPos(ImVec2((float)p.x, (float)p.y));
			    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
				ImGui::Begin("MainDockspace", nullptr, flags);
				ImGui::PopStyleVar();
				mainMenu();
				ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
				ImGui::DockSpace(dockspace_id, ImVec2(0, 0));
				ImGui::End();
			
				m_profiler_ui->onGUI();
				m_asset_browser->onGUI();
				m_log_ui->onGUI();
				m_asset_compiler->onGUI();
				m_property_grid->onGUI();
				onEntityListGUI();
				onEditCameraGUI();
				onSaveAsDialogGUI();
				for (auto* plugin : m_gui_plugins)
				{
					plugin->onWindowGUI();
				}
				m_settings.onGUI();
				onPackDataGUI();
			}
		}
		ImGui::PopFont();
		ImGui::Render();
		ImGui::UpdatePlatformWindows();

		for (auto* plugin : m_gui_plugins)
		{
			plugin->guiEndFrame();
		}
	}

	void showGizmos() {
		const Array<EntityRef>& ents = m_editor->getSelectedEntities();
		if (ents.empty()) return;

		Universe* universe = m_editor->getUniverse();

		UniverseView& view = m_editor->getView();
		const DVec3 cam_pos = view.getViewport().pos;
		if (ents.size() > 1) {
			AABB aabb = m_render_interface->getEntityAABB(*universe, ents[0], cam_pos);
			for (int i = 1; i < ents.size(); ++i) {
				const AABB entity_aabb = m_render_interface->getEntityAABB(*universe, ents[i], cam_pos);
				aabb.merge(entity_aabb);
			}

			addCube(view, cam_pos + aabb.min, cam_pos + aabb.max, 0xffffff00);
			return;
		}

		for (ComponentUID cmp = universe->getFirstComponent(ents[0]); cmp.isValid(); cmp = universe->getNextComponent(cmp)) {
			for (auto* plugin : m_plugins) {
				if (plugin->showGizmo(view, cmp)) break;
			}
		}
	}

	void update()
	{
		for (i32 i = m_deferred_destroy_windows.size() - 1; i >= 0; --i) {
			--m_deferred_destroy_windows[i].counter;
			if (m_deferred_destroy_windows[i].counter == 0) {
				os::destroyWindow(m_deferred_destroy_windows[i].window);
				m_deferred_destroy_windows.swapAndPop(i);
			}
		}

		PROFILE_FUNCTION();
		Profiler::blockColor(0x7f, 0x7f, 0x7f);
		m_asset_compiler->update();
		if (m_watched_plugin.reload_request) tryReloadPlugin();

		guiBeginFrame();

		float time_delta = m_engine->getLastTimeDelta();

		ImGuiIO& io = ImGui::GetIO();
		if (!io.KeyShift) {
			m_editor->getView().setSnapMode(false, false);
		}
		else if (io.KeyCtrl) {
			m_editor->getView().setSnapMode(io.KeyShift, io.KeyCtrl);
		}
		if (m_set_pivot_action.isActive()) m_editor->getView().setCustomPivot();
		if (m_reset_pivot_action.isActive()) m_editor->getView().resetPivot();

		m_editor->getView().setMouseSensitivity(m_settings.m_mouse_sensitivity.x, m_settings.m_mouse_sensitivity.y);
		m_editor->update();
		showGizmos();
		
		m_engine->update(*m_editor->getUniverse());

		++m_fps_frame;
		if (m_fps_timer.getTimeSinceTick() > 1.0f) {
			m_fps = m_fps_frame / m_fps_timer.tick();
			m_fps_frame = 0;
		}

		if (m_deferred_game_mode_exit) {
			m_deferred_game_mode_exit = false;
			m_editor->toggleGameMode();
		}

		for (auto* plugin : m_gui_plugins) {
			plugin->update(time_delta);
		}

		m_asset_browser->update();
		m_log_ui->update(time_delta);

		guiEndFrame();
	}


	void extractBundled() {
		#ifdef _WIN32
			HRSRC hrsrc = FindResourceA(GetModuleHandle(NULL), MAKEINTRESOURCE(102), "TAR");
			if (!hrsrc) return;
			HGLOBAL hglobal = LoadResource(GetModuleHandle(NULL), hrsrc);
			if (!hglobal) return;
			const DWORD size = SizeofResource(GetModuleHandle(NULL), hrsrc);
			if (size == 0) return;
			const void* res_mem = LockResource(hglobal);
			if (!res_mem) return;
	
			TCHAR exe_path[MAX_PATH_LENGTH];
			GetModuleFileNameA(NULL, exe_path, MAX_PATH_LENGTH);

			// TODO extract only nonexistent files
			u64 bundled_last_modified = os::getLastModified(exe_path);
			InputMemoryStream str(res_mem, size);

			TarHeader header;
			while (str.getPosition() < str.size()) {
				const u8* ptr = (const u8*)str.getData() + str.getPosition();
				str.read(&header, sizeof(header));
				u32 size;
				fromCStringOctal(Span(header.size, sizeof(header.size)), Ref(size)); 
				if (header.name[0] && header.typeflag == 0 || header.typeflag == '0') {
					const StaticString<MAX_PATH_LENGTH> path(m_engine->getFileSystem().getBasePath(), "/", header.name);
					char dir[MAX_PATH_LENGTH];
					Path::getDir(Span(dir), path);
					if (!os::makePath(dir)) logError("");
					if (!os::fileExists(path)) {
						os::OutputFile file;
						if (file.open(path)) {
							if (!file.write(ptr + 512, size)) {
								logError("Failed to write ", path);
							}
							file.close();
						}
						else {
							logError("Failed to extract ", path);
						}
					}
				}

				str.setPosition(str.getPosition() + (512 - str.getPosition() % 512) % 512);
				str.setPosition(str.getPosition() + size + (512 - size % 512) % 512);
			}

			UnlockResource(res_mem);
		#endif
	}


	void initDefaultUniverse() {
		EntityRef env = m_editor->addEntity();
		m_editor->setEntityName(env, "environment");
		ComponentType env_cmp_type = reflection::getComponentType("environment");
		ComponentType lua_script_cmp_type = reflection::getComponentType("lua_script");
		Span<EntityRef> entities(&env, 1);
		m_editor->addComponent(entities, env_cmp_type);
		m_editor->addComponent(entities, lua_script_cmp_type);
		Quat rot;
		rot.fromEuler(Vec3(degreesToRadians(45.f), 0, 0));
		m_editor->setEntitiesRotations(&env, &rot, 1);
		const ComponentUID cmp = m_editor->getUniverse()->getComponent(env, lua_script_cmp_type);
		m_editor->addArrayPropertyItem(cmp, "scripts");
		m_editor->setProperty(lua_script_cmp_type, "scripts", 0, "Path", entities, Path("pipelines/atmo.lua"));
	}


	void showWelcomeScreen()
	{
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
								 ImGuiWindowFlags_NoSavedSettings;
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetWorkPos());
        ImGui::SetNextWindowSize(viewport->GetWorkSize());
		ImGui::SetNextWindowViewport(viewport->ID);
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
				ImGui::Text("Working directory: %s", m_engine->getFileSystem().getBasePath());
				ImGui::SameLine();
				if (ImGui::Button("Change...")) {
					char dir[MAX_PATH_LENGTH];
					if (os::getOpenDirectory(Span(dir), m_engine->getFileSystem().getBasePath())) {
						os::OutputFile cfg_file;
						if (cfg_file.open(".lumixuser")) {
							cfg_file << dir;
							cfg_file.close();
						}
						m_engine->getFileSystem().setBasePath(dir);
						extractBundled();
						m_editor->loadProject();
						scanUniverses();
					}
				}
				ImGui::Separator();
				if (ImGui::Button("New universe")) {
					initDefaultUniverse();
					m_is_welcome_screen_open = false;
				}
				ImGui::Text("Open universe:");
				ImGui::Indent();
				if(m_universes.empty()) {
					ImGui::Text("No universes found");
				}
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
					os::shellExecuteOpen("https://github.com/nem0/LumixEngine/wiki");
				}

				if (ImGui::Button("Show major releases"))
				{
					os::shellExecuteOpen("https://github.com/nem0/LumixEngine/releases");
				}

				if (ImGui::Button("Show latest commits"))
				{
					os::shellExecuteOpen("https://github.com/nem0/LumixEngine/commits/master");
				}

				if (ImGui::Button("Show issues"))
				{
					os::shellExecuteOpen("https://github.com/nem0/lumixengine/issues");
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
		os::setWindowTitle(m_main_window, tmp);
	}


	void save()
	{
		if (m_editor->isGameMode())
		{
			logError("Could not save while the game is running");
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
		if (m_save_as_request) {
			ImGui::OpenPopup("Save Universe As");
			m_save_as_request = false;
		}
		if (ImGui::BeginPopupModal("Save Universe As"))
		{
			static char name[64] = "";
			ImGuiEx::Label("Name");
			ImGui::InputText("##name", name, lengthOf(name));
			if (ImGui::Button(ICON_FA_SAVE "Save")) {
				setTitle(name);
				m_editor->saveUniverse(name, true);
				scanUniverses();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_TIMES "Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}


	void saveAs()
	{
		if (m_editor->isGameMode())
		{
			logError("Can not save while the game is running");
			return;
		}

		m_save_as_request = true;
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
			initDefaultUniverse();
		}
	}


	GUIPlugin* getFocusedPlugin()
	{
		for (GUIPlugin* plugin : m_gui_plugins)
		{
			if (plugin->hasFocus()) return plugin;
		}
		return nullptr;
	}

	Gizmo::Config& getGizmoConfig() override { return m_gizmo_config; }
	
	void setCursorCaptured(bool captured) override { m_cursor_captured = captured; }

	void toggleProjection() { 
		Viewport vp = m_editor->getView().getViewport(); 
		vp.is_ortho = !vp.is_ortho;
		m_editor->getView().setViewport(vp); 
	}

	void undo() { m_editor->undo(); }
	void redo() { m_editor->redo(); }
	void copy() { m_editor->copyEntities(); }
	void paste() { m_editor->pasteEntities(); }
	void duplicate() { m_editor->duplicateEntities(); }
	void setTopView() { m_editor->getView().setTopView(); }
	void setFrontView() { m_editor->getView().setFrontView(); }
	void setSideView() { m_editor->getView().setSideView(); }
	void setLocalCoordSystem() { getGizmoConfig().coord_system = Gizmo::Config::LOCAL; }
	void setGlobalCoordSystem() { getGizmoConfig().coord_system = Gizmo::Config::GLOBAL; }
	void addEntity() { m_editor->addEntity(); }
	void setEditCamTransform() { m_is_edit_cam_transform_ui_open = !m_is_edit_cam_transform_ui_open; }
	void lookAtSelected() { m_editor->getView().lookAtSelected(); }
	void copyViewTransform() { m_editor->getView().copyTransform(); }
	void toggleSettings() { m_settings.m_is_open = !m_settings.m_is_open; }
	bool areSettingsOpen() const { return m_settings.m_is_open; }
	void toggleEntityList() { m_is_entity_list_open = !m_is_entity_list_open; }
	bool isEntityListOpen() const { return m_is_entity_list_open; }
	void toggleAssetBrowser() { m_asset_browser->m_is_open = !m_asset_browser->m_is_open; }
	bool isAssetBrowserOpen() const { return m_asset_browser->m_is_open; }
	int getExitCode() const override { return m_exit_code; }
	AssetBrowser& getAssetBrowser() override
	{
		ASSERT(m_asset_browser.get());
		return *m_asset_browser;
	}
	AssetCompiler& getAssetCompiler() override
	{
		ASSERT(m_asset_compiler.get());
		return *m_asset_compiler;
	}
	PropertyGrid& getPropertyGrid() override
	{
		ASSERT(m_property_grid.get());
		return *m_property_grid;
	}
	LogUI& getLogUI() override
	{
		ASSERT(m_log_ui.get());
		return *m_log_ui;
	}
	void toggleGameMode() { m_editor->toggleGameMode(); }
	void setTranslateGizmoMode() { getGizmoConfig().mode = Gizmo::Config::TRANSLATE; }
	void setRotateGizmoMode() { getGizmoConfig().mode = Gizmo::Config::ROTATE; }
	void setScaleGizmoMode() { getGizmoConfig().mode = Gizmo::Config::SCALE; }


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
		if (os::getSaveFilename(Span(tmp), "Prefab files\0*.fab\0", "fab"))
		{
			Path::normalize(tmp, Span(filename));
			const char* base_path = m_engine->getFileSystem().getBasePath();
			auto& selected_entities = m_editor->getSelectedEntities();
			if (selected_entities.size() != 1) return;

			EntityRef entity = selected_entities[0];
			if (startsWith(filename, base_path))
			{
				m_editor->getPrefabSystem().savePrefab(entity, Path(filename + stringLength(base_path)));
			}
			else
			{
				m_editor->getPrefabSystem().savePrefab(entity, Path(filename));
			}
		}
	}

	void snapDown() override {
		const Array<EntityRef>& selected = m_editor->getSelectedEntities();
		if (selected.empty()) return;

		Array<DVec3> new_positions(m_allocator);
		Universe* universe = m_editor->getUniverse();

		for (EntityRef entity : selected) {
			const DVec3 origin = universe->getPosition(entity);
			auto hit = getRenderInterface()->castRay(*universe, origin, Vec3(0, -1, 0), entity);
			if (hit.is_hit) {
				new_positions.push(origin + Vec3(0, -hit.t, 0));
			}
			else {
				hit = getRenderInterface()->castRay(*universe, origin, Vec3(0, 1, 0), entity);
				if (hit.is_hit) {
					new_positions.push(origin + Vec3(0, hit.t, 0));
				}
				else {
					new_positions.push(universe->getPosition(entity));
				}
			}
		}
		m_editor->setEntitiesPositions(&selected[0], &new_positions[0], new_positions.size());
	}

	void launchRenderDoc() {
		m_render_interface->launchRenderDoc();
	}

	void autosnapDown()
	{
		Gizmo::Config& cfg = getGizmoConfig();
		cfg.setAutosnapDown(!cfg.isAutosnapDown());
	}


	void destroySelectedEntity()
	{
		auto& selected_entities = m_editor->getSelectedEntities();
		if (selected_entities.empty()) return;
		m_editor->destroyEntities(&selected_entities[0], selected_entities.size());
	}


	void removeAction(Action* action) override
	{
		m_actions.eraseItem(action);
		m_window_actions.eraseItem(action);
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
	Action& addAction(const char* label_short, const char* label_long, const char* name, const char* font_icon = "")
	{
		Action* a = LUMIX_NEW(m_editor->getAllocator(), Action);
		a->init(label_short, label_long, name, font_icon, true);
		a->func.bind<Func>(this);
		addAction(a);
		m_owned_actions.push(a);
		return *a;
	}


	template <void (StudioAppImpl::*Func)()>
	void addAction(const char* label_short,
		const char* label_long,
		const char* name,
		const char* font_icon,
		os::Keycode shortcut,
		u8 modifiers)
	{
		Action* a = LUMIX_NEW(m_editor->getAllocator(), Action);
		a->init(label_short, label_long, name, font_icon, shortcut, modifiers, true);
		a->func.bind<Func>(this);
		m_owned_actions.push(a);
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
			if (!node->plugin)
				showAddComponentNode(node->child, filter);
			else if (stristr(node->plugin->getLabel(), filter))
				node->plugin->onGUI(false, true);
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
		last = last ? last + 1 : node->label;
		if (last[0] == ' ') ++last;
		if (ImGui::BeginMenu(last))
		{
			showAddComponentNode(node->child, filter);
			ImGui::EndMenu();
		}
		showAddComponentNode(node->next, filter);
	}


	void onCreateEntityWithComponentGUI()
	{
		doMenuItem(*getAction("createEntity"), true);
		const float w = ImGui::CalcTextSize(ICON_FA_TIMES).x + ImGui::GetStyle().ItemSpacing.x * 2;
		ImGui::SetNextItemWidth(-w);
		ImGui::InputTextWithHint("##filter", "Filter", m_component_filter, sizeof(m_component_filter));
		ImGui::SameLine();
		if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) {
			m_component_filter[0] = '\0';
		}
		showAddComponentNode(m_add_cmp_root.child, m_component_filter);
	}


	void entityMenu()
	{
		if (!ImGui::BeginMenu("Entity")) return;

		const auto& selected_entities = m_editor->getSelectedEntities();
		bool is_any_entity_selected = !selected_entities.empty();
		if (ImGui::BeginMenu(ICON_FA_PLUS_SQUARE "Create"))
		{
			onCreateEntityWithComponentGUI();
			ImGui::EndMenu();
		}
		doMenuItem(*getAction("destroyEntity"), is_any_entity_selected);
		doMenuItem(*getAction("savePrefab"), selected_entities.size() == 1);
		doMenuItem(*getAction("makeParent"), selected_entities.size() == 2);
		bool can_unparent =
			selected_entities.size() == 1 && m_editor->getUniverse()->getParent(selected_entities[0]).isValid();
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
		doMenuItem(*getAction("setTranslateGizmoMode"), true);
		doMenuItem(*getAction("setRotateGizmoMode"), true);
		doMenuItem(*getAction("setScaleGizmoMode"), true);
		doMenuItem(*getAction("setLocalCoordSystem"), true);
		doMenuItem(*getAction("setGlobalCoordSystem"), true);
		if (ImGui::BeginMenu(ICON_FA_CAMERA "View", true))
		{
			doMenuItem(*getAction("toggleProjection"), true);
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
		if (ImGui::BeginMenu(NO_ICON "Open"))
		{
			ImGui::Dummy(ImVec2(200, 1)); // to force minimal menu size
			const float w = ImGui::CalcTextSize(ICON_FA_TIMES).x + ImGui::GetStyle().ItemSpacing.x * 2;
			ImGui::SetNextItemWidth(-w);
			ImGui::InputTextWithHint("##filter", "Filter", m_open_filter, sizeof(m_open_filter));
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) {
				m_open_filter[0] = '\0';
			}

			for (auto& univ : m_universes)
			{
				if ((m_open_filter[0] == '\0' || stristr(univ.data, m_open_filter)) && ImGui::MenuItem(univ.data))
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
		doMenuItem(*getAction("setEditCamTransform"), true);
		doMenuItem(*getAction("lookAtSelected"), is_any_entity_selected);
		doMenuItem(*getAction("copyViewTransform"), is_any_entity_selected);
		doMenuItem(*getAction("toggleGameMode"), true);
		doMenuItem(*getAction("snapDown"), is_any_entity_selected);
		doMenuItem(*getAction("autosnapDown"), true);
		doMenuItem(*getAction("pack_data"), true);
		if (renderDocOption()) doMenuItem(*getAction("launch_renderdoc"), true);
		ImGui::EndMenu();
	}


	void viewMenu()
	{
		if (!ImGui::BeginMenu("View")) return;

		ImGui::MenuItem(ICON_FA_IMAGES "Asset browser", nullptr, &m_asset_browser->m_is_open);
		doMenuItem(*getAction("entityList"), true);
		ImGui::MenuItem(ICON_FA_COMMENT_ALT "Log", nullptr, &m_log_ui->m_is_open);
		ImGui::MenuItem(ICON_FA_CHART_AREA "Profiler", nullptr, &m_profiler_ui->m_is_open);
		ImGui::MenuItem(ICON_FA_INFO_CIRCLE "Inspector", nullptr, &m_property_grid->m_is_open);
		doMenuItem(*getAction("settings"), true);
		ImGui::Separator();
		for (Action* action : m_window_actions)
		{
			doMenuItem(*action, true);
		}
		ImGui::EndMenu();
	}


	void mainMenu()
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
				initDefaultUniverse();
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
		if (ImGui::BeginPopupModal("Confirm"))
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

		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 4));
		if (ImGui::BeginMainMenuBar())
		{
			fileMenu();
			editMenu();
			entityMenu();
			toolsMenu();
			viewMenu();
			ImGui::PopStyleVar(2);

			float w = ImGui::GetWindowContentRegionWidth() * 0.5f - m_toolbar_actions.size() * 15 - ImGui::GetCursorPosX();
			ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
			for (auto* action : m_toolbar_actions) {
				action->toolbarButton(m_big_icon_font);
			}

			StaticString<200> stats("");
			if (m_engine->getFileSystem().hasWork()) stats << ICON_FA_HOURGLASS_HALF "Loading... | ";
			stats << "FPS: ";
			stats << (u32)(m_fps + 0.5f);
			if (!isFocused()) stats << " - inactive window";
			auto stats_size = ImGui::CalcTextSize(stats);
			ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x);
			ImGui::Text("%s", (const char*)stats);

			if (m_log_ui->getUnreadErrorCount() == 1)
			{
				ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x);
				auto error_stats_size = ImGui::CalcTextSize(ICON_FA_EXCLAMATION_TRIANGLE "1 error | ");
				ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x - error_stats_size.x);
				ImGui::TextColored(ImVec4(1, 0, 0, 1), ICON_FA_EXCLAMATION_TRIANGLE "1 error | ");
			}
			else if (m_log_ui->getUnreadErrorCount() > 1)
			{
				StaticString<50> error_stats(ICON_FA_EXCLAMATION_TRIANGLE, m_log_ui->getUnreadErrorCount(), " errors | ");
				ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x);
				auto error_stats_size = ImGui::CalcTextSize(error_stats);
				ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x - error_stats_size.x);
				ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", (const char*)error_stats);
			}
			ImGui::EndMainMenuBar();
		}
		ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.y * 2));
	}


	void showHierarchy(EntityRef entity, const Array<EntityRef>& selected_entities)
	{
		char buffer[1024];
		Universe* universe = m_editor->getUniverse();
		getEntityListDisplayName(*this, getWorldEditor(), Span(buffer), entity);
		bool selected = selected_entities.indexOf(entity) >= 0;
		ImGui::PushID(entity.index);
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_AllowItemOverlap;
		bool has_child = universe->getFirstChild(entity).isValid();
		if (!has_child) flags = ImGuiTreeNodeFlags_Leaf;
		if (selected) flags |= ImGuiTreeNodeFlags_Selected;
		
		bool node_open;
		if (m_renaming_entity == entity) {
			node_open = ImGui::TreeNodeEx((void*)(intptr_t)entity.index, flags, "%s", "");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
			if (m_set_rename_focus) {
				ImGui::SetKeyboardFocusHere();
				m_set_rename_focus = false;
			}
			if (ImGui::InputText("##renamed_val", m_rename_buf, sizeof(m_rename_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
				m_editor->setEntityName((EntityRef)m_renaming_entity, m_rename_buf);
				m_renaming_entity = INVALID_ENTITY;
			}
			if (ImGui::IsItemDeactivated()) {
				m_renaming_entity = INVALID_ENTITY;
			}
			m_set_rename_focus = false;
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor();
		}
		else {
			node_open = ImGui::TreeNodeEx((void*)(intptr_t)entity.index, flags, "%s", buffer);
		}
		
		if (ImGui::IsMouseReleased(1) && ImGui::IsItemHovered()) ImGui::OpenPopup("entity_context_menu");
		if (ImGui::BeginPopup("entity_context_menu"))
		{
			if (ImGui::MenuItem("Create child"))
			{
				m_editor->beginCommandGroup(crc32("create_child_entity"));
				EntityRef child = m_editor->addEntity();
				m_editor->makeParent(entity, child);
				const DVec3 pos = m_editor->getUniverse()->getPosition(entity);
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
		else {
			if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
				m_editor->selectEntities(Span(&entity, 1), ImGui::GetIO().KeyCtrl);
			}
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (auto* payload = ImGui::AcceptDragDropPayload("entity"))
			{
				EntityRef dropped_entity = *(EntityRef*)payload->Data;
				if (dropped_entity != entity)
				{
					m_editor->makeParent(entity, dropped_entity);
					ImGui::EndDragDropTarget();
					if (node_open) ImGui::TreePop();
					return;
				}
			}

			ImGui::EndDragDropTarget();
		}

		if (node_open)
		{
			for (EntityPtr e_ptr = universe->getFirstChild(entity); e_ptr.isValid();
				 e_ptr = universe->getNextSibling((EntityRef)e_ptr))
			{
				showHierarchy((EntityRef)e_ptr, selected_entities);
			}
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && m_is_f2_pressed) {
				m_renaming_entity = selected_entities.empty() ? INVALID_ENTITY : selected_entities[0];
				if (m_renaming_entity.isValid()) {
					m_set_rename_focus = true;
					const char* name = m_editor->getUniverse()->getEntityName(selected_entities[0]);
					copyString(m_rename_buf, name);
				}
			}

			ImGui::TreePop();
		}
	}


	void onEditCameraGUI()
	{
		if (!m_is_edit_cam_transform_ui_open) return;
		if (ImGui::Begin("Edit camera")) {
			Viewport vp = m_editor->getView().getViewport();
			ImGuiEx::Label("Position");
			if (ImGui::DragScalarN("##pos", ImGuiDataType_Double, &vp.pos.x, 3, 1.f)) {
				m_editor->getView().setViewport(vp);
			}
			Vec3 angles = vp.rot.toEuler();
			ImGuiEx::Label("Rotation");
			if (ImGui::DragFloat3("##rot", &angles.x, 0.01f)) {
				vp.rot.fromEuler(angles);
				m_editor->getView().setViewport(vp);
			}
		}
		ImGui::End();
	}


	void onEntityListGUI()
	{
		PROFILE_FUNCTION();
		const Array<EntityRef>& entities = m_editor->getSelectedEntities();
		static char filter[64] = "";
		if (!m_is_entity_list_open) return;
		if (ImGui::Begin(ICON_FA_STREAM "Hierarchy##hierarchy", &m_is_entity_list_open))
		{
			auto* universe = m_editor->getUniverse();
			const float w = ImGui::CalcTextSize(ICON_FA_TIMES).x + ImGui::GetStyle().ItemSpacing.x * 2;
			ImGui::SetNextItemWidth(-w);
			ImGui::InputTextWithHint("##filter", "Filter", filter, sizeof(filter));
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) {
				filter[0] = '\0';
			}

			if (ImGui::BeginChild("entities"))
			{
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetStyle().FramePadding.x);
				if (filter[0] == '\0')
				{
					for (EntityPtr e = universe->getFirstEntity(); e.isValid();
						 e = universe->getNextEntity((EntityRef)e))
					{
						const EntityRef e_ref = (EntityRef)e;
						if (!universe->getParent(e_ref).isValid())
						{
							showHierarchy(e_ref, entities);
						}
					}
				}
				else
				{
					for (EntityPtr e = universe->getFirstEntity(); e.isValid();
						 e = universe->getNextEntity((EntityRef)e))
					{
						char buffer[1024];
						getEntityListDisplayName(*this, getWorldEditor(), Span(buffer), e);
						if (stristr(buffer, filter) == nullptr) continue;
						ImGui::PushID(e.index);
						const EntityRef e_ref = (EntityRef)e;
						bool selected = entities.indexOf(e_ref) >= 0;
						if (ImGui::Selectable(buffer, &selected))
						{
							m_editor->selectEntities(Span(&e_ref, 1), ImGui::GetIO().KeyCtrl);
						}
						if (ImGui::BeginDragDropSource())
						{
							ImGui::Text("%s", buffer);
							ImGui::SetDragDropPayload("entity", &e, sizeof(e));
							ImGui::EndDragDropSource();
						}
						ImGui::PopID();
					}
				}
				ImGui::PopItemWidth();
			}
			ImGui::EndChild();
			if (ImGui::BeginDragDropTarget())
			{
				if (auto* payload = ImGui::AcceptDragDropPayload("entity"))
				{
					EntityRef dropped_entity = *(EntityRef*)payload->Data;
					m_editor->makeParent(INVALID_ENTITY, dropped_entity);
				}
				ImGui::EndDragDropTarget();
			}
		}
		ImGui::End();
	}


	void dummy() {}


	void setFullscreen(bool fullscreen) override
	{
		if(fullscreen) {
			m_fullscreen_restore_state = os::setFullscreen(m_main_window);
		}
		else {
			os::restore(m_main_window, m_fullscreen_restore_state);
		}
	}


	void saveSettings() override
	{
		ImGuiIO& io = ImGui::GetIO();
		if (io.WantSaveIniSettings) {
			const char* data = ImGui::SaveIniSettingsToMemory();
			m_settings.m_imgui_state = data;
		}
		m_settings.m_is_asset_browser_open = m_asset_browser->m_is_open;
		m_settings.m_asset_browser_left_column_width = m_asset_browser->m_left_column_width;
		m_settings.m_is_entity_list_open = m_is_entity_list_open;
		m_settings.m_is_log_open = m_log_ui->m_is_open;
		m_settings.m_is_profiler_open = m_profiler_ui->m_is_open;
		m_settings.m_is_properties_open = m_property_grid->m_is_open;

		for (auto* i : m_gui_plugins) {
			i->onBeforeSettingsSaved();
		}

		m_settings.save();
	}


	ImFont* addFontFromFile(const char* path, float size, bool merge_icons) {
		FileSystem& fs = m_engine->getFileSystem();
		OutputMemoryStream data(m_allocator);
		if (!fs.getContentSync(Path(path), Ref(data))) return nullptr;
		ImGuiIO& io = ImGui::GetIO();
		ImFontConfig cfg;
		copyString(cfg.Name, path);
		cfg.FontDataOwnedByAtlas = false;
		auto font = io.Fonts->AddFontFromMemoryTTF((void*)data.data(), (i32)data.size(), size, &cfg);
		if(merge_icons) {
			ImFontConfig config;
			copyString(config.Name, "editor/fonts/fa-regular-400.ttf");
			config.MergeMode = true;
			config.FontDataOwnedByAtlas = false;
			config.GlyphMinAdvanceX = size; // Use if you want to make the icon monospaced
			static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
			OutputMemoryStream icons_data(m_allocator);
			if (fs.getContentSync(Path("editor/fonts/fa-regular-400.ttf"), Ref(icons_data))) {
				ImFont* icons_font = io.Fonts->AddFontFromMemoryTTF((void*)icons_data.data(), (i32)icons_data.size(), size * 0.75f, &config, icon_ranges);
				ASSERT(icons_font);
			}
			copyString(config.Name, "editor/fonts/fa-solid-900.ttf");
			icons_data.clear();
			if (fs.getContentSync(Path("editor/fonts/fa-solid-900.ttf"), Ref(icons_data))) {
				ImFont* icons_font = io.Fonts->AddFontFromMemoryTTF((void*)icons_data.data(), (i32)icons_data.size(), size * 0.75f, &config, icon_ranges);
				ASSERT(icons_font);
			}
		}

		return font;
	}

	void initIMGUIPlatformIO() {
		ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
		static StudioAppImpl* that = this;
		ASSERT(that == this);
		pio.Platform_CreateWindow = [](ImGuiViewport* vp){
			os::InitWindowArgs args = {};
			args.flags = os::InitWindowArgs::NO_DECORATION | os::InitWindowArgs::NO_TASKBAR_ICON;
			ImGuiViewport* parent = ImGui::FindViewportByID(vp->ParentViewportId);
			args.parent = parent ? parent->PlatformHandle : os::INVALID_WINDOW;
			args.name = "child";
			vp->PlatformHandle = os::createWindow(args);
			that->m_windows.push(vp->PlatformHandle);

			ASSERT(vp->PlatformHandle != os::INVALID_WINDOW);
		};
		pio.Platform_DestroyWindow = [](ImGuiViewport* vp){
			os::WindowHandle w = (os::WindowHandle)vp->PlatformHandle;
			that->m_deferred_destroy_windows.push({w, 3});
			vp->PlatformHandle = nullptr;
			vp->PlatformUserData = nullptr;
			that->m_windows.eraseItem(w);
		};
		pio.Platform_ShowWindow = [](ImGuiViewport* vp){};
		pio.Platform_SetWindowPos = [](ImGuiViewport* vp, ImVec2 pos) {
			const os::WindowHandle h = vp->PlatformHandle;
			os::Rect r = os::getWindowScreenRect(h);
			r.left = int(pos.x);
			r.top = int(pos.y);
			os::setWindowScreenRect(h, r);
		};
		pio.Platform_GetWindowPos = [](ImGuiViewport* vp) -> ImVec2 {
			os::WindowHandle win = (os::WindowHandle)vp->PlatformHandle;
			const os::Rect r = os::getWindowClientRect(win);
			const os::Point p = os::toScreen(win, r.left, r.top);
			return {(float)p.x, (float)p.y};

		};
		pio.Platform_SetWindowSize = [](ImGuiViewport* vp, ImVec2 pos) {
			const os::WindowHandle h = vp->PlatformHandle;
			os::Rect r = os::getWindowScreenRect(h);
			r.width = int(pos.x);
			r.height = int(pos.y);
			os::setWindowScreenRect(h, r);
		};
		pio.Platform_GetWindowSize = [](ImGuiViewport* vp) -> ImVec2 {
			const os::Rect r = os::getWindowClientRect((os::WindowHandle)vp->PlatformHandle);
			return {(float)r.width, (float)r.height};
		};
		pio.Platform_SetWindowTitle = [](ImGuiViewport* vp, const char* title){
			os::setWindowTitle((os::WindowHandle)vp->PlatformHandle, title);
		};
		pio.Platform_GetWindowFocus = [](ImGuiViewport* vp){
			return os::getFocused() == vp->PlatformHandle;
		};
		pio.Platform_SetWindowFocus = [](ImGuiViewport* vp){
			ASSERT(false);
		};

		ImGuiViewport* mvp = ImGui::GetMainViewport();
		mvp->PlatformHandle = m_main_window;
		mvp->DpiScale = 1;
		mvp->PlatformUserData = (void*)1;

		updateIMGUIMonitors();
	}

	static void updateIMGUIMonitors() {
		os::Monitor monitors[32];
		const u32 monitor_count = os::getMonitors(Span(monitors));
		ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
		pio.Monitors.clear();
		for (u32 i = 0; i < monitor_count; ++i) {
			const os::Monitor& m = monitors[i];
			ImGuiPlatformMonitor im;
			im.MainPos = ImVec2((float)m.monitor_rect.left, (float)m.monitor_rect.top);
			im.MainSize = ImVec2((float)m.monitor_rect.width, (float)m.monitor_rect.height);
			im.WorkPos = ImVec2((float)m.work_rect.left, (float)m.work_rect.top);
			im.WorkSize = ImVec2((float)m.work_rect.width, (float)m.work_rect.height);

			if (m.primary) {
				pio.Monitors.push_front(im);
			}
			else {
				pio.Monitors.push_back(im);
			}
		}
	}

	void initIMGUI()
	{
		logInfo("Initializing imgui...");
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		#ifdef __linux__ 
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			io.BackendFlags = ImGuiBackendFlags_HasMouseCursors;
		#else
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
			io.BackendFlags = ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports | ImGuiBackendFlags_HasMouseCursors;
		#endif

		initIMGUIPlatformIO();

		const int dpi = os::getDPI();
		float font_scale = dpi / 96.f;
		FileSystem& fs = m_engine->getFileSystem();
		
		ImGui::LoadIniSettingsFromMemory(m_settings.m_imgui_state.c_str());

		m_font = addFontFromFile("editor/fonts/NotoSans-Regular.ttf", (float)m_settings.m_font_size * font_scale, true);
		m_bold_font = addFontFromFile("editor/fonts/NotoSans-Bold.ttf", (float)m_settings.m_font_size * font_scale, true);
		
		OutputMemoryStream data(m_allocator);
		if (fs.getContentSync(Path("editor/fonts/fa-solid-900.ttf"), Ref(data))) {
			const float size = (float)m_settings.m_font_size * font_scale * 1.25f;
			ImFontConfig cfg;
			copyString(cfg.Name, "editor/fonts/fa-solid-900.ttf");
			cfg.FontDataOwnedByAtlas = false;
			cfg.GlyphMinAdvanceX = size; // Use if you want to make the icon monospaced
			static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
			m_big_icon_font = io.Fonts->AddFontFromMemoryTTF((void*)data.data(), (i32)data.size(), size, &cfg, icon_ranges);
			cfg.MergeMode = true;
			copyString(cfg.Name, "editor/fonts/fa-regular-400.ttf");
			if (fs.getContentSync(Path("editor/fonts/fa-regular-400.ttf"), Ref(data))) {
				ImFont* icons_font = io.Fonts->AddFontFromMemoryTTF((void*)data.data(), (i32)data.size(), size, &cfg, icon_ranges);
				ASSERT(icons_font);
			}
		}

		if (!m_font || !m_bold_font) {
			os::messageBox(
				"Could not open editor/fonts/NotoSans-Regular.ttf or editor/fonts/NotoSans-Bold.ttf\n"
				"It very likely means that data are not bundled with\n"
				"the exe and the exe is not in the correct directory.\n"
				"The program will eventually crash!"
			);
		}

		io.KeyMap[ImGuiKey_Space] = (int)os::Keycode::SPACE;
		io.KeyMap[ImGuiKey_Tab] = (int)os::Keycode::TAB;
		io.KeyMap[ImGuiKey_LeftArrow] = (int)os::Keycode::LEFT;
		io.KeyMap[ImGuiKey_RightArrow] = (int)os::Keycode::RIGHT;
		io.KeyMap[ImGuiKey_UpArrow] = (int)os::Keycode::UP;
		io.KeyMap[ImGuiKey_DownArrow] = (int)os::Keycode::DOWN;
		io.KeyMap[ImGuiKey_PageUp] = (int)os::Keycode::PAGEUP;
		io.KeyMap[ImGuiKey_PageDown] = (int)os::Keycode::PAGEDOWN;
		io.KeyMap[ImGuiKey_Home] = (int)os::Keycode::HOME;
		io.KeyMap[ImGuiKey_End] = (int)os::Keycode::END;
		io.KeyMap[ImGuiKey_Delete] = (int)os::Keycode::DEL;
		io.KeyMap[ImGuiKey_Backspace] = (int)os::Keycode::BACKSPACE;
		io.KeyMap[ImGuiKey_Enter] = (int)os::Keycode::RETURN;
		io.KeyMap[ImGuiKey_Escape] = (int)os::Keycode::ESCAPE;
		io.KeyMap[ImGuiKey_A] = (int)os::Keycode::A;
		io.KeyMap[ImGuiKey_C] = (int)os::Keycode::C;
		io.KeyMap[ImGuiKey_V] = (int)os::Keycode::V;
		io.KeyMap[ImGuiKey_X] = (int)os::Keycode::X;
		io.KeyMap[ImGuiKey_Y] = (int)os::Keycode::Y;
		io.KeyMap[ImGuiKey_Z] = (int)os::Keycode::Z;

		ImGuiStyle& style = ImGui::GetStyle();
		style.FramePadding.y = 0;
		style.ItemSpacing.y = 2;
		style.ItemInnerSpacing.x = 2;
	}

	void setRenderInterface(RenderInterface* ri) override { m_render_interface = ri; }
	RenderInterface* getRenderInterface() override { return m_render_interface; }

	float getFOV() const override { return m_fov; }
	void setFOV(float fov_radians) override { m_fov = fov_radians; }
	Settings& getSettings() override { return m_settings; }

	void loadSettings()
	{
		logInfo("Loading settings...");
		char cmd_line[2048];
		os::getCommandLine(Span(cmd_line));

		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (!parser.currentEquals("-no_crash_report")) continue;

			m_settings.m_force_no_crash_report = true;
			break;
		}

		m_settings.load();
		for (auto* i : m_gui_plugins) {
			i->onSettingsLoaded();
		}

		m_asset_browser->m_is_open = m_settings.m_is_asset_browser_open;
		m_asset_browser->m_left_column_width = m_settings.m_asset_browser_left_column_width;
		m_is_entity_list_open = m_settings.m_is_entity_list_open;
		m_log_ui->m_is_open = m_settings.m_is_log_open;
		m_profiler_ui->m_is_open = m_settings.m_is_profiler_open;
		m_property_grid->m_is_open = m_settings.m_is_properties_open;

		if (m_settings.m_is_maximized)
		{
			os::maximizeWindow(m_main_window);
		}
		else if (m_settings.m_window.w > 0)
		{
			os::Rect r;
			r.left = m_settings.m_window.x;
			r.top = m_settings.m_window.y;
			r.width = m_settings.m_window.w;
			r.height = m_settings.m_window.h;
			os::setWindowScreenRect(m_main_window, r);
		}
	}


	void addActions()
	{
		addAction<&StudioAppImpl::newUniverse>(ICON_FA_PLUS "New", "New universe", "newUniverse", ICON_FA_PLUS);
		addAction<&StudioAppImpl::save>(
			ICON_FA_SAVE "Save", "Save universe", "save", ICON_FA_SAVE, os::Keycode::S, (u8)Action::Modifiers::CTRL);
		addAction<&StudioAppImpl::saveAs>(
			NO_ICON "Save As", "Save universe as", "saveAs", "", os::Keycode::S, (u8)Action::Modifiers::CTRL | (u8)Action::Modifiers::SHIFT);
		addAction<&StudioAppImpl::exit>(
			ICON_FA_SIGN_OUT_ALT "Exit", "Exit Studio", "exit", ICON_FA_SIGN_OUT_ALT, os::Keycode::X, (u8)Action::Modifiers::CTRL);
		addAction<&StudioAppImpl::redo>(
			ICON_FA_REDO "Redo", "Redo scene action", "redo", ICON_FA_REDO, os::Keycode::Z, (u8)Action::Modifiers::CTRL | (u8)Action::Modifiers::SHIFT);
		addAction<&StudioAppImpl::undo>(
			ICON_FA_UNDO "Undo", "Undo scene action", "undo", ICON_FA_UNDO, os::Keycode::Z, (u8)Action::Modifiers::CTRL);
		addAction<&StudioAppImpl::copy>(
			ICON_FA_CLIPBOARD "Copy", "Copy entity", "copy", ICON_FA_CLIPBOARD, os::Keycode::C, (u8)Action::Modifiers::CTRL);
		addAction<&StudioAppImpl::paste>(
			ICON_FA_PASTE "Paste", "Paste entity", "paste", ICON_FA_PASTE, os::Keycode::V, (u8)Action::Modifiers::CTRL);
		addAction<&StudioAppImpl::duplicate>(
			ICON_FA_CLONE "Duplicate", "Duplicate entity", "duplicate", ICON_FA_CLONE, os::Keycode::D, (u8)Action::Modifiers::CTRL);
		addAction<&StudioAppImpl::setTranslateGizmoMode>(ICON_FA_ARROWS_ALT "Translate", "Set translate mode", "setTranslateGizmoMode", ICON_FA_ARROWS_ALT)
			.is_selected.bind<&Gizmo::Config::isTranslateMode>(&getGizmoConfig());
		addAction<&StudioAppImpl::setRotateGizmoMode>(ICON_FA_UNDO "Rotate", "Set rotate mode", "setRotateGizmoMode", ICON_FA_UNDO)
			.is_selected.bind<&Gizmo::Config::isRotateMode>(&getGizmoConfig());
		addAction<&StudioAppImpl::setScaleGizmoMode>(ICON_FA_EXPAND_ALT "Scale", "Set scale mode", "setScaleGizmoMode", ICON_FA_EXPAND_ALT)
			.is_selected.bind<&Gizmo::Config::isScaleMode>(&getGizmoConfig());
		addAction<&StudioAppImpl::toggleProjection>(NO_ICON "Ortho/perspective", "Toggle ortho/perspective projection", "toggleProjection");
		addAction<&StudioAppImpl::setTopView>(NO_ICON "Top", "Set top camera view", "viewTop");
		addAction<&StudioAppImpl::setFrontView>(NO_ICON "Front", "Set front camera view", "viewFront");
		addAction<&StudioAppImpl::setSideView>(NO_ICON "Side", "Set side camera view", "viewSide");
		addAction<&StudioAppImpl::setLocalCoordSystem>(ICON_FA_HOME "Local", "Set local transform system", "setLocalCoordSystem", ICON_FA_HOME)
			.is_selected.bind<&Gizmo::Config::isLocalCoordSystem>(&getGizmoConfig());
		addAction<&StudioAppImpl::setGlobalCoordSystem>(ICON_FA_GLOBE "Global", "Set global transform system", "setGlobalCoordSystem", ICON_FA_GLOBE)
			.is_selected.bind<&Gizmo::Config::isGlobalCoordSystem>(&getGizmoConfig());

		addAction<&StudioAppImpl::addEntity>(ICON_FA_PLUS_SQUARE "Create empty", "Create empty entity", "createEntity", ICON_FA_PLUS_SQUARE);
		addAction<&StudioAppImpl::destroySelectedEntity>(ICON_FA_MINUS_SQUARE "Destroy",
			"Destroy entity",
			"destroyEntity",
			ICON_FA_MINUS_SQUARE,
			os::Keycode::DEL,
			0);
		addAction<&StudioAppImpl::savePrefab>(ICON_FA_SAVE "Save prefab", "Save selected entities as prefab", "savePrefab", ICON_FA_SAVE);
		addAction<&StudioAppImpl::makeParent>(ICON_FA_OBJECT_GROUP "Make parent", "Make entity parent", "makeParent", ICON_FA_OBJECT_GROUP);
		addAction<&StudioAppImpl::unparent>(ICON_FA_OBJECT_UNGROUP "Unparent", "Unparent entity", "unparent", ICON_FA_OBJECT_UNGROUP);

		addAction<&StudioAppImpl::toggleGameMode>(ICON_FA_PLAY "Game Mode", "Toggle game mode", "toggleGameMode", ICON_FA_PLAY)
			.is_selected.bind<&WorldEditor::isGameMode>(m_editor.get());
		addAction<&StudioAppImpl::autosnapDown>(NO_ICON "Autosnap down", "Toggle autosnap down", "autosnapDown")
			.is_selected.bind<&Gizmo::Config::isAutosnapDown>(&getGizmoConfig());
		addAction<&StudioAppImpl::launchRenderDoc>(NO_ICON "Launch RenderDoc", "Launch RenderDoc", "launch_renderdoc");
		addAction<&StudioAppImpl::snapDown>(NO_ICON "Snap down", "Snap entities down", "snapDown");
		addAction<&StudioAppImpl::setEditCamTransform>(NO_ICON "Camera transform", "Set camera transformation", "setEditCamTransform");
		addAction<&StudioAppImpl::copyViewTransform>(NO_ICON "Copy view transform", "Copy view transform", "copyViewTransform");
		addAction<&StudioAppImpl::lookAtSelected>(NO_ICON "Look at selected", "Look at selected entity", "lookAtSelected");
		addAction<&StudioAppImpl::toggleAssetBrowser>(ICON_FA_IMAGES "Asset Browser", "Toggle asset browser", "assetBrowser", ICON_FA_IMAGES)
			.is_selected.bind<&StudioAppImpl::isAssetBrowserOpen>(this);
		addAction<&StudioAppImpl::toggleEntityList>(ICON_FA_STREAM "Hierarchy", "Toggle hierarchy", "entityList", ICON_FA_STREAM)
			.is_selected.bind<&StudioAppImpl::isEntityListOpen>(this);
		addAction<&StudioAppImpl::toggleSettings>(ICON_FA_COG "Settings", "Toggle settings UI", "settings", ICON_FA_COG)
			.is_selected.bind<&StudioAppImpl::areSettingsOpen>(this);
		addAction<&StudioAppImpl::showPackDataDialog>(ICON_FA_ARCHIVE "Pack data", "Pack data", "pack_data", ICON_FA_ARCHIVE);
	}


	static bool copyPlugin(const char* src, int iteration, char (&out)[MAX_PATH_LENGTH])
	{
		char tmp_path[MAX_PATH_LENGTH];
		os::getExecutablePath(Span(tmp_path));
		StaticString<MAX_PATH_LENGTH> copy_path;
		Path::getDir(Span(copy_path.data), tmp_path);
		copy_path << "plugins/" << iteration;
		if (!os::makePath(copy_path)) logError("Could not create ", copy_path);
		Path::getBasename(Span(tmp_path), src);
		copy_path << "/" << tmp_path << "." << getPluginExtension();
#ifdef _WIN32
		StaticString<MAX_PATH_LENGTH> src_pdb(src);
		StaticString<MAX_PATH_LENGTH> dest_pdb(copy_path);
		if (Path::replaceExtension(dest_pdb.data, "pdb") && Path::replaceExtension(src_pdb.data, "pda"))
		{
			os::deleteFile(dest_pdb);
			if (!os::copyFile(src_pdb, dest_pdb))
			{
				copyString(out, src);
				return false;
			}
		}
#endif

		os::deleteFile(copy_path);
		if (!os::copyFile(src, copy_path))
		{
			copyString(out, src);
			return false;
		}
		copyString(out, copy_path);
		return true;
	}


	void loadUserPlugins()
	{
		char cmd_line[2048];
		os::getCommandLine(Span(cmd_line));

		CommandLineParser parser(cmd_line);
		auto& plugin_manager = m_engine->getPluginManager();
		while (parser.next())
		{
			if (!parser.currentEquals("-plugin")) continue;
			if (!parser.next()) break;

			char src[MAX_PATH_LENGTH];
			parser.getCurrent(src, lengthOf(src));

			bool is_full_path = findSubstring(src, ".") != nullptr;
			Lumix::IPlugin* loaded_plugin;
			if (is_full_path)
			{
				char copy_path[MAX_PATH_LENGTH];
				copyPlugin(src, 0, copy_path);
				loaded_plugin = plugin_manager.load(copy_path);
			}
			else
			{
				loaded_plugin = plugin_manager.load(src);
			}

			if (!loaded_plugin)
			{
				logError("Could not load plugin ", src, " requested by command line");
			}
			else if (is_full_path && !m_watched_plugin.watcher.get())
			{
				char dir[MAX_PATH_LENGTH];
				char basename[MAX_PATH_LENGTH];
				Path::getBasename(Span(basename), src);
				m_watched_plugin.basename = basename;
				Path::getDir(Span(dir), src);
				m_watched_plugin.watcher = FileSystemWatcher::create(dir, m_allocator);
				m_watched_plugin.watcher->getCallback().bind<&StudioAppImpl::onPluginChanged>(this);
				m_watched_plugin.dir = dir;
				m_watched_plugin.plugin = loaded_plugin;
			}
		}
	}


	static const char* getPluginExtension()
	{
		const char* ext =
#ifdef _WIN32
			"dll";
#elif defined __linux__
			"so";
#else
#error Unknown platform
#endif
		return ext;
	}


	void onPluginChanged(const char* path)
	{
		const char* ext = getPluginExtension();
		if (!Path::hasExtension(path, ext)
#ifdef _WIN32
			&& !Path::hasExtension(path, "pda")
#endif
		)
			return;

		char basename[MAX_PATH_LENGTH];
		Path::getBasename(Span(basename), path);
		if (!equalIStrings(basename, m_watched_plugin.basename)) return;

		m_watched_plugin.reload_request = true;
	}


	void tryReloadPlugin()
	{
		/*
		m_watched_plugin.reload_request = false;

		StaticString<MAX_PATH_LENGTH> src(m_watched_plugin.dir, m_watched_plugin.basename, ".", getPluginExtension());
		char copy_path[MAX_PATH_LENGTH];
		++m_watched_plugin.iteration;

		if (!copyPlugin(src, m_watched_plugin.iteration, copy_path)) return;

		logInfo("Editor") << "Trying to reload plugin " << m_watched_plugin.basename;

		OutputMemoryStream blob(m_allocator);
		blob.reserve(16 * 1024);
		PluginManager& plugin_manager = m_engine->getPluginManager();

		Universe* universe = m_editor->getUniverse();
		for (IScene* scene : universe->getScenes())
		{
			if (&scene->getPlugin() != m_watched_plugin.plugin) continue;
			if (m_editor->isGameMode()) scene->stopGame();
			scene->serialize(blob);
			universe->removeScene(scene);
			scene->getPlugin().destroyScene(scene);
		}
		plugin_manager.unload(m_watched_plugin.plugin);

		// TODO try to delete the old version

		m_watched_plugin.plugin = plugin_manager.load(copy_path);
		if (!m_watched_plugin.plugin)
		{
			logError("Failed to load plugin " << copy_path << ". Reload failed.");
			return;
		}

		InputMemoryStream input_blob(blob);
		m_watched_plugin.plugin->createScenes(*universe);
		for (IScene* scene : universe->getScenes())
		{
			if (&scene->getPlugin() != m_watched_plugin.plugin) continue;
			scene->deserialize(input_blob);
			if (m_editor->isGameMode()) scene->startGame();
		}
		logInfo("Editor") << "Finished reloading plugin.";
		*/
		// TODO
		ASSERT(false);
	}

	bool workersCountOption(Ref<u32> workers_count) {
		char cmd_line[2048];
		os::getCommandLine(Span(cmd_line));

		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (parser.currentEquals("-workers")) {
				if(!parser.next()) {
					logError("command line option '-workers` without value");
					return false;
				}
				char tmp[64];
				parser.getCurrent(tmp, sizeof(tmp));
				fromCString(Span(tmp, (u32)strlen(tmp)), workers_count);
				return true;
			}
		}
		return false;

	}

	bool renderDocOption() {
		char cmd_line[2048];
		os::getCommandLine(Span(cmd_line));

		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (parser.currentEquals("-renderdoc")) return true;
		}
		return false;
	}


	bool shouldSleepWhenInactive()
	{
		char cmd_line[2048];
		os::getCommandLine(Span(cmd_line));

		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (parser.currentEquals("-no_sleep_inactive")) return false;
		}
		return true;
	}


	void loadUniverseFromCommandLine()
	{
		char cmd_line[2048];
		char path[MAX_PATH_LENGTH];
		os::getCommandLine(Span(cmd_line));

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
		os::getCommandLine(Span(cmd_line));

		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (!parser.currentEquals("-data_dir")) continue;
			if (!parser.next()) break;

			parser.getCurrent(dir, max_size);
			break;
		}
	}
	
	Span<MousePlugin*> getMousePlugins() override { return m_mouse_plugins; }

	GUIPlugin* getPlugin(const char* name) override
	{
		for (auto* i : m_gui_plugins)
		{
			if (equalStrings(i->getName(), name)) return i;
		}
		return nullptr;
	}


	void initPlugins() override
	{
		for (int i = 1, c = m_plugins.size(); i < c; ++i) {
			for (int j = 0; j < i; ++j) {
				IPlugin* p = m_plugins[i];
				if (m_plugins[j]->dependsOn(*p)) {
					m_plugins.erase(i);
					--i;
					m_plugins.insert(j, p);
				}
			}
		}

		for (IPlugin* plugin : m_plugins) {
			plugin->init();
		}

		for (const reflection::RegisteredComponent& cmp : reflection::getComponents()) {
			ASSERT(cmp.cmp->component_type != INVALID_COMPONENT_TYPE);
			const reflection::ComponentBase* r = cmp.cmp;
			
			if (m_component_labels.find(r->component_type).isValid()) continue;

			struct : reflection::IEmptyPropertyVisitor {
				void visit(const reflection::Property<Path>& prop) override {
					for (const reflection::IAttribute* attr : prop.getAttributes()) {
						if (attr->getType() == reflection::IAttribute::RESOURCE) {
							is_res = true;
							reflection::ResourceAttribute* a = (reflection::ResourceAttribute*)attr;
							res_type = a->resource_type;
							prop_name = prop.name;
						}
					}
				}
				bool is_res = false;
				const char* prop_name;
				ResourceType res_type;
			} visitor;

			r->visit(visitor);
			if (visitor.is_res) {
				registerComponent(r->icon, r->component_type, r->label, visitor.res_type, visitor.prop_name);
			}
			else {
				registerComponent(r->icon, r->component_type, r->label);
			}
		}
	}


	void addPlugin(IPlugin& plugin) override { m_plugins.push(&plugin); }


	void addPlugin(GUIPlugin& plugin) override
	{
		m_gui_plugins.push(&plugin);
		for (auto* i : m_gui_plugins)
		{
			i->pluginAdded(plugin);
			plugin.pluginAdded(*i);
		}
	}

	void addPlugin(MousePlugin& plugin) override { m_mouse_plugins.push(&plugin); }
	void removePlugin(GUIPlugin& plugin) override { m_gui_plugins.swapAndPopItem(&plugin); }
	void removePlugin(MousePlugin& plugin) override { m_mouse_plugins.swapAndPopItem(&plugin); }


	void setStudioApp()
	{
#ifdef STATIC_PLUGINS
		#define LUMIX_EDITOR_PLUGINS
		#include "engine/plugins.inl"
		#undef LUMIX_EDITOR_PLUGINS
#else
		auto& plugin_manager = m_engine->getPluginManager();
		for (auto* lib : plugin_manager.getLibraries())
		{
			auto* f = (StudioApp::IPlugin * (*)(StudioApp&)) os::getLibrarySymbol(lib, "setStudioApp");
			if (f)
			{
				StudioApp::IPlugin* plugin = f(*this);
				addPlugin(*plugin);
			}
		}
#endif
		initPlugins();
		PrefabSystem::createEditorPlugins(*this, m_editor->getPrefabSystem());
	}


	void runScript(const char* src, const char* script_name) override
	{
		lua_State* L = m_engine->getState();
		bool errors = luaL_loadbuffer(L, src, stringLength(src), script_name) != 0;
		errors = errors || lua_pcall(L, 0, 0, 0) != 0;
		if (errors)
		{
			logError(script_name, ": ", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}


	void savePrefabAs(const char* path) {
		auto& selected_entities = m_editor->getSelectedEntities();
		if (selected_entities.size() != 1) return;

		EntityRef entity = selected_entities[0];
		m_editor->getPrefabSystem().savePrefab(entity, Path(path)); 
	}


	void destroyEntity(EntityRef e) { m_editor->destroyEntities(&e, 1); }


	void selectEntity(EntityRef e) { m_editor->selectEntities(Span(&e, 1), false); }


	EntityRef createEntity() { return m_editor->addEntity(); }

	void createComponent(EntityRef e, const char* type)
	{
		const ComponentType cmp_type = reflection::getComponentType(type);
		m_editor->addComponent(Span(&e, 1), cmp_type);
	}

	void exitGameMode() { m_deferred_game_mode_exit = true; }


	void exitWithCode(int exit_code)
	{
		m_finished = true;
		m_exit_code = exit_code;
	}


	struct SetPropertyVisitor : reflection::IPropertyVisitor
	{
		void visit(const reflection::Property<int>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (!lua_isnumber(L, -1)) return;

			if(reflection::getAttribute(prop, reflection::IAttribute::ENUM)) {
				notSupported(prop);
			}

			int val = (int)lua_tointeger(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), val);
		}

		void visit(const reflection::Property<u32>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (!lua_isnumber(L, -1)) return;

			const u32 val = (u32)lua_tointeger(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), val);
		}

		void visit(const reflection::Property<float>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (!lua_isnumber(L, -1)) return;

			float val = (float)lua_tonumber(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), val);
		}

		void visit(const reflection::Property<Vec2>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (!LuaWrapper::isType<Vec2>(L, -1)) return;

			const Vec2 val = LuaWrapper::toType<Vec2>(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), val);
		}

		void visit(const reflection::Property<Vec3>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (!LuaWrapper::isType<Vec3>(L, -1)) return;

			const Vec3 val = LuaWrapper::toType<Vec3>(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), val);
		}

		void visit(const reflection::Property<IVec3>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (!LuaWrapper::isType<IVec3>(L, -1)) return;

			const IVec3 val = LuaWrapper::toType<IVec3>(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), val);
		}

		void visit(const reflection::Property<Vec4>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (!LuaWrapper::isType<Vec4>(L, -1)) return;

			const Vec4 val = LuaWrapper::toType<Vec4>(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), val);
		}
		
		void visit(const reflection::Property<const char*>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (!lua_isstring(L, -1)) return;

			const char* str = lua_tostring(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), str);
		}


		void visit(const reflection::Property<Path>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (!lua_isstring(L, -1)) return;

			const char* str = lua_tostring(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), str);
		}


		void visit(const reflection::Property<bool>& prop) override
		{
			if (!equalStrings(property_name, prop.name)) return;
			if (!lua_isboolean(L, -1)) return;

			bool val = lua_toboolean(L, -1) != 0;
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), val);
		}

		void visit(const reflection::Property<EntityPtr>& prop) override { notSupported(prop); }
		void visit(const reflection::IArrayProperty& prop) override { notSupported(prop); }
		void visit(const reflection::IBlobProperty& prop) override { notSupported(prop); }

		template <typename T>
		void notSupported(const T& prop)
		{
			if (!equalStrings(property_name, prop.name)) return;
			logError("Property ", prop.name, " has unsupported type");
		}


		lua_State* L;
		EntityRef entity;
		ComponentType cmp_type;
		const char* property_name;
		WorldEditor* editor;
	};


	static int createEntityEx(lua_State* L)
	{
		auto* studio = LuaWrapper::checkArg<StudioAppImpl*>(L, 1);
		LuaWrapper::checkTableArg(L, 2);

		WorldEditor& editor = *studio->m_editor;
		editor.beginCommandGroup(crc32("createEntityEx"));
		EntityRef e = editor.addEntity();
		editor.selectEntities(Span(&e, 1), false);

		lua_pushvalue(L, 2);
		lua_pushnil(L);
		while (lua_next(L, -2) != 0)
		{
			const char* parameter_name = luaL_checkstring(L, -2);
			if (equalStrings(parameter_name, "name"))
			{
				const char* name = LuaWrapper::toType<const char*>(L, -1);
				editor.setEntityName(e, name);
			}
			else if (equalStrings(parameter_name, "position"))
			{
				const DVec3 pos = LuaWrapper::toType<DVec3>(L, -1);
				editor.setEntitiesPositions(&e, &pos, 1);
			}
			else if (equalStrings(parameter_name, "rotation"))
			{
				const Quat rot = LuaWrapper::toType<Quat>(L, -1);
				editor.setEntitiesRotations(&e, &rot, 1);
			}
			else
			{
				ComponentType cmp_type = reflection::getComponentType(parameter_name);
				editor.addComponent(Span(&e, 1), cmp_type);

				IScene* scene = editor.getUniverse()->getScene(cmp_type);
				if (scene)
				{
					ComponentUID cmp(e, cmp_type, scene);
					const reflection::ComponentBase* cmp_des = reflection::getComponent(cmp_type);
					if (cmp.isValid())
					{
						lua_pushvalue(L, -1);
						lua_pushnil(L);
						while (lua_next(L, -2) != 0)
						{
							const char* property_name = luaL_checkstring(L, -2);
							SetPropertyVisitor v;
							v.property_name = property_name;
							v.entity = (EntityRef)cmp.entity;
							v.cmp_type = cmp.type;
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

		AssetCompiler& compiler = studio->getAssetCompiler();
		if (ResourceType(type) == INVALID_RESOURCE_TYPE) return 0;
		const auto& resources = compiler.lockResources();

		lua_createtable(L, resources.size(), 0);
		int i = 0;
		for (const AssetCompiler::ResourceItem& res : resources)
		{
			LuaWrapper::push(L, res.path.c_str());
			lua_rawseti(L, -2, i + 1);
			++i;
		}

		compiler.unlockResources();
		return 1;
	}


	void saveUniverseAs(const char* basename, bool save_path) { m_editor->saveUniverse(basename, save_path); }


	void saveUniverse() { save(); }


	void createLua()
	{
		lua_State* L = m_engine->getState();

		LuaWrapper::createSystemVariable(L, "Editor", "editor", this);

#define REGISTER_FUNCTION(F)                                                                                    \
	do                                                                                                          \
	{                                                                                                           \
		auto f = &LuaWrapper::wrapMethodClosure<&StudioAppImpl::F>; \
		LuaWrapper::createSystemClosure(L, "Editor", this, #F, f);                                              \
	} while (false)

		REGISTER_FUNCTION(savePrefabAs);
		REGISTER_FUNCTION(selectEntity);
		REGISTER_FUNCTION(createEntity);
		REGISTER_FUNCTION(createComponent);
		REGISTER_FUNCTION(destroyEntity);
		REGISTER_FUNCTION(newUniverse);
		REGISTER_FUNCTION(saveUniverse);
		REGISTER_FUNCTION(saveUniverseAs);
		REGISTER_FUNCTION(exitWithCode);
		REGISTER_FUNCTION(exitGameMode);

#undef REGISTER_FUNCTION

		LuaWrapper::createSystemFunction(L, "Editor", "getResources", &getResources);
		LuaWrapper::createSystemFunction(L, "Editor", "createEntityEx", &createEntityEx);
	}


	void checkScriptCommandLine() {
		char command_line[1024];
		os::getCommandLine(Span(command_line));
		CommandLineParser parser(command_line);
		while (parser.next()) {
			if (parser.currentEquals("-run_script")) {
				if (!parser.next()) break;

				char tmp[MAX_PATH_LENGTH];
				parser.getCurrent(tmp, lengthOf(tmp));
				OutputMemoryStream content(m_allocator);
				
				if (m_engine->getFileSystem().getContentSync(Path(tmp), Ref(content))) {
					content.write('\0');
					runScript((const char*)content.data(), tmp);
				}
				else {
					logError("Could not read ", tmp);
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
		if (equalStrings("main.pak", filename)) return false;
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


	struct PackFileInfo
	{
		u32 hash;
		u64 offset;
		u64 size;
		u64 size_compressed;

		char path[MAX_PATH_LENGTH];
	};

	void scanCompiled(AssociativeArray<u32, PackFileInfo>& infos) {
		os::FileIterator* iter = m_engine->getFileSystem().createFileIterator(".lumix/assets");
		const char* base_path = m_engine->getFileSystem().getBasePath();
		os::FileInfo info;
		while (os::getNextFile(iter, &info)) {
			if (info.is_directory) continue;

			char basename[MAX_PATH_LENGTH];
			Path::getBasename(Span(basename), info.filename);
			PackFileInfo rec;
			fromCString(Span(basename), Ref(rec.hash));
			rec.offset = 0;
			rec.size = os::getFileSize(StaticString<MAX_PATH_LENGTH>(base_path, ".lumix/assets/", info.filename));
			copyString(rec.path, ".lumix/assets/");
			catString(rec.path, info.filename);
			infos.insert(rec.hash, rec);
		}
		
		os::destroyFileIterator(iter);

		packDataScan("pipelines/", infos);
		packDataScan("universes/", infos);
	}

	void packDataScan(const char* dir_path, AssociativeArray<u32, PackFileInfo>& infos)
	{
		auto* iter = m_engine->getFileSystem().createFileIterator(dir_path);
		const char* base_path = m_engine->getFileSystem().getBasePath();
		os::FileInfo info;
		while (os::getNextFile(iter, &info))
		{
			char normalized_path[MAX_PATH_LENGTH];
			Path::normalize(info.filename, Span(normalized_path));
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

			if (!includeFileInPack(normalized_path)) continue;

			StaticString<MAX_PATH_LENGTH> out_path;
			if (dir_path[0] == '.')
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
			out_info.size = os::getFileSize(StaticString<MAX_PATH_LENGTH>(base_path, out_path.data));
			out_info.offset = ~0UL;
		}
		os::destroyFileIterator(iter);
	}


	void packDataScanResources(AssociativeArray<u32, PackFileInfo>& infos)
	{
		ResourceManagerHub& rm = m_engine->getResourceManager();
		for (auto iter = rm.getAll().begin(), end = rm.getAll().end(); iter != end; ++iter)
		{
			const auto& resources = iter.value()->getResourceTable();
			for (Resource* res : resources)
			{
				u32 hash = crc32(res->getPath().c_str());
				auto& out_info = infos.emplace(hash);
				copyString(Span(out_info.path), res->getPath().c_str());
				out_info.hash = hash;
				out_info.size = os::getFileSize(res->getPath().c_str());
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
		copyString(Span(out_info.path), unv_path);
		out_info.hash = hash;
		out_info.size = os::getFileSize(unv_path);
		out_info.offset = ~0UL;
	}


	void showPackDataDialog() { m_is_pack_data_dialog_open = true; }


	void onPackDataGUI()
	{
		if (!m_is_pack_data_dialog_open) return;
		if (ImGui::Begin("Pack data", &m_is_pack_data_dialog_open)) {
			ImGuiEx::Label("Destination dir");
			if (ImGui::Button(m_pack.dest_dir.empty() ? "..." : m_pack.dest_dir.data)) {
				bool res = os::getOpenDirectory(Span(m_pack.dest_dir.data), m_engine->getFileSystem().getBasePath());
				(void)res;
			}

			ImGuiEx::Label("Mode");
			ImGui::Combo("##mode", (int*)&m_pack.mode, "All files\0Loaded universe\0");

			if (ImGui::Button("Pack")) packData();
		}
		ImGui::End();
	}


	void packData() {
		if (m_pack.dest_dir.empty()) return;

		char dest[MAX_PATH_LENGTH];

		static const char* OUT_FILENAME = "main.pak";
		copyString(dest, m_pack.dest_dir);
		catString(dest, OUT_FILENAME);
		AssociativeArray<u32, PackFileInfo> infos(m_allocator);
		infos.reserve(10000);

		switch (m_pack.mode) {
			case PackConfig::Mode::ALL_FILES: scanCompiled(infos); break;
			case PackConfig::Mode::CURRENT_UNIVERSE: packDataScanResources(infos); break;
			default: ASSERT(false); break;
		}

		if (infos.size() == 0) {
			logError("No files found while trying to create ", dest);
			return;
		}

		FileSystem& fs = m_engine->getFileSystem();
		OutputMemoryStream compressed(m_allocator);
		OutputMemoryStream src(m_allocator);
		u64 total_size = 0;
		for (auto& info : infos) {
			if (!fs.getContentSync(Path(info.path), Ref(src))) {
				logError("Could not open ", info.path);
				return;
			}

			total_size += (u32)src.size();

			info.size = src.size();
			info.offset = compressed.size();
			const i32 cap = LZ4_compressBound((i32)src.size());
			compressed.reserve(compressed.size() + cap);
			const i32 dst_size = LZ4_compress_default((const char*)src.data(), (char*)compressed.skip(0), (i32)src.size(), cap); 
			if (dst_size == 0) {
				logError("Could not compress ", info.path);
				return;
			}
			
			info.size_compressed = dst_size;
			compressed.resize(compressed.size() + dst_size);
		}
		logInfo("Packed ", infos.size(), " files (", total_size / 1024, "KiB) in ", compressed.size() / 1024, " KiB");

		bool success;
		os::OutputFile file;
		if (!file.open(dest)) {
			logError("Could not create ", dest);
			return;
		}

		const u32 count = (u32)infos.size();
		success = file.write(&count, sizeof(count));

		for (auto& info : infos) {
			success = file.write(&info.hash, sizeof(info.hash)) || success;
			success = file.write(&info.offset, sizeof(info.offset)) || success;
			success = file.write(&info.size, sizeof(info.size)) || success;
			success = file.write(&info.size_compressed, sizeof(info.size_compressed)) || success;
		}

		success = file.write(compressed.data(), compressed.size()) || success;
		file.close();

		if (!success) {
			logError("Could not write ", dest);
			return;
		}

		const char* bin_files[] = {"app.exe", "dbghelp.dll", "dbgcore.dll"};
		StaticString<MAX_PATH_LENGTH> src_dir("bin/");
		if (!os::fileExists("bin/app.exe")) {
			char tmp[MAX_PATH_LENGTH];
			os::getExecutablePath(Span(tmp));
			Path::getDir(Span(src_dir.data), tmp);
		}

		for (auto& file : bin_files) {
			StaticString<MAX_PATH_LENGTH> tmp(m_pack.dest_dir, file);
			StaticString<MAX_PATH_LENGTH> src(src_dir, file);
			if (!os::copyFile(src, tmp)) {
				logError("Failed to copy ", src, " to ", tmp);
			}
		}

		for (GUIPlugin* plugin : m_gui_plugins)	{
			if (!plugin->packData(m_pack.dest_dir)) {
				logError("Plugin ", plugin->getName(), " failed to pack data.");
			}
		}
		logInfo("Packing finished.");
	}


	void loadLuaPlugin(const char* dir, const char* filename)
	{
		StaticString<MAX_PATH_LENGTH> path(dir, filename);

		OutputMemoryStream src(m_engine->getAllocator());
		if (m_engine->getFileSystem().getContentSync(Path(path), Ref(src))) {
			LuaPlugin* plugin = LUMIX_NEW(m_editor->getAllocator(), LuaPlugin)(*this, Span((const char*)src.data(), (u32)src.size()), filename);
			addPlugin(*plugin);
		}
		else {
			logWarning("Failed to load ", path);
		}
	}


	void scanUniverses()
	{
		m_universes.clear();
		auto* iter = m_engine->getFileSystem().createFileIterator("universes");
		os::FileInfo info;
		while (os::getNextFile(iter, &info))
		{
			if (info.filename[0] == '.') continue;
			if (!info.is_directory) continue;
			if (startsWith(info.filename, "__")) continue;

			char basename[MAX_PATH_LENGTH];
			Path::getBasename(Span(basename), info.filename);
			m_universes.emplace(basename);
		}
		os::destroyFileIterator(iter);
	}


	void findLuaPlugins(const char* dir)
	{
		auto* iter = m_engine->getFileSystem().createFileIterator(dir);
		os::FileInfo info;
		while (os::getNextFile(iter, &info))
		{
			char normalized_path[MAX_PATH_LENGTH];
			Path::normalize(info.filename, Span(normalized_path));
			if (normalized_path[0] == '.') continue;
			if (info.is_directory)
			{
				char dir_path[MAX_PATH_LENGTH] = {0};
				if (dir[0] != '.') copyString(dir_path, dir);
				catString(dir_path, info.filename);
				catString(dir_path, "/");
				findLuaPlugins(dir_path);
			}
			else
			{
				char ext[5];
				Path::getExtension(Span(ext), Span(info.filename, stringLength(info.filename)));
				if (equalStrings(ext, "lua"))
				{
					loadLuaPlugin(dir, info.filename);
				}
			}
		}
		os::destroyFileIterator(iter);
	}


	const os::Event* getEvents() const override { return m_events.empty() ? nullptr : &m_events[0]; }


	int getEventsCount() const override { return m_events.size(); }

	
	static void checkWorkingDirectory()
	{
		if (!os::fileExists("../LumixStudio.lnk")) return;

		if (!os::dirExists("bin") && os::dirExists("../bin") &&
			os::dirExists("../pipelines"))
		{
			os::setCurrentDirectory("../");
		}

		if (!os::dirExists("bin"))
		{
			os::messageBox("Bin directory not found, please check working directory.");
		}
		else if (!os::dirExists("pipelines"))
		{
			os::messageBox("Pipelines directory not found, please check working directory.");
		}
	}


	void checkShortcuts()
	{
		if (ImGui::IsAnyItemActive()) return;
		GUIPlugin* plugin = getFocusedPlugin();
		u8 pressed_modifiers = 0;
		if (os::isKeyDown(os::Keycode::SHIFT)) pressed_modifiers |= (u8)Action::Modifiers::SHIFT;
		if (os::isKeyDown(os::Keycode::CTRL)) pressed_modifiers |= (u8)Action::Modifiers::CTRL;
		if (os::isKeyDown(os::Keycode::MENU)) pressed_modifiers |= (u8)Action::Modifiers::ALT;

		for (Action* a : m_actions) {
			if (!a->is_global || (a->shortcut == os::Keycode::INVALID && a->modifiers ==0)) continue;
			if (a->plugin != plugin) continue;
			if (a->shortcut != os::Keycode::INVALID && !os::isKeyDown(a->shortcut)) continue;
			if (a->modifiers != pressed_modifiers) continue;

			a->func.invoke();
			return;
		}
	}

	IAllocator& getAllocator() override { return m_allocator; }
	Engine& getEngine() override { return *m_engine; }

	WorldEditor& getWorldEditor() override
	{
		ASSERT(m_editor.get());
		return *m_editor;
	}

	
	ImFont* getBigIconFont() override { return m_big_icon_font; }
	ImFont* getBoldFont() override { return m_bold_font; }

	struct WindowToDestroy {
		os::WindowHandle window;
		u32 counter;
	};

	DefaultAllocator m_main_allocator;
	#ifdef LUMIX_DEBUG
		debug::Allocator m_allocator;
	#else
		IAllocator& m_allocator;
	#endif
	UniquePtr<Engine> m_engine;
	Array<os::WindowHandle> m_windows;
	Array<WindowToDestroy> m_deferred_destroy_windows;
	os::WindowHandle m_main_window;
	os::WindowState m_fullscreen_restore_state;
	Array<Action*> m_owned_actions;
	Array<Action*> m_actions;
	Array<Action*> m_window_actions;
	Array<Action*> m_toolbar_actions;
	Array<GUIPlugin*> m_gui_plugins;
	Array<MousePlugin*> m_mouse_plugins;
	Array<IPlugin*> m_plugins;
	Array<IAddComponentPlugin*> m_add_cmp_plugins;
	Array<StaticString<MAX_PATH_LENGTH>> m_universes;
	AddCmpTreeNode m_add_cmp_root;
	HashMap<ComponentType, String> m_component_labels;
	HashMap<ComponentType, StaticString<5>> m_component_icons;
	UniquePtr<WorldEditor> m_editor;
	Action m_set_pivot_action;
	Action m_reset_pivot_action;
	Gizmo::Config m_gizmo_config;
	bool m_save_as_request = false;
	bool m_cursor_captured = false;
	bool m_confirm_exit;
	bool m_confirm_load;
	bool m_confirm_new;
	char m_universe_to_load[MAX_PATH_LENGTH];
	Local<AssetBrowser> m_asset_browser;
	UniquePtr<AssetCompiler> m_asset_compiler;
	Local<PropertyGrid> m_property_grid;
	Local<LogUI> m_log_ui;
	UniquePtr<ProfilerUI> m_profiler_ui;
	Settings m_settings;
	float m_fov = degreesToRadians(60);
	RenderInterface* m_render_interface = nullptr;
	Array<os::Event> m_events;
	char m_template_name[100];
	char m_open_filter[64];
	char m_component_filter[32];
	float m_fps = 0;
	os::Timer m_fps_timer;
	os::Timer m_inactive_fps_timer;
	u32 m_fps_frame = 0;

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
	EntityPtr m_renaming_entity = INVALID_ENTITY;
	bool m_set_rename_focus = false;
	char m_rename_buf[256];
	bool m_is_f2_pressed = false;
	bool m_is_edit_cam_transform_ui_open = false;
	ImFont* m_font;
	ImFont* m_big_icon_font;
	ImFont* m_bold_font;

	struct WatchedPlugin
	{
		UniquePtr<FileSystemWatcher> watcher;
		StaticString<MAX_PATH_LENGTH> dir;
		StaticString<MAX_PATH_LENGTH> basename;
		Lumix::IPlugin* plugin = nullptr;
		int iteration = 0;
		bool reload_request = false;
	} m_watched_plugin;
};


static size_t alignMask(size_t _value, size_t _mask)
{
	return (_value + _mask) & ((~0) & (~_mask));
}


static void* alignPtr(void* _ptr, size_t _align)
{
	union {
		void* ptr;
		size_t addr;
	} un;
	un.ptr = _ptr;
	size_t mask = _align - 1;
	size_t aligned = alignMask(un.addr, mask);
	un.addr = aligned;
	return un.ptr;
}

static Local<StudioAppImpl> g_studio;

StudioApp* StudioApp::create()
{
	g_studio.create();
	return g_studio.get();
}


void StudioApp::destroy(StudioApp& app)
{
	ASSERT(&app == g_studio.get());
	g_studio.destroy();
}


} // namespace Lumix