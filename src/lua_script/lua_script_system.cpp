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
#include "core/path_utils.h"
#include "core/resource_manager.h"
#include "debug/debug.h"
#include "editor/asset_browser.h"
#include "editor/imgui/imgui.h"
#include "editor/property_grid.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "engine/property_register.h"
#include "engine/property_descriptor.h"
#include "iplugin.h"
#include "lua_script/lua_script_manager.h"
#include "plugin_manager.h"
#include "universe/universe.h"


namespace Lumix
{


	enum class LuaScriptVersion
	{
		MULTIPLE_SCRIPTS,

		LATEST
	};


	class LuaScriptSystemImpl;


	void registerEngineLuaAPI(LuaScriptScene& scene, Engine& engine, lua_State* L);
	void registerUniverse(Universe*, lua_State* L);



	static const uint32 LUA_SCRIPT_HASH = crc32("lua_script");


	class LuaScriptSystem : public IPlugin
	{
	public:
		LuaScriptSystem(Engine& engine);
		virtual ~LuaScriptSystem();

		IAllocator& getAllocator();
		IScene* createScene(Universe& universe) override;
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
		struct UpdateData
		{
			LuaScript* script;
			lua_State* state;
			int environment;
			ComponentIndex cmp;
		};

		struct ScriptInstance
		{
			ScriptInstance(IAllocator& allocator)
				: m_properties(allocator)
			{
				m_script = nullptr;
				m_state = nullptr;
			}

			LuaScript* m_script;
			lua_State* m_state;
			int m_environment;
			Array<Property> m_properties;
		};


		struct ScriptComponent
		{
			ScriptComponent(IAllocator& allocator)
				: m_scripts(allocator)
			{
			}

			Array<ScriptInstance> m_scripts;
			int m_entity;
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
			int scr_index;
		};


	public:
		LuaScriptSceneImpl(LuaScriptSystem& system, Universe& ctx)
			: m_system(system)
			, m_universe(ctx)
			, m_scripts(system.getAllocator())
			, m_global_state(nullptr)
			, m_updates(system.getAllocator())
			, m_entity_script_map(system.getAllocator())
		{
			m_function_call.is_in_progress = false;
		}


		ComponentIndex getComponent(Entity entity) override
		{
			auto iter = m_entity_script_map.find(entity);
			if (!iter.isValid()) return INVALID_COMPONENT;

			return iter.value();
		}


		IFunctionCall* beginFunctionCall(ComponentIndex cmp, int scr_index, const char* function) override
		{
			ASSERT(!m_function_call.is_in_progress);

			auto& script = *m_scripts[cmp];
			if (!script.m_scripts[scr_index].m_state) return nullptr;

			lua_rawgeti(script.m_scripts[scr_index].m_state, LUA_REGISTRYINDEX, script.m_scripts[scr_index].m_environment);
			if (lua_getfield(script.m_scripts[scr_index].m_state, -1, function) != LUA_TFUNCTION)
			{
				lua_pop(script.m_scripts[scr_index].m_state, 1);
				return nullptr;
			}

			m_function_call.state = m_scripts[cmp]->m_scripts[scr_index].m_state;
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

			auto& script = m_scripts[m_function_call.cmp]->m_scripts[m_function_call.scr_index];
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

				for (auto script : m_scripts[i]->m_scripts)
				{
					if (script.m_script) m_system.getScriptManager().unload(*script.m_script);
				}
				LUMIX_DELETE(m_system.getAllocator(), m_scripts[i]);
			}
			m_entity_script_map.clear();
			m_scripts.clear();
		}


		Universe& getUniverse() override { return m_universe; }


		void registerAPI(lua_State* L)
		{
			registerUniverse(&m_universe, L);
			registerEngineLuaAPI(*this, m_system.m_engine, L);
			uint32 register_msg = crc32("registerLuaAPI");
			for (auto* i : m_universe.getScenes())
			{
				i->sendMessage(register_msg, nullptr);
			}
		}


		int getEnvironment(Entity entity, int scr_index) override
		{
			auto iter = m_entity_script_map.find(entity);
			if (iter == m_entity_script_map.end()) return -1;

			return m_scripts[iter.value()]->m_scripts[scr_index].m_environment;
		}


		void applyProperty(ScriptInstance& script, Property& prop)
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


		LuaScript* getScriptResource(ComponentIndex cmp, int scr_index) const override
		{
			return m_scripts[cmp]->m_scripts[scr_index].m_script;
		}


