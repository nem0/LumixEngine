#include "core/iallocator.h"
#include "core/binary_array.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/json_serializer.h"
#include "core/library.h"
#include "core/log.h"
#include "core/array.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "universe/universe.h"
#include <lua.hpp>
#include <lauxlib.h>


static const uint32_t SCRIPT_HASH = crc32("script");


namespace Lumix
{
	class LuaScriptSystemImpl;


	namespace LuaWrapper
	{
		template <typename T> T toType(lua_State* L, int index) { return (T)lua_touserdata(L, index); }
		template <> int toType(lua_State* L, int index) { return (int)lua_tointeger(L, index); }
		template <> int64_t toType(lua_State* L, int index) { return (int64_t)lua_tointeger(L, index); }
		template <> bool toType(lua_State* L, int index) { return lua_toboolean(L, index) != 0; }
		template <> float toType(lua_State* L, int index) { return (float)lua_tonumber(L, index); }
		template <> const char* toType(lua_State* L, int index) { return lua_tostring(L, index); }
		template <> void* toType(lua_State* L, int index) { return lua_touserdata(L, index); }


		template <typename T> bool isType(lua_State* L, int index) { return lua_islightuserdata(L, index) != 0; }
		template <> bool isType<int>(lua_State* L, int index) { return lua_isinteger(L, index) != 0; }
		template <> bool isType<int64_t>(lua_State* L, int index) { return lua_isinteger(L, index) != 0; }
		template <> bool isType<bool>(lua_State* L, int index) { return lua_isboolean(L, index) != 0; }
		template <> bool isType<float>(lua_State* L, int index) { return lua_isnumber(L, index) != 0; }
		template <> bool isType<const char*>(lua_State* L, int index) { return lua_isstring(L, index) != 0; }
		template <> bool isType<void*>(lua_State* L, int index) { return lua_islightuserdata(L, index) != 0; }

		template <typename T> void pushLua(lua_State* L, T value) { return lua_pushnumber(L, value); }
		template <> void pushLua(lua_State* L, float value) { return lua_pushnumber(L, value); }
		template <> void pushLua(lua_State* L, int value) { return lua_pushinteger(L, value); }

		template <int N>
		struct FunctionCaller
		{
			template <typename R, typename... ArgsF, typename... Args>
			static LUMIX_FORCE_INLINE R callFunction(R(*f)(ArgsF...), lua_State* L, Args... args)
			{
				typedef std::tuple_element<sizeof...(ArgsF)-N, std::tuple<ArgsF...> >::type T;
				if (!isType<T>(L, sizeof...(ArgsF)-N + 1))
				{
					lua_Debug entry;
					int depth = 0;

					char tmp[2048];
					tmp[0] = 0;
					auto er = g_log_error.log("lua");
					er << "Wrong arguments in\n";
					while (lua_getstack(L, depth, &entry))
					{
						int status = lua_getinfo(L, "Sln", &entry);
						ASSERT(status);
						er << entry.short_src << "(" << entry.currentline << "): " << (entry.name ? entry.name : "?") << "\n";
						depth++;
					}
					return R();
				}
				T a = toType<T>(L, sizeof...(ArgsF)-N + 1);
				return FunctionCaller<N - 1>::callFunction(f, L, args..., a);
			}
		};


		template <>
		struct FunctionCaller<0>
		{
			template <typename R, typename... ArgsF, typename... Args>
			static LUMIX_FORCE_INLINE R callFunction(R(*f)(ArgsF...), lua_State*, Args... args)
			{
				return f(args...);
			}
		};


		template <typename R, typename... ArgsF>
		int LUMIX_FORCE_INLINE callFunction(R(*f)(ArgsF...), lua_State* L)
		{
			R v = FunctionCaller<sizeof...(ArgsF)>::callFunction(f, L);
			pushLua(L, v);
			return 1;
		}

		template <typename... ArgsF>
		int LUMIX_FORCE_INLINE callFunction(void(*f)(ArgsF...), lua_State* L)
		{
			FunctionCaller<sizeof...(ArgsF)>::callFunction(f, L);
			return 0;
		}

