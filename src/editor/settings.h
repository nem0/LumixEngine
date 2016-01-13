#pragma once


#include "lumix.h"


namespace Lumix
{
	class IAllocator;
}


struct Action;
struct lua_State;


struct LUMIX_EDITOR_API Settings
{
	// gui - not saved
	bool m_is_opened;
	char m_filter[100];

	// actual settings
	struct Rect
	{
		int x, y;
		int w, h;
	};

	Rect m_window;
	bool m_is_maximized;

	bool m_is_asset_browser_opened;
	bool m_is_entity_list_opened;
	bool m_is_entity_template_list_opened;
	bool m_is_log_opened;
	bool m_is_profiler_opened;
	bool m_is_properties_opened;
	bool m_is_crash_reporting_enabled;

	int m_autosave_time;

	Settings(Lumix::IAllocator& allocator);
	~Settings();

	static Settings* getInstance();

	bool save(Action** actions, int actions_count);
	bool load(Action** actions, int actions_count);
	void onGUI(Action** actions, int actions_count);
	void setValue(const char* name, bool value);
	void setValue(const char* name, int value);
	int getValue(const char* name, int default_value) const;
	bool getValue(const char* name, bool default_value) const;

private:
	Lumix::IAllocator& m_allocator;
	lua_State* m_state;

private:
	void showShortcutSettings(Action** actions, int actions_count);
};