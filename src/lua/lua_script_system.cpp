#include "core/allocator.h"
#include "core/array.h"
#include "core/associative_array.h"
#include "core/hash.h"
#include "core/log.h"
#include "core/metaprogramming.h"
#include "core/os.h"
#include "core/profiler.h"
#include "core/stream.h"
#include "core/string.h"
#include "engine/component_types.h"
#include "engine/engine.h"
#include "engine/input_system.h"
#include "engine/plugin.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "gui/gui_module.h"
#include "lua_script.h"
#include "lua_script_system.h"
#include "lua_wrapper.h"
#include <luacode.h>


namespace Lumix {

static const char* toString(InputSystem::Device::Type type) {
	switch (type) {
		case InputSystem::Device::KEYBOARD: return "keyboard";
		case InputSystem::Device::MOUSE: return "mouse";
		case InputSystem::Device::CONTROLLER: return "controller";
	}
	ASSERT(false);
	return "N/A";
}

static const char* toString(InputSystem::Event::Type type) {
	switch (type) {
		case InputSystem::Event::AXIS: return "axis";
		case InputSystem::Event::BUTTON: return "button";
		case InputSystem::Event::TEXT_INPUT: return "text_input";
		case InputSystem::Event::DEVICE_ADDED: return "device_added";
		case InputSystem::Event::DEVICE_REMOVED: return "device_removed";
	}
	ASSERT(false);
	return "N/A";
}

enum class LuaModuleVersion : i32 {
	HASH64,
	INLINE_SCRIPT,
	ARRAY_PROPERTIES,

	LATEST
};


inline void toCString(EntityPtr value, Span<char> output) {
	toCString(value.index, output);
}

inline const char* fromCString(StringView input, EntityPtr& value) {
	return fromCString(input, value.index);
}

template <typename T> static T fromString(const char* val) {
	T res;
	fromCString(val, res);
	return res;
}

template <> const char* fromString(const char* val) { return val; }
template <> float fromString(const char* val) { return (float)atof(val); }
template <> bool fromString(const char* val) { return equalIStrings(val, "true"); }
template <> Vec3 fromString(const char* val) { 
	if (val[0] == '\0') return {};
	Vec3 r;
	r.x = (float)atof(val + 1);
	const char* c = strstr(val + 1, ",");
	r.y = (float)atof(c + 1);
	c = strstr(val + 1, ",");
	r.z = (float)atof(c + 1);
	return r;
}

template <typename T> static void toString(T val, String& out) {
	char tmp[128];
	toCString(val, Span(tmp));
	out = tmp;
}

template <> void toString(float val, String& out) {
	char tmp[128];
	toCString(val, Span(tmp), 10);
	out = tmp;
}

template <> void toString(Vec3 val, String& out) {
	StaticString<512> tmp("{", val.x, ", ", val.y, ", ", val.z, "}");
	out = tmp;
}

struct LuaScriptManager final : ResourceManager
{
	LuaScriptManager(IAllocator& allocator)
		: ResourceManager(allocator)
		, m_allocator(allocator)
	{
	}

	Resource* createResource(const Path& path) override {
		return LUMIX_NEW(m_allocator, LuaScript)(path, *this, m_allocator);
	}

	void destroyResource(Resource& resource) override {
		LUMIX_DELETE(m_allocator, static_cast<LuaScript*>(&resource));
	}

	IAllocator& m_allocator;
};

void registerEngineAPI(lua_State* L, Engine* engine);


struct LuaScriptSystemImpl final : LuaScriptSystem
{
	explicit LuaScriptSystemImpl(Engine& engine);
	virtual ~LuaScriptSystemImpl();

	void initEnd() override {
		PROFILE_FUNCTION();
		registerEngineAPI(m_state, &m_engine);
	}

	void createModules(World& world) override;
	const char* getName() const override { return "lua_script"; }
	LuaScriptManager& getScriptManager() { return m_script_manager; }
	void serialize(OutputMemoryStream& stream) const override {}
	bool deserialize(i32 version, InputMemoryStream& stream) override { return version == 0; }
	lua_State* getState() override { return m_state; }

	void update(float dt) override {
		static u32 lua_mem_counter = profiler::createCounter("Lua Memory (KB)", 0);
		profiler::pushCounter(lua_mem_counter, float(double(m_lua_allocated) / 1024.0));
	}

	void unloadLuaResource(LuaResourceHandle resource) override
	{
		auto iter = m_lua_resources.find(resource);
		if (!iter.isValid()) return;
		Resource* res = iter.value();
		m_lua_resources.erase(iter);
		res->decRefCount();
	}

	LuaResourceHandle addLuaResource(const Path& path, ResourceType type) override
	{
		Resource* res = m_engine.getResourceManager().load(type, path);
		if (!res) return 0xffFFffFF;
		++m_last_lua_resource_idx;
		ASSERT(m_last_lua_resource_idx != 0xffFFffFF);
		m_lua_resources.insert(m_last_lua_resource_idx, res);
		return m_last_lua_resource_idx;
	}

	Resource* getLuaResource(LuaResourceHandle resource) const override
	{
		auto iter = m_lua_resources.find(resource);
		if (iter.isValid()) return iter.value();
		return nullptr;
	}

	TagAllocator m_allocator;
	TagAllocator m_lua_allocator;
	lua_State* m_state;
	Engine& m_engine;
	LuaScriptManager m_script_manager;
	size_t m_lua_allocated = 0;
	HashMap<int, Resource*> m_lua_resources;
	u32 m_last_lua_resource_idx = -1;
};


struct LuaScriptModuleImpl final : LuaScriptModule {
	struct TimerData {
		float time;
		lua_State* state;
		int func;
	};

	struct CallbackData {
		lua_State* state;
		int environment;
	};

	struct ScriptComponent;

	struct ScriptEnvironment {
		lua_State* m_state = nullptr;
		int m_environment = -1;
		int m_thread_ref = -1;
	};

	struct ScriptInstance : ScriptEnvironment {
		enum Flags : u32 {
			NONE = 0,
			ENABLED = 1 << 0,
			LOADED = 1 << 1,
			MOVED_FROM = 1 << 2
		};

		explicit ScriptInstance(ScriptComponent& cmp, IAllocator& allocator)
			: m_properties(allocator)
			, m_cmp(&cmp)
		{
			LuaScriptModuleImpl& module = cmp.m_module;
			lua_State* L = module.m_system.m_state;
			LuaWrapper::DebugGuard guard(L);
			m_state = lua_newthread(L);
			m_thread_ref = LuaWrapper::createRef(L);
			lua_pop(L, 1); // []
			lua_newtable(m_state); // [env]
			m_environment = LuaWrapper::createRef(m_state); // [env]

			// environment's metatable & __index
			lua_pushvalue(m_state, -1); // [env, env]
			lua_setmetatable(m_state, -2); // [env]
			lua_pushvalue(m_state, LUA_GLOBALSINDEX); // [evn, _G]
			lua_setfield(m_state, -2, "__index");  // [env]

			// set this
			lua_getglobal(m_state, "Lumix"); // [env, Lumix]
			lua_getfield(m_state, -1, "Entity"); // [env, Lumix, Lumix.Entity]
			lua_remove(m_state, -2); // [env, Lumix.Entity]
			lua_getfield(m_state, -1, "new"); // [env, Lumix.Entity, Entity.new]
			lua_pushvalue(m_state, -2); // [env, Lumix.Entity, Entity.new, Lumix.Entity]
			lua_remove(m_state, -3); // [env, Entity.new, Lumix.Entity]
			LuaWrapper::push(m_state, &module.m_world); // [env, Entity.new, Lumix.Entity, world]
			LuaWrapper::push(m_state, cmp.m_entity.index); // [env, Entity.new, Lumix.Entity, world, entity_index]
			const bool error = !LuaWrapper::pcall(m_state, 3, 1); // [env, entity]
			ASSERT(!error);
			lua_setfield(m_state, -2, "this"); // [env]
			lua_pop(m_state, 1); // []

			m_flags = Flags(m_flags | ENABLED);
		}

		ScriptInstance(const ScriptInstance&) = delete;

		ScriptInstance(ScriptInstance&& rhs) 
			: m_properties(rhs.m_properties.move())
			, m_cmp(rhs.m_cmp)
			, m_script(rhs.m_script)
			, m_flags(rhs.m_flags)
		{
			m_environment = rhs.m_environment;
			m_thread_ref = rhs.m_thread_ref;
			m_state = rhs.m_state;
			rhs.m_script = nullptr;
			rhs.m_flags = Flags(rhs.m_flags | MOVED_FROM);
		}

		void operator =(ScriptInstance&& rhs) 
		{
			m_properties = rhs.m_properties.move();
			m_environment = rhs.m_environment;
			m_thread_ref = rhs.m_thread_ref;
			m_cmp = rhs.m_cmp;
			m_script = rhs.m_script;
			m_state = rhs.m_state;
			m_flags = rhs.m_flags;
			rhs.m_script = nullptr;
			rhs.m_flags = Flags(rhs.m_flags | MOVED_FROM);
		}

		~ScriptInstance() {
			if (!(m_flags & MOVED_FROM)) {
				if (m_script) {
					m_script->getObserverCb().unbind<&ScriptComponent::onScriptLoaded>(m_cmp);
					m_script->decRefCount();
				}
				lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_environment); // [env]
				ASSERT(lua_type(m_state, -1) == LUA_TTABLE);
				lua_getfield(m_state, -1, "onDestroy"); // [env, onDestroy]
				if (lua_type(m_state, -1) != LUA_TFUNCTION) {
					lua_pop(m_state, 2); // []
				}
				else {
					if (lua_pcall(m_state, 0, 0, 0) != 0) { // [env]
						logError(lua_tostring(m_state, -1));
						lua_pop(m_state, 1);
					}
					lua_pop(m_state, 1); // []
				}

				m_cmp->m_module.disableScript(*this);

				lua_State* L = m_cmp->m_module.m_system.m_state;
				LuaWrapper::releaseRef(L, m_thread_ref);
				LuaWrapper::releaseRef(m_state, m_environment);
			}
		}

