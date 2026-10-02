#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "api.h"

#ifdef __cplusplus
extern "C" {
#endif

EVOX_API size_t ex_platform_page_size(void);
EVOX_API void* ex_platform_reserve(size_t size);
EVOX_API bool ex_platform_commit(void* address, size_t size);
EVOX_API void ex_platform_release(void* address, size_t size);
EVOX_API double ex_platform_now_ms(void);
EVOX_API void* ex_platform_allocate(size_t size, size_t align);
EVOX_API void ex_platform_deallocate(void* ptr);

#ifdef __cplusplus
}
#endif
