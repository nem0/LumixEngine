#pragma once


#include "engine/lumix.h"


namespace Lumix
{
	class IAllocator;
	class WorldEditor;
}


struct Action;
struct lua_State;
class StudioApp;


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
	bool m_force_no_crash_report;
	float m_mouse_sensitivity_x;
	float m_mouse_sensitivity_y;
	char m_data_dir[Lumix::MAX_PATH_LENGTH];
	Lumix::WorldEditor* m_editor;

	int m_autosave_time;

	explicit Settings(StudioApp& app);
	~Settings();

	bool save();
	bool load();
	void onGUI();
	void setValue(const char* name, bool value);
	void setValue(const char* name, int value);
	int getValue(const char* name, int default_value) const;
	bool getValue(const char* name, bool default_value) const;

private:
	StudioApp& m_app;
	lua_State* m_state;

private:
	void showShortcutSettings();
	void showToolbarSettings();
};