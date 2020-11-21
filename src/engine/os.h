#pragma once


#include "lumix.h"
#include "stream.h"
#ifdef __linux__
	#include <pthread.h>
#endif


namespace Lumix
{

struct IAllocator;

namespace OS
{

#ifdef _WIN32
	using ThreadID = u32;
#else
	using ThreadID = pthread_t;
#endif

enum class Keycode : u8;

enum class CursorType : u32 {
	DEFAULT,
	SIZE_NS,
	SIZE_WE,
	SIZE_NWSE,
	LOAD,
	TEXT_INPUT,

	UNDEFINED
};

enum class ExecuteOpenResult : int
{
	SUCCESS,
	NO_ASSOCIATION,
	OTHER_ERROR
};

enum class MouseButton : int 
{
    LEFT = 0,
    RIGHT = 1,
    MIDDLE = 2,
    EXTENDED = 3, // 3 and higher

	MAX = 16
};

struct Point { int x, y; };
struct Rect { int left, top, width, height; };

using WindowHandle = void*;
constexpr WindowHandle INVALID_WINDOW = nullptr;

struct Event {
    enum class Type {
        QUIT,
        KEY,
        CHAR,
        MOUSE_BUTTON,
		MOUSE_MOVE,
        MOUSE_WHEEL,
        WINDOW_CLOSE,
        WINDOW_SIZE,
        WINDOW_MOVE,
        DROP_FILE,
		FOCUS
    };

    Type type;
    WindowHandle window;
    union {
        struct { u32 utf8; } text_input;
        struct { int w, h; } win_size;
        struct { int x, y; } win_move;
		struct { bool down; MouseButton button; } mouse_button;
		struct { int xrel, yrel; } mouse_move;
        struct { bool down; Keycode keycode; } key;
        struct { void* handle; } file_drop;
        struct { float amount; } mouse_wheel;
		struct { bool gained; } focus;
    };
};


struct InitWindowArgs
{
	enum Flags {
		NO_DECORATION = 1 << 0,
		NO_TASKBAR_ICON = 1 << 1
	};
	const char* name = ""; 
	bool handle_file_drops = false;
	bool fullscreen = false;
	u32 flags = 0;
	WindowHandle parent = INVALID_WINDOW;
};


struct LUMIX_ENGINE_API Interface
{
	virtual ~Interface() {}
    virtual void onEvent(const Event& event) = 0;
    virtual void onInit() = 0;
    virtual void onIdle() = 0;
};

struct Monitor {
	Rect work_rect;
	Rect monitor_rect;
	bool primary;
};
	
struct LUMIX_ENGINE_API InputFile final : IInputStream
{
public:
	InputFile();
	~InputFile();

	[[nodiscard]] bool open(const char* path);
	void close();
	
	using IInputStream::read;
	[[nodiscard]] bool read(void* data, u64 size) override;
	const void* getBuffer() const override { return nullptr; }

	u64 size() const override;
	u64 pos();

	[[nodiscard]] bool seek(u64 pos);
	
private:
	void* m_handle;
};
	

struct LUMIX_ENGINE_API OutputFile final : IOutputStream
{
public:
	OutputFile();
	~OutputFile();

	[[nodiscard]] bool open(const char* path);
	void close();
	void flush();
    bool isError() const { return m_is_error; }

