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

	
	static int s_argc = 0;
	static char** s_argv = nullptr;
	
	
	void setCommandLine(int argc, char* argv[])
	{
		s_argc = argc;
		s_argv = argv;
	}
	

	bool getCommandLine(char* output, int max_size)
	{
		if (max_size <=0) return false;
		if (s_argc < 2)
		{
			*output = '\0';
		}
		else
		{
			copyString(output, max_size, s_argv[1]);
			for (int i = 2; i < s_argc; ++i)
			{
				catString(output, max_size, " ");
				catString(output, max_size, s_argv[i]);
			}
		}
		return true;
	}


	void* loadLibrary(const char* path)
	{
		return dlopen(path, RTLD_LOCAL | RTLD_LAZY);
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