		void onScriptLoaded(LuaScriptModuleImpl& module, ScriptComponent& cmp, int scr_index);
		void onScriptUnloaded(LuaScriptModuleImpl& module, ScriptComponent& cmp, int scr_index);

		ScriptComponent* m_cmp;
		LuaScript* m_script = nullptr;
		Array<Property> m_properties;
		Flags m_flags = Flags::NONE;
	};

	struct InlineScriptComponent : ScriptEnvironment {
		InlineScriptComponent(EntityRef entity, LuaScriptModuleImpl& module, IAllocator& allocator)
			: m_source(allocator)
			, m_entity(entity)
			, m_module(module)
		{
			lua_State* L = module.m_system.m_state;
			m_state = lua_newthread(L);
			m_thread_ref = LuaWrapper::createRef(L);
			lua_pop(L, 1); // []
			lua_newtable(m_state);						   // [env]
			// reference environment
			m_environment = LuaWrapper::createRef(m_state); // [env]

			// environment's metatable & __index
			lua_pushvalue(m_state, -1);				  // [env, env]
			lua_setmetatable(m_state, -2);			  // [env]
			lua_pushvalue(m_state, LUA_GLOBALSINDEX); // [evn, _G]
			lua_setfield(m_state, -2, "__index");	  // [env]

			// set this
			lua_getglobal(m_state, "Lumix");					  // [env, Lumix]
			lua_getfield(m_state, -1, "Entity");				  // [env, Lumix, Lumix.Entity]
			lua_remove(m_state, -2);							  // [env, Lumix.Entity]
			lua_getfield(m_state, -1, "new");					  // [env, Lumix.Entity, Entity.new]
			lua_pushvalue(m_state, -2);							  // [env, Lumix.Entity, Entity.new, Lumix.Entity]
			lua_remove(m_state, -3);							  // [env, Entity.new, Lumix.Entity]
			LuaWrapper::push(m_state, &module.m_world);		  // [env, Entity.new, Lumix.Entity, world]
			LuaWrapper::push(m_state, entity.index);		  // [env, Entity.new, Lumix.Entity, world, entity_index]
			const bool error = !LuaWrapper::pcall(m_state, 3, 1); // [env, entity]
			ASSERT(!error);
			lua_setfield(m_state, -2, "this"); // [env]
			lua_pop(m_state, 1);			   // []
		}

		InlineScriptComponent(InlineScriptComponent&& rhs)
			: m_module(rhs.m_module)
			, m_source(rhs.m_source)
		{
			m_environment = rhs.m_environment;
			m_thread_ref = rhs.m_thread_ref;
			m_state = rhs.m_state;
			rhs.m_state = nullptr;
		}

		void operator=(InlineScriptComponent&& rhs) = delete;
			
		~InlineScriptComponent() {
			if (!m_state) return;
			
			m_module.disableScript(*this);

			lua_State* L = m_module.m_system.m_state;
			LuaWrapper::releaseRef(L, m_thread_ref);
			LuaWrapper::releaseRef(m_state, m_environment);
		}

		void runSource() {
			lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_environment); // [env]
			ASSERT(lua_type(m_state, -1) == LUA_TTABLE);

			bool errors = LuaWrapper::luaL_loadbuffer(m_state, m_source.c_str(), m_source.length(), "inline script") != 0; // [env, func]

			if (errors) {
				logError("Inline script, entity ", m_entity.index ,": ", lua_tostring(m_state, -1));
				lua_pop(m_state, 2);
				return;
			}

			lua_pushvalue(m_state, -2); // [env, func, env]
			lua_setfenv(m_state, -2);

			errors = lua_pcall(m_state, 0, 0, 0) != 0; // [env]
			if (errors) {
				logError("Inline script, entity ", m_entity.index, ": ", lua_tostring(m_state, -1));
				lua_pop(m_state, 1);
			}
			lua_pop(m_state, 1); // []
		}

