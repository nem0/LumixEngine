#include "engine/lumix.h"
#include "engine/mt/thread.h"
#include <pthread.h>
#include <time.h>
#include <unistd.h>


namespace Lumix
{
namespace MT
{


void sleep(uint32 milliseconds)
{
	timespec t;
	t.tv_sec = milliseconds / 1000;
	t.tv_nsec = (milliseconds % 1000) * 1000000;
	clock_nanosleep(CLOCK_MONOTONIC, 0, &t, nullptr);
}

uint32 getCPUsCount()
{
	return sysconf(_SC_NPROCESSORS_ONLN);
}

ThreadID getCurrentThreadID()
{
	return pthread_self();
}

uint32 getProccessAffinityMask()
{
	cpu_set_t affinity;
	int r = pthread_getaffinity_np(pthread_self(), sizeof(affinity), &affinity);
	ASSERT(r == 0);
	if(CPU_COUNT(&affinity) == 0) return 0;
	for(int i = 0; i < 1024; ++i)
	{
		if (CPU_ISSET(i, &affinity)) return i;
	}
	return 0;
}


void setThreadName(ThreadID thread_id, const char* thread_name)
{
	pthread_setname_np(thread_id, thread_name);
}


} //! namespace MT
} //! namespace Lumix