		template <typename T, T t>
		int wrap(lua_State* L)
		{
			return callFunction(t, L);
		}


	} // namespace LuaWrapper


	static const uint32_t LUA_SCRIPT_HASH = crc32("lua_script");


	class LuaScriptSystem : public IPlugin
	{
		public:
			Engine& m_engine;
			BaseProxyAllocator m_allocator;

			LuaScriptSystem(Engine& engine);
			IAllocator& getAllocator();
			virtual IScene* createScene(Universe& universe) override;
			virtual void destroyScene(IScene* scene) override;
			virtual bool create() override;
			virtual void destroy() override;
			virtual const char* getName() const override;
	};


	namespace LuaAPI
	{


		void setEntityPosition(Universe* univ, int entity_index, float x, float y, float z)
		{
			Entity(univ, entity_index).setPosition(x, y, z);
		}


	} // namespace LuaAPI


	class LuaScriptScene : public IScene
	{
		public:
			LuaScriptScene(LuaScriptSystem& system, Engine& engine, Universe& universe)
				: m_system(system)
				, m_universe(universe)
				, m_scripts(system.getAllocator())
				, m_valid(system.getAllocator())
			{
				if (system.m_engine.getWorldEditor())
				{
					system.m_engine.getWorldEditor()->gameModeToggled().bind<LuaScriptScene, &LuaScriptScene::onGameModeToggled>(this);
				}
			}


			~LuaScriptScene()
			{
				if (m_system.m_engine.getWorldEditor())
				{
					m_system.m_engine.getWorldEditor()->gameModeToggled().unbind<LuaScriptScene, &LuaScriptScene::onGameModeToggled>(this);
				}
			}


			void registerCFunction(const char* name, lua_CFunction function) const
			{
				lua_pushcfunction(m_global_state, function);
				lua_setglobal(m_global_state, name);
			}


			void registerAPI(lua_State* L)
			{
				lua_pushlightuserdata(L, &m_universe);
				lua_setglobal(L, "g_universe");

				lua_pushlightuserdata(L, this);
				lua_setglobal(L, "g_scene");
				
				registerCFunction("setEntityPosition", LuaWrapper::wrap<decltype(&LuaAPI::setEntityPosition), LuaAPI::setEntityPosition>);
			}


			void startGame()
			{
				m_global_state = luaL_newstate();
				luaL_openlibs(m_global_state);
				registerAPI(m_global_state);
				for (int i = 0; i < m_scripts.size(); ++i)
				{
					if (m_valid[i])
					{
						Script& script = m_scripts[i];
						script.m_state = lua_newthread(m_global_state);

						FILE* fp = fopen(script.m_path.c_str(), "r");
						if (fp)
						{
							fseek(fp, 0, SEEK_END);
							long size = ftell(fp);
							fseek(fp, 0, SEEK_SET);
							Array<char> content(m_system.getAllocator());
							content.resize(size);
							fread(&content[0], 1, size, fp);
							bool errors = luaL_loadbuffer(script.m_state, &content[0], size, "") != LUA_OK;
							errors = errors || lua_pcall(script.m_state, 0, LUA_MULTRET, 0) != LUA_OK;
							if (errors)
							{
								g_log_error.log("lua") << script.m_path.c_str() << ": " << lua_tostring(script.m_state, -1);
							}
							fclose(fp);
						}
					}
				}
			}


			void stopGame()
			{
				lua_close(m_global_state);
			}


			void onGameModeToggled(bool is_game_mode)
			{
				if (is_game_mode)
				{
					startGame();
				}
				else
				{
					stopGame();
				}
			}


			virtual Component createComponent(uint32_t type, const Entity& entity) override
			{
				if (type == LUA_SCRIPT_HASH)
				{
					LuaScriptScene::Script& script = m_scripts.pushEmpty();
					script.m_entity = entity.index;
					script.m_path = "";
					script.m_state = nullptr;
					m_valid.push(true);
					Component cmp = m_universe.addComponent(entity, type, this, m_scripts.size() - 1);
					m_universe.componentCreated().invoke(cmp);
					return cmp;
				}
				return Component::INVALID;
			}


