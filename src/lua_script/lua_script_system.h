#pragma once


#include "engine/core/path.h"
#include "engine/core/string.h"
#include "engine/iplugin.h"


struct lua_State;


namespace Lumix
{


class LuaScript;


class LuaScriptScene : public IScene
{
public:
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
};


} // namespace Lumix