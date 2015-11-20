#include "plugin_manager.h"
#include "core/array.h"
#include "core/log.h"
#include "core/system.h"
#include "engine.h"
#include "iplugin.h"


namespace Lumix 
{


class PluginManagerImpl : public PluginManager
{
	private:
		typedef Array<IPlugin*> PluginList;
		typedef Array<void*> LibraryList;


	public:
		PluginManagerImpl(Engine& engine, IAllocator& allocator)
			: m_plugins(allocator)
			, m_libraries(allocator)
			, m_allocator(allocator)
			, m_engine(engine)
			, m_library_loaded(allocator)
		{ }


		~PluginManagerImpl()
		{
			for (int i = m_plugins.size() - 1; i >= 0; --i)
			{
				m_plugins[i]->destroy();
				LUMIX_DELETE(m_engine.getAllocator(), m_plugins[i]);
			}

			for (int i = 0; i < m_libraries.size(); ++i)
			{
				unloadLibrary(m_libraries[i]);
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


		const Array<void*>& getLibraries() const override
		{
			return m_libraries;
		}


		const Array<IPlugin*>& getPlugins() const override
		{
			return m_plugins;
		}


		IPlugin* getPlugin(const char* name) override
		{
			for (int i = 0; i < m_plugins.size(); ++i)
			{
				if (compareString(m_plugins[i]->getName(), name) == 0)
				{
					return m_plugins[i];
				}
			}
			return 0;
		}


		DelegateList<void(void*)>& libraryLoaded() override
		{
			return m_library_loaded;
		}


		IPlugin* load(const char* path) override
		{
			g_log_info.log("plugins") << "loading plugin " << path;
			typedef IPlugin* (*PluginCreator)(Engine&);

			auto* lib = loadLibrary(path);
			if (lib)
			{
				PluginCreator creator = (PluginCreator)getLibrarySymbol(lib, "createPlugin");
				if (creator)
				{
					IPlugin* plugin = creator(m_engine);
					if (!plugin || !plugin->create())
					{
						LUMIX_DELETE(m_engine.getAllocator(), plugin);
						ASSERT(false);
						return nullptr;
					}
					m_plugins.push(plugin);
					m_libraries.push(lib);
					m_library_loaded.invoke(lib);
					g_log_info.log("plugins") << "plugin loaded";
					return plugin;
				}
			}
			unloadLibrary(lib);
			return 0;
		}


		IAllocator& getAllocator() { return m_allocator; }


		void addPlugin(IPlugin* plugin) override
		{
			m_plugins.push(plugin);
		}


	private:
		Engine& m_engine;
		DelegateList<void(void*)> m_library_loaded;
		LibraryList m_libraries;
		PluginList m_plugins;
		IAllocator& m_allocator;
};
	

PluginManager* PluginManager::create(Engine& engine)
{
	return LUMIX_NEW(engine.getAllocator(), PluginManagerImpl)(engine, engine.getAllocator());
}


void PluginManager::destroy(PluginManager* manager)
{
	LUMIX_DELETE(static_cast<PluginManagerImpl*>(manager)->getAllocator(), manager);
}


} // ~namespace Lumix
