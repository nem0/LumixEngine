#include "engine/allocator.h"
#include "engine/allocators.h"
#include "engine/atomic.h"
#include "engine/log.h"
#include "engine/lumix.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/string.h"
#define UNICODE
#pragma warning(push)
#pragma warning(disable : 4091)
#include <windowsx.h>
#include <Shobjidl_core.h>
#include <shlobj_core.h>
#include <Psapi.h>
#include <sysinfoapi.h>
#pragma warning(pop)
#pragma warning(disable : 4996)

#pragma comment(lib, "Ws2_32.lib")

//Request high performace profiles from mobile chipsets
extern "C" {
	LUMIX_LIBRARY_EXPORT DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
	LUMIX_LIBRARY_EXPORT DWORD NvOptimusEnablement = 0x00000001;
}

// some winapi calls can fail but we don't have any known way to "fix" the issue
// some of these calls are not fatal (e.g. if we fail to move a window), so we just assert -> DEBUG_CHECK
#define DEBUG_CHECK(R) if (!(R)) ASSERT(false)
// some may end up corrupting user data (e.g. not having enought space for path string and using such shorter path), so we better abort
#define FATAL_CHECK(R) do { if (!(R)) abort(); } while(false)


namespace Lumix::os
{

struct EventQueue {
	struct Rec {
		Event e;
		Rec* prev;
		Rec* next = nullptr;
	};

	void pushBack(const Event& e) {
		Rec* n = LUMIX_NEW(getGlobalAllocator(), Rec);
		n->prev = back;
		if (back) back->next = n;
		back = n;
		if (!front) front = n;
		n->e = e;
	}

	Event popFront() {
		ASSERT(front);
		Event e = front->e;
		Rec* tmp = front;
		front = tmp->next;
		if (!front) back = nullptr;
		LUMIX_DELETE(getGlobalAllocator(), tmp);
		return e;
	}

	bool empty() const { 
		return !front;
	}

	Rec* front = nullptr;
	Rec* back = nullptr;
};

static struct {
	WindowHandle grabbed_window = INVALID_WINDOW;
	EventQueue event_queue;
	Point relative_mode_pos = {};
	bool relative_mouse = false;
	bool raw_input_registered = false;
	u16 surrogate = 0;
	bool key_states[256] = {};
	struct {
		HCURSOR load;
		HCURSOR size_ns;
		HCURSOR size_we;
		HCURSOR size_nwse;
		HCURSOR arrow;
		HCURSOR text_input;
	} cursors;
	CursorType current_cursor = CursorType::DEFAULT;
} G;


InputFile::InputFile()
{
	m_handle = (void*)INVALID_HANDLE_VALUE;
	static_assert(sizeof(m_handle) >= sizeof(HANDLE), "");
}


OutputFile::OutputFile()
{
    m_is_error = false;
	m_handle = (void*)INVALID_HANDLE_VALUE;
	static_assert(sizeof(m_handle) >= sizeof(HANDLE), "");
}


InputFile::~InputFile()
{
	ASSERT((HANDLE)m_handle == INVALID_HANDLE_VALUE);
}


OutputFile::~OutputFile()
{
	ASSERT((HANDLE)m_handle == INVALID_HANDLE_VALUE);
}


bool OutputFile::open(const char* path)
{
	m_handle = (HANDLE)::CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	m_is_error = INVALID_HANDLE_VALUE == m_handle;
    return !m_is_error;
}


bool InputFile::open(const char* path)
{
	m_handle = (HANDLE)::CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	return INVALID_HANDLE_VALUE != m_handle;
}


void OutputFile::flush()
{
	ASSERT(nullptr != m_handle);
	FlushFileBuffers((HANDLE)m_handle);
}


void OutputFile::close()
{
	if (INVALID_HANDLE_VALUE != (HANDLE)m_handle)
	{
		::CloseHandle((HANDLE)m_handle);
		m_handle = (void*)INVALID_HANDLE_VALUE;
	}
}


void InputFile::close()
{
	if (INVALID_HANDLE_VALUE != (HANDLE)m_handle)
	{
		::CloseHandle((HANDLE)m_handle);
		m_handle = (void*)INVALID_HANDLE_VALUE;
	}
}


bool OutputFile::write(const void* data, u64 size)
{
	ASSERT(INVALID_HANDLE_VALUE != (HANDLE)m_handle);
	u64 written = 0;
	::WriteFile((HANDLE)m_handle, data, (DWORD)size, (LPDWORD)&written, nullptr);
	m_is_error = m_is_error || size != written;
    return !m_is_error;
}

bool InputFile::read(void* data, u64 size)
{
	ASSERT(INVALID_HANDLE_VALUE != m_handle);
	DWORD readed = 0;
	BOOL success = ::ReadFile((HANDLE)m_handle, data, (DWORD)size, (LPDWORD)&readed, nullptr);
	return success && size == readed;
}

u64 InputFile::size() const
{
	ASSERT(INVALID_HANDLE_VALUE != m_handle);
	return ::GetFileSize((HANDLE)m_handle, 0);
}

u64 InputFile::pos()
{
	ASSERT(INVALID_HANDLE_VALUE != m_handle);
	return ::SetFilePointer((HANDLE)m_handle, 0, nullptr, FILE_CURRENT);
}


bool InputFile::seek(u64 pos)
{
	ASSERT(INVALID_HANDLE_VALUE != m_handle);
	LARGE_INTEGER dist;
	dist.QuadPart = pos;
	return ::SetFilePointer((HANDLE)m_handle, dist.u.LowPart, &dist.u.HighPart, FILE_BEGIN) != INVALID_SET_FILE_POINTER;
}


static void fromWChar(Span<char> out, const WCHAR* in)
{
	const WCHAR* c = in;
	char* cout = out.begin();
	const u32 size = out.length();
	while (*c && c - in < size - 1)
	{
		*cout = (char)*c;
		++cout;
		++c;
	}
	FATAL_CHECK(!*c);
	*cout = 0;
}


template <int N> static void toWChar(WCHAR (&out)[N], StringView in) {
	const char* c = in.begin;
	WCHAR* cout = out;
	while (c != in.end && c - in.begin < N - 1) {
		*cout = *c;
		++cout;
		++c;
	}
	FATAL_CHECK(c == in.end);
	*cout = 0;
}


template <int N>
struct WCharStr {
	WCharStr(StringView rhs)
	{
		toWChar(data, rhs);
	}

