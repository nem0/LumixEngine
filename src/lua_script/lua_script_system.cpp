#include "lua_script_system.h"
#include "engine/array.h"
#include "engine/base_proxy_allocator.h"
#include "engine/binary_array.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/debug/debug.h"
#include "engine/engine.h"
#include "engine/fs/file_system.h"
#include "engine/iallocator.h"
#include "engine/iplugin.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/profiler.h"
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
#include "engine/resource_manager.h"
#include "engine/universe/universe.h"
#include "lua_script/lua_script_manager.h"


namespace Lumix
{


	enum class LuaScriptVersion
	{
		MULTIPLE_SCRIPTS,
		REFACTOR,

		LATEST
	};
	
	
	static const ComponentType LUA_SCRIPT_TYPE = PropertyRegister::getComponentType("lua_script");
	static const ResourceType LUA_SCRIPT_RESOURCE_TYPE("lua_script");


	class LuaScriptSystemImpl : public IPlugin
	{
	public:
		explicit LuaScriptSystemImpl(Engine& engine);
		virtual ~LuaScriptSystemImpl();

		IScene* createScene(Universe& universe) override;
		void destroyScene(IScene* scene) override;
		const char* getName() const override { return "lua_script"; }
		LuaScriptManager& getScriptManager() { return m_script_manager; }

		Engine& m_engine;
		Debug::Allocator m_allocator;
		LuaScriptManager m_script_manager;
	};


	struct LuaScriptSceneImpl : public LuaScriptScene
	{
		struct TimerData
		{
			float time;
			lua_State* state;
			int func;
		};

		struct UpdateData
		{
			LuaScript* script;
			lua_State* state;
			int environment;
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


			static int getProperty(ScriptInstance& inst, uint32 hash)
			{
				for(int i = 0, c = inst.m_properties.size(); i < c; ++i)
				{
					if (inst.m_properties[i].name_hash == hash) return i;
				}
				return -1;
			}


			void detectProperties(ScriptInstance& inst)
			{
				static const uint32 INDEX_HASH = crc32("__index");
				static const uint32 THIS_HASH = crc32("this");
				lua_State* L = inst.m_state;
				bool is_env_valid = lua_rawgeti(L, LUA_REGISTRYINDEX, inst.m_environment) == LUA_TTABLE;
				ASSERT(is_env_valid);
				lua_pushnil(L);
				auto& allocator = m_scene.m_system.m_allocator;
				BinaryArray valid_properties(m_scene.m_system.m_engine.getLIFOAllocator());
				valid_properties.resize(inst.m_properties.size());
				setMemory(valid_properties.getRaw(), 0, valid_properties.size() >> 3);

				while (lua_next(L, -2))
				{
					if (lua_type(L, -1) != LUA_TFUNCTION)
					{
						const char* name = lua_tostring(L, -2);
						if(name[0] != '_')
						{
							uint32 hash = crc32(name);
							if (m_scene.m_property_names.find(hash) < 0)
							{
								m_scene.m_property_names.emplace(hash, name, allocator);
							}
							if (hash != INDEX_HASH && hash != THIS_HASH)
							{
								int prop_index = getProperty(inst, hash);
								if (prop_index >= 0)
								{
									valid_properties[prop_index] = true;
									Property& existing_prop = inst.m_properties[prop_index];
									if (existing_prop.type == Property::ANY)
									{
										switch (lua_type(inst.m_state, -1))
										{
										case LUA_TBOOLEAN: existing_prop.type = Property::BOOLEAN; break;
										default: existing_prop.type = Property::FLOAT;
										}
									}
									m_scene.applyProperty(inst, existing_prop, existing_prop.stored_value.c_str());
								}
								else
								{
									auto& prop = inst.m_properties.emplace(allocator);
									valid_properties.push(true);
									switch (lua_type(inst.m_state, -1))
									{
									case LUA_TBOOLEAN: prop.type = Property::BOOLEAN; break;
									default: prop.type = Property::FLOAT;
									}
									prop.name_hash = hash;
								}
							}
						}
					}
					lua_pop(L, 1);
				}
				for (int i = inst.m_properties.size() - 1; i >= 0; --i)
				{
					if (valid_properties[i]) continue;
					inst.m_properties.eraseFast(i);
				}
			}


