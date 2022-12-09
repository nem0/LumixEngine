#include <imgui/imgui.h>

#include "utils.h"
#include "editor/render_interface.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/path.h"
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
	Modifiers modifiers,
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
	
	Modifiers pressed_modifiers = Modifiers::NONE;
	if (os::isKeyDown(os::Keycode::MENU)) pressed_modifiers |= Modifiers::ALT;
	if (os::isKeyDown(os::Keycode::SHIFT)) pressed_modifiers |= Modifiers::SHIFT;
	if (os::isKeyDown(os::Keycode::CTRL)) pressed_modifiers |= Modifiers::CTRL;
	if (modifiers != pressed_modifiers && modifiers != Modifiers::NONE) return false;

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
		catString(buf, tmp);
	}
}

void menuItem(Action& a, bool enabled)
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

static int inputTextCallback(ImGuiInputTextCallbackData* data) {
	if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
		String* str = (String*)data->UserData;
		ASSERT(data->Buf == str->c_str());
		str->resize(data->BufTextLen);
		data->Buf = (char*)str->c_str();
	}
	return 0;
}

bool inputString(const char* label, String* value) {
	ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
	return ImGui::InputText(label, (char*)value->c_str(), value->length() + 1, flags, inputTextCallback, value);
}

SimpleUndoRedo::SimpleUndoRedo(IAllocator& allocator)
	: m_stack(allocator)
	, m_allocator(allocator)
{}

bool SimpleUndoRedo::canUndo() const { return m_stack_idx > 0; }
bool SimpleUndoRedo::canRedo() const { return m_stack_idx < m_stack.size() - 1; }

void SimpleUndoRedo::undo() {
	if (m_stack_idx <= 0) return;

	InputMemoryStream blob(m_stack[m_stack_idx - 1].blob);
	deserialize(blob);
	--m_stack_idx;
}

void SimpleUndoRedo::redo() {
	if (m_stack_idx + 1 >= m_stack.size()) return;

	InputMemoryStream blob(m_stack[m_stack_idx + 1].blob);
	deserialize(blob);
	++m_stack_idx;
}

void SimpleUndoRedo::pushUndo(u32 tag) {
	while (m_stack.size() > m_stack_idx + 1) m_stack.pop();

	Undo u(m_allocator);
	u.tag = tag;
	serialize(u.blob);
	if (tag == NO_MERGE_UNDO || m_stack.back().tag != tag) {
		m_stack.push(static_cast<Undo&&>(u));
		++m_stack_idx;
	}
	else {
		m_stack.back() = static_cast<Undo&&>(u);
	}
}

void SimpleUndoRedo::clearUndoStack() {
	m_stack.clear();
	m_stack_idx = -1;
}

void FileSelector::fillSubitems() {
	m_subdirs.clear();
	m_subfiles.clear();
	FileSystem& fs = m_app.getEngine().getFileSystem();
	const char* base_path = fs.getBasePath();
	
	StaticString<LUMIX_MAX_PATH> path(base_path, "/", m_current_dir.c_str());
	os::FileIterator* iter = os::createFileIterator(path, m_app.getAllocator());
	os::FileInfo info;
	const char* ext = m_accepted_extension.c_str();
	while (os::getNextFile(iter, &info)) {
		if (equalStrings(info.filename, ".")) continue;
		if (equalStrings(info.filename, "..")) continue;
		if (equalStrings(info.filename, ".lumix") && m_current_dir.length() == 0) continue;

		if (info.is_directory) {
			m_subdirs.emplace(info.filename, m_app.getAllocator());
		}
		else {
			if (!ext[0] || Path::hasExtension(info.filename, ext)) {
				m_subfiles.emplace(info.filename, m_app.getAllocator());
			}
		}
	}
	os::destroyFileIterator(iter);
}


bool FileSelector::breadcrumb(Span<const char> path) {
	if (path.length() == 0) {
		if (ImGui::Button(".")) {
			m_current_dir = "";
			fillSubitems();
			return true;
		}
		return false;
	}
	if (path.back() == '/') path = path.fromRight(1);
	
	Span<const char> dir = Path::getDir(path);
	Span<const char> basename = Path::getBasename(path);
	if (breadcrumb(dir)) return true;
	ImGui::SameLine();
	ImGui::TextUnformatted("/");
	ImGui::SameLine();
	
	char tmp[LUMIX_MAX_PATH];
	copyString(Span(tmp), basename);
	if (ImGui::Button(tmp)) {
		m_current_dir = String(path, m_app.getAllocator());
		fillSubitems();
		return true;
	}
	return false;
}

