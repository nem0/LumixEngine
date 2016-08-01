#include "editor/platform_interface.h"
#include "engine/hash_map.h"
#include "engine/mt/task.h"
#include "engine/profiler.h"
#include "engine/string.h"
#include "file_system_watcher.h"
#include <sys/inotify.h>
#include <unistd.h>


struct FileSystemWatcherImpl;



struct FileSystemWatcherTask : public Lumix::MT::Task
{
	FileSystemWatcherTask(const char* path,
        FileSystemWatcherImpl& _watcher,
		Lumix::IAllocator& _allocator)
		: Task(_allocator)
		, watcher(_watcher)
		, watched(_allocator)
		, allocator(_allocator)
	{
		Lumix::copyString(this->path, path);
		int len = Lumix::stringLength(path);
		if(len > 0 && path[len - 1] != '/') Lumix::catString(this->path, "/");
	}


	int task() override;


    Lumix::IAllocator& allocator;
    FileSystemWatcherImpl& watcher;
	volatile bool finished;
	char path[Lumix::MAX_PATH_LENGTH];
	Lumix::HashMap<int, Lumix::StaticString<Lumix::MAX_PATH_LENGTH> > watched;
	int fd;
};


struct FileSystemWatcherImpl : public FileSystemWatcher
{
	explicit FileSystemWatcherImpl(Lumix::IAllocator& _allocator)
		: allocator(_allocator)
		, task(nullptr)
	{
	}


    ~FileSystemWatcherImpl()
    {
        if (task)
        {
            task->destroy();
            LUMIX_DELETE(allocator, task);
        }
    }


    bool start(const char* path)
    {
        task = LUMIX_NEW(allocator, FileSystemWatcherTask)(path, *this, allocator);
        if (!task->create("FileSystemWatcherTask"))
        {
            LUMIX_DELETE(allocator, task);
            task = nullptr;
            return false;
        }
        return true;
    }


	virtual Lumix::Delegate<void(const char*)>& getCallback() { return callback; }


    FileSystemWatcherTask* task;
	Lumix::Delegate<void(const char*)> callback;
	Lumix::IAllocator& allocator;
};


FileSystemWatcher* FileSystemWatcher::create(const char* path, Lumix::IAllocator& allocator)
{
	auto* watcher = LUMIX_NEW(allocator, FileSystemWatcherImpl)(allocator);
	if(!watcher->start(path))
    {
        LUMIX_DELETE(allocator, watcher);
        return nullptr;
    }
	return watcher;
}


void FileSystemWatcher::destroy(FileSystemWatcher* watcher)
{
	if (!watcher) return;
	auto* impl_watcher = (FileSystemWatcherImpl*)watcher;
	LUMIX_DELETE(impl_watcher->allocator, impl_watcher);
}


static void addWatch(FileSystemWatcherTask& task, const char* path, int root_length)
{
	if (!PlatformInterface::dirExists(path)) return;
	
    int wd = inotify_add_watch(task.fd, path, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE);
    task.watched.insert(wd, path + root_length);

    auto iter = PlatformInterface::createFileIterator(path, task.allocator);
    PlatformInterface::FileInfo info;
    while (PlatformInterface::getNextFile(iter, &info))
    {
        if (!info.is_directory) continue;
		if (Lumix::equalStrings(info.filename, ".")) continue;
		if (Lumix::equalStrings(info.filename, "..")) continue;

        Lumix::StaticString<Lumix::MAX_PATH_LENGTH> tmp(path, info.filename, "/");
        addWatch(task, tmp, root_length);
    }
    PlatformInterface::destroyFileIterator(iter);
}


static void getName(FileSystemWatcherTask& task, inotify_event* event, char* out, int max_size)
{
    auto iter = task.watched.find(event->wd);

    if (iter == task.watched.end())
    {
        Lumix::copyString(out, max_size, event->name);
        return;
    }

    Lumix::copyString(out, max_size, iter.value());
    Lumix::catString(out, max_size, event->name);
}


int FileSystemWatcherTask::task()
{
    fd = inotify_init();
    if (fd == -1) return false;

	int root_length = Lumix::stringLength(path);
    addWatch(*this, path, root_length);

    char buf[4096];
    while (!finished)
    {
        int r = read(fd, buf, sizeof(buf));
        if(r == -1) return 1;
        auto* event = (inotify_event*)buf;

        while ((char*)event < buf + r)
        {
            char tmp[Lumix::MAX_PATH_LENGTH];
            getName(*this, event, tmp, Lumix::lengthOf(tmp));
            if (event->mask & IN_CREATE) addWatch(*this, tmp, root_length);
            watcher.callback.invoke(tmp);

            event = (inotify_event*)((char*)event + sizeof(*event) + event->len);
        }
    }

    return close(fd) != -1;
}

