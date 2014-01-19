#include "core/new_macros.h"
#include "core/memory_tracker.h"

namespace Lux 
{
	LUX_FORCE_INLINE void* lux_new(size_t size, const char* file, int32_t line)
	{
		if(!size)size = 1;

		void* p = malloc(size);
		MemoryTracker::getInstance().add(p, size, file, line);
		return p;
	}

	LUX_FORCE_INLINE void* lux_new_aligned(size_t size, size_t alignment, const char* file, int32_t line)
	{
		if(!size)size = 1;
		void* p = _aligned_malloc(size, alignment);
		MemoryTracker::getInstance().add(p, size, file, line);
		return p;
	}
	
	LUX_FORCE_INLINE void* lux_realloc(void* ptr, size_t size, const char* file, int32_t line)
	{
		if(NULL == ptr && 0 < size)
		{
			void* p = malloc(size);
			MemoryTracker::getInstance().add(p, size, file, line);
			return p;
		}

		if(NULL != ptr && 0 == size)
		{
			if(!ptr)
				return NULL;

			Lux::MemoryTracker::getInstance().remove(ptr);
			free(ptr);
			return NULL;
		}

		if(NULL != ptr && 0 < size)
		{
			Lux::MemoryTracker::getInstance().remove(ptr);
			void* p = realloc(ptr, size);
			Lux::MemoryTracker::getInstance().add(p, size, file, line);
			
			return p;
		}

		return NULL;
	}
	
	LUX_FORCE_INLINE void lux_delete(void* ptr)
	{
		if(!ptr)return;

		Lux::MemoryTracker::getInstance().remove(ptr);
		free(ptr);
	}

	LUX_FORCE_INLINE void lux_delete_aligned(void* ptr)
	{
		if(!ptr)return;

		Lux::MemoryTracker::getInstance().remove(ptr);
		_aligned_free(ptr);
	}

	void* dll_lux_new(size_t size, const char* file, int32_t line)
	{ 
		return lux_new(size, file, line); 
	}

	void* dll_lux_new_aligned(size_t size, size_t alignment, const char* file, int32_t line)
	{ 
		return lux_new_aligned(size, alignment, file, line); 
	}

	void* dll_lux_realloc(void* ptr, size_t size, const char* file, int32_t line)
	{ 
		return lux_realloc(ptr, size, file, line); 
	}

	void dll_lux_delete(void* ptr)
	{ 
		lux_delete(ptr); 
	}

	void dll_lux_delete_aligned(void* ptr)
	{ 
		lux_delete_aligned(ptr); 
	}
}
