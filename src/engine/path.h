#pragma once

#include "engine/lumix.h"


namespace Lumix
{

struct LUMIX_ENGINE_API PathInfo
{
	explicit PathInfo(const char* path);

	char m_extension[10];
	char m_basename[MAX_PATH_LENGTH];
	char m_dir[MAX_PATH_LENGTH];
};

struct PathInternal
{
	char m_path[MAX_PATH_LENGTH];
	u32 m_id;
	volatile i32 m_ref_count;
};

struct LUMIX_ENGINE_API Path
{
public:
	static void normalize(const char* path, Span<char> out);
	static void getDir(Span<char> dir, const char* src);
	static void getBasename(Span<char> basename, const char* src);
	static void getExtension(Span<char> extension, Span<const char> src);
	static bool hasExtension(const char* filename, const char* ext);
	static bool replaceExtension(char* path, const char* ext);

public:
	Path();
	Path(const Path& rhs);
	explicit Path(u32 hash);
	explicit Path(const char* path);
	~Path();

	void operator=(const Path& rhs);
	void operator=(const char* rhs);
	bool operator==(const Path& rhs) const;
	bool operator!=(const Path& rhs) const;

	int length() const;
	u32 getHash() const;
	const char* c_str() const;
	bool isValid() const;

private:
	PathInternal* m_data;
};


struct LUMIX_ENGINE_API PathManager
{
	static PathManager* create(struct IAllocator& allocator);
	static void destroy(PathManager& obj);
	static const Path& getEmptyPath();
	
	virtual ~PathManager() {};

	virtual void serialize(struct OutputMemoryStream& serializer) = 0;
	virtual void deserialize(struct InputMemoryStream& serializer) = 0;

	virtual void clear() = 0;
};


} // namespace Lumix
