#include "tcp_filesystem.h"
#include "task.h"
#include "core/vector.h"
#include "core/string.h"
#include "platform/mutex.h"
#include <cstdio>
#include "socket.h"

namespace Lux
{
	struct TCPFileSystemTask : public Task
	{
		virtual int task() LUX_OVERRIDE;
		void stop();
		void handleMessage(char* buffer, int size);

		bool finished;
		Mutex mutex;
		struct TCPFileSystemImpl* fs;
		Socket socket;
		Socket* work_socket;
	};


	struct TCPFileSystemImpl
	{
		struct FileItem
		{
			enum Status
			{
				LOADING,
				SUCCESS,
				FAIL
			};
			string path;
			IFileSystem::ReadCallback callback;
			void* user_data;
			char* file_data;
			int file_length;
			int uid;
			Status status;
		};

		TCPFileSystemTask* task;
		vector<FileItem*> queue;
		vector<FileItem*> loaded;
		vector<FileItem*> in_progress;
		int last_uid;
	};


	int TCPFileSystemTask::task()
	{
		return 0;
	}

	void TCPFileSystemTask::handleMessage(char* buffer, int size)
	{

	}


	void TCPFileSystem::processLoaded()
	{

	}


	void TCPFileSystemTask::stop()
	{
		finished = true;
	}


	bool TCPFileSystem::create()
	{
		return true;
	}


	void TCPFileSystem::destroy()
	{

	}


	IFileSystem::Handle TCPFileSystem::openFile(const char* path, ReadCallback callback, void* user_data)
	{
		return LUX_NULL;
	}


} // ~namespace Lux