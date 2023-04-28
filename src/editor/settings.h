#pragma once


#include "engine/lumix.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/string.h"


struct lua_State;


namespace Lumix {


struct LUMIX_EDITOR_API Settings {
	// gui - not saved
	bool m_is_open;
	char m_filter[100];

	enum Storage {
		GLOBAL, // shortcuts, ...
		LOCAL // recently open files, ...
	};

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
	bool m_sleep_when_inactive;
	Vec2 m_mouse_sensitivity;
	float m_mouse_sensitivity_y;
	int m_font_size = 13;
	String m_imgui_state;

	explicit Settings(struct StudioApp& app);
	~Settings();

	[[nodiscard]] bool save();
	bool load();
	void onGUI();
	void setValue(Storage storage, const char* name, bool value) const;
	void setValue(Storage storage, const char* name, float value) const;
	void setValue(Storage storage, const char* name, int value) const;
	void setValue(Storage storage, const char* name, const char* value) const;
	float getValue(Storage storage, const char* name, float default_value) const;
	int getValue(Storage storage, const char* name, int default_value) const;
	bool getValue(Storage storage, const char* name, bool default_value) const;
	u32 getValue(Storage storage, const char* name, Span<char> out) const;
	const char* getStringValue(Storage storage, const char* name, const char* default_value) const;
	const char* getAppDataPath() const { return m_app_data_path; }
	float getTimeSinceLastSave() const { return m_time_since_last_save.getTimeSinceTick(); }

private:
	static void writeCustom(lua_State* L, struct IOutputStream& file);
	lua_State* getState(Storage storage) const;
	bool loadAppData();

	StudioApp& m_app;
	struct Action* m_edit_action = nullptr;
	lua_State* m_global_state;
	lua_State* m_local_state;
	char m_app_data_path[LUMIX_MAX_PATH];
	os::Timer m_time_since_last_save;

private:
	void showShortcutSettings();
	void showStyleEditor() const;
};


} // namespace Lumix
