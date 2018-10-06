#include "animation/editor/animation_editor.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/ieditor_command.h"
#include "editor/platform_interface.h"
#include "editor/property_grid.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/array.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/fs/file_system.h"
#include "engine/fs/os_file.h"
#include "engine/iallocator.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/path_utils.h"
#include "engine/reflection.h"
#include "engine/system.h"
#include "engine/universe/universe.h"
#include "imgui/imgui.h"
#include "lua_script/lua_script_manager.h"
#include "lua_script/lua_script_system.h"
#include <cstdlib>


using namespace Lumix;


static const ComponentType LUA_SCRIPT_TYPE = Reflection::getComponentType("lua_script");


namespace
{


struct PropertyGridPlugin final : public PropertyGrid::IPlugin
{
	struct AddLuaScriptCommand final : public IEditorCommand
	{
		explicit AddLuaScriptCommand(WorldEditor& _editor)
			: editor(_editor)
		{
		}


		bool execute() override
		{
			auto* scene = static_cast<LuaScriptScene*>(editor.getUniverse()->getScene(crc32("lua_script")));
			if (entity.isValid()) scr_index = scene->addScript((EntityRef)entity);
			return true;
		}


		void undo() override
		{
			auto* scene = static_cast<LuaScriptScene*>(editor.getUniverse()->getScene(crc32("lua_script")));
			if (entity.isValid()) scene->removeScript((EntityRef)entity, scr_index);
		}


		void serialize(JsonSerializer& serializer) override { serializer.serialize("entity", entity); }


		void deserialize(JsonDeserializer& serializer) override
		{
			serializer.deserialize("entity", entity, INVALID_ENTITY);
		}


		const char* getType() override { return "add_script"; }


		bool merge(IEditorCommand& command) override { return false; }


		WorldEditor& editor;
		EntityPtr entity;
		int scr_index;
	};


	struct MoveScriptCommand final : public IEditorCommand
	{
		explicit MoveScriptCommand(WorldEditor& editor)
			: blob(editor.getAllocator())
			, scr_index(-1)
			, entity(INVALID_ENTITY)
			, up(true)
		{
			scene = static_cast<LuaScriptScene*>(editor.getUniverse()->getScene(crc32("lua_script")));
		}


		explicit MoveScriptCommand(IAllocator& allocator)
			: blob(allocator)
			, scene(nullptr)
			, scr_index(-1)
			, entity(INVALID_ENTITY)
			, up(true)
		{
		}


		bool execute() override
		{
			if (entity.isValid()) scene->moveScript((EntityRef)entity, scr_index, up);
			return true;
		}


		void undo() override
		{
			if (entity.isValid()) scene->moveScript((EntityRef)entity, up ? scr_index - 1 : scr_index + 1, !up);
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("entity", entity);
			serializer.serialize("scr_index", scr_index);
			serializer.serialize("up", up);
		}


		void deserialize(JsonDeserializer& serializer) override
		{
			serializer.deserialize("entity", entity, INVALID_ENTITY);
			serializer.deserialize("scr_index", scr_index, 0);
			serializer.deserialize("up", up, false);
		}


		const char* getType() override { return "move_script"; }


		bool merge(IEditorCommand& command) override { return false; }


		OutputBlob blob;
		LuaScriptScene* scene;
		EntityPtr entity;
		int scr_index;
		bool up;
	};


	struct RemoveScriptCommand final : public IEditorCommand
	{
		explicit RemoveScriptCommand(WorldEditor& editor)
			: blob(editor.getAllocator())
			, scr_index(-1)
			, entity(INVALID_ENTITY)
		{
			scene = static_cast<LuaScriptScene*>(editor.getUniverse()->getScene(crc32("lua_script")));
		}


		explicit RemoveScriptCommand(IAllocator& allocator)
			: blob(allocator)
			, scene(nullptr)
			, scr_index(-1)
			, entity(INVALID_ENTITY)
		{
		}


		bool execute() override
		{
			if (entity.isValid()) {
				scene->serializeScript((EntityRef)entity, scr_index, blob);
				scene->removeScript((EntityRef)entity, scr_index);
			}
			return true;
		}


		void undo() override
		{
			if (entity.isValid()) {
				scene->insertScript((EntityRef)entity, scr_index);
				InputBlob input(blob);
				scene->deserializeScript((EntityRef)entity, scr_index, input);
			}
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("entity", entity);
			serializer.serialize("scr_index", scr_index);
		}


