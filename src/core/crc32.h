#pragma once


#include "core/lumix.h"


LUX_CORE_API uint32_t crc32(const void* data, int length);
LUX_CORE_API uint32_t crc32(const char str[]);
