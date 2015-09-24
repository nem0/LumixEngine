#include "../file_iterator.h"
#include "core/string.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


namespace Lumix
{
namespace FS
{

struct FileIterator
{
	HANDLE handle;
	IAllocator* allocator;
	WIN32_FIND_DATAA ffd;
	bool is_valid;
};


FileIterator* createFileIterator(const char* path, IAllocator& allocator)
{
	char tmp[MAX_PATH_LENGTH];
	copyString(tmp, sizeof(tmp), path);
	catString(tmp, sizeof(tmp), "/*");
	auto* iter = allocator.newObject<FileIterator>();
	iter->allocator = &allocator;
	iter->handle = FindFirstFile(tmp, &iter->ffd);
	iter->is_valid = iter->handle != NULL;
	return iter;
}


void destroyFileIterator(FileIterator* iterator)
{
	FindClose(iterator->handle);
	iterator->allocator->deleteObject(iterator);
}


bool getNextFile(FileIterator* iterator, FileInfo* info)
{
	if (!iterator->is_valid) return false;

	info->is_directory =
		(iterator->ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	copyString(info->filename, sizeof(info->filename), iterator->ffd.cFileName);

	iterator->is_valid = FindNextFile(iterator->handle, &iterator->ffd) == TRUE;
	return true;
}


} // namespace FS
} // namespace Lumix