#pragma once

#include "core/os.h"
#include "core/string.h"
#include "engine/lumix.h"
#include <imgui/IconsFontAwesome5.h>

struct ImFont;

namespace Lumix {

struct LUMIX_EDITOR_API Action {
	enum Modifiers : u8 {
		NONE = 0,

		SHIFT = 1 << 0,
		ALT = 1 << 1,
		CTRL = 1 << 2
	};

	enum Type {
		NORMAL,
		TOOL,
		WINDOW,
		TEMPORARY
	};

	Action(const char* group, const char* label_short, const char* label_long, const char* name, const char* font_icon, Type type = NORMAL);
	~Action();
	Action(const Action&) = delete;
	void operator =(const Action&) = delete;
	bool toolbarButton(ImFont* font, bool is_selected = false);
	bool isActive() const;
	bool iconButton(bool enabled = true, struct StudioApp* app = nullptr);
	bool shortcutText(Span<char> out) const;

	StaticString<32> name;			// used for serialization
	StaticString<32> label_short;	// used in menus
	StaticString<32> group;			// used in shortcut editor
	StaticString<64> label_long;	// used in shortcut editor
	bool request = false;			// programatically request to invoke the action
	Modifiers modifiers = Modifiers::NONE;
	os::Keycode shortcut;
	StaticString<5> font_icon;
	Type type;

	// linked list of all actions
	static Action* first_action;
	Action* next = nullptr;
	Action* prev = nullptr;
};

struct CommonActions {
	Action save{"Common", "Save", "Save", "save", ICON_FA_SAVE};
	Action undo{"Common", "Undo", "Undo", "undo", ICON_FA_UNDO};
	Action redo{"Common", "Redo", "Redo", "redo", ICON_FA_REDO};
	Action del{"Common", "Delete", "Delete", "delete", ICON_FA_MINUS_SQUARE};
	
	Action cam_orbit{"Camera", "Orbit", "Orbit with RMB", "orbit_rmb", ""};
	Action cam_forward{"Camera", "Move forward", "Move forward", "camera_move_forward", ""};
	Action cam_backward{"Camera", "Move back", "Move backward", "camera_move_back", ""};
	Action cam_left{"Camera", "Move left", "Move left", "camera_move_left", ""};
	Action cam_right{"Camera", "Move right", "Move right", "camera_move_right", ""};
	Action cam_up{"Camera", "Move up", "Move up", "camera_move_up", ""};
	Action cam_down{"Camera", "Move down", "Move down", "camera_move_down", ""};
	
	Action select_all{"Common", "Select all", "Select all", "select_all", ""};
	Action rename{"Common", "Rename", "Rename", "rename", ""};
	Action copy{"Common", "Copy", "Copy", "copy", ICON_FA_CLIPBOARD};
	Action paste{"Common", "Paste", "Paste", "paste", ICON_FA_PASTE};
	Action close_window{"Common", "Close", "Close window", "close_window", ""};
	Action open_externally{"Common", "Open externally", "Open externally", "open_externally", ICON_FA_EXTERNAL_LINK_ALT};
	Action view_in_browser{"Common", "View in browser", "View in asset browser", "view_in_asset_browser", ICON_FA_SEARCH};
};

inline Action::Modifiers operator |(Action::Modifiers a, Action::Modifiers b) { return Action::Modifiers((u8)a | (u8)b); }
inline void operator |= (Action::Modifiers& a, Action::Modifiers b) { a = a | b; }

LUMIX_EDITOR_API void getShortcut(const Action& action, Span<char> buf);

}