	operator const WCHAR*() const
	{
		return data;
	}

	WCHAR data[N];
};

void sleep(u32 milliseconds) { ::Sleep(milliseconds); }

static_assert(sizeof(ThreadID) == sizeof(::GetCurrentThreadId()));
ThreadID getCurrentThreadID() { return ::GetCurrentThreadId(); }

u32 getCPUsCount() {
	SYSTEM_INFO sys_info;
	GetSystemInfo(&sys_info);

	u32 num = sys_info.dwNumberOfProcessors;
	num = num > 0 ? num : 1;

	return num;
}

void logInfo() {
	DWORD dwVersion = 0;
	DWORD dwMajorVersion = 0;
	DWORD dwMinorVersion = 0;
	DWORD dwBuild = 0;
	
	dwVersion = GetVersion();

	dwMajorVersion = (DWORD)(LOBYTE(LOWORD(dwVersion)));
	dwMinorVersion = (DWORD)(HIBYTE(LOWORD(dwVersion)));

	if (dwVersion < 0x80000000) dwBuild = (DWORD)(HIWORD(dwVersion));

	Lumix::logInfo("OS Version: ", u32(dwMajorVersion), ".", u32(dwMinorVersion), " (", u32(dwBuild), ")");

	SYSTEM_INFO sys_info;
	GetSystemInfo(&sys_info);
	Lumix::logInfo("Page size: ", u32(sys_info.dwPageSize));
	Lumix::logInfo("Number of processors: ", u32(sys_info.dwNumberOfProcessors));
	Lumix::logInfo("Allocation granularity: ", u32(sys_info.dwAllocationGranularity));
}


bool getDropFile(const Event& event, int idx, Span<char> out) {
	ASSERT(out.length() > 0);
	HDROP drop = (HDROP)event.file_drop.handle;
	WCHAR buffer[MAX_PATH];
	if (DragQueryFile(drop, idx, buffer, MAX_PATH)) {
		fromWChar(out, buffer);
		return true;
	}
	return false;
}


int getDropFileCount(const Event& event)
{
	HDROP drop = (HDROP)event.file_drop.handle;
	return (int)DragQueryFile(drop, 0xFFFFFFFF, NULL, 0);
}


void finishDrag(const Event& event)
{
	HDROP drop = (HDROP)event.file_drop.handle;
	DragFinish(drop);
}

static void UTF32ToUTF8(u32 utf32, char* utf8)
{
    if (utf32 <= 0x7F) {
        utf8[0] = (char) utf32;
    } else if (utf32 <= 0x7FF) {
        utf8[0] = 0xC0 | (char) ((utf32 >> 6) & 0x1F);
        utf8[1] = 0x80 | (char) (utf32 & 0x3F);
    } else if (utf32 <= 0xFFFF) {
        utf8[0] = 0xE0 | (char) ((utf32 >> 12) & 0x0F);
        utf8[1] = 0x80 | (char) ((utf32 >> 6) & 0x3F);
        utf8[2] = 0x80 | (char) (utf32 & 0x3F);
    } else if (utf32 <= 0x10FFFF) {
        utf8[0] = 0xF0 | (char) ((utf32 >> 18) & 0x0F);
        utf8[1] = 0x80 | (char) ((utf32 >> 12) & 0x3F);
        utf8[2] = 0x80 | (char) ((utf32 >> 6) & 0x3F);
        utf8[3] = 0x80 | (char) (utf32 & 0x3F);
	}
	else {
		ASSERT(false);
	}
}

bool getEvent(Event& event) {
	if (!G.event_queue.empty()) {
		event = G.event_queue.popFront();
		return true;
	}

	retry:
	MSG msg;
	if (!PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) return false;

	bool translate_msg = true;
	event.window = msg.hwnd;
	switch (msg.message) {
		case WM_DROPFILES:
			event.type = Event::Type::DROP_FILE;
			event.file_drop.handle = (HDROP)msg.wParam;
			break;
		case WM_QUIT: 
			event.type = Event::Type::QUIT; 
			break;
		case WM_CLOSE: 
			event.type = Event::Type::WINDOW_CLOSE; 
			break;
		case WM_SYSKEYDOWN:
			if (msg.wParam == VK_MENU) translate_msg = false;
			event.type = Event::Type::KEY;
			event.key.down = true;
			event.key.keycode = (Keycode)msg.wParam;
			event.key.is_repeat = msg.lParam & (1 << 30);
			G.key_states[(u32)event.key.keycode] = true;
			break;
		case WM_SYSCOMMAND:
			if (msg.wParam != SC_KEYMENU || (msg.lParam >> 16) > 0) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			goto retry;
		case WM_SYSKEYUP:
			event.type = Event::Type::KEY;
			event.key.down = false;
			event.key.keycode = (Keycode)msg.wParam;
			G.key_states[(u32)event.key.keycode] = false;
			break;
		case WM_KEYDOWN:
			event.type = Event::Type::KEY;
			event.key.down = true;
			event.key.keycode = (Keycode)msg.wParam;
			event.key.is_repeat = msg.lParam & (1 << 30);
			G.key_states[(u32)event.key.keycode] = true;
			break;
		case WM_KEYUP:
			event.type = Event::Type::KEY;
			event.key.down = false;
			event.key.keycode = (Keycode)msg.wParam;
			G.key_states[(u32)event.key.keycode] = false;
			break;
		case WM_CHAR: {
			event.type = Event::Type::CHAR;
			event.text_input.utf8 = 0;
			u32 c = (u32)msg.wParam;
			if (c >= 0xd800 && c <= 0xdbff) {
				G.surrogate = (u16)c;
				goto retry;
			}

			if (c >= 0xdc00 && c <= 0xdfff) {
				if (G.surrogate) {
					c = (G.surrogate - 0xd800) << 10;
					c += (WCHAR)msg.wParam - 0xdc00;
					c += 0x10000;
				}
			}
			G.surrogate = 0;

			UTF32ToUTF8(c, (char*)&event.text_input.utf8);
			break;
		}
		case WM_INPUT: {
			HRAWINPUT hRawInput = (HRAWINPUT)msg.lParam;
			UINT dataSize;
			GetRawInputData(hRawInput, RID_INPUT, NULL, &dataSize, sizeof(RAWINPUTHEADER));
			alignas(RAWINPUT) char dataBuf[1024];
			if (dataSize == 0 || dataSize > sizeof(dataBuf)) break;

			GetRawInputData(hRawInput, RID_INPUT, dataBuf, &dataSize, sizeof(RAWINPUTHEADER));

			const RAWINPUT* raw = (const RAWINPUT*)dataBuf;
			if (raw->header.dwType != RIM_TYPEMOUSE) break;

			const RAWMOUSE& mouseData = raw->data.mouse;
			const USHORT flags = mouseData.usButtonFlags;
			const short wheel_delta = (short)mouseData.usButtonData;
			const LONG x = mouseData.lLastX, y = mouseData.lLastY;

			Event e;
			if (wheel_delta) {
				e.mouse_wheel.amount = (float)wheel_delta / WHEEL_DELTA;
				e.type = Event::Type::MOUSE_WHEEL;
				G.event_queue.pushBack(e);
			}

			if(flags & RI_MOUSE_LEFT_BUTTON_DOWN) {
				e.type = Event::Type::MOUSE_BUTTON;
				e.mouse_button.button = MouseButton::LEFT;
				e.mouse_button.down = true;
				G.event_queue.pushBack(e);
			}
			if(flags & RI_MOUSE_LEFT_BUTTON_UP) {
				e.type = Event::Type::MOUSE_BUTTON;
				e.mouse_button.button = MouseButton::LEFT;
				e.mouse_button.down = false;
				G.event_queue.pushBack(e);
			}
					
			if(flags & RI_MOUSE_RIGHT_BUTTON_UP) {
				e.type = Event::Type::MOUSE_BUTTON;
				e.mouse_button.button = MouseButton::RIGHT;
				e.mouse_button.down = false;
				G.event_queue.pushBack(e);
			}
			if(flags & RI_MOUSE_RIGHT_BUTTON_DOWN) {
				e.type = Event::Type::MOUSE_BUTTON;
				e.mouse_button.button = MouseButton::RIGHT;
				e.mouse_button.down = true;
				G.event_queue.pushBack(e);
			}

			if(flags & RI_MOUSE_MIDDLE_BUTTON_UP) {
				e.type = Event::Type::MOUSE_BUTTON;
				e.mouse_button.button = MouseButton::MIDDLE;
				e.mouse_button.down = false;
				G.event_queue.pushBack(e);
			}
			if(flags & RI_MOUSE_MIDDLE_BUTTON_DOWN) {
				e.type = Event::Type::MOUSE_BUTTON;
				e.mouse_button.button = MouseButton::MIDDLE;
				e.mouse_button.down = true;
				G.event_queue.pushBack(e);
			}

			if(flags & RI_MOUSE_BUTTON_4_DOWN) {
				e.type = Event::Type::MOUSE_BUTTON;
				e.mouse_button.button = MouseButton::EXTENDED1;
				e.mouse_button.down = true;
				G.event_queue.pushBack(e);
			}

			if(flags & RI_MOUSE_BUTTON_4_UP) {
				e.type = Event::Type::MOUSE_BUTTON;
				e.mouse_button.button = MouseButton::EXTENDED1;
				e.mouse_button.down = false;
				G.event_queue.pushBack(e);
			}
			
			if(flags & RI_MOUSE_BUTTON_5_DOWN) {
				e.type = Event::Type::MOUSE_BUTTON;
				e.mouse_button.button = MouseButton::EXTENDED2;
				e.mouse_button.down = true;
				G.event_queue.pushBack(e);
			}

			if(flags & RI_MOUSE_BUTTON_5_UP) {
				e.type = Event::Type::MOUSE_BUTTON;
				e.mouse_button.button = MouseButton::EXTENDED2;
				e.mouse_button.down = false;
				G.event_queue.pushBack(e);
			}

			if (x != 0 || y != 0) {
				e.type = Event::Type::MOUSE_MOVE;
				e.mouse_move.xrel = x;
				e.mouse_move.yrel = y;
				G.event_queue.pushBack(e);
			}
				
			if (G.event_queue.empty()) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
				return false;
			}

			event = G.event_queue.popFront();

			break;
		}
		default:
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			goto retry;
	}

