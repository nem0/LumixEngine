#include "core/file_system.h"
#include "core/disk_file_device.h"
#include "core/ifile.h"
#include "core/string.h"
#include "core/transaction_queue.h"
#include "core/vector.h"

#include "core/task.h"

namespace Lux
{
	namespace FS
	{
		struct AsyncItem
		{
			AsyncItem() {}

			IFile* m_file;
			void* m_user_data;
			ReadCallback m_cb;
			Mode m_mode;
			char m_path[_MAX_PATH];
			bool m_result;

		};

		typedef MT::Transaction<AsyncItem> AsynTrans;
		typedef MT::TransactionQueue<AsynTrans, 16> TransQueue;
		typedef vector<AsynTrans*> TransTable;
		typedef vector<IFileDevice*> DevicesTable;

		class FSTask : public MT::Task
		{
		public:
			FSTask(TransQueue* queue) : m_trans_queue(queue) {}
			~FSTask() {}

			int task()
			{
				while(!m_trans_queue->isAborted())
				{
					AsynTrans* tr = m_trans_queue->pop(true);
					if(NULL == tr)
						break;

					tr->data.m_result = tr->data.m_user_data != NULL 
						? tr->data.m_file->open(tr->data.m_path, tr->data.m_mode)
						: (tr->data.m_file->close(), true);
					tr->setCompleted();
				}
				return 0;
			}

			void stop()
			{
				m_trans_queue->abort();
			}

		private:
			TransQueue* m_trans_queue;
		};

		class FileSystemImpl : public FileSystem
		{
		public:
			FileSystemImpl()
			{
				m_task = new FSTask(&m_transaction_queue);
				m_task->create("FSTask");
				m_task->run();
			}

			~FileSystemImpl()
			{
				m_task->stop();
				m_task->destroy();
				delete m_task;
			}

			bool mount(IFileDevice* device) LUX_OVERRIDE
			{
				for(int i = 0; i < m_devices.size(); i++)
				{
					if(m_devices[i] == device)
					{
						return false;
					}
				}

				m_devices.push_back(device);
				return true;
			}

			bool unMount(IFileDevice* device) LUX_OVERRIDE
			{
				for(int i = 0; i < m_devices.size(); i++)
				{
					if(m_devices[i] == device)
					{
						m_devices.eraseFast(i);
						return true;
					}
				}

				return false;
			}

			IFile* open(const char* device_list, const char* file, Mode mode) LUX_OVERRIDE
			{
				IFile* prev = parseDeviceList(device_list);

				if(prev)
				{
					if(prev->open(file, mode))
					{
						return prev;
					}
					else
					{
						close(prev);
						return NULL;
					}
				}
				return NULL;
			}

			IFile* openAsync(const char* device_list, const char* file, int mode, ReadCallback call_back, void* user_data) LUX_OVERRIDE
			{
				IFile* prev = parseDeviceList(device_list);

				if(prev)
				{
					AsynTrans* tr = m_transaction_queue.alloc(true);
					if(tr)
					{
						tr->data.m_file = prev;
						tr->data.m_user_data = user_data;
						tr->data.m_cb = call_back;
						tr->data.m_mode = mode;
						strcpy(tr->data.m_path, file);
						tr->data.m_result = false;
						tr->reset();

						m_transaction_queue.push(tr, true);
						m_in_progress.push_back(tr);
					}
				}
				return NULL;
			}

			void close(IFile* file) LUX_OVERRIDE
			{
				file->close();
				delete file;
			}

			void closeAsync(IFile* file) LUX_OVERRIDE
			{
				AsynTrans* tr = m_transaction_queue.alloc(true);
				if(tr)
				{
					tr->data.m_file = file;
					tr->data.m_user_data = NULL;
					tr->data.m_cb = closeAsync;
					tr->data.m_mode = 0;
					tr->data.m_result = false;
					tr->reset();

					m_transaction_queue.push(tr, true);
					m_in_progress.push_back(tr);
				}
			}

			void updateAsyncTransactions() LUX_OVERRIDE
			{
				for(int32_t i = 0; i < m_in_progress.size(); i++)
				{
					AsynTrans* tr = m_in_progress[i];
					if(tr->isCompleted())
					{
						m_in_progress.erase(i);

						tr->data.m_cb(tr->data.m_file, tr->data.m_result, tr->data.m_user_data);
						m_transaction_queue.dealoc(tr);

						break;
					}
				}
			}

			const char* getDefaultDevice() const LUX_OVERRIDE { return "memory:disk"; }
			const char* getSaveGameDevice() const LUX_OVERRIDE { return "memory:disk"; }

			IFileDevice* getDevice(const char* device)
			{
				for(int i = 0; i < m_devices.size(); ++i)
				{
					if(strcmp(m_devices[i]->name(), device) == 0)
						return m_devices[i];
				}

				return NULL;
			}

			IFile* parseDeviceList(const char* device_list)
			{
				IFile* prev = NULL;
				string token, dev_list = device_list;
				while(dev_list.length() > 0)
				{
					int pos = dev_list.rfind(':');

					if(string::npos != pos)
					{
						token = dev_list.substr(pos + 1, dev_list.length() - pos);
						dev_list = dev_list.substr(0, pos);
					}
					else
					{
						token = dev_list;
						dev_list = "";
					}

					IFileDevice* dev = getDevice(token.c_str());
					if(NULL != dev)
					{
						prev = dev->createFile(prev);
					}
				}

				return prev;
			}

			static void closeAsync(IFile* file, bool success, void* user_data)
			{
				delete file;
			}

			void destroy()
			{
				m_transaction_queue.abort();
				m_task->destroy();
			}

		private:
			FSTask* m_task;
			DevicesTable m_devices;

			TransQueue m_transaction_queue;
			TransTable m_in_progress;
		};

		FileSystem* FileSystem::create()
		{
			return new FileSystemImpl();
		}

		void FileSystem::destroy(FileSystem* fs)
		{
			delete fs;
		}
	} // ~namespace FS
} // ~namespace Lux