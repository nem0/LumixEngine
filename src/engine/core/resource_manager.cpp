#include "lumix.h"
#include "core/path.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"

namespace Lumix
{
	ResourceManager::ResourceManager(IAllocator& allocator) 
		: m_resource_managers(allocator)
		, m_allocator(allocator)
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
	
	ResourceManagerBase* ResourceManager::get(uint32 id)
	{
		return m_resource_managers[id]; 
	}

	void ResourceManager::add(uint32 id, ResourceManagerBase* rm)
	{ 
		m_resource_managers.insert(id, rm);
	}

	void ResourceManager::remove(uint32 id) 
	{ 
		m_resource_managers.erase(id); 
	}

	void ResourceManager::removeUnreferenced()
	{
		for (auto* i : m_resource_managers)
		{
			i->removeUnreferenced();
		}
	}

	void ResourceManager::reload(const Path& path)
	{
		for (auto iter = m_resource_managers.begin(), end = m_resource_managers.end(); iter != end; ++iter)
		{
			iter.value()->reload(Path(path));
		}
	}
}