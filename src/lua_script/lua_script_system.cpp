#include "lua_script_system.h"
#include "core/array.h"
#include "core/base_proxy_allocator.h"
#include "core/binary_array.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/iallocator.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/lua_wrapper.h"
#include "core/resource_manager.h"
#include "debug/debug.h"
#include "editor/property_register.h"
#include "editor/property_descriptor.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "iplugin.h"
#include "lua_script/lua_script_manager.h"
#include "plugin_manager.h"
#include "studio_lib/asset_browser.h"
#include "studio_lib/imgui/imgui.h"
#include "studio_lib/property_grid.h"
#include "studio_lib/studio_app.h"
#include "studio_lib/utils.h"
#include "universe/universe.h"


namespace Lumix
{


	class LuaScriptSystemImpl;


	void registerEngineLuaAPI(LuaScriptScene& scene, Engine& engine, lua_State* L);
	void registerUniverse(UniverseContext*, lua_State* L);



	static const uint32 LUA_SCRIPT_HASH = crc32("lua_script");


	class LuaScriptSystem : public IPlugin
	{
	public:
		LuaScriptSystem(Engine& engine);
		virtual ~LuaScriptSystem();

		IAllocator& getAllocator();
		IScene* createScene(UniverseContext& universe) override;
		void destroyScene(IScene* scene) override;
		bool create() override;
		void destroy() override;
		const char* getName() const override;
		LuaScriptManager& getScriptManager() { return m_script_manager; }

		Engine& m_engine;
		Debug::Allocator m_allocator;
		LuaScriptManager m_script_manager;
	};


	class LuaScriptSceneImpl : public LuaScriptScene
	{
	public:
		struct ScriptComponent
		{
			ScriptComponent(IAllocator& allocator)
				: m_properties(allocator)
			{
				m_script = nullptr;
			}

			LuaScript* m_script;
			int m_entity;
			ComponentIndex m_component;
			lua_State* m_state;
			int m_environment;
			Array<Property> m_properties;
		};


		struct FunctionCall : IFunctionCall
		{
			void add(int parameter) override
			{
				lua_pushinteger(state, parameter);
				++parameter_count;
			}


			void add(float parameter) override
			{
				lua_pushnumber(state, parameter);
				++parameter_count;
			}


			int parameter_count;
			lua_State* state;
			bool is_in_progress;
			ComponentIndex cmp;
		};


	public:
		LuaScriptSceneImpl(LuaScriptSystem& system, UniverseContext& ctx)
			: m_system(system)
			, m_universe_context(ctx)
			, m_scripts(system.getAllocator())
			, m_global_state(nullptr)
			, m_updates(system.getAllocator())
			, m_entity_script_map(system.getAllocator())
		{
			auto* scene = m_universe_context.getScene(crc32("physics"));
			m_first_free_script = -1;
			m_function_call.is_in_progress = false;
		}


		ComponentIndex getComponent(Entity entity) override
		{
			auto iter = m_entity_script_map.find(entity);
			if (!iter.isValid()) return INVALID_COMPONENT;

			return iter.value()->m_component;
		}


		IFunctionCall* beginFunctionCall(ComponentIndex cmp, const char* function) override
		{
			ASSERT(!m_function_call.is_in_progress);

			auto& script = *m_scripts[cmp];
			if (!script.m_state) return nullptr;

			lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment);
			if (lua_getfield(script.m_state, -1, function) != LUA_TFUNCTION)
			{
				lua_pop(script.m_state, 1);
				return nullptr;
			}

			m_function_call.state = m_scripts[cmp]->m_state;
			m_function_call.cmp = cmp;
			m_function_call.is_in_progress = true;
			m_function_call.parameter_count = 0;

			return &m_function_call;
		}


