#pragma once

#include "engine/lumix.h"

namespace Lumix
{

struct IAllocator;
class Path;
template <typename T> class Delegate;


class LUMIX_ENGINE_API FileSystem
{
public:
	using ContentCallback = Delegate<void(u64, const u8*, bool)>;

	struct LUMIX_ENGINE_API AsyncHandle {
		static AsyncHandle invalid() { return AsyncHandle(0xffFFffFF); }
		explicit AsyncHandle(u32 value) : value(value) {}
		u32 value;
		bool isValid() const { return value != 0xffFFffFF; }
	};

	static FileSystem* create(const char* base_path, IAllocator& allocator);
	static void destroy(FileSystem* fs);

	virtual ~FileSystem() {}

	virtual void setBasePath(const char* path) = 0;
	virtual const char* getBasePath() const = 0;
	virtual void updateAsyncTransactions() = 0;
	virtual bool hasWork() = 0;

	virtual AsyncHandle getContent(const Path& file, const ContentCallback& callback) = 0;
	virtual void cancel(AsyncHandle handle) = 0;
};


} // namespace Lumix
