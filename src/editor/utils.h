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

	Span<const char> full;
};


struct LUMIX_EDITOR_API Action
{
	Action(const char* label_short, const char* label_long, const char* name);
	Action(const char* label_short, const char* label_long, const char* name, OS::Keycode key0, OS::Keycode key1, OS::Keycode key2);
	bool toolbarButton();
	bool isActive();
	void getIconPath(Span<char> path);
	bool isRequested();

	static bool falseConst() { return false; }

	OS::Keycode shortcut[3];
	StaticString<32> name;
	StaticString<32> label_short;
	StaticString<64> label_long;
	bool is_global;
	void* plugin;
	ImTextureID icon;
	Delegate<void ()> func;
	Delegate<bool ()> is_selected;
};


LUMIX_EDITOR_API void getEntityListDisplayName(struct StudioApp& app, struct WorldEditor& editor, Span<char> buf, EntityPtr entity);


} // namespace Lumix
