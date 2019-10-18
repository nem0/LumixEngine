#include "engine/file_system.h"

#include "engine/allocator.h"
#include "engine/array.h"
#include "engine/crc32.h"
#include "engine/delegate_list.h"
#include "engine/flag_set.h"
#include "engine/hash_map.h"
#include "engine/log.h"
#include "engine/mt/sync.h"
#include "engine/mt/task.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/path_utils.h"
#include "engine/profiler.h"
#include "engine/queue.h"
#include "engine/stream.h"
#include "engine/string.h"
#ifdef _WIN32
	#include <Windows.h>
#endif

namespace Lumix
{


struct TarHeader {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char chksum[8];
	char typeflag;
	char linkname[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char devmajor[8];
	char devminor[8];
	char prefix[155];   
};


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
		, m_bundled(allocator)
		, m_bundled_map(allocator)
	{
		setBasePath(base_path);
		loadBundled();
		m_task = LUMIX_NEW(m_allocator, FSTask)(*this, m_allocator);
		m_task->create("Filesystem", true);
	}


	~FileSystemImpl()
	{
		m_task->stop();
		m_task->destroy();
		LUMIX_DELETE(m_allocator, m_task);
	}


	void loadBundled() {
		#ifdef _WIN32
			HRSRC hrsrc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(102), "TAR");
			if (!hrsrc) return;
			HGLOBAL hglobal = LoadResource(GetModuleHandle(NULL), hrsrc);
			if (!hglobal) return;
			const DWORD size = SizeofResource(GetModuleHandle(NULL), hrsrc);
			if (size == 0) return;
			const void* res_mem = LockResource(hglobal);
			if (!res_mem) return;
	
			TCHAR exe_path[MAX_PATH_LENGTH];
			GetModuleFileName(NULL, exe_path, MAX_PATH_LENGTH);

			m_bundled_last_modified = OS::getLastModified(exe_path);
			m_bundled.resize((int)size);
			memcpy(m_bundled.begin(), res_mem, m_bundled.byte_size());
			InputMemoryStream str(m_bundled.begin(), m_bundled.byte_size());

			UnlockResource(res_mem);

			TarHeader header;
			while (str.getPosition() < str.size()) {
				const u8* ptr = (const u8*)str.getData() + str.getPosition();
				str.read(&header, sizeof(header));
				u32 size;
				fromCStringOctal(Span(header.size, sizeof(header.size)), Ref(size)); 
				if (header.name[0] && header.typeflag == 0 || header.typeflag == '0') {
					Path path(header.name);
					m_bundled_map.insert(path.getHash(), ptr);
				}

				str.setPosition(str.getPosition() + (512 - str.getPosition() % 512) % 512);
				str.setPosition(str.getPosition() + size + (512 - size % 512) % 512);
			}
		#endif
	}


	bool hasWork() override
	{
		MT::CriticalSectionLock lock(m_mutex);
		return !m_queue.empty();
	}



	const char* getBasePath() const override { return m_base_path; }


	void setBasePath(const char* dir) override
	{ 
		PathUtils::normalize(dir, Span(m_base_path.data));
		if (!endsWith(m_base_path, "/") && !endsWith(m_base_path, "\\")) {
			m_base_path << '/';
		}
	}

	bool getContentSync(const Path& path, Ref<Array<u8>> content) override {
		OS::InputFile file;
		StaticString<MAX_PATH_LENGTH> full_path(m_base_path, path.c_str());

		if (!file.open(full_path)) {
			auto iter = m_bundled_map.find(path.getHash());
			if (iter.isValid()) {
				const TarHeader* header = (const TarHeader*)iter.value();
				u32 size;
				fromCStringOctal(Span(header->size), Ref(size));
				content->resize(size);
				copyMemory(content->begin(), iter.value() + 512, content->byte_size());
				return true;
			}
			return false;
		}

		content->resize((int)file.size());
		if (!file.read(content->begin(), content->byte_size())) {
			file.close();
			return false;
		}
		file.close();
		return true;
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
		return OS::deleteFile(path);
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
		if (OS::copyFile(full_path_from, full_path_to)) return true;

		auto iter = m_bundled_map.find(crc32(from));
		if(!iter.isValid()) return false;

		OS::OutputFile file;
		if(!file.open(full_path_to)) return false;

		u32 size;
		TarHeader* header = (TarHeader*)iter.value();
		fromCStringOctal(Span(header->size), Ref(size));
		const bool res = file.write(iter.value() + 512, size);
		file.close();
		return res;
	}


	bool fileExists(const char* path) override
	{
		StaticString<MAX_PATH_LENGTH> full_path(m_base_path, path);
		if (!OS::fileExists(full_path)) {
			return (m_bundled_map.find(crc32(path)).isValid());
		}
		return true;
	}


	u64 getLastModified(const char* path) override
	{
		StaticString<MAX_PATH_LENGTH> full_path(m_base_path, path);
		const u64 res = OS::getLastModified(full_path);
		if (!res && m_bundled_map.find(crc32(path)).isValid()) {
			return m_bundled_last_modified;
		}
		return res;
	}


	OS::FileIterator* createFileIterator(const char* dir) override
	{
		StaticString<MAX_PATH_LENGTH> path(m_base_path, dir);
		return OS::createFileIterator(path, m_allocator);
	}


	void updateAsyncTransactions() override
	{
		PROFILE_FUNCTION();

		OS::Timer timer;
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
	Array<u8> m_bundled;
	HashMap<u32, const u8*> m_bundled_map;
	u64 m_bundled_last_modified;
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
		
		Array<u8> data(m_fs.m_allocator);

		OS::InputFile file;
		StaticString<MAX_PATH_LENGTH> full_path(m_fs.m_base_path, path);
		
		if (file.open(full_path)) {
			data.resize((int)file.size());
			if (!file.read(data.begin(), data.byte_size())) {
				success = false;
			}
			file.close();
		}
		else {
			auto iter = m_fs.m_bundled_map.find(crc32(path));
			if (iter.isValid()) {
				const TarHeader* header = (const TarHeader*)iter.value();
				u32 size;
				fromCStringOctal(Span(header->size), Ref(size));
				data.resize(size);
				copyMemory(data.begin(), iter.value() + 512, data.byte_size());
				success = true;
			}
			else {
				success = false;
			}
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
	LUMIX_DELETE(static_cast<FileSystemImpl*>(fs)->m_allocator, fs);
}


} // namespace Lumix
