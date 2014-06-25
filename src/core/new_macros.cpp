#include "core/new_macros.h"
#include "core/memory_tracker.h"

namespace Lumix 
{
#ifdef MEM_TRACK
	void storePtr(void* ptr, size_t size, const char* file, int32_t line)
	{
		MemoryTracker::getInstance().add(ptr, size, file, line);
	}

	void removePtr(void* ptr)
	{
		MemoryTracker::getInstance().remove(ptr);
	}
#else
	void storePtr(void* ptr, size_t size, const char* file, int32_t line) {}
	void removePtr(void* ptr) {}
#endif

	LUMIX_FORCE_INLINE void* lumix_new(size_t size, const char* file, int32_t line)
	{
		if(!size)size = 1;

		void* p = malloc(size);
		storePtr(p, size, file, line);
		return p;
	}

	LUMIX_FORCE_INLINE void* lumix_new_aligned(size_t size, size_t alignment, const char* file, int32_t line)
	{
		if(!size)size = 1;
		void* p = _aligned_malloc(size, alignment);
		storePtr(p, size, file, line);
		return p;
	}
	
	LUMIX_FORCE_INLINE void* lumix_realloc(void* ptr, size_t size, const char* file, int32_t line)
	{
		if(NULL == ptr && 0 < size)
		{
			void* p = malloc(size);
			storePtr(p, size, file, line);
			return p;
		}

		if(NULL != ptr && 0 == size)
		{
			if(!ptr)
				return NULL;

			removePtr(ptr);
			free(ptr);
			return NULL;
		}

		if(NULL != ptr && 0 < size)
		{
			removePtr(ptr);
			void* p = realloc(ptr, size);
			Lumix::storePtr(p, size, file, line);
			
			return p;
		}

		return NULL;
	}
	
	LUMIX_FORCE_INLINE void lumix_delete(void* ptr)
	{
		if(!ptr)return;

		removePtr(ptr);
		free(ptr);
	}

	LUMIX_FORCE_INLINE void lumix_delete_aligned(void* ptr)
	{
		if(!ptr)return;

		removePtr(ptr);
		_aligned_free(ptr);
	}

	void* dll_lumix_new(size_t size, const char* file, int32_t line)
	{ 
		return lumix_new(size, file, line); 
	}

	void* dll_lumix_new_aligned(size_t size, size_t alignment, const char* file, int32_t line)
	{ 
		return lumix_new_aligned(size, alignment, file, line); 
	}

	void* dll_lumix_realloc(void* ptr, size_t size, const char* file, int32_t line)
	{ 
		return lumix_realloc(ptr, size, file, line); 
	}

	void dll_lumix_delete(void* ptr)
	{ 
		lumix_delete(ptr); 
	}

	void dll_lumix_delete_aligned(void* ptr)
	{ 
		lumix_delete_aligned(ptr); 
	}
}