	if (translate_msg) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return true;
}

struct WindowData {
	InitWindowArgs init_args;
};

void destroyWindow(WindowHandle window) {
	WindowData* data = (WindowData*)GetWindowLongPtrW((HWND)window, GWLP_USERDATA);
	if (data) LUMIX_DELETE(getGlobalAllocator(), data);
	DestroyWindow((HWND)window);
}


Point toScreen(WindowHandle win, int x, int y)
{
	POINT p;
	p.x = x;
	p.y = y;
	FATAL_CHECK(::ClientToScreen((HWND)win, &p));
	Point res;
	res.x = p.x;
	res.y = p.y;
	return res;
}

void updateGrabbedMouse() {
	if (G.grabbed_window == INVALID_WINDOW) {
		DEBUG_CHECK(ClipCursor(NULL));
		return;
	}

	RECT rect;
	DEBUG_CHECK(GetWindowRect((HWND)G.grabbed_window, &rect));
	DEBUG_CHECK(ClipCursor(&rect));
}

WindowHandle createWindow(const InitWindowArgs& args) {
	PROFILE_FUNCTION();
	WCharStr<MAX_PATH> cls_name("lunex_window");
	static WNDCLASS wndcls = [&]() -> WNDCLASS {
		WNDCLASS wc = {};
		auto WndProc = [](HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
			Event e;
			e.window = hWnd;
			WindowData* win = (WindowData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
			switch (Msg) {
				case WM_SETCURSOR: 
					if (LOWORD(lParam) == HTCLIENT) {
						setCursor(G.current_cursor);
						return 1;
					}
					break;
				case WM_EXITSIZEMOVE:
					e.type = Event::Type::MOUSE_BUTTON;
					e.mouse_button.down = false;
					e.mouse_button.button = MouseButton::LEFT;
					G.event_queue.pushBack(e);
					break;
				case WM_NCCREATE: {
					void* userdata = ((CREATESTRUCTW*)lParam)->lpCreateParams;
					SetWindowLongPtrW(hWnd, GWLP_USERDATA, LONG_PTR(userdata));
					break;
				}
				case WM_MOVE:
					e.type = Event::Type::WINDOW_MOVE;
					e.win_move.x = (i16)LOWORD(lParam);
					e.win_move.y = (i16)HIWORD(lParam);
					G.event_queue.pushBack(e);
					updateGrabbedMouse();
					return 0;
				case WM_SIZE:
					e.type = Event::Type::WINDOW_SIZE;
					e.win_size.w = LOWORD(lParam);
					e.win_size.h = HIWORD(lParam);
					G.event_queue.pushBack(e);
					updateGrabbedMouse();
					return 0;
				case WM_CLOSE:
					e.type = Event::Type::WINDOW_CLOSE;
					G.event_queue.pushBack(e);
					if (hWnd == G.grabbed_window) G.grabbed_window = INVALID_WINDOW;
					updateGrabbedMouse();
					return 0;
				case WM_ACTIVATE:
					if (wParam == WA_INACTIVE) {
						showCursor(true);
						grabMouse(INVALID_WINDOW);
						G.key_states[(u32)os::Keycode::SHIFT] = false;
						G.key_states[(u32)os::Keycode::CTRL] = false;
						G.key_states[(u32)os::Keycode::ALT] = false;
						G.key_states[(u32)os::Keycode::LSHIFT] = false;
						G.key_states[(u32)os::Keycode::LCTRL] = false;
						G.key_states[(u32)os::Keycode::LALT] = false;
					}

					e.type = Event::Type::FOCUS;
					e.focus.gained = wParam != WA_INACTIVE;
					G.event_queue.pushBack(e);
					updateGrabbedMouse();
					break;
				case WM_NCPAINT:
				case WM_NCACTIVATE:
					if (win->init_args.flags & InitWindowArgs::NO_DECORATION) return TRUE;
					break;
				case WM_NCCALCSIZE:
					if (wParam == TRUE && win->init_args.flags & InitWindowArgs::NO_DECORATION) {
						NCCALCSIZE_PARAMS& params = *(NCCALCSIZE_PARAMS*)lParam;
						if (!isMaximized(hWnd)) return 0;

						auto monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONULL);
						if (!monitor) return 0;

						MONITORINFO monitor_info{};
						monitor_info.cbSize = sizeof(monitor_info);
						if (!GetMonitorInfoW(monitor, &monitor_info)) return 0;

						params.rgrc[0] = monitor_info.rcWork;
						return 0;
					}
					break;
				case WM_NCHITTEST: {
					if (win->init_args.flags & InitWindowArgs::NO_DECORATION) {
						// https://github.dev/melak47/BorderlessWindow/blob/master/BorderlessWindow/src/BorderlessWindow.cpp
						const POINT border{
							GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER),
							GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER)
						};
						RECT window_rect;
						if (!GetWindowRect(hWnd, &window_rect)) return HTNOWHERE;

						const POINT cp = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

						if (win->init_args.hit_test_callback) {
							switch (win->init_args.hit_test_callback(win->init_args.user_data, hWnd, {cp.x, cp.y})) {
								case HitTestResult::CAPTION: if (cp.y < (window_rect.top + border.y)) break; return HTCAPTION;
								case HitTestResult::CLIENT: return HTCLIENT;
								case HitTestResult::NONE: break;
							}
						}

						enum {
							CLIENT = 0b0000,
							LEFT = 0b0001,
							RIGHT = 0b0010,
							TOP = 0b0100,
							BOTTOM = 0b1000,
						};

						const auto result =
							LEFT * (cp.x < (window_rect.left + border.x)) |
							RIGHT * (cp.x >= (window_rect.right - border.x)) |
							TOP * (cp.y < (window_rect.top + border.y)) |
							BOTTOM * (cp.y >= (window_rect.bottom - border.y));

						switch (result) {
							case LEFT: return HTLEFT;
							case RIGHT: return HTRIGHT;
							case TOP: return HTTOP;
							case BOTTOM: return HTBOTTOM;
							case TOP | LEFT: return HTTOPLEFT;
							case TOP | RIGHT: return HTTOPRIGHT;
							case BOTTOM | LEFT: return HTBOTTOMLEFT;
							case BOTTOM | RIGHT: return HTBOTTOMRIGHT;
							case CLIENT: return HTCLIENT;
							default: return HTNOWHERE;
						}
					}
					break;
				}

			}
			return DefWindowProc(hWnd, Msg, wParam, lParam);
		};

		HICON hIcon = args.icon ? (HICON)LoadImageA(NULL, args.icon, IMAGE_ICON, 32, 32, LR_LOADFROMFILE) : NULL;
		if (!hIcon) hIcon = LoadIcon(NULL, IDI_APPLICATION);

		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = WndProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = GetModuleHandle(NULL);
		wc.hIcon = (HICON)hIcon;
		wc.hCursor = NULL;
		wc.hbrBackground = NULL;
		wc.lpszClassName = cls_name;

		FATAL_CHECK(RegisterClass(&wc));
		return wc;
	}();

	HWND parent_window = (HWND)args.parent;

	WCharStr<MAX_PATH> wname(args.name);
	DWORD style =  args.flags & InitWindowArgs::NO_DECORATION 
		? (args.hit_test_callback ? WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX : WS_POPUP)
		: WS_OVERLAPPEDWINDOW;
	DWORD ext_style = args.flags & InitWindowArgs::NO_TASKBAR_ICON ? WS_EX_TOOLWINDOW : WS_EX_APPWINDOW;
	WindowData* window_data = LUMIX_NEW(getGlobalAllocator(), WindowData);
	window_data->init_args = args;
	const HWND hwnd = CreateWindowEx(
		ext_style,
		cls_name,
		wname,
		style,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		800,
		600,
		parent_window,
		NULL,
		wndcls.hInstance,
		window_data);

	FATAL_CHECK(hwnd);

	if (args.handle_file_drops)
	{
		DragAcceptFiles(hwnd, TRUE);
	}

	ShowWindow(hwnd, SW_SHOW);
	DEBUG_CHECK(UpdateWindow(hwnd));

	if (!G.raw_input_registered) {
		RAWINPUTDEVICE device;
		device.usUsagePage = 0x01;
		device.usUsage = 0x02;
		device.dwFlags = RIDEV_INPUTSINK;
		device.hwndTarget = hwnd;
		FATAL_CHECK(RegisterRawInputDevices(&device, 1, sizeof(device)));
		G.raw_input_registered = true;
	}

	return hwnd;
}


