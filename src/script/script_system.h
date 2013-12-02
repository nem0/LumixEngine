#pragma once


#include "core/lux.h"
#include "core/string.h"
#include "universe/universe.h"


namespace Lux
{


class Renderer;
class Navigation;
class InputSystem;
class EntityNamesMap;


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
		void reloadScript(const char* path);
		Renderer* getRenderer() const;
		void setRenderer(Renderer* renderer);
		void setInputSystem(InputSystem* input_system);
		InputSystem* getInputSystem() const;
		
		void setEntityNamesMap(EntityNamesMap* names_map);
		Entity getEntityByName(const char* entity_name) const;

		void deserialize(ISerializer& serializer);
		void serialize(ISerializer& serializer);

		void getScriptPath(Component cmp, string& str);
		void setScriptPath(Component cmp, const string& str);

	private:
		struct ScriptSystemImpl* m_impl;
};


} // ~namespace Lux