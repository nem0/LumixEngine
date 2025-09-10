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

//@ module LuaScriptModule lua_script "Lua"
struct LuaScriptModule : IModule {
	struct Property {
		enum Type : int {
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
		{}

		StableHash32 name_hash_legacy;
		StableHash name_hash;
		Type type;
		ResourceType resource_type;
		OutputMemoryStream stored_value;
		bool is_array = false;
	};

	struct IFunctionCall {
		virtual ~IFunctionCall() {}
		virtual void add(int parameter) = 0;
		virtual void add(bool parameter) = 0;
		virtual void add(float parameter) = 0;
		virtual void add(void* parameter) = 0;
		virtual void add(EntityPtr parameter) = 0;
		virtual void addEnvironment(int env) = 0;
	};

	//@ component Script id lua_script label "File"
	//@ array Script scripts
	virtual bool isScriptEnabled(EntityRef entity, int scr_index) = 0;
	virtual void enableScript(EntityRef entity, int scr_index, bool enable) = 0;
	virtual Path getScriptPath(EntityRef entity, int scr_index) = 0;					//@ label "Path" resource_type LuaScript::TYPE
	virtual void setScriptPath(EntityRef entity, int scr_index, const Path& path) = 0;
	virtual void getScriptBlob(EntityRef e, u32 index, OutputMemoryStream& stream) = 0;
	virtual void setScriptBlob(EntityRef e, u32 index, InputMemoryStream& stream) = 0;
	//@ end
	//@ end
	virtual void createScript(EntityRef entity) = 0;
	virtual void destroyScript(EntityRef entity) = 0;
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
	virtual void moveScript(EntityRef entity, int scr_index, bool up) = 0;
	virtual int getPropertyCount(EntityRef entity, int scr_index) = 0;
	virtual const char* getPropertyName(EntityRef entity, int scr_index, int prop_index) = 0;
	virtual const Property& getProperty(EntityRef entity, int scr_index, int prop_index) = 0;
	
	//@ component InlineScript id lua_script_inline label "Inline"
	virtual const char* getInlineScriptCode(EntityRef entity) = 0;	//@ multiline
	virtual void setInlineScriptCode(EntityRef entity, const char* value) = 0;
	//@ end
	virtual void createInlineScript(EntityRef entity) = 0;
	virtual void destroyInlineScript(EntityRef entity) = 0;
};


} // namespace Lumix