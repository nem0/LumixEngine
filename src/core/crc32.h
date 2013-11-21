#pragma once


#include "core/lux.h"


LUX_CORE_API unsigned int crc32(const void* data, int length);
LUX_CORE_API unsigned int crc32(const char str[]);
