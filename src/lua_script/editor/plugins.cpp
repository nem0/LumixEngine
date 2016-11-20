#include "editor/asset_browser.h"
#include "editor/ieditor_command.h"
#include "editor/platform_interface.h"
#include "editor/property_grid.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/array.h"
#include "engine/base_proxy_allocator.h"
#include "engine/binary_array.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/debug/debug.h"
#include "engine/engine.h"
#include "engine/fs/file_system.h"
#include "engine/fs/os_file.h"
#include "engine/iallocator.h"
#include "engine/iplugin.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
#include "engine/resource_manager.h"
#include "engine/universe/universe.h"
#include "imgui/imgui.h"
#include "lua_script/lua_script_manager.h"
#include "lua_script/lua_script_system.h"


using namespace Lumix;


static const ComponentType LUA_SCRIPT_TYPE = PropertyRegister::getComponentType("lua_script");
static const ResourceType LUA_SCRIPT_RESOURCE_TYPE("lua_script");


namespace
{


struct PropertyGridPlugin LUMIX_FINAL : public PropertyGrid::IPlugin
{
	struct AddScriptCommand LUMIX_FINAL : public IEditorCommand
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


	struct MoveScriptCommand LUMIX_FINAL : public IEditorCommand
	{
		explicit MoveScriptCommand(WorldEditor& editor)
			: blob(editor.getAllocator())
			, scr_index(-1)
			, cmp(INVALID_COMPONENT)
			, up(true)
		{
			scene = static_cast<LuaScriptScene*>(editor.getUniverse()->getScene(crc32("lua_script")));
		}


		explicit MoveScriptCommand(IAllocator& allocator)
			: blob(allocator)
			, scene(nullptr)
			, scr_index(-1)
			, cmp(INVALID_COMPONENT)
			, up(true)
		{
		}


		bool execute() override
		{
			scene->moveScript(cmp, scr_index, up);
			return true;
		}


		void undo() override
		{
			scene->moveScript(cmp, up ? scr_index - 1 : scr_index + 1, !up);
		}


		void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("component", cmp);
			serializer.serialize("scr_index", scr_index);
			serializer.serialize("up", up);
		}


		void deserialize(JsonSerializer& serializer) override
		{
			serializer.deserialize("component", cmp, INVALID_COMPONENT);
			serializer.deserialize("scr_index", scr_index, 0);
			serializer.deserialize("up", up, false);
		}


		const char* getType() override { return "move_script"; }


		bool merge(IEditorCommand& command) override { return false; }


		OutputBlob blob;
		LuaScriptScene* scene;
		ComponentHandle cmp;
		int scr_index;
		bool up;
	};


	struct RemoveScriptCommand LUMIX_FINAL : public IEditorCommand
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