FileSelector::FileSelector(const char* ext, StudioApp& app)
	: m_app(app)
	, m_filename(app.getAllocator())
	, m_full_path(app.getAllocator())
	, m_current_dir(app.getAllocator())
	, m_subdirs(app.getAllocator())
	, m_subfiles(app.getAllocator())
	, m_accepted_extension(ext, app.getAllocator())
{
	fillSubitems();
}

DirSelector::DirSelector(StudioApp& app)
	: m_app(app)
	, m_current_dir(app.getAllocator())
	, m_subdirs(app.getAllocator())
{}


void DirSelector::fillSubitems() {
	m_subdirs.clear();
	FileSystem& fs = m_app.getEngine().getFileSystem();
	const char* base_path = fs.getBasePath();
	
	StaticString<LUMIX_MAX_PATH> path(base_path, "/", m_current_dir.c_str());
	os::FileIterator* iter = os::createFileIterator(path, m_app.getAllocator());
	os::FileInfo info;
	while (os::getNextFile(iter, &info)) {
		if (equalStrings(info.filename, ".")) continue;
		if (equalStrings(info.filename, "..")) continue;
		if (equalStrings(info.filename, ".lumix") && m_current_dir.length() == 0) continue;

		if (info.is_directory) {
			m_subdirs.emplace(info.filename, m_app.getAllocator());
		}
	}
	os::destroyFileIterator(iter);
}

bool DirSelector::breadcrumb(Span<const char> path) {
	if (path.length() == 0) {
		if (ImGui::Button(".")) {
			m_current_dir = "";
			fillSubitems();
			return true;
		}
		return false;
	}
	if (path.back() == '/') path = path.fromRight(1);
	
	Span<const char> dir = Path::getDir(path);
	Span<const char> basename = Path::getBasename(path);
	if (breadcrumb(dir)) return true;
	ImGui::SameLine();
	ImGui::TextUnformatted("/");
	ImGui::SameLine();
	
	char tmp[LUMIX_MAX_PATH];
	copyString(Span(tmp), basename);
	if (ImGui::Button(tmp)) {
		m_current_dir = String(path, m_app.getAllocator());
		fillSubitems();
		return true;
	}
	return false;
}

bool DirSelector::gui(const char* label, bool* open) {
	if (*open && !ImGui::IsPopupOpen(label)) {
		ImGui::OpenPopup(label);
		fillSubitems();
	}

	if (ImGui::BeginPopupModal(label, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		bool recently_open_create_folder = false;
		if (ImGui::Button(ICON_FA_PLUS " Create folder")) {
			m_creating_folder = true;
			m_new_folder_name[0] = '\0';
			recently_open_create_folder = true;
		}
		breadcrumb(Span(m_current_dir.c_str(), m_current_dir.length()));
		if (ImGui::BeginChild("list", ImVec2(300, 300), true, ImGuiWindowFlags_NoScrollbar)) {
			if (m_current_dir.length() > 0) {
				if (ImGui::Selectable(ICON_FA_LEVEL_UP_ALT "..", false, ImGuiSelectableFlags_DontClosePopups)) {
					Span<const char> dir = Path::getDir(m_current_dir.c_str());
					if (dir.length() > 0) dir = dir.fromRight(1);
					m_current_dir = String(dir, m_app.getAllocator());
					fillSubitems();
				}
			}
			
			if (m_creating_folder) {
				ImGui::SetNextItemWidth(-1);
				if (recently_open_create_folder) ImGui::SetKeyboardFocusHere();
				ImGui::InputTextWithHint("##nf", "New folder name", m_new_folder_name, sizeof(m_new_folder_name), ImGuiInputTextFlags_AutoSelectAll);
				if (ImGui::IsItemDeactivated()) {
					m_creating_folder = false;
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						if (m_new_folder_name[0]) {
							FileSystem& fs = m_app.getEngine().getFileSystem();
							StaticString<LUMIX_MAX_PATH> fullpath(fs.getBasePath(), m_current_dir.c_str(), "/", m_new_folder_name);
							if (!os::makePath(fullpath)) {
								logError("Failed to create ", fullpath);
							}
							else {
								m_current_dir.cat("/").cat(m_new_folder_name); 
								m_new_folder_name[0] = '\0';
							}
							fillSubitems();
						}
					}
				}
			}

			for (const String& subdir : m_subdirs) {
				ImGui::TextUnformatted(ICON_FA_FOLDER); ImGui::SameLine();
				if (ImGui::Selectable(subdir.c_str(), false, ImGuiSelectableFlags_DontClosePopups)) {
					m_current_dir.cat("/");
					m_current_dir.cat(subdir.c_str());
					fillSubitems();
				}
			}
		}
		ImGui::EndChild();
	
		bool res = ImGui::Button(ICON_FA_CHECK " Select");
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_TIMES " Cancel")) ImGui::CloseCurrentPopup();
	
		if (res) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		if (!ImGui::IsPopupOpen(label)) *open = false;
		return res;
	}
	return false;
}

