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

//TODO: PC only
// Typedef for the function pointer
typedef void (*_PVFV)(void);

static void LastOnExitFunc()
{
	Lux::MemoryTracker::getInstance().dumpDetailed();
	Lux::MemoryTracker::destruct();
}

static void CInit()
{
	atexit(&LastOnExitFunc);
}

// Define where our segment names
#define SEGMENT_C_INIT      ".CRT$XIM"

// Build our various function tables and insert them into the correct segments.
#pragma data_seg(SEGMENT_C_INIT)
#pragma data_seg() // Switch back to the default segment

// Call create our call function pointer arrays and place them in the segments created above
#define SEG_ALLOCATE(SEGMENT)   __declspec(allocate(SEGMENT))
SEG_ALLOCATE(SEGMENT_C_INIT) _PVFV c_init_funcs[] = { &CInit };
