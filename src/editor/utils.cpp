#include "utils.h"
#include "engine/math_utils.h"
#include "engine/path.h"
#include "engine/path_utils.h"
#include "engine/property_register.h"
#include "editor/render_interface.h"
#include "editor/world_editor.h"
#include "imgui/imgui.h"
#include "engine/universe/universe.h"
#include <SDL.h>


namespace Lumix
{


Action::Action(const char* label, const char* name)
{
	this->label = label;
	this->name = name;
	plugin = nullptr;
	shortcut[0] = shortcut[1] = shortcut[2] = -1;
	is_global = true;
	icon = nullptr;
	is_selected.bind<falseConst>();
}


Action::Action(const char* label,
	const char* name,
	int shortcut0,
	int shortcut1,
	int shortcut2)
{
	this->label = label;
	this->name = name;
	plugin = nullptr;
	shortcut[0] = shortcut0;
	shortcut[1] = shortcut1;
	shortcut[2] = shortcut2;
	is_global = true;
	icon = nullptr;
	is_selected.bind<falseConst>();
}


bool Action::toolbarButton()
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


void Action::getIconPath(char* path, int max_size)
{
	copyString(path, max_size, "models/editor/icon_"); 
		
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

	catString(path, max_size, tmp);
	catString(path, max_size, ".dds");
}


bool Action::isRequested()
{
	if (ImGui::IsAnyItemActive()) return false;

	bool* keysDown = ImGui::GetIO().KeysDown;
	float* keysDownDuration = ImGui::GetIO().KeysDownDuration;
	if (shortcut[0] == -1) return false;

	for (int i = 0; i < lengthOf(shortcut) + 1; ++i)
	{
		if (i == lengthOf(shortcut) || shortcut[i] == -1)
		{
			return true;
		}

		if (!keysDown[shortcut[i]] || keysDownDuration[shortcut[i]] > 0) return false;
	}
	return false;
}



bool Action::isActive()
{
	if (ImGui::IsAnyItemActive()) return false;
	if (shortcut[0] == -1) return false;

	int key_count;
	auto* state = SDL_GetKeyboardState(&key_count);

	for (int i = 0; i < lengthOf(shortcut) + 1; ++i)
	{
		if (i == lengthOf(shortcut) || shortcut[i] == -1)
		{
			return true;
		}

		if (shortcut[i] >= key_count || !state[shortcut[i]]) return false;
	}
	return false;
}


void getEntityListDisplayName(WorldEditor& editor, char* buf, int max_size, Entity entity)
{
	if (!entity.isValid())
	{
		*buf = '\0';
		return;
	}
	const char* name = editor.getUniverse()->getEntityName(entity);
	static const auto MODEL_INSTANCE_TYPE = PropertyRegister::getComponentType("renderable");
	ComponentHandle model_instance = editor.getUniverse()->getComponent(entity, MODEL_INSTANCE_TYPE).handle;
	if (model_instance.isValid())
	{
		auto* render_interface = editor.getRenderInterface();
		auto path = render_interface->getModelInstancePath(model_instance);
		if (path.isValid())
		{
			char basename[MAX_PATH_LENGTH];
			copyString(buf, max_size, path.c_str());
			PathUtils::getBasename(basename, MAX_PATH_LENGTH, path.c_str());
			if (name && name[0] != '\0')
				copyString(buf, max_size, name);
			else
				toCString(entity.index, buf, max_size);

			catString(buf, max_size, " - ");
			catString(buf, max_size, basename);
			return;
		}
	}

	if (name && name[0] != '\0')
	{
		copyString(buf, max_size, name);
	}
	else
	{
		toCString(entity.index, buf, max_size);
	}
}


} // namespace Lumix