#include "engine/lumix.h"
#include "engine/core/iallocator.h"
#include "engine/core/mt/task.h"
#include "engine/core/mt/thread.h"
#include "engine/core/profiler.h"


namespace Lumix
{
namespace MT
{

struct TaskImpl
{
	IAllocator& allocator;
};

Task::Task(IAllocator& allocator)
{
	m_implementation = LUMIX_NEW(allocator, TaskImpl) {allocator};
}

Task::~Task()
{
	LUMIX_DELETE(m_implementation->allocator, m_implementation);
}

bool Task::create(const char* name)
{
	ASSERT(false);
	return false;
}

bool Task::destroy()
{
	ASSERT(false);
	return false;
}

void Task::setAffinityMask(uint32 affinity_mask)
{
	ASSERT(false);
}

uint32 Task::getAffinityMask() const
{
	ASSERT(false);
	return 0;
}

bool Task::isRunning() const
{
	return false;
}

bool Task::isFinished() const
{
	return false;
}

bool Task::isForceExit() const
{
	return false;
}

IAllocator& Task::getAllocator()
{
	return m_implementation->allocator;
}

void Task::forceExit(bool wait)
{
	ASSERT(false);
}

} // namespace MT
} // namespace Lumix
