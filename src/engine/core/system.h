#pragma once


#include "lumix.h"


namespace Lumix
{

	class IAllocator;
	struct Process;

	LUMIX_ENGINE_API bool deleteFile(const char* path);
	LUMIX_ENGINE_API bool moveFile(const char* from, const char* to);
	LUMIX_ENGINE_API bool copyFile(const char* from, const char* to);
	LUMIX_ENGINE_API bool fileExists(const char* path);
	LUMIX_ENGINE_API bool getOpenFilename(char* out, int max_size, const char* filter);
	LUMIX_ENGINE_API bool getSaveFilename(
		char* out, int max_size, const char* filter, const char* default_extension);
	LUMIX_ENGINE_API bool getOpenDirectory(char* out, int max_size);
	LUMIX_ENGINE_API uint64_t getLastModified(const char* file);
	LUMIX_ENGINE_API void messageBox(const char* text);

	LUMIX_ENGINE_API bool shellExecuteOpen(const char* path);

	LUMIX_ENGINE_API Process* createProcess(const char* cmd, char* args, IAllocator& allocator);
	LUMIX_ENGINE_API void destroyProcess(Process& process);
	LUMIX_ENGINE_API bool isProcessFinished(Process& process);
	LUMIX_ENGINE_API bool makePath(const char* path);

}