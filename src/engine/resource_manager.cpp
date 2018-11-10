#include "engine/log.h"
#include "engine/lumix.h"
#include "engine/path.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"


namespace Lumix
{


void ResourceManager::create(ResourceType type, ResourceManagerHub& owner)
{
	owner.add(type, this);
	m_owner = &owner;
}

void ResourceManager::destroy()
{
	for (auto iter = m_resources.begin(), end = m_resources.end(); iter != end; ++iter)
	{
		Resource* resource = iter.value();
		if (!resource->isEmpty())
		{
			g_log_error.log("Engine") << "Leaking resource " << resource->getPath().c_str() << "\n";
		}
		destroyResource(*resource);
	}
	m_resources.clear();
}

Resource* ResourceManager::get(const Path& path)
{
	ResourceTable::iterator it = m_resources.find(path.getHash());

	if(m_resources.end() != it)
	{
		return *it;
	}

	return nullptr;
}

Resource* ResourceManager::load(const Path& path)
{
	if (!path.isValid()) return nullptr;
	Resource* resource = get(path);

	if(nullptr == resource)
	{
		resource = createResource(path);
		m_resources.insert(path.getHash(), resource);
	}

	if(resource->isEmpty() && resource->m_desired_state == Resource::State::EMPTY)
	{
		if (m_owner->onBeforeLoad(*resource))
		{
			resource->m_desired_state = Resource::State::READY;
			resource->addRef(); // for hook
			resource->addRef(); // for return value
			return resource;
		}
		resource->doLoad();
	}

	resource->addRef();
	return resource;
}

void ResourceManager::removeUnreferenced()
{
	if (!m_is_unload_enabled) return;

	Array<Resource*> to_remove(m_allocator);
	for (auto* i : m_resources)
	{
		if (i->getRefCount() == 0) to_remove.push(i);
	}

	for (auto* i : to_remove)
	{
		m_resources.erase(i->getPath().getHash());
		destroyResource(*i);
	}
}

void ResourceManager::load(Resource& resource)
{
	if(resource.isEmpty() && resource.m_desired_state == Resource::State::EMPTY)
	{
		if (m_owner->onBeforeLoad(resource))
		{
			resource.addRef(); // for hook
			return;
		}
		resource.doLoad();
	}

	resource.addRef();
}

void ResourceManager::unload(const Path& path)
{
	Resource* resource = get(path);
	if (resource) unload(*resource);
}

void ResourceManager::unload(Resource& resource)
{
	int new_ref_count = resource.remRef();
	ASSERT(new_ref_count >= 0);
	if(new_ref_count == 0 && m_is_unload_enabled)
	{
		resource.doUnload();
	}
}

void ResourceManager::reload(const Path& path)
{
	Resource* resource = get(path);
	if(resource) reload(*resource);
}

void ResourceManager::reload(Resource& resource)
{
	resource.doUnload();
	if (m_owner->onBeforeLoad(resource))
	{
		resource.m_desired_state = Resource::State::READY;
		resource.addRef(); // for hook
		resource.addRef(); // for return value
	}
	else {
		resource.doLoad();
	}
}

void ResourceManager::enableUnload(bool enable)
{
	m_is_unload_enabled = enable;
	if (!enable) return;

	for (auto* resource : m_resources)
	{
		if (resource->getRefCount() == 0)
		{
			resource->doUnload();
		}
	}
}

ResourceManager::ResourceManager(IAllocator& allocator)
	: m_resources(allocator)
	, m_allocator(allocator)
	, m_owner(nullptr)
	, m_is_unload_enabled(true)
{ }

ResourceManager::~ResourceManager()
{
	ASSERT(m_resources.empty());
}

ResourceManagerHub::ResourceManagerHub(IAllocator& allocator) 
	: m_resource_managers(allocator)
	, m_allocator(allocator)
	, m_load_hook(nullptr)
	, m_file_system(nullptr)
{
}

ResourceManagerHub::~ResourceManagerHub() = default;


void ResourceManagerHub::init(FS::FileSystem& fs)
{
	m_file_system = &fs;
}

Resource* ResourceManagerHub::load(ResourceType type, const Path& path)
{
	ResourceManager* manager = get(type);
	if(!manager) return nullptr;
	return load(*manager, path);
}
	
Resource* ResourceManagerHub::load(ResourceManager& manager, const Path& path)
{
	return manager.load(path);
}

ResourceManager* ResourceManagerHub::get(ResourceType type)
{
	return m_resource_managers[type.type]; 
}

void ResourceManagerHub::LoadHook::continueLoad(Resource& resource)
{
	ASSERT(resource.isEmpty());
	resource.remRef(); // release from hook
	resource.m_desired_state = Resource::State::EMPTY;
	resource.doLoad();
}

void ResourceManagerHub::setLoadHook(LoadHook* hook)
{
	ASSERT(!m_load_hook || !hook);
	m_load_hook = hook;
}

bool ResourceManagerHub::onBeforeLoad(Resource& resource) const
{
	return m_load_hook ? m_load_hook->onBeforeLoad(resource) : false;
}

void ResourceManagerHub::add(ResourceType type, ResourceManager* rm)
{ 
	m_resource_managers.insert(type.type, rm);
}

void ResourceManagerHub::remove(ResourceType type)
{ 
	m_resource_managers.erase(type.type);
}

void ResourceManagerHub::removeUnreferenced()
{
	for (auto* manager : m_resource_managers)
	{
		manager->removeUnreferenced();
	}
}

void ResourceManagerHub::enableUnload(bool enable)
{
	for (auto* manager : m_resource_managers)
	{
		manager->enableUnload(enable);
	}
}

void ResourceManagerHub::reload(const Path& path)
{
	for (auto* manager : m_resource_managers)
	{
		manager->reload(path);
	}
}

} // namespace Lumix
