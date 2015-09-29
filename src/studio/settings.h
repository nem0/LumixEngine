#pragma once


struct Action;


struct Settings
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
	bool m_is_gameview_opened;
	bool m_is_hierarchy_opened;
	bool m_is_log_opened;
	bool m_is_profiler_opened;
	bool m_is_properties_opened;
	bool m_is_style_editor_opened;

	Settings();

	bool save(Action* actions, int actions_count);
	bool load(Action* actions, int actions_count);
	void onGui(Action* actions, int actions_count);

private:
	void showShortcutSettings(Action* actions, int actions_count);
};