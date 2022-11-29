#pragma once

#include "engine/delegate.h"
#include "engine/lumix.h"
#include "engine/string.h"
#include "engine/stream.h"
#include <imgui/imgui.h>

namespace Lumix {

namespace os { enum class Keycode : u8; }

struct LUMIX_EDITOR_API ResourceLocator {
	ResourceLocator(const Span<const char>& path);

	Span<const char> subresource;
	Span<const char> dir;
	Span<const char> basename;
	Span<const char> ext;
	Span<const char> resource;

	Span<const char> full;
};


struct LUMIX_EDITOR_API Action
{
	enum Modifiers : u8 {
		NONE = 0,

		SHIFT = 1 << 0,
		ALT = 1 << 1,
		CTRL = 1 << 2
	};

	Action();
	void init(const char* label_short, const char* label_long, const char* name, const char* font_icon, os::Keycode key0, Modifiers modifiers, bool is_global);
	void init(const char* label_short, const char* label_long, const char* name, const char* font_icon, bool is_global);
	bool toolbarButton(struct ImFont* font);
	bool isActive();
	bool shortcutText(Span<char> out) const;

	static bool falseConst() { return false; }

	Modifiers modifiers = Modifiers::NONE;
	os::Keycode shortcut;
	StaticString<32> name;
	StaticString<32> label_short;
	StaticString<64> label_long;
	StaticString<5> font_icon;
	bool is_global;
	void* plugin;
	Delegate<void ()> func;
	Delegate<bool ()> is_selected;
};

inline Action::Modifiers operator |(Action::Modifiers a, Action::Modifiers b) { return Action::Modifiers((u8)a | (u8)b); }
inline void operator |= (Action::Modifiers& a, Action::Modifiers b) { a = a | b; }

LUMIX_EDITOR_API void getShortcut(const Action& action, Span<char> buf);
LUMIX_EDITOR_API void menuItem(Action& a, bool enabled);
LUMIX_EDITOR_API void getEntityListDisplayName(struct StudioApp& app, struct Universe& editor, Span<char> buf, EntityPtr entity);
LUMIX_EDITOR_API bool InputString(const char* label, String* value);

struct SimpleUndoRedo {
	enum { NO_MERGE_UNDO = 0xffFFffFF };
	struct Undo {
		Undo(IAllocator& allocator) : blob(allocator) {}
		u32 tag;
		OutputMemoryStream blob;
	};

	SimpleUndoRedo(IAllocator& allocator);
	virtual ~SimpleUndoRedo() {}

	bool canUndo() const;
	bool canRedo() const;
	void undo();
	void redo();
	virtual void pushUndo(u32 tag);
	void clearUndoStack();

	virtual void deserialize(InputMemoryStream& blob) = 0;
	virtual void serialize(OutputMemoryStream& blob) = 0;

private:
	IAllocator& m_allocator;
	Array<Undo> m_stack;
	i32 m_stack_idx = -1;
};

struct NodeEditorLink {
	u32 from;
	u32 to;
	u32 color = 0xffFFffFF;

	u16 getToNode() const { return to & 0xffFF; }
	u16 getFromNode() const { return from & 0xffFF; }

	u16 getToPin() const { return (to >> 16) & 0x7fFF; }
	u16 getFromPin() const { return (from >> 16) & 0x7fFF; }
};

struct NodeEditorNode {
	u16 m_id;
	ImVec2 m_pos;

	virtual bool hasInputPins() const = 0;
	virtual bool hasOutputPins() const = 0;
	virtual bool nodeGUI() = 0;
};

struct DirSelector {
	DirSelector(StudioApp& app);
	bool gui(const char* label, bool* open);
	const char* getDir() const { return m_current_dir.c_str(); }

private:
	void fillSubitems();
	bool breadcrumb(Span<const char> path);

	StudioApp& m_app;
	String m_current_dir;
	Array<String> m_subdirs;
	bool m_creating_folder = false;
	char m_new_folder_name[LUMIX_MAX_PATH] = "";
};

struct FileSelector {
	FileSelector(StudioApp& app);
	FileSelector(const char* ext, StudioApp& app);
	// popup
	bool gui(const char* label, bool* open, const char* extension, bool save);
	// inplace
	bool gui(bool show_breadcrumbs);
	const char* getPath();
	String m_current_dir;

private:
	bool breadcrumb(Span<const char> path);
	void fillSubitems();

	StudioApp& m_app;
	bool m_save;
	String m_filename;
	String m_accepted_extension;
	Array<String> m_subdirs;
	Array<String> m_subfiles;
	String m_full_path;
};

struct NodeEditor : SimpleUndoRedo {
	enum { OUTPUT_FLAG = 1 << 31 };

	NodeEditor(IAllocator& allocator);

	virtual void onCanvasClicked(ImVec2 pos, i32 hovered_link) = 0;
	virtual void onLinkDoubleClicked(NodeEditorLink& link, ImVec2 pos) = 0;
	virtual void onContextMenu(bool recently_opened, ImVec2 pos) = 0;

	void splitLink(const NodeEditorNode* node, Array<NodeEditorLink>& links, u32 link_idx);
	void nodeEditorGUI(Span<NodeEditorNode*> nodes, Array<NodeEditorLink>& links);

	ImGuiEx::Canvas m_canvas;
	ImVec2 m_offset = ImVec2(0, 0);
	ImGuiID m_half_link_start = 0;
	bool m_is_any_item_active = false;
	u32 m_dragged_node = 0xFFffFFff;
};

} // namespace Lumix
