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
#include "core/command_line_parser.h"
#include "core/crt.h"
#include "core/hash.h"
#include "core/log.h"
#include "core/math.h"
#include "core/os.h"
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
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "lua_script.h"
#include "lua_script_system.h"
#include "lua_wrapper.h"
#include "renderer/editor/game_view.h"
#include "renderer/editor/scene_view.h"


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

struct StudioLuaPlugin : StudioApp::GUIPlugin {
	static StudioLuaPlugin* create(StudioApp& app, StringView content, const Path& path) {
		LuaScriptSystem* system = (LuaScriptSystem*)app.getEngine().getSystemManager().getSystem("lua_script");
		lua_State* L = system->getState();
		LuaWrapper::DebugGuard guard(L);
		if (!LuaWrapper::execute(L, content, path.c_str(), 1)) return nullptr;

		if (lua_isnil(L, -1)) {
			lua_pop(L, 1);
			return nullptr;
		}

		if (lua_getfield(L, -1, "name") != LUA_TSTRING) {
			logError(path, ": missing `name` or `name` is not a string");
			return nullptr;
		}
		const char* name = LuaWrapper::toType<const char*>(L, -1);

		StudioLuaPlugin* plugin = LUMIX_NEW(app.getAllocator(), StudioLuaPlugin)(app, name);
		lua_pop(L, 1);


		if (lua_getfield(L, -1, "windowMenuAction") == LUA_TFUNCTION) {
			char tmp[64];
			convertToLuaName(name, tmp);
			plugin->m_action.create("Lua Studio plugin", name, name, tmp, "", Action::WINDOW);
		}
		lua_pop(L, 1);

		plugin->m_plugin_ref = LuaWrapper::createRef(L);
		lua_pop(L, 1);
		app.addPlugin(*plugin);
		return plugin;
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
		LuaScriptSystem* system = (LuaScriptSystem*)m_app.getEngine().getSystemManager().getSystem("lua_script");
		lua_State* L = system->getState();
		
		// check window action
		if (m_action.get() && m_app.checkShortcut(*m_action.get(), true)) {
			LuaWrapper::DebugGuard guard(L);
			lua_rawgeti(L, LUA_REGISTRYINDEX, m_plugin_ref);
			lua_getfield(L, -1, "windowMenuAction");
			LuaWrapper::pcall(L, 0, 0);
			lua_pop(L, 1);
		}

		// gui
		LuaWrapper::DebugGuard guard(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, m_plugin_ref);
		lua_getfield(L, -1, "gui");
		LuaWrapper::pcall(L, 0, 0);
		lua_pop(L, 1);
	}