		LuaScriptModuleImpl& m_module;
		EntityRef m_entity;
		String m_source;
	};

	struct ScriptComponent
	{
		ScriptComponent(LuaScriptModuleImpl& module, EntityRef entity, IAllocator& allocator)
			: m_scripts(allocator)
			, m_module(module)
			, m_entity(entity)
		{
		}


		static int getPropertyLegacy(ScriptInstance& inst, const char* name) {
			const StableHash32 hash(name);
			for(int i = 0, c = inst.m_properties.size(); i < c; ++i)
			{
				if (inst.m_properties[i].name_hash_legacy == hash) {
					inst.m_properties[i].name_hash = StableHash(name);
					inst.m_properties[i].name_hash_legacy = StableHash32();
					return i;
				}
			}
			return -1;
		}

		static int getProperty(ScriptInstance& inst, StableHash hash)
		{
			for(int i = 0, c = inst.m_properties.size(); i < c; ++i)
			{
				if (inst.m_properties[i].name_hash == hash) return i;
			}
			return -1;
		}

		bool isResource(lua_State* L, i32 idx, ResourceType* resource_type) {
			ASSERT(resource_type);
			lua_getmetatable(L, idx); // mt
			lua_getglobal(L, "Lumix");  // mt, Lumix
			lua_getfield(L, -1, "Resource"); // mt, Lumix, Resource
			bool is_instance = lua_equal(L, -1, -3);
			lua_pop(L, 3);
			if (!is_instance) return false;
			lua_getfield(L, idx, "_type");
			resource_type->type = RuntimeHash::fromU64((u64)lua_tolightuserdata(L, -1));
			lua_pop(L, 1);
			return true;
		}

		bool isLumixClass(lua_State* L, i32 idx, const char* class_name) {
			lua_getmetatable(L, idx); // mt
			lua_getglobal(L, "Lumix");  // mt, Lumix
			lua_getfield(L, -1, class_name); // mt, Lumix, class
			bool is_instance = lua_equal(L, -1, -3);
			lua_pop(L, 3);
			return is_instance;
		}

		void detectProperties(ScriptInstance& inst) {
			static const StableHash INDEX_HASH("__index");
			static const StableHash THIS_HASH("this");
			IAllocator& allocator = m_module.m_system.m_allocator;
			u8 valid_properties[256];
			if (inst.m_properties.size() >= sizeof(valid_properties) * 8) {
				logError("Too many properties in ", inst.m_script->getPath(), ", entity ", m_entity.index, ". Some will be ignored.");
				inst.m_properties.shrink(sizeof(valid_properties) * 8);
			}
			memset(valid_properties, 0, (inst.m_properties.size() + 7) / 8);
			
			lua_State* L = inst.m_state;
			lua_rawgeti(L, LUA_REGISTRYINDEX, inst.m_environment); // [env]
			ASSERT(lua_type(L, -1) == LUA_TTABLE);
			lua_pushnil(L); // [env, nil]
			while (lua_next(L, -2)) { // [env, key, value] | [env]
				if (lua_type(L, -1) == LUA_TFUNCTION) {
					lua_pop(L, 1); // [env, key]
					continue;
				}
				
				const char* name = lua_tostring(L, -2);
				if(!name || name[0] == '_' || equalStrings(name, "enabled")) {
					lua_pop(L, 1); // [env, key]
					continue;
				}

				const StableHash hash(name);
				if (hash == INDEX_HASH || hash == THIS_HASH) {
					lua_pop(L, 1); // [env, key]
					continue;
				}

				if (!m_module.m_property_names.find(hash).isValid()) {
					m_module.m_property_names.insert(hash, String(name, allocator));
				}

				i32 prop_index = getProperty(inst, hash);
				if (prop_index < 0) prop_index = getPropertyLegacy(inst, name);
				if (prop_index >= 0) {
					valid_properties[prop_index / 8] |=  1 << (prop_index % 8);
					Property& existing_prop = inst.m_properties[prop_index];
					if (existing_prop.type == Property::ANY) {
						switch (lua_type(inst.m_state, -1)) {
							case LUA_TBOOLEAN: existing_prop.type = Property::BOOLEAN; break;
							case LUA_TSTRING: existing_prop.type = Property::STRING; break;
							case LUA_TTABLE: {
								if (isLumixClass(inst.m_state, -1, "Entity")) existing_prop.type = Property::ENTITY;
								else if (isResource(inst.m_state, -1, &existing_prop.resource_type)) existing_prop.type = Property::RESOURCE;
								else existing_prop.type = Property::COLOR;
								break;
							}
							default: existing_prop.type = Property::FLOAT;
						}
					}
					InputMemoryStream stream(existing_prop.stored_value);
					m_module.applyProperty(inst, name, existing_prop, stream);
				}
				else {
					const i32 size = inst.m_properties.size();
					if (inst.m_properties.size() < sizeof(valid_properties) * 8) {
						auto& prop = inst.m_properties.emplace(allocator);
						valid_properties[size / 8] |= 1 << (size % 8);
						switch (lua_type(inst.m_state, -1)) {
							case LUA_TBOOLEAN: prop.type = Property::BOOLEAN; break;
							case LUA_TSTRING: prop.type = Property::STRING; break;
							case LUA_TTABLE: {
								if (isLumixClass(inst.m_state, -1, "Entity")) prop.type = Property::ENTITY;
								else if (isResource(inst.m_state, -1, &prop.resource_type)) prop.type = Property::RESOURCE;
								else prop.type = Property::COLOR;
								break;
							}
							default: prop.type = Property::FLOAT;
						}
						prop.name_hash = hash;
					}
					else {
						logError("Too many properties in ", inst.m_script->getPath(), ", entity ", m_entity.index, ". Some will be ignored.");
					}
				}
				lua_pop(L, 1); // [env, key]
			}
			
			// [env]
			for (int i = inst.m_properties.size() - 1; i >= 0; --i) {
				if (valid_properties[i / 8] & (1 << (i % 8))) continue;
				inst.m_properties.swapAndPop(i);
			}
			lua_pop(L, 1);
		}

		void onScriptLoaded(Resource::State old_state, Resource::State new_state, Resource& resource) {
			for (int scr_index = 0, c = m_scripts.size(); scr_index < c; ++scr_index)
			{
				ScriptInstance& script = m_scripts[scr_index];
					
				if (!script.m_script) continue;
				if (script.m_script != &resource) continue;
				if (new_state == Resource::State::READY) {
					script.onScriptLoaded(m_module, *this, scr_index);
				}
				else if (new_state == Resource::State::EMPTY) {
					script.onScriptUnloaded(m_module, *this, scr_index);
				}
			}
		}

		Array<ScriptInstance> m_scripts;
		LuaScriptModuleImpl& m_module;
		EntityRef m_entity;
	};


	struct FunctionCall : IFunctionCall
	{
		void add(int parameter) override
		{
			lua_pushinteger(state, parameter);
			++parameter_count;
		}

		void add(EntityPtr parameter) override {
			LuaWrapper::pushEntity(state, parameter, world);
			++parameter_count;
		}

		void add(bool parameter) override
		{
			lua_pushboolean(state, parameter);
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
			ASSERT(lua_type(state, -1) == LUA_TTABLE);
			++parameter_count;
		}

		World* world;
		int parameter_count;
		lua_State* state;
		bool is_in_progress;
	};


	LuaScriptModuleImpl(LuaScriptSystemImpl& system, World& world)
		: m_system(system)
		, m_world(world)
		, m_deferred_destructions(system.m_allocator)
		, m_deferred_partition_destructions(system.m_allocator)
		, m_scripts(system.m_allocator)
		, m_inline_scripts(system.m_allocator)
		, m_updates(system.m_allocator)
		, m_input_handlers(system.m_allocator)
		, m_timers(system.m_allocator)
		, m_property_names(system.m_allocator)
		, m_is_game_running(false)
		, m_is_api_registered(false)
		, m_to_start(system.m_allocator)
	{
		m_function_call.is_in_progress = false;
		registerAPI();
	}


	int getVersion() const override { return (int)LuaModuleVersion::LATEST; }

	const char* getName() const override { return "lua_script"; }

	IFunctionCall* beginFunctionCall(const ScriptEnvironment& env, const char* function) {
		lua_rawgeti(env.m_state, LUA_REGISTRYINDEX, env.m_environment);
		ASSERT(lua_type(env.m_state, -1) == LUA_TTABLE);
		lua_getfield(env.m_state, -1, function);
		if (lua_type(env.m_state, -1) != LUA_TFUNCTION) {
			lua_pop(env.m_state, 2);
			return nullptr;
		}

		m_function_call.state = env.m_state;
		m_function_call.world = &m_world;
		m_function_call.is_in_progress = true;
		m_function_call.parameter_count = 0;

		return &m_function_call;
	}

	IFunctionCall* beginFunctionCallInlineScript(EntityRef entity, const char* function) override {
		ASSERT(!m_function_call.is_in_progress);
		auto iter = m_inline_scripts.find(entity);
		if (!iter.isValid()) return nullptr;

		InlineScriptComponent& script = iter.value();
		if (!script.m_state) return nullptr;

		return beginFunctionCall(script, function);
	}
		
	IFunctionCall* beginFunctionCall(EntityRef entity, int scr_index, const char* function) override
	{
		ASSERT(!m_function_call.is_in_progress);
		auto iter = m_scripts.find(entity);
		if (!iter.isValid()) return nullptr;

		ScriptComponent* script_cmp = iter.value();
		auto& script = script_cmp->m_scripts[scr_index];
		if (!script.m_state) return nullptr;

		return beginFunctionCall(script, function);
	}


	void endFunctionCall() override
	{
		ASSERT(m_function_call.is_in_progress);

		m_function_call.is_in_progress = false;

		LuaWrapper::pcall(m_function_call.state, m_function_call.parameter_count, 0);
		lua_pop(m_function_call.state, 1);
	}


	int getPropertyCount(EntityRef entity, int scr_index) override
	{
		return m_scripts[entity]->m_scripts[scr_index].m_properties.size();
	}


	const char* getPropertyName(EntityRef entity, int scr_index, int prop_index) override
	{
		return getPropertyName(m_scripts[entity]->m_scripts[scr_index].m_properties[prop_index].name_hash);
	}

	const Property& getProperty(EntityRef entity, int scr_index, int prop_index) override {
		return m_scripts[entity]->m_scripts[scr_index].m_properties[prop_index];
	}

	~LuaScriptModuleImpl() {
		Path invalid_path;
		for (auto* script_cmp : m_scripts) {
			ASSERT(script_cmp);
			LUMIX_DELETE(m_system.m_allocator, script_cmp);
		}
	}

	bool execute(EntityRef entity, i32 scr_index, StringView code) override {
		const ScriptInstance& script = m_scripts[entity]->m_scripts[scr_index]; 

		lua_State* state = script.m_state;
		if (!state) return false;

		bool errors = LuaWrapper::luaL_loadbuffer(state, code.begin, code.size(), nullptr) != 0;
		if (errors) {
			logError(lua_tostring(state, -1));
			lua_pop(state, 1);
			return false;
		}

		lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment);
		ASSERT(lua_type(script.m_state, -1) == LUA_TTABLE);
		lua_setfenv(script.m_state, -2);

		errors = lua_pcall(state, 0, 0, 0) != 0;

		if (errors) {
			logError(script.m_script->getPath(), ": ", lua_tostring(state, -1));
			lua_pop(state, 1);
			return false;
		}
		return true;
	}

	lua_State* getState(EntityRef entity, int scr_index) override
	{
		return m_scripts[entity]->m_scripts[scr_index].m_state;
	}


	World& getWorld() override { return m_world; }


	static int setArrayPropertyType(lua_State* L) {
		LuaWrapper::DebugGuard guard(L);
		LuaWrapper::checkTableArg(L, 1);
		const char* prop_name = LuaWrapper::checkArg<const char*>(L, 2);
		int type = LuaWrapper::checkArg<int>(L, 3);
		ResourceType resource_type;
		if (type == Property::Type::RESOURCE) {
			resource_type = ResourceType(LuaWrapper::checkArg<const char*>(L, 4));
		}

		lua_getfield(L, 1, "world");
		if (!lua_istable(L, -1)) {
			luaL_error(L, "%s", "Invalid `this.world`");
		}

		lua_getfield(L, -1, "value");
		if (!lua_islightuserdata(L, -1)) {
			luaL_error(L, "%s", "Invalid `this.world.value`");
		}

		auto* world = LuaWrapper::toType<World*>(L, -1);
		auto* module = (LuaScriptModuleImpl*)world->getModule(types::lua_script);

		lua_pop(L, 2);
		const StableHash prop_name_hash(prop_name);
		const StableHash32 prop_name_hash32(prop_name);
		for (auto& prop : module->m_current_script_instance->m_properties) {
			if (prop.name_hash == prop_name_hash || prop.name_hash_legacy == prop_name_hash32) {
				prop.type = (Property::Type)type;
				prop.is_array = true;
				prop.resource_type = resource_type;
				return 0;
			}
		}

		auto& prop = module->m_current_script_instance->m_properties.emplace(module->m_system.m_allocator);
		prop.name_hash = prop_name_hash;
		prop.type = (Property::Type)type;
		prop.resource_type = resource_type;
		prop.is_array = true;
		if (!module->m_property_names.find(prop_name_hash).isValid())
		{
			module->m_property_names.insert(prop_name_hash, String(prop_name, module->m_system.m_allocator));
		}
		return 0;
	}

	static int setPropertyType(lua_State* L) {
		LuaWrapper::DebugGuard guard(L);
		LuaWrapper::checkTableArg(L, 1);
		const char* prop_name = LuaWrapper::checkArg<const char*>(L, 2);
		int type = LuaWrapper::checkArg<int>(L, 3);
		ResourceType resource_type;
		if (type == Property::Type::RESOURCE) {
			resource_type = ResourceType(LuaWrapper::checkArg<const char*>(L, 4));
		}

		lua_getfield(L, 1, "world");
		if (!lua_istable(L, -1)) {
			luaL_error(L, "%s", "Invalid `this.world`");
		}

		lua_getfield(L, -1, "value");
		if (!lua_islightuserdata(L, -1)) {
			luaL_error(L, "%s", "Invalid `this.world.value`");
		}

		auto* world = LuaWrapper::toType<World*>(L, -1);
		auto* module = (LuaScriptModuleImpl*)world->getModule(types::lua_script);

		lua_pop(L, 2);
		const StableHash prop_name_hash(prop_name);
		const StableHash32 prop_name_hash32(prop_name);
		for (auto& prop : module->m_current_script_instance->m_properties) {
			if (prop.name_hash == prop_name_hash || prop.name_hash_legacy == prop_name_hash32) {
				prop.type = (Property::Type)type;
				prop.resource_type = resource_type;
				return 0;
			}
		}

		auto& prop = module->m_current_script_instance->m_properties.emplace(module->m_system.m_allocator);
		prop.name_hash = prop_name_hash;
		prop.type = (Property::Type)type;
		prop.resource_type = resource_type;
		if (!module->m_property_names.find(prop_name_hash).isValid())
		{
			module->m_property_names.insert(prop_name_hash, String(prop_name, module->m_system.m_allocator));
		}
		return 0;
	}

	static int rescan(lua_State* L) {
		const auto* world = LuaWrapper::checkArg<World*>(L, 1);
		const EntityRef entity = LuaWrapper::checkArg<EntityRef>(L, 2);
		const int scr_index = LuaWrapper::checkArg<int>(L, 3);

		if (!world->hasComponent(entity, types::lua_script)) {
			return 0;
		}
			
		LuaScriptModuleImpl* module = (LuaScriptModuleImpl*)world->getModule(types::lua_script);

		const int count = module->getScriptCount(entity);
		if (scr_index >= count) {
			return 0;
		}

		const ScriptInstance& instance = module->m_scripts[entity]->m_scripts[scr_index];
		LuaWrapper::DebugGuard guard(instance.m_state);
		lua_rawgeti(instance.m_state, LUA_REGISTRYINDEX, instance.m_environment);
		if (lua_type(instance.m_state, -1) != LUA_TTABLE) {
			ASSERT(false);
			lua_pop(instance.m_state, 1);
			return 0;
		}
		lua_getfield(instance.m_state, -1, "update");
		if (lua_type(instance.m_state, -1) == LUA_TFUNCTION) {
			auto& update_data = module->m_updates.emplace();
			update_data.state = instance.m_state;
			update_data.environment = instance.m_environment;
		}
		lua_pop(instance.m_state, 1);
		lua_getfield(instance.m_state, -1, "onInputEvent");
		if (lua_type(instance.m_state, -1) == LUA_TFUNCTION) {
			auto& callback = module->m_input_handlers.emplace();
			callback.state = instance.m_state;
			callback.environment = instance.m_environment;
		}
		lua_pop(instance.m_state, 1);
		lua_pop(instance.m_state, 1);

		return 0;
	}
		
	static int lua_new_module(lua_State* L) {
		LuaWrapper::DebugGuard guard(L, 1);
		LuaWrapper::checkTableArg(L, 1); // self
		IModule* module = LuaWrapper::checkArg<IModule*>(L, 2);
			
		lua_newtable(L);
		LuaWrapper::setField(L, -1, "_module", module);
		lua_pushvalue(L, 1);
		lua_setmetatable(L, -2);
		return 1;
	}

	void cancelTimer(int timer_func)
	{
		for (int i = 0, c = m_timers.size(); i < c; ++i)
		{
			if (m_timers[i].func == timer_func)
			{
				m_timers.swapAndPop(i);
				break;
			}
		}
	}


	static int setTimer(lua_State* L)
	{
		auto* module = LuaWrapper::checkArg<LuaScriptModuleImpl*>(L, 1);
		float time = LuaWrapper::checkArg<float>(L, 2);
		if (!lua_isfunction(L, 3)) LuaWrapper::argError(L, 3, "function");
		TimerData& timer = module->m_timers.emplace();
		timer.time = time;
		timer.state = L;
		lua_pushvalue(L, 3);
		timer.func = LuaWrapper::createRef(L);
		lua_pop(L, 1);
		LuaWrapper::push(L, timer.func);
		return 1;
	}


	void registerAPI() {
		if (m_is_api_registered) return;

		m_is_api_registered = true;

		lua_State* L = m_system.m_state;
			
		LuaWrapper::createSystemFunction(L, "Editor", "setPropertyType", &LuaWrapper::wrap<&setPropertyType>);
		LuaWrapper::createSystemFunction(L, "Editor", "setArrayPropertyType", &LuaWrapper::wrap<&setArrayPropertyType>);
		LuaWrapper::createSystemVariable(L, "Editor", "BOOLEAN_PROPERTY", Property::BOOLEAN);
		LuaWrapper::createSystemVariable(L, "Editor", "FLOAT_PROPERTY", Property::FLOAT);
		LuaWrapper::createSystemVariable(L, "Editor", "INT_PROPERTY", Property::INT);
		LuaWrapper::createSystemVariable(L, "Editor", "ENTITY_PROPERTY", Property::ENTITY);
		LuaWrapper::createSystemVariable(L, "Editor", "RESOURCE_PROPERTY", Property::RESOURCE);
		LuaWrapper::createSystemVariable(L, "Editor", "COLOR_PROPERTY", Property::COLOR);
		
		LuaWrapper::createSystemFunction(L, "LuaScript", "rescan", &LuaScriptModuleImpl::rescan);
		LuaWrapper::createSystemFunction(L, "LuaScript", "cancelTimer", &LuaWrapper::wrapMethod<&LuaScriptModuleImpl::cancelTimer>); 
		LuaWrapper::createSystemFunction(L, "LuaScript", "setTimer", &LuaScriptModuleImpl::setTimer);
	}


	int getEnvironment(EntityRef entity, int scr_index) override {
		const Array<ScriptInstance>& scripts = m_scripts[entity]->m_scripts;
		if (scr_index >= scripts.size()) return -1;
		return scripts[scr_index].m_environment;
	}

	int getInlineEnvironment(EntityRef entity) override {
		const InlineScriptComponent& script = m_inline_scripts[entity];
		return script.m_environment;
	}


	const char* getPropertyName(StableHash name_hash) const
	{
		auto iter = m_property_names.find(name_hash);
		if (iter.isValid()) return iter.value().c_str();
		return "";
	}

	void applyProperty(ScriptInstance& script, const char* name, Property& prop, InputMemoryStream& stream) {
		if (stream.size() == 0) return;

		lua_State* L = script.m_state;
		ASSERT(L);
		
		LuaWrapper::DebugGuard guarD(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, script.m_environment);
		
		auto pushValue = [&]() {
			switch (prop.type) {
				case Property::ANY:
					ASSERT(false);
					break;
				case Property::RESOURCE: {
					const char* path = stream.readString();
					pushResource(L, path, prop.resource_type);
					break;
				}
				case Property::ENTITY: {
					EntityPtr e = stream.read<EntityPtr>();
					LuaWrapper::pushEntity(L, e, &m_world);
					break;
				}
				case Property::FLOAT: {
					float val = stream.read<float>();
					LuaWrapper::push(L, val);
					break;
				}
				case Property::BOOLEAN: {
					bool val = stream.read<u8>() != 0;
					LuaWrapper::push(L, val);
					break;
				}
				case Property::INT: {
					i32 val = stream.read<i32>();
					LuaWrapper::push(L, val);
					break;
				}
				case Property::COLOR: {
					Vec3 val = stream.read<Vec3>();
					LuaWrapper::push(L, val);
					break;
				}
				case Property::STRING: {
					const char* val = stream.readString();
					LuaWrapper::push(L, val);
					break;
				}
			}
		};

		if (prop.is_array) {
			lua_newtable(L);
			int array_idx = 1;
			const u32 count = stream.read<u32>();
			for (u32 i = 0; i < count; ++i) {
				pushValue();
				lua_rawseti(L, -2, array_idx++);
			}
			lua_setfield(L, -2, name);
		}
		else {
			pushValue();
			lua_setfield(L, -2, name);
		}
		lua_pop(L, 1);
	}

	const char* getPropertyName(EntityRef entity, int scr_index, int index) const
	{
		auto& script = m_scripts[entity]->m_scripts[scr_index];

		return getPropertyName(script.m_properties[index].name_hash);
	}


	int getPropertyCount(EntityRef entity, int scr_index) const
	{
		auto& script = m_scripts[entity]->m_scripts[scr_index];

		return script.m_properties.size();
	}

	void disableScript(ScriptEnvironment& inst)
	{
		if (!inst.m_state) return;
		for (int i = 0; i < m_timers.size(); ++i)
		{
			if (m_timers[i].state == inst.m_state)
			{
				LuaWrapper::releaseRef(m_timers[i].state, m_timers[i].func);
				m_timers.swapAndPop(i);
				--i;
			}
		}

		for (int i = 0; i < m_updates.size(); ++i)
		{
			if (m_updates[i].state == inst.m_state)
			{
				m_updates.swapAndPop(i);
				break;
			}
		}

		for (int i = 0; i < m_input_handlers.size(); ++i)
		{
			if (m_input_handlers[i].state == inst.m_state)
			{
				m_input_handlers.swapAndPop(i);
				break;
			}
		}
	}


	void setPath(ScriptComponent& cmp, ScriptInstance& inst, const Path& path)
	{
		registerAPI();

		if (inst.m_script) {
			auto& cb = inst.m_script->getObserverCb();
			cb.unbind<&ScriptComponent::onScriptLoaded>(&cmp);
			inst.m_script->decRefCount();
		}

		ResourceManagerHub& rm = m_system.m_engine.getResourceManager();
		inst.m_script = path.isEmpty() ? nullptr : rm.load<LuaScript>(path);
		if (inst.m_script) inst.m_script->onLoaded<&ScriptComponent::onScriptLoaded>(&cmp);
	}

	void startScript(EntityRef entity, InlineScriptComponent& instance, bool is_reload) {
		instance.runSource();
		startScriptInternal(entity, instance, is_reload);
	}

	void startScript(EntityRef entity, ScriptInstance& instance, bool is_reload) {
		if (!(instance.m_flags & ScriptInstance::ENABLED)) return;
			
		if (is_reload) disableScript(instance);
		startScriptInternal(entity, instance, is_reload);
	}

	void startScriptInternal(EntityRef entity, ScriptEnvironment& instance, bool is_reload)
	{
		if (!instance.m_state) return;
			
		lua_rawgeti(instance.m_state, LUA_REGISTRYINDEX, instance.m_environment);
		if (lua_type(instance.m_state, -1) != LUA_TTABLE)
		{
			ASSERT(false);
			lua_pop(instance.m_state, 1);
			return;
		}
		lua_getfield(instance.m_state, -1, "update");
		if (lua_type(instance.m_state, -1) == LUA_TFUNCTION)
		{
			auto& update_data = m_updates.emplace();
			update_data.state = instance.m_state;
			update_data.environment = instance.m_environment;
		}
		lua_pop(instance.m_state, 1);
		lua_getfield(instance.m_state, -1, "onInputEvent");
		if (lua_type(instance.m_state, -1) == LUA_TFUNCTION)
		{
			auto& callback = m_input_handlers.emplace();
			callback.state = instance.m_state;
			callback.environment = instance.m_environment;
		}
		lua_pop(instance.m_state, 1);

		if (!is_reload) {
			lua_getfield(instance.m_state, -1, "start");
			if (lua_type(instance.m_state, -1) != LUA_TFUNCTION) {
				lua_pop(instance.m_state, 2);
				return;
			}

			LuaWrapper::pcall(instance.m_state, 0, 0);
		}
		lua_pop(instance.m_state, 1);
	}


	void onButtonClicked(EntityRef e) { onGUIEvent(e, "onButtonClicked"); }
	void onRectHovered(EntityRef e) { onGUIEvent(e, "onRectHovered"); }
	void onRectHoveredOut(EntityRef e) { onGUIEvent(e, "onRectHoveredOut"); }
		
	void onRectMouseDown(EntityRef e, float x, float y) { 
		auto* inline_call = beginFunctionCallInlineScript(e, "onRectMouseDown");
		if (inline_call) {
			inline_call->add(x);
			inline_call->add(y);
			endFunctionCall();
		}

		if (!m_world.hasComponent(e, types::lua_script)) return;

		for (int i = 0, c = getScriptCount(e); i < c; ++i)
		{
			auto* call = beginFunctionCall(e, i, "onRectMouseDown");
			if (call) {
				call->add(x);
				call->add(y);
				endFunctionCall();
			}
		}
	}

	LUMIX_FORCE_INLINE void onGUIEvent(EntityRef e, const char* event)
	{
		auto* inline_call = beginFunctionCallInlineScript(e, event);
		if (inline_call) {
			endFunctionCall();
		}

		if (!m_world.hasComponent(e, types::lua_script)) return;

		for (int i = 0, c = getScriptCount(e); i < c; ++i)
		{
			auto* call = beginFunctionCall(e, i, event);
			if (call) endFunctionCall();
		}
	}

	void startGame() override {
		// the same script can be added multiple times to m_to_start (e.g. by enabling and disabling the script several time in editor)
		// so we need to remove duplicates
		m_to_start.removeDuplicates();

		m_is_game_running = true;
		m_gui_module = (GUIModule*)m_world.getModule("gui");
		if (m_gui_module) {
			m_gui_module->buttonClicked().bind<&LuaScriptModuleImpl::onButtonClicked>(this);
			m_gui_module->rectHovered().bind<&LuaScriptModuleImpl::onRectHovered>(this);
			m_gui_module->rectHoveredOut().bind<&LuaScriptModuleImpl::onRectHoveredOut>(this);
			m_gui_module->rectMouseDown().bind<&LuaScriptModuleImpl::onRectMouseDown>(this);
		}
	}


	void stopGame() override
	{
		if (m_gui_module)
		{
			m_gui_module->buttonClicked().unbind<&LuaScriptModuleImpl::onButtonClicked>(this);
			m_gui_module->rectHovered().unbind<&LuaScriptModuleImpl::onRectHovered>(this);
			m_gui_module->rectHoveredOut().unbind<&LuaScriptModuleImpl::onRectHoveredOut>(this);
			m_gui_module->rectMouseDown().unbind<&LuaScriptModuleImpl::onRectMouseDown>(this);
		}
		m_gui_module = nullptr;
		m_is_game_running = false;
		m_updates.clear();
		m_input_handlers.clear();
		m_timers.clear();
	}

	void createInlineScript(EntityRef entity) override {
		m_inline_scripts.insert(entity, InlineScriptComponent(entity, *this, m_system.m_allocator));
		m_world.onComponentCreated(entity, types::lua_script_inline, this);
	}

	void destroyInlineScript(EntityRef entity) override {
		m_inline_scripts.erase(entity);
		m_world.onComponentDestroyed(entity, types::lua_script_inline, this);
	}

	void createScript(EntityRef entity) override {
		auto& allocator = m_system.m_allocator;
		ScriptComponent* script = LUMIX_NEW(allocator, ScriptComponent)(*this, entity, allocator);
		m_scripts.insert(entity, script);
		m_world.onComponentCreated(entity, types::lua_script, this);
	}

	void destroyScript(EntityRef entity) override {
		ScriptComponent* cmp = m_scripts[entity];
		LUMIX_DELETE(m_system.m_allocator, cmp);
		m_scripts.erase(entity);
		m_world.onComponentDestroyed(entity, types::lua_script, this);
		m_to_start.eraseItems([entity](DeferredStart& element){
			return element.entity == entity;
		});
	}

	// TODO type-checking (does lua type match c++), check other places too
	void serializePropertyValue(Property& prop, const char* prop_name, ScriptInstance& inst, OutputMemoryStream& stream) {
		lua_State* L = inst.m_state;
		
		LuaWrapper::DebugGuard guard(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, inst.m_environment);
		lua_getfield(L, -1, prop_name);

		auto writeOne = [&]() {
			switch (prop.type) {
				case Property::ANY: ASSERT(false); break;
				case Property::BOOLEAN: {
					bool b = lua_toboolean(L, -1) != 0;
					stream.write((u8)b);
					break;
				}
				case Property::FLOAT: {
					float val = (float)lua_tonumber(L, -1);
					stream.write(val);
					break;
				}
				case Property::INT: {
					i32 val = lua_tointeger(L, -1);
					stream.write(val);
					break;
				}
				case Property::ENTITY: {
					EntityPtr e = INVALID_ENTITY;
					if (lua_type(L, -1) == LUA_TTABLE) {
						if (LuaWrapper::getField(L, -1, "_entity") == LUA_TNUMBER) {
							e = EntityPtr{ (i32)lua_tointeger(L, -1) };
						}
						lua_pop(L, 1);
					}
					stream.write(e.index);
					break;
				}
				case Property::STRING:
					stream.writeString(lua_tostring(L, -1));
					break;
				case Property::RESOURCE: {
					lua_getfield(L, -1, "_handle");
					int res_idx = LuaWrapper::toType<int>(L, -1);
					lua_pop(L, 1);
					Resource* res = m_system.getLuaResource(res_idx);
					stream.writeString(res ? res->getPath().c_str() : "");
					break;
				}
				case Property::COLOR: {
					const Vec3 val = LuaWrapper::toType<Vec3>(L, -1);
					stream.write(val);
					break;
				}
			}
		};

		if (prop.is_array) {
			const i32 num_elems = lua_objlen(inst.m_state, -1);
			stream.write(num_elems);

			for (i32 i = 0; i < num_elems; ++i) {
				lua_rawgeti(inst.m_state, -1, i + 1);
				writeOne();
				lua_pop(inst.m_state, 1);
			}
		}
		else {
			writeOne();
		}
		lua_pop(inst.m_state, 2);
	}


	void serialize(OutputMemoryStream& serializer) override {
		serializer.write(m_inline_scripts.size());
		for (auto iter : m_inline_scripts.iterated()) {
			serializer.write(iter.key());
			serializer.write(iter.value().m_source);
		}
			
		serializer.write(m_scripts.size());
		for (ScriptComponent* script_cmp : m_scripts) {
			serializer.write(script_cmp->m_entity);
			serializer.write(script_cmp->m_scripts.size());
			
			for (auto& scr : script_cmp->m_scripts) {
				serializer.writeString(scr.m_script ? scr.m_script->getPath() : Path());
				serializer.write(scr.m_flags);
				serializer.write(scr.m_properties.size());
				
				for (Property& prop : scr.m_properties) {
					serializer.write(prop.name_hash);
					serializer.write(prop.type);
					serializer.write(prop.is_array);
					auto iter = m_property_names.find(prop.name_hash);
					ASSERT(iter.isValid());

					const char* name = iter.value().c_str();
					serializePropertyValue(prop, name, scr, serializer);
				}
			}
		}
	}

	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override {
		if (version > (i32)LuaModuleVersion::INLINE_SCRIPT) {
			const i32 len = serializer.read<i32>();
			m_inline_scripts.reserve(m_scripts.size() + len);
			for (int i = 0; i < len; ++i) {
				EntityRef entity;
				serializer.read(entity);
				entity = entity_map.get(entity);
				auto iter = m_inline_scripts.insert(entity, InlineScriptComponent(entity, *this, m_system.m_allocator));
				serializer.read(iter.value().m_source);
				m_world.onComponentCreated(entity, types::lua_script_inline, this);
				m_to_start.push({entity, (u32)i, true, false});
			}
		}

		int len = serializer.read<int>();
		m_scripts.reserve(len + m_scripts.size());
		for (int i = 0; i < len; ++i) {
			auto& allocator = m_system.m_allocator;
			EntityRef entity;
			serializer.read(entity);
			entity = entity_map.get(entity);
			ScriptComponent* script = LUMIX_NEW(allocator, ScriptComponent)(*this, entity, allocator);

			m_scripts.insert(script->m_entity, script);
			int scr_count;
			serializer.read(scr_count);
			for (int scr_idx = 0; scr_idx < scr_count; ++scr_idx) {
				auto& scr = script->m_scripts.emplace(*script, allocator);

				const char* path = serializer.readString();
				serializer.read(scr.m_flags);
				int prop_count;
				serializer.read(prop_count);
				scr.m_properties.reserve(prop_count);
				for (int prop_idx = 0; prop_idx < prop_count; ++prop_idx) {
					Property& prop = scr.m_properties.emplace(allocator);
					prop.type = Property::ANY;
					if (version <= (i32)LuaModuleVersion::HASH64) {
						serializer.read(prop.name_hash_legacy);
					}
					else {
						serializer.read(prop.name_hash);
					}
					Property::Type type;
					serializer.read(type);
					
					if (version > (i32)LuaModuleVersion::ARRAY_PROPERTIES) {
						prop.is_array = serializer.read<bool>();
						u32 num_elements = 1;
						if (prop.is_array) {
							num_elements = serializer.read<u32>();
							prop.stored_value.reserve(num_elements * 4 + sizeof(num_elements));
							prop.stored_value.write(num_elements);	
						}
						// TODO small buffer optimization - most properties are <= 4B
						switch (type) {
							case Property::ANY: ASSERT(false); break;
							case Property::ENTITY: {
								for (u32 j = 0; j < num_elements; ++j) {
									EntityPtr e = serializer.read<EntityPtr>();
									e = entity_map.get(e);
									prop.stored_value.write(e);
								}
								break;
							}
							case Property::FLOAT: prop.stored_value.write(serializer.skip(num_elements * sizeof(float)), num_elements * sizeof(float)); break;
							case Property::BOOLEAN: prop.stored_value.write(serializer.skip(num_elements * sizeof(u8)), num_elements * sizeof(u8)); break;
							case Property::INT: prop.stored_value.write(serializer.skip(num_elements * sizeof(i32)), num_elements * sizeof(i32)); break;
							case Property::COLOR: prop.stored_value.write(serializer.skip(num_elements * sizeof(Vec3)), num_elements * sizeof(Vec3)); break;
							case Property::STRING:
							case Property::RESOURCE:
								for(u32 j = 0; j < num_elements; ++j) {
									prop.stored_value.writeString(serializer.readString());
								}
								break;
						}
					}
					else {
						const char* tmp = serializer.readString();
						switch (type) {
							case Property::ANY: ASSERT(false); break;
							case Property::ENTITY: {
								EntityPtr prop_value;
								fromCString(tmp, prop_value.index);
								prop_value = entity_map.get(prop_value);
								prop.stored_value.write(prop_value);
								break;
							}
							case Property::FLOAT: {
								float prop_value = fromString<float>(tmp);
								prop.stored_value.write(prop_value);
								break;
							}
							case Property::BOOLEAN: {
								bool prop_value = fromString<bool>(tmp);
								prop.stored_value.write(prop_value);
								break;
							}
							case Property::INT: {
								i32 prop_value = fromString<i32>(tmp);
								prop.stored_value.write(prop_value);
								break;
							}
							case Property::COLOR: {
								Vec3 prop_value = fromString<Vec3>(tmp);
								prop.stored_value.write(prop_value);
								break;
							}
							case Property::STRING:
							case Property::RESOURCE:
								prop.stored_value.writeString(tmp);
								break;
						}
					}
				}
				setPath(*script, scr, Path(path));
				m_to_start.push({entity, (u32)scr_idx, false, false});
			}
			m_world.onComponentCreated(script->m_entity, types::lua_script, this);
		}
	}


	ISystem& getSystem() const override { return m_system; }


	void startScripts() {
		for (auto& s : m_to_start) {
			if (s.is_inline) {
				InlineScriptComponent& scr = m_inline_scripts[s.entity];
				startScript(s.entity, scr, s.is_reload);
			}
			else {
				ScriptInstance& scr = m_scripts[s.entity]->m_scripts[s.scr_index];
				if (!scr.m_script) continue;
				if (!scr.m_script->isReady()) continue;
				startScript(s.entity, scr, s.is_reload);
			}
		}

		m_to_start.clear();
	}


	void updateTimers(float time_delta)
	{
		int timers_to_remove[1024];
		u32 timers_to_remove_count = 0;
		for (int i = 0, c = m_timers.size(); i < c; ++i)
		{
			auto& timer = m_timers[i];
			timer.time -= time_delta;
			if (timer.time < 0)
			{
				lua_rawgeti(timer.state, LUA_REGISTRYINDEX, timer.func);
				if (lua_type(timer.state, -1) != LUA_TFUNCTION)
				{
					ASSERT(false);
				}

				if (lua_pcall(timer.state, 0, 0, 0) != 0)
				{
					logError(lua_tostring(timer.state, -1));
					lua_pop(timer.state, 1);
				}
				timers_to_remove[timers_to_remove_count] = i;
				++timers_to_remove_count;
				if (timers_to_remove_count >= lengthOf(timers_to_remove))
				{
					logError("Too many lua timers in one frame, some are not executed");
					break;
				}
			}
		}
		for (u32 i = timers_to_remove_count - 1; i != 0xffFFffFF; --i)
		{
			auto& timer = m_timers[timers_to_remove[i]];
			LuaWrapper::releaseRef(timer.state, timer.func);
			m_timers.swapAndPop(timers_to_remove[i]);
		}
	}

	void processInputEvent(const CallbackData& callback, const InputSystem::Event& event)
	{
		lua_State* L = callback.state;
		lua_newtable(L); // [lua_event]
		LuaWrapper::push(L, toString(event.type)); // [lua_event, event.type]
		lua_setfield(L, -2, "type"); // [lua_event]

		lua_newtable(L); // [lua_event, lua_device]
		LuaWrapper::push(L, toString(event.device->type)); // [lua_event, lua_device, device.type]
		lua_setfield(L, -2, "type"); // [lua_event, lua_device]
		LuaWrapper::push(L, event.device->index); // [lua_event, lua_device, device.index]
		lua_setfield(L, -2, "index"); // [lua_event, lua_device]

		lua_setfield(L, -2, "device"); // [lua_event]

		switch(event.type)
		{
			case InputSystem::Event::DEVICE_ADDED:
				break;
			case InputSystem::Event::DEVICE_REMOVED:
				break;
			case InputSystem::Event::BUTTON:
				LuaWrapper::push(L, event.data.button.down); // [lua_event, button.down]
				lua_setfield(L, -2, "down"); // [lua_event]
				LuaWrapper::push(L, event.data.button.key_id); // [lua_event, button.key_id]
				lua_setfield(L, -2, "key_id"); // [lua_event]
				LuaWrapper::push(L, event.data.button.is_repeat); // [lua_event, button.is_repeat]
				lua_setfield(L, -2, "is_repeat"); // [lua_event]
				LuaWrapper::push(L, event.data.button.x); // [lua_event, button.x_abs]
				lua_setfield(L, -2, "x"); // [lua_event]
				LuaWrapper::push(L, event.data.button.y); // [lua_event, button.y_abs]
				lua_setfield(L, -2, "y"); // [lua_event]
				break;
			case InputSystem::Event::AXIS:
				LuaWrapper::push(L, event.data.axis.x); // [lua_event, axis.x]
				lua_setfield(L, -2, "x"); // [lua_event]
				LuaWrapper::push(L, event.data.axis.y); // [lua_event, axis.y]
				lua_setfield(L, -2, "y"); // [lua_event]
				LuaWrapper::push(L, event.data.axis.x_abs); // [lua_event, axis.x_abs]
				lua_setfield(L, -2, "x_abs"); // [lua_event]
				LuaWrapper::push(L, event.data.axis.y_abs); // [lua_event, axis.y_abs]
				lua_setfield(L, -2, "y_abs"); // [lua_event]
				break;
			case InputSystem::Event::TEXT_INPUT:
				LuaWrapper::push(L, event.data.text.utf8); // [lua_event, utf8]
				lua_setfield(L, -2, "text"); // [lua_event]
				break;
		}


		lua_rawgeti(L, LUA_REGISTRYINDEX, callback.environment);
		if (lua_type(L, -1) != LUA_TTABLE) // [lua_event, environment]
		{
			ASSERT(false);
		}
		lua_getfield(L, -1, "onInputEvent");
		if (lua_type(L, -1) != LUA_TFUNCTION)  // [lua_event, environment, func]
		{
			lua_pop(L, 3); // []
			return;
		}

		lua_pushvalue(L, -3); // [lua_event, environment, func, lua_event]
			
		LuaWrapper::pcall(L, 1, 0); // [lua_event, environment]
		lua_pop(L, 2); // []
	}


	void processInputEvents() {
		if (m_input_handlers.empty()) return;

		InputSystem& input_system = m_system.m_engine.getInputSystem();
		Span<const InputSystem::Event> events = input_system.getEvents();
		for (const InputSystem::Event& e : events) {
			for (const CallbackData& cb : m_input_handlers) {
				processInputEvent(cb, e);
			}
		}
	}


	void update(float time_delta) override
	{
		PROFILE_FUNCTION();

		if (!m_is_game_running) return;
		startScripts();

		processInputEvents();
		updateTimers(time_delta);

		for (int i = 0; i < m_updates.size(); ++i)
		{
			CallbackData update_item = m_updates[i];
			LuaWrapper::DebugGuard guard(update_item.state, 0);
			lua_rawgeti(update_item.state, LUA_REGISTRYINDEX, update_item.environment);
			if (lua_type(update_item.state, -1) != LUA_TTABLE)
			{
				ASSERT(false);
			}
			lua_getfield(update_item.state, -1, "update");
			if (lua_type(update_item.state, -1) != LUA_TFUNCTION)
			{
				lua_pop(update_item.state, 2);
				continue;
			}

			lua_pushnumber(update_item.state, time_delta);
			LuaWrapper::pcall(update_item.state, 1, 0);
			lua_pop(update_item.state, 1);
		}

		for (EntityRef e : m_deferred_destructions) {
			m_world.destroyEntity(e);
		}
		m_deferred_destructions.clear();

		for (World::PartitionHandle p : m_deferred_partition_destructions) {
			m_world.destroyPartition(p);
		}
		m_deferred_partition_destructions.clear();
	}


	Property& getScriptProperty(EntityRef entity, int scr_index, const char* name)
	{
		const StableHash name_hash(name);
		ScriptComponent* script_cmp = m_scripts[entity];
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
		prop.type = Property::ANY;
		return prop;
	}


	Path getScriptPath(EntityRef entity, int scr_index) override
	{
		auto& tmp = m_scripts[entity]->m_scripts[scr_index];
		return tmp.m_script ? tmp.m_script->getPath() : Path("");
	}


	void setScriptPath(EntityRef entity, int scr_index, const Path& path) override
	{
		auto* script_cmp = m_scripts[entity];
		if (script_cmp->m_scripts.size() <= scr_index) return;
		setPath(*script_cmp, script_cmp->m_scripts[scr_index], path);
	}


	int getScriptCount(EntityRef entity) override
	{
		return m_scripts[entity]->m_scripts.size();
	}


	void insertScript(EntityRef entity, int idx) override
	{
		ScriptComponent* cmp = m_scripts[entity];
		cmp->m_scripts.emplaceAt(idx, *cmp, m_system.m_allocator);
	}


	int addScript(EntityRef entity, int scr_index) override
	{
		ScriptComponent* script_cmp = m_scripts[entity];
		if (scr_index == -1) scr_index = script_cmp->m_scripts.size();
		script_cmp->m_scripts.emplaceAt(scr_index, *script_cmp, m_system.m_allocator);
		return scr_index;
	}


	void moveScript(EntityRef entity, int scr_index, bool up) override
	{
		auto* script_cmp = m_scripts[entity];
		if (!up && scr_index > script_cmp->m_scripts.size() - 2) return;
		if (up && scr_index == 0) return;
		int other = up ? scr_index - 1 : scr_index + 1;
		swap(script_cmp->m_scripts[scr_index], script_cmp->m_scripts[other]);
	}


	void setEnableProperty(EntityRef entity, int scr_index, ScriptInstance& inst, bool enabled)
	{
		if (!inst.m_state) return;

		lua_rawgeti(inst.m_state, LUA_REGISTRYINDEX, inst.m_environment); // [env]
		ASSERT(lua_type(inst.m_state, -1) == LUA_TTABLE);
		lua_pushboolean(inst.m_state, enabled);  // [env, enabled]
		lua_setfield(inst.m_state, -2, "enabled"); // [env]
		lua_pop(inst.m_state, 1); // []

		const char* fn = enabled ? "onEnable" : "onDisable";
		if (beginFunctionCall(entity, scr_index, fn)) endFunctionCall();
	}

	void deferPartitionDestruction(u16 partition) override {
		m_deferred_partition_destructions.push(partition);
	}

	void deferEntityDestruction(EntityRef entity) override {
		m_deferred_destructions.push(entity);
	}

	void enableScript(EntityRef entity, int scr_index, bool enable) override
	{
		ScriptInstance& inst = m_scripts[entity]->m_scripts[scr_index];
		if (isFlagSet(inst.m_flags, ScriptInstance::ENABLED) == enable) return;

		setFlag(inst.m_flags, ScriptInstance::ENABLED, enable);

		setEnableProperty(entity, scr_index, inst, enable);

		if(enable) {
			m_to_start.push({entity, (u32)scr_index, false, false});
		}
		else {
			disableScript(inst);
		}
	}


	bool isScriptEnabled(EntityRef entity, int scr_index) override
	{
		return m_scripts[entity]->m_scripts[scr_index].m_flags & ScriptInstance::ENABLED;
	}


	void removeScript(EntityRef entity, int scr_index) override {
		m_scripts[entity]->m_scripts.swapAndPop(scr_index);
	}

	const char* getInlineScriptCode(EntityRef entity) override {
		return m_inline_scripts[entity].m_source.c_str();
	}
		
	void setInlineScriptCode(EntityRef entity, const char* value) override {
		m_inline_scripts[entity].m_source = value;
	}

	void getScriptBlob(EntityRef e, u32 index, OutputMemoryStream& stream) override {
		ScriptInstance& inst = m_scripts[e]->m_scripts[index];
		ASSERT(inst.m_state);
		u32 num_known_properties = 0;
		for (Property& prop : inst.m_properties) {
			auto iter = m_property_names.find(prop.name_hash);
			if (iter.isValid()) ++num_known_properties;
		}

		stream.write(num_known_properties);

		for (Property& prop : inst.m_properties) {
			auto iter = m_property_names.find(prop.name_hash);
			// iter can be invalid if the referenced script is not accessible (removed, moved)
			// since m_property_names is fill from actual lua source code
			if (iter.isValid()) {
				const char* prop_name = iter.value().c_str();
				stream.writeString(prop_name);
				serializePropertyValue(prop, prop_name, inst, stream);
			}
		}
	}

	// TODO resource leaks all over the place
	void pushResource(lua_State* L, const char* path, ResourceType resource_type) {
		const i32 res_idx = path[0] ? m_system.addLuaResource(Path(path), resource_type) : -1;
		
		lua_newtable(L);
		lua_getglobal(L, "Lumix");
		lua_getfield(L, -1, "Resource");
		lua_setmetatable(L, -3);
		lua_pop(L, 1);

		LuaWrapper::push(L, res_idx);
		lua_setfield(L, -2, "_handle");

		lua_pushlightuserdata(L, (void*)resource_type.type.getHashValue());
		lua_setfield(L, -2, "_type");
	}

	void setScriptBlob(EntityRef entity, u32 index, InputMemoryStream& stream) override {
		// TODO make sure properties in set/get blobs match
		ScriptInstance& inst = m_scripts[entity]->m_scripts[index];
		ASSERT(inst.m_state);

		lua_State* L = inst.m_state; 
		LuaWrapper::DebugGuard guard(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, inst.m_environment);
		
		u32 num_props = stream.read<u32>();
		for (u32 i = 0; i < num_props; ++i) {
			const char* prop_name = stream.readString();
			Property& prop = getScriptProperty(entity, index, prop_name);
			
			const char* name = getPropertyName(prop.name_hash);
			ASSERT(name);

			auto readProperty = [&](){
				switch (prop.type) {
					case Property::ANY: break;
					case Property::BOOLEAN: {
						bool value = stream.read<u8>() != 0;
						LuaWrapper::push(L, value);
						break;
					}
					case Property::FLOAT: {
						float value = stream.read<float>();
						LuaWrapper::push(L, value);
						break;
					}
					case Property::INT: {
						i32 value = stream.read<i32>();
						LuaWrapper::push(L, value);
						break;
					}
					case Property::ENTITY: {
						EntityPtr value;
						stream.read(value);
						// TODO entity map - when copy/pasting script components, entity properties are not remaped to new entities
						LuaWrapper::pushEntity(L, value, &m_world);
						break;
					}
					case Property::RESOURCE: {
						const char* path = stream.readString();
						pushResource(L, path, prop.resource_type);
						break;
					}
					case Property::STRING: {
						const char* value = stream.readString();
						LuaWrapper::push(L, value);
						break;
					}
					case Property::COLOR: {
						Vec3 value = stream.read<Vec3>();
						LuaWrapper::push(L, value);
						break;
					}
				}
			};

			if (prop.is_array) {
				lua_newtable(L);
				const i32 len = stream.read<i32>();
				for (i32 j = 0; j < len; ++j) {
					readProperty();
					lua_rawseti(L, -2, j + 1);
				}
				lua_setfield(L, -2, name);
			}
			else {
				readProperty();
				lua_setfield(L, -2, name);
			}
		}

		lua_pop(L, 1);
	}

	struct DeferredStart {
		EntityRef entity;
		u32 scr_index;
		bool is_inline;
		bool is_reload;
		bool operator==(const DeferredStart& rhs) const { 
			return entity == rhs.entity 
			&& is_inline == rhs.is_inline
			&& scr_index == rhs.scr_index;
		}
	};

	LuaScriptSystemImpl& m_system;
	Array<EntityRef> m_deferred_destructions;
	Array<World::PartitionHandle> m_deferred_partition_destructions;
	HashMap<EntityRef, ScriptComponent*> m_scripts;
	HashMap<EntityRef, InlineScriptComponent> m_inline_scripts;
	HashMap<StableHash, String> m_property_names;
	Array<CallbackData> m_input_handlers;
	World& m_world;
	Array<DeferredStart> m_to_start;
	Array<CallbackData> m_updates;
	Array<TimerData> m_timers;
	FunctionCall m_function_call;
	ScriptInstance* m_current_script_instance;
	bool m_is_api_registered = false;
	bool m_is_game_running = false;
	GUIModule* m_gui_module = nullptr;
};

