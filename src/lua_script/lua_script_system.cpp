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
#include "engine/plugin_manager.h"
#include "engine/lua_wrapper.h"
#include "universe/universe.h"

static const uint32_t SCRIPT_HASH = crc32("script");

namespace Lumix
{


class LuaScriptSystemImpl;


void registerEngineLuaAPI(Engine&, Universe&, lua_State* L);
void registerPhysicsLuaAPI(Engine&, Universe&, lua_State* L);


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
	virtual void setWorldEditor(WorldEditor& editor) override;
};


class LuaScriptScene : public IScene
{
public:
	LuaScriptScene(LuaScriptSystem& system, Engine& engine, Universe& universe)
		: m_system(system)
		, m_universe(universe)
		, m_scripts(system.getAllocator())
		, m_valid(system.getAllocator())
		, m_global_state(nullptr)
	{
		if (system.m_engine.getWorldEditor())
		{
			system.m_engine.getWorldEditor()
				->gameModeToggled()
				.bind<LuaScriptScene, &LuaScriptScene::onGameModeToggled>(this);
		}
	}


	~LuaScriptScene()
	{
		if (m_system.m_engine.getWorldEditor())
		{
			m_system.m_engine.getWorldEditor()
				->gameModeToggled()
				.unbind<LuaScriptScene, &LuaScriptScene::onGameModeToggled>(
					this);
		}
	}


	virtual Universe& getUniverse() override { return m_universe; }


	void registerAPI(lua_State* L)
	{
		registerEngineLuaAPI(m_system.m_engine, m_universe, L);
		if (m_system.m_engine.getPluginManager().getPlugin("physics"))
		{
			registerPhysicsLuaAPI(m_system.m_engine, m_universe, L);
		}
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

				FILE* fp = fopen(script.m_path.c_str(), "rb");
				if (fp)
				{
					script.m_state = lua_newthread(m_global_state);
					lua_pushinteger(script.m_state, script.m_entity);
					lua_setglobal(script.m_state, "this");
					fseek(fp, 0, SEEK_END);
					long size = ftell(fp);
					fseek(fp, 0, SEEK_SET);
					Array<char> content(m_system.getAllocator());
					content.resize(size + 1);
					fread(&content[0], 1, size, fp);
					content[size] = '\0';
					bool errors =
						luaL_loadbuffer(
							script.m_state, &content[0], size, script.m_path.c_str()) != LUA_OK;
					errors =
						errors ||
						lua_pcall(script.m_state, 0, LUA_MULTRET, 0) != LUA_OK;
					if (errors)
					{
						g_log_error.log("lua")
							<< script.m_path.c_str() << ": "
							<< lua_tostring(script.m_state, -1);
						lua_pop(script.m_state, 1);
					}
					fclose(fp);
				}
				else
				{
					script.m_state = nullptr;
					g_log_error.log("lua script") << "error loading " << script.m_path.c_str();
				}
			}
		}
	}


	void stopGame() { lua_close(m_global_state); }


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


	virtual ComponentNew createComponent(uint32_t type,
									  const Entity& entity) override
	{
		if (type == LUA_SCRIPT_HASH)
		{
			LuaScriptScene::Script& script = m_scripts.pushEmpty();
			script.m_entity = entity;
			script.m_path = "";
			script.m_state = nullptr;
			m_valid.push(true);
			ComponentOld cmp = m_universe.addComponent(
				entity, type, this, m_scripts.size() - 1);
			m_universe.componentCreated().invoke(cmp);
			return cmp.index;
		}
		return NEW_INVALID_COMPONENT;
	}


	virtual void destroyComponent(ComponentNew component,
								  uint32_t type) override
	{
		if (type == LUA_SCRIPT_HASH)
		{
			m_universe.destroyComponent(ComponentOld(
				Entity(m_scripts[component].m_entity), type, this, component));
			m_valid[component] = false;
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
				m_universe.addComponent(
					Entity(m_scripts[i].m_entity),
					LUA_SCRIPT_HASH,
					this,
					i);
			}
		}
	}


	virtual IPlugin& getPlugin() const override { return m_system; }


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


	void getScriptPath(ComponentNew cmp, string& path)
	{
		path = m_scripts[cmp].m_path.c_str();
	}


	void setScriptPath(ComponentNew cmp, const string& path)
	{
		m_scripts[cmp].m_path = path;
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
	return true;
}


void LuaScriptSystem::setWorldEditor(WorldEditor& editor)
{
	IAllocator& allocator = editor.getAllocator();
	editor.registerComponentType("lua_script", "Lua script");
	TODO("todo");
	/*
	editor.registerProperty(
		"lua_script",
		allocator.newObject<FilePropertyDescriptor<LuaScriptScene>>(
			"source",
			(void (LuaScriptScene::*)(ComponentOld, string&)) &
				LuaScriptScene::getScriptPath,
			&LuaScriptScene::setScriptPath,
			"Lua (*.lua)",
			allocator));
			*/
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
