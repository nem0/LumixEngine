#include "core/lux.h"
#include "core/resource_manager.h"

namespace Lux
{
	ResourceManager::ResourceManager() 
	{
	}

	ResourceManager::~ResourceManager()
	{
	}

	void ResourceManager::create(FS::FileSystem& fs)
	{
		m_file_system = &fs;
	}

	void ResourceManager::destroy()
	{
	}

	ResourceManagerBase* ResourceManager::get(uint32_t id)
	{
		return m_resource_managers[id]; 
	}

	void ResourceManager::add(uint32_t id, ResourceManagerBase* rm)
	{ 
		m_resource_managers.insert(id, rm);
	}

	void ResourceManager::remove(uint32_t id) 
	{ 
		m_resource_managers.erase(id); 
	}
}