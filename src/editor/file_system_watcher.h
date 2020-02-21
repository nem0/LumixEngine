#pragma once

#include "engine/delegate.h"


namespace Lumix
{

struct LUMIX_EDITOR_API FileSystemWatcher
{
	virtual ~FileSystemWatcher() {}

	static FileSystemWatcher* create(const char* path, struct IAllocator& allocator);
	static void destroy(FileSystemWatcher* watcher); 
	virtual Delegate<void (const char*)>& getCallback() = 0;
};


} // namespace Lumix