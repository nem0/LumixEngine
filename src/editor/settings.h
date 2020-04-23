#pragma once


#include "engine/lumix.h"
#include "engine/math.h"
#include "engine/string.h"


struct lua_State;


namespace Lumix {


struct LUMIX_EDITOR_API Settings {
	// gui - not saved
	bool m_is_open;
	char m_filter[100];

	// actual settings
	struct Rect
	{
		int x, y;
		int w, h;
	};

	Rect m_window;
	bool m_is_maximized;

	bool m_is_asset_browser_open;
	bool m_is_entity_list_open;
	bool m_is_entity_template_list_open;
	bool m_is_log_open;
	bool m_is_profiler_open;
	bool m_is_properties_open;
	bool m_is_crash_reporting_enabled;
	bool m_force_no_crash_report;
	float m_asset_browser_left_column_width;
	Vec2 m_mouse_sensitivity;
	float m_mouse_sensitivity_y;
	int m_font_size = 13;
	String m_imgui_state;

	explicit Settings(struct StudioApp& app);
	~Settings();

	bool save();
	bool load();
	void onGUI();
	void setValue(const char* name, bool value) const;
	void setValue(const char* name, int value) const;
	int getValue(const char* name, int default_value) const;
	bool getValue(const char* name, bool default_value) const;

private:
	StudioApp& m_app;
	struct Action* m_edit_action = nullptr;
	lua_State* m_state;

private:
	void showShortcutSettings();
	void showToolbarSettings() const;
};


} // namespace Lumix
