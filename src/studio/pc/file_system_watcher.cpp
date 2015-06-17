#include "file_system_watcher.h"
#include <qstring.h>
#include <windows.h>

static const DWORD READ_DIR_CHANGE_FILTER = 
	FILE_NOTIFY_CHANGE_SECURITY|
	FILE_NOTIFY_CHANGE_CREATION|
	FILE_NOTIFY_CHANGE_LAST_ACCESS|
	FILE_NOTIFY_CHANGE_LAST_WRITE|
	FILE_NOTIFY_CHANGE_SIZE|
	FILE_NOTIFY_CHANGE_ATTRIBUTES|
	FILE_NOTIFY_CHANGE_DIR_NAME|
	FILE_NOTIFY_CHANGE_FILE_NAME;

class FileSystemWatcherPC : public FileSystemWatcher
{
	public:
		static void wcharToCharArray(const WCHAR* src, char* dest, int len)
		{
			for(unsigned int i = 0; i < len / sizeof(WCHAR); ++i)
			{
				dest[i] = static_cast<char>(src[i]); 
			}
			dest[len / sizeof(WCHAR)] = '\0';
		}

		void start(LPCWSTR path)
		{
			m_overlapped.hEvent = this;
			m_handle = CreateFile(path, FILE_LIST_DIRECTORY, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
			ReadDirectoryChangesW(m_handle, m_info, sizeof(m_info), TRUE, READ_DIR_CHANGE_FILTER, &m_received, &m_overlapped, callback);
		}

		static void CALLBACK callback(DWORD errorCode, DWORD tferred, LPOVERLAPPED over)
		{
			ASSERT(errorCode == 0);
			FileSystemWatcherPC* watcher = (FileSystemWatcherPC*)over->hEvent;
			if(tferred > 0)
			{
				FILE_NOTIFY_INFORMATION* info = &watcher->m_info[0];
				while(info)
				{
					switch(info->Action)
					{
						case FILE_ACTION_RENAMED_NEW_NAME:
						case FILE_ACTION_ADDED:
						case FILE_ACTION_MODIFIED:
							{
								char tmp[MAX_PATH];
								wcharToCharArray(info->FileName, tmp, info->FileNameLength);
								watcher->m_callback.invoke(tmp);
//								watcher->m_asset_browser->emitFileChanged(tmp);
							}
							break;
						case FILE_ACTION_RENAMED_OLD_NAME:
						case FILE_ACTION_REMOVED:
							//do not do anything
							break;
						default:
							ASSERT(false);
							break;
					}
					info = info->NextEntryOffset == 0 ? NULL : (FILE_NOTIFY_INFORMATION*)(((char*)info) + info->NextEntryOffset);
				}
			}
			BOOL b = ReadDirectoryChangesW(watcher->m_handle, watcher->m_info, sizeof(watcher->m_info), TRUE, READ_DIR_CHANGE_FILTER, &watcher->m_received, &watcher->m_overlapped, callback);
			ASSERT(b);
		}
		

		virtual Lumix::Delegate<void (const char*)>& getCallback() 
		{
			return m_callback;
		}
		
		FILE_NOTIFY_INFORMATION m_info[10];
		HANDLE m_handle;
		DWORD m_received;
		OVERLAPPED m_overlapped;
		Lumix::Delegate<void (const char*)> m_callback;
};


FileSystemWatcher* FileSystemWatcher::create(const QString& path)
{
	FileSystemWatcherPC* watcher = new FileSystemWatcherPC();
	TCHAR win_path[MAX_PATH];
	for(int i = 0; i < path.length(); ++i)
	{
		win_path[i] = (TCHAR)path[i].toLatin1();
	}
	win_path[path.length()] = 0;
	watcher->start(win_path);
	return watcher;
}

void FileSystemWatcher::destroy(FileSystemWatcher* watcher)
{
	delete watcher;
}
		
