#include "engine/system.h"
#include "engine/iallocator.h"
#include "engine/string.h"
#include <cstdio>
#include <dlfcn.h>


namespace Lumix
{
	bool copyFile(const char* from, const char* to)
	{
		ASSERT(false);
		return false;
	}


	void messageBox(const char* text)
	{
		printf("%s", text);
	}


	bool getCommandLine(char* output, int max_size)
	{
		ASSERT(false);
		return false;
	}


	void* loadLibrary(const char* path)
	{
		return dlopen(path, 0);
	}


	void unloadLibrary(void* handle)
	{
		dlclose(handle);
	}


	void* getLibrarySymbol(void* handle, const char* name)
	{
		return dlsym(handle, name);
	}
}