FileSelector::FileSelector(StudioApp& app)
	: m_app(app)
	, m_filename(app.getAllocator())
	, m_full_path(app.getAllocator())
	, m_current_dir(app.getAllocator())
	, m_subdirs(app.getAllocator())
	, m_subfiles(app.getAllocator())
	, m_accepted_extension(app.getAllocator())
{}

const char* FileSelector::getPath() {
	Span<const char> span(m_full_path.c_str(), m_full_path.length());
	if (Path::getExtension(span).length() == 0) m_full_path.cat(".").cat(m_accepted_extension.c_str());
	return m_full_path.c_str();
}

bool FileSelector::gui(bool show_breadcrumbs) {
	bool res = false;
	ImGui::TextUnformatted("Filename"); ImGui::SameLine(); ImGui::SetNextItemWidth(-1);
	bool changed = inputString("##fn", &m_filename);
	
	if (show_breadcrumbs) {
		changed = breadcrumb(Span(m_current_dir.c_str(), m_current_dir.length())) || changed;
	}
	if (ImGui::BeginChild("list", ImVec2(300, 300), true, ImGuiWindowFlags_NoScrollbar)) {
		if (m_current_dir.length() > 0) {
			if (ImGui::Selectable(ICON_FA_LEVEL_UP_ALT "..", false, ImGuiSelectableFlags_DontClosePopups)) {
				Span<const char> dir = Path::getDir(m_current_dir.c_str());
				if (dir.length() > 0) dir = dir.fromRight(1);
				m_current_dir = String(dir, m_app.getAllocator());
				fillSubitems();
				changed = true;
			}
		}
		
		for (const String& subdir : m_subdirs) {
			ImGui::TextUnformatted(ICON_FA_FOLDER); ImGui::SameLine();
			if (ImGui::Selectable(subdir.c_str(), false, ImGuiSelectableFlags_DontClosePopups)) {
				m_current_dir.cat("/");
				m_current_dir.cat(subdir.c_str());
				fillSubitems();
				changed = true;
			}
		}
		
		for (const String& subfile : m_subfiles) {
			if (ImGui::Selectable(subfile.c_str(), false, ImGuiSelectableFlags_DontClosePopups | ImGuiSelectableFlags_AllowDoubleClick)) {
				m_filename = subfile;
				changed = true;
				if (ImGui::IsMouseDoubleClicked(0)) {
					res = true;
				}
			}
		}
	}
	ImGui::EndChild();
	if (changed) {
		m_full_path = m_current_dir;
		m_full_path.cat("/").cat(m_filename.c_str());
	}
	return res;
}

