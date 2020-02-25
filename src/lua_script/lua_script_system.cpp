#include "lua_script_system.h"
#include "animation/animation_scene.h"
#include "engine/array.h"
#include "engine/associative_array.h"
#include "engine/crc32.h"
#include "engine/debug.h"
#include "engine/engine.h"
#include "engine/flag_set.h"
#include "engine/allocator.h"
#include "engine/input_system.h"
#include "engine/plugin.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "engine/string.h"
#include "engine/universe.h"
#include "gui/gui_scene.h"
#include "lua_script/lua_script.h"


namespace Lumix
{


static const ComponentType LUA_SCRIPT_TYPE = Reflection::getComponentType("lua_script");


enum class LuaSceneVersion : int
{
	PROPERTY_TYPE,
	FLAGS,

	LATEST
};


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


struct LuaScriptSystemImpl final : IPlugin
{
	explicit LuaScriptSystemImpl(Engine& engine);
	virtual ~LuaScriptSystemImpl();

	void createScenes(Universe& universe) override;
	void destroyScene(IScene* scene) override;
	const char* getName() const override { return "lua_script"; }
	LuaScriptManager& getScriptManager() { return m_script_manager; }

	Engine& m_engine;
	IAllocator& m_allocator;
	LuaScriptManager m_script_manager;
};


struct LuaScriptSceneImpl final : LuaScriptScene
{
	struct TimerData
	{
		float time;
		lua_State* state;
		int func;
	};

	struct CallbackData
	{
		LuaScript* script;
		lua_State* state;
		int environment;
	};


	struct ScriptInstance
	{
		enum Flags : u32
		{
			ENABLED = 1 << 0
		};

		explicit ScriptInstance(IAllocator& allocator)
			: m_properties(allocator)
			, m_script(nullptr)
			, m_state(nullptr)
			, m_environment(-1)
			, m_thread_ref(-1)
		{
			m_flags.set(ENABLED);
		}

		LuaScript* m_script;
		lua_State* m_state;
		int m_environment;
		int m_thread_ref;
		Array<Property> m_properties;
		FlagSet<Flags, u32> m_flags;
	};


	struct ScriptComponent
	{
		ScriptComponent(LuaScriptSceneImpl& scene, EntityRef entity, IAllocator& allocator)
			: m_scripts(allocator)
			, m_scene(scene)
			, m_entity(entity)
		{
		}


		static int getProperty(ScriptInstance& inst, u32 hash)
		{
			for(int i = 0, c = inst.m_properties.size(); i < c; ++i)
			{
				if (inst.m_properties[i].name_hash == hash) return i;
			}
			return -1;
		}


		void detectProperties(ScriptInstance& inst)
		{
			static const u32 INDEX_HASH = crc32("__index");
			static const u32 THIS_HASH = crc32("this");
			lua_State* L = inst.m_state;
			lua_rawgeti(L, LUA_REGISTRYINDEX, inst.m_environment); // [env]
			ASSERT(lua_type(L, -1) == LUA_TTABLE);
			lua_pushnil(L); // [env, nil]
			IAllocator& allocator = m_scene.m_system.m_allocator;
			u32 valid_properties[256];
			if (inst.m_properties.size() >= sizeof(valid_properties) * 8) {
				logError("Lua Script") << "Too many properties in " << inst.m_script->getPath() << ", entity " << m_entity.index
					<< ". Some will be ignored.";
				inst.m_properties.shrink(sizeof(valid_properties) * 8);
			}
			memset(valid_properties, 0, (inst.m_properties.size() + 7) / 8);

			while (lua_next(L, -2)) // [env, key, value] | [env]
			{
				if (lua_type(L, -1) != LUA_TFUNCTION)
				{
					const char* name = lua_tostring(L, -2);
					if(name[0] != '_' && !equalStrings(name, "enabled"))
					{
						const u32 hash = crc32(name);
						auto iter = m_scene.m_property_names.find(hash);
						if (!iter.isValid())
						{
							m_scene.m_property_names.insert(hash, String(name, allocator));
						}
						if (hash != INDEX_HASH && hash != THIS_HASH)
						{
							int prop_index = getProperty(inst, hash);
							if (prop_index >= 0) {
								valid_properties[prop_index / 8] |=  1 << (prop_index % 8);
								Property& existing_prop = inst.m_properties[prop_index];
								if (existing_prop.type == Property::ANY) {
									switch (lua_type(inst.m_state, -1)) {
										case LUA_TSTRING: existing_prop.type = Property::STRING; break;
										case LUA_TBOOLEAN: existing_prop.type = Property::BOOLEAN; break;
										default: existing_prop.type = Property::FLOAT;
									}
								}
								m_scene.applyProperty(inst, existing_prop, existing_prop.stored_value.c_str());
							}
							else {
								const int prop_index = inst.m_properties.size();
								if (inst.m_properties.size() < sizeof(valid_properties) * 8) {
									auto& prop = inst.m_properties.emplace(allocator);
									valid_properties[prop_index / 8] |=  1 << (prop_index % 8);
									switch (lua_type(inst.m_state, -1)) {
										case LUA_TBOOLEAN: prop.type = Property::BOOLEAN; break;
										case LUA_TSTRING: prop.type = Property::STRING; break;
										default: prop.type = Property::FLOAT;
									}
									prop.name_hash = hash;
								}
								else {
									logError("Lua Script") << "Too many properties in " << inst.m_script->getPath() << ", entity " << m_entity.index
										<< ". Some will be ignored.";
								}
							}
						}
					}
				}
				lua_pop(L, 1); // [env, key]
			}
			// [env]
			for (int i = inst.m_properties.size() - 1; i >= 0; --i)
			{
				if (valid_properties[i / 8] & (1 << (i % 8))) continue;
				inst.m_properties.swapAndPop(i);
			}
			sortProperties(inst.m_properties);
			lua_pop(L, 1);
		}


