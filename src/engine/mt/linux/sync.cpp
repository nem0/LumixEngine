#include "engine/allocator.h"
#include "engine/crt.h"
#include "engine/mt/sync.h"
#include "engine/mt/atomic.h"
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

ConditionVariable::ConditionVariable() {
	const int res = pthread_cond_init(&cv, nullptr);
	ASSERT(res == 0);
}

ConditionVariable::~ConditionVariable() {
	const int res = pthread_cond_destroy(&cv);
	ASSERT(res == 0);
}

void ConditionVariable::sleep(CriticalSection& cs) {
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
