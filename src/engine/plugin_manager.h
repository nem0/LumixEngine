#pragma once


#include "core/lumix.h"
#include "core/array.h"


namespace Lumix
{

	class Engine;
	class IPlugin;
	class JsonSerializer;
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
			void serialize(JsonSerializer& serializer);
			void deserialize(JsonSerializer& serializer);
			IPlugin* getPlugin(const char* name);
			const Array<IPlugin*>& getPlugins() const;

		private:
			struct PluginManagerImpl* m_impl;
	};


} // ~namespace Lumix
