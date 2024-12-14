#pragma once

#include "engine/lumix.h"

namespace Lumix
{

template <typename T> struct Delegate;
template <typename T> struct UniquePtr;

struct LUMIX_EDITOR_API FileSystemWatcher
{
	virtual ~FileSystemWatcher() {}

	static UniquePtr<FileSystemWatcher> create(const char* path, struct IAllocator& allocator);
	// TODO on windows, this always returns lower case path
	virtual Delegate<void (const char*)>& getCallback() = 0;
};


} // namespace Lumix