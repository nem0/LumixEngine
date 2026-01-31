#pragma once

#include "engine/black.h.h"

#include "core/array.h"
#include "core/delegate.h"
#include "core/span.h"
#include "core/string.h"
#include "core/stream.h"

#include <imgui/imgui.h>

namespace black {

namespace os { enum class Keycode : u8; }

struct BLACK_EDITOR_API ResourceLocator {
	ResourceLocator(StringView path);

	StringView subresource;
	StringView dir;
	StringView basename;
	StringView ext;
	StringView resource;

	StringView full;
};

BLACK_EDITOR_API [[nodiscard]] bool menuItem(const struct Action& a, bool enabled);
BLACK_EDITOR_API void getEntityListDisplayName(StudioApp& app, struct World& editor, Span<char> buf, EntityPtr entity, bool force_display_index = false);
BLACK_EDITOR_API bool inputRotation(const char* label, struct Quat* value);
BLACK_EDITOR_API bool inputString(const char* label, String* value, ImGuiInputTextFlags flags = 0);
BLACK_EDITOR_API bool inputString(const char* str_id, const char* label, String* value);
BLACK_EDITOR_API bool inputStringMultiline(const char* label, String* value, const ImVec2& size = ImVec2(0, 0));
BLACK_EDITOR_API void openCenterStrip(const char* str_id);
BLACK_EDITOR_API bool beginCenterStrip(const char* str_id, u32 lines = 5);
BLACK_EDITOR_API void endCenterStrip();

struct BLACK_EDITOR_API SimpleUndoRedo {
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
	bool isReady() const { return !m_stack.empty(); }

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
	u32 color = ImGui::GetColorU32(ImGuiCol_Text);

	u16 getToNode() const { return to & 0xffFF; }
	u16 getFromNode() const { return from & 0xffFF; }

	u16 getToPin() const { return (to >> 16) & 0x7fFF; }
	u16 getFromPin() const { return (from >> 16) & 0x7fFF; }
};

struct NodeEditorNode {
	u16 m_id;
	ImVec2 m_pos;

	virtual ~NodeEditorNode() {}
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

	StudioApp& m_app;
	String m_current_dir;
	Array<String> m_subdirs;
};

struct BLACK_EDITOR_API FileSelector {
	FileSelector(StudioApp& app);
	FileSelector(const char* ext, StudioApp& app);
	// popup
	bool gui(const char* label, bool* open, const char* extension, bool save);
	// inplace
	bool gui(const char* extension);
	const char* getPath();

	String m_path;
private:
	void fillSubitems();
	
	StudioApp& m_app;
	bool m_save;
	String m_accepted_extension;
	Array<String> m_subdirs;
	Array<String> m_subfiles;
};

struct BLACK_EDITOR_API NodeEditor : SimpleUndoRedo {
	enum { OUTPUT_FLAG = 1 << 31 };

	NodeEditor(IAllocator& allocator);

	virtual void onCanvasClicked(ImVec2 pos, i32 hovered_link) = 0;
	virtual void onLinkDoubleClicked(NodeEditorLink& link, ImVec2 pos) = 0;
	virtual void onNodeDoubleClicked(NodeEditorNode& node) {}
	virtual void onContextMenu(ImVec2 pos) = 0;

	void splitLink(const NodeEditorNode* node, Array<NodeEditorLink>& links, u32 link_idx);
	void nodeEditorGUI(Span<NodeEditorNode*> nodes, Array<NodeEditorLink>& links);

	ImGuiEx::Canvas m_canvas;
	ImVec2 m_offset = ImVec2(0, 0);
	ImVec2 m_mouse_pos_canvas;
	ImGuiID m_half_link_start = 0;
	bool m_is_any_item_active = false;
	u32 m_dragged_node = 0xFFffFFff;
};

struct CodeEditor {
	using Tokenizer = bool (*)(const char* str, u32& token_len, u8& token_type, u8 prev_token_type);

	virtual ~CodeEditor() {}
	
	virtual u32 getCursorLine(u32 cursor_index = 0) = 0;
	virtual u32 getCursorColumn(u32 cursor_index = 0) = 0;
	virtual void setSelection(u32 from_line, u32 from_col, u32 to_line, u32 to_col, bool ensure_visibility) = 0;
	virtual void selectWord() = 0;
	virtual bool canHandleInput() = 0;
	virtual void underlineTokens(u32 line, u32 col_from, u32 col_to, const char* msg) = 0;
	virtual void insertText(const char* text) = 0;
	virtual u32 getNumCursors() = 0;
	virtual ImVec2 getCursorScreenPosition(u32 cursor_index = 0) = 0;
	virtual void focus() = 0;
	// get part of word left of cursor, usable for example for autocomplete
	virtual StringView getPrefix() = 0;
	
	virtual void setReadOnly(bool readonly) = 0;
	virtual void setText(StringView text) = 0;
	virtual void serializeText(OutputMemoryStream& blob) = 0;
	virtual void setTokenColors(Span<const u32> colors) = 0; // keep colors alive while CodeEditor uses them
	virtual void setTokenizer(Tokenizer tokenizer) = 0; // user needs to keep the language alive while CodeEditor uses it
	virtual bool gui(const char* str_id, const ImVec2& size, ImFont* code_font, ImFont* ui_font) = 0;

	static inline i32 s_font_size = 20;
	static inline bool s_show_line_numbers = true;
};

BLACK_EDITOR_API UniquePtr<CodeEditor> createCodeEditor(StudioApp& app);
BLACK_EDITOR_API UniquePtr<CodeEditor> createCppCodeEditor(StudioApp& app);
BLACK_EDITOR_API UniquePtr<CodeEditor> createLuaCodeEditor(StudioApp& app);
BLACK_EDITOR_API UniquePtr<CodeEditor> createHLSLCodeEditor(StudioApp& app);
BLACK_EDITOR_API UniquePtr<CodeEditor> createParticleScriptEditor(StudioApp& app);

template <typename F> void alignGUI(float align, const F& f) {
	const ImVec2 container_size = ImGui::GetContentRegionAvail();
	const ImVec2 cp = ImGui::GetCursorScreenPos();
	ImGuiStyle& style = ImGui::GetStyle();
	float alpha_backup = style.DisabledAlpha;
	style.DisabledAlpha = 0;
	ImGui::BeginDisabled();
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
							   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
							   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking;
	const char* id = "imgui_black.h_measure__";
	ImGui::Begin(id, nullptr, flags);
	ImGuiEx::SetSkipItems(false);
	ImGui::BeginGroup();
	f();
	ImGui::EndGroup();
	const ImVec2 size = ImGui::GetItemRectSize();
	ImGui::End();
	ImGui::EndDisabled();
	style.DisabledAlpha = alpha_backup;
	ImGui::SetCursorScreenPos(ImVec2(cp.x + (container_size.x - size.x) * align, cp.y));
	f();
}

template <typename F> void alignGUIRight(const F& f) { alignGUI(1, f); }
template <typename F> void alignGUICenter(const F& f) { alignGUI(0.5f, f); }


} // namespace black