		void deserialize(JsonDeserializer& serializer) override
		{
			serializer.deserialize("entity", entity, INVALID_ENTITY);
			serializer.deserialize("scr_index", scr_index, 0);
		}


		const char* getType() override { return "remove_script"; }


		bool merge(IEditorCommand& command) override { return false; }

		OutputBlob blob;
		LuaScriptScene* scene;
		EntityPtr entity;
		int scr_index;
	};


	struct SetPropertyCommand final : public IEditorCommand
	{
		explicit SetPropertyCommand(WorldEditor& _editor)
			: property_name(_editor.getAllocator())
			, value(_editor.getAllocator())
			, old_value(_editor.getAllocator())
			, editor(_editor)
		{
		}


		SetPropertyCommand(WorldEditor& _editor,
			EntityRef entity,
			int scr_index,
			const char* property_name,
			const char* val,
			IAllocator& allocator)
			: property_name(property_name, allocator)
			, value(val, allocator)
			, old_value(allocator)
			, entity(entity)
			, script_index(scr_index)
			, editor(_editor)
		{
			auto* scene = static_cast<LuaScriptScene*>(editor.getUniverse()->getScene(crc32("lua_script")));
			if (property_name[0] == '-')
			{
				old_value = scene->getScriptPath(entity, script_index).c_str();
			}
			else
			{
				char tmp[1024];
				tmp[0] = '\0';
				scene->getPropertyValue(entity, scr_index, property_name, tmp, lengthOf(tmp));
				old_value = tmp;
				return;
			}
		}


		bool execute() override
		{
			if (!entity.isValid()) return false;
			auto* scene = static_cast<LuaScriptScene*>(editor.getUniverse()->getScene(crc32("lua_script")));
			if (property_name.length() > 0 && property_name[0] == '-')
			{
				scene->setScriptPath((EntityRef)entity, script_index, Path(value.c_str()));
			}
			else
			{
				scene->setPropertyValue((EntityRef)entity, script_index, property_name.c_str(), value.c_str());
			}
			return true;
		}


		void undo() override
		{
			if (!entity.isValid()) return;
			auto* scene = static_cast<LuaScriptScene*>(editor.getUniverse()->getScene(crc32("lua_script")));
			if (property_name.length() > 0 && property_name[0] == '-')
			{
				scene->setScriptPath((EntityRef)entity, script_index, Path(old_value.c_str()));
			}
			else
			{
				scene->setPropertyValue((EntityRef)entity, script_index, property_name.c_str(), old_value.c_str());
			}
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("entity", entity);
			serializer.serialize("script_index", script_index);
			serializer.serialize("property_name", property_name.c_str());
			serializer.serialize("value", value.c_str());
			serializer.serialize("old_value", old_value.c_str());
		}


		void deserialize(JsonDeserializer& serializer) override
		{
			serializer.deserialize("entity", entity, INVALID_ENTITY);
			serializer.deserialize("script_index", script_index, 0);
			char buf[256];
			serializer.deserialize("property_name", buf, lengthOf(buf), "");
			property_name = buf;
			serializer.deserialize("value", buf, lengthOf(buf), "");
			value = buf;
			serializer.deserialize("old_value", buf, lengthOf(buf), "");
			old_value = buf;
		}


		const char* getType() override { return "set_script_property"; }


		bool merge(IEditorCommand& command) override
		{
			auto& cmd = static_cast<SetPropertyCommand&>(command);
			if (cmd.script_index == script_index && cmd.property_name == property_name)
			{
				//cmd.scene = scene;
				cmd.value = value;
				return true;
			}
			return false;
		}


		WorldEditor& editor;
		string property_name;
		string value;
		string old_value;
		EntityPtr entity;
		int script_index;
	};


	explicit PropertyGridPlugin(StudioApp& app)
		: m_app(app)
	{
	}


	struct SortedProperty
	{
		int index;
		const char* name;
	};


