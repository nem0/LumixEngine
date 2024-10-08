#include "core/allocator.h"
#include "core/crt.h"
#include "core/sync.h"
#include "core/atomic.h"
#include "core/profiler.h"
#include "core/string.h"
#include "core/win/simple_win.h"


namespace Lumix
{


Semaphore::Semaphore(int init_count, int max_count)
{
	m_id = ::CreateSemaphore(nullptr, init_count, max_count, nullptr);
	ASSERT(m_id);
}

Semaphore::~Semaphore()
{
	::CloseHandle(m_id);
}

void Semaphore::signal()
{
	BOOL res = ::ReleaseSemaphore(m_id, 1, nullptr);
	ASSERT(res);
}

i32 Semaphore::waitMultiple(Semaphore& a, Semaphore& b) {
	HANDLE handles[] = {a.m_id, b.m_id};
	DWORD res = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);
	if (res == WAIT_OBJECT_0) return 0;
	if (res == WAIT_OBJECT_0 + 1) return 1;
	return -1;
}

bool Semaphore::wait(u32 timeout_ms) {
	auto res = ::WaitForSingleObject(m_id, timeout_ms);
	return WAIT_OBJECT_0 == res;
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

void ConditionVariable::sleep(Mutex& mutex) { SleepConditionVariableSRW((CONDITION_VARIABLE*)data, (SRWLOCK*)mutex.data, INFINITE, 0); }
void ConditionVariable::wakeup() { WakeConditionVariable((CONDITION_VARIABLE*)data); }

Mutex::Mutex()
{
	static_assert(sizeof(data) >= sizeof(SRWLOCK), "Size is not enough");
	static_assert(alignof(Mutex) == alignof(SRWLOCK), "Alignment does not match");
	memset(data, 0, sizeof(data));
	SRWLOCK* lock = new (NewPlaceholder(), data) SRWLOCK;
	InitializeSRWLock(lock);
}

Mutex::~Mutex() {
	SRWLOCK* lock = (SRWLOCK*)data;
	lock->~SRWLOCK();
}

void Mutex::enter() {
	SRWLOCK* lock = (SRWLOCK*)data;
	AcquireSRWLockExclusive(lock);
}

void Mutex::exit() {
	SRWLOCK* lock = (SRWLOCK*)data;
	ReleaseSRWLockExclusive (lock);
}


} // namespace Lumix
