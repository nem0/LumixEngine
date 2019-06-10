#include "engine/fs/file_system.h"

#include "engine/array.h"
#include "engine/base_proxy_allocator.h"
#include "engine/delegate_list.h"
#include "engine/fs/os_file.h"
#include "engine/mt/lock_free_fixed_queue.h"
#include "engine/mt/task.h"
#include "engine/mt/transaction.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/queue.h"
#include "engine/stream.h"
#include "engine/string.h"


namespace Lumix
{
namespace FS
{


enum TransFlags
{
	E_CLOSE = 0,
	E_SUCCESS = 0x1,
	E_IS_OPEN = E_SUCCESS << 1,
	E_FAIL = E_IS_OPEN << 1,
	E_CANCELED = E_FAIL << 1
};

struct AsyncItem
{
	OSInputFile m_file;
	ReadCallback m_cb;
	u32 m_id;
	char m_path[MAX_PATH_LENGTH];
	u8 m_flags;
};

static const i32 C_MAX_TRANS = 16;

typedef MT::Transaction<AsyncItem> AsynTrans;
typedef MT::LockFreeFixedQueue<AsynTrans, C_MAX_TRANS> TransQueue;
typedef Queue<AsynTrans*, C_MAX_TRANS> InProgressQueue;
typedef Array<AsyncItem> ItemsTable;


class FSTask final : public MT::Task
{
public:
	FSTask(TransQueue* queue, IAllocator& allocator)
		: MT::Task(allocator)
		, m_trans_queue(queue)
	{
	}


	~FSTask() = default;


	int task() override
	{
		while (!m_trans_queue->isAborted())
		{
			PROFILE_BLOCK("transaction");
			AsynTrans* tr = m_trans_queue->pop(true);
			if (!tr) break;

			if ((tr->data.m_flags & E_IS_OPEN) == E_IS_OPEN)
			{
				PROFILE_BLOCK("open");
				Profiler::blockColor(0, 0xff, 0);
				Profiler::recordString(tr->data.m_path);
				const bool opened = tr->data.m_file.open(tr->data.m_path);
				if (!opened) Profiler::blockColor(0xff, 0, 0);
				tr->data.m_flags |= opened ? E_SUCCESS : E_FAIL;
			}
			else if ((tr->data.m_flags & E_CLOSE) == E_CLOSE)
			{
				PROFILE_BLOCK("close");
				Profiler::blockColor(0, 0, 0xff);
				tr->data.m_file.close();
			}
			tr->setCompleted();
		}
		return 0;
	}

	void stop() { m_trans_queue->abort(); }

private:
	TransQueue* m_trans_queue;
};


class FileSystemImpl final : public FileSystem
{
public:
	explicit FileSystemImpl(const char* base_path, IAllocator& allocator)
		: m_allocator(allocator)
		, m_pending(m_allocator)
		, m_in_progress(m_allocator)
		, m_last_id(0)
	{
		m_task = LUMIX_NEW(m_allocator, FSTask)(&m_transaction_queue, m_allocator);
		m_task->create("Filesystem", true);
	}

	~FileSystemImpl()
	{
		m_task->stop();
		m_task->destroy();
		LUMIX_DELETE(m_allocator, m_task);
		while (!m_in_progress.empty())
		{
			auto* trans = m_in_progress.front();
			m_in_progress.pop();
			trans->data.m_file.close();
		}
		for (auto& i : m_pending)
		{
			i.m_file.close();
		}
	}

	BaseProxyAllocator& getAllocator() { return m_allocator; }


	bool hasWork() const override { return !m_in_progress.empty() || !m_pending.empty(); }


	const char* getBasePath() const override { return m_base_path; }


	void read(AsyncHandle handle, u64 offset, u64 size, const ReadCallback& callback) override
	{
		ASSERT(false);
		// TODO
	}


