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
class Renderer;


class LUMIX_SCRIPT_API ScriptSystem : public IPlugin
{
	public:
		virtual void update(float time_delta) = 0;
		virtual Universe* getUniverse() const = 0;
		virtual Component createScript(Entity entity) = 0;
		virtual Engine* getEngine() const = 0;

		virtual void deserialize(ISerializer& serializer) = 0;
		virtual void serialize(ISerializer& serializer) = 0;

		virtual void getScriptPath(Component cmp, string& str) = 0;
		virtual void setScriptPath(Component cmp, const string& str) = 0;

	protected:
		ScriptSystem() {}
		virtual ~ScriptSystem() {}
};


extern "C"
{
	LUMIX_SCRIPT_API IPlugin* createPlugin();
}


} // ~namespace Lumix
