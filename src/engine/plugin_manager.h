#pragma once


#include "core/lux.h"


namespace Lux
{

	class Engine;
	class IPlugin;
	class ISerializer;
	class Universe;

	class LUX_ENGINE_API PluginManager
	{
		public:
			PluginManager() { m_impl = 0; }

			bool create(Engine& engine);
			void destroy();
			IPlugin* load(const char* path);
			void addPlugin(IPlugin* plugin);
			void update(float dt);
			void serialize(ISerializer& serializer);
			void deserialize(ISerializer& serializer);
			void onDestroyUniverse(Universe& universe);
			void onCreateUniverse(Universe& universe);
			IPlugin* getPlugin(const char* name);

		private:
			struct PluginManagerImpl* m_impl;
	};


} // ~namespace Lux