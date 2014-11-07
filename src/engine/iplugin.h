#pragma once


#include "core/lumix.h"
#include "universe/component.h"


namespace Lumix
{
	class Engine;
	class IPlugin;
	class ISerializer;
	class Universe;


	class LUMIX_ENGINE_API IScene abstract
	{
		public:
			virtual ~IScene() {}

			virtual Component createComponent(uint32_t, const Entity&) = 0;
			virtual void destroyComponent(const Component& component) = 0;
			virtual void serialize(ISerializer& serializer) = 0;
			virtual void deserialize(ISerializer& serializer) = 0;
			virtual IPlugin& getPlugin() const = 0;
			virtual void update(float time_delta) = 0;
	};


	class LUMIX_ENGINE_API IPlugin abstract
	{
		public:
			virtual ~IPlugin();

			virtual bool create(Engine& engine) = 0;
			virtual void destroy() = 0;
			virtual void serialize(ISerializer&) {}
			virtual void deserialize(ISerializer&) {}
			virtual void update(float) {}
			virtual const char* getName() const = 0;
			virtual void sendMessage(const char*) {};

			virtual IScene* createScene(Universe&) { return NULL; }
			virtual void destroyScene(IScene*) { ASSERT(false); }
	};


};