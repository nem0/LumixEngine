#include "platform_interface.h"
#include "core/iallocator.h"
#include "core/string.h"
#include "ocornut-imgui/imgui.h"
#include "renderer/renderer.h"


#include <ShlObj.h>
#include <Windows.h>
#include <mmsystem.h>


namespace PlatformInterface
{

	struct PlatformData
	{
		PlatformData()
		{
			for (int i = 0; i < Lumix::lengthOf(m_key_map); ++i)
			{
				m_key_map[i] = -1;
				m_system_key_map[i] = -1;
			}

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

			for (int i = 0; i < Lumix::lengthOf(m_key_map); ++i)
			{
				if (m_key_map[i] != -1) m_system_key_map[m_key_map[i]] = i;
			}
		}

		HWND m_hwnd;
		bool m_is_mouse_tracked;
		SystemEventHandler* m_handler;
		int m_key_map[512];
		int m_system_key_map[512];
	};


	struct FileIterator
	{
		HANDLE handle;
		Lumix::IAllocator* allocator;
		WIN32_FIND_DATAA ffd;
		bool is_valid;
	};


	FileIterator* createFileIterator(const char* path, Lumix::IAllocator& allocator)
	{
		char tmp[Lumix::MAX_PATH_LENGTH];
		Lumix::copyString(tmp, path);
		Lumix::catString(tmp, "/*");
		auto* iter = LUMIX_NEW(allocator, FileIterator);
		iter->allocator = &allocator;
		iter->handle = FindFirstFile(tmp, &iter->ffd);
		iter->is_valid = iter->handle != NULL;
		return iter;
	}


	void destroyFileIterator(FileIterator* iterator)
	{
		FindClose(iterator->handle);
		LUMIX_DELETE(*iterator->allocator, iterator);
	}


	bool getNextFile(FileIterator* iterator, FileInfo* info)
	{
		if (!iterator->is_valid) return false;

		info->is_directory = (iterator->ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		Lumix::copyString(info->filename, iterator->ffd.cFileName);

		iterator->is_valid = FindNextFile(iterator->handle, &iterator->ffd) == TRUE;
		return true;
	}


	static PlatformData g_platform_data;


	static int getSystemKey(int key)
	{
		if (g_platform_data.m_key_map[key] != -1)
		{
			return g_platform_data.m_key_map[key];
		}
		else
		{
			return key;
		}
	}


	static int getKeyFromSystem(int key)
	{
		if (g_platform_data.m_system_key_map[key] != -1)
		{
			return g_platform_data.m_system_key_map[key];
		}
		else
		{
			return key;
		}
	}


	static void trackMouse()
	{
		TRACKMOUSEEVENT track_event;
		track_event.cbSize = sizeof(TRACKMOUSEEVENT);
		track_event.dwFlags = TME_LEAVE;
		track_event.hwndTrack = g_platform_data.m_hwnd;
		g_platform_data.m_is_mouse_tracked = TrackMouseEvent(&track_event) == TRUE;
	}


	void getCurrentDirectory(char* buffer, int buffer_size)
	{
		GetCurrentDirectory(buffer_size, buffer);
	}


	void shutdown()
	{
		HINSTANCE hInst = GetModuleHandle(NULL);
		UnregisterClassA("lmxa", hInst);
	}


	void moveWindow(int x, int y, int w, int h)
	{
		MoveWindow(g_platform_data.m_hwnd, x, y, w, h, FALSE);
	}


	bool isMaximized()
	{
		WINDOWPLACEMENT wndpl;
		wndpl.length = sizeof(wndpl);
		if (GetWindowPlacement(g_platform_data.m_hwnd, &wndpl))
		{
			return wndpl.showCmd == SW_MAXIMIZE;
		}
		return false;
	}


	void maximizeWindow()
	{
		ShowWindow(g_platform_data.m_hwnd, SW_MAXIMIZE);
	}


	bool isWindowActive()
	{
		return GetActiveWindow() == g_platform_data.m_hwnd;
	}


	void clipCursor(float min_x, float min_y, float max_x, float max_y)
	{
		POINT min;
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
		ClipCursor(&rect);
	}


	void showCursor(bool show)
	{
		ShowCursor(show);
	}


	void unclipCursor()
	{
		ClipCursor(NULL);
	}


	int getWindowX()
	{
		RECT rect;
		GetClientRect(g_platform_data.m_hwnd, &rect);
		return int(rect.left);
	}


	int getWindowY()
	{
		RECT rect;
		GetClientRect(g_platform_data.m_hwnd, &rect);
		return int(rect.top);
	}


	int getWindowWidth()
	{
		RECT rect;
		GetClientRect(g_platform_data.m_hwnd, &rect);
		return int(rect.right - rect.left);
	}


	int getWindowHeight()
	{
		RECT rect;
		GetClientRect(g_platform_data.m_hwnd, &rect);
		return int(rect.bottom - rect.top);
	}


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
				GetClientRect(g_platform_data.m_hwnd, &rect);
				g_platform_data.m_handler->onWindowTransformed(
					rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
			}
			break;
			case WM_CLOSE: PostQuitMessage(0); break;
			case WM_MOUSELEAVE:
			{
				g_platform_data.m_is_mouse_tracked = false;
				g_platform_data.m_handler->onMouseLeftWindow();
			}
			break;
			case WM_KEYUP:
			case WM_SYSKEYUP: g_platform_data.m_handler->onKeyUp(getKeyFromSystem((int)wParam)); break;
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN: g_platform_data.m_handler->onKeyDown(getKeyFromSystem((int)wParam)); break;
			case WM_CHAR: g_platform_data.m_handler->onChar((int)wParam); break;
		}

