#pragma once
#include "engine/lumix.h"
#ifdef __linux__
	#include <pthread.h>
#endif

namespace Lumix
{
namespace MT
{


#if defined _WIN32
	typedef void* SemaphoreHandle;
	typedef void* MutexHandle;
	typedef void* EventHandle;
	typedef volatile long SpinMutexHandle;
#elif defined __linux__
	struct SemaphoreHandle
	{
		pthread_mutex_t mutex;
		pthread_cond_t cond;
		i32 count;
	};
	typedef pthread_mutex_t MutexHandle;
	struct EventHandle
	{
		pthread_mutex_t mutex;
		pthread_cond_t cond;
		bool signaled;
		bool manual_reset;
	};
	typedef volatile i32 SpinMutexHandle;
#endif


class alignas(8) LUMIX_ENGINE_API CriticalSection
{
public:
	CriticalSection();
	~CriticalSection();

	void enter();
	void exit();

private:
	u8 data[64];
};


class LUMIX_ENGINE_API Semaphore
{
public:
	Semaphore(int init_count, int max_count);
	~Semaphore();

	void signal();

	void wait();
	bool poll();

private:
	SemaphoreHandle m_id;
};


class LUMIX_ENGINE_API Event
{
public:
	explicit Event(bool manual_reset);
	~Event();

	void reset();

	void trigger();

	void wait();
	void waitTimeout(u32 timeout_ms);
	bool poll();

private:
	EventHandle m_id;
};


class LUMIX_ENGINE_API SpinMutex
{
public:
	explicit SpinMutex();
	~SpinMutex();

	void lock();

	void unlock();

private:
	alignas(64) SpinMutexHandle m_id;
};


class SpinLock
{
public:
	explicit SpinLock(SpinMutex& mutex)
		: m_mutex(mutex)
	{
		mutex.lock();
	}
	~SpinLock() { m_mutex.unlock(); }

	SpinLock(const SpinLock&) = delete;
	void operator=(const SpinLock&) = delete;

private:
	SpinMutex& m_mutex;
};


class CriticalSectionLock
{
public:
	explicit CriticalSectionLock(CriticalSection& cs)
		: m_critical_section(cs)
	{
		cs.enter();
	}
	~CriticalSectionLock() { m_critical_section.exit(); }

	CriticalSectionLock(const CriticalSectionLock&) = delete;
	void operator=(const CriticalSectionLock&) = delete;

private:
	CriticalSection& m_critical_section;
};


} // namespace MT
} // namespace Lumix
