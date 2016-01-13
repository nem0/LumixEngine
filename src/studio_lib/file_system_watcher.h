#pragma once

#include "core/delegate.h"
#include "core/path.h"


class LUMIX_STUDIO_LIB_API FileSystemWatcher
{
	public:
		virtual ~FileSystemWatcher() {}

		static FileSystemWatcher* create(const char* path, Lumix::IAllocator& allocator);
		static void destroy(FileSystemWatcher* watcher); 
		virtual Lumix::Delegate<void (const char*)>& getCallback() = 0;
};

