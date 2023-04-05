#include "engine/array.h"
#include "engine/debug.h"
#include "engine/delegate_list.h"
#include "engine/engine.h"
#include "engine/plugin.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/stream.h"
#include "engine/string.h"

namespace Lumix
{
	#define LUMIX_PLUGIN_DECLS
	#include "plugins.inl"
	#undef LUMIX_PLUGIN_DECLS

	IPlugin::~IPlugin() = default;

	struct PluginManagerImpl final : PluginManager
	{
		public:
			PluginManagerImpl(Engine& engine, IAllocator& allocator)
				: m_plugins(allocator)
				, m_libraries(allocator)
				, m_allocator(allocator)
				, m_engine(engine)
				, m_library_loaded(allocator)
			{}


			~PluginManagerImpl()
			{
				for (int i = m_plugins.size() - 1; i >= 0; --i)
				{
					LUMIX_DELETE(m_engine.getAllocator(), m_plugins[i]);
				}

				for (void* lib : m_libraries)
				{
					os::unloadLibrary(lib);
				}
			}


			void initPlugins() override
			{
				PROFILE_FUNCTION();
				for (int i = 0, c = m_plugins.size(); i < c; ++i)
				{
					m_plugins[i]->init();
				}
			}


			void update(float dt) override
			{
				PROFILE_FUNCTION();
				for (int i = 0, c = m_plugins.size(); i < c; ++i)
				{
					m_plugins[i]->update(dt);
				}
			}


			void* getLibrary(IPlugin* plugin) const override
			{
				int idx = m_plugins.indexOf(plugin);
				if (idx < 0) return nullptr;

				return m_libraries[idx];
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
				for (IPlugin* plugin : m_plugins)
				{
					if (equalStrings(plugin->getName(), name))
					{
						return plugin;
					}
				}
				return nullptr;
			}


			DelegateList<void(void*)>& libraryLoaded() override
			{
				return m_library_loaded;
			}
			

			void unload(IPlugin* plugin) override
			{
				int idx = m_plugins.indexOf(plugin);
				ASSERT(idx >= 0);
				LUMIX_DELETE(m_engine.getAllocator(), m_plugins[idx]);
				os::unloadLibrary(m_libraries[idx]);
				m_libraries.erase(idx);
				m_plugins.erase(idx);
			}


			IPlugin* load(const char* path) override
			{
				char path_with_ext[LUMIX_MAX_PATH];
				copyString(path_with_ext, path);
				const char* ext =
				#ifdef _WIN32
					".dll";
				#elif defined __linux__
					".so";
				#else 
					#error Unknown platform
				#endif
				if (!Path::hasExtension(path, ext + 1)) catString(path_with_ext, ext);
				logInfo("loading plugin ", path_with_ext);
				using PluginCreator = IPlugin* (*)(Engine&);
				auto* lib = os::loadLibrary(path_with_ext);
				if (lib)
				{
					PluginCreator creator = (PluginCreator)os::getLibrarySymbol(lib, "createPlugin");
					if (creator)
					{
						IPlugin* plugin = creator(m_engine);
						if (!plugin)
						{
							logError("createPlugin failed.");
							LUMIX_DELETE(m_engine.getAllocator(), plugin);
							ASSERT(false);
						}
						else
						{
							addPlugin(plugin, lib);
							m_library_loaded.invoke(lib);
							logInfo("Plugin loaded.");
							debug::StackTree::refreshModuleList();
							return plugin;
						}
					}
					else
					{
						logError("No createPlugin function in plugin.");
					}
					os::unloadLibrary(lib);
				}
				else {
					logWarning("Failed to load plugin.");
				}
				return nullptr;
			}


			IAllocator& getAllocator() { return m_allocator; }


			void addPlugin(IPlugin* plugin, void* library) override
			{
				m_plugins.push(plugin);
				m_libraries.push(library);
				for (auto* i : m_plugins)
				{
					i->pluginAdded(*plugin);
					plugin->pluginAdded(*i);
				}
			}


		private:
			Engine& m_engine;
			DelegateList<void(void*)> m_library_loaded;
			Array<void*> m_libraries;
			Array<IPlugin*> m_plugins;
			IAllocator& m_allocator;
	};
		

	UniquePtr<PluginManager> PluginManager::create(Engine& engine) {
		return UniquePtr<PluginManagerImpl>::create(engine.getAllocator(), engine, engine.getAllocator());
	}

	void PluginManager::createAllStatic(Engine& engine) {
		#include "plugins.inl"
		for (IPlugin* plugin : engine.getPluginManager().getPlugins()) {
			logInfo("Plugin ", plugin->getName(), " loaded");
		}
	}
}
