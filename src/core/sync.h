#pragma once
#include "core.h"
#ifdef __linux__
	#include <pthread.h>
#endif

namespace Lumix {

struct alignas(8) LUMIX_CORE_API SRWLock {
	SRWLock();
	SRWLock(const SRWLock&) = delete;
	~SRWLock();

	void enterExclusive();
	void exitExclusive();

	void enterShared();
	void exitShared();
	
	#ifdef _WIN32
		u8 data[8];
	#else
		#error "Not implemented"
	#endif
};

struct alignas(8) LUMIX_CORE_API Mutex {
	friend struct ConditionVariable;
	
	Mutex();
	Mutex(const Mutex&) = delete;
	~Mutex();

	void enter();
	void exit();

private:
	#ifdef _WIN32
		u8 data[8];
	#else
		pthread_mutex_t mutex;
	#endif
};


struct LUMIX_CORE_API Semaphore {
	Semaphore(int init_count, int max_count);
	Semaphore(const Semaphore&) = delete;
	~Semaphore();

	void signal(u32 count = 1);
	void wait();
	// return false on timeout
	bool wait(u32 timeout_ms);

private:
	#if defined _WIN32
		void* m_id;
	#elif defined __linux__
		struct {
			pthread_mutex_t mutex;
			pthread_cond_t cond;
			volatile i32 count;
		} m_id;
	#endif
};


struct ConditionVariable {
	ConditionVariable();
	ConditionVariable(const ConditionVariable&) = delete;
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


struct MutexGuard {
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

struct LUMIX_CORE_API MutexGuardProfiled {
	explicit MutexGuardProfiled(Mutex& cs);
	~MutexGuardProfiled();

	MutexGuardProfiled(const MutexGuardProfiled&) = delete;
	void operator=(const MutexGuardProfiled&) = delete;

private:
	Mutex& m_mutex;
	u64 start_enter;
	u64 end_enter;
	u64 start_exit;
	u64 end_exit;
};

} // namespace Lumix
