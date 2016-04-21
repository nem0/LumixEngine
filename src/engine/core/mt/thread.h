#pragma once


namespace Lumix
{


namespace MT
{


LUMIX_ENGINE_API void setThreadName(uint32 thread_id,
									const char* thread_name);
LUMIX_ENGINE_API void sleep(uint32 milliseconds);
LUMIX_ENGINE_API inline void yield()
{
	sleep(0);
}

LUMIX_ENGINE_API uint32 getCPUsCount();

LUMIX_ENGINE_API uint32 getCurrentThreadID();
LUMIX_ENGINE_API uint32 getProccessAffinityMask();

} //! namespace MT


} // !namespace Lumix