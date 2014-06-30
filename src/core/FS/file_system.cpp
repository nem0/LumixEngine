#include "core/fs/file_system.h"

#include "core/array.h"
#include "core/fs/disk_file_device.h"
#include "core/fs/ifile.h"
#include "core/MT/lock_free_fixed_queue.h"
#include "core/MT/task.h"
#include "core/MT/transaction.h"
#include "core/profiler.h"
#include "core/queue.h"
#include "core/stack_allocator.h"
#include "core/string.h"

namespace Lumix
{
	namespace FS
	{
		enum TransFlags
		{
			E_NONE = 0,
			E_SUCCESS = 0x1,
			E_IS_OPEN = E_SUCCESS << 1,
		};

		struct AsyncItem
		{
			AsyncItem() {}

			IFile* m_file;
			ReadCallback m_cb;
			Mode m_mode;
			char m_path[LUMIX_MAX_PATH];
			uint8_t m_flags;

		};

		static const int32_t C_MAX_TRANS = 16;

		typedef MT::Transaction<AsyncItem> AsynTrans;
		typedef MT::LockFreeFixedQueue<AsynTrans, C_MAX_TRANS> TransQueue;
		typedef Queue<AsynTrans*, C_MAX_TRANS> InProgressQueue;
		typedef Array<AsyncItem> ItemsTable;
		typedef Array<IFileDevice*> DevicesTable;

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

					if((tr->data.m_flags & E_IS_OPEN) == E_IS_OPEN)
					{
						tr->data.m_flags |= tr->data.m_file->open(tr->data.m_path, tr->data.m_mode) ? E_SUCCESS : E_NONE;
					}
					else
					{
						tr->data.m_file->close();
					}
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
				m_task = LUMIX_NEW(FSTask)(&m_transaction_queue);
				m_task->create("FSTask");
				m_task->run();
			}

			~FileSystemImpl()
			{
				m_task->stop();
				m_task->destroy();
				LUMIX_DELETE(m_task);
			}

			bool mount(IFileDevice* device) override
			{
				for(int i = 0; i < m_devices.size(); i++)
				{
					if(m_devices[i] == device)
					{
						return false;
					}
				}

				m_devices.push(device);
				return true;
			}

			bool unMount(IFileDevice* device) override
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

			IFile* open(const char* device_list, const char* file, Mode mode) override
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
						LUMIX_DELETE(prev);
						return NULL;
					}
				}
				return NULL;
			}

			bool openAsync(const char* device_list, const char* file, int mode, const ReadCallback& call_back) override
			{
				IFile* prev = parseDeviceList(device_list);

				if(prev)
				{
					AsyncItem& item = m_pending.pushEmpty();

					item.m_file = prev;
					item.m_cb = call_back;
					item.m_mode = mode;
					copyString(item.m_path, sizeof(item.m_path), file);
					item.m_flags = E_IS_OPEN;
				}

				return NULL != prev;
			}

			void close(IFile* file) override
			{
				file->close();
				LUMIX_DELETE(file);
			}

			void closeAsync(IFile* file) override
			{
				AsyncItem& item = m_pending.pushEmpty();

				item.m_file = file;
				item.m_cb.bind<closeAsync>();
				item.m_mode = 0;
				item.m_flags = E_NONE;
			}

			void updateAsyncTransactions() override
			{
				PROFILE_FUNCTION();
				while(!m_in_progress.empty())
				{
					AsynTrans* tr = m_in_progress.front();
					if(tr->isCompleted())
					{
						PROFILE_BLOCK("processAsyncTransaction");
						m_in_progress.pop();

						tr->data.m_cb.invoke(tr->data.m_file, !!(tr->data.m_flags & E_SUCCESS), *this);
						m_transaction_queue.dealoc(tr, true);
					}
					else
					{
						break;
					}
				}

				int32_t can_add = C_MAX_TRANS - m_in_progress.size();
				while(can_add && !m_pending.empty())
				{
					AsynTrans* tr = m_transaction_queue.alloc(false);
					if(tr)
					{
						AsyncItem& item = m_pending[0];
						tr->data.m_file = item.m_file;
						tr->data.m_cb = item.m_cb;
						tr->data.m_mode = item.m_mode;
						copyString(tr->data.m_path, sizeof(tr->data.m_path), item.m_path);
						tr->data.m_flags = item.m_flags;
						tr->reset();

						m_transaction_queue.push(tr, true);
						m_in_progress.push(tr);
						m_pending.erase(0);
					}
					can_add--;
				}
			}

			const char* getDefaultDevice() const override { return m_default_device.c_str(); }
			const char* getSaveGameDevice() const override { return m_save_game_device.c_str(); }

			void setDefaultDevice(const char* dev) override { m_default_device = dev; }
			void setSaveGameDevice(const char* dev) override { m_save_game_device = dev; }

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
				base_string<char, StackAllocator<128>> token, dev_list(device_list);
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

			static void closeAsync(IFile* file, bool, FileSystem&)
			{
				LUMIX_DELETE(file);
			}

			void destroy()
			{
				m_transaction_queue.abort();
				m_task->destroy();
			}

		private:
			FSTask* m_task;
			DevicesTable m_devices;

			ItemsTable		m_pending;
			TransQueue		m_transaction_queue;
			InProgressQueue m_in_progress;

			string m_default_device;
			string m_save_game_device;
		};

		FileSystem* FileSystem::create()
		{
			return LUMIX_NEW(FileSystemImpl)();
		}

		void FileSystem::destroy(FileSystem* fs)
		{
			LUMIX_DELETE(fs);
		}
	} // ~namespace FS
} // ~namespace Lumix
