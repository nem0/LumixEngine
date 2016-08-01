#include "engine/array.h"
#include "engine/base_proxy_allocator.h"
#include "engine/binary_array.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/fs/file_system.h"
#include "engine/iallocator.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/path_utils.h"
#include "engine/resource_manager.h"
#include "engine/debug/debug.h"
#include "editor/asset_browser.h"
#include "editor/ieditor_command.h"
#include "editor/property_grid.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/property_register.h"
#include "engine/property_descriptor.h"
#include "engine/iplugin.h"
#include "imgui/imgui.h"
#include "lua_script/lua_script_manager.h"
#include "lua_script/lua_script_system.h"
#include "engine/plugin_manager.h"
#include "engine/universe/universe.h"


using namespace Lumix;


static const ComponentType LUA_SCRIPT_TYPE = PropertyRegister::getComponentType("lua_script");
static const ResourceType LUA_SCRIPT_RESOURCE_TYPE("lua_script");


namespace
{


int DragFloat(lua_State* L)
{
	auto* name = Lumix::LuaWrapper::checkArg<const char*>(L, 1);
	float value = Lumix::LuaWrapper::checkArg<float>(L, 2);
	bool changed = ImGui::DragFloat(name, &value);
	lua_pushboolean(L, changed);
	lua_pushnumber(L, value);
	return 2;
}


int Text(lua_State* L)
{
	auto* text = Lumix::LuaWrapper::checkArg<const char*>(L, 1);
	ImGui::Text("%s", text);
	return 0;
}


int Button(lua_State* L)
{
	auto* label = Lumix::LuaWrapper::checkArg<const char*>(L, 1);
	bool clicked = ImGui::Button(label);
	lua_pushboolean(L, clicked);
	return 1;
}


int Checkbox(lua_State* L)
{
	auto* label = Lumix::LuaWrapper::checkArg<const char*>(L, 1);
	bool b = Lumix::LuaWrapper::checkArg<bool>(L, 2);
	bool clicked = ImGui::Checkbox(label, &b);
	lua_pushboolean(L, clicked);
	lua_pushboolean(L, b);
	return 2;
}


int Image(lua_State* L)
{
	auto* texture_id = Lumix::LuaWrapper::checkArg<void*>(L, 1);
	float size_x = Lumix::LuaWrapper::checkArg<float>(L, 2);
	float size_y = Lumix::LuaWrapper::checkArg<float>(L, 3);
	ImGui::Image(texture_id, ImVec2(size_x, size_y));
	return 0;
}


int Begin(lua_State* L)
{
	auto* label = Lumix::LuaWrapper::checkArg<const char*>(L, 1);
	bool res = ImGui::Begin(label);
	lua_pushboolean(L, res);
	return 1;
}


int BeginDock(lua_State* L)
{
	auto* label = Lumix::LuaWrapper::checkArg<const char*>(L, 1);
	bool res = ImGui::BeginDock(label);
	lua_pushboolean(L, res);
	return 1;
}


int SameLine(lua_State* L)
{
	ImGui::SameLine();
	return 0;
}


void registerCFunction(lua_State* L, const char* name, lua_CFunction f)
{
	lua_pushvalue(L, -1);
	lua_pushcfunction(L, f);
	lua_setfield(L, -2, name);
}


struct PropertyGridPlugin : public PropertyGrid::IPlugin
{
	struct AddScriptCommand : public IEditorCommand
	{
		AddScriptCommand() {}


		explicit AddScriptCommand(WorldEditor& editor)
		{
			scene = static_cast<LuaScriptScene*>(editor.getUniverse()->getScene(crc32("lua_script")));
		}


		bool execute() override
		{
			scr_index = scene->addScript(cmp);
			return true;
		}


		void undo() override { scene->removeScript(cmp, scr_index); }


		void serialize(JsonSerializer& serializer) override { serializer.serialize("component", cmp); }


		void deserialize(JsonSerializer& serializer) override
		{
			serializer.deserialize("component", cmp, INVALID_COMPONENT);
		}


		const char* getType() override { return "add_script"; }


		bool merge(IEditorCommand& command) override { return false; }