	static void getSortedProperties(Array<SortedProperty>& props, LuaScriptScene& scene, EntityRef entity, int script_index)
	{
		int property_count = scene.getPropertyCount(entity, script_index);
		props.resize(property_count);
		
		for (int i = 0; i < property_count; ++i)
		{
			props[i].index = i;
			props[i].name = scene.getPropertyName(entity, script_index, i);
		}

		if (!props.empty())
		{
			qsort(&props[0], props.size(), sizeof(props[0]), [](const void* a, const void* b) -> int {
				auto* pa = (SortedProperty*)a;
				auto* pb = (SortedProperty*)b;

				if (!pa->name) return -1;
				if (!pb->name) return 1;
				return compareString(pa->name, pb->name);
			});
		}
	}


	void onGUI(PropertyGrid& grid, ComponentUID cmp) override
	{
		if (cmp.type != LUA_SCRIPT_TYPE) return;

		const EntityRef entity = (EntityRef)cmp.entity;
		auto* scene = (LuaScriptScene*)cmp.scene;
		WorldEditor& editor = m_app.getWorldEditor();
		IAllocator& allocator = editor.getAllocator();

		if (ImGui::Button("Add script"))
		{
			auto* cmd = LUMIX_NEW(allocator, AddLuaScriptCommand)(editor);
			cmd->entity = cmp.entity;
			editor.executeCommand(cmd);
		}

		for (int j = 0; j < scene->getScriptCount(entity); ++j)
		{
			char buf[MAX_PATH_LENGTH];
			copyString(buf, scene->getScriptPath(entity, j).c_str());
			StaticString<MAX_PATH_LENGTH + 20> header;
			PathUtils::getBasename(header.data, lengthOf(header.data), buf);
			if (header.empty()) header << j;
			ImGui::Unindent();
			bool open = ImGui::TreeNodeEx(StaticString<32>("###", j), ImGuiTreeNodeFlags_AllowItemOverlap);
			bool enabled = scene->isScriptEnabled(entity, j);
			ImGui::SameLine();
			if (ImGui::Checkbox(header, &enabled))
			{
				scene->enableScript(entity, j, enabled);
			}

			if (open)
			{
				if (ImGui::Button("Remove script"))
				{
					auto* cmd = LUMIX_NEW(allocator, RemoveScriptCommand)(allocator);
					cmd->entity = cmp.entity;
					cmd->scr_index = j;
					cmd->scene = scene;
					editor.executeCommand(cmd);
					ImGui::TreePop();
					ImGui::Indent();
					break;
				}
				ImGui::SameLine();
				if (ImGui::Button("Up"))
				{
					auto* cmd = LUMIX_NEW(allocator, MoveScriptCommand)(allocator);
					cmd->entity = cmp.entity;
					cmd->scr_index = j;
					cmd->scene = scene;
					cmd->up = true;
					editor.executeCommand(cmd);
					ImGui::TreePop();
					ImGui::Indent();
					break;
				}
				ImGui::SameLine();
				if (ImGui::Button("Down"))
				{
					auto* cmd = LUMIX_NEW(allocator, MoveScriptCommand)(allocator);
					cmd->entity = cmp.entity;
					cmd->scr_index = j;
					cmd->scene = scene;
					cmd->up = false;
					editor.executeCommand(cmd);
					ImGui::TreePop();
					ImGui::Indent();
					break;
				}

				if (m_app.getAssetBrowser().resourceInput(
						"Source", "src", buf, lengthOf(buf), LuaScript::TYPE))
				{
					auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(editor, entity, j, "-source", buf, allocator);
					editor.executeCommand(cmd);
				}

				Array<SortedProperty> sorted_props(editor.getEngine().getLIFOAllocator());
				getSortedProperties(sorted_props, *scene, entity, j);

				for (const SortedProperty& sorted_prop : sorted_props)
				{
					int k = sorted_prop.index;
					char buf[256];
					const char* property_name = scene->getPropertyName(entity, j, k);
					if (!property_name) continue;
					scene->getPropertyValue(entity, j, property_name, buf, lengthOf(buf));
					switch (scene->getPropertyType(entity, j, k))
					{
						case LuaScriptScene::Property::BOOLEAN:
						{
							bool b = equalStrings(buf, "true");
							if (ImGui::Checkbox(property_name, &b))
							{
								auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
									editor, entity, j, property_name, b ? "true" : "false", allocator);
								editor.executeCommand(cmd);
							}
						}
						break;
						case LuaScriptScene::Property::FLOAT:
						{
							float f = (float)atof(buf);
							if (ImGui::DragFloat(property_name, &f))
							{
								toCString(f, buf, sizeof(buf), 5);
								auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
									editor, entity, j, property_name, buf, allocator);
								editor.executeCommand(cmd);
							}
						}
						break;
						case LuaScriptScene::Property::ENTITY:
						{
							EntityPtr e;
							fromCString(buf, sizeof(buf), &e.index);
							if (grid.entityInput(property_name, StaticString<50>(property_name, entity.index), e))
							{
								toCString(e.index, buf, sizeof(buf));
								auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
									editor, entity, j, property_name, buf, allocator);
								editor.executeCommand(cmd);
							}
						}
						break;
						case LuaScriptScene::Property::STRING:
						case LuaScriptScene::Property::ANY:
							if (ImGui::InputText(property_name, buf, sizeof(buf)))
							{
								auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
									editor, entity, j, property_name, buf, allocator);
								editor.executeCommand(cmd);
							}
							break;
						case LuaScriptScene::Property::RESOURCE:
						{
							ResourceType res_type = scene->getPropertyResourceType(entity, j, k);
							if (m_app.getAssetBrowser().resourceInput(
									property_name, property_name, buf, lengthOf(buf), res_type))
							{
								auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
									editor, entity, j, property_name, buf, allocator);
								editor.executeCommand(cmd);
							}
						}
						break;
						default: ASSERT(false); break;
					}
				}
				if (scene->beginFunctionCall(entity, j, "onGUI"))
				{
					scene->endFunctionCall();
				}
				ImGui::TreePop();
			}
			ImGui::Indent();
		}
	}

	StudioApp& m_app;
};


