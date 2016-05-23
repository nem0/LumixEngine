#include "engine/mt/task.h"
#include "engine/profiler.h"
#include "engine/string.h"
#include "file_system_watcher.h"

/*
class FileSystemWatcherPC;


static const DWORD READ_DIR_CHANGE_FILTER =
	FILE_NOTIFY_CHANGE_SECURITY | FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_LAST_ACCESS |
	FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_ATTRIBUTES |
	FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_FILE_NAME;


struct FileSystemWatcherTask : public Lumix::MT::Task
{
	FileSystemWatcherTask(const char* path,
		FileSystemWatcherPC& watcher,
		Lumix::IAllocator& allocator)
		: Task(allocator)
		, m_watcher(watcher)
	{
		Lumix::copyString(m_path, path);
	}


	int task() override;

	volatile bool m_finished;
	FILE_NOTIFY_INFORMATION m_info[10];
	HANDLE m_handle;
	DWORD m_received;
	OVERLAPPED m_overlapped;
	char m_path[Lumix::MAX_PATH_LENGTH];
	FileSystemWatcherPC& m_watcher;
};
*/

class FileSystemWatcherPC : public FileSystemWatcher
{
public:
	explicit FileSystemWatcherPC(Lumix::IAllocator& allocator)
		: m_allocator(allocator)
	{
	}


	virtual Lumix::Delegate<void(const char*)>& getCallback() { return m_callback; }


	Lumix::Delegate<void(const char*)> m_callback;
	Lumix::IAllocator& m_allocator;
};


FileSystemWatcher* FileSystemWatcher::create(const char* path, Lumix::IAllocator& allocator)
{
	FileSystemWatcherPC* watcher = LUMIX_NEW(allocator, FileSystemWatcherPC)(allocator);
	return watcher;
}


void FileSystemWatcher::destroy(FileSystemWatcher* watcher)
{
	if (!watcher) return;
	FileSystemWatcherPC* pc_watcher = (FileSystemWatcherPC*)watcher;
	LUMIX_DELETE(pc_watcher->m_allocator, pc_watcher);
}

