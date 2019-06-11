#include "engine/file_system.h"

#include "engine/array.h"
#include "engine/base_proxy_allocator.h"
#include "engine/delegate_list.h"
#include "engine/flag_set.h"
#include "engine/mt/sync.h"
#include "engine/mt/task.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/queue.h"
#include "engine/stream.h"
#include "engine/string.h"


namespace Lumix
{


struct AsyncItem
{
	enum class Flags : u32 {
		FAILED = 1 << 0,
		CANCELED = 1 << 1,
	};

	AsyncItem(IAllocator& allocator) : data(allocator) {}
	
	bool isFailed() const { return flags.isSet(Flags::FAILED); }
	bool isCanceled() const { return flags.isSet(Flags::CANCELED); }

	FileSystem::ContentCallback callback;
	Array<u8> data;
	StaticString<MAX_PATH_LENGTH> path;
	u32 id = 0;
	FlagSet<Flags, u32> flags;
};


struct FileSystemImpl;


class FSTask final : public MT::Task
{
public:
	FSTask(FileSystemImpl& fs, IAllocator& allocator)
		: MT::Task(allocator)
		, m_fs(fs)
	{
	}


	~FSTask() = default;


	void stop();
	int task() override;

private:
	FileSystemImpl& m_fs;
	bool m_finish = false;
};


struct FileSystemImpl final : public FileSystem
{
	explicit FileSystemImpl(const char* base_path, IAllocator& allocator)
		: m_allocator(allocator)
		, m_queue(allocator)	
		, m_finished(allocator)	
		, m_last_id(0)
		, m_semaphore(0, 0xffFF)
	{
		m_base_path = base_path;
		if (!endsWith(base_path, "/") && !endsWith(base_path, "\\")) {
			m_base_path << '/';
		}
		m_task = LUMIX_NEW(m_allocator, FSTask)(*this, m_allocator);
		m_task->create("Filesystem", true);
	}

	~FileSystemImpl()
	{
		m_task->stop();
		m_task->destroy();
		LUMIX_DELETE(m_allocator, m_task);
	}

	BaseProxyAllocator& getAllocator() { return m_allocator; }


	bool hasWork() override
	{
		MT::CriticalSectionLock lock(m_mutex);
		return !m_queue.empty();
	}



	const char* getBasePath() const override { return m_base_path; }


	void setBasePath(const char* dir) override
	{ 
		m_base_path = dir; 
		if (!endsWith(dir, "/") && !endsWith(dir, "\\")) {
			m_base_path << '/';
		}
	}



	AsyncHandle getContent(const Path& file, const ContentCallback& callback) override
	{
		if (!file.isValid()) return AsyncHandle::invalid();

		MT::CriticalSectionLock lock(m_mutex);
		AsyncItem& item = m_queue.emplace(m_allocator);
		++m_last_id;
		if (m_last_id == 0) ++m_last_id;
		item.id = m_last_id;
		item.path = file.c_str();
		item.callback = callback;
		m_semaphore.signal();
		return AsyncHandle(item.id);
	}


	void cancel(AsyncHandle async) override
	{
		MT::CriticalSectionLock lock(m_mutex);
		for (AsyncItem& item : m_queue) {
			if (item.id == async.value) {
				item.flags.set(AsyncItem::Flags::CANCELED);
				return;
			}
		}
		for (AsyncItem& item : m_finished) {
			if (item.id == async.value) {
				item.flags.set(AsyncItem::Flags::CANCELED);
				return;
			}
		}
	}


	bool open(const char* path, OS::InputFile* file) override
	{
		ASSERT(file);
		StaticString<MAX_PATH_LENGTH> full_path(m_base_path, path);
		return file->open(full_path);
	}


	bool open(const char* path, OS::OutputFile* file) override
	{
		ASSERT(file);
		StaticString<MAX_PATH_LENGTH> full_path(m_base_path, path);
		return file->open(full_path);
	}


	OS::FileIterator* createFileIterator(const char* dir) override
	{
		StaticString<MAX_PATH_LENGTH> path(m_base_path, dir);
		return OS::createFileIterator(path, m_allocator);
	}


	void updateAsyncTransactions() override
	{
		PROFILE_FUNCTION();

		for(;;) {
			m_mutex.enter();
			if (m_finished.empty()) {
				m_mutex.exit();
				break;
			}

			AsyncItem item = static_cast<AsyncItem&&>(m_finished[0]);
			m_finished.erase(0);

			m_mutex.exit();

			if(!item.isCanceled()) {
				item.callback.invoke(item.data.size(), item.data.begin(), !item.isFailed());
			}
		}
	}

	BaseProxyAllocator m_allocator;
	FSTask* m_task;
	StaticString<MAX_PATH_LENGTH> m_base_path;
	Array<AsyncItem> m_queue;
	Array<AsyncItem> m_finished;
	MT::CriticalSection m_mutex;
	MT::Semaphore m_semaphore;

	u32 m_last_id;
};


int FSTask::task()
{
	while (!m_finish) {
		m_fs.m_semaphore.wait();
		if (m_finish) break;

		StaticString<MAX_PATH_LENGTH> path;
		{
			MT::CriticalSectionLock lock(m_fs.m_mutex);
			ASSERT(!m_fs.m_queue.empty());
			path = m_fs.m_queue[0].path;
			if (m_fs.m_queue[0].isCanceled()) {
				m_fs.m_queue.erase(0);
				continue;
			}
		}

		bool success = true;
		OS::InputFile file;
		StaticString<MAX_PATH_LENGTH> full_path(m_fs.m_base_path, path);
		Array<u8> data(m_fs.m_allocator);
		
		if (file.open(full_path)) {
			data.resize((int)file.size());
			if (!file.read(data.begin(), data.byte_size())) {
				success = false;
			}
			file.close();
		}
		else {
			success = false;
		}

		{
			MT::CriticalSectionLock lock(m_fs.m_mutex);
			if (!m_fs.m_queue[0].isCanceled()) {
				m_fs.m_finished.emplace(static_cast<AsyncItem&&>(m_fs.m_queue[0]));
				m_fs.m_finished.back().data = static_cast<Array<u8>&&>(data);
				if(!success) {
					m_fs.m_finished.back().flags.set(AsyncItem::Flags::FAILED);
				}
				m_fs.m_queue.erase(0);
			}
		}
	}
	return 0;
}


void FSTask::stop()
{
	m_finish = true;
	m_fs.m_semaphore.signal();
}


FileSystem* FileSystem::create(const char* base_path, IAllocator& allocator)
{
	return LUMIX_NEW(allocator, FileSystemImpl)(base_path, allocator);
}

void FileSystem::destroy(FileSystem* fs)
{
	LUMIX_DELETE(static_cast<FileSystemImpl*>(fs)->getAllocator().getSourceAllocator(), fs);
}


} // namespace Lumix
