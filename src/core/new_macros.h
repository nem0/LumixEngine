#pragma once

#include "core/lux.h"

namespace Lux 
{
	LUX_CORE_API void* dll_lux_new(size_t size, const char* file, int32_t line);
	LUX_CORE_API void* dll_lux_new(size_t size, const char* file, int32_t line);
	LUX_CORE_API void* dll_lux_new_aligned(size_t size, size_t alignment, const char* file, int32_t line);
	LUX_CORE_API void* dll_lux_realloc(void* ptr, size_t size, const char* file, int32_t line);
	LUX_CORE_API void dll_lux_delete(void* ptr);
	LUX_CORE_API void dll_lux_delete_aligned(void* ptr);
}

#define LUX_NEW(T) new(__FILE__, __LINE__) T
#define LUX_NEW_ARRAY(T, count) new(__FILE__, __LINE__) T[count]
#define LUX_DELETE(ptr) delete (ptr);
#define LUX_DELETE_ARRAY(ptr) delete[] (ptr)
#define LUX_MALLOC(size) Lux::dll_lux_new(size, __FILE__, __LINE__)
#define LUX_FREE(ptr) Lux::dll_lux_delete(ptr)
#define LUX_REALLOC(ptr, size) dll_lux_realloc(ptr, size, __FILE__, __LINE__)
#define LUX_MALLOC_ALIGNED(size, alignment) Lux::dll_lux_new_aligned(size, alignment, __FILE__, __LINE__)
#define LUX_FREE_ALIGNED(ptr) Lux::dll_lux_delete_aligned(ptr)