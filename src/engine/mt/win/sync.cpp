#include "engine/allocator.h"
#include "engine/crt.h"
#include "engine/mt/sync.h"
#include "engine/mt/atomic.h"
#include "engine/mt/thread.h"
#include "engine/profiler.h"
#include "engine/string.h"
#include "engine/win/simple_win.h"
#include <intrin.h>


namespace Lumix
{
namespace MT
{


Semaphore::Semaphore(int init_count, int max_count)
{
	m_id = ::CreateSemaphore(nullptr, init_count, max_count, nullptr);
}

Semaphore::~Semaphore()
{
	::CloseHandle(m_id);
}

void Semaphore::signal()
{
	::ReleaseSemaphore(m_id, 1, nullptr);
}

void Semaphore::wait()
{
	::WaitForSingleObject(m_id, INFINITE);
}

bool Semaphore::poll()
{
	return WAIT_OBJECT_0 == ::WaitForSingleObject(m_id, 0);
}


Event::Event()
{
	m_id = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

Event::~Event()
{
	::CloseHandle(m_id);
}

void Event::reset()
{
	::ResetEvent(m_id);
}

void Event::trigger()
{
	::SetEvent(m_id);
}

void Event::waitMultiple(Event& event0, Event& event1, u32 timeout_ms)
{
	const HANDLE handles[2] = { event0.m_id, event1.m_id };
	::WaitForMultipleObjects(2, handles, false, timeout_ms);
}

void Event::waitTimeout(u32 timeout_ms)
{
	::WaitForSingleObject(m_id, timeout_ms);
}

void Event::wait()
{
	::WaitForSingleObject(m_id, INFINITE);
}

bool Event::poll()
{
	return WAIT_OBJECT_0 == ::WaitForSingleObject(m_id, 0);
}


CriticalSection::CriticalSection()
{
	static_assert(sizeof(data) >= sizeof(CRITICAL_SECTION), "Size is not enough");
	static_assert(alignof(CriticalSection) == alignof(CRITICAL_SECTION), "Alignment does not match");
	memset(data, 0, sizeof(data));
	CRITICAL_SECTION* cs = new (NewPlaceholder(), data) CRITICAL_SECTION;
	InitializeCriticalSectionAndSpinCount(cs, 0x400);
}


CriticalSection::~CriticalSection()
{
	CRITICAL_SECTION* cs = (CRITICAL_SECTION*)data;
	DeleteCriticalSection(cs);
	cs->~CRITICAL_SECTION();
}

void CriticalSection::enter()
{
	CRITICAL_SECTION* cs = (CRITICAL_SECTION*)data;
	EnterCriticalSection(cs);
}

void CriticalSection::exit()
{
	CRITICAL_SECTION* cs = (CRITICAL_SECTION*)data;
	LeaveCriticalSection(cs);
}


} // namespace MT
} // namespace Lumix
