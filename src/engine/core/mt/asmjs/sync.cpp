#include "engine/core/mt/sync.h"
#include "engine/core/mt/atomic.h"
#include "engine/core/mt/thread.h"


namespace Lumix
{
namespace MT
{


Semaphore::Semaphore(int init_count, int /*max_count*/)
{
	m_id = nullptr;
}

Semaphore::~Semaphore()
{
}

void Semaphore::signal()
{
	ASSERT(false);
}

void Semaphore::wait()
{
	ASSERT(false);
}

bool Semaphore::poll()
{
	ASSERT(false);
	return false;
}


Mutex::Mutex(bool locked)
{
	m_id = nullptr;
}

Mutex::~Mutex()
{
}

void Mutex::lock()
{
	ASSERT(false);
}


void Mutex::unlock()
{
	ASSERT(false);
}


Event::Event()
{
	m_id = nullptr;
}

Event::~Event()
{
}

void Event::reset()
{
	ASSERT(false);
}

void Event::trigger()
{
	ASSERT(false);
}

void Event::wait()
{
	ASSERT(false);
}

bool Event::poll()
{
	ASSERT(false);
	return false;
}


SpinMutex::SpinMutex(bool locked)
	: m_id(0)
{
	if (locked)
	{
		lock();
	}
}

SpinMutex::~SpinMutex()
{
}

void SpinMutex::lock()
{
	for (;;)
	{
		if (compareAndExchange(&m_id, 1, 0))
		{
			memoryBarrier();
			return;
		}

		while (m_id)
		{
			yield();
		}
	}
}

bool SpinMutex::poll()
{
	if (compareAndExchange(&m_id, 1, 0))
	{
		memoryBarrier();
		return true;
	}
	return false;
}

void SpinMutex::unlock()
{
	memoryBarrier();
	m_id = 0;
}


} // namespace MT
} // namespace Lumix
