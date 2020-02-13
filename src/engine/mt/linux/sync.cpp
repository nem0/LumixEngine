#include "engine/allocator.h"
#include "engine/crt.h"
#include "engine/mt/sync.h"
#include "engine/mt/atomic.h"
#include "engine/mt/thread.h"
#include "engine/profiler.h"
#include "engine/string.h"
#include <errno.h>
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


Event::Event(bool manual_reset)
{
	m_id.fd = eventfd(0, EFD_NONBLOCK);
	m_id.manual_reset = manual_reset;
}

Event::~Event()
{
	close(m_id.fd);
}

void Event::reset()
{
	u64 v;
	read(m_id.fd, &v, sizeof(v)); // reset to 0, nonblocking
}

void Event::trigger()
{
	const u64 v = 1;
	const ssize_t res = write(m_id.fd, &v, sizeof(v));
	ASSERT(res == sizeof(v));
}

void Event::waitMultiple(Event& event0, Event& event1, u32 timeout_ms)
{
	fd_set fs;
	FD_ZERO(&fs);
	FD_SET(event0.m_id.fd, &fs);
	FD_SET(event1.m_id.fd, &fs);
	struct timeval tv;
	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;
	const int nfds = event0.m_id.fd < event1.m_id.fd ? event1.m_id.fd : event0.m_id.fd;
	select(nfds + 1, &fs, nullptr, nullptr, &tv);
}

void Event::waitTimeout(u32 timeout_ms)
{
	fd_set fs;
	FD_ZERO(&fs);
	FD_SET(m_id.fd, &fs);
	struct timeval tv;
	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;
	select(m_id.fd + 1, &fs, nullptr, nullptr, &tv);
}

void Event::wait()
{
	for (;;) {
		fd_set fs;
		const int res = select(m_id.fd + 1, &fs, nullptr, nullptr, nullptr);
		u64 v;
		read(m_id.fd, &v, sizeof(v));
		if (errno != EAGAIN) break;
	}
}

bool Event::poll()
{
	u64 v;
	read(m_id.fd, &v, sizeof(v));
	return errno != EAGAIN;
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
