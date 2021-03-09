#include "engine/file_system.h"

#include "engine/allocator.h"
#include "engine/array.h"
#include "engine/crc32.h"
#include "engine/delegate_list.h"
#include "engine/flag_set.h"
#include "engine/hash_map.h"
#include "engine/metaprogramming.h"
#include "engine/log.h"
#include "engine/lz4.h"
#include "engine/sync.h"
#include "engine/thread.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/queue.h"
#include "engine/stream.h"
#include "engine/string.h"

namespace Lumix {

struct AsyncItem {
	enum class Flags : u32 {
		FAILED = 1 << 0,
		CANCELED = 1 << 1,
	};

	AsyncItem(IAllocator& allocator) : data(allocator) {}
	
	bool isFailed() const { return flags.isSet(Flags::FAILED); }
	bool isCanceled() const { return flags.isSet(Flags::CANCELED); }

	FileSystem::ContentCallback callback;
	OutputMemoryStream data;
	StaticString<LUMIX_MAX_PATH> path;
	u32 id = 0;
	FlagSet<Flags, u32> flags;
};


struct FileSystemImpl;


struct FSTask final : Thread {
	FSTask(FileSystemImpl& fs, IAllocator& allocator)
		: Thread(allocator)
		, m_fs(fs)
	{}

	~FSTask() = default;

	void stop();
	int task() override;

private:
	FileSystemImpl& m_fs;
	bool m_finish = false;
};


struct FileSystemImpl : FileSystem {
	explicit FileSystemImpl(const char* base_path, IAllocator& allocator)
		: m_allocator(allocator)
		, m_queue(allocator)	
		, m_finished(allocator)	
		, m_last_id(0)
		, m_semaphore(0, 0xffFF)
	{
		setBasePath(base_path);
		m_task.create(*this, m_allocator);
		m_task->create("Filesystem", true);
	}

	~FileSystemImpl() override {
		m_task->stop();
		m_task->destroy();
		m_task.destroy();
	}


	bool hasWork() override
	{
		return m_work_counter != 0;
	}

	const char* getBasePath() const override { return m_base_path; }

	void setBasePath(const char* dir) final
	{ 
		Path::normalize(dir, Span(m_base_path.data));
		if (!endsWith(m_base_path, "/") && !endsWith(m_base_path, "\\")) {
			m_base_path << '/';
		}
	}

	bool getContentSync(const Path& path, OutputMemoryStream& content) override {
		os::InputFile file;
		StaticString<LUMIX_MAX_PATH> full_path(m_base_path, path.c_str());

		if (!file.open(full_path)) return false;

		content.resize((int)file.size());
		if (!file.read(content.getMutableData(), content.size())) {
			logError("Could not read ", path);
			file.close();
			return false;
		}
		file.close();
		return true;
	}

