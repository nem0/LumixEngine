#pragma once


#include "core/path.h"
#include "core/string.h"
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
		explicit Property(IAllocator& allocator)
			: m_value(allocator)
		{
		}

		string m_value;
		uint32 m_name_hash;
	};

	class IFunctionCall
	{
	public:
		virtual void add(int parameter) = 0;
		virtual void add(float parameter) = 0;
		virtual void add(void* parameter) = 0;
	};


	typedef int (*lua_CFunction) (lua_State *L);

public:
	virtual Path getScriptPath(ComponentIndex cmp, int scr_index) = 0;	
	virtual void setScriptPath(ComponentIndex cmp, int scr_index, const Path& path) = 0;
	virtual int getPropertyCount(ComponentIndex cmp, int scr_index) const = 0;
	virtual const char* getPropertyName(ComponentIndex cmp, int scr_index, int prop_index) const = 0;
	virtual const char* getPropertyValue(ComponentIndex cmp, int scr_index, int prop_index) const = 0;
	virtual LuaScript* getScriptResource(ComponentIndex cmp, int scr_index) const = 0;
	virtual void setPropertyValue(ComponentIndex cmp,
		int scr_index,
		const char* name,
		const char* value) = 0;
	virtual ComponentIndex getComponent(Entity entity) = 0;
	virtual int getEnvironment(Entity entity, int scr_index) = 0;
	virtual IFunctionCall* beginFunctionCall(ComponentIndex cmp, int scr_index, const char* function) = 0;
	virtual void endFunctionCall(IFunctionCall& caller) = 0;
	virtual int getScriptCount(ComponentIndex cmp) = 0;
	virtual lua_State* getGlobalState() = 0;
};


} // namespace Lumix