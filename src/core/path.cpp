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


	Path::Path()
	{
		m_path_string = NULL;
	}


	Path::Path(const char* path, FileSystem& file_system)
	{
		char normalized[MAX_LENGTH];
		normalize(path, normalized);
		m_path_string = file_system.getPathManager().addReference(normalized, crc32(normalized));
	}


	Path::Path(const Path& path)
	{
		m_path_string = path.isValid() ? path.m_path_string->m_path_manager->addReference(*path.m_path_string) : NULL;
	}


	void Path::operator=(const Path& path)
	{
		if(isValid())
		{
			m_path_string->m_path_manager->removeReference(*m_path_string);
		}
		if(path.isValid())
		{
			m_path_string = path.m_path_string->m_path_manager->addReference(*path.m_path_string);
		}
		else
		{
			m_path_string = NULL;
		}
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
		if(isValid())
		{
			m_path_string->m_path_manager->removeReference(*m_path_string);
		}
	}


} // ~namespace Path
} // ~namspace Lux