	using IOutputStream::write;
	[[nodiscard]] bool write(const void* data, u64 size) override;

private:
	void* m_handle;
    bool m_is_error;
};
	

struct FileInfo {
	bool is_directory;
	char filename[MAX_PATH_LENGTH];
};

struct FileIterator;

struct WindowState {
	u64 style;
	Rect rect;
};

LUMIX_ENGINE_API void init();
LUMIX_ENGINE_API void logVersion();
LUMIX_ENGINE_API u32 getCPUsCount();
LUMIX_ENGINE_API void sleep(u32 milliseconds);
LUMIX_ENGINE_API ThreadID getCurrentThreadID();

LUMIX_ENGINE_API void* memReserve(size_t size);
LUMIX_ENGINE_API void memCommit(void* ptr, size_t size);
LUMIX_ENGINE_API void memRelease(void* ptr);
LUMIX_ENGINE_API u32 getMemPageSize();

LUMIX_ENGINE_API FileIterator* createFileIterator(const char* path, IAllocator& allocator);
LUMIX_ENGINE_API void destroyFileIterator(FileIterator* iterator);
LUMIX_ENGINE_API bool getNextFile(FileIterator* iterator, FileInfo* info);

LUMIX_ENGINE_API void setCurrentDirectory(const char* path);
LUMIX_ENGINE_API void getCurrentDirectory(Span<char> path);
LUMIX_ENGINE_API [[nodiscard]] bool getOpenFilename(Span<char> out, const char* filter, const char* starting_file);
LUMIX_ENGINE_API [[nodiscard]] bool getSaveFilename(Span<char> out, const char* filter, const char* default_extension);
LUMIX_ENGINE_API [[nodiscard]] bool getOpenDirectory(Span<char> out, const char* starting_dir);
LUMIX_ENGINE_API ExecuteOpenResult shellExecuteOpen(const char* path);
LUMIX_ENGINE_API ExecuteOpenResult openExplorer(const char* path);
LUMIX_ENGINE_API void copyToClipboard(const char* text);

LUMIX_ENGINE_API bool deleteFile(const char* path);
LUMIX_ENGINE_API [[nodiscard]] bool moveFile(const char* from, const char* to);
LUMIX_ENGINE_API size_t getFileSize(const char* path);
LUMIX_ENGINE_API bool fileExists(const char* path);
LUMIX_ENGINE_API bool dirExists(const char* path);
LUMIX_ENGINE_API u64 getLastModified(const char* file);
LUMIX_ENGINE_API [[nodiscard]] bool makePath(const char* path);

LUMIX_ENGINE_API void setCursor(CursorType type);
LUMIX_ENGINE_API void clipCursor(int screen_x, int screen_y, int w, int h);
LUMIX_ENGINE_API void unclipCursor();

LUMIX_ENGINE_API void quit();

LUMIX_ENGINE_API void getDropFile(const Event& event, int idx, Span<char> out);
LUMIX_ENGINE_API int getDropFileCount(const Event& event);
LUMIX_ENGINE_API void finishDrag(const Event& event);

LUMIX_ENGINE_API Point getMouseScreenPos();
LUMIX_ENGINE_API void setMouseScreenPos(int x, int y);
LUMIX_ENGINE_API void showCursor(bool show);

LUMIX_ENGINE_API u32 getMonitors(Span<Monitor> monitors);
LUMIX_ENGINE_API Point toScreen(WindowHandle win, int x, int y);
LUMIX_ENGINE_API WindowHandle createWindow(const InitWindowArgs& args);
LUMIX_ENGINE_API void destroyWindow(WindowHandle wnd);
LUMIX_ENGINE_API Rect getWindowScreenRect(WindowHandle win);
LUMIX_ENGINE_API Rect getWindowClientRect(WindowHandle win);
LUMIX_ENGINE_API void setWindowScreenRect(WindowHandle win, const Rect& rect);
LUMIX_ENGINE_API void setWindowTitle(WindowHandle win, const char* title);
LUMIX_ENGINE_API void maximizeWindow(WindowHandle win);
LUMIX_ENGINE_API WindowState setFullscreen(WindowHandle win);
LUMIX_ENGINE_API void restore(WindowHandle win, WindowState state);
LUMIX_ENGINE_API bool isMaximized(WindowHandle win);
LUMIX_ENGINE_API WindowHandle getFocused();

LUMIX_ENGINE_API bool isKeyDown(Keycode keycode);
LUMIX_ENGINE_API void getKeyName(Keycode keycode, Span<char> out);
LUMIX_ENGINE_API int getDPI();

LUMIX_ENGINE_API [[nodiscard]] bool copyFile(const char* from, const char* to);
LUMIX_ENGINE_API void getExecutablePath(Span<char> path);
LUMIX_ENGINE_API void messageBox(const char* text);
LUMIX_ENGINE_API void setCommandLine(int, char**);
LUMIX_ENGINE_API bool getCommandLine(Span<char> output);
LUMIX_ENGINE_API void* loadLibrary(const char* path);
LUMIX_ENGINE_API void unloadLibrary(void* handle);
LUMIX_ENGINE_API void* getLibrarySymbol(void* handle, const char* name);

LUMIX_ENGINE_API void run(Interface& iface);

enum class Keycode : u8
{
	LBUTTON = 0x01,
	RBUTTON = 0x02,
	CANCEL = 0x03,
	MBUTTON = 0x04,
	BACKSPACE = 0x08,
	TAB = 0x09,
	CLEAR = 0x0C,
	RETURN = 0x0D,
	SHIFT = 0x10,
	CTRL = 0x11,
	MENU = 0x12,
	PAUSE = 0x13,
	CAPITAL = 0x14,
	KANA = 0x15,
	HANGEUL = 0x15,
	HANGUL = 0x15,
	JUNJA = 0x17,
	FINAL = 0x18,
	HANJA = 0x19,
	KANJI = 0x19,
	ESCAPE = 0x1B,
	CONVERT = 0x1C,
	NONCONVERT = 0x1D,
	ACCEPT = 0x1E,
	MODECHANGE = 0x1F,
	SPACE = 0x20,
	PAGEUP = 0x21,
	PAGEDOWN = 0x22,
	END = 0x23,
	HOME = 0x24,
	LEFT = 0x25,
	UP = 0x26,
	RIGHT = 0x27,
	DOWN = 0x28,
	SELECT = 0x29,
	PRINT = 0x2A,
	EXECUTE = 0x2B,
	SNAPSHOT = 0x2C,
	INSERT = 0x2D,
	DEL = 0x2E,
	HELP = 0x2F,
	LWIN = 0x5B,
	RWIN = 0x5C,
	APPS = 0x5D,
	SLEEP = 0x5F,
	NUMPAD0 = 0x60,
	NUMPAD1 = 0x61,
	NUMPAD2 = 0x62,
	NUMPAD3 = 0x63,
	NUMPAD4 = 0x64,
	NUMPAD5 = 0x65,
	NUMPAD6 = 0x66,
	NUMPAD7 = 0x67,
	NUMPAD8 = 0x68,
	NUMPAD9 = 0x69,
	MULTIPLY = 0x6A,
	ADD = 0x6B,
	SEPARATOR = 0x6C,
	SUBTRACT = 0x6D,
	DECIMAL = 0x6E,
	DIVIDE = 0x6F,
	F1 = 0x70,
	F2 = 0x71,
	F3 = 0x72,
	F4 = 0x73,
	F5 = 0x74,
	F6 = 0x75,
	F7 = 0x76,
	F8 = 0x77,
	F9 = 0x78,
	F10 = 0x79,
	F11 = 0x7A,
	F12 = 0x7B,
	F13 = 0x7C,
	F14 = 0x7D,
	F15 = 0x7E,
	F16 = 0x7F,
	F17 = 0x80,
	F18 = 0x81,
	F19 = 0x82,
	F20 = 0x83,
	F21 = 0x84,
	F22 = 0x85,
	F23 = 0x86,
	F24 = 0x87,
	NUMLOCK = 0x90,
	SCROLL = 0x91,
	OEM_NEC_EQUAL = 0x92,
	OEM_FJ_JISHO = 0x92,
	OEM_FJ_MASSHOU = 0x93,
	OEM_FJ_TOUROKU = 0x94,
	OEM_FJ_LOYA = 0x95,
	OEM_FJ_ROYA = 0x96,
	LSHIFT = 0xA0,
	RSHIFT = 0xA1,
	LCTRL = 0xA2,
	RCTRL = 0xA3,
	LMENU = 0xA4,
	RMENU = 0xA5,
	BROWSER_BACK = 0xA6,
	BROWSER_FORWARD = 0xA7,
	BROWSER_REFRESH = 0xA8,
	BROWSER_STOP = 0xA9,
	BROWSER_SEARCH = 0xAA,
	BROWSER_FAVORITES = 0xAB,
	BROWSER_HOME = 0xAC,
	VOLUME_MUTE = 0xAD,
	VOLUME_DOWN = 0xAE,
	VOLUME_UP = 0xAF,
	MEDIA_NEXT_TRACK = 0xB0,
	MEDIA_PREV_TRACK = 0xB1,
	MEDIA_STOP = 0xB2,
	MEDIA_PLAY_PAUSE = 0xB3,
	LAUNCH_MAIL = 0xB4,
	LAUNCH_MEDIA_SELECT = 0xB5,
	LAUNCH_APP1 = 0xB6,
	LAUNCH_APP2 = 0xB7,
	OEM_1 = 0xBA,
	OEM_PLUS = 0xBB,
	OEM_COMMA = 0xBC,
	OEM_MINUS = 0xBD,
	OEM_PERIOD = 0xBE,
	OEM_2 = 0xBF,
	OEM_3 = 0xC0,
	OEM_4 = 0xDB,
	OEM_5 = 0xDC,
	OEM_6 = 0xDD,
	OEM_7 = 0xDE,
	OEM_8 = 0xDF,
	OEM_AX = 0xE1,
	OEM_102 = 0xE2,
	ICO_HELP = 0xE3,
	ICO_00 = 0xE4,
	PROCESSKEY = 0xE5,
	ICO_CLEAR = 0xE6,
	PACKET = 0xE7,
	OEM_RESET = 0xE9,
	OEM_JUMP = 0xEA,
	OEM_PA1 = 0xEB,
	OEM_PA2 = 0xEC,
	OEM_PA3 = 0xED,
	OEM_WSCTRL = 0xEE,
	OEM_CUSEL = 0xEF,
	OEM_ATTN = 0xF0,
	OEM_FINISH = 0xF1,
	OEM_COPY = 0xF2,
	OEM_AUTO = 0xF3,
	OEM_ENLW = 0xF4,
	OEM_BACKTAB = 0xF5,
	ATTN = 0xF6,
	CRSEL = 0xF7,
	EXSEL = 0xF8,
	EREOF = 0xF9,
	PLAY = 0xFA,
	ZOOM = 0xFB,
	NONAME = 0xFC,
	PA1 = 0xFD,
	OEM_CLEAR = 0xFE,

	A = 'A',
	C = 'C',
	D = 'D',
	E = 'E',
	K = 'K',
	S = 'S',
	V = 'V',
	X = 'X',
	Y = 'Y',
	Z = 'Z',

	INVALID = 0,
	MAX = 0xff
};
	

struct LUMIX_ENGINE_API Timer
{
	Timer();

	float tick();
	float getTimeSinceStart();
	float getTimeSinceTick();

	static u64 getRawTimestamp();
	static u64 getFrequency();

	u64 frequency;
	u64 last_tick;
	u64 first_tick;
};


} // namespace OS

} // namespace Lumix

