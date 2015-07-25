#include "engine/plugin_manager.h"
#include "core/library.h"
#include "core/log.h"
#include "core/array.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include <Windows.h>


namespace Lumix 
{


class PluginManagerImpl : public PluginManager
{
	private:
		typedef Array<IPlugin*> PluginList;
		typedef Array<Library*> LibraryList;


	public:
		PluginManagerImpl(Engine& engine, IAllocator& allocator)
			: m_plugins(allocator)
			, m_libraries(allocator)
			, m_allocator(allocator)
			, m_engine(engine)
		{ }


		~PluginManagerImpl()
		{
			for (int i = 0; i < m_plugins.size(); ++i)
			{
				m_plugins[i]->destroy();
				m_engine.getAllocator().deleteObject(m_plugins[i]);
			}

			for (int i = 0; i < m_libraries.size(); ++i)
			{
				Library::destroy(m_libraries[i]);
			}
		}


		void update(float dt) override
		{
			for (int i = 0, c = m_plugins.size(); i < c; ++i)
			{
				m_plugins[i]->update(dt);
			}
		}


		void serialize(OutputBlob& serializer) override
		{
			for (int i = 0, c = m_plugins.size(); i < c; ++i)
			{
				m_plugins[i]->serialize(serializer);
			}
		}


		void deserialize(InputBlob& serializer) override
		{
			for (int i = 0, c = m_plugins.size(); i < c; ++i)
			{
				m_plugins[i]->deserialize(serializer);
			}
		}


		const Array<IPlugin*>& getPlugins() const override
		{
			return m_plugins;
		}


		IPlugin* getPlugin(const char* name) override
		{
			for (int i = 0; i < m_plugins.size(); ++i)
			{
				if (strcmp(m_plugins[i]->getName(), name) == 0)
				{
					return m_plugins[i];
				}
			}
			return 0;
		}


		IPlugin* load(const char* path) override
		{
			g_log_info.log("plugins") << "loading plugin " << path;
			typedef IPlugin* (*PluginCreator)(Engine&);

			Library* lib = Library::create(Path(path), m_engine.getAllocator());
			if (lib->load())
			{
				PluginCreator creator = (PluginCreator)lib->resolve("createPlugin");
				if (creator)
				{
					IPlugin* plugin = creator(m_engine);
					if (!plugin->create())
					{
						m_engine.getAllocator().deleteObject(plugin);
						ASSERT(false);
						return nullptr;
					}
					m_plugins.push(plugin);
					m_libraries.push(lib);
					g_log_info.log("plugins") << "plugin loaded";
					return plugin;
				}
			}
			Library::destroy(lib);
			return 0;
		}


		IAllocator& getAllocator() { return m_allocator; }


		void addPlugin(IPlugin* plugin) override
		{
			m_plugins.push(plugin);
		}


	private:
		Engine& m_engine;
		LibraryList m_libraries;
		PluginList m_plugins;
		IAllocator& m_allocator;
};
	

PluginManager* PluginManager::create(Engine& engine)
{
	return engine.getAllocator().newObject<PluginManagerImpl>(engine, engine.getAllocator());
}


void PluginManager::destroy(PluginManager* manager)
{
	static_cast<PluginManagerImpl*>(manager)->getAllocator().deleteObject(manager);
}


} // ~namespace Lumix
