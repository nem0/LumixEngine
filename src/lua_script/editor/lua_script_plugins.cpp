#define LUMIX_NO_CUSTOM_CRT
#include <lua.h>
#ifdef LUMIX_LUAU_ANALYSIS
	#include <Luau/AstQuery.h>
	#include <Luau/Autocomplete.h>
	#include <Luau/Frontend.h>
	#include <Luau/BuiltinDefinitions.h>
#endif
#include <imgui/imgui.h>

#include "core/allocator.h"
#include "core/array.h"
#include "core/crt.h"
#include "core/hash.h"
#include "core/log.h"
#include "core/math.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/stream.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/editor_asset.h"
#include "editor/property_grid.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/component_uid.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/lua_wrapper.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "lua_script/lua_script.h"
#include "lua_script/lua_script_system.h"


using namespace Lumix;


static const ComponentType LUA_SCRIPT_TYPE = reflection::getComponentType("lua_script");


namespace {

#ifdef LUMIX_LUAU_ANALYSIS
struct LuauAnalysis :Luau::FileResolver {
	struct Location {
		u32 line;
		u32 col;
	};

	struct Range {
		Location from, to;
	};

	struct OpenEditor {
		Path path;
		CodeEditor* editor;
	};

	LuauAnalysis(StudioApp& app)
		: m_app(app)
		, m_luau_frontend(this, &m_luau_config_resolver)
		, m_open_editors(app.getAllocator())
	{
		OutputMemoryStream def_blob(m_app.getAllocator());

		Luau::registerBuiltinGlobals(m_luau_frontend, m_luau_frontend.globals, false);
		Luau::registerBuiltinGlobals(m_luau_frontend, m_luau_frontend.globalsForAutocomplete, true);
		
		if (m_app.getEngine().getFileSystem().getContentSync(Path("scripts/lumix.d.lua"), def_blob)) {
			std::string_view def_src((const char*)def_blob.data(), def_blob.size());
			m_luau_frontend.loadDefinitionFile(m_luau_frontend.globals, m_luau_frontend.globals.globalScope, def_src, "@lumix", false, false);
			m_luau_frontend.loadDefinitionFile(m_luau_frontend.globalsForAutocomplete, m_luau_frontend.globalsForAutocomplete.globalScope, def_src, "@lumix", false, true);
		}
	}

	std::optional<Range> goTo(const char* module_name, u32 line, u32 col) {
		auto* source_module = m_luau_frontend.getSourceModule(module_name);
		Luau::ModulePtr module = m_luau_frontend.moduleResolverForAutocomplete.getModule(module_name);
		if (!source_module || !module) return {};

		Luau::Position position(line, col);
		auto binding = Luau::findBindingAtPosition(*module, *source_module, position);
		if (binding) {
			Range res;
			res.from.col = binding->location.begin.column;
			res.from.line = binding->location.begin.line;
			res.to.col = binding->location.end.column;
			res.to.line = binding->location.end.line;
			return res;
		}
		return {};
	}

	template <typename T>
	Range autocomplete(const char* file, u32 line, u32 col, const T& f) {
		Luau::Position pos(line, col);
		
		Luau::AutocompleteResult result = Luau::autocomplete(m_luau_frontend, file, pos, 
			[&](const std::string& tag, std::optional<const Luau::ClassType*> ctx, std::optional<std::string> contents) -> std::optional<Luau::AutocompleteEntryMap> {
				return {};
			}
		);

		if (result.entryMap.empty()) return {};

		for (auto& [name, entry] : result.entryMap) {
			f(name.c_str());
		}
		const Luau::AstNode* node = result.ancestry.back();
		const Luau::Location* loc = &node->location;
		if (const auto* index = node->as<Luau::AstExprIndexName>()) {
			if (index->indexLocation.begin.line != index->expr->location.end.line) {
				Range res;
				res.from.line = index->opPosition.line;
				res.from.col = index->opPosition.column + 1;
				res.to.line = res.from.line;
				res.to.col = res.from.col;
				return res;
			}
			loc = &index->indexLocation;
		}
		Range res;
		res.from.line = loc->begin.line;
		res.from.col = loc->begin.column;
		res.to.line = loc->end.line;
		res.to.col = loc->end.column;
		return res;
	}

	void markDirty(const Path& path) {
		m_luau_frontend.markDirty(path.c_str()); 
		m_luau_frontend.queueModuleCheck(path.c_str());
		Luau::FrontendOptions options;
		options.forAutocomplete = true;
		// TODO don't do this on every change
		m_luau_frontend.checkQueuedModules(options);
	}

