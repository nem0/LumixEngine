#include "engine/allocator.h"
#include "engine/crt.h"
#include "engine/mt/sync.h"
#include "engine/mt/atomic.h"
#include "engine/mt/thread.h"
#include "engine/profiler.h"
#include "engine/string.h"
#include <errno.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

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


Event::Event()
{
	m_id = eventfd(0, EFD_NONBLOCK);
}

Event::~Event()
{
	close(m_id);
}

void Event::reset()
{
	u64 v;
	read(m_id, &v, sizeof(v)); // reset to 0, nonblocking
}

void Event::trigger()
{
	const u64 v = 1;
	const ssize_t res = write(m_id, &v, sizeof(v));
	ASSERT(res == sizeof(v));
}

void Event::waitMultiple(Event& event0, Event& event1, u32 timeout_ms)
{
	pollfd fds[2];
	fds[0].fd = event0.m_id;
	fds[1].fd = event1.m_id;
	fds[0].events = POLLIN;
	fds[1].events = POLLIN;
	::poll(fds, 2, timeout_ms);
}

void Event::waitTimeout(u32 timeout_ms)
{
	pollfd pfd;
	pfd.fd = m_id;
	pfd.events = POLLIN;
	::poll(&pfd, 1, timeout_ms);
}

void Event::wait()
{
	pollfd pfd;
	pfd.fd = m_id;
	pfd.events = POLLIN;
	::poll(&pfd, 1, -1);
}

bool Event::poll()
{
	pollfd pfd;
	pfd.fd = m_id;
	pfd.events = POLLIN;
	const int res = ::poll(&pfd, 1, 0);
	return res > 0;
}

CriticalSection::CriticalSection()
{
	const int res = pthread_mutex_init(&mutex, nullptr);
	ASSERT(res == 0);
}


CriticalSection::~CriticalSection()
{
	const int res = pthread_mutex_destroy(&mutex);
	ASSERT(res == 0);
}

void CriticalSection::enter()
{
	const int res = pthread_mutex_lock(&mutex);
	ASSERT(res == 0);
}

void CriticalSection::exit()
{
	const int res = pthread_mutex_unlock(&mutex);
	ASSERT(res == 0);
}


} // namespace MT
} // namespace Lumix
