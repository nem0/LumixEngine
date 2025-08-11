#include "core/core.h"
#include "core/allocator.h"
#include "core/debug.h"
#include "core/sync.h"
#include "core/tag_allocator.h"
#include "core/thread.h"
#include "core/os.h"
#include "core/string.h"
#include "core/win/simple_win.h"
#include "core/profiler.h"


namespace Lumix
{


static constexpr u32 STACK_SIZE = 0x8000;

struct ThreadImpl
{
	explicit ThreadImpl(IAllocator& allocator)
		: m_allocator(allocator)
	{}

	debug::AllocationInfo m_allocation_info;
	IAllocator& m_allocator;
	HANDLE m_handle;
	DWORD m_thread_id;
	u64 m_affinity_mask;
	u32 m_priority;
	volatile bool m_is_running;
	volatile bool m_exited;
	StaticString<64> m_thread_name;
	ConditionVariable m_cv;
	Thread* m_owner;
};

static const DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push,8)
	typedef struct tagTHREADNAME_INFO
	{
		DWORD type;
		LPCSTR name;
		DWORD thread_id;
		DWORD flags;
	} THREADNAME_INFO;
#pragma pack(pop)

static void setThreadName(os::ThreadID thread_id, const char* thread_name)
{
	THREADNAME_INFO info;
	info.type = 0x1000;
	info.name = thread_name;
	info.thread_id = thread_id;
	info.flags = 0;

	__try
	{
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
}

static DWORD WINAPI threadFunction(LPVOID ptr)
{
	static TagAllocator tag_allocator(getGlobalAllocator(), "thread stack");

	struct ThreadImpl* impl = reinterpret_cast<ThreadImpl*>(ptr);
	impl->m_allocation_info.align = 16;
	ULONG_PTR low, high;
	GetCurrentThreadStackLimits(&low, &high);
	impl->m_allocation_info.size = size_t(high - low);
	impl->m_allocation_info.tag = &tag_allocator;
	impl->m_allocation_info.flags = debug::AllocationInfo::IS_MISC;
	debug::registerAlloc(impl->m_allocation_info);
	setThreadName(impl->m_thread_id, impl->m_thread_name);
	profiler::setThreadName(impl->m_thread_name);
	const u32 ret = impl->m_owner->task();
	impl->m_exited = true;
	impl->m_is_running = false;
	debug::unregisterAlloc(impl->m_allocation_info);
	return ret;
}

Thread::Thread(IAllocator& allocator)
{
	ThreadImpl* impl = LUMIX_NEW(allocator, ThreadImpl)(allocator);
	impl->m_handle = nullptr;
	impl->m_priority = ::GetThreadPriority(GetCurrentThread());
	impl->m_is_running = false;
	impl->m_exited = false;
	impl->m_thread_name = "";
	impl->m_owner = this;

	m_implementation = impl;
}

Thread::~Thread()
{
	ASSERT(!m_implementation->m_handle);
	LUMIX_DELETE(m_implementation->m_allocator, m_implementation);
}

bool Thread::create(const char* name, bool is_extended)
{
	HANDLE handle = CreateThread(
		nullptr, STACK_SIZE, threadFunction, m_implementation, CREATE_SUSPENDED, &m_implementation->m_thread_id);
	if (handle)
	{
		m_implementation->m_exited = false;
		m_implementation->m_thread_name = name;
		m_implementation->m_handle = handle;
		m_implementation->m_is_running = true;

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

bool Thread::destroy()
{
	while (m_implementation->m_is_running) os::sleep(1);

	::CloseHandle(m_implementation->m_handle);
	m_implementation->m_handle = nullptr;
	return true;
}

void Thread::setAffinityMask(u64 affinity_mask)
{
	m_implementation->m_affinity_mask = affinity_mask;
	if (m_implementation->m_handle)
	{
		::SetThreadAffinityMask(m_implementation->m_handle, affinity_mask);
	}
}

void Thread::sleep(Mutex& mutex) {
	m_implementation->m_cv.sleep(mutex);
}

void Thread::wakeup() {
	m_implementation->m_cv.wakeup();
}

bool Thread::isRunning() const
{
	return m_implementation->m_is_running;
}

bool Thread::isFinished() const
{
	return m_implementation->m_exited;
}

IAllocator& Thread::getAllocator()
{
	return m_implementation->m_allocator;
}

} // namespace Lumix

