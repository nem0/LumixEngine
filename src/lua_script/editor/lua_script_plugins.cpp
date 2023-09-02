#include <imgui/imgui.h>

#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/editor_asset.h"
#include "editor/property_grid.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/allocator.h"
#include "engine/array.h"
#include "engine/crt.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/hash.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "engine/world.h"
#include "lua_script/lua_script.h"
#include "lua_script/lua_script_system.h"
#include <lua.hpp>


using namespace Lumix;


static const ComponentType LUA_SCRIPT_TYPE = reflection::getComponentType("lua_script");


namespace {


struct EditorWindow : AssetEditorWindow {
	EditorWindow(const Path& path, StudioApp& app)
		: AssetEditorWindow(app)
		, m_app(app)
		, m_path(path)
	{
		m_file_async_handle = app.getEngine().getFileSystem().getContent(path, makeDelegate<&EditorWindow::onFileLoaded>(this));
	}

	~EditorWindow() {
		if (m_file_async_handle.isValid()) {
			m_app.getEngine().getFileSystem().cancel(m_file_async_handle);
		}
	}

	void onFileLoaded(Span<const u8> data, bool success) {
		m_file_async_handle = FileSystem::AsyncHandle::invalid();
		if (success) {
			StringView v;
			v.begin = (const char*)data.begin();
			v.end = (const char*)data.end();
			m_code_editor = createLuaCodeEditor(m_app);
			m_code_editor->setText(v);
		}
	}

	void save() {
		OutputMemoryStream blob(m_app.getAllocator());
		m_code_editor->serializeText(blob);
		m_app.getAssetBrowser().saveResource(m_path, blob);
		m_dirty = false;
	}
	
	bool onAction(const Action& action) override { 
		if (&action == &m_app.getCommonActions().save) save();
		else return false;
		return true;
	}

	void windowGUI() override {
		if (ImGui::BeginMenuBar()) {
			if (ImGuiEx::IconButton(ICON_FA_SAVE, "Save")) save();
			if (ImGuiEx::IconButton(ICON_FA_EXTERNAL_LINK_ALT, "Open externally")) m_app.getAssetBrowser().openInExternalEditor(m_path);
			if (ImGuiEx::IconButton(ICON_FA_SEARCH, "View in browser")) m_app.getAssetBrowser().locate(m_path);
			ImGui::EndMenuBar();
		}

		if (m_file_async_handle.isValid()) {
			ImGui::TextUnformatted("Loading...");
			return;
		}

		if (m_code_editor) {
			ImGui::PushFont(m_app.getMonospaceFont());
			if (m_code_editor->gui("codeeditor", ImVec2(0, 0), m_app.getDefaultFont())) m_dirty = true;
			ImGui::PopFont();
		}
	}
	
	const Path& getPath() override { return m_path; }
	const char* getName() const override { return "lua script editor"; }

