#pragma once

#include "core/associative_array.h"
#include "core/base_proxy_allocator.h"
#include "core/default_allocator.h"
#include "core/mt/sync.h"
#include "core/string.h"

namespace Lumix
{


class InputBlob;
class OutputBlob;


class PathInternal
{
public:
	char m_path[MAX_PATH_LENGTH];
	uint32 m_id;
	volatile int32 m_ref_count;
};


class LUMIX_ENGINE_API PathManager
{
	friend class Path;

public:
	PathManager();
	~PathManager();

	void serialize(OutputBlob& serializer);
	void deserialize(InputBlob& serializer);

	void clear();

private:
	PathInternal* getPath(uint32 hash, const char* path);
	PathInternal* getPath(uint32 hash);
	PathInternal* getPathMultithreadUnsafe(uint32 hash, const char* path);
	void incrementRefCount(PathInternal* path);
	void decrementRefCount(PathInternal* path);

private:
	DefaultAllocator m_src_allocator;
	BaseProxyAllocator m_allocator;
	AssociativeArray<uint32, PathInternal*> m_paths;
	MT::SpinMutex m_mutex;
	PathInternal* m_empty_path;
};


extern PathManager LUMIX_ENGINE_API g_path_manager;


class LUMIX_ENGINE_API Path
{
public:
	Path();
	Path(const Path& rhs);
	explicit Path(uint32 hash);
	explicit Path(const char* path);
	void operator=(const Path& rhs);
	void operator=(const char* rhs);
	bool operator==(const Path& rhs) const
	{
		return m_data->m_id == rhs.m_data->m_id;
	}

	~Path();

	operator const char*() const { return m_data->m_path; }
	operator uint32() const { return m_data->m_id; }
	uint32 getHash() const { return m_data->m_id; }

	const char* c_str() const { return m_data->m_path; }
	size_t length() const { return stringLength(m_data->m_path); }
	bool isValid() const { return m_data->m_path[0] != '\0'; }

private:
	PathInternal* m_data;
};


} // namespace Lumix