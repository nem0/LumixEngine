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
		return GetExitCodeProcess(process.process_info.hProcess, &exit_code) != STILL_ACTIVE;
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


	bool fileExists(const char* path)
	{
		DWORD dwAttrib = GetFileAttributes(path);
		return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
			!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
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
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

		char current_dir[MAX_PATH];
		GetCurrentDirectory(sizeof(current_dir), current_dir);
		bool status = GetOpenFileName(&ofn) == TRUE;
		SetCurrentDirectory(current_dir);

		return status;
	}


	bool getOpenDirectory(char* out, int max_size)
	{
		ASSERT(max_size >= MAX_PATH);
		BROWSEINFO bi;
		ZeroMemory(&bi, sizeof(bi));
		bi.hwndOwner = NULL;
		bi.pidlRoot = NULL;
		bi.pszDisplayName = out;
		bi.lpszTitle = "Please, select a folder";
		bi.ulFlags = 0;
		bi.lpfn = NULL;
		bi.lParam = 0;
		bi.iImage = -1;
		SHBrowseForFolder(&bi);

		return true;
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

}