bool FileSelector::gui(const char* label, bool* open, const char* extension, bool save) {
	if (*open && !ImGui::IsPopupOpen(label)) {
		ImGui::OpenPopup(label);
		m_save = save;
		m_accepted_extension = extension;
		m_filename = "";
		m_full_path = "";
		fillSubitems();
	}

	bool res = false;
	if (ImGui::BeginPopupModal(label, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		res = gui(true);
	
		if (m_save) {
			if (ImGui::Button(ICON_FA_SAVE " Save")) {
				if (!Path::hasExtension(m_full_path.c_str(), m_accepted_extension.c_str())) {
					m_full_path.cat(".").cat(m_accepted_extension.c_str());
				}
				if (m_app.getEngine().getFileSystem().fileExists(m_full_path.c_str())) {
					ImGui::OpenPopup("warn_overwrite");
				}
				else {
					res = true;
				}
			}
		}
		else {
			if (ImGui::Button(ICON_FA_FOLDER_OPEN " Open")) {
				if (m_app.getEngine().getFileSystem().fileExists(m_full_path.c_str())) {
					res = true;
				}
			}
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_TIMES " Cancel")) ImGui::CloseCurrentPopup();
	
		if (ImGui::BeginPopup("warn_overwrite")) {
			ImGui::TextUnformatted("File already exists, are you sure you want to overwrite it?");
			if (ImGui::Selectable("Yes")) res = true;
			ImGui::Selectable("No");
			ImGui::EndPopup();
		}
		if (res) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		if (!ImGui::IsPopupOpen(label)) *open = false;
		return res;
	}
	return false;
}

enum { OUTPUT_FLAG = 1 << 31 };

NodeEditor::NodeEditor(IAllocator& allocator)
: SimpleUndoRedo(allocator)
{}

void NodeEditor::splitLink(const NodeEditorNode* node, Array<NodeEditorLink>& links, u32 link_idx) {
	if (node->hasInputPins() && node->hasOutputPins()) {
		NodeEditorLink& new_link = links.emplace();
		NodeEditorLink& link = links[link_idx];
		new_link.color = link.color;
		new_link.to = link.to;
		new_link.from = node->m_id;
		link.to = node->m_id;
		pushUndo(SimpleUndoRedo::NO_MERGE_UNDO);
	}
}

void NodeEditor::nodeEditorGUI(Span<NodeEditorNode*> nodes, Array<NodeEditorLink>& links) {
	m_canvas.begin();

	ImGuiEx::BeginNodeEditor("node_editor", &m_offset);
	const ImVec2 origin = ImGui::GetCursorScreenPos();

	ImGuiID moved = 0;
	ImGuiID unlink_moved = 0;
	u32 moved_count = 0;
	u32 unlink_moved_count = 0;
	for (NodeEditorNode* node : nodes) {
		const ImVec2 old_pos = node->m_pos;
		if (node->nodeGUI()) {
			pushUndo(node->m_id);
		}
		if (ImGui::IsMouseDragging(0) && ImGui::IsItemHovered()) m_dragged_node = node->m_id;
		if (old_pos.x != node->m_pos.x || old_pos.y != node->m_pos.y) {
			moved = node->m_id;
			++moved_count;
			if (ImGui::GetIO().KeyAlt) {
				u32 old_count = links.size();
					
				for (i32 i = links.size() - 1; i >= 0; --i) {
					const NodeEditorLink& link = links[i];
					if (link.getToNode() == node->m_id) {
						for (NodeEditorLink& rlink : links) {
							if (rlink.getFromNode() == node->m_id && rlink.getFromPin() == link.getToPin()) {
								rlink.from = link.from;
								links.erase(i);
							}
						}
					}
				}
					
				unlink_moved_count += old_count != links.size() ? 1 : 0;
				unlink_moved = node->m_id;
			}
		}
	}

	if (moved_count > 0) {
		if (unlink_moved_count > 1) pushUndo(NO_MERGE_UNDO);
		else if (unlink_moved_count == 1) pushUndo(unlink_moved);
		else if (moved_count > 1) pushUndo(NO_MERGE_UNDO - 1);
		else pushUndo(moved);
	}
		
	bool open_context = false;
	i32 hovered_link = -1;
	for (i32 i = 0, c = links.size(); i < c; ++i) {
		NodeEditorLink& link = links[i];
		ImGuiEx::NodeLinkEx(link.from | OUTPUT_FLAG, link.to, link.color, ImGui::GetColorU32(ImGuiCol_TabActive));
		if (ImGuiEx::IsLinkHovered()) {
			if (ImGui::IsMouseClicked(0) && ImGui::GetIO().KeyCtrl) {
				if (ImGuiEx::IsLinkStartHovered()) {
					ImGuiEx::StartNewLink(link.to, true);
				}
				else {
					ImGuiEx::StartNewLink(link.from | OUTPUT_FLAG, false);
				}
				links.erase(i);
				--c;
			}
			if (ImGui::IsMouseDoubleClicked(0)) {
				onLinkDoubleClicked(link, ImGui::GetMousePos() - origin - m_offset);
			}
			else {
				hovered_link = i;
			}
		}
	}

	if (hovered_link >= 0 && ImGui::IsMouseReleased(0) && ImGui::GetIO().KeyAlt) {
		i32 node_idx = nodes.find([this](const NodeEditorNode* node){ return node->m_id == m_dragged_node; });
		if (node_idx >= 0) {
			splitLink(nodes[node_idx], links, hovered_link);
		}
	}

	if (ImGui::IsMouseReleased(0)) m_dragged_node = 0xffFFffFF;

	{
		ImGuiID start_attr, end_attr;
		if (ImGuiEx::GetHalfLink(&start_attr)) {
			open_context = true;
			m_half_link_start = start_attr;
		}

		if (ImGuiEx::GetNewLink(&start_attr, &end_attr)) {
			ASSERT(start_attr & OUTPUT_FLAG);
			links.eraseItems([&](const NodeEditorLink& link) { return link.to == end_attr; });
			links.push({u32(start_attr) & ~OUTPUT_FLAG, u32(end_attr)});
			
			pushUndo(SimpleUndoRedo::NO_MERGE_UNDO);
		}
	}

	ImGuiEx::EndNodeEditor();
		
	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
		if (ImGui::GetIO().KeyAlt && hovered_link != -1) {
			links.erase(hovered_link);
			pushUndo(SimpleUndoRedo::NO_MERGE_UNDO);
		}
		else {
			onCanvasClicked(ImGui::GetMousePos() - origin - m_offset, hovered_link);
		}
	}

	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
		ImGui::OpenPopup("context_menu");
		open_context = true;
		m_half_link_start = 0;
	}

	if (ImGui::BeginPopup("context_menu")) {
		const ImVec2 pos = ImGui::GetMousePosOnOpeningCurrentPopup() - origin - m_offset;
		onContextMenu(pos);
		ImGui::EndPopup();
	}		

	m_is_any_item_active = ImGui::IsAnyItemActive();

	m_canvas.end();
}

