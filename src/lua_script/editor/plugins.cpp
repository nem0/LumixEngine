#include <imgui/imgui.h>

#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/property_grid.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/array.h"
#include "engine/crc32.h"
#include "engine/crt.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/allocator.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/reflection.h"
#include "engine/stream.h"
#include "engine/universe.h"
#include "lua_script/lua_script.h"
#include "lua_script/lua_script_system.h"


using namespace Lumix;


static const ComponentType LUA_SCRIPT_TYPE = Reflection::getComponentType("lua_script");


namespace
{


struct AssetPlugin : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	explicit AssetPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("lua", LuaScript::TYPE);
		m_text_buffer[0] = 0;
	}


	bool compile(const Path& src) override
	{
		return m_app.getAssetCompiler().copyCompile(src);
	}

	
	void onGUI(Span<Resource*> resources) override
	{
		if (resources.length() > 1) return;

		auto* script = static_cast<LuaScript*>(resources[0]);

		if (m_text_buffer[0] == '\0')
		{
			m_too_long = !copyString(m_text_buffer, script->getSourceCode());
		}
		ImGui::SetNextItemWidth(-1);
		if (!m_too_long) {
			ImGui::InputTextMultiline("##code", m_text_buffer, sizeof(m_text_buffer), ImVec2(0, 300));
			if (ImGui::Button(ICON_FA_SAVE "Save"))
			{
				FileSystem& fs = m_app.getEngine().getFileSystem();
				OS::OutputFile file;
				if (!fs.open(script->getPath().c_str(), Ref(file)))
				{
					logWarning("Could not save ", script->getPath());
					return;
				}

				if (!file.write(m_text_buffer, stringLength(m_text_buffer))) {
					logError("Could not write ", script->getPath());
				}
				file.close();
			}
			ImGui::SameLine();
		}
		else {
			ImGui::Text(ICON_FA_EXCLAMATION_TRIANGLE "File is too big to be edited here, please use external editor");
		}
		if (ImGui::Button(ICON_FA_EXTERNAL_LINK_ALT "Open externally"))
		{
			m_app.getAssetBrowser().openInExternalEditor(script);
		}
	}


	void onResourceUnloaded(Resource*) override { m_text_buffer[0] = 0; }
	const char* getName() const override { return "Lua Script"; }


	ResourceType getResourceType() const override { return LuaScript::TYPE; }


	bool createTile(const char* in_path, const char* out_path, ResourceType type) override
	{
		if (type == LuaScript::TYPE)
		{
			FileSystem& fs = m_app.getEngine().getFileSystem();
			return fs.copyFile("models/editor/tile_lua_script.dds", out_path);
		}
		return false;
	}


	StudioApp& m_app;
	char m_text_buffer[8192];
	bool m_too_long = false;
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
		open = app.getSettings().getValue("is_script_console_open", false);
	}

	void onBeforeSettingsSaved() override {
		app.getSettings().setValue("is_script_console_open", open);
	}

	/*static const int LUA_CALL_EVENT_SIZE = 32;

	void pluginAdded(GUIPlugin& plugin) override
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
		LuaScriptScene* scene = (LuaScriptScene*)app.getWorldEditor().getUniverse()->getScene(crc32("lua_script"));
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
			copyNString(Span(tmp), data->Buf + start_word, data->CursorPos - start_word);

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
					const char* a_str = ((const String*)a)->c_str();
					const char* b_str = ((const String*)b)->c_str();
					return compareString(a_str, b_str);
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


	void onWindowGUI() override
	{
		if (!open) return;
		if (ImGui::Begin(ICON_FA_SCROLL "Lua console##lua_console", &open))
		{
			if (ImGui::Button("Execute"))
			{
				lua_State* L = app.getEngine().getState();
				
				bool errors = luaL_loadbuffer(L, buf, stringLength(buf), nullptr) != 0;
				errors = errors || lua_pcall(L, 0, 0, 0) != 0;

				if (errors)
				{
					logError(lua_tostring(L, -1));
					lua_pop(L, 1);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Execute file"))
			{
				char tmp[MAX_PATH_LENGTH] = {};
				if (OS::getOpenFilename(Span(tmp), "Scripts\0*.lua\0", nullptr))
				{
					OS::InputFile file;
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
			if(insert_value) ImGui::SetKeyboardFocusHere();
			ImGui::InputTextMultiline("##repl",
				buf,
				lengthOf(buf),
				ImVec2(-1, -1),
				ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackCompletion,
				autocompleteCallback,
				this);

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
				if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow))) ++autocomplete_selected;
				if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow))) --autocomplete_selected;
				if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter))) insert_value = autocomplete[autocomplete_selected].c_str();
				if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape))) ImGui::CloseCurrentPopup();
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
	int autocomplete_selected = 1;
	const char* insert_value = nullptr;
	char buf[10 * 1024];
};


struct AddComponentPlugin final : StudioApp::IAddComponentPlugin
{
	explicit AddComponentPlugin(StudioApp& _app)
		: app(_app)
	{
	}


	void onGUI(bool create_entity, bool) override
	{
		ImGui::SetNextWindowSize(ImVec2(300, 300));
		if (!ImGui::BeginMenu(getLabel())) return;
		char buf[MAX_PATH_LENGTH];
		AssetBrowser& asset_browser = app.getAssetBrowser();
		bool new_created = false;
		if (ImGui::Selectable("New"))
		{
			char full_path[MAX_PATH_LENGTH];
			if (OS::getSaveFilename(Span(full_path), "Lua script\0*.lua\0", "lua"))
			{
				OS::OutputFile file;
				FileSystem& fs = app.getEngine().getFileSystem();
				if(fs.makeRelative(Span(buf), full_path)) {
					if (file.open(full_path))
					{
						new_created = true;
						file.close();
					}
					else
					{
						logError("Failed to create ", buf);
					}
				}
				else {
					logError("Can not create ", full_path, " because it's not in root directory.");
				}
			}
		}
		bool create_empty = ImGui::Selectable("Empty", false);

		static u32 selected_res_hash = 0;
		if (asset_browser.resourceList(Span(buf), Ref(selected_res_hash), LuaScript::TYPE, 0, false) || create_empty || new_created)
		{
			WorldEditor& editor = app.getWorldEditor();
			if (create_entity)
			{
				EntityRef entity = editor.addEntity();
				editor.selectEntities(Span(&entity, 1), false);
			}
			if (editor.getSelectedEntities().empty()) return;
			EntityRef entity = editor.getSelectedEntities()[0];

			if (!editor.getUniverse()->hasComponent(entity, LUA_SCRIPT_TYPE))
			{
				editor.addComponent(Span(&entity, 1), LUA_SCRIPT_TYPE);
			}

			const ComponentUID cmp = editor.getUniverse()->getComponent(entity, LUA_SCRIPT_TYPE);
			editor.beginCommandGroup(crc32("add_lua_script"));
			editor.addArrayPropertyItem(cmp, "scripts");

			if (!create_empty) {
				auto* script_scene = static_cast<LuaScriptScene*>(editor.getUniverse()->getScene(LUA_SCRIPT_TYPE));
				int scr_count = script_scene->getScriptCount(entity);
				editor.setProperty(cmp.type, "scripts", scr_count - 1, "Path", Span((const EntityRef*)&entity, 1), Path(buf));
			}
			editor.endCommandGroup();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndMenu();
	}


	const char* getLabel() const override 
	{
		return "Lua Script";
	}


	StudioApp& app;
};

struct PropertyGridPlugin final : PropertyGrid::IPlugin
{
	void onGUI(PropertyGrid& grid, ComponentUID cmp) override {
		if (cmp.type != LUA_SCRIPT_TYPE) return;

		LuaScriptScene* scene = (LuaScriptScene*)cmp.scene; 
		const EntityRef e = (EntityRef)cmp.entity;
		const u32 count = scene->getScriptCount(e);
		for (u32 i = 0; i < count; ++i) {
			if (scene->beginFunctionCall(e, i, "onGUI")) {
				scene->endFunctionCall();
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
		IAllocator& allocator = m_app.getAllocator();

		AddComponentPlugin* add_cmp_plugin = LUMIX_NEW(m_app.getAllocator(), AddComponentPlugin)(m_app);
		m_app.registerComponent(ICON_FA_MOON, "lua_script", *add_cmp_plugin);

		const char* exts[] = { "lua", nullptr };
		m_app.getAssetCompiler().addPlugin(m_asset_plugin, exts);
		m_app.getAssetBrowser().addPlugin(m_asset_plugin);
		m_app.addPlugin(m_console_plugin);
		m_app.getPropertyGrid().addPlugin(m_property_grid_plugin);
	}

	~StudioAppPlugin()
	{
		IAllocator& allocator = m_app.getAllocator();
		
		m_app.getAssetCompiler().removePlugin(m_asset_plugin);
		m_app.getAssetBrowser().removePlugin(m_asset_plugin);
		m_app.removePlugin(m_console_plugin);
		m_app.getPropertyGrid().removePlugin(m_property_grid_plugin);
	}

	bool showGizmo(UniverseView& view, ComponentUID cmp) override
	{
		if (cmp.type == LUA_SCRIPT_TYPE)
		{
			auto* scene = static_cast<LuaScriptScene*>(cmp.scene);
			int count = scene->getScriptCount((EntityRef)cmp.entity);
			for (int i = 0; i < count; ++i)
			{
				if (scene->beginFunctionCall((EntityRef)cmp.entity, i, "onDrawGizmo"))
				{
					scene->endFunctionCall();
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


LUMIX_STUDIO_ENTRY(lua_script)
{
	IAllocator& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}