bool isKeyDown(Keycode keycode) {
	return G.key_states[(i32)keycode];
}


void getKeyName(Keycode keycode, Span<char> out)
{
	LONG scancode = MapVirtualKey((UINT)keycode, MAPVK_VK_TO_VSC);

	// because MapVirtualKey strips the extended bit for some keys
	switch ((UINT)keycode)
	{
		case VK_LEFT:
		case VK_UP:
		case VK_RIGHT:
		case VK_DOWN:
		case VK_PRIOR:
		case VK_NEXT:
		case VK_END:
		case VK_HOME:
		case VK_INSERT:
		case VK_DELETE:
		case VK_DIVIDE:
		case VK_NUMLOCK: scancode |= 0x100; break;
	}

	WCHAR tmp[256];
	u32 size = out.length();
	ASSERT(size <= 256 && size > 0);
	int res = GetKeyNameText(scancode << 16, tmp, size);
	if (res == 0) {
		out[0] = 0;
	}
	else {
		fromWChar(out, tmp);
	}
}


void showCursor(bool show)
{
	if (show) {
		while(ShowCursor(show) < 0);
	}
	else {
		while(ShowCursor(show) >= 0);
	}
}

void abort() {
	ASSERT(false);
	::abort();
}

void init() {
	G.cursors.arrow = LoadCursor(NULL, IDC_ARROW);
	G.cursors.text_input = LoadCursor(NULL, IDC_IBEAM);
	G.cursors.load = LoadCursor(NULL, IDC_WAIT);
	G.cursors.size_ns = LoadCursor(NULL, IDC_SIZENS);
	G.cursors.size_we = LoadCursor(NULL, IDC_SIZEWE);
	G.cursors.size_nwse = LoadCursor(NULL, IDC_SIZENWSE);
}

