#include "lua_script_system.h"
#include "animation/animation_module.h"
#include "engine/array.h"
#include "engine/associative_array.h"
#include "engine/hash.h"
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
#include "engine/world.h"
#include "gui/gui_module.h"
#include "lua_script/lua_script.h"


namespace Lumix
{
	static void pushObject(lua_State* L, void* obj, Span<const char> type_name) {
		ASSERT(type_name.length() > 0);
		LuaWrapper::DebugGuard guard(L, 1);
		lua_getglobal(L, "LumixAPI");
		char tmp[64];
		const char* c = type_name.end() - 1;
		while (*c != ':' && c != type_name.begin()) --c;
		if (*c == ':') ++c;
		copyNString(Span(tmp), c, int(type_name.end() - c - 2));

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

	static void toVariant(reflection::Variant::Type type, lua_State* L, int idx, reflection::Variant& val) {
		switch(type) {
			case reflection::Variant::BOOL: val = LuaWrapper::checkArg<bool>(L, idx); break;
			case reflection::Variant::U32: val = LuaWrapper::checkArg<u32>(L, idx); break;
			case reflection::Variant::I32: val = LuaWrapper::checkArg<i32>(L, idx); break;
			case reflection::Variant::FLOAT: val = LuaWrapper::checkArg<float>(L, idx); break;
			case reflection::Variant::ENTITY: val = LuaWrapper::checkArg<EntityPtr>(L, idx); break;
			case reflection::Variant::VEC2: val = LuaWrapper::checkArg<Vec2>(L, idx); break;
			case reflection::Variant::COLOR:
			case reflection::Variant::VEC3: val = LuaWrapper::checkArg<Vec3>(L, idx); break;
			case reflection::Variant::DVEC3: val = LuaWrapper::checkArg<DVec3>(L, idx); break;
			case reflection::Variant::CSTR: val = LuaWrapper::checkArg<const char*>(L, idx); break;
			case reflection::Variant::PTR: {
				void* ptr;
				if (!LuaWrapper::checkField(L, idx, "_value", &ptr)) {
					luaL_argerror(L, idx, "expected object");
				}
				val = ptr;
				break;
			}
			case reflection::Variant::VOID: ASSERT(false); break;
		}	
	}

	static int push(lua_State* L, const reflection::Variant& v, Span<const char> type_name) {
		switch (v.type) {
			case reflection::Variant::ENTITY: ASSERT(false); return 0;
			case reflection::Variant::VOID: return 0;
			case reflection::Variant::BOOL: LuaWrapper::push(L, v.b); return 1;
			case reflection::Variant::U32: LuaWrapper::push(L, v.u); return 1;
			case reflection::Variant::I32: LuaWrapper::push(L, v.i); return 1;
			case reflection::Variant::FLOAT: LuaWrapper::push(L, v.f); return 1;
			case reflection::Variant::CSTR: LuaWrapper::push(L, v.s); return 1;
			case reflection::Variant::VEC2: LuaWrapper::push(L, v.v2); return 1;
			case reflection::Variant::COLOR:
			case reflection::Variant::VEC3: LuaWrapper::push(L, v.v3); return 1;
			case reflection::Variant::DVEC3: LuaWrapper::push(L, v.dv3); return 1;
			case reflection::Variant::PTR: pushObject(L, v.ptr, type_name); return 1;
		}
		ASSERT(false);
		return 0;
	}

	static int luaMethodClosure(lua_State* L) {
		LuaWrapper::checkTableArg(L, 1); // self
		void* obj;
		if (!LuaWrapper::checkField(L, 1, "_value", &obj)) {
			ASSERT(false);
			return 0;
		}

		reflection::FunctionBase* f = LuaWrapper::toType<reflection::FunctionBase*>(L, lua_upvalueindex(1));
		
		LuaWrapper::DebugGuard guard(L, f->getReturnType().type == reflection::Variant::VOID ? 0 : 1);

		reflection::Variant args[32];
		ASSERT(f->getArgCount() <= lengthOf(args));
		for (u32 i = 0; i < f->getArgCount(); ++i) {
			reflection::Variant::Type type = f->getArgType(i).type;
			toVariant(type, L, i + 2, args[i]);
		}

		const reflection::Variant res = f->invoke(obj, Span(args, f->getArgCount()));
		return push(L, res, f->getReturnTypeName());
	}

	static int luaModuleMethodClosure(lua_State* L) {
		LuaWrapper::checkTableArg(L, 1); // self
		IModule* module;
		if (!LuaWrapper::checkField(L, 1, "_module", &module)) {
			ASSERT(false);
			return 0;
		}

		reflection::FunctionBase* f = LuaWrapper::toType<reflection::FunctionBase*>(L, lua_upvalueindex(1));
		reflection::Variant args[32];
		ASSERT(f->getArgCount() <= lengthOf(args));
		for (u32 i = 0; i < f->getArgCount(); ++i) {
			reflection::Variant::Type type = f->getArgType(i).type;
			toVariant(type, L, i + 2, args[i]);
		}
		const reflection::Variant res = f->invoke(module, Span(args, f->getArgCount()));
		if (res.type == reflection::Variant::ENTITY) {
			LuaWrapper::pushEntity(L, res.e, &module->getWorld());
			return 1;
		}
		return push(L, res, f->getReturnTypeName());
	}

	static int luaCmpMethodClosure(lua_State* L) {
		LuaWrapper::checkTableArg(L, 1); // self
		if (LuaWrapper::getField(L, 1, "_module") != LUA_TLIGHTUSERDATA) {
			ASSERT(false);
			lua_pop(L, 1);
			return 0;
		}
		IModule* module = LuaWrapper::toType<IModule*>(L, -1);
		lua_pop(L, 1);
			
		if (LuaWrapper::getField(L, 1, "_entity") != LUA_TNUMBER) {
			ASSERT(false);
			lua_pop(L, 1);
			return 0;
		}
		EntityRef entity = {LuaWrapper::toType<int>(L, -1)};
		lua_pop(L, 1);

		reflection::FunctionBase* f = LuaWrapper::toType<reflection::FunctionBase*>(L, lua_upvalueindex(1));
		reflection::Variant args[32];
		ASSERT(f->getArgCount() < lengthOf(args));
		args[0] = entity;
		for (u32 i = 1; i < f->getArgCount(); ++i) {
			reflection::Variant::Type type = f->getArgType(i).type;
			toVariant(type, L, i + 1, args[i]);
		}
		const reflection::Variant res = f->invoke(module, Span(args, f->getArgCount()));
		if (res.type == reflection::Variant::ENTITY) {
			LuaWrapper::pushEntity(L, res.e, &module->getWorld());
			return 1;
		}
		return push(L, res, f->getReturnTypeName());
	}

	static void createClasses(lua_State* L) {
		LuaWrapper::DebugGuard guard(L);
		lua_getglobal(L, "LumixAPI");
		for (auto* f : reflection::allFunctions()) {
			char tmp_obj_type_name[128];
			copyString(Span(tmp_obj_type_name), f->getThisTypeName());
			const char* c = tmp_obj_type_name + strlen(tmp_obj_type_name);
			while (*c != ':' && c != tmp_obj_type_name) --c;
			if (*c == ':') ++c;
			const char* obj_type_name = c;
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

	static const ComponentType LUA_SCRIPT_TYPE = reflection::getComponentType("lua_script");
	static const ComponentType LUA_SCRIPT_INLINE_TYPE = reflection::getComponentType("lua_script_inline");

	enum class LuaModuleVersion : i32
	{
		HASH64,
		INLINE_SCRIPT,

		LATEST
	};

	template <typename T> static T fromString(const char* val) {
		T res;
		fromCString(Span(val, stringLength(val) + 1), res);
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


	struct LuaScriptSystemImpl final : ISystem
	{
		explicit LuaScriptSystemImpl(Engine& engine);
		virtual ~LuaScriptSystemImpl();

		void initBegin() override;
		void createModules(World& world) override;
		const char* getName() const override { return "lua_script"; }
		LuaScriptManager& getScriptManager() { return m_script_manager; }
		void serialize(OutputMemoryStream& stream) const override {}
		bool deserialize(i32 version, InputMemoryStream& stream) override { return version == 0; }

		TagAllocator m_allocator;
		Engine& m_engine;
		LuaScriptManager m_script_manager;
	};


	struct LuaScriptModuleImpl final : LuaScriptModule
	{
		struct TimerData
		{
			float time;
			lua_State* state;
			int func;
		};

		struct CallbackData
		{
			lua_State* state;
			int environment;
		};

		struct ScriptComponent;

		struct ScriptEnvironment {
			lua_State* m_state = nullptr;
			int m_environment = -1;
			int m_thread_ref = -1;
		};

		struct ScriptInstance : ScriptEnvironment
		{
			enum Flags : u32 {
				ENABLED = 1 << 0,
				LOADED = 1 << 1,
				MOVED_FROM = 1 << 2
			};

			explicit ScriptInstance(ScriptComponent& cmp, IAllocator& allocator)
				: m_properties(allocator)
				, m_cmp(&cmp)
			{
				LuaScriptModuleImpl& module = cmp.m_module;
				Engine& engine = module.m_system.m_engine;
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
				LuaWrapper::push(m_state, &module.m_world); // [env, Entity.new, Lumix.Entity, world]
				LuaWrapper::push(m_state, cmp.m_entity.index); // [env, Entity.new, Lumix.Entity, world, entity_index]
				const bool error = !LuaWrapper::pcall(m_state, 3, 1); // [env, entity]
				ASSERT(!error);
				lua_setfield(m_state, -2, "this"); // [env]
				lua_pop(m_state, 1); // []

				m_flags.set(ENABLED);
			}

			ScriptInstance(const ScriptInstance&) = delete;

			ScriptInstance(ScriptInstance&& rhs) 
				: m_properties(rhs.m_properties.move())
				, m_cmp(rhs.m_cmp)
				, m_script(rhs.m_script)
				, m_flags(rhs.m_flags)
			{
				ASSERT(!rhs.m_flags.isSet(MOVED_FROM));
				m_environment = rhs.m_environment;
				m_thread_ref = rhs.m_thread_ref;
				m_state = rhs.m_state;
				rhs.m_script = nullptr;
				rhs.m_flags.set(MOVED_FROM);
			}

			void operator =(ScriptInstance&& rhs) 
			{
				ASSERT(!rhs.m_flags.isSet(MOVED_FROM));
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

					m_cmp->m_module.disableScript(*this);

					Engine& engine = m_cmp->m_module.m_system.m_engine;
					lua_State* L = engine.getState();
					luaL_unref(L, LUA_REGISTRYINDEX, m_thread_ref);
					luaL_unref(m_state, LUA_REGISTRYINDEX, m_environment);
				}
			}

			void onScriptLoaded(LuaScriptModuleImpl& module, ScriptComponent& cmp, int scr_index);
			void onScriptUnloaded(LuaScriptModuleImpl& module, ScriptComponent& cmp, int scr_index);

			ScriptComponent* m_cmp;
			LuaScript* m_script = nullptr;
			Array<Property> m_properties;
			FlagSet<Flags, u32> m_flags;
		};

		struct InlineScriptComponent : ScriptEnvironment {
			InlineScriptComponent(EntityRef entity, LuaScriptModuleImpl& module, IAllocator& allocator)
				: m_source(allocator)
				, m_entity(entity)
				, m_module(module)
			{
				Engine& engine = module.m_system.m_engine;
				lua_State* L = engine.getState();
				m_state = lua_newthread(L);
				m_thread_ref = luaL_ref(L, LUA_REGISTRYINDEX); // []
				lua_newtable(m_state);						   // [env]
				// reference environment
				lua_pushvalue(m_state, -1);							  // [env, env]
				m_environment = luaL_ref(m_state, LUA_REGISTRYINDEX); // [env]

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

				Engine& engine = m_module.m_system.m_engine;
				lua_State* L = engine.getState();
				luaL_unref(L, LUA_REGISTRYINDEX, m_thread_ref);
				luaL_unref(m_state, LUA_REGISTRYINDEX, m_environment);
			}

			void runSource() {
				lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_environment); // [env]
				ASSERT(lua_type(m_state, -1) == LUA_TTABLE);

				bool errors = luaL_loadbuffer(m_state, m_source.c_str(), m_source.length(), "inline script") != 0; // [env, func]

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


			void detectProperties(ScriptInstance& inst)
			{
				static const StableHash INDEX_HASH("__index");
				static const StableHash THIS_HASH("this");
				lua_State* L = inst.m_state;
				lua_rawgeti(L, LUA_REGISTRYINDEX, inst.m_environment); // [env]
				ASSERT(lua_type(L, -1) == LUA_TTABLE);
				lua_pushnil(L); // [env, nil]
				IAllocator& allocator = m_module.m_system.m_allocator;
				u8 valid_properties[256];
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
							const StableHash hash(name);
							if (!m_module.m_property_names.find(hash).isValid()) {
								m_module.m_property_names.insert(hash, String(name, allocator));
							}
							if (hash != INDEX_HASH && hash != THIS_HASH)
							{
								i32 prop_index = getProperty(inst, hash);
								if (prop_index < 0) prop_index = getPropertyLegacy(inst, name);
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
									m_module.applyProperty(inst, existing_prop, existing_prop.stored_value.c_str());
								}
								else {
									const int size = inst.m_properties.size();
									if (inst.m_properties.size() < sizeof(valid_properties) * 8) {
										auto& prop = inst.m_properties.emplace(allocator);
										valid_properties[size / 8] |= 1 << (size % 8);
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


	public:
		LuaScriptModuleImpl(LuaScriptSystemImpl& system, World& world)
			: m_system(system)
			, m_world(world)
			, m_scripts(system.m_allocator)
			, m_inline_scripts(system.m_allocator)
			, m_updates(system.m_allocator)
			, m_input_handlers(system.m_allocator)
			, m_timers(system.m_allocator)
			, m_property_names(system.m_allocator)
			, m_is_game_running(false)
			, m_is_api_registered(false)
			, m_animation_module(nullptr)
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


		ResourceType getPropertyResourceType(EntityRef entity, int scr_index, int prop_index) override
		{
			return m_scripts[entity]->m_scripts[scr_index].m_properties[prop_index].resource_type;
		}


		Property::Type getPropertyType(EntityRef entity, int scr_index, int prop_index) override
		{
			return m_scripts[entity]->m_scripts[scr_index].m_properties[prop_index].type;
		}


		~LuaScriptModuleImpl() {
			Path invalid_path;
			for (auto* script_cmp : m_scripts) {
				ASSERT(script_cmp);
				LUMIX_DELETE(m_system.m_allocator, script_cmp);
			}
		}


		lua_State* getState(EntityRef entity, int scr_index) override
		{
			return m_scripts[entity]->m_scripts[scr_index].m_state;
		}


		World& getWorld() override { return m_world; }


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

			lua_getfield(L, 1, "world");
			if (!lua_istable(L, -1)) {
				luaL_error(L, "%s", "Invalid `this.world`");
			}

			lua_getfield(L, -1, "value");
			if (!lua_islightuserdata(L, -1)) {
				luaL_error(L, "%s", "Invalid `this.world.value`");
			}

			auto* world = LuaWrapper::toType<World*>(L, -1);
			auto* module = (LuaScriptModuleImpl*)world->getModule(LUA_SCRIPT_TYPE);

			lua_pop(L, 2);
			const StableHash prop_name_hash(prop_name);
			const StableHash32 prop_name_hash32(prop_name);
			for (auto& prop : module->m_current_script_instance->m_properties)
			{
				if (prop.name_hash == prop_name_hash || prop.name_hash_legacy == prop_name_hash32)
				{
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
			const auto* world = LuaWrapper::checkArg<World*>(L, 1);
			const EntityRef entity = LuaWrapper::checkArg<EntityRef>(L, 2);
			const int scr_index = LuaWrapper::checkArg<int>(L, 3);

			if (!world->hasComponent(entity, LUA_SCRIPT_TYPE)) {
				return 0;
			}
			
			LuaScriptModuleImpl* module = (LuaScriptModuleImpl*)world->getModule(LUA_SCRIPT_TYPE);

			const int count = module->getScriptCount(entity);
			if (scr_index >= count) {
				return 0;
			}

			/////
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

			if (LuaWrapper::getField(L, 1, "_world") != LUA_TLIGHTUSERDATA) {
				lua_pop(L, 1);
				LuaWrapper::argError(L, 1, "entity");
			}
			World* world = LuaWrapper::toType<World*>(L, -1);
			lua_pop(L, 1);

			const i32 scr_index = LuaWrapper::checkArg<i32>(L, 2);

			if (!world->hasComponent(entity, LUA_SCRIPT_TYPE))
			{
				lua_pushnil(L);
				return 1;
			}
			
			LuaScriptModule* module = (LuaScriptModule*)world->getModule(LUA_SCRIPT_TYPE);

			int count = module->getScriptCount(entity);
			if (scr_index >= count)
			{
				lua_pushnil(L);
				return 1;
			}

			int env = module->getEnvironment(entity, scr_index);
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

		static bool isSameProperty(const char* name, const char* lua_name) {
			char tmp[50];
			convertPropertyToLuaName(name, Span(tmp));
			return equalStrings(tmp, lua_name);
		}

		static void pushArrayPropertyProxy(lua_State* L, const ComponentUID& cmp, const reflection::ArrayProperty& prop) {
			auto getter = [](lua_State* L) -> int {
				auto setter = [](lua_State* L) -> int {
					LuaPropSetterVisitor visitor;
					LuaWrapper::checkTableArg(L, 1);
					visitor.L = L;
					visitor.prop_name = LuaWrapper::checkArg<const char*>(L, 2);
					auto* prop = LuaWrapper::toType<const reflection::ArrayProperty*>(L, lua_upvalueindex(1));
					visitor.cmp.module = LuaWrapper::toType<IModule*>(L, lua_upvalueindex(2));
					visitor.cmp.entity.index = LuaWrapper::toType<i32>(L, lua_upvalueindex(3));
					visitor.cmp.type = LuaWrapper::toType<ComponentType>(L, lua_upvalueindex(4));
					visitor.idx = LuaWrapper::toType<int>(L, lua_upvalueindex(5));
					prop->visitChildren(visitor);
					return 0;
				};

				auto getter = [](lua_State* L) -> int {
					LuaPropGetterVisitor visitor;
					LuaWrapper::checkTableArg(L, 1);
					visitor.L = L;
					visitor.prop_name = LuaWrapper::checkArg<const char*>(L, 2);
					auto* prop = LuaWrapper::toType<const reflection::ArrayProperty*>(L, lua_upvalueindex(1));
					visitor.cmp.module = LuaWrapper::toType<IModule*>(L, lua_upvalueindex(2));
					visitor.cmp.entity.index = LuaWrapper::toType<i32>(L, lua_upvalueindex(3));
					visitor.cmp.type = LuaWrapper::toType<ComponentType>(L, lua_upvalueindex(4));
					visitor.idx = LuaWrapper::toType<int>(L, lua_upvalueindex(5));
					prop->visitChildren(visitor);
					return visitor.found ? 1 : 0;
				};
				
				LuaWrapper::DebugGuard guard(L, 1);
				auto* prop = LuaWrapper::toType<const reflection::ArrayProperty*>(L, lua_upvalueindex(1));
				auto* module = LuaWrapper::toType<IModule*>(L, lua_upvalueindex(2));
				int entity_index = LuaWrapper::toType<i32>(L, lua_upvalueindex(3));
				ComponentType cmp_type = LuaWrapper::toType<ComponentType>(L, lua_upvalueindex(4));
				LuaWrapper::checkTableArg(L, 1); // self
				const int idx = LuaWrapper::checkArg<int>(L, 2);
				lua_newtable(L); // {}
				lua_newtable(L); // {}, mt

				lua_pushlightuserdata(L, (void*)prop);
				lua_pushlightuserdata(L, (void*)module);
				LuaWrapper::push(L, entity_index);
				LuaWrapper::push(L, cmp_type);
				LuaWrapper::push(L, idx);
				lua_pushcclosure(L, getter, 5); // {}, mt, getter
				lua_setfield(L, -2, "__index"); // {}, mt

				lua_pushlightuserdata(L, (void*)prop);
				lua_pushlightuserdata(L, (void*)module);
				LuaWrapper::push(L, entity_index);
				LuaWrapper::push(L, cmp_type);
				LuaWrapper::push(L, idx);
				lua_pushcclosure(L, setter, 5); // {}, mt, getter
				lua_setfield(L, -2, "__newindex"); // {}, mt

				lua_setmetatable(L, -2); // {}
				return 1;
			};
			lua_newtable(L); // {}
			lua_newtable(L); // {}, metatable
			lua_pushlightuserdata(L, (void*)&prop); // {}, mt, &prop
			lua_pushlightuserdata(L, (void*)cmp.module); // {}, mt, &prop, module
			LuaWrapper::push(L, cmp.entity.index); // {}, mt, &prop, module, entity.index
			LuaWrapper::push(L, cmp.type); // {}, mt, &prop, module, entity.index, cmp_type
			lua_pushcclosure(L, getter, 4); // {}, mt, getter
			lua_setfield(L, -2, "__index"); // {}, mt
			lua_setmetatable(L, -2); // {}
		}

		struct LuaPropGetterVisitor  : reflection::IPropertyVisitor
		{
			template <typename T>
			void get(const reflection::Property<T>& prop)
			{
				if (!isSameProperty(prop.name, prop_name)) return;
				
				const T val = prop.get(cmp, idx);
				found = true;
				LuaWrapper::push(L, val);
			}

			void visit(const reflection::Property<float>& prop) override { get(prop); }
			void visit(const reflection::Property<int>& prop) override { get(prop); }
			void visit(const reflection::Property<u32>& prop) override { get(prop); }
			void visit(const reflection::Property<Vec2>& prop) override { get(prop); }
			void visit(const reflection::Property<Vec3>& prop) override { get(prop); }
			void visit(const reflection::Property<IVec3>& prop) override { get(prop); }
			void visit(const reflection::Property<Vec4>& prop) override { get(prop); }
			void visit(const reflection::Property<bool>& prop) override { get(prop); }

			void visit(const reflection::Property<EntityPtr>& prop) override { 
				if (!isSameProperty(prop.name, prop_name)) return;
				
				const EntityPtr val = prop.get(cmp, idx);
				found = true;
				LuaWrapper::pushEntity(L, val, &cmp.module->getWorld());
			}

			void visit(const reflection::Property<Path>& prop) override { 
				if (!isSameProperty(prop.name, prop_name)) return;
				
				const Path p = prop.get(cmp, idx);
				found = true;
				LuaWrapper::push(L, p.c_str());
			}

			void visit(const reflection::Property<const char*>& prop) override { 
				if (!isSameProperty(prop.name, prop_name)) return;
				
				const char* tmp = prop.get(cmp, idx);
				found = true;
				LuaWrapper::push(L, tmp);
			}

			void visit(const reflection::ArrayProperty& prop) override {
				if (!isSameProperty(prop.name, prop_name)) return;

				found = true;
				pushArrayPropertyProxy(L, cmp, prop);
			}
			void visit(const reflection::BlobProperty& prop) override {}

			ComponentUID cmp;
			const char* prop_name;
			int idx;
			bool found = false;
			lua_State* L;
		};
		
		struct LuaPropSetterVisitor : reflection::IPropertyVisitor
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
			void set(const reflection::Property<T>& prop)
			{
				if (!isSameProperty(prop.name, prop_name)) return;
				if (!prop.setter) {
					luaL_error(L, "%s is readonly", prop_name);
					return;
				}
				
				const T val = LuaWrapper::toType<T>(L, 3);
				prop.set(cmp, idx, val);
			}

			void visit(const reflection::Property<float>& prop) override { set(prop); }
			void visit(const reflection::Property<int>& prop) override { set(prop); }
			void visit(const reflection::Property<u32>& prop) override { set(prop); }
			void visit(const reflection::Property<EntityPtr>& prop) override { set(prop); }
			void visit(const reflection::Property<Vec2>& prop) override { set(prop); }
			void visit(const reflection::Property<Vec3>& prop) override { set(prop); }
			void visit(const reflection::Property<IVec3>& prop) override { set(prop); }
			void visit(const reflection::Property<Vec4>& prop) override { set(prop); }
			void visit(const reflection::Property<bool>& prop) override { set(prop); }

			void visit(const reflection::Property<Path>& prop) override {
				if (!isSameProperty(prop.name, prop_name)) return;
				if (!prop.setter) {
					luaL_error(L, "%s is readonly", prop_name);
					return;
				}
				
				const char* val = LuaWrapper::toType<const char*>(L, 3);
				prop.set(cmp, idx, Path(val));
			}

			void visit(const reflection::Property<const char*>& prop) override { 
				if (!isSameProperty(prop.name, prop_name)) return;
				if (!prop.setter) {
					luaL_error(L, "%s is readonly", prop_name);
					return;
				}
				
				const char* val = LuaWrapper::toType<const char*>(L, 3);
				prop.set(cmp, idx, val);
			}

			void visit(const reflection::ArrayProperty& prop) override {}
			void visit(const reflection::BlobProperty& prop) override {}

			ComponentUID cmp;
			const char* prop_name;
			int idx;
			lua_State* L;
			bool found = false;
		};

		static int lua_new_cmp(lua_State* L) {
			LuaWrapper::DebugGuard guard(L, 1);
			LuaWrapper::checkTableArg(L, 1); // self
			const World* world = LuaWrapper::checkArg<World*>(L, 2);
			const EntityRef e = {LuaWrapper::checkArg<i32>(L, 3)};
			
			LuaWrapper::getField(L, 1, "cmp_type");
			const int cmp_type = LuaWrapper::toType<int>(L, -1);
			lua_pop(L, 1);
			IModule* module = world->getModule(ComponentType{cmp_type});

			lua_newtable(L);
			LuaWrapper::setField(L, -1, "_entity", e);
			LuaWrapper::setField(L, -1, "_module", module);
			lua_pushvalue(L, 1);
			lua_setmetatable(L, -2);
			return 1;
		}

		static int lua_prop_getter(lua_State* L) {
			LuaWrapper::checkTableArg(L, 1); // self

			lua_getfield(L, 1, "_module");
			LuaScriptModuleImpl* module = LuaWrapper::toType<LuaScriptModuleImpl*>(L, -1);
			lua_getfield(L, 1, "_entity");
			const EntityRef entity = {LuaWrapper::toType<i32>(L, -1)};
			lua_pop(L, 2);

			if (lua_isnumber(L, 2)) {
				const i32 scr_index = LuaWrapper::toType<i32>(L, 2);
				int env = module->getEnvironment(entity, scr_index);
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
			const reflection::ComponentBase* cmp = reflection::getComponent(v.cmp.type);

			v.cmp.module = module;
			v.cmp.entity = entity;

			cmp->visit(v);
			if (v.found) return 1;

			// TODO put this directly in table, so we don't have to look it up here every time
			for (auto* f : cmp->functions) {
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
			const reflection::ComponentBase* cmp = reflection::getComponent(v.cmp.type);

			lua_getfield(L, 1, "_module");
			v.cmp.module = LuaWrapper::toType<IModule*>(L, -1);
			lua_getfield(L, 1, "_entity");
			v.cmp.entity.index = LuaWrapper::toType<i32>(L, -1);
			lua_pop(L, 2);

			cmp->visit(v);

			if (!v.found) {
				luaL_error(L, "Property `%s` does not exist", v.prop_name);
			}

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

		void registerProperties()
		{
			lua_State* L = m_system.m_engine.getState();
			LuaWrapper::DebugGuard guard(L);

			reflection::Module* module = reflection::getFirstModule();
			while (module) {
				lua_newtable(L); // [ module ]
				lua_getglobal(L, "Lumix"); // [ module, Lumix ]
				lua_pushvalue(L, -2); // [ module, Lumix, module]
				lua_setfield(L, -2, module->name); // [ module, Lumix ]
				lua_pop(L, 1); // [ module ]

				lua_pushvalue(L, -1); // [ module, module ]
				lua_setfield(L, -2, "__index"); // [ module ]

				lua_pushcfunction(L, lua_new_module); // [ module, fn_new_module ]
				lua_setfield(L, -2, "new"); // [ module ]

				for (const reflection::FunctionBase* f :  module->functions) {
					const char* c = f->decl_code;
					while (*c != ':' && *c) ++c;
					ASSERT(*c == ':');
					c += 2;
					lua_pushlightuserdata(L, (void*)f); // [module, f]
					lua_pushcclosure(L, luaModuleMethodClosure, 1); // [module, fn]
					lua_setfield(L, -2, c); // [module]
				}
				lua_pop(L, 1); // []

				module = module->next;
			}

			for (const reflection::RegisteredComponent& cmp : reflection::getComponents()) {
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
			auto* module = LuaWrapper::checkArg<LuaScriptModuleImpl*>(L, 1);
			float time = LuaWrapper::checkArg<float>(L, 2);
			if (!lua_isfunction(L, 3)) LuaWrapper::argError(L, 3, "function");
			TimerData& timer = module->m_timers.emplace();
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
			LuaWrapper::createSystemFunction(engine_state, "LuaScript", "getEnvironment", &LuaScriptModuleImpl::getEnvironment);
			LuaWrapper::createSystemFunction(engine_state, "LuaScript", "rescan", &LuaScriptModuleImpl::rescan);
			
			#define REGISTER_FUNCTION(F) \
				do { \
					auto f = &LuaWrapper::wrapMethod<&LuaScriptModuleImpl::F>; \
					LuaWrapper::createSystemFunction(engine_state, "LuaScript", #F, f); \
				} while(false)

			REGISTER_FUNCTION(cancelTimer);

			#undef REGISTER_FUNCTION

			LuaWrapper::createSystemFunction(engine_state, "LuaScript", "setTimer", &LuaScriptModuleImpl::setTimer);
		}


		int getEnvironment(EntityRef entity, int scr_index) override
		{
			return m_scripts[entity]->m_scripts[scr_index].m_environment;
		}


		const char* getPropertyName(StableHash name_hash) const
		{
			auto iter = m_property_names.find(name_hash);
			if (iter.isValid()) return iter.value().c_str();
			return "N/A";
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
			LuaWrapper::push(script.m_state, &m_world); // [env, Entity.new, Lumix.Entity, world]
			LuaWrapper::push(script.m_state, e.index); // [env, Entity.new, Lumix.Entity, world, entity_index]
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

			if (prop.type != Property::STRING && prop.type != Property::RESOURCE && value[0] == '\0') return;

			if (prop.type == Property::ENTITY)
			{
				applyEntityProperty(script, name, prop, value);
				return;
			}

			StaticString<1024> tmp(name, " = ");
			if (prop.type == Property::STRING) tmp.append("\"", value, "\"");
			else tmp.add(value);

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
				toString(value, prop.stored_value);
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


		void disableScript(ScriptEnvironment& inst)
		{
			if (!inst.m_state) return;
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
			inst.m_script = path.isEmpty() ? nullptr : rm.load<LuaScript>(path);
			if (inst.m_script) inst.m_script->onLoaded<&ScriptComponent::onScriptLoaded>(&cmp);
		}

		void startScript(EntityRef entity, InlineScriptComponent& instance, bool is_reload) {
			instance.runSource();
			startScriptInternal(entity, instance, is_reload);
		}

		void startScript(EntityRef entity, ScriptInstance& instance, bool is_reload) {
			if (!instance.m_flags.isSet(ScriptInstance::ENABLED)) return;
			
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
			auto* inline_call = beginFunctionCallInlineScript(e, "onRectMouseDown");
			if (inline_call) {
				inline_call->add(x);
				inline_call->add(y);
				endFunctionCall();
			}

			if (!m_world.hasComponent(e, LUA_SCRIPT_TYPE)) return;

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

			if (!m_world.hasComponent(e, LUA_SCRIPT_TYPE)) return;

			for (int i = 0, c = getScriptCount(e); i < c; ++i)
			{
				auto* call = beginFunctionCall(e, i, event);
				if (call) endFunctionCall();
			}
		}

		void startGame() override
		{
			m_animation_module = (AnimationModule*)m_world.getModule("animation");
			m_is_game_running = true;
			m_gui_module = (GUIModule*)m_world.getModule("gui");
			if (m_gui_module)
			{
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
			m_scripts_start_called = false;
			m_is_game_running = false;
			m_updates.clear();
			m_input_handlers.clear();
			m_timers.clear();
			m_animation_module = nullptr;
		}

		void createInlineScriptComponent(EntityRef entity) {
			m_inline_scripts.insert(entity, InlineScriptComponent(entity, *this, m_system.m_allocator));
			m_world.onComponentCreated(entity, LUA_SCRIPT_INLINE_TYPE, this);
		}

		void destroyInlineScriptComponent(EntityRef entity) {
			m_inline_scripts.erase(entity);
			m_world.onComponentDestroyed(entity, LUA_SCRIPT_INLINE_TYPE, this);
		}

		void createScriptComponent(EntityRef entity) {
			auto& allocator = m_system.m_allocator;
			ScriptComponent* script = LUMIX_NEW(allocator, ScriptComponent)(*this, entity, allocator);
			m_scripts.insert(entity, script);
			m_world.onComponentCreated(entity, LUA_SCRIPT_TYPE, this);
		}

		void destroyScriptComponent(EntityRef entity) {
			ScriptComponent* cmp = m_scripts[entity];
			LUMIX_DELETE(m_system.m_allocator, cmp);
			m_scripts.erase(entity);
			m_world.onComponentDestroyed(entity, LUA_SCRIPT_TYPE, this);
		}

		template <typename T>
		T getPropertyValue(EntityRef entity, int scr_index, const char* property_name) {
			const StableHash hash(property_name);
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

			const StableHash hash(property_name);
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
							e = EntityPtr{ (i32)lua_tointeger(scr.m_state, -1) };
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
			serializer.write(m_inline_scripts.size());
			for (auto iter = m_inline_scripts.begin(), end = m_inline_scripts.end(); iter != end; ++iter) {
				serializer.write(iter.key());
				serializer.write(iter.value().m_source);
			}
			
			serializer.write(m_scripts.size());
			for (ScriptComponent* script_cmp : m_scripts)
			{
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
						auto iter = m_property_names.find(prop.name_hash);
						if (iter.isValid()) {
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


		void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override
		{
			if (version > (i32)LuaModuleVersion::INLINE_SCRIPT) {
				const i32 len = serializer.read<i32>();
				m_inline_scripts.reserve(m_scripts.size() + len);
				for (int i = 0; i < len; ++i) {
					EntityRef entity;
					serializer.read(entity);
					entity = entity_map.get(entity);
					auto iter = m_inline_scripts.insert(entity, InlineScriptComponent(entity, *this, m_system.m_allocator));
					serializer.read(iter.value().m_source);
					m_world.onComponentCreated(entity, LUA_SCRIPT_INLINE_TYPE, this);
				}
			}

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
				for (int scr_idx = 0; scr_idx < scr_count; ++scr_idx)
				{
					auto& scr = script->m_scripts.emplace(*script, allocator);

					const char* path = serializer.readString();
					serializer.read(scr.m_flags);
					int prop_count;
					serializer.read(prop_count);
					scr.m_properties.reserve(prop_count);
					for (int j = 0; j < prop_count; ++j)
					{
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
						const char* tmp = serializer.readString();
						if (type == Property::ENTITY) {
							EntityPtr prop_value;
							fromCString(Span(tmp, stringLength(tmp)), prop_value.index);
							prop_value = entity_map.get(prop_value);
							StaticString<64> buf(prop_value.index);
							prop.stored_value = buf;
						}
						else {
							prop.stored_value = tmp;
						}
					}
					setPath(*script, scr, Path(path));
				}
				m_world.onComponentCreated(script->m_entity, LUA_SCRIPT_TYPE, this);
			}
		}


		ISystem& getSystem() const override { return m_system; }


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

					startScript(instance.m_cmp->m_entity, instance, false);
				}
			}

			for (auto iter = m_inline_scripts.begin(), end = m_inline_scripts.end(); iter != end; ++iter) {
				startScript(iter.key(), iter.value(), false);
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


		void update(float time_delta) override
		{
			PROFILE_FUNCTION();

			if (!m_is_game_running) return;
			if (!m_scripts_start_called) startScripts();

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


		void enableScript(EntityRef entity, int scr_index, bool enable) override
		{
			ScriptInstance& inst = m_scripts[entity]->m_scripts[scr_index];
			if (inst.m_flags.isSet(ScriptInstance::ENABLED) == enable) return;

			inst.m_flags.set(ScriptInstance::ENABLED, enable);

			setEnableProperty(entity, scr_index, inst, enable);

			if(enable)
			{
				startScript(entity, inst, false);
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

		const char* getInlineScriptCode(EntityRef entity) override {
			return m_inline_scripts[entity].m_source.c_str();
		}
		
		void setInlineScriptCode(EntityRef entity, const char* value) override {
			m_inline_scripts[entity].m_source = value;
		}

		LuaScriptSystemImpl& m_system;
		HashMap<EntityRef, ScriptComponent*> m_scripts;
		HashMap<EntityRef, InlineScriptComponent> m_inline_scripts;
		HashMap<StableHash, String> m_property_names;
		Array<CallbackData> m_input_handlers;
		World& m_world;
		Array<CallbackData> m_updates;
		Array<TimerData> m_timers;
		FunctionCall m_function_call;
		ScriptInstance* m_current_script_instance;
		bool m_scripts_start_called = false;
		bool m_is_api_registered = false;
		bool m_is_game_running = false;
		GUIModule* m_gui_module = nullptr;
		AnimationModule* m_animation_module;
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

		module.m_current_script_instance = this;
		errors = lua_pcall(m_state, 0, 0, 0) != 0; // [env]
		if (errors)	{
			logError(m_script->getPath(), ": ", lua_tostring(m_state, -1));
			lua_pop(m_state, 1);
		}
		lua_pop(m_state, 1); // []

		cmp.detectProperties(*this);
					
		bool enabled = m_flags.isSet(ScriptInstance::ENABLED);
		module.setEnableProperty(cmp.m_entity, scr_index, *this, enabled);
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

		if (module.m_is_game_running) module.startScript(m_cmp->m_entity, *this, is_reload);
	}


	struct LuaProperties : reflection::DynamicProperties {
		LuaProperties(IAllocator& allocator)
			: DynamicProperties(allocator)
		{
			name = "lua_properties";
		}
		
		u32 getCount(ComponentUID cmp, int index) const override { 
			LuaScriptModuleImpl& module = (LuaScriptModuleImpl&)*cmp.module;
			const EntityRef e = (EntityRef)cmp.entity;
			return module.getPropertyCount(e, index);
		}

		Type getType(ComponentUID cmp, int array_idx, u32 idx) const override { 
			LuaScriptModuleImpl& module = (LuaScriptModuleImpl&)*cmp.module;
			const EntityRef e = (EntityRef)cmp.entity;
			const LuaScriptModule::Property::Type type = module.getPropertyType(e, array_idx, idx);
			switch(type) {
				case LuaScriptModule::Property::Type::BOOLEAN: return BOOLEAN;
				case LuaScriptModule::Property::Type::INT: return I32;
				case LuaScriptModule::Property::Type::FLOAT: return FLOAT;
				case LuaScriptModule::Property::Type::STRING: return STRING;
				case LuaScriptModule::Property::Type::ENTITY: return ENTITY;
				case LuaScriptModule::Property::Type::RESOURCE: return RESOURCE;
				case LuaScriptModule::Property::Type::COLOR: return COLOR;
				case LuaScriptModule::Property::Type::ANY: return NONE;
			}
			ASSERT(false);
			return NONE;
		}

		const char* getName(ComponentUID cmp, int array_idx, u32 idx) const override {
			LuaScriptModuleImpl& module = (LuaScriptModuleImpl&)*cmp.module;
			const EntityRef e = (EntityRef)cmp.entity;
			return module.getPropertyName(e, array_idx, idx);
		}

		reflection::ResourceAttribute getResourceAttribute(ComponentUID cmp, int array_idx, u32 idx) const override {
			reflection::ResourceAttribute attr;
			LuaScriptModuleImpl& module = (LuaScriptModuleImpl&)*cmp.module;
			const EntityRef e = (EntityRef)cmp.entity;
			const LuaScriptModule::Property::Type type = module.getPropertyType(e, array_idx, idx);
			ASSERT(type == LuaScriptModule::Property::Type::RESOURCE);
			attr.resource_type  = module.getPropertyResourceType(e, array_idx, idx);
			return attr;
		}


		Value getValue(ComponentUID cmp, int array_idx, u32 idx) const override { 
			LuaScriptModuleImpl& module = (LuaScriptModuleImpl&)*cmp.module;
			const EntityRef e = (EntityRef)cmp.entity;
			const LuaScriptModule::Property::Type type = module.getPropertyType(e, array_idx, idx);
			const char* name = module.getPropertyName(e, array_idx, idx);
			Value v = {};
			switch(type) {
				case LuaScriptModule::Property::Type::COLOR: reflection::set(v, module.getPropertyValue<Vec3>(e, array_idx, name)); break;
				case LuaScriptModule::Property::Type::BOOLEAN: reflection::set(v, module.getPropertyValue<bool>(e, array_idx, name)); break;
				case LuaScriptModule::Property::Type::INT: reflection::set(v, module.getPropertyValue<i32>(e, array_idx, name)); break;
				case LuaScriptModule::Property::Type::FLOAT: reflection::set(v, module.getPropertyValue<float>(e, array_idx, name)); break;
				case LuaScriptModule::Property::Type::STRING: reflection::set(v, module.getPropertyValue<const char*>(e, array_idx, name)); break;
				case LuaScriptModule::Property::Type::ENTITY: reflection::set(v, module.getPropertyValue<EntityPtr>(e, array_idx, name)); break;
				case LuaScriptModule::Property::Type::RESOURCE: {
					const i32 res_idx = module.getPropertyValue<i32>(e, array_idx, name);
					if (res_idx < 0) {
						reflection::set(v, ""); 
					}
					else {
						Resource* res = module.m_system.m_engine.getLuaResource(res_idx);
						reflection::set(v, res ? res->getPath().c_str() : ""); 
					}
					break;
				}
				case LuaScriptModule::Property::Type::ANY: reflection::set(v, module.getPropertyValue<const char*>(e, array_idx, name)); break;
			}
			return v;
		}
		
		void set(ComponentUID cmp, int array_idx, const char* name, Type type, Value v) const override { 
			LuaScriptModuleImpl& module = (LuaScriptModuleImpl&)*cmp.module;
			const EntityRef e = (EntityRef)cmp.entity;
			switch(type) {
				case BOOLEAN: module.setPropertyValue(e, array_idx, name, v.b); break;
				case I32: module.setPropertyValue(e, array_idx, name, v.i); break;
				case FLOAT: module.setPropertyValue(e, array_idx, name, v.f); break;
				case STRING: module.setPropertyValue(e, array_idx, name, v.s); break;
				case ENTITY: module.setPropertyValue(e, array_idx, name, v.e); break;
				case RESOURCE: module.setPropertyValue(e, array_idx, name, v.s); break;
				case COLOR: module.setPropertyValue(e, array_idx, name, v.v3); break;
				case NONE: break;
			}
		}

		void set(ComponentUID cmp, int array_idx, u32 idx, Value v) const override {
			LuaScriptModuleImpl& module = (LuaScriptModuleImpl&)*cmp.module;
			const EntityRef e = (EntityRef)cmp.entity;
			const LuaScriptModule::Property::Type type = module.getPropertyType(e, array_idx, idx);
			const char* name = module.getPropertyName(e, array_idx, idx);
			switch(type) {
				case LuaScriptModule::Property::Type::BOOLEAN: module.setPropertyValue(e, array_idx, name, v.b); break;
				case LuaScriptModule::Property::Type::INT: module.setPropertyValue(e, array_idx, name, v.i); break;
				case LuaScriptModule::Property::Type::FLOAT: module.setPropertyValue(e, array_idx, name, v.f); break;
				case LuaScriptModule::Property::Type::STRING: module.setPropertyValue(e, array_idx, name, v.s); break;
				case LuaScriptModule::Property::Type::ENTITY: module.setPropertyValue(e, array_idx, name, v.e); break;
				case LuaScriptModule::Property::Type::RESOURCE: module.setPropertyValue(e, array_idx, name, v.s); break;
				case LuaScriptModule::Property::Type::COLOR: module.setPropertyValue(e, array_idx, name, v.v3); break;
				case LuaScriptModule::Property::Type::ANY: ASSERT(false); break;
			}
		}
	};


	LuaScriptSystemImpl::LuaScriptSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator(), "lua system")
		, m_script_manager(m_allocator)
	{
		m_script_manager.create(LuaScript::TYPE, engine.getResourceManager());

		LUMIX_MODULE(LuaScriptModuleImpl, "lua_script")
			.LUMIX_CMP(InlineScriptComponent, "lua_script_inline", "Lua Script / Inline") 
				.LUMIX_PROP(InlineScriptCode, "Code").multilineAttribute()
			.LUMIX_CMP(ScriptComponent, "lua_script", "Lua Script / File") 
			.begin_array<&LuaScriptModule::getScriptCount, &LuaScriptModule::addScript, &LuaScriptModule::removeScript>("scripts")
				.prop<&LuaScriptModule::isScriptEnabled, &LuaScriptModule::enableScript>("Enabled")
				.LUMIX_PROP(ScriptPath, "Path").resourceAttribute(LuaScript::TYPE)
				.property<LuaProperties>()
			.end_array();
	}

	void LuaScriptSystemImpl::initBegin() {
		PROFILE_FUNCTION();
		createClasses(m_engine.getState());
	}

	LuaScriptSystemImpl::~LuaScriptSystemImpl()
	{
		m_script_manager.destroy();
	}

	void LuaScriptSystemImpl::createModules(World& world)
	{
		UniquePtr<LuaScriptModuleImpl> module = UniquePtr<LuaScriptModuleImpl>::create(m_allocator, *this, world);
		world.addModule(module.move());
	}


	LUMIX_PLUGIN_ENTRY(lua_script) {
		PROFILE_FUNCTION();
		return LUMIX_NEW(engine.getAllocator(), LuaScriptSystemImpl)(engine);
	}
} // namespace Lumix
