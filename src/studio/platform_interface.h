#pragma once


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


	struct SystemEventHandler
	{
		enum class MouseButton
		{
			LEFT,
			RIGHT
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
	void setWindowTitle(const char* title);
	bool isMaximized();
	void maximizeWindow();
	void moveWindow(int x, int y, int w, int h);
	void getCurrentDirectory(char* buffer, int buffer_size);
	void shutdown();
	bool isPressed(int key);
	void getKeyName(int key, char* out, int max_size);
	void setCursor(Cursor cursor);

} // namespace PlatformInterface
