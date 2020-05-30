#include "engine/resource.h"
#include "engine/crc32.h"
#include "engine/log.h"
#include "engine/lumix.h"
#include "engine/path.h"
#include "engine/resource_manager.h"


namespace Lumix
{


ResourceType::ResourceType(const char* type_name)
{
	ASSERT(type_name[0] == 0 || (type_name[0] >= 'a' && type_name[0] <= 'z'));
	type = crc32(type_name);
}


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
	, m_async_op(FileSystem::AsyncHandle::invalid())
{
}


Resource::~Resource() = default;


void Resource::refresh() {
	const State old_state = m_current_state;
	m_current_state = State::EMPTY;	
	m_cb.invoke(old_state, m_current_state, *this);
	checkState();
}


void Resource::checkState()
{
	auto old_state = m_current_state;
	if (m_failed_dep_count > 0 && m_current_state != State::FAILURE)
	{
		m_current_state = State::FAILURE;
		#ifdef LUMIX_DEBUG
			m_invoking = true;
		#endif
		m_cb.invoke(old_state, m_current_state, *this);
		#ifdef LUMIX_DEBUG
			m_invoking = false;
		#endif
	}

	if (m_failed_dep_count == 0)
	{
		if (m_empty_dep_count == 0 && m_current_state != State::READY &&
			m_desired_state != State::EMPTY)
		{
			onBeforeReady();
			const bool state_changed = m_empty_dep_count != 0 
				|| m_current_state == State::READY 
				|| m_desired_state == State::EMPTY;
			
			if (state_changed) {
				return;
			}

			m_current_state = State::READY;
			#ifdef LUMIX_DEBUG
				m_invoking = true;
			#endif
			m_cb.invoke(old_state, m_current_state, *this);
			#ifdef LUMIX_DEBUG
				m_invoking = false;
			#endif
		}

		if (m_empty_dep_count > 0 && m_current_state != State::EMPTY)
		{
			onBeforeEmpty();
			m_current_state = State::EMPTY;
			#ifdef LUMIX_DEBUG
				m_invoking = true;
			#endif
			m_cb.invoke(old_state, m_current_state, *this);
			#ifdef LUMIX_DEBUG
				m_invoking = false;
			#endif
		}
	}
}


void Resource::fileLoaded(u64 size, const u8* mem, bool success)
{
	m_async_op = FileSystem::AsyncHandle::invalid();
	if (m_desired_state != State::READY) return;
	
	ASSERT(m_current_state != State::READY);
	ASSERT(m_empty_dep_count == 1);

	if (!success)
	{
		logError("Core") << "Could not open " << getPath().c_str();
		ASSERT(m_empty_dep_count > 0);
		--m_empty_dep_count;
		++m_failed_dep_count;
		checkState();
		m_async_op = FileSystem::AsyncHandle::invalid();
		return;
	}

	if (!load(size, mem)) {
		++m_failed_dep_count;
	}

	ASSERT(m_empty_dep_count > 0);
	--m_empty_dep_count;
	checkState();
	m_async_op = FileSystem::AsyncHandle::invalid();
}


void Resource::doUnload()
{
	if (m_async_op.isValid())
	{
		FileSystem& fs = m_resource_manager.getOwner().getFileSystem();
		fs.cancel(m_async_op);
		m_async_op = FileSystem::AsyncHandle::invalid();
	}

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
	m_desired_state = State::READY;
	m_failed_dep_count = state == State::FAILURE ? 1 : 0;
	m_empty_dep_count = 0;
}


void Resource::doLoad()
{
	if (m_desired_state == State::READY) return;
	m_desired_state = State::READY;

	if (m_async_op.isValid()) return;

	FileSystem& fs = m_resource_manager.getOwner().getFileSystem();
	FileSystem::ContentCallback cb;
	cb.bind<&Resource::fileLoaded>(this);

	const u32 hash = m_path.getHash();
	const StaticString<MAX_PATH_LENGTH> res_path(".lumix/assets/", hash, ".res");

	m_async_op = fs.getContent(Path(res_path), cb);
}


void Resource::addDependency(Resource& dependent_resource)
{
	ASSERT(m_desired_state != State::EMPTY);

	dependent_resource.m_cb.bind<&Resource::onStateChanged>(this);
	if (dependent_resource.isEmpty()) ++m_empty_dep_count;
	if (dependent_resource.isFailure()) {
		++m_failed_dep_count;
	}

	checkState();
}


void Resource::removeDependency(Resource& dependent_resource)
{
	#ifdef LUMIX_DEBUG
		ASSERT(!m_invoking);
	#endif
	dependent_resource.m_cb.unbind<&Resource::onStateChanged>(this);
	if (dependent_resource.isEmpty()) 
	{
		ASSERT(m_empty_dep_count > 0); 
		--m_empty_dep_count;
	}
	if (dependent_resource.isFailure())
	{
		ASSERT(m_failed_dep_count > 0);
		--m_failed_dep_count;
	}

	checkState();
}


void Resource::onStateChanged(State old_state, State new_state, Resource&)
{
	ASSERT(old_state != new_state);
	ASSERT(m_current_state != State::EMPTY || m_desired_state != State::EMPTY);

	if (old_state == State::EMPTY)
	{
		ASSERT(m_empty_dep_count > 0);
		--m_empty_dep_count;
	}
	if (old_state == State::FAILURE)
	{
		ASSERT(m_failed_dep_count > 0);
		--m_failed_dep_count;
	}

	if (new_state == State::EMPTY) ++m_empty_dep_count;
	if (new_state == State::FAILURE) ++m_failed_dep_count;

	checkState();
}


} // namespace Lumix
