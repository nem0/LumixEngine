#include "engine/allocator.h"
#include "engine/log.h"
#include "engine/lumix.h"
#include "engine/os.h"
#include "engine/path_utils.h"
#include "engine/string.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace Lumix::OS
{


static struct
{
	bool finished = false;
	Interface* iface = nullptr;
	Point relative_mode_pos = {};
	bool relative_mouse = false;
	WindowHandle win = INVALID_WINDOW;
} G;


InputFile::InputFile()
{
	m_handle = nullptr;
	static_assert(sizeof(m_handle) >= sizeof(FILE*), "");
}


OutputFile::OutputFile()
{
    m_is_error = false;
	m_handle = nullptr;
	static_assert(sizeof(m_handle) >= sizeof(FILE*), "");
}


InputFile::~InputFile()
{
	ASSERT(!m_handle);
}


OutputFile::~OutputFile()
{
	ASSERT(!m_handle);
}


bool OutputFile::open(const char* path)
{
    m_handle = fopen(path, "wb");
	m_is_error = !m_handle;
    return !m_is_error;
}


bool InputFile::open(const char* path)
{
	m_handle = fopen(path, "rb");
	return m_handle;
}


void OutputFile::flush()
{
	ASSERT(m_handle);
	fflush((FILE*)m_handle);
}


void OutputFile::close()
{
	if (m_handle) {
		fclose((FILE*)m_handle);
		m_handle = nullptr;
	}
}


void InputFile::close()
{
	if (m_handle) {
		fclose((FILE*)m_handle);
		m_handle = nullptr;
	}
}


bool OutputFile::write(const void* data, u64 size)
{
    ASSERT(m_handle);
    const size_t written = fwrite(data, size, 1, (FILE*)m_handle);
    return written == 1;
}

bool InputFile::read(void* data, u64 size)
{
	ASSERT(nullptr != m_handle);
	size_t read = fread(data, size, 1, (FILE*)m_handle);
	return read == 1;
}

u64 InputFile::size() const
{
	ASSERT(nullptr != m_handle);
	long pos = ftell((FILE*)m_handle);
	fseek((FILE*)m_handle, 0, SEEK_END);
	size_t size = (size_t)ftell((FILE*)m_handle);
	fseek((FILE*)m_handle, pos, SEEK_SET);
	return size;
}


u64 InputFile::pos()
{
	ASSERT(nullptr != m_handle);
	long pos = ftell((FILE*)m_handle);
	return (size_t)pos;
}


bool InputFile::seek(u64 pos)
{
	ASSERT(nullptr != m_handle);
	return fseek((FILE*)m_handle, pos, SEEK_SET) == 0;
}


OutputFile& OutputFile::operator <<(const char* text)
{
	write(text, stringLength(text));
	return *this;
}


OutputFile& OutputFile::operator <<(i32 value)
{
	char buf[20];
	toCString(value, Span(buf));
	write(buf, stringLength(buf));
	return *this;
}


OutputFile& OutputFile::operator <<(u32 value)
{
	char buf[20];
	toCString(value, Span(buf));
	write(buf, stringLength(buf));
	return *this;
}


OutputFile& OutputFile::operator <<(u64 value)
{
	char buf[30];
	toCString(value, Span(buf));
	write(buf, stringLength(buf));
	return *this;
}


OutputFile& OutputFile::operator <<(float value)
{
	char buf[128];
	toCString(value, Span(buf), 7);
	write(buf, stringLength(buf));
	return *this;
}


void logVersion() {
    ASSERT(false);
    // TODO
}


void getDropFile(const Event& event, int idx, Span<char> out)
{
    ASSERT(false);
    // TODO
}


int getDropFileCount(const Event& event)
{
    ASSERT(false);
    // TODO
}


void finishDrag(const Event& event)
{
    ASSERT(false);
    // TODO
}


static void processEvents()
{
	ASSERT(false);
    // TODO
}


void destroyWindow(WindowHandle window)
{
	ASSERT(false);
    // TODO
}


Point toScreen(WindowHandle win, int x, int y)
{
	ASSERT(false);
    // TODO
    return {};
}

WindowHandle createWindow(const InitWindowArgs& args)
{
	ASSERT(false);
    // TODO
    return {};
}


void quit()
{
	G.finished = true;
}


bool isKeyDown(Keycode keycode)
{
	ASSERT(false);
    // TODO
    return {};
}


void getKeyName(Keycode keycode, Span<char> out)
{
	ASSERT(false);
    // TODO
}


void showCursor(bool show)
{
	ASSERT(false);
    // TODO
}


void setWindowTitle(WindowHandle win, const char* title)
{
	ASSERT(false);
    // TODO
}


Rect getWindowScreenRect(WindowHandle win)
{
	ASSERT(false);
    // TODO
    return {};
}

Rect getWindowClientRect(WindowHandle win)
{
	ASSERT(false);
    // TODO
    return {};
}

void setWindowScreenRect(WindowHandle win, const Rect& rect)
{
	ASSERT(false);
    // TODO
}

u32 getMonitors(Span<Monitor> monitors)
{
	ASSERT(false);
    // TODO
    return {};
}

void setMouseScreenPos(int x, int y)
{
	ASSERT(false);
    // TODO
}

Point getMousePos(WindowHandle win)
{
	ASSERT(false);
    // TODO
    return {};
}

Point getMouseScreenPos()
{
	ASSERT(false);
    // TODO
    return {};
}


WindowHandle getFocused()
{
	ASSERT(false);
    // TODO
    return {};
}

bool isMaximized(WindowHandle win) {
	ASSERT(false);
    // TODO
    return {};
}

void restore(WindowHandle win, WindowState state) {
	ASSERT(false);
    // TODO
}

WindowState setFullscreen(WindowHandle win) {
	ASSERT(false);
    // TODO
    return {};
}

void maximizeWindow(WindowHandle win)
{
	ASSERT(false);
    // TODO
}


bool isRelativeMouseMode()
{
	return G.relative_mouse;
}


void run(Interface& iface)
{
	G.iface = &iface;
	G.iface->onInit();
	while (!G.finished)
	{
		processEvents();
		G.iface->onIdle();
	}
}


int getDPI()
{
	ASSERT(false);
    // TODO
    return {};
}

u32 getMemPageSize() {
	ASSERT(false);
    // TODO
    return {};
}

void* memReserve(size_t size) {
	ASSERT(false);
    // TODO
    return {};
}

void memCommit(void* ptr, size_t size) {
	ASSERT(false);
    // TODO
}

void memRelease(void* ptr) {
	ASSERT(false);
    // TODO
}

struct FileIterator
{
	//HANDLE handle;
	IAllocator* allocator;
	//WIN32_FIND_DATA ffd;
	bool is_valid;
};


FileIterator* createFileIterator(const char* path, IAllocator& allocator)
{
		ASSERT(false);
    // TODO
    return {};

}


void destroyFileIterator(FileIterator* iterator)
{
		ASSERT(false);
    // TODO
}


bool getNextFile(FileIterator* iterator, FileInfo* info)
{
		ASSERT(false);
    // TODO
    return {};

}


void setCurrentDirectory(const char* path)
{
		ASSERT(false);
    // TODO
}


void getCurrentDirectory(Span<char> output)
{
		ASSERT(false);
    // TODO
}


bool getSaveFilename(Span<char> out, const char* filter, const char* default_extension)
{
		ASSERT(false);
    // TODO
    return {};

}


bool getOpenFilename(Span<char> out, const char* filter, const char* starting_file)
{
		ASSERT(false);
    // TODO
    return {};

}


bool getOpenDirectory(Span<char> output, const char* starting_dir)
{
		ASSERT(false);
    // TODO
    return {};

}


void copyToClipboard(const char* text)
{
		ASSERT(false);
    // TODO
}


ExecuteOpenResult shellExecuteOpen(const char* path)
{
		ASSERT(false);
    // TODO
    return {};

}


ExecuteOpenResult openExplorer(const char* path)
{
		ASSERT(false);
    // TODO
    return {};

}


bool deleteFile(const char* path)
{
	ASSERT(false);
    // TODO
    return {};
}


bool moveFile(const char* from, const char* to)
{
	ASSERT(false);
    // TODO
    return {};
}


size_t getFileSize(const char* path)
{
	ASSERT(false);
    // TODO
    return {};
}


bool fileExists(const char* path)
{
	ASSERT(false);
    // TODO
    return {};
}


bool dirExists(const char* path)
{
	ASSERT(false);
    // TODO
    return {};
}


u64 getLastModified(const char* path)
{
	ASSERT(false);
    // TODO
    return {};

}


bool makePath(const char* path)
{
	ASSERT(false);
    // TODO
    return {};

}


void clipCursor(int x, int y, int w, int h)
{
	ASSERT(false);
    // TODO
}


void unclipCursor()
{
	ASSERT(false);
    // TODO
}


bool copyFile(const char* from, const char* to)
{
	ASSERT(false);
    // TODO
    return {};

}


void getExecutablePath(Span<char> buffer)
{
	ASSERT(false);
    // TODO
    ;
}


void messageBox(const char* text)
{
	ASSERT(false);
    // TODO
    ;
}

	
void setCommandLine(int, char**)
{
	ASSERT(false);
}
	

bool getCommandLine(Span<char> output)
{
	ASSERT(false);
    // TODO
    return {};
}


void* loadLibrary(const char* path)
{
	return dlopen(path, RTLD_LOCAL | RTLD_LAZY);
}


void unloadLibrary(void* handle)
{
	dlclose(handle);
}


void* getLibrarySymbol(void* handle, const char* name)
{
	return dlsym(handle, name);
}

Timer::Timer()
{
	last_tick = getRawTimestamp();
	first_tick = last_tick;
}


float Timer::getTimeSinceStart()
{
	return getRawTimestamp() - first_tick;
}


float Timer::getTimeSinceTick()
{
	return getRawTimestamp() - last_tick;
}


float Timer::tick()
{
	const u64 now = getRawTimestamp();
	const float delta = float(double(now - last_tick) / double(getFrequency()));
	last_tick = now;
	return delta;
}


u64 Timer::getFrequency()
{
	return 1'000'000'000;
}


u64 Timer::getRawTimestamp()
{
	timespec tick;
	clock_gettime(CLOCK_REALTIME, &tick);
	return u64(tick.tv_sec) * 1000000000 + u64(tick.tv_nsec);
}


} // namespace Lumix::OS
