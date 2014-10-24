#include "core/lumix.h"
#include "core/path.h"

#include "core/crc32.h"
#include "core/path_utils.h"
#include "core/string.h"

#include <string>

namespace Lumix
{
	Path::Path(const Path& rhs)
		: m_id(rhs.m_id)
	{
		size_t len = strlen(rhs.m_path);
		ASSERT(len < LUMIX_MAX_PATH);
		copyString(m_path, sizeof(m_path), rhs.m_path);
	}

	Path::Path(const char* path)
	{
		size_t len = strlen(path);
		ASSERT(len < LUMIX_MAX_PATH);
		PathUtils::normalize(path, m_path, len + 1);
		m_id = crc32(m_path);
	}

	Path::Path(const string& path)
	{
		size_t len = path.length();
		ASSERT(len < LUMIX_MAX_PATH);
		PathUtils::normalize(path.c_str(), m_path, len + 1);
		m_id = crc32(m_path);
	}

	Path::Path(uint32_t id, const char* path)
		: m_id(id)
	{
		copyString(m_path, sizeof(m_path), path);
	}

	Path::~Path()
	{
	}

	void Path::operator =(const Path& rhs)
	{
		m_id = rhs.m_id;
		copyString(m_path, sizeof(m_path), rhs.m_path);
	}
}