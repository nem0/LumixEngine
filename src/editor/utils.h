#pragma once


#include "engine/lumix.h"
#include "engine/delegate.h"
#include "engine/string.h"
#include "imgui/imgui.h"
#include <SDL.h>


struct Action
{
	Action(const char* label, const char* name)
	{
		this->label = label;
		this->name = name;
		shortcut[0] = shortcut[1] = shortcut[2] = -1;
		is_global = true;
		icon = nullptr;
		is_selected.bind<falseConst>();
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
		icon = nullptr;
		is_selected.bind<falseConst>();
	}


	static bool falseConst() { return false; }


	bool toolbarButton()
	{
		if (!icon) return false;

		ImVec4 col_active = ImGui::GetStyle().Colors[ImGuiCol_ButtonActive];
		ImVec4 bg_color = is_selected.invoke() ? col_active : ImVec4(0, 0, 0, 0);
		if (ImGui::ToolbarButton(icon, bg_color, label))
		{
			func.invoke();
			return true;
		}
		return false;
	}


	bool isActive()
	{
		if (ImGui::IsAnyItemActive()) return false;
		if (shortcut[0] == -1) return false;

		int key_count;
		auto* state = SDL_GetKeyboardState(&key_count);

		for (int i = 0; i < Lumix::lengthOf(shortcut) + 1; ++i)
		{
			if (i == Lumix::lengthOf(shortcut) || shortcut[i] == -1)
			{
				return true;
			}

			if (shortcut[i] >= key_count || !state[shortcut[i]]) return false;
		}
		return false;
	}


	void getIconPath(char* path, int max_size)
	{
		Lumix::copyString(path, max_size, "models/editor/icon_"); 
		
		char tmp[1024];
		const char* c = name;
		char* out = tmp;
		while (*c)
		{
			if (*c >= 'A' && *c <= 'Z') *out = *c - ('A' - 'a');
			else if (*c >= 'a' && *c <= 'z') *out = *c;
			else *out = '_';
			++out;
			++c;
		}
		*out = 0;

		Lumix::catString(path, max_size, tmp);
		Lumix::catString(path, max_size, ".dds");
	}



	bool isRequested()
	{
		if (ImGui::IsAnyItemActive()) return false;

		bool* keysDown = ImGui::GetIO().KeysDown;
		float* keysDownDuration = ImGui::GetIO().KeysDownDuration;
		if (shortcut[0] == -1) return false;

		for (int i = 0; i < Lumix::lengthOf(shortcut) + 1; ++i)
		{
			if (i == Lumix::lengthOf(shortcut) || shortcut[i] == -1)
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
	ImTextureID icon;
	Lumix::Delegate<void> func;
	Lumix::Delegate<bool> is_selected;
};


namespace Lumix
{
	class WorldEditor;
}

LUMIX_EDITOR_API void getEntityListDisplayName(Lumix::WorldEditor& editor,
	char* buf,
	int max_size,
	Lumix::Entity entity);
