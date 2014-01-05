#include "core/path.h"
#include "core/atomic.h"
#include "core/crc32.h"
#include "core/file_system.h"
#include "core/mutex.h"
#include "core/path_manager.h"


namespace Lux
{
namespace FS
{


Path::Path(const char* path, FileSystem& file_system)
	: m_file_system(file_system)
{
	char normalized[MAX_LENGTH];
	normalize(path, normalized);
	m_path_string = file_system.getPathManager().addReference(normalized, crc32(normalized));
}


Path::Path(const Path& path)
	: m_file_system(path.m_file_system)
{
	m_path_string = m_file_system.getPathManager().addReference(path.getCString(), path.getHash());
}


void Path::normalize(const char* src, char* dest)
{
	const char* src_c = src;
	char* dest_c = dest;
	while(*src_c)
	{
		char c = *src_c;
		if(c != '\\')
		{
			*dest_c = c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
		}
		else
		{
			*dest_c = '/';
		}
		++src_c;
		++dest_c;
	}
	*dest_c = '\0';
}


Path::~Path()
{
	m_file_system.getPathManager().removeReference(m_path_string->m_hash);
}


} // ~namespace Path
} // ~namspace Lux