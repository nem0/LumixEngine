#pragma once


#include "engine/lumix.h"


namespace Lumix
{
	LUMIX_ENGINE_API bool copyFile(const char* from, const char* to);
	LUMIX_ENGINE_API void messageBox(const char* text);
	LUMIX_ENGINE_API void setCommandLine(int argc, char* argv[]);
	LUMIX_ENGINE_API bool getCommandLine(char* output, int max_size);
}