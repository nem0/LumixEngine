#include "engine/lumix.h"
#include "engine/path.h"

#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/mt/sync.h"
#include "engine/path_utils.h"
#include "engine/string.h"


namespace Lumix
{

	static PathManager* g_path_manager = nullptr;


	PathManager::PathManager(Lumix::IAllocator& allocator)
		: m_paths(allocator)
		, m_mutex(false)
		, m_allocator(allocator)
	{
		g_path_manager = this;
		m_empty_path = getPath(0, "");
	}


	PathManager::~PathManager()
	{
		decrementRefCount(m_empty_path);
		m_empty_path = nullptr;
		ASSERT(m_paths.size() == 0);
		g_path_manager = nullptr;
	}


	void PathManager::serialize(OutputBlob& serializer)
	{
		MT::SpinLock lock(m_mutex);
		clear();
		serializer.write((i32)m_paths.size());
		for (int i = 0; i < m_paths.size(); ++i)
		{
			serializer.writeString(m_paths.at(i)->m_path);
		}
	}


	void PathManager::deserialize(InputBlob& serializer)
	{
		MT::SpinLock lock(m_mutex);
		i32 size;
		serializer.read(size);
		for (int i = 0; i < size; ++i)
		{
			char path[MAX_PATH_LENGTH];
			serializer.readString(path, sizeof(path));
			u32 hash = crc32(path);
			PathInternal* internal = getPathMultithreadUnsafe(hash, path);
			--internal->m_ref_count;
		}
	}


	Path::Path()
	{
		m_data = g_path_manager->getPath(0, "");
	}


	Path::Path(u32 hash)
	{
		m_data = g_path_manager->getPath(hash);
		ASSERT(m_data);
	}


	PathInternal* PathManager::getPath(u32 hash)
	{
		MT::SpinLock lock(m_mutex);
		int index = m_paths.find(hash);
		if (index < 0)
		{
			return nullptr;
		}
		++m_paths.at(index)->m_ref_count;
		return m_paths.at(index);
	}


	PathInternal* PathManager::getPath(u32 hash, const char* path)
	{
		MT::SpinLock lock(m_mutex);
		return getPathMultithreadUnsafe(hash, path);
	}


	void PathManager::clear()
	{
		for (int i = m_paths.size() - 1; i >= 0; --i)
		{
			if (m_paths.at(i)->m_ref_count == 0)
			{
				LUMIX_DELETE(m_allocator, m_paths.at(i));
				m_paths.eraseAt(i);
			}
		}
	}


	PathInternal* PathManager::getPathMultithreadUnsafe(u32 hash, const char* path)
	{
		int index = m_paths.find(hash);
		if (index < 0)
		{
			PathInternal* internal = LUMIX_NEW(m_allocator, PathInternal);
			internal->m_ref_count = 1;
			internal->m_id = hash;
			copyString(internal->m_path, path);
			m_paths.insert(hash, internal);
			return internal;
		}
		else
		{
			++m_paths.at(index)->m_ref_count;
			return m_paths.at(index);
		}
	}


	void PathManager::incrementRefCount(PathInternal* path)
	{
		MT::SpinLock lock(m_mutex);
		++path->m_ref_count;
	}


	void PathManager::decrementRefCount(PathInternal* path)
	{
		MT::SpinLock lock(m_mutex);
		--path->m_ref_count;
		if (path->m_ref_count == 0)
		{
			m_paths.erase(path->m_id);
			LUMIX_DELETE(m_allocator, path);
		}
	}


	Path::Path(const Path& rhs)
		: m_data(rhs.m_data)
	{
		g_path_manager->incrementRefCount(m_data);
	}


	Path::Path(const char* path)
	{
		char tmp[MAX_PATH_LENGTH];
		size_t len = stringLength(path);
		ASSERT(len < MAX_PATH_LENGTH);
		PathUtils::normalize(path, tmp, (u32)len + 1);
		u32 hash = crc32(tmp);
		m_data = g_path_manager->getPath(hash, tmp);
	}


	Path::~Path()
	{
		g_path_manager->decrementRefCount(m_data);
	}


	int Path::length() const
	{
		return stringLength(m_data->m_path);
	}


	void Path::operator =(const Path& rhs)
	{
		g_path_manager->decrementRefCount(m_data);
		m_data = rhs.m_data;
		g_path_manager->incrementRefCount(m_data);
	}


	void Path::operator =(const char* rhs)
	{
		g_path_manager->decrementRefCount(m_data);
		char tmp[MAX_PATH_LENGTH];
		size_t len = stringLength(rhs);
		ASSERT(len < MAX_PATH_LENGTH);
		PathUtils::normalize(rhs, tmp, (u32)len + 1);
		u32 hash = crc32(tmp);
		m_data = g_path_manager->getPath(hash, tmp);
	}


} // namespace Lumix
