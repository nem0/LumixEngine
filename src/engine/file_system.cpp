#include "engine/file_system.h"

#include "engine/allocator.h"
#include "engine/array.h"
#include "engine/crc32.h"
#include "engine/delegate_list.h"
#include "engine/flag_set.h"
#include "engine/hash_map.h"
#include "engine/metaprogramming.h"
#include "engine/log.h"
#include "engine/sync.h"
#include "engine/thread.h"
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
	OutputMemoryStream data;
	StaticString<MAX_PATH_LENGTH> path;
	u32 id = 0;
	FlagSet<Flags, u32> flags;
};


struct FileSystemImpl;


struct FSTask final : Thread
{
public:
	FSTask(FileSystemImpl& fs, IAllocator& allocator)
		: Thread(allocator)
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


struct FileSystemImpl final : FileSystem
{
	explicit FileSystemImpl(const char* base_path, IAllocator& allocator)
		: m_allocator(allocator)
		, m_queue(allocator)	
		, m_finished(allocator)	
		, m_last_id(0)
		, m_semaphore(0, 0xffFF)
	{
		setBasePath(base_path);
		m_task = LUMIX_NEW(m_allocator, FSTask)(*this, m_allocator);
		m_task->create("Filesystem", true);
	}


	~FileSystemImpl()
	{
		m_task->stop();
		m_task->destroy();
		LUMIX_DELETE(m_allocator, m_task);
	}


	bool hasWork() override
	{
		MutexGuard lock(m_mutex);
		return !m_queue.empty();
	}



	const char* getBasePath() const override { return m_base_path; }


	void setBasePath(const char* dir) override
	{ 
		Path::normalize(dir, Span(m_base_path.data));
		if (!endsWith(m_base_path, "/") && !endsWith(m_base_path, "\\")) {
			m_base_path << '/';
		}
	}

	bool getContentSync(const Path& path, Ref<OutputMemoryStream> content) override {
		OS::InputFile file;
		StaticString<MAX_PATH_LENGTH> full_path(m_base_path, path.c_str());

		if (!file.open(full_path)) return false;

		content->resize((int)file.size());
		if (!file.read(content->getMutableData(), content->size())) {
			file.close();
			return false;
		}
		file.close();
		return true;
	}

	AsyncHandle getContent(const Path& file, const ContentCallback& callback) override
	{
		if (!file.isValid()) return AsyncHandle::invalid();

		MutexGuard lock(m_mutex);
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
		MutexGuard lock(m_mutex);
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


	bool open(const char* path, Ref<OS::InputFile> file) override
	{
		StaticString<MAX_PATH_LENGTH> full_path(m_base_path, path);
		return file->open(full_path);
	}


	bool open(const char* path, Ref<OS::OutputFile> file) override
	{
		StaticString<MAX_PATH_LENGTH> full_path(m_base_path, path);
		return file->open(full_path);
	}


	bool deleteFile(const char* path) override
	{
		StaticString<MAX_PATH_LENGTH> full_path(m_base_path, path);
		return OS::deleteFile(full_path);
	}


	bool moveFile(const char* from, const char* to) override
	{
		StaticString<MAX_PATH_LENGTH> full_path_from(m_base_path, from);
		StaticString<MAX_PATH_LENGTH> full_path_to(m_base_path, to);
		return OS::moveFile(full_path_from, full_path_to);
	}


	bool copyFile(const char* from, const char* to) override
	{
		StaticString<MAX_PATH_LENGTH> full_path_from(m_base_path, from);
		StaticString<MAX_PATH_LENGTH> full_path_to(m_base_path, to);
		return OS::copyFile(full_path_from, full_path_to);
	}


	bool fileExists(const char* path) override
	{
		StaticString<MAX_PATH_LENGTH> full_path(m_base_path, path);
		return OS::fileExists(full_path);
	}


	u64 getLastModified(const char* path) override
	{
		StaticString<MAX_PATH_LENGTH> full_path(m_base_path, path);
		return OS::getLastModified(full_path);
	}


	OS::FileIterator* createFileIterator(const char* dir) override
	{
		StaticString<MAX_PATH_LENGTH> path(m_base_path, dir);
		return OS::createFileIterator(path, m_allocator);
	}

	void makeAbsolute(Span<char> absolute, const char* relative) const override {
		bool is_absolute = relative[0] == '\\' || relative[0] == '/';
		is_absolute = is_absolute || (relative[0] != 0 && relative[1] == ':');

		if (is_absolute) {
			copyString(absolute, relative);
			return;
		}

		copyString(absolute, m_base_path);
		catString(absolute, relative);
	}

	bool makeRelative(Span<char> relative, const char* absolute) const override {
		if (startsWith(absolute, m_base_path)) {
			copyString(relative, absolute + stringLength(m_base_path));
			return true;
		}
		copyString(relative, absolute);
		return false;
	}

	void processCallbacks() override
	{
		PROFILE_FUNCTION();

		OS::Timer timer;
		for(;;) {
			m_mutex.enter();
			if (m_finished.empty()) {
				m_mutex.exit();
				break;
			}

			AsyncItem item = Move(m_finished[0]);
			m_finished.erase(0);

			m_mutex.exit();

			if(!item.isCanceled()) {
				item.callback.invoke(item.data.size(), (const u8*)item.data.data(), !item.isFailed());
			}

			if (timer.getTimeSinceStart() > 0.1f) {
				break;
			}
		}
	}

	IAllocator& m_allocator;
	FSTask* m_task;
	StaticString<MAX_PATH_LENGTH> m_base_path;
	Array<AsyncItem> m_queue;
	Array<AsyncItem> m_finished;
	Mutex m_mutex;
	Semaphore m_semaphore;

	u32 m_last_id;
};


int FSTask::task()
{
	while (!m_finish) {
		m_fs.m_semaphore.wait();
		if (m_finish) break;

		StaticString<MAX_PATH_LENGTH> path;
		{
			MutexGuard lock(m_fs.m_mutex);
			ASSERT(!m_fs.m_queue.empty());
			path = m_fs.m_queue[0].path;
			if (m_fs.m_queue[0].isCanceled()) {
				m_fs.m_queue.erase(0);
				continue;
			}
		}

		bool success = true;
		
		OutputMemoryStream data(m_fs.m_allocator);

		OS::InputFile file;
		StaticString<MAX_PATH_LENGTH> full_path(m_fs.m_base_path, path);
		
		if (file.open(full_path)) {
			data.resize((int)file.size());
			if (!file.read(data.getMutableData(), data.size())) {
				success = false;
			}
			file.close();
		}
		else {
			success = false;
		}

		{
			MutexGuard lock(m_fs.m_mutex);
			if (!m_fs.m_queue[0].isCanceled()) {
				m_fs.m_finished.emplace(Move(m_fs.m_queue[0]));
				m_fs.m_finished.back().data = Move(data);
				if(!success) {
					m_fs.m_finished.back().flags.set(AsyncItem::Flags::FAILED);
				}
			}
			m_fs.m_queue.erase(0);
		}
	}
	return 0;
}


void FSTask::stop()
{
	m_finish = true;
	m_fs.m_semaphore.signal();
}


UniquePtr<FileSystem> FileSystem::create(const char* base_path, IAllocator& allocator)
{
	return UniquePtr<FileSystemImpl>::create(allocator, base_path, allocator);
}


} // namespace Lumix
