#pragma once

#include "engine/delegate.h"


namespace Lumix
{

template <typename T> struct UniquePtr;

struct LUMIX_EDITOR_API FileSystemWatcher
{
	virtual ~FileSystemWatcher() {}

	static UniquePtr<FileSystemWatcher> create(const char* path, struct IAllocator& allocator);
	virtual Delegate<void (const char*)>& getCallback() = 0;
};


} // namespace Lumix