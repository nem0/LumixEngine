#pragma once

#include "engine/delegate.h"
#include "engine/path.h"


class LUMIX_EDITOR_API FileSystemWatcher
{
	public:
		virtual ~FileSystemWatcher() {}

		static FileSystemWatcher* create(const char* path, Lumix::IAllocator& allocator);
		static void destroy(FileSystemWatcher* watcher); 
		virtual Lumix::Delegate<void (const char*)>& getCallback() = 0;
};

