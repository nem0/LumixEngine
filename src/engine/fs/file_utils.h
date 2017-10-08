#pragma once


#include "engine/lumix.h"


namespace Lumix
{


struct IAllocator;


namespace FS
{


LUMIX_ENGINE_API bool  makeFile(const char* path, const char* content, IAllocator& allocator);


}


}