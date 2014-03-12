#include "core/lux.h"
#include "core/resource.h"

#include "core/file_system.h"
#include "core/path.h"
#include "core/resource_manager.h"

namespace Lux
{
	Resource::Resource(const Path& path, ResourceManager& resource_manager)
		: m_ref_count()
		, m_dep_count(1)
		, m_state(State::EMPTY)
		, m_path(path)
		, m_cb()
		, m_resource_manager(resource_manager)
	{ }

	Resource::~Resource()
	{ }

	void Resource::onEmpty(void)
	{
		m_state = State::EMPTY;
		m_cb.invoke(State::EMPTY);
	}

	void Resource::onLoading(void)
	{
		m_state = State::LOADING;
		m_cb.invoke(State::LOADING);
	}

	void Resource::onReady(void)
	{
		m_state = State::READY;
		m_cb.invoke(State::READY);
	}

	void Resource::onUnloading(void)
	{
		m_state = State::UNLOADING;
		++m_dep_count;
		m_cb.invoke(State::UNLOADING);
	}

	void Resource::onReloading(void)
	{
		m_state = State::UNLOADING;
		m_cb.invoke(State::UNLOADING);
	}

	void Resource::onFailure(void)
	{
		m_state = State::FAILURE;
		m_cb.invoke(State::FAILURE);
	}

	void Resource::doLoad(void)
	{
		FS::FileSystem& fs = m_resource_manager.getFileSystem();
		fs.openAsync(fs.getDefaultDevice(), m_path, FS::Mode::OPEN | FS::Mode::READ, getReadCallback());
	}

	void Resource::addResDependency(Resource& dependent_resource)
	{
		dependent_resource.m_cb.bind<Resource, &Resource::onResourceUpdated>(this);
		m_dep_count += (dependent_resource.isReady() ? 0 : 1);
	}

	void Resource::remResDependency(Resource& dependent_resource)
	{
		dependent_resource.m_cb.unbind<Resource, &Resource::onResourceUpdated>(this);
		if(dependent_resource.isReady())
		{
			decrementDepCount();
		}
	}

	void Resource::onResourceUpdated(uint32_t new_state)
	{
		if(State::READY == new_state)
		{
			decrementDepCount();
		}
		else
		{
			m_dep_count++;
			if(isReady())
			{
				onUnloading();
			}
		}
	}

	void Resource::decrementDepCount()
	{
		if(--m_dep_count == 0)
			onReady();
	}
} // ~namespace Lux