void LuaScriptModuleImpl::ScriptInstance::onScriptUnloaded(LuaScriptModuleImpl& module, struct ScriptComponent& cmp, int scr_index) {
	LuaWrapper::DebugGuard guard(m_state);
	lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_environment); // [env]
	lua_getfield(m_state, -1, "onUnload"); // [env, awake]
	if (lua_type(m_state, -1) != LUA_TFUNCTION)
	{
		lua_pop(m_state, 1); // []
	}
	else {
		if (lua_pcall(m_state, 0, 0, 0) != 0) { // [env] | [env, error]
			logError(lua_tostring(m_state, -1));
			lua_pop(m_state, 1); // [env]
		}
	}
		
	// remove reference to functions, we don't want them to be called in case 
	// this script is reloaded and functions are not there in the new version
	lua_pushnil(m_state);
	while (lua_next(m_state, -2) != 0) {
		if (lua_isfunction(m_state, -1) && lua_isstring(m_state, -2)) {
			const char* key = lua_tostring(m_state, -2);
			lua_pushnil(m_state);
			lua_setfield(m_state, -4, key);
		}
		lua_pop(m_state, 1);
	}
	lua_pop(m_state, 1);
}

void LuaScriptModuleImpl::ScriptInstance::onScriptLoaded(LuaScriptModuleImpl& module, struct ScriptComponent& cmp, int scr_index) {
	LuaWrapper::DebugGuard guard(m_state);
		
	bool is_reload = m_flags & LOADED;
		
	lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_environment); // [env]
	ASSERT(lua_type(m_state, -1) == LUA_TTABLE);

	bool errors = LuaWrapper::luaL_loadbuffer(m_state,
		m_script->getSourceCode().begin,
		m_script->getSourceCode().size(),
		m_script->getPath().c_str()) != 0; // [env, func]

	if (errors) {
		logError(m_script->getPath(), ": ", lua_tostring(m_state, -1));
		lua_pop(m_state, 2);
		return;
	}

	lua_pushvalue(m_state, -2); // [env, func, env]
	lua_setfenv(m_state, -2);

	module.m_current_script_instance = this;
	errors = lua_pcall(m_state, 0, 0, 0) != 0; // [env]
	if (errors)	{
		logError(m_script->getPath(), ": ", lua_tostring(m_state, -1));
		lua_pop(m_state, 1);
	}
	lua_pop(m_state, 1); // []

	cmp.detectProperties(*this);
					
	bool enabled = m_flags & ScriptInstance::ENABLED;
	module.setEnableProperty(cmp.m_entity, scr_index, *this, enabled);
	m_flags = Flags(m_flags | LOADED);

	lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_environment); // [env]
	lua_getfield(m_state, -1, "awake"); // [env, awake]
	if (lua_type(m_state, -1) != LUA_TFUNCTION)
	{
		lua_pop(m_state, 2); // []
	}
	else {
		if (lua_pcall(m_state, 0, 0, 0) != 0) { // [env] | [env, error]
			logError(lua_tostring(m_state, -1));
			lua_pop(m_state, 1); // [env]
		}
		lua_pop(m_state, 1); // []
	}

	module.m_to_start.push({cmp.m_entity, (u32)scr_index, false, is_reload && module.m_is_game_running});
}


