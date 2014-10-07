#pragma once


#include "core/lumix.h"


LUMIX_CORE_API uint32_t crc32(const void* data, int length);
LUMIX_CORE_API uint32_t crc32(const char str[]);
LUMIX_CORE_API uint32_t continueCrc32(uint32_t original_crc, const char str[]);
