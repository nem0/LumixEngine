#include <imgui/imgui.h>

#include "utils.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/path.h"
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
	resource.m_begin = dir.m_begin;
	resource.m_end = ext.m_end;
}

Action::Action() {
	shortcut = os::Keycode::INVALID;
}

void Action::init(const char* label_short, const char* label_long, const char* name, const char* font_icon, bool is_global) {
	this->label_long = label_long;
	this->label_short = label_short;
	this->font_icon = font_icon;
	this->name = name;
	this->is_global = is_global;
	plugin = nullptr;
	shortcut = os::Keycode::INVALID;
	is_selected.bind<falseConst>();
}


void Action::init(const char* label_short,
	const char* label_long,
	const char* name,
	const char* font_icon,
	os::Keycode shortcut,
	u8 modifiers,
	bool is_global)
{
	this->label_long = label_long;
	this->label_short = label_short;
	this->name = name;
	this->font_icon = font_icon;
	this->is_global = is_global;
	this->shortcut = shortcut;
	this->modifiers = modifiers;
	plugin = nullptr;
	is_selected.bind<falseConst>();
}

bool Action::shortcutText(Span<char> out) const {
	if (shortcut == os::Keycode::INVALID && modifiers == 0) {
		copyString(out, "");
		return false;
	}
	char tmp[32];
	os::getKeyName(shortcut, Span(tmp));
	
	copyString(out, "");
	if (modifiers & (u8)Action::Modifiers::CTRL) catString(out, "Ctrl ");
	if (modifiers & (u8)Action::Modifiers::SHIFT) catString(out, "Shift ");
	if (modifiers & (u8)Action::Modifiers::ALT) catString(out, "Alt ");
	catString(out, shortcut == os::Keycode::INVALID ? "" : tmp);
	const i32 len = stringLength(out.m_begin);
	if (len > 0 && out[len - 1] == ' ') {
		out[len - 1] = '\0';
	}
	return true;
}

bool Action::toolbarButton(ImFont* font)
{
	const ImVec4 col_active = ImGui::GetStyle().Colors[ImGuiCol_ButtonActive];
	const ImVec4 bg_color = is_selected.invoke() ? col_active : ImGui::GetStyle().Colors[ImGuiCol_Text];

	if (!font_icon[0]) return false;

	ImGui::SameLine();
	if(ImGuiEx::ToolbarButton(font, font_icon, bg_color, label_long)) {
		func.invoke();
		return true;
	}
	return false;
}


bool Action::isActive()
{
	if (ImGui::IsAnyItemFocused()) return false;
	if (shortcut == os::Keycode::INVALID && modifiers == 0) return false;

	if (shortcut != os::Keycode::INVALID && !os::isKeyDown(shortcut)) return false;
	
	if ((modifiers & (u8)Modifiers::ALT) != 0 && !os::isKeyDown(os::Keycode::MENU)) return false;
	if ((modifiers & (u8)Modifiers::SHIFT) != 0 && !os::isKeyDown(os::Keycode::SHIFT)) return false;
	if ((modifiers & (u8)Modifiers::CTRL) != 0 && !os::isKeyDown(os::Keycode::CTRL)) return false;

	return true;
}

void getShortcut(const Action& action, Span<char> buf) {
	buf[0] = 0;
		
	if (action.modifiers & (u8)Action::Modifiers::CTRL) catString(buf, "CTRL ");
	if (action.modifiers & (u8)Action::Modifiers::SHIFT) catString(buf, "SHIFT ");
	if (action.modifiers & (u8)Action::Modifiers::ALT) catString(buf, "ALT ");

	if (action.shortcut != os::Keycode::INVALID) {
		char tmp[64];
		os::getKeyName(action.shortcut, Span(tmp));
		if (tmp[0] == 0) return;
		catString(buf, " ");
		catString(buf, tmp);
	}
}

void doMenuItem(Action& a, bool enabled)
{
	char buf[20];
	getShortcut(a, Span(buf));
	if (ImGui::MenuItem(a.label_short, buf, a.is_selected.invoke(), enabled))
	{
		a.func.invoke();
	}
}

void getEntityListDisplayName(StudioApp& app, Universe& universe, Span<char> buf, EntityPtr entity)
{
	if (!entity.isValid())
	{
		buf[0] = '\0';
		return;
	}

	EntityRef e = (EntityRef)entity;
	const char* name = universe.getEntityName(e);
	static const auto MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
	if (universe.hasComponent(e, MODEL_INSTANCE_TYPE))
	{
		RenderInterface* render_interface = app.getRenderInterface();
		const Path path = render_interface->getModelInstancePath(universe, e);
		if (!path.isEmpty())
		{
			const char* c = path.c_str();
			while (*c && *c != ':') ++c;
			if (*c == ':') {
				copyNString(buf, path.c_str(), int(c - path.c_str() + 1));
				return;
			}

			copyString(buf, path.c_str());
			Span<const char> basename = Path::getBasename(path.c_str());
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