#pragma once


#include "lumix.h"


namespace Lumix
{
	class Engine;
	class InputBlob;
	class IPlugin;
	class OutputBlob;
	class Universe;
	class Universe;


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

			virtual IScene* createScene(Universe&) { return nullptr; }
			virtual void destroyScene(IScene*) { ASSERT(false); }
	};


	struct LUMIX_ENGINE_API StaticPluginRegister
	{
		typedef IPlugin* (*Creator)(Engine& engine);
		StaticPluginRegister(const char* name, Creator creator);
		
		static IPlugin* create(const char* name, Engine& engine);

		StaticPluginRegister* next;
		Creator creator;
		const char* name;
	};


} // namespace Lumix


#ifdef STATIC_PLUGINS
	#define LUMIX_PLUGIN_ENTRY(plugin_name)                                           \
		extern "C" Lumix::IPlugin* createPlugin_##plugin_name(Lumix::Engine& engine); \
		extern "C" StaticPluginRegister s_##plugin_name##_plugin_register(            \
			#plugin_name, createPlugin_##plugin_name);                                \
		extern "C" Lumix::IPlugin* createPlugin_##plugin_name(Lumix::Engine& engine)
#else
	#define LUMIX_PLUGIN_ENTRY(plugin_name) \
		extern "C" LUMIX_LIBRARY_EXPORT Lumix::IPlugin* createPlugin(Lumix::Engine& engine)
#endif

