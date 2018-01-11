#pragma once


#include "engine/lumix.h"


namespace Lumix
{
	struct IDeserializer;
	class Engine;
	class InputBlob;
	struct IPlugin;
	struct ISerializer;
	class OutputBlob;
	class Universe;
	class Universe;


	struct LUMIX_ENGINE_API IScene
	{
		virtual ~IScene() {}

		virtual void serialize(OutputBlob& serializer) = 0;
		virtual void serialize(ISerializer& serializer) {}
		virtual void deserialize(IDeserializer& serializer) {}
		virtual void deserialize(InputBlob& serializer) = 0;
		virtual IPlugin& getPlugin() const = 0;
		virtual void update(float time_delta, bool paused) = 0;
		virtual void lateUpdate(float time_delta, bool paused) {}
		virtual ComponentHandle getComponent(Entity entity, ComponentType type) = 0;
		virtual Universe& getUniverse() = 0;
		virtual void startGame() {}
		virtual void stopGame() {}
		virtual int getVersion() const { return -1; }
		virtual void clear() = 0;
	};


	struct LUMIX_ENGINE_API IPlugin
	{
		virtual ~IPlugin();

		virtual void serialize(OutputBlob&) {}
		virtual void deserialize(InputBlob&) {}
		virtual void update(float) {}
		virtual const char* getName() const = 0;
		virtual void pluginAdded(IPlugin& plugin) {}

		virtual void createScenes(Universe&) {}
		virtual void destroyScene(IScene*) { ASSERT(false); }
		virtual void startGame() {}
		virtual void stopGame() {}
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
		extern "C" IPlugin* createPlugin_##plugin_name(Engine& engine); \
		extern "C" { StaticPluginRegister LUMIX_ATTRIBUTE_USED s_##plugin_name##_plugin_register(          \
			#plugin_name, createPlugin_##plugin_name); }                              \
		extern "C" IPlugin* createPlugin_##plugin_name(Engine& engine)
#else
	#define LUMIX_PLUGIN_ENTRY(plugin_name) \
		extern "C" LUMIX_LIBRARY_EXPORT IPlugin* createPlugin(Engine& engine)
#endif

