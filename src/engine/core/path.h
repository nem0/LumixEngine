#pragma once

#include "core/associative_array.h"
#include "core/base_proxy_allocator.h"
#include "core/default_allocator.h"
#include "core/mt/sync.h"


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
	explicit PathManager(Lumix::IAllocator& allocator);
	~PathManager();

	void serialize(OutputBlob& serializer);
	void deserialize(InputBlob& serializer);
	void setBasePath(const char* path);
	const char* getBasePath();

	void clear();

private:
	PathInternal* getPath(uint32 hash, const char* path);
	PathInternal* getPath(uint32 hash);
	PathInternal* getPathMultithreadUnsafe(uint32 hash, const char* path);
	void incrementRefCount(PathInternal* path);
	void decrementRefCount(PathInternal* path);

private:
	IAllocator& m_allocator;
	AssociativeArray<uint32, PathInternal*> m_paths;
	MT::SpinMutex m_mutex;
	PathInternal* m_empty_path;
	char m_base_path[MAX_PATH_LENGTH];
	int m_base_path_length;
};


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

	uint32 getHash() const { return m_data->m_id; }
	const char* c_str() const { return m_data->m_path; }

	int length() const;
	bool isValid() const { return m_data->m_path[0] != '\0'; }

private:
	PathInternal* m_data;
};


} // namespace Lumix