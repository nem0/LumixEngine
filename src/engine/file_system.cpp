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
#include "engine/resource.h"

namespace Lumix {

struct FileIterator {
	os::FileIterator* iter;
	struct FileSystemImpl* fs;
	i32 mount_idx = -1;
};

struct QueueItem {
	enum class Type : u8 {
		INVALID,
		GET_CONTENT,
		OPEN_STREAM,
		CLOSE_STREAM,
		STREAM
	};

	enum class Flags : u32 {
		NONE = 0,
		FAILED = 1 << 0,
		CANCELED = 1 << 1,
	};

	QueueItem(IAllocator& allocator) : data(allocator) {}
	
	bool isFailed() const { return isFlagSet(flags, Flags::FAILED); }
	bool isCanceled() const { return isFlagSet(flags, Flags::CANCELED); }

	Type type = Type::INVALID;
	Flags flags = Flags::NONE;
	FileSystem::ContentCallback callback;
	FileSystem::StreamCallback stream_callback;
	u64 user_data = 0;
	OutputMemoryStream data;
	Path path;
	u32 id = 0;
	FileSystem::StreamHandle stream_id = {};
	u64 offset = 0;
	u64 size = 0;
};

struct FileSystemImpl;


struct FSTask final : Thread {
	struct Stream {
		Stream() = default;
		Stream(Stream&&) = default;
		// streams that were never closed explicitly are closed on shutdown
		~Stream() { file.close(); }
		os::InputFile file; // not used by packed streams, they read from the pak file
		u64 base = 0;       // offset of the stream's first byte in the underlying file
		u64 size = ~(u64)0; // size of the readable window, reads outside of it fail
	};

	FSTask(FileSystemImpl& fs, IAllocator& allocator)
		: Thread(allocator)
		, m_fs(fs)
		, m_streams(allocator)
	{}

	~FSTask() = default;

	void stop();
	int task() override;

private:
	FileSystemImpl& m_fs;
	bool m_finish = false;
	HashMap<u32, FSTask::Stream> m_streams;
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
	
		m_task.create(*this, m_allocator);
		m_task->create("Filesystem", true);
	}

	~FileSystemImpl() override {
		m_task->stop();
		m_task->destroy();
		m_task.destroy();
	}

	const char* getEngineDataDir() override {
		return m_mounts[0].path.c_str();
	}

