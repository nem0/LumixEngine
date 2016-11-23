#include "engine/lumix.h"
#include "engine/iallocator.h"
#include "engine/mt/task.h"
#include "engine/mt/thread.h"
#include "engine/profiler.h"
#include <pthread.h>


#if !LUMIX_SINGLE_THREAD()


namespace Lumix
{
namespace MT
{

struct TaskImpl
{
	IAllocator& allocator;
	bool force_exit;
	bool exited;
	bool is_running;
	pthread_t handle;
	const char* thread_name;
	u32 affinity_mask;
	Task* owner;
};

static void* threadFunction(void* ptr)
{
	struct TaskImpl* impl = reinterpret_cast<TaskImpl*>(ptr);
	setThreadName(getCurrentThreadID(), impl->thread_name);
	Profiler::setThreadName(impl->thread_name);
	u32 ret = 0xffffFFFF;
	if (!impl->force_exit) ret = impl->owner->task();
	impl->exited = true;
	impl->is_running = false;

	return nullptr;
}

Task::Task(IAllocator& allocator)
{
	auto impl = LUMIX_NEW(allocator, TaskImpl) {allocator};

	impl->is_running = false;
	impl->force_exit = false;
	impl->exited = false;
	impl->thread_name = "";
	impl->owner = this;
	impl->affinity_mask = getThreadAffinityMask();

	m_implementation = impl;
}

Task::~Task()
{
	LUMIX_DELETE(m_implementation->allocator, m_implementation);
}

bool Task::create(const char* name)
{
	pthread_attr_t attr;
	int res = pthread_attr_init(&attr);
	ASSERT(res == 0);
	if (res != 0) return false;
	res = pthread_create(&m_implementation->handle, &attr, threadFunction, m_implementation);
	ASSERT(res == 0);
	if (res != 0) return false;
	return true;
}

bool Task::destroy()
{
	return pthread_join(m_implementation->handle, nullptr) == 0;
}

void Task::setAffinityMask(u32 affinity_mask)
{
	cpu_set_t set;
	CPU_ZERO(&set);
	for (int i = 0; i < 32; ++i)
	{
		if (affinity_mask & (1 << i))
		{
			CPU_SET(i, &set);
		}
	}
	m_implementation->affinity_mask = affinity_mask;
	pthread_setaffinity_np(m_implementation->handle, sizeof(set), &set);
}

u32 Task::getAffinityMask() const
{
	return m_implementation->affinity_mask;
}

bool Task::isRunning() const
{
	return m_implementation->is_running;
}

bool Task::isFinished() const
{
	return m_implementation->exited;
}

bool Task::isForceExit() const
{
	return m_implementation->force_exit;
}

IAllocator& Task::getAllocator()
{
	return m_implementation->allocator;
}

void Task::forceExit(bool wait)
{
	m_implementation->force_exit = true;

	if (wait)
	{
		pthread_join(m_implementation->handle, nullptr);
	}
}

} // namespace MT
} // namespace Lumix


#endif
