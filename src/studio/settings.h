#pragma once



namespace Lumix
{
	class IAllocator;
}


struct Action;
class GUIInterface;
struct lua_State;


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
	bool m_is_log_opened;
	bool m_is_profiler_opened;
	bool m_is_properties_opened;
	bool m_is_style_editor_opened;
	bool m_is_crash_reporting_enabled;
	bool m_is_shader_editor_opened;
	bool m_is_clip_manager_opened;

	int m_autosave_time;

	Settings(Lumix::IAllocator& allocator);
	~Settings();

	static Settings* getInstance();

	void setGUIInterface(GUIInterface& gui);
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
	GUIInterface* m_gui;

private:
	void showShortcutSettings(Action** actions, int actions_count);
};