	StudioApp& m_app;
	FileSystem::AsyncHandle m_file_async_handle = FileSystem::AsyncHandle::invalid();
	Path m_path;
	UniquePtr<CodeEditor> m_code_editor;
};


struct AssetPlugin : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	explicit AssetPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("lua", LuaScript::TYPE);
	}

	void openEditor(const Path& path) override {
		IAllocator& allocator = m_app.getAllocator();
		UniquePtr<EditorWindow> win = UniquePtr<EditorWindow>::create(allocator, path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	bool compile(const Path& src) override { return m_app.getAssetCompiler().copyCompile(src); }
	const char* getLabel() const override { return "Lua script"; }
	bool canCreateResource() const override { return true; }
	const char* getDefaultExtension() const override { return "lua"; }

	void createResource(OutputMemoryStream& blob) override {
		blob << "function update(time_delta)\nend\n";
	}

	StudioApp& m_app;
};


struct ConsolePlugin final : StudioApp::GUIPlugin
{
	explicit ConsolePlugin(StudioApp& _app)
		: app(_app)
		, open(false)
		, autocomplete(_app.getAllocator())
	{
		m_toggle_ui.init("Script Console", "Toggle script console", "script_console", "", true);
		m_toggle_ui.func.bind<&ConsolePlugin::toggleOpen>(this);
		m_toggle_ui.is_selected.bind<&ConsolePlugin::isOpen>(this);
		app.addWindowAction(&m_toggle_ui);
		buf[0] = '\0';
	}

	~ConsolePlugin() {
		app.removeAction(&m_toggle_ui);
	}

	void onSettingsLoaded() override {
		Settings& settings = app.getSettings();
		open = settings.getValue(Settings::GLOBAL, "is_script_console_open", false);
		if (!buf[0]) {
			StringView dir = Path::getDir(settings.getAppDataPath());
			const StaticString<MAX_PATH> path(dir, "/lua_console_content.lua");
			os::InputFile file;
			if (file.open(path)) {
				const u64 size = file.size();
				if (size + 1 <= sizeof(buf)) {
					if (!file.read(buf, size)) {
						logError("Failed to read ", path);
						buf[0] = '\0';
					}
					else {
						buf[size] = '\0';
					}
				}
				file.close();
			}
		}
	}

	void onBeforeSettingsSaved() override {
		Settings& settings = app.getSettings();
		settings.setValue(Settings::GLOBAL, "is_script_console_open", open);
		if (buf[0]) {
			StringView dir = Path::getDir(settings.getAppDataPath());
			const StaticString<MAX_PATH> path(dir, "/lua_console_content.lua");
			os::OutputFile file;
			if (!file.open(path)) {
				logError("Failed to save ", path);
			}
			else {
				if (!file.write(buf, stringLength(buf))) {
					logError("Failed to write ", path);
				}
				file.close();
			}
		}
	}

	/*static const int LUA_CALL_EVENT_SIZE = 32;

	void systemAdded(GUIPlugin& plugin) override
	{
		if (!equalStrings(plugin.getName(), "animation_editor")) return;

		auto& anim_editor = (AnimEditor::IAnimationEditor&)plugin;
		auto& event_type = anim_editor.createEventType("lua_call");
		event_type.size = LUA_CALL_EVENT_SIZE;
		event_type.label = "Lua call";
		event_type.editor.bind<ConsolePlugin, &ConsolePlugin::onLuaCallEventGUI>(this);
	}


	void onLuaCallEventGUI(u8* data, AnimEditor::Component& component) const
	{
		LuaScriptModule* plugin = (LuaScriptModule*)app.getWorldEditor().getWorld()->getModule(LUA_SCRIPT_TYPE);
		ImGui::InputText("Function", (char*)data, LUA_CALL_EVENT_SIZE);
	}
	*/

	const char* getName() const override { return "script_console"; }


	bool isOpen() const { return open; }
	void toggleOpen() { open = !open; }


	void autocompleteSubstep(lua_State* L, const char* str, ImGuiInputTextCallbackData *data)
	{
		char item[128];
		const char* next = str;
		char* c = item;
		while (*next != '.' && *next != '\0')
		{
			*c = *next;
			++next;
			++c;
		}
		*c = '\0';

		if (!lua_istable(L, -1)) return;

		lua_pushnil(L);
		while (lua_next(L, -2) != 0)
		{
			const char* name = lua_tostring(L, -2);
			if (startsWith(name, item))
			{
				if (*next == '.' && next[1] == '\0')
				{
					autocompleteSubstep(L, "", data);
				}
				else if (*next == '\0')
				{
					autocomplete.push(String(name, app.getAllocator()));
				}
				else
				{
					autocompleteSubstep(L, next + 1, data);
				}
			}
			lua_pop(L, 1);
		}
	}


	static bool isWordChar(char c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
	}


	static int autocompleteCallback(ImGuiInputTextCallbackData *data)
	{
		auto* that = (ConsolePlugin*)data->UserData;
		if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
		{
			lua_State* L = that->app.getEngine().getState();

			int start_word = data->CursorPos;
			char c = data->Buf[start_word - 1];
			while (start_word > 0 && (isWordChar(c) || c == '.'))
			{
				--start_word;
				c = data->Buf[start_word - 1];
			}
			char tmp[128];
			copyString(Span(tmp), StringView(data->Buf + start_word, data->CursorPos - start_word));

			that->autocomplete.clear();
			lua_pushvalue(L, LUA_GLOBALSINDEX);
			that->autocompleteSubstep(L, tmp, data);
			lua_pop(L, 1);
			if (!that->autocomplete.empty())
			{
				that->open_autocomplete = true;
				qsort(&that->autocomplete[0],
					that->autocomplete.size(),
					sizeof(that->autocomplete[0]),
					[](const void* a, const void* b) {
					const String* a_str = (const String*)a;
					const String* b_str = (const String*)b;
					return compareString(*a_str, *b_str);
				});
			}
		}
		else if (that->insert_value)
		{
			int start_word = data->CursorPos;
			char c = data->Buf[start_word - 1];
			while (start_word > 0 && (isWordChar(c)))
			{
				--start_word;
				c = data->Buf[start_word - 1];
			}
			data->InsertChars(data->CursorPos, that->insert_value + data->CursorPos - start_word);
			that->insert_value = nullptr;
		}
		return 0;
	}


	void onGUI() override
	{
		if (!open) return;
		if (ImGui::Begin(ICON_FA_SCROLL "Lua console##lua_console", &open))
		{
			if (ImGui::Button("Execute")) {
				lua_State* L;
				if (run_on_entity) {
					WorldEditor& editor = app.getWorldEditor();
					const Array<EntityRef>& selected = editor.getSelectedEntities();
					if (selected.size() == 1) {
						World& world = *editor.getWorld();
						LuaScriptModule* module = (LuaScriptModule*)world.getModule("lua_script");
						if (world.hasComponent(selected[0], LUA_SCRIPT_TYPE) && module->getScriptCount(selected[0]) > 0) {
							module->execute(selected[0], 0, StringView(buf));
						}
					}
				}
				else {
					L = app.getEngine().getState();
				
					bool errors = luaL_loadbuffer(L, buf, stringLength(buf), nullptr) != 0;
					errors = errors || lua_pcall(L, 0, 0, 0) != 0;

					if (errors)
					{
						logError(lua_tostring(L, -1));
						lua_pop(L, 1);
					}
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Execute file"))
			{
				char tmp[MAX_PATH] = {};
				if (os::getOpenFilename(Span(tmp), "Scripts\0*.lua\0", nullptr))
				{
					os::InputFile file;
					IAllocator& allocator = app.getAllocator();
					if (file.open(tmp))
					{
						size_t size = file.size();
						Array<char> data(allocator);
						data.resize((int)size);
						if (!file.read(&data[0], size)) {
							logError("Could not read ", tmp);
							data.clear();
						}
						file.close();
						lua_State* L = app.getEngine().getState();
						bool errors = luaL_loadbuffer(L, &data[0], data.size(), tmp) != 0;
						errors = errors || lua_pcall(L, 0, 0, 0) != 0;

						if (errors)
						{
							logError(lua_tostring(L, -1));
							lua_pop(L, 1);
						}
					}
					else
					{
						logError("Failed to open file ", tmp);
					}
				}
			}
			ImGui::SameLine();
			ImGui::Checkbox("Run on entity", &run_on_entity);
			if(insert_value) ImGui::SetKeyboardFocusHere();
			ImGui::PushFont(app.getMonospaceFont());
			ImGui::InputTextMultiline("##repl",
				buf,
				lengthOf(buf),
				ImVec2(-1, -1),
				ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackCompletion,
				autocompleteCallback,
				this);
			ImGui::PopFont();

			if (open_autocomplete)
			{
				ImGui::OpenPopup("autocomplete");
				ImGui::SetNextWindowPos(ImGuiEx::GetOsImePosRequest());
			}
			open_autocomplete = false;
			if (ImGui::BeginPopup("autocomplete"))
			{
				if (autocomplete.size() == 1)
				{
					insert_value = autocomplete[0].c_str();
				}
				if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) ++autocomplete_selected;
				if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) --autocomplete_selected;
				if (ImGui::IsKeyPressed(ImGuiKey_Enter)) insert_value = autocomplete[autocomplete_selected].c_str();
				if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
				autocomplete_selected = clamp(autocomplete_selected, 0, autocomplete.size() - 1);
				for (int i = 0, c = autocomplete.size(); i < c; ++i)
				{
					const char* value = autocomplete[i].c_str();
					if (ImGui::Selectable(value, autocomplete_selected == i))
					{
						insert_value = value;
					}
				}
				ImGui::EndPopup();
			}
		}
		ImGui::End();
	}


	StudioApp& app;
	Action m_toggle_ui;
	Array<String> autocomplete;
	bool open;
	bool open_autocomplete = false;
	bool run_on_entity = false;
	int autocomplete_selected = 1;
	const char* insert_value = nullptr;
	char buf[10 * 1024];
};


struct AddComponentPlugin final : StudioApp::IAddComponentPlugin
{
	explicit AddComponentPlugin(StudioApp& app)
		: app(app)
		, file_selector("lua", app)
	{
	}


	void onGUI(bool create_entity, bool, EntityPtr parent, WorldEditor& editor) override
	{
		if (!ImGui::BeginMenu("File")) return;
		Path path;
		AssetBrowser& asset_browser = app.getAssetBrowser();
		bool new_created = false;
		if (ImGui::BeginMenu("New")) {
			file_selector.gui(false, "lua");
			if (ImGui::Button("Create")) {
				path = file_selector.getPath();
				os::OutputFile file;
				FileSystem& fs = app.getEngine().getFileSystem();
				if (fs.open(file_selector.getPath(), file)) {
					new_created = true;
					file.close();
				}
				else {
					logError("Failed to create ", path);
				}
			}
			ImGui::EndMenu();
		}
		bool create_empty = ImGui::Selectable("Empty", false);

		static FilePathHash selected_res_hash;
		if (asset_browser.resourceList(path, selected_res_hash, LuaScript::TYPE, false) || create_empty || new_created)
		{
			editor.beginCommandGroup("createEntityWithComponent");
			if (create_entity)
			{
				EntityRef entity = editor.addEntity();
				editor.selectEntities(Span(&entity, 1), false);
			}
			if (editor.getSelectedEntities().empty()) return;
			EntityRef entity = editor.getSelectedEntities()[0];

			if (!editor.getWorld()->hasComponent(entity, LUA_SCRIPT_TYPE))
			{
				editor.addComponent(Span(&entity, 1), LUA_SCRIPT_TYPE);
			}

			const ComponentUID cmp = editor.getWorld()->getComponent(entity, LUA_SCRIPT_TYPE);
			editor.addArrayPropertyItem(cmp, "scripts");

			if (!create_empty) {
				auto* script_scene = static_cast<LuaScriptModule*>(editor.getWorld()->getModule(LUA_SCRIPT_TYPE));
				int scr_count = script_scene->getScriptCount(entity);
				editor.setProperty(cmp.type, "scripts", scr_count - 1, "Path", Span((const EntityRef*)&entity, 1), path);
			}
			if (parent.isValid()) editor.makeParent(parent, entity);
			editor.endCommandGroup();
			editor.lockGroupCommand();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndMenu();
	}


	const char* getLabel() const override 
	{
		return "Lua Script / File";
	}


	StudioApp& app;
	FileSelector file_selector;
};

struct PropertyGridPlugin final : PropertyGrid::IPlugin
{
	void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, WorldEditor& editor) override {
		if (cmp_type != LUA_SCRIPT_TYPE) return;
		if (entities.length() != 1) return;

		LuaScriptModule* module = (LuaScriptModule*)editor.getWorld()->getModule(cmp_type); 
		const EntityRef e = entities[0];
		const u32 count = module->getScriptCount(e);
		for (u32 i = 0; i < count; ++i) {
			if (module->beginFunctionCall(e, i, "onGUI")) {
				module->endFunctionCall();
			}
		}
	}
};

struct StudioAppPlugin : StudioApp::IPlugin
{
	StudioAppPlugin(StudioApp& app)
		: m_app(app)
		, m_asset_plugin(app)
		, m_console_plugin(app)
	{
	}

	const char* getName() const override { return "lua_script"; }

	void init() override
	{
		AddComponentPlugin* add_cmp_plugin = LUMIX_NEW(m_app.getAllocator(), AddComponentPlugin)(m_app);
		m_app.registerComponent(ICON_FA_MOON, "lua_script", *add_cmp_plugin);

		const char* exts[] = { "lua" };
		m_app.getAssetCompiler().addPlugin(m_asset_plugin, Span(exts));
		m_app.getAssetBrowser().addPlugin(m_asset_plugin, Span(exts));
		m_app.addPlugin(m_console_plugin);
		m_app.getPropertyGrid().addPlugin(m_property_grid_plugin);
	}

	~StudioAppPlugin()
	{
		m_app.getAssetCompiler().removePlugin(m_asset_plugin);
		m_app.getAssetBrowser().removePlugin(m_asset_plugin);
		m_app.removePlugin(m_console_plugin);
		m_app.getPropertyGrid().removePlugin(m_property_grid_plugin);
	}

	bool showGizmo(WorldView& view, ComponentUID cmp) override
	{
		if (cmp.type == LUA_SCRIPT_TYPE)
		{
			auto* module = static_cast<LuaScriptModule*>(cmp.module);
			int count = module->getScriptCount((EntityRef)cmp.entity);
			for (int i = 0; i < count; ++i)
			{
				if (module->beginFunctionCall((EntityRef)cmp.entity, i, "onDrawGizmo"))
				{
					module->endFunctionCall();
				}
			}
			return true;
		}
		return false;
	}
	
	StudioApp& m_app;
	AssetPlugin m_asset_plugin;
	ConsolePlugin m_console_plugin;
	PropertyGridPlugin m_property_grid_plugin;
};


} // anonymous namespace


LUMIX_STUDIO_ENTRY(lua_script) {
	PROFILE_FUNCTION();
	IAllocator& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}