void setCursor(CursorType type) {
	G.current_cursor = type;
	switch (type) {
		case CursorType::DEFAULT: SetCursor(G.cursors.arrow); break;
		case CursorType::LOAD: SetCursor(G.cursors.load); break;
		case CursorType::SIZE_NS: SetCursor(G.cursors.size_ns); break;
		case CursorType::SIZE_WE: SetCursor(G.cursors.size_we); break;
		case CursorType::SIZE_NWSE: SetCursor(G.cursors.size_nwse); break;
		case CursorType::TEXT_INPUT: SetCursor(G.cursors.text_input); break;
		case CursorType::UNDEFINED: break;
	}
}

void setWindowTitle(WindowHandle win, const char* title)
{
	WCharStr<256> tmp(title);
	DEBUG_CHECK(SetWindowText((HWND)win, tmp));
}


Rect getWindowScreenRect(WindowHandle win)
{
	RECT rect;
	FATAL_CHECK(GetWindowRect((HWND)win, &rect));
	return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
}

Rect getWindowClientRect(WindowHandle win)
{
	RECT rect;
	FATAL_CHECK(GetClientRect((HWND)win, &rect));
	return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
}

void setWindowScreenRect(WindowHandle win, const Rect& rect)
{
	DEBUG_CHECK(MoveWindow((HWND)win, rect.left, rect.top, rect.width, rect.height, TRUE));
}

u32 getMonitors(Span<Monitor> monitors)
{
	struct Callback {
		struct Data {
			Span<Monitor>* monitors;
			u32 index;
		};

		static BOOL CALLBACK func(HMONITOR monitor, HDC, LPRECT, LPARAM lparam)
		{
			Data* data = reinterpret_cast<Data*>(lparam);
			if (data->index >= data->monitors->length()) return TRUE;

			MONITORINFO info = { 0 };
			info.cbSize = sizeof(MONITORINFO);
			if (!::GetMonitorInfo(monitor, &info)) return TRUE;
			
			Monitor& m = (*data->monitors)[data->index];
			m.monitor_rect.left = info.rcMonitor.left;
			m.monitor_rect.top = info.rcMonitor.top;
			m.monitor_rect.width = info.rcMonitor.right - info.rcMonitor.left;
			m.monitor_rect.height = info.rcMonitor.bottom - info.rcMonitor.top;

			m.work_rect.left = info.rcWork.left;
			m.work_rect.top = info.rcWork.top;
			m.work_rect.width = info.rcWork.right - info.rcWork.left;
			m.work_rect.height = info.rcWork.bottom - info.rcWork.top;
			
			m.primary = info.dwFlags & MONITORINFOF_PRIMARY;
			++data->index;

			return TRUE;
		}
	};

	Callback::Data data = {
		&monitors,
		0
	};

	DEBUG_CHECK(::EnumDisplayMonitors(NULL, NULL, &Callback::func, (LPARAM)&data));
	return data.index;
}

void setMouseScreenPos(int x, int y)
{
	DEBUG_CHECK(SetCursorPos(x, y));
}

Point getMouseScreenPos()
{
	POINT p;
	static POINT last_p = {};
	const BOOL b = GetCursorPos(&p);
	// GetCursorPos fails after computer wakes up and the app is running, we return last known position
	if (!b) p = last_p;
	last_p = p;
	return {p.x, p.y};
}


WindowHandle getFocused()
{
	return GetActiveWindow();
}

bool isMaximized(WindowHandle win) {
	WINDOWPLACEMENT placement;
	DEBUG_CHECK(GetWindowPlacement((HWND)win, &placement));
	return placement.showCmd == SW_SHOWMAXIMIZED;
}

bool isMinimized(WindowHandle win) {
	WINDOWPLACEMENT placement;
	DEBUG_CHECK(GetWindowPlacement((HWND)win, &placement));
	return placement.showCmd == SW_SHOWMINIMIZED;
}

void restore(WindowHandle win, WindowState state) {
	DEBUG_CHECK(SetWindowLongPtr((HWND)win, GWL_STYLE, state.style));
	setWindowScreenRect(win, state.rect);
}

