#include "platform_interface.h"
#include "engine/iallocator.h"
#include "engine/path_utils.h"
#include "engine/string.h"
#include "imgui/imgui.h"

#include <SDL.h>
#include <ShlObj.h>
#include <mmsystem.h>
#include <SDL_syswm.h>


namespace PlatformInterface
{

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
	iter->is_valid = iter->handle != INVALID_HANDLE_VALUE;
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

	iterator->is_valid = FindNextFile(iterator->handle, &iterator->ffd) != FALSE;
	return true;
}


void getCurrentDirectory(char* buffer, int buffer_size)
{
	GetCurrentDirectory(buffer_size, buffer);
}


struct Process
{
	explicit Process(Lumix::IAllocator& allocator)
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
	if (CreatePipe(&process->output_read_pipe, &process->output_write_pipe, &sec_attrs, 0) == FALSE)
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
	suinfo.hStdInput = INVALID_HANDLE_VALUE;

	char rw_args[1024];
	Lumix::copyString(rw_args, args);
	auto create_process_ret = CreateProcess(
		cmd, rw_args, NULL, NULL, TRUE, NORMAL_PRIORITY_CLASS, NULL, NULL, &suinfo, &process->process_info);

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
	char tmp[Lumix::MAX_PATH_LENGTH];
	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = tmp;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = Lumix::lengthOf(tmp);
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrDefExt = default_extension;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_NONETWORKBUTTON;

	bool res = GetSaveFileName(&ofn) != FALSE;
	if (res) Lumix::PathUtils::normalize(tmp, out, max_size);
	return res;
}


bool getOpenFilename(char* out, int max_size, const char* filter, const char* starting_file)
{
	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	if (starting_file)
	{
		char* to = out;
		for (const char *from = starting_file; *from; ++from, ++to)
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

	return GetOpenFileName(&ofn) != FALSE;
}


bool getOpenDirectory(char* out, int max_size, const char* starting_dir)
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


void copyToClipboard(const char* text)
{
	if (!OpenClipboard(NULL)) return;
	int len = Lumix::stringLength(text);
	HGLOBAL mem_handle = GlobalAlloc(GMEM_MOVEABLE, len * sizeof(char));
	if (!mem_handle) return;

	char* mem = (char*)GlobalLock(mem_handle);
	Lumix::copyString(mem, len, text);
	GlobalUnlock(mem_handle);
	EmptyClipboard();
	SetClipboardData(CF_TEXT, mem_handle);
	CloseClipboard();
}


bool shellExecuteOpen(const char* path)
{
	return (uintptr_t)ShellExecute(NULL, NULL, path, NULL, NULL, SW_SHOW) > 32;
}


bool deleteFile(const char* path)
{
	return DeleteFile(path) != FALSE;
}


bool moveFile(const char* from, const char* to)
{
	return MoveFile(from, to) != FALSE;
}


size_t getFileSize(const char* path)
{
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (!GetFileAttributesEx(path, GetFileExInfoStandard, &fad)) return -1;
	LARGE_INTEGER size;
	size.HighPart = fad.nFileSizeHigh;
	size.LowPart = fad.nFileSizeLow;
	return (size_t)size.QuadPart;
}


bool fileExists(const char* path)
{
	DWORD dwAttrib = GetFileAttributes(path);
	return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}


bool dirExists(const char* path)
{
	DWORD dwAttrib = GetFileAttributes(path);
	return (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}


Lumix::u64 getLastModified(const char* file)
{
	FILETIME ft;
	HANDLE handle = CreateFile(file, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (GetFileTime(handle, NULL, NULL, &ft) == FALSE)
	{
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
	return SHCreateDirectoryEx(NULL, path, NULL) == ERROR_SUCCESS;
}


static HWND g_window = NULL;


void setWindow(SDL_Window* window)
{
	SDL_SysWMinfo window_info;
	SDL_VERSION(&window_info.version);
	SDL_GetWindowWMInfo(window, &window_info);
	Lumix::Engine::PlatformData platform_data = {};
	g_window = window_info.info.win.window;
}


void clipCursor(int x, int y, int w, int h)
{
	POINT min;
	POINT max;
	min.x = LONG(x);
	min.y = LONG(y);
	max.x = LONG(x + w);
	max.y = LONG(y + h);

	ClientToScreen(g_window, &min);
	ClientToScreen(g_window, &max);
	RECT rect;
	rect.left = min.x;
	rect.right = max.x;
	rect.top = min.y;
	rect.bottom = max.y;
	ClipCursor(&rect);
}


void unclipCursor()
{
	ClipCursor(NULL);
}


} // namespace PlatformInterface