			void onScriptLoaded(Resource::State, Resource::State, Resource& resource)
			{
				lua_State* L = m_scene.m_system.m_engine.getState();
				for (auto& script : m_scripts)
				{
					if (!script.m_script) continue;
					if (!script.m_script->isReady()) continue;
					if (script.m_script != &resource) continue;

					bool is_reload = true;
					if (!script.m_state)
					{
						is_reload = false;
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
						lua_pushinteger(script.m_state, m_entity.index);
						lua_setfield(script.m_state, -2, "this");
					}

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

					if (m_scene.m_is_game_running) m_scene.startScript(script, is_reload);
				}
			}


			Array<ScriptInstance> m_scripts;
			LuaScriptSceneImpl& m_scene;
			Entity m_entity;
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
			ScriptComponent* cmp;
			int scr_index;
		};


	public:
		LuaScriptSceneImpl(LuaScriptSystemImpl& system, Universe& ctx)
			: m_system(system)
			, m_universe(ctx)
			, m_scripts(system.m_allocator)
			, m_updates(system.m_allocator)
			, m_timers(system.m_allocator)
			, m_property_names(system.m_allocator)
			, m_is_game_running(false)
			, m_is_api_registered(false)
		{
			m_function_call.is_in_progress = false;
			
			registerAPI();
			ctx.registerComponentTypeScene(LUA_SCRIPT_TYPE, this);
		}


		ComponentHandle getComponent(Entity entity) override
		{
			if (m_scripts.find(entity) == m_scripts.end()) return INVALID_COMPONENT;
			return {entity.index};
		}


		IFunctionCall* beginFunctionCall(ComponentHandle cmp, int scr_index, const char* function) override
		{
			ASSERT(!m_function_call.is_in_progress);

			auto* script_cmp = m_scripts[{cmp.index}];
			auto& script = script_cmp->m_scripts[scr_index];
			if (!script.m_state) return nullptr;

			bool is_env_valid = lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment) == LUA_TTABLE;
			ASSERT(is_env_valid);
			if (lua_getfield(script.m_state, -1, function) != LUA_TFUNCTION)
			{
				lua_pop(script.m_state, 2);
				return nullptr;
			}

			m_function_call.state = script.m_state;
			m_function_call.cmp = script_cmp;
			m_function_call.is_in_progress = true;
			m_function_call.parameter_count = 0;
			m_function_call.scr_index = scr_index;

			return &m_function_call;
		}


		void endFunctionCall() override
		{
			ASSERT(m_function_call.is_in_progress);

			m_function_call.is_in_progress = false;

			auto& script = m_function_call.cmp->m_scripts[m_function_call.scr_index];
			if (!script.m_state) return;

			if (lua_pcall(script.m_state, m_function_call.parameter_count, 0, 0) != LUA_OK)
			{
				g_log_warning.log("Lua Script") << lua_tostring(script.m_state, -1);
				lua_pop(script.m_state, 1);
			}
			lua_pop(script.m_state, 1);
		}


		int getPropertyCount(ComponentHandle cmp, int scr_index) override
		{
			return m_scripts[{cmp.index}]->m_scripts[scr_index].m_properties.size();
		}


		const char* getPropertyName(ComponentHandle cmp, int scr_index, int prop_index) override
		{
			return getPropertyName(m_scripts[{cmp.index}]->m_scripts[scr_index].m_properties[prop_index].name_hash);
		}


		ResourceType getPropertyResourceType(ComponentHandle cmp, int scr_index, int prop_index) override
		{
			return m_scripts[{cmp.index}]->m_scripts[scr_index].m_properties[prop_index].resource_type;
		}


		Property::Type getPropertyType(ComponentHandle cmp, int scr_index, int prop_index) override
		{
			return m_scripts[{cmp.index}]->m_scripts[scr_index].m_properties[prop_index].type;
		}


		void getScriptData(ComponentHandle cmp, OutputBlob& blob) override
		{
			auto* scr = m_scripts[{cmp.index}];
			blob.write(scr->m_scripts.size());
			for (int i = 0; i < scr->m_scripts.size(); ++i)
			{
				auto& inst = scr->m_scripts[i];
				blob.writeString(inst.m_script ? inst.m_script->getPath().c_str() : "");
				blob.write(inst.m_properties.size());
				for (auto& prop : inst.m_properties)
				{
					blob.write(prop.name_hash);
					blob.write(prop.type);
					char tmp[1024];
					tmp[0] = '\0';
					const char* prop_name = getPropertyName(prop.name_hash);
					if(prop_name) getPropertyValue(cmp, i, getPropertyName(prop.name_hash), tmp, lengthOf(tmp));
					blob.writeString(prop_name ? tmp : prop.stored_value.c_str());
				}
			}
		}