		void onScriptLoaded(Resource::State, Resource::State, Resource& resource)
		{
			lua_State* L = m_scene.m_system.m_engine.getState();
			for (int scr_index = 0, c = m_scripts.size(); scr_index < c; ++scr_index)
			{
				auto& script = m_scripts[scr_index];
				if (!script.m_script) continue;
				if (!script.m_script->isReady()) continue;
				if (script.m_script != &resource) continue;

				bool is_reload = true;
				if (!script.m_state)
				{
					is_reload = false;

					script.m_state = lua_newthread(L); // [thread]
					LuaWrapper::DebugGuard guard(script.m_state, 1);
					script.m_thread_ref = luaL_ref(L, LUA_REGISTRYINDEX); // []
					lua_newtable(script.m_state); // [env]
					// reference environment
					lua_pushvalue(script.m_state, -1); // [env, env]
					script.m_environment = luaL_ref(script.m_state, LUA_REGISTRYINDEX); // [env]

					// environment's metatable & __index
					lua_pushvalue(script.m_state, -1); // [env, env]
					lua_setmetatable(script.m_state, -2); // [env]
					lua_pushvalue(script.m_state, LUA_GLOBALSINDEX); // [evn, _G]
					lua_setfield(script.m_state, -2, "__index");  // [env]

					// set this
					lua_getglobal(script.m_state, "Lumix"); // [env, Lumix]
					lua_getfield(script.m_state, -1, "Entity"); // [env, Lumix, Lumix.Entity]
					lua_remove(script.m_state, -2); // [env, Lumix.Entity]
					lua_getfield(script.m_state, -1, "new"); // [env, Lumix.Entity, Entity.new]
					lua_pushvalue(script.m_state, -2); // [env, Lumix.Entity, Entity.new, Lumix.Entity]
					lua_remove(script.m_state, -3); // [env, Entity.new, Lumix.Entity]
					LuaWrapper::push(script.m_state, &m_scene.m_universe); // [env, Entity.new, Lumix.Entity, universe]
					LuaWrapper::push(script.m_state, m_entity.index); // [env, Entity.new, Lumix.Entity, universe, entity_index]
					const bool error = !LuaWrapper::pcall(script.m_state, 3, 1); // [env, entity]
					ASSERT(!error);
					lua_setfield(script.m_state, -2, "this"); // [env]
				}
				else
				{
					lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment); // [env]
					ASSERT(lua_type(script.m_state, -1) == LUA_TTABLE);
				}

				bool errors = luaL_loadbuffer(script.m_state,
					script.m_script->getSourceCode(),
					stringLength(script.m_script->getSourceCode()),
					script.m_script->getPath().c_str()) != 0; // [env, func]

				if (errors)
				{
					logError("Lua Script") << script.m_script->getPath() << ": "
						<< lua_tostring(script.m_state, -1);
					lua_pop(script.m_state, 1);
					continue;
				}

				lua_pushvalue(script.m_state, -2); // [env, func, env]
				lua_setfenv(script.m_state, -2);

				m_scene.m_current_script_instance = &script;
				errors = lua_pcall(script.m_state, 0, 0, 0) != 0; // [env]
				if (errors)
				{
					logError("Lua Script") << script.m_script->getPath() << ": "
						<< lua_tostring(script.m_state, -1);
					lua_pop(script.m_state, 1);
				}
				lua_pop(script.m_state, 1); // []

				detectProperties(script);
					
				bool enabled = script.m_flags.isSet(ScriptInstance::ENABLED);
				m_scene.setEnableProperty(m_entity, scr_index, script, enabled);

				lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment);
				lua_getfield(script.m_state, -1, "awake");
				if (lua_type(script.m_state, -1) != LUA_TFUNCTION)
				{
					lua_pop(script.m_state, 2);
				}
				else {
					if (lua_pcall(script.m_state, 0, 0, 0) != 0) {
						logError("Lua Script") << lua_tostring(script.m_state, -1);
						lua_pop(script.m_state, 1);
					}
				}
				lua_pop(script.m_state, 1);

				if (m_scene.m_is_game_running) m_scene.startScript(script, is_reload);
			}
		}