		LuaScriptScene* scene;
		ComponentHandle cmp;
		int scr_index;
	};


	struct RemoveScriptCommand : public IEditorCommand
	{
		explicit RemoveScriptCommand(WorldEditor& editor)
			: blob(editor.getAllocator())
			, scr_index(-1)
			, cmp(INVALID_COMPONENT)
		{
			scene = static_cast<LuaScriptScene*>(editor.getUniverse()->getScene(crc32("lua_script")));
		}


		explicit RemoveScriptCommand(IAllocator& allocator)
			: blob(allocator)
			, scene(nullptr)
			, scr_index(-1)
			, cmp(INVALID_COMPONENT)
		{
		}


		bool execute() override
		{
			scene->serializeScript(cmp, scr_index, blob);
			scene->removeScript(cmp, scr_index);
			return true;
		}


		void undo() override
		{
			scene->insertScript(cmp, scr_index);
			InputBlob input(blob);
			scene->deserializeScript(cmp, scr_index, input);
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("component", cmp);
			serializer.serialize("scr_index", scr_index);
		}


		void deserialize(JsonSerializer& serializer) override
		{
			serializer.deserialize("component", cmp, INVALID_COMPONENT);
			serializer.deserialize("scr_index", scr_index, 0);
		}


		const char* getType() override { return "remove_script"; }


		bool merge(IEditorCommand& command) override { return false; }

		OutputBlob blob;
		LuaScriptScene* scene;
		ComponentHandle cmp;
		int scr_index;
	};


	struct SetPropertyCommand : public IEditorCommand
	{
		explicit SetPropertyCommand(WorldEditor& editor)
			: property_name(editor.getAllocator())
			, value(editor.getAllocator())
			, old_value(editor.getAllocator())
		{
			scene = static_cast<LuaScriptScene*>(editor.getUniverse()->getScene(crc32("lua_script")));
		}


		SetPropertyCommand(LuaScriptScene* scene,
			ComponentHandle cmp,
			int scr_index,
			const char* property_name,
			const char* val,
			IAllocator& allocator)
			: property_name(property_name, allocator)
			, value(val, allocator)
			, old_value(allocator)
			, component(cmp)
			, script_index(scr_index)
		{
			this->scene = scene;
			if (property_name[0] == '-')
			{
				old_value = scene->getScriptPath(component, script_index).c_str();
			}
			else
			{
				char tmp[1024];
				tmp[0] = '\0';
				uint32 prop_name_hash = crc32(property_name);
				scene->getPropertyValue(cmp, scr_index, property_name, tmp, lengthOf(tmp));
				old_value = tmp;
				return;
			}
		}


		bool execute() override
		{
			if (property_name.length() > 0 && property_name[0] == '-')
			{
				scene->setScriptPath(component, script_index, Path(value.c_str()));
			}
			else
			{
				scene->setPropertyValue(component, script_index, property_name.c_str(), value.c_str());
			}
			return true;
		}


		void undo() override
		{
			if (property_name.length() > 0 && property_name[0] == '-')
			{
				scene->setScriptPath(component, script_index, Path(old_value.c_str()));
			}
			else
			{
				scene->setPropertyValue(component, script_index, property_name.c_str(), old_value.c_str());
			}
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("component", component);
			serializer.serialize("script_index", script_index);
			serializer.serialize("property_name", property_name.c_str());
			serializer.serialize("value", value.c_str());
			serializer.serialize("old_value", old_value.c_str());
		}


		void deserialize(JsonSerializer& serializer) override
		{
			serializer.deserialize("component", component, INVALID_COMPONENT);
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
				cmd.value = value;
				return true;
			}
			return false;
		}


		LuaScriptScene* scene;
		string property_name;
		string value;
		string old_value;
		ComponentHandle component;
		int script_index;
	};