struct AssetPlugin : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	explicit AssetPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetBrowser().registerExtension("lua", LuaScript::TYPE);
		m_text_buffer[0] = 0;
	}


	bool compile(const Path& src) override
	{
		const char* dst_dir = m_app.getAssetCompiler().getCompiledDir();
		const u32 hash = crc32(src.c_str());

		const StaticString<MAX_PATH_LENGTH> dst(dst_dir, hash, ".res");

		return copyFile(src.c_str(), dst);
	}

	
	void onGUI(Resource* resource) override
	{
		auto* script = static_cast<LuaScript*>(resource);

		if (m_text_buffer[0] == '\0')
		{
			copyString(m_text_buffer, script->getSourceCode());
		}
		ImGui::InputTextMultiline("Code", m_text_buffer, sizeof(m_text_buffer), ImVec2(0, 300));
		if (ImGui::Button("Save"))
		{
			auto& fs = m_app.getWorldEditor().getEngine().getFileSystem();
			auto* file = fs.open(fs.getDefaultDevice(), resource->getPath(), FS::Mode::CREATE_AND_WRITE);

			if (!file)
			{
				g_log_warning.log("Lua Script") << "Could not save " << resource->getPath();
				return;
			}

			file->write(m_text_buffer, stringLength(m_text_buffer));
			fs.close(*file);
		}
		ImGui::SameLine();
		if (ImGui::Button("Open in external editor"))
		{
			m_app.getAssetBrowser().openInExternalEditor(resource);
		}
	}


	void onResourceUnloaded(Resource*) override { m_text_buffer[0] = 0; }
	const char* getName() const override { return "Lua Script"; }


	ResourceType getResourceType() const override { return LuaScript::TYPE; }


	bool createTile(const char* in_path, const char* out_path, ResourceType type) override
	{
		if (type == LuaScript::TYPE)
		{
			return copyFile("models/editor/tile_lua_script.dds", out_path);
		}
		return false;
	}


	StudioApp& m_app;
	char m_text_buffer[8192];
};


struct ConsolePlugin final : public StudioApp::GUIPlugin
{
	explicit ConsolePlugin(StudioApp& _app)
		: app(_app)
		, open(false)
		, autocomplete(_app.getWorldEditor().getAllocator())
	{
		Action* action = LUMIX_NEW(app.getWorldEditor().getAllocator(), Action)("Script Console", "Toggle script console", "script_console");
		action->func.bind<ConsolePlugin, &ConsolePlugin::toggleOpen>(this);
		action->is_selected.bind<ConsolePlugin, &ConsolePlugin::isOpen>(this);
		app.addWindowAction(action);
		buf[0] = '\0';
	}


