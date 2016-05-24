#include "platform_interface.h"
#include "engine/iallocator.h"
#include "engine/string.h"
#include "imgui/imgui.h"
#include <cstdint>
#include <X11/Xlib.h>


namespace PlatformInterface
{

	struct PlatformData
	{
		PlatformData()
		{
			is_quit_requested = false;
			for (int i = 0; i < Lumix::lengthOf(key_map); ++i)
			{
				key_map[i] = -1;
				system_key_map[i] = -1;
			}
/*
			m_key_map[(int)Keys::ALT] = VK_MENU;
			m_key_map[(int)Keys::CONTROL] = VK_CONTROL;
			m_key_map[(int)Keys::SHIFT] = VK_SHIFT;
			m_key_map[(int)Keys::TAB] = VK_TAB;
			m_key_map[(int)Keys::LEFT] = VK_LEFT;
			m_key_map[(int)Keys::RIGHT] = VK_RIGHT;
			m_key_map[(int)Keys::UP] = VK_UP;
			m_key_map[(int)Keys::DOWN] = VK_DOWN;
			m_key_map[(int)Keys::PAGE_UP] = VK_PRIOR;
			m_key_map[(int)Keys::PAGE_DOWN] = VK_NEXT;
			m_key_map[(int)Keys::HOME] = VK_HOME;
			m_key_map[(int)Keys::END] = VK_END;
			m_key_map[(int)Keys::DEL] = VK_DELETE;
			m_key_map[(int)Keys::BACKSPACE] = VK_BACK;
			m_key_map[(int)Keys::ENTER] = VK_RETURN;
			m_key_map[(int)Keys::ESCAPE] = VK_ESCAPE;
*/
			for (int i = 0; i < Lumix::lengthOf(key_map); ++i)
			{
				if (key_map[i] != -1) system_key_map[key_map[i]] = i;
			}
		}

		Window window_handle;
		Display* display;
		bool is_mouse_tracked;
		bool is_quit_requested;
		SystemEventHandler* handler;
		int key_map[512];
		int system_key_map[512];
	};


	struct FileIterator
	{
/*		HANDLE handle;
		Lumix::IAllocator* allocator;
		WIN32_FIND_DATAA ffd;
		bool is_valid;*/
	};


	FileIterator* createFileIterator(const char* path, Lumix::IAllocator& allocator)
	{
		//char tmp[Lumix::MAX_PATH_LENGTH];
		//Lumix::copyString(tmp, path);
		//Lumix::catString(tmp, "/*");
		auto* iter = LUMIX_NEW(allocator, FileIterator);
		//iter->allocator = &allocator;
		//iter->handle = FindFirstFile(tmp, &iter->ffd);
		//iter->is_valid = iter->handle != NULL;
		return iter;
	}


	void destroyFileIterator(FileIterator* iterator)
	{
	/*	FindClose(iterator->handle);
		LUMIX_DELETE(*iterator->allocator, iterator);*/
	}


	bool getNextFile(FileIterator* iterator, FileInfo* info)
	{
		/*if (!iterator->is_valid) return false;

		info->is_directory = (iterator->ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		Lumix::copyString(info->filename, iterator->ffd.cFileName);

		iterator->is_valid = FindNextFile(iterator->handle, &iterator->ffd) == TRUE;
		return true;*/
		return false;
	}


	static PlatformData g_platform_data;


	static int getSystemKey(int key)
	{
		if (g_platform_data.key_map[key] != -1)
		{
			return g_platform_data.key_map[key];
		}
		else
		{
			return key;
		}
	}


	static int getKeyFromSystem(int key)
	{
		if (g_platform_data.system_key_map[key] != -1)
		{
			return g_platform_data.system_key_map[key];
		}
		else
		{
			return key;
		}
	}


	static void trackMouse()
	{
		/*TRACKMOUSEEVENT track_event;
		track_event.cbSize = sizeof(TRACKMOUSEEVENT);
		track_event.dwFlags = TME_LEAVE;
		track_event.hwndTrack = g_platform_data.m_hwnd;
		g_platform_data.m_is_mouse_tracked = TrackMouseEvent(&track_event) == TRUE;*/
	}