	struct SetPropertyCommand LUMIX_FINAL : public IEditorCommand
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
				u32 prop_name_hash = crc32(property_name);
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
	}


	void onGUI(PropertyGrid& grid, ComponentUID cmp) override
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
			StaticString<MAX_PATH_LENGTH + 20> header;
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
				ImGui::SameLine();
				if (ImGui::Button("Up"))
				{
					auto* cmd = LUMIX_NEW(allocator, MoveScriptCommand)(allocator);
					cmd->cmp = cmp.handle;
					cmd->scr_index = j;
					cmd->scene = scene;
					cmd->up = true;
					editor.executeCommand(cmd);
					ImGui::PopID();
					break;
				}
				ImGui::SameLine();
				if (ImGui::Button("Down"))
				{
					auto* cmd = LUMIX_NEW(allocator, MoveScriptCommand)(allocator);
					cmd->cmp = cmp.handle;
					cmd->scr_index = j;
					cmd->scene = scene;
					cmd->up = false;
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
								toCString(f, buf, sizeof(buf), 5);
								auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
									scene, cmp.handle, j, property_name, buf, allocator);
								editor.executeCommand(cmd);
							}
						}
						break;
						case LuaScriptScene::Property::ENTITY:
						{
							Entity e;
							fromCString(buf, sizeof(buf), &e.index);
							if (grid.entityInput(property_name, StaticString<50>(property_name, cmp.handle.index), e))
							{
								toCString(e.index, buf, sizeof(buf));
								auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
									scene, cmp.handle, j, property_name, buf, allocator);
								editor.executeCommand(cmd);
							}
						}
						break;
						case LuaScriptScene::Property::STRING:
						case LuaScriptScene::Property::ANY:
							if (ImGui::InputText(property_name, buf, sizeof(buf)))
							{
								auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
									scene, cmp.handle, j, property_name, buf, allocator);
								editor.executeCommand(cmd);
							}
							break;
						case LuaScriptScene::Property::RESOURCE:
						{
							ResourceType res_type = scene->getPropertyResourceType(cmp.handle, j, k);
							if (m_app.getAssetBrowser()->resourceInput(
									property_name, property_name, buf, lengthOf(buf), res_type))
							{
								auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
									scene, cmp.handle, j, property_name, buf, allocator);
								editor.executeCommand(cmd);
							}
						}
						break;
						default: ASSERT(false); break;
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


	bool acceptExtension(const char* ext, ResourceType type) const override
	{
		return type == LUA_SCRIPT_RESOURCE_TYPE && equalStrings(".lua", ext);
	}


	bool onGUI(Resource* resource, ResourceType type) override
	{
		if (type != LUA_SCRIPT_RESOURCE_TYPE) return false;

		auto* script = static_cast<LuaScript*>(resource);

		if (m_text_buffer[0] == '\0')
		{
			copyString(m_text_buffer, script->getSourceCode());
		}
		ImGui::InputTextMultiline("Code", m_text_buffer, sizeof(m_text_buffer), ImVec2(0, 300));
		if (ImGui::Button("Save"))
		{
			auto& fs = m_app.getWorldEditor()->getEngine().getFileSystem();
			auto* file = fs.open(fs.getDefaultDevice(), resource->getPath(), FS::Mode::CREATE_AND_WRITE);

			if (!file)
			{
				g_log_warning.log("Lua Script") << "Could not save " << resource->getPath();
				return true;
			}

			file->write(m_text_buffer, stringLength(m_text_buffer));
			fs.close(*file);
		}
		ImGui::SameLine();
		if (ImGui::Button("Open in external editor"))
		{
			m_app.getAssetBrowser()->openInExternalEditor(resource);
		}
		return true;
	}


	ResourceType getResourceType(const char* ext) override
	{
		if (equalStrings(ext, "lua")) return LUA_SCRIPT_RESOURCE_TYPE;
		return INVALID_RESOURCE_TYPE;
	}


	void onResourceUnloaded(Resource*) override { m_text_buffer[0] = 0; }
	const char* getName() const override { return "Lua Script"; }


	bool hasResourceManager(ResourceType type) const override { return type == LUA_SCRIPT_RESOURCE_TYPE; }


	StudioApp& m_app;
	char m_text_buffer[8192];
};


