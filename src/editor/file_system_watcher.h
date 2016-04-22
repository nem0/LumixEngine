#pragma once

#include "engine/core/delegate.h"
#include "engine/core/path.h"


class LUMIX_EDITOR_API FileSystemWatcher
{
	public:
		virtual ~FileSystemWatcher() {}

		static FileSystemWatcher* create(const char* path, Lumix::IAllocator& allocator);
		static void destroy(FileSystemWatcher* watcher); 
		virtual Lumix::Delegate<void (const char*)>& getCallback() = 0;
};

