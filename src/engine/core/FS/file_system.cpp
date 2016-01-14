#include "core/fs/file_system.h"

#include "core/array.h"
#include "core/base_proxy_allocator.h"
#include "core/fs/disk_file_device.h"
#include "core/fs/ifile.h"
#include "core/mt/lock_free_fixed_queue.h"
#include "core/mt/task.h"
#include "core/mt/transaction.h"
#include "core/path.h"
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
	E_CLOSE = 0,
	E_SUCCESS = 0x1,
	E_IS_OPEN = E_SUCCESS << 1,
	E_FAIL = E_IS_OPEN << 1
};

struct AsyncItem
{
	AsyncItem() {}

	IFile* m_file;
	ReadCallback m_cb;
	Mode m_mode;
	char m_path[MAX_PATH_LENGTH];
	uint8 m_flags;
};

static const int32 C_MAX_TRANS = 16;

typedef MT::Transaction<AsyncItem> AsynTrans;
typedef MT::LockFreeFixedQueue<AsynTrans, C_MAX_TRANS> TransQueue;
typedef Queue<AsynTrans*, C_MAX_TRANS> InProgressQueue;
typedef Array<AsyncItem> ItemsTable;
typedef Array<IFileDevice*> DevicesTable;


class FSTask : public MT::Task
{
public:
	FSTask(TransQueue* queue, IAllocator& allocator)
		: MT::Task(allocator)
		, m_trans_queue(queue)
	{
	}


	~FSTask() {}


	int task()
	{
		while (!m_trans_queue->isAborted())
		{
			PROFILE_BLOCK("transaction");
			AsynTrans* tr = m_trans_queue->pop(true);
			if (!tr) break;

			if ((tr->data.m_flags & E_IS_OPEN) == E_IS_OPEN)
			{
				tr->data.m_flags |=
					tr->data.m_file->open(Path(tr->data.m_path), tr->data.m_mode) ? E_SUCCESS : E_FAIL;
			}
			else if ((tr->data.m_flags & E_CLOSE) == E_CLOSE)
			{
				tr->data.m_file->close();
				tr->data.m_file->release();
				tr->data.m_file = nullptr;
			}
			tr->setCompleted();
		}
		return 0;
	}

	void stop() { m_trans_queue->abort(); }

private:
	TransQueue* m_trans_queue;
};

class FileSystemImpl : public FileSystem
{
public:
	FileSystemImpl(IAllocator& allocator)
		: m_allocator(allocator)
		, m_in_progress(m_allocator)
		, m_pending(m_allocator)
		, m_devices(m_allocator)
	{
		m_task = LUMIX_NEW(m_allocator, FSTask)(&m_transaction_queue, m_allocator);
		m_task->create("FSTask");
		m_task->run();
	}

	~FileSystemImpl()
	{
		m_task->stop();
		m_task->destroy();
		while (!m_in_progress.empty())
		{
			auto* trans = m_in_progress.front();
			m_in_progress.pop();
			if (trans->data.m_file) close(*trans->data.m_file);
		}
		for (auto& i : m_pending)
		{
			close(*i.m_file);
		}
		LUMIX_DELETE(m_allocator, m_task);
	}

	BaseProxyAllocator& getAllocator() { return m_allocator; }


	bool hasWork() const override { return !m_in_progress.empty() || !m_pending.empty(); }


	bool mount(IFileDevice* device) override
	{
		for (int i = 0; i < m_devices.size(); i++)
		{
			if (m_devices[i] == device)
			{
				return false;
			}
		}

		if (compareString(device->name(), "memory") == 0)
		{
			m_memory_device.m_devices[0] = device;
			m_memory_device.m_devices[1] = nullptr;
		}
		else if (compareString(device->name(), "disk") == 0)
		{
			m_disk_device.m_devices[0] = device;
			m_disk_device.m_devices[1] = nullptr;
		}

		m_devices.push(device);
		return true;
	}

	bool unMount(IFileDevice* device) override
	{
		for (int i = 0; i < m_devices.size(); i++)
		{
			if (m_devices[i] == device)
			{
				m_devices.eraseFast(i);
				return true;
			}
		}

		return false;
	}


	IFile* createFile(const DeviceList& device_list)
	{
		IFile* prev = nullptr;
		for (int i = 0; i < lengthOf(device_list.m_devices); ++i)
		{
			if (!device_list.m_devices[i])
			{
				break;
			}
			prev = device_list.m_devices[i]->createFile(prev);
		}
		return prev;
	}


