#include "lua_script_system.h"
#include "core/array.h"
#include "core/base_proxy_allocator.h"
#include "core/binary_array.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/iallocator.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/lua_wrapper.h"
#include "core/path_utils.h"
#include "core/resource_manager.h"
#include "debug/debug.h"
#include "editor/asset_browser.h"
#include "editor/ieditor_command.h"
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
		struct Property
		{
			enum Type : int
			{
				BOOLEAN,
				FLOAT,
				ENTITY,
				ANY
			};

			explicit Property(IAllocator& allocator)
				: stored_value(allocator)
			{
			}

			uint32 name_hash;
			Type type;
			string stored_value;
		};


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
			ScriptComponent(LuaScriptSceneImpl& scene, IAllocator& allocator)
				: m_scripts(allocator)
				, m_scene(scene)
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
				lua_rawgeti(L, LUA_REGISTRYINDEX, inst.m_environment);
				lua_pushnil(L);
				auto& allocator = m_scene.m_system.getAllocator();
				while(lua_next(L, -2))
				{
					if(lua_type(L, -1) != LUA_TFUNCTION)
					{
						const char* name = lua_tostring(L, -2);
						uint32 hash = crc32(name);
						m_scene.m_property_names.insert(hash, string(name, allocator));
						if(hash != INDEX_HASH && hash != THIS_HASH)
						{
							auto* existing_prop = getProperty(inst, hash);
							if (existing_prop)
							{
								if (existing_prop->type == Property::ANY)
								{
									switch (lua_type(inst.m_state, -1))
									{
										case LUA_TBOOLEAN:
											existing_prop->type = Property::BOOLEAN;
											break;
										default: existing_prop->type = Property::FLOAT;
									}
								}
								m_scene.applyProperty(
									inst, *existing_prop, existing_prop->stored_value.c_str());
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
				for (auto& script : m_scripts)
				{
					if ((!script.m_script || !script.m_script->isReady()) && script.m_state)
					{
						m_scene.destroy(script);
						continue;
					}

					if (!script.m_script) continue;
					if (!script.m_script->isReady()) continue;
					if (script.m_state) continue;

					script.m_environment = -1;

					script.m_state = lua_newthread(m_scene.m_global_state);
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
				lua_rawgeti(state, LUA_REGISTRYINDEX, env);
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
			, m_global_state(nullptr)
			, m_global_lua_thread_ref(-1)
			, m_scripts(system.getAllocator())
			, m_updates(system.getAllocator())
			, m_entity_script_map(system.getAllocator())
			, m_property_names(system.getAllocator())
		{
			m_function_call.is_in_progress = false;
			m_is_api_registered = false;
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

			lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment);
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
				g_log_warning.log("Lua Script") << lua_tostring(script.m_state, -1);
				lua_pop(script.m_state, 1);
			}
			lua_pop(script.m_state, 1);
		}


		~LuaScriptSceneImpl()
		{
			if (m_global_lua_thread_ref != -1)
			{
				luaL_unref(m_system.m_engine.getState(), LUA_REGISTRYINDEX, m_global_lua_thread_ref);
			}

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


		lua_State* getGlobalState() { return m_global_state; }


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
			auto f = &LuaWrapper::wrap<decltype(&setPropertyType), &setPropertyType>;
			LuaWrapper::createSystemFunction(m_global_state, "Editor", "setPropertyType", f);
			LuaWrapper::createSystemVariable(m_global_state, "Editor", "BOOLEAN_PROPERTY", Property::BOOLEAN);
			LuaWrapper::createSystemVariable(m_global_state, "Editor", "FLOAT_PROPERTY", Property::FLOAT);
			LuaWrapper::createSystemVariable(m_global_state, "Editor", "ENTITY_PROPERTY", Property::ENTITY);
		}


		static int getEnvironment(lua_State* L)
		{
			auto* scene = LuaWrapper::checkArg<LuaScriptScene*>(L, 1);
			Entity entity = LuaWrapper::checkArg<Entity>(L, 2);
			int scr_index = LuaWrapper::checkArg<int>(L, 3);

			ComponentIndex cmp = scene->getComponent(entity);
			int env = scene->getEnvironment(cmp, scr_index);
			if (env < 0)
			{
				lua_pushnil(L);
			}
			else
			{
				lua_rawgeti(L, LUA_REGISTRYINDEX, env);
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
					desc->get(cmp, -1, OutputBlob(&v, sizeof(v)));
					LuaWrapper::pushLua(L, v);
				}
				break;
				case IPropertyDescriptor::BOOL:
				{
					bool v;
					desc->get(cmp, -1, OutputBlob(&v, sizeof(v)));
					LuaWrapper::pushLua(L, v);
				}
				break;
				case IPropertyDescriptor::INTEGER:
				{
					int v;
					desc->get(cmp, -1, OutputBlob(&v, sizeof(v)));
					LuaWrapper::pushLua(L, v);
				}
				break;
				case IPropertyDescriptor::COLOR:
				case IPropertyDescriptor::VEC3:
				{
					Vec3 v;
					desc->get(cmp, -1, OutputBlob(&v, sizeof(v)));
					LuaWrapper::pushLua(L, v);
				}
				break;
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
					desc->set(cmp, -1, InputBlob(&v, sizeof(v)));
				}
				break;
				case IPropertyDescriptor::INTEGER:
				{
					auto v = LuaWrapper::checkArg<int>(L, 3);
					desc->set(cmp, -1, InputBlob(&v, sizeof(v)));
				}
				break;
				case IPropertyDescriptor::BOOL:
				{
					auto v = LuaWrapper::checkArg<bool>(L, 3);
					desc->set(cmp, -1, InputBlob(&v, sizeof(v)));
				}
				break;
				case IPropertyDescriptor::COLOR:
				case IPropertyDescriptor::VEC3:
				{
					auto v = LuaWrapper::checkArg<Vec3>(L, 3);
					desc->set(cmp, -1, InputBlob(&v, sizeof(v)));
				}
				break;
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
			for (int i = 0; i < cmps_count; ++i)
			{
				const char* cmp_name = PropertyRegister::getComponentTypeID(i);
				lua_newtable(m_global_state);
				lua_pushvalue(m_global_state, -1);
				char tmp[50];
				convertPropertyToLuaName(cmp_name, tmp, lengthOf(tmp));
				lua_setglobal(m_global_state, tmp);

				uint32 cmp_name_hash = crc32(cmp_name);
				auto& descs = PropertyRegister::getDescriptors(cmp_name_hash);
				char setter[50];
				char getter[50];
				for (auto* desc : descs)
				{
					convertPropertyToLuaName(desc->getName(), tmp, lengthOf(tmp));
					copyString(setter, "set");
					copyString(getter, "get");
					catString(setter, tmp);
					catString(getter, tmp);
					switch (desc->getType())
					{
						case IPropertyDescriptor::VEC3:
						case IPropertyDescriptor::COLOR:
							lua_pushlightuserdata(m_global_state, desc);
							lua_pushinteger(m_global_state, cmp_name_hash);
							lua_pushcclosure(m_global_state, &LUA_setProperty, 2);
							lua_setfield(m_global_state, -2, setter);

							lua_pushlightuserdata(m_global_state, desc);
							lua_pushinteger(m_global_state, cmp_name_hash);
							lua_pushcclosure(m_global_state, &LUA_getProperty, 2);
							lua_setfield(m_global_state, -2, getter);
							break;
					}
				}
				lua_pop(m_global_state, 1);
			}
		}


		void registerAPI()
		{
			if (m_is_api_registered) return;

			m_is_api_registered = true;

			lua_State* engine_state = m_system.m_engine.getState();
			m_global_state = lua_newthread(engine_state);
			lua_pushvalue(engine_state, -1);
			m_global_lua_thread_ref = luaL_ref(engine_state, LUA_REGISTRYINDEX);
			for (auto* scene : m_universe.getScenes())
			{
				const char* name = scene->getPlugin().getName();
				char tmp[128];

				copyString(tmp, "g_scene_");
				catString(tmp, name);
				lua_pushlightuserdata(m_global_state, scene);
				lua_setglobal(m_global_state, tmp);
			}

			lua_pushlightuserdata(m_global_state, &m_universe);
			lua_setglobal(m_global_state, "g_universe");
			LuaWrapper::createSystemFunction(
				m_global_state, "LuaScript", "getEnvironment", &LuaScriptSceneImpl::getEnvironment);
			registerProperties();
			registerPropertyAPI();
			uint32 register_msg = crc32("registerLuaAPI");
			for (auto* i : m_universe.getScenes())
			{
				i->sendMessage(register_msg, nullptr);
			}
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

			lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment);
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
			const char* value)
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


		void destroy(ScriptInstance& inst)
		{
			lua_rawgeti(inst.m_state, LUA_REGISTRYINDEX, inst.m_environment);
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
					destroy(inst);
				}
				inst.m_state = nullptr;
				inst.m_properties.clear();
				auto& cb = inst.m_script->getObserverCb();
				cb.unbind<ScriptComponent, &ScriptComponent::onScriptLoaded>(&cmp);
				m_system.getScriptManager().unload(*inst.m_script);
			}
			inst.m_script = path.isValid()
								? static_cast<LuaScript*>(m_system.getScriptManager().load(path))
								: nullptr;
			if (inst.m_script)
			{
				inst.m_script->onLoaded<ScriptComponent, &ScriptComponent::onScriptLoaded>(&cmp);
			}
		}


		void startGame() override
		{
			for (auto* scr : m_scripts)
			{
				if (!scr) continue;
				for (auto& i : scr->m_scripts)
				{
					if (!i.m_script) continue;

					lua_rawgeti(i.m_state, LUA_REGISTRYINDEX, i.m_environment);
					if (lua_getfield(i.m_state, -1, "update") == LUA_TFUNCTION)
					{
						auto& update_data = m_updates.emplace();
						update_data.script = i.m_script;
						update_data.state = i.m_state;
						update_data.environment = i.m_environment;
					}
					lua_pop(i.m_state, 1);

					if (lua_getfield(i.m_state, -1, "init") != LUA_TFUNCTION)
					{
						lua_pop(i.m_state, 2);
						continue;
					}

					if (lua_pcall(i.m_state, 0, 0, 0) != LUA_OK)
					{
						g_log_error.log("Lua Script") << lua_tostring(i.m_state, -1);
						lua_pop(i.m_state, 1);
					}
					lua_pop(i.m_state, 1);
				}
			}
		}


		void stopGame() override
		{
			m_updates.clear();
		}


		ComponentIndex createComponent(uint32 type, Entity entity) override
		{
			if (type != LUA_SCRIPT_HASH) return INVALID_COMPONENT;

			ScriptComponent& script =
				*LUMIX_NEW(m_system.getAllocator(), ScriptComponent)(*this, m_system.getAllocator());
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
				if (scr.m_state) destroy(scr);
				if (scr.m_script) m_system.getScriptManager().unload(*scr.m_script);
			}
			m_entity_script_map.erase(m_scripts[component]->m_entity);
			auto* script = m_scripts[component];
			m_scripts[component] = nullptr;
			m_universe.destroyComponent(script->m_entity, type, this, component);
			LUMIX_DELETE(m_system.getAllocator(), script);
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

				ScriptComponent& script = *LUMIX_NEW(m_system.getAllocator(), ScriptComponent)(*this, m_system.getAllocator());
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
					scr.m_state = nullptr;
					int prop_count;
					serializer.read(prop_count);
					scr.m_properties.reserve(prop_count);
					for (int j = 0; j < prop_count; ++j)
					{
						Property& prop = scr.m_properties.emplace(m_system.getAllocator());
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
			if (!m_global_state || paused) { return; }

			for (auto& i : m_updates)
			{
				if (lua_rawgeti(i.state, LUA_REGISTRYINDEX, i.environment) != LUA_TTABLE)
				{
					ASSERT(false);
				}
				if (lua_getfield(i.state, -1, "update") != LUA_TFUNCTION)
				{
					lua_pop(i.state, 2);
					continue;
				}

				auto t = lua_gettop(i.state);
				lua_pushnumber(i.state, time_delta);
				if (lua_pcall(i.state, 1, 0, 0) != LUA_OK)
				{
					g_log_error.log("Lua Script") << lua_tostring(i.state, -1);
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


		void insertScript(ComponentIndex cmp, int idx)
		{
			m_scripts[cmp]->m_scripts.emplaceAt(idx, m_system.m_allocator);
		}


		int addScript(ComponentIndex cmp)
		{
			m_scripts[cmp]->m_scripts.emplace(m_system.m_allocator);
			return m_scripts[cmp]->m_scripts.size() - 1;
		}


		void removeScript(ComponentIndex cmp, int scr_index)
		{
			setScriptPath(cmp, scr_index, Path());
			m_scripts[cmp]->m_scripts.eraseFast(scr_index);
		}


		void serializeScript(ComponentIndex cmp, int scr_index, OutputBlob& blob)
		{
			auto& scr = m_scripts[cmp]->m_scripts[scr_index];
			blob.writeString(scr.m_script ? scr.m_script->getPath().c_str() : "");
			blob.write(scr.m_properties.size());
			for (auto prop : scr.m_properties)
			{
				blob.write(prop.name_hash);
				char tmp[1024];
				getProperty(prop, getPropertyName(prop.name_hash), scr, tmp, lengthOf(tmp));
				blob.writeString(tmp);
			}
		}


		void deserializeScript(ComponentIndex cmp, int scr_index, InputBlob& blob)
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
		lua_State* m_global_state;
		int m_global_lua_thread_ref;
		Universe& m_universe;
		Array<UpdateData> m_updates;
		FunctionCall m_function_call;
		ScriptInstance* m_current_script_instance;
		bool m_is_api_registered;
	};


	LuaScriptSystemImpl::LuaScriptSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_script_manager(m_allocator)
	{
		m_script_manager.create(crc32("lua_script"), engine.getResourceManager());

		PropertyRegister::registerComponentType("lua_script", "Lua script");
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
			AddScriptCommand(){}
			

			explicit AddScriptCommand(WorldEditor& editor)
			{
				scene = static_cast<LuaScriptSceneImpl*>(editor.getScene(crc32("lua_script")));
			}


			bool execute() override
			{
				scr_index = scene->addScript(cmp);
				return true;
			}


			void undo() override
			{
				scene->removeScript(cmp, scr_index);
			}


			void serialize(JsonSerializer& serializer) override
			{
				serializer.serialize("component", cmp);
			}


			void deserialize(JsonSerializer& serializer) override
			{
				serializer.deserialize("component", cmp, 0);
			}


			uint32 getType() override
			{
				static const uint32 hash = crc32("add_script");
				return hash;
			}


			bool merge(IEditorCommand& command) override
			{
				return false;
			}


			LuaScriptSceneImpl* scene;
			ComponentIndex cmp;
			int scr_index;
		};


		struct RemoveScriptCommand : public IEditorCommand
		{
			explicit RemoveScriptCommand(WorldEditor& editor)
				: blob(editor.getAllocator())
			{
				scene = static_cast<LuaScriptSceneImpl*>(editor.getScene(crc32("lua_script")));
			}


			explicit RemoveScriptCommand(IAllocator& allocator)
				: blob(allocator)
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
				scene->deserializeScript(cmp, scr_index, InputBlob(blob));
			}


			void serialize(JsonSerializer& serializer) override
			{
				serializer.serialize("component", cmp);
				serializer.serialize("scr_index", scr_index);
			}


			void deserialize(JsonSerializer& serializer) override
			{
				serializer.deserialize("component", cmp, 0);
				serializer.deserialize("scr_index", scr_index, 0);
			}


			uint32 getType() override
			{
				static const uint32 hash = crc32("remove_script");
				return hash;
			}


			bool merge(IEditorCommand& command) override
			{
				return false;
			}

			OutputBlob blob;
			LuaScriptSceneImpl* scene;
			ComponentIndex cmp;
			int scr_index;
		};


		struct SetPropertyCommand : public IEditorCommand
		{
			explicit SetPropertyCommand(WorldEditor& editor)
				: property_name(editor.getAllocator())
				, value(editor.getAllocator())
				, old_value(editor.getAllocator())
			{
				scene = static_cast<LuaScriptSceneImpl*>(editor.getScene(crc32("lua_script")));
			}


			SetPropertyCommand(LuaScriptSceneImpl* scene,
				ComponentIndex cmp,
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
					auto& inst = scene->m_scripts[cmp]->m_scripts[scr_index];
					uint32 prop_name_hash = crc32(property_name);
					for (auto& prop : inst.m_properties)
					{
						if (prop.name_hash == prop_name_hash)
						{
							scene->getProperty(prop, property_name, inst, tmp, lengthOf(tmp));
							break;
						}
					}
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
				serializer.deserialize("component", component, 0);
				serializer.deserialize("script_index", script_index, 0);
				char buf[256];
				serializer.deserialize("property_name", buf, lengthOf(buf), "");
				property_name = buf;
				serializer.deserialize("value", buf, lengthOf(buf), "");
				value = buf;
				serializer.deserialize("old_value", buf, lengthOf(buf), "");
				old_value = buf;
			}
			
			
			uint32 getType() override
			{
				static const uint32 hash = crc32("set_script_property");
				return hash;
			}


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


			LuaScriptSceneImpl* scene;
			string property_name;
			string value;
			string old_value;
			ComponentIndex component;
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
			registerCFunction(L, "Checkbox", &Checkbox);

			lua_pop(L, 1);

		}


		void onGUI(PropertyGrid& grid, Lumix::ComponentUID cmp) override
		{
			if (cmp.type != LUA_SCRIPT_HASH) return;

			auto* scene = static_cast<LuaScriptSceneImpl*>(cmp.scene);
			auto& editor = *m_app.getWorldEditor();
			auto& allocator = editor.getAllocator();

			if (ImGui::Button("Add script"))
			{
				auto* cmd = LUMIX_NEW(allocator, AddScriptCommand);
				cmd->scene = scene;
				cmd->cmp = cmp.index;
				editor.executeCommand(cmd);
			}

			for (int j = 0; j < scene->getScriptCount(cmp.index); ++j)
			{
				char buf[MAX_PATH_LENGTH];
				copyString(buf, scene->getScriptPath(cmp.index, j).c_str());
				char basename[50];
				PathUtils::getBasename(basename, lengthOf(basename), buf);
				if (basename[0] == 0) toCString(j, basename, lengthOf(basename));

				if (ImGui::CollapsingHeader(basename))
				{
					ImGui::PushID(j);
					if (ImGui::Button("Remove script"))
					{
						auto* cmd = LUMIX_NEW(allocator, RemoveScriptCommand)(allocator);
						cmd->cmp = cmp.index;
						cmd->scr_index = j;
						cmd->scene = scene;
						editor.executeCommand(cmd);
						ImGui::PopID();
						break;
					}
					if (m_app.getAssetBrowser()->resourceInput("Source", "src", buf, lengthOf(buf), LUA_SCRIPT_HASH))
					{
						auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(scene, cmp.index, j, "-source", buf, allocator);
						editor.executeCommand(cmd);
					}
					auto& inst = scene->m_scripts[cmp.index]->m_scripts[j];
					if (inst.m_state)
					{
						for (auto& prop : inst.m_properties)
						{
							char buf[256];
							const char* property_name = scene->getPropertyName(prop.name_hash);
							if (!property_name) continue;
							scene->getProperty(prop, property_name, inst, buf, lengthOf(buf));
							switch (prop.type)
							{
								case LuaScriptSceneImpl::Property::BOOLEAN:
								{
									bool b = compareString(buf, "true") == 0;
									if (ImGui::Checkbox(property_name, &b))
									{
										auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
											scene, cmp.index, j, property_name, b ? "true" : "false", allocator);
										editor.executeCommand(cmd);
									}
								}
								break;
								case LuaScriptSceneImpl::Property::FLOAT:
								{
									float f = (float)atof(buf);
									if (ImGui::DragFloat(property_name, &f))
									{
										Lumix::toCString(f, buf, sizeof(buf), 5);
										auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
											scene, cmp.index, j, property_name, buf, allocator);
										editor.executeCommand(cmd);
									}
								}
								break;
								case LuaScriptSceneImpl::Property::ENTITY:
								{
									Lumix::Entity e;
									Lumix::fromCString(buf, sizeof(buf), &e);
									if (grid.entityInput(property_name,
											StaticString<50>(property_name, cmp.index),
											e))
									{
										Lumix::toCString(e, buf, sizeof(buf));
										auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
											scene, cmp.index, j, property_name, buf, allocator);
										editor.executeCommand(cmd);
									}
								}
								break;
								case LuaScriptSceneImpl::Property::ANY:
									if (ImGui::InputText(property_name, buf, sizeof(buf)))
									{
										auto* cmd = LUMIX_NEW(allocator, SetPropertyCommand)(
											scene, cmp.index, j, property_name, buf, allocator);
										editor.executeCommand(cmd);
									}
									break;
							}
						}
						if (auto* call = scene->beginFunctionCall(cmp.index, j, "onGUI"))
						{
							scene->endFunctionCall(*call);
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
		explicit AssetBrowserPlugin(StudioApp& app)
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
				auto* file = fs.open(fs.getDefaultDevice(),
					resource->getPath(),
					Lumix::FS::Mode::CREATE_AND_WRITE);

				if (!file)
				{
					Lumix::g_log_warning.log("Lua Script") << "Could not save "
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


	LUMIX_STUDIO_ENTRY(lua_script)
	{
		auto& editor = *app.getWorldEditor();
		editor.registerEditorCommandCreator("add_script", createAddScriptCommand);
		editor.registerEditorCommandCreator("remove_script", createRemoveScriptCommand);
		editor.registerEditorCommandCreator("set_script_property", createSetPropertyCommand);

		auto* plugin = LUMIX_NEW(editor.getAllocator(), PropertyGridPlugin)(app);
		app.getPropertyGrid()->addPlugin(*plugin);

		auto* asset_browser_plugin = LUMIX_NEW(editor.getAllocator(), AssetBrowserPlugin)(app);
		app.getAssetBrowser()->addPlugin(*asset_browser_plugin);
	}


	LUMIX_PLUGIN_ENTRY(lua_script)
	{
		return LUMIX_NEW(engine.getAllocator(), LuaScriptSystemImpl)(engine);
	}
}
