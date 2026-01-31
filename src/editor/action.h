#pragma once

#include "core/os.h"
#include "core/string.h"
#include "engine/black.h.h"

struct ImFont;

namespace black {

struct BLACK_EDITOR_API Action {
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
	CommonActions();

	Action save;
	Action undo;
	Action redo;
	Action del;
	
	Action cam_orbit;
	Action cam_forward;
	Action cam_backward;
	Action cam_left;
	Action cam_right;
	Action cam_up;
	Action cam_down;
	
	Action select_all;
	Action rename;
	Action copy;
	Action paste;
	Action close_window;
	Action open_externally;
	Action view_in_browser;
};

inline Action::Modifiers operator |(Action::Modifiers a, Action::Modifiers b) { return Action::Modifiers((u8)a | (u8)b); }
inline void operator |= (Action::Modifiers& a, Action::Modifiers b) { a = a | b; }

BLACK_EDITOR_API void getShortcut(const Action& action, Span<char> buf);

}