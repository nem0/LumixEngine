#pragma once

namespace Lumix { namespace FS { class OsFile; } }


struct lua_State;


namespace ImGui
{

IMGUI_API int PlotHistogramEx(const char* label,
    float (*values_getter)(void* data, int idx),
    void* data,
    int values_count,
    int values_offset,
    const char* overlay_text,
    float scale_min,
    float scale_max,
    ImVec2 graph_size,
    int selected_index);

IMGUI_API bool ListBox(const char* label,
    int* current_item,
    int scroll_to_item,
    bool (*items_getter)(void*, int, const char**),
    void* data,
    int items_count,
    int height_in_items);

IMGUI_API void ResetActiveID();

IMGUI_API ImVec2 GetWindowSizeContents();
IMGUI_API void BringToFront();

IMGUI_API void BeginNode(ImGuiID id, ImVec2 screen_pos);
IMGUI_API void EndNode(ImVec2& pos);
IMGUI_API bool NodePin(ImGuiID id, ImVec2 screen_pos);
IMGUI_API void NodeLink(ImVec2 from, ImVec2 to);
IMGUI_API ImVec2 GetNodeInputPos(ImGuiID node_id, int input);
IMGUI_API ImVec2 GetNodeOutputPos(ImGuiID node_id, int output);
IMGUI_API void NodeSlots(int count, bool input);


struct CurveEditor
{
	bool valid;
	ImVec2 beg_pos;
	ImVec2 editor_size;
	ImVec2 prev_point;
	ImVec2 prev_tangent;
	int point_idx;
	int tangent_idx;
	int point_count;
};

struct CurvePointData
{
	ImVec2 left_tangent;
	ImVec2 point;
	ImVec2 right_tangent;
};

IMGUI_API CurveEditor BeginCurveEditor(const char* label);
IMGUI_API bool CurvePoint(ImVec2* point, CurveEditor& editor);
IMGUI_API void EndCurveEditor(const CurveEditor& editor);
IMGUI_API CurvePointData FindClosest(ImVec2* points, const ImVec2& point);


} // namespace ImGui


#include "imgui_dock.h"
