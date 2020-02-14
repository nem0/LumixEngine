#include "engine/lumix.h"
#include "engine/allocator.h"
#include "engine/mt/sync.h"
#include "engine/mt/thread.h"
#include "engine/os.h"
#include "engine/win/simple_win.h"
#include "engine/profiler.h"


namespace Lumix
{
namespace MT
{


const u32 STACK_SIZE = 0x8000;

struct ThreadImpl
{
	explicit ThreadImpl(IAllocator& allocator)
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
	MT::ConditionVariable m_cv;
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

static void setThreadName(OS::ThreadID thread_id, const char* thread_name)
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
	struct ThreadImpl* impl = reinterpret_cast<ThreadImpl*>(ptr);
	setThreadName(impl->m_thread_id, impl->m_thread_name);
	Profiler::setThreadName(impl->m_thread_name);
	const u32 ret = impl->m_owner->task();
	impl->m_exited = true;
	impl->m_is_running = false;
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
	while (m_implementation->m_is_running) OS::sleep(1);

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

void Thread::sleep(CriticalSection& cs) {
	m_implementation->m_cv.sleep(cs);
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

} // namespace MT
} // namespace Lumix

