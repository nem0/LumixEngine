#pragma once


#include "core/lumix.h"
#include "core/array.h"


namespace Lumix
{

	class Engine;
	class InputBlob;
	class IPlugin;
	class JsonSerializer;
	class OutputBlob;
	class Universe;

	class LUMIX_ENGINE_API PluginManager
	{
		public:
			PluginManager() { m_impl = 0; }

			bool create(Engine& engine);
			void destroy();
			IPlugin* load(const char* path);
			void addPlugin(IPlugin* plugin);
			void update(float dt);
			void serialize(OutputBlob& serializer);
			void deserialize(InputBlob& serializer);
			IPlugin* getPlugin(const char* name);
			const Array<IPlugin*>& getPlugins() const;

		private:
			struct PluginManagerImpl* m_impl;
	};


} // ~namespace Lumix