		void setScriptData(ComponentHandle cmp, InputBlob& blob) override
		{
			auto* scr = m_scripts[{cmp.index}];
			int count;
			blob.read(count);
			for (int i = 0; i < count; ++i)
			{
				int idx = addScript(cmp);
				auto& inst = scr->m_scripts[idx];
				char tmp[Lumix::MAX_PATH_LENGTH];
				blob.readString(tmp, lengthOf(tmp));
				setScriptPath(cmp, idx, Lumix::Path(tmp));
				
				int prop_count;
				blob.read(prop_count);
				for (int j = 0; j < prop_count; ++j)
				{
					uint32 hash;
					blob.read(hash);
					int prop_index = scr->getProperty(inst, hash);
					if (prop_index < 0)
					{
						scr->m_scripts[idx].m_properties.emplace(m_system.m_allocator);
						prop_index = scr->m_scripts[idx].m_properties.size() - 1;
					}
					auto& prop = scr->m_scripts[idx].m_properties[prop_index];
					prop.name_hash = hash;
					blob.read(prop.type);
					char tmp[1024];
					blob.readString(tmp, lengthOf(tmp));
					prop.stored_value = tmp;
					if (scr->m_scripts[idx].m_state) applyProperty(scr->m_scripts[idx], prop, tmp);
				}
			}
		}


		void clear() override
		{
			Path invalid_path;
			for (auto* script_cmp : m_scripts)
			{
				if (!script_cmp) continue;

				for (auto script : script_cmp->m_scripts)
				{
					setScriptPath(*script_cmp, script, invalid_path);
				}
				LUMIX_DELETE(m_system.m_allocator, script_cmp);
			}
			m_scripts.clear();
		}


		lua_State* getState(ComponentHandle cmp, int scr_index) override
		{
			return m_scripts[{cmp.index}]->m_scripts[scr_index].m_state;
		}


		Universe& getUniverse() override { return m_universe; }


		static int setPropertyType(lua_State* L)
		{
			const char* prop_name = LuaWrapper::checkArg<const char*>(L, 1);
			int type = LuaWrapper::checkArg<int>(L, 2);
			ResourceType resource_type;
			if (type == Property::Type::RESOURCE)
			{
				resource_type = ResourceType(LuaWrapper::checkArg<const char*>(L, 3));
			}
			int tmp = lua_getglobal(L, "g_scene_lua_script");
			ASSERT(tmp == LUA_TLIGHTUSERDATA);
			auto* scene = LuaWrapper::toType<LuaScriptSceneImpl*>(L, -1);
			uint32 prop_name_hash = crc32(prop_name);
			for (auto& prop : scene->m_current_script_instance->m_properties)
			{
				if (prop.name_hash == prop_name_hash)
				{
					prop.type = (Property::Type)type;
					prop.resource_type = resource_type;
					lua_pop(L, -1);
					return 0;
				}
			}

			auto& prop = scene->m_current_script_instance->m_properties.emplace(scene->m_system.m_allocator);
			prop.name_hash = prop_name_hash;
			prop.type = (Property::Type)type;
			prop.resource_type = resource_type;
			if (scene->m_property_names.find(prop_name_hash) < 0)
			{
				scene->m_property_names.emplace(prop_name_hash, prop_name, scene->m_system.m_allocator);
			}
			return 0;
		}


		void registerPropertyAPI()
		{
			lua_State* L = m_system.m_engine.getState();
			auto f = &LuaWrapper::wrap<decltype(&setPropertyType), &setPropertyType>;
			LuaWrapper::createSystemFunction(L, "Editor", "setPropertyType", f);
			LuaWrapper::createSystemVariable(L, "Editor", "BOOLEAN_PROPERTY", Property::BOOLEAN);
			LuaWrapper::createSystemVariable(L, "Editor", "FLOAT_PROPERTY", Property::FLOAT);
			LuaWrapper::createSystemVariable(L, "Editor", "ENTITY_PROPERTY", Property::ENTITY);
			LuaWrapper::createSystemVariable(L, "Editor", "RESOURCE_PROPERTY", Property::RESOURCE);
		}


