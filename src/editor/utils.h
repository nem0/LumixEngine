#pragma once


#include "engine/lumix.h"
#include "engine/delegate.h"
#include "imgui/imgui.h"


namespace Lumix
{


struct LUMIX_EDITOR_API Action
{
	Action(const char* label_short, const char* label_long, const char* name);
	Action(const char* label_short, const char* label_long, const char* name, int shortcut0, int shortcut1, int shortcut2);
	bool toolbarButton();
	bool isActive();
	void getIconPath(char* path, int max_size);
	bool isRequested();

	static bool falseConst() { return false; }

	int shortcut[3];
	const char* name;
	const char* label_short;
	const char* label_long;
	bool is_global;
	void* plugin;
	ImTextureID icon;
	Delegate<void> func;
	Delegate<bool> is_selected;
};


class WorldEditor;


LUMIX_EDITOR_API void getEntityListDisplayName(WorldEditor& editor,
	char* buf,
	int max_size,
	Entity entity);


} // namespace Lumix