	AsyncHandle getContent(const Path& file, const ContentCallback& callback) override
	{
		if (file.isEmpty()) return AsyncHandle::invalid();

		MutexGuard lock(m_mutex);
		++m_work_counter;
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
				--m_work_counter;
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


	bool open(const char* path, os::InputFile& file) override
	{
		StaticString<LUMIX_MAX_PATH> full_path(m_base_path, path);
		return file.open(full_path);
	}


	bool open(const char* path, os::OutputFile& file) override
	{
		StaticString<LUMIX_MAX_PATH> full_path(m_base_path, path);
		return file.open(full_path);
	}


	bool deleteFile(const char* path) override
	{
		StaticString<LUMIX_MAX_PATH> full_path(m_base_path, path);
		return os::deleteFile(full_path);
	}


	bool moveFile(const char* from, const char* to) override
	{
		StaticString<LUMIX_MAX_PATH> full_path_from(m_base_path, from);
		StaticString<LUMIX_MAX_PATH> full_path_to(m_base_path, to);
		return os::moveFile(full_path_from, full_path_to);
	}


	bool copyFile(const char* from, const char* to) override
	{
		StaticString<LUMIX_MAX_PATH> full_path_from(m_base_path, from);
		StaticString<LUMIX_MAX_PATH> full_path_to(m_base_path, to);
		return os::copyFile(full_path_from, full_path_to);
	}


	bool fileExists(const char* path) override
	{
		StaticString<LUMIX_MAX_PATH> full_path(m_base_path, path);
		return os::fileExists(full_path);
	}


	u64 getLastModified(const char* path) override
	{
		StaticString<LUMIX_MAX_PATH> full_path(m_base_path, path);
		return os::getLastModified(full_path);
	}


	os::FileIterator* createFileIterator(const char* dir) override
	{
		StaticString<LUMIX_MAX_PATH> path(m_base_path, dir);
		return os::createFileIterator(path, m_allocator);
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

		os::Timer timer;
		for(;;) {
			m_mutex.enter();
			if (m_finished.empty()) {
				m_mutex.exit();
				break;
			}

			AsyncItem item = static_cast<AsyncItem&&>(m_finished[0]);
			m_finished.erase(0);
			ASSERT(m_work_counter > 0);
			--m_work_counter;

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
	Local<FSTask> m_task;
	StaticString<LUMIX_MAX_PATH> m_base_path;
	Array<AsyncItem> m_queue;
	u32 m_work_counter = 0;
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

		StaticString<LUMIX_MAX_PATH> path;
		{
			MutexGuard lock(m_fs.m_mutex);
			ASSERT(!m_fs.m_queue.empty());
			path = m_fs.m_queue[0].path;
			if (m_fs.m_queue[0].isCanceled()) {
				m_fs.m_queue.erase(0);
				continue;
			}
		}

		OutputMemoryStream data(m_fs.m_allocator);
		bool success = m_fs.getContentSync(Path(path), data);

		{
			MutexGuard lock(m_fs.m_mutex);
			if (!m_fs.m_queue[0].isCanceled()) {
				m_fs.m_finished.emplace(static_cast<AsyncItem&&>(m_fs.m_queue[0]));
				m_fs.m_finished.back().data = static_cast<OutputMemoryStream&&>(data);
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

struct PackFileSystem : FileSystemImpl {
	PackFileSystem(const char* pak_path, IAllocator& allocator) 
		: FileSystemImpl("pack://", allocator) 
		, m_map(allocator)
		, m_allocator(allocator)
	{
		if (!m_file.open(pak_path)) {
			logError("Failed to open game.pak");
			return;
		}
		const u32 count = m_file.read<u32>();
		for (u32 i = 0; i < count; ++i) {
			const u32 hash = m_file.read<u32>();
			PackFile& f = m_map.insert(hash);
			f.offset = m_file.read<u64>();
			f.size = m_file.read<u64>();
			f.compressed_size = m_file.read<u64>();
		}
	}

	~PackFileSystem() {
		m_file.close();
	}

	bool getContentSync(const Path& path, OutputMemoryStream& content) override {
		Span<const char> basename = Path::getBasename(path.c_str());
		u32 hash;
		fromCString(basename, hash);
		if (basename[0] < '0' || basename[0] > '9' || hash == 0) {
			hash = path.getHash();
		}
		auto iter = m_map.find(hash);
		if (!iter.isValid()) return false;

		OutputMemoryStream compressed(m_allocator);
		compressed.resize(iter.value().compressed_size);
		MutexGuard lock(m_mutex);
		const u32 header_size = sizeof(u32) + m_map.size() * (3 * sizeof(u64) + sizeof(u32));
		if (!m_file.seek(iter.value().offset + header_size) || !m_file.read(compressed.getMutableData(), compressed.size())) {
			logError("Could not read ", path);
			return false;
		}

		content.resize(iter.value().size);
		const i32 res = LZ4_decompress_safe((const char*)compressed.data(), (char*)content.getMutableData(), (i32)iter.value().compressed_size, (i32)content.size());
		
		if (res != content.size()) {
			logError("Could not decompress ", path);
			return false;
		}
		return true;
	}
	
	struct PackFile {
		u64 offset;
		u64 size;
		u64 compressed_size;
	};

	IAllocator& m_allocator;
	HashMap<u32, PackFile> m_map;
	Mutex m_mutex;
	os::InputFile m_file;
};


UniquePtr<FileSystem> FileSystem::create(const char* base_path, IAllocator& allocator)
{
	return UniquePtr<FileSystemImpl>::create(allocator, base_path, allocator);
}

UniquePtr<FileSystem> FileSystem::createPacked(const char* pak_path, IAllocator& allocator)
{
	return UniquePtr<PackFileSystem>::create(allocator, pak_path, allocator);
}


} // namespace Lumix
