#include "system.h"
#include "core/iallocator.h"
#include <ShlObj.h>
#include <Windows.h>


namespace Lumix
{
	struct Process
	{
		Process(IAllocator& allocator)
			: allocator(allocator)
		{
		}

		PROCESS_INFORMATION process_info;
		IAllocator& allocator;
	};


	bool isProcessFinished(Process& process)
	{
		DWORD exit_code;
		if (GetExitCodeProcess(process.process_info.hProcess, &exit_code) == FALSE) return false;
		return exit_code != STILL_ACTIVE;
	}


	void destroyProcess(Process& process)
	{
		CloseHandle(process.process_info.hProcess);
		CloseHandle(process.process_info.hThread);
		process.allocator.deleteObject(&process);
	}


	Process* createProcess(const char* cmd, char* args, IAllocator& allocator)
	{
		auto* process = allocator.newObject<Process>(allocator);

		STARTUPINFO suinfo;
		ZeroMemory(&suinfo, sizeof(suinfo));
		suinfo.cb = sizeof(suinfo);
		suinfo.dwFlags = STARTF_USESHOWWINDOW;
		suinfo.wShowWindow = SW_HIDE;

		CreateProcess(cmd,
					  args,
					  NULL,
					  NULL,
					  TRUE,
					  NORMAL_PRIORITY_CLASS,
					  NULL,
					  NULL,
					  &suinfo,
					  &process->process_info);
		return process;
	}


	bool deleteFile(const char* path)
	{
		return DeleteFile(path) == TRUE;
	}


	bool moveFile(const char* from, const char* to)
	{
		return MoveFile(from, to) == TRUE;
	}


	bool copyFile(const char* from, const char* to)
	{
		return CopyFile(from, to, FALSE) == TRUE;
	}


	bool fileExists(const char* path)
	{
		DWORD dwAttrib = GetFileAttributes(path);
		return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
			!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
	}


	void messageBox(const char* text)
	{
		MessageBox(NULL, text, "Message", MB_OK);
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
		if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
		{
			DWORD dwOptions;
			if (SUCCEEDED(pfd->GetOptions(&dwOptions)))
			{
				pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);
			}
			if (SUCCEEDED(pfd->Show(NULL)))
			{
				IShellItem *psi;
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


	uint64_t getLastModified(const char* file)
	{
		FILETIME ft;
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
		return i.QuadPart;
	}


	bool makePath(const char* path)
	{
		return SHCreateDirectoryEx(NULL, path, NULL) == ERROR_SUCCESS;
	}

}