#include "core/array.h"
#include "core/base_proxy_allocator.h"
#include "core/binary_array.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/iallocator.h"
#include "core/json_serializer.h"
#include "core/library.h"
#include "core/log.h"
#include "core/lua_wrapper.h"
#include "core/resource_manager.h"
#include "debug/allocator.h"
#include "engine.h"
#include "engine/property_descriptor.h"
#include "iplugin.h"
#include "plugin_manager.h"
#include "lua_script/lua_script_manager.h"
#include "universe/universe.h"


static const uint32_t SCRIPT_HASH = Lumix::crc32("script");


namespace Lumix
{


class LuaScriptSystemImpl;


void registerEngineLuaAPI(Engine&, lua_State* L);
void registerUniverse(UniverseContext*, lua_State* L);
void registerPhysicsLuaAPI(Engine&, Universe&, lua_State* L);


static const uint32_t LUA_SCRIPT_HASH = crc32("lua_script");


class LuaScriptSystem : public IPlugin
{
public:
	LuaScriptSystem(Engine& engine);
	virtual ~LuaScriptSystem();

	IAllocator& getAllocator();
	virtual IScene* createScene(UniverseContext& universe) override;
	virtual void destroyScene(IScene* scene) override;
	virtual bool create() override;
	virtual void destroy() override;
	virtual const char* getName() const override;
	void registerProperties();
	LuaScriptManager& getScriptManager() { return m_script_manager; }

	Engine& m_engine;
	Debug::Allocator m_allocator;
	LuaScriptManager m_script_manager;
};


class LuaScriptScene : public IScene
{
public:
	struct Property
	{
		Property(IAllocator& allocator)
			: m_value(allocator)
		{
		}

		string m_value;
		uint32_t m_name_hash;
	};

	struct Script
	{
		Script(IAllocator& allocator)
			: m_properties(allocator)
		{
			m_script = nullptr;
		}

		LuaScript* m_script;
		int m_entity;
		lua_State* m_state;
		Array<Property> m_properties;
	};


public:
	LuaScriptScene(LuaScriptSystem& system, Engine& engine, UniverseContext& ctx)
		: m_system(system)
		, m_universe_context(ctx)
		, m_scripts(system.getAllocator())
		, m_valid(system.getAllocator())
		, m_global_state(nullptr)
	{
	}


	~LuaScriptScene()
	{
		unloadAllScripts();
	}


	void unloadAllScripts()
	{
		for (int i = 0; i < m_scripts.size(); ++i)
		{
			Script& script = m_scripts[i];
			if (m_valid[i] && script.m_script)
			{
				m_system.getScriptManager().unload(*script.m_script);
			}
		}
		m_scripts.clear();
	}


	virtual Universe& getUniverse() override { return *m_universe_context.m_universe; }


	void registerAPI(lua_State* L)
	{
		registerUniverse(&m_universe_context, L);
		registerEngineLuaAPI(m_system.m_engine, L);
		if (m_system.m_engine.getPluginManager().getPlugin("physics"))
		{
			registerPhysicsLuaAPI(m_system.m_engine, *m_universe_context.m_universe, L);
		}
	}


	void applyProperty(Script& script, Property& prop)
	{
		if (prop.m_value.length() == 0)
		{
			return;
		}

		lua_State* state = script.m_state;
		const char* name = script.m_script->getPropertyName(prop.m_name_hash);
		if (!name)
		{
			return;
		}
		lua_pushnil(state);
		lua_setglobal(state, name);
		char tmp[1024];
		copyString(tmp, sizeof(tmp), name);
		catString(tmp, sizeof(tmp), " = ");
		catString(tmp, sizeof(tmp), prop.m_value.c_str());

		bool errors =
			luaL_loadbuffer(state, tmp, strlen(tmp), nullptr) != LUA_OK;
		errors = errors || lua_pcall(state, 0, LUA_MULTRET, 0) != LUA_OK;

		if (errors)
		{
			g_log_error.log("lua") << script.m_script->getPath().c_str() << ": "
								   << lua_tostring(state, -1);
			lua_pop(state, 1);
		}
	}


