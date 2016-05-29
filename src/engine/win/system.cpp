#include "engine/system.h"
#include "engine/iallocator.h"
#include "engine/string.h"
#include <ShlObj.h>


namespace Lumix
{
	bool copyFile(const char* from, const char* to)
	{
		return CopyFile(from, to, FALSE) == TRUE;
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
}
