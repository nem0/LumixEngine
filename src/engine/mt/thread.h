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
	typedef u32 ThreadID;
#else
	typedef pthread_t ThreadID;
#endif
	

LUMIX_ENGINE_API void setThreadName(ThreadID thread_id,
									const char* thread_name);
LUMIX_ENGINE_API void sleep(u32 milliseconds);
LUMIX_ENGINE_API void yield();

LUMIX_ENGINE_API u32 getCPUsCount();

LUMIX_ENGINE_API ThreadID getCurrentThreadID();
LUMIX_ENGINE_API u32 getThreadAffinityMask();

} //! namespace MT


} // !namespace Lumix