		static int getEnvironment(lua_State* L)
		{
			auto* scene = LuaWrapper::checkArg<LuaScriptScene*>(L, 1);
			Entity entity = LuaWrapper::checkArg<Entity>(L, 2);
			int scr_index = LuaWrapper::checkArg<int>(L, 3);

			ComponentHandle cmp = scene->getComponent(entity);
			if (cmp == INVALID_COMPONENT)
			{
				lua_pushnil(L);
				return 1;
			}
			int count = scene->getScriptCount(cmp);
			if (scr_index >= count)
			{
				lua_pushnil(L);
				return 1;
			}

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
			return 1;
		}


		static int LUA_getProperty(lua_State* L)
		{
			auto* desc = LuaWrapper::toType<IPropertyDescriptor*>(L, lua_upvalueindex(1));
			ComponentType type = { LuaWrapper::toType<int>(L, lua_upvalueindex(2)) };
			ComponentUID cmp;
			cmp.scene = LuaWrapper::checkArg<IScene*>(L, 1);
			cmp.handle = LuaWrapper::checkArg<ComponentHandle>(L, 2);
			cmp.type = type;
			cmp.entity = INVALID_ENTITY;
			switch (desc->getType())
			{
				case IPropertyDescriptor::DECIMAL:
				{
					float v;
					OutputBlob blob(&v, sizeof(v));
					desc->get(cmp, -1, blob);
					LuaWrapper::push(L, v);
				}
				break;
				case IPropertyDescriptor::BOOL:
				{
					bool v;
					OutputBlob blob(&v, sizeof(v));
					desc->get(cmp, -1, blob);
					LuaWrapper::push(L, v);
				}
				break;
				case IPropertyDescriptor::INTEGER:
				{
					int v;
					OutputBlob blob(&v, sizeof(v));
					desc->get(cmp, -1, blob);
					LuaWrapper::push(L, v);
				}
				break;
				case IPropertyDescriptor::RESOURCE:
				case IPropertyDescriptor::FILE:
				case IPropertyDescriptor::STRING:
				{
					char buf[1024];
					OutputBlob blob(buf, sizeof(buf));
					desc->get(cmp, -1, blob);
					LuaWrapper::push(L, buf);
				}
				break;
				case IPropertyDescriptor::COLOR:
				case IPropertyDescriptor::VEC3:
				{
					Vec3 v;
					OutputBlob blob(&v, sizeof(v));
					desc->get(cmp, -1, blob);
					LuaWrapper::push(L, v);
				}
				break;
				case IPropertyDescriptor::VEC2:
				{
					Vec2 v;
					OutputBlob blob(&v, sizeof(v));
					desc->get(cmp, -1, blob);
					LuaWrapper::push(L, v);
				}
				break;
				case IPropertyDescriptor::INT2:
				{
					Int2 v;
					OutputBlob blob(&v, sizeof(v));
					desc->get(cmp, -1, blob);
					LuaWrapper::push(L, v);
				}
				break;
				default: luaL_argerror(L, 1, "Unsupported property type"); break;
			}
			return 1;
		}


