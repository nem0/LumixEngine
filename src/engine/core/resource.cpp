#include "lumix.h"
#include "core/resource.h"
#include "core/fs/file_system.h"
#include "core/log.h"
#include "core/path.h"
#include "core/resource_manager.h"

namespace Lumix
{

Resource::Resource(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: m_ref_count()
	, m_empty_dep_count(1)
	, m_failed_dep_count(0)
	, m_current_state(State::EMPTY)
	, m_desired_state(State::EMPTY)
	, m_path(path)
	, m_size()
	, m_cb(allocator)
	, m_resource_manager(resource_manager)
{
}


Resource::~Resource()
{
}


void Resource::checkState()
{
	auto old_state = m_current_state;
	if (m_failed_dep_count > 0 && m_current_state != State::FAILURE)
	{
		m_current_state = State::FAILURE;
		m_cb.invoke(old_state, m_current_state);
	}

	if (m_failed_dep_count == 0)
	{
		if (m_empty_dep_count == 0 && m_current_state != State::READY)
		{
			onBeforeReady();
			m_current_state = State::READY;
			m_cb.invoke(old_state, m_current_state);
		}

		if (m_empty_dep_count > 0 && m_current_state != State::EMPTY)
		{
			m_current_state = State::EMPTY;
			m_cb.invoke(old_state, m_current_state);
		}
	}
}


void Resource::fileLoaded(FS::IFile& file, bool success, FS::FileSystem& fs)
{
	ASSERT(m_current_state != State::READY);
	ASSERT(m_empty_dep_count == 1);

	if (m_desired_state != State::READY) return;

	if (!success)
	{
		g_log_error.log("resource") << "Could not open " << getPath().c_str();
		--m_empty_dep_count;
		++m_failed_dep_count;
		checkState();
		return;
	}

	if (!load(file))
	{
		++m_failed_dep_count;
	}

	--m_empty_dep_count;
	checkState();
}


void Resource::doUnload()
{
	m_desired_state = State::EMPTY;
	unload();
	ASSERT(m_empty_dep_count <= 1);
	
	m_size = 0;
	m_empty_dep_count = 1;
	m_failed_dep_count = 0;
	checkState();
}


void Resource::onCreated(State state)
{
	ASSERT(m_empty_dep_count == 1);
	ASSERT(m_failed_dep_count == 0);

	m_current_state = state;
	m_failed_dep_count = state == State::FAILURE ? 1 : 0;
	m_empty_dep_count = 0;
}


void Resource::doLoad()
{
	if (m_desired_state == State::READY) return;
	m_desired_state = State::READY;

	FS::FileSystem& fs = m_resource_manager.getFileSystem();
	FS::ReadCallback cb;
	cb.bind<Resource, &Resource::fileLoaded>(this);
	fs.openAsync(fs.getDefaultDevice(), m_path, FS::Mode::OPEN | FS::Mode::READ, cb);
}


void Resource::addDependency(Resource& dependent_resource)
{
	dependent_resource.m_cb.bind<Resource, &Resource::onStateChanged>(this);
	if (dependent_resource.isEmpty()) ++m_empty_dep_count;
	if (dependent_resource.isFailure()) ++m_failed_dep_count;

	checkState();
}


void Resource::removeDependency(Resource& dependent_resource)
{
	dependent_resource.m_cb.unbind<Resource, &Resource::onStateChanged>(this);
	if (dependent_resource.isEmpty()) --m_empty_dep_count;
	if (dependent_resource.isFailure()) --m_failed_dep_count;

	checkState();
}


void Resource::onStateChanged(State old_state, State new_state)
{
	ASSERT(old_state != new_state);

	if (old_state == State::EMPTY) --m_empty_dep_count;
	if (old_state == State::FAILURE) --m_failed_dep_count;

	if (new_state == State::EMPTY) ++m_empty_dep_count;
	if (new_state == State::FAILURE) ++m_failed_dep_count;

	checkState();
}


} // ~namespace Lumix
