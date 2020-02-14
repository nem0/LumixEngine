#include "engine/allocator.h"
#include "engine/crt.h"
#include "engine/mt/sync.h"
#include "engine/mt/atomic.h"
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

ConditionVariable::~ConditionVariable() {
	((CONDITION_VARIABLE*)data)->~CONDITION_VARIABLE();
}

ConditionVariable::ConditionVariable() {
	static_assert(sizeof(data) >= sizeof(CONDITION_VARIABLE), "Size is not enough");
	static_assert(alignof(CONDITION_VARIABLE) == alignof(CONDITION_VARIABLE), "Alignment does not match");
	memset(data, 0, sizeof(data));
	CONDITION_VARIABLE* cv = new (NewPlaceholder(), data) CONDITION_VARIABLE;
	InitializeConditionVariable(cv);
}

void ConditionVariable::sleep(CriticalSection& cs) { SleepConditionVariableCS((CONDITION_VARIABLE*)data, (CRITICAL_SECTION*)cs.data, INFINITE); }
void ConditionVariable::wakeup() { WakeConditionVariable((CONDITION_VARIABLE*)data); }

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