	void getCurrentDirectory(char* buffer, int buffer_size)
	{
		//GetCurrentDirectory(buffer_size, buffer);
	}


	void shutdown()
	{
		//HINSTANCE hInst = GetModuleHandle(NULL);
		//UnregisterClassA("lmxa", hInst);
	}


	void moveWindow(int x, int y, int w, int h)
	{
		//MoveWindow(g_platform_data.m_hwnd, x, y, w, h, FALSE);
	}


	bool isMaximized()
	{
		/*WINDOWPLACEMENT wndpl;
		wndpl.length = sizeof(wndpl);
		if (GetWindowPlacement(g_platform_data.m_hwnd, &wndpl))
		{
			return wndpl.showCmd == SW_MAXIMIZE;
		}*/
		return false;
	}


	void maximizeWindow()
	{
	}


	bool isWindowActive()
	{
		//return GetActiveWindow() == g_platform_data.m_hwnd;
		return false;
	}


	void clipCursor(float min_x, float min_y, float max_x, float max_y)
	{
		/*POINT min;
		POINT max;
		min.x = LONG(min_x);
		min.y = LONG(min_y);
		max.x = LONG(max_x);
		max.y = LONG(max_y);
		ClientToScreen(g_platform_data.m_hwnd, &min);
		ClientToScreen(g_platform_data.m_hwnd, &max);
		RECT rect;
		rect.left = min.x;
		rect.right = max.x;
		rect.top = min.y;
		rect.bottom = max.y;
		ClipCursor(&rect);*/
	}


	void showCursor(bool show)
	{
		/*if (show)
		{
			while (ShowCursor(true) < 0);
		}
		else
		{
			while (ShowCursor(false) >= 0);
		}*/
	}


	void unclipCursor()
	{
		//ClipCursor(NULL);
	}


	int getWindowX()
	{
		/*RECT rect;
		GetClientRect(g_platform_data.m_hwnd, &rect);
		return int(rect.left);*/
		return 0;
	}


	int getWindowY()
	{
		/*RECT rect;
		GetClientRect(g_platform_data.m_hwnd, &rect);
		return int(rect.top);*/
		return 0;
	}


	int getWindowWidth()
	{
		/*RECT rect;
		GetClientRect(g_platform_data.m_hwnd, &rect);
		return int(rect.right - rect.left);*/
		return 0;
	}


	int getWindowHeight()
	{
		/*RECT rect;
		GetClientRect(g_platform_data.m_hwnd, &rect);
		return int(rect.bottom - rect.top);*/
		return 0;
	}

/*
	void handleRawInput(LPARAM lParam)
	{
		UINT dwSize;
		char data[sizeof(RAWINPUT) * 10];

		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
		if (dwSize > sizeof(data)) return;

		if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, data, &dwSize, sizeof(RAWINPUTHEADER)) !=
			dwSize) return;

		RAWINPUT* raw = (RAWINPUT*)data;
		if (raw->header.dwType == RIM_TYPEMOUSE &&
			raw->data.mouse.usFlags == MOUSE_MOVE_RELATIVE)
		{
			POINT p;
			GetCursorPos(&p);
			ScreenToClient(g_platform_data.m_hwnd, &p);

			g_platform_data.m_handler->onMouseMove(
				int(p.x), int(p.y), int(raw->data.mouse.lLastX), int(raw->data.mouse.lLastY));
		}
	}
*/

	void clearQuitRequest()
	{
		//g_platform_data.m_is_quit_requested = false;
	}


