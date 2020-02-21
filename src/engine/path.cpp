#include "engine/lumix.h"
#include "engine/path.h"

#include "engine/associative_array.h"
#include "engine/crc32.h"
#include "engine/sync.h"
#include "engine/path.h"
#include "engine/stream.h"
#include "engine/string.h"


namespace Lumix
{

struct PathManagerImpl;
static PathManagerImpl* g_path_manager = nullptr;


struct PathManagerImpl : PathManager
{
	PathManagerImpl(IAllocator& allocator)
		: m_paths(allocator)
		, m_allocator(allocator)
	{
		g_path_manager = this;
		m_empty_path = LUMIX_NEW(m_allocator, Path)();
	}

	~PathManagerImpl() override {
		LUMIX_DELETE(m_allocator, m_empty_path);
		ASSERT(m_paths.size() == 0);
		g_path_manager = nullptr;
	}

	void serialize(IOutputStream& serializer) override {
		MutexGuard lock(m_mutex);
		clear();
		serializer.write((i32)m_paths.size());
		for (int i = 0; i < m_paths.size(); ++i) {
			serializer.writeString(m_paths.at(i)->m_path);
		}
	}

	void deserialize(IInputStream& serializer) override {
		MutexGuard lock(m_mutex);
		i32 size;
		serializer.read(size);
		for (int i = 0; i < size; ++i) {
			char path[MAX_PATH_LENGTH];
			serializer.readString(Span(path));
			u32 hash = crc32(path);
			PathInternal* internal = getPathMultithreadUnsafe(hash, path);
			--internal->m_ref_count;
		}
	}

	void clear() override {
		for (int i = m_paths.size() - 1; i >= 0; --i) {
			if (m_paths.at(i)->m_ref_count == 0) {
				LUMIX_DELETE(m_allocator, m_paths.at(i));
				m_paths.eraseAt(i);
			}
		}
	}

	PathInternal* getPath(u32 hash, const char* path) {
		MutexGuard lock(m_mutex);
		return getPathMultithreadUnsafe(hash, path);
	}

	PathInternal* getPath(u32 hash) {
		MutexGuard lock(m_mutex);
		int index = m_paths.find(hash);
		if (index < 0) {
			return nullptr;
		}
		++m_paths.at(index)->m_ref_count;
		return m_paths.at(index);
	}

	PathInternal* getPathMultithreadUnsafe(u32 hash, const char* path) {
		int index = m_paths.find(hash);
		if (index < 0) {
			PathInternal* internal = LUMIX_NEW(m_allocator, PathInternal);
			internal->m_ref_count = 1;
			internal->m_id = hash;
			copyString(internal->m_path, path);
			m_paths.insert(hash, internal);
			return internal;
		}
		++m_paths.at(index)->m_ref_count;
		return m_paths.at(index);
	}

	void incrementRefCount(PathInternal* path) {
		MutexGuard lock(m_mutex);
		++path->m_ref_count;
	}

	void decrementRefCount(PathInternal* path) {
		MutexGuard lock(m_mutex);
		--path->m_ref_count;
		if (path->m_ref_count == 0) {
			m_paths.erase(path->m_id);
			LUMIX_DELETE(m_allocator, path);
		}
	}

	IAllocator& m_allocator;
	AssociativeArray<u32, PathInternal*> m_paths;
	Mutex m_mutex;
	Path* m_empty_path;
};


PathManager* PathManager::create(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, PathManagerImpl)(allocator);
}


void PathManager::destroy(PathManager& obj)
{
	LUMIX_DELETE(((PathManagerImpl&)obj).m_allocator, &obj);
}


const Path& PathManager::getEmptyPath()
{
	return *g_path_manager->m_empty_path;
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
	Path::normalize(path, Span(tmp, (u32)len + 1));
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
	Path::normalize(rhs, Span(tmp, (u32)len + 1));
	u32 hash = crc32(tmp);
	m_data = g_path_manager->getPath(hash, tmp);
}


