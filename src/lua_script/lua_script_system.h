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
		virtual void add(int parameter) = 0;
		virtual void add(float parameter) = 0;
		virtual void add(void* parameter) = 0;
		virtual void addEnvironment(int env) = 0;
	};


	typedef int (*lua_CFunction) (lua_State *L);

public:
	virtual Path getScriptPath(ComponentHandle cmp, int scr_index) = 0;	
	virtual void setScriptPath(ComponentHandle cmp, int scr_index, const Path& path) = 0;
	virtual ComponentHandle getComponent(Entity entity) = 0;
	virtual int getEnvironment(ComponentHandle cmp, int scr_index) = 0;
	virtual IFunctionCall* beginFunctionCall(ComponentHandle cmp, int scr_index, const char* function) = 0;
	virtual void endFunctionCall() = 0;
	virtual int getScriptCount(ComponentHandle cmp) = 0;
	virtual lua_State* getState(ComponentHandle cmp, int scr_index) = 0;
	virtual void insertScript(ComponentHandle cmp, int idx) = 0;
	virtual int addScript(ComponentHandle cmp) = 0;
	virtual void removeScript(ComponentHandle cmp, int scr_index) = 0;
	virtual void moveScript(ComponentHandle cmp, int scr_index, bool up) = 0;
	virtual void serializeScript(ComponentHandle cmp, int scr_index, OutputBlob& blob) = 0;
	virtual void deserializeScript(ComponentHandle cmp, int scr_index, InputBlob& blob) = 0;
	virtual void setPropertyValue(ComponentHandle cmp, int scr_index, const char* name, const char* value) = 0;
	virtual void getPropertyValue(ComponentHandle cmp, int scr_index, const char* property_name, char* out, int max_size) = 0;
	virtual int getPropertyCount(ComponentHandle cmp, int scr_index) = 0;
	virtual const char* getPropertyName(ComponentHandle cmp, int scr_index, int prop_index) = 0;
	virtual Property::Type getPropertyType(ComponentHandle cmp, int scr_index, int prop_index) = 0;
	virtual ResourceType getPropertyResourceType(ComponentHandle cmp, int scr_index, int prop_index) = 0;
	virtual void getScriptData(ComponentHandle cmp, OutputBlob& blob) = 0;
	virtual void setScriptData(ComponentHandle cmp, InputBlob& blob) = 0;
};


} // namespace Lumix