	bool isQuitRequested()
	{
		//return g_platform_data.m_is_quit_requested;
		return false;
	}

/*
	static LRESULT WINAPI msgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (!g_platform_data.m_handler) return DefWindowProc(hWnd, msg, wParam, lParam);

		switch (msg)
		{
			case WM_LBUTTONUP:
				g_platform_data.m_handler->onMouseButtonUp(SystemEventHandler::MouseButton::LEFT);
				break;
			case WM_LBUTTONDOWN:
				g_platform_data.m_handler->onMouseButtonDown(SystemEventHandler::MouseButton::LEFT);
				break;
			case WM_RBUTTONDOWN:
				g_platform_data.m_handler->onMouseButtonDown(SystemEventHandler::MouseButton::RIGHT);
				break;
			case WM_RBUTTONUP:
				g_platform_data.m_handler->onMouseButtonUp(SystemEventHandler::MouseButton::RIGHT);
				break;
			case WM_MBUTTONUP:
				g_platform_data.m_handler->onMouseButtonUp(SystemEventHandler::MouseButton::MIDDLE);
				break;
			case WM_MBUTTONDOWN:
				g_platform_data.m_handler->onMouseButtonDown(SystemEventHandler::MouseButton::MIDDLE);
				break;
			case WM_MOUSEWHEEL:
				g_platform_data.m_handler->onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
				break;
			case WM_INPUT: handleRawInput(lParam); break;
			case WM_MOUSEMOVE:
			{
				if (!g_platform_data.m_is_mouse_tracked) trackMouse();
			}
			break;
			case WM_ERASEBKGND: return 1;
			case WM_MOVE:
			case WM_SIZE:
			{
				RECT rect;
				RECT screen_rect;
				GetWindowRect(g_platform_data.m_hwnd, &screen_rect);
				GetClientRect(g_platform_data.m_hwnd, &rect);
				g_platform_data.m_handler->onWindowTransformed(
					screen_rect.left, screen_rect.top, rect.right - rect.left, rect.bottom - rect.top);
			}
			break;
			case WM_CLOSE: g_platform_data.m_is_quit_requested = true; return true;
			case WM_MOUSELEAVE:
			{
				g_platform_data.m_is_mouse_tracked = false;
				g_platform_data.m_handler->onMouseLeftWindow();
			}
			break;
			case WM_SYSCOMMAND:
			{
				bool is_alt_key_menu = wParam == SC_KEYMENU && (lParam >> 16) <= 0;
				if (is_alt_key_menu) return 0;
				break;
			}
			case WM_KEYUP:
			case WM_SYSKEYUP: g_platform_data.m_handler->onKeyUp(getKeyFromSystem((int)wParam)); break;
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN: g_platform_data.m_handler->onKeyDown(getKeyFromSystem((int)wParam)); break;
			case WM_CHAR: g_platform_data.m_handler->onChar((int)wParam); break;
		}

		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
*/

	void getKeyName(int key, char* out, int max_size)
	{
		/*int virtualKey = getSystemKey(key);
		unsigned int scanCode = MapVirtualKey(virtualKey, MAPVK_VK_TO_VSC);

		// because MapVirtualKey strips the extended bit for some keys
		switch (virtualKey)
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
		case VK_NUMLOCK:
		{
			scanCode |= 0x100;
			break;
		}
		}

		GetKeyNameText(scanCode << 16, out, max_size);*/
	}

	
	bool processSystemEvents()
	{
		bool want_quit = false;
		XEvent e;
		while (XPending(g_platform_data.display) > 0)
		{
			XNextEvent(g_platform_data.display, &e);
			if (e.type == KeyPress) want_quit = true;
		}
		return !want_quit;
	}


	void setSystemEventHandler(SystemEventHandler* handler)
	{
		g_platform_data.handler = handler;
	}

	
	Lumix::Engine::PlatformData getPlatformData()
	{
		Lumix::Engine::PlatformData ret = {};
		ret.window_handle = (void*)(uintptr_t)g_platform_data.window_handle;
		ret.display = g_platform_data.display;
		return ret;
	}
	

	void* getWindowHandle()
	{
		return (void*)(uintptr_t)g_platform_data.window_handle;
	}


	void createWindow(SystemEventHandler* handler)
	{
		Display* display = XOpenDisplay(nullptr);
		if (!display) return;
		
		int s = DefaultScreen(display);
		Window window = XCreateSimpleWindow(display, RootWindow(display, s), 10, 10, 100, 100, 1
			, BlackPixel(display, s), WhitePixel(display, s));
		XSelectInput(display, window, ExposureMask | KeyPressMask);
		XMapWindow(display, window);
		g_platform_data.display = display;
		g_platform_data.window_handle = window;
	}


