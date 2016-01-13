#pragma once


#include "lumix.h"


namespace Lumix
{
	class IAllocator;
}


namespace PlatformInterface
{
	enum class Keys
	{
		CONTROL,
		ALT,
		SHIFT,
		TAB,
		LEFT,
		RIGHT,
		UP,
		DOWN,
		PAGE_UP,
		PAGE_DOWN,
		HOME,
		END,
		DEL,
		BACKSPACE,
		ENTER,
		ESCAPE
	};


	enum class Cursor
	{
		NONE,
		DEFAULT
	};


	struct FileInfo
	{
		bool is_directory;
		char filename[Lumix::MAX_PATH_LENGTH];
	};

	struct FileIterator;

	LUMIX_STUDIO_LIB_API FileIterator* createFileIterator(const char* path, Lumix::IAllocator& allocator);
	LUMIX_STUDIO_LIB_API void destroyFileIterator(FileIterator* iterator);
	LUMIX_STUDIO_LIB_API bool getNextFile(FileIterator* iterator, FileInfo* info);


	struct SystemEventHandler
	{
		enum class MouseButton
		{
			LEFT,
			RIGHT,
			MIDDLE
		};

		virtual void onWindowTransformed(int x, int y, int w, int h) = 0;
		virtual void onMouseLeftWindow() = 0;
		virtual void onMouseMove(int x, int y, int rel_x, int rel_y) = 0;
		virtual void onMouseWheel(int amount) = 0;
		virtual void onMouseButtonDown(MouseButton button) = 0;
		virtual void onMouseButtonUp(MouseButton button) = 0;
		virtual void onKeyDown(int key) = 0;
		virtual void onKeyUp(int key) = 0;
		virtual void onChar(int key) = 0;
	};


	LUMIX_STUDIO_LIB_API bool isWindowActive();
	LUMIX_STUDIO_LIB_API bool processSystemEvents();
	LUMIX_STUDIO_LIB_API void clipCursor(float min_x, float min_y, float max_x, float max_y);
	LUMIX_STUDIO_LIB_API void showCursor(bool show);
	LUMIX_STUDIO_LIB_API void unclipCursor();
	LUMIX_STUDIO_LIB_API int getWindowX();
	LUMIX_STUDIO_LIB_API int getWindowY();
	LUMIX_STUDIO_LIB_API int getWindowWidth();
	LUMIX_STUDIO_LIB_API int getWindowHeight();
	LUMIX_STUDIO_LIB_API void createWindow(SystemEventHandler* handler);
	LUMIX_STUDIO_LIB_API void* getWindowHandle();
	LUMIX_STUDIO_LIB_API void setSystemEventHandler(SystemEventHandler* handler);
	LUMIX_STUDIO_LIB_API void setWindowTitle(const char* title);
	LUMIX_STUDIO_LIB_API bool isMaximized();
	LUMIX_STUDIO_LIB_API void maximizeWindow();
	LUMIX_STUDIO_LIB_API void moveWindow(int x, int y, int w, int h);
	LUMIX_STUDIO_LIB_API void getCurrentDirectory(char* buffer, int buffer_size);
	LUMIX_STUDIO_LIB_API void shutdown();
	LUMIX_STUDIO_LIB_API bool isPressed(int key);
	LUMIX_STUDIO_LIB_API void getKeyName(int key, char* out, int max_size);
	LUMIX_STUDIO_LIB_API void setCursor(Cursor cursor);
	
	LUMIX_STUDIO_LIB_API bool getOpenFilename(char* out, int max_size, const char* filter);
	LUMIX_STUDIO_LIB_API bool getSaveFilename(char* out,
		int max_size,
		const char* filter,
		const char* default_extension);
	LUMIX_STUDIO_LIB_API bool getOpenDirectory(char* out, int max_size);
	LUMIX_STUDIO_LIB_API bool shellExecuteOpen(const char* path);

	LUMIX_STUDIO_LIB_API bool deleteFile(const char* path);
	LUMIX_STUDIO_LIB_API bool moveFile(const char* from, const char* to);
	LUMIX_STUDIO_LIB_API bool fileExists(const char* path);
	LUMIX_STUDIO_LIB_API bool dirExists(const char* path);
	LUMIX_STUDIO_LIB_API Lumix::uint64 getLastModified(const char* file);
	LUMIX_STUDIO_LIB_API bool makePath(const char* path);

	struct Process;

	LUMIX_STUDIO_LIB_API Process* createProcess(const char* cmd, const char* args, Lumix::IAllocator& allocator);
	LUMIX_STUDIO_LIB_API void destroyProcess(Process& process);
	LUMIX_STUDIO_LIB_API bool isProcessFinished(Process& process);
	LUMIX_STUDIO_LIB_API int getProcessExitCode(Process& process);
	LUMIX_STUDIO_LIB_API int getProcessOutput(Process& process, char* buf, int buf_size);


} // namespace PlatformInterface
