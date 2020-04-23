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


Action::Action(const char* label_short, const char* label_long, const char* name, const char* font_icon)
	: label_long(label_long)
	, label_short(label_short)
	, font_icon(font_icon)
	, name(name)
	, plugin(nullptr)
	, is_global(true)
	, shortcut(OS::Keycode::INVALID)
{
	is_selected.bind<falseConst>();
}


Action::Action(const char* label_short,
	const char* label_long,
	const char* name,
	const char* font_icon,
	OS::Keycode shortcut,
	u8 modifiers)
	: label_long(label_long)
	, label_short(label_short)
	, name(name)
	, font_icon(font_icon)
	, plugin(nullptr)
	, is_global(true)
	, shortcut(shortcut)
	, modifiers(modifiers)
{
	is_selected.bind<falseConst>();
}


bool Action::toolbarButton(ImFont* font)
{
	const ImVec4 col_active = ImGui::GetStyle().Colors[ImGuiCol_ButtonActive];
	const ImVec4 bg_color = is_selected.invoke() ? col_active : ImVec4(0, 0, 0, 0);

	if (!font_icon[0]) return false;

	if(ImGui::ToolbarButton(font, font_icon, bg_color, label_long)) {
		func.invoke();
		return true;
	}
	return false;
}


bool Action::isActive()
{
	if (ImGui::IsAnyItemFocused()) return false;
	if (shortcut == OS::Keycode::INVALID && modifiers == 0) return false;

	if (shortcut != OS::Keycode::INVALID && !OS::isKeyDown(shortcut)) return false;
	
	if ((modifiers & (u8)Modifiers::ALT) != 0 && !OS::isKeyDown(OS::Keycode::MENU)) return false;
	if ((modifiers & (u8)Modifiers::SHIFT) != 0 && !OS::isKeyDown(OS::Keycode::SHIFT)) return false;
	if ((modifiers & (u8)Modifiers::CTRL) != 0 && !OS::isKeyDown(OS::Keycode::CTRL)) return false;

	return true;
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