	std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name) override {
		for (const LuauAnalysis::OpenEditor& editor : m_open_editors) {
			if (editor.path == name.c_str()) {
				Luau::SourceCode res;
				res.type = Luau::SourceCode::Local;
				OutputMemoryStream blob(m_app.getAllocator());
				editor.editor->serializeText(blob);
				res.source = std::string(blob.data(), blob.data() + blob.size());
				return res;
			}
		}

		OutputMemoryStream blob(m_app.getAllocator());
		if (!m_app.getEngine().getFileSystem().getContentSync(Path(name.c_str()), blob)) return {};
		Luau::SourceCode res;
		res.type = Luau::SourceCode::Local;
		res.source = std::string(blob.data(), blob.data() + blob.size());
		return res;
	}

	void unregisterOpenEditor(const Path& path) {
		m_open_editors.eraseItems([&](const OpenEditor& e){
			return e.path == path;
		});
	}

	void registerOpenEditor(const Path& path, CodeEditor* editor) {
		for (const LuauAnalysis::OpenEditor& editor : m_open_editors) {
			if (editor.path == path) return;
		}

		m_open_editors.push({path, editor});
	}

	StudioApp& m_app;
	Array<OpenEditor> m_open_editors;
	Luau::Frontend m_luau_frontend;
	Luau::NullConfigResolver m_luau_config_resolver;
};
#else
	struct LuauAnalysis { 
		LuauAnalysis(StudioApp& app) {} 
		void markDirty(const Path& path) {}
		void unregisterOpenEditor(const Path& path) {}
		void registerOpenEditor(const Path& path, CodeEditor* editor) {}
	};