	void mount(StringView path, StringView virtual_path) override {
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

	bool getFullPath(StringView virtual_path, Path& out) const override {
		if (startsWith(virtual_path, "/") || startsWith(virtual_path, "\\")) {
			virtual_path.removePrefix(1);
		}
		for (Mount& m : m_mounts) {
			if (startsWith(virtual_path, m.point)) {
				// exact match
				if (virtual_path.size() == m.point.length()) {
					out = m.path;
					return true;
				}
				char next = virtual_path[m.point.length()];
				if (m.point.length() == 0 || next == '\\' || next == '/') {
					virtual_path.removePrefix(m.point.length());
					out = Path(m.path, "/", virtual_path);
					return true;
				}
			}
		}
		return false;
	}

	bool saveContentSync(const Path& path, Span<const u8> content) override {
		os::OutputFile file;
		Path full_path;
		if (!getFullPath(path, full_path)) return false;
		if (!file.open(full_path.c_str())) return false;

		bool res = file.write(content.begin(), content.length());
		file.close();

		return res;
	}

	bool getContentSync(const Path& path, OutputMemoryStream& content) override {
		PROFILE_FUNCTION();
		os::InputFile file;
		Path full_path;
		if (!getFullPath(path, full_path)) return false;

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

	AsyncHandle getContent(const Path& file, const ContentCallback& callback) override {
		CHECK(!file.isEmpty());

		MutexGuard lock(m_mutex);
		QueueItem& item = queueItem(QueueItem::Type::GET_CONTENT);
		item.path = file.c_str();
		item.callback = callback;
		m_semaphore.signal();
		return AsyncHandle(item.id);
	}

	void cancel(AsyncHandle async) override {
		MutexGuard lock(m_mutex);
		for (QueueItem& item : m_queue) {
			if (item.id == async.value) {
				item.flags |= QueueItem::Flags::CANCELED;
				ASSERT(item.type != QueueItem::Type::CLOSE_STREAM);
				ASSERT(item.type != QueueItem::Type::OPEN_STREAM);
				ASSERT(item.type != QueueItem::Type::STREAM);
				--m_work_counter;
				return;
			}
		}
		for (QueueItem& item : m_finished) {
			if (item.id == async.value) {
				item.flags |= QueueItem::Flags::CANCELED;
				ASSERT(item.type != QueueItem::Type::CLOSE_STREAM);
				ASSERT(item.type != QueueItem::Type::OPEN_STREAM);
				ASSERT(item.type != QueueItem::Type::STREAM);
				return;
			}
		}
		ASSERT(false);
	}


	bool open(StringView path, os::InputFile& file) override
	{
		Path full_path;
		if (!getFullPath(path, full_path)) return false;
		return file.open(full_path.c_str());
	}


	bool open(StringView path, os::OutputFile& file) override {
		Path full_path;
		if (!getFullPath(path, full_path)) return false;
		return file.open(full_path.c_str());
	}


	bool deleteFile(StringView path) override {
		Path full_path;
		if (!getFullPath(path, full_path)) return false;
		return os::deleteFile(full_path.c_str());
	}


	bool moveFile(StringView from, StringView to) override {
		Path full_path_from;
		if (!getFullPath(from, full_path_from)) return false;
		Path full_path_to;
		if (!getFullPath(to, full_path_to)) return false;
		return os::moveFile(full_path_from, full_path_to);
	}


	bool copyFile(StringView from, StringView to) override {
		Path full_path_from;
		if (!getFullPath(from, full_path_from)) return false;
		Path full_path_to;
		if (!getFullPath(to, full_path_to)) return false;
		return os::copyFile(full_path_from, full_path_to);
	}

	bool fileExists(StringView path) override {
		Path full_path;
		if (!getFullPath(path, full_path)) return false;
		return os::fileExists(full_path);
	}

	bool dirExists(StringView path) override {
		Path full_path;
		if (!getFullPath(path, full_path)) return false;
		return os::dirExists(full_path);
	}

	u64 getLastModified(StringView path) override {
		Path full_path;
		if (!getFullPath(path, full_path)) return 0;
		return os::getLastModified(full_path);
	}


	FileIterator* createFileIterator(StringView dir) override
	{
		Path path;
		if (!getFullPath(dir, path)) return nullptr;

		FileIterator* iter = LUMIX_NEW(m_allocator, FileIterator);
		iter->iter = os::createFileIterator(path, m_allocator);
		iter->fs = this;
		iter->mount_idx = -1;
		if (dir.size() == 0) iter->mount_idx = 0;
		else if (equalStrings(dir, "/")) iter->mount_idx = 0;
		else if (equalStrings(dir, "\\")) iter->mount_idx = 0;
		if (!iter->iter && iter->mount_idx < 0) {
			LUMIX_DELETE(m_allocator, iter);
			return nullptr;
		}
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

			QueueItem item = static_cast<QueueItem&&>(m_finished[0]);
			m_finished.erase(0);
			ASSERT(m_work_counter > 0);
			--m_work_counter;

			m_mutex.exit();

			if(!item.isCanceled()) {
				switch (item.type) {
					case QueueItem::Type::GET_CONTENT:
						item.callback.invoke(Span((const u8*)item.data.data(), (u32)item.data.size()), !item.isFailed());
						break;
					case QueueItem::Type::INVALID:
						CHECK(false);
						break;
					case QueueItem::Type::STREAM:
						item.stream_callback.invoke(item.user_data, Span((const u8*)item.data.data(), (u32)item.data.size()), !item.isFailed());
						break;
					case QueueItem::Type::CLOSE_STREAM:
					case QueueItem::Type::OPEN_STREAM:
						break;
				}
			}

			if (timer.getTimeSinceStart() > 0.1f) {
				break;
			}
		}
	}

	// called from FS thread, `out` is filled on success
	virtual bool openStreamImpl(const Path& path, FSTask::Stream& out) {
		Path full_path;
		if (!getFullPath(path, full_path) || !out.file.open(full_path.c_str())) return false;
		if (!isCompiledResource(path)) return true;
		
		CompiledResourceHeader header;
		if (!out.file.read(&header, sizeof(header)) || !isStreamable(header)) return false;
		out.base = sizeof(header);
		return true;
	}

	static bool isCompiledResource(const Path& path) { return Path::hasExtension(path, "res"); }

	// streams of compiled resources start after the header, offsets are in the stored data. Compressed data can't be read at an offset.
	static bool isStreamable(const CompiledResourceHeader& header) {
		if (header.magic != CompiledResourceHeader::MAGIC) return false;
		if (header.flags & CompiledResourceHeader::COMPRESSED) {
			logError("Can not stream a compressed resource, it has to be written with try_compress = false");
			return false;
		}
		return true;
	}

	// called from FS thread
	virtual bool readStreamImpl(FSTask::Stream& stream, u64 offset, u64 size, OutputMemoryStream& out) {
		if (size > stream.size || offset > stream.size - size) return false;
		out.resize(size);
		return stream.file.seek(stream.base + offset) && stream.file.read(out.getMutableData(), size);
	}

	QueueItem& queueItem(QueueItem::Type type) {
		++m_work_counter;
		QueueItem& item = m_queue.emplace(m_allocator);
		++m_last_id;
		if (m_last_id == 0) ++m_last_id;
		item.id = m_last_id;
		item.type = type;
		return item;
	}

	// must be called with m_mutex locked, 0 is reserved for invalid handle
	StreamHandle allocStreamHandle() {
		++m_last_stream_id;
		if (m_last_stream_id == 0) ++m_last_stream_id;
		return (StreamHandle)m_last_stream_id;
	}

	StreamHandle openStreaming(const Path& file) override {
		CHECK(!file.isEmpty());

		MutexGuard lock(m_mutex);
		QueueItem& item = queueItem(QueueItem::Type::OPEN_STREAM);
		item.path = file.c_str();
		item.stream_id = allocStreamHandle();
		m_semaphore.signal();
		return item.stream_id;
	}

	void stream(StreamHandle handle, u64 user_data, u64 offset, u64 size, StreamCallback& callback) override {
		CHECK((u32)handle != 0);

		MutexGuard lock(m_mutex);
		QueueItem& item = queueItem(QueueItem::Type::STREAM);
		item.stream_id = handle;
		item.user_data = user_data;
		item.offset = offset;
		item.size = size;
		item.stream_callback = callback;
		m_semaphore.signal();
	}

	void close(StreamHandle handle) override {
		CHECK((u32)handle != 0);

		MutexGuard lock(m_mutex);
		QueueItem& item = queueItem(QueueItem::Type::CLOSE_STREAM);
		item.stream_id = handle;
		m_semaphore.signal();
	}

	IAllocator& m_allocator;
	Local<FSTask> m_task;
	Array<QueueItem> m_queue;
	u32 m_work_counter = 0;
	Array<QueueItem> m_finished;
	Mutex m_mutex;
	Semaphore m_semaphore;
	Array<Mount> m_mounts;

	u32 m_last_id;
	u32 m_last_stream_id = 0;
};


int FSTask::task() {
	while (!m_finish) {
		m_fs.m_semaphore.wait();
		if (m_finish) break;

		QueueItem::Type item_type;
		Path path;
		u32 stream_id;
		u64 offset;
		u64 size;
		{
			MutexGuard lock(m_fs.m_mutex);
			CHECK(!m_fs.m_queue.empty());
			path = m_fs.m_queue[0].path;
			item_type = m_fs.m_queue[0].type;
			stream_id = (u32)m_fs.m_queue[0].stream_id;
			offset = m_fs.m_queue[0].offset;
			size = m_fs.m_queue[0].size;
			if (m_fs.m_queue[0].isCanceled()) {
				m_fs.m_queue.erase(0);
				ASSERT(item_type != QueueItem::Type::OPEN_STREAM);
				ASSERT(item_type != QueueItem::Type::CLOSE_STREAM);
				continue;
			}
		}

		switch (item_type) {
			case QueueItem::Type::GET_CONTENT: {
				OutputMemoryStream data(m_fs.m_allocator);
				bool success = m_fs.getContentSync(path, data);

				{
					MutexGuard lock(m_fs.m_mutex);
					if (!m_fs.m_queue[0].isCanceled()) {
						m_fs.m_finished.emplace(static_cast<QueueItem&&>(m_fs.m_queue[0]));
						m_fs.m_finished.back().data = static_cast<OutputMemoryStream&&>(data);
						if(!success) {
							m_fs.m_finished.back().flags |= QueueItem::Flags::FAILED;
						}
					}
					m_fs.m_queue.erase(0);
				}
				break;
			}
			case QueueItem::Type::STREAM: {
				auto iter = m_streams.find(stream_id);
				bool success = iter.isValid();
				OutputMemoryStream data(m_fs.m_allocator);
				if (success) {
					success = m_fs.readStreamImpl(iter.value(), offset, size, data);
				}

				MutexGuard lock(m_fs.m_mutex);
				ASSERT(!m_fs.m_queue[0].isCanceled());
				m_fs.m_finished.emplace(static_cast<QueueItem&&>(m_fs.m_queue[0]));
				if(success) {
					m_fs.m_finished.back().data = static_cast<OutputMemoryStream&&>(data);
				}
				else {
					m_fs.m_finished.back().flags |= QueueItem::Flags::FAILED;
				}
				m_fs.m_queue.erase(0);
				break;
			}
			case QueueItem::Type::OPEN_STREAM: {
				CHECK(!m_streams.find(stream_id).isValid());
				FSTask::Stream s;
				const bool success = m_fs.openStreamImpl(path, s);
				if (success) {
					m_streams.insert(stream_id, static_cast<FSTask::Stream&&>(s));
				}
				MutexGuard lock(m_fs.m_mutex);
				ASSERT(!m_fs.m_queue[0].isCanceled());
				m_fs.m_finished.emplace(static_cast<QueueItem&&>(m_fs.m_queue[0]));
				if(!success) {
					m_fs.m_finished.back().flags |= QueueItem::Flags::FAILED;
				}
				m_fs.m_queue.erase(0);
				break;
			}
			case QueueItem::Type::CLOSE_STREAM: {
				auto iter = m_streams.find(stream_id);
				const bool found = iter.isValid();
				if (found) {
					iter.value().file.close();
					m_streams.erase(stream_id);
				}
				MutexGuard lock(m_fs.m_mutex);
				ASSERT(!m_fs.m_queue[0].isCanceled());
				m_fs.m_finished.emplace(static_cast<QueueItem&&>(m_fs.m_queue[0]));
				if (!found) m_fs.m_finished.back().flags |= QueueItem::Flags::FAILED;
				m_fs.m_queue.erase(0);
				break;
			}
			case QueueItem::Type::INVALID:
				CHECK(false);
				break;
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
		m_header_size = sizeof(u32) + m_map.size() * (2 * sizeof(u64) + sizeof(FilePathHash));
	}

	~PackFileSystem() {
		m_file.close();
	}

	struct PackFile {
		u64 offset;
		u64 size;
	};

	bool findEntry(const Path& path, PackFile& out) const {
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
		out = iter.value();
		return true;
	}

	bool getContentSync(const Path& path, OutputMemoryStream& content) override {
		ASSERT(content.size() == 0);
		PackFile entry;
		if (!findEntry(path, entry)) return false;

		content.resize(entry.size);
		MutexGuard lock(m_file_mutex);
		if (!m_file.seek(entry.offset + m_header_size) || !m_file.read(content.getMutableData(), content.size())) {
			logError("Could not read ", path);
			return false;
		}

		return true;
	}

	bool openStreamImpl(const Path& path, FSTask::Stream& out) override {
		PackFile entry;
		if (!findEntry(path, entry)) return false;
		out.base = entry.offset + m_header_size;
		out.size = entry.size;
		if (!isCompiledResource(path)) return true;

		CompiledResourceHeader header;
		if (entry.size < sizeof(header)) return false;
		{
			MutexGuard lock(m_file_mutex);
			if (!m_file.seek(out.base) || !m_file.read(&header, sizeof(header))) return false;
		}
		if (!isStreamable(header)) return false;
		out.base += sizeof(header);
		out.size -= sizeof(header);
		return true;
	}

	bool readStreamImpl(FSTask::Stream& stream, u64 offset, u64 size, OutputMemoryStream& out) override {
		if (size > stream.size || offset > stream.size - size) return false;
		out.resize(size);
		MutexGuard lock(m_file_mutex);
		return m_file.seek(stream.base + offset) && m_file.read(out.getMutableData(), size);
	}

	HashMap<FilePathHash, PackFile> m_map;
	os::InputFile m_file;
	Mutex m_file_mutex; // seek + read on m_file must be atomic
	u64 m_header_size = 0;
};

void destroyFileIterator(FileIterator* iterator) {
	if (iterator->iter) os::destroyFileIterator(iterator->iter);
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
	if (!iterator->iter) return false;
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