WindowState setFullscreen(WindowHandle win) {
	WindowState res;
	res.rect = getWindowScreenRect(win);
	res.style = SetWindowLongPtr((HWND)win, GWL_STYLE, WS_VISIBLE | WS_POPUP);
	DEBUG_CHECK(res.style);
	int w = GetSystemMetrics(SM_CXSCREEN);
	int h = GetSystemMetrics(SM_CYSCREEN);
	DEBUG_CHECK(SetWindowPos((HWND)win, HWND_TOP, 0, 0, w, h, SWP_FRAMECHANGED));
	return res;
}

void restore(WindowHandle win) {
	DEBUG_CHECK(ShowWindow((HWND)win, SW_RESTORE));
}

void maximizeWindow(WindowHandle win)
{
	DEBUG_CHECK(ShowWindow((HWND)win, SW_SHOWMAXIMIZED));
}

void minimizeWindow(WindowHandle win)
{
	DEBUG_CHECK(ShowWindow((HWND)win, SW_SHOWMINIMIZED));
}


bool isRelativeMouseMode()
{
	return G.relative_mouse;
}

int getDPI()
{
	const HDC hdc = GetDC(NULL);
    return GetDeviceCaps(hdc, LOGPIXELSX);
}

u32 getMemPageSize() {
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	return info.dwPageSize;
}

u64 getProcessMemory() {
	PROCESS_MEMORY_COUNTERS counters;
	if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
		return counters.WorkingSetSize;
	}
	ASSERT(false);
	return 0;
}


u32 getMemPageAlignment() {
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	return info.dwAllocationGranularity;
}

void* memReserve(size_t size) {
	return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE);
}

void memCommit(void* ptr, size_t size) {
	VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
}

void memRelease(void* ptr, size_t size) {
	VirtualFree(ptr, 0, MEM_RELEASE);
}

struct FileIterator
{
	HANDLE handle;
	IAllocator* allocator;
	WIN32_FIND_DATA ffd;
	bool is_valid;
};


FileIterator* createFileIterator(StringView path, IAllocator& allocator)
{
	StaticString<MAX_PATH> tmp(path, "/*");
	
	WCharStr<MAX_PATH> wtmp(tmp);
	auto* iter = LUMIX_NEW(allocator, FileIterator);
	iter->allocator = &allocator;
	iter->handle = FindFirstFile(wtmp, &iter->ffd);
	iter->is_valid = iter->handle != INVALID_HANDLE_VALUE;
	return iter;
}


void destroyFileIterator(FileIterator* iterator)
{
	if (iterator->is_valid) {
		DEBUG_CHECK(FindClose(iterator->handle));
	}
	LUMIX_DELETE(*iterator->allocator, iterator);
}


bool getNextFile(FileIterator* iterator, FileInfo* info)
{
	if (!iterator->is_valid) return false;

	info->is_directory = (iterator->ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	fromWChar(Span(info->filename), iterator->ffd.cFileName);

	iterator->is_valid = FindNextFile(iterator->handle, &iterator->ffd) != FALSE;
	return true;
}


void setCurrentDirectory(StringView path)
{
	WCharStr<MAX_PATH> tmp(path);
	FATAL_CHECK(SetCurrentDirectory(tmp));
}


void getCurrentDirectory(Span<char> output)
{
	WCHAR tmp[MAX_PATH];
	FATAL_CHECK(GetCurrentDirectory(lengthOf(tmp), tmp));
	fromWChar(output, tmp);
}


bool getSaveFilename(Span<char> out, const char* filter, const char* default_extension)
{
	WCharStr<MAX_PATH> wtmp("");
	WCHAR wfilter[MAX_PATH];
	
	const char* c = filter;
	WCHAR* cout = wfilter;
	while ((*c || *(c + 1)) && (c - filter) < MAX_PATH - 2) {
		*cout = *c;
		++cout;
		++c;
	}
	*cout = 0;
	++cout;
	*cout = 0;

	WCharStr<MAX_PATH> wdefault_extension(default_extension ? default_extension : "");
	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = wtmp.data;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = lengthOf(wtmp.data);
	ofn.lpstrFilter = wfilter;
	ofn.nFilterIndex = 1;
	ofn.lpstrDefExt = default_extension ? wdefault_extension.data : nullptr;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_NONETWORKBUTTON;

	bool res = GetSaveFileName(&ofn) != FALSE;

	char tmp[MAX_PATH];
	fromWChar(Span(tmp), wtmp);
	if (res) Path::normalize(tmp, out);
	return res;
}


bool getOpenFilename(Span<char> out, const char* filter, const char* starting_file)
{
	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	if (starting_file)
	{
		char* to = out.begin();
		for (const char *from = starting_file; *from; ++from, ++to)
		{
			if (to - out.begin() > out.length() - 1) break;
			*to = *to == '/' ? '\\' : *from;
		}
		*to = '\0';
	}
	else
	{
		out[0] = '\0';
	}
	WCHAR wout[MAX_PATH] = {};
	WCHAR wfilter[MAX_PATH];
	
	const char* c = filter;
	WCHAR* cout = wfilter;
	while ((*c || *(c + 1)) && (c - filter) < MAX_PATH - 2) {
		*cout = *c;
		++cout;
		++c;
	}
	*cout = 0;
	++cout;
	*cout = 0;

	ofn.lpstrFile = wout;
	ofn.nMaxFile = sizeof(wout);
	ofn.lpstrFilter = wfilter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = nullptr;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_NONETWORKBUTTON;

	const bool res = GetOpenFileName(&ofn) != FALSE;
	if (res) {
		char tmp[MAX_PATH];
		fromWChar(Span(tmp), wout);
		Path::normalize(tmp, out);
	}
	else {
		auto err = CommDlgExtendedError();
		ASSERT(err == 0);
	}
	return res;
}


bool getOpenDirectory(Span<char> output, const char* starting_dir)
{
	bool ret = false;
	IFileDialog* pfd;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
	{
		if (starting_dir)
		{
			PIDLIST_ABSOLUTE pidl;
			WCHAR wstarting_dir[MAX_PATH];
			WCHAR* wc = wstarting_dir;
			for (const char *c = starting_dir; *c && wc - wstarting_dir < MAX_PATH - 1; ++c, ++wc)
			{
				*wc = *c == '/' ? '\\' : *c;
			}
			*wc = 0;

			HRESULT hresult = ::SHParseDisplayName(wstarting_dir, 0, &pidl, SFGAO_FOLDER, 0);
			if (SUCCEEDED(hresult))
			{
				IShellItem* psi;
				hresult = ::SHCreateShellItem(NULL, NULL, pidl, &psi);
				if (SUCCEEDED(hresult))
				{
					pfd->SetFolder(psi);
				}
				ILFree(pidl);
			}
		}

		DWORD dwOptions;
		if (SUCCEEDED(pfd->GetOptions(&dwOptions)))
		{
			pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);
		}
		if (SUCCEEDED(pfd->Show(NULL)))
		{
			IShellItem* psi;
			if (SUCCEEDED(pfd->GetResult(&psi)))
			{
				WCHAR* tmp;
				if (SUCCEEDED(psi->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &tmp)))
				{
					char* c = output.begin();
					const u32 max_size = output.length();
					while (*tmp && c - output.begin() < max_size - 1)
					{
						*c = (char)*tmp;
						++c;
						++tmp;
					}
					*c = '\0';
					if (!endsWith(output.begin(), "/") && !endsWith(output.begin(), "\\") && c - output.begin() < max_size - 1)
					{
						*c = '/';
						++c;
						*c = '\0';
					}
					ret = true;
				}
				psi->Release();
			}
		}
		pfd->Release();
	}
	return ret;
}