#endif

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

	bool onAction(const Action& action) override {
		if (&action == &m_action) {
			runWindowAction();
			return true;
		}
		return false;
	}

	bool exportData(const char* dest_dir) override
	{
		#ifndef LUMIX_STATIC_LUAU
			char exe_path[MAX_PATH];
			os::getExecutablePath(Span(exe_path));
			char exe_dir[MAX_PATH];

			copyString(Span(exe_dir), Path::getDir(exe_path));
			StaticString<MAX_PATH> tmp(exe_dir, "Luau.dll");
			if (!os::fileExists(tmp)) return false;
			StaticString<MAX_PATH> dest(dest_dir, "Luau.dll");
			if (!os::copyFile(tmp, dest))
			{
				logError("Failed to copy ", tmp, " to ", dest);
				return false;
			}
		#endif
		return true; 
	}

	void onGUI() override {
		lua_State* L = m_app.getEngine().getState();
		LuaWrapper::DebugGuard guard(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, m_plugin_ref);
		lua_getfield(L, -1, "gui");
		LuaWrapper::pcall(L, 0, 0);
		lua_pop(L, 1);
	}

	void onSettingsLoaded() override {
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
	
	void onBeforeSettingsSaved() override {
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
	EditorWindow(LuauAnalysis& analysis, const Path& path, StudioApp& app)
		: AssetEditorWindow(app)
		, m_app(app)
		, m_analysis(analysis)
		, m_path(path)
		#ifdef LUMIX_LUAU_ANALYSIS
			, m_autocomplete_list(app.getAllocator())
		#endif
	{
		m_file_async_handle = app.getEngine().getFileSystem().getContent(path, makeDelegate<&EditorWindow::onFileLoaded>(this));
	}

	~EditorWindow() {
		m_analysis.unregisterOpenEditor(m_path);
		if (m_file_async_handle.isValid()) {
			m_app.getEngine().getFileSystem().cancel(m_file_async_handle);
		}
	}

	void underline() {
		#ifdef LUMIX_LUAU_ANALYSIS
			Luau::FrontendOptions options;
			options.forAutocomplete = true;
			Luau::CheckResult check_res = m_analysis.m_luau_frontend.check(m_path.c_str(), options);
			
			for (const Luau::TypeError& err : check_res.errors) {
				const char* msg;
				std::string msg_str;
				if (const auto* syntax_error = Luau::get_if<Luau::SyntaxError>(&err.data))
					msg = syntax_error->message.c_str();
				else {
					msg_str = Luau::toString(err, Luau::TypeErrorToStringOptions{&m_analysis});
					msg = msg_str.c_str();
				}

				m_code_editor->underlineTokens(err.location.begin.line, err.location.begin.column, err.location.end.column, msg);
			}
		#endif
	}

	void onFileLoaded(Span<const u8> data, bool success) {
		m_file_async_handle = FileSystem::AsyncHandle::invalid();
		if (success) {
			StringView v;
			v.begin = (const char*)data.begin();
			v.end = (const char*)data.end();
			m_code_editor = createLuaCodeEditor(m_app);
			m_code_editor->setText(v);
			underline();

			m_analysis.registerOpenEditor(m_path, m_code_editor.get());
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
			
			if (m_code_editor->gui("codeeditor", ImVec2(0, 0), m_app.getDefaultFont())) {
				m_dirty = true;
				m_analysis.markDirty(m_path);
				underline();
			}
			#ifdef LUMIX_LUAU_ANALYSIS
				if (m_code_editor->canHandleInput()) {
					if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && ImGui::GetIO().KeyCtrl && m_code_editor->getNumCursors() == 1) {
						m_autocomplete_list.clear();
						StringView prefix = m_code_editor->getPrefix();
						if (equalStrings(prefix, ".") || equalStrings(prefix, ":")) prefix = {};
						m_analysis.autocomplete(m_path.c_str(), m_code_editor->getCursorLine(), m_code_editor->getCursorColumn(), [&](const char* v){
							if (!startsWith(v, prefix)) return;
							String tmp(v, m_app.getAllocator());
							i32 idx = 0;
							for (; idx < m_autocomplete_list.size(); ++idx) {
								if (compareString(tmp, m_autocomplete_list[idx]) < 0) break;
							}
							m_autocomplete_list.insert(idx, static_cast<String&&>(tmp));
						});
						if (!m_autocomplete_list.empty()) {
							if (m_autocomplete_list.size() == 1) {
								m_code_editor->selectWord();
								m_code_editor->insertText(m_autocomplete_list[0].c_str());
								m_autocomplete_list.clear();
								m_analysis.markDirty(m_path);
								underline();
							}
							else {
								ImGui::OpenPopup("autocomplete");
								m_autocomplete_filter.clear();
								m_autocomplete_selection_idx = 0;
								ImGui::SetNextWindowPos(m_code_editor->getCursorScreenPosition());
							}
						}
					}
					if (ImGui::IsKeyDown(ImGuiKey_F11)) {
						std::optional<LuauAnalysis::Range> range = m_analysis.goTo(m_path.c_str(), m_code_editor->getCursorLine(), m_code_editor->getCursorColumn());
						if (range.has_value()) {
							m_code_editor->setSelection(range->from.line, range->from.col, range->to.line, range->to.col, true);
						}
					}
				}
				if (ImGui::BeginPopup("autocomplete")) {
					u32 sel_idx = m_autocomplete_selection_idx;
					if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) m_autocomplete_selection_idx += m_autocomplete_list.size() - 1;
					if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) ++m_autocomplete_selection_idx;
					m_autocomplete_selection_idx = m_autocomplete_selection_idx % m_autocomplete_list.size();
					if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
						ImGui::CloseCurrentPopup();
						m_code_editor->focus();
					}
					bool is_child = false;
					if (m_autocomplete_list.size() > 12) {
						ImGui::PushFont(m_app.getDefaultFont());
						m_autocomplete_filter.gui("Filter", 250, ImGui::IsWindowAppearing());
						ImGui::PopFont();
						ImGui::BeginChild("asl", ImVec2(00, ImGui::GetTextLineHeight() * 12));
						is_child = true;
					}

					bool is_enter = ImGui::IsKeyPressed(ImGuiKey_Enter);
					u32 i = 0;
					for (const String& s : m_autocomplete_list) {
						if (!m_autocomplete_filter.pass(s.c_str())) continue;
						if (i - 1 == m_autocomplete_selection_idx) ImGui::SetScrollHereY(0.5f);
						// use sel_idx so is_selected is synced with scrolling, which is one frame behind
						const bool is_selected = i == sel_idx; 
						if (ImGui::Selectable(s.c_str(), is_selected) || is_enter && i == m_autocomplete_selection_idx) {
							m_code_editor->selectWord();
							m_code_editor->insertText(s.c_str());
							m_analysis.markDirty(m_path);
							underline();
							ImGui::CloseCurrentPopup();
							m_code_editor->focus();
							m_autocomplete_list.clear();
							break;
						}
						++i;
					}
					m_autocomplete_selection_idx = minimum(m_autocomplete_selection_idx, i - 1);
					if (is_child) ImGui::EndChild();

					ImGui::EndPopup();
				}
			#endif
			ImGui::PopFont();
		}
	}
	
	const Path& getPath() override { return m_path; }
	const char* getName() const override { return "lua script editor"; }

	StudioApp& m_app;
	FileSystem::AsyncHandle m_file_async_handle = FileSystem::AsyncHandle::invalid();
	Path m_path;
	UniquePtr<CodeEditor> m_code_editor;
	LuauAnalysis& m_analysis;
	#ifdef LUMIX_LUAU_ANALYSIS
		Array<String> m_autocomplete_list;
		u32 m_autocomplete_selection_idx = 0;
		TextFilter m_autocomplete_filter;
	#endif
};