		void endFunctionCall(IFunctionCall& caller)
		{
			ASSERT(&caller == &m_function_call);
			ASSERT(m_global_state);
			ASSERT(m_function_call.is_in_progress);

			m_function_call.is_in_progress = false;

			auto& script = *m_scripts[m_function_call.cmp];
			if (!script.m_state) return;

			if (lua_pcall(script.m_state, m_function_call.parameter_count, 0, 0) != LUA_OK)
			{
				g_log_error.log("lua") << lua_tostring(script.m_state, -1);
				lua_pop(script.m_state, 1);
			}
			lua_pop(script.m_state, 1);
		}


		~LuaScriptSceneImpl()
		{
			unloadAllScripts();
		}


		void registerFunction(const char* system, const char* name, lua_CFunction function) override
		{
			if (lua_getglobal(m_global_state, system) == LUA_TNIL)
			{
				lua_pop(m_global_state, 1);
				lua_newtable(m_global_state);
				lua_setglobal(m_global_state, system);
				lua_getglobal(m_global_state, system);
			}

			lua_pushcfunction(m_global_state, function);
			lua_setfield(m_global_state, -2, name);
		}


		void unloadAllScripts()
		{
			for (int i = 0; i < m_scripts.size(); ++i)
			{
				if (!m_scripts[i]) continue;

				ScriptComponent& script = *m_scripts[i];
				if (script.m_script) m_system.getScriptManager().unload(*script.m_script);
				LUMIX_DELETE(m_system.getAllocator(), &script);
			}
			m_entity_script_map.clear();
			m_scripts.clear();
			m_first_free_script = -1;
		}


		Universe& getUniverse() override { return *m_universe_context.m_universe; }


		void registerAPI(lua_State* L)
		{
			registerUniverse(&m_universe_context, L);
			registerEngineLuaAPI(*this, m_system.m_engine, L);
			uint32 register_msg = crc32("registerLuaAPI");
			for (auto* i : m_universe_context.m_scenes)
			{
				i->sendMessage(register_msg, nullptr);
			}
		}


		int getEnvironment(Entity entity) override
		{
			auto iter = m_entity_script_map.find(entity);
			if (iter == m_entity_script_map.end()) return -1;

			return iter.value()->m_environment;
		}


		void applyProperty(ScriptComponent& script, Property& prop)
		{
			if (prop.m_value.length() == 0) return;

			lua_State* state = script.m_state;
			const char* name = script.m_script->getPropertyName(prop.m_name_hash);
			if (!name)
			{
				return;
			}
			char tmp[1024];
			copyString(tmp, name);
			catString(tmp, " = ");
			catString(tmp, prop.m_value.c_str());

			bool errors =
				luaL_loadbuffer(state, tmp, stringLength(tmp), nullptr) != LUA_OK;

			lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment);
			lua_setupvalue(script.m_state, -2, 1);

			errors = errors || lua_pcall(state, 0, LUA_MULTRET, 0) != LUA_OK;

			if (errors)
			{
				g_log_error.log("lua") << script.m_script->getPath() << ": "
					<< lua_tostring(state, -1);
				lua_pop(state, 1);
			}
		}


		LuaScript* getScriptResource(ComponentIndex cmp) const override
		{
			return m_scripts[cmp]->m_script;
		}


		const char* getPropertyValue(Lumix::ComponentIndex cmp, int index) const override
		{
			auto& script = *m_scripts[cmp];
			uint32 hash = crc32(getPropertyName(cmp, index));

			for (auto& value : script.m_properties)
			{
				if (value.m_name_hash == hash)
				{
					return value.m_value.c_str();
				}
			}

			return "";
		}


		void setPropertyValue(Lumix::ComponentIndex cmp,
			const char* name,
			const char* value) override
		{
			if (!m_scripts[cmp]) return;

			Property& prop = getScriptProperty(cmp, name);
			prop.m_value = value;

			if (m_scripts[cmp]->m_state)
			{
				applyProperty(*m_scripts[cmp], prop);
			}
		}


		const char* getPropertyName(Lumix::ComponentIndex cmp, int index) const override
		{
			auto& script = *m_scripts[cmp];

			return script.m_script ? script.m_script->getProperties()[index].name : "";
		}


		int getPropertyCount(Lumix::ComponentIndex cmp) const override
		{
			auto& script = *m_scripts[cmp];

			return script.m_script ? script.m_script->getProperties().size() : 0;
		}


		void applyProperties(ScriptComponent& script)
		{
			if (!script.m_script) return;

			for (Property& prop : script.m_properties)
			{
				applyProperty(script, prop);
			}
		}


		static void* luaAllocator(void* ud, void* ptr, size_t osize, size_t nsize)
		{
			auto& allocator = *static_cast<IAllocator*>(ud);
			if (nsize == 0)
			{
				allocator.deallocate(ptr);
				return nullptr;
			}
			if (nsize > 0 && ptr == nullptr) return allocator.allocate(nsize);

			void* new_mem = allocator.allocate(nsize);
			copyMemory(new_mem, ptr, Math::minValue(osize, nsize));
			allocator.deallocate(ptr);
			return new_mem;
		}


		void startGame() override
		{
			m_global_state = lua_newstate(luaAllocator, &m_system.getAllocator());
			luaL_openlibs(m_global_state);
			registerAPI(m_global_state);
			for (int i = 0; i < m_scripts.size(); ++i)
			{
				if (!m_scripts[i] || !m_scripts[i]->m_script) continue;

				ScriptComponent& script = *m_scripts[i];
				script.m_environment = -1;

				if (!script.m_script->isReady())
				{
					script.m_state = nullptr;
					g_log_error.log("lua script") << "Script " << script.m_script->getPath()
						<< " is not loaded";
					continue;
				}

				script.m_state = lua_newthread(m_global_state);
				lua_newtable(script.m_state);
				// reference environment
				lua_pushvalue(script.m_state, -1);
				script.m_environment = luaL_ref(script.m_state, LUA_REGISTRYINDEX);

				// environment's metatable & __index
				lua_pushvalue(script.m_state, -1);
				lua_setmetatable(script.m_state, -2);
				lua_pushglobaltable(script.m_state);
				lua_setfield(script.m_state, -2, "__index");

				// set this
				lua_pushinteger(script.m_state, script.m_entity);
				lua_setfield(script.m_state, -2, "this");

				applyProperties(script);
				lua_pop(script.m_state, 1);
			}

			for (int i = 0; i < m_scripts.size(); ++i)
			{
				if (!m_scripts[i] || !m_scripts[i]->m_script) continue;

				ScriptComponent& script = *m_scripts[i];
				if (!script.m_script->isReady()) continue;

				lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment);
				bool errors = luaL_loadbuffer(script.m_state,
					script.m_script->getSourceCode(),
					stringLength(script.m_script->getSourceCode()),
					script.m_script->getPath().c_str()) != LUA_OK;

				if (errors)
				{
					g_log_error.log("lua") << script.m_script->getPath() << ": "
						<< lua_tostring(script.m_state, -1);
					lua_pop(script.m_state, 1);
					continue;
				}

				lua_pushvalue(script.m_state, -2);
				lua_setupvalue(script.m_state, -2, 1); // function's environment

				errors = errors || lua_pcall(script.m_state, 0, LUA_MULTRET, 0) != LUA_OK;
				if (errors)
				{
					g_log_error.log("lua") << script.m_script->getPath() << ": "
						<< lua_tostring(script.m_state, -1);
					lua_pop(script.m_state, 1);
				}
				lua_pop(script.m_state, 1);

				lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment);
				if (lua_getfield(script.m_state, -1, "update") == LUA_TFUNCTION)
				{
					m_updates.push(m_scripts[i]);
				}
				lua_pop(script.m_state, 1);
			}
		}


		void stopGame() override
		{
			m_updates.clear();
			for (ScriptComponent* script : m_scripts)
			{
				if (script) script->m_state = nullptr;
			}

			lua_close(m_global_state);
			m_global_state = nullptr;
		}


		ComponentIndex createComponent(uint32 type, Entity entity) override
		{
			if (type == LUA_SCRIPT_HASH)
			{
				ScriptComponent& script =
					*LUMIX_NEW(m_system.getAllocator(), ScriptComponent)(m_system.getAllocator());
				m_entity_script_map.insert(entity, &script);
				ComponentIndex cmp = -1;
				if (m_first_free_script >= 0)
				{
					int next_free = *(int*)&m_scripts[m_first_free_script];
					m_scripts[m_first_free_script] = &script;
					cmp = m_first_free_script;
					m_first_free_script = next_free;
				}
				else
				{
					cmp = m_scripts.size();
					script.m_component = cmp;
					m_scripts.push(&script);
				}
				script.m_entity = entity;
				script.m_script = nullptr;
				script.m_state = nullptr;
				m_universe_context.m_universe->addComponent(entity, type, this, cmp);
				return m_scripts.size() - 1;
			}
			return INVALID_COMPONENT;
		}


		void destroyComponent(ComponentIndex component, uint32 type) override
		{
			if (type == LUA_SCRIPT_HASH)
			{
				m_updates.eraseItem(m_scripts[component]);
				if (m_scripts[component]->m_script)
					m_system.getScriptManager().unload(*m_scripts[component]->m_script);
				m_entity_script_map.erase(m_scripts[component]->m_entity);

				auto* script = m_scripts[component];
				m_scripts[component] = nullptr;
				m_universe_context.m_universe->destroyComponent(
					script->m_entity, type, this, component);
				LUMIX_DELETE(m_system.getAllocator(), script);
				if (m_first_free_script >= 0)
				{
					*(int*)&m_scripts[component] = m_first_free_script;
					m_first_free_script = component;
				}
				else
				{
					m_first_free_script = component;
					*(int*)&m_scripts[m_first_free_script] = -1;
				}
			}
		}


		void serialize(OutputBlob& serializer) override
		{
			serializer.write(m_scripts.size());
			for (int i = 0; i < m_scripts.size(); ++i)
			{
				serializer.write(m_scripts[i] ? true : false);
				if (!m_scripts[i]) continue;

				serializer.write(m_scripts[i]->m_entity);
				serializer.writeString(
					m_scripts[i]->m_script ? m_scripts[i]->m_script->getPath().c_str() : "");
				serializer.write(m_scripts[i]->m_properties.size());
				for (Property& prop : m_scripts[i]->m_properties)
				{
					serializer.write(prop.m_name_hash);
					serializer.writeString(prop.m_value.c_str());
				}
			}
		}


		void deserialize(InputBlob& serializer, int) override
		{
			int len = serializer.read<int>();
			unloadAllScripts();
			m_scripts.reserve(len);
			for (int i = 0; i < len; ++i)
			{
				bool is_valid;
				serializer.read(is_valid);
				if (!is_valid)
				{
					m_scripts.push(nullptr);
					continue;
				}

				ScriptComponent& script = *LUMIX_NEW(m_system.getAllocator(), ScriptComponent)(m_system.getAllocator());
				script.m_component = m_scripts.size();
				m_scripts.push(&script);
				serializer.read(m_scripts[i]->m_entity);
				m_entity_script_map.insert(m_scripts[i]->m_entity, &script);
				char tmp[MAX_PATH_LENGTH];
				serializer.readString(tmp, MAX_PATH_LENGTH);
				script.m_script = static_cast<LuaScript*>(
					m_system.getScriptManager().load(Lumix::Path(tmp)));
				script.m_state = nullptr;
				int prop_count;
				serializer.read(prop_count);
				script.m_properties.reserve(prop_count);
				for (int j = 0; j < prop_count; ++j)
				{
					Property& prop =
						script.m_properties.emplace(m_system.getAllocator());
					serializer.read(prop.m_name_hash);
					char tmp[1024];
					tmp[0] = 0;
					serializer.readString(tmp, sizeof(tmp));
					prop.m_value = tmp;
				}
				m_universe_context.m_universe->addComponent(
					Entity(m_scripts[i]->m_entity), LUA_SCRIPT_HASH, this, i);
			}
		}


		IPlugin& getPlugin() const override { return m_system; }


		void update(float time_delta) override
		{
			if (!m_global_state) { return; }

			for (auto* i : m_updates)
			{
				auto& script = *i;
				lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment);
				if (lua_getfield(script.m_state, -1, "update") != LUA_TFUNCTION)
				{
					lua_pop(script.m_state, 1);
					continue;
				}

				lua_pushnumber(script.m_state, time_delta);
				if (lua_pcall(script.m_state, 1, 0, 0) != LUA_OK)
				{
					g_log_error.log("lua") << lua_tostring(script.m_state, -1);
					lua_pop(script.m_state, 1);
				}
				lua_pop(script.m_state, 1);
			}
		}


		ComponentIndex getComponent(Entity entity, uint32 type) override
		{
			ASSERT(ownComponentType(type));
			for (auto* i : m_scripts)
			{
				if (i && i->m_entity == entity) return i->m_component;
			}
			return INVALID_COMPONENT;
		}


		bool ownComponentType(uint32 type) const override
		{
			return type == LUA_SCRIPT_HASH;
		}


		Property& getScriptProperty(ComponentIndex cmp, const char* name)
		{
			uint32 name_hash = crc32(name);
			for (auto& prop : m_scripts[cmp]->m_properties)
			{
				if (prop.m_name_hash == name_hash)
				{
					return prop;
				}
			}

			m_scripts[cmp]->m_properties.emplace(m_system.getAllocator());
			auto& prop = m_scripts[cmp]->m_properties.back();
			prop.m_name_hash = name_hash;
			return prop;
		}


		Path getScriptPath(ComponentIndex cmp) override
		{
			return m_scripts[cmp]->m_script ? m_scripts[cmp]->m_script->getPath() : Path("");
		}


		void setScriptPath(ComponentIndex cmp, const Path& path) override
		{
			if (m_scripts[cmp]->m_script)
			{
				m_system.getScriptManager().unload(*m_scripts[cmp]->m_script);
			}

			m_scripts[cmp]->m_script =
				static_cast<LuaScript*>(m_system.getScriptManager().load(path));
		}


	private:
		LuaScriptSystem& m_system;

		Array<ScriptComponent*> m_scripts;
		PODHashMap<Entity, ScriptComponent*> m_entity_script_map;
		lua_State* m_global_state;
		UniverseContext& m_universe_context;
		Array<ScriptComponent*> m_updates;
		int m_first_free_script;
		FunctionCall m_function_call;
	};


	LuaScriptSystem::LuaScriptSystem(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_script_manager(m_allocator)
	{
		m_script_manager.create(crc32("lua_script"), engine.getResourceManager());
	}


	LuaScriptSystem::~LuaScriptSystem()
	{
		m_script_manager.destroy();
	}


	IAllocator& LuaScriptSystem::getAllocator()
	{
		return m_allocator;
	}


	IScene* LuaScriptSystem::createScene(UniverseContext& ctx)
	{
		return LUMIX_NEW(m_allocator, LuaScriptSceneImpl)(*this, ctx);
	}


	void LuaScriptSystem::destroyScene(IScene* scene)
	{
		LUMIX_DELETE(m_allocator, scene);
	}


	bool LuaScriptSystem::create()
	{
		return true;
	}


	void LuaScriptSystem::destroy()
	{
	}


	const char* LuaScriptSystem::getName() const
	{
		return "lua_script";
	}


	namespace
	{


	struct PropertyGridPlugin : public PropertyGrid::Plugin
	{
		void onGUI(PropertyGrid& grid, Lumix::ComponentUID cmp) override
		{
			if (cmp.type != LUA_SCRIPT_HASH) return;

			auto* scene = static_cast<Lumix::LuaScriptScene*>(cmp.scene);

			for (int i = 0; i < scene->getPropertyCount(cmp.index); ++i)
			{
				char buf[256];
				Lumix::copyString(buf, scene->getPropertyValue(cmp.index, i));
				const char* property_name = scene->getPropertyName(cmp.index, i);
				auto* script_res = scene->getScriptResource(cmp.index);
				switch (script_res->getProperties()[i].type)
				{
					case Lumix::LuaScript::Property::FLOAT:
					{
						float f = (float)atof(buf);
						if (ImGui::DragFloat(property_name, &f))
						{
							Lumix::toCString(f, buf, sizeof(buf), 5);
							scene->setPropertyValue(cmp.index, property_name, buf);
						}
					}
					break;
					case Lumix::LuaScript::Property::ENTITY:
					{
						Lumix::Entity e;
						Lumix::fromCString(buf, sizeof(buf), &e);
						if (grid.entityInput(
								property_name, StringBuilder<50>(property_name, cmp.index), e))
						{
							Lumix::toCString(e, buf, sizeof(buf));
							scene->setPropertyValue(cmp.index, property_name, buf);
						}
					}
					break;
					case Lumix::LuaScript::Property::ANY:
						if (ImGui::InputText(property_name, buf, sizeof(buf)))
						{
							scene->setPropertyValue(cmp.index, property_name, buf);
						}
						break;
				}
			}
		}
	};


	struct AssetBrowserPlugin : AssetBrowser::IPlugin
	{
		AssetBrowserPlugin(StudioApp& app)
			: m_app(app)
		{
			m_text_buffer[0] = 0;
		}


		bool onGUI(Lumix::Resource* resource, Lumix::uint32 type) override 
		{
			if (type != LUA_SCRIPT_HASH) return false;

			auto* script = static_cast<Lumix::LuaScript*>(resource);

			if (m_text_buffer[0] == '\0')
			{
				Lumix::copyString(m_text_buffer, script->getSourceCode());
			}
			ImGui::InputTextMultiline("Code", m_text_buffer, sizeof(m_text_buffer), ImVec2(0, 300));
			if (ImGui::Button("Save"))
			{
				auto& fs = m_app.getWorldEditor()->getEngine().getFileSystem();
				auto* file = fs.open(fs.getDiskDevice(),
					resource->getPath(),
					Lumix::FS::Mode::CREATE | Lumix::FS::Mode::WRITE);

				if (!file)
				{
					Lumix::g_log_warning.log("Asset browser") << "Could not save "
															  << resource->getPath();
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


		Lumix::uint32 getResourceType(const char* ext) override
		{
			if (compareString(ext, "lua") == 0) return LUA_SCRIPT_HASH;
			return 0;
		}


		void onResourceUnloaded(Lumix::Resource*) override { m_text_buffer[0] = 0; }
		const char* getName() const override { return "Lua Script"; }


		bool hasResourceManager(Lumix::uint32 type) const override 
		{
			return type == LUA_SCRIPT_HASH;
		}


		StudioApp& m_app;
		char m_text_buffer[8192];
		bool m_is_opened;
	};


	} // anonoymous namespace


extern "C" LUMIX_LIBRARY_EXPORT void setStudioApp(StudioApp& app)
{
	auto& allocator = app.getWorldEditor()->getAllocator();
	PropertyRegister::registerComponentType("lua_script", "Lua script");

	PropertyRegister::add("lua_script",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<LuaScriptScene>)("source",
		&LuaScriptScene::getScriptPath,
		&LuaScriptScene::setScriptPath,
		"Lua (*.lua)",
		crc32("lua_script"),
		allocator));

	auto* plugin = LUMIX_NEW(app.getWorldEditor()->getAllocator(), PropertyGridPlugin);
	app.getPropertyGrid()->addPlugin(*plugin);

	auto* asset_browser_plugin =
		LUMIX_NEW(app.getWorldEditor()->getAllocator(), AssetBrowserPlugin)(app);
	app.getAssetBrowser()->addPlugin(*asset_browser_plugin);
}


extern "C" LUMIX_LIBRARY_EXPORT IPlugin* createPlugin(Engine& engine)
{
	return LUMIX_NEW(engine.getAllocator(), LuaScriptSystem)(engine);
}


} // ~namespace Lumix
