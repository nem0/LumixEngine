#include "tcp_filesystem.h"
#include "task.h"
#include "core/vector.h"
#include "core/string.h"
#include "core/mutex.h"
#include <cstdio>
#include "socket.h"

namespace Lux
{
	struct TCPFileSystemTask : public Task
	{
		virtual int task() LUX_OVERRIDE;
		void stop();
		void handleMessage(char* buffer, int size);

		bool m_finished;
		Mutex m_mutex;
		struct TCPFileSystemImpl* m_fs;
		Socket m_socket;
		Socket* m_work_socket;
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
			string m_path;
			IFileSystem::ReadCallback m_callback;
			void* m_user_data;
			char* m_file_data;
			int m_file_length;
			int m_uid;
			Status m_status;
		};

		TCPFileSystemTask* m_task;
		vector<FileItem*> m_queue;
		vector<FileItem*> m_loaded;
		vector<FileItem*> m_in_progress;
		int m_last_uid;
	};


	int TCPFileSystemTask::task()
	{
		char* buffer = new char[256];
		m_socket.create(10001);
		m_work_socket = m_socket.accept();

		m_finished = false;

		while(!m_finished)
		{
			m_mutex.lock();
			if(!m_fs->m_queue.empty())
			{
				TCPFileSystemImpl::FileItem* item = m_fs->m_queue.back();
				m_fs->m_queue.pop_back();
				m_mutex.unlock();
				
				int len = item->m_path.length() + 5;
				memcpy(buffer, &len, sizeof(len));				
				buffer[4] = 0;
				memcpy(buffer+5, &item->m_uid, sizeof(item->m_uid));
				
				m_work_socket->send(buffer, 9);
				m_work_socket->send(item->m_path.c_str(), item->m_path.length());

				m_fs->m_in_progress.push_back(item);
			}
			else
			{
				m_mutex.unlock();
			}

			char buffer2[256];
			if(m_work_socket->canReceive())
			{
				int received = m_work_socket->receive(buffer2, 5);
				handleMessage(buffer2, received);
			}
		}
		delete[] buffer;
		return 0;
	}

	void TCPFileSystemTask::handleMessage(char* buffer, int size)
	{
		if(size > 0)
		{
			if(size < 9)
			{
				if(!m_work_socket->receiveAllBytes(buffer + size, 9 - size))
					assert(false);
				size = 9;
			}
			
			int len = *(int*)buffer;
			if(buffer[4] == 0)
			{
				int uid = *(int*)(buffer + 5);
				bool found = false;
				for(int i = 0; i < m_fs->m_in_progress.size(); ++i)
				{
					if(m_fs->m_in_progress[i]->m_uid == uid)
					{
						if(len < 0)
						{
							m_fs->m_in_progress[i]->m_file_length = -1;
							m_fs->m_in_progress[i]->m_file_data = 0;
							m_fs->m_in_progress[i]->m_status = TCPFileSystemImpl::FileItem::FAIL;
						}
						else
						{
							m_fs->m_in_progress[i]->m_file_length = len - 5;
							m_fs->m_in_progress[i]->m_file_data = new char[len - 5];
							if(size > 9)
								memcpy(m_fs->m_in_progress[i]->m_file_data, buffer + 9, size - 9);
							m_work_socket->receiveAllBytes(m_fs->m_in_progress[i]->m_file_data + size - 9, len - size + 4); 					
							m_fs->m_in_progress[i]->m_status = TCPFileSystemImpl::FileItem::SUCCESS;
						}
						m_mutex.lock();
						m_fs->m_loaded.push_back(m_fs->m_in_progress[i]);
						m_fs->m_in_progress.eraseFast(i);
						m_mutex.unlock();
						found = true;
						break;
					}
					int last_err = WSAGetLastError();
					assert(found);
				}
			}
			else
			{
				assert(false);
			}
		}
	}


	void TCPFileSystem::processLoaded()
	{
		m_impl->m_task->m_mutex.lock();
		if(!m_impl->m_loaded.empty())
		{
			TCPFileSystemImpl::FileItem* item = m_impl->m_loaded.back();
			m_impl->m_loaded.pop_back();
			m_impl->m_task->m_mutex.lock();
			item->m_callback(item->m_user_data, item->m_file_data, item->m_file_length, item->m_status == TCPFileSystemImpl::FileItem::SUCCESS);
			delete item;
		}
		else
		{
			m_impl->m_task->m_mutex.unlock();
		}
	}


	void TCPFileSystemTask::stop()
	{
		m_finished = true;
	}


	bool TCPFileSystem::create()
	{
		Socket::init();
		m_impl = new TCPFileSystemImpl();
		m_impl->m_last_uid = 0;
		m_impl->m_task = new TCPFileSystemTask();
		m_impl->m_task->m_fs = m_impl;
		if(!m_impl->m_task->create())
		{
			m_impl->m_task->m_mutex.create();
			delete m_impl;
			m_impl = 0;
			return false;
		}
		if(!m_impl->m_task->run())
		{
			m_impl->m_task->destroy();
			delete m_impl;
			m_impl = 0;
			return false;
		}
		return true;
	}


	void TCPFileSystem::destroy()
	{
		if(m_impl)
		{
			m_impl->m_task->stop();
			m_impl->m_task->m_mutex.destroy();			
			m_impl->m_task->destroy();
			delete m_impl;
			m_impl = 0;
		}
	}


	IFileSystem::Handle TCPFileSystem::openFile(const char* path, ReadCallback callback, void* user_data)
	{
		TCPFileSystemImpl::FileItem* item = new TCPFileSystemImpl::FileItem();
		item->m_callback = callback;
		item->m_path = path;
		item->m_user_data = user_data;
		item->m_uid = ++m_impl->m_last_uid;
		m_impl->m_task->m_mutex.lock();
		m_impl->m_queue.push_back(item);
		m_impl->m_task->m_mutex.unlock();
		return item->m_uid;
	}


} // ~namespace Lux