#include "engine/system.h"
#include "engine/iallocator.h"
#include "engine/string.h"
#include <ShlObj.h>


namespace Lumix
{
	bool copyFile(const char* from, const char* to)
	{
		if (CopyFile(from, to, FALSE) == FALSE) return false;

		FILETIME ft;
		SYSTEMTIME st;

		GetSystemTime(&st);
		SystemTimeToFileTime(&st, &ft);
		HANDLE handle = CreateFile(to, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		bool f = SetFileTime(handle, (LPFILETIME)NULL, (LPFILETIME)NULL, &ft) != FALSE;
		ASSERT(f);
		CloseHandle(handle);

		return true;
	}


	void getExecutablePath(char* buffer, int buffer_size)
	{
		GetModuleFileName(NULL, buffer, buffer_size);
	}


	void messageBox(const char* text)
	{
		MessageBox(NULL, text, "Message", MB_OK);
	}

	
	void setCommandLine(int, char**)
	{
		ASSERT(false);
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
		return (void*)GetProcAddress((HMODULE)handle, name);
	}
}