	AsyncHandle getContent(const Path& file, const ContentCallback& callback) override
	{
		ASSERT(false);
		// TODO
		return AsyncHandle::invalid();
	}


	AsyncHandle open(const Path& file, const OpenCallback& callback) override
	{
/*		IFile* prev = m_disk_device->createFile();

		if (prev)
		{
			AsyncItem& item = m_pending.emplace();

			item.m_file = prev;
			item.m_cb = call_back;
			item.m_mode = mode;
			copyString(item.m_path, file.c_str());
			item.m_flags = E_IS_OPEN;
			item.m_id = m_last_id;
			++m_last_id;
			if (m_last_id == INVALID_ASYNC) m_last_id = 0;
			return item.m_id;
		}
		*/
		ASSERT(false);
		// TODO
		return AsyncHandle::invalid();
	}


	void cancel(AsyncHandle async) override
	{
		ASSERT(false);
		// TODO
/*		if (id == INVALID_ASYNC) return;

		for (int i = 0, c = m_pending.size(); i < c; ++i)
		{
			if (m_pending[i].m_id == id)
			{
				m_pending[i].m_flags |= E_CANCELED;
				return;
			}
		}

		for (auto iter = m_in_progress.begin(), end = m_in_progress.end(); iter != end; ++iter)
		{
			if (iter.value()->data.m_id == id)
			{
				iter.value()->data.m_flags |= E_CANCELED;
				return;
			}
		}*/

	}


	void close(AsyncHandle handle) override
	{
		/*
		AsyncItem& item = m_pending.emplace();

		item.m_file = &file;
		item.m_cb.bind<closeAsync>();
		item.m_mode = 0;
		item.m_flags = E_CLOSE;*/
		// TODO
		ASSERT(false);
	}


	void updateAsyncTransactions() override
	{
		/*PROFILE_FUNCTION();
		while (!m_in_progress.empty())
		{
			AsynTrans* tr = m_in_progress.front();
			if (!tr->isCompleted()) break;

			PROFILE_BLOCK("processAsyncTransaction");
			m_in_progress.pop();

			if ((tr->data.m_flags & E_CANCELED) == 0)
			{
				tr->data.m_cb.invoke(tr->data.m_file, !!(tr->data.m_flags & E_SUCCESS));
			}
			if ((tr->data.m_flags & (E_SUCCESS | E_FAIL)) != 0)
			{
				closeAsync(*tr->data.m_file);
			}
			m_transaction_queue.dealoc(tr);
		}

		i32 can_add = C_MAX_TRANS - m_in_progress.size();
		while (can_add && !m_pending.empty())
		{
			AsynTrans* tr = m_transaction_queue.alloc(false);
			if (tr)
			{
				AsyncItem& item = m_pending[0];
				tr->data.m_file = item.m_file;
				tr->data.m_cb = item.m_cb;
				tr->data.m_id = item.m_id;
				tr->data.m_mode = item.m_mode;
				copyString(tr->data.m_path, sizeof(tr->data.m_path), item.m_path);
				tr->data.m_flags = item.m_flags;
				tr->reset();

				m_transaction_queue.push(tr, true);
				m_in_progress.push(tr);
				m_pending.erase(0);
			}
			can_add--;
		}*/
		// TODO
		ASSERT(false);
	}

private:
	BaseProxyAllocator m_allocator;
	FSTask* m_task;
	ItemsTable m_pending;
	TransQueue m_transaction_queue;
	InProgressQueue m_in_progress;
	char m_base_path[MAX_PATH_LENGTH];

	u32 m_last_id;
};

FileSystem* FileSystem::create(const char* base_path, IAllocator& allocator)
{
	return LUMIX_NEW(allocator, FileSystemImpl)(base_path, allocator);
}

void FileSystem::destroy(FileSystem* fs)
{
	LUMIX_DELETE(static_cast<FileSystemImpl*>(fs)->getAllocator().getSourceAllocator(), fs);
}


} // namespace FS
} // namespace Lumix
