#pragma once


#include "core/hash.h"
#include "core/path.h"
#include "engine/plugin.h"
#include "engine/resource.h"
#include "core/string.h"


struct lua_State;


namespace Lumix
{


struct LuaScript;

struct LuaScriptSystem : ISystem {
	using LuaResourceHandle = u32;
	
	virtual lua_State* getState() = 0;
	virtual struct Resource* getLuaResource(LuaResourceHandle idx) const = 0;
	virtual LuaResourceHandle addLuaResource(const struct Path& path, struct ResourceType type) = 0;
	virtual void unloadLuaResource(LuaResourceHandle resource_idx) = 0;
};

struct LuaScriptModule : IModule
{
	struct Property
	{
		enum Type : int
		{
			BOOLEAN,
			FLOAT,
			INT,
			ENTITY,
			RESOURCE,
			STRING,
			COLOR,
			ANY
		};

		explicit Property(IAllocator& allocator)
			: stored_value(allocator)
		{
		}

		StableHash32 name_hash_legacy;
		StableHash name_hash;
		Type type;
		ResourceType resource_type;
		String stored_value;
	};


	struct IFunctionCall
	{
		virtual ~IFunctionCall() {}
		virtual void add(int parameter) = 0;
		virtual void add(bool parameter) = 0;
		virtual void add(float parameter) = 0;
		virtual void add(void* parameter) = 0;
		virtual void add(EntityPtr parameter) = 0;
		virtual void addEnvironment(int env) = 0;
	};


	using lua_CFunction = int (*) (lua_State *L);

	virtual Path getScriptPath(EntityRef entity, int scr_index) = 0;	
	virtual void setScriptPath(EntityRef entity, int scr_index, const Path& path) = 0;
	virtual int getEnvironment(EntityRef entity, int scr_index) = 0;
	virtual IFunctionCall* beginFunctionCall(EntityRef entity, int scr_index, const char* function) = 0;
	virtual IFunctionCall* beginFunctionCallInlineScript(EntityRef entity, const char* function) = 0;
	virtual void endFunctionCall() = 0;
	virtual int getScriptCount(EntityRef entity) = 0;
	virtual bool execute(EntityRef entity, i32 scr_index, StringView code) = 0;
	virtual lua_State* getState(EntityRef entity, int scr_index) = 0;
	virtual void insertScript(EntityRef entity, int idx) = 0;
	virtual int addScript(EntityRef entity, int scr_index) = 0;
	virtual void removeScript(EntityRef entity, int scr_index) = 0;
	virtual void enableScript(EntityRef entity, int scr_index, bool enable) = 0;
	virtual bool isScriptEnabled(EntityRef entity, int scr_index) = 0;
	virtual void moveScript(EntityRef entity, int scr_index, bool up) = 0;
	virtual int getPropertyCount(EntityRef entity, int scr_index) = 0;
	virtual const char* getPropertyName(EntityRef entity, int scr_index, int prop_index) = 0;
	virtual Property::Type getPropertyType(EntityRef entity, int scr_index, int prop_index) = 0;
	virtual ResourceType getPropertyResourceType(EntityRef entity, int scr_index, int prop_index) = 0;
	virtual const char* getInlineScriptCode(EntityRef entity) = 0;
	virtual void setInlineScriptCode(EntityRef entity, const char* value) = 0;
};


} // namespace Lumix