	void applyProperties(Script& script)
	{
		if (!script.m_script)
		{
			return;
		}
		lua_State* state = script.m_state;
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
		if (nsize > 0 && ptr == nullptr)
		{
			return allocator.allocate(nsize);
		}
		void* new_mem = allocator.allocate(nsize);
		memcpy(new_mem, ptr, Math::minValue(osize, nsize));
		allocator.deallocate(ptr);
		return new_mem;
	}


	virtual void startGame() override
	{
		m_global_state = lua_newstate(luaAllocator, &m_system.getAllocator());
		luaL_openlibs(m_global_state);
		registerAPI(m_global_state);
		for (int i = 0; i < m_scripts.size(); ++i)
		{
			if (m_valid[i] && m_scripts[i].m_script)
			{
				Script& script = m_scripts[i];

				if (script.m_script->isReady())
				{
					script.m_state = lua_newthread(m_global_state);
					lua_pushinteger(script.m_state, script.m_entity);
					lua_setglobal(script.m_state, "this");

					applyProperties(script);

					bool errors =
						luaL_loadbuffer(
							script.m_state,
							script.m_script->getSourceCode(),
							strlen(script.m_script->getSourceCode()),
							script.m_script->getPath().c_str()) != LUA_OK;
					errors =
						errors ||
						lua_pcall(script.m_state, 0, LUA_MULTRET, 0) != LUA_OK;
					if (errors)
					{
						g_log_error.log("lua")
							<< script.m_script->getPath().c_str() << ": "
							<< lua_tostring(script.m_state, -1);
						lua_pop(script.m_state, 1);
					}
				}
				else
				{
					script.m_state = nullptr;
					g_log_error.log("lua script")
						<< "Script "
						<< script.m_script->getPath().c_str()
						<< " is not loaded";
				}
			}
		}
	}


	virtual void stopGame() override
	{
		for (Script& script : m_scripts)
		{
			script.m_state = nullptr;
		}

		lua_close(m_global_state);
		m_global_state = nullptr;
	}

	
	virtual ComponentIndex createComponent(uint32_t type,
										   Entity entity) override
	{
		if (type == LUA_SCRIPT_HASH)
		{
			LuaScriptScene::Script& script =
				m_scripts.emplace(m_system.getAllocator());
			script.m_entity = entity;
			script.m_script = nullptr;
			script.m_state = nullptr;
			m_valid.push(true);
			m_universe_context.m_universe->addComponent(
				entity, type, this, m_scripts.size() - 1);
			return m_scripts.size() - 1;
		}
		return INVALID_COMPONENT;
	}


	virtual void destroyComponent(ComponentIndex component,
								  uint32_t type) override
	{
		if (type == LUA_SCRIPT_HASH)
		{
			m_universe_context.m_universe->destroyComponent(
				Entity(m_scripts[component].m_entity), type, this, component);
			m_valid[component] = false;
		}
	}


	virtual void serialize(OutputBlob& serializer) override
	{
		serializer.write(m_scripts.size());
		for (int i = 0; i < m_scripts.size(); ++i)
		{
			serializer.write(m_scripts[i].m_entity);
			serializer.writeString(
				m_scripts[i].m_script ? m_scripts[i].m_script->getPath().c_str()
									  : "");
			serializer.write((bool)m_valid[i]);
			if (m_valid[i])
			{
				serializer.write(m_scripts[i].m_properties.size());
				for (Property& prop : m_scripts[i].m_properties)
				{
					serializer.write(prop.m_name_hash);
					serializer.writeString(prop.m_value.c_str());
				}
			}
		}
	}


