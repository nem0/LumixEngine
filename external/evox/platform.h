#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t ex_platform_page_size(void);
void* ex_platform_reserve(size_t size);
bool ex_platform_commit(void* address, size_t size);
void ex_platform_release(void* address, size_t size);
double ex_platform_now_ms(void);
void* ex_platform_allocate(size_t size, size_t align);
void ex_platform_deallocate(void* ptr);

#ifdef __cplusplus
}
#endif