	void onSettingsLoaded() override {
		LuaScriptSystem* system = (LuaScriptSystem*)m_app.getEngine().getSystemManager().getSystem("lua_script");
		lua_State* L = system->getState();
		
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
					bool val = lua_toboolean(L, -1) != 0;
					val = m_app.getSettings().getBool(setting_name, val);
					lua_pushboolean(L, val);
					lua_setfield(L, -4, setting_name);
					break;
				}
				case LUA_TNUMBER: {
					float val = (float)lua_tonumber(L, -1);
					val = m_app.getSettings().getFloat(setting_name, val);
					lua_pushnumber(L, val);
					lua_setfield(L, -4, setting_name);
					break;
				}
				case LUA_TSTRING: {
					const char* val = lua_tostring(L, -1);
					val = m_app.getSettings().getString(setting_name, val);
					lua_pushstring(L, val);
					lua_setfield(L, -4, setting_name);
					break;
				}
				default:
					logError(m_path, ": ", setting_name, " has unsupported type");
					break;
			}
			lua_pop(L, 1);
		}

		lua_pop(L, 2);
	}
	
	void onBeforeSettingsSaved() override {
		LuaScriptSystem* system = (LuaScriptSystem*)m_app.getEngine().getSystemManager().getSystem("lua_script");
		lua_State* L = system->getState();

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
					bool val = lua_toboolean(L, -1) != 0;
					m_app.getSettings().setBool(setting_name, val, Settings::Storage::WORKSPACE);
					break;
				}
				case LUA_TNUMBER: {
					float val = (float)lua_tonumber(L, -1);
					m_app.getSettings().setFloat(setting_name, val, Settings::Storage::WORKSPACE);
					break;
				}
				case LUA_TSTRING: {
					const char* val = lua_tostring(L, -1);
					m_app.getSettings().setString(setting_name, val, Settings::Storage::WORKSPACE);
					break;
				}
				default:
					logError(m_path, ": ", setting_name, " has unsupported type");
					break;
			}
			lua_pop(L, 1);
		}

		lua_pop(L, 2);
	}

	const char* getName() const override { return m_name.c_str(); }

	StudioApp& m_app;
	Path m_path;
	Local<Action> m_action;
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
	
	void windowGUI() override {
		CommonActions& actions = m_app.getCommonActions();

		if (ImGui::BeginMenuBar()) {
			if (actions.save.iconButton(m_dirty, &m_app)) save();
			if (actions.open_externally.iconButton(true, &m_app)) m_app.getAssetBrowser().openInExternalEditor(m_path);
			if (actions.view_in_browser.iconButton(true, &m_app)) m_app.getAssetBrowser().locate(m_path);
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
		lua_newtable(L);
		return 1;
	};

	auto index_fn = [](lua_State* L) -> int {
		lua_insert(L, 1);
		return 1;
	};

	auto call_fn = [](lua_State* L) -> int {
		lua_insert(L, 1);
		return 1;
	};

	lua_pushcfunction(L, reg_dep, "inherit");
	lua_setfield(L, LUA_GLOBALSINDEX, "inherit");

	lua_pushcfunction(L, reg_dep, "require");
	lua_setfield(L, LUA_GLOBALSINDEX, "require");

	lua_pushcfunction(L, reg_dep, "dofile");
	lua_setfield(L, LUA_GLOBALSINDEX, "dofile");

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
	lua_getglobal(L, "dofile"); // metatable, new_g, require
	lua_setfield(L, -2, "dofile"); // metatable, new_g
	lua_getglobal(L, "inherit"); // metatable, new_g, require
	lua_setfield(L, -2, "inherit"); // metatable, new_g

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
	ResourceType getResourceType() const override { return LuaScript::TYPE; }
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

			const ComponentUID cmp(entity, LUA_SCRIPT_TYPE, editor.getWorld()->getModule(LUA_SCRIPT_TYPE));
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

template <typename T> struct StoredType { 
	using Type = T; 
	static T construct(T value, IAllocator& allocator) { return value; }
	static T get(T value) { return value; }
	static T toType(lua_State* L, i32 idx, LuaScriptSystem& system) { return LuaWrapper::toType<T>(L, -1); }
	static void push(lua_State* L, T value, LuaScriptSystem&, ResourceType, WorldEditor&) { LuaWrapper::push(L, value); }
};

template <> struct StoredType<const char*> {
	using Type = String;
	static String construct(const char* value, IAllocator& allocator) { return String(value, allocator); }
	static const char* get(const String& value) { return value.c_str(); }
	static const char* toType(lua_State* L, i32 idx, LuaScriptSystem& system) { return LuaWrapper::toType<const char*>(L, -1); }
	static void push(lua_State* L, StringView value, LuaScriptSystem&, ResourceType, WorldEditor&) { LuaWrapper::push(L, value); }
};

template <> struct StoredType<EntityRef>;

template <> struct StoredType<EntityPtr> {
	using Type = EntityPtr; 
	static EntityPtr construct(EntityPtr value, IAllocator& allocator) { return value; }
	static EntityPtr get(EntityPtr value) { return value; }
	static EntityPtr toType(lua_State* L, i32 idx, LuaScriptSystem& system) { return LuaWrapper::toType<EntityPtr>(L, -1); }
	static void push(lua_State* L, EntityPtr value, LuaScriptSystem&, ResourceType, WorldEditor& editor) {
		LuaWrapper::pushEntity(L, value, editor.getWorld());
	}
};

template <> struct StoredType<Path> {
	using Type = Path;
	static Path construct(Path value, IAllocator& allocator) { return value; }
	static Path get(Path value) { return value; }
	
	static Path toType(lua_State* L, i32 idx, LuaScriptSystem& system) {
		lua_getfield(L, -1, "_handle");
		int res_idx = lua_tointeger(L, -1);
		lua_pop(L, 1);
		Resource* res = system.getLuaResource(res_idx);
		return res ? res->getPath() : Path();
	}

	static void push(lua_State* L, Path value, LuaScriptSystem& system, ResourceType resource_type, WorldEditor&) { 
		const i32 res_idx = value.isEmpty() ? -1 : system.addLuaResource(value, resource_type);
		
		lua_newtable(L);
		lua_getglobal(L, "Lumix");
		lua_getfield(L, -1, "Resource");
		lua_setmetatable(L, -3);
		lua_pop(L, 1);

		LuaWrapper::push(L, res_idx);
		lua_setfield(L, -2, "_handle");

		lua_pushlightuserdata(L, (void*)resource_type.type.getHashValue());
		lua_setfield(L, -2, "_type");
	}
};

template <typename T>
struct RemoveLuaArrayElementCommand : IEditorCommand {
	RemoveLuaArrayElementCommand(LuaScriptSystem& system, WorldEditor& editor, EntityRef entity, u32 script_index, const char* property_name, int index, ResourceType resource_type = {})
		: m_system(system)
		, m_editor(editor)
		, m_entity(entity)
		, m_script_index(script_index)
		, m_property_name(property_name)
		, m_index(index)
		, m_old_value(StoredType<T>::construct({}, editor.getAllocator()))
		, m_resource_type(resource_type)
	{}

	bool execute() override {
		LuaScriptModule* module = (LuaScriptModule*)m_editor.getWorld()->getModule(LUA_SCRIPT_TYPE);
		lua_State* L = module->getState(m_entity, m_script_index);
		if (!L) return false;

		LuaWrapper::DebugGuard guard(L);
		const i32 env = module->getEnvironment(m_entity, m_script_index);

		lua_rawgeti(L, LUA_REGISTRYINDEX, env);
		lua_getfield(L, -1, m_property_name);
		int len = (int)lua_objlen(L, -1);
		ASSERT (m_index >= 0 && m_index < len);

		lua_rawgeti(L, -1, m_index + 1);
		m_old_value = StoredType<T>::toType(L, -1, m_system);
		lua_pop(L, 1);

		for (int i = m_index + 1; i < len; ++i) {
			lua_rawgeti(L, -1, i + 1);
			lua_rawseti(L, -2, i);
		}

		lua_pushnil(L);
		lua_rawseti(L, -2, len);
		lua_pop(L, 2);
		return true;
	}

	void undo() override {
		LuaScriptModule* module = (LuaScriptModule*)m_editor.getWorld()->getModule(LUA_SCRIPT_TYPE);
		lua_State* L = module->getState(m_entity, m_script_index);
		if (!L) return;

		LuaWrapper::DebugGuard guard(L);
		const i32 env = module->getEnvironment(m_entity, m_script_index);

		lua_rawgeti(L, LUA_REGISTRYINDEX, env);
		lua_getfield(L, -1, m_property_name);
		int len = (int)lua_objlen(L, -1);

		for (int i = len; i >= m_index + 1; --i) {
			lua_rawgeti(L, -1, i);
			lua_rawseti(L, -2, i + 1);
		}

		StoredType<T>::push(L, m_old_value, m_system, m_resource_type, m_editor);
		
		lua_rawseti(L, -2, m_index + 1);
		lua_pop(L, 2);
	}

	const char* getType() override { return "remove_lua_array_element"; }
	bool merge(IEditorCommand& command) override { return false; }

	LuaScriptSystem& m_system;
	WorldEditor& m_editor;
	EntityRef m_entity;
	u32 m_script_index;
	const char* m_property_name;
	int m_index;
	StoredType<T>::Type m_old_value;
	ResourceType m_resource_type;
};

template <typename T>
struct SetLuaPropertyCommand : IEditorCommand {
	SetLuaPropertyCommand(LuaScriptSystem& system, WorldEditor& editor, EntityRef entity, u32 script_index, const char* property_name, T value, i32 array_index = -1, ResourceType resource_type = {})
		: m_editor(editor)
		, m_entity(entity)
		, m_script_index(script_index)
		, m_property_name(property_name)
		, m_new_value(StoredType<T>::construct(value, editor.getAllocator()))
		, m_old_value(StoredType<T>::construct({}, editor.getAllocator()))
		, m_system(system)
		, m_array_index(array_index)
		, m_resource_type(resource_type)
	{
		LuaScriptModule* module = (LuaScriptModule*)m_editor.getWorld()->getModule(LUA_SCRIPT_TYPE);
		lua_State* L = module->getState(m_entity, m_script_index);
		ASSERT(L);
		
		LuaWrapper::DebugGuard guard(L);
		const i32 env = module->getEnvironment(m_entity, m_script_index);
		lua_rawgeti(L, LUA_REGISTRYINDEX, env);
		lua_getfield(L, -1, m_property_name);
		
		if (m_array_index >= 0) {
			const int type = lua_rawgeti(L, -1, m_array_index + 1);
			if (type == LUA_TNIL) {
				m_old_value = StoredType<T>::construct({}, editor.getAllocator());
				m_new_element = true;
			}
			else {
				m_old_value = StoredType<T>::toType(L, -1, m_system);
			}
			lua_pop(L, 1);
		}
		else {
			m_old_value = StoredType<T>::toType(L, -1, m_system);
		}
		
		lua_pop(L, 2);
	}

	bool execute() override {
		return setValue(StoredType<T>::get(m_new_value));
	}

	void undo() override {
		if (m_new_element) {
			LuaScriptModule* module = (LuaScriptModule*)m_editor.getWorld()->getModule(LUA_SCRIPT_TYPE);
			lua_State* L = module->getState(m_entity, m_script_index);
			ASSERT(L);

			LuaWrapper::DebugGuard guard(L);
			const i32 env = module->getEnvironment(m_entity, m_script_index);
			lua_rawgeti(L, LUA_REGISTRYINDEX, env);
			lua_getfield(L, -1, m_property_name);
			int len = (int)lua_objlen(L, -1);
			lua_pushnil(L);
			lua_rawseti(L, -2, len);
			lua_pop(L, 2);
		}
		else {
			setValue(StoredType<T>::get(m_old_value));
		}
	}

	bool setValue(T value) {
		LuaScriptModule* module = (LuaScriptModule*)m_editor.getWorld()->getModule(LUA_SCRIPT_TYPE);
		lua_State* L = module->getState(m_entity, m_script_index);
		ASSERT(L);
		
		LuaWrapper::DebugGuard guard(L);
		const i32 env = module->getEnvironment(m_entity, m_script_index);
		lua_rawgeti(L, LUA_REGISTRYINDEX, env);
		
		if constexpr (IsSame<T, Path>::Value) {
			lua_getfield(L, -1, m_property_name);

			if (m_array_index >= 0) {
				if (LUA_TNIL != lua_rawgeti(L, -1, m_array_index + 1)) {
					lua_getfield(L, -1, "_handle");
					int prev_res_idx = lua_tointeger(L, -1);
					lua_pop(L, 2);
					m_system.unloadLuaResource(prev_res_idx);
				}
				else {
					lua_pop(L, 1);
				}

				StoredType<T>::push(L, value, m_system, m_resource_type, m_editor);

				lua_rawseti(L, -2, m_array_index + 1);
				lua_pop(L, 2);
			}
			else {
				lua_getfield(L, -1, "_handle");
				int prev_res_idx = lua_tointeger(L, -1);
				lua_pop(L, 2);
				m_system.unloadLuaResource(prev_res_idx);

				StoredType<T>::push(L, value, m_system, m_resource_type, m_editor);

				lua_setfield(L, -2, m_property_name);
				lua_pop(L, 1);
			}
			return true;
		}
		else {
			if (m_array_index >= 0) {
				lua_getfield(L, -1, m_property_name);
				
				StoredType<T>::push(L, value, m_system, m_resource_type, m_editor);

				lua_rawseti(L, -2, m_array_index + 1);
				lua_pop(L, 2);
			}
			else {
				lua_pushstring(L, m_property_name);
				
				StoredType<T>::push(L, value, m_system, m_resource_type, m_editor);
				
				lua_settable(L, -3);
				lua_pop(L, 1);
			}
			return true;
		}
	}

	const char* getType() override { return "set_lua_property"; }

	bool merge(IEditorCommand& command) override { 
		ASSERT(command.getType() == getType());
		auto& my_command = static_cast<SetLuaPropertyCommand<T>&>(command);
		if (my_command.m_array_index != m_array_index) return false;
		if (my_command.m_entity != m_entity) return false;
		if (my_command.m_script_index != m_script_index) return false;
		if (!equalStrings(my_command.m_property_name, m_property_name)) return false;
		
		my_command.m_new_value = static_cast<StoredType<T>::Type&&>(m_new_value);
		return true;		
	}

	WorldEditor& m_editor;
	LuaScriptSystem& m_system;
	EntityRef m_entity;
	u32 m_script_index;
	const char* m_property_name;
	StoredType<T>::Type m_new_value;
	StoredType<T>::Type m_old_value;
	i32 m_array_index;
	bool m_new_element = false;
	ResourceType m_resource_type;
};

struct PropertyGridPlugin final : PropertyGrid::IPlugin {
	PropertyGridPlugin(StudioApp& app)
		: m_app(app)
		, m_editor(app.getWorldEditor())
	{}

	void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, const TextFilter& filter, WorldEditor& editor) override {}
	
	void removeElement(LuaScriptSystem& system, EntityRef e, u32 script_idx, const char* name, const LuaScriptModule::Property property, i32 idx) {
		UniquePtr<IEditorCommand> cmd;
		IAllocator& allocator = m_editor.getAllocator();
		switch (property.type) {
			case LuaScriptModule::Property::BOOLEAN: cmd = UniquePtr<RemoveLuaArrayElementCommand<bool>>::create(allocator, system, m_editor, e, script_idx, name, idx); break;
			case LuaScriptModule::Property::INT: cmd = UniquePtr<RemoveLuaArrayElementCommand<int>>::create(allocator, system, m_editor, e, script_idx, name, idx); break;
			case LuaScriptModule::Property::FLOAT: cmd = UniquePtr<RemoveLuaArrayElementCommand<float>>::create(allocator, system, m_editor, e, script_idx, name, idx); break;
			case LuaScriptModule::Property::COLOR: cmd = UniquePtr<RemoveLuaArrayElementCommand<Vec3>>::create(allocator, system, m_editor, e, script_idx, name, idx); break;
			case LuaScriptModule::Property::STRING: cmd = UniquePtr<RemoveLuaArrayElementCommand<const char*>>::create(allocator, system, m_editor, e, script_idx, name, idx); break;
			case LuaScriptModule::Property::RESOURCE: cmd = UniquePtr<RemoveLuaArrayElementCommand<Path>>::create(allocator, system, m_editor, e, script_idx, name, idx, property.resource_type); break;
			case LuaScriptModule::Property::ENTITY: cmd = UniquePtr<RemoveLuaArrayElementCommand<EntityPtr>>::create(allocator, system, m_editor, e, script_idx, name, idx); break;
			case LuaScriptModule::Property::ANY: ASSERT(false); break;
		}
		m_editor.executeCommand(cmd.move());
	}

	void addElement(LuaScriptSystem& system, EntityRef e, u32 script_idx, const char* name, const LuaScriptModule::Property& property, i32 idx) {
		IAllocator& allocator = m_editor.getAllocator();
		UniquePtr<IEditorCommand> cmd;
		switch (property.type) {
			case LuaScriptModule::Property::Type::BOOLEAN: cmd = UniquePtr<SetLuaPropertyCommand<bool>>::create(allocator, system, m_editor, e, script_idx, name, true, idx); break;
			case LuaScriptModule::Property::Type::INT: cmd = UniquePtr<SetLuaPropertyCommand<int>>::create(allocator, system, m_editor, e, script_idx, name, 0, idx); break;
			case LuaScriptModule::Property::Type::FLOAT: cmd = UniquePtr<SetLuaPropertyCommand<float>>::create(allocator, system, m_editor, e, script_idx, name, 0.0f, idx); break;
			case LuaScriptModule::Property::Type::COLOR: cmd = UniquePtr<SetLuaPropertyCommand<Vec3>>::create(allocator, system, m_editor, e, script_idx, name, Vec3(1, 1, 1), idx); break;
			case LuaScriptModule::Property::Type::STRING: cmd = UniquePtr<SetLuaPropertyCommand<const char*>>::create(allocator, system, m_editor, e, script_idx, name, "", idx); break;
			case LuaScriptModule::Property::Type::RESOURCE: cmd = UniquePtr<SetLuaPropertyCommand<Path>>::create(allocator, system, m_editor, e, script_idx, name, Path(), idx, property.resource_type); break;
			case LuaScriptModule::Property::Type::ENTITY: cmd = UniquePtr<SetLuaPropertyCommand<EntityPtr>>::create(allocator, system, m_editor, e, script_idx, name, INVALID_ENTITY, idx); break;
			case LuaScriptModule::Property::Type::ANY: ASSERT(false); break;
		}
		m_editor.executeCommand(cmd.move());
	}

	void propertyInput(lua_State* L, LuaScriptSystem& system, EntityRef e, u32 script_idx, const char* name, const LuaScriptModule::Property& property, i32 array_index) {
		UniquePtr<IEditorCommand> cmd;
		IAllocator& allocator = m_editor.getAllocator();
		switch (property.type) {
			case LuaScriptModule::Property::ANY: ASSERT(false); break;
			case LuaScriptModule::Property::RESOURCE: {
				lua_getfield(L, -1, "_handle");
				i32 res_idx = lua_tointeger(L, -1);
				lua_pop(L, 1);
				Resource* res = system.getLuaResource(res_idx);
				Path path = res ? res->getPath() : Path();
				if (m_app.getAssetBrowser().resourceInput("##v", path, property.resource_type)) {
					const i32 prev_res_idx = res_idx;
					cmd = UniquePtr<SetLuaPropertyCommand<Path>>::create(allocator, system, m_editor, e, script_idx, name, path, array_index, property.resource_type);
					system.unloadLuaResource(prev_res_idx);
				}
				break;
			}						
			case LuaScriptModule::Property::BOOLEAN: {
				bool value = lua_toboolean(L, -1) != 0;
				if (ImGui::Checkbox("##v", &value)) {
					cmd = UniquePtr<SetLuaPropertyCommand<bool>>::create(allocator, system, m_editor, e, script_idx, name, value, array_index);
				}
				break;
			}
			case LuaScriptModule::Property::COLOR: {
				Vec3 color = LuaWrapper::toType<Vec3>(L, -1);
				if (ImGui::ColorEdit3("##v", (float*)&color)) {
					cmd = UniquePtr<SetLuaPropertyCommand<Vec3>>::create(allocator, system, m_editor, e, script_idx, name, color, array_index);
				}
				break;
			}
			case LuaScriptModule::Property::INT: {
				i32 value = lua_tointeger(L, -1);
				if (ImGui::DragInt("##v", &value)) {
					cmd = UniquePtr<SetLuaPropertyCommand<i32>>::create(allocator, system, m_editor, e, script_idx, name, value, array_index);
				}
				break;
			}
			case LuaScriptModule::Property::FLOAT: {
				float value = (float)lua_tonumber(L, -1);
				if (ImGui::DragFloat("##v", &value)) {
					cmd = UniquePtr<SetLuaPropertyCommand<float>>::create(allocator, system, m_editor, e, script_idx, name, value, array_index);
				}
				break;
			}
			case LuaScriptModule::Property::ENTITY: {
				lua_getfield(L, -1, "_entity");

				EntityPtr value = EntityPtr{lua_tointeger(L, -1)};
				lua_pop(L, 1);
				if (m_app.getPropertyGrid().entityInput(name, &	value)) {
					cmd = UniquePtr<SetLuaPropertyCommand<EntityPtr>>::create(allocator, system, m_editor, e, script_idx, name, value, array_index);
				}
				break;
			}
			case LuaScriptModule::Property::STRING: 
				const char* value = lua_tostring(L, -1);
				char buf[256]; // TODO
				copyString(buf, value);
				if (ImGui::InputText("##v", buf, sizeof(buf))) {
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						cmd = UniquePtr<SetLuaPropertyCommand<const char*>>::create(allocator, system, m_editor, e, script_idx, name, buf, array_index);
					}
				}
				break;
		}
		if (cmd.get()) m_editor.executeCommand(cmd.move());
	};

	void blobGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, u32 script_idx, const TextFilter& filter, WorldEditor& editor) override {
		if (cmp_type != LUA_SCRIPT_TYPE) return;
		if (entities.length() != 1) return;

		LuaScriptModule* module = (LuaScriptModule*)editor.getWorld()->getModule(cmp_type); 
		const EntityRef e = entities[0];
		const u32 count = module->getScriptCount(e);
		if (!filter.isActive()) {
			if (module->beginFunctionCall(e, script_idx, "onGUI")) {
				module->endFunctionCall();
			}
		}

		lua_State* L = module->getState(e, script_idx);
		LuaWrapper::DebugGuard guard(L);
		const i32 env = module->getEnvironment(e, script_idx);
		const i32 num_props = module->getPropertyCount(e, script_idx);
		lua_rawgeti(L, LUA_REGISTRYINDEX, env);
		
		for (i32 prop_idx = 0; prop_idx < num_props; ++prop_idx) {
			const char* name = module->getPropertyName(e, script_idx, prop_idx);
			if (!filter.pass(name)) continue;

			const LuaScriptModule::Property& property = module->getProperty(e, script_idx, prop_idx);
			LuaScriptSystem& system = ((LuaScriptSystem&)module->getSystem());

			ImGui::PushID(name);
			ImGuiEx::Label(name);
			
			lua_pushstring(L, name);
			lua_gettable(L, -2);
			if (property.is_array) {
				ImGui::BeginGroup();
				const i32 num_elements = lua_objlen(L, -1);
				ImGui::PushItemWidth(-1);
				
				for (i32 i = 0; i < num_elements; ++i) {
					ImGui::PushID(i);

					if (ImGui::Button(ICON_FA_TIMES)) {
						removeElement(system, e, script_idx, name, property, i);
						ImGui::PopID();
						break;
					}
					
					lua_rawgeti(L, -1, i + 1);
					ImGui::SameLine();
					propertyInput(L, system, e, script_idx, name, property, i);
					lua_pop(L, 1);

					ImGui::PopID();
				}
				if (ImGui::Button(ICON_FA_PLUS, ImVec2(-1, 0))) {
					addElement(system, e, script_idx, name, property, num_elements);
				}
				ImGui::PopItemWidth();
				ImGui::EndGroup();
			}
			else {
				propertyInput(L, system, e, script_idx, name, property, -1);
			}
			lua_pop(L, 1);

			ImGui::PopID();
		}
		lua_pop(L, 1);
	}

	StudioApp& m_app;
	WorldEditor& m_editor;
};

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

