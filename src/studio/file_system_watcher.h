#pragma once

#include "core/delegate.h"
#include "core/path.h"


class QString;


class FileSystemWatcher
{
	public:
		virtual ~FileSystemWatcher() {}

		static FileSystemWatcher* create(const QString& path);
		static void destroy(FileSystemWatcher* watcher); 
		virtual Lumix::Delegate<void (const char*)>& getCallback() = 0;
};