	virtual void deserialize(InputBlob& serializer) override
	{
		int len = serializer.read<int>();
		unloadAllScripts();
		m_scripts.reserve(len);
		m_valid.resize(len);
		for (int i = 0; i < len; ++i)
		{
			Script& script = m_scripts.emplace(m_system.getAllocator());
			serializer.read(m_scripts[i].m_entity);
			char tmp[MAX_PATH_LENGTH];
			serializer.readString(tmp, MAX_PATH_LENGTH);
			m_valid[i] = serializer.read<bool>();
			script.m_script = static_cast<LuaScript*>(
				m_system.getScriptManager().load(Lumix::Path(tmp)));
			script.m_state = nullptr;
			if (m_valid[i])
			{
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
					Entity(m_scripts[i].m_entity), LUA_SCRIPT_HASH, this, i);
			}
		}
	}


	virtual IPlugin& getPlugin() const override { return m_system; }


	virtual void update(float time_delta) override
	{
		if (!m_global_state)
		{
			return;
		}

		if (lua_getglobal(m_global_state, "update") == LUA_TFUNCTION)
		{
			lua_pushnumber(m_global_state, time_delta);
			if (lua_pcall(m_global_state, 1, 0, 0) != LUA_OK)
			{
				g_log_error.log("lua") << lua_tostring(m_global_state, -1);
			}
		}
		else
		{
			lua_pop(m_global_state, 1);
		}
	}


	virtual bool ownComponentType(uint32_t type) const override
	{
		return type == LUA_SCRIPT_HASH;
	}


	const Script& getScript(ComponentIndex cmp) const { return m_scripts[cmp]; }


	void
	setPropertyValue(ComponentIndex cmp, const char* name, const char* value)
	{
		Property& prop = getScriptProperty(cmp, name);
		prop.m_value = value;

		if (m_scripts[cmp].m_state)
		{
			applyProperty(m_scripts[cmp], prop);
		}
	}


	Property& getScriptProperty(ComponentIndex cmp, const char* name)
	{
		uint32_t name_hash = crc32(name);
		for (auto& prop : m_scripts[cmp].m_properties)
		{
			if (prop.m_name_hash == name_hash)
			{
				return prop;
			}
		}

		m_scripts[cmp].m_properties.emplace(m_system.getAllocator());
		auto& prop = m_scripts[cmp].m_properties.back();
		prop.m_name_hash = name_hash;
		return prop;
	}


	const char* getScriptPath(ComponentIndex cmp)
	{
		return m_scripts[cmp].m_script
				   ? m_scripts[cmp].m_script->getPath().c_str()
				   : "";
	}


	void setScriptPath(ComponentIndex cmp, const char* path)
	{
		if (m_scripts[cmp].m_script)
		{
			m_system.getScriptManager().unload(*m_scripts[cmp].m_script);
		}

		m_scripts[cmp].m_script = static_cast<LuaScript*>(
			m_system.getScriptManager().load(Lumix::Path(path)));
	}


private:
	LuaScriptSystem& m_system;

	BinaryArray m_valid;
	Array<Script> m_scripts;
	lua_State* m_global_state;
	UniverseContext& m_universe_context;
};


LuaScriptSystem::LuaScriptSystem(Engine& engine)
	: m_engine(engine)
	, m_allocator(engine.getAllocator())
	, m_script_manager(m_allocator)
{
	m_script_manager.create(crc32("lua_script"), engine.getResourceManager());
	registerProperties();
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
	return m_allocator.newObject<LuaScriptScene>(*this, m_engine, ctx);
}


void LuaScriptSystem::destroyScene(IScene* scene)
{
	m_allocator.deleteObject(scene);
}


bool LuaScriptSystem::create()
{
	return true;
}


void LuaScriptSystem::registerProperties()
{
	IAllocator& allocator = m_engine.getAllocator();
	m_engine.registerComponentType("lua_script", "Lua script");
	m_engine.registerProperty(
		"lua_script",
		allocator.newObject<FilePropertyDescriptor<LuaScriptScene>>(
			"source",
			&LuaScriptScene::getScriptPath,
			&LuaScriptScene::setScriptPath,
			"Lua (*.lua)",
			allocator));
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