	void setWindowTitle(const char* title)
	{
		//SetWindowTextA(g_platform_data.m_hwnd, title);
	}

	
	bool isPressed(int key)
	{
		//return (GetKeyState(getSystemKey(key)) & 0x8000) != 0;
		return false;
	}


	void setCursor(Cursor cursor)
	{
		/*switch (cursor)
		{
			case Cursor::NONE:
				SetCursor(NULL);
				break;
			default:
				SetCursor(LoadCursor(NULL, IDC_ARROW));
				break;
		}*/
	}

	
	struct Process
	{
		/*explicit Process(Lumix::IAllocator& allocator)
			: allocator(allocator)
		{
		}

		PROCESS_INFORMATION process_info;
		HANDLE output_read_pipe;
		HANDLE output_write_pipe;
		Lumix::IAllocator& allocator;*/
	};


	bool isProcessFinished(Process& process)
	{
		/*DWORD exit_code;
		if (GetExitCodeProcess(process.process_info.hProcess, &exit_code) == FALSE) return true;
		return exit_code != STILL_ACTIVE;*/
		return false;
	}


	int getProcessExitCode(Process& process)
	{
		/*DWORD exit_code;
		if (GetExitCodeProcess(process.process_info.hProcess, &exit_code) == FALSE) return -1;
		return (int)exit_code;*/
		return 0;
	}


	void destroyProcess(Process& process)
	{
		/*CloseHandle(process.output_read_pipe);
		CloseHandle(process.process_info.hProcess);
		CloseHandle(process.process_info.hThread);
		LUMIX_DELETE(process.allocator, &process);*/
	}


	Process* createProcess(const char* cmd, const char* args, Lumix::IAllocator& allocator)
	{
		/*auto* process = LUMIX_NEW(allocator, Process)(allocator);

		SECURITY_ATTRIBUTES sec_attrs;
		sec_attrs.nLength = sizeof(SECURITY_ATTRIBUTES);
		sec_attrs.bInheritHandle = TRUE;
		sec_attrs.lpSecurityDescriptor = NULL;
		if (CreatePipe(&process->output_read_pipe, &process->output_write_pipe, &sec_attrs, 0) ==
			FALSE)
		{
			LUMIX_DELETE(allocator, process);
			return nullptr;
		}
		if (SetHandleInformation(process->output_read_pipe, HANDLE_FLAG_INHERIT, 0) == FALSE)
		{
			LUMIX_DELETE(allocator, process);
			return nullptr;
		}

		STARTUPINFO suinfo;
		ZeroMemory(&suinfo, sizeof(suinfo));
		suinfo.cb = sizeof(suinfo);
		suinfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
		suinfo.wShowWindow = SW_HIDE;
		suinfo.hStdOutput = process->output_write_pipe;
		suinfo.hStdError = process->output_write_pipe;

		char rw_args[1024];
		Lumix::copyString(rw_args, args);
		auto create_process_ret = CreateProcess(
			cmd,
			rw_args,
			NULL,
			NULL,
			TRUE,
			NORMAL_PRIORITY_CLASS,
			NULL,
			NULL,
			&suinfo,
			&process->process_info);

		if (create_process_ret == FALSE)
		{
			LUMIX_DELETE(allocator, process);
			return nullptr;
		}

		CloseHandle(process->output_write_pipe);

		return process;*/
		return nullptr;
	}


	int getProcessOutput(Process& process, char* buf, int buf_size)
	{
		/*DWORD read;
		if (ReadFile(process.output_read_pipe, buf, buf_size, &read, NULL) == FALSE) return -1;
		return read;*/
		return -1;
	}




	bool getSaveFilename(char* out, int max_size, const char* filter, const char* default_extension)
	{
		/*OPENFILENAME ofn;
		ZeroMemory(&ofn, sizeof(ofn));
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = NULL;
		ofn.lpstrFile = out;
		ofn.lpstrFile[0] = '\0';
		ofn.nMaxFile = max_size;
		ofn.lpstrFilter = filter;
		ofn.nFilterIndex = 1;
		ofn.lpstrDefExt = default_extension;
		ofn.lpstrFileTitle = NULL;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrInitialDir = NULL;
		ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_NONETWORKBUTTON;

		return GetSaveFileName(&ofn) == TRUE;*/
		return false;
	}


