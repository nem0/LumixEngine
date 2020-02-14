#include "engine/allocator.h"
#include "engine/log.h"
#include "engine/lumix.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/path_utils.h"
#include "engine/string.h"
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#define GLX_GLXEXT_LEGACY
#include <GL/glx.h>

namespace Lumix::OS
{


static struct
{
	bool finished = false;
	Interface* iface = nullptr;
	Point relative_mode_pos = {};
	bool relative_mouse = false;
	WindowHandle win = INVALID_WINDOW;

	int argc = 0;
	char** argv = nullptr;
	Display* display = nullptr; 
	IVec2 mouse_screen_pos = {};
	bool key_states[256] = {};
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

u32 getCPUsCount() { return sysconf(_SC_NPROCESSORS_ONLN); }
void sleep(u32 milliseconds) { if (milliseconds) usleep(useconds_t(milliseconds * 1000)); }
ThreadID getCurrentThreadID() { return pthread_self(); }

void logVersion() {
    struct utsname tmp;
	if (uname(&tmp) == 0) {
		logInfo("Engine") << "sysname: " << tmp.sysname;
		logInfo("Engine") << "nodename: " << tmp.nodename;
		logInfo("Engine") << "release: " << tmp.release;
		logInfo("Engine") << "version: " << tmp.version;
		logInfo("Engine") << "machine: " << tmp.machine;
	}
	else {
		logWarning("Engine") << "uname failed";
	}
}


void getDropFile(const Event& event, int idx, Span<char> out)
{
    ASSERT(false); // not supported, processEvents does not generate the drop event
}


int getDropFileCount(const Event& event)
{
    ASSERT(false); // not supported, processEvents does not generate the drop event
}


void finishDrag(const Event& event)
{
    ASSERT(false); // not supported, processEvents does not generate the drop event
}


static void processEvents()
{
	while (XPending(G.display) > 0) {
		XEvent xevent;
		XNextEvent(G.display, &xevent);
		
		if (XFilterEvent(&xevent, None)) continue;

		Event e;
		switch (xevent.type) {
			case KeyPress:
			case KeyRelease:
				// TODO set G.key_states
				ASSERT(false);
				break;
			case ButtonPress:
			case ButtonRelease:
				e.type = Event::Type::MOUSE_BUTTON;
				e.window = (WindowHandle)xevent.xbutton.window;
				switch (xevent.xbutton.button) {
					case Button1: e.mouse_button.button = MouseButton::LEFT; break;
					case Button2: e.mouse_button.button = MouseButton::MIDDLE; break;
					case Button3: e.mouse_button.button = MouseButton::RIGHT; break;
					default: e.mouse_button.button = MouseButton::EXTENDED; break;
				}
				e.mouse_button.down = xevent.type == ButtonPress;
				G.iface->onEvent(e);
				break;			
			case ConfigureNotify:
				e.window = (WindowHandle)xevent.xconfigure.window;

				e.type = Event::Type::WINDOW_SIZE;
				e.win_size.w = xevent.xconfigure.width;
				e.win_size.h = xevent.xconfigure.height;
				G.iface->onEvent(e);

				e.type = Event::Type::WINDOW_MOVE;
				e.win_move.x = xevent.xconfigure.x;
				e.win_move.y = xevent.xconfigure.y;
				G.iface->onEvent(e);
				break;
			case MotionNotify:
				const IVec2 mp(xevent.xmotion.x, xevent.xmotion.y);
				const IVec2 rel = mp - G.mouse_screen_pos;
				G.mouse_screen_pos = mp;
				e.window = (WindowHandle)xevent.xmotion.window;
				e.type = Event::Type::MOUSE_MOVE;
				e.mouse_move.xrel = rel.x;
				e.mouse_move.yrel = rel.y;
				G.iface->onEvent(e);
				break;
		}
	}

}


void destroyWindow(WindowHandle window)
{
	XUnmapWindow(G.display, (Window)window);
	XDestroyWindow(G.display, (Window)window);
}


Point toScreen(WindowHandle win, int x, int y)
{
	XWindowAttributes attrs;
	XGetWindowAttributes(G.display, (Window)win, &attrs);
    return {x + attrs.x, y + attrs.y};
}

WindowHandle createWindow(const InitWindowArgs& args)
{
	ASSERT(G.display);
	
	Display* display = G.display;
	static i32 screen = DefaultScreen(display);
	static i32 depth = DefaultDepth(display, screen);
	static Window root = RootWindow(display, screen);
	static Visual* visual = DefaultVisual(display, screen);
	static XSetWindowAttributes attrs = [](){
		XSetWindowAttributes ret = {};
		ret.background_pixmap = 0;
		ret.border_pixel = 0;
		ret.event_mask = ButtonPressMask
			| ButtonReleaseMask
			| ExposureMask
			| KeyPressMask
			| KeyReleaseMask
			| PointerMotionMask
			| StructureNotifyMask;
		return ret;
	}();
	Window win = XCreateWindow(display
		, args.parent ? (Window)args.parent : root
		, 0
		, 0
		, 800
		, 600
		, 0
		, depth
		, InputOutput
		, visual
		, CWBorderPixel | CWEventMask
		, &attrs);
	XSetWindowAttributes attr = {};
	XChangeWindowAttributes(display, win, CWBackPixel, &attr);

	XMapWindow(display, win);
	XStoreName(display, win, args.name && args.name[0] ? args.name : "Lumix App");

	// TODO glx context fails to create without this
	for (int i = 0; i < 100; ++i) { processEvents(); }

    WindowHandle res = (WindowHandle)win;
	return res;
}


void quit()
{
	G.finished = true;
}


bool isKeyDown(Keycode keycode)
{
	return G.key_states[(u8)keycode];
}


void getKeyName(Keycode keycode, Span<char> out)
{
	ASSERT(false);
    // TODO
}


void showCursor(bool show)
{
	//ASSERT(false);
    // TODO
}


void setWindowTitle(WindowHandle win, const char* title)
{
	XStoreName(G.display, (Window)win, title);
}


Rect getWindowScreenRect(WindowHandle win)
{
	XWindowAttributes attrs;
	XGetWindowAttributes(G.display, (Window)win, &attrs);
	Rect r;
	r.left = attrs.x;
	r.top = attrs.y;
	r.width = attrs.width;
	r.height = attrs.height;
	XGetWindowAttributes(G.display, attrs.root, &attrs);
	r.left += attrs.x;
	r.top += attrs.y;
    return r;
}

Rect getWindowClientRect(WindowHandle win)
{
	XWindowAttributes attrs;
	XGetWindowAttributes(G.display, (Window)win, &attrs);
	Rect r;
	r.left = 0;
	r.top = 0;
	r.width = attrs.width;
	r.height = attrs.height;
    return r;
}

void setWindowScreenRect(WindowHandle win, const Rect& rect)
{
	XMoveResizeWindow(G.display, (Window)win, rect.left, rect.top, rect.width, rect.height);
}

u32 getMonitors(Span<Monitor> monitors)
{
	//ASSERT(false);
    // TODO
    return {};
}

void setMouseScreenPos(int x, int y)
{
	//ASSERT(false);
    // TODO
}

Point getMousePos(WindowHandle win)
{
	const Rect r = getWindowScreenRect(win);
	const Point mp = getMouseScreenPos();
	return {
		mp.x - r.left,
		mp.y - r.top
	};
}

Point getMouseScreenPos()
{
	const int screen_count = ScreenCount(G.display);
	for (int screen = 0; screen < screen_count; ++screen) {
		Window root, child;
		int root_x, root_y, win_x, win_y;
		unsigned mask;
		if (XQueryPointer(G.display, RootWindow(G.display, screen), &root, &child, &root_x, &root_y, &win_x, &win_y, &mask)) {
			XWindowAttributes attrs;
			XGetWindowAttributes(G.display, root, &attrs);
			return {attrs.x + root_x, attrs.y + root_y};
		}
	}

	return {0, 0};
}


WindowHandle getFocused()
{
	Window win;
	int dummy;
	XGetInputFocus(G.display, &win, &dummy);
	return (WindowHandle)win;
}

bool isMaximized(WindowHandle win) {
	//ASSERT(false);
    // TODO
    return false;
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
	XInitThreads();
	G.display = XOpenDisplay(nullptr);
	XIM im = XOpenIM(G.display, nullptr, nullptr, nullptr);

	G.iface = &iface;
	G.iface->onInit();
	while (!G.finished) {
		processEvents();
		G.iface->onIdle();
	}

	XCloseIM(im);
	XCloseDisplay(G.display);
}


int getDPI()
{
	//ASSERT(false);
    // TODO
    return 96;
}

u32 getMemPageSize() {
	const u32 sz = sysconf(_SC_PAGESIZE);
	return sz;
}

void* memReserve(size_t size) {
	return malloc(size);
}

void memCommit(void* ptr, size_t size) {
	// noop on linux
}

void memRelease(void* ptr) {
	free(ptr);
}

struct FileIterator {};

FileIterator* createFileIterator(const char* path, IAllocator& allocator)
{
	return (FileIterator*)opendir(path);
}


void destroyFileIterator(FileIterator* iterator)
{
	closedir((DIR*)iterator);
}


bool getNextFile(FileIterator* iterator, FileInfo* info)
{
	if (!iterator) return false;

	auto* dir = (DIR*)iterator;
	auto* dir_ent = readdir(dir);
	if (!dir_ent) return false;

	info->is_directory = dir_ent->d_type == DT_DIR;
	Lumix::copyString(info->filename, dir_ent->d_name);
	return true;
}


void setCurrentDirectory(const char* path)
{
	auto res = chdir(path);
	(void)res;
}


void getCurrentDirectory(Span<char> output)
{
	if (!getcwd(output.m_begin, output.length())) {
		output[0] = 0;
	}
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
	return system(path) == 0 ? ExecuteOpenResult::SUCCESS : ExecuteOpenResult::OTHER_ERROR;
}


ExecuteOpenResult openExplorer(const char* path)
{
		ASSERT(false);
    // TODO
    return {};

}


bool deleteFile(const char* path)
{
	return unlink(path) == 0;
}


bool moveFile(const char* from, const char* to)
{
	return rename(from, to) == 0;
}


size_t getFileSize(const char* path)
{
	struct stat tmp;
	stat(path, &tmp);
	return tmp.st_size;
}


bool fileExists(const char* path)
{
	struct stat tmp;
	return ((stat(path, &tmp) == 0) && (((tmp.st_mode) & S_IFMT) != S_IFDIR));
}


bool dirExists(const char* path)
{
	struct stat tmp;
	return ((stat(path, &tmp) == 0) && (((tmp.st_mode) & S_IFMT) == S_IFDIR));
}


u64 getLastModified(const char* path)
{
	struct stat tmp;
	Lumix::u64 ret = 0;
	ret = tmp.st_mtim.tv_sec * 1000 + Lumix::u64(tmp.st_mtim.tv_nsec / 1000000);
	return ret;
}


bool makePath(const char* path)
{
	return mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0;
}


void clipCursor(int x, int y, int w, int h)
{
	//ASSERT(false);
    // TODO
}


void unclipCursor()
{
	//ASSERT(false);
    // TODO
}


bool copyFile(const char* from, const char* to)
{
    const int source = open(from, O_RDONLY, 0);
	if (source < 0) return false;
    const int dest = open(to, O_WRONLY | O_CREAT, 0644);
	if (dest < 1) {
		close(source);
		return false;
	}

	char buf[BUFSIZ];
    size_t size;
    while ((size = read(source, buf, BUFSIZ)) > 0) {
        write(dest, buf, size);
    }

    close(source);
    close(dest);
}


void getExecutablePath(Span<char> buffer)
{
	ASSERT(false);
    // TODO
    ;
}


void messageBox(const char* text)
{
	fprintf(stderr, "%s", text);
}

	
void setCommandLine(int argc, char** argv)
{
	G.argc = argc;
	G.argv = argv;
}
	

bool getCommandLine(Span<char> output)
{
	copyString(output, "");
	for (int i = 0; i < G.argc; ++i) {
		catString(output, G.argv[i]);
		catString(output, " ");
	}
    return true;
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
	return float(double(getRawTimestamp() - first_tick) / double(getFrequency()));
}


float Timer::getTimeSinceTick()
{
	return float(double(getRawTimestamp() - last_tick) / double(getFrequency()));
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
