#pragma once


#include "engine/lumix.h"


namespace Lumix
{


LUMIX_ENGINE_API uint32 crc32(const void* data, int length);
LUMIX_ENGINE_API uint32 crc32(const char* str);
LUMIX_ENGINE_API uint32 continueCrc32(uint32 original_crc, const char* str);


} // namespace Lumix