		static int LUA_setProperty(lua_State* L)
		{
			auto* desc = LuaWrapper::toType<IPropertyDescriptor*>(L, lua_upvalueindex(1));
			ComponentType type = { LuaWrapper::toType<int>(L, lua_upvalueindex(2)) };
			ComponentUID cmp;
			cmp.scene = LuaWrapper::checkArg<IScene*>(L, 1);
			cmp.handle = LuaWrapper::checkArg<ComponentHandle>(L, 2);
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

				ComponentType cmp_type = PropertyRegister::getComponentType(cmp_name);
				auto& descs = PropertyRegister::getDescriptors(cmp_type);
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
							lua_pushinteger(L, cmp_type.index);
							lua_pushcclosure(L, &LUA_setProperty, 2);
							lua_setfield(L, -2, setter);

							lua_pushlightuserdata(L, desc);
							lua_pushinteger(L, cmp_type.index);
							lua_pushcclosure(L, &LUA_getProperty, 2);
							lua_setfield(L, -2, getter);
							break;
						default: break;
					}
				}
				lua_pop(L, 1);
			}
		}



		void cancelTimer(int timer_func)
		{
			for (int i = 0, c = m_timers.size(); i < c; ++i)
			{
				if (m_timers[i].func == timer_func)
				{
					m_timers.eraseFast(i);
					break;
				}
			}
		}


		static int setTimer(lua_State* L)
		{
			auto* scene = LuaWrapper::checkArg<LuaScriptSceneImpl*>(L, 1);
			float time = LuaWrapper::checkArg<float>(L, 2);
			if (!lua_isfunction(L, 3)) LuaWrapper::argError(L, 3, "function");
			TimerData& timer = scene->m_timers.emplace();
			timer.time = time;
			timer.state = L;
			lua_pushvalue(L, 3);
			timer.func = luaL_ref(L, LUA_REGISTRYINDEX);
			lua_pop(L, 1);
			LuaWrapper::push(L, timer.func);
			return 1;
		}


		LuaScript* preloadScript(const char* path)
		{
			auto* script_manager = m_system.m_engine.getResourceManager().get(LUA_SCRIPT_RESOURCE_TYPE);
			return static_cast<LuaScript*>(script_manager->load(Path(path)));
		}


		void unloadScript(LuaScript* script)
		{
			if (!script) return;
			script->getResourceManager().unload(*script);
		}


		void setScriptSource(ComponentHandle cmp, int scr_index, const char* path)
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
			REGISTER_FUNCTION(getScriptCount);
			REGISTER_FUNCTION(setScriptSource);
			REGISTER_FUNCTION(preloadScript);
			REGISTER_FUNCTION(unloadScript);
			REGISTER_FUNCTION(cancelTimer);

			#undef REGISTER_FUNCTION

			LuaWrapper::createSystemFunction(engine_state, "LuaScript", "setTimer", &LuaScriptSceneImpl::setTimer);
		}


		int getEnvironment(ComponentHandle cmp, int scr_index) override
		{
			return m_scripts[{cmp.index}]->m_scripts[scr_index].m_environment;
		}


		const char* getPropertyName(uint32 name_hash) const
		{
			int idx = m_property_names.find(name_hash);
			if(idx >= 0) return m_property_names.at(idx).c_str();
			return nullptr;
		}


		void applyResourceProperty(ScriptInstance& script, const char* name, Property& prop, const char* value)
		{
			bool is_env_valid = lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment) == LUA_TTABLE;
			ASSERT(is_env_valid);
			lua_getfield(script.m_state, -1, name);
			int res_idx = LuaWrapper::toType<int>(script.m_state, -1);
			m_system.m_engine.unloadLuaResource(res_idx);
			lua_pop(script.m_state, 1);

			int new_res = m_system.m_engine.addLuaResource(Path(value), prop.resource_type);
			lua_pushinteger(script.m_state, new_res);
			lua_setfield(script.m_state, -2, name);
			lua_pop(script.m_state, 1);
		}


		void applyProperty(ScriptInstance& script, Property& prop, const char* value)
		{
			if (!value) return;
			lua_State* state = script.m_state;
			const char* name = getPropertyName(prop.name_hash);
			if (!name) return;

			if (prop.type == Property::RESOURCE)
			{
				applyResourceProperty(script, name, prop, value);
				return;
			}

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


		void setPropertyValue(Lumix::ComponentHandle cmp,
			int scr_index,
			const char* name,
			const char* value) override
		{
			auto* script_cmp = m_scripts[{cmp.index}];
			if (!script_cmp) return;
			if(!script_cmp->m_scripts[scr_index].m_state) return;

			Property& prop = getScriptProperty(cmp, scr_index, name);
			applyProperty(script_cmp->m_scripts[scr_index], prop, value);
		}


		const char* getPropertyName(Lumix::ComponentHandle cmp, int scr_index, int index) const
		{
			auto& script = m_scripts[{cmp.index}]->m_scripts[scr_index];

			return getPropertyName(script.m_properties[index].name_hash);
		}


		int getPropertyCount(Lumix::ComponentHandle cmp, int scr_index) const
		{
			auto& script = m_scripts[{cmp.index}]->m_scripts[scr_index];

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


		void destroyInstance(ScriptComponent& scr,  ScriptInstance& inst)
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

			for (int i = 0; i < m_timers.size(); ++i)
			{
				if (m_timers[i].state == inst.m_state)
				{
					luaL_unref(m_timers[i].state, LUA_REGISTRYINDEX, m_timers[i].func);
					m_timers.eraseFast(i);
					--i;
				}
			}

			for(int i = 0; i < m_updates.size(); ++i)
			{
				if(m_updates[i].state == inst.m_state)
				{
					m_updates.eraseFast(i);
					break;
				}
			}

			luaL_unref(inst.m_state, LUA_REGISTRYINDEX, inst.m_thread_ref);
			luaL_unref(inst.m_state, LUA_REGISTRYINDEX, inst.m_environment);
			inst.m_state = nullptr;
		}


		void setScriptPath(ScriptComponent& cmp, ScriptInstance& inst, const Path& path)
		{
			registerAPI();

			if (inst.m_script)
			{
				if (inst.m_state) destroyInstance(cmp, inst);
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


		void startScript(ScriptInstance& instance, bool is_restart)
		{
			if (is_restart)
			{
				for (int i = 0; i < m_updates.size(); ++i)
				{
					if (m_updates[i].state == instance.m_state)
					{
						m_updates.eraseFast(i);
						break;
					}
				}
			}

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

			if (!is_restart)
			{
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
			}
			lua_pop(instance.m_state, 1);
		}


		void startGame() override
		{
			m_is_game_running = true;

			// copy m_scripts to tmp, because scripts can create other scripts -> m_scripts is not const
			Lumix::Array<ScriptComponent*> tmp(m_system.m_allocator);
			tmp.reserve(m_scripts.size());
			for (auto* scr : m_scripts) tmp.push(scr); 

			for (auto* scr : tmp)
			{
				for (int j = 0; j < scr->m_scripts.size(); ++j)
				{
					auto& instance = scr->m_scripts[j];
					if (!instance.m_script) continue;
					if (!instance.m_script->isReady()) continue;

					startScript(instance, false);
				}
			}
		}


		void stopGame() override
		{
			m_is_game_running = false;
			m_updates.clear();
			m_timers.clear();
		}


		ComponentHandle createComponent(ComponentType type, Entity entity) override
		{
			if (type != LUA_SCRIPT_TYPE) return INVALID_COMPONENT;

			auto& allocator = m_system.m_allocator;
			ScriptComponent* script = LUMIX_NEW(allocator, ScriptComponent)(*this, allocator);
			ComponentHandle cmp = {entity.index};
			script->m_entity = entity;
			m_scripts.insert(entity, script);
			m_universe.addComponent(entity, type, this, cmp);

			return cmp;
		}


		void destroyComponent(ComponentHandle component, ComponentType type) override
		{
			if (type != LUA_SCRIPT_TYPE) return;

			Entity entity = {component.index};
			auto* script = m_scripts[entity];
			for (auto& scr : script->m_scripts)
			{
				if (scr.m_state) destroyInstance(*script, scr);
				if (scr.m_script)
				{
					auto& cb = scr.m_script->getObserverCb();
					cb.unbind<ScriptComponent, &ScriptComponent::onScriptLoaded>(script);
					m_system.getScriptManager().unload(*scr.m_script);
				}
			}
			LUMIX_DELETE(m_system.m_allocator, script);
			m_scripts.erase(entity);
			m_universe.destroyComponent(entity, type, this, component);
		}


		void getPropertyValue(ComponentHandle cmp,
			int scr_index,
			const char* property_name,
			char* out,
			int max_size) override
		{
			ASSERT(max_size > 0);

			uint32 hash = crc32(property_name);
			auto& inst = m_scripts[{cmp.index}]->m_scripts[scr_index];
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
			if (!scr.m_state)
			{
				copyString(out, max_size, prop.stored_value.c_str());
				return;
			}

			*out = '\0';
			lua_rawgeti(scr.m_state, LUA_REGISTRYINDEX, scr.m_environment);
			if (lua_getfield(scr.m_state, -1, prop_name) == LUA_TNIL)
			{
				copyString(out, max_size, prop.stored_value.c_str());
				return;
			}
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
					Entity val = {(int)lua_tointeger(scr.m_state, -1)};
					toCString(val.index, out, max_size);
				}
				break;
				case Property::RESOURCE: 
				{
					int res_idx = LuaWrapper::toType<int>(scr.m_state, -1);
					Resource* res = m_system.m_engine.getLuaResource(res_idx);
					copyString(out, max_size, res ? res->getPath().c_str() : "");
				}
				break;
				default: ASSERT(false); break;
			}
			lua_pop(scr.m_state, 2);
		}


		void serialize(OutputBlob& serializer) override
		{
			serializer.write(m_scripts.size());
			for (auto iter = m_scripts.begin(), end = m_scripts.end(); iter != end; ++iter)
			{
				ScriptComponent* script_cmp = iter.value();
				serializer.write(script_cmp->m_entity);
				serializer.write(script_cmp->m_scripts.size());
				for (auto& scr : script_cmp->m_scripts)
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
			m_scripts.rehash(len);
			for (int i = 0; i < len; ++i)
			{
				bool is_valid = true;
				if (version <= (int)LuaScriptVersion::REFACTOR) serializer.read(is_valid);
				if (!is_valid) continue;

				auto& allocator = m_system.m_allocator;
				ScriptComponent* script = LUMIX_NEW(allocator, ScriptComponent)(*this, allocator);

				serializer.read(script->m_entity);
				m_scripts.insert(script->m_entity, script);
				int scr_count;
				serializer.read(scr_count);
				for (int j = 0; j < scr_count; ++j)
				{
					auto& scr = script->m_scripts.emplace(allocator);

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
					setScriptPath(*script, scr, Path(tmp));
				}
				ComponentHandle cmp = {script->m_entity.index};
				m_universe.addComponent(script->m_entity, LUA_SCRIPT_TYPE, this, cmp);
			}
		}


		void deserializeOld(InputBlob& serializer)
		{
			int len = serializer.read<int>();
			m_scripts.rehash(len);
			for (int i = 0; i < len; ++i)
			{
				bool is_valid;
				serializer.read(is_valid);
				if (!is_valid) continue;

				IAllocator& allocator = m_system.m_allocator;
				ScriptComponent* script = LUMIX_NEW(allocator, ScriptComponent)(*this, allocator);
				serializer.read(script->m_entity);
				m_scripts.insert(script->m_entity, script);

				char tmp[MAX_PATH_LENGTH];
				serializer.readString(tmp, MAX_PATH_LENGTH);
				auto& scr = script->m_scripts.emplace(m_system.m_allocator);
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
				setScriptPath(*script, scr, Path(tmp));
				ComponentHandle cmp = {script->m_entity.index};
				m_universe.addComponent(script->m_entity, LUA_SCRIPT_TYPE, this, cmp);
			}
		}


		IPlugin& getPlugin() const override { return m_system; }


		void update(float time_delta, bool paused) override
		{
			PROFILE_FUNCTION();
			if (paused) return;

			int timers_to_remove[1024];
			int timers_to_remove_count = 0;
			for (int i = 0, c = m_timers.size(); i < c; ++i)
			{
				auto& timer = m_timers[i];
				timer.time -= time_delta;
				if (timer.time < 0)
				{
					if (lua_rawgeti(timer.state, LUA_REGISTRYINDEX, timer.func) != LUA_TFUNCTION)
					{
						ASSERT(false);
					}

					if (lua_pcall(timer.state, 0, 0, 0) != LUA_OK)
					{
						g_log_error.log("Lua Script") << lua_tostring(timer.state, -1);
						lua_pop(timer.state, 1);
					}
					timers_to_remove[timers_to_remove_count] = i;
					++timers_to_remove_count;
					if (timers_to_remove_count >= lengthOf(timers_to_remove))
					{
						g_log_error.log("Lua Script") << "Too many lua timers in one frame, some are not executed";
						break;
					}
				}
			}
			for (int i = timers_to_remove_count - 1; i >= 0; --i)
			{
				auto& timer = m_timers[timers_to_remove[i]];
				luaL_unref(timer.state, LUA_REGISTRYINDEX, timer.func);
				m_timers.eraseFast(timers_to_remove[i]);
			}

			for (int i = 0; i < m_updates.size(); ++i)
			{
				UpdateData update_item = m_updates[i];
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


		ComponentHandle getComponent(Entity entity, ComponentType type) override
		{
			if (m_scripts.find(entity) == m_scripts.end()) return INVALID_COMPONENT;
			return {entity.index};
		}


		Property& getScriptProperty(ComponentHandle cmp, int scr_index, const char* name)
		{
			uint32 name_hash = crc32(name);
			ScriptComponent* script_cmp = m_scripts[{cmp.index}];
			for (auto& prop : script_cmp->m_scripts[scr_index].m_properties)
			{
				if (prop.name_hash == name_hash)
				{
					return prop;
				}
			}

			script_cmp->m_scripts[scr_index].m_properties.emplace(m_system.m_allocator);
			auto& prop = script_cmp->m_scripts[scr_index].m_properties.back();
			prop.name_hash = name_hash;
			return prop;
		}


		Path getScriptPath(ComponentHandle cmp, int scr_index) override
		{
			auto& tmp = m_scripts[{cmp.index}]->m_scripts[scr_index];
			return tmp.m_script ? tmp.m_script->getPath() : Path("");
		}


		void setScriptPath(ComponentHandle cmp, int scr_index, const Path& path) override
		{
			auto* script_cmp = m_scripts[{cmp.index}];
			if (script_cmp->m_scripts.size() <= scr_index) return;
			setScriptPath(*script_cmp, script_cmp->m_scripts[scr_index], path);
		}


		int getScriptCount(ComponentHandle cmp) override
		{
			return m_scripts[{cmp.index}]->m_scripts.size();
		}


		void insertScript(ComponentHandle cmp, int idx) override
		{
			m_scripts[{cmp.index}]->m_scripts.emplaceAt(idx, m_system.m_allocator);
		}


		int addScript(ComponentHandle cmp) override
		{
			ScriptComponent* script_cmp = m_scripts[{cmp.index}];
			script_cmp->m_scripts.emplace(m_system.m_allocator);
			return script_cmp->m_scripts.size() - 1;
		}


		void moveScript(ComponentHandle cmp, int scr_index, bool up) override
		{
			auto* script_cmp = m_scripts[{cmp.index}];
			if (!up && scr_index > script_cmp->m_scripts.size() - 2) return;
			if (up && scr_index == 0) return;
			int other = up ? scr_index - 1 : scr_index + 1;
			ScriptInstance tmp = script_cmp->m_scripts[scr_index];
			script_cmp->m_scripts[scr_index] = script_cmp->m_scripts[other];
			script_cmp->m_scripts[other] = tmp;
		}


		void removeScript(ComponentHandle cmp, int scr_index) override
		{
			setScriptPath(cmp, scr_index, Path());
			m_scripts[{cmp.index}]->m_scripts.eraseFast(scr_index);
		}


		void serializeScript(ComponentHandle cmp, int scr_index, OutputBlob& blob) override
		{
			auto& scr = m_scripts[{cmp.index}]->m_scripts[scr_index];
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


		void deserializeScript(ComponentHandle cmp, int scr_index, InputBlob& blob) override
		{
			auto& scr = m_scripts[{cmp.index}]->m_scripts[scr_index];
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
		HashMap<Entity, ScriptComponent*> m_scripts;
		AssociativeArray<uint32, string> m_property_names;
		Universe& m_universe;
		Array<UpdateData> m_updates;
		Array<TimerData> m_timers;
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
		m_script_manager.create(LUA_SCRIPT_RESOURCE_TYPE, engine.getResourceManager());

		auto& allocator = engine.getAllocator();
		PropertyRegister::add("lua_script",
			LUMIX_NEW(allocator, BlobPropertyDescriptor<LuaScriptScene>)(
				"data", &LuaScriptScene::getScriptData, &LuaScriptScene::setScriptData));
	}


	LuaScriptSystemImpl::~LuaScriptSystemImpl()
	{
		m_script_manager.destroy();
	}


	IScene* LuaScriptSystemImpl::createScene(Universe& ctx)
	{
		return LUMIX_NEW(m_allocator, LuaScriptSceneImpl)(*this, ctx);
	}


	void LuaScriptSystemImpl::destroyScene(IScene* scene)
	{
		LUMIX_DELETE(m_allocator, scene);
	}


	LUMIX_PLUGIN_ENTRY(lua_script)
	{
		return LUMIX_NEW(engine.getAllocator(), LuaScriptSystemImpl)(engine);
	}
}
