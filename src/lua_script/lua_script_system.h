#pragma once


#include "engine/path.h"
#include "engine/string.h"
#include "engine/iplugin.h"


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
			ANY
		};

		explicit Property(IAllocator& allocator)
			: stored_value(allocator)
		{
		}

		uint32 name_hash;
		Type type;
		string stored_value;
	};


	class IFunctionCall
	{
	public:
		virtual void add(int parameter) = 0;
		virtual void add(float parameter) = 0;
		virtual void add(void* parameter) = 0;
		virtual void addEnvironment(int env) = 0;
	};


	typedef int (*lua_CFunction) (lua_State *L);

public:
	virtual Path getScriptPath(ComponentIndex cmp, int scr_index) = 0;	
	virtual void setScriptPath(ComponentIndex cmp, int scr_index, const Path& path) = 0;
	virtual ComponentIndex getComponent(Entity entity) = 0;
	virtual int getEnvironment(ComponentIndex cmp, int scr_index) = 0;
	virtual IFunctionCall* beginFunctionCall(ComponentIndex cmp, int scr_index, const char* function) = 0;
	virtual void endFunctionCall(IFunctionCall& caller) = 0;
	virtual int getScriptCount(ComponentIndex cmp) = 0;
	virtual lua_State* getState(ComponentIndex cmp, int scr_index) = 0;
	virtual void insertScript(ComponentIndex cmp, int idx) = 0;
	virtual int addScript(ComponentIndex cmp) = 0;
	virtual void removeScript(ComponentIndex cmp, int scr_index) = 0;
	virtual void serializeScript(ComponentIndex cmp, int scr_index, OutputBlob& blob) = 0;
	virtual void deserializeScript(ComponentIndex cmp, int scr_index, InputBlob& blob) = 0;
	virtual void setPropertyValue(Lumix::ComponentIndex cmp, int scr_index, const char* name, const char* value) = 0;
	virtual void getPropertyValue(ComponentIndex cmp, int scr_index, const char* property_name, char* out, int max_size) = 0;
	virtual int getPropertyCount(ComponentIndex cmp, int scr_index) = 0;
	virtual const char* getPropertyName(ComponentIndex cmp, int scr_index, int prop_index) = 0;
	virtual Property::Type getPropertyType(ComponentIndex cmp, int scr_index, int prop_index) = 0;
};


} // namespace Lumix