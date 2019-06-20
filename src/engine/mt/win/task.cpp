#include "engine/lumix.h"
#include "engine/allocator.h"
#include "engine/mt/task.h"
#include "engine/mt/thread.h"
#include "engine/win/simple_win.h"
#include "engine/profiler.h"


namespace Lumix
{
namespace MT
{


const u32 STACK_SIZE = 0x8000;

struct TaskImpl
{
	explicit TaskImpl(IAllocator& allocator)
		: m_allocator(allocator)
	{
	}

	IAllocator& m_allocator;
	HANDLE m_handle;
	DWORD m_thread_id;
	u64 m_affinity_mask;
	u32 m_priority;
	volatile bool m_is_running;
	volatile bool m_exited;
	const char* m_thread_name;
	Task* m_owner;
};

static DWORD WINAPI threadFunction(LPVOID ptr)
{
	struct TaskImpl* impl = reinterpret_cast<TaskImpl*>(ptr);
	setThreadName(impl->m_thread_id, impl->m_thread_name);
	Profiler::setThreadName(impl->m_thread_name);
	const u32 ret = impl->m_owner->task();
	impl->m_exited = true;
	impl->m_is_running = false;
	return ret;
}

Task::Task(IAllocator& allocator)
{
	TaskImpl* impl = LUMIX_NEW(allocator, TaskImpl)(allocator);
	impl->m_handle = nullptr;
	impl->m_affinity_mask = getThreadAffinityMask();
	impl->m_priority = ::GetThreadPriority(GetCurrentThread());
	impl->m_is_running = false;
	impl->m_exited = false;
	impl->m_thread_name = "";
	impl->m_owner = this;

	m_implementation = impl;
}

Task::~Task()
{
	ASSERT(!m_implementation->m_handle);
	LUMIX_DELETE(m_implementation->m_allocator, m_implementation);
}

bool Task::create(const char* name, bool is_extended)
{
	HANDLE handle = CreateThread(
		nullptr, STACK_SIZE, threadFunction, m_implementation, CREATE_SUSPENDED, &m_implementation->m_thread_id);
	if (handle)
	{
		m_implementation->m_exited = false;
		m_implementation->m_thread_name = name;
		m_implementation->m_handle = handle;
		m_implementation->m_is_running = true;
		SetThreadAffinityMask(handle, m_implementation->m_affinity_mask);

		bool success = ::ResumeThread(m_implementation->m_handle) != -1;
		if (success)
		{
			return true;
		}
		::CloseHandle(m_implementation->m_handle);
		m_implementation->m_handle = nullptr;
		return false;
	}
	return false;
}

bool Task::destroy()
{
	while (m_implementation->m_is_running)
	{
		yield();
	}

	::CloseHandle(m_implementation->m_handle);
	m_implementation->m_handle = nullptr;
	return true;
}

void Task::setAffinityMask(u64 affinity_mask)
{
	m_implementation->m_affinity_mask = affinity_mask;
	if (m_implementation->m_handle)
	{
		::SetThreadAffinityMask(m_implementation->m_handle, affinity_mask);
	}
}

bool Task::isRunning() const
{
	return m_implementation->m_is_running;
}

bool Task::isFinished() const
{
	return m_implementation->m_exited;
}

IAllocator& Task::getAllocator()
{
	return m_implementation->m_allocator;
}

} // namespace MT
} // namespace Lumix

