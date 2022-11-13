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


struct SimpleUndoRedo {
	enum { NO_MERGE_UNDO = 0xffFFffFF };
	struct Undo {
		Undo(IAllocator& allocator) : blob(allocator) {}
		u32 tag;
		OutputMemoryStream blob;
	};

	SimpleUndoRedo(IAllocator& allocator)
		: m_stack(allocator)
		, m_allocator(allocator)
	{}

	bool canUndo() const { return m_stack_idx > 0; }
	bool canRedo() const { return m_stack_idx < m_stack.size() - 1; }

	void undo() {
		if (m_stack_idx <= 0) return;
	
		InputMemoryStream blob(m_stack[m_stack_idx - 1].blob);
		deserialize(blob);
		--m_stack_idx;
	}

	void redo() {
		if (m_stack_idx + 1 >= m_stack.size()) return;
	
		InputMemoryStream blob(m_stack[m_stack_idx + 1].blob);
		deserialize(blob);
		++m_stack_idx;
	}

	virtual void pushUndo(u32 tag) {
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

	void clearUndoStack() {
		m_stack.clear();
		m_stack_idx = -1;
	}

	virtual void deserialize(InputMemoryStream& blob) = 0;
	virtual void serialize(OutputMemoryStream& blob) = 0;

private:
	IAllocator& m_allocator;
	Array<Undo> m_stack;
	i32 m_stack_idx = -1;
};

template<typename ResourceType, typename NodeType, typename LinkType>
struct NodeEditor : SimpleUndoRedo {
	enum { OUTPUT_FLAG = 1 << 31 };

	NodeEditor(IAllocator& allocator)
		: SimpleUndoRedo(allocator)
	{}

	virtual void onCanvasClicked(ImVec2 pos) = 0;
	virtual void onLinkDoubleClicked(LinkType& link, ImVec2 pos) = 0;
	virtual void onContextMenu(bool recently_opened, ImVec2 pos) = 0;

	void nodeEditorGUI(ResourceType& resource) {
		m_canvas.begin();

		ImGuiEx::BeginNodeEditor("node_editor", &m_offset);
		const ImVec2 origin = ImGui::GetCursorScreenPos();

		ImGuiID moved = 0;
		ImGuiID unlink_moved = 0;
		u32 moved_count = 0;
		u32 unlink_moved_count = 0;
		for (NodeType& node : resource.m_nodes) {
			const ImVec2 old_pos = node->m_pos;
			if (node->nodeGUI()) {
				pushUndo(node->m_id);
			}
			if (old_pos.x != node->m_pos.x || old_pos.y != node->m_pos.y) {
				moved = node->m_id;
				++moved_count;
				if (ImGui::GetIO().KeyAlt) {
					u32 old_count = resource.m_links.size();
					
					for (i32 i = resource.m_links.size() - 1; i >= 0; --i) {
						const LinkType& link = resource.m_links[i];
						if (link.getToNode() == node->m_id) {
							for (LinkType& rlink : resource.m_links) {
								if (rlink.getFromNode() == node->m_id && rlink.getFromPin() == link.getToPin()) {
									rlink.from = link.from;
									resource.m_links.erase(i);
								}
							}
						}
					}
					
					unlink_moved_count += old_count != resource.m_links.size() ? 1 : 0;
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
		for (i32 i = 0, c = resource.m_links.size(); i < c; ++i) {
			LinkType& link = resource.m_links[i];
			ImGuiEx::NodeLinkEx(link.from | OUTPUT_FLAG, link.to, link.color, ImGui::GetColorU32(ImGuiCol_TabActive));
			if (ImGuiEx::IsLinkHovered()) {
				if (ImGui::IsMouseClicked(0) && ImGui::GetIO().KeyCtrl) {
					if (ImGuiEx::IsLinkStartHovered()) {
						ImGuiEx::StartNewLink(link.to, true);
					}
					else {
						ImGuiEx::StartNewLink(link.from | OUTPUT_FLAG, false);
					}
					resource.m_links.erase(i);
					--c;
				}
				if (ImGui::IsMouseDoubleClicked(0)) {
					onLinkDoubleClicked(link, ImGui::GetMousePos() - m_offset);
				}
				else {
					hovered_link = i;
				}
			}
		}

		{
			ImGuiID start_attr, end_attr;
			if (ImGuiEx::GetHalfLink(&start_attr)) {
				open_context = true;
				m_half_link_start = start_attr;
			}

			if (ImGuiEx::GetNewLink(&start_attr, &end_attr)) {
				ASSERT(start_attr & OUTPUT_FLAG);
				resource.m_links.eraseItems([&](const LinkType& link) { return link.to == end_attr; });
				resource.m_links.push({u32(start_attr) & ~OUTPUT_FLAG, u32(end_attr)});
			
				pushUndo(SimpleUndoRedo::NO_MERGE_UNDO);
			}
		}

		ImGuiEx::EndNodeEditor();
		
		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
			if (ImGui::GetIO().KeyAlt && hovered_link != -1) {
				resource.m_links.erase(hovered_link);
				pushUndo(SimpleUndoRedo::NO_MERGE_UNDO);
			}
			else {
				onCanvasClicked(ImGui::GetMousePos() - m_offset);
			}
		}

		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
			open_context = true;
			m_half_link_start = 0;
		}

		if (open_context) ImGui::OpenPopup("context_menu");

		if (ImGui::BeginPopup("context_menu")) {
			const ImVec2 pos = ImGui::GetMousePosOnOpeningCurrentPopup() - m_offset;
			onContextMenu(open_context, pos);
			ImGui::EndPopup();
		}		

		m_is_any_item_active = ImGui::IsAnyItemActive();

		m_canvas.end();
	}

	ImGuiEx::Canvas m_canvas;
	ImVec2 m_offset = ImVec2(0, 0);
	ImGuiID m_half_link_start = 0;
	bool m_is_any_item_active = false;
};

} // namespace Lumix
