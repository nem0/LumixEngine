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
#include "engine/world.h"


namespace Lumix
{


ResourceLocator::ResourceLocator(StringView path) {
	full = path;
	const char* c = path.begin;
	subresource.begin = c;
	while(c != path.end && *c != ':') {
		++c;
	}
	if(c != path.end) {
		subresource.end = c;
		dir.begin = c + 1;
	}
	else {
		subresource.end = subresource.begin;
		dir.begin = path.begin;
	}
	
	ext.end = path.end;
	ext.begin = reverseFind(StringView(dir.begin, ext.end), '.');
	if (ext.begin) {
		basename.end = ext.begin;
		++ext.begin;
	}
	else {
		ext.begin = ext.end;
		basename.end = path.end;
	}
	basename.begin = reverseFind(StringView(dir.begin, basename.end), '/');
	if (!basename.begin) basename.begin = reverseFind(StringView(dir.begin, basename.end), '\\');
	if (basename.begin)  {
		dir.end = basename.begin;
		++basename.begin;
	}
	else {
		basename.begin = dir.begin;
		dir.end = dir.begin;
	}
	resource.begin = dir.begin;
	resource.end = ext.end;
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


bool Action::isActive() const 
{
	if (ImGui::IsAnyItemFocused()) return false;
	if (shortcut == os::Keycode::INVALID && modifiers == 0) return false;

	if (shortcut != os::Keycode::INVALID && !os::isKeyDown(shortcut)) return false;
	
	Modifiers pressed_modifiers = Modifiers::NONE;
	if (os::isKeyDown(os::Keycode::ALT)) pressed_modifiers |= Modifiers::ALT;
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

bool menuItem(const Action& a, bool enabled) {
	char buf[20];
	getShortcut(a, Span(buf));
	return ImGui::MenuItem(a.label_short, buf, a.is_selected.invoke(), enabled);
}

void getEntityListDisplayName(StudioApp& app, World& world, Span<char> buf, EntityPtr entity)
{
	if (!entity.isValid())
	{
		buf[0] = '\0';
		return;
	}

	EntityRef e = (EntityRef)entity;
	const char* name = world.getEntityName(e);
	static const auto MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
	if (world.hasComponent(e, MODEL_INSTANCE_TYPE))
	{
		RenderInterface* render_interface = app.getRenderInterface();
		const Path path = render_interface->getModelInstancePath(world, e);
		if (!path.isEmpty())
		{
			const char* c = path.c_str();
			while (*c && *c != ':') ++c;
			if (*c == ':') {
				copyString(buf, StringView(path.c_str(), u32(c - path.c_str() + 1)));
				return;
			}

			copyString(buf, path);
			StringView basename = Path::getBasename(path);
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

bool inputStringMultiline(const char* label, String* value, const ImVec2& size) {
	ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput;
	return ImGui::InputTextMultiline(label, (char*)value->c_str(), value->length() + 1, size, flags, inputTextCallback, value);
}

bool inputString(const char* label, String* value) {
	ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
	return ImGui::InputText(label, (char*)value->c_str(), value->length() + 1, flags, inputTextCallback, value);
}

bool inputString(const char* str_id, const char* label, String* value) {
	ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
	ImGuiEx::Label(label);
	return ImGui::InputText(str_id, (char*)value->c_str(), value->length() + 1, flags, inputTextCallback, value);
}

bool inputRotation(const char* label, Quat* value) {
	Vec3 euler = value->toEuler();
	if (ImGuiEx::InputRotation(label, &euler.x)) {
		value->fromEuler(euler);
		return true;
	}
	return false;
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
	
	const Path path(base_path, "/", m_current_dir);
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


bool FileSelector::breadcrumb(StringView path) {
	if (path.empty()) {
		if (ImGui::Button(".")) {
			m_current_dir = "";
			fillSubitems();
			return true;
		}
		return false;
	}

	if (path.back() == '/') path.removeSuffix(1);
	
	StringView dir = Path::getDir(path);
	StringView basename = Path::getBasename(path);
	if (breadcrumb(dir)) return true;
	ImGui::SameLine();
	ImGui::TextUnformatted("/");
	ImGui::SameLine();
	
	char tmp[MAX_PATH];
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
	
	const Path path(base_path, "/", m_current_dir);
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

bool DirSelector::breadcrumb(StringView path) {
	if (path.empty()) {
		if (ImGui::Button(".")) {
			m_current_dir = "";
			fillSubitems();
			return true;
		}
		return false;
	}
	if (path.back() == '/') path.removeSuffix(1);
	
	StringView dir = Path::getDir(path);
	StringView basename = Path::getBasename(path);
	if (breadcrumb(dir)) return true;
	ImGui::SameLine();
	ImGui::TextUnformatted("/");
	ImGui::SameLine();
	
	char tmp[MAX_PATH];
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
		breadcrumb(m_current_dir);
		if (ImGui::BeginChild("list", ImVec2(300, 300), true, ImGuiWindowFlags_NoScrollbar)) {
			if (m_current_dir.length() > 0) {
				if (ImGui::Selectable(ICON_FA_LEVEL_UP_ALT "..", false, ImGuiSelectableFlags_DontClosePopups)) {
					StringView dir = Path::getDir(m_current_dir);
					if (!dir.empty()) dir.removeSuffix(1);
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
							const Path fullpath(fs.getBasePath(), m_current_dir, "/", m_new_folder_name);
							if (!os::makePath(fullpath.c_str())) {
								logError("Failed to create ", fullpath);
							}
							else {
								m_current_dir.append("/", m_new_folder_name); 
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
					m_current_dir.append("/", subdir.c_str());
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
	if (Path::getExtension(m_full_path).empty()) m_full_path.append(".", m_accepted_extension.c_str());
	return m_full_path.c_str();
}

bool FileSelector::gui(bool show_breadcrumbs, const char* accepted_extension) {
	if (m_accepted_extension != accepted_extension) {
		m_accepted_extension = accepted_extension;
		fillSubitems();
	}

	bool res = false;
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Filename"); ImGui::SameLine(); ImGui::SetNextItemWidth(-1);
	bool changed = inputString("##fn", &m_filename);
	if (ImGui::IsItemDeactivatedAfterEdit() && ImGui::IsKeyPressed(ImGuiKey_Enter)) res = true;

	if (show_breadcrumbs) {
		changed = breadcrumb(m_current_dir) || changed;
	}
	if (ImGui::BeginChild("list", ImVec2(300, 300), true, ImGuiWindowFlags_NoScrollbar)) {
		if (m_current_dir.length() > 0) {
			if (ImGui::Selectable(ICON_FA_LEVEL_UP_ALT "..", false, ImGuiSelectableFlags_DontClosePopups)) {
				StringView dir = Path::getDir(m_current_dir);
				if (!dir.empty()) dir.removeSuffix(1);
				m_current_dir = String(dir, m_app.getAllocator());
				fillSubitems();
				changed = true;
			}
		}
		
		for (const String& subdir : m_subdirs) {
			ImGui::TextUnformatted(ICON_FA_FOLDER); ImGui::SameLine();
			if (ImGui::Selectable(subdir.c_str(), false, ImGuiSelectableFlags_DontClosePopups)) {
				m_current_dir.append("/", subdir.c_str());
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
		m_full_path.append("/", m_filename.c_str());
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
		res = gui(true, extension);
	
		if (m_save) {
			if (ImGui::Button(ICON_FA_SAVE " Save")) {
				if (!Path::hasExtension(m_full_path, m_accepted_extension)) {
					m_full_path.append(".", m_accepted_extension.c_str());
				}
				if (m_app.getEngine().getFileSystem().fileExists(m_full_path)) {
					ImGui::OpenPopup("warn_overwrite");
				}
				else {
					res = true;
				}
			}
		}
		else {
			if (ImGui::Button(ICON_FA_FOLDER_OPEN " Open")) {
				if (m_app.getEngine().getFileSystem().fileExists(m_full_path)) {
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
		if (ImGui::IsItemHovered()) {
			if (ImGui::IsMouseDragging(0)) m_dragged_node = node->m_id;
			else if (ImGui::IsMouseDoubleClicked(0)) onNodeDoubleClicked(*node);
		}
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
			m_half_link_start = start_attr;
		}

		if (ImGuiEx::GetNewLink(&start_attr, &end_attr)) {
			ASSERT(start_attr & OUTPUT_FLAG);
			links.eraseItems([&](const NodeEditorLink& link) { return link.to == end_attr; });
			links.push({u32(start_attr) & ~OUTPUT_FLAG, u32(end_attr)});
			
			pushUndo(NO_MERGE_UNDO);
		}
	}

	ImGuiEx::EndNodeEditor();
		
	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
		if (ImGui::GetIO().KeyAlt && hovered_link != -1) {
			links.erase(hovered_link);
			pushUndo(NO_MERGE_UNDO);
		}
		else {
			onCanvasClicked(ImGui::GetMousePos() - origin - m_offset, hovered_link);
		}
	}

	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
		ImGui::OpenPopup("context_menu");
		m_half_link_start = 0;
	}

	if (ImGui::BeginPopup("context_menu")) {
		const ImVec2 pos = ImGui::GetMousePosOnOpeningCurrentPopup() - origin - m_offset;
		onContextMenu(pos);
		ImGui::EndPopup();
	}		

	m_is_any_item_active = ImGui::IsAnyItemActive();
	m_mouse_pos_canvas = ImGui::GetMousePos() - origin - m_offset;

	m_canvas.end();
}

bool TextFilter::pass(StringView text) const {
	for (u32 i = 0; i < count; ++i) {
		if (*subfilters[i].begin == '-') {
			if (findInsensitive(text, StringView(subfilters[i].begin + 1, subfilters[i].end))) return false;
		}
		else {
			if (!findInsensitive(text, subfilters[i])) return false;
		}
	}
	return true;
}

void TextFilter::build() {
	count = 0;
	StringView tmp;
	tmp.begin = filter;
	tmp.end = filter;
	for (;;) {
		if (*tmp.end == ' ' || *tmp.end == '\0') {
			if (tmp.size() > 0) {
				if (tmp.size() > 1 || *tmp.begin != '-') {
					subfilters[count] = tmp;
					++count;
					if (count == lengthOf(subfilters)) break;
				}
			}
			if (*tmp.end == '\0') break;
			tmp.begin = tmp.end + 1;
			tmp.end = tmp.begin;
		}
		else {
			++tmp.end;
		}
	}
}

bool TextFilter::gui(const char* hint, float width, bool set_keyboard_focus) {
	if (ImGuiEx::filter(hint, filter, sizeof(filter), width, set_keyboard_focus)) {
		build();
		return true;
	}
	return false;
}

} // namespace Lumix