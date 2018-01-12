#pragma once


#include "engine/iplugin.h"
#include "engine/path.h"
#include "engine/resource.h"
#include "engine/string.h"


struct lua_State;


namespace Lumix
{


class LuaScript;


class LuaScriptScene : public IScene
{
public:
	struct Property
	{
		enum Type : int
		{
			BOOLEAN,
			FLOAT,
			ENTITY,
			RESOURCE,
			STRING,
			ANY
		};

		explicit Property(IAllocator& allocator)
			: stored_value(allocator)
		{
		}

		u32 name_hash;
		Type type;
		ResourceType resource_type;
		string stored_value;
	};


	struct IFunctionCall
	{
		virtual ~IFunctionCall() {}
		virtual void add(int parameter) = 0;
		virtual void add(bool parameter) = 0;
		virtual void add(float parameter) = 0;
		virtual void add(void* parameter) = 0;
		virtual void addEnvironment(int env) = 0;
	};


	typedef int (*lua_CFunction) (lua_State *L);

public:
	virtual Path getScriptPath(Entity entity, int scr_index) = 0;	
	virtual void setScriptPath(Entity entity, int scr_index, const Path& path) = 0;
	virtual int getEnvironment(Entity entity, int scr_index) = 0;
	virtual IFunctionCall* beginFunctionCall(Entity entity, int scr_index, const char* function) = 0;
	virtual void endFunctionCall() = 0;
	virtual int getScriptCount(Entity entity) = 0;
	virtual lua_State* getState(Entity entity, int scr_index) = 0;
	virtual void insertScript(Entity entity, int idx) = 0;
	virtual int addScript(Entity entity) = 0;
	virtual void removeScript(Entity entity, int scr_index) = 0;
	virtual void enableScript(Entity entity, int scr_index, bool enable) = 0;
	virtual bool isScriptEnabled(Entity entity, int scr_index) const = 0;
	virtual void moveScript(Entity entity, int scr_index, bool up) = 0;
	virtual void serializeScript(Entity entity, int scr_index, OutputBlob& blob) = 0;
	virtual void deserializeScript(Entity entity, int scr_index, InputBlob& blob) = 0;
	virtual void setPropertyValue(Entity entity, int scr_index, const char* name, const char* value) = 0;
	virtual void getPropertyValue(Entity entity, int scr_index, const char* property_name, char* out, int max_size) = 0;
	virtual int getPropertyCount(Entity entity, int scr_index) = 0;
	virtual const char* getPropertyName(Entity entity, int scr_index, int prop_index) = 0;
	virtual Property::Type getPropertyType(Entity entity, int scr_index, int prop_index) = 0;
	virtual ResourceType getPropertyResourceType(Entity entity, int scr_index, int prop_index) = 0;
	virtual void getScriptData(Entity entity, OutputBlob& blob) = 0;
	virtual void setScriptData(Entity entity, InputBlob& blob) = 0;
};


} // namespace Lumix