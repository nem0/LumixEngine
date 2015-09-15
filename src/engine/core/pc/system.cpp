#include "system.h"
#include <Windows.h>


namespace Lumix
{

	bool shellExecute(const char* cmd, const char* args)
	{
		return (int)ShellExecute(NULL, NULL, cmd, args, NULL, SW_HIDE) > 32;
	}

}