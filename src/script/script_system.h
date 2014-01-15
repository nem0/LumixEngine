#pragma once


#include "core/lux.h"
#include "core/string.h"
#include "universe/universe.h"


namespace Lux
{


class Engine;
class InputSystem;
class Navigation;
class Renderer;


class LUX_ENGINE_API ScriptSystem
{
	public:
		ScriptSystem();
		~ScriptSystem();

		void start();
		void stop();
		void update(float time_delta);
		void setUniverse(Universe* universe);
		Universe* getUniverse() const;
		Component createScript(Entity entity);
		void setEngine(Engine& engine);
		Engine* getEngine() const;

		void deserialize(ISerializer& serializer);
		void serialize(ISerializer& serializer);

		void getScriptPath(Component cmp, string& str);
		void setScriptPath(Component cmp, const string& str);

	private:
		struct ScriptSystemImpl* m_impl;
};


} // ~namespace Lux