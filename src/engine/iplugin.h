#pragma once


#include "lumix.h"


namespace Lumix
{
	class Engine;
	class InputBlob;
	class IPlugin;
	class OutputBlob;
	class Universe;
	struct UniverseContext;


	class LUMIX_ENGINE_API IScene
	{
		public:
			virtual ~IScene() {}

			virtual ComponentIndex createComponent(uint32, Entity) = 0;
			virtual void destroyComponent(ComponentIndex component, uint32 type) = 0;
			virtual void serialize(OutputBlob& serializer) = 0;
			virtual void deserialize(InputBlob& serializer, int version) = 0;
			virtual IPlugin& getPlugin() const = 0;
			virtual void update(float time_delta, bool paused) = 0;
			virtual bool ownComponentType(uint32 type) const = 0;
			virtual ComponentIndex getComponent(Entity entity, uint32 type) = 0;
			virtual Universe& getUniverse() = 0;
			virtual void startGame() {}
			virtual void stopGame() {}
			virtual int getVersion() const { return -1; }
			virtual void sendMessage(uint32 /*type*/, void* /*message*/) {}
	};


	class LUMIX_ENGINE_API IPlugin
	{
		public:
			virtual ~IPlugin();

			virtual bool create() = 0;
			virtual void destroy() = 0;
			virtual void serialize(OutputBlob&) {}
			virtual void deserialize(InputBlob&) {}
			virtual void update(float) {}
			virtual const char* getName() const = 0;
			virtual void sendMessage(const char*) {}

			virtual IScene* createScene(UniverseContext&) { return nullptr; }
			virtual void destroyScene(IScene*) { ASSERT(false); }
	};


};