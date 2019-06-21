#pragma once

#include "engine/lumix.h"


namespace Lumix
{


struct IAllocator;
struct IInputStream;
struct IOutputStream;


class LUMIX_ENGINE_API Path
{
public:
	Path();
	Path(const Path& rhs);
	Path(const char* s1, const char* s2);
	Path(const char* s1, const char* s2, const char* s3);
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
	class PathInternal* m_data;
};


struct LUMIX_ENGINE_API PathManager
{
	static PathManager* create(IAllocator& allocator);
	static void destroy(PathManager& obj);
	static const Path& PathManager::getEmptyPath();
	
	virtual ~PathManager() {};

	virtual void serialize(IOutputStream& serializer) = 0;
	virtual void deserialize(IInputStream& serializer) = 0;

	virtual void clear() = 0;
};


} // namespace Lumix
