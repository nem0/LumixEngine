namespace Lumix
{
	namespace MT
	{
		LUMIX_CORE_API void SetThreadName(uint32_t thread_id, const char* thread_name);
		LUMIX_CORE_API void sleep(uint32_t miliseconds);
		LUMIX_CORE_API inline void yield() { sleep(0); }

		LUMIX_CORE_API uint32_t getCPUsCount();

		LUMIX_CORE_API uint32_t getCurrentThreadID();
		LUMIX_CORE_API uint32_t getProccessAffinityMask();

		LUMIX_CORE_API bool isMainThread();
		LUMIX_CORE_API void setMainThread();
	} //!namespace MT
} // !namespace Lumix