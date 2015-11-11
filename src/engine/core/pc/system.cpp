#include "core/system.h"
#include "core/iallocator.h"
#include "core/string.h"
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
		HANDLE output_read_pipe;
		HANDLE output_write_pipe;
		IAllocator& allocator;
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


	Process* createProcess(const char* cmd, const char* args, IAllocator& allocator)
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
		copyString(rw_args, args);
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


	bool dirExists(const char* path)
	{
		DWORD dwAttrib = GetFileAttributes(path);
		return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
			(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
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


	uint64 getLastModified(const char* file)
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


	bool getCommandLine(char* output, int max_size)
	{
		return copyString(output, max_size, GetCommandLine());
	}

}