#pragma once


#include "lumix.h"
#include "core/iallocator.h"


namespace Lumix
{
namespace FS
{


struct FileInfo
{
	bool is_directory;
	char filename[MAX_PATH_LENGTH];
};

struct FileIterator;

LUMIX_ENGINE_API FileIterator* createFileIterator(const char* path, IAllocator& allocator);
LUMIX_ENGINE_API void destroyFileIterator(FileIterator* iterator);
LUMIX_ENGINE_API bool getNextFile(FileIterator* iterator, FileInfo* info);


} // namespace FS
} // namespace Lumix