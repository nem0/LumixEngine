#include "lumix.h"
#include "core/resource.h"

#include "core/fs/file_system.h"
#include "core/path.h"
#include "core/resource_manager.h"

namespace Lumix
{
	Resource::Resource(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
		: m_ref_count()
		, m_dep_count(1)
		, m_state(State::EMPTY)
		, m_path(path)
		, m_size()
		, m_cb(allocator)
		, m_resource_manager(resource_manager)
	{ }

	Resource::~Resource()
	{ }

	void Resource::onEmpty(void)
	{
		State old_state = m_state;
		m_state = State::EMPTY;
		m_cb.invoke(old_state, State::EMPTY);
		if (old_state == State::LOADING)
		{
			m_resource_manager.decrementLoadingResources();
		}
	}

	void Resource::onLoading(void)
	{
		State old_state = m_state;
		m_state = State::LOADING;
		m_cb.invoke(old_state, State::LOADING);
		m_resource_manager.incrementLoadingResources();
	}

	void Resource::onReady(void)
	{
		State old_state = m_state;
		m_state = State::READY;
		m_cb.invoke(old_state, State::READY);
		if (old_state == State::LOADING)
		{
			m_resource_manager.decrementLoadingResources();
		}
	}

	void Resource::onUnloading(void)
	{
		State old_state = m_state;
		m_state = State::UNLOADING;
		m_cb.invoke(old_state, State::UNLOADING);
		if (old_state == State::LOADING)
		{
			m_resource_manager.decrementLoadingResources();
		}
	}

	void Resource::onReloading(void)
	{
		State old_state = m_state;

		m_state = State::UNLOADING;
		m_cb.invoke(old_state, State::UNLOADING);
		if (old_state == State::LOADING)
		{
			m_resource_manager.decrementLoadingResources();
		}
	}

	void Resource::onFailure(void)
	{
		State old_state = m_state;
		m_state = State::FAILURE;
		m_cb.invoke(old_state, State::FAILURE);
		if (old_state == State::LOADING)
		{
			m_resource_manager.decrementLoadingResources();
		}
	}

	void Resource::doLoad(void)
	{
		FS::FileSystem& fs = m_resource_manager.getFileSystem();
		FS::ReadCallback cb;
		cb.bind<Resource, &Resource::loaded>(this);
		fs.openAsync(fs.getDefaultDevice(), m_path, FS::Mode::OPEN | FS::Mode::READ, cb);
	}

	void Resource::addDependency(Resource& dependent_resource)
	{
		dependent_resource.m_cb.bind<Resource, &Resource::onStateChanged>(this);
		if (!dependent_resource.isReady() && !dependent_resource.isFailure())
		{
			incrementDepCount();
		}
	}

	void Resource::removeDependency(Resource& dependent_resource)
	{
		dependent_resource.m_cb.unbind<Resource, &Resource::onStateChanged>(this);
		if (!dependent_resource.isReady())
		{
			decrementDepCount();
		}
	}

	void Resource::onStateChanged(State old_state, State new_state)
	{
		if (State::FAILURE == new_state)
		{
			onFailure();
		}
		else if (State::READY == new_state)
		{
			decrementDepCount();
		}
		else if (State::READY == old_state && State::UNLOADING == new_state)
		{
			if(isReady())
			{
				onUnloading();
			}

			incrementDepCount();
		}
	}

	void Resource::incrementDepCount()
	{
		if (m_dep_count++ == 0)
			onUnloading();
	}

	void Resource::decrementDepCount()
	{
		if(--m_dep_count == 0)
			onReady();
	}
} // ~namespace Lumix
