#include "foundation/os.h"
#include "foundation/allocators.h"
#include "foundation/hash_map.h"
#include "foundation/log.h"
#include "foundation/foundation.h"
#include "foundation/math.h"
#include "foundation/path.h"
#include "foundation/queue.h"
#include "foundation/string.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xmd.h>
#include <X11/Xresource.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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
#define GLX_GLXEXT_LEGACY
#include <GL/glx.h>
#include <X11/extensions/XInput2.h>
#include <X11/cursorfont.h>
#include <X11/Xmd.h>
#include <gtk/gtk.h>


#define _NET_WM_STATE_REMOVE 0
#define _NET_WM_STATE_ADD 1

namespace Lumix::os {


static DefaultAllocator s_allocator;
static HashMap<KeySym, Keycode> s_from_x11_keysym(s_allocator);
static const char* s_keycode_names[256];

static struct {
	bool finished = false;
	Queue<Event, 128> event_queue;
	Point relative_mode_pos = {};
	bool relative_mouse = false;
	WindowHandle win = INVALID_WINDOW;
	Cursor arrow_cursor = None;
	Cursor size_ns_cursor = None;
	Cursor size_we_cursor = None;
	Cursor size_nwse_cursor = None;
	Cursor load_cursor = None;
	Cursor text_input_cursor = None;
	Cursor hidden_cursor = None;
	bool is_cursor_visible = true;

