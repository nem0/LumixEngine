#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t ls_platform_page_size(void);
void* ls_platform_reserve(size_t size);
bool ls_platform_commit(void* address, size_t size);
void ls_platform_release(void* address, size_t size);
double ls_platform_now_ms(void);

#ifdef __cplusplus
}
#endif
