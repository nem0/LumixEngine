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
#include "engine/lua_wrapper.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "engine/world.h"
#include "lua_script/lua_script.h"
#include "lua_script/lua_script_system.h"
#include <lua.h>


using namespace Lumix;


static const ComponentType LUA_SCRIPT_TYPE = reflection::getComponentType("lua_script");


namespace {

/*
-- example lua usage
Editor.addAction {
	name ="spawn_10_cubes",
	label = "Spawn 10 cubes",
	run = function()
		for i = 1, 10 do
			Editor.createEntityEx {
				position = { 3 * i, 0, 0 },
				model_instance = { Source = "models/shapes/cube.fbx" }
			}
		end
	end
}
*/ 
static int LUA_addAction(lua_State* L) {
	struct LuaAction {
		void run() {
			LuaWrapper::DebugGuard guard(L);
			lua_rawgeti(L, LUA_REGISTRYINDEX, ref_action);
			lua_getfield(L, -1, "run");
			LuaWrapper::pcall(L, 0, 0);
			lua_pop(L, 1);
		}
		Action action;
		lua_State* L;
		int ref_thread;
		int ref_action;
	};

	LuaWrapper::DebugGuard guard(L);
	StudioApp* app = LuaWrapper::getClosureObject<StudioApp>(L);
	LuaWrapper::checkTableArg(L, 1);
	char name[64];
	char label[128];
	if (!LuaWrapper::checkStringField(L, 1, "name", Span(name))) luaL_argerror(L, 1, "missing name");
	if (!LuaWrapper::checkStringField(L, 1, "label", Span(label))) luaL_argerror(L, 1, "missing label");

	// TODO leak
	LuaAction* action = LUMIX_NEW(app->getAllocator(), LuaAction);

	lua_pushthread(L);
	action->ref_thread = LuaWrapper::createRef(L);
	lua_pushvalue(L, 1);
	action->ref_action = LuaWrapper::createRef(L);
	lua_pop(L, 2);
	action->action.init(label, label, name, "", Action::Type::IMGUI_PRIORITY);
	action->action.func.bind<&LuaAction::run>(action);
	action->L = L;
	app->addAction(&action->action);
	return 0;
}

struct StudioLuaPlugin : StudioApp::GUIPlugin {
	static void create(StudioApp& app, StringView content, const Path& path) {
		lua_State* L = app.getEngine().getState();
		LuaWrapper::DebugGuard guard(L);
		if (!LuaWrapper::execute(L, content, path.c_str(), 1)) return;

		if (lua_isnil(L, -1)) {
			lua_pop(L, 1);
			return;
		}

		if (lua_getfield(L, -1, "name") != LUA_TSTRING) {
			logError(path, ": missing `name` or `name` is not a string");
			return;
		}
		const char* name = LuaWrapper::toType<const char*>(L, -1);


		StudioLuaPlugin* plugin = LUMIX_NEW(app.getAllocator(), StudioLuaPlugin)(app, name);
		lua_pop(L, 1);
		
		if (lua_getfield(L, -1, "windowMenuAction") == LUA_TFUNCTION) {
			char tmp[64];
			convertToLuaName(name, tmp);
			plugin->m_action.init(name, name, tmp, "", Action::IMGUI_PRIORITY);
			plugin->m_action.func.bind<&StudioLuaPlugin::runWindowAction>(plugin);
			app.addWindowAction(&plugin->m_action);
		}
		lua_pop(L, 1);
		
		plugin->m_plugin_ref = LuaWrapper::createRef(L);
		lua_pop(L, 1);
		app.addPlugin(*plugin);

	} 
	
	static void convertToLuaName(const char* src, Span<char> out) {
		const u32 max_size = out.length();
		ASSERT(max_size > 0);
		char* dest = out.begin();
		while (*src && dest - out.begin() < max_size - 1) {
			if (isLetter(*src)) {
				*dest = isUpperCase(*src) ? *src - 'A' + 'a' : *src;
				++dest;
			}
			else if (isNumeric(*src)) {
				*dest = *src;
				++dest;
			}
			else {
				*dest = '_';
				++dest;
			}
			++src;
		}
		*dest = 0;
	}

	StudioLuaPlugin(StudioApp& app, const char* name)
		: m_app(app)
		, m_name(name, app.getAllocator())
	{}
	
	~StudioLuaPlugin() {
		m_app.removeAction(&m_action);
	}

	void runWindowAction() {
		lua_State* L = m_app.getEngine().getState();
		LuaWrapper::DebugGuard guard(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, m_plugin_ref);
		lua_getfield(L, -1, "windowMenuAction");
		LuaWrapper::pcall(L, 0, 0);
		lua_pop(L, 1);
	}

	bool onAction(const Action& action) {
		if (&action == &m_action) {
			runWindowAction();
			return true;
		}
		return false;
	}


	void onGUI() override {
		lua_State* L = m_app.getEngine().getState();
		LuaWrapper::DebugGuard guard(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, m_plugin_ref);
		lua_getfield(L, -1, "gui");
		LuaWrapper::pcall(L, 0, 0);
		lua_pop(L, 1);
	}

