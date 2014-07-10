#pragma once

#include "core/delegate.h"
#include "core/path.h"


class FileSystemWatcher
{
	public:
		static FileSystemWatcher* create(const Lumix::Path& path);
		static void destroy(FileSystemWatcher* watcher); 
		virtual Lumix::Delegate<void (const char*)>& getCallback() = 0;
};

