#pragma once

#include "engine/lumix.h"

namespace Lumix
{

struct IAllocator;
class OutputBlob;
class Path;
template <typename T> class Delegate;


namespace FS
{


using OpenCallback = Delegate<void()>;
using ContentCallback = Delegate<void(u64, const u8*, bool)>;
using ReadCallback = Delegate<void(u64, u64, void*)>;


struct AsyncHandle {
	static AsyncHandle invalid() { return AsyncHandle(0xffFFffFF); }
	explicit AsyncHandle(u32 value) : value(value) {}
	u32 value;
	bool isValid() const { return value != 0xffFFffFF; }
};


class LUMIX_ENGINE_API FileSystem
{
public:
	static FileSystem* create(const char* base_path, IAllocator& allocator);
	static void destroy(FileSystem* fs);

	virtual ~FileSystem() {}
	
	virtual const char* getBasePath() const = 0;

	virtual AsyncHandle getContent(const Path& file, const ContentCallback& callback) = 0;
	virtual AsyncHandle open(const Path& file, const OpenCallback& callback) = 0;
	virtual void read(AsyncHandle handle, u64 offset, u64 size, const ReadCallback& callback) = 0;
	virtual void cancel(AsyncHandle handle) = 0;

	virtual void close(AsyncHandle handle) = 0;

	virtual void updateAsyncTransactions() = 0;

	virtual bool hasWork() const = 0;
};


} // namespace FS
} // namespace Lumix