	static const int LUA_CALL_EVENT_SIZE = 32;


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
					autocomplete.push(string(name, app.getWorldEditor().getAllocator()));
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
			lua_State* L = that->app.getWorldEditor().getEngine().getState();

			int start_word = data->CursorPos;
			char c = data->Buf[start_word - 1];
			while (start_word > 0 && (isWordChar(c) || c == '.'))
			{
				--start_word;
				c = data->Buf[start_word - 1];
			}
			char tmp[128];
			copyNString(tmp, lengthOf(tmp), data->Buf + start_word, data->CursorPos - start_word);

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
					const char* a_str = ((const string*)a)->c_str();
					const char* b_str = ((const string*)b)->c_str();
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
		if (ImGui::Begin("Script console", &open))
		{
			if (ImGui::Button("Execute"))
			{
				lua_State* L = app.getWorldEditor().getEngine().getState();
				
				bool errors = luaL_loadbuffer(L, buf, stringLength(buf), nullptr) != 0;
				errors = errors || lua_pcall(L, 0, 0, 0) != 0;

				if (errors)
				{
					g_log_error.log("Lua Script") << lua_tostring(L, -1);
					lua_pop(L, 1);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Execute file"))
			{
				char tmp[MAX_PATH_LENGTH];
				if (PlatformInterface::getOpenFilename(tmp, MAX_PATH_LENGTH, "Scripts\0*.lua\0", nullptr))
				{
					FS::OsFile file;
					IAllocator& allocator = app.getWorldEditor().getAllocator();
					if (file.open(tmp, FS::Mode::OPEN_AND_READ))
					{
						size_t size = file.size();
						Array<char> data(allocator);
						data.resize((int)size);
						file.read(&data[0], size);
						file.close();
						lua_State* L = app.getWorldEditor().getEngine().getState();
						bool errors = luaL_loadbuffer(L, &data[0], data.size(), tmp) != 0;
						errors = errors || lua_pcall(L, 0, 0, 0) != 0;

						if (errors)
						{
							g_log_error.log("Lua Script") << lua_tostring(L, -1);
							lua_pop(L, 1);
						}
					}
					else
					{
						g_log_error.log("Lua Script") << "Failed to open file " << tmp;
					}
				}
			}
			if(insert_value) ImGui::SetKeyboardFocusHere();
			ImGui::InputTextMultiline("",
				buf,
				lengthOf(buf),
				ImVec2(-1, -1),
				ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackCompletion,
				autocompleteCallback,
				this);

			if (open_autocomplete)
			{
				ImGui::OpenPopup("autocomplete");
				ImGui::SetNextWindowPos(ImGui::GetOsImePosRequest());
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
				autocomplete_selected = Math::clamp(autocomplete_selected, 0, autocomplete.size() - 1);
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
	Array<string> autocomplete;
	bool open;
	bool open_autocomplete = false;
	int autocomplete_selected = 1;
	const char* insert_value = nullptr;
	char buf[10 * 1024];
};


IEditorCommand* createAddLuaScriptCommand(WorldEditor& editor)
{
	return LUMIX_NEW(editor.getAllocator(), PropertyGridPlugin::AddLuaScriptCommand)(editor);
}


IEditorCommand* createSetPropertyCommand(WorldEditor& editor)
{
	return LUMIX_NEW(editor.getAllocator(), PropertyGridPlugin::SetPropertyCommand)(editor);
}


IEditorCommand* createRemoveScriptCommand(WorldEditor& editor)
{
	return LUMIX_NEW(editor.getAllocator(), PropertyGridPlugin::RemoveScriptCommand)(editor);
}


struct AddComponentPlugin final : public StudioApp::IAddComponentPlugin
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
			if (PlatformInterface::getSaveFilename(full_path, lengthOf(full_path), "Lua script\0*.lua\0", "lua"))
			{
				FS::OsFile file;
				WorldEditor& editor = app.getWorldEditor();
				if (file.open(full_path, FS::Mode::CREATE_AND_WRITE))
				{
					new_created = true;
					editor.makeRelative(buf, lengthOf(buf), full_path);
					file.close();
				}
				else
				{
					g_log_error.log("Lua Script") << "Failed to create " << buf;
				}
			}
		}
		bool create_empty = ImGui::Selectable("Empty", false);

		if (asset_browser.resourceList(buf, lengthOf(buf), LuaScript::TYPE, 0) || create_empty || new_created)
		{
			WorldEditor& editor = app.getWorldEditor();
			if (create_entity)
			{
				EntityRef entity = editor.addEntity();
				editor.selectEntities(&entity, 1, false);
			}
			if (editor.getSelectedEntities().empty()) return;
			EntityRef entity = editor.getSelectedEntities()[0];

			if (!editor.getUniverse()->hasComponent(entity, LUA_SCRIPT_TYPE))
			{
				editor.addComponent(LUA_SCRIPT_TYPE);
			}

			IAllocator& allocator = editor.getAllocator();
			auto* cmd = LUMIX_NEW(allocator, PropertyGridPlugin::AddLuaScriptCommand)(editor);

			auto* script_scene = static_cast<LuaScriptScene*>(editor.getUniverse()->getScene(LUA_SCRIPT_TYPE));
			cmd->entity = entity;
			editor.executeCommand(cmd);

			if (!create_empty)
			{
				int scr_count = script_scene->getScriptCount(entity);
				auto* set_source_cmd = LUMIX_NEW(allocator, PropertyGridPlugin::SetPropertyCommand)(
					editor, entity, scr_count - 1, "-source", buf, allocator);
				editor.executeCommand(set_source_cmd);
			}

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


struct GizmoPlugin : public WorldEditor::Plugin
{
	explicit GizmoPlugin(WorldEditor& _editor)
		: editor(_editor)
	{
	}


	bool showGizmo(ComponentUID cmp) override
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


	WorldEditor& editor;
};


struct StudioAppPlugin : StudioApp::IPlugin
{
	StudioAppPlugin(StudioApp& app)
		: m_app(app)
	{
		WorldEditor& editor = app.getWorldEditor();
		IAllocator& allocator = editor.getAllocator();

		m_add_component_plugin = LUMIX_NEW(allocator, AddComponentPlugin)(app);
		app.registerComponent("lua_script", *m_add_component_plugin);

		editor.registerEditorCommandCreator("add_script", createAddLuaScriptCommand);
		editor.registerEditorCommandCreator("remove_script", createRemoveScriptCommand);
		editor.registerEditorCommandCreator("set_script_property", createSetPropertyCommand);
		m_gizmo_plugin = LUMIX_NEW(allocator, GizmoPlugin)(editor);
		editor.addPlugin(*m_gizmo_plugin);

		m_prop_grid_plugin = LUMIX_NEW(allocator, PropertyGridPlugin)(app);
		app.getPropertyGrid().addPlugin(*m_prop_grid_plugin);

		m_asset_plugin = LUMIX_NEW(allocator, AssetPlugin)(app);
		app.getAssetBrowser().addPlugin(*m_asset_plugin);
		const char* exts[] = { "lua", nullptr };
		app.getAssetCompiler().addPlugin(*m_asset_plugin, exts);

		m_console_plugin = LUMIX_NEW(allocator, ConsolePlugin)(app);
		app.addPlugin(*m_console_plugin);
	}


	~StudioAppPlugin()
	{
		IAllocator& allocator = m_app.getWorldEditor().getAllocator();
		
		m_app.getWorldEditor().removePlugin(*m_gizmo_plugin);
		LUMIX_DELETE(allocator, m_gizmo_plugin);
		
		m_app.getPropertyGrid().removePlugin(*m_prop_grid_plugin);
		LUMIX_DELETE(allocator, m_prop_grid_plugin);

		m_app.getAssetCompiler().removePlugin(*m_asset_plugin);
		m_app.getAssetBrowser().removePlugin(*m_asset_plugin);
		LUMIX_DELETE(allocator, m_asset_plugin);

		m_app.removePlugin(*m_console_plugin);
		LUMIX_DELETE(allocator, m_console_plugin);
	}


	StudioApp& m_app;
	AddComponentPlugin* m_add_component_plugin;
	GizmoPlugin* m_gizmo_plugin;
	PropertyGridPlugin* m_prop_grid_plugin;
	AssetPlugin* m_asset_plugin;
	ConsolePlugin* m_console_plugin;
};


} // anonymous namespace


LUMIX_STUDIO_ENTRY(lua_script)
{
	IAllocator& allocator = app.getWorldEditor().getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}


