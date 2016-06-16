#include "lua_script_system.h"
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
#include "engine/engine.h"
#include "engine/property_register.h"
#include "engine/property_descriptor.h"
#include "engine/iplugin.h"
#include "lua_script/lua_script_manager.h"
#include "engine/plugin_manager.h"
#include "engine/universe/universe.h"


namespace Lumix
{


	enum class LuaScriptVersion
	{
		MULTIPLE_SCRIPTS,

		LATEST
	};
	
	
	static const uint32 LUA_SCRIPT_HASH = crc32("lua_script");


	class LuaScriptSystemImpl : public IPlugin
	{
	public:
		explicit LuaScriptSystemImpl(Engine& engine);
		virtual ~LuaScriptSystemImpl();

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


	struct LuaScriptSceneImpl : public LuaScriptScene
	{
		struct UpdateData
		{
			LuaScript* script;
			lua_State* state;
			int environment;
			ComponentIndex cmp;
		};


		struct ScriptInstance
		{
			explicit ScriptInstance(IAllocator& allocator)
				: m_properties(allocator)
				, m_script(nullptr)
				, m_state(nullptr)
				, m_environment(-1)
				, m_thread_ref(-1)
			{
			}

			LuaScript* m_script;
			lua_State* m_state;
			int m_environment;
			int m_thread_ref;
			Array<Property> m_properties;
		};


		struct ScriptComponent
		{
			ScriptComponent(LuaScriptSceneImpl& scene, IAllocator& allocator)
				: m_scripts(allocator)
				, m_scene(scene)
				, m_entity(INVALID_ENTITY)
			{
			}


			static Property* getProperty(ScriptInstance& inst, uint32 hash)
			{
				for(auto& i : inst.m_properties)
				{
					if(i.name_hash == hash) return &i;
				}
				return nullptr;
			}


			void detectProperties(ScriptInstance& inst)
			{
				static const uint32 INDEX_HASH = crc32("__index");
				static const uint32 THIS_HASH = crc32("this");
				lua_State* L = inst.m_state;
				bool is_env_valid = lua_rawgeti(L, LUA_REGISTRYINDEX, inst.m_environment) == LUA_TTABLE;
				ASSERT(is_env_valid);
				lua_pushnil(L);
				auto& allocator = m_scene.m_system.getAllocator();
				while (lua_next(L, -2))
				{
					if (lua_type(L, -1) != LUA_TFUNCTION)
					{
						const char* name = lua_tostring(L, -2);
						uint32 hash = crc32(name);
						m_scene.m_property_names.insert(hash, string(name, allocator));
						if (hash != INDEX_HASH && hash != THIS_HASH)
						{
							auto* existing_prop = getProperty(inst, hash);
							if (existing_prop)
							{
								if (existing_prop->type == Property::ANY)
								{
									switch (lua_type(inst.m_state, -1))
									{
										case LUA_TBOOLEAN: existing_prop->type = Property::BOOLEAN; break;
										default: existing_prop->type = Property::FLOAT;
									}
								}
								m_scene.applyProperty(inst, *existing_prop, existing_prop->stored_value.c_str());
							}
							else
							{
								auto& prop = inst.m_properties.emplace(allocator);
								switch (lua_type(inst.m_state, -1))
								{
									case LUA_TBOOLEAN: prop.type = Property::BOOLEAN; break;
									default: prop.type = Property::FLOAT;
								}
								prop.name_hash = hash;
							}
						}
					}
					lua_pop(L, 1);
				}
			}


			void onScriptLoaded(Resource::State, Resource::State)
			{
				lua_State* L = m_scene.m_system.m_engine.getState();
				for (auto& script : m_scripts)
				{
					if ((!script.m_script || !script.m_script->isReady()) && script.m_state)
					{
						m_scene.destroy(*this, script);
						continue;
					}

					if (!script.m_script) continue;
					if (!script.m_script->isReady()) continue;
					if (script.m_state) continue;

					script.m_environment = -1;

					script.m_state = lua_newthread(L);
					lua_pushvalue(L, -1);
					script.m_thread_ref = luaL_ref(L, LUA_REGISTRYINDEX);
					lua_pop(L, 1);
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
					lua_pushinteger(script.m_state, m_entity);
					lua_setfield(script.m_state, -2, "this");

					lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment);
					bool errors = luaL_loadbuffer(script.m_state,
						script.m_script->getSourceCode(),
						stringLength(script.m_script->getSourceCode()),
						script.m_script->getPath().c_str()) != LUA_OK;

					if (errors)
					{
						g_log_error.log("Lua Script") << script.m_script->getPath() << ": "
							<< lua_tostring(script.m_state, -1);
						lua_pop(script.m_state, 1);
						continue;
					}

					lua_pushvalue(script.m_state, -2);
					lua_setupvalue(script.m_state, -2, 1); // function's environment

					m_scene.m_current_script_instance = &script;
					errors = errors || lua_pcall(script.m_state, 0, 0, 0) != LUA_OK;
					if (errors)
					{
						g_log_error.log("Lua Script") << script.m_script->getPath() << ": "
							<< lua_tostring(script.m_state, -1);
						lua_pop(script.m_state, 1);
					}
					lua_pop(script.m_state, 1);

					detectProperties(script);

					if (m_scene.m_is_game_running) m_scene.startScript(script);
				}
			}


