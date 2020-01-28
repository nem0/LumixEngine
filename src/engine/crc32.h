#pragma once


#include "engine/lumix.h"


namespace Lumix
{


LUMIX_ENGINE_API u32 crc32(const void* data, u32 length);
LUMIX_ENGINE_API u32 crc32(const char* str);
LUMIX_ENGINE_API u32 continueCrc32(u32 original_crc, const char* str);
LUMIX_ENGINE_API u32 continueCrc32(u32 original_crc, const void* data, u32 length);


} // namespace Lumix