#include "core/allocator.h"
#include "core/array.h"
#include "core/delegate_list.h"
#include "core/hash_map.h"
#include "core/log.h"
#include "core/sync.h"
#include "core/thread.h"
#include "core/os.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/stream.h"
#include "core/string.h"

#include "engine/file_system.h"

namespace Lumix {

struct FileIterator {
	os::FileIterator* iter;
	struct FileSystemImpl* fs;
	i32 mount_idx = -1;
};

struct AsyncItem {
	enum class Flags : u32 {
		NONE = 0,
		FAILED = 1 << 0,
		CANCELED = 1 << 1,
	};

	AsyncItem(IAllocator& allocator) : data(allocator) {}
	
	bool isFailed() const { return isFlagSet(flags, Flags::FAILED); }
	bool isCanceled() const { return isFlagSet(flags, Flags::CANCELED); }

	FileSystem::ContentCallback callback;
	OutputMemoryStream data;
	Path path;
	u32 id = 0;
	Flags flags = Flags::NONE;
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

struct Mount {
	Mount(IAllocator& allocator) : point(allocator), path(allocator) {}
	String point;
	String path;
};

struct FileSystemImpl : FileSystem {
	explicit FileSystemImpl(const char* engine_data_dir, IAllocator& allocator)
		: m_allocator(allocator)
		, m_queue(allocator)	
		, m_finished(allocator)	
		, m_last_id(0)
		, m_semaphore(0, 0xffFF)
		, m_mounts(m_allocator)
	{
		mount(engine_data_dir, "engine");
		mount(engine_data_dir, "");
	
		m_task.create(*this, m_allocator);
		m_task->create("Filesystem", true);
	}

	~FileSystemImpl() override {
		m_task->stop();
		m_task->destroy();
		m_task.destroy();
	}

	void mount(StringView path, StringView virtual_path) {
		Mount& m = m_mounts.emplace(m_allocator);
		m.path = path;
		if (!endsWith(m.path, "/") && !endsWith(m.path, "\\")) {
			m.path.append("/");
		}
		m.point = virtual_path;
	}

	bool hasWork() override
	{
		return m_work_counter != 0;
	}

	const char* getBasePath() const override { return m_mounts.last().path.c_str(); }

	void setBasePath(const char* dir) final {
		m_mounts.pop();
		Mount& m = m_mounts.emplace(m_allocator);
		m.point = "";
		StringView sv = dir;
		if (endsWith(sv, "/") || endsWith(sv, "/")) {
			sv.removeSuffix(1);
		}
		m.path = sv;
	}

	Path getFullPath(StringView virtual_path) const override {
		if (startsWith(virtual_path, "/") || startsWith(virtual_path, "\\")) {
			virtual_path.removePrefix(1);
		}
		for (Mount& m : m_mounts) {
			if (startsWith(virtual_path, m.point)) {
				// exact match
				if (virtual_path.size() == m.point.length()) {
					return Path(m.path);
				}
				char next = virtual_path[m.point.length()];
				if (m.point.length() == 0 || next == '\\' || next == '/') {
					virtual_path.removePrefix(m.point.length());
					return Path(m.path, "/", virtual_path);
				}
			}
		}
		ASSERT(false);
		return {};
	}

	bool saveContentSync(const Path& path, Span<const u8> content) override {
		os::OutputFile file;
		const Path full_path = getFullPath(path);
		if (!file.open(full_path.c_str())) return false;

		bool res = file.write(content.begin(), content.length());
		file.close();

		return res;
	}

	bool getContentSync(const Path& path, OutputMemoryStream& content) override {
		PROFILE_FUNCTION();
		os::InputFile file;
		const Path full_path = getFullPath(path);

		if (!file.open(full_path.c_str())) return false;

		content.resize(file.size());
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
				item.flags |= AsyncItem::Flags::CANCELED;
				--m_work_counter;
				return;
			}
		}
		for (AsyncItem& item : m_finished) {
			if (item.id == async.value) {
				item.flags |= AsyncItem::Flags::CANCELED;
				return;
			}
		}
		ASSERT(false);
	}


	bool open(StringView path, os::InputFile& file) override
	{
		const Path full_path = getFullPath(path);
		return file.open(full_path.c_str());
	}


	bool open(StringView path, os::OutputFile& file) override
	{
		const Path full_path = getFullPath(path);
		return file.open(full_path.c_str());
	}


	bool deleteFile(StringView path) override
	{
		const Path full_path = getFullPath(path);
		return os::deleteFile(full_path.c_str());
	}


	bool moveFile(StringView from, StringView to) override
	{
		const Path full_path_from = getFullPath(from);
		const Path full_path_to = getFullPath(to);
		return os::moveFile(full_path_from, full_path_to);
	}


	bool copyFile(StringView from, StringView to) override
	{
		const Path full_path_from = getFullPath(from);
		const Path full_path_to = getFullPath(to);
		return os::copyFile(full_path_from, full_path_to);
	}


