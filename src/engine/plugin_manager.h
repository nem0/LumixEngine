#pragma once


#include "lumix.h"
#include "core/array.h"
#include "core/delegate_list.h"


namespace Lumix
{

	class Engine;
	class InputBlob;
	class IPlugin;
	class JsonSerializer;
	class Library;
	class OutputBlob;
	class Universe;

	class LUMIX_ENGINE_API PluginManager
	{
		public:
			virtual ~PluginManager() {}

			static PluginManager* create(Engine& engine);
			static void destroy(PluginManager* manager);
			
			virtual IPlugin* load(const char* path) = 0;
			virtual void addPlugin(IPlugin* plugin) = 0;
			virtual void update(float dt) = 0;
			virtual void serialize(OutputBlob& serializer) = 0;
			virtual void deserialize(InputBlob& serializer) = 0;
			virtual IPlugin* getPlugin(const char* name) = 0;
			virtual const Array<IPlugin*>& getPlugins() const = 0;
			virtual const Array<Library*>& getLibraries() const = 0;
			virtual DelegateList<void(Library&)>& libraryLoaded() = 0;
	};


} // ~namespace Lumix
