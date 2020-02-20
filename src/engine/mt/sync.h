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
#elif defined __linux__
	struct SemaphoreHandle
	{
		pthread_mutex_t mutex;
		pthread_cond_t cond;
		i32 count;
	};
	typedef pthread_mutex_t MutexHandle;
	using EventHandle = int;
#endif


class alignas(8) LUMIX_ENGINE_API Mutex
{
friend class ConditionVariable;
public:
	Mutex();
	~Mutex();

	Mutex(const Mutex&) = delete;

	void enter();
	void exit();

private:
	#ifdef _WIN32
		u8 data[8];
	#else
		pthread_mutex_t mutex;
	#endif
};


class LUMIX_ENGINE_API Semaphore
{
public:
	Semaphore(int init_count, int max_count);
	~Semaphore();

	void signal();

	void wait();

private:
	SemaphoreHandle m_id;
};


class ConditionVariable
{
public:
	ConditionVariable();
	~ConditionVariable();
	void sleep(Mutex& cs);
	void wakeup();
private:
	#ifdef _WIN32
		u8 data[64];
	#else
		pthread_cond_t cv;
	#endif
};


class MutexGuard
{
public:
	explicit MutexGuard(Mutex& cs)
		: m_mutex(cs)
	{
		cs.enter();
	}
	~MutexGuard() { m_mutex.exit(); }

	MutexGuard(const MutexGuard&) = delete;
	void operator=(const MutexGuard&) = delete;

private:
	Mutex& m_mutex;
};


} // namespace MT
} // namespace Lumix