			Array<ScriptInstance> m_scripts;
			LuaScriptSceneImpl& m_scene;
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


			void add(void* parameter) override
			{
				lua_pushlightuserdata(state, parameter);
				++parameter_count;
			}


			void addEnvironment(int env) override
			{
				bool is_valid = lua_rawgeti(state, LUA_REGISTRYINDEX, env) == LUA_TTABLE;
				ASSERT(is_valid);
				++parameter_count;
			}


			int parameter_count;
			lua_State* state;
			bool is_in_progress;
			ComponentIndex cmp;
			int scr_index;
		};


	public:
		LuaScriptSceneImpl(LuaScriptSystemImpl& system, Universe& ctx)
			: m_system(system)
			, m_universe(ctx)
			, m_scripts(system.getAllocator())
			, m_updates(system.getAllocator())
			, m_entity_script_map(system.getAllocator())
			, m_property_names(system.getAllocator())
			, m_is_game_running(false)
			, m_is_api_registered(false)
		{
			m_function_call.is_in_progress = false;
			
			registerAPI();
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

			auto& script = m_scripts[cmp]->m_scripts[scr_index];
			if (!script.m_state) return nullptr;

			bool is_env_valid = lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment) == LUA_TTABLE;
			ASSERT(is_env_valid);
			if (lua_getfield(script.m_state, -1, function) != LUA_TFUNCTION)
			{
				lua_pop(script.m_state, 2);
				return nullptr;
			}

			m_function_call.state = script.m_state;
			m_function_call.cmp = cmp;
			m_function_call.is_in_progress = true;
			m_function_call.parameter_count = 0;
			m_function_call.scr_index = scr_index;

			return &m_function_call;
		}


		void endFunctionCall(IFunctionCall& caller) override
		{
			ASSERT(&caller == &m_function_call);
			ASSERT(m_function_call.is_in_progress);

			m_function_call.is_in_progress = false;

			auto& script = m_scripts[m_function_call.cmp]->m_scripts[m_function_call.scr_index];
			if (!script.m_state) return;

			if (lua_pcall(script.m_state, m_function_call.parameter_count, 0, 0) != LUA_OK)
			{
				g_log_warning.log("Lua Script") << lua_tostring(script.m_state, -1);
				lua_pop(script.m_state, 1);
			}
			lua_pop(script.m_state, 1);
		}


		int getPropertyCount(ComponentIndex cmp, int scr_index) override
		{
			return m_scripts[cmp]->m_scripts[scr_index].m_properties.size();
		}


		const char* getPropertyName(ComponentIndex cmp, int scr_index, int prop_index) override
		{
			return getPropertyName(m_scripts[cmp]->m_scripts[scr_index].m_properties[prop_index].name_hash);
		}


		Property::Type getPropertyType(ComponentIndex cmp, int scr_index, int prop_index) override
		{
			return m_scripts[cmp]->m_scripts[scr_index].m_properties[prop_index].type;
		}


		~LuaScriptSceneImpl()
		{
			unloadAllScripts();
		}


		void unloadAllScripts()
		{
			Path invalid_path;
			for (int i = 0; i < m_scripts.size(); ++i)
			{
				if (!m_scripts[i]) continue;

				for (auto script : m_scripts[i]->m_scripts)
				{
					setScriptPath(*m_scripts[i], script, invalid_path);
				}
				LUMIX_DELETE(m_system.getAllocator(), m_scripts[i]);
			}
			m_entity_script_map.clear();
			m_scripts.clear();
		}


		lua_State* getState(ComponentIndex cmp, int scr_index) override
		{
			return m_scripts[cmp]->m_scripts[scr_index].m_state;
		}


		Universe& getUniverse() override { return m_universe; }


		static void setPropertyType(lua_State* L, const char* prop_name, int type)
		{
			int tmp = lua_getglobal(L, "g_scene_lua_script");
			ASSERT(tmp == LUA_TLIGHTUSERDATA);
			auto* scene = LuaWrapper::toType<LuaScriptSceneImpl*>(L, -1);
			uint32 prop_name_hash = crc32(prop_name);
			for (auto& prop : scene->m_current_script_instance->m_properties)
			{
				if (prop.name_hash == prop_name_hash)
				{
					prop.type = (Property::Type)type;
					lua_pop(L, -1);
					return;
				}
			}

			auto& prop = scene->m_current_script_instance->m_properties.emplace(scene->m_system.getAllocator());
			prop.name_hash = prop_name_hash;
			prop.type = (Property::Type)type;
			scene->m_property_names.insert(prop_name_hash, string(prop_name, scene->m_system.getAllocator()));
		}


