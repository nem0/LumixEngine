#pragma once


#include "core/lumix.h"
#include "core/string.h"
#include "universe/universe.h"


namespace Lumix
{


class Engine;
class InputSystem;
class Navigation;
class Renderer;


class LUX_ENGINE_API ScriptSystem abstract
{
	public:
		static ScriptSystem* create();
		static void destroy(ScriptSystem* instance);

		virtual void start() = 0;
		virtual void stop() = 0;
		virtual void update(float time_delta) = 0;
		virtual void setUniverse(Universe* universe) = 0;
		virtual Universe* getUniverse() const = 0;
		virtual Component createScript(Entity entity) = 0;
		virtual void setEngine(Engine& engine) = 0;
		virtual Engine* getEngine() const = 0;

		virtual void deserialize(ISerializer& serializer) = 0;
		virtual void serialize(ISerializer& serializer) = 0;

		virtual void getScriptPath(Component cmp, string& str) = 0;
		virtual void setScriptPath(Component cmp, const string& str) = 0;

	protected:
		ScriptSystem() {}
		~ScriptSystem() {}
};


} // ~namespace Lumix
