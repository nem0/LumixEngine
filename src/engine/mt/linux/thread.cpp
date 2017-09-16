#include "engine/lumix.h"
#include "engine/mt/thread.h"
#include <pthread.h>
#include <time.h>
#include <unistd.h>


namespace Lumix
{
namespace MT
{


void sleep(u32 milliseconds)
{
	if (milliseconds) usleep(useconds_t(milliseconds * 1000));
}


void yield()
{
	pthread_yield();
}


u32 getCPUsCount()
{
	return sysconf(_SC_NPROCESSORS_ONLN);
}

ThreadID getCurrentThreadID()
{
	return pthread_self();
}

u64 getThreadAffinityMask()
{
	cpu_set_t cpu_set;
	int r = pthread_getaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set);
	ASSERT(r == 0);
	if(CPU_COUNT(&affinity) == 0) return 0;
	
	int affinity = 0;
	for(u64 i = 0; i < sizeof(u64) * 8; ++i)
	{
		if (CPU_ISSET(i, &cpu_set)) affinity = affinity | (1 << i);
	}
	return affinity;
}


void setThreadName(ThreadID thread_id, const char* thread_name)
{
	pthread_setname_np(thread_id, thread_name);
}


} //! namespace MT
} //! namespace Lumix
