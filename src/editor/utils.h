#pragma once

#include "engine/delegate.h"
#include "engine/lumix.h"
#include "engine/os.h"
#include "engine/string.h"

namespace Lumix
{

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
	Action() {}
	void init(const char* label_short, const char* label_long, const char* name, const char* font_icon, OS::Keycode key0, u8 modifiers, bool is_global);
	void init(const char* label_short, const char* label_long, const char* name, const char* font_icon, bool is_global);
	bool toolbarButton(struct ImFont* font);
	bool isActive();
	bool shortcutText(Span<char> out);

	static bool falseConst() { return false; }

	enum class Modifiers : u8 {
		SHIFT = 1 << 0,
		ALT = 1 << 1,
		CTRL = 1 << 2
	};

	u8 modifiers = 0;
	OS::Keycode shortcut = OS::Keycode::INVALID;
	StaticString<32> name;
	StaticString<32> label_short;
	StaticString<64> label_long;
	StaticString<5> font_icon;
	bool is_global;
	void* plugin;
	Delegate<void ()> func;
	Delegate<bool ()> is_selected;
};


LUMIX_EDITOR_API void getEntityListDisplayName(struct StudioApp& app, struct WorldEditor& editor, Span<char> buf, EntityPtr entity);


} // namespace Lumix
