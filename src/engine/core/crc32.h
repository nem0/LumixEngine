#pragma once


#include "lumix.h"


namespace Lumix
{


LUMIX_ENGINE_API uint32_t crc32(const void* data, int length);
LUMIX_ENGINE_API uint32_t crc32(const char str[]);
LUMIX_ENGINE_API uint32_t continueCrc32(uint32_t original_crc,
										const char str[]);


} // namespace Lumix