	explicit PropertyGridPlugin(StudioApp& app)
		: m_app(app)
	{
		lua_State* L = app.getWorldEditor()->getEngine().getState();
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setglobal(L, "ImGui");

		registerCFunction(L, "DragFloat", &DragFloat);
		registerCFunction(L, "Button", &Button);
		registerCFunction(L, "Text", &Text);
		registerCFunction(L, "Checkbox", &Checkbox);
		registerCFunction(L, "SameLine", &SameLine);
		registerCFunction(L, "BeginPopup", &LuaWrapper::wrap<decltype(&ImGui::BeginPopup), &ImGui::BeginPopup>);
		registerCFunction(L, "EndPopup", &LuaWrapper::wrap<decltype(&ImGui::EndPopup), &ImGui::EndPopup>);
		registerCFunction(L, "OpenPopup", &LuaWrapper::wrap<decltype(&ImGui::OpenPopup), &ImGui::OpenPopup>);
		registerCFunction(L, "BeginDock", &LuaWrapper::wrap<decltype(&BeginDock), &BeginDock>);
		registerCFunction(L, "EndDock", &LuaWrapper::wrap<decltype(&ImGui::EndDock), &ImGui::EndDock>);
		registerCFunction(L, "Begin", &LuaWrapper::wrap<decltype(&Begin), &Begin>);
		registerCFunction(L, "End", &LuaWrapper::wrap<decltype(&ImGui::End), &ImGui::End>);
		registerCFunction(L, "Image", &LuaWrapper::wrap<decltype(&Image), &Image>);

		lua_pop(L, 1);
	}


	void onGUI(PropertyGrid& grid, Lumix::ComponentUID cmp) override
	{
		if (cmp.type != LUA_SCRIPT_TYPE) return;

		auto* scene = static_cast<LuaScriptScene*>(cmp.scene);
		auto& editor = *m_app.getWorldEditor();
		auto& allocator = editor.getAllocator();

		if (ImGui::Button("Add script"))
		{
			auto* cmd = LUMIX_NEW(allocator, AddScriptCommand);
			cmd->scene = scene;
			cmd->cmp = cmp.handle;
			editor.executeCommand(cmd);
		}

		for (int j = 0; j < scene->getScriptCount(cmp.handle); ++j)
		{
			char buf[MAX_PATH_LENGTH];
			copyString(buf, scene->getScriptPath(cmp.handle, j).c_str());
			StaticString<Lumix::MAX_PATH_LENGTH + 20> header;
			PathUtils::getBasename(header.data, lengthOf(header.data), buf);
			if (header.data[0] == 0) header << j;
			header << "###" << j;
			if (ImGui::CollapsingHeader(header))
			{
				ImGui::PushID(j);
				if (ImGui::Button("Remove script"))
				{
					auto* cmd = LUMIX_NEW(allocator, RemoveScriptCommand)(allocator);
					cmd->cmp = cmp.handle;
					cmd->scr_index = j;
					cmd->scene = scene;
					editor.executeCommand(cmd);
					ImGui::PopID();
					break;
				}
				if (m_app.getAssetBrowser()->resourceInput(
						"Source", "src", buf, lengthOf(buf), LUA_SCRIPT_RESOURCE_TYPE))
				{
					auto* cmd =
						LUMIX_NEW(allocator, SetPropertyCommand)(scene, cmp.handle, j, "-source", buf, allocator);
					editor.executeCommand(cmd);
				}
				for (int k = 0, kc = scene->getPropertyCount(cmp.handle, j); k < kc; ++k)
				{
					char buf[256];
					const char* property_name = scene->getPropertyName(cmp.handle, j, k);
					if (!property_name) continue;
					scene->getPropertyValue(cmp.handle, j, property_name, buf, lengthOf(buf));
					switch (scene->getPropertyType(cmp.handle, j, k))
					{
						case LuaScriptScene::Property::BOOLEAN:
						{
							bool b = equalStrings(buf, "true");
							if (ImGui::Checkbox(property_name, &b))
							{
								auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
									scene, cmp.handle, j, property_name, b ? "true" : "false", allocator);
								editor.executeCommand(cmd);
							}
						}
						break;
						case LuaScriptScene::Property::FLOAT:
						{
							float f = (float)atof(buf);
							if (ImGui::DragFloat(property_name, &f))
							{
								Lumix::toCString(f, buf, sizeof(buf), 5);
								auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
									scene, cmp.handle, j, property_name, buf, allocator);
								editor.executeCommand(cmd);
							}
						}
						break;
						case LuaScriptScene::Property::ENTITY:
						{
							Lumix::Entity e;
							Lumix::fromCString(buf, sizeof(buf), &e.index);
							if (grid.entityInput(property_name, StaticString<50>(property_name, cmp.handle.index), e))
							{
								Lumix::toCString(e.index, buf, sizeof(buf));
								auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
									scene, cmp.handle, j, property_name, buf, allocator);
								editor.executeCommand(cmd);
							}
						}
						break;
						case LuaScriptScene::Property::ANY:
							if (ImGui::InputText(property_name, buf, sizeof(buf)))
							{
								auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
									scene, cmp.handle, j, property_name, buf, allocator);
								editor.executeCommand(cmd);
							}
							break;
					}
				}
				if (auto* call = scene->beginFunctionCall(cmp.handle, j, "onGUI"))
				{
					scene->endFunctionCall();
				}
				ImGui::PopID();
			}
		}
	}

	StudioApp& m_app;
};


