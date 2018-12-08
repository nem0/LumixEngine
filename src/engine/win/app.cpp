#include "../app.h"
#include "engine/lumix.h"
#define UNICODE
#include <Windows.h>
#include <cassert>

namespace Lumix::App
{


static struct
{
	bool finished = false;
	App::Interface* iface = nullptr;
	Point relative_mode_pos = {};
	bool relative_mouse = false;
	WindowHandle win = INVALID_WINDOW;
} G;


static void fromWChar(char* out, int size, const WCHAR* in)
{
	const WCHAR* c = in;
	char* cout = out;
	while (*c && c - in < size - 1)
	{
		*cout = (char)*c;
		++cout;
		++c;
	}
	*cout = 0;
}


template <int N> static void toWChar(WCHAR (&out)[N], const char* in)
{
	const char* c = in;
	WCHAR* cout = out;
	while (*c && c - in < N - 1)
	{
		*cout = *c;
		++cout;
		++c;
	}
	*cout = 0;
}


void getDropFile(const Event& event, int idx, char* out, int max_size)
{
	assert(max_size > 0);
	HDROP drop = (HDROP)event.file_drop.handle;
	WCHAR buffer[MAX_PATH];
	if (DragQueryFile(drop, idx, buffer, MAX_PATH))
	{
		fromWChar(out, max_size, buffer);
	}
	else
	{
		assert(false);
	}
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


static void processEvents()
{
	MSG msg;
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		Event e;
		e.window = msg.hwnd;
		switch (msg.message) {
			case WM_DROPFILES:
				e.type = Event::Type::DROP_FILE;
				e.file_drop.handle = (HDROP)msg.wParam;
				G.iface->onEvent(e);
				break;
			case WM_QUIT: 
				e.type = Event::Type::QUIT; 
				G.iface->onEvent(e);
				break;
			case WM_CLOSE: 
				e.type = Event::Type::WINDOW_CLOSE; 
				G.iface->onEvent(e);
				break;
			case WM_KEYDOWN:
				e.type = Event::Type::KEY;
				e.key.down = true;
				e.key.keycode = (Keycode)msg.wParam;
				G.iface->onEvent(e);
				break;
			case WM_KEYUP:
				e.type = Event::Type::KEY;
				e.key.down = false;
				e.key.keycode = (Keycode)msg.wParam;
				G.iface->onEvent(e);
				break;
			case WM_CHAR:
				e.type = Event::Type::CHAR;
				e.text_input.utf32 = (u32)msg.wParam;
				// TODO msg.wParam is utf16, convert
				// e.g. https://github.com/SFML/SFML/blob/master/src/SFML/Window/Win32/WindowImplWin32.cpp#L694
				G.iface->onEvent(e);
				break;
			case WM_INPUT: {
				HRAWINPUT hRawInput = (HRAWINPUT)msg.lParam;
				UINT dataSize;
				GetRawInputData(hRawInput, RID_INPUT, NULL, &dataSize, sizeof(RAWINPUTHEADER));
				char dataBuf[1024];
				if (dataSize == 0 || dataSize > sizeof(dataBuf)) break;

				GetRawInputData(hRawInput, RID_INPUT, dataBuf, &dataSize, sizeof(RAWINPUTHEADER));

				const RAWINPUT* raw = (const RAWINPUT*)dataBuf;
				if (raw->header.dwType != RIM_TYPEMOUSE) break;

				const HANDLE deviceHandle = raw->header.hDevice;
				const RAWMOUSE& mouseData = raw->data.mouse;
				const USHORT flags = mouseData.usButtonFlags;
				const short wheel_delta = (short)mouseData.usButtonData;
				const LONG x = mouseData.lLastX, y = mouseData.lLastY;

				if (wheel_delta) {
					e.mouse_wheel.amount = (float)wheel_delta / WHEEL_DELTA;
					e.type = Event::Type::MOUSE_WHEEL;
					G.iface->onEvent(e);
				}

				if(flags & RI_MOUSE_LEFT_BUTTON_DOWN) {
					e.type = Event::Type::MOUSE_BUTTON;
					e.mouse_button.button = MouseButton::LEFT;
					e.mouse_button.down = true;
					G.iface->onEvent(e);
				}
				if(flags & RI_MOUSE_LEFT_BUTTON_UP) {
					e.type = Event::Type::MOUSE_BUTTON;
					e.mouse_button.button = MouseButton::LEFT;
					e.mouse_button.down = false;
					G.iface->onEvent(e);
				}
					
				if(flags & RI_MOUSE_RIGHT_BUTTON_UP) {
					e.type = Event::Type::MOUSE_BUTTON;
					e.mouse_button.button = MouseButton::RIGHT;
					e.mouse_button.down = false;
					G.iface->onEvent(e);
				}
				if(flags & RI_MOUSE_RIGHT_BUTTON_DOWN) {
					e.type = Event::Type::MOUSE_BUTTON;
					e.mouse_button.button = MouseButton::RIGHT;
					e.mouse_button.down = true;
					G.iface->onEvent(e);
				}

				if(flags & RI_MOUSE_MIDDLE_BUTTON_UP) {
					e.type = Event::Type::MOUSE_BUTTON;
					e.mouse_button.button = MouseButton::MIDDLE;
					e.mouse_button.down = false;
					G.iface->onEvent(e);
				}
				if(flags & RI_MOUSE_MIDDLE_BUTTON_DOWN) {
					e.type = Event::Type::MOUSE_BUTTON;
					e.mouse_button.button = MouseButton::MIDDLE;
					e.mouse_button.down = true;
					G.iface->onEvent(e);
				}

				if (x != 0 || y != 0) {
					e.type = Event::Type::MOUSE_MOVE;
					e.mouse_move.xrel = x;
					e.mouse_move.yrel = y;
					G.iface->onEvent(e);
				}
				break;
			}
		}

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}


void destroyWindow(WindowHandle window)
{
	DestroyWindow((HWND)window);
	G.win = INVALID_WINDOW;
}

void UTF32ToUTF8(u32 utf32, char* utf8)
{
    if (utf32 <= 0x7F) {
        utf8[0] = (char) utf32;
        utf8[1] = '\0';
    } else if (utf32 <= 0x7FF) {
        utf8[0] = 0xC0 | (char) ((utf32 >> 6) & 0x1F);
        utf8[1] = 0x80 | (char) (utf32 & 0x3F);
        utf8[2] = '\0';
    } else if (utf32 <= 0xFFFF) {
        utf8[0] = 0xE0 | (char) ((utf32 >> 12) & 0x0F);
        utf8[1] = 0x80 | (char) ((utf32 >> 6) & 0x3F);
        utf8[2] = 0x80 | (char) (utf32 & 0x3F);
        utf8[3] = '\0';
    } else if (utf32 <= 0x10FFFF) {
        utf8[0] = 0xF0 | (char) ((utf32 >> 18) & 0x0F);
        utf8[1] = 0x80 | (char) ((utf32 >> 12) & 0x3F);
        utf8[2] = 0x80 | (char) ((utf32 >> 6) & 0x3F);
        utf8[3] = 0x80 | (char) (utf32 & 0x3F);
        utf8[4] = '\0';
	}
	else {
		ASSERT(false);
	}
}


WindowHandle createWindow(const InitWindowArgs& args)
{
	WNDCLASS wc = {};

	auto WndProc = [](HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
		App::Event e;
		e.window = hWnd;
		switch (Msg)
		{
			case WM_MOVE:
				e.type = Event::Type::WINDOW_MOVE;
				e.win_move.x = LOWORD(lParam);
				e.win_move.y = HIWORD(lParam);
				App::G.iface->onEvent(e);
				return 0;
			case WM_SIZE:
				e.type = Event::Type::WINDOW_SIZE;
				e.win_size.w = LOWORD(lParam);
				e.win_size.h = HIWORD(lParam);
				return 0;
			case WM_CLOSE:
				e.type = Event::Type::WINDOW_CLOSE;
				App::G.iface->onEvent(e);
				return 0;
			case WM_ACTIVATE:
				if (wParam == WA_INACTIVE) ShowCursor(TRUE);
				break;
		}
		return DefWindowProc(hWnd, Msg, wParam, lParam);
	};

	wc.style = 0;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = GetModuleHandle(NULL);
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	WCHAR wname[256];
	toWChar(wname, args.name);
	wc.lpszClassName = wname;

	if (!RegisterClass(&wc)) return INVALID_WINDOW;

	const HWND hwnd = CreateWindow(wname,
		wname,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		NULL,
		NULL,
		wc.hInstance,
		NULL);

	if (args.handle_file_drops)
	{
		DragAcceptFiles(hwnd, TRUE);
	}

	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
	G.win = hwnd;
	return hwnd;
}


void quit()
{
	G.finished = true;
}


bool isKeyDown(Keycode keycode)
{
	const SHORT res = GetAsyncKeyState((int)keycode);
	return (res & 0x8000) != 0;
}


void getKeyName(Keycode keycode, char* out, int size)
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
	assert(size <= 256 && size > 0);
	int res = GetKeyNameText(scancode << 16, tmp, size);
	if (res == 0)
	{
		*out = 0;
	}
	else
	{
		fromWChar(out, size, tmp);
	}
}


void showCursor(bool show)
{
	ShowCursor(show);
}


void setWindowTitle(WindowHandle win, const char* title)
{
	WCHAR tmp[256];
	toWChar(tmp, title);
	SetWindowText((HWND)win, tmp);
}


Rect getWindowScreenRect(WindowHandle win)
{
	RECT rect;
	GetWindowRect((HWND)win, &rect);
	return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
}


Point getWindowClientSize(WindowHandle win)
{
	RECT rect;
	GetClientRect((HWND)win, &rect);
	return {rect.right - rect.left, rect.bottom - rect.top};
}


void setWindowScreenRect(WindowHandle win, const Rect& rect)
{
	MoveWindow((HWND)win, rect.left, rect.top, rect.width, rect.height, TRUE);
}


void setMousePos(int x, int y)
{
	SetCursorPos(x, y);
}

Point getMousePos(WindowHandle win)
{
	POINT p;
	BOOL b = GetCursorPos(&p);
	ScreenToClient((HWND)win, &p);
	assert(b);
	return {p.x, p.y};
}

Point getMousePos()
{
	POINT p;
	BOOL b = GetCursorPos(&p);
	assert(b);
	return {p.x, p.y};
}


WindowHandle getFocused()
{
	return GetActiveWindow();
}


bool isMaximized(WindowHandle win)
{
	// TODO
	return false;
}


void maximizeWindow(WindowHandle win)
{
	ShowWindow((HWND)win, SW_SHOWMAXIMIZED);
}


bool isRelativeMouseMode()
{
	return G.relative_mouse;
}


void run(Interface& iface)
{
	RAWINPUTDEVICE device;
	device.usUsagePage = 0x01;
	device.usUsage = 0x02;
	device.dwFlags = 0;
	device.hwndTarget = 0;
	RegisterRawInputDevices(&device, 1, sizeof(device));

	G.iface = &iface;
	G.iface->onInit();
	while (!G.finished)
	{
		App::processEvents();
		G.iface->onIdle();
	}
}


int getDPI()
{
	// TODO
	return 96;
}


} // namespace Lumix::App
