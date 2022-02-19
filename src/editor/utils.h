#pragma once

#include "engine/delegate.h"
#include "engine/lumix.h"
#include "engine/string.h"

namespace Lumix {

namespace os { enum class Keycode : u8; }

struct LUMIX_EDITOR_API ResourceLocator {
	ResourceLocator(const Span<const char>& path);

	Span<const char> subresource;
	Span<const char> dir;
	Span<const char> basename;
	Span<const char> ext;
	Span<const char> resource;

	Span<const char> full;
};


struct LUMIX_EDITOR_API Action
{
	enum Modifiers : u8 {
		NONE = 0,

		SHIFT = 1 << 0,
		ALT = 1 << 1,
		CTRL = 1 << 2
	};

	Action();
	void init(const char* label_short, const char* label_long, const char* name, const char* font_icon, os::Keycode key0, Modifiers modifiers, bool is_global);
	void init(const char* label_short, const char* label_long, const char* name, const char* font_icon, bool is_global);
	bool toolbarButton(struct ImFont* font);
	bool isActive();
	bool shortcutText(Span<char> out) const;

	static bool falseConst() { return false; }

	Modifiers modifiers = Modifiers::NONE;
	os::Keycode shortcut;
	StaticString<32> name;
	StaticString<32> label_short;
	StaticString<64> label_long;
	StaticString<5> font_icon;
	bool is_global;
	void* plugin;
	Delegate<void ()> func;
	Delegate<bool ()> is_selected;
};

inline Action::Modifiers operator |(Action::Modifiers a, Action::Modifiers b) { return Action::Modifiers((u8)a | (u8)b); }
inline void operator |= (Action::Modifiers& a, Action::Modifiers b) { a = a | b; }

LUMIX_EDITOR_API void getShortcut(const Action& action, Span<char> buf);
LUMIX_EDITOR_API void menuItem(Action& a, bool enabled);
LUMIX_EDITOR_API void getEntityListDisplayName(struct StudioApp& app, struct Universe& editor, Span<char> buf, EntityPtr entity);


} // namespace Lumix
