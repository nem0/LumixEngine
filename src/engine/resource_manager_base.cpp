#include "engine/resource_manager_base.h"
#include "engine/crc32.h"
#include "engine/log.h"
#include "engine/lumix.h"
#include "engine/path.h"
#include "engine/path_utils.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"


namespace Lumix
{
	void ResourceManagerBase::create(ResourceType type, ResourceManager& owner)
	{
		owner.add(type, this);
		m_owner = &owner;
	}

	void ResourceManagerBase::destroy(void)
	{
		for (auto iter = m_resources.begin(), end = m_resources.end(); iter != end; ++iter)
		{
			Resource* resource = iter.value();
			if (!resource->isEmpty())
			{
				g_log_error.log("Engine") << "Leaking resource " << resource->getPath().c_str();
				ASSERT(false);
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
		Resource* resource = get(path);

		if(nullptr == resource)
		{
			resource = createResource(path);
			m_resources.insert(path.getHash(), resource);
		}
		
		if(resource->isEmpty())
		{
			resource->doLoad();
		}

		resource->addRef();
		return resource;
	}

	void ResourceManagerBase::removeUnreferenced()
	{
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
		if(resource.isEmpty())
		{
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

	ResourceManagerBase::ResourceManagerBase(IAllocator& allocator)
		: m_size(0)
		, m_resources(allocator)
		, m_allocator(allocator)
		, m_owner(nullptr)
		, m_is_unload_enabled(true)
	{ }

	ResourceManagerBase::~ResourceManagerBase()
	{
		ASSERT(m_resources.empty());
	}
}
