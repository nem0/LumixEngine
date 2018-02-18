#pragma once


#include "engine/lumix.h"
#include "engine/engine.h"


struct SDL_Window;


namespace Lumix
{

	
struct IAllocator;


namespace PlatformInterface
{
	struct FileInfo
	{
		bool is_directory;
		char filename[MAX_PATH_LENGTH];
	};

	struct FileIterator;

	LUMIX_EDITOR_API FileIterator* createFileIterator(const char* path, IAllocator& allocator);
	LUMIX_EDITOR_API void destroyFileIterator(FileIterator* iterator);
	LUMIX_EDITOR_API bool getNextFile(FileIterator* iterator, FileInfo* info);

	LUMIX_EDITOR_API void getExecutablePath(char* buffer, int buffer_size);
	LUMIX_EDITOR_API void setCurrentDirectory(const char* path);
	LUMIX_EDITOR_API void getCurrentDirectory(char* buffer, int buffer_size);
	LUMIX_EDITOR_API bool getOpenFilename(char* out, int max_size, const char* filter, const char* starting_file);
	LUMIX_EDITOR_API bool getSaveFilename(char* out, int max_size, const char* filter, const char* default_extension);
	LUMIX_EDITOR_API bool getOpenDirectory(char* out, int max_size, const char* starting_dir);
	LUMIX_EDITOR_API bool shellExecuteOpen(const char* path, const char* parameters);
	LUMIX_EDITOR_API void copyToClipboard(const char* text);

	LUMIX_EDITOR_API bool deleteFile(const char* path);
	LUMIX_EDITOR_API bool moveFile(const char* from, const char* to);
	LUMIX_EDITOR_API size_t getFileSize(const char* path);
	LUMIX_EDITOR_API bool fileExists(const char* path);
	LUMIX_EDITOR_API bool dirExists(const char* path);
	LUMIX_EDITOR_API u64 getLastModified(const char* file);
	LUMIX_EDITOR_API bool makePath(const char* path);

	LUMIX_EDITOR_API void setWindow(SDL_Window* window);
	LUMIX_EDITOR_API void clipCursor(int x, int y, int w, int h);
	LUMIX_EDITOR_API void unclipCursor();

	struct Process;

	LUMIX_EDITOR_API Process* createProcess(const char* cmd, const char* args, IAllocator& allocator);
	LUMIX_EDITOR_API void destroyProcess(Process& process);
	LUMIX_EDITOR_API bool isProcessFinished(Process& process);
	LUMIX_EDITOR_API int getProcessExitCode(Process& process);
	LUMIX_EDITOR_API int getProcessOutput(Process& process, char* buf, int buf_size);

} // namespace PlatformInterface


} // namespace Lumix