RecentPaths::RecentPaths(const char* settings_name, u32 max_paths, StudioApp& app)
	: m_app(app)
	, m_settings_name(settings_name)
	, m_max_paths(max_paths)
	, m_paths(app.getAllocator())
{
}
	
void RecentPaths::onBeforeSettingsSaved() {
	Settings& settings = m_app.getSettings();
	for (const String& p : m_paths) {
		const u32 i = u32(&p - m_paths.begin());
		const StaticString<32> key(m_settings_name, i);
		settings.setValue(Settings::LOCAL, key, p.c_str());
	}
}

void RecentPaths::onSettingsLoaded() {
	m_paths.clear();
	char tmp[LUMIX_MAX_PATH];
	Settings& settings = m_app.getSettings();
	FileSystem& fs = m_app.getEngine().getFileSystem();
	for (u32 i = 0; ; ++i) {
		const StaticString<32> key(m_settings_name, i);
		const u32 len = settings.getValue(Settings::LOCAL, key, Span(tmp));
		if (len == 0) break;
		if (fs.fileExists(tmp)) m_paths.emplace(tmp, m_app.getAllocator());
	}}

void RecentPaths::push(const char* path) {
	String p(path, m_app.getAllocator());
	m_paths.eraseItems([&](const String& s) { return s == path; });
	m_paths.push(static_cast<String&&>(p));
	if (m_paths.size() > (i32)m_max_paths) {
		m_paths.erase(0);
	}
}

const char* RecentPaths::menu() {
	const char* res = nullptr;
	if (ImGui::BeginMenu("Recent", !m_paths.empty())) {
		for (const String& s : m_paths) {
			if (ImGui::MenuItem(s.c_str())) res = s.c_str();
		}
		ImGui::EndMenu();
	}
	ImGui::EndMenu();
	return res;
}


} // namespace Lumix