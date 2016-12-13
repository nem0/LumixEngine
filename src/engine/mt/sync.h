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
	typedef volatile i32 SpinMutexHandle;
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
	};
	typedef volatile i32 SpinMutexHandle;
#endif
	

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
	explicit Event();
	~Event();

	void reset();

	void trigger();

	void wait();
	bool poll();

private:
	EventHandle m_id;
};


class LUMIX_ENGINE_API SpinMutex
{
public:
	explicit SpinMutex(bool locked);
	~SpinMutex();

	void lock();
	bool poll();

	void unlock();

private:
	SpinMutexHandle m_id;
};


class SpinLock
{
public:
	SpinLock(SpinMutex& mutex)
		: m_mutex(mutex)
	{
		mutex.lock();
	}
	~SpinLock() { m_mutex.unlock(); }

private:
	SpinLock(const SpinLock&);
	void operator=(const SpinLock&);
	SpinMutex& m_mutex;
};


} // namespace MT
} // namespace Lumix
