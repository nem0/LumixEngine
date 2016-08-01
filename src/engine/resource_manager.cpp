#include "engine/lumix.h"
#include "engine/path.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"

namespace Lumix
{
	ResourceManager::ResourceManager(IAllocator& allocator) 
		: m_resource_managers(allocator)
		, m_allocator(allocator)
		, m_file_system(nullptr)
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
	
	ResourceManagerBase* ResourceManager::get(ResourceType type)
	{
		return m_resource_managers[type.type]; 
	}

	void ResourceManager::add(ResourceType type, ResourceManagerBase* rm)
	{ 
		m_resource_managers.insert(type.type, rm);
	}

	void ResourceManager::remove(ResourceType type)
	{ 
		m_resource_managers.erase(type.type);
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
