#include "engine/allocator.h"
#include "engine/crt.h"
#include "engine/mt/sync.h"
#include "engine/mt/atomic.h"
#include "engine/mt/thread.h"
#include "engine/profiler.h"
#include "engine/string.h"
#include <mutex>


namespace Lumix
{
namespace MT
{


Semaphore::Semaphore(int init_count, int max_count)
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


Event::Event(bool manual_reset)
{
	m_id.signaled = false;
	m_id.manual_reset = manual_reset;
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

void Event::waitMultiple(Event& event0, Event& event1, u32 timeout_ms)
{
	ASSERT(false);
    // TODO
}

void Event::waitTimeout(u32 timeout_ms)
{
	int res = pthread_mutex_lock(&m_id.mutex);
	ASSERT(res == 0);

	timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_nsec += (long)timeout_ms * 1000 * 1000;
	if(ts.tv_nsec > 1000000000)
	{
		ts.tv_nsec -= 1000000000;
		ts.tv_sec += 1;
	}
	while (!m_id.signaled)
	{
		res = pthread_cond_timedwait(&m_id.cond, &m_id.mutex, &ts);
		if(res == ETIMEDOUT) break;
		ASSERT(res == 0);
	}

	if (!m_id.manual_reset) m_id.signaled = false;

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
	
	if (!m_id.manual_reset) m_id.signaled = false;
	
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


CriticalSection::CriticalSection()
{
    static_assert(sizeof(std::mutex) <= sizeof(data));
	new (NewPlaceholder(), data) std::mutex;
}


CriticalSection::~CriticalSection()
{
	((std::mutex*)data)->~mutex();
}

void CriticalSection::enter()
{
	((std::mutex*)data)->lock();

}

void CriticalSection::exit()
{
	((std::mutex*)data)->unlock();
}


} // namespace MT
} // namespace Lumix
