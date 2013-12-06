#pragma once


#include "core/lux.h"
#include "universe/component.h"


namespace Lux
{
	class Engine;
	class ISerializer;
	class Universe;


	class LUX_ENGINE_API IPlugin LUX_ABSTRACT
	{
		public:
			virtual ~IPlugin();

			virtual bool create(Engine& engine) = 0;
			virtual void onCreateUniverse(Universe&) {}
			virtual void onDestroyUniverse(Universe&) {}
			virtual void serialize(ISerializer&) {}
			virtual void deserialize(ISerializer&) {}
			virtual void update(float) {}
			virtual Component createComponent(uint32_t, const Entity&) = 0;
			virtual const char* getName() const = 0;
			virtual void sendMessage(const char* message) {};
	};


};