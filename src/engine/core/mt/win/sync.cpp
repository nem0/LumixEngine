#include "engine/core/mt/sync.h"
#include "engine/core/mt/atomic.h"
#include "engine/core/mt/thread.h"
#include "engine/core/win/simple_win.h"



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


Mutex::Mutex(bool locked)
{
	m_id = ::CreateMutex(nullptr, locked, nullptr);
}

Mutex::~Mutex()
{
	::CloseHandle(m_id);
}

void Mutex::lock()
{
	::WaitForSingleObject(m_id, INFINITE);
}

void Mutex::unlock()
{
	::ReleaseMutex(m_id);
}


Event::Event()
{
	m_id = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
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

void Event::wait()
{
	::WaitForSingleObject(m_id, INFINITE);
}

bool Event::poll()
{
	return WAIT_OBJECT_0 == ::WaitForSingleObject(m_id, 0);
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