		const char* getPropertyValue(Lumix::ComponentIndex cmp, int scr_index, int index) const override
		{
			auto& script = *m_scripts[cmp];
			uint32 hash = crc32(getPropertyName(cmp, scr_index, index));

			for (auto& value : script.m_scripts[scr_index].m_properties)
			{
				if (value.m_name_hash == hash)
				{
					return value.m_value.c_str();
				}
			}

			return "";
		}


		void setPropertyValue(Lumix::ComponentIndex cmp,
			int scr_index,
			const char* name,
			const char* value) override
		{
			if (!m_scripts[cmp]) return;

			Property& prop = getScriptProperty(cmp, scr_index, name);
			prop.m_value = value;

			if (m_scripts[cmp]->m_scripts[scr_index].m_state)
			{
				applyProperty(m_scripts[cmp]->m_scripts[scr_index], prop);
			}
		}


		const char* getPropertyName(Lumix::ComponentIndex cmp, int scr_index, int index) const override
		{
			auto& script = m_scripts[cmp]->m_scripts[scr_index];

			return script.m_script ? script.m_script->getProperties()[index].name : "";
		}


		int getPropertyCount(Lumix::ComponentIndex cmp, int scr_index) const override
		{
			auto& script = m_scripts[cmp]->m_scripts[scr_index];

			return script.m_script ? script.m_script->getProperties().size() : 0;
		}


		void applyProperties(ScriptInstance& script)
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
				if (!m_scripts[i]) continue;

