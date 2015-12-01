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

	FileIterator* createFileIterator(const char* path, Lumix::IAllocator& allocator);
	void destroyFileIterator(FileIterator* iterator);
	bool getNextFile(FileIterator* iterator, FileInfo* info);


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


	bool processSystemEvents();
	bool isForegroundWindow();
	void clipCursor(float min_x, float min_y, float max_x, float max_y);
	void showCursor(bool show);
	void unclipCursor();
	int getWindowX();
	int getWindowY();
	int getWindowWidth();
	int getWindowHeight();
	void createWindow(SystemEventHandler* handler);
	void setSystemEventHandler(SystemEventHandler* handler);
	void setWindowTitle(const char* title);
	bool isMaximized();
	void maximizeWindow();
	void moveWindow(int x, int y, int w, int h);
	void getCurrentDirectory(char* buffer, int buffer_size);
	void shutdown();
	bool isPressed(int key);
	void getKeyName(int key, char* out, int max_size);
	void setCursor(Cursor cursor);
	void* getWindowHandle();
	
	bool getOpenFilename(char* out, int max_size, const char* filter);
	bool getSaveFilename(char* out,
		int max_size,
		const char* filter,
		const char* default_extension);
	bool getOpenDirectory(char* out, int max_size);
	bool shellExecuteOpen(const char* path);

	struct Process;

	Process* createProcess(const char* cmd, const char* args, Lumix::IAllocator& allocator);
	void destroyProcess(Process& process);
	bool isProcessFinished(Process& process);
	int getProcessExitCode(Process& process);
	int getProcessOutput(Process& process, char* buf, int buf_size);


} // namespace PlatformInterface