static bool gatherRequires(Span<const u8> src, Lumix::Array<Path>& dependencies, const Path& path) {
	lua_State* L = luaL_newstate();

	auto reg_dep = [](lua_State* L) -> int {
		lua_getglobal(L, "__deps");
		Lumix::Array<Path>* deps = (Lumix::Array<Path>*)lua_tolightuserdata(L, -1);
		lua_pop(L, 1);
		const char* path = LuaWrapper::checkArg<const char*>(L, 1);
		Path lua_path(path, ".lua");
		deps->push(lua_path);
		return 0;
	};

	auto index_fn = [](lua_State* L) -> int {
		lua_insert(L, 1);
		return 1;
	};

	auto call_fn = [](lua_State* L) -> int {
		lua_insert(L, 1);
		return 1;
	};

	lua_pushcclosure(L, reg_dep, "require", 0);
	lua_setfield(L, LUA_GLOBALSINDEX, "require");

	lua_pushlightuserdata(L, &dependencies);
	lua_setfield(L, LUA_GLOBALSINDEX, "__deps");
		
	lua_newtable(L); // metatable
	lua_pushcfunction(L, index_fn, "__index"); // metatable, fn
	lua_setfield(L, -2, "__index"); // metatable
		
	lua_pushcfunction(L, call_fn, "__call"); // metatable, fn
	lua_setfield(L, -2, "__call"); // metatable

	lua_newtable(L); // metatable, new_g
	lua_getglobal(L, "require"); // metatable, new_g, require
	lua_setfield(L, -2, "require"); // metatable, new_g
		
	lua_insert(L, -2); // new_g, meta
	lua_setmetatable(L, -2); //new_g
		
	bool errors = LuaWrapper::luaL_loadbuffer(L, (const char*)src.m_begin, src.length(), path.c_str()); // new_g, fn
	if (errors) {
		const char* msg = lua_tostring(L, -1);
		logError(msg);
		lua_close(L);
		return false;
	}
	lua_insert(L, -2); // fn, new_g
	lua_setfenv(L, -2);
	const bool res = LuaWrapper::pcall(L, 0, 0);
	lua_close(L);
	return res;
}

struct AssetPlugin : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	explicit AssetPlugin(LuauAnalysis& analysis, StudioApp& app)
		: m_app(app)
		, m_analysis(analysis)
	{
		app.getAssetCompiler().registerExtension("lua", LuaScript::TYPE);
	}

	void openEditor(const Path& path) override {
		IAllocator& allocator = m_app.getAllocator();
		UniquePtr<EditorWindow> win = UniquePtr<EditorWindow>::create(allocator, m_analysis, path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	bool compile(const Path& src) override {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream src_data(m_app.getAllocator());
		if (!fs.getContentSync(src, src_data)) return false;
		
		Array<Path> deps(m_app.getAllocator());
		if (!gatherRequires(src_data, deps, src)) return false;

		OutputMemoryStream out(m_app.getAllocator());
		out.write(deps.size());
		for (const Path& dep : deps) {
			out.writeString(dep.c_str());
		}
		out.write(src_data.data(), src_data.size());
		return m_app.getAssetCompiler().writeCompiledResource(src, out);
	}

	const char* getLabel() const override { return "Lua script"; }
	bool canCreateResource() const override { return true; }
	const char* getDefaultExtension() const override { return "lua"; }

	void createResource(OutputMemoryStream& blob) override {
		blob << "function update(time_delta)\nend\n";
	}

	StudioApp& m_app;
	LuauAnalysis& m_analysis;
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
	void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, const TextFilter& filter, WorldEditor& editor) override {
		if (filter.isActive()) return;
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
		, m_luau_analysis(app)
		, m_asset_plugin(m_luau_analysis, app)
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
	LuauAnalysis m_luau_analysis;
	AssetPlugin m_asset_plugin;
	PropertyGridPlugin m_property_grid_plugin;
};


} // anonymous namespace


LUMIX_STUDIO_ENTRY(lua_script) {
	PROFILE_FUNCTION();
	IAllocator& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}