	bool fileExists(StringView path) override {
		const Path full_path = getFullPath(path);
		return os::fileExists(full_path);
	}

	bool dirExists(StringView path) override {
		const Path full_path = getFullPath(path);
		return os::dirExists(full_path);
	}

	u64 getLastModified(StringView path) override
	{
		const Path full_path = getFullPath(path);
		return os::getLastModified(full_path);
	}


	FileIterator* createFileIterator(StringView dir) override
	{
		const Path path = getFullPath(dir);
		FileIterator* iter = LUMIX_NEW(m_allocator, FileIterator);
		iter->iter = os::createFileIterator(path, m_allocator);
		iter->fs = this;
		iter->mount_idx = -1;
		if (dir.size() == 0) iter->mount_idx = 0;
		else if (equalStrings(dir, "/")) iter->mount_idx = 0;
		else if (equalStrings(dir, "\\")) iter->mount_idx = 0;
		return iter;
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
				item.callback.invoke(Span((const u8*)item.data.data(), (u32)item.data.size()), !item.isFailed());
			}

			if (timer.getTimeSinceStart() > 0.1f) {
				break;
			}
		}
	}

	IAllocator& m_allocator;
	Local<FSTask> m_task;
	Array<AsyncItem> m_queue;
	u32 m_work_counter = 0;
	Array<AsyncItem> m_finished;
	Mutex m_mutex;
	Semaphore m_semaphore;
	Array<Mount> m_mounts;

	u32 m_last_id;
};


int FSTask::task()
{
	while (!m_finish) {
		m_fs.m_semaphore.wait();
		if (m_finish) break;

		Path path;
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
		bool success = m_fs.getContentSync(path, data);

		{
			MutexGuard lock(m_fs.m_mutex);
			if (!m_fs.m_queue[0].isCanceled()) {
				m_fs.m_finished.emplace(static_cast<AsyncItem&&>(m_fs.m_queue[0]));
				m_fs.m_finished.back().data = static_cast<OutputMemoryStream&&>(data);
				if(!success) {
					m_fs.m_finished.back().flags |= AsyncItem::Flags::FAILED;
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
	{
		if (!m_file.open(pak_path)) {
			logError("Failed to open game.pak");
			return;
		}
		const u32 count = m_file.read<u32>();
		for (u32 i = 0; i < count; ++i) {
			const FilePathHash hash = m_file.read<FilePathHash>();
			PackFile& f = m_map.insert(hash);
			f.offset = m_file.read<u64>();
			f.size = m_file.read<u64>();
		}
	}

	~PackFileSystem() {
		m_file.close();
	}

	bool getContentSync(const Path& path, OutputMemoryStream& content) override {
		ASSERT(content.size() == 0);
		StringView basename = Path::getBasename(path);
		u64 hashu64;
		fromCString(basename, hashu64);
		FilePathHash hash = FilePathHash::fromU64(hashu64);
		if (basename[0] < '0' || basename[0] > '9' || hashu64 == 0) {
			hash = path.getHash();
		}
		auto iter = m_map.find(hash);
		if (!iter.isValid()) {
			iter = m_map.find(path.getHash());
			if (!iter.isValid()) return false;
		}

		content.resize(iter.value().size);
		MutexGuard lock(m_mutex);
		const u32 header_size = sizeof(u32) + m_map.size() * (2 * sizeof(u64) + sizeof(FilePathHash));
		if (!m_file.seek(iter.value().offset + header_size) || !m_file.read(content.getMutableData(), content.size())) {
			logError("Could not read ", path);
			return false;
		}

		return true;
	}
	
	struct PackFile {
		u64 offset;
		u64 size;
	};

	HashMap<FilePathHash, PackFile> m_map;
	os::InputFile m_file;
};

void destroyFileIterator(FileIterator* iterator) {
	LUMIX_DELETE(iterator->fs->m_allocator, iterator);
}

bool getNextFile(FileIterator* iterator, os::FileInfo* info) {
	if (iterator->mount_idx >= 0) {
		info->is_directory = true;
		const Mount& m = iterator->fs->m_mounts[iterator->mount_idx];
		copyString(info->filename, m.point);
		++iterator->mount_idx;
		if (iterator->mount_idx == iterator->fs->m_mounts.size() - 1) {
			iterator->mount_idx = -1;
		}
		return true;
	}
	return os::getNextFile(iterator->iter, info);
}

UniquePtr<FileSystem> FileSystem::create(const char* base_path, IAllocator& allocator)
{
	return UniquePtr<FileSystemImpl>::create(allocator, base_path, allocator);
}

UniquePtr<FileSystem> FileSystem::createPacked(const char* pak_path, IAllocator& allocator)
{
	return UniquePtr<PackFileSystem>::create(allocator, pak_path, allocator);
}


} // namespace Lumix
