#pragma once

#include "core/lumix.h"

namespace Lumix 
{
	LUX_CORE_API void* dll_lumix_new(size_t size, const char* file, int32_t line);
	LUX_CORE_API void* dll_lumix_new(size_t size, const char* file, int32_t line);
	LUX_CORE_API void* dll_lumix_new_aligned(size_t size, size_t alignment, const char* file, int32_t line);
	LUX_CORE_API void* dll_lumix_realloc(void* ptr, size_t size, const char* file, int32_t line);
	LUX_CORE_API void dll_lumix_delete(void* ptr);
	LUX_CORE_API void dll_lumix_delete_aligned(void* ptr);
}

#define LUMIX_NEW(T) new(__FILE__, __LINE__) T
#define LUMIX_NEW_ARRAY(T, count) new(__FILE__, __LINE__) T[count]
#define LUX_DELETE(ptr) delete (ptr);
#define LUX_DELETE_ARRAY(ptr) delete[] (ptr)
#define LUX_MALLOC(size) Lumix::dll_lumix_new(size, __FILE__, __LINE__)
#define LUX_FREE(ptr) Lumix::dll_lumix_delete(ptr)
#define LUX_REALLOC(ptr, size) dll_lumix_realloc(ptr, size, __FILE__, __LINE__)
#define LUX_MALLOC_ALIGNED(size, alignment) Lumix::dll_lumix_new_aligned(size, alignment, __FILE__, __LINE__)
#define LUX_FREE_ALIGNED(ptr) Lumix::dll_lumix_delete_aligned(ptr)