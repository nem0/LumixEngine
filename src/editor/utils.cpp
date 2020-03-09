#include <imgui/imgui.h>

#include "utils.h"
#include "engine/math.h"
#include "engine/path.h"
#include "engine/reflection.h"
#include "editor/render_interface.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/universe.h"


namespace Lumix
{


ResourceLocator::ResourceLocator(const Span<const char>& path)
{
	full = path;
	const char* c = path.m_begin;
	subresource.m_begin = c;
	while(c != path.m_end && *c != ':') {
		++c;
	}
	if(c != path.m_end) {
		subresource.m_end = c;
		dir.m_begin = c + 1;
	}
	else {
		subresource.m_end = subresource.m_begin;
		dir.m_begin = path.m_begin;
	}
	
	ext.m_end = path.m_end;
	ext.m_begin = reverseFind(dir.m_begin, ext.m_end, '.');
	if (ext.m_begin) {
		basename.m_end = ext.m_begin;
		++ext.m_begin;
	}
	else {
		ext.m_begin = ext.m_end;
		basename.m_end = path.m_end;
	}
	basename.m_begin = reverseFind(dir.m_begin, basename.m_end, '/');
	if (!basename.m_begin) basename.m_begin = reverseFind(dir.m_begin, basename.m_end, '\\');
	if (basename.m_begin)  {
		dir.m_end = basename.m_begin;
		++basename.m_begin;
	}
	else {
		basename.m_begin = dir.m_begin;
		dir.m_end = dir.m_begin;
	}
}


Action::Action(const char* label_short, const char* label_long, const char* name)
	: label_long(label_long)
	, label_short(label_short)
	, name(name)
	, plugin(nullptr)
	, is_global(true)
	, icon(nullptr)
{
	this->label_short = label_short;
	this->label_long = label_long;
	this->name = name;
	shortcut[0] = shortcut[1] = shortcut[2] = OS::Keycode::INVALID;
	is_selected.bind<falseConst>();
}


Action::Action(const char* label_short,
	const char* label_long,
	const char* name,
	OS::Keycode shortcut0,
	OS::Keycode shortcut1,
	OS::Keycode shortcut2)
	: label_long(label_long)
	, label_short(label_short)
	, name(name)
	, plugin(nullptr)
	, is_global(true)
	, icon(nullptr)
{
	shortcut[0] = shortcut0;
	shortcut[1] = shortcut1;
	shortcut[2] = shortcut2;
	is_selected.bind<falseConst>();
}


bool Action::toolbarButton()
{
	if (!icon) return false;

	ImVec4 col_active = ImGui::GetStyle().Colors[ImGuiCol_ButtonActive];
	ImVec4 bg_color = is_selected.invoke() ? col_active : ImVec4(0, 0, 0, 0);
	int* t = (int*)icon; 
	if (ImGui::ToolbarButton((void*)(uintptr)*t, bg_color, label_long))
	{
		func.invoke();
		return true;
	}
	return false;
}


void Action::getIconPath(Span<char> path)
{
	copyString(path, "editor/icons/icon_"); 
		
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

	catString(path, tmp);
	catString(path, ".dds");
}


bool Action::isRequested()
{
	if (ImGui::IsAnyItemActive()) return false;

	bool* keys_down = ImGui::GetIO().KeysDown;
	float* keys_down_duration = ImGui::GetIO().KeysDownDuration;
	if (shortcut[0] == OS::Keycode::INVALID) return false;

	for (u32 i = 0; i < lengthOf(shortcut) + 1; ++i)
	{
		if (i == lengthOf(shortcut) || shortcut[i] == OS::Keycode::INVALID)
		{
			return true;
		}

		if (!keys_down[(int)shortcut[i]] || keys_down_duration[(int)shortcut[i]] > 0) return false;
	}
	return false;
}



bool Action::isActive()
{
	if (ImGui::IsAnyItemFocused()) return false;
	if (shortcut[0] == OS::Keycode::INVALID) return false;

	for (u32 i = 0; i < lengthOf(shortcut) + 1; ++i) {
		if (i == lengthOf(shortcut) || shortcut[i] == OS::Keycode::INVALID) {
			return true;
		}
		if (!OS::isKeyDown(shortcut[i])) return false;
	}
	return false;
}


void getEntityListDisplayName(StudioApp& app, WorldEditor& editor, Span<char> buf, EntityPtr entity)
{
	if (!entity.isValid())
	{
		buf[0] = '\0';
		return;
	}

	EntityRef e = (EntityRef)entity;
	const char* name = editor.getUniverse()->getEntityName(e);
	static const auto MODEL_INSTANCE_TYPE = Reflection::getComponentType("model_instance");
	if (editor.getUniverse()->hasComponent(e, MODEL_INSTANCE_TYPE))
	{
		RenderInterface* render_interface = app.getRenderInterface();
		auto path = render_interface->getModelInstancePath(*editor.getUniverse(), e);
		if (path.isValid())
		{
			const char* c = path.c_str();
			while (*c && *c != ':') ++c;
			if (*c == ':') {
				copyNString(buf, path.c_str(), int(c - path.c_str() + 1));
				return;
			}

			char basename[MAX_PATH_LENGTH];
			copyString(buf, path.c_str());
			Path::getBasename(Span(basename), path.c_str());
			if (name && name[0] != '\0')
				copyString(buf, name);
			else
				toCString(entity.index, buf);

			catString(buf, " - ");
			catString(buf, basename);
			return;
		}
	}

	if (name && name[0] != '\0')
	{
		copyString(buf, name);
	}
	else
	{
		toCString(entity.index, buf);
	}
}


} // namespace Lumix