#pragma once
#include <imgui/IconsFontAwesome5.h>


struct lua_State;


namespace ImGuiEx
{

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

enum class PinShape {
	CIRCLE,
	TRIANGLE,
	SQUARE
};

IMGUI_API int CurveEditor(const char* label
	, float* values
	, int points_count
	, int capacity
	, const ImVec2& size = ImVec2(-1, -1)
	, ImU32 flags = 0
	, int* new_count = nullptr
	, int* selected_point = nullptr);
IMGUI_API bool BeginResizablePopup(const char* str_id, const ImVec2& size_on_first_use);
IMGUI_API bool BeginResizableMenu(const char* label, const char* icon, bool enabled);
IMGUI_API void HSplitter(const char* str_id, ImVec2* size);
IMGUI_API void VSplitter(const char* str_id, ImVec2* size);
IMGUI_API void Rect(float w, float h, ImU32 color);

IMGUI_API void BeginNodeEditor(const char* title, ImVec2* offset);
IMGUI_API void EndNodeEditor();
IMGUI_API ImVec2 GetNodeEditorOffset();
IMGUI_API void BeginNodeTitleBar();
IMGUI_API void BeginNodeTitleBar(ImU32 color);
IMGUI_API void EndNodeTitleBar();
IMGUI_API void NodeTitle(const char* text);
IMGUI_API void NodeTitle(const char* text, ImU32 color);
IMGUI_API void BeginNode(ImGuiID id, ImVec2& screen_pos, bool* selected);
IMGUI_API void EndNode();
IMGUI_API void Pin(ImGuiID id, bool is_input, PinShape shape = PinShape::CIRCLE);
IMGUI_API bool GetHalfLink(ImGuiID* from);
IMGUI_API void StartNewLink(ImGuiID from, bool is_input);
IMGUI_API bool GetNewLink(ImGuiID* from, ImGuiID* to);
IMGUI_API void NodeLink(ImGuiID from, ImGuiID to);
IMGUI_API void NodeLinkEx(ImGuiID from, ImGuiID to, ImU32 color, ImU32 active_color);
IMGUI_API bool IsLinkHovered();
IMGUI_API bool IsLinkStartHovered();

IMGUI_API bool InputRotation(const char* label, float* euler);
IMGUI_API void Label(const char* label);
IMGUI_API void TextClipped(const char* text, float size);
IMGUI_API bool IconButton(const char* icon, const char* tooltip);
IMGUI_API bool Gradient4(const char* label, int max_count, int* count, float* keys, float* values);
IMGUI_API void PushReadOnly();
IMGUI_API void PopReadOnly();
IMGUI_API bool filter(const char* hint, char* buf, int buf_size, float width = -1, bool set_keyboard_focus = false);

struct IMGUI_API Canvas {
	~Canvas();
	void begin();
	void end();

	ImVec2 m_origin;
	ImVec2 m_size = ImVec2(0, 0);
	float m_scale = 1.f;
	ImGuiContext* m_ctx = nullptr;
	ImGuiContext* m_original_ctx = nullptr;
};


} // namespace ImGuiEx
