#include "core/system.h"
#include "core/iallocator.h"
#include "core/string.h"
#include <ShlObj.h>
#include <Windows.h>


namespace Lumix
{

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


	void* loadLibrary(const char* path)
	{
		return LoadLibrary(path);
	}


	void unloadLibrary(void* handle)
	{
		FreeLibrary((HMODULE)handle);
	}


	void* getLibrarySymbol(void* handle, const char* name)
	{
		return GetProcAddress((HMODULE)handle, name);
	}
}