		void registerPropertyAPI()
		{
			lua_State* L = m_system.m_engine.getState();
			auto f = &LuaWrapper::wrap<decltype(&setPropertyType), &setPropertyType>;
			LuaWrapper::createSystemFunction(L, "Editor", "setPropertyType", f);
			LuaWrapper::createSystemVariable(L, "Editor", "BOOLEAN_PROPERTY", Property::BOOLEAN);
			LuaWrapper::createSystemVariable(L, "Editor", "FLOAT_PROPERTY", Property::FLOAT);
			LuaWrapper::createSystemVariable(L, "Editor", "ENTITY_PROPERTY", Property::ENTITY);
		}


		static int getEnvironment(lua_State* L)
		{
			auto* scene = LuaWrapper::checkArg<LuaScriptScene*>(L, 1);
			Entity entity = LuaWrapper::checkArg<Entity>(L, 2);
			int scr_index = LuaWrapper::checkArg<int>(L, 3);

			ComponentIndex cmp = scene->getComponent(entity);
			if (cmp == INVALID_COMPONENT)
			{
				lua_pushnil(L);
			}
			else
			{
				int env = scene->getEnvironment(cmp, scr_index);
				if (env < 0)
				{
					lua_pushnil(L);
				}
				else
				{
					bool is_valid = lua_rawgeti(L, LUA_REGISTRYINDEX, env) == LUA_TTABLE;
					ASSERT(is_valid);
				}
			}
			return 1;
		}


		static int LUA_getProperty(lua_State* L)
		{
			auto* desc = LuaWrapper::toType<IPropertyDescriptor*>(L, lua_upvalueindex(1));
			auto type = LuaWrapper::toType<uint32>(L, lua_upvalueindex(2));
			ComponentUID cmp;
			cmp.scene = LuaWrapper::checkArg<IScene*>(L, 1);
			cmp.index = LuaWrapper::checkArg<ComponentIndex>(L, 2);
			cmp.type = type;
			cmp.entity = INVALID_ENTITY;
			switch (desc->getType())
			{
				case IPropertyDescriptor::DECIMAL:
				{
					float v;
					OutputBlob blob(&v, sizeof(v));
					desc->get(cmp, -1, blob);
					LuaWrapper::pushLua(L, v);
				}
				break;
				case IPropertyDescriptor::BOOL:
				{
					bool v;
					OutputBlob blob(&v, sizeof(v));
					desc->get(cmp, -1, blob);
					LuaWrapper::pushLua(L, v);
				}
				break;
				case IPropertyDescriptor::INTEGER:
				{
					int v;
					OutputBlob blob(&v, sizeof(v));
					desc->get(cmp, -1, blob);
					LuaWrapper::pushLua(L, v);
				}
				break;
				case IPropertyDescriptor::RESOURCE:
				case IPropertyDescriptor::FILE:
				case IPropertyDescriptor::STRING:
				{
					char buf[1024];
					OutputBlob blob(buf, sizeof(buf));
					desc->get(cmp, -1, blob);
					LuaWrapper::pushLua(L, buf);
				}
				break;
				case IPropertyDescriptor::COLOR:
				case IPropertyDescriptor::VEC3:
				{
					Vec3 v;
					OutputBlob blob(&v, sizeof(v));
					desc->get(cmp, -1, blob);
					LuaWrapper::pushLua(L, v);
				}
				break;
				case IPropertyDescriptor::VEC2:
				{
					Vec2 v;
					OutputBlob blob(&v, sizeof(v));
					desc->get(cmp, -1, blob);
					LuaWrapper::pushLua(L, v);
				}
				break;
				case IPropertyDescriptor::INT2:
				{
					Int2 v;
					OutputBlob blob(&v, sizeof(v));
					desc->get(cmp, -1, blob);
					LuaWrapper::pushLua(L, v);
				}
				break;
				default: luaL_argerror(L, 1, "Unsupported property type"); break;
			}
			return 1;
		}


