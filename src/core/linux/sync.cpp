#include "core/allocator.h"
#include "core/crt.h"
#include "core/sync.h"
#include "core/atomic.h"
#include "core/profiler.h"
#include "core/string.h"
#include <errno.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace Lumix
{

SRWLock::SRWLock() {
	const int res = pthread_rwlock_init(&rwlock, nullptr);
	ASSERT(res == 0);
}

SRWLock::~SRWLock() {
	const int res = pthread_rwlock_destroy(&rwlock);
	ASSERT(res == 0);
}

void SRWLock::enterExclusive() {
	const int res = pthread_rwlock_wrlock(&rwlock);
	ASSERT(res == 0);
}

void SRWLock::exitExclusive() {
	const int res = pthread_rwlock_unlock(&rwlock);
	ASSERT(res == 0);
}

void SRWLock::enterShared() {
	const int res = pthread_rwlock_rdlock(&rwlock);
	ASSERT(res == 0);
}

void SRWLock::exitShared() {
	const int res = pthread_rwlock_unlock(&rwlock);
	ASSERT(res == 0);
}

ConditionVariable::ConditionVariable() {
	const int res = pthread_cond_init(&cv, nullptr);
	ASSERT(res == 0);
}

ConditionVariable::~ConditionVariable() {
	const int res = pthread_cond_destroy(&cv);
	ASSERT(res == 0);
}

void ConditionVariable::sleep(Mutex& cs) {
	const int res = pthread_cond_wait(&cv, &cs.mutex);
	ASSERT(res == 0);
}

void ConditionVariable::wakeup() {
	const int res = pthread_cond_signal(&cv);
	ASSERT(res == 0);
}

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

void Semaphore::signal(u32 count)
{
	int res = pthread_mutex_lock(&m_id.mutex);
	ASSERT(res == 0);
	m_id.count += count;
	res = pthread_cond_broadcast(&m_id.cond);
	ASSERT(res == 0);
	res = pthread_mutex_unlock(&m_id.mutex);
	ASSERT(res == 0);
}

void Semaphore::wait()
{
	int res = pthread_mutex_lock(&m_id.mutex);
	ASSERT(res == 0);
	
	while (m_id.count <= 0)
	{
		res = pthread_cond_wait(&m_id.cond, &m_id.mutex);
		ASSERT(res == 0);
	}
	
	m_id.count = m_id.count - 1;
	
	res = pthread_mutex_unlock(&m_id.mutex);
	ASSERT(res == 0);
}


Mutex::Mutex()
{
	const int res = pthread_mutex_init(&mutex, nullptr);
	ASSERT(res == 0);
}


Mutex::~Mutex()
{
	const int res = pthread_mutex_destroy(&mutex);
	ASSERT(res == 0);
}

void Mutex::enter()
{
	const int res = pthread_mutex_lock(&mutex);
	ASSERT(res == 0);
}

void Mutex::exit()
{
	const int res = pthread_mutex_unlock(&mutex);
	ASSERT(res == 0);
}


} // namespace Lumix