	int argc = 0;
	char** argv = nullptr;
	Display* display = nullptr;
	XIC ic;
	XIM im;
	IVec2 mouse_screen_pos = {}; // only valid if has_raw_inputs == false
	bool key_states[256] = {};
	Atom net_wm_state_fullscreen_atom;
	Atom net_wm_state_atom;
	Atom net_wm_state_hidden;
	Atom net_wm_state_maximized_vert_atom;
	Atom net_wm_state_maximized_horz_atom;
	Atom wm_protocols_atom;
	Atom wm_delete_window_atom;
	Atom clipboard_atom;
	int xinput_opcode = 0;
	bool has_raw_inputs = false;
	char* clipboard = nullptr;
} G;

bool getAppDataDir(Span<char> path) {
	char* home = getenv("HOME");
	if (!home) return false;
	copyString(path, home);
	catString(path, "/.lumix/");
	return true;
}

static Keycode getKeycode(KeySym keysym) {
	auto iter = s_from_x11_keysym.find(keysym);
	if (iter.isValid()) return iter.value();

	if ((u8)keysym >= 'a' && (u8)keysym <= 'z') return (Keycode)(keysym - 'a' + 'A');
	if ((u8)keysym >= 'A' && (u8)keysym <= 'Z' || (u8)keysym >= '0' && (u8)keysym <= '9') return (Keycode)keysym;

	return Keycode::INVALID;
}

void init() {
	static bool once = true;
	ASSERT(once);
	once = false;

	XInitThreads();
	G.display = XOpenDisplay(nullptr);
	G.im = XOpenIM(G.display, nullptr, nullptr, nullptr);

	struct {
		KeySym x11;
		Keycode lumix;
		const char* name;
	} map[] = {
		{XK_BackSpace, Keycode::BACKSPACE, "Backspace"},
		{XK_Tab, Keycode::TAB, "Tab"},
		{XK_Clear, Keycode::CLEAR, "Clear"},
		{XK_Return, Keycode::RETURN, "Return"},
		{XK_Shift_L, Keycode::SHIFT, "Shift"},
		{XK_Control_L, Keycode::CTRL, "Ctrl"},
		{XK_Menu, Keycode::ALT, "Menu"},
		{XK_Pause, Keycode::PAUSE, "Pause"},
		//{ XK_, Keycode::CAPITAL, "" },
		//{ XK_, Keycode::KANA, "" },
		//{ XK_, Keycode::HANGEUL, "" },
		//{ XK_, Keycode::HANGUL, "" },
		//{ XK_, Keycode::JUNJA, "" },
		//{ XK_, Keycode::FINAL, "" },
		//{ XK_, Keycode::HANJA, "" },
		//{ XK_, Keycode::KANJI, "" },
		{XK_Escape, Keycode::ESCAPE, "Escape"},
		//{ XK_, Keycode::CONVERT, "" },
		//{ XK_, Keycode::NONCONVERT, "" },
		//{ XK_, Keycode::ACCEPT, "" },
		//{ XK_m, Keycode::MODECHANGE, "" },
		{XK_space, Keycode::SPACE, "Space"},
		{XK_Page_Up, Keycode::PAGEUP, "Page Up"},
		{XK_Page_Down, Keycode::PAGEDOWN, "Page Down"},
		{XK_End, Keycode::END, "End"},
		{XK_Home, Keycode::HOME, "Home"},
		{XK_Left, Keycode::LEFT, "Left"},
		{XK_Up, Keycode::UP, "Up"},
		{XK_Right, Keycode::RIGHT, "Right"},
		{XK_Down, Keycode::DOWN, "Down"},
		{XK_Select, Keycode::SELECT, "Select"},
		{XK_Print, Keycode::PRINT, "Print"},
		{XK_Execute, Keycode::EXECUTE, "Execute"},
		//{ XK_, Keycode::SNAPSHOT, "" },
		{XK_Insert, Keycode::INSERT, "Insert"},
		{XK_Delete, Keycode::DEL, "Delete"},
		{XK_Help, Keycode::HELP, "Help"},
		//{ XK_, Keycode::LWIN, "" },
		//{ XK_, Keycode::RWIN, "" },
		//{ XK_, Keycode::APPS, "" },
		//{ XK_, Keycode::SLEEP, "" },
		{XK_KP_0, Keycode::NUMPAD0, "Numpad 0"},
		{XK_KP_1, Keycode::NUMPAD1, "Numpad 1"},
		{XK_KP_2, Keycode::NUMPAD2, "Numpad 2"},
		{XK_KP_3, Keycode::NUMPAD3, "Numpad 3"},
		{XK_KP_4, Keycode::NUMPAD4, "Numpad 4"},
		{XK_KP_5, Keycode::NUMPAD5, "Numpad 5"},
		{XK_KP_6, Keycode::NUMPAD6, "Numpad 6"},
		{XK_KP_7, Keycode::NUMPAD7, "Numpad 7"},
		{XK_KP_8, Keycode::NUMPAD8, "Numpad 8"},
		{XK_KP_9, Keycode::NUMPAD9, "Numpad 9"},
		{XK_multiply, Keycode::MULTIPLY, "*"},
		{XK_KP_Add, Keycode::ADD, "+"},
		{XK_KP_Separator, Keycode::SEPARATOR, "N/A"},
		{XK_KP_Subtract, Keycode::SUBTRACT, "-"},
		{XK_KP_Decimal, Keycode::DECIMAL, "."},
		{XK_KP_Divide, Keycode::DIVIDE, "/"},
		{XK_F1, Keycode::F1, "F1"},
		{XK_F2, Keycode::F2, "F2"},
		{XK_F3, Keycode::F3, "F3"},
		{XK_F4, Keycode::F4, "F4"},
		{XK_F5, Keycode::F5, "F5"},
		{XK_F6, Keycode::F6, "F6"},
		{XK_F7, Keycode::F7, "F7"},
		{XK_F8, Keycode::F8, "F8"},
		{XK_F9, Keycode::F9, "F9"},
		{XK_F10, Keycode::F10, "F10"},
		{XK_F11, Keycode::F11, "F11"},
		{XK_F12, Keycode::F12, "F12"},
		{XK_F13, Keycode::F13, "F13"},
		{XK_F14, Keycode::F14, "F14"},
		{XK_F15, Keycode::F15, "F15"},
		{XK_F16, Keycode::F16, "F16"},
		{XK_F17, Keycode::F17, "F17"},
		{XK_F18, Keycode::F18, "F18"},
		{XK_F19, Keycode::F19, "F19"},
		{XK_F20, Keycode::F20, "F20"},
		{XK_F21, Keycode::F21, "F21"},
		{XK_F22, Keycode::F22, "F22"},
		{XK_F23, Keycode::F23, "F23"},
		{XK_F24, Keycode::F24, "F24"},
		{XK_Num_Lock, Keycode::NUMLOCK, "Num lock"},
		{XK_Scroll_Lock, Keycode::SCROLL, "Scroll lock"},
		//{ XK_, Keycode::OEM_NEC_EQUAL, "" },
		//{ XK_, Keycode::OEM_FJ_JISHO, "" },
		//{ XK_, Keycode::OEM_FJ_MASSHOU, "" },
		//{ XK_, Keycode::OEM_FJ_TOUROKU, "" },
		//{ XK_, Keycode::OEM_FJ_LOYA, "" },
		//{ XK_, Keycode::OEM_FJ_ROYA, "" },
		{XK_Shift_L, Keycode::LSHIFT, "LShift"},
		{XK_Shift_R, Keycode::RSHIFT, "RShift"},
		{XK_Control_L, Keycode::LCTRL, "LCtrl"},
		{XK_Control_R, Keycode::RCTRL, "RCtrl"},
		//{ XK_, Keycode::LMENU, "" },
		//{ XK_, Keycode::RMENU, "" },
		//{ XK_, Keycode::BROWSER_BACK, "" },
		//{ XK_, Keycode::BROWSER_FORWARD, "" },
		//{ XK_, Keycode::BROWSER_REFRESH, "" },
		//{ XK_, Keycode::BROWSER_STOP, "" },
		//{ XK_, Keycode::BROWSER_SEARCH, "" },
		//{ XK_, Keycode::BROWSER_FAVORITE, ""S },
		//{ XK_, Keycode::BROWSER_HOME, "" },
		//{ XK_, Keycode::VOLUME_MUTE, "" },
		//{ XK_, Keycode::VOLUME_DOWN, "" },
		//{ XK_, Keycode::VOLUME_UP, "" },
		//{ XK_, Keycode::MEDIA_NEXT_TRACK, "" },
		//{ XK_, Keycode::MEDIA_PREV_TRACK, "" },
		//{ XK_, Keycode::MEDIA_STOP, "" },
		//{ XK_, Keycode::MEDIA_PLAY_PAUSE, "" },
		//{ XK_, Keycode::LAUNCH_MAIL, "" },
		//{ XK_, Keycode::LAUNCH_MEDIA_SEL, ""ECT },
		//{ XK_, Keycode::LAUNCH_APP1, "" },
		//{ XK_, Keycode::LAUNCH_APP2, "" },
		//{ XK_, Keycode::OEM_1, "" },
		//{ XK_, Keycode::OEM_PLUS, "" },
		//{ XK_, Keycode::OEM_COMMA, "" },
		//{ XK_, Keycode::OEM_MINUS, "" },
		//{ XK_, Keycode::OEM_PERIOD, "" },
		//{ XK_, Keycode::OEM_2, "" },
		//{ XK_, Keycode::OEM_3, "" },
		//{ XK_, Keycode::OEM_4, "" },
		//{ XK_, Keycode::OEM_5, "" },
		//{ XK_, Keycode::OEM_6, "" },
		//{ XK_, Keycode::OEM_7, "" },
		//{ XK_, Keycode::OEM_8, "" },
		//{ XK_, Keycode::OEM_AX, "" },
		//{ XK_, Keycode::OEM_102, "" },
		//{ XK_, Keycode::ICO_HELP, "" },
		//{ XK_, Keycode::ICO_00, "" },
		//{ XK_, Keycode::PROCESSKEY, "" },
		//{ XK_, Keycode::ICO_CLEAR, "" },
		//{ XK_, Keycode::PACKET, "" },
		//{ XK_, Keycode::OEM_RESET, "" },
		//{ XK_, Keycode::OEM_JUMP, "" },
		//{ XK_, Keycode::OEM_PA1, "" },
		//{ XK_, Keycode::OEM_PA2, "" },
		//{ XK_, Keycode::OEM_PA3, "" },
		//{ XK_, Keycode::OEM_WSCTRL, "" },
		//{ XK_, Keycode::OEM_CUSEL, "" },
		//{ XK_, Keycode::OEM_ATTN, "" },
		//{ XK_, Keycode::OEM_FINISH, "" },
		//{ XK_, Keycode::OEM_COPY, "" },
		//{ XK_, Keycode::OEM_AUTO, "" },
		//{ XK_, Keycode::OEM_ENLW, "" },
		//{ XK_, Keycode::OEM_BACKTAB, "" },
		//{ XK_, Keycode::ATTN, "" },
		//{ XK_, Keycode::CRSEL, "" },
		//{ XK_, Keycode::EXSEL, "" },
		//{ XK_, Keycode::EREOF, "" },
		//{ XK_, Keycode::PLAY, "" },
		//{ XK_, Keycode::ZOOM, "" },
		//{ XK_, Keycode::NONAME, "" },
		//{ XK_, Keycode::PA1, "" },
		//{ XK_, Keycode::OEM_CLEAR, "" },
		{XK_A, Keycode::A, "A"},
		{XK_C, Keycode::C, "B"},
		{XK_D, Keycode::D, "C"},
		{XK_K, Keycode::K, "D"},
		{XK_S, Keycode::S, "E"},
		{XK_V, Keycode::V, "F"},
		{XK_X, Keycode::X, "G"},
		{XK_Y, Keycode::Y, "H"},
		{XK_Z, Keycode::Z, "I"},
		{'a', Keycode::A, "A"},
		{'c', Keycode::C, "C"},
		{'d', Keycode::D, "D"},
		{'k', Keycode::K, "K"},
		{'s', Keycode::S, "S"},
		{'v', Keycode::V, "V"},
		{'x', Keycode::X, "X"},
		{'y', Keycode::Y, "Y"},
		{'z', Keycode::Z, "Z"},
	};

	for (const auto& m : map) {
		s_from_x11_keysym.insert(m.x11, m.lumix);
		s_keycode_names[(u8)m.lumix] = m.name;
	}
	G.net_wm_state_fullscreen_atom = XInternAtom(G.display, "_NET_WM_STATE_FULLSCREEN", False);
	G.net_wm_state_atom = XInternAtom(G.display, "_NET_WM_STATE", False);
	G.net_wm_state_hidden = XInternAtom(G.display, "_NET_WM_STATE_HIDDEN ", False);
	G.net_wm_state_maximized_horz_atom = XInternAtom(G.display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
	G.net_wm_state_maximized_vert_atom = XInternAtom(G.display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
	G.wm_protocols_atom = XInternAtom(G.display, "WM_PROTOCOLS", False);
	G.wm_delete_window_atom = XInternAtom(G.display, "WM_DELETE_WINDOW", False);
	G.clipboard_atom = XInternAtom(G.display, "CLIPBOARD", False);
	
	int first, error;
	if (XQueryExtension(G.display, "XInputExtension", &G.xinput_opcode, &first, &error) == False) {
		logError("Missing XInputExtension, mouse input will be broken.");
	}
	else {
		u8 mask_bytes[XIMaskLen(XI_RawMotion)] = {};
		XISetMask(mask_bytes, XI_RawMotion);

		XIEventMask mask;

		mask.deviceid = XIAllMasterDevices;
		mask.mask_len = sizeof(mask_bytes);
		mask.mask     = mask_bytes;

		Window root  = DefaultRootWindow(G.display);
		XISelectEvents(G.display, root, &mask, 1);
		G.has_raw_inputs = true;
	}
}

InputFile::InputFile() {
	m_handle = nullptr;
	static_assert(sizeof(m_handle) >= sizeof(FILE*), "");
}


OutputFile::OutputFile() {
	m_is_error = false;
	m_handle = nullptr;
	static_assert(sizeof(m_handle) >= sizeof(FILE*), "");
}


InputFile::~InputFile() {
	ASSERT(!m_handle);
}


OutputFile::~OutputFile() {
	ASSERT(!m_handle);
}


bool OutputFile::open(const char* path) {
	m_handle = fopen(path, "wb");
	m_is_error = !m_handle;
	return !m_is_error;
}


bool InputFile::open(const char* path) {
	m_handle = fopen(path, "rb");
	return m_handle;
}


void OutputFile::flush() {
	ASSERT(m_handle);
	fflush((FILE*)m_handle);
}


void OutputFile::close() {
	if (m_handle) {
		fclose((FILE*)m_handle);
		m_handle = nullptr;
	}
}


void InputFile::close() {
	if (m_handle) {
		fclose((FILE*)m_handle);
		m_handle = nullptr;
	}
}


bool OutputFile::write(const void* data, u64 size) {
	ASSERT(m_handle);
	const size_t written = fwrite(data, size, 1, (FILE*)m_handle);
	return written == 1;
}

bool InputFile::read(void* data, u64 size) {
	ASSERT(nullptr != m_handle);
	size_t read = fread(data, size, 1, (FILE*)m_handle);
	return read == 1;
}

u64 InputFile::size() const {
	ASSERT(nullptr != m_handle);
	long pos = ftell((FILE*)m_handle);
	fseek((FILE*)m_handle, 0, SEEK_END);
	size_t size = (size_t)ftell((FILE*)m_handle);
	fseek((FILE*)m_handle, pos, SEEK_SET);
	return size;
}


u64 InputFile::pos() {
	ASSERT(nullptr != m_handle);
	long pos = ftell((FILE*)m_handle);
	return (size_t)pos;
}


bool InputFile::seek(u64 pos) {
	ASSERT(nullptr != m_handle);
	return fseek((FILE*)m_handle, pos, SEEK_SET) == 0;
}


u32 getCPUsCount() {
	return sysconf(_SC_NPROCESSORS_ONLN);
}
void sleep(u32 milliseconds) {
	if (milliseconds) usleep(useconds_t(milliseconds * 1000));
}
ThreadID getCurrentThreadID() {
	return pthread_self();
}

void logInfo() {
	struct utsname tmp;
	if (uname(&tmp) == 0) {
		Lumix::logInfo("sysname: ", tmp.sysname);
		Lumix::logInfo("nodename: ", tmp.nodename);
		Lumix::logInfo("release: ", tmp.release);
		Lumix::logInfo("version: ", tmp.version);
		Lumix::logInfo("machine: ", tmp.machine);
	} else {
		logWarning("uname failed");
	}
}


bool getDropFile(const Event& event, int idx, Span<char> out) {
	ASSERT(false); // not supported, processEvents does not generate the drop event
	return false;
}


int getDropFileCount(const Event& event) {
	ASSERT(false); // not supported, processEvents does not generate the drop event
	return 0;
}


void finishDrag(const Event& event) {
	ASSERT(false); // not supported, processEvents does not generate the drop event
}

static unsigned long get_window_property(Window win, Atom property, Atom type, unsigned char** value) {
	Atom actual_type;
	int format;
	unsigned long count, bytes_after;
	XGetWindowProperty(G.display, win, property, 0, LONG_MAX, False, type, &actual_type, &format, &count, &bytes_after, value);
	return count;
}

bool getEvent(Event& e) {
	if (!G.event_queue.empty()) {
		e = G.event_queue.front();
		G.event_queue.pop();
		return true;
	}

next:
	if (XPending(G.display) <= 0) return false;
	XEvent xevent;
	XNextEvent(G.display, &xevent);

	if (XFilterEvent(&xevent, None)) return false;

	if ((xevent.xcookie.type == GenericEvent) && (xevent.xcookie.extension == G.xinput_opcode)) {
		XGetEventData(G.display, &xevent.xcookie);
		bool handled = false;

		if (xevent.xcookie.evtype == XI_RawMotion) {
			XIRawEvent* re = (XIRawEvent*)xevent.xcookie.data;
			if (re->valuators.mask_len) {
				const double* values = re->raw_values;

				e.window = INVALID_WINDOW;
				e.type = Event::Type::MOUSE_MOVE;

				if (XIMaskIsSet(re->valuators.mask, 0)) {
					e.mouse_move.xrel = *values;
					values++;
				}

				if (XIMaskIsSet(re->valuators.mask, 1)) {
					e.mouse_move.yrel = *values;
				}

				handled = true;
			}
		}

		XFreeEventData(G.display, &xevent.xcookie);
		if(handled) return true;
	}

	switch (xevent.type) {
		default: goto next;
		case SelectionClear:
		case SelectionRequest:
			ASSERT(false); // TODO
			goto next;
		case KeyPress: {
			KeySym keysym;
			Status status = 0;
			u32 utf8 = 0;
			const int len = Xutf8LookupString(G.ic, &xevent.xkey, (char*)&utf8, sizeof(utf8), &keysym, &status);

			e.type = Event::Type::KEY;
			e.key.down = true;
			e.key.keycode = getKeycode(keysym);
			e.key.is_repeat = false;
			G.key_states[(u8)e.key.keycode] = true;

			switch (status) {
				case XLookupChars:
				case XLookupBoth:
					if (0 != len) {
						Event e2;
						e2.type = Event::Type::CHAR;
						e2.text_input.utf8 = utf8;
						G.event_queue.push(e2);
					}
					break;
				default: break;
			}
			return true;
		}
		case KeyRelease: {
			const KeySym keysym = XLookupKeysym(&xevent.xkey, 0);
			e.type = Event::Type::KEY;
			e.key.down = false;
			e.key.keycode = getKeycode(keysym);
			G.key_states[(u8)e.key.keycode] = false;
			return true;
		}
		case ButtonPress:
		case ButtonRelease:
			e.window = (WindowHandle)xevent.xbutton.window;
			if (xevent.xbutton.button <= Button3) {
				e.type = Event::Type::MOUSE_BUTTON;
				switch (xevent.xbutton.button) {
					case Button1: e.mouse_button.button = MouseButton::LEFT; break;
					case Button2: e.mouse_button.button = MouseButton::MIDDLE; break;
					case Button3: e.mouse_button.button = MouseButton::RIGHT; break;
					default: e.mouse_button.button = MouseButton::EXTENDED; break;
				}
				e.mouse_button.down = xevent.type == ButtonPress;
			} else {
				e.type = Event::Type::MOUSE_WHEEL;
				switch (xevent.xbutton.button) {
					case 4: e.mouse_wheel.amount = 1; break;
					case 5: e.mouse_wheel.amount = -1; break;
				}
			}
			return true;
		case ClientMessage:
			if (xevent.xclient.message_type == G.wm_protocols_atom) {
				const Atom protocol = xevent.xclient.data.l[0];
				if (protocol == G.wm_delete_window_atom) {
					e.window = (WindowHandle)xevent.xclient.window;
					e.type = Event::Type::WINDOW_CLOSE;
					return true;
				}
			}
			goto next;
		case ConfigureNotify: {
			e.window = (WindowHandle)xevent.xconfigure.window;

			e.type = Event::Type::WINDOW_SIZE;
			e.win_size.w = xevent.xconfigure.width;
			e.win_size.h = xevent.xconfigure.height;

			Event e2;
			e2.type = Event::Type::WINDOW_MOVE; //-V519
			e2.win_move.x = xevent.xconfigure.x;
			e2.win_move.y = xevent.xconfigure.y;
			G.event_queue.push(e2);
			return true;
		}
		case MotionNotify:
			if (G.has_raw_inputs) goto next;
			
			const IVec2 mp(xevent.xmotion.x, xevent.xmotion.y);
			const IVec2 rel = mp - G.mouse_screen_pos;
			G.mouse_screen_pos = mp;

			e.window = (WindowHandle)xevent.xmotion.window;
			e.type = Event::Type::MOUSE_MOVE;
			e.mouse_move.xrel = rel.x;
			e.mouse_move.yrel = rel.y;
			return true;
	}
	return false;
}


void destroyWindow(WindowHandle window) {
	XUnmapWindow(G.display, (Window)window);
	XDestroyWindow(G.display, (Window)window);
}


Point toScreen(WindowHandle win, int x, int y) {
	XWindowAttributes attrs;
	Point p;
	p.x = x;
	p.y = y;
	while (win != INVALID_WINDOW) {
		XGetWindowAttributes(G.display, (Window)win, &attrs);
		p.x += attrs.x;
		p.y += attrs.y;
		Window root, parent;
		Window* children;
		u32 children_count;
		XQueryTree(G.display, (Window)win, &root, &parent, &children, &children_count);
		win = (WindowHandle)parent;
	}

	return p;
}

WindowHandle createWindow(const InitWindowArgs& args) {
	ASSERT(G.display);

	Display* display = G.display;
	static i32 screen = DefaultScreen(display);
	static i32 depth = DefaultDepth(display, screen);
	static Window root = RootWindow(display, screen);
	static Visual* visual = DefaultVisual(display, screen);
	static XSetWindowAttributes attrs = []() {
		XSetWindowAttributes ret = {};
		ret.background_pixmap = 0;
		ret.border_pixel = 0;
		ret.event_mask = ButtonPressMask | ButtonReleaseMask | ExposureMask | KeyPressMask | KeyReleaseMask | PointerMotionMask | StructureNotifyMask;
		return ret;
	}();
	Window win = XCreateWindow(display, args.parent ? (Window)args.parent : root, 0, 0, 800, 600, 0, depth, InputOutput, visual, CWBorderPixel | CWEventMask, &attrs);
	XSetWindowAttributes attr = {};
	XChangeWindowAttributes(display, win, CWBackPixel, &attr);

	XMapWindow(display, win);
	XStoreName(display, win, args.name && args.name[0] ? args.name : "Lumix App");

	G.ic = XCreateIC(G.im, XNInputStyle, 0 | XIMPreeditNothing | XIMStatusNothing, XNClientWindow, win, NULL);

	Atom protocols = G.wm_delete_window_atom;
	XSetWMProtocols(G.display, (Window)win, &protocols, 1);

	// TODO glx context fails to create without this
	for (int i = 0; i < 100; ++i) {
		Event e;
		if (getEvent(e)) {
			G.event_queue.push(e);
		}
	}

	WindowHandle res = (WindowHandle)win;
	G.win = res;
	return res;
}


void quit() {
	G.finished = true;
}


bool isKeyDown(Keycode keycode) {
	return G.key_states[(u8)keycode];
}


void getKeyName(Keycode keycode, Span<char> out) {
	ASSERT(out.length() > 1);
	if ((u8)keycode >= 'a' && (u8)keycode <= 'z' || (u8)keycode >= 'A' && (u8)keycode <= 'Z' || (u8)keycode >= '0' && (u8)keycode <= '9') {
		out[0] = (char)keycode;
		out[1] = '\0';
		return;
	}
	const char* name = s_keycode_names[(u8)keycode];
	copyString(out, name ? name : keycode != Keycode::INVALID ? "N/A" : "");
}

static void initCursors() {
	if (G.arrow_cursor == None) G.arrow_cursor = XCreateFontCursor(G.display, 68);
	if (G.size_ns_cursor == None) G.size_ns_cursor = XCreateFontCursor(G.display, 116);
	if (G.size_we_cursor == None) G.size_we_cursor = XCreateFontCursor(G.display, 108);
	if (G.size_nwse_cursor == None) G.size_nwse_cursor = XCreateFontCursor(G.display, 52);
	if (G.load_cursor == None) G.load_cursor = XCreateFontCursor(G.display, 150);
	if (G.text_input_cursor == None) G.text_input_cursor = XCreateFontCursor(G.display, 152);
	if (G.hidden_cursor == None) {
		Pixmap cursorPixmap = XCreatePixmap(G.display, (Window)G.win, 1, 1, 1);
		GC graphicsContext = XCreateGC(G.display, cursorPixmap, 0, NULL);
		XDrawPoint(G.display, cursorPixmap, graphicsContext, 0, 0);
		XFreeGC(G.display, graphicsContext);
		XColor color;
		color.flags = DoRed | DoGreen | DoBlue;
		color.red = color.blue = color.green = 0;
		G.hidden_cursor = XCreatePixmapCursor(G.display, cursorPixmap, cursorPixmap, &color, &color, 0, 0);
		XFreePixmap(G.display, cursorPixmap);
	}
}

void setCursor(CursorType type) {
	initCursors();
	if (!G.is_cursor_visible) return;
	switch (type) {
		case CursorType::DEFAULT: XDefineCursor(G.display, (Window)G.win, G.arrow_cursor); break;
		case CursorType::SIZE_NS: XDefineCursor(G.display, (Window)G.win, G.size_ns_cursor); break;
		case CursorType::SIZE_WE: XDefineCursor(G.display, (Window)G.win, G.size_we_cursor); break;
		case CursorType::SIZE_NWSE: XDefineCursor(G.display, (Window)G.win, G.size_nwse_cursor); break;
		case CursorType::LOAD: XDefineCursor(G.display, (Window)G.win, G.load_cursor); break;
		case CursorType::TEXT_INPUT: XDefineCursor(G.display, (Window)G.win, G.text_input_cursor); break;
		default: ASSERT(false); break;
	}
}

void showCursor(bool show) {
	initCursors();

	G.is_cursor_visible = show;
	if (show) {
		XDefineCursor(G.display, (Window)G.win, G.arrow_cursor);
	}
	else {
		XDefineCursor(G.display, (Window)G.win, G.hidden_cursor);
	}
}


void setWindowTitle(WindowHandle win, const char* title) {
	XStoreName(G.display, (Window)win, title);
}


Rect getWindowScreenRect(WindowHandle win) {
	XWindowAttributes attrs;
	XGetWindowAttributes(G.display, (Window)win, &attrs);
	Rect r;
	r.left = attrs.x;
	r.top = attrs.y;
	r.width = attrs.width;
	r.height = attrs.height;

	Window dummy;
	XTranslateCoordinates(G.display, (Window)win, attrs.root, 0, 0, &r.left, &r.top, &dummy);
	return r;
}

Rect getWindowClientRect(WindowHandle win) {
	XWindowAttributes attrs;
	XGetWindowAttributes(G.display, (Window)win, &attrs);
	Rect r;
	r.left = 0;
	r.top = 0;
	r.width = attrs.width;
	r.height = attrs.height;
	return r;
}

void setWindowScreenRect(WindowHandle win, const Rect& rect) {
	XMoveResizeWindow(G.display, (Window)win, rect.left, rect.top, rect.width, rect.height);
}

u32 getMonitors(Span<Monitor> monitors) {
	ASSERT(monitors.length() > 0);

	const int count = minimum(ScreenCount(G.display), monitors.length());
	for (int i = 0; i < count; ++i) {
		Monitor& m = monitors[i];
		m.primary = true;
		m.work_rect.left = 0;
		m.work_rect.top = 0;
		Window root = RootWindow(G.display, i);
		XWindowAttributes attrs;
		XGetWindowAttributes(G.display, root, &attrs);
		m.work_rect.width = attrs.width;
		m.work_rect.height = attrs.height;
		m.monitor_rect = monitors[0].work_rect;
	}

	return count;
}

void setMouseScreenPos(int x, int y) {
	Window root = DefaultRootWindow(G.display);
	XWarpPointer(G.display, None, root, 0, 0, 0, 0, x, y);
}

Point getMousePos(WindowHandle win) {
	const Rect r = getWindowScreenRect(win);
	const Point mp = getMouseScreenPos();
	return {mp.x - r.left, mp.y - r.top};
}

Point getMouseScreenPos() {
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


WindowHandle getFocused() {
	Window win;
	int dummy;
	XGetInputFocus(G.display, &win, &dummy);
	return (WindowHandle)win;
}

bool isMinimized(WindowHandle win) {
	if (!G.net_wm_state_atom) return false;
	if (!G.net_wm_state_hidden) return false;

	Atom* states;
	const unsigned long count = get_window_property((Window)win, G.net_wm_state_atom, XA_ATOM, (unsigned char**)&states);

	bool minimized = false;
	for (unsigned long i = 0; i < count; ++i) {
		if (states[i] == G.net_wm_state_hidden) {
			minimized = true;
			break;
		}
	}

	XFree(states);

	return minimized;
}

bool isMaximized(WindowHandle win) {
	if (!G.net_wm_state_atom) return false;
	if (!G.net_wm_state_maximized_horz_atom) return false;
	if (!G.net_wm_state_maximized_vert_atom) return false;

	Atom* states;
	const unsigned long count = get_window_property((Window)win, G.net_wm_state_atom, XA_ATOM, (unsigned char**)&states);

	bool maximized = false;
	for (unsigned long i = 0; i < count; ++i) {
		if (states[i] == G.net_wm_state_maximized_horz_atom || states[i] == G.net_wm_state_maximized_vert_atom) {
			maximized = true;
			break;
		}
	}

	XFree(states);

	return maximized;
}

 void restore(WindowHandle win) {
	// TODO
	ASSERT(false);
 }

void restore(WindowHandle win, WindowState state) {
	XEvent event = {ClientMessage};
	event.xclient.window = (Window)win;
	event.xclient.format = 32;
	event.xclient.message_type = G.net_wm_state_atom;
	event.xclient.data.l[0] = _NET_WM_STATE_REMOVE;
	event.xclient.data.l[1] = G.net_wm_state_fullscreen_atom;
	event.xclient.data.l[2] = 0;
	event.xclient.data.l[3] = 0;
	event.xclient.data.l[4] = 0;

	XWindowAttributes attrs;
	XGetWindowAttributes(G.display, (Window)win, &attrs);
	Window root = RootWindowOfScreen(attrs.screen);
	XSendEvent(G.display, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &event);
}

WindowState setFullscreen(WindowHandle win) {
	XEvent event = {ClientMessage};
	event.xclient.window = (Window)win;
	event.xclient.format = 32;
	event.xclient.message_type = G.net_wm_state_atom;
	event.xclient.data.l[0] = _NET_WM_STATE_ADD;
	event.xclient.data.l[1] = G.net_wm_state_fullscreen_atom;
	event.xclient.data.l[2] = 0;
	event.xclient.data.l[3] = 0;
	event.xclient.data.l[4] = 0;

	XWindowAttributes attrs;
	XGetWindowAttributes(G.display, (Window)win, &attrs);
	Window root = RootWindowOfScreen(attrs.screen);
	XSendEvent(G.display, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &event);
	return {};
}

void minimizeWindow(WindowHandle win) {
	// TODO
	ASSERT(false);
}

void maximizeWindow(WindowHandle win) {
	XEvent event = {ClientMessage};
	event.xclient.window = (Window)win;
	event.xclient.format = 32;
	event.xclient.message_type = G.net_wm_state_atom;
	event.xclient.data.l[0] = _NET_WM_STATE_ADD;
	event.xclient.data.l[1] = G.net_wm_state_maximized_vert_atom;
	event.xclient.data.l[2] = G.net_wm_state_maximized_horz_atom;
	event.xclient.data.l[3] = 1;
	event.xclient.data.l[4] = 0;

	XWindowAttributes attrs;
	XGetWindowAttributes(G.display, (Window)win, &attrs);
	Window root = RootWindowOfScreen(attrs.screen);
	XSendEvent(G.display, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &event);
}


bool isRelativeMouseMode() {
	return G.relative_mouse;
}


int getDPI() {
	float dpi = DisplayWidth(G.display, 0) * 25.4f / DisplayWidthMM(G.display, 0);
	char* rms = XResourceManagerString(G.display);
	if (rms) {
		XrmDatabase db = XrmGetStringDatabase(rms);
		if (db) {
			XrmValue value;
			char* type;
			if (XrmGetResource(db, "Xft.dpi", "String", &type, &value) && value.addr) {
				dpi = atof(value.addr);
			}
			XrmDestroyDatabase(db);
		}
	}

	return int(dpi + 0.5f);
}

u32 getMemPageSize() {
	const u32 sz = sysconf(_SC_PAGESIZE);
	return sz;
}

u32 getMemPageAlignment() {
	return getMemPageSize();
}

void* memReserve(size_t size) {
	void* mem = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ASSERT(mem);
	return mem;
}

void memCommit(void* ptr, size_t size) {
	// noop on linux
}

void memRelease(void* ptr, size_t size) {
	munmap(ptr, size);
}

struct FileIterator {};

FileIterator* createFileIterator(StringView _path, IAllocator& allocator) {
	char path[MAX_PATH];
	copyString(path, _path);

	return (FileIterator*)opendir(path);
}


void destroyFileIterator(FileIterator* iterator) {
	closedir((DIR*)iterator);
}


bool getNextFile(FileIterator* iterator, FileInfo* info) {
	if (!iterator) return false;

	auto* dir = (DIR*)iterator;
	auto* dir_ent = readdir(dir);
	if (!dir_ent) return false;

	info->is_directory = dir_ent->d_type == DT_DIR;
	Lumix::copyString(info->filename, dir_ent->d_name);
	return true;
}


void setCurrentDirectory(const char* path) {
	auto res = chdir(path);
	(void)res;
}


void getCurrentDirectory(Span<char> output) {
	if (!getcwd(output.m_begin, output.length())) {
		output[0] = 0;
	}
}

static bool dialog(Span<char> out, const char* filter_str, const char* starting_file, bool is_dir, bool is_save) {
	gtk_init_check(NULL, NULL);
	GtkWidget* dialog = gtk_file_chooser_dialog_new(is_save	 ? "Save file"
													: is_dir ? "Select folder"
															 : "Open File",
		NULL,
		is_save	 ? GTK_FILE_CHOOSER_ACTION_SAVE
		: is_dir ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
				 : GTK_FILE_CHOOSER_ACTION_OPEN,
		"_Cancel",
		GTK_RESPONSE_CANCEL,
		is_save ? "_Save" : "_Open",
		GTK_RESPONSE_ACCEPT,
		NULL);
	GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);
	if (is_save) gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);

	const char* filters = filter_str;
	GtkFileFilter* filter;
	char default_ext[64] = "";
	while (filters && *filters) {
		filter = gtk_file_filter_new();
		gtk_file_filter_set_name(filter, filters);
		filters += strlen(filters) + 1;

		char buf[512];
		copyString(buf, filters);
		char* patterns;
		for (patterns = buf; *patterns; ++patterns) {
			if (*patterns == ';') {
				*patterns = '\0';
			}
			if (!default_ext[0]) copyString(default_ext, patterns[0] == '*' ? patterns + 1 : patterns);
		}

		patterns = buf;
		while (*patterns) {
			gtk_file_filter_add_pattern(filter, patterns);
			patterns += strlen(patterns) + 1;
		}

		gtk_file_chooser_add_filter(chooser, filter);
		filters += strlen(filters) + 1;
	}

	char* name = nullptr;

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		name = gtk_file_chooser_get_filename(chooser);
	}
	gtk_widget_destroy(dialog);

	while (gtk_events_pending()) gtk_main_iteration();

	if (name) {
		copyString(out, name);
		if (is_save) {
			StringView ext = Path::getExtension(name);
			if (ext.empty()) {
				catString(out, default_ext);
			}
		}
		free(name);
		return true;
	}
	return false;
} 

bool getSaveFilename(Span<char> out, const char* filter, const char* default_extension) {
	return dialog(out, filter, "", false, true);
}


bool getOpenFilename(Span<char> out, const char* filter_str, const char* starting_file) {
	return dialog(out, filter_str, starting_file, false, false);
}


bool getOpenDirectory(Span<char> output, const char* starting_dir) {
	return dialog(output, nullptr, starting_dir, true, false);
}


void copyToClipboard(const char* text) {
	ASSERT(text);
	if (!text) return;

	free(G.clipboard);
	G.clipboard = (char*)malloc(strlen(text) + 1);
	ASSERT(G.clipboard);
	if (!G.clipboard) return;

	strcpy(G.clipboard, text);
	XSetSelectionOwner(G.display, G.clipboard_atom, (Window)G.win, CurrentTime);
	ASSERT(XGetSelectionOwner(G.display, G.clipboard_atom) == (Window)G.win);
	// TODO finish
}


ExecuteOpenResult shellExecuteOpen(StringView path, StringView args, StringView working_dir) {
	ASSERT(args.empty()); // not supported 
	ASSERT(working_dir.empty());
	char tmp[MAX_PATH];
	copyString(tmp, path);
	return system(tmp) == 0 ? ExecuteOpenResult::SUCCESS : ExecuteOpenResult::OTHER_ERROR;
}


ExecuteOpenResult openExplorer(StringView _path) {
	char path[MAX_PATH];
	copyString(path, _path);
	
	StaticString<1024> tmp("xdg-open ", path);
	return system(tmp) == 0 ? ExecuteOpenResult::SUCCESS : ExecuteOpenResult::OTHER_ERROR;
}


bool deleteFile(StringView path) {
	char tmp[MAX_PATH];
	copyString(tmp, path);
	return unlink(tmp) == 0;
}


bool moveFile(StringView _from, StringView _to) {
	char from[MAX_PATH];
	copyString(from, _from);
	char to[MAX_PATH];
	copyString(to, _to);
	
	return rename(from, to) == 0;
}


size_t getFileSize(StringView path) {
	char path_tmp[MAX_PATH];
	copyString(path_tmp, path);
	struct stat tmp;
	stat(path_tmp, &tmp);
	return tmp.st_size;
}


bool fileExists(StringView _path) {
	char path[MAX_PATH];
	copyString(path, _path);
	struct stat tmp;
	return ((stat(path, &tmp) == 0) && (((tmp.st_mode) & S_IFMT) != S_IFDIR));
}


bool dirExists(StringView _path) {
	char path[MAX_PATH];
	copyString(path, _path);
	struct stat tmp;
	return ((stat(path, &tmp) == 0) && (((tmp.st_mode) & S_IFMT) == S_IFDIR));
}


u64 getLastModified(StringView _path) {
	char path[MAX_PATH];
	copyString(path, _path);

	struct stat tmp;
	Lumix::u64 ret = 0;
	if (stat(path, &tmp) != 0) return 0;
	ret = tmp.st_mtim.tv_sec * 1000 + Lumix::u64(tmp.st_mtim.tv_nsec / 1000000);
	return ret;
}


bool makePath(const char* path) {
	char tmp[MAX_PATH];
	const char* cin = path;
	char* cout = tmp;

	while (*cin) {
		*cout = *cin;
		if (*cout == '\\' || *cout == '/' || *cout == '\0') {
			if (cout != tmp) {
				*cout = '\0';
				mkdir(tmp, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
				*cout = *cin;
			}
		}
		++cout;
		++cin;
	}
	int res = mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	return res == 0 || (res == -1 && errno == EEXIST);
}


void clipCursor(WindowHandle window, const Rect& rect) {
	if (window == INVALID_WINDOW) {
		XUngrabPointer(G.display, CurrentTime);
	}
	else {
		const u32 mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask;
		XGrabPointer(G.display, (Window)window, True, mask, GrabModeAsync, GrabModeAsync, (Window)window, None, CurrentTime);
	}
}


bool copyFile(StringView from, StringView to) {
	char tmp[MAX_PATH];
	copyString(tmp, from);

	const int source = open(tmp, O_RDONLY, 0);
	if (source < 0) return false;
	
	copyString(tmp, to);
	const int dest = open(tmp, O_WRONLY | O_CREAT, 0644);
	if (dest < 1) {
		::close(source);
		return false;
	}

	char buf[BUFSIZ];
	size_t size;
	while ((size = ::read(source, buf, BUFSIZ)) > 0) {
		const ssize_t res = ::write(dest, buf, size); //-V512
		if (res == -1) {
			::close(source);
			::close(dest);
			return false;
		}
	}

	::close(source);
	::close(dest);
	return true;
}


void getExecutablePath(Span<char> buffer) {
	char self[PATH_MAX] = {};
	const int res = readlink("/proc/self/exe", self, sizeof(self));

	if (res < 0) {
		copyString(buffer, "");
		return;
	}
	copyString(buffer, self);
}


void messageBox(const char* text) {
	fprintf(stderr, "%s", text);
}


void setCommandLine(int argc, char** argv) {
	G.argc = argc;
	G.argv = argv;
}


bool getCommandLine(Span<char> output) {
	copyString(output, "");
	for (int i = 0; i < G.argc; ++i) {
		catString(output, G.argv[i]);
		catString(output, " ");
	}
	return true;
}


void* loadLibrary(const char* path) {
	return dlopen(path, RTLD_LOCAL | RTLD_LAZY);
}


void unloadLibrary(void* handle) {
	if (handle != NULL) dlclose(handle);
}


void* getLibrarySymbol(void* handle, const char* name) {
	return dlsym(handle, name);
}

float getTimeSinceProcessStart() { return -1; }

Timer::Timer() {
	last_tick = getRawTimestamp();
	first_tick = last_tick;
}


float Timer::getTimeSinceStart() const {
	return float(double(getRawTimestamp() - first_tick) / double(getFrequency()));
}


float Timer::getTimeSinceTick() const {
	return float(double(getRawTimestamp() - last_tick) / double(getFrequency()));
}


float Timer::tick() {
	const u64 now = getRawTimestamp();
	const float delta = float(double(now - last_tick) / double(getFrequency()));
	last_tick = now;
	return delta;
}


u64 Timer::getFrequency() {
	return 1'000'000'000;
}


u64 Timer::getRawTimestamp() {
	timespec tick;
	clock_gettime(CLOCK_REALTIME, &tick);
	return u64(tick.tv_sec) * 1000000000 + u64(tick.tv_nsec);
}

// network not implemented on linux
bool initNetwork() {
	ASSERT(false); 
	return false;
}

void shutdownNetwork() {}
struct NetworkStream* listen(const char* ip, u16 port, IAllocator& allocator) { ASSERT(false); return nullptr; }
NetworkStream* connect(const char* ip, u16 port, IAllocator& allocator) { ASSERT(false); return nullptr; }
bool read(NetworkStream& stream, void* mem, u32 size) { ASSERT(false); return false; }
bool write(NetworkStream& stream, const void* data, u32 size) { ASSERT(false); return false; }
void close(NetworkStream& stream) {}


} // namespace Lumix::os