		static int LUA_setProperty(lua_State* L)
		{
			auto* desc = LuaWrapper::toType<IPropertyDescriptor*>(L, lua_upvalueindex(1));
			auto type = LuaWrapper::toType<uint32>(L, lua_upvalueindex(2));
			ComponentUID cmp;
			cmp.scene = LuaWrapper::checkArg<IScene*>(L, 1);
			cmp.index = LuaWrapper::checkArg<ComponentIndex>(L, 2);
			cmp.type = type;
			cmp.entity = INVALID_ENTITY;
			switch(desc->getType())
			{
				case IPropertyDescriptor::DECIMAL:
				{
					auto v = LuaWrapper::checkArg<float>(L, 3);
					InputBlob blob(&v, sizeof(v));
					desc->set(cmp, -1, blob);
				}
				break;
				case IPropertyDescriptor::INTEGER:
				{
					auto v = LuaWrapper::checkArg<int>(L, 3);
					InputBlob blob(&v, sizeof(v));
					desc->set(cmp, -1, blob);
				}
				break;
				case IPropertyDescriptor::BOOL:
				{
					auto v = LuaWrapper::checkArg<bool>(L, 3);
					InputBlob blob(&v, sizeof(v));
					desc->set(cmp, -1, blob);
				}
				break;
				case IPropertyDescriptor::RESOURCE:
				case IPropertyDescriptor::FILE:
				case IPropertyDescriptor::STRING:
				{
					auto* v = LuaWrapper::checkArg<const char*>(L, 3);
					InputBlob blob(v, stringLength(v) + 1);
					desc->set(cmp, -1, blob);
				}
				break;
				case IPropertyDescriptor::COLOR:
				case IPropertyDescriptor::VEC3:
				{
					auto v = LuaWrapper::checkArg<Vec3>(L, 3);
					InputBlob blob(&v, sizeof(v));
					desc->set(cmp, -1, blob);
				}
				break;
				case IPropertyDescriptor::VEC2:
				{
					auto v = LuaWrapper::checkArg<Vec2>(L, 3);
					InputBlob blob(&v, sizeof(v));
					desc->set(cmp, -1, blob);
				}
				break;
				case IPropertyDescriptor::INT2:
				{
					auto v = LuaWrapper::checkArg<Int2>(L, 3);
					InputBlob blob(&v, sizeof(v));
					desc->set(cmp, -1, blob);
				}
				break;
				default: luaL_argerror(L, 1, "Unsupported property type"); break;
			}
			return 0;
		}

		
		static void convertPropertyToLuaName(const char* src, char* out, int max_size)
		{
			ASSERT(max_size > 0);
			bool to_upper = true;
			char* dest = out;
			while (*src && dest - out < max_size - 1)
			{
				if (isLetter(*src))
				{
					*dest = to_upper && !isUpperCase(*src) ? *src - 'a' + 'A' : *src;
					to_upper = false;
					++dest;
				}
				else
				{
					to_upper = true;
				}
				++src;
			}
			*dest = 0;
		}


		void registerProperties()
		{
			int cmps_count = PropertyRegister::getComponentTypesCount();
			lua_State* L = m_system.m_engine.getState();
			for (int i = 0; i < cmps_count; ++i)
			{
				const char* cmp_name = PropertyRegister::getComponentTypeID(i);
				lua_newtable(L);
				lua_pushvalue(L, -1);
				char tmp[50];
				convertPropertyToLuaName(cmp_name, tmp, lengthOf(tmp));
				lua_setglobal(L, tmp);

				uint32 cmp_name_hash = crc32(cmp_name);
				auto& descs = PropertyRegister::getDescriptors(cmp_name_hash);
				char setter[50];
				char getter[50];
				for (auto* desc : descs)
				{
					switch (desc->getType())
					{
						case IPropertyDescriptor::DECIMAL:
						case IPropertyDescriptor::INTEGER:
						case IPropertyDescriptor::BOOL:
						case IPropertyDescriptor::VEC3:
						case IPropertyDescriptor::VEC2:
						case IPropertyDescriptor::INT2:
						case IPropertyDescriptor::COLOR:
						case IPropertyDescriptor::RESOURCE:
						case IPropertyDescriptor::FILE:
						case IPropertyDescriptor::STRING:
							convertPropertyToLuaName(desc->getName(), tmp, lengthOf(tmp));
							copyString(setter, "set");
							copyString(getter, "get");
							catString(setter, tmp);
							catString(getter, tmp);
							lua_pushlightuserdata(L, desc);
							lua_pushinteger(L, cmp_name_hash);
							lua_pushcclosure(L, &LUA_setProperty, 2);
							lua_setfield(L, -2, setter);

							lua_pushlightuserdata(L, desc);
							lua_pushinteger(L, cmp_name_hash);
							lua_pushcclosure(L, &LUA_getProperty, 2);
							lua_setfield(L, -2, getter);
							break;
						default: break;
					}
				}
				lua_pop(L, 1);
			}
		}


		LuaScript* preloadScript(const char* path)
		{
			auto* script_manager = m_system.m_engine.getResourceManager().get(LUA_SCRIPT_HASH);
			return static_cast<LuaScript*>(script_manager->load(Path(path)));
		}