struct LuaAction {
	void run() {
		LuaWrapper::DebugGuard guard(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, ref_action);
		lua_getfield(L, -1, "run");
		LuaWrapper::pcall(L, 0, 0);
		lua_pop(L, 1);
	}
	Local<Action> action;
	lua_State* L;
	int ref_thread;
	int ref_action;
};



struct StudioAppPlugin : StudioApp::IPlugin {
	StudioAppPlugin(StudioApp& app)
		: m_app(app)
		, m_luau_analysis(app)
		, m_asset_plugin(m_luau_analysis, app)
		, m_lua_actions(app.getAllocator())
		, m_plugins(app.getAllocator())
		, m_property_grid_plugin(app)
	{
		LuaScriptSystem* system = (LuaScriptSystem*)app.getEngine().getSystemManager().getSystem("lua_script");
		lua_State* L = system->getState();
		LuaWrapper::createSystemClosure(L, "Editor", this, "addAction", &LUA_addAction);
		initPlugins();
	}

	void update(float) override {
		for (LuaAction* action : m_lua_actions) {
			if (m_app.checkShortcut(*action->action, true)) action->run();
		}
	}

	static int LUA_addAction(lua_State* L) {
		LuaWrapper::DebugGuard guard(L);
		StudioAppPlugin* plugin = LuaWrapper::getClosureObject<StudioAppPlugin>(L);
		StudioApp& app = plugin->m_app;
		LuaWrapper::checkTableArg(L, 1);
		char name[64];
		char label[128];
		if (!LuaWrapper::checkStringField(L, 1, "name", Span(name))) luaL_argerror(L, 1, "missing name");
		if (!LuaWrapper::checkStringField(L, 1, "label", Span(label))) luaL_argerror(L, 1, "missing label");

		// TODO leak
		LuaAction* action = LUMIX_NEW(app.getAllocator(), LuaAction);
		plugin->m_lua_actions.push(action);

		lua_pushthread(L);
		action->ref_thread = LuaWrapper::createRef(L);
		lua_pushvalue(L, 1);
		action->ref_action = LuaWrapper::createRef(L);
		lua_pop(L, 2);
		action->action.create("Lua actions", label, label, name, "");
		action->L = L;
		return 0;
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
			StudioLuaPlugin* plugin = StudioLuaPlugin::create(m_app, content, path);
			if (plugin) m_plugins.push(plugin);
		}
		os::destroyFileIterator(iter);
	}

	const char* getName() const override { return "lua_script"; }


	static int LUA_getSelectedEntity(lua_State* L) {
		LuaWrapper::DebugGuard guard(L, 1);
		i32 entity_idx = LuaWrapper::checkArg<i32>(L, 1);
		
		StudioApp* inst = LuaWrapper::getClosureObject<StudioApp>(L);
		WorldEditor& editor = inst->getWorldEditor();
		EntityRef entity = editor.getSelectedEntities()[entity_idx];

		lua_getglobal(L, "Lumix");
		lua_getfield(L, -1, "Entity");
		lua_remove(L, -2);
		lua_getfield(L, -1, "new");
		lua_pushvalue(L, -2); // [Lumix.Entity, Entity.new, Lumix.Entity]
		lua_remove(L, -3); // [Entity.new, Lumix.Entity]
		World* world = editor.getWorld();
		LuaWrapper::push(L, world); // [Entity.new, Lumix.Entity, world]
		LuaWrapper::push(L, entity.index); // [Entity.new, Lumix.Entity, world, entity_index]
		const bool error = !LuaWrapper::pcall(L, 3, 1); // [entity]
		return error ? 0 : 1;
	}

	static int LUA_getResources(lua_State* L) {
		auto* studio = LuaWrapper::checkArg<StudioApp*>(L, 1);
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

	static void LUA_makeParent(lua_State* L, EntityPtr parent, EntityRef child) {
		StudioApp* studio = LuaWrapper::getClosureObject<StudioApp>(L);
		studio->getWorldEditor().makeParent(parent, child);
	}

struct SetPropertyVisitor : reflection::IPropertyVisitor {
		static bool isSameProperty(const char* name, const char* lua_name) {
			char tmp[128];
			LuaWrapper::convertPropertyToLuaName(name, Span(tmp));
			return equalStrings(tmp, lua_name);
		}

		void visit(const reflection::Property<int>& prop) override
		{
			if (!isSameProperty(prop.name, property_name)) return;
			if (!lua_isnumber(L, -1)) return;

			if(reflection::getAttribute(prop, reflection::IAttribute::ENUM)) {
				notSupported(prop);
			}

			int val = (int)lua_tointeger(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), val);
		}

		void visit(const reflection::Property<u32>& prop) override
		{
			if (!isSameProperty(prop.name, property_name)) return;
			if (!lua_isnumber(L, -1)) return;

			const u32 val = (u32)lua_tointeger(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), val);
		}

		void visit(const reflection::Property<float>& prop) override
		{
			if (!isSameProperty(prop.name, property_name)) return;
			if (!lua_isnumber(L, -1)) return;

			float val = (float)lua_tonumber(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), val);
		}

		void visit(const reflection::Property<Vec2>& prop) override
		{
			if (!isSameProperty(prop.name, property_name)) return;
			if (!LuaWrapper::isType<Vec2>(L, -1)) return;

			const Vec2 val = LuaWrapper::toType<Vec2>(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), val);
		}

		void visit(const reflection::Property<Vec3>& prop) override
		{
			if (!isSameProperty(prop.name, property_name)) return;
			if (!LuaWrapper::isType<Vec3>(L, -1)) return;

			const Vec3 val = LuaWrapper::toType<Vec3>(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), val);
		}

		void visit(const reflection::Property<IVec3>& prop) override
		{
			if (!isSameProperty(prop.name, property_name)) return;
			if (!LuaWrapper::isType<IVec3>(L, -1)) return;

			const IVec3 val = LuaWrapper::toType<IVec3>(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), val);
		}

		void visit(const reflection::Property<Vec4>& prop) override
		{
			if (!isSameProperty(prop.name, property_name)) return;
			if (!LuaWrapper::isType<Vec4>(L, -1)) return;

			const Vec4 val = LuaWrapper::toType<Vec4>(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), val);
		}
		
		void visit(const reflection::Property<const char*>& prop) override
		{
			if (!isSameProperty(prop.name, property_name)) return;
			if (!lua_isstring(L, -1)) return;

			const char* str = lua_tostring(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), str);
		}


		void visit(const reflection::Property<Path>& prop) override
		{
			if (!isSameProperty(prop.name, property_name)) return;
			if (!lua_isstring(L, -1)) return;

			const char* str = lua_tostring(L, -1);
			editor->setProperty(cmp_type, "", 0, prop.name, Span(&entity, 1), Path(str));
		}


		void visit(const reflection::Property<bool>& prop) override
		{
			if (!isSameProperty(prop.name, property_name)) return;
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

	static int LUA_createEntityEx(lua_State* L) {
		StudioApp* studio = LuaWrapper::getClosureObject<StudioApp>(L);
		LuaWrapper::checkTableArg(L, 1);

		WorldEditor& editor = studio->getWorldEditor();
		editor.beginCommandGroup("createEntityEx");
		EntityRef e = editor.addEntityAt(DVec3(0, 0, 0));
		editor.selectEntities(Span(&e, 1), false);

		lua_pushvalue(L, 1);
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

				IModule* module = editor.getWorld()->getModule(cmp_type);
				if (module)
				{
					ComponentUID cmp(e, cmp_type, module);
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
		LuaWrapper::pushEntity(L, e, editor.getWorld());
		return 1;
	}
	
	void checkScriptCommandLine() {
		char command_line[1024];
		os::getCommandLine(Span(command_line));
		CommandLineParser parser(command_line);
		while (parser.next()) {
			if (parser.currentEquals("-run_script")) {
				if (!parser.next()) break;

				char tmp[MAX_PATH];
				parser.getCurrent(tmp, lengthOf(tmp));
				OutputMemoryStream content(m_app.getAllocator());
				
				if (m_app.getEngine().getFileSystem().getContentSync(Path(tmp), content)) {
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

	static void luaImGuiTable(const char* prefix, lua_State* L) {
		lua_pushnil(L);
		while (lua_next(L, -2)) {
			const char* name = lua_tostring(L, -2);
			if (!lua_isfunction(L, -1) && !equalStrings(name, "__index")) {
				if (lua_istable(L, -1)) {
					luaImGuiTable(StaticString<128>(prefix, name, "."), L);
				}
				else {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text("%s%s", prefix, name);
					ImGui::TableNextColumn();
					switch (lua_type(L, -1)) {
						case LUA_TLIGHTUSERDATA:
							ImGui::TextUnformatted("light user data");
							break;
						case LUA_TBOOLEAN: {
							const bool b = lua_toboolean(L, -1) != 0;
							ImGui::TextUnformatted(b ? "true" : "false");
							break;
						}
						case LUA_TNUMBER: {
							const double val = lua_tonumber(L, -1);
							ImGui::Text("%f", val);
							break;
						}
						case LUA_TSTRING: 
							ImGui::TextUnformatted(lua_tostring(L, -1));
							break;
					}
				}
			}
			lua_pop(L, 1);
		}
	}

	// asserts once if called between ImGui::Begin/End, can be safely skipped
	void luaDebugLoop(lua_State* L, const char* error_msg) {
		if (!m_lua_debug_enabled) return;
		// TODO custom imgui context?
		// TODO can we somehow keep running normal loop while lua is being debugged?
		// end normal loop
		ImGui::PopFont();
		ImGui::Render();
		ImGui::UpdatePlatformWindows();
		m_app.beginCustomTicking();

		// debug loop
		// we have special loop for debug, because we don't want world or lua state to change while we debug
		bool finished = false;
		while (!finished) {
			m_app.beginCustomTick();

			const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

			static int selected_stack_level = -1;

			ImGui::SetNextWindowDockID(dockspace_id, ImGuiCond_FirstUseEver);			
			if (ImGui::Begin("REPL")) {
				LuaWrapper::DebugGuard guard(L);
				static char repl[4096] = "";
				ImGui::SetNextItemWidth(-1);
				ImGui::InputTextMultiline("##repl", repl, sizeof(repl));
				if (ImGui::Button("Run")) {
					const bool errors = LuaWrapper::luaL_loadbuffer(L, repl, strlen(repl), "REPL");
					if (!errors) {
						if (selected_stack_level >= 0) {
							lua_Debug ar;
							if (0 != lua_getinfo(L, selected_stack_level + 1, "f", &ar)) {
								lua_getfenv(L, -1);
								lua_setfenv(L, -3);
								lua_pop(L, 1);
							}
						}
						if (::lua_pcall(L, 0, 0, 0) != 0) {
							const char* msg = lua_tostring(L, -1); 
							ASSERT(false); // TODO
						}
					}
				}
			}
			ImGui::End();

			ImGui::SetNextWindowDockID(dockspace_id, ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Callstack")) {
				LuaWrapper::DebugGuard guard(L);
				lua_Debug ar;
				for (u32 stack_level = 1/*skip traceback fn*/; ;++stack_level) {
					if (0 == lua_getinfo(L, stack_level + 1, "nsl", &ar)) break;
					const bool selected = selected_stack_level == stack_level;
					const StaticString<MAX_PATH + 128> label(ar.source, ": ", ar.name, " Line ", ar.currentline);
					if (ImGui::Selectable(label, selected)) selected_stack_level = stack_level;
				}
			}
			ImGui::End();
			
			ImGui::SetNextWindowDockID(dockspace_id, ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Locals") && selected_stack_level >= 0) {
				if (ImGui::BeginTable("locals", 2, ImGuiTableFlags_Resizable)) {
					lua_Debug ar;
					if (0 != lua_getinfo(L, selected_stack_level + 1, "nslf", &ar)) {
						lua_getfenv(L, -1);

						luaImGuiTable("", L);

						lua_pop(L, 2);
					}
					ImGui::EndTable();
				}
			}
			ImGui::End();

			ImGui::SetNextWindowDockID(dockspace_id, ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Lua debugger")) {
				ImGui::TextUnformatted(error_msg);
				ImGui::Checkbox("Enable debugger", &m_lua_debug_enabled);
				ImGui::SameLine();
				if (ImGui::Button("Resume")) finished = true;
			}
			ImGui::End();
		
			ImGui::PopFont();
			ImGui::Render();
			ImGui::UpdatePlatformWindows();

			m_app.endCustomTick();
		}

		m_app.endCustomTicking();
	}

	static int LUA_debugCallback(lua_State* L) {
		const char* error_msg = lua_tostring(L, 1);
		if (lua_getglobal(L, "Editor") != LUA_TTABLE) {
			lua_pop(L, 1);
			return 0;
		}
		if (lua_getfield(L, -1, "editor") != LUA_TLIGHTUSERDATA) {
			lua_pop(L, 2);
			return 0;
		}
		StudioApp* app = (StudioApp*)lua_tolightuserdata(L, -1);
		lua_pop(L, 2);
		StudioAppPlugin* plugin = (StudioAppPlugin*)app->getIPlugin("lua_script");
		plugin->luaDebugLoop(L, error_msg);
		return 0;
	}

	i32 getSelectedEntitiesCount() { return m_app.getWorldEditor().getSelectedEntities().size(); }
	EntityRef getSelectedEntity(u32 idx) { return m_app.getWorldEditor().getSelectedEntities()[idx]; }
	EntityRef createEntity() { return m_app.getWorldEditor().addEntity(); }
	void createComponent(EntityRef e, const char* type) {
		const ComponentType cmp_type = reflection::getComponentType(type);
		m_app.getWorldEditor().addComponent(Span(&e, 1), cmp_type);
	}

	void init() override
	{
		AddComponentPlugin* add_cmp_plugin = LUMIX_NEW(m_app.getAllocator(), AddComponentPlugin)(m_app);
		m_app.registerComponent(ICON_FA_MOON, "lua_script", *add_cmp_plugin);

		const char* exts[] = { "lua" };
		m_app.getAssetCompiler().addPlugin(m_asset_plugin, Span(exts));
		m_app.getAssetBrowser().addPlugin(m_asset_plugin, Span(exts));
		m_app.getPropertyGrid().addPlugin(m_property_grid_plugin);

		// lua API
		// TODO cleanup
		LuaScriptSystem* system = (LuaScriptSystem*)m_app.getEngine().getSystemManager().getSystem("lua_script");
		lua_State* L = system->getState();

		{
			StudioApp::GUIPlugin* game_view = m_app.getGUIPlugin("game_view");
			auto f = &LuaWrapper::wrapMethodClosure<&GameView::forceViewport>;
			LuaWrapper::createSystemClosure(L, "GameView", game_view, "forceViewport", f);
		}
		
		lua_getglobal(L, "Editor");
		StudioApp::GUIPlugin* scene_view = m_app.getGUIPlugin("scene_view");
		LuaWrapper::pushObject(L, scene_view, "SceneView");
		lua_setfield(L, -2, "scene_view");

		LuaWrapper::pushObject(L, &m_app.getAssetBrowser(), "AssetBrowser");
		lua_setfield(L, -2, "asset_browser");
		lua_pop(L, 1);

		LuaWrapper::createSystemVariable(L, "Editor", "editor", &m_app);
		
		lua_pushcfunction(L, &LUA_debugCallback, "LumixDebugCallback");
		lua_setglobal(L, "LumixDebugCallback");

		#define REGISTER_FUNCTION(F)                                    \
		do {                                                            \
			auto f = &LuaWrapper::wrapMethodClosure<&StudioApp::F>; \
			LuaWrapper::createSystemClosure(L, "Editor", this, #F, f);  \
		} while (false)

		REGISTER_FUNCTION(exitGameMode);
		REGISTER_FUNCTION(exitWithCode);
		REGISTER_FUNCTION(newWorld);

		#undef REGISTER_FUNCTION

		#define REGISTER_FUNCTION(F)                                    \
		do {                                                            \
			auto f = &LuaWrapper::wrapMethodClosure<&StudioAppPlugin::F>; \
			LuaWrapper::createSystemClosure(L, "Editor", this, #F, f);  \
		} while (false)
		REGISTER_FUNCTION(createEntity);
		REGISTER_FUNCTION(createComponent);
		REGISTER_FUNCTION(getSelectedEntitiesCount);
		REGISTER_FUNCTION(getSelectedEntity);
		#undef REGISTER_FUNCTION

		LuaWrapper::createSystemClosure(L, "Editor", &m_app, "getSelectedEntity", &LUA_getSelectedEntity);
		LuaWrapper::createSystemFunction(L, "Editor", "getResources", &LUA_getResources);
		LuaWrapper::createSystemClosure(L, "Editor", &m_app, "createEntityEx", &LUA_createEntityEx);
		LuaWrapper::createSystemClosure(L, "Editor", &m_app, "makeParent", &LuaWrapper::wrap<LUA_makeParent>);

		checkScriptCommandLine();
	}

	void runScript(const char* src, const char* script_name) {
		LuaScriptSystem* system = (LuaScriptSystem*)m_app.getEngine().getSystemManager().getSystem("lua_script");
		lua_State* L = system->getState();
		bool errors = LuaWrapper::luaL_loadbuffer(L, src, stringLength(src), script_name) != 0;
		errors = errors || lua_pcall(L, 0, 0, 0) != 0;
		if (errors)
		{
			logError(script_name, ": ", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}

	~StudioAppPlugin()
	{
		m_app.getAssetCompiler().removePlugin(m_asset_plugin);
		m_app.getAssetBrowser().removePlugin(m_asset_plugin);
		m_app.getPropertyGrid().removePlugin(m_property_grid_plugin);

		for (StudioLuaPlugin* plugin : m_plugins) {
			m_app.removePlugin(*plugin);
			LUMIX_DELETE(m_app.getAllocator(), plugin);
		}

		for (LuaAction* action : m_lua_actions) {
			LUMIX_DELETE(m_app.getAllocator(), action);
		}
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
	Array<LuaAction*> m_lua_actions;
	Array<StudioLuaPlugin*> m_plugins;
	bool m_lua_debug_enabled = true;
};


} // anonymous namespace


LUMIX_STUDIO_ENTRY(lua) {
	PROFILE_FUNCTION();
	IAllocator& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}