struct ConsolePlugin LUMIX_FINAL : public StudioApp::IPlugin
{
	ConsolePlugin(StudioApp& _app)
		: app(_app)
		, opened(false)
	{
		Action* action = LUMIX_NEW(app.getWorldEditor()->getAllocator(), Action)("Script Console", "script_console");
		action->func.bind<ConsolePlugin, &ConsolePlugin::toggleOpened>(this);
		action->is_selected.bind<ConsolePlugin, &ConsolePlugin::isOpened>(this);
		app.addWindowAction(action);
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
			ImGui::SameLine();
			if (ImGui::Button("Execute file"))
			{
				char tmp[MAX_PATH_LENGTH];
				if (PlatformInterface::getOpenFilename(tmp, MAX_PATH_LENGTH, "Scripts\0*.lua\0", nullptr))
				{
					FS::OsFile file;
					IAllocator& allocator = app.getWorldEditor()->getAllocator();
					if (file.open(tmp, FS::Mode::OPEN_AND_READ, allocator))
					{
						size_t size = file.size();
						Array<char> data(allocator);
						data.resize((int)size);
						file.read(&data[0], size);
						file.close();
						lua_State* L = app.getWorldEditor()->getEngine().getState();
						bool errors = luaL_loadbuffer(L, &data[0], data.size(), nullptr) != LUA_OK;
						errors = errors || lua_pcall(L, 0, 0, 0) != LUA_OK;

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


struct AddComponentPlugin LUMIX_FINAL : public StudioApp::IAddComponentPlugin
{
	AddComponentPlugin(StudioApp& _app)
		: app(_app)
	{
	}


	void onGUI(bool create_entity, bool) override
	{
		ImGui::SetNextWindowSize(ImVec2(300, 300));
		if (!ImGui::BeginMenu(getLabel())) return;
		char buf[MAX_PATH_LENGTH];
		auto* asset_browser = app.getAssetBrowser();
		bool create_empty = ImGui::Selectable("Empty", false);
		if (asset_browser->resourceList(buf, lengthOf(buf), LUA_SCRIPT_RESOURCE_TYPE, 0) || create_empty)
		{
			auto& editor = *app.getWorldEditor();
			if (create_entity)
			{
				Entity entity = editor.addEntity();
				editor.selectEntities(&entity, 1);
			}
			if (editor.getSelectedEntities().empty()) return;
			Entity entity = editor.getSelectedEntities()[0];

			if (!editor.getUniverse()->hasComponent(entity, LUA_SCRIPT_TYPE))
			{
				editor.addComponent(LUA_SCRIPT_TYPE);
			}

			auto& allocator = editor.getAllocator();
			auto* cmd = LUMIX_NEW(allocator, PropertyGridPlugin::AddScriptCommand);

			auto* script_scene = static_cast<LuaScriptScene*>(editor.getUniverse()->getScene(LUA_SCRIPT_TYPE));
			ComponentHandle cmp = editor.getUniverse()->getComponent(entity, LUA_SCRIPT_TYPE).handle;
			cmd->scene = script_scene;
			cmd->cmp = cmp;
			editor.executeCommand(cmd);

			if (!create_empty)
			{
				int scr_count = script_scene->getScriptCount(cmp);
				auto* set_source_cmd = LUMIX_NEW(allocator, PropertyGridPlugin::SetPropertyCommand)(
					script_scene, cmp, scr_count - 1, "-source", buf, allocator);
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


struct EditorPlugin : public WorldEditor::Plugin
{
	EditorPlugin(WorldEditor& _editor)
		: editor(_editor)
	{
	}


	bool showGizmo(ComponentUID cmp) override
	{
		if (cmp.type == LUA_SCRIPT_TYPE)
		{
			auto* scene = static_cast<LuaScriptScene*>(cmp.scene);
			int count = scene->getScriptCount(cmp.handle);
			for (int i = 0; i < count; ++i)
			{
				if (auto* call = scene->beginFunctionCall(cmp.handle, i, "onDrawGizmo"))
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


LUMIX_STUDIO_ENTRY(lua_script)
{
	auto& editor = *app.getWorldEditor();
	auto* cmp_plugin = LUMIX_NEW(editor.getAllocator(), AddComponentPlugin)(app);
	app.registerComponent("lua_script", "Lua Script", *cmp_plugin);

	editor.registerEditorCommandCreator("add_script", createAddScriptCommand);
	editor.registerEditorCommandCreator("remove_script", createRemoveScriptCommand);
	editor.registerEditorCommandCreator("set_script_property", createSetPropertyCommand);
	auto* editor_plugin = LUMIX_NEW(editor.getAllocator(), EditorPlugin)(editor);
	editor.addPlugin(*editor_plugin);

	auto* plugin = LUMIX_NEW(editor.getAllocator(), PropertyGridPlugin)(app);
	app.getPropertyGrid()->addPlugin(*plugin);

	auto* asset_browser_plugin = LUMIX_NEW(editor.getAllocator(), AssetBrowserPlugin)(app);
	app.getAssetBrowser()->addPlugin(*asset_browser_plugin);

	auto* console_plugin = LUMIX_NEW(editor.getAllocator(), ConsolePlugin)(app);
	app.addPlugin(*console_plugin);
}


