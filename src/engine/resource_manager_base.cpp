#include "engine/lumix.h"
#include "engine/resource_manager_base.h"
#include "engine/crc32.h"
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
			ASSERT(resource->isEmpty());
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

	void ResourceManagerBase::remove(Resource* resource)
	{
		ASSERT(resource->isEmpty());
		m_resources.erase(resource->getPath().getHash());
		resource->remRef();
	}

	void ResourceManagerBase::add(Resource* resource)
	{
		ASSERT(resource && resource->isReady());
		m_resources.insert(resource->getPath().getHash(), resource);
		resource->addRef();
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
		if(nullptr != resource)
		{
			unload(*resource);
		}
	}

	void ResourceManagerBase::unload(Resource& resource)
	{
		int new_ref_count = resource.remRef();
		ASSERT(new_ref_count >= 0);
		if(new_ref_count == 0)
		{
			resource.doUnload();
		}
	}

	void ResourceManagerBase::forceUnload(const Path& path)
	{
		Resource* resource = get(path);
		if(nullptr != resource)
		{
			forceUnload(*resource);
		}
	}

	void ResourceManagerBase::forceUnload(Resource& resource)
	{
		resource.doUnload();
		resource.m_ref_count = 0;
	}

	void ResourceManagerBase::reload(const Path& path)
	{
		Resource* resource = get(path);
		if(nullptr != resource)
		{
			reload(*resource);
		}
	}

	void ResourceManagerBase::reload(Resource& resource)
	{
		resource.doUnload();
		resource.doLoad();
	}

	ResourceManagerBase::ResourceManagerBase(IAllocator& allocator)
		: m_size(0)
		, m_resources(allocator)
		, m_allocator(allocator)
		, m_owner(nullptr)
	{ }

	ResourceManagerBase::~ResourceManagerBase()
	{
		ASSERT(m_resources.empty());
	}
}
