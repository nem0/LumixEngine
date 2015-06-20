#include "core/lumix.h"
#include "core/resource_manager_base.h"

#include "core/crc32.h"
#include "core/path.h"
#include "core/path_utils.h"
#include "core/resource.h"
#include "core/resource_manager.h"

namespace Lumix
{
	void ResourceManagerBase::create(uint32_t id, ResourceManager& owner)
	{
		owner.add(id, this);
		m_owner = &owner;
	}

	void ResourceManagerBase::destroy(void)
	{ 
		for (ResourceTable::iterator iter = m_resources.begin(), end = m_resources.end(); iter != end; ++iter)
		{
			ASSERT(iter.value()->isEmpty());
			destroyResource(*iter.value());
		}

	}

	Resource* ResourceManagerBase::get(const Path& path)
	{
		ResourceTable::iterator it = m_resources.find(path);

		if(m_resources.end() != it)
		{
			return *it;
		}

		return NULL;
	}

	void ResourceManagerBase::remove(Resource* resource)
	{
		ASSERT(resource->isEmpty());
		m_resources.erase(resource->getPath());
		resource->remRef();
	}

	void ResourceManagerBase::add(Resource* resource)
	{
		ASSERT(resource && resource->isReady());
		m_resources.insert(resource->getPath(), resource);
		resource->addRef();
	}

	Resource* ResourceManagerBase::load(const Path& path)
	{
		Resource* resource = get(path);

		if(NULL == resource)
		{
			resource = createResource(path);
			m_resources.insert(path, resource);
		}
		
		if(resource->isEmpty())
		{
			resource->onLoading();
			resource->doLoad();
		}

		resource->addRef();
		return resource;
	}

	void ResourceManagerBase::load(Resource& resource)
	{
		if(resource.isEmpty())
		{
			resource.onLoading();
			resource.doLoad();
		}

		resource.addRef();
	}

	void ResourceManagerBase::unload(const Path& path)
	{
		Resource* resource = get(path);
		if(NULL != resource)
		{
			unload(*resource);
		}
	}

	void ResourceManagerBase::unload(Resource& resource)
	{
		if(0 == resource.remRef())
		{
			if (resource.isReady() || resource.isFailure() || resource.isEmpty())
			{
				resource.incrementDepCount();
			}
			resource.onUnloading();
			resource.doUnload();
		}
	}

	void ResourceManagerBase::forceUnload(const Path& path)
	{
		Resource* resource = get(path);
		if(NULL != resource)
		{
			forceUnload(*resource);
		}
	}

	void ResourceManagerBase::forceUnload(Resource& resource)
	{
		if (resource.isReady() || resource.isFailure() || resource.isEmpty())
		{
			resource.incrementDepCount();
		}
		resource.onUnloading();
		resource.doUnload();
		resource.m_ref_count = 0;
	}

	void ResourceManagerBase::reload(const Path& path)
	{
		Resource* resource = get(path);
		if(NULL != resource)
		{
			reload(*resource);
		}
	}

	void ResourceManagerBase::reload(Resource& resource)
	{
		if (resource.isReady() || resource.isFailure() || resource.isEmpty())
		{
			if (!resource.isFailure())
			{
				resource.incrementDepCount();
			}
			resource.onReloading();
			resource.doUnload();
			resource.onLoading();
			resource.doLoad();
		}
	}

	ResourceManagerBase::ResourceManagerBase(IAllocator& allocator)
		: m_size(0)
		, m_resources(allocator)
	{ }

	ResourceManagerBase::~ResourceManagerBase()
	{ 
	}
}