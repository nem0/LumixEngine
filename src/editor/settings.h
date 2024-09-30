#pragma once

#include "core/array.h"
#include "core/delegate.h"
#include "core/hash_map.h"
#include "core/math.h"
#include "core/os.h"
#include "core/stack_array.h"
#include "core/string.h"
#include "core/tag_allocator.h"
#include "editor/utils.h"
#include "engine/lumix.h"

namespace Lumix {

struct LUMIX_EDITOR_API MouseSensitivity {
	MouseSensitivity(IAllocator& allocator);
	void gui();
	float eval(float value);
	void save(const char* name, OutputMemoryStream& blob);
	bool load(struct Tokenizer& tokenizer);

	StackArray<Vec2, 8> values; 
};

// every setting can be stored in two places - workspace and user
// workspace settings are stored in the project folder and are shared between all users
// user settings are stored in the user's home folder and are unique for each user
// user settings override workspace settings
struct LUMIX_EDITOR_API Settings {
	enum Storage {
		WORKSPACE,
		USER
	};

	enum : u32 { INVALID_CATEGORY = 0xFFffFFff };

	Settings(struct StudioApp& app);

	void gui();
	void load();
	void save();

	i32 getI32(const char* var_name, i32 default_value);
	bool getBool(const char* var_name, bool default_value);
	void setI32(const char* var_name, i32 value, Storage storage);
	void setBool(const char* var_name, bool value, Storage storage);
	// register variable with memory storage not in Settings
	// if category is null, the variable is not visible in settings UI
	// otherwise it's grouped in the category
	void registerPtr(const char* name, bool* value, const char* category = nullptr, Delegate<void()>* set_callback = nullptr);
	void registerPtr(const char* name, String* value, const char* category = nullptr);
	void registerPtr(const char* name, i32* value, const char* category = nullptr);
	void registerPtr(const char* name, float* value, const char* category = nullptr, bool is_angle = false);

	float getTimeSinceLastSave() const;

	struct Variable {
		Variable(IAllocator& allocator) : string_value(allocator) {}
		Storage storage = WORKSPACE;
		enum Type {
			BOOL,
			BOOL_PTR,
			STRING,
			STRING_PTR,
			I32,
			I32_PTR,
			FLOAT,
			FLOAT_PTR,
		};
		union {
			bool bool_value;
			bool* bool_ptr;
			String* string_ptr;
			i32 i32_value;
			i32* i32_ptr;
			float float_value;
			float* float_ptr;
		};
		String string_value;
		Type type;
		u32 category = INVALID_CATEGORY;
		Delegate<void()> set_callback; // only called by settings
		bool is_angle = false; // is angle in radians
	};

	// category is only to group variables in the settings window
	struct Category {
		Category(IAllocator& allocator) : name(allocator) {}
		String name;
	};

	StudioApp& m_app;
	TagAllocator m_allocator;
	Array<Category> m_categories;
	HashMap<String, Variable> m_variables;
	String m_imgui_state;
	String m_app_data_path;
	MouseSensitivity m_mouse_sensitivity_x;
	MouseSensitivity m_mouse_sensitivity_y;
	bool m_is_open = false;
	u64 m_last_save_time = 0;
	Action* m_edit_action = nullptr;
	Action m_toggle_ui_action{"Settings", "Settings - toggle UI", "settings_toggle_ui", "", Action::WINDOW};
	Action m_focus_search{"Focus search", "Settings - focus shortcut search", "settings_focus_search", ""};

private:
	void shortcutsGUI();
};

} // namespace Lumix
