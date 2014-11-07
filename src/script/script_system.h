#pragma once


#include "core/lumix.h"
#include "core/string.h"
#include "engine/iplugin.h"
#include "universe/universe.h"


namespace Lumix
{


class Engine;
class InputSystem;
class Navigation;


class LUMIX_SCRIPT_API ScriptScene : public IScene
{
	public:
		virtual void getScriptPath(Component cmp, string& str) = 0;
		virtual void setScriptPath(Component cmp, const string& str) = 0;
};


class LUMIX_SCRIPT_API ScriptSystem : public IPlugin
{
	public:
		virtual Engine* getEngine() const = 0;

	protected:
		ScriptSystem() {}
		virtual ~ScriptSystem() {}
};


extern "C"
{
	LUMIX_SCRIPT_API IPlugin* createPlugin(Engine& engine);
}


} // ~namespace Lumix
