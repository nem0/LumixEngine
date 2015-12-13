#pragma once

namespace ImGui
{

int PlotHistogramEx(const char* label,
	float (*values_getter)(void* data, int idx),
	void* data,
	int values_count,
	int values_offset,
	const char* overlay_text,
	float scale_min,
	float scale_max,
	ImVec2 graph_size,
	int selected_index);

bool ListBox(const char* label,
	int* current_item,
	int scroll_to_item,
	bool (*items_getter)(void*, int, const char**),
	void* data,
	int items_count,
	int height_in_items);

void ResetActiveID();

ImVec2 GetWindowSizeContents();

void BeginNode(ImGuiID id, ImVec2 screen_pos);
void EndNode(ImVec2& pos);
bool NodePin(ImGuiID id, ImVec2 screen_pos);
void NodeLink(ImVec2 from, ImVec2 to);
ImVec2 GetNodeInputPos(ImGuiID node_id, int input);
ImVec2 GetNodeOutputPos(ImGuiID node_id, int output);
void NodeSlots(int count, bool input);


struct CurveEditor
{
	bool valid;
	ImVec2 beg_pos;
	ImVec2 prev_point;
	ImVec2 prev_tangent;
	int point_idx;
	int tangent_idx;
	int point_count;
};

CurveEditor BeginCurveEditor(const char* label);
bool CurvePoint(ImVec2* point, CurveEditor& editor);
float FindClosest(const ImVec2* points, const ImVec2& point);
void EndCurveEditor(const CurveEditor& editor);


} // namespace ImGui