static int finishrequire(lua_State* L)
{
	if (lua_isstring(L, -1))
		lua_error(L);

	return 1;
}

static int LUA_inherit(lua_State* L) {
	LuaWrapper::DebugGuard guard(L);
	const char* name = luaL_checkstring(L, 1);
	Engine* engine = LuaWrapper::getClosureObject<Engine>(L);
	Path path(name, ".lua");
	LuaScript* dep = engine->getResourceManager().load<LuaScript>(path);
	if (!dep->isReady()) {
		ASSERT(false); // inherited-d files should be registered as dependencies, so it should be impossible to get here
		luaL_argerrorL(L, 1, "failed to inherit file, it's not ready");
	}

	const StringView src = dep->getSourceCode();
	bool errors = LuaWrapper::luaL_loadbuffer(L, src.begin, src.size(), name);
	if (errors) {
		lua_error(L);
		return 0;
	}

	// set the environment of the inherited file to the environment of the calling function
	lua_Debug ar;
	lua_getinfo(L, 1, "f", &ar); // fn, cur fn
	lua_getfenv(L, -1); 		 // fn, cur fn, env
	lua_setfenv(L, -3);			 // fn, cur fn
	lua_pop(L, 1);				 // fn

	errors = lua_pcall(L, 0, 0, 0) != 0;
	if (errors) {
		lua_error(L);
		return 0;
	}

	return 0;
}