		Array<ScriptInstance> m_scripts;
		LuaScriptSceneImpl& m_scene;
		EntityRef m_entity;
	};


	struct FunctionCall : IFunctionCall
	{
		void add(int parameter) override
		{
			lua_pushinteger(state, parameter);
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
		, m_input_handlers(system.m_allocator)
		, m_timers(system.m_allocator)
		, m_property_names(system.m_allocator)
		, m_is_game_running(false)
		, m_is_api_registered(false)
		, m_animation_scene(nullptr)
	{
		m_function_call.is_in_progress = false;
			
		registerAPI();
		ctx.registerComponentType(LUA_SCRIPT_TYPE
			, this
			, &LuaScriptSceneImpl::createLuaScriptComponent
			, &LuaScriptSceneImpl::destroyLuaScriptComponent);
	}


	int getVersion() const override { return (int)LuaSceneVersion::LATEST; }


	IFunctionCall* beginFunctionCall(EntityRef entity, int scr_index, const char* function) override
	{
		ASSERT(!m_function_call.is_in_progress);

		auto* script_cmp = m_scripts[entity];
		auto& script = script_cmp->m_scripts[scr_index];
		if (!script.m_state) return nullptr;

		lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment);
		ASSERT(lua_type(script.m_state, -1) == LUA_TTABLE);
		lua_getfield(script.m_state, -1, function);
		if (lua_type(script.m_state, -1) != LUA_TFUNCTION)
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

		if (lua_pcall(script.m_state, m_function_call.parameter_count, 0, 0) != 0)
		{
			logWarning("Lua Script") << lua_tostring(script.m_state, -1);
			lua_pop(script.m_state, 1);
		}
		lua_pop(script.m_state, 1);
	}


	int getPropertyCount(EntityRef entity, int scr_index) override
	{
		return m_scripts[entity]->m_scripts[scr_index].m_properties.size();
	}


	const char* getPropertyName(EntityRef entity, int scr_index, int prop_index) override
	{
		return getName(m_scripts[entity]->m_scripts[scr_index].m_properties[prop_index]);
	}


	ResourceType getPropertyResourceType(EntityRef entity, int scr_index, int prop_index) override
	{
		return m_scripts[entity]->m_scripts[scr_index].m_properties[prop_index].resource_type;
	}


	Property::Type getPropertyType(EntityRef entity, int scr_index, int prop_index) override
	{
		return m_scripts[entity]->m_scripts[scr_index].m_properties[prop_index].type;
	}


	void getScriptData(EntityRef entity, IOutputStream& blob) override
	{
		auto* scr = m_scripts[entity];
		blob.write(scr->m_scripts.size());
		for (int i = 0; i < scr->m_scripts.size(); ++i)
		{
			auto& inst = scr->m_scripts[i];
			blob.writeString(inst.m_script ? inst.m_script->getPath().c_str() : "");
			blob.write(inst.m_flags);
			blob.write(inst.m_properties.size());
			for (auto& prop : inst.m_properties)
			{
				blob.write(prop.name_hash);
				blob.write(prop.type);
				char tmp[1024];
				tmp[0] = '\0';
				const char* prop_name = getName(prop);
				if(prop_name) getPropertyValue(entity, i, getName(prop), Span(tmp));
				blob.writeString(prop_name ? tmp : prop.stored_value.c_str());
			}
		}
	}


	static void sortProperties(Array<Property>& props) {
		qsort(props.begin(), props.size(), sizeof(props[0]), [](const void* a, const void* b){
			const u32 h0 = ((Property*)a)->name_hash;
			const u32 h1 = ((Property*)b)->name_hash;
			return int(h0 > h1 ? 1 : (h0 < h1 ? -1 : 0));
		});
	}


	void setScriptData(EntityRef entity, InputMemoryStream& blob) override
	{
		auto* scr = m_scripts[entity];
		int count;
		blob.read(count);
		for (int i = 0; i < count; ++i)
		{
			addScript(entity, i);
			auto& inst = scr->m_scripts[i];
			char tmp[MAX_PATH_LENGTH];
			blob.readString(Span(tmp));
			blob.read(inst.m_flags);
			setScriptPath(entity, i, Path(tmp));
				
			int prop_count;
			blob.read(prop_count);
			for (int j = 0; j < prop_count; ++j)
			{
				u32 hash;
				blob.read(hash);
				int prop_index = scr->getProperty(inst, hash);
				if (prop_index < 0)
				{
					scr->m_scripts[i].m_properties.emplace(m_system.m_allocator);
					prop_index = scr->m_scripts[i].m_properties.size() - 1;
				}
				auto& prop = scr->m_scripts[i].m_properties[prop_index];
				prop.name_hash = hash;
				blob.read(prop.type);
				char tmp[1024];
				blob.readString(Span(tmp));
				prop.stored_value = tmp;
				if (scr->m_scripts[i].m_state) applyProperty(scr->m_scripts[i], prop, tmp);
			}
			sortProperties(scr->m_scripts[i].m_properties);
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


	lua_State* getState(EntityRef entity, int scr_index) override
	{
		return m_scripts[entity]->m_scripts[scr_index].m_state;
	}


	Universe& getUniverse() override { return m_universe; }


	static int setPropertyType(lua_State* L)
	{
		LuaWrapper::DebugGuard guard(L);
		LuaWrapper::checkTableArg(L, 1);
		const char* prop_name = LuaWrapper::checkArg<const char*>(L, 2);
		int type = LuaWrapper::checkArg<int>(L, 3);
		ResourceType resource_type;
		if (type == Property::Type::RESOURCE) {
			resource_type = ResourceType(LuaWrapper::checkArg<const char*>(L, 4));
		}

		lua_getfield(L, 1, "universe");
		if (!lua_istable(L, -1)) {
			luaL_error(L, "%s", "Invalid `this.universe`");
		}

		lua_getfield(L, -1, "value");
		if (!lua_islightuserdata(L, -1)) {
			luaL_error(L, "%s", "Invalid `this.universe.value`");
		}

		auto* universe = LuaWrapper::toType<Universe*>(L, -1);
		auto* scene = (LuaScriptSceneImpl*)universe->getScene(LUA_SCRIPT_TYPE);

		lua_pop(L, 2);
		u32 prop_name_hash = crc32(prop_name);
		for (auto& prop : scene->m_current_script_instance->m_properties)
		{
			if (prop.name_hash == prop_name_hash)
			{
				prop.type = (Property::Type)type;
				prop.resource_type = resource_type;
				return 0;
			}
		}

		auto& prop = scene->m_current_script_instance->m_properties.emplace(scene->m_system.m_allocator);
		prop.name_hash = prop_name_hash;
		prop.type = (Property::Type)type;
		prop.resource_type = resource_type;
		auto iter = scene->m_property_names.find(prop_name_hash);
		if (!iter.isValid())
		{
			scene->m_property_names.insert(prop_name_hash, String(prop_name, scene->m_system.m_allocator));
		}
		sortProperties(scene->m_current_script_instance->m_properties);
		return 0;
	}


	void registerPropertyAPI()
	{
		lua_State* L = m_system.m_engine.getState();
		auto f = &LuaWrapper::wrap<&setPropertyType>;
		LuaWrapper::createSystemFunction(L, "Editor", "setPropertyType", f);
		LuaWrapper::createSystemVariable(L, "Editor", "BOOLEAN_PROPERTY", Property::BOOLEAN);
		LuaWrapper::createSystemVariable(L, "Editor", "FLOAT_PROPERTY", Property::FLOAT);
		LuaWrapper::createSystemVariable(L, "Editor", "INT_PROPERTY", Property::INT);
		LuaWrapper::createSystemVariable(L, "Editor", "ENTITY_PROPERTY", Property::ENTITY);
		LuaWrapper::createSystemVariable(L, "Editor", "RESOURCE_PROPERTY", Property::RESOURCE);
	}


	static int getEnvironment(lua_State* L)
	{
		auto* scene = LuaWrapper::checkArg<LuaScriptScene*>(L, 1);
		EntityRef entity = LuaWrapper::checkArg<EntityRef>(L, 2);
		int scr_index = LuaWrapper::checkArg<int>(L, 3);

		if (!scene->getUniverse().hasComponent(entity, LUA_SCRIPT_TYPE))
		{
			lua_pushnil(L);
			return 1;
		}
		int count = scene->getScriptCount(entity);
		if (scr_index >= count)
		{
			lua_pushnil(L);
			return 1;
		}

		int env = scene->getEnvironment(entity, scr_index);
		if (env < 0)
		{
			lua_pushnil(L);
		}
		else
		{
			lua_rawgeti(L, LUA_REGISTRYINDEX, env);
			ASSERT(lua_type(L, -1) == LUA_TTABLE);
		}
		return 1;
	}
		
	static void convertPropertyToLuaName(const char* src, Span<char> out)
	{
		const u32 max_size = out.length();
		ASSERT(max_size > 0);
		bool to_upper = true;
		char* dest = out.begin();
		while (*src && dest - out.begin() < max_size - 1)
		{
			if (isLetter(*src))
			{
				*dest = to_upper && !isUpperCase(*src) ? *src - 'a' + 'A' : *src;
				to_upper = false;
				++dest;
			}
			else if (isNumeric(*src))
			{
				*dest = *src;
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

	struct LuaPropGetterVisitor final : IComponentVisitor
	{
		static bool isSameProperty(const char* name, const char* lua_name) {
			char tmp[50];
			convertPropertyToLuaName(name, Span(tmp));
			return equalStrings(tmp, lua_name);
		}

		template <typename T>
		void get(const Prop<T>& prop)
		{
			if (!isSameProperty(prop.name, prop_name)) return;
				
			T val = prop.get();
			count = 1;
			LuaWrapper::push(L, val);
		}

		void visit(const Prop<float>& prop) override { get(prop); }
		void visit(const Prop<int>& prop) override { get(prop); }
		void visit(const Prop<u32>& prop) override { get(prop); }
		void visit(const Prop<EntityPtr>& prop) override { get(prop); }
		void visit(const Prop<Vec2>& prop) override { get(prop); }
		void visit(const Prop<Vec3>& prop) override { get(prop); }
		void visit(const Prop<IVec3>& prop) override { get(prop); }
		void visit(const Prop<Vec4>& prop) override { get(prop); }
		void visit(const Prop<bool>& prop) override { get(prop); }

		void visit(const Prop<Path>& prop) override { 
			if (!isSameProperty(prop.name, prop_name)) return;
				
			const Path p = prop.get();
			count = 1;
			LuaWrapper::push(L, p.c_str());
		}

		void visit(const Prop<const char*>& prop) override { 
			if (!isSameProperty(prop.name, prop_name)) return;
				
			count = 1;
			LuaWrapper::push(L, prop.get());
		}
		
		bool beginArray(const char* name, const ArrayProp& prop) override { return false; }

		const char* prop_name;
		u32 count = 0;
		lua_State* L;
	};
		
	struct LuaPropSetterVisitor final : IComponentVisitor
	{
		bool isSameProperty(const char* name, const char* lua_name) {
			char tmp[50];
			convertPropertyToLuaName(name, Span(tmp));
			if (equalStrings(tmp, lua_name)) {
				found = true;
				return true;
			}
			return false;
		}

		template <typename T>
		void set(const Prop<T>& prop)
		{
			if (!isSameProperty(prop.name, prop_name)) return;
				
			const T val = LuaWrapper::toType<T>(L, 3);
			prop.set(val);
		}

		void visit(const Prop<float>& prop) override { set(prop); }
		void visit(const Prop<int>& prop) override { set(prop); }
		void visit(const Prop<u32>& prop) override { set(prop); }
		void visit(const Prop<EntityPtr>& prop) override { set(prop); }
		void visit(const Prop<Vec2>& prop) override { set(prop); }
		void visit(const Prop<Vec3>& prop) override { set(prop); }
		void visit(const Prop<IVec3>& prop) override { set(prop); }
		void visit(const Prop<Vec4>& prop) override { set(prop); }
		void visit(const Prop<bool>& prop) override { set(prop); }

		void visit(const Prop<Path>& prop) override {
			if (!isSameProperty(prop.name, prop_name)) return;
				
			const char* val = LuaWrapper::toType<const char*>(L, 3);
			InputMemoryStream blob(val, 1 + stringLength(val));
			prop.set(Path(val));
		}

		void visit(const Prop<const char*>& prop) override { 
			if (!isSameProperty(prop.name, prop_name)) return;

			const char* val = LuaWrapper::toType<const char*>(L, 3);
			prop.set(val);	
		}

		bool beginArray(const char* name, const ArrayProp& prop) override { return false; }

		const char* prop_name;
		lua_State* L;
		bool found = false;
	};

	static int lua_new_cmp(lua_State* L) {
		LuaWrapper::DebugGuard guard(L, 1);
		LuaWrapper::checkTableArg(L, 1); // self
		const Universe* universe = LuaWrapper::checkArg<Universe*>(L, 2);
		const EntityRef e = LuaWrapper::checkArg<EntityRef>(L, 3);
			
		LuaWrapper::getField(L, 1, "cmp_type");
		const int cmp_type = LuaWrapper::toType<int>(L, -1);
		lua_pop(L, 1);
		IScene* scene = universe->getScene(ComponentType{cmp_type});

		lua_newtable(L);
		LuaWrapper::setField(L, -1, "entity", e);
		LuaWrapper::setField(L, -1, "scene", scene);
		lua_pushvalue(L, 1);
		lua_setmetatable(L, -2);
		return 1;
	}

	static int lua_prop_getter(lua_State* L) {
		LuaWrapper::checkTableArg(L, 1); // self

		LuaPropGetterVisitor v;
		v.prop_name = LuaWrapper::checkArg<const char*>(L, 2);
		v.L = L;
		const ComponentType cmp_type = LuaWrapper::toType<ComponentType>(L, lua_upvalueindex(1));

		lua_getfield(L, 1, "scene");
		IScene* scene = LuaWrapper::toType<IScene*>(L, -1);
		lua_getfield(L, 1, "entity");
		const EntityRef e = LuaWrapper::toType<EntityRef>(L, -1);
		lua_pop(L, 2);

		scene->visit(e, cmp_type, v);

		if (v.count == 0) {
			luaL_error(L, "Property `%s` does not exist", v.prop_name);
		}

		return v.count;
	}

	static int lua_prop_setter(lua_State* L) {
		LuaWrapper::checkTableArg(L, 1); // self

		LuaPropSetterVisitor v;
		v.prop_name = LuaWrapper::checkArg<const char*>(L, 2);
		v.L = L;
		const ComponentType cmp_type = LuaWrapper::toType<ComponentType>(L, lua_upvalueindex(1));

		lua_getfield(L, 1, "scene");
		IScene* scene = LuaWrapper::toType<IScene*>(L, -1);
		lua_getfield(L, 1, "entity");
		const EntityRef e = LuaWrapper::toType<EntityRef>(L, -1);
		lua_pop(L, 2);

		scene->visit(e, cmp_type, v);

		if (!v.found) {
			luaL_error(L, "Property `%s` does not exist", v.prop_name);
		}

		return 0;
	}

	void registerProperties()
	{
		int cmps_count = Reflection::getComponentTypesCount();
		lua_State* L = m_system.m_engine.getState();
		LuaWrapper::DebugGuard guard(L);
		for (int i = 0; i < cmps_count; ++i) {
			const char* cmp_name = Reflection::getComponentTypeID(i);
			const ComponentType cmp_type = Reflection::getComponentType(cmp_name);

			lua_newtable(L); // [ cmp ]
			lua_getglobal(L, "Lumix"); // [ cmp, Lumix ]
			lua_pushvalue(L, -2); // [ cmp, Lumix, cmp]
			lua_setfield(L, -2, cmp_name); // [ cmp, Lumix ]
			lua_pop(L, 1); // [ cmp ]

			lua_pushcfunction(L, lua_new_cmp); // [ cmp, fn_new_cmp ]
			lua_setfield(L, -2, "new"); // [ cmp ]

			LuaWrapper::setField(L, -1, "cmp_type", cmp_type.index);

			LuaWrapper::push(L, cmp_type); // [ cmp, cmp_type ]
			lua_pushcclosure(L, lua_prop_getter, 1); // [ cmp, fn_prop_getter ]
			lua_setfield(L, -2, "__index"); // [ cmp ]
				
			LuaWrapper::push(L, cmp_type); // [ cmp, cmp_type ]
			lua_pushcclosure(L, lua_prop_setter, 1); // [ cmp, fn_prop_setter ]
			lua_setfield(L, -2, "__newindex"); // [ cmp ]

			lua_pop(L, 1);
		}
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


	void setScriptSource(EntityRef entity, int scr_index, const char* path)
	{
		setScriptPath(entity, scr_index, Path(path));
	}


	void registerAPI()
	{
		if (m_is_api_registered) return;

		m_is_api_registered = true;

		lua_State* engine_state = m_system.m_engine.getState();
			
		registerProperties();
		registerPropertyAPI();
		LuaWrapper::createSystemFunction(
			engine_state, "LuaScript", "getEnvironment", &LuaScriptSceneImpl::getEnvironment);
			
		#define REGISTER_FUNCTION(F) \
			do { \
				auto f = &LuaWrapper::wrapMethod<&LuaScriptSceneImpl::F>; \
				LuaWrapper::createSystemFunction(engine_state, "LuaScript", #F, f); \
			} while(false)

		REGISTER_FUNCTION(addScript);
		REGISTER_FUNCTION(getScriptCount);
		REGISTER_FUNCTION(setScriptSource);
		REGISTER_FUNCTION(cancelTimer);

		#undef REGISTER_FUNCTION

		LuaWrapper::createSystemFunction(engine_state, "LuaScript", "setTimer", &LuaScriptSceneImpl::setTimer);
	}


	int getEnvironment(EntityRef entity, int scr_index) override
	{
		return m_scripts[entity]->m_scripts[scr_index].m_environment;
	}


	void applyResourceProperty(ScriptInstance& script, const char* name, Property& prop, const char* value)
	{
		lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment);
		ASSERT(lua_type(script.m_state, -1));
		lua_getfield(script.m_state, -1, name);
		int res_idx = LuaWrapper::toType<int>(script.m_state, -1);
		m_system.m_engine.unloadLuaResource(res_idx);
		lua_pop(script.m_state, 1);

		int new_res = value[0] ? m_system.m_engine.addLuaResource(Path(value), prop.resource_type) : -1;
		lua_pushinteger(script.m_state, new_res);
		lua_setfield(script.m_state, -2, name);
		lua_pop(script.m_state, 1);
	}


	void applyProperty(ScriptInstance& script, Property& prop, const char* value)
	{
		if (!value) return;
		lua_State* state = script.m_state;
		if (!state) return;
		const char* name = getName(prop);
		if (!name) return;

		if (prop.type == Property::RESOURCE)
		{
			applyResourceProperty(script, name, prop, value);
			return;
		}

		StaticString<1024> tmp(name, " = ");
		if (prop.type == Property::STRING) tmp << "\"" << value << "\"";
		else tmp << value;

		bool errors = luaL_loadbuffer(state, tmp, stringLength(tmp), nullptr) != 0;
		if (errors)
		{
			logError("Lua Script") << script.m_script->getPath() << ": " << lua_tostring(state, -1);
			lua_pop(state, 1);
			return;
		}

		lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment);
		ASSERT(lua_type(script.m_state, -1) == LUA_TTABLE);
		lua_setfenv(script.m_state, -2);

		errors = lua_pcall(state, 0, 0, 0) != 0;

		if (errors)
		{
			logError("Lua Script") << script.m_script->getPath() << ": " << lua_tostring(state, -1);
			lua_pop(state, 1);
		}
	}


	void setPropertyValue(EntityRef entity,
		int scr_index,
		const char* name,
		const char* value) override
	{
		auto* script_cmp = m_scripts[entity];
		if (!script_cmp) return;
		Property& prop = getScriptProperty(entity, scr_index, name);
		if (!script_cmp->m_scripts[scr_index].m_state)
		{
			prop.stored_value = value;
			return;
		}

		applyProperty(script_cmp->m_scripts[scr_index], prop, value);
	}


	const char* getPropertyName(EntityRef entity, int scr_index, int index) const
	{
		auto& script = m_scripts[entity]->m_scripts[scr_index];

		return getName(script.m_properties[index]);
	}


	int getPropertyCount(EntityRef entity, int scr_index) const
	{
		auto& script = m_scripts[entity]->m_scripts[scr_index];

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
		if (ptr == nullptr) return allocator.allocate(nsize);

		void* new_mem = allocator.allocate(nsize);
		memcpy(new_mem, ptr, minimum(osize, nsize));
		allocator.deallocate(ptr);
		return new_mem;
	}


	void disableScript(ScriptInstance& inst)
	{
		for (int i = 0; i < m_timers.size(); ++i)
		{
			if (m_timers[i].state == inst.m_state)
			{
				luaL_unref(m_timers[i].state, LUA_REGISTRYINDEX, m_timers[i].func);
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


	void destroyInstance(ScriptComponent& scr,  ScriptInstance& inst)
	{
		lua_rawgeti(inst.m_state, LUA_REGISTRYINDEX, inst.m_environment);
		ASSERT(lua_type(inst.m_state, -1) == LUA_TTABLE);
		lua_getfield(inst.m_state, -1, "onDestroy");
		if (lua_type(inst.m_state, -1) != LUA_TFUNCTION)
		{
			lua_pop(inst.m_state, 2);
		}
		else
		{
			if (lua_pcall(inst.m_state, 0, 0, 0) != 0)
			{
				logError("Lua Script") << lua_tostring(inst.m_state, -1);
				lua_pop(inst.m_state, 1);
			}
			lua_pop(inst.m_state, 1);
		}

		disableScript(inst);

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
			cb.unbind<&ScriptComponent::onScriptLoaded>(&cmp);
			inst.m_script->getResourceManager().unload(*inst.m_script);
		}
		ResourceManagerHub& rm = m_system.m_engine.getResourceManager();
		inst.m_script = path.isValid() ? rm.load<LuaScript>(path) : nullptr;
		if (inst.m_script)
		{
			inst.m_script->onLoaded<&ScriptComponent::onScriptLoaded>(&cmp);
		}
	}


	void startScript(ScriptInstance& instance, bool is_restart)
	{
		if (!instance.m_flags.isSet(ScriptInstance::ENABLED)) return;
		if (!instance.m_state) return;

		if (is_restart) disableScript(instance);
			
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
			update_data.script = instance.m_script;
			update_data.state = instance.m_state;
			update_data.environment = instance.m_environment;
		}
		lua_pop(instance.m_state, 1);
		lua_getfield(instance.m_state, -1, "onInputEvent");
		if (lua_type(instance.m_state, -1) == LUA_TFUNCTION)
		{
			auto& callback = m_input_handlers.emplace();
			callback.script = instance.m_script;
			callback.state = instance.m_state;
			callback.environment = instance.m_environment;
		}
		lua_pop(instance.m_state, 1);

		if (!is_restart)
		{
			lua_getfield(instance.m_state, -1, "start");
			if (lua_type(instance.m_state, -1) != LUA_TFUNCTION)
			{
				lua_pop(instance.m_state, 2);
				return;
			}

			if (lua_pcall(instance.m_state, 0, 0, 0) != 0)
			{
				logError("Lua Script") << lua_tostring(instance.m_state, -1);
				lua_pop(instance.m_state, 1);
			}
		}
		lua_pop(instance.m_state, 1);
	}


	void onButtonClicked(EntityRef e) { onGUIEvent(e, "onButtonClicked"); }
	void onRectHovered(EntityRef e) { onGUIEvent(e, "onRectHovered"); }
	void onRectHoveredOut(EntityRef e) { onGUIEvent(e, "onRectHoveredOut"); }


	LUMIX_FORCE_INLINE void onGUIEvent(EntityRef e, const char* event)
	{
		if (!m_universe.hasComponent(e, LUA_SCRIPT_TYPE)) return;

		for (int i = 0, c = getScriptCount(e); i < c; ++i)
		{
			auto* call = beginFunctionCall(e, i, event);
			if (call) endFunctionCall();
		}
	}


	void startGame() override
	{
		m_animation_scene = (AnimationScene*)m_universe.getScene(crc32("animation"));
		m_is_game_running = true;
		m_gui_scene = (GUIScene*)m_universe.getScene(crc32("gui"));
		if (m_gui_scene)
		{
			m_gui_scene->buttonClicked().bind<&LuaScriptSceneImpl::onButtonClicked>(this);
			m_gui_scene->rectHovered().bind<&LuaScriptSceneImpl::onRectHovered>(this);
			m_gui_scene->rectHoveredOut().bind<&LuaScriptSceneImpl::onRectHoveredOut>(this);
		}
	}


	void stopGame() override
	{
		if (m_gui_scene)
		{
			m_gui_scene->buttonClicked().unbind<&LuaScriptSceneImpl::onButtonClicked>(this);
			m_gui_scene->rectHovered().unbind<&LuaScriptSceneImpl::onRectHovered>(this);
			m_gui_scene->rectHoveredOut().unbind<&LuaScriptSceneImpl::onRectHoveredOut>(this);
		}
		m_gui_scene = nullptr;
		m_scripts_start_called = false;
		m_is_game_running = false;
		m_updates.clear();
		m_input_handlers.clear();
		m_timers.clear();
		m_animation_scene = nullptr;
	}


	void createLuaScriptComponent(EntityRef entity)
	{
		auto& allocator = m_system.m_allocator;
		ScriptComponent* script = LUMIX_NEW(allocator, ScriptComponent)(*this, entity, allocator);
		m_scripts.insert(entity, script);
		m_universe.onComponentCreated(entity, LUA_SCRIPT_TYPE, this);
	}


	void destroyLuaScriptComponent(EntityRef entity)
	{
		auto* script = m_scripts[entity];
		for (auto& scr : script->m_scripts)
		{
			if (scr.m_state) destroyInstance(*script, scr);
			if (scr.m_script)
			{
				auto& cb = scr.m_script->getObserverCb();
				cb.unbind<&ScriptComponent::onScriptLoaded>(script);
				m_system.getScriptManager().unload(*scr.m_script);
			}
		}
		LUMIX_DELETE(m_system.m_allocator, script);
		m_scripts.erase(entity);
		m_universe.onComponentDestroyed(entity, LUA_SCRIPT_TYPE, this);
	}


	void getPropertyValue(EntityRef entity,
		int scr_index,
		const char* property_name,
		Span<char> out) override
	{
		ASSERT(out.length() > 0);

		u32 hash = crc32(property_name);
		auto& inst = m_scripts[entity]->m_scripts[scr_index];
		for (auto& prop : inst.m_properties)
		{
			if (prop.name_hash == hash)
			{
				if (inst.m_script->isReady())
					getProperty(prop, property_name, inst, out);
				else
					copyString(out, prop.stored_value.c_str());
				return;
			}
		}
		out[0] = '\0';
	}


	void getProperty(Property& prop, const char* prop_name, ScriptInstance& scr, Span<char> out)
	{
		if (out.length() <= 0) return;
		if (!scr.m_state)
		{
			copyString(out, prop.stored_value.c_str());
			return;
		}

		out[0] = '\0';
		lua_rawgeti(scr.m_state, LUA_REGISTRYINDEX, scr.m_environment);
		lua_getfield(scr.m_state, -1, prop_name);
		if (lua_type(scr.m_state, -1) == LUA_TNIL)
		{
			copyString(out, prop.stored_value.c_str());
			lua_pop(scr.m_state, 2);
			return;
		}
		switch (prop.type)
		{
			case Property::BOOLEAN:
			{
				bool b = lua_toboolean(scr.m_state, -1) != 0;
				copyString(out, b ? "true" : "false");
			}
			break;
			case Property::FLOAT:
			{
				float val = (float)lua_tonumber(scr.m_state, -1);
				toCString(val, out, 8);
			}
			break;
			case Property::INT:
			{
				int val = (int )lua_tointeger(scr.m_state, -1);
				toCString(val, out);
			}
			break;
			case Property::ENTITY:
			{
				EntityRef val = { (int)lua_tointeger(scr.m_state, -1) };
				toCString(val.index, out);
			}
			break;
			case Property::STRING:
			{
				copyString(out, lua_tostring(scr.m_state, -1));
			}
			break;
			case Property::RESOURCE:
			{
				int res_idx = LuaWrapper::toType<int>(scr.m_state, -1);
				Resource* res = m_system.m_engine.getLuaResource(res_idx);
				copyString(out, res ? res->getPath().c_str() : "");
			}
			break;
			default: ASSERT(false); break;
		}
		lua_pop(scr.m_state, 2);
	}


	void serialize(OutputMemoryStream& serializer) override
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
				serializer.write(scr.m_flags);
				serializer.write(scr.m_properties.size());
				for (Property& prop : scr.m_properties)
				{
					serializer.write(prop.name_hash);
					auto iter = m_property_names.find(prop.name_hash);
					if (iter.isValid())
					{
						const char* name = iter.value().c_str();
						char tmp[1024];
						getProperty(prop, name, scr, Span(tmp));
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


	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map) override
	{
		int len = serializer.read<int>();
		m_scripts.reserve(len + m_scripts.size());
		for (int i = 0; i < len; ++i)
		{
			auto& allocator = m_system.m_allocator;
			EntityRef entity;
			serializer.read(entity);
			entity = entity_map.get(entity);
			ScriptComponent* script = LUMIX_NEW(allocator, ScriptComponent)(*this, entity, allocator);

			m_scripts.insert(script->m_entity, script);
			int scr_count;
			serializer.read(scr_count);
			for (int j = 0; j < scr_count; ++j)
			{
				auto& scr = script->m_scripts.emplace(allocator);

				char tmp[MAX_PATH_LENGTH];
				serializer.readString(Span(tmp));
				serializer.read(scr.m_flags);
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
					serializer.readString(Span(tmp));
					// TODO map entities if property is of entity type
					prop.stored_value = tmp;
				}
				setScriptPath(*script, scr, Path(tmp));
			}
			m_universe.onComponentCreated(script->m_entity, LUA_SCRIPT_TYPE, this);
		}
	}


	IPlugin& getPlugin() const override { return m_system; }


	void startScripts()
	{
		ASSERT(!m_scripts_start_called && m_is_game_running);
		// copy m_scripts to tmp, because scripts can create other scripts -> m_scripts is not const
		Array<ScriptComponent*> tmp(m_system.m_allocator);
		tmp.reserve(m_scripts.size());
		for (auto* scr : m_scripts) tmp.push(scr);

		for (auto* scr : tmp) {
			for (int j = 0; j < scr->m_scripts.size(); ++j) {
				auto& instance = scr->m_scripts[j];
				if (!instance.m_script) continue;
				if (!instance.m_script->isReady()) continue;
				if (!instance.m_flags.isSet(ScriptInstance::ENABLED)) continue;

				startScript(instance, false);
			}
		}
		m_scripts_start_called = true;
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
					logError("Lua Script") << lua_tostring(timer.state, -1);
					lua_pop(timer.state, 1);
				}
				timers_to_remove[timers_to_remove_count] = i;
				++timers_to_remove_count;
				if (timers_to_remove_count >= lengthOf(timers_to_remove))
				{
					logError("Lua Script") << "Too many lua timers in one frame, some are not executed";
					break;
				}
			}
		}
		for (u32 i = timers_to_remove_count - 1; i != 0xffFFffFF; --i)
		{
			auto& timer = m_timers[timers_to_remove[i]];
			luaL_unref(timer.state, LUA_REGISTRYINDEX, timer.func);
			m_timers.swapAndPop(timers_to_remove[i]);
		}
	}


	void processInputEvent(const CallbackData& callback, const InputSystem::Event& event)
	{
		lua_State* L = callback.state;
		lua_newtable(L); // [lua_event]
		LuaWrapper::push(L, (u32)event.type); // [lua_event, event.type]
		lua_setfield(L, -2, "type"); // [lua_event]

		lua_newtable(L); // [lua_event, lua_device]
		LuaWrapper::push(L, (u32)event.device->type); // [lua_event, lua_device, device.type]
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
				LuaWrapper::push(L, event.data.button.key_id); // [lua_event, button.x_abs]
				lua_setfield(L, -2, "key_id"); // [lua_event]
				LuaWrapper::push(L, event.data.button.x_abs); // [lua_event, button.x_abs]
				lua_setfield(L, -2, "x_abs"); // [lua_event]
				LuaWrapper::push(L, event.data.button.y_abs); // [lua_event, button.y_abs]
				lua_setfield(L, -2, "y_abs"); // [lua_event]
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
			default:
				ASSERT(false);
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
			
		if (lua_pcall(L, 1, 0, 0) != 0)// [lua_event, environment]
		{
			logError("Lua Script") << lua_tostring(L, -1);
			lua_pop(L, 1); // []
		}
		lua_pop(L, 2); // []
	}


	void processInputEvents()
	{
		if (m_input_handlers.empty()) return;
		InputSystem& input_system = m_system.m_engine.getInputSystem();
		const InputSystem::Event* events = input_system.getEvents();
		for (int i = 0, c = input_system.getEventsCount(); i < c; ++i)
		{
			for (const CallbackData& cb : m_input_handlers)
			{
				processInputEvent(cb, events[i]);
			}
		}
	}


	void update(float time_delta, bool paused) override
	{
		PROFILE_FUNCTION();

		if (!m_is_game_running) return;
		if (!m_scripts_start_called) startScripts();

		if (paused) return;

		processInputEvents();
		updateTimers(time_delta);

		for (int i = 0; i < m_updates.size(); ++i)
		{
			CallbackData update_item = m_updates[i];
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
			if (lua_pcall(update_item.state, 1, 0, 0) != 0)
			{
				logError("Lua Script") << lua_tostring(update_item.state, -1);
				lua_pop(update_item.state, 1);
			}
			lua_pop(update_item.state, 1);
		}

		processAnimationEvents();
	}


	void processAnimationEvents()
	{
		if (!m_animation_scene) return;

		InputMemoryStream blob(m_animation_scene->getEventStream());
		u32 lua_call_type = crc32("lua_call");
		while (blob.getPosition() < blob.size())
		{
			u32 type;
			u8 size;
			EntityRef entity;
			blob.read(type);
			blob.read(entity);
			blob.read(size);
			if (type == lua_call_type)
			{
				char tmp[64];
				if (size + 1 > sizeof(tmp))
				{
					blob.skip(size);
					logError("Lua Script") << "Skipping lua_call animation event because it is too big.";
				}
				else
				{
					blob.read(tmp, size);
					tmp[size] = 0;
					ScriptComponent* scr = m_scripts[entity];
					for (int i = 0, c = scr->m_scripts.size(); i < c; ++i)
					{
						if (beginFunctionCall(entity, i, tmp)) endFunctionCall();
					}
				}
			}
			else
			{
				blob.skip(size);
			}
		}
	}


	Property& getScriptProperty(EntityRef entity, int scr_index, const char* name)
	{
		u32 name_hash = crc32(name);
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
		sortProperties(script_cmp->m_scripts[scr_index].m_properties);
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
		setScriptPath(*script_cmp, script_cmp->m_scripts[scr_index], path);
	}


	int getScriptCount(EntityRef entity) override
	{
		return m_scripts[entity]->m_scripts.size();
	}


	void insertScript(EntityRef entity, int idx) override
	{
		m_scripts[entity]->m_scripts.emplaceAt(idx, m_system.m_allocator);
	}


	void addScript(EntityRef entity, u32 index) override
	{
		ScriptComponent* script_cmp = m_scripts[entity];
		script_cmp->m_scripts.emplaceAt(index, m_system.m_allocator);
	}


	void moveScript(EntityRef entity, int scr_index, bool up) override
	{
		auto* script_cmp = m_scripts[entity];
		if (!up && scr_index > script_cmp->m_scripts.size() - 2) return;
		if (up && scr_index == 0) return;
		int other = up ? scr_index - 1 : scr_index + 1;
		ScriptInstance tmp = script_cmp->m_scripts[scr_index];
		script_cmp->m_scripts[scr_index] = script_cmp->m_scripts[other];
		script_cmp->m_scripts[other] = tmp;
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


	void enableScript(EntityRef entity, int scr_index, bool enable) override
	{
		ScriptInstance& inst = m_scripts[entity]->m_scripts[scr_index];
		if (inst.m_flags.isSet(ScriptInstance::ENABLED) == enable) return;

		inst.m_flags.set(ScriptInstance::ENABLED, enable);

		setEnableProperty(entity, scr_index, inst, enable);

		if(enable)
		{
			startScript(inst, false);
		}
		else
		{
			disableScript(inst);
		}
	}


	bool isScriptEnabled(EntityRef entity, int scr_index) const override
	{
		return m_scripts[entity]->m_scripts[scr_index].m_flags.isSet(ScriptInstance::ENABLED);
	}


	void removeScript(EntityRef entity, int scr_index) override
	{
		setScriptPath(entity, scr_index, Path());
		m_scripts[entity]->m_scripts.erase(scr_index);
	}


	void serializeScript(EntityRef entity, int scr_index, OutputMemoryStream& blob) override
	{
		auto& scr = m_scripts[entity]->m_scripts[scr_index];
		blob.writeString(scr.m_script ? scr.m_script->getPath().c_str() : "");
		blob.write(scr.m_flags);
		blob.write(scr.m_properties.size());
		for (auto prop : scr.m_properties)
		{
			blob.write(prop.name_hash);
			char tmp[1024];
			const char* property_name = getName(prop);
			if (!property_name)
			{
				blob.writeString(prop.stored_value.c_str());
			}
			else
			{
				getProperty(prop, property_name, scr, Span(tmp));
				blob.writeString(tmp);
			}
		}
	}


	void deserializeScript(EntityRef entity, int scr_index, InputMemoryStream& blob) override
	{
		auto& scr = m_scripts[entity]->m_scripts[scr_index];
		int count;
		char path[MAX_PATH_LENGTH];
		blob.readString(Span(path));
		blob.read(scr.m_flags);
		blob.read(count);
		scr.m_environment = -1;
		scr.m_properties.clear();
		char buf[256];
		for (int i = 0; i < count; ++i)
		{
			auto& prop = scr.m_properties.emplace(m_system.m_allocator);
			prop.type = Property::ANY;
			blob.read(prop.name_hash);
			blob.readString(Span(buf));
			prop.stored_value = buf;
		}
		setScriptPath(entity, scr_index, Path(path));
	}

	const char* getName(const Property& p) const {
		auto iter = m_property_names.find(p.name_hash);
		if (iter.isValid()) return iter.value().c_str();
		ASSERT(false);
		return "N/A";
	}

	void visit(EntityRef entity, ComponentType cmp_type, struct IComponentVisitor& v) override {
		ASSERT(cmp_type == LUA_SCRIPT_TYPE);
		ScriptComponent* cmp = m_scripts[entity];
		struct : ArrayProp {
			u32 count() const override { return that->m_scripts[entity]->m_scripts.size(); }
			void add(u32 idx) const override { that->addScript(entity, idx); }
			void remove(u32 idx) const override { that->removeScript(entity, idx); }
			LuaScriptSceneImpl* that;
			EntityRef entity;
		} ar;
		ar.that = this;
		ar.entity = entity;

		if (!v.beginArray("Scripts", ar)) return;
		for (u32 i = 0; i < (u32)cmp->m_scripts.size(); ++i) {
			if (!v.beginArrayItem("Scripts", i, ar)) continue;

			ScriptInstance& scr = cmp->m_scripts[i];
			Lumix::visit(v, "Path", this, entity, (int)i, LUMIX_PROP(LuaScriptScene, ScriptPath), Reflection::ResourceAttribute("*.lua", ResourceType("lua_script")));
			for (u32 j = 0; j < (u32)scr.m_properties.size(); ++j) {
				Property& prop = scr.m_properties[j];
				const char* name = getName(prop);
				char buf[256];
				if (scr.m_script->isReady())
					getProperty(prop, name, scr, Span(buf));
				else
					copyString(Span(buf), prop.stored_value.c_str());

				switch (prop.type) {
					case Property::BOOLEAN:
						Lumix::visitFunctor(v, name,
							[buf](){ return equalIStrings(buf, "true"); },
							[j, this, &scr, &prop](bool v){
								const char* tmp = v ? "true" : "false";
								if (scr.m_state) {
									applyProperty(scr, prop, tmp);
								}
								else {
									prop.stored_value = tmp;
								}
							}
						);
						break;
					case Property::INT:
						Lumix::visitFunctor(v, name,
							[buf](){ return atoi(buf); },
							[j, this, &scr, &prop](i32 v){
								char tmp[64];
								toCString(v, Span(tmp));
								if (scr.m_state) {
									applyProperty(scr, prop, tmp);
								}
								else {
									prop.stored_value = tmp;
								}
							}
						);
						break;
					case Property::FLOAT:
						Lumix::visitFunctor(v, name, 
							[buf](){ return (float)atof(buf); },
							[j, this, &scr, &prop](float v){
								char tmp[64];
								toCString(v, Span(tmp), 32);
								if (scr.m_state) {
									applyProperty(scr, prop, tmp);
								}
								else {
									prop.stored_value = tmp;
								}
							}
						);
						break;
					case Property::RESOURCE: {
						Lumix::visitFunctor(v, name,
							[buf](){ return Path(buf); },
							[j, this, &scr, &prop](Path v){
								if (scr.m_state) {
									applyProperty(scr, prop, v.c_str());
								}
								else {
									prop.stored_value = v.c_str();
								}
							},
							Reflection::ResourceAttribute("", prop.resource_type)
						);
						break;
					}
					case Property::STRING:
					case Property::ANY:
						Lumix::visitFunctor(v, name,
							[buf](){ return buf; },
							[j, this, &scr, &prop](const char* v){
								if (scr.m_state) {
									applyProperty(scr, prop, v);
								}
								else {
									prop.stored_value = v;
								}
							}
						);
						break;
					case Property::ENTITY:
						Lumix::visitFunctor(v, name,
							[buf](){ return EntityPtr{atoi(buf)}; },
							[j, this, &scr, &prop](EntityPtr v){
								char buf[64];
								toCString(v.index, Span(buf));
								if (scr.m_state) {
									applyProperty(scr, prop, buf);
								}
								else {
									prop.stored_value = buf;
								}
							}
						);
						break;
					default: ASSERT(false); break;
				}
			}
			v.endArrayItem();
		}
		v.endArray();
	}

	LuaScriptSystemImpl& m_system;
	HashMap<EntityRef, ScriptComponent*> m_scripts;
	HashMap<u32, String> m_property_names;
	Array<CallbackData> m_input_handlers;
	Universe& m_universe;
	Array<CallbackData> m_updates;
	Array<TimerData> m_timers;
	FunctionCall m_function_call;
	ScriptInstance* m_current_script_instance;
	bool m_scripts_start_called = false;
	bool m_is_api_registered = false;
	bool m_is_game_running = false;
	GUIScene* m_gui_scene = nullptr;
	AnimationScene* m_animation_scene;
};


LuaScriptSystemImpl::LuaScriptSystemImpl(Engine& engine)
	: m_engine(engine)
	, m_allocator(engine.getAllocator())
	, m_script_manager(m_allocator)
{
	m_script_manager.create(LuaScript::TYPE, engine.getResourceManager());
}


LuaScriptSystemImpl::~LuaScriptSystemImpl()
{
	m_script_manager.destroy();
}


void LuaScriptSystemImpl::createScenes(Universe& ctx)
{
	auto* scene = LUMIX_NEW(m_allocator, LuaScriptSceneImpl)(*this, ctx);
	ctx.addScene(scene);
}


void LuaScriptSystemImpl::destroyScene(IScene* scene)
{
	LUMIX_DELETE(m_allocator, scene);
}


LUMIX_PLUGIN_ENTRY(lua_script)
	{
		return LUMIX_NEW(engine.getAllocator(), LuaScriptSystemImpl)(engine);
	}

} // namespace Lumix
