#include "native_file_system.h"
#include "task.h"
#include "core/vector.h"
#include <cstdio>
#include "core/string.h"


namespace Lux
{


	class FileSystemTask : public Task
	{
		public:
			virtual int task() LUX_OVERRIDE;

			NativeFileSystemImpl* fs;
	};


	struct NativeFileSystemImpl
	{
		struct Info
		{
			FileSystem::OnFinished on_finished;
			void* data;
			string path;
		};

		vector<Info> infos;
		FileSystemTask task;
	};


	int FileSystemTask::task()
	{
		bool finished = false;
		while(!finished)
		{
			if(!fs->infos.empty())
			{
				NativeFileSystemImpl::Info& h = fs->infos.back();

				FILE* fp;
				fopen_s(&fp, h.path.c_str(), "r");
				if(fp)
				{
					fseek(fp, 0, SEEK_END);
					int size = ftell(fp);
					fseek(fp, 0, SEEK_SET);
					void* data = new char[size];
					fread(h.data, 1, size, fp);
					h.on_finished(h.path.c_str(), h.data, data, size, FileSystem::SUCCESS);
					delete[] data;
				}
				else
				{
					h.on_finished(h.path.c_str(), h.data, 0, 0, FileSystem::FAIL);
				}

				fs->infos.pop_back();
			}
		}
		return 0;
	}


	bool NativeFileSystem::create()
	{
		m_impl = new NativeFileSystemImpl();
		bool success =  m_impl->task.create();
		m_impl->task.fs = m_impl;
		success = success && m_impl->task.run();
		return success;
	}


	void NativeFileSystem::destroy()
	{
		m_impl->task.destroy();
		delete m_impl;
	}


	void NativeFileSystem::open(const char* filename, OnFinished callback, void* data)
	{
		NativeFileSystemImpl::Info& info = m_impl->infos.push_back_empty();
		info.on_finished = callback;
		info.data = data;
		info.path = filename;
	}




} // ~namespace Lux