bool Path::operator==(const Path& rhs) const
{
	return m_data->m_id == rhs.m_data->m_id;
}


bool Path::operator!=(const Path& rhs) const
{
	return m_data->m_id != rhs.m_data->m_id;
}


u32 Path::getHash() const
{
	return m_data->m_id;
}


const char* Path::c_str() const
{
	return m_data->m_path;
}


bool Path::isValid() const
{
	return m_data->m_path[0] != '\0';
}


void Path::normalize(const char* path, Span<char> output)
{
	char* out = output.begin();
	u32 max_size = output.length();
	ASSERT(max_size > 0);
	u32 i = 0;

	bool is_prev_slash = false;

	if (path[0] == '.' && (path[1] == '\\' || path[1] == '/'))
		path += 2;
	#ifdef _WIN32
		if (path[0] == '\\' || path[0] == '/')
			++path;
	#endif
	while (*path != '\0' && i < max_size)
	{
		bool is_current_slash = *path == '\\' || *path == '/';

		if (is_current_slash && is_prev_slash)
		{
			++path;
			continue;
		}

		*out = *path == '\\' ? '/' : *path;
		#ifdef _WIN32
			*out = *path >= 'A' && *path <= 'Z' ? *path - 'A' + 'a' : *out;
		#endif

		path++;
		out++;
		i++;

		is_prev_slash = is_current_slash;
	}
	(i < max_size ? *out : *(out - 1)) = '\0';
}

void Path::getDir(Span<char> dir, const char* src)
{
	copyString(dir, src);
	for (int i = stringLength(dir.begin()) - 1; i >= 0; --i)
	{
		if (dir[i] == '\\' || dir[i] == '/')
		{
			++i;
			dir[i] = '\0';
			return;
		}
	}
	dir[0] = '\0';
}

void Path::getBasename(Span<char> basename, const char* src)
{
	basename[0] = '\0';
	for (int i = stringLength(src) - 1; i >= 0; --i)
	{
		if (src[i] == '\\' || src[i] == '/' || i == 0)
		{
			if (src[i] == '\\' || src[i] == '/')
				++i;
			u32 j = 0;
			basename[j] = src[i];
			while (j < basename.length() - 1 && src[i + j] && src[i + j] != '.')
			{
				++j;
				basename[j] = src[j + i];
			}
			basename[j] = '\0';
			return;
		}
	}
}


void Path::getExtension(Span<char> extension, Span<const char> src)
{
	ASSERT(extension.length() > 0);
	for (int i = src.length() - 1; i >= 0; --i)
	{
		if (src[i] == '.')
		{
			++i;
			Span<const char> tmp = { src.begin() + i, src.end() };
			copyString(extension, tmp);
			return;
		}
	}
	extension[0] = '\0';
}


bool Path::replaceExtension(char* path, const char* ext)
{
	char* end = path + stringLength(path);
	while (end > path && *end != '.')
	{
		--end;
	}
	if (*end != '.') return false;

	++end;
	const char* src = ext;
	while (*src != '\0' && *end != '\0')
	{
		*end = *src;
		++end;
		++src;
	}
	bool copied_whole_ext = *src == '\0';
	if (!copied_whole_ext) return false;

	*end = '\0';
	return true;
}


bool Path::hasExtension(const char* filename, const char* ext)
{
	char tmp[20];
	getExtension(Span(tmp), Span(filename, stringLength(filename)));
	makeLowercase(Span(tmp), tmp);

	return equalStrings(tmp, ext);
}

PathInfo::PathInfo(const char* path) {
	char tmp[MAX_PATH_LENGTH];
	Path::normalize(path, Span(tmp));
	Path::getExtension(Span(m_extension), Span(tmp, stringLength(tmp)));
	Path::getBasename(Span(m_basename), tmp);
	Path::getDir(Span(m_dir), tmp);
}



} // namespace Lumix
