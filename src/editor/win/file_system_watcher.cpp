#include "engine/mt/task.h"
#include "engine/profiler.h"
#include "engine/string.h"
#include "file_system_watcher.h"

#include "engine/win/simple_win.h"


class FileSystemWatcherPC;


static const DWORD READ_DIR_CHANGE_FILTER =
	FILE_NOTIFY_CHANGE_SECURITY | FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_LAST_ACCESS |
	FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_ATTRIBUTES |
	FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_FILE_NAME;


struct FileSystemWatcherTask LUMIX_FINAL : public Lumix::MT::Task
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


class FileSystemWatcherPC LUMIX_FINAL : public FileSystemWatcher
{
public:
	explicit FileSystemWatcherPC(Lumix::IAllocator& allocator)
		: m_allocator(allocator)
	{
		m_task = nullptr;
	}


	virtual ~FileSystemWatcherPC()
	{
		if (m_task)
		{
			CancelIoEx(m_task->m_handle, nullptr);
			CloseHandle(m_task->m_handle);

			m_task->destroy();
			LUMIX_DELETE(m_allocator, m_task);
		}
	}


	bool start(LPCSTR path)
	{
		m_task = LUMIX_NEW(m_allocator, FileSystemWatcherTask)(path, *this, m_allocator);
		if (!m_task->create("FileSystemWatcher"))
		{
			LUMIX_DELETE(m_allocator, m_task);
			m_task = nullptr;
			return false;
		}
		return true;
	}


	virtual Lumix::Delegate<void(const char*)>& getCallback() { return m_callback; }


	Lumix::Delegate<void(const char*)> m_callback;
	Lumix::IAllocator& m_allocator;
	FileSystemWatcherTask* m_task;
};


FileSystemWatcher* FileSystemWatcher::create(const char* path, Lumix::IAllocator& allocator)
{
	FileSystemWatcherPC* watcher = LUMIX_NEW(allocator, FileSystemWatcherPC)(allocator);
	if (!watcher->start(path))
	{
		LUMIX_DELETE(allocator, watcher);
		return nullptr;
	}
	return watcher;
}


void FileSystemWatcher::destroy(FileSystemWatcher* watcher)
{
	if (!watcher) return;
	FileSystemWatcherPC* pc_watcher = (FileSystemWatcherPC*)watcher;
	LUMIX_DELETE(pc_watcher->m_allocator, pc_watcher);
}


static void wcharToCharArray(const WCHAR* src, char* dest, int len)
{
	for (unsigned int i = 0; i < len / sizeof(WCHAR); ++i)
	{
		dest[i] = static_cast<char>(src[i]);
	}
	dest[len / sizeof(WCHAR)] = '\0';
}


static void CALLBACK notif(DWORD status, DWORD tferred, LPOVERLAPPED over)
{
	auto* task = (FileSystemWatcherTask*)over->hEvent;
	if (status == ERROR_OPERATION_ABORTED)
	{
		task->m_finished = true;
		return;
	}

	FILE_NOTIFY_INFORMATION* info = &task->m_info[0];
	while (info)
	{
		auto action = info->Action;
		switch (action)
		{
			case FILE_ACTION_RENAMED_NEW_NAME:
			case FILE_ACTION_ADDED:
			case FILE_ACTION_MODIFIED:
			case FILE_ACTION_REMOVED:
			{
				char tmp[MAX_PATH];
				wcharToCharArray(info->FileName, tmp, info->FileNameLength);
				task->m_watcher.m_callback.invoke(tmp);
			}
			break;
			case FILE_ACTION_RENAMED_OLD_NAME: break;
			default: ASSERT(false); break;
		}
		info = info->NextEntryOffset == 0
				   ? nullptr
				   : (FILE_NOTIFY_INFORMATION*)(((char*)info) + info->NextEntryOffset);
	}
}


int FileSystemWatcherTask::task()
{
	m_handle = CreateFile(m_path,
		FILE_LIST_DIRECTORY,
		FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		nullptr);
	if (m_handle == INVALID_HANDLE_VALUE) return -1;

	Lumix::setMemory(&m_overlapped, 0, sizeof(m_overlapped));
	m_overlapped.hEvent = this;
	m_finished = false;
	while (!m_finished)
	{
		PROFILE_BLOCK("Change handling");
		BOOL status = ReadDirectoryChangesW(m_handle,
			m_info,
			sizeof(m_info),
			TRUE,
			READ_DIR_CHANGE_FILTER,
			&m_received,
			&m_overlapped,
			&notif);
		if (status == FALSE) break;
		SleepEx(INFINITE, TRUE);
	}
	return 0;
}