	void onSettingsLoaded() {
		lua_State* L = m_app.getEngine().getState();
		LuaWrapper::DebugGuard guard(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, m_plugin_ref);
		if (lua_getfield(L, -1, "settings") == LUA_TNIL) {
			lua_pop(L, 2);
			return;
		}
		if (!lua_istable(L, -1)) {
			logError(m_name, ": settings must be a table");
			lua_pop(L, 1);
			return;
		}

		lua_pushnil(L);
		while (lua_next(L, -2) != 0) {
			if (!lua_isstring(L, -2)) {
				logError(m_name, ": settings must be a table with string keys");
				lua_pop(L, 3);
				return;
			} 
			const char* setting_name = lua_tostring(L, -2);
			switch (lua_type(L, -1)) {
				case LUA_TBOOLEAN: {
					bool val = lua_toboolean(L, -1);
					val = m_app.getSettings().getValue(Settings::LOCAL, setting_name, val);
					lua_pushboolean(L, val);
					lua_setfield(L, -4, setting_name);
					break;
				}
				case LUA_TNUMBER: {
					float val = (float)lua_tonumber(L, -1);
					val = m_app.getSettings().getValue(Settings::LOCAL, setting_name, val);
					lua_pushnumber(L, val);
					lua_setfield(L, -4, setting_name);
					break;
				}
				case LUA_TSTRING: {
					const char* val = lua_tostring(L, -1);
					val = m_app.getSettings().getStringValue(Settings::LOCAL, setting_name, val);
					lua_pushstring(L, val);
					lua_setfield(L, -4, setting_name);
					break;
				}
				default: break;
			}
			lua_pop(L, 1);
		}

		lua_pop(L, 2);
	}
	
	void onBeforeSettingsSaved() {
		lua_State* L = m_app.getEngine().getState();
		LuaWrapper::DebugGuard guard(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, m_plugin_ref);
		if (lua_getfield(L, -1, "settings") == LUA_TNIL) {
			lua_pop(L, 2);
			return;
		}
		if (!lua_istable(L, -1)) {
			logError(m_name, ": settings must be a table");
			lua_pop(L, 1);
			return;
		}

		lua_pushnil(L);
		while (lua_next(L, -2) != 0) {
			if (!lua_isstring(L, -2)) {
				logError(m_name, ": settings must be a table with string keys");
				lua_pop(L, 3);
				return;
			} 
			const char* setting_name = lua_tostring(L, -2);
			switch (lua_type(L, -1)) {
				case LUA_TBOOLEAN: {
					bool val = lua_toboolean(L, -1);
					m_app.getSettings().setValue(Settings::LOCAL, setting_name, val);
					break;
				}
				case LUA_TNUMBER: {
					float val = (float)lua_tonumber(L, -1);
					m_app.getSettings().setValue(Settings::LOCAL, setting_name, val);
					break;
				}
				case LUA_TSTRING: {
					const char* val = lua_tostring(L, -1);
					m_app.getSettings().setValue(Settings::LOCAL, setting_name, val);
					break;
				}
				default:
					logError(m_name, "settings: ", setting_name, " has unsupported type");
					break;
			}
			lua_pop(L, 1);
		}

		lua_pop(L, 2);
	}

	const char* getName() const override { return m_name.c_str(); }

	StudioApp& m_app;
	Action m_action;
	String m_name;
	i32 m_plugin_ref;
};

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

struct StudioAppPlugin : StudioApp::IPlugin {
	StudioAppPlugin(StudioApp& app)
		: m_app(app)
		, m_asset_plugin(app)
	{
		lua_State* L = app.getEngine().getState();
		LuaWrapper::createSystemClosure(L, "Editor", &app, "addAction", &LUA_addAction);
		initPlugins();
	}

	void initPlugins() {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		os::FileIterator* iter = fs.createFileIterator("editor/scripts/plugins");
		os::FileInfo info;
		while (os::getNextFile(iter, &info)) {
			info.is_directory = info.is_directory;
			if (info.is_directory) continue;
			if (!Path::hasExtension(info.filename, "lua")) continue;

			OutputMemoryStream blob(m_app.getAllocator());
			const Path path("editor/scripts/plugins/", info.filename);
			if (!fs.getContentSync(path, blob)) continue;

			StringView content;
			content.begin = (const char*)blob.data();
			content.end = content.begin + blob.size();
			StudioLuaPlugin::create(m_app, content, path);
		}
		os::destroyFileIterator(iter);
	}

	const char* getName() const override { return "lua_script"; }

	void init() override
	{
		AddComponentPlugin* add_cmp_plugin = LUMIX_NEW(m_app.getAllocator(), AddComponentPlugin)(m_app);
		m_app.registerComponent(ICON_FA_MOON, "lua_script", *add_cmp_plugin);

		const char* exts[] = { "lua" };
		m_app.getAssetCompiler().addPlugin(m_asset_plugin, Span(exts));
		m_app.getAssetBrowser().addPlugin(m_asset_plugin, Span(exts));
		m_app.getPropertyGrid().addPlugin(m_property_grid_plugin);
	}

	~StudioAppPlugin()
	{
		m_app.getAssetCompiler().removePlugin(m_asset_plugin);
		m_app.getAssetBrowser().removePlugin(m_asset_plugin);
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
	PropertyGridPlugin m_property_grid_plugin;
};


} // anonymous namespace


LUMIX_STUDIO_ENTRY(lua_script) {
	PROFILE_FUNCTION();
	IAllocator& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}