static int LUA_require(lua_State* L) {
	const char* name = luaL_checkstring(L, 1);

	luaL_findtable(L, LUA_REGISTRYINDEX, "_MODULES", 1);

	// return the module from the cache
	lua_getfield(L, -1, name);
	if (!lua_isnil(L, -1))
	{
		// L stack: _MODULES result
		return finishrequire(L);
	}

	lua_pop(L, 1);

	Engine* engine = LuaWrapper::getClosureObject<Engine>(L);
	Path path(name, ".lua");
	LuaScript* dep = engine->getResourceManager().load<LuaScript>(path);
	if (!dep->isReady()) {
		ASSERT(false); // require-d modules should be registered as dependencies, so it should be impossible to get here
		luaL_argerrorL(L, 1, "error loading module");
	}

	// module needs to run in a new thread, isolated from the rest
	// note: we create ML on main thread so that it doesn't inherit environment of L
	lua_State* GL = lua_mainthread(L);
	lua_State* ML = lua_newthread(GL);
	lua_xmove(GL, L, 1);

	// new thread needs to have the globals sandboxed
	luaL_sandboxthread(ML);

	// now we can compile & run module on the new thread
	size_t bytecode_size;
	char* bytecode = luau_compile((const char*)dep->getSourceCode().begin, dep->getSourceCode().size(), nullptr, &bytecode_size);
	if (bytecode_size == 0) {
		lua_pushstring(L, bytecode);
		free(bytecode);
		lua_error(L);
	}

	if (luau_load(ML, name, bytecode, bytecode_size, 0) == 0)
	{
		int status = lua_resume(ML, L, 0);

		if (status == 0)
		{
			if (lua_gettop(ML) == 0)
				lua_pushstring(ML, "module must return a value");
			else if (!lua_istable(ML, -1) && !lua_isfunction(ML, -1))
				lua_pushstring(ML, "module must return a table or function");
		}
		else if (status == LUA_YIELD)
		{
			lua_pushstring(ML, "module can not yield");
		}
		else if (!lua_isstring(ML, -1))
		{
			lua_pushstring(ML, "unknown error while running module");
		}
	}
	free(bytecode);

	// there's now a return value on top of ML; L stack: _MODULES ML
	lua_xmove(ML, L, 1);
	lua_pushvalue(L, -1);
	lua_setfield(L, -4, name);

	// L stack: _MODULES ML result
	return finishrequire(L);
}

