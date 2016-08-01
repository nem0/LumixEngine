#pragma once


#include "engine/lumix.h"


namespace Lumix
{

	class Engine;
	class InputBlob;
	class IPlugin;
	class JsonSerializer;
	class OutputBlob;
	class Universe;
	template <typename T> class Array;
	template <typename T> class DelegateList;


	class LUMIX_ENGINE_API PluginManager
	{
		public:
			virtual ~PluginManager() {}

			static PluginManager* create(Engine& engine);
			static void destroy(PluginManager* manager);
			
			virtual IPlugin* load(const char* path) = 0;
			virtual void addPlugin(IPlugin* plugin) = 0;
			virtual void update(float dt, bool paused) = 0;
			virtual void serialize(OutputBlob& serializer) = 0;
			virtual void deserialize(InputBlob& serializer) = 0;
			virtual IPlugin* getPlugin(const char* name) = 0;
			virtual const Array<IPlugin*>& getPlugins() const = 0;
			virtual const Array<void*>& getLibraries() const = 0;
			virtual DelegateList<void(void*)>& libraryLoaded() = 0;
	};


} // ~namespace Lumix