			virtual void destroyComponent(const Component& component) override
			{
				if (component.type == LUA_SCRIPT_HASH)
				{
					m_universe.destroyComponent(component);
					m_valid[component.index] = false;
				}
			}


			virtual void serialize(OutputBlob& serializer) override
			{
				serializer.write(m_scripts.size());
				for (int i = 0; i < m_scripts.size(); ++i)
				{
					serializer.write(m_scripts[i].m_entity);
					serializer.writeString(m_scripts[i].m_path.c_str());
					serializer.write((bool)m_valid[i]);
				}
			}


			virtual void deserialize(InputBlob& serializer) override
			{
				int len = serializer.read<int>();
				m_scripts.resize(len);
				m_valid.resize(len);
				for (int i = 0; i < m_scripts.size(); ++i)
				{
					serializer.read(m_scripts[i].m_entity);
					char tmp[LUMIX_MAX_PATH];
					serializer.readString(tmp, LUMIX_MAX_PATH);
					m_valid[i] = serializer.read<bool>();
					m_scripts[i].m_path = tmp;
					m_scripts[i].m_state = nullptr;
					if (m_valid[i])
					{
						m_universe.addComponent(Entity(&m_universe, m_scripts[i].m_entity), LUA_SCRIPT_HASH, this, i);
					}
				}
			}


			virtual IPlugin& getPlugin() const override
			{
				return m_system;
			}


			virtual void update(float time_delta) override
			{
				if (lua_getglobal(m_global_state, "update") == LUA_TFUNCTION)
				{
					lua_pushnumber(m_global_state, time_delta);
					if (lua_pcall(m_global_state, 1, 0, 0) != LUA_OK)
					{
						g_log_error.log("lua") << lua_tostring(m_global_state, -1);
					}
				}

			}


			virtual bool ownComponentType(uint32_t type) const override
			{
				return type == LUA_SCRIPT_HASH;
			}


			void getScriptPath(Component cmp, string& path)
			{
				path = m_scripts[cmp.index].m_path.c_str();
			}


			void setScriptPath(Component cmp, const string& path)
			{
				m_scripts[cmp.index].m_path = path;
			}

		private:
			class Script
			{
				public:
					Lumix::Path m_path;
					int m_entity;
					lua_State* m_state;
			};

		private:
			LuaScriptSystem& m_system;
			
			BinaryArray m_valid;
			Array<Script> m_scripts;
			lua_State* m_global_state;
			Universe& m_universe;
	};


	LuaScriptSystem::LuaScriptSystem(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
	{
	}


	IAllocator& LuaScriptSystem::getAllocator()
	{
		return m_allocator;
	}


	IScene* LuaScriptSystem::createScene(Universe& universe)
	{
		return m_allocator.newObject<LuaScriptScene>(*this, m_engine, universe);
	}


	void LuaScriptSystem::destroyScene(IScene* scene)
	{
		m_allocator.deleteObject(scene);
	}


	bool LuaScriptSystem::create()
	{
		if (m_engine.getWorldEditor())
		{
			IAllocator& allocator = m_engine.getWorldEditor()->getAllocator();
			m_engine.getWorldEditor()->registerComponentType("lua_script", "Lua script");
			m_engine.getWorldEditor()->registerProperty("lua_script"
				, allocator.newObject<FilePropertyDescriptor<LuaScriptScene> >("source"
					, (void (LuaScriptScene::*)(Component, string&))&LuaScriptScene::getScriptPath
					, &LuaScriptScene::setScriptPath
					, "Lua (*.lua)"
					, allocator)
			);
		}
		return true;
	}


	void LuaScriptSystem::destroy()
	{
	}


	const char* LuaScriptSystem::getName() const
	{
		return "lua_script";
	}


	extern "C" LUMIX_LIBRARY_EXPORT IPlugin* createPlugin(Engine& engine)
	{
		return engine.getAllocator().newObject<LuaScriptSystem>(engine);
	}

} // ~namespace Lumix