static int LUA_dofile(lua_State* L) {
	LuaWrapper::DebugGuard guard(L, 1);
	const char* name = luaL_checkstring(L, 1);

	Engine* engine = LuaWrapper::getClosureObject<Engine>(L);
	Path path(name, ".lua");
	LuaScript* dep = engine->getResourceManager().load<LuaScript>(path);
	if (!dep->isReady()) {
		ASSERT(false); // require-d modules should be registered as dependencies, so it should be impossible to get here
		luaL_argerrorL(L, 1, "error loading module");
	}

	lua_State* GL = lua_mainthread(L);
	lua_State* ML = lua_newthread(GL);
	LuaWrapper::DebugGuard guard2(ML);
	lua_xmove(GL, L, 1);

	luaL_sandboxthread(ML);

	size_t bytecode_size;
	char* bytecode = luau_compile((const char*)dep->getSourceCode().begin, dep->getSourceCode().size(), nullptr, &bytecode_size);
	if (bytecode_size == 0) {
		lua_pushstring(L, bytecode);
		free(bytecode);
		lua_error(L);
	}

	if (luau_load(ML, name, bytecode, bytecode_size, 0) == 0)
	{
		int status = lua_resume(ML, L, 0);

		if (status == 0)
		{
			if (lua_gettop(ML) == 0)
				lua_pushstring(ML, "module must return a value");
			else if (!lua_istable(ML, -1) && !lua_isfunction(ML, -1))
				lua_pushstring(ML, "module must return a table or function");
		}
		else if (status == LUA_YIELD)
		{
			lua_pushstring(ML, "module can not yield");
		}
		else if (!lua_isstring(ML, -1))
		{
			lua_pushstring(ML, "unknown error while running module");
		}
	}
	free(bytecode);

	lua_xmove(ML, L, 1);
	lua_remove(L, -2);
	return finishrequire(L);
}

