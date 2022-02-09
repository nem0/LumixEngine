#pragma once
#include <imgui/IconsFontAwesome5.h>


struct lua_State;


namespace ImGuiEx
{

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
IMGUI_API bool BeginResizableMenu(const char* label, const char* icon, bool enabled);
IMGUI_API void HSplitter(const char* str_id, ImVec2* size);
IMGUI_API void VSplitter(const char* str_id, ImVec2* size);
IMGUI_API void Rect(float w, float h, ImU32 color);

IMGUI_API bool InputRotation(const char* label, float* euler);
IMGUI_API void Label(const char* label);
IMGUI_API void TextClipped(const char* text, float size);
IMGUI_API bool IconButton(const char* icon, const char* tooltip);
IMGUI_API bool Gradient4(const char* label, int max_count, int* count, float* keys, float* values);
IMGUI_API void PushReadOnly();
IMGUI_API void PopReadOnly();

} // namespace ImGuiEx