void copyToClipboard(const char* text)
{
	if (!OpenClipboard(NULL)) return;
	int len = stringLength(text) + 1;
	HGLOBAL mem_handle = GlobalAlloc(GMEM_MOVEABLE, len * sizeof(char));
	if (!mem_handle) return;

	char* mem = (char*)GlobalLock(mem_handle);
	copyString(Span(mem, len), text);
	GlobalUnlock(mem_handle);
	EmptyClipboard();
	SetClipboardData(CF_TEXT, mem_handle);
	CloseClipboard();
}


ExecuteOpenResult shellExecuteOpen(StringView path, StringView args, StringView working_dir)
{
	const WCharStr<MAX_PATH> wpath(path);
	const WCharStr<MAX_PATH> wargs(args);
	const WCharStr<MAX_PATH> wdir(working_dir);
	const uintptr_t res = (uintptr_t)ShellExecute(NULL, NULL, wpath, args.empty() ? NULL : wargs.data, working_dir.empty() ? NULL : wdir.data, SW_SHOW);
	if (res > 32) return ExecuteOpenResult::SUCCESS;
	if (res == SE_ERR_NOASSOC) return ExecuteOpenResult::NO_ASSOCIATION;
	return ExecuteOpenResult::OTHER_ERROR;
}


ExecuteOpenResult openExplorer(StringView path)
{
	const WCharStr<MAX_PATH> wpath(path);
	const uintptr_t res = (uintptr_t)ShellExecute(NULL, L"explore", wpath, NULL, NULL, SW_SHOWNORMAL);
	if (res > 32) return ExecuteOpenResult::SUCCESS;
	if (res == SE_ERR_NOASSOC) return ExecuteOpenResult::NO_ASSOCIATION;
	return ExecuteOpenResult::OTHER_ERROR;
}


bool deleteFile(StringView path)
{
	const WCharStr<MAX_PATH> wpath(path);
	return DeleteFile(wpath) != FALSE;
}


bool moveFile(StringView from, StringView to)
{
	const WCharStr<MAX_PATH> wfrom(from);
	const WCharStr<MAX_PATH> wto(to);
	return MoveFileEx(wfrom, wto, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != FALSE;
}


size_t getFileSize(StringView path)
{
	WIN32_FILE_ATTRIBUTE_DATA fad;
	const WCharStr<MAX_PATH> wpath(path);
	if (!GetFileAttributesEx(wpath, GetFileExInfoStandard, &fad)) return -1;
	LARGE_INTEGER size;
	size.HighPart = fad.nFileSizeHigh;
	size.LowPart = fad.nFileSizeLow;
	return (size_t)size.QuadPart;
}


bool fileExists(StringView path)
{
	const WCharStr<MAX_PATH> wpath(path);
	DWORD dwAttrib = GetFileAttributes(wpath);
	return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}


bool dirExists(StringView path)
{
	const WCharStr<MAX_PATH> wpath(path);
	DWORD dwAttrib = GetFileAttributes(wpath);
	return (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}


u64 getLastModified(StringView path)
{
	const WCharStr<MAX_PATH> wpath(path);
	FILETIME ft;
	HANDLE handle = CreateFile(wpath, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE) return 0;
	if (GetFileTime(handle, NULL, NULL, &ft) == FALSE) {
		CloseHandle(handle);
		return 0;
	}
	CloseHandle(handle);

	ULARGE_INTEGER i;
	i.LowPart = ft.dwLowDateTime;
	i.HighPart = ft.dwHighDateTime;
	return i.QuadPart;
}


bool makePath(const char* path)
{
	char tmp[MAX_PATH];
	char* out = tmp;
	const char* in = path;
	while (*in && out - tmp < lengthOf(tmp) - 1)
	{
		*out = *in == '/' ? '\\' : *in;
		++out;
		++in;
	}
	*out = '\0';

	const WCharStr<MAX_PATH> wpath(tmp);
	int error_code = SHCreateDirectoryEx(NULL, wpath, NULL);
	return error_code == ERROR_SUCCESS || error_code == ERROR_ALREADY_EXISTS;
}

void grabMouse(WindowHandle win) {
	G.grabbed_window = win;
	updateGrabbedMouse();
}


bool copyFile(StringView from, StringView to)
{
	WCHAR tmp_from[MAX_PATH];
	WCHAR tmp_to[MAX_PATH];
	toWChar(tmp_from, from);
	toWChar(tmp_to, to);
	
	if (CopyFile(tmp_from, tmp_to, FALSE) == FALSE) return false;

	FILETIME ft;
	SYSTEMTIME st;

	GetSystemTime(&st);
	SystemTimeToFileTime(&st, &ft);
	HANDLE handle = CreateFile(tmp_to, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	bool f = SetFileTime(handle, (LPFILETIME)NULL, (LPFILETIME)NULL, &ft) != FALSE;
	ASSERT(f);
	CloseHandle(handle);

	return true;
}

bool getAppDataDir(Span<char> out) {
	WCHAR path[MAX_PATH];
	if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, path))) return false;
	fromWChar(out, path);
	return true;
}