		void unloadScript(LuaScript* script)
		{
			if (!script) return;
			script->getResourceManager().get(LUA_SCRIPT_HASH)->unload(*script);
		}


		void setScriptSource(ComponentIndex cmp, int scr_index, const char* path)
		{
			setScriptPath(cmp, scr_index, Lumix::Path(path));
		}


		void registerAPI()
		{
			if (m_is_api_registered) return;

			m_is_api_registered = true;

			lua_State* engine_state = m_system.m_engine.getState();
			
			lua_pushlightuserdata(engine_state, &m_universe);
			lua_setglobal(engine_state, "g_universe");
			registerProperties();
			registerPropertyAPI();
			LuaWrapper::createSystemFunction(
				engine_state, "LuaScript", "getEnvironment", &LuaScriptSceneImpl::getEnvironment);
			
			#define REGISTER_FUNCTION(F) \
				do { \
					auto f = &LuaWrapper::wrapMethod<LuaScriptSceneImpl, \
						decltype(&LuaScriptSceneImpl::F), \
						&LuaScriptSceneImpl::F>; \
					LuaWrapper::createSystemFunction(engine_state, "LuaScript", #F, f); \
				} while(false)

			REGISTER_FUNCTION(addScript);
			REGISTER_FUNCTION(setScriptSource);
			REGISTER_FUNCTION(preloadScript);
			REGISTER_FUNCTION(unloadScript);

			#undef REGISTER_FUNCTION
		}


		int getEnvironment(ComponentIndex cmp, int scr_index) override
		{
			return m_scripts[cmp]->m_scripts[scr_index].m_environment;
		}


		const char* getPropertyName(uint32 name_hash) const
		{
			int idx = m_property_names.find(name_hash);
			if(idx >= 0) return m_property_names.at(idx).c_str();
			return nullptr;
		}


		void applyProperty(ScriptInstance& script, Property& prop, const char* value)
		{
			if (!value) return;
			lua_State* state = script.m_state;
			const char* name = getPropertyName(prop.name_hash);
			if (!name) return;

			char tmp[1024];
			copyString(tmp, name);
			catString(tmp, " = ");
			catString(tmp, value);

			bool errors =
				luaL_loadbuffer(state, tmp, stringLength(tmp), nullptr) != LUA_OK;
			if (errors)
			{
				g_log_error.log("Lua Script") << script.m_script->getPath() << ": "
					<< lua_tostring(state, -1);
				lua_pop(state, 1);
				return;
			}

			bool is_env_valid = lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment) == LUA_TTABLE;
			ASSERT(is_env_valid);
			lua_setupvalue(script.m_state, -2, 1);

			errors = errors || lua_pcall(state, 0, 0, 0) != LUA_OK;

			if (errors)
			{
				g_log_error.log("Lua Script") << script.m_script->getPath() << ": "
					<< lua_tostring(state, -1);
				lua_pop(state, 1);
			}
		}


		void setPropertyValue(Lumix::ComponentIndex cmp,
			int scr_index,
			const char* name,
			const char* value) override
		{
			if (!m_scripts[cmp]) return;
			if(!m_scripts[cmp]->m_scripts[scr_index].m_state) return;

			Property& prop = getScriptProperty(cmp, scr_index, name);
			applyProperty(m_scripts[cmp]->m_scripts[scr_index], prop, value);
		}


		const char* getPropertyName(Lumix::ComponentIndex cmp, int scr_index, int index) const
		{
			auto& script = m_scripts[cmp]->m_scripts[scr_index];

			return getPropertyName(script.m_properties[index].name_hash);
		}


		int getPropertyCount(Lumix::ComponentIndex cmp, int scr_index) const
		{
			auto& script = m_scripts[cmp]->m_scripts[scr_index];

			return script.m_properties.size();
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
			copyMemory(new_mem, ptr, Math::minimum(osize, nsize));
			allocator.deallocate(ptr);
			return new_mem;
		}


		void destroy(ScriptComponent& scr,  ScriptInstance& inst)
		{
			bool is_env_valid = lua_rawgeti(inst.m_state, LUA_REGISTRYINDEX, inst.m_environment) == LUA_TTABLE;
			ASSERT(is_env_valid);
			if (lua_getfield(inst.m_state, -1, "onDestroy") != LUA_TFUNCTION)
			{
				lua_pop(inst.m_state, 2);
			}
			else
			{
				if (lua_pcall(inst.m_state, 0, 0, 0) != LUA_OK)
				{
					g_log_error.log("Lua Script") << lua_tostring(inst.m_state, -1);
					lua_pop(inst.m_state, 1);
				}
				lua_pop(inst.m_state, 1);
			}

			for(int i = 0; i < m_updates.size(); ++i)
			{
				if(m_updates[i].environment == inst.m_environment)
				{
					m_updates.eraseFast(i);
					break;
				}
			}

			inst.m_script->getObserverCb().unbind<ScriptComponent, &ScriptComponent::onScriptLoaded>(&scr);

			luaL_unref(inst.m_state, LUA_REGISTRYINDEX, inst.m_thread_ref);
			luaL_unref(inst.m_state, LUA_REGISTRYINDEX, inst.m_environment);
			inst.m_state = nullptr;
		}


