#pragma once

#include "engine/black.h.h"

namespace black
{

template <typename T> struct Delegate;
template <typename T> struct UniquePtr;

struct BLACK_EDITOR_API FileSystemWatcher
{
	virtual ~FileSystemWatcher() {}

	static UniquePtr<FileSystemWatcher> create(const char* path, struct IAllocator& allocator);
	virtual Delegate<void (const char*)>& getCallback() = 0;
};


} // namespace black