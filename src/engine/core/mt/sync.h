#pragma once
#include "engine/lumix.h"

namespace Lumix
{
namespace MT
{


typedef void* SemaphoreHandle;


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


typedef void* MutexHandle;


class LUMIX_ENGINE_API Mutex
{
public:
	explicit Mutex(bool locked);
	~Mutex();

	void lock();

	void unlock();

private:
	MutexHandle m_id;
};


class Lock
{
public:
	Lock(Mutex& mutex)
		: m_mutex(mutex)
	{
		mutex.lock();
	}
	~Lock() { m_mutex.unlock(); }

private:
	void operator=(const Lock&);
	Mutex& m_mutex;
};


typedef void* EventHandle;

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


typedef volatile int32 SpinMutexHandle;


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
	void operator=(const SpinLock&);
	SpinMutex& m_mutex;
};


} // namespace MT
} // namespace Lumix
