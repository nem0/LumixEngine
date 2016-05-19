#pragma once

#include "engine/lumix.h"

#ifdef __linux__
	#include <pthread.h>
#endif


namespace Lumix
{


namespace MT
{

	
#ifdef _WIN32
	typedef uint32 ThreadID;
#else
	typedef pthread_t ThreadID;
#endif
	

LUMIX_ENGINE_API void setThreadName(ThreadID thread_id,
									const char* thread_name);
LUMIX_ENGINE_API void sleep(uint32 milliseconds);
LUMIX_ENGINE_API inline void yield()
{
	sleep(0);
}

LUMIX_ENGINE_API uint32 getCPUsCount();

LUMIX_ENGINE_API ThreadID getCurrentThreadID();
LUMIX_ENGINE_API uint32 getThreadAffinityMask();

} //! namespace MT


} // !namespace Lumix