#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include "audio/audio_scene.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/entity_folders.h"
#include "editor/file_system_watcher.h"
#include "editor/gizmo.h"
#include "editor/prefab_system.h"
#include "editor/render_interface.h"
#include "editor/spline_editor.h"
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


struct StudioAppImpl final : StudioApp
{
	StudioAppImpl()
		: m_is_entity_list_open(true)
		, m_finished(false)
		, m_deferred_game_mode_exit(false)
		, m_actions(m_allocator)
		, m_owned_actions(m_allocator)
		, m_window_actions(m_allocator)
		, m_is_welcome_screen_open(true)
		, m_is_export_game_dialog_open(false)
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
		if (workersCountOption(workers)) {
			cpus_count = workers;
		}
		if (!jobs::init(cpus_count, m_allocator)) {
			logError("Failed to initialize job system.");
		}

		memset(m_imgui_key_map, 0, sizeof(m_imgui_key_map));
		m_imgui_key_map[(int)os::Keycode::CTRL] = ImGuiKey_ModCtrl;
		m_imgui_key_map[(int)os::Keycode::MENU] = ImGuiKey_ModAlt;
		m_imgui_key_map[(int)os::Keycode::SHIFT] = ImGuiKey_ModShift;
		m_imgui_key_map[(int)os::Keycode::SHIFT] = ImGuiKey_ModShift;
		m_imgui_key_map[(int)os::Keycode::LSHIFT] = ImGuiKey_LeftShift;
		m_imgui_key_map[(int)os::Keycode::RSHIFT] = ImGuiKey_RightShift;
		m_imgui_key_map[(int)os::Keycode::SPACE] = ImGuiKey_Space;
		m_imgui_key_map[(int)os::Keycode::TAB] = ImGuiKey_Tab;
		m_imgui_key_map[(int)os::Keycode::LEFT] = ImGuiKey_LeftArrow;
		m_imgui_key_map[(int)os::Keycode::RIGHT] = ImGuiKey_RightArrow;
		m_imgui_key_map[(int)os::Keycode::UP] = ImGuiKey_UpArrow;
		m_imgui_key_map[(int)os::Keycode::DOWN] = ImGuiKey_DownArrow;
		m_imgui_key_map[(int)os::Keycode::PAGEUP] = ImGuiKey_PageUp;
		m_imgui_key_map[(int)os::Keycode::PAGEDOWN] = ImGuiKey_PageDown;
		m_imgui_key_map[(int)os::Keycode::HOME] = ImGuiKey_Home;
		m_imgui_key_map[(int)os::Keycode::END] = ImGuiKey_End;
		m_imgui_key_map[(int)os::Keycode::DEL] = ImGuiKey_Delete;
		m_imgui_key_map[(int)os::Keycode::BACKSPACE] = ImGuiKey_Backspace;
		m_imgui_key_map[(int)os::Keycode::RETURN] = ImGuiKey_Enter;
		m_imgui_key_map[(int)os::Keycode::ESCAPE] = ImGuiKey_Escape;
		m_imgui_key_map[(int)os::Keycode::A] = ImGuiKey_A;
		m_imgui_key_map[(int)os::Keycode::C] = ImGuiKey_C;
		m_imgui_key_map[(int)os::Keycode::V] = ImGuiKey_V;
		m_imgui_key_map[(int)os::Keycode::X] = ImGuiKey_X;
		m_imgui_key_map[(int)os::Keycode::Y] = ImGuiKey_Y;
		m_imgui_key_map[(int)os::Keycode::Z] = ImGuiKey_Z;
	}

	void onEvent(const os::Event& event)
	{
		const bool handle_input = isFocused();
		m_events.push(event);
		switch (event.type) {
			case os::Event::Type::MOUSE_MOVE: 
				if (!m_cursor_captured) {
					ImGuiIO& io = ImGui::GetIO();
					const os::Point cp = os::getMouseScreenPos();
					if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
						io.AddMousePosEvent((float)cp.x, (float)cp.y);
					}
					else {
						const os::Rect screen_rect = os::getWindowScreenRect(m_main_window);
						io.AddMousePosEvent((float)cp.x - screen_rect.left, (float)cp.y - screen_rect.top);
					}
				}
				break;
			case os::Event::Type::FOCUS: break;
			case os::Event::Type::MOUSE_BUTTON: {
				ImGuiIO& io = ImGui::GetIO();
				m_editor->getView().setSnapMode(io.KeyShift, io.KeyCtrl);
				if (handle_input || !event.mouse_button.down) {
					io.AddMouseButtonEvent((int)event.mouse_button.button, event.mouse_button.down);
				}
				break;
			}
			case os::Event::Type::MOUSE_WHEEL:
				if (handle_input) {
					ImGuiIO& io = ImGui::GetIO();
					io.AddMouseWheelEvent(0, event.mouse_wheel.amount);
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
				if (handle_input || !event.key.down) {
					ImGuiIO& io = ImGui::GetIO();
					ImGuiKey key = m_imgui_key_map[(int)event.key.keycode];
					if (key != ImGuiKey_None) io.AddKeyEvent(key, event.key.down);

					if (event.key.down && event.key.keycode == os::Keycode::F2) {
						m_is_f2_pressed = true;
					}
					checkShortcuts();
				}
				break;
			case os::Event::Type::DROP_FILE:
				for(int i = 0, c = os::getDropFileCount(event); i < c; ++i) {
					char tmp[LUMIX_MAX_PATH];
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

	void onShutdown() {
		while (m_engine->getFileSystem().hasWork()) {
			m_engine->getFileSystem().processCallbacks();
		}
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

		profiler::frame();
		m_events.clear();
		m_is_f2_pressed = false;
	}


	void run() override
	{
		profiler::setThreadName("Main thread");
		Semaphore semaphore(0, 1);
		struct Data {
			StudioAppImpl* that;
			Semaphore* semaphore;
		} data = {this, &semaphore};
		jobs::runLambda([&data]() {
			data.that->onInit();
			while (!data.that->m_finished) {
				os::Event e;
				while(os::getEvent(e)) {
					data.that->onEvent(e);
				}
				data.that->onIdle();
			}
			data.that->onShutdown();
			data.semaphore->signal();
		}, nullptr, 0);
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

		char saved_data_dir[LUMIX_MAX_PATH] = {};
		os::InputFile cfg_file;
		if (cfg_file.open(".lumixuser")) {
			if (!cfg_file.read(saved_data_dir, minimum(lengthOf(saved_data_dir), (int)cfg_file.size()))) {
				logError("Failed to read .lumixuser");
			}
			cfg_file.close();
		}

		char current_dir[LUMIX_MAX_PATH];
		os::getCurrentDirectory(Span(current_dir));

		char data_dir[LUMIX_MAX_PATH] = {};
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

		m_asset_compiler = AssetCompiler::create(*this);
		m_editor = WorldEditor::create(*m_engine, m_allocator);
		scanUniverses();
		loadUserPlugins();
		addActions();

		m_asset_browser = AssetBrowser::create(*this);
		m_property_grid.create(*this);
		m_profiler_ui = ProfilerUI::create(*this);
		m_log_ui.create(m_editor->getAllocator());

		ImGui::SetAllocatorFunctions(imguiAlloc, imguiFree, this);
		ImGui::CreateContext();
		loadSettings();
		initIMGUI();

		m_set_pivot_action.init("Set custom pivot",
			"Set custom pivot",
			"set_custom_pivot",
			"",
			os::Keycode::K,
			Action::Modifiers::NONE,
			false);
		addAction(&m_set_pivot_action);

		m_reset_pivot_action.init("Reset pivot",
			"Reset pivot",
			"reset_pivot",
			"",
			os::Keycode::K,
			Action::Modifiers::SHIFT,
			false);
		addAction(&m_reset_pivot_action);

		setStudioApp();
		loadSettings();
		loadUniverseFromCommandLine();

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
		m_asset_browser.reset();
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
		
		jobs::shutdown();
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
			void onGUI(bool create_entity, bool from_filter, WorldEditor& editor) override {
				ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
				const char* last = reverseFind(label, nullptr, '/');
				last = last && !from_filter ? last + 1 : label;
				if (last[0] == ' ') ++last;
				if (!ImGuiEx::BeginResizableMenu(last, nullptr, true)) return;
				char buf[LUMIX_MAX_PATH];
				bool create_empty = ImGui::MenuItem(ICON_FA_BROOM " Empty");
				static u32 selected_res_hash = 0;
				if (asset_browser->resourceList(Span(buf), selected_res_hash, resource_type, 0, true) || create_empty) {
					if (create_entity) {
						EntityRef entity = editor.addEntity();
						editor.selectEntities(Span(&entity, 1), false);
					}

					const Array<EntityRef>& selected_entites = editor.getSelectedEntities();
					editor.addComponent(selected_entites, type);
					if (!create_empty) {
						editor.setProperty(type, "", -1, property, editor.getSelectedEntities(), Path(buf));
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndMenu();
			}


			const char* getLabel() const override { return label; }

			PropertyGrid* property_grid;
			AssetBrowser* asset_browser;
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
			void onGUI(bool create_entity, bool from_filter, WorldEditor& editor) override
			{
				const char* last = reverseFind(label, nullptr, '/');
				last = last && !from_filter ? last + 1 : label;
				if (last[0] == ' ') ++last;
				if (ImGui::MenuItem(last))
				{
					if (create_entity)
					{
						EntityRef entity = editor.addEntity();
						editor.selectEntities(Span(&entity, 1), false);
					}

					editor.addComponent(editor.getSelectedEntities(), type);
				}
			}

			const char* getLabel() const override { return label; }

			PropertyGrid* property_grid;
			ComponentType type;
			char label[64];
		};

		auto& allocator = m_editor->getAllocator();
		auto* plugin = LUMIX_NEW(allocator, Plugin);
		plugin->property_grid = m_property_grid.get();
		plugin->type = type;
		copyString(plugin->label, label);
		addPlugin(*plugin);

		m_component_labels.insert(plugin->type, String(label, m_allocator));
		if (icon && icon[0]) {
			m_component_icons.insert(plugin->type, icon);
		}
	}


	const Array<Action*>& getActions() override { return m_actions; }


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
				onSaveAsDialogGUI();
				for (auto* plugin : m_gui_plugins)
				{
					plugin->onWindowGUI();
				}
				m_settings.onGUI();
				onExportDataGUI();
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
			DVec3 min(FLT_MAX), max(-FLT_MAX);
			for (EntityRef e : ents) {
				const DVec3 p = universe->getPosition(e);
				min = minimum(p, min);
				max = maximum(p, max);
			}

			addCube(view, min, max, 0xffffff00);
			return;
		}

		for (ComponentUID cmp = universe->getFirstComponent(ents[0]); cmp.isValid(); cmp = universe->getNextComponent(cmp)) {
			for (auto* plugin : m_plugins) {
				if (plugin->showGizmo(view, cmp)) break;
			}
		}
	}

	void updateGizmoOffset() {
		const Array<EntityRef>& ents = m_editor->getSelectedEntities();
		if (ents.size() != 1) {
			m_gizmo_config.offset = Vec3::ZERO;
			return;
		}
		static EntityPtr last_selected = INVALID_ENTITY;
		if (last_selected != ents[0]) {
			m_gizmo_config.offset = Vec3::ZERO;
			last_selected = ents[0];
		}
	}

	void update()
	{
		updateGizmoOffset();

		for (i32 i = m_deferred_destroy_windows.size() - 1; i >= 0; --i) {
			--m_deferred_destroy_windows[i].counter;
			if (m_deferred_destroy_windows[i].counter == 0) {
				os::destroyWindow(m_deferred_destroy_windows[i].window);
				m_deferred_destroy_windows.swapAndPop(i);
			}
		}

		PROFILE_FUNCTION();
		profiler::blockColor(0x7f, 0x7f, 0x7f);
		if (m_watched_plugin.reload_request) tryReloadPlugin();

		guiBeginFrame();
		m_asset_compiler->update();

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
	
			TCHAR exe_path[LUMIX_MAX_PATH];
			GetModuleFileNameA(NULL, exe_path, LUMIX_MAX_PATH);

			// TODO extract only nonexistent files
			u64 bundled_last_modified = os::getLastModified(exe_path);
			InputMemoryStream str(res_mem, size);

			TarHeader header;
			while (str.getPosition() < str.size()) {
				const u8* ptr = (const u8*)str.getData() + str.getPosition();
				str.read(&header, sizeof(header));
				u32 size;
				fromCStringOctal(Span(header.size, sizeof(header.size)), size); 
				if (header.name[0] && header.typeflag == 0 || header.typeflag == '0') {
					const StaticString<LUMIX_MAX_PATH> path(m_engine->getFileSystem().getBasePath(), "/", header.name);
					char dir[LUMIX_MAX_PATH];
					copyString(Span(dir), Path::getDir(path));
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
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
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
					char dir[LUMIX_MAX_PATH];
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
						m_asset_compiler->onBasePathChanged();
						m_engine->getResourceManager().reloadAll();
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


	void save() {
		if (m_editor->isGameMode()) {
			logError("Could not save while the game is running");
			return;
		}

		if (m_editor->getUniverse()->getName()[0]) {
			m_editor->saveUniverse(m_editor->getUniverse()->getName(), true);
		} else {
			saveAs();
		}
	}


	void onSaveAsDialogGUI()
	{
		if (m_save_as_request) {
			ImGui::OpenPopup("Save Universe As");
			m_save_as_request = false;
		}
		ImGui::SetNextWindowSizeConstraints(ImVec2(300, 150), ImVec2(9000, 9000));
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
	void lookAtSelected() { m_editor->getView().lookAtSelected(); }
	void copyViewTransform() { m_editor->getView().copyTransform(); }
	void toggleSettings() { m_settings.m_is_open = !m_settings.m_is_open; }
	bool areSettingsOpen() const { return m_settings.m_is_open; }
	void toggleEntityList() { m_is_entity_list_open = !m_is_entity_list_open; }
	bool isEntityListOpen() const { return m_is_entity_list_open; }
	void toggleAssetBrowser() { m_asset_browser->setOpen(!m_asset_browser->isOpen()); }
	bool isAssetBrowserOpen() const { return m_asset_browser->isOpen(); }
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

	void nextFrame() { m_engine->nextFrame(); }
	void pauseGame() { m_engine->pause(!m_engine->isPaused()); }
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
		char filename[LUMIX_MAX_PATH];
		char tmp[LUMIX_MAX_PATH];
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
		Action::Modifiers modifiers)
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


	static void showAddComponentNode(const StudioApp::AddCmpTreeNode* node, const char* filter, WorldEditor& editor)
	{
		if (!node) return;

		if (filter[0])
		{
			if (!node->plugin)
				showAddComponentNode(node->child, filter, editor);
			else if (stristr(node->plugin->getLabel(), filter))
				node->plugin->onGUI(false, true, editor);
			showAddComponentNode(node->next, filter, editor);
			return;
		}

		if (node->plugin)
		{
			node->plugin->onGUI(true, false, editor);
			showAddComponentNode(node->next, filter, editor);
			return;
		}

		const char* last = reverseFind(node->label, nullptr, '/');
		last = last ? last + 1 : node->label;
		if (last[0] == ' ') ++last;
		if (ImGui::BeginMenu(last))
		{
			showAddComponentNode(node->child, filter, editor);
			ImGui::EndMenu();
		}
		showAddComponentNode(node->next, filter, editor);
	}


	void onCreateEntityWithComponentGUI()
	{
		menuItem("createEntity", true);
		const float w = ImGui::CalcTextSize(ICON_FA_TIMES).x + ImGui::GetStyle().ItemSpacing.x * 2;
		ImGui::SetNextItemWidth(-w);
		ImGui::InputTextWithHint("##filter", "Filter", m_component_filter, sizeof(m_component_filter));
		ImGui::SameLine();
		if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) {
			m_component_filter[0] = '\0';
		}
		showAddComponentNode(m_add_cmp_root.child, m_component_filter, *m_editor);
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
		menuItem("destroyEntity", is_any_entity_selected);
		menuItem("savePrefab", selected_entities.size() == 1);
		menuItem("makeParent", selected_entities.size() == 2);
		bool can_unparent =
			selected_entities.size() == 1 && m_editor->getUniverse()->getParent(selected_entities[0]).isValid();
		menuItem("unparent", can_unparent);
		ImGui::EndMenu();
	}

	void menuItem(const char* name, bool enabled) {
		Action* action = getAction(name);
		if (!action) {
			ASSERT(false);
			return;
		}
		Lumix::menuItem(*action, enabled);
	}

	void editMenu()
	{
		if (!ImGui::BeginMenu("Edit")) return;

		bool is_any_entity_selected = !m_editor->getSelectedEntities().empty();
		menuItem("undo", m_editor->canUndo());
		menuItem("redo", m_editor->canRedo());
		ImGui::Separator();
		menuItem("copy", is_any_entity_selected);
		menuItem("paste", m_editor->canPasteEntities());
		menuItem("duplicate", is_any_entity_selected);
		ImGui::Separator();
		menuItem("setTranslateGizmoMode", true);
		menuItem("setRotateGizmoMode", true);
		menuItem("setScaleGizmoMode", true);
		menuItem("setLocalCoordSystem", true);
		menuItem("setGlobalCoordSystem", true);
		if (ImGui::BeginMenu(ICON_FA_CAMERA "View", true))
		{
			menuItem("toggleProjection", true);
			menuItem("viewTop", true);
			menuItem("viewFront", true);
			menuItem("viewSide", true);
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}


	void fileMenu()
	{
		if (!ImGui::BeginMenu("File")) return;

		menuItem("newUniverse", true);
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
		menuItem("save", !m_editor->isGameMode());
		menuItem("saveAs", !m_editor->isGameMode());
		menuItem("exit", true);
		ImGui::EndMenu();
	}


	void toolsMenu()
	{
		if (!ImGui::BeginMenu("Tools")) return;

		bool is_any_entity_selected = !m_editor->getSelectedEntities().empty();
		menuItem("lookAtSelected", is_any_entity_selected);
		menuItem("copyViewTransform", is_any_entity_selected);
		menuItem("snapDown", is_any_entity_selected);
		menuItem("autosnapDown", true);
		menuItem("export_game", true);
		if (renderDocOption()) menuItem("launch_renderdoc", true);
		ImGui::EndMenu();
	}

	void viewMenu() {
		if (!ImGui::BeginMenu("View")) return;

		bool is_open = m_asset_browser->isOpen();
		if (ImGui::MenuItem(ICON_FA_IMAGES "Asset browser", nullptr, &is_open)) {
			m_asset_browser->setOpen(is_open);		
		}
		menuItem("entityList", true);
		ImGui::MenuItem(ICON_FA_COMMENT_ALT "Log", nullptr, &m_log_ui->m_is_open);
		ImGui::MenuItem(ICON_FA_CHART_AREA "Profiler", nullptr, &m_profiler_ui->m_is_open);
		ImGui::MenuItem(ICON_FA_INFO_CIRCLE "Inspector", nullptr, &m_property_grid->m_is_open);
		menuItem("settings", true);
		ImGui::Separator();
		for (Action* action : m_window_actions) {
			Lumix::menuItem(*action, true);
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

			float w = (ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x) * 0.5f - 30 - ImGui::GetCursorPosX();
			ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeightWithSpacing()));
			getAction("toggleGameMode")->toolbarButton(m_big_icon_font);
			getAction("pauseGameMode")->toolbarButton(m_big_icon_font);
			getAction("nextFrame")->toolbarButton(m_big_icon_font);

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
		Universe* universe = m_editor->getUniverse();
		bool selected = selected_entities.indexOf(entity) >= 0;
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_AllowItemOverlap;
		bool has_child = universe->getFirstChild(entity).isValid();
		if (!has_child) flags = ImGuiTreeNodeFlags_Leaf;
		if (selected) flags |= ImGuiTreeNodeFlags_Selected;
		flags |= ImGuiTreeNodeFlags_SpanAvailWidth;
		
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
			const ImVec2 cp = ImGui::GetCursorPos();
			ImGui::Dummy(ImVec2(1.f, ImGui::GetTextLineHeightWithSpacing()));
			if (ImGui::IsItemVisible()) {
				ImGui::SetCursorPos(cp);
				char buffer[1024];
				getEntityListDisplayName(*this, *universe, Span(buffer), entity);
				node_open = ImGui::TreeNodeEx((void*)(intptr_t)entity.index, flags, "%s", buffer);
			}
			else {
				const char* dummy = "";
				const ImGuiID id = ImGui::GetCurrentWindow()->GetID((void*)(intptr_t)entity.index);
				if (ImGui::TreeNodeBehaviorIsOpen(id, flags)) {
					ImGui::SetCursorPos(cp);
					node_open = ImGui::TreeNodeBehavior(id, flags, dummy, dummy);
				}
				else {
					node_open = false;
				}
			}
		}
		
		if (ImGui::IsItemVisible()) {
			ImGui::PushID(entity.index);
			if (ImGui::IsMouseReleased(1) && ImGui::IsItemHovered()) ImGui::OpenPopup("entity_context_menu");
			if (ImGui::BeginPopup("entity_context_menu"))
			{
				if (ImGui::MenuItem("Create child"))
				{
					m_editor->beginCommandGroup("create_child_entity");
					EntityRef child = m_editor->addEntity();
					m_editor->makeParent(entity, child);
					const DVec3 pos = m_editor->getUniverse()->getPosition(entity);
					m_editor->setEntitiesPositions(&child, &pos, 1);
					m_editor->endCommandGroup();
				}
				if (ImGui::MenuItem("Select all children"))
				{
					Array<EntityRef> tmp(m_allocator);
					Universe* universe = m_editor->getUniverse();
					for (EntityPtr e = universe->getFirstChild(entity); e.isValid(); e = universe->getNextSibling(*e)) {
						tmp.push(*e);
					}
					m_editor->selectEntities(tmp, false);
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
			if (ImGui::BeginDragDropSource())
			{
				char buffer[1024];
				getEntityListDisplayName(*this, *universe, Span(buffer), entity);
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

	void folderUI(EntityFolders::FolderID folder_id, EntityFolders& folders, u32 level) {
		const EntityFolders::Folder& folder = folders.getFolder(folder_id);
		ImGui::PushID(&folder);
		bool node_open;
		ImGuiTreeNodeFlags flags = level == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0;
		flags |= ImGuiTreeNodeFlags_OpenOnArrow;
		if (folders.getSelectedFolder() == folder_id) flags |= ImGuiTreeNodeFlags_Selected;
		if (m_renaming_folder == folder_id) {
			node_open = ImGui::TreeNodeEx((void*)&folder, flags, "%s", ICON_FA_FOLDER);
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
				m_editor->renameEntityFolder(m_renaming_folder, m_rename_buf);
			}
			if (ImGui::IsItemDeactivated()) {
				m_renaming_folder = EntityFolders::INVALID_FOLDER;
			}
			m_set_rename_focus = false;
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor();
		}
		else {
			node_open = ImGui::TreeNodeEx((void*)&folder, flags, "%s%s", ICON_FA_FOLDER, folder.name);
		}
		
		if (ImGui::BeginDragDropTarget()) {
			if (auto* payload = ImGui::AcceptDragDropPayload("entity")) {
				EntityRef dropped_entity = *(EntityRef*)payload->Data;
				m_editor->beginCommandGroup("move_entity_to_folder_group");
				m_editor->makeParent(INVALID_ENTITY, dropped_entity);
				m_editor->moveEntityToFolder(dropped_entity, folder_id);
				m_editor->endCommandGroup();
			}
			ImGui::EndDragDropTarget();
		}

		if (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered()) {
			folders.selectFolder(folder_id);
		}

		if (ImGui::IsMouseReleased(1) && ImGui::IsItemHovered()) {
			ImGui::OpenPopup("folder_context_menu");
		}
		if (ImGui::BeginPopup("folder_context_menu")) {
			if (ImGui::Selectable("New folder")) {
				EntityFolders::FolderID new_folder = m_editor->createEntityFolder(folder_id);
				m_renaming_folder = new_folder;
				m_set_rename_focus = true;
			}
			if (ImGui::Selectable("Delete")) {
				m_editor->destroyEntityFolder(folder_id);
			}

			const bool has_children = folders.getFolder(folder_id).first_entity.isValid();
			if (ImGui::Selectable("Select entities", false, has_children ? 0 : ImGuiSelectableFlags_Disabled)) {
				Array<EntityRef> entities(m_allocator);
				EntityPtr e = folders.getFolder(folder_id).first_entity;
				while (e.isValid()) {
					entities.push((EntityRef)e);
					const EntityPtr next = folders.getNextEntity((EntityRef)e);
					e = next;
				}
				m_editor->selectEntities(entities, false);
			}
			if (level > 0 && ImGui::Selectable("Rename")) {
				m_renaming_folder = folder_id;
				m_set_rename_focus = true;
			}
			ImGui::EndPopup();
		}

		if (!node_open) {
			ImGui::PopID();
			return;
		}

		u16 child_id = folder.child_folder;
		while (child_id != EntityFolders::INVALID_FOLDER) {
			const EntityFolders::Folder& child = folders.getFolder(child_id);
			folderUI(child_id, folders, level + 1);
			child_id = child.next_folder;
		}

		EntityPtr child_e = folder.first_entity;
		while (child_e.isValid()) {
			if (!m_editor->getUniverse()->getParent((EntityRef)child_e).isValid()) {
				showHierarchy((EntityRef)child_e, m_editor->getSelectedEntities());
			}
			child_e = folders.getNextEntity((EntityRef)child_e);
		}

		ImGui::TreePop();
		ImGui::PopID();
	}

	void onEntityListGUI()
	{
		PROFILE_FUNCTION();
		const Array<EntityRef>& entities = m_editor->getSelectedEntities();
		static char filter[64] = "";
		if (!m_is_entity_list_open) return;
		if (ImGui::Begin(ICON_FA_STREAM "Hierarchy##hierarchy", &m_is_entity_list_open))
		{
			Universe* universe = m_editor->getUniverse();
			const float w = ImGui::CalcTextSize(ICON_FA_TIMES).x + ImGui::GetStyle().ItemSpacing.x * 2;
			ImGui::SetNextItemWidth(-w);
			ImGui::InputTextWithHint("##filter", "Filter", filter, sizeof(filter));
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) {
				filter[0] = '\0';
			}

			if (ImGui::BeginChild("entities")) {
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetStyle().FramePadding.x);
				
				if (filter[0] == '\0') {
					EntityFolders& folders = m_editor->getEntityFolders();
					folderUI(folders.getRoot(), folders, 0);
				} else {
					for (EntityPtr e = universe->getFirstEntity(); e.isValid(); e = universe->getNextEntity((EntityRef)e)) {
						char buffer[1024];
						getEntityListDisplayName(*this, *universe, Span(buffer), e);
						if (stristr(buffer, filter) == nullptr) continue;
						ImGui::PushID(e.index);
						const EntityRef e_ref = (EntityRef)e;
						bool selected = entities.indexOf(e_ref) >= 0;
						if (ImGui::Selectable(buffer, &selected, ImGuiSelectableFlags_SpanAvailWidth)) {
							m_editor->selectEntities(Span(&e_ref, 1), ImGui::GetIO().KeyCtrl);
						}
						if (ImGui::BeginDragDropSource()) {
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

			if (ImGui::BeginDragDropTarget()) {
				if (auto* payload = ImGui::AcceptDragDropPayload("entity")) {
					EntityRef dropped_entity = *(EntityRef*)payload->Data;
					m_editor->beginCommandGroup("move_entity_to_folder_group");
					m_editor->makeParent(INVALID_ENTITY, dropped_entity);
					m_editor->moveEntityToFolder(dropped_entity, 0);
					m_editor->endCommandGroup();
				}
				ImGui::EndDragDropTarget();
			}
		}
		ImGui::End();
	}


	void dummy() {}


	void setFullscreen(bool fullscreen) override
	{
		if (fullscreen) {
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
		m_settings.m_is_asset_browser_open = m_asset_browser->isOpen();
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
		if (!fs.getContentSync(Path(path), data)) return nullptr;
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
			if (fs.getContentSync(Path("editor/fonts/fa-regular-400.ttf"), icons_data)) {
				ImFont* icons_font = io.Fonts->AddFontFromMemoryTTF((void*)icons_data.data(), (i32)icons_data.size(), size * 0.75f, &config, icon_ranges);
				ASSERT(icons_font);
			}
			copyString(config.Name, "editor/fonts/fa-solid-900.ttf");
			icons_data.clear();
			if (fs.getContentSync(Path("editor/fonts/fa-solid-900.ttf"), icons_data)) {
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
		pio.Monitors.resize(0);
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

		m_font = addFontFromFile("editor/fonts/notosans-regular.ttf", (float)m_settings.m_font_size * font_scale, true);
		m_bold_font = addFontFromFile("editor/fonts/notosans-bold.ttf", (float)m_settings.m_font_size * font_scale, true);
		
		OutputMemoryStream data(m_allocator);
		if (fs.getContentSync(Path("editor/fonts/fa-solid-900.ttf"), data)) {
			const float size = (float)m_settings.m_font_size * font_scale * 1.25f;
			ImFontConfig cfg;
			copyString(cfg.Name, "editor/fonts/fa-solid-900.ttf");
			cfg.FontDataOwnedByAtlas = false;
			cfg.GlyphMinAdvanceX = size; // Use if you want to make the icon monospaced
			static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
			m_big_icon_font = io.Fonts->AddFontFromMemoryTTF((void*)data.data(), (i32)data.size(), size, &cfg, icon_ranges);
			cfg.MergeMode = true;
			copyString(cfg.Name, "editor/fonts/fa-regular-400.ttf");
			if (fs.getContentSync(Path("editor/fonts/fa-regular-400.ttf"), data)) {
				ImFont* icons_font = io.Fonts->AddFontFromMemoryTTF((void*)data.data(), (i32)data.size(), size, &cfg, icon_ranges);
				ASSERT(icons_font);
			}
		}

		if (!m_font || !m_bold_font) {
			os::messageBox(
				"Could not open editor/fonts/notosans-regular.ttf or editor/fonts/NotoSans-Bold.ttf\n"
				"It very likely means that data are not bundled with\n"
				"the exe and the exe is not in the correct directory.\n"
				"The program will eventually crash!"
			);
		}

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

		m_asset_browser->setOpen(m_settings.m_is_asset_browser_open);
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
		m_export.dest_dir = "";
		m_settings.getValue(Settings::LOCAL, "export_dir", Span(m_export.dest_dir.data));
		m_settings.getValue(Settings::LOCAL, "export_pack", m_export.pack);
	}


	void addActions()
	{
		addAction<&StudioAppImpl::newUniverse>(ICON_FA_PLUS "New", "New universe", "newUniverse", ICON_FA_PLUS);
		addAction<&StudioAppImpl::save>(
			ICON_FA_SAVE "Save", "Save universe", "save", ICON_FA_SAVE, os::Keycode::S, Action::Modifiers::CTRL);
		addAction<&StudioAppImpl::saveAs>(
			NO_ICON "Save As", "Save universe as", "saveAs", "", os::Keycode::S, Action::Modifiers::CTRL | Action::Modifiers::SHIFT);
		addAction<&StudioAppImpl::exit>(
			ICON_FA_SIGN_OUT_ALT "Exit", "Exit Studio", "exit", ICON_FA_SIGN_OUT_ALT, os::Keycode::X, Action::Modifiers::CTRL);
		addAction<&StudioAppImpl::redo>(
			ICON_FA_REDO "Redo", "Redo scene action", "redo", ICON_FA_REDO, os::Keycode::Z, Action::Modifiers::CTRL | Action::Modifiers::SHIFT);
		addAction<&StudioAppImpl::undo>(
			ICON_FA_UNDO "Undo", "Undo scene action", "undo", ICON_FA_UNDO, os::Keycode::Z, Action::Modifiers::CTRL);
		addAction<&StudioAppImpl::copy>(
			ICON_FA_CLIPBOARD "Copy", "Copy entity", "copy", ICON_FA_CLIPBOARD, os::Keycode::C, Action::Modifiers::CTRL);
		addAction<&StudioAppImpl::paste>(
			ICON_FA_PASTE "Paste", "Paste entity", "paste", ICON_FA_PASTE, os::Keycode::V, Action::Modifiers::CTRL);
		addAction<&StudioAppImpl::duplicate>(
			ICON_FA_CLONE "Duplicate", "Duplicate entity", "duplicate", ICON_FA_CLONE, os::Keycode::D, Action::Modifiers::CTRL);
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
			Action::Modifiers::NONE);
		addAction<&StudioAppImpl::savePrefab>(ICON_FA_SAVE "Save prefab", "Save selected entities as prefab", "savePrefab", ICON_FA_SAVE);
		addAction<&StudioAppImpl::makeParent>(ICON_FA_OBJECT_GROUP "Make parent", "Make entity parent", "makeParent", ICON_FA_OBJECT_GROUP);
		addAction<&StudioAppImpl::unparent>(ICON_FA_OBJECT_UNGROUP "Unparent", "Unparent entity", "unparent", ICON_FA_OBJECT_UNGROUP);

		addAction<&StudioAppImpl::nextFrame>(ICON_FA_STEP_FORWARD "Next frame", "Next frame", "nextFrame", ICON_FA_STEP_FORWARD);
		addAction<&StudioAppImpl::pauseGame>(ICON_FA_PAUSE "Pause", "Pause game mode", "pauseGameMode", ICON_FA_PAUSE)
			.is_selected.bind<&Engine::isPaused>(m_engine.get());
		addAction<&StudioAppImpl::toggleGameMode>(ICON_FA_PLAY "Game Mode", "Toggle game mode", "toggleGameMode", ICON_FA_PLAY)
			.is_selected.bind<&WorldEditor::isGameMode>(m_editor.get());
		addAction<&StudioAppImpl::autosnapDown>(NO_ICON "Autosnap down", "Toggle autosnap down", "autosnapDown")
			.is_selected.bind<&Gizmo::Config::isAutosnapDown>(&getGizmoConfig());
		addAction<&StudioAppImpl::launchRenderDoc>(NO_ICON "Launch RenderDoc", "Launch RenderDoc", "launch_renderdoc");
		addAction<&StudioAppImpl::snapDown>(NO_ICON "Snap down", "Snap entities down", "snapDown");
		addAction<&StudioAppImpl::copyViewTransform>(NO_ICON "Copy view transform", "Copy view transform", "copyViewTransform");
		addAction<&StudioAppImpl::lookAtSelected>(NO_ICON "Look at selected", "Look at selected entity", "lookAtSelected");
		addAction<&StudioAppImpl::toggleAssetBrowser>(ICON_FA_IMAGES "Asset Browser", "Toggle asset browser", "assetBrowser", ICON_FA_IMAGES)
			.is_selected.bind<&StudioAppImpl::isAssetBrowserOpen>(this);
		addAction<&StudioAppImpl::toggleEntityList>(ICON_FA_STREAM "Hierarchy", "Toggle hierarchy", "entityList", ICON_FA_STREAM)
			.is_selected.bind<&StudioAppImpl::isEntityListOpen>(this);
		addAction<&StudioAppImpl::toggleSettings>(ICON_FA_COG "Settings", "Toggle settings UI", "settings", ICON_FA_COG)
			.is_selected.bind<&StudioAppImpl::areSettingsOpen>(this);
		addAction<&StudioAppImpl::showExportGameDialog>(ICON_FA_FILE_EXPORT "Export game", "Export game", "export_game", ICON_FA_FILE_EXPORT);
	}


	static bool copyPlugin(const char* src, int iteration, char (&out)[LUMIX_MAX_PATH])
	{
		char tmp_path[LUMIX_MAX_PATH];
		os::getExecutablePath(Span(tmp_path));
		StaticString<LUMIX_MAX_PATH> copy_path;
		copyString(Span(copy_path.data), Path::getDir(tmp_path));
		copy_path << "plugins/" << iteration;
		if (!os::makePath(copy_path)) logError("Could not create ", copy_path);
		copyString(Span(tmp_path), Path::getBasename(src));
		copy_path << "/" << tmp_path << "." << getPluginExtension();
#ifdef _WIN32
		StaticString<LUMIX_MAX_PATH> src_pdb(src);
		StaticString<LUMIX_MAX_PATH> dest_pdb(copy_path);
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

			char src[LUMIX_MAX_PATH];
			parser.getCurrent(src, lengthOf(src));

			bool is_full_path = findSubstring(src, ".") != nullptr;
			Lumix::IPlugin* loaded_plugin;
			if (is_full_path)
			{
				char copy_path[LUMIX_MAX_PATH];
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
				char dir[LUMIX_MAX_PATH];
				copyString(Span(m_watched_plugin.basename.data), Path::getBasename(src));
				copyString(Span(dir), Path::getDir(src));
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

		char basename[LUMIX_MAX_PATH];
		copyString(Span(basename), Path::getBasename(path));
		if (!equalIStrings(basename, m_watched_plugin.basename)) return;

		m_watched_plugin.reload_request = true;
	}


	void tryReloadPlugin()
	{
		/*
		m_watched_plugin.reload_request = false;

		StaticString<LUMIX_MAX_PATH> src(m_watched_plugin.dir, m_watched_plugin.basename, ".", getPluginExtension());
		char copy_path[LUMIX_MAX_PATH];
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

	bool workersCountOption(u32& workers_count) {
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
		char path[LUMIX_MAX_PATH];
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
					for (const reflection::IAttribute* attr : prop.attributes) {
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
		addPlugin(*createSplineEditor(*this));

		for (IPlugin* plugin : m_plugins) {
			logInfo("Studio plugin ", plugin->getName(), " loaded");
		}

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
		void visit(const reflection::ArrayProperty& prop) override { notSupported(prop); }
		void visit(const reflection::BlobProperty& prop) override { notSupported(prop); }

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
		editor.beginCommandGroup("createEntityEx");
		EntityRef e = editor.addEntity();
		editor.selectEntities(Span(&e, 1), false);

		lua_pushvalue(L, 2);
		lua_pushnil(L);
		while (lua_next(L, -2) != 0)
		{
			const char* parameter_name = LuaWrapper::toType<const char*>(L, -2);
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
							const char* property_name = LuaWrapper::toType<const char*>(L, -2);
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
		if (!ResourceType(type).isValid()) return 0;
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

				char tmp[LUMIX_MAX_PATH];
				parser.getCurrent(tmp, lengthOf(tmp));
				OutputMemoryStream content(m_allocator);
				
				if (m_engine->getFileSystem().getContentSync(Path(tmp), content)) {
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

	static bool includeFileInExport(const char* filename) {
		if (filename[0] == '.') return false;
		if (compareStringN("bin/", filename, 4) == 0) return false;
		if (equalStrings("main.pak", filename)) return false;
		if (equalStrings("error.log", filename)) return false;
		return true;
	}

	static bool includeDirInExport(const char* filename) {
		if (filename[0] == '.') return false;
		if (compareStringN("bin", filename, 4) == 0) return false;
		return true;
	}

	struct ExportFileInfo {
		u32 hash;
		u64 offset;
		u64 size;

		char path[LUMIX_MAX_PATH];
	};

	void scanCompiled(AssociativeArray<u32, ExportFileInfo>& infos) {
		os::FileIterator* iter = m_engine->getFileSystem().createFileIterator(".lumix/assets");
		const char* base_path = m_engine->getFileSystem().getBasePath();
		os::FileInfo info;
		while (os::getNextFile(iter, &info)) {
			if (info.is_directory) continue;

			Span<const char> basename = Path::getBasename(info.filename);
			ExportFileInfo rec;
			fromCString(Span(basename), rec.hash);
			rec.offset = 0;
			rec.size = os::getFileSize(StaticString<LUMIX_MAX_PATH>(base_path, ".lumix/assets/", info.filename));
			copyString(rec.path, ".lumix/assets/");
			catString(rec.path, info.filename);
			infos.insert(rec.hash, rec);
		}
		
		os::destroyFileIterator(iter);

		exportDataScan("pipelines/", infos);
		exportDataScan("universes/", infos);
	}

	void exportDataScan(const char* dir_path, AssociativeArray<u32, ExportFileInfo>& infos)
	{
		auto* iter = m_engine->getFileSystem().createFileIterator(dir_path);
		const char* base_path = m_engine->getFileSystem().getBasePath();
		os::FileInfo info;
		while (os::getNextFile(iter, &info)) {
			char normalized_path[LUMIX_MAX_PATH];
			Path::normalize(info.filename, Span(normalized_path));
			if (info.is_directory)
			{
				if (!includeDirInExport(normalized_path)) continue;

				char dir[LUMIX_MAX_PATH] = {0};
				if (dir_path[0] != '.') copyString(dir, dir_path);
				catString(dir, info.filename);
				catString(dir, "/");
				exportDataScan(dir, infos);
				continue;
			}

			if (!includeFileInExport(normalized_path)) continue;

			StaticString<LUMIX_MAX_PATH> out_path;
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
			out_info.size = os::getFileSize(StaticString<LUMIX_MAX_PATH>(base_path, out_path.data));
			out_info.offset = ~0UL;
		}
		os::destroyFileIterator(iter);
	}


	void exportDataScanResources(AssociativeArray<u32, ExportFileInfo>& infos)
	{
		ResourceManagerHub& rm = m_engine->getResourceManager();
		for (auto iter = rm.getAll().begin(), end = rm.getAll().end(); iter != end; ++iter) {
			const auto& resources = iter.value()->getResourceTable();
			for (Resource* res : resources) {
				u32 hash = res->getPath().getHash();
				const StaticString<LUMIX_MAX_PATH> baked_path(".lumix/assets/", hash, ".res");

				auto& out_info = infos.emplace(hash);
				copyString(Span(out_info.path), baked_path);
				out_info.hash = hash;
				out_info.size = os::getFileSize(baked_path);
				out_info.offset = ~0UL;
			}
		}
		exportDataScan("pipelines/", infos);
		exportDataScan("universes/", infos);
	}


	void showExportGameDialog() { m_is_export_game_dialog_open = true; }


	void onExportDataGUI() {
		if (!m_is_export_game_dialog_open) return;

		ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Export game", &m_is_export_game_dialog_open)) {
			ImGuiEx::Label("Destination dir");
			if (ImGui::Button(m_export.dest_dir.empty() ? "..." : m_export.dest_dir.data)) {
				if (os::getOpenDirectory(Span(m_export.dest_dir.data), m_engine->getFileSystem().getBasePath())) {
					m_settings.setValue(Settings::LOCAL, "export_dir", m_export.dest_dir);
				}
			}

			ImGuiEx::Label("Pack data");
			if (ImGui::Checkbox("##pack", &m_export.pack)) {
				m_settings.setValue(Settings::LOCAL, "export_pack", m_export.pack);
			}
			ImGuiEx::Label("Mode");
			if (ImGui::Combo("##mode", (int*)&m_export.mode, "All files\0Loaded universe\0")) {
				m_settings.setValue(Settings::LOCAL, "export_pack", (i32)m_export.mode);
			}

			ImGuiEx::Label("Startup universe");
			if (m_universes.size() > 0 && m_export.startup_universe[0] == '\0') m_export.startup_universe = m_universes[0].data;
			if (ImGui::BeginCombo("##startunv", m_export.startup_universe)) {
				for (const auto& unv : m_universes) {
					if (ImGui::Selectable(unv)) m_export.startup_universe = unv.data;
				}
				ImGui::EndCombo();
			}

			if (ImGui::Button("Export")) exportData();
		}
		ImGui::End();
	}


	void exportData() {
		if (m_export.dest_dir.empty()) return;

		AssociativeArray<u32, ExportFileInfo> infos(m_allocator);
		infos.reserve(10000);

		switch (m_export.mode) {
			case ExportConfig::Mode::ALL_FILES: scanCompiled(infos); break;
			case ExportConfig::Mode::CURRENT_UNIVERSE: exportDataScanResources(infos); break;
			default: ASSERT(false); break;
		}

		{
			StaticString<LUMIX_MAX_PATH> project_path(m_export.dest_dir, "lumix.prj");
			OutputMemoryStream prj_blob(m_allocator);
			m_engine->serializeProject(prj_blob, m_export.startup_universe);
			os::OutputFile file;
			if (!file.open(project_path)) {
				logError("Could not create ", project_path);
				return;
			}

			(void)file.write(prj_blob.data(), prj_blob.size());

			file.close();
			if (file.isError()) {
				logError("Could not write ", project_path);
				return;
			}
		}


		FileSystem& fs = m_engine->getFileSystem();
		if (m_export.pack) {
			static const char* OUT_FILENAME = "main.pak";
			char dest[LUMIX_MAX_PATH];
			copyString(dest, m_export.dest_dir);
			catString(dest, OUT_FILENAME);
			if (infos.size() == 0) {
				logError("No files found while trying to create ", dest);
				return;
			}
			u64 total_size = 0;
			for (ExportFileInfo& info : infos) {
				info.offset = total_size;
				total_size += info.size;
			}
			
			os::OutputFile file;
			if (!file.open(dest)) {
				logError("Could not create ", dest);
				return;
			}

			const u32 count = (u32)infos.size();
			bool success = file.write(&count, sizeof(count));

			for (auto& info : infos) {
				success = file.write(&info.hash, sizeof(info.hash)) && success;
				success = file.write(&info.offset, sizeof(info.offset)) && success;
				success = file.write(&info.size, sizeof(info.size)) && success;
			}

			OutputMemoryStream src(m_allocator);
			for (const ExportFileInfo& info : infos) {
				src.clear();
				if (!fs.getContentSync(Path(info.path), src)) {
					logError("Could not read ", info.path);
					file.close();
					return;
				}
				success = file.write(src.data(), src.size()) && success;
			}
			file.close();

			if (!success) {
				logError("Could not write ", dest);
				return;
			}
		}
		else {
			char dest[LUMIX_MAX_PATH];
			copyString(dest, m_export.dest_dir);
			const char* base_path = fs.getBasePath();
			for (auto& info : infos) {
				StaticString<LUMIX_MAX_PATH> src(base_path, info.path);
				StaticString<LUMIX_MAX_PATH> dst(dest, info.path);

				StaticString<LUMIX_MAX_PATH> dst_dir(dest, Path::getDir(info.path));
				if (!os::makePath(dst_dir) && !os::dirExists(dst_dir)) {
					logError("Failed to create ", dst_dir);
					return;
				}

				if (!os::copyFile(src, dst)) {
					logError("Failed to copy ", src, " to ", dst);
					return;
				}
			}
		}

		const char* bin_files[] = {"app.exe", "dbghelp.dll", "dbgcore.dll"};
		StaticString<LUMIX_MAX_PATH> src_dir("bin/");
		if (!os::fileExists("bin/app.exe")) {
			char tmp[LUMIX_MAX_PATH];
			os::getExecutablePath(Span(tmp));
			copyString(Span(src_dir.data), Path::getDir(tmp));
		}

		for (auto& file : bin_files) {
			StaticString<LUMIX_MAX_PATH> tmp(m_export.dest_dir, file);
			StaticString<LUMIX_MAX_PATH> src(src_dir, file);
			if (!os::copyFile(src, tmp)) {
				logError("Failed to copy ", src, " to ", tmp);
			}
		}

		for (GUIPlugin* plugin : m_gui_plugins)	{
			if (!plugin->exportData(m_export.dest_dir)) {
				logError("Plugin ", plugin->getName(), " failed to pack data.");
			}
		}
		logInfo("Exporting finished.");
	}


	void scanUniverses()
	{
		m_universes.clear();
		auto* iter = m_engine->getFileSystem().createFileIterator("universes");
		os::FileInfo info;
		while (os::getNextFile(iter, &info))
		{
			if (info.filename[0] == '.') continue;
			if (info.is_directory) continue;
			if (startsWith(info.filename, "__")) continue;
			if (!Path::hasExtension(info.filename, "unv")) continue;

			char basename[LUMIX_MAX_PATH];
			copyString(Span(basename), Path::getBasename(info.filename));
			m_universes.emplace(basename);
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
		if (os::isKeyDown(os::Keycode::SHIFT)) pressed_modifiers |= Action::Modifiers::SHIFT;
		if (os::isKeyDown(os::Keycode::CTRL)) pressed_modifiers |= Action::Modifiers::CTRL;
		if (os::isKeyDown(os::Keycode::MENU)) pressed_modifiers |= Action::Modifiers::ALT;

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
	debug::Allocator m_allocator;
	UniquePtr<Engine> m_engine;
	ImGuiKey m_imgui_key_map[255];
	Array<os::WindowHandle> m_windows;
	Array<WindowToDestroy> m_deferred_destroy_windows;
	os::WindowHandle m_main_window;
	os::WindowState m_fullscreen_restore_state;
	Array<Action*> m_owned_actions;
	Array<Action*> m_actions;
	Array<Action*> m_window_actions;
	Array<GUIPlugin*> m_gui_plugins;
	Array<MousePlugin*> m_mouse_plugins;
	Array<IPlugin*> m_plugins;
	Array<IAddComponentPlugin*> m_add_cmp_plugins;
	Array<StaticString<LUMIX_MAX_PATH>> m_universes;
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
	char m_universe_to_load[LUMIX_MAX_PATH];
	UniquePtr<AssetBrowser> m_asset_browser;
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

	struct ExportConfig {
		enum class Mode : i32 {
			ALL_FILES,
			CURRENT_UNIVERSE
		};

		Mode mode = Mode::ALL_FILES;

		bool pack = false;
		StaticString<96> startup_universe;
		StaticString<LUMIX_MAX_PATH> dest_dir;
	};

	ExportConfig m_export;
	bool m_finished;
	bool m_deferred_game_mode_exit;
	int m_exit_code;

	bool m_sleep_when_inactive;
	bool m_is_welcome_screen_open;
	bool m_is_export_game_dialog_open;
	bool m_is_entity_list_open;
	EntityPtr m_renaming_entity = INVALID_ENTITY;
	EntityFolders::FolderID m_renaming_folder = EntityFolders::INVALID_FOLDER;
	bool m_set_rename_focus = false;
	char m_rename_buf[Universe::ENTITY_NAME_MAX_LENGTH];
	bool m_is_f2_pressed = false;
	ImFont* m_font;
	ImFont* m_big_icon_font;
	ImFont* m_bold_font;

	struct WatchedPlugin
	{
		UniquePtr<FileSystemWatcher> watcher;
		StaticString<LUMIX_MAX_PATH> dir;
		StaticString<LUMIX_MAX_PATH> basename;
		Lumix::IPlugin* plugin = nullptr;
		int iteration = 0;
		bool reload_request = false;
	} m_watched_plugin;
};

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