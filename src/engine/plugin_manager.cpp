#include "engine/plugin_manager.h"
#include "engine/array.h"
#include "engine/log.h"
#include "engine/profiler.h"
#include "engine/system.h"
#include "engine/debug/debug.h"
#include "engine/engine.h"
#include "engine/iplugin.h"


namespace Lumix 
{


class PluginManagerImpl LUMIX_FINAL : public PluginManager
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
				LUMIX_DELETE(m_engine.getAllocator(), m_plugins[i]);
			}

			for (int i = 0; i < m_libraries.size(); ++i)
			{
				unloadLibrary(m_libraries[i]);
			}
		}


		void update(float dt, bool paused) override
		{
			PROFILE_FUNCTION();
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
				if (equalStrings(m_plugins[i]->getName(), name))
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
			char path_with_ext[MAX_PATH_LENGTH];
			copyString(path_with_ext, path);
			#ifdef _WIN32
				catString(path_with_ext, ".dll");
			#elif defined __linux__
				catString(path_with_ext, ".so");
			#else 
				#error Unknown platform
			#endif
			g_log_info.log("Core") << "loading plugin " << path_with_ext;
			typedef IPlugin* (*PluginCreator)(Engine&);
			auto* lib = loadLibrary(path_with_ext);
			if (lib)
			{
				PluginCreator creator = (PluginCreator)getLibrarySymbol(lib, "createPlugin");
				if (creator)
				{
					IPlugin* plugin = creator(m_engine);
					if (!plugin)
					{
						g_log_error.log("Core") << "createPlugin failed.";
						LUMIX_DELETE(m_engine.getAllocator(), plugin);
						ASSERT(false);
					}
					else
					{
						addPlugin(plugin);
						m_libraries.push(lib);
						m_library_loaded.invoke(lib);
						g_log_info.log("Core") << "Plugin loaded.";
						Lumix::Debug::StackTree::refreshModuleList();
						return plugin;
					}
				}
				else
				{
					g_log_error.log("Core") << "No createPlugin function in plugin.";
				}
				unloadLibrary(lib);
			}
			else
			{
				auto* plugin = StaticPluginRegister::create(path, m_engine);
				if (plugin)
				{
					g_log_info.log("Core") << "Plugin loaded.";
					addPlugin(plugin);
					return plugin;
				}
				g_log_warning.log("Core") << "Failed to load plugin.";
			}
			return nullptr;
		}


		IAllocator& getAllocator() { return m_allocator; }


		void addPlugin(IPlugin* plugin) override
		{
			m_plugins.push(plugin);
			for (auto* i : m_plugins)
			{
				i->pluginAdded(*plugin);
				plugin->pluginAdded(*i);
			}
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