struct AssetBrowserPlugin : AssetBrowser::IPlugin
{
	explicit AssetBrowserPlugin(StudioApp& app)
		: m_app(app)
	{
		m_text_buffer[0] = 0;
	}


	bool acceptExtension(const char* ext, Lumix::ResourceType type) const override
	{
		return type == LUA_SCRIPT_RESOURCE_TYPE && equalStrings(".lua", ext);
	}


	bool onGUI(Lumix::Resource* resource, Lumix::ResourceType type) override
	{
		if (type != LUA_SCRIPT_RESOURCE_TYPE) return false;

		auto* script = static_cast<Lumix::LuaScript*>(resource);

		if (m_text_buffer[0] == '\0')
		{
			Lumix::copyString(m_text_buffer, script->getSourceCode());
		}
		ImGui::InputTextMultiline("Code", m_text_buffer, sizeof(m_text_buffer), ImVec2(0, 300));
		if (ImGui::Button("Save"))
		{
			auto& fs = m_app.getWorldEditor()->getEngine().getFileSystem();
			auto* file = fs.open(fs.getDefaultDevice(), resource->getPath(), Lumix::FS::Mode::CREATE_AND_WRITE);

			if (!file)
			{
				Lumix::g_log_warning.log("Lua Script") << "Could not save " << resource->getPath();
				return true;
			}

			file->write(m_text_buffer, Lumix::stringLength(m_text_buffer));
			fs.close(*file);
		}
		ImGui::SameLine();
		if (ImGui::Button("Open in external editor"))
		{
			m_app.getAssetBrowser()->openInExternalEditor(resource);
		}
		return true;
	}


	Lumix::ResourceType getResourceType(const char* ext) override
	{
		if (equalStrings(ext, "lua")) return LUA_SCRIPT_RESOURCE_TYPE;
		return INVALID_RESOURCE_TYPE;
	}


	void onResourceUnloaded(Lumix::Resource*) override { m_text_buffer[0] = 0; }
	const char* getName() const override { return "Lua Script"; }


	bool hasResourceManager(Lumix::ResourceType type) const override { return type == LUA_SCRIPT_RESOURCE_TYPE; }


	StudioApp& m_app;
	char m_text_buffer[8192];
};


struct ConsolePlugin : public StudioApp::IPlugin
{
	ConsolePlugin(StudioApp& _app)
		: app(_app)
		, opened(false)
	{
		m_action = LUMIX_NEW(app.getWorldEditor()->getAllocator(), Action)("Script Console", "script_console");
		m_action->func.bind<ConsolePlugin, &ConsolePlugin::toggleOpened>(this);
		m_action->is_selected.bind<ConsolePlugin, &ConsolePlugin::isOpened>(this);
		buf[0] = '\0';
	}


	bool isOpened() const { return opened; }
	void toggleOpened() { opened = !opened; }