	IFile* open(const DeviceList& device_list, const Path& file, Mode mode) override
	{
		IFile* prev = createFile(device_list);

		if (prev)
		{
			if (prev->open(file, mode))
			{
				return prev;
			}
			else
			{
				prev->release();
				return nullptr;
			}
		}
		return nullptr;
	}


	bool openAsync(const DeviceList& device_list,
		const Path& file,
		int mode,
		const ReadCallback& call_back) override
	{
		IFile* prev = createFile(device_list);

		if (prev)
		{
			AsyncItem& item = m_pending.emplace();

			item.m_file = prev;
			item.m_cb = call_back;
			item.m_mode = mode;
			copyString(item.m_path, file.c_str());
			item.m_flags = E_IS_OPEN;
		}

		return nullptr != prev;
	}


	void setDefaultDevice(const char* dev) override { fillDeviceList(dev, m_default_device); }


	void fillDeviceList(const char* dev, DeviceList& device_list) override
	{
		const char* token = nullptr;

		int device_index = 0;
		const char* end = dev + stringLength(dev);

		while (end > dev)
		{
			token = reverseFind(dev, token, ':');
			char device[32];
			if (token)
			{
				copyNString(device, (int)sizeof(device), token + 1, int(end - token - 1));
			}
			else
			{
				copyNString(device, (int)sizeof(device), dev, int(end - dev));
			}
			end = token;
			device_list.m_devices[device_index] = getDevice(device);
			ASSERT(device_list.m_devices[device_index]);
			++device_index;
		}
		device_list.m_devices[device_index] = nullptr;
	}


	const DeviceList& getMemoryDevice() const override { return m_memory_device; }


	const DeviceList& getDiskDevice() const override { return m_disk_device; }


	void setSaveGameDevice(const char* dev) override { fillDeviceList(dev, m_save_game_device); }


	void close(IFile& file) override
	{
		file.close();
		file.release();
	}


	void closeAsync(IFile& file) override
	{
		AsyncItem& item = m_pending.emplace();

		item.m_file = &file;
		item.m_cb.bind<closeAsync>();
		item.m_mode = 0;
		item.m_flags = E_CLOSE;
	}


	void updateAsyncTransactions() override
	{
		PROFILE_FUNCTION();
		while (!m_in_progress.empty())
		{
			AsynTrans* tr = m_in_progress.front();
			if (!tr->isCompleted()) break;

			PROFILE_BLOCK("processAsyncTransaction");
			m_in_progress.pop();

			tr->data.m_cb.invoke(*tr->data.m_file, !!(tr->data.m_flags & E_SUCCESS));
			if ((tr->data.m_flags & (E_SUCCESS | E_FAIL)) != 0)
			{
				closeAsync(*tr->data.m_file);
			}
			m_transaction_queue.dealoc(tr);
		}

		int32 can_add = C_MAX_TRANS - m_in_progress.size();
		while (can_add && !m_pending.empty())
		{
			AsynTrans* tr = m_transaction_queue.alloc(false);
			if (tr)
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

	const DeviceList& getDefaultDevice() const override { return m_default_device; }

	const DeviceList& getSaveGameDevice() const override { return m_save_game_device; }

	IFileDevice* getDevice(const char* device)
	{
		for (int i = 0; i < m_devices.size(); ++i)
		{
			if (compareString(m_devices[i]->name(), device) == 0) return m_devices[i];
		}

		return nullptr;
	}

	static void closeAsync(IFile&, bool) {}

	void destroy()
	{
		m_transaction_queue.abort();
		m_task->destroy();
	}

private:
	BaseProxyAllocator m_allocator;
	FSTask* m_task;
	DevicesTable m_devices;

	ItemsTable m_pending;
	TransQueue m_transaction_queue;
	InProgressQueue m_in_progress;

	DeviceList m_disk_device;
	DeviceList m_memory_device;
	DeviceList m_default_device;
	DeviceList m_save_game_device;
};

FileSystem* FileSystem::create(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, FileSystemImpl)(allocator);
}

void FileSystem::destroy(FileSystem* fs)
{
	LUMIX_DELETE(static_cast<FileSystemImpl*>(fs)->getAllocator().getSourceAllocator(), fs);
}


} // namespace FS
} // namespace Lumix