		void setScriptPath(ScriptComponent& cmp, ScriptInstance& inst, const Path& path)
		{
			registerAPI();

			if (inst.m_script)
			{
				if (inst.m_state)
				{
					destroy(cmp, inst);
				}
				inst.m_state = nullptr;
				inst.m_properties.clear();
				auto& cb = inst.m_script->getObserverCb();
				cb.unbind<ScriptComponent, &ScriptComponent::onScriptLoaded>(&cmp);
				m_system.getScriptManager().unload(*inst.m_script);
			}
			inst.m_script = path.isValid() ? static_cast<LuaScript*>(m_system.getScriptManager().load(path)) : nullptr;
			if (inst.m_script)
			{
				inst.m_script->onLoaded<ScriptComponent, &ScriptComponent::onScriptLoaded>(&cmp);
			}
		}


		void startScript(ScriptInstance& instance)
		{
			if (lua_rawgeti(instance.m_state, LUA_REGISTRYINDEX, instance.m_environment) != LUA_TTABLE)
			{
				ASSERT(false);
				lua_pop(instance.m_state, 1);
				return;
			}
			if (lua_getfield(instance.m_state, -1, "update") == LUA_TFUNCTION)
			{
				auto& update_data = m_updates.emplace();
				update_data.script = instance.m_script;
				update_data.state = instance.m_state;
				update_data.environment = instance.m_environment;
			}
			lua_pop(instance.m_state, 1);

			if (lua_getfield(instance.m_state, -1, "init") != LUA_TFUNCTION)
			{
				lua_pop(instance.m_state, 2);
				return;
			}

			if (lua_pcall(instance.m_state, 0, 0, 0) != LUA_OK)
			{
				g_log_error.log("Lua Script") << lua_tostring(instance.m_state, -1);
				lua_pop(instance.m_state, 1);
			}
			lua_pop(instance.m_state, 1);
		}


		void startGame() override
		{
			m_is_game_running = true;
			for (int i = 0; i < m_scripts.size(); ++i)
			{
				auto* scr = m_scripts[i];
				if (!scr) continue;
				for (int j = 0; j < scr->m_scripts.size(); ++j)
				{
					auto& instance = scr->m_scripts[j];
					if (!instance.m_script) continue;
					if (!instance.m_script->isReady()) continue;

					startScript(instance);
				}
			}
		}


		void stopGame() override
		{
			m_is_game_running = false;
			m_updates.clear();
		}


