#pragma once
#include <imgui/IconsFontAwesome5.h>


struct lua_State;


namespace ImGuiEx
{

struct TreeViewClipper {
	// persist
	float cursor_end = 0;
	float cursor_visible_start = 0;
	ImU32 first_visible_index = 0;
	float last_scroll = 0;
	float cursor_visible_end = 0;
	ImU32 visible_end_index = 0;
	bool full_pass = true;

	// valid only between begin end
	bool scrolled;
	bool met_visible;
	bool last_is_visible;
	ImU32 idx;
	float y;
	bool finished;
	ImU32 count;

	// returns index of first visible top-level node
	ImU32 Begin(ImU32 _count) {
		count = _count;
		scrolled = ImGui::GetScrollY() != last_scroll;
		if (scrolled) full_pass = true;
		if (full_pass) Refresh();

		// skip invisible space
		ImGui::SetCursorPosY(cursor_visible_start);

		// init runtime data
		met_visible = false;
		last_is_visible = true;
		idx = first_visible_index;
		finished = idx >= count;

		return idx;
	}

	void Refresh() {
		full_pass = false;
		last_scroll = ImGui::GetScrollY();
		first_visible_index = 0;
		cursor_visible_start = 0;
		cursor_end = 0;
	}

	bool BeginNode() {
		y = ImGui::GetCursorPosY();
		return !finished;
	}

	void EndNode() {
		const bool visible = ImGui::IsItemVisible();
		const bool is_first_visible = visible && !met_visible;
		if (is_first_visible) {
			met_visible = true;
			first_visible_index = idx;
			cursor_visible_start = y;
		}
		if (met_visible && !visible) {
			last_is_visible = false;
			y = ImGui::GetCursorPosY();
			if (cursor_end != 0) {
				// something has expended or collapsed
				if (y != cursor_visible_end && cursor_visible_end != 0) full_pass = true;
				if (idx != visible_end_index && visible_end_index != 0) full_pass = true;
				finished = true;
				cursor_visible_end = y;
				visible_end_index = idx;
			}
		}
		++idx;
		if (idx == count) finished = true;
	}

	void End() {
		if (cursor_end == 0 || last_is_visible) {
			cursor_end = ImGui::GetCursorPosY();
		}
		else {
			ImGui::SetCursorPosY(cursor_end - 2); // TODO why -2
		}
	}
};

IMGUI_API bool CheckboxEx(const char* label, bool* v);
IMGUI_API ImVec2 GetOsImePosRequest();
IMGUI_API void ResetActiveID();

IMGUI_API void BringToFront();

IMGUI_API bool BeginToolbar(const char* str_id, ImVec2 screen_pos, ImVec2 size);
IMGUI_API void EndToolbar();
IMGUI_API bool ToolbarButton(ImFont* font, const char* font_icon, const ImVec4& bg_color, const char* tooltip);

enum class CurveEditorFlags
{
	NO_TANGENTS = 1 << 0,
	SHOW_GRID = 1 << 1,
	RESET = 1 << 2
};

IMGUI_API int CurveEditor(const char* label
	, float* values
	, int points_count
	, const ImVec2& size = ImVec2(-1, -1)
	, ImU32 flags = 0
	, int* new_count = nullptr
	, int* selected_point = nullptr);
IMGUI_API bool BeginResizablePopup(const char* str_id, const ImVec2& size_on_first_use);
IMGUI_API void HSplitter(const char* str_id, ImVec2* size);
IMGUI_API void VSplitter(const char* str_id, ImVec2* size);
IMGUI_API void Rect(float w, float h, ImU32 color);

IMGUI_API void Label(const char* label);
IMGUI_API void TextClipped(const char* text, float size);
IMGUI_API bool IconButton(const char* icon, const char* tooltip);
IMGUI_API bool Gradient4(const char* label, int max_count, int* count, float* keys, float* values);

} // namespace ImGuiEx