		return DefWindowProc(hWnd, msg, wParam, lParam);
	}


	void getKeyName(int key, char* out, int max_size)
	{
		int virtualKey = getSystemKey(key);
		unsigned int scanCode = MapVirtualKey(virtualKey, MAPVK_VK_TO_VSC);

		// because MapVirtualKey strips the extended bit for some keys
		switch (virtualKey)
		{
		case VK_LEFT:
		case VK_UP:
		case VK_RIGHT:
		case VK_DOWN: // arrow keys
		case VK_PRIOR:
		case VK_NEXT: // page up and page down
		case VK_END:
		case VK_HOME:
		case VK_INSERT:
		case VK_DELETE:
		case VK_DIVIDE: // numpad slash
		case VK_NUMLOCK:
		{
			scanCode |= 0x100; // set extended bit
			break;
		}
		}

		GetKeyNameText(scanCode << 16, out, max_size);
	}


	bool processSystemEvents()
	{
		bool want_quit = false;
		MSG msg;
		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT)
			{
				want_quit = true;
			}
		}
		return !want_quit;
	}


	void setSystemEventHandler(SystemEventHandler* handler)
	{
		g_platform_data.m_handler = handler;
	}


	void createWindow(SystemEventHandler* handler)
	{
		g_platform_data.m_handler = handler;

		HINSTANCE hInst = GetModuleHandle(NULL);
		WNDCLASSEX wnd;
		memset(&wnd, 0, sizeof(wnd));
		wnd.cbSize = sizeof(wnd);
		wnd.style = CS_HREDRAW | CS_VREDRAW;
		wnd.lpfnWndProc = msgProc;
		wnd.hInstance = hInst;
		wnd.hIcon = LoadIcon(NULL, IDI_APPLICATION);
		wnd.hCursor = LoadCursor(NULL, IDC_ARROW);
		wnd.lpszClassName = "lmxa";
		wnd.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
		RegisterClassExA(&wnd);
		g_platform_data.m_hwnd = CreateWindowA(
			"lmxa", "lmxa", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 800, 600, NULL, NULL, hInst, 0);
		SetWindowTextA(g_platform_data.m_hwnd, "Lumix Studio");

		RAWINPUTDEVICE Rid;
		Rid.usUsagePage = 0x01;
		Rid.usUsage = 0x02;
		Rid.dwFlags = 0;
		Rid.hwndTarget = 0;
		RegisterRawInputDevices(&Rid, 1, sizeof(Rid));

		timeBeginPeriod(1);
		trackMouse();

		Lumix::Renderer::setInitData(g_platform_data.m_hwnd);
		ImGui::GetIO().ImeWindowHandle = g_platform_data.m_hwnd;
	}


	void setWindowTitle(const char* title)
	{
		SetWindowTextA(g_platform_data.m_hwnd, title);
	}


	bool isPressed(int key)
	{
		return (GetKeyState(getSystemKey(key)) & 0x8000) != 0;
	}


	void setCursor(Cursor cursor)
	{
		switch (cursor)
		{
			case Cursor::NONE:
				SetCursor(NULL);
				break;
			default:
				SetCursor(LoadCursor(NULL, IDC_ARROW));
				break;
		}
		
	}


	void* getWindowHandle()
	{
		return g_platform_data.m_hwnd;
	}


	struct Process
	{
		Process(Lumix::IAllocator& allocator)
			: allocator(allocator)
		{
		}

		PROCESS_INFORMATION process_info;
		HANDLE output_read_pipe;
		HANDLE output_write_pipe;
		Lumix::IAllocator& allocator;
	};


	bool isProcessFinished(Process& process)
	{
		DWORD exit_code;
		if (GetExitCodeProcess(process.process_info.hProcess, &exit_code) == FALSE) return true;
		return exit_code != STILL_ACTIVE;
	}


	int getProcessExitCode(Process& process)
	{
		DWORD exit_code;
		if (GetExitCodeProcess(process.process_info.hProcess, &exit_code) == FALSE) return -1;
		return (int)exit_code;
	}


	void destroyProcess(Process& process)
	{
		CloseHandle(process.output_read_pipe);
		CloseHandle(process.process_info.hProcess);
		CloseHandle(process.process_info.hThread);
		LUMIX_DELETE(process.allocator, &process);
	}


	Process* createProcess(const char* cmd, const char* args, Lumix::IAllocator& allocator)
	{
		auto* process = LUMIX_NEW(allocator, Process)(allocator);

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

		return process;
	}


	int getProcessOutput(Process& process, char* buf, int buf_size)
	{
		DWORD read;
		if (ReadFile(process.output_read_pipe, buf, buf_size, &read, NULL) == FALSE) return -1;
		return read;
	}




	bool getSaveFilename(char* out, int max_size, const char* filter, const char* default_extension)
	{
		OPENFILENAME ofn;
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

		return GetSaveFileName(&ofn) == TRUE;
	}


	bool getOpenFilename(char* out, int max_size, const char* filter)
	{
		OPENFILENAME ofn;
		ZeroMemory(&ofn, sizeof(ofn));
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = NULL;
		ofn.lpstrFile = out;
		ofn.lpstrFile[0] = '\0';
		ofn.nMaxFile = max_size;
		ofn.lpstrFilter = filter;
		ofn.nFilterIndex = 1;
		ofn.lpstrFileTitle = NULL;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrInitialDir = NULL;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_NONETWORKBUTTON;

		return GetOpenFileName(&ofn) == TRUE;
	}


	bool getOpenDirectory(char* out, int max_size)
	{
		bool ret = false;
		IFileDialog *pfd;
		if (SUCCEEDED(CoCreateInstance(
			CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
		{
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
		return ret;
	}


	bool shellExecuteOpen(const char* path)
	{
		return (int)ShellExecute(NULL, NULL, path, NULL, NULL, SW_SHOW) > 32;
	}


} // namespace PlatformInterface


INT WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, INT)
{
	int studioMain();
	return studioMain();
}
