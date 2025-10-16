#include "core/allocator.h"
#include "core/atomic.h"
#include "core/crt.h"
#include "core/os.h"
#include "core/profiler.h"
#include "core/string.h"
#include "core/sync.h"
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

void Semaphore::signal(u32 count)
{
	BOOL res = ::ReleaseSemaphore(m_id, count, nullptr);
	ASSERT(res);
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

SRWLock::SRWLock() {
	static_assert(sizeof(data) >= sizeof(SRWLOCK), "Size is not enough");
	static_assert(alignof(SRWLock) == alignof(SRWLOCK), "Alignment does not match");
	memset(data, 0, sizeof(data));
	SRWLOCK* lock = new (NewPlaceholder(), data) SRWLOCK;
	InitializeSRWLock(lock);
}

SRWLock::~SRWLock() {
	SRWLOCK* lock = (SRWLOCK*)data;
	lock->~SRWLOCK();
}

void SRWLock::enterExclusive() {
	SRWLOCK* lock = (SRWLOCK*)data;
	AcquireSRWLockExclusive(lock);
}

void SRWLock::exitExclusive() {
	SRWLOCK* lock = (SRWLOCK*)data;
	ReleaseSRWLockExclusive(lock);
}

void SRWLock::enterShared() {
	SRWLOCK* lock = (SRWLOCK*)data;
	AcquireSRWLockShared(lock);
}

void SRWLock::exitShared() {
	SRWLOCK* lock = (SRWLOCK*)data;
	ReleaseSRWLockShared(lock);
}

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
	ReleaseSRWLockExclusive(lock);
}

MutexGuardProfiled::MutexGuardProfiled(Mutex& cs)
	: m_mutex(cs)
{
	start_enter = os::Timer::getRawTimestamp();
	cs.enter();
	end_enter = os::Timer::getRawTimestamp();
}

MutexGuardProfiled::~MutexGuardProfiled() {
	start_exit = os::Timer::getRawTimestamp();
	m_mutex.exit();
	end_exit = os::Timer::getRawTimestamp();
	if (end_exit - start_enter > 20) profiler::pushMutexEvent(u64(&m_mutex), start_enter, end_enter, start_exit, end_exit);
}


} // namespace Lumix
