#include "core/lux.h"
#include "core/resource_manager_base.h"

#include "core/crc32.h"
#include "core/path.h"
#include "core/path_utils.h"
#include "core/resource.h"

namespace Lux
{
	void ResourceManagerBase::create(FS::FileSystem& fs)
	{
		m_file_system = &fs;
	}

	void ResourceManagerBase::destroy(void)
	{ }

	Resource* ResourceManagerBase::get(const Path& path)
	{
		ResourceTable::iterator it = m_resources.find(path);

		if(m_resources.end() != it)
		{
			return *it;
		}

		return NULL;
	}

	Resource* ResourceManagerBase::load(const Path& path)
	{
		Resource* resource = get(path);

		if(NULL == resource)
		{
			resource = createResource(path);
			resource->onLoading();
			resource->doLoad();
		}

		return resource;
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
		resource.onUnloading();
		resource.doUnload();
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
		resource.onReloading();
		resource.doReload();
	}

	void ResourceManagerBase::releaseAll(void)
	{
		for(ResourceTable::iterator it = m_resources.begin(); m_resources.end() != it; ++it)
		{
			ASSERT((*it)->m_ref_count == 0);
			LUX_DELETE(*it);
		}
	}

	ResourceManagerBase::ResourceManagerBase()
		: m_size(0)
		, m_file_system(NULL)
	{ }

	ResourceManagerBase::~ResourceManagerBase()
	{ }
}