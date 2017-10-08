#include "file_utils.h"
#include "engine/fs/os_file.h"


namespace Lumix
{


namespace FS
{


bool makeFile(const char* path, const char* content, IAllocator& allocator)
{
	FS::OsFile file;
	if (!file.open(path, FS::Mode::CREATE_AND_WRITE, allocator)) return false;
	bool success = file.writeText(content);
	file.close();
	return success;
}


} // namespace FS


} // namespace Lumix