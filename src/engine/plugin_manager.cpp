#include "engine/plugin_manager.h"
#include "core/log.h"
#include "core/array.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include <Windows.h>


namespace Lumix 
{
	struct PluginManagerImpl
	{
		typedef Array<IPlugin*> PluginList;

		PluginManagerImpl(IAllocator& allocator)
			: m_plugins(allocator)
		{ }

		Engine* m_engine;
		PluginList m_plugins;
	};


	void PluginManager::update(float dt)
	{
		PluginManagerImpl::PluginList& plugins = m_impl->m_plugins;
		for(int i = 0, c = plugins.size(); i < c; ++i)
		{
			plugins[i]->update(dt);
		}
	}


	void PluginManager::serialize(OutputBlob& serializer)
	{
		PluginManagerImpl::PluginList& plugins = m_impl->m_plugins;
		for(int i = 0, c = plugins.size(); i < c; ++i)
		{
			plugins[i]->serialize(serializer);
		}
	}


	void PluginManager::deserialize(InputBlob& serializer)
	{
		PluginManagerImpl::PluginList& plugins = m_impl->m_plugins;
		for(int i = 0, c = plugins.size(); i < c; ++i)
		{
			plugins[i]->deserialize(serializer);
		}
	}

	const Array<IPlugin*>& PluginManager::getPlugins() const
	{
		return m_impl->m_plugins;
	}

	IPlugin* PluginManager::getPlugin(const char* name)
	{
		for(int i = 0; i < m_impl->m_plugins.size(); ++i)
		{
			if(strcmp(m_impl->m_plugins[i]->getName(), name) == 0)
			{
				return m_impl->m_plugins[i];
			}
		}
		return 0;
	}

	IPlugin* PluginManager::load(const char* path)
	{
		g_log_info.log("plugins") << "loading plugin " << path;
		typedef IPlugin* (*PluginCreator)(Engine&);
		HMODULE lib = LoadLibrary(TEXT(path));
		if(lib)
		{
			PluginCreator creator = (PluginCreator)GetProcAddress(lib, TEXT("createPlugin"));
			if(creator)
			{
				IPlugin* plugin = creator(*m_impl->m_engine);
				if(!plugin->create())
				{
					m_impl->m_engine->getAllocator().deleteObject(plugin);
					ASSERT(false);
					return NULL;
				}
				m_impl->m_plugins.push(plugin);
				g_log_info.log("plugins") << "plugin loaded";
				return plugin;
			}
		}
		return 0;
	}


	void PluginManager::addPlugin(IPlugin* plugin)
	{
		m_impl->m_plugins.push(plugin);
	}

	
	bool PluginManager::create(Engine& engine)
	{
		m_impl = engine.getAllocator().newObject<PluginManagerImpl>(engine.getAllocator());
		m_impl->m_engine = &engine;
		return true;
	}


	void PluginManager::destroy()
	{
		for(int i = 0; i < m_impl->m_plugins.size(); ++i)
		{
			m_impl->m_plugins[i]->destroy();
			m_impl->m_engine->getAllocator().deleteObject(m_impl->m_plugins[i]);
		}
		m_impl->m_engine->getAllocator().deleteObject(m_impl);
		m_impl = NULL;
	}


} // ~namespace Lumix