		ComponentIndex createComponent(uint32 type, Entity entity) override
		{
			if (type != LUA_SCRIPT_HASH) return INVALID_COMPONENT;

			auto& allocator = m_system.getAllocator();
			ScriptComponent& script = *LUMIX_NEW(allocator, ScriptComponent)(*this, allocator);
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


		void destroyComponent(ComponentIndex component, uint32 type) override
		{
			if (type != LUA_SCRIPT_HASH) return;

			for (int i = 0, c = m_updates.size(); i < c; ++i)
			{
				if(m_updates[i].cmp == component) m_updates.erase(i);
			}
			for (auto& scr : m_scripts[component]->m_scripts)
			{
				if (scr.m_state) destroy(*m_scripts[component], scr);
				if (scr.m_script) m_system.getScriptManager().unload(*scr.m_script);
			}
			m_entity_script_map.erase(m_scripts[component]->m_entity);
			auto* script = m_scripts[component];
			m_scripts[component] = nullptr;
			m_universe.destroyComponent(script->m_entity, type, this, component);
			LUMIX_DELETE(m_system.getAllocator(), script);
		}


		void getPropertyValue(ComponentIndex cmp,
			int scr_index,
			const char* property_name,
			char* out,
			int max_size) override
		{
			ASSERT(max_size > 0);

			uint32 hash = crc32(property_name);
			auto& inst = m_scripts[cmp]->m_scripts[scr_index];
			if (inst.m_script->isReady())
			{
				for (auto& prop : inst.m_properties)
				{
					if (prop.name_hash == hash)
					{
						getProperty(prop, property_name, inst, out, max_size);
						return;
					}
				}
			}
			*out = '\0';
		}


		void getProperty(Property& prop, const char* prop_name, ScriptInstance& scr, char* out, int max_size)
		{
			if(max_size <= 0) return;

			*out = '\0';
			lua_rawgeti(scr.m_state, LUA_REGISTRYINDEX, scr.m_environment);
			auto type = lua_getfield(scr.m_state, -1, prop_name);
			switch (prop.type)
			{
				case Property::BOOLEAN:
				{
					bool b = lua_toboolean(scr.m_state, -1) != 0;
					copyString(out, max_size, b ? "true" : "false");
				}
				break;
				case Property::FLOAT:
				{
					float val = (float)lua_tonumber(scr.m_state, -1);
					toCString(val, out, max_size, 8);
				}
				break;
				case Property::ENTITY:
				{
					Entity val = (Entity)lua_tointeger(scr.m_state, -1);
					toCString(val, out, max_size);
				}
				break;
				default: ASSERT(false); break;
			}
			lua_pop(scr.m_state, 2);
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
					serializer.writeString(scr.m_script ? scr.m_script->getPath().c_str() : "");
					serializer.write(scr.m_properties.size());
					for (Property& prop : scr.m_properties)
					{
						serializer.write(prop.name_hash);
						int idx = m_property_names.find(prop.name_hash);
						if (idx >= 0)
						{
							const char* name = m_property_names.at(idx).c_str();
							char tmp[1024];
							getProperty(prop, name, scr, tmp, lengthOf(tmp));
							serializer.writeString(tmp);
						}
						else
						{
							serializer.writeString("");
						}
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

				auto& allocator = m_system.getAllocator();
				ScriptComponent& script = *LUMIX_NEW(allocator, ScriptComponent)(*this, allocator);
				m_scripts.push(&script);

				int scr_count;
				serializer.read(m_scripts[i]->m_entity);
				serializer.read(scr_count);
				m_entity_script_map.insert(m_scripts[i]->m_entity, i);
				for (int j = 0; j < scr_count; ++j)
				{
					auto& scr = script.m_scripts.emplace(allocator);

					char tmp[MAX_PATH_LENGTH];
					serializer.readString(tmp, MAX_PATH_LENGTH);
					scr.m_state = nullptr;
					int prop_count;
					serializer.read(prop_count);
					scr.m_properties.reserve(prop_count);
					for (int j = 0; j < prop_count; ++j)
					{
						Property& prop = scr.m_properties.emplace(allocator);
						prop.type = Property::ANY;
						serializer.read(prop.name_hash);
						char tmp[1024];
						tmp[0] = 0;
						serializer.readString(tmp, sizeof(tmp));
						prop.stored_value = tmp;
					}
					setScriptPath(*m_scripts[i], scr, Path(tmp));
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

				ScriptComponent& script = *LUMIX_NEW(m_system.getAllocator(), ScriptComponent)(*this, m_system.getAllocator());
				m_scripts.push(&script);
				serializer.read(m_scripts[i]->m_entity);
				m_entity_script_map.insert(m_scripts[i]->m_entity, i);
				char tmp[MAX_PATH_LENGTH];
				serializer.readString(tmp, MAX_PATH_LENGTH);
				auto& scr = script.m_scripts.emplace(m_system.m_allocator);
				scr.m_state = nullptr;
				int prop_count;
				serializer.read(prop_count);
				scr.m_properties.reserve(prop_count);
				for (int j = 0; j < prop_count; ++j)
				{
					Property& prop =
						scr.m_properties.emplace(m_system.getAllocator());
					prop.type = Property::ANY;
					serializer.read(prop.name_hash);
					char tmp[1024];
					tmp[0] = 0;
					serializer.readString(tmp, sizeof(tmp));
					prop.stored_value = tmp;
				}
				setScriptPath(script, scr, Path(tmp));
				m_universe.addComponent(m_scripts[i]->m_entity, LUA_SCRIPT_HASH, this, i);
			}
		}


		IPlugin& getPlugin() const override { return m_system; }


		void update(float time_delta, bool paused) override
		{
			if (paused) return;

			for (int i = 0, c = m_updates.size(); i < c; ++i)
			{
				auto& update_item = m_updates[i];
				if (lua_rawgeti(update_item.state, LUA_REGISTRYINDEX, update_item.environment) != LUA_TTABLE)
				{
					ASSERT(false);
				}
				if (lua_getfield(update_item.state, -1, "update") != LUA_TFUNCTION)
				{
					lua_pop(update_item.state, 2);
					continue;
				}

				lua_pushnumber(update_item.state, time_delta);
				if (lua_pcall(update_item.state, 1, 0, 0) != LUA_OK)
				{
					g_log_error.log("Lua Script") << lua_tostring(update_item.state, -1);
					lua_pop(update_item.state, 1);
				}
				lua_pop(update_item.state, 1);
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
				if (prop.name_hash == name_hash)
				{
					return prop;
				}
			}

			m_scripts[cmp]->m_scripts[scr_index].m_properties.emplace(m_system.getAllocator());
			auto& prop = m_scripts[cmp]->m_scripts[scr_index].m_properties.back();
			prop.name_hash = name_hash;
			return prop;
		}


		Path getScriptPath(ComponentIndex cmp, int scr_index) override
		{
			return m_scripts[cmp]->m_scripts[scr_index].m_script ? m_scripts[cmp]->m_scripts[scr_index].m_script->getPath() : Path("");
		}


		void setScriptPath(ComponentIndex cmp, int scr_index, const Path& path) override
		{
			if (!m_scripts[cmp]) return;
			if (m_scripts[cmp]->m_scripts.size() <= scr_index) return;
			setScriptPath(*m_scripts[cmp], m_scripts[cmp]->m_scripts[scr_index], path);
		}


		int getScriptCount(ComponentIndex cmp) override
		{
			return m_scripts[cmp]->m_scripts.size();
		}


		void insertScript(ComponentIndex cmp, int idx) override
		{
			m_scripts[cmp]->m_scripts.emplaceAt(idx, m_system.m_allocator);
		}


		int addScript(ComponentIndex cmp) override
		{
			m_scripts[cmp]->m_scripts.emplace(m_system.m_allocator);
			return m_scripts[cmp]->m_scripts.size() - 1;
		}


		void removeScript(ComponentIndex cmp, int scr_index) override
		{
			setScriptPath(cmp, scr_index, Path());
			m_scripts[cmp]->m_scripts.eraseFast(scr_index);
		}


		void serializeScript(ComponentIndex cmp, int scr_index, OutputBlob& blob) override
		{
			auto& scr = m_scripts[cmp]->m_scripts[scr_index];
			blob.writeString(scr.m_script ? scr.m_script->getPath().c_str() : "");
			blob.write(scr.m_properties.size());
			for (auto prop : scr.m_properties)
			{
				blob.write(prop.name_hash);
				char tmp[1024];
				const char* property_name = getPropertyName(prop.name_hash);
				if (!property_name)
				{
					blob.writeString(prop.stored_value.c_str());
				}
				else
				{
					getProperty(prop, property_name, scr, tmp, lengthOf(tmp));
					blob.writeString(tmp);
				}
			}
		}


		void deserializeScript(ComponentIndex cmp, int scr_index, InputBlob& blob) override
		{
			auto& scr = m_scripts[cmp]->m_scripts[scr_index];
			int count;
			char path[MAX_PATH_LENGTH];
			blob.readString(path, lengthOf(path));
			blob.read(count);
			scr.m_environment = -1;
			scr.m_properties.clear();
			char buf[256];
			for (int i = 0; i < count; ++i)
			{
				auto& prop = scr.m_properties.emplace(m_system.m_allocator);
				prop.type = Property::ANY;
				blob.read(prop.name_hash);
				blob.readString(buf, lengthOf(buf));
				prop.stored_value = buf;
			}
			setScriptPath(cmp, scr_index, Path(path));
		}


		LuaScriptSystemImpl& m_system;
		Array<ScriptComponent*> m_scripts;
		HashMap<Entity, ComponentIndex> m_entity_script_map;
		AssociativeArray<uint32, string> m_property_names;
		Universe& m_universe;
		Array<UpdateData> m_updates;
		FunctionCall m_function_call;
		ScriptInstance* m_current_script_instance;
		bool m_is_api_registered;
		bool m_is_game_running;
	};


	LuaScriptSystemImpl::LuaScriptSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_script_manager(m_allocator)
	{
		m_script_manager.create(crc32("lua_script"), engine.getResourceManager());

		PropertyRegister::registerComponentType("lua_script");
	}


	LuaScriptSystemImpl::~LuaScriptSystemImpl()
	{
		m_script_manager.destroy();
	}


	IAllocator& LuaScriptSystemImpl::getAllocator()
	{
		return m_allocator;
	}


	IScene* LuaScriptSystemImpl::createScene(Universe& ctx)
	{
		return LUMIX_NEW(m_allocator, LuaScriptSceneImpl)(*this, ctx);
	}


	void LuaScriptSystemImpl::destroyScene(IScene* scene)
	{
		LUMIX_DELETE(m_allocator, scene);
	}


	bool LuaScriptSystemImpl::create()
	{
		return true;
	}


	void LuaScriptSystemImpl::destroy()
	{
	}


	const char* LuaScriptSystemImpl::getName() const
	{
		return "lua_script";
	}


	LUMIX_PLUGIN_ENTRY(lua_script)
	{
		return LUMIX_NEW(engine.getAllocator(), LuaScriptSystemImpl)(engine);
	}
}
