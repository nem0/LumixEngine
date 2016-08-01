#include "engine/mt/sync.h"
#include "engine/mt/atomic.h"
#include "engine/mt/thread.h"


namespace Lumix
{
namespace MT
{


Semaphore::Semaphore(int init_count, int /*max_count*/)
{
	m_id.count = init_count;
	int res = pthread_mutex_init(&m_id.mutex, nullptr);
	ASSERT(res == 0);
	res = pthread_cond_init(&m_id.cond, nullptr);
	ASSERT(res == 0);
}

Semaphore::~Semaphore()
{
	int res = pthread_mutex_destroy(&m_id.mutex);
	ASSERT(res == 0);
	res = pthread_cond_destroy(&m_id.cond);
	ASSERT(res == 0);
}

void Semaphore::signal()
{
	int res = pthread_mutex_lock(&m_id.mutex);
	ASSERT(res == 0);
	res = pthread_cond_signal(&m_id.cond);
	ASSERT(res == 0);
	++m_id.count;
	res = pthread_mutex_unlock(&m_id.mutex);
	ASSERT(res == 0);
}

void Semaphore::wait()
{
	int res = pthread_mutex_lock(&m_id.mutex);
	ASSERT(res == 0);
	
	while(m_id.count <= 0)
	{
		res = pthread_cond_wait(&m_id.cond, &m_id.mutex);
		ASSERT(res == 0);
	}
	
	--m_id.count;
	
	res = pthread_mutex_unlock(&m_id.mutex);
	ASSERT(res == 0);
}

bool Semaphore::poll()
{
	int res = pthread_mutex_lock(&m_id.mutex);
	ASSERT(res == 0);
	
	bool ret = false;
	if(m_id.count > 0)
	{
		--m_id.count;
		ret = true;
	}

	res = pthread_mutex_unlock(&m_id.mutex);
	ASSERT(res == 0);
	
	return ret;
}


Event::Event()
{
	m_id.signaled = false;
	int res = pthread_mutex_init(&m_id.mutex, nullptr);
	ASSERT(res == 0);
	res = pthread_cond_init(&m_id.cond, nullptr);
	ASSERT(res == 0);
}

Event::~Event()
{
	int res = pthread_mutex_destroy(&m_id.mutex);
	ASSERT(res == 0);
	res = pthread_cond_destroy(&m_id.cond);
	ASSERT(res == 0);
}

void Event::reset()
{
	int res = pthread_mutex_lock(&m_id.mutex);
	ASSERT(res == 0);
	res = pthread_cond_signal(&m_id.cond);
	ASSERT(res == 0);
	m_id.signaled = false;
	res = pthread_mutex_unlock(&m_id.mutex);
	ASSERT(res == 0);
}

void Event::trigger()
{
	int res = pthread_mutex_lock(&m_id.mutex);
	ASSERT(res == 0);
	res = pthread_cond_signal(&m_id.cond);
	ASSERT(res == 0);
	m_id.signaled = true;
	res = pthread_mutex_unlock(&m_id.mutex);
	ASSERT(res == 0);
}

void Event::wait()
{
	int res = pthread_mutex_lock(&m_id.mutex);
	ASSERT(res == 0);
	
	while (!m_id.signaled)
	{
		res = pthread_cond_wait(&m_id.cond, &m_id.mutex);
		ASSERT(res == 0);
	}
	
	m_id.signaled = false;
	
	res = pthread_mutex_unlock(&m_id.mutex);
	ASSERT(res == 0);
}

bool Event::poll()
{
	int res = pthread_mutex_lock(&m_id.mutex);
	ASSERT(res == 0);
	
	bool ret = false;
	if (m_id.signaled)
	{
		m_id.signaled = false;
		ret = true;
	}

	res = pthread_mutex_unlock(&m_id.mutex);
	ASSERT(res == 0);
	
	return ret;
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