static void* luaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
	LuaScriptSystemImpl* system = (LuaScriptSystemImpl*)ud;
	system->m_lua_allocated = system->m_lua_allocated + nsize - osize;
	if (nsize == 0) {
		if (osize > 0) system->m_lua_allocator.deallocate(ptr);
		return nullptr;
	}
	if (!ptr) {
		ASSERT(osize == 0);
		return system->m_lua_allocator.allocate(nsize, 8);
	}

	ASSERT(osize > 0);
	return system->m_lua_allocator.reallocate(ptr, nsize, osize, 8);
}

LuaScriptSystemImpl::LuaScriptSystemImpl(Engine& engine)
	: m_engine(engine)
	, m_allocator(engine.getAllocator(), "lua system")
	, m_script_manager(m_allocator)
	, m_lua_allocator(engine.getAllocator(), "luau")
	, m_lua_resources(m_allocator)
{
	#ifdef _WIN32
		m_state = lua_newstate(luaAlloc, this);
	#else 
		m_state = luaL_newstate();
	#endif
	luaL_openlibs(m_state);
	
	lua_State* L = m_state;
	lua_pushlightuserdata(L, &engine);
	lua_pushcclosure(L, &LUA_require, "require", 1);
	lua_setglobal(L, "require");

	lua_pushlightuserdata(L, &engine);
	lua_pushcclosure(L, &LUA_inherit, "inherit", 1);
	lua_setglobal(L, "inherit");

	lua_pushlightuserdata(L, &engine);
	lua_pushcclosure(L, &LUA_dofile, "dofile", 1);
	lua_setglobal(L, "dofile");

	m_script_manager.create(LuaScript::TYPE, engine.getResourceManager());

	#include "lua_script_system.gen.h"
}


LuaScriptSystemImpl::~LuaScriptSystemImpl()
{
	for (Resource* res : m_lua_resources) {
		res->decRefCount();
	}
	lua_close(m_state);
	m_script_manager.destroy();
}

void LuaScriptSystemImpl::createModules(World& world)
{
	UniquePtr<LuaScriptModuleImpl> module = UniquePtr<LuaScriptModuleImpl>::create(m_allocator, *this, world);
	world.addModule(module.move());
}


LUMIX_PLUGIN_ENTRY(lua) {
	PROFILE_FUNCTION();
	return LUMIX_NEW(engine.getAllocator(), LuaScriptSystemImpl)(engine);
}

} // namespace Lumix
