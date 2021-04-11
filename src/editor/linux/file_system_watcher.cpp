#include "engine/hash_map.h"
#include "engine/thread.h"
#include "engine/delegate.h"
#include "engine/os.h"
#include "engine/profiler.h"
#include "engine/string.h"
#include "file_system_watcher.h"
#include <sys/inotify.h>
#include <unistd.h>


namespace Lumix
{


struct FileSystemWatcherImpl;



struct FileSystemWatcherTask : Lumix::Thread
{
	FileSystemWatcherTask(const char* path,
        FileSystemWatcherImpl& _watcher,
		Lumix::IAllocator& _allocator)
		: Thread(_allocator)
		, watcher(_watcher)
		, watched(_allocator)
		, allocator(_allocator)
	{
		Lumix::copyString(this->path, path);
		int len = Lumix::stringLength(path);
		if(len > 0 && path[len - 1] != '/') Lumix::catString(this->path, "/");
	}


	int task() override;

    void cancel() {
        finished = true;
        close(fd);
    }

    Lumix::IAllocator& allocator;
    FileSystemWatcherImpl& watcher;
	volatile bool finished;
	char path[LUMIX_MAX_PATH];
	Lumix::HashMap<int, Lumix::StaticString<LUMIX_MAX_PATH> > watched;
	int fd;
};


struct FileSystemWatcherImpl : FileSystemWatcher
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
            task->cancel();
            task->destroy();
            LUMIX_DELETE(allocator, task);
        }
    }


    bool start(const char* path)
    {
        task = LUMIX_NEW(allocator, FileSystemWatcherTask)(path, *this, allocator);
        if (!task->create("FileSystemWatcherTask", true))
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


UniquePtr<FileSystemWatcher> FileSystemWatcher::create(const char* path, Lumix::IAllocator& allocator)
{
	UniquePtr<FileSystemWatcherImpl> watcher = UniquePtr<FileSystemWatcherImpl>::create(allocator, allocator);
	if(!watcher->start(path))
    {
        return UniquePtr<FileSystemWatcher>();
    }
	return watcher;
}


static void addWatch(FileSystemWatcherTask& task, const char* path, int root_length)
{
	if (!os::dirExists(path)) return;
	
    int wd = inotify_add_watch(task.fd, path, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE);
    task.watched.insert(wd, path + root_length);

    auto iter = os::createFileIterator(path, task.allocator);
    os::FileInfo info;
    while (os::getNextFile(iter, &info))
    {
        if (!info.is_directory) continue;
		if (Lumix::equalStrings(info.filename, ".")) continue;
		if (Lumix::equalStrings(info.filename, "..")) continue;

        Lumix::StaticString<LUMIX_MAX_PATH> tmp(path, info.filename, "/");
        addWatch(task, tmp, root_length);
    }
    os::destroyFileIterator(iter);
}


static void getName(FileSystemWatcherTask& task, inotify_event* event, char* out, int max_size)
{
    auto iter = task.watched.find(event->wd);

    if (iter == task.watched.end())
    {
        Lumix::copyString(Span(out, max_size), event->name);
        return;
    }

    Lumix::copyString(Span(out, max_size), iter.value());
    Lumix::catString(Span(out, max_size), event->name);
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
        if (finished) return 0;
        auto* event = (inotify_event*)buf;

        while ((char*)event < buf + r)
        {
            char tmp[LUMIX_MAX_PATH];
            getName(*this, event, tmp, Lumix::lengthOf(tmp));
            if (event->mask & IN_CREATE) addWatch(*this, tmp, root_length);
            watcher.callback.invoke(tmp);

            event = (inotify_event*)((char*)event + sizeof(*event) + event->len);
        }
    }

    return close(fd) != -1;
}


} // namespace Lumix