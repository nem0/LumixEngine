#pragma once


namespace Lumix
{


namespace MT
{


LUMIX_ENGINE_API void setThreadName(uint32_t thread_id,
									const char* thread_name);
LUMIX_ENGINE_API void sleep(uint32_t milliseconds);
LUMIX_ENGINE_API inline void yield()
{
	sleep(0);
}

LUMIX_ENGINE_API uint32_t getCPUsCount();

LUMIX_ENGINE_API uint32_t getCurrentThreadID();
LUMIX_ENGINE_API uint32_t getProccessAffinityMask();

LUMIX_ENGINE_API bool isMainThread();
LUMIX_ENGINE_API void setMainThread();


} //! namespace MT


} // !namespace Lumix