				for (auto& script : m_scripts[i]->m_scripts)
				{
					if (!script.m_script) continue;
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
					lua_pushinteger(script.m_state, m_scripts[i]->m_entity);
					lua_setfield(script.m_state, -2, "this");

					applyProperties(script);
					lua_pop(script.m_state, 1);
				}
			}

			for (int i = 0; i < m_scripts.size(); ++i)
			{
				if (!m_scripts[i]) continue;

				for (auto& script : m_scripts[i]->m_scripts)
				{
					if (!script.m_script) continue;
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
						auto& update_data = m_updates.emplace();
						update_data.script = script.m_script;
						update_data.state = script.m_state;
						update_data.environment = script.m_environment;
					}
					lua_pop(script.m_state, 1);
				}
			}
		}


		void stopGame() override
		{
			m_updates.clear();
			for (ScriptComponent* script : m_scripts)
			{
				if (!script) continue;
				for (auto& i : script->m_scripts)
				{
					i.m_state = nullptr;
				}
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
				ComponentIndex cmp = INVALID_COMPONENT;
				for (int i = 0; i < m_scripts.size(); ++i)
				{
					if (m_scripts[i] == nullptr)
					{
						cmp = i;
						m_scripts[i] = &script;
						break;
					}
				}
				if (cmp == INVALID_COMPONENT)
				{
					cmp = m_scripts.size();
					m_scripts.push(&script);
				}
				m_entity_script_map.insert(entity, cmp);
				script.m_entity = entity;
				m_universe.addComponent(entity, type, this, cmp);
				return cmp;
			}
			return INVALID_COMPONENT;
		}


		void destroyComponent(ComponentIndex component, uint32 type) override
		{
			if (type != LUA_SCRIPT_HASH) return;

			for (int i = 0, c = m_updates.size(); i < c; ++i)
			{
				if(m_updates[i].cmp == component) m_updates.erase(i);
			}
			for (auto& scr : m_scripts[component]->m_scripts)
			{
				if (scr.m_script) m_system.getScriptManager().unload(*scr.m_script);
			}
			m_entity_script_map.erase(m_scripts[component]->m_entity);
			auto* script = m_scripts[component];
			m_scripts[component] = nullptr;
			m_universe.destroyComponent(script->m_entity, type, this, component);
			LUMIX_DELETE(m_system.getAllocator(), script);
		}


		void serialize(OutputBlob& serializer) override
		{
			serializer.write(m_scripts.size());
			for (int i = 0; i < m_scripts.size(); ++i)
			{
				serializer.write(m_scripts[i] ? true : false);
				if (!m_scripts[i]) continue;

				serializer.write(m_scripts[i]->m_entity);
				serializer.write(m_scripts[i]->m_scripts.size());
				for (auto& scr : m_scripts[i]->m_scripts)
				{
					serializer.writeString(
						m_scripts[i]->m_scripts[i].m_script
							? m_scripts[i]->m_scripts[i].m_script->getPath().c_str()
							: "");
					serializer.write(m_scripts[i]->m_scripts[i].m_properties.size());
					for (Property& prop : m_scripts[i]->m_scripts[i].m_properties)
					{
						serializer.write(prop.m_name_hash);
						serializer.writeString(prop.m_value.c_str());
					}
				}
			}
		}


		int getVersion() const override
		{
			return (int)LuaScriptVersion::LATEST;
		}


		void deserialize(InputBlob& serializer, int version) override
		{
			if (version <= (int)LuaScriptVersion::MULTIPLE_SCRIPTS)
			{
				deserializeOld(serializer);
				return;
			}

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
				m_scripts.push(&script);

				int scr_count;
				serializer.read(m_scripts[i]->m_entity);
				serializer.read(scr_count);
				m_entity_script_map.insert(m_scripts[i]->m_entity, i);
				for (int j = 0; j < scr_count; ++j)
				{
					auto& scr = script.m_scripts.emplace(m_system.m_allocator);

					char tmp[MAX_PATH_LENGTH];
					serializer.readString(tmp, MAX_PATH_LENGTH);
					scr.m_script =
						static_cast<LuaScript*>(m_system.getScriptManager().load(Lumix::Path(tmp)));
					scr.m_state = nullptr;
					int prop_count;
					serializer.read(prop_count);
					scr.m_properties.reserve(prop_count);
					for (int j = 0; j < prop_count; ++j)
					{
						Property& prop = scr.m_properties.emplace(m_system.getAllocator());
						serializer.read(prop.m_name_hash);
						char tmp[1024];
						tmp[0] = 0;
						serializer.readString(tmp, sizeof(tmp));
						prop.m_value = tmp;
					}
				}
				m_universe.addComponent(
					Entity(m_scripts[i]->m_entity), LUA_SCRIPT_HASH, this, i);
			}
		}


		void deserializeOld(InputBlob& serializer)
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
				m_scripts.push(&script);
				serializer.read(m_scripts[i]->m_entity);
				m_entity_script_map.insert(m_scripts[i]->m_entity, i);
				char tmp[MAX_PATH_LENGTH];
				serializer.readString(tmp, MAX_PATH_LENGTH);
				auto& scr = script.m_scripts.emplace(m_system.m_allocator);
				scr.m_script =
					static_cast<LuaScript*>(m_system.getScriptManager().load(Lumix::Path(tmp)));
				scr.m_state = nullptr;
				int prop_count;
				serializer.read(prop_count);
				scr.m_properties.reserve(prop_count);
				for (int j = 0; j < prop_count; ++j)
				{
					Property& prop =
						scr.m_properties.emplace(m_system.getAllocator());
					serializer.read(prop.m_name_hash);
					char tmp[1024];
					tmp[0] = 0;
					serializer.readString(tmp, sizeof(tmp));
					prop.m_value = tmp;
				}
				m_universe.addComponent(m_scripts[i]->m_entity, LUA_SCRIPT_HASH, this, i);
			}
		}


		IPlugin& getPlugin() const override { return m_system; }


		void update(float time_delta, bool paused) override
		{
			if (!m_global_state || paused) { return; }

			for (auto& i : m_updates)
			{
				lua_rawgeti(i.state, LUA_REGISTRYINDEX, i.environment);
				if (lua_getfield(i.state, -1, "update") != LUA_TFUNCTION)
				{
					lua_pop(i.state, 1);
					continue;
				}

				lua_pushnumber(i.state, time_delta);
				if (lua_pcall(i.state, 1, 0, 0) != LUA_OK)
				{
					g_log_error.log("lua") << lua_tostring(i.state, -1);
					lua_pop(i.state, 1);
				}
				lua_pop(i.state, 1);
			}
		}


		ComponentIndex getComponent(Entity entity, uint32 type) override
		{
			ASSERT(ownComponentType(type));
			auto iter = m_entity_script_map.find(entity);
			if (iter.isValid()) return iter.value();
			return INVALID_COMPONENT;
		}


		bool ownComponentType(uint32 type) const override
		{
			return type == LUA_SCRIPT_HASH;
		}


		Property& getScriptProperty(ComponentIndex cmp, int scr_index, const char* name)
		{
			uint32 name_hash = crc32(name);
			for (auto& prop : m_scripts[cmp]->m_scripts[scr_index].m_properties)
			{
				if (prop.m_name_hash == name_hash)
				{
					return prop;
				}
			}

			m_scripts[cmp]->m_scripts[scr_index].m_properties.emplace(m_system.getAllocator());
			auto& prop = m_scripts[cmp]->m_scripts[scr_index].m_properties.back();
			prop.m_name_hash = name_hash;
			return prop;
		}


		Path getScriptPath(ComponentIndex cmp, int scr_index) override
		{
			return m_scripts[cmp]->m_scripts[scr_index].m_script ? m_scripts[cmp]->m_scripts[scr_index].m_script->getPath() : Path("");
		}


		void setScriptPath(ComponentIndex cmp, int scr_index, const Path& path) override
		{
			if (m_scripts[cmp]->m_scripts[scr_index].m_script)
			{
				m_system.getScriptManager().unload(*m_scripts[cmp]->m_scripts[scr_index].m_script);
			}

			m_scripts[cmp]->m_scripts[scr_index].m_script =
				static_cast<LuaScript*>(m_system.getScriptManager().load(path));
		}


		int getScriptCount(ComponentIndex cmp) override
		{
			return m_scripts[cmp]->m_scripts.size();
		}


		void addScript(ComponentIndex cmp)
		{
			m_scripts[cmp]->m_scripts.emplace(m_system.m_allocator);
		}


		void removeScript(ComponentIndex cmp, int scr_index)
		{
			m_scripts[cmp]->m_scripts.eraseFast(scr_index);
		}


	private:
		LuaScriptSystem& m_system;

		Array<ScriptComponent*> m_scripts;
		PODHashMap<Entity, ComponentIndex> m_entity_script_map;
		lua_State* m_global_state;
		Universe& m_universe;
		Array<UpdateData> m_updates;
		FunctionCall m_function_call;
	};


	LuaScriptSystem::LuaScriptSystem(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_script_manager(m_allocator)
	{
		m_script_manager.create(crc32("lua_script"), engine.getResourceManager());

		PropertyRegister::registerComponentType("lua_script", "Lua script");
	}


	LuaScriptSystem::~LuaScriptSystem()
	{
		m_script_manager.destroy();
	}


	IAllocator& LuaScriptSystem::getAllocator()
	{
		return m_allocator;
	}


	IScene* LuaScriptSystem::createScene(Universe& ctx)
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


	struct PropertyGridPlugin : public PropertyGrid::IPlugin
	{
		PropertyGridPlugin(StudioApp& app)
			: m_app(app)
		{
		}


		void onGUI(PropertyGrid& grid, Lumix::ComponentUID cmp) override
		{
			if (cmp.type != LUA_SCRIPT_HASH) return;

			auto* scene = static_cast<Lumix::LuaScriptSceneImpl*>(cmp.scene);

			if (ImGui::Button("Add script"))
			{
				scene->addScript(cmp.index);
			}

			for (int j = 0; j < scene->getScriptCount(cmp.index); ++j)
			{
				char buf[MAX_PATH_LENGTH];
				copyString(buf, scene->getScriptPath(cmp.index, j).c_str());
				char basename[50];
				PathUtils::getBasename(basename, lengthOf(basename), buf);
				if (basename[0] == 0)
				{
					toCString(j, basename, lengthOf(basename));
				}

				if (ImGui::CollapsingHeader(basename))
				{
					ImGui::PushID(j);
					if (ImGui::Button("Remove script"))
					{
						scene->removeScript(cmp.index, j);
						ImGui::PopID();
						break;
					}
					if (m_app.getAssetBrowser()->resourceInput("Source", "src", buf, lengthOf(buf), LUA_SCRIPT_HASH))
					{
						scene->setScriptPath(cmp.index, j, Path(buf));
					}
					auto* script_res = scene->getScriptResource(cmp.index, j);
					for (int i = 0; i < scene->getPropertyCount(cmp.index, j); ++i)
					{
						char buf[256];
						Lumix::copyString(buf, scene->getPropertyValue(cmp.index, j, i));
						const char* property_name = scene->getPropertyName(cmp.index, j, i);
						switch (script_res->getProperties()[i].type)
						{
						case Lumix::LuaScript::Property::FLOAT:
						{
							float f = (float)atof(buf);
							if (ImGui::DragFloat(property_name, &f))
							{
								Lumix::toCString(f, buf, sizeof(buf), 5);
								scene->setPropertyValue(cmp.index, j, property_name, buf);
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
								scene->setPropertyValue(cmp.index, j, property_name, buf);
							}
						}
						break;
						case Lumix::LuaScript::Property::ANY:
							if (ImGui::InputText(property_name, buf, sizeof(buf)))
							{
								scene->setPropertyValue(cmp.index, j, property_name, buf);
							}
							break;
						}
					}
					ImGui::PopID();
				}
			}
		}

		StudioApp& m_app;
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
	auto* plugin = LUMIX_NEW(app.getWorldEditor()->getAllocator(), PropertyGridPlugin)(app);
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