	void onWindowGUI() override
	{
		if (ImGui::BeginDock("Script console", &opened))
		{
			if (ImGui::Button("Execute"))
			{
				lua_State* L = app.getWorldEditor()->getEngine().getState();
				bool errors = luaL_loadbuffer(L, buf, stringLength(buf), nullptr) != LUA_OK;
				errors = errors || lua_pcall(L, 0, 0, 0) != LUA_OK;

				if (errors)
				{
					g_log_error.log("Lua Script") << lua_tostring(L, -1);
					lua_pop(L, 1);
				}

			}
			ImGui::InputTextMultiline("", buf, lengthOf(buf), ImVec2(-1, -1));
		}
		ImGui::EndDock();
	}


	StudioApp& app;
	bool opened;
	char buf[4096];
};


} // anonoymous namespace


IEditorCommand* createAddScriptCommand(WorldEditor& editor)
{
	return LUMIX_NEW(editor.getAllocator(), PropertyGridPlugin::AddScriptCommand)(editor);
}


IEditorCommand* createSetPropertyCommand(WorldEditor& editor)
{
	return LUMIX_NEW(editor.getAllocator(), PropertyGridPlugin::SetPropertyCommand)(editor);
}


IEditorCommand* createRemoveScriptCommand(WorldEditor& editor)
{
	return LUMIX_NEW(editor.getAllocator(), PropertyGridPlugin::RemoveScriptCommand)(editor);
}


struct AddComponentPlugin : public StudioApp::IAddComponentPlugin
{
	AddComponentPlugin(StudioApp& _app)
		: app(_app)
	{
	}


	void onGUI(bool create_entity) override
	{
		ImGui::SetNextWindowSize(ImVec2(300, 300));
		if (!ImGui::BeginMenu(getLabel())) return;
		char buf[Lumix::MAX_PATH_LENGTH];
		auto* asset_browser = app.getAssetBrowser();
		bool create_empty = ImGui::Selectable("Empty", false);
		if (asset_browser->resourceList(buf, Lumix::lengthOf(buf), LUA_SCRIPT_RESOURCE_TYPE, 0) || create_empty)
		{
			auto& editor = *app.getWorldEditor();
			if (create_entity)
			{
				Entity entity = editor.addEntity();
				editor.selectEntities(&entity, 1);
			}
			editor.addComponent(LUA_SCRIPT_TYPE);

			auto& allocator = editor.getAllocator();
			auto* cmd = LUMIX_NEW(allocator, PropertyGridPlugin::AddScriptCommand);

			auto* script_scene = static_cast<LuaScriptScene*>(editor.getUniverse()->getScene(LUA_SCRIPT_TYPE));
			Entity entity = editor.getSelectedEntities()[0];
			ComponentHandle cmp = editor.getUniverse()->getComponent(entity, LUA_SCRIPT_TYPE).handle;
			cmd->scene = script_scene;
			cmd->cmp = cmp;
			editor.executeCommand(cmd);

			if (!create_empty)
			{
				auto* set_source_cmd = LUMIX_NEW(allocator, PropertyGridPlugin::SetPropertyCommand)(
					script_scene, cmp, 0, "-source", buf, allocator);
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


LUMIX_STUDIO_ENTRY(lua_script)
{
	auto& editor = *app.getWorldEditor();
	auto* cmp_plugin = LUMIX_NEW(editor.getAllocator(), AddComponentPlugin)(app);
	app.registerComponent("lua_script", "Lua Script", *cmp_plugin);

	editor.registerEditorCommandCreator("add_script", createAddScriptCommand);
	editor.registerEditorCommandCreator("remove_script", createRemoveScriptCommand);
	editor.registerEditorCommandCreator("set_script_property", createSetPropertyCommand);

	auto* plugin = LUMIX_NEW(editor.getAllocator(), PropertyGridPlugin)(app);
	app.getPropertyGrid()->addPlugin(*plugin);

	auto* asset_browser_plugin = LUMIX_NEW(editor.getAllocator(), AssetBrowserPlugin)(app);
	app.getAssetBrowser()->addPlugin(*asset_browser_plugin);

	auto* console_plugin = LUMIX_NEW(editor.getAllocator(), ConsolePlugin)(app);
	app.addPlugin(*console_plugin);
}


