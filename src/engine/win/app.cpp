#include "../app.h"
#include "engine/lumix.h"
#define UNICODE
#include <Windows.h>
#include <cassert>

namespace Lumix::App {


static bool g_finished = false;
static App::Interface* g_interface = nullptr;
static Point g_last_mouse_pos = {};


static void fromWChar(char* out, int size, const WCHAR* in)
{
    const WCHAR* c = in;
    char* cout = out;
    while (*c && c - in < size - 1) {
        *cout = (char)*c;
        ++cout;
        ++c;
    }
    *cout = 0;
}


template <int N>
static void toWChar(WCHAR (&out)[N], const char* in)
{
    const char* c = in;
    WCHAR* cout = out;
    while (*c && c - in < N - 1) {
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
    if (DragQueryFile(drop, idx, buffer, MAX_PATH)) {
        fromWChar(out, max_size, buffer);
    }
    else {
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


static bool getEvent(Event* e)
{
    MSG msg;
    bool known_msg = true;
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        e->window = msg.hwnd;
        switch(msg.message) {
            case WM_DROPFILES: 
                e->type = Event::Type::DROP_FILE;
                e->file_drop.handle = (HDROP) msg.wParam;
                break;
            case WM_QUIT:
                e->type = Event::Type::QUIT; 
                break;
            case WM_CLOSE: 
                e->type = Event::Type::WINDOW_CLOSE;
                break;
            case WM_KEYDOWN:
                e->type = Event::Type::KEY;
                e->key.down = true;
                e->key.keycode = (Keycode)msg.wParam;
                break;
            case WM_KEYUP:
                e->type = Event::Type::KEY;
                e->key.down = false;
                e->key.keycode = (Keycode)msg.wParam;
                break;
            case WM_CHAR:
                e->type = Event::Type::CHAR;
                e->text_input.utf32 = (u32)msg.wParam; 
                // TODO msg.wParam is utf16, convert
                // e.g. https://github.com/SFML/SFML/blob/master/src/SFML/Window/Win32/WindowImplWin32.cpp#L694
                break;
            case WM_MOUSEWHEEL: {
                    const short amount = GET_WHEEL_DELTA_WPARAM(msg.wParam);
                    e->mouse_wheel.amount = (float)amount / WHEEL_DELTA;
                    e->type = Event::Type::MOUSE_WHEEL;
                    break;
                }
			case WM_MOUSEMOVE: {
				e->type = Event::Type::MOUSE_MOVE;
				e->mouse_move.x = LOWORD(msg.lParam); 
				e->mouse_move.y = HIWORD(msg.lParam); 
				e->mouse_move.xrel = e->mouse_move.x - g_last_mouse_pos.x;
				e->mouse_move.yrel = e->mouse_move.y - g_last_mouse_pos.y;
				g_last_mouse_pos.x = e->mouse_move.x;
				g_last_mouse_pos.y = e->mouse_move.y;
				break;
			}
            case WM_LBUTTONDOWN:
                e->type = Event::Type::MOUSE_BUTTON;
                e->mouse_button.button = MouseButton::LEFT;
                e->mouse_button.down = true;
				e->mouse_button.x = LOWORD(msg.lParam);
				e->mouse_button.y = HIWORD(msg.lParam);
                break;
            case WM_LBUTTONUP:
				e->type = Event::Type::MOUSE_BUTTON;
                e->mouse_button.button = MouseButton::LEFT;
                e->mouse_button.down = false;
				e->mouse_button.x = LOWORD(msg.lParam);
				e->mouse_button.y = HIWORD(msg.lParam);
				break;
            case WM_RBUTTONDOWN:
				e->type = Event::Type::MOUSE_BUTTON;
                e->mouse_button.button = MouseButton::RIGHT;
                e->mouse_button.down = true;
				e->mouse_button.x = LOWORD(msg.lParam);
				e->mouse_button.y = HIWORD(msg.lParam);
				break;
            case WM_RBUTTONUP:
				e->type = Event::Type::MOUSE_BUTTON;
                e->mouse_button.button = MouseButton::RIGHT;
                e->mouse_button.down = false;
				e->mouse_button.x = LOWORD(msg.lParam);
				e->mouse_button.y = HIWORD(msg.lParam);
				break;
            case WM_MBUTTONDOWN:
				e->type = Event::Type::MOUSE_BUTTON;
                e->mouse_button.button = MouseButton::MIDDLE;
                e->mouse_button.down = true;
				e->mouse_button.x = LOWORD(msg.lParam);
				e->mouse_button.y = HIWORD(msg.lParam);
				break;
            case WM_MBUTTONUP:
				e->type = Event::Type::MOUSE_BUTTON;
                e->mouse_button.button = MouseButton::MIDDLE;
                e->mouse_button.down = false;
				e->mouse_button.x = LOWORD(msg.lParam);
				e->mouse_button.y = HIWORD(msg.lParam);
				break;
            default: known_msg = false; break;
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
        return known_msg;
    }
    return false;
}


void destroyWindow(WindowHandle window)
{
    DestroyWindow((HWND)window);
}


WindowHandle createWindow(const InitWindowArgs& args)
{
    WNDCLASS wc = {};

    auto WndProc = [](HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        App::Event e;
        e.window = hWnd;
        switch(Msg) {
            case WM_MOVE:
                e.type = Event::Type::WINDOW_MOVE;
                e.win_move.x = LOWORD(lParam);
                e.win_move.y = HIWORD(lParam);
                App::g_interface->onEvent(e);
                return 0;
            case WM_SIZE:
                e.type = Event::Type::WINDOW_SIZE;
                e.win_size.w = LOWORD(lParam);
                e.win_size.h = HIWORD(lParam);
                return 0;
            case WM_CLOSE: 
                e.type = Event::Type::WINDOW_CLOSE;
                App::g_interface->onEvent(e);
                return 0;
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

    const HWND hwnd = CreateWindow(wname
        , wname
        , WS_OVERLAPPEDWINDOW
        , CW_USEDEFAULT
        , CW_USEDEFAULT
        , CW_USEDEFAULT
        , CW_USEDEFAULT
        , NULL
        , NULL
        , wc.hInstance
        , NULL);

    if (args.handle_file_drops) {
        DragAcceptFiles(hwnd, TRUE);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}


void quit()
{
    g_finished = true;
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
	switch ((UINT)keycode) {
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
		case VK_NUMLOCK: 
			scancode |= 0x100;
			break;
	}

    WCHAR tmp[256];
    assert(size <= 256 && size > 0);
    int res = GetKeyNameText(scancode << 16, tmp, size);
    if(res == 0) {
		*out = 0;
	}
	else {
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
    return { rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top };
}


Point getWindowClientSize(WindowHandle win)
{
	RECT rect;
	GetClientRect((HWND)win, &rect);
	return { rect.right - rect.left, rect.bottom - rect.top };
}


void setWindowScreenRect(WindowHandle win, const Rect& rect)
{
    MoveWindow((HWND)win, rect.left, rect.top, rect.width, rect.height, TRUE);
}


void setMousePos(int x, int y)
{
	SetCursorPos(x, y);
}


Point getMousePos()
{
    POINT p;
    BOOL b = GetCursorPos(&p);
    assert(b);
    return {p.x, p.y};
}


Scancode toScancode(Keycode keycode)
{
    return MapVirtualKey((UINT)keycode, MAPVK_VK_TO_VSC);
}


Keycode toKeycode(Scancode scancode)
{
    return (Keycode)MapVirtualKey(scancode, MAPVK_VSC_TO_VK);
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


void run(Interface& iface) {
	POINT cp;
	GetCursorPos(&cp);
	g_last_mouse_pos.x = cp.x;
	g_last_mouse_pos.y = cp.y;
	g_interface = &iface;
    g_interface->onInit();
    while(!App::g_finished) {
        Event e;
        while (App::getEvent(&e)) {
            g_interface->onEvent(e);
        }
        g_interface->onIdle();
    }
}


int getDPI()
{
	// TODO
	return 96;
}


} // namespace Lumix::App
