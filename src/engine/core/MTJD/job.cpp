#include "lumix.h"
#include "core/MTJD/job.h"

#include "core/MTJD/manager.h"

namespace Lumix
{
namespace MTJD
{
Job::Job(int flags,
	Priority priority,
	Manager& manager,
	IAllocator& allocator,
	IAllocator& job_allocator)
	: BaseEntry(1, (flags & SYNC_EVENT) != 0, allocator)
	, m_manager(manager)
	, m_priority(priority)
	, m_auto_destroy((flags & AUTO_DESTROY) != 0)
	, m_scheduled(false)
	, m_executed(false)
	, m_allocator(allocator)
	, m_job_allocator(job_allocator)
{
	setJobName("Unknown Job");
}

Job::~Job()
{
}

void Job::incrementDependency()
{
#if TYPE == MULTI_THREAD

	MT::atomicIncrement(&m_dependency_count);
	ASSERT(!m_scheduled);

#endif // TYPE == MULTI_THREAD
}

void Job::decrementDependency()
{
#if TYPE == MULTI_THREAD

	uint32 count = MT::atomicDecrement(&m_dependency_count);
	if (1 == count)
	{
		m_manager.schedule(this);
	}

#endif // TYPE == MULTI_THREAD
}

void Job::onExecuted()
{
	m_executed = true;
	bool auto_destroy = m_auto_destroy;

	BaseEntry::dependencyReady();

	if (auto_destroy)
	{
		LUMIX_DELETE(m_job_allocator, this);
	}
}
} // namepsace MTJD
} // namepsace Lumix
