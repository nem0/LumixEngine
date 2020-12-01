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
#include "engine/metaprogramming.h"
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
	static void pushObject(lua_State* L, void* obj, const char* type_name) {
		LuaWrapper::DebugGuard guard(L, 1);
		lua_getglobal(L, "LumixAPI");
		char tmp[64];
		const char* c = type_name + strlen(type_name);
		while (*c != ':' && c != type_name) --c;
		if (*c == ':') ++c;
		copyNString(Span(tmp), c, int(strlen(c)) - 2);

		if (LuaWrapper::getField(L, -1, tmp) != LUA_TTABLE) {
			lua_pop(L, 2);
			lua_newtable(L);
			lua_pushlightuserdata(L, obj);
			lua_setfield(L, -2, "_value");
			ASSERT(false);
			return;
		}

		lua_newtable(L); // [LumixAPI, class, obj]
		lua_pushlightuserdata(L, obj); // [LumixAPI, class, obj, obj_ptr]
		lua_setfield(L, -2, "_value"); // [LumixAPI, class, obj]
		lua_pushvalue(L, -2); // [LumixAPI, class, obj, class]
		lua_setmetatable(L, -2); // [LumixAPI, class, obj]
		lua_remove(L, -2); // [LumixAPI, obj]
		lua_remove(L, -2); // [obj]
	}

	static void toVariant(Reflection::Variant::Type type, lua_State* L, int idx, Ref<Reflection::Variant> val) {
		switch(type) {
			case Reflection::Variant::BOOL: val = LuaWrapper::toType<bool>(L, idx); break;
			case Reflection::Variant::U32: val = LuaWrapper::toType<u32>(L, idx); break;
			case Reflection::Variant::I32: val = LuaWrapper::toType<i32>(L, idx); break;
			case Reflection::Variant::FLOAT: val = LuaWrapper::toType<float>(L, idx); break;
			case Reflection::Variant::ENTITY: val = LuaWrapper::toType<EntityPtr>(L, idx); break;
			case Reflection::Variant::VEC2: val = LuaWrapper::toType<Vec2>(L, idx); break;
			case Reflection::Variant::VEC3: val = LuaWrapper::toType<Vec3>(L, idx); break;
			case Reflection::Variant::DVEC3: val = LuaWrapper::toType<DVec3>(L, idx); break;
			case Reflection::Variant::CSTR: val = LuaWrapper::toType<const char*>(L, idx); break;
			case Reflection::Variant::PTR: {
				void* ptr;
				if (!LuaWrapper::checkField(L, idx, "_value", &ptr)) {
					luaL_argerror(L, idx, "expected object");
				}
				val = ptr;
				break;
			}
			default: ASSERT(false); break;
		}	
	}

	static int push(lua_State* L, const Reflection::Variant& v, const char* type_name) {
		switch (v.type) {
			case Reflection::Variant::ENTITY: ASSERT(false); return 0;
			case Reflection::Variant::VOID: return 0;
			case Reflection::Variant::BOOL: LuaWrapper::push(L, v.b); return 1;
			case Reflection::Variant::U32: LuaWrapper::push(L, v.u); return 1;
			case Reflection::Variant::I32: LuaWrapper::push(L, v.i); return 1;
			case Reflection::Variant::FLOAT: LuaWrapper::push(L, v.f); return 1;
			case Reflection::Variant::CSTR: LuaWrapper::push(L, v.s); return 1;
			case Reflection::Variant::VEC2: LuaWrapper::push(L, v.v2); return 1;
			case Reflection::Variant::VEC3: LuaWrapper::push(L, v.v3); return 1;
			case Reflection::Variant::DVEC3: LuaWrapper::push(L, v.dv3); return 1;
			case Reflection::Variant::PTR: pushObject(L, v.ptr, type_name); return 1;
			default: ASSERT(false); return 0;
		}
	}

	static int luaMethodClosure(lua_State* L) {
		LuaWrapper::checkTableArg(L, 1); // self
		void* obj;
		if (!LuaWrapper::checkField(L, 1, "_value", &obj)) {
			ASSERT(false);
			return 0;
		}

		Reflection::FunctionBase* f = LuaWrapper::toType<Reflection::FunctionBase*>(L, lua_upvalueindex(1));
		
		LuaWrapper::DebugGuard guard(L, f->getReturnType() == Reflection::Variant::VOID ? 0 : 1);

		Reflection::Variant args[32];
		ASSERT(f->getArgCount() <= lengthOf(args));
		for (u32 i = 0; i < f->getArgCount(); ++i) {
			Reflection::Variant::Type type = f->getArgType(i);
			toVariant(type, L, i + 2, Ref(args[i]));
		}

		const Reflection::Variant res = f->invoke(obj, Span(args, f->getArgCount()));
		return push(L, res, f->getReturnTypeName());
	}

	static int luaSceneMethodClosure(lua_State* L) {
		LuaWrapper::checkTableArg(L, 1); // self
		IScene* scene;
		if (!LuaWrapper::checkField(L, 1, "_scene", &scene)) {
			ASSERT(false);
			return 0;
		}

		Reflection::FunctionBase* f = LuaWrapper::toType<Reflection::FunctionBase*>(L, lua_upvalueindex(1));
		Reflection::Variant args[32];
		ASSERT(f->getArgCount() <= lengthOf(args));
		for (u32 i = 0; i < f->getArgCount(); ++i) {
			Reflection::Variant::Type type = f->getArgType(i);
			toVariant(type, L, i + 2, Ref(args[i]));
		}
		const Reflection::Variant res = f->invoke(scene, Span(args, f->getArgCount()));
		if (res.type == Reflection::Variant::ENTITY) {
			LuaWrapper::pushEntity(L, res.e, &scene->getUniverse());
			return 1;
		}
		return push(L, res, f->getReturnTypeName());
	}

	static int luaCmpMethodClosure(lua_State* L) {
		LuaWrapper::checkTableArg(L, 1); // self
		if (LuaWrapper::getField(L, 1, "_scene") != LUA_TLIGHTUSERDATA) {
			ASSERT(false);
			lua_pop(L, 1);
			return 0;
		}
		IScene* scene = LuaWrapper::toType<IScene*>(L, -1);
		lua_pop(L, 1);
			
		if (LuaWrapper::getField(L, 1, "_entity") != LUA_TNUMBER) {
			ASSERT(false);
			lua_pop(L, 1);
			return 0;
		}
		EntityRef entity = {LuaWrapper::toType<int>(L, -1)};
		lua_pop(L, 1);

		Reflection::FunctionBase* f = LuaWrapper::toType<Reflection::FunctionBase*>(L, lua_upvalueindex(1));
		Reflection::Variant args[32];
		ASSERT(f->getArgCount() < lengthOf(args));
		args[0] = entity;
		for (u32 i = 1; i < f->getArgCount(); ++i) {
			Reflection::Variant::Type type = f->getArgType(i);
			toVariant(type, L, i + 1, Ref(args[i]));
		}
		const Reflection::Variant res = f->invoke(scene, Span(args, f->getArgCount()));
		if (res.type == Reflection::Variant::ENTITY) {
			LuaWrapper::pushEntity(L, res.e, &scene->getUniverse());
			return 1;
		}
		return push(L, res, f->getReturnTypeName());
	}

	static void createClasses(lua_State* L) {
		LuaWrapper::DebugGuard guard(L);
		lua_getglobal(L, "LumixAPI");
		for (auto* f : Reflection::allFunctions()) {
			const char* obj_type_name = f->getThisTypeName();
			const char* c = obj_type_name + strlen(obj_type_name);
			while (*c != ':' && c != obj_type_name) --c;
			if (*c == ':') ++c;
			obj_type_name = c;
			if (LuaWrapper::getField(L, -1, obj_type_name) != LUA_TTABLE) { // [LumixAPI, obj|nil ]
				lua_pop(L, 1);						// [LumixAPI]
				lua_newtable(L);					// [LumixAPI, obj]
				lua_pushvalue(L, -1);				// [LumixAPI, obj, obj]
				lua_setfield(L, -3, obj_type_name); // [LumixAPI, obj]
				lua_pushvalue(L, -1); // [LumixAPI, obj, obj]
				lua_setfield(L, -2, "__index"); // [LumixAPI, obj]
			}
			lua_pushlightuserdata(L, f);				// [LumixAPI, obj, f]
			lua_pushcclosure(L, luaMethodClosure, 1); // [LumixAPI, obj, closure]

			if (f->name) {
				lua_setfield(L, -2, f->name);
			} else {
				const char* fn_name = f->decl_code + strlen(f->decl_code);
				while (*fn_name != ':' && fn_name != f->decl_code) --fn_name;
				if (*fn_name == ':') ++fn_name;
				lua_setfield(L, -2, fn_name);
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}

	static const ComponentType LUA_SCRIPT_TYPE = Reflection::getComponentType("lua_script");


	enum class LuaSceneVersion : i32
	{
		LATEST
	};

	template <typename T> static T fromString(const char* val) {
		T res;
		fromCString(Span(val, stringLength(val) + 1), Ref(res));
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

	template <typename T> static void toString(T val, Ref<String> out) {
		char tmp[128];
		toCString(val, Span(tmp));
		out = tmp;
	}

	template <> void toString(float val, Ref<String> out) {
		char tmp[128];
		toCString(val, Span(tmp), 10);
		out = tmp;
	}

	template <> void toString(Vec3 val, Ref<String> out) {
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


	struct LuaScriptSystemImpl final : IPlugin
	{
		explicit LuaScriptSystemImpl(Engine& engine);
		virtual ~LuaScriptSystemImpl();

		void init() override;
		void createScenes(Universe& universe) override;
		const char* getName() const override { return "lua_script"; }
		LuaScriptManager& getScriptManager() { return m_script_manager; }
		u32 getVersion() const override { return 0; }
		void serialize(OutputMemoryStream& stream) const override {}
		bool deserialize(u32 version, InputMemoryStream& stream) override { return version == 0; }

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

		struct ScriptComponent;

		struct ScriptInstance
		{
			enum Flags : u32 {
				ENABLED = 1 << 0,
				LOADED = 1 << 1,
				MOVED_FROM = 1 << 2
			};

			explicit ScriptInstance(ScriptComponent& cmp, IAllocator& allocator)
				: m_properties(allocator)
				, m_environment(-1)
				, m_thread_ref(-1)
				, m_cmp(&cmp)
			{
				LuaScriptSceneImpl& scene = cmp.m_scene;
				Engine& engine = scene.m_system.m_engine;
				lua_State* L = engine.getState();
				m_state = lua_newthread(L);
				m_thread_ref = luaL_ref(L, LUA_REGISTRYINDEX); // []
				lua_newtable(m_state); // [env]
				// reference environment
				lua_pushvalue(m_state, -1); // [env, env]
				m_environment = luaL_ref(m_state, LUA_REGISTRYINDEX); // [env]

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
				LuaWrapper::push(m_state, &scene.m_universe); // [env, Entity.new, Lumix.Entity, universe]
				LuaWrapper::push(m_state, cmp.m_entity.index); // [env, Entity.new, Lumix.Entity, universe, entity_index]
				const bool error = !LuaWrapper::pcall(m_state, 3, 1); // [env, entity]
				ASSERT(!error);
				lua_setfield(m_state, -2, "this"); // [env]
				lua_pop(m_state, 1); // []

				m_flags.set(ENABLED);
			}

			ScriptInstance(const ScriptInstance&) = delete;

			ScriptInstance(ScriptInstance&& rhs) 
				: m_properties(rhs.m_properties.move())
				, m_environment(rhs.m_environment)
				, m_thread_ref(rhs.m_thread_ref)
				, m_cmp(rhs.m_cmp)
				, m_script(rhs.m_script)
				, m_state(rhs.m_state)
				, m_flags(rhs.m_flags)
			{
				rhs.m_script = nullptr;
				rhs.m_flags.set(MOVED_FROM);
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
				rhs.m_flags.set(MOVED_FROM);
			}

			~ScriptInstance() {
				if (!m_flags.isSet(MOVED_FROM)) {
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

					m_cmp->m_scene.disableScript(*this);

					luaL_unref(m_state, LUA_REGISTRYINDEX, m_thread_ref);
					luaL_unref(m_state, LUA_REGISTRYINDEX, m_environment);
				}
			}

			void onScriptLoaded(LuaScriptSceneImpl& scene, ScriptComponent& cmp, int scr_index);

			ScriptComponent* m_cmp;
			LuaScript* m_script = nullptr;
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
					logError("Too many properties in ", inst.m_script->getPath(), ", entity ", m_entity.index
						, ". Some will be ignored.");
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
							u32 hash = crc32(name);
							if (m_scene.m_property_names.find(hash) < 0)
							{
								m_scene.m_property_names.emplace(hash, name, allocator);
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
										logError("Too many properties in ", inst.m_script->getPath(), ", entity ", m_entity.index
											, ". Some will be ignored.");
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
				lua_pop(L, 1);
			}


			void onScriptLoaded(Resource::State, Resource::State, Resource& resource)
			{
				for (int scr_index = 0, c = m_scripts.size(); scr_index < c; ++scr_index)
				{
					ScriptInstance& script = m_scripts[scr_index];
					
					if (!script.m_script) continue;
					if (script.m_script != &resource) continue;
					if (!script.m_script->isReady()) continue;
					
					script.onScriptLoaded(m_scene, *this, scr_index);
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

			void add(EntityPtr parameter) override {
				LuaWrapper::pushEntity(state, parameter, &cmp->m_scene.getUniverse());
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

			LuaWrapper::pcall(script.m_state, m_function_call.parameter_count, 0);
			lua_pop(script.m_state, 1);
		}


		int getPropertyCount(EntityRef entity, int scr_index) override
		{
			return m_scripts[entity]->m_scripts[scr_index].m_properties.size();
		}


		const char* getPropertyName(EntityRef entity, int scr_index, int prop_index) override
		{
			return getPropertyName(m_scripts[entity]->m_scripts[scr_index].m_properties[prop_index].name_hash);
		}


		ResourceType getPropertyResourceType(EntityRef entity, int scr_index, int prop_index) override
		{
			return m_scripts[entity]->m_scripts[scr_index].m_properties[prop_index].resource_type;
		}


		Property::Type getPropertyType(EntityRef entity, int scr_index, int prop_index) override
		{
			return m_scripts[entity]->m_scripts[scr_index].m_properties[prop_index].type;
		}


		void clear() override
		{
			Path invalid_path;
			for (auto* script_cmp : m_scripts) {
				ASSERT(script_cmp);
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
			if (scene->m_property_names.find(prop_name_hash) < 0)
			{
				scene->m_property_names.emplace(prop_name_hash, prop_name, scene->m_system.m_allocator);
			}
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
			LuaWrapper::createSystemVariable(L, "Editor", "COLOR_PROPERTY", Property::COLOR);
		}

		static int rescan(lua_State* L) {
			const auto* universe = LuaWrapper::checkArg<Universe*>(L, 1);
			const EntityRef entity = LuaWrapper::checkArg<EntityRef>(L, 2);
			const int scr_index = LuaWrapper::checkArg<int>(L, 3);

			if (!universe->hasComponent(entity, LUA_SCRIPT_TYPE)) {
				return 0;
			}
			
			LuaScriptSceneImpl* scene = (LuaScriptSceneImpl*)universe->getScene(LUA_SCRIPT_TYPE);

			const int count = scene->getScriptCount(entity);
			if (scr_index >= count) {
				return 0;
			}

			/////
			const ScriptInstance& instance = scene->m_scripts[entity]->m_scripts[scr_index];
			LuaWrapper::DebugGuard guard(instance.m_state);
			lua_rawgeti(instance.m_state, LUA_REGISTRYINDEX, instance.m_environment);
			if (lua_type(instance.m_state, -1) != LUA_TTABLE) {
				ASSERT(false);
				lua_pop(instance.m_state, 1);
				return 0;
			}
			lua_getfield(instance.m_state, -1, "update");
			if (lua_type(instance.m_state, -1) == LUA_TFUNCTION) {
				auto& update_data = scene->m_updates.emplace();
				update_data.script = instance.m_script;
				update_data.state = instance.m_state;
				update_data.environment = instance.m_environment;
			}
			lua_pop(instance.m_state, 1);
			lua_getfield(instance.m_state, -1, "onInputEvent");
			if (lua_type(instance.m_state, -1) == LUA_TFUNCTION) {
				auto& callback = scene->m_input_handlers.emplace();
				callback.script = instance.m_script;
				callback.state = instance.m_state;
				callback.environment = instance.m_environment;
			}
			lua_pop(instance.m_state, 1);
			lua_pop(instance.m_state, 1);

			return 0;
		}

		static int getEnvironment(lua_State* L)
		{
			if (!lua_istable(L, 1)) {
				LuaWrapper::argError(L, 1, "entity");
			}

			if (LuaWrapper::getField(L, 1, "_entity") != LUA_TNUMBER) {
				lua_pop(L, 1);
				LuaWrapper::argError(L, 1, "entity");
			}
			const EntityRef entity = {LuaWrapper::toType<i32>(L, -1)};
			lua_pop(L, 1);

			if (LuaWrapper::getField(L, 1, "_universe") != LUA_TLIGHTUSERDATA) {
				lua_pop(L, 1);
				LuaWrapper::argError(L, 1, "entity");
			}
			Universe* universe = LuaWrapper::toType<Universe*>(L, -1);
			lua_pop(L, 1);

			const i32 scr_index = LuaWrapper::checkArg<i32>(L, 2);

			if (!universe->hasComponent(entity, LUA_SCRIPT_TYPE))
			{
				lua_pushnil(L);
				return 1;
			}
			
			LuaScriptScene* scene = (LuaScriptScene*)universe->getScene(LUA_SCRIPT_TYPE);

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
		
		static void convertPropertyToLuaName(const char* src, Span<char> out) {
			const u32 max_size = out.length();
			ASSERT(max_size > 0);
			char* dest = out.begin();
			while (*src && dest - out.begin() < max_size - 1) {
				if (isLetter(*src)) {
					*dest = isUpperCase(*src) ? *src - 'A' + 'a' : *src;
					++dest;
				}
				else if (isNumeric(*src)) {
					*dest = *src;
					++dest;
				}
				else {
					*dest = '_';
					++dest;
				}
				++src;
			}
			*dest = 0;
		}

		struct LuaPropGetterVisitor  : Reflection::IPropertyVisitor
		{
			static bool isSameProperty(const char* name, const char* lua_name) {
				char tmp[50];
				convertPropertyToLuaName(name, Span(tmp));
				return equalStrings(tmp, lua_name);
			}

			template <typename T>
			void get(const Reflection::Property<T>& prop)
			{
				if (!isSameProperty(prop.name, prop_name)) return;
				
				const T val = prop.get(cmp, idx);
				found = true;
				LuaWrapper::push(L, val);
			}

			void visit(const Reflection::Property<float>& prop) override { get(prop); }
			void visit(const Reflection::Property<int>& prop) override { get(prop); }
			void visit(const Reflection::Property<u32>& prop) override { get(prop); }
			void visit(const Reflection::Property<Vec2>& prop) override { get(prop); }
			void visit(const Reflection::Property<Vec3>& prop) override { get(prop); }
			void visit(const Reflection::Property<IVec3>& prop) override { get(prop); }
			void visit(const Reflection::Property<Vec4>& prop) override { get(prop); }
			void visit(const Reflection::Property<bool>& prop) override { get(prop); }

			void visit(const Reflection::Property<EntityPtr>& prop) override { 
				if (!isSameProperty(prop.name, prop_name)) return;
				
				const EntityPtr val = prop.get(cmp, idx);
				found = true;
				LuaWrapper::pushEntity(L, val, &cmp.scene->getUniverse());
			}

			void visit(const Reflection::Property<Path>& prop) override { 
				if (!isSameProperty(prop.name, prop_name)) return;
				
				const Path p = prop.get(cmp, idx);
				found = true;
				LuaWrapper::push(L, p.c_str());
			}

			void visit(const Reflection::Property<const char*>& prop) override { 
				if (!isSameProperty(prop.name, prop_name)) return;
				
				const char* tmp = prop.get(cmp, idx);
				found = true;
				LuaWrapper::push(L, tmp);
			}

			void visit(const Reflection::IArrayProperty& prop) override {}
			void visit(const Reflection::IBlobProperty& prop) override {}

			ComponentUID cmp;
			const char* prop_name;
			int idx;
			bool found = false;
			lua_State* L;
		};
		
		struct LuaPropSetterVisitor : Reflection::IPropertyVisitor
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
			void set(const Reflection::Property<T>& prop)
			{
				if (!isSameProperty(prop.name, prop_name)) return;
				
				const T val = LuaWrapper::toType<T>(L, 3);
				prop.set(cmp, idx, val);
			}

			void visit(const Reflection::Property<float>& prop) override { set(prop); }
			void visit(const Reflection::Property<int>& prop) override { set(prop); }
			void visit(const Reflection::Property<u32>& prop) override { set(prop); }
			void visit(const Reflection::Property<EntityPtr>& prop) override { set(prop); }
			void visit(const Reflection::Property<Vec2>& prop) override { set(prop); }
			void visit(const Reflection::Property<Vec3>& prop) override { set(prop); }
			void visit(const Reflection::Property<IVec3>& prop) override { set(prop); }
			void visit(const Reflection::Property<Vec4>& prop) override { set(prop); }
			void visit(const Reflection::Property<bool>& prop) override { set(prop); }

			void visit(const Reflection::Property<Path>& prop) override {
				if (!isSameProperty(prop.name, prop_name)) return;
				
				const char* val = LuaWrapper::toType<const char*>(L, 3);
				prop.set(cmp, idx, Path(val));
			}

			void visit(const Reflection::Property<const char*>& prop) override { 
				if (!isSameProperty(prop.name, prop_name)) return;
				
				const char* val = LuaWrapper::toType<const char*>(L, 3);
				prop.set(cmp, idx, val);
			}

			void visit(const Reflection::IArrayProperty& prop) override {}
			void visit(const Reflection::IBlobProperty& prop) override {}

			ComponentUID cmp;
			const char* prop_name;
			int idx;
			lua_State* L;
			bool found = false;
		};

		static int lua_new_cmp(lua_State* L) {
			LuaWrapper::DebugGuard guard(L, 1);
			LuaWrapper::checkTableArg(L, 1); // self
			const Universe* universe = LuaWrapper::checkArg<Universe*>(L, 2);
			const EntityRef e = {LuaWrapper::checkArg<i32>(L, 3)};
			
			LuaWrapper::getField(L, 1, "cmp_type");
			const int cmp_type = LuaWrapper::toType<int>(L, -1);
			lua_pop(L, 1);
			IScene* scene = universe->getScene(ComponentType{cmp_type});

			lua_newtable(L);
			LuaWrapper::setField(L, -1, "_entity", e);
			LuaWrapper::setField(L, -1, "_scene", scene);
			lua_pushvalue(L, 1);
			lua_setmetatable(L, -2);
			return 1;
		}

		static int lua_prop_getter(lua_State* L) {
			LuaWrapper::checkTableArg(L, 1); // self

			lua_getfield(L, 1, "_scene");
			LuaScriptSceneImpl* scene = LuaWrapper::toType<LuaScriptSceneImpl*>(L, -1);
			lua_getfield(L, 1, "_entity");
			const EntityRef entity = {LuaWrapper::toType<i32>(L, -1)};
			lua_pop(L, 2);

			if (lua_isnumber(L, 2)) {
				const i32 scr_index = LuaWrapper::toType<i32>(L, 2);
				int env = scene->getEnvironment(entity, scr_index);
				if (env < 0) {
					lua_pushnil(L);
				}
				else {
					lua_rawgeti(L, LUA_REGISTRYINDEX, env);
					ASSERT(lua_type(L, -1) == LUA_TTABLE);
				}
				return 1;
			}

			LuaPropGetterVisitor v;
			v.prop_name = LuaWrapper::checkArg<const char*>(L, 2);
			v.L = L;
			v.idx = -1;
			v.cmp.type = LuaWrapper::toType<ComponentType>(L, lua_upvalueindex(1));
			const Reflection::ComponentBase* cmp = Reflection::getComponent(v.cmp.type);

			v.cmp.scene = scene;
			v.cmp.entity = entity;

			cmp->visit(v);
			if (v.found) return 1;

			// TODO put this directly in table, so we don't have to look it up here every time
			const auto& functions = cmp->getFunctions();
			for (auto* f : functions) {
				if (equalStrings(v.prop_name, f->name)) {
					lua_pushlightuserdata(L, (void*)f);
					lua_pushcclosure(L, luaCmpMethodClosure, 1);
					return 1;
				}
			}

			return 0;
		}

		static int lua_prop_setter(lua_State* L) {
			LuaWrapper::checkTableArg(L, 1); // self

			LuaPropSetterVisitor v;
			v.prop_name = LuaWrapper::checkArg<const char*>(L, 2);
			v.L = L;
			v.idx = -1;
			v.cmp.type = LuaWrapper::toType<ComponentType>(L, lua_upvalueindex(1));
			const Reflection::ComponentBase* cmp = Reflection::getComponent(v.cmp.type);

			lua_getfield(L, 1, "_scene");
			v.cmp.scene = LuaWrapper::toType<IScene*>(L, -1);
			lua_getfield(L, 1, "_entity");
			v.cmp.entity.index = LuaWrapper::toType<i32>(L, -1);
			lua_pop(L, 2);

			cmp->visit(v);

			if (!v.found) {
				luaL_error(L, "Property `%s` does not exist", v.prop_name);
			}

			return 0;
		}

		static int lua_new_scene(lua_State* L) {
			LuaWrapper::DebugGuard guard(L, 1);
			LuaWrapper::checkTableArg(L, 1); // self
			IScene* scene = LuaWrapper::checkArg<IScene*>(L, 2);
			
			lua_newtable(L);
			LuaWrapper::setField(L, -1, "_scene", scene);
			lua_pushvalue(L, 1);
			lua_setmetatable(L, -2);
			return 1;
		}

		void registerProperties()
		{
			lua_State* L = m_system.m_engine.getState();
			LuaWrapper::DebugGuard guard(L);

			Reflection::SceneBase* scene = Reflection::getFirstScene();
			while (scene) {
				lua_newtable(L); // [ scene ]
				lua_getglobal(L, "Lumix"); // [ scene, Lumix ]
				lua_pushvalue(L, -2); // [ scene, Lumix, scene]
				lua_setfield(L, -2, scene->name); // [ scene, Lumix ]
				lua_pop(L, 1); // [ scene ]

				lua_pushvalue(L, -1); // [ scene, scene ]
				lua_setfield(L, -2, "__index"); // [ scene ]

				lua_pushcfunction(L, lua_new_scene); // [ scene, fn_new_scene ]
				lua_setfield(L, -2, "new"); // [ scene ]

				for (const Reflection::FunctionBase* f :  scene->getFunctions()) {
					const char* c = f->decl_code;
					while (*c != ':') ++c;
					c += 2;
					lua_pushlightuserdata(L, (void*)f); // [scene, f]
					lua_pushcclosure(L, luaSceneMethodClosure, 1); // [scene, fn]
					lua_setfield(L, -2, c); // [scene]
				}
				lua_pop(L, 1); // []

				scene = scene->next;
			}

			for (const Reflection::RegisteredComponent& cmp : Reflection::getComponents()) {
				const char* cmp_name = cmp.cmp->name;
				const ComponentType cmp_type = cmp.cmp->component_type;

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


		void registerAPI()
		{
			if (m_is_api_registered) return;

			m_is_api_registered = true;

			lua_State* engine_state = m_system.m_engine.getState();
			
			registerProperties();
			registerPropertyAPI();
			LuaWrapper::createSystemFunction(engine_state, "LuaScript", "getEnvironment", &LuaScriptSceneImpl::getEnvironment);
			LuaWrapper::createSystemFunction(engine_state, "LuaScript", "rescan", &LuaScriptSceneImpl::rescan);
			
			#define REGISTER_FUNCTION(F) \
				do { \
					auto f = &LuaWrapper::wrapMethod<&LuaScriptSceneImpl::F>; \
					LuaWrapper::createSystemFunction(engine_state, "LuaScript", #F, f); \
				} while(false)

			REGISTER_FUNCTION(cancelTimer);

			#undef REGISTER_FUNCTION

			LuaWrapper::createSystemFunction(engine_state, "LuaScript", "setTimer", &LuaScriptSceneImpl::setTimer);
		}


		int getEnvironment(EntityRef entity, int scr_index) override
		{
			return m_scripts[entity]->m_scripts[scr_index].m_environment;
		}


		const char* getPropertyName(u32 name_hash) const
		{
			int idx = m_property_names.find(name_hash);
			if(idx >= 0) return m_property_names.at(idx).c_str();
			return nullptr;
		}

		void applyEntityProperty(ScriptInstance& script, const char* name, Property& prop, const char* value)
		{
			LuaWrapper::DebugGuard guard(script.m_state);
			lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment); // [env]
			ASSERT(lua_type(script.m_state, -1));
			const EntityPtr e = fromString<EntityPtr>(value);

			if (!e.isValid()) {
				lua_newtable(script.m_state); // [env, {}]
				lua_setfield(script.m_state, -2, name); // [env]
				lua_pop(script.m_state, 1);
				return;
			}

			lua_getglobal(script.m_state, "Lumix"); // [env, Lumix]
			lua_getfield(script.m_state, -1, "Entity"); // [env, Lumix, Lumix.Entity]
			lua_remove(script.m_state, -2); // [env, Lumix.Entity]
			lua_getfield(script.m_state, -1, "new"); // [env, Lumix.Entity, Entity.new]
			lua_pushvalue(script.m_state, -2); // [env, Lumix.Entity, Entity.new, Lumix.Entity]
			lua_remove(script.m_state, -3); // [env, Entity.new, Lumix.Entity]
			LuaWrapper::push(script.m_state, &m_universe); // [env, Entity.new, Lumix.Entity, universe]
			LuaWrapper::push(script.m_state, e.index); // [env, Entity.new, Lumix.Entity, universe, entity_index]
			const bool error = !LuaWrapper::pcall(script.m_state, 3, 1); // [env, entity]
			ASSERT(!error);
			lua_setfield(script.m_state, -2, name); // [env]
			lua_pop(script.m_state, 1);
		}

		void applyResourceProperty(ScriptInstance& script, const char* name, Property& prop, const char* path)
		{
			lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment);
			ASSERT(lua_type(script.m_state, -1));
			lua_getfield(script.m_state, -1, name);
			int res_idx = LuaWrapper::toType<int>(script.m_state, -1);
			m_system.m_engine.unloadLuaResource(res_idx);
			lua_pop(script.m_state, 1);

			int new_res = path[0] ? m_system.m_engine.addLuaResource(Path(path), prop.resource_type) : -1;
			lua_pushinteger(script.m_state, new_res);
			lua_setfield(script.m_state, -2, name);
			lua_pop(script.m_state, 1);
		}

		template <typename T>
		void applyProperty(ScriptInstance& script, Property& prop, T value) {
			char tmp[64];
			toCString(value, Span(tmp));
			applyProperty(script, prop, tmp);
		}

		void applyProperty(ScriptInstance& script, Property& prop, float value) {
			char tmp[64];
			toCString(value, Span(tmp), 10);
			applyProperty(script, prop, tmp);
		}

		void applyProperty(ScriptInstance& script, Property& prop, Vec3 value) {
			const StaticString<512> tmp("{", value.x, ",", value.y, ",", value.z, "}");
			applyProperty(script, prop, tmp.data);
		}

		void applyProperty(ScriptInstance& script, Property& prop, char* value) {
			applyProperty(script, prop, (const char*)value);
		}

		void applyProperty(ScriptInstance& script, Property& prop, const char* value)
		{
			if (!value) return;

			lua_State* state = script.m_state;
			if (!state) return;

			const char* name = getPropertyName(prop.name_hash);
			if (!name) return;

			if (prop.type == Property::RESOURCE)
			{
				applyResourceProperty(script, name, prop, value);
				return;
			}

			if (prop.type != Property::STRING && prop.type != Property::RESOURCE && value[0] == '\0') return; //-V560

			if (prop.type == Property::ENTITY)
			{
				applyEntityProperty(script, name, prop, value);
				return;
			}

			StaticString<1024> tmp(name, " = ");
			if (prop.type == Property::STRING) tmp << "\"" << value << "\"";
			else tmp << value;

			bool errors = luaL_loadbuffer(state, tmp, stringLength(tmp), nullptr) != 0;
			if (errors)
			{
				logError(script.m_script->getPath(), ": ", lua_tostring(state, -1));
				lua_pop(state, 1);
				return;
			}

			lua_rawgeti(script.m_state, LUA_REGISTRYINDEX, script.m_environment);
			ASSERT(lua_type(script.m_state, -1) == LUA_TTABLE);
			lua_setfenv(script.m_state, -2);

			errors = lua_pcall(state, 0, 0, 0) != 0;

			if (errors)
			{
				logError(script.m_script->getPath(), ": ", lua_tostring(state, -1));
				lua_pop(state, 1);
			}
		}

		template <typename T>
		void setPropertyValue(EntityRef entity, int scr_index, const char* property_name, T value) {
			auto* script_cmp = m_scripts[entity];
			if (!script_cmp) return;
			Property& prop = getScriptProperty(entity, scr_index, property_name);
			if (!script_cmp->m_scripts[scr_index].m_state) {
				toString(value, Ref(prop.stored_value));
				return;
			}

			applyProperty(script_cmp->m_scripts[scr_index], prop, value);
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

			return getPropertyName(script.m_properties[index].name_hash);
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


		void setPath(ScriptComponent& cmp, ScriptInstance& inst, const Path& path)
		{
			registerAPI();

			if (inst.m_script) {
				auto& cb = inst.m_script->getObserverCb();
				cb.unbind<&ScriptComponent::onScriptLoaded>(&cmp);
				inst.m_script->decRefCount();
			}

			ResourceManagerHub& rm = m_system.m_engine.getResourceManager();
			inst.m_script = path.isValid() ? rm.load<LuaScript>(path) : nullptr;
			if (inst.m_script) inst.m_script->onLoaded<&ScriptComponent::onScriptLoaded>(&cmp);
		}


		void startScript(ScriptInstance& instance, bool is_reload)
		{
			if (!instance.m_flags.isSet(ScriptInstance::ENABLED)) return;
			if (!instance.m_state) return;

			if (is_reload) disableScript(instance);
			
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

			if (!is_reload)
			{
				lua_getfield(instance.m_state, -1, "start");
				if (lua_type(instance.m_state, -1) != LUA_TFUNCTION)
				{
					lua_pop(instance.m_state, 2);
					return;
				}

				if (lua_pcall(instance.m_state, 0, 0, 0) != 0)
				{
					logError(lua_tostring(instance.m_state, -1));
					lua_pop(instance.m_state, 1);
				}
			}
			lua_pop(instance.m_state, 1);
		}


		void onButtonClicked(EntityRef e) { onGUIEvent(e, "onButtonClicked"); }
		void onRectHovered(EntityRef e) { onGUIEvent(e, "onRectHovered"); }
		void onRectHoveredOut(EntityRef e) { onGUIEvent(e, "onRectHoveredOut"); }
		
		void onRectMouseDown(EntityRef e, float x, float y) { 
			if (!m_universe.hasComponent(e, LUA_SCRIPT_TYPE)) return;

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
				m_gui_scene->rectMouseDown().bind<&LuaScriptSceneImpl::onRectMouseDown>(this);
			}
		}


		void stopGame() override
		{
			if (m_gui_scene)
			{
				m_gui_scene->buttonClicked().unbind<&LuaScriptSceneImpl::onButtonClicked>(this);
				m_gui_scene->rectHovered().unbind<&LuaScriptSceneImpl::onRectHovered>(this);
				m_gui_scene->rectHoveredOut().unbind<&LuaScriptSceneImpl::onRectHoveredOut>(this);
				m_gui_scene->rectMouseDown().unbind<&LuaScriptSceneImpl::onRectMouseDown>(this);
			}
			m_gui_scene = nullptr;
			m_scripts_start_called = false;
			m_is_game_running = false;
			m_updates.clear();
			m_input_handlers.clear();
			m_timers.clear();
			m_animation_scene = nullptr;
		}


		void createComponent(EntityRef entity) {
			auto& allocator = m_system.m_allocator;
			ScriptComponent* script = LUMIX_NEW(allocator, ScriptComponent)(*this, entity, allocator);
			m_scripts.insert(entity, script);
			m_universe.onComponentCreated(entity, LUA_SCRIPT_TYPE, this);
		}

		void destroyComponent(EntityRef entity) {
			ScriptComponent* cmp = m_scripts[entity];
			LUMIX_DELETE(m_system.m_allocator, cmp);
			m_scripts.erase(entity);
			m_universe.onComponentDestroyed(entity, LUA_SCRIPT_TYPE, this);
		}

		template <typename T>
		T getPropertyValue(EntityRef entity, int scr_index, const char* property_name) {
			u32 hash = crc32(property_name);
			auto& inst = m_scripts[entity]->m_scripts[scr_index];
			for (auto& prop : inst.m_properties)
			{
				if (prop.name_hash == hash)
				{
					if (inst.m_script && inst.m_script->isReady()) return getProperty<T>(prop, property_name, inst);
					return fromString<T>(prop.stored_value.c_str());
				}
			}
			return {};
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


		template <typename T> T getProperty(Property& prop, const char* prop_name, ScriptInstance& scr) {
			if (!scr.m_state) return {};

			lua_rawgeti(scr.m_state, LUA_REGISTRYINDEX, scr.m_environment);
			lua_getfield(scr.m_state, -1, prop_name);	
			
			if (!LuaWrapper::isType<T>(scr.m_state, -1)) {
				lua_pop(scr.m_state, 2);
				return T();
			}
			const T res = LuaWrapper::toType<T>(scr.m_state, -1);
			lua_pop(scr.m_state, 2);
			return res;
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
			const int type = lua_type(scr.m_state, -1);
			if (type == LUA_TNIL)
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
				case Property::COLOR:
				{
					const Vec3 val = LuaWrapper::toType<Vec3>(scr.m_state, -1);
					const StaticString<512> tmp("{", val.x, ",", val.y, ",", val.z, "}");
					copyString(out, tmp.data);
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
					EntityPtr e = INVALID_ENTITY;
					if (type != LUA_TTABLE) {
						e = INVALID_ENTITY;
					}
					else {
						if (LuaWrapper::getField(scr.m_state, -1, "_entity") == LUA_TNUMBER) {
							e = { (i32)lua_tointeger(scr.m_state, -1) };
						}
						lua_pop(scr.m_state, 1);
					}
					toCString(e.index, out);
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
						serializer.write(prop.type);
						int idx = m_property_names.find(prop.name_hash);
						if (idx >= 0)
						{
							const char* name = m_property_names.at(idx).c_str();
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


		void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override
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
					auto& scr = script->m_scripts.emplace(*script, allocator);

					const char* tmp = serializer.readString();
					serializer.read(scr.m_flags);
					int prop_count;
					serializer.read(prop_count);
					scr.m_properties.reserve(prop_count);
					for (int j = 0; j < prop_count; ++j)
					{
						Property& prop = scr.m_properties.emplace(allocator);
						prop.type = Property::ANY;
						serializer.read(prop.name_hash);
						Property::Type type;
						serializer.read(type);
						const char* tmp = serializer.readString();
						if (type == Property::ENTITY) {
							EntityPtr entity;
							fromCString(Span(tmp, stringLength(tmp)), Ref(entity.index));
							entity = entity_map.get(entity);
							StaticString<64> buf(entity.index);
							prop.stored_value = buf;
						}
						else {
							prop.stored_value = tmp;
						}
					}
					setPath(*script, scr, Path(tmp));
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
				logError(lua_tostring(L, -1));
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
						logError("Skipping lua_call animation event because it is too big.");
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


		bool isScriptEnabled(EntityRef entity, int scr_index) override
		{
			return m_scripts[entity]->m_scripts[scr_index].m_flags.isSet(ScriptInstance::ENABLED);
		}


		void removeScript(EntityRef entity, int scr_index) override {
			m_scripts[entity]->m_scripts.swapAndPop(scr_index);
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
				const char* property_name = getPropertyName(prop.name_hash);
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
			const char* path = blob.readString();
			blob.read(scr.m_flags);
			blob.read(count);
			scr.m_environment = -1;
			scr.m_properties.clear();
			for (int i = 0; i < count; ++i)
			{
				auto& prop = scr.m_properties.emplace(m_system.m_allocator);
				prop.type = Property::ANY;
				blob.read(prop.name_hash);
				const char* buf = blob.readString();
				prop.stored_value = buf;
			}
			setScriptPath(entity, scr_index, Path(path));
		}


		LuaScriptSystemImpl& m_system;
		HashMap<EntityRef, ScriptComponent*> m_scripts;
		AssociativeArray<u32, String> m_property_names;
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

	void LuaScriptSceneImpl::ScriptInstance::onScriptLoaded(LuaScriptSceneImpl& scene, struct ScriptComponent& cmp, int scr_index) {
		LuaWrapper::DebugGuard guard(m_state);
		
		bool is_reload = m_flags.isSet(LOADED);
		
		lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_environment); // [env]
		ASSERT(lua_type(m_state, -1) == LUA_TTABLE);

		bool errors = luaL_loadbuffer(m_state,
			m_script->getSourceCode(),
			stringLength(m_script->getSourceCode()),
			m_script->getPath().c_str()) != 0; // [env, func]

		if (errors) {
			logError(m_script->getPath(), ": ", lua_tostring(m_state, -1));
			lua_pop(m_state, 2);
			return;
		}

		lua_pushvalue(m_state, -2); // [env, func, env]
		lua_setfenv(m_state, -2);

		scene.m_current_script_instance = this;
		errors = lua_pcall(m_state, 0, 0, 0) != 0; // [env]
		if (errors)	{
			logError(m_script->getPath(), ": ", lua_tostring(m_state, -1));
			lua_pop(m_state, 1);
		}
		lua_pop(m_state, 1); // []

		cmp.detectProperties(*this);
					
		bool enabled = m_flags.isSet(ScriptInstance::ENABLED);
		scene.setEnableProperty(cmp.m_entity, scr_index, *this, enabled);
		m_flags.set(LOADED);

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

		if (scene.m_is_game_running) scene.startScript(*this, is_reload);
	}


	struct LuaProperties : Reflection::IDynamicProperties {
		LuaProperties() { name = "lua_properties"; }
		
		u32 getCount(ComponentUID cmp, int index) const override { 
			LuaScriptSceneImpl& scene = (LuaScriptSceneImpl&)*cmp.scene;
			const EntityRef e = (EntityRef)cmp.entity;
			return scene.getPropertyCount(e, index);
		}

		Type getType(ComponentUID cmp, int array_idx, u32 idx) const override { 
			LuaScriptSceneImpl& scene = (LuaScriptSceneImpl&)*cmp.scene;
			const EntityRef e = (EntityRef)cmp.entity;
			const LuaScriptScene::Property::Type type = scene.getPropertyType(e, array_idx, idx);
			switch(type) {
				case LuaScriptScene::Property::Type::BOOLEAN: return BOOLEAN;
				case LuaScriptScene::Property::Type::INT: return I32;
				case LuaScriptScene::Property::Type::FLOAT: return FLOAT;
				case LuaScriptScene::Property::Type::STRING: return STRING;
				case LuaScriptScene::Property::Type::ENTITY: return ENTITY;
				case LuaScriptScene::Property::Type::RESOURCE: return RESOURCE;
				case LuaScriptScene::Property::Type::COLOR: return COLOR;
				default: ASSERT(false); return NONE;
			}
		}

		const char* getName(ComponentUID cmp, int array_idx, u32 idx) const override {
			LuaScriptSceneImpl& scene = (LuaScriptSceneImpl&)*cmp.scene;
			const EntityRef e = (EntityRef)cmp.entity;
			return scene.getPropertyName(e, array_idx, idx);
		}

		Reflection::ResourceAttribute getResourceAttribute(ComponentUID cmp, int array_idx, u32 idx) const override {
			Reflection::ResourceAttribute attr;
			LuaScriptSceneImpl& scene = (LuaScriptSceneImpl&)*cmp.scene;
			const EntityRef e = (EntityRef)cmp.entity;
			const LuaScriptScene::Property::Type type = scene.getPropertyType(e, array_idx, idx);
			ASSERT(type == LuaScriptScene::Property::Type::RESOURCE);
			attr.resource_type  = scene.getPropertyResourceType(e, array_idx, idx);
			return attr;
		}


		Value getValue(ComponentUID cmp, int array_idx, u32 idx) const override { 
			LuaScriptSceneImpl& scene = (LuaScriptSceneImpl&)*cmp.scene;
			const EntityRef e = (EntityRef)cmp.entity;
			const LuaScriptScene::Property::Type type = scene.getPropertyType(e, array_idx, idx);
			const char* name = scene.getPropertyName(e, array_idx, idx);
			Value v = {};
			switch(type) {
				case LuaScriptScene::Property::Type::COLOR: Reflection::set(v, scene.getPropertyValue<Vec3>(e, array_idx, name)); break;
				case LuaScriptScene::Property::Type::BOOLEAN: Reflection::set(v, scene.getPropertyValue<bool>(e, array_idx, name)); break;
				case LuaScriptScene::Property::Type::INT: Reflection::set(v, scene.getPropertyValue<i32>(e, array_idx, name)); break;
				case LuaScriptScene::Property::Type::FLOAT: Reflection::set(v, scene.getPropertyValue<float>(e, array_idx, name)); break;
				case LuaScriptScene::Property::Type::STRING: Reflection::set(v, scene.getPropertyValue<const char*>(e, array_idx, name)); break;
				case LuaScriptScene::Property::Type::ENTITY: Reflection::set(v, scene.getPropertyValue<EntityPtr>(e, array_idx, name)); break;
				case LuaScriptScene::Property::Type::RESOURCE: {
					const i32 res_idx = scene.getPropertyValue<i32>(e, array_idx, name);
					if (res_idx < 0) {
						Reflection::set(v, ""); 
					}
					else {
						Resource* res = scene.m_system.m_engine.getLuaResource(res_idx);
						Reflection::set(v, res ? res->getPath().c_str() : ""); 
					}
					break;
				}
				default: ASSERT(false); break;
			}
			return v;
		}
		
		void set(ComponentUID cmp, int array_idx, const char* name, Type type, Value v) const override { 
			LuaScriptSceneImpl& scene = (LuaScriptSceneImpl&)*cmp.scene;
			const EntityRef e = (EntityRef)cmp.entity;
			switch(type) {
				case BOOLEAN: scene.setPropertyValue(e, array_idx, name, v.b); break;
				case I32: scene.setPropertyValue(e, array_idx, name, v.i); break;
				case FLOAT: scene.setPropertyValue(e, array_idx, name, v.f); break;
				case STRING: scene.setPropertyValue(e, array_idx, name, v.s); break;
				case ENTITY: scene.setPropertyValue(e, array_idx, name, v.e); break;
				case RESOURCE: scene.setPropertyValue(e, array_idx, name, v.s); break;
				case COLOR: scene.setPropertyValue(e, array_idx, name, v.v3); break;
				default: ASSERT(false); break;
			}
		}

		void set(ComponentUID cmp, int array_idx, u32 idx, Value v) const override {
			LuaScriptSceneImpl& scene = (LuaScriptSceneImpl&)*cmp.scene;
			const EntityRef e = (EntityRef)cmp.entity;
			const LuaScriptScene::Property::Type type = scene.getPropertyType(e, array_idx, idx);
			const char* name = scene.getPropertyName(e, array_idx, idx);
			switch(type) {
				case LuaScriptScene::Property::Type::BOOLEAN: scene.setPropertyValue(e, array_idx, name, v.b); break;
				case LuaScriptScene::Property::Type::INT: scene.setPropertyValue(e, array_idx, name, v.i); break;
				case LuaScriptScene::Property::Type::FLOAT: scene.setPropertyValue(e, array_idx, name, v.f); break;
				case LuaScriptScene::Property::Type::STRING: scene.setPropertyValue(e, array_idx, name, v.s); break;
				case LuaScriptScene::Property::Type::ENTITY: scene.setPropertyValue(e, array_idx, name, v.e); break;
				case LuaScriptScene::Property::Type::RESOURCE: scene.setPropertyValue(e, array_idx, name, v.s); break;
				case LuaScriptScene::Property::Type::COLOR: scene.setPropertyValue(e, array_idx, name, v.v3); break;
				default: ASSERT(false); break;
			}
		}
	};


	LuaScriptSystemImpl::LuaScriptSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_script_manager(m_allocator)
	{
		m_script_manager.create(LuaScript::TYPE, engine.getResourceManager());

		LUMIX_SCENE(LuaScriptSceneImpl, "lua_script",
			LUMIX_CMP(Component, "lua_script", "Lua script", 
				array("scripts", &LuaScriptScene::getScriptCount, &LuaScriptScene::addScript, &LuaScriptScene::removeScript, 
					property("Enabled", &LuaScriptScene::isScriptEnabled, &LuaScriptScene::enableScript),
					LUMIX_PROP(ScriptPath, "Path", ResourceAttribute(LuaScript::TYPE)),
					LuaProperties()
				)
			)
		);
	}

	void LuaScriptSystemImpl::init() {
		createClasses(m_engine.getState());
	}

	LuaScriptSystemImpl::~LuaScriptSystemImpl()
	{
		m_script_manager.destroy();
	}

	void LuaScriptSystemImpl::createScenes(Universe& ctx)
	{
		UniquePtr<LuaScriptSceneImpl> scene = UniquePtr<LuaScriptSceneImpl>::create(m_allocator, *this, ctx);
		ctx.addScene(scene.move());
	}


	LUMIX_PLUGIN_ENTRY(lua_script)
	{
		return LUMIX_NEW(engine.getAllocator(), LuaScriptSystemImpl)(engine);
	}
} // namespace Lumix
