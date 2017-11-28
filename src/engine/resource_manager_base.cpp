#include "engine/resource_manager_base.h"
#include "engine/log.h"
#include "engine/lumix.h"
#include "engine/path.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"


namespace Lumix
{


void ResourceManagerBase::LoadHook::continueLoad(Resource& resource)
{
	ASSERT(resource.isEmpty());
	resource.remRef(); // release from hook
	resource.m_desired_state = Resource::State::EMPTY;
	resource.doLoad();
}


void ResourceManagerBase::create(ResourceType type, ResourceManager& owner)
{
	owner.add(type, this);
	m_owner = &owner;
}

void ResourceManagerBase::destroy()
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

Resource* ResourceManagerBase::get(const Path& path)
{
	ResourceTable::iterator it = m_resources.find(path.getHash());

	if(m_resources.end() != it)
	{
		return *it;
	}

	return nullptr;
}

Resource* ResourceManagerBase::load(const Path& path)
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
		if (m_load_hook && m_load_hook->onBeforeLoad(*resource))
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

void ResourceManagerBase::removeUnreferenced()
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

void ResourceManagerBase::load(Resource& resource)
{
	if(resource.isEmpty() && resource.m_desired_state == Resource::State::EMPTY)
	{
		if (m_load_hook && m_load_hook->onBeforeLoad(resource))
		{
			resource.addRef(); // for hook
			resource.addRef(); // for return value
			return;
		}
		resource.doLoad();
	}

	resource.addRef();
}

void ResourceManagerBase::unload(const Path& path)
{
	Resource* resource = get(path);
	if (resource) unload(*resource);
}

void ResourceManagerBase::unload(Resource& resource)
{
	int new_ref_count = resource.remRef();
	ASSERT(new_ref_count >= 0);
	if(new_ref_count == 0 && m_is_unload_enabled)
	{
		resource.doUnload();
	}
}

void ResourceManagerBase::reload(const Path& path)
{
	Resource* resource = get(path);
	if(resource) reload(*resource);
}

void ResourceManagerBase::reload(Resource& resource)
{
	resource.doUnload();
	resource.doLoad();
}

void ResourceManagerBase::enableUnload(bool enable)
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

void ResourceManagerBase::setLoadHook(LoadHook& load_hook)
{
	ASSERT(!m_load_hook);
	m_load_hook = &load_hook;
}

ResourceManagerBase::ResourceManagerBase(IAllocator& allocator)
	: m_resources(allocator)
	, m_allocator(allocator)
	, m_owner(nullptr)
	, m_is_unload_enabled(true)
	, m_load_hook(nullptr)
{ }

ResourceManagerBase::~ResourceManagerBase()
{
	ASSERT(m_resources.empty());
}


} // namespace Lumix
