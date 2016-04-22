#pragma once


#include "engine/lumix.h"
#include "engine/core/delegate.h"
#include "engine/core/string.h"
#include "imgui/imgui.h"


struct Action
{
	Action(const char* label, const char* name)
	{
		this->label = label;
		this->name = name;
		shortcut[0] = shortcut[1] = shortcut[2] = -1;
		is_global = true;
	}

	Action(const char* label,
		const char* name,
		int shortcut0,
		int shortcut1,
		int shortcut2)
	{
		this->label = label;
		this->name = name;
		shortcut[0] = shortcut0;
		shortcut[1] = shortcut1;
		shortcut[2] = shortcut2;
		is_global = true;
	}


	bool isActive()
	{
		if (ImGui::IsAnyItemActive()) return false;

		bool* keysDown = ImGui::GetIO().KeysDown;
		if (shortcut[0] == -1) return false;

		for (int i = 0; i < Lumix::lengthOf(shortcut) + 1; ++i)
		{
			if (shortcut[i] == -1 || i == Lumix::lengthOf(shortcut))
			{
				return true;
			}

			if (!keysDown[shortcut[i]]) return false;
		}
		return false;
	}


	bool isRequested()
	{
		if (ImGui::IsAnyItemActive()) return false;

		bool* keysDown = ImGui::GetIO().KeysDown;
		float* keysDownDuration = ImGui::GetIO().KeysDownDuration;
		if (shortcut[0] == -1) return false;

		for (int i = 0; i < Lumix::lengthOf(shortcut) + 1; ++i)
		{
			if (shortcut[i] == -1 || i == Lumix::lengthOf(shortcut))
			{
				return true;
			}

			if (!keysDown[shortcut[i]] || keysDownDuration[shortcut[i]] > 0) return false;
		}
		return false;
	}


	int shortcut[3];
	const char* name;
	const char* label;
	bool is_global;
	Lumix::Delegate<void> func;
};


namespace Lumix
{
	class WorldEditor;
}

LUMIX_EDITOR_API void getEntityListDisplayName(Lumix::WorldEditor& editor,
	char* buf,
	int max_size,
	Lumix::Entity entity);