	bool getOpenFilename(char* out, int max_size, const char* filter, const char* starting_file)
	{
		/*OPENFILENAME ofn;
		ZeroMemory(&ofn, sizeof(ofn));
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = NULL;
		if (starting_file)
		{
			char* to = out;
			for (const char* from = starting_file; *from; ++from, ++to)
			{
				if (to - out > max_size - 1) break;
				*to = *to == '/' ? '\\' : *from;
			}
			*to = '\0';
		}
		else
		{
			out[0] = '\0';
		}
		ofn.lpstrFile = out;
		ofn.nMaxFile = max_size;
		ofn.lpstrFilter = filter;
		ofn.nFilterIndex = 1;
		ofn.lpstrFileTitle = NULL;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrInitialDir = nullptr;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_NONETWORKBUTTON;

		return GetOpenFileName(&ofn) == TRUE;*/
		return false;
	}


	bool getOpenDirectory(char* out, int max_size, const char* starting_dir)
	{
		/*bool ret = false;
		IFileDialog *pfd;
		if (SUCCEEDED(CoCreateInstance(
			CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
		{
			if (starting_dir)
			{
				PIDLIST_ABSOLUTE pidl;
				WCHAR wstarting_dir[MAX_PATH];
				WCHAR* wc = wstarting_dir;
				for (const char* c = starting_dir; *c && wc - wstarting_dir < MAX_PATH - 1; ++c, ++wc)
				{
					*wc = *c == '/' ? '\\' : *c;
				}
				*wc = 0;

				HRESULT hresult = ::SHParseDisplayName(wstarting_dir, 0, &pidl, SFGAO_FOLDER, 0);
				if (SUCCEEDED(hresult))
				{
					IShellItem *psi;
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
						char* c = out;
						while (*tmp && c - out < max_size - 1)
						{
							*c = (char)*tmp;
							++c;
							++tmp;
						}
						*c = '\0';
						ret = true;
					}
					psi->Release();
				}
			}
			pfd->Release();
		}
		return ret;*/
		return false;
	}


	bool shellExecuteOpen(const char* path)
	{
		//return (uintptr_t)ShellExecute(NULL, NULL, path, NULL, NULL, SW_SHOW) > 32;
		return false;
	}


	bool deleteFile(const char* path)
	{
		//return DeleteFile(path) == TRUE;
		return false;
	}


	bool moveFile(const char* from, const char* to)
	{
		//return MoveFile(from, to) == TRUE;
		return false;
	}


	bool copyFile(const char* from, const char* to)
	{
		//return CopyFile(from, to, FALSE) == TRUE;
		return false;
	}


	size_t getFileSize(const char* path)
	{
		/*WIN32_FILE_ATTRIBUTE_DATA fad;
		if(!GetFileAttributesEx(path, GetFileExInfoStandard, &fad))	return -1; 
		LARGE_INTEGER size;
		size.HighPart = fad.nFileSizeHigh;
		size.LowPart = fad.nFileSizeLow;
		return (size_t)size.QuadPart;*/
		return 0;
	}


	bool fileExists(const char* path)
	{
		/*DWORD dwAttrib = GetFileAttributes(path);
		return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
			!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));*/
		return false;
	}


	bool dirExists(const char* path)
	{
		/*DWORD dwAttrib = GetFileAttributes(path);
		return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
			(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));*/
		return false;
	}

	
	Lumix::uint64 getLastModified(const char* file)
	{
		/*FILETIME ft;
		HANDLE handle = CreateFile(file,
			GENERIC_READ,
			0,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL);
		if (GetFileTime(handle, NULL, NULL, &ft) == FALSE)
		{
			return 0;
		}
		CloseHandle(handle);

		ULARGE_INTEGER i;
		i.LowPart = ft.dwLowDateTime;
		i.HighPart = ft.dwHighDateTime;
		return i.QuadPart;*/
		return 0;
	}


	bool makePath(const char* path)
	{
		//return SHCreateDirectoryEx(NULL, path, NULL) == ERROR_SUCCESS;
		return false;
	}



} // namespace PlatformInterface

