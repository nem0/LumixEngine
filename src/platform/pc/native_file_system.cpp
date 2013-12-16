#include "platform/native_file_system.h"
#include <cstdio>
#include "core/vector.h"
#include "core/string.h"
#include "platform/event.h"
#include "platform/mutex.h"
#include "platform/task.h"


namespace Lux
{


	class FileSystemTask : public MT::Task
	{
		public:
			virtual int task() LUX_OVERRIDE;

			NativeFileSystemImpl* fs;
	};


	struct NativeFileSystemImpl
	{
		struct Info
		{
			enum Status
			{
				SUCCESS,
				FAIL
			};

			IFileSystem::ReadCallback m_callback;
			void* m_user_data;
			char* m_file_data;
			int m_file_size;
			string m_path;
			Status m_status;
		};

		vector<Info> m_infos;
		vector<Info> m_processed;
		FileSystemTask m_task;
		MT::Mutex* m_mutex;
		MT::Event* m_event;
	};


	int FileSystemTask::task()
	{
		bool finished = false;
		while(!finished)
		{
			if(!fs->m_infos.empty())
			{
				fs->m_mutex->lock();
				NativeFileSystemImpl::Info& h = fs->m_infos.back();
				fs->m_mutex->unlock();

				FILE* fp;
				fopen_s(&fp, h.m_path.c_str(), "rb");
				if(fp)
				{
					fseek(fp, 0, SEEK_END);
					h.m_file_size = ftell(fp);
					fseek(fp, 0, SEEK_SET);
					h.m_file_data = new char[h.m_file_size];
					fread(h.m_file_data, 1, h.m_file_size, fp);
					h.m_status = NativeFileSystemImpl::Info::SUCCESS;
					fs->m_mutex->lock();
					fs->m_processed.push_back(h);
					fs->m_mutex->unlock();
				}
				else
				{
					h.m_status = NativeFileSystemImpl::Info::FAIL;
					fs->m_processed.push_back(h);
				}
				fs->m_infos.pop_back();
			}
		}
		return 0;
	}


	bool NativeFileSystem::create()
	{
		m_impl = new NativeFileSystemImpl();
		m_impl->m_mutex = MT::Mutex::create("native fs");
		m_impl->m_event = MT::Event::create("native fs", MT::EventFlags::MANUAL_RESET);
		bool success =  m_impl->m_task.create("NativeFileSystemTask");
		m_impl->m_task.fs = m_impl;
		success = success && m_impl->m_task.run();
		return success;
	}


	void NativeFileSystem::processLoaded()
	{
		m_impl->m_mutex->lock();
		if(!m_impl->m_processed.empty())
		{
			NativeFileSystemImpl::Info item = m_impl->m_processed.back();
			m_impl->m_processed.pop_back();
			m_impl->m_mutex->unlock();
			item.m_callback(item.m_user_data, item.m_file_data, item.m_file_size, item.m_status == NativeFileSystemImpl::Info::SUCCESS);
		}
		else
		{
			m_impl->m_mutex->unlock();
		}
	}


	void NativeFileSystem::destroy()
	{
		m_impl->m_task.destroy();
		delete m_impl;
	}


	IFileSystem::Handle NativeFileSystem::openFile(const char* path, ReadCallback callback, void* user_data)
	{
		MT::Lock lock(*m_impl->m_mutex);
		NativeFileSystemImpl::Info& info = m_impl->m_infos.push_back_empty();
		info.m_callback = callback;
		info.m_user_data = user_data;
		info.m_path = path;
		return 0;
	}




} // ~namespace Lux