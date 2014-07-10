#pragma once


#include "core/lumix.h"


LUMIX_CORE_API uint32_t crc32(const void* data, int length);
LUMIX_CORE_API uint32_t crc32(const char str[]);
