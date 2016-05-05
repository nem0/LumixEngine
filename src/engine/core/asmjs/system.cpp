#include "engine/core/system.h"
#include "engine/core/iallocator.h"
#include "engine/core/string.h"
#include <cstdio>


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
		ASSERT(false);
		return nullptr;
	}


	void unloadLibrary(void* handle)
	{
		ASSERT(false);
	}


	void* getLibrarySymbol(void* handle, const char* name)
	{
		ASSERT(false);
		return nullptr;
	}
}