void getExecutablePath(Span<char> buffer)
{
	WCHAR tmp[MAX_PATH];
	FATAL_CHECK(GetModuleFileName(NULL, tmp, sizeof(tmp)));
	fromWChar(buffer, tmp);
}


void messageBox(const char* text)
{
	WCHAR tmp[2048];
	toWChar(tmp, text);
	DEBUG_CHECK(MessageBox(NULL, tmp, L"Message", MB_OK));
}

	
void setCommandLine(int, char**)
{
	ASSERT(false);
}
	

bool getCommandLine(Span<char> output)
{
	WCHAR* cl = GetCommandLine();
	fromWChar(output, cl);
	return true;
}


void* loadLibrary(const char* path)
{
	WCHAR tmp[MAX_PATH];
	toWChar(tmp, path);
	return LoadLibrary(tmp);
}


void unloadLibrary(void* handle)
{
	if (handle) {
		DEBUG_CHECK(FreeLibrary((HMODULE)handle));
	}
}

float getTimeSinceProcessStart() {
	FILETIME now, created, dummy;
	GetSystemTimeAsFileTime(&now);
	if (!GetProcessTimes(GetCurrentProcess(), &created, &dummy, &dummy, &dummy)) return 0;

	u64 now_t = u64(now.dwHighDateTime) << 32 | now.dwLowDateTime;
	u64 created_t = u64(created.dwHighDateTime) << 32 | created.dwLowDateTime;
	return float(double((now_t - created_t) / 10) / 1000.0 / 1000.0);
}

void* getLibrarySymbol(void* handle, const char* name)
{
	return (void*)GetProcAddress((HMODULE)handle, name);
}

Timer::Timer()
{
	LARGE_INTEGER f, n;

	DEBUG_CHECK(QueryPerformanceFrequency(&f));
	DEBUG_CHECK(QueryPerformanceCounter(&n));
	first_tick = last_tick = n.QuadPart;
	frequency = f.QuadPart;

}


float Timer::getTimeSinceStart() const
{
	LARGE_INTEGER n;
	DEBUG_CHECK(QueryPerformanceCounter(&n));
	const u64 tick = n.QuadPart;
	float delta = static_cast<float>((double)(tick - first_tick) / (double)frequency);
	return delta;
}


float Timer::getTimeSinceTick() const
{
	LARGE_INTEGER n;
	DEBUG_CHECK(QueryPerformanceCounter(&n));
	const u64 tick = n.QuadPart;
	float delta = static_cast<float>((double)(tick - last_tick) / (double)frequency);
	return delta;
}


float Timer::tick()
{
	LARGE_INTEGER n;
	DEBUG_CHECK(QueryPerformanceCounter(&n));
	const u64 tick = n.QuadPart;
	float delta = static_cast<float>((double)(tick - last_tick) / (double)frequency);
	last_tick = tick;
	return delta;
}


u64 Timer::getFrequency()
{
	static u64 freq = []() {
		LARGE_INTEGER f;
		DEBUG_CHECK(QueryPerformanceFrequency(&f));
		return f.QuadPart;
	}();
	return freq;
}


u64 Timer::getRawTimestamp()
{
	LARGE_INTEGER tick;
	DEBUG_CHECK(QueryPerformanceCounter(&tick));
	return tick.QuadPart;
}

static bool g_network_initialized = false;
bool initNetwork() {
	if (g_network_initialized) return true;
	WORD sockVer;
	WSADATA wsaData;
	sockVer = 2 | (2 << 8);
	g_network_initialized = WSAStartup(sockVer, &wsaData) == 0;
	return g_network_initialized;
}

void shutdownNetwork() {
	if (g_network_initialized) WSACleanup();
	g_network_initialized = false;
}

struct NetworkStream {
	IAllocator* allocator;
	SOCKET socket;
};

struct NetworkStream* listen(const char* ip, u16 port, IAllocator& allocator) {
	SOCKET listen_socket = ::socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_socket == INVALID_SOCKET) return nullptr;

	SOCKADDR_IN sin = {};
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = ::inet_addr(ip);

	int retVal = ::bind(listen_socket, (LPSOCKADDR)&sin, sizeof(sin));
	if (retVal == SOCKET_ERROR) return nullptr;
			
	i32 res = ::listen(listen_socket, 10);
	if (res != 0) return nullptr;
			
	SOCKET socket = ::accept(listen_socket, nullptr, nullptr);
	closesocket(listen_socket);
	if (socket == INVALID_SOCKET) return nullptr;

	auto* stream = LUMIX_NEW(allocator, NetworkStream);
	stream->socket = socket;
	stream->allocator = &allocator;
	return stream;
}

NetworkStream* connect(const char* ip, u16 port, IAllocator& allocator) {
	SOCKET socket = ::socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket == INVALID_SOCKET) return nullptr;

	SOCKADDR_IN sin = {};
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = ::inet_addr(ip);

	if (::connect(socket, (LPSOCKADDR)&sin, sizeof(sin)) != 0) return nullptr;

	auto* stream = LUMIX_NEW(allocator, NetworkStream);
	stream->socket = socket;
	stream->allocator = &allocator;
	return stream;
}

bool read(NetworkStream& stream, void* mem, u32 size) {
	i32 to_receive = size;
	char* ptr = (char*)mem;

	do {
		i32 received = ::recv(stream.socket, ptr, to_receive, 0);
		ptr += received;
		to_receive -= received;
		if (received == SOCKET_ERROR) {
			i32 err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK) {
				ptr -= received;
				to_receive += received;
			} else {
				return false;
			}
		}
	} while (to_receive > 0);
	return true;
}

bool write(NetworkStream& stream, const void* data, u32 size) {
	i32 send = ::send(stream.socket, (const char*)data, size, 0);
	return send == size;
}

void close(NetworkStream& stream) {
	closesocket(stream.socket);
	stream.allocator->deallocate(&stream);
}


} // namespace Lumix::OS
