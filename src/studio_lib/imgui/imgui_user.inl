#include "imgui.h"
#include "imgui_internal.h"
#include "core/delegate.h"


static const ImVec2 NODE_WINDOW_PADDING(8.0f, 8.0f);
static const float NODE_SLOT_RADIUS = 4.0f;


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
	int selected_index)
{
	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems) return -1;

	ImGuiState& g = *GImGui;
	const ImGuiStyle& style = g.Style;

	const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
	if (graph_size.x == 0.0f) graph_size.x = CalcItemWidth() + (style.FramePadding.x * 2);
	if (graph_size.y == 0.0f) graph_size.y = label_size.y + (style.FramePadding.y * 2);

	const ImRect frame_bb(
		window->DC.CursorPos, window->DC.CursorPos + ImVec2(graph_size.x, graph_size.y));
	const ImRect inner_bb(frame_bb.Min + style.FramePadding, frame_bb.Max - style.FramePadding);
	const ImRect total_bb(frame_bb.Min,
		frame_bb.Max +
			ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0));
	ItemSize(total_bb, style.FramePadding.y);
	if (!ItemAdd(total_bb, NULL)) return -1;

	// Determine scale from values if not specified
	if (scale_min == FLT_MAX || scale_max == FLT_MAX)
	{
		float v_min = FLT_MAX;
		float v_max = -FLT_MAX;
		for (int i = 0; i < values_count; i++)
		{
			const float v = values_getter(data, i);
			v_min = ImMin(v_min, v);
			v_max = ImMax(v_max, v);
		}
		if (scale_min == FLT_MAX) scale_min = v_min;
		if (scale_max == FLT_MAX) scale_max = v_max;
	}

	RenderFrame(
		frame_bb.Min, frame_bb.Max, GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

	int res_w = ImMin((int)graph_size.x, values_count);

	// Tooltip on hover
	int v_hovered = -1;
	if (IsHovered(inner_bb, 0))
	{
		const float t = ImClamp(
			(g.IO.MousePos.x - inner_bb.Min.x) / (inner_bb.Max.x - inner_bb.Min.x), 0.0f, 0.9999f);
		const int v_idx = (int)(t * (values_count + 0));
		IM_ASSERT(v_idx >= 0 && v_idx < values_count);

		const float v0 = values_getter(data, (v_idx + values_offset) % values_count);
		ImGui::SetTooltip("%d: %8.4g", v_idx, v0);
		v_hovered = v_idx;
	}

	const float t_step = 1.0f / (float)res_w;

	float v0 = values_getter(data, (0 + values_offset) % values_count);
	float t0 = 0.0f;
	ImVec2 p0 = ImVec2(t0, 1.0f - ImSaturate((v0 - scale_min) / (scale_max - scale_min)));

	const ImU32 col_base = GetColorU32(ImGuiCol_PlotHistogram);
	const ImU32 col_hovered = GetColorU32(ImGuiCol_PlotHistogramHovered);

	for (int n = 0; n < res_w; n++)
	{
		const float t1 = t0 + t_step;
		const int v_idx = (int)(t0 * values_count + 0.5f);
		IM_ASSERT(v_idx >= 0 && v_idx < values_count);
		const float v1 = values_getter(data, (v_idx + values_offset + 1) % values_count);
		const ImVec2 p1 = ImVec2(t1, 1.0f - ImSaturate((v1 - scale_min) / (scale_max - scale_min)));

		window->DrawList->AddRectFilled(ImLerp(inner_bb.Min, inner_bb.Max, p0),
			ImLerp(inner_bb.Min, inner_bb.Max, ImVec2(p1.x, 1.0f)) + ImVec2(-1, 0),
			selected_index == v_idx ? col_hovered : col_base);

		t0 = t1;
		p0 = p1;
	}

	if (overlay_text)
	{
		RenderTextClipped(ImVec2(frame_bb.Min.x, frame_bb.Min.y + style.FramePadding.y),
			frame_bb.Max,
			overlay_text,
			NULL,
			NULL,
			ImGuiAlign_Center);
	}

	RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, inner_bb.Min.y), label);

	if (v_hovered >= 0 && IsMouseClicked(0))
	{
		return v_hovered;
	}
	return -1;
}


bool ImGui::ListBox(const char* label,
	int* current_item,
	int scroll_to_item,
	bool (*items_getter)(void*, int, const char**),
	void* data,
	int items_count,
	int height_in_items)
{
	if (!ImGui::ListBoxHeader(label, items_count, height_in_items))
		return false;

	// Assume all items have even height (= 1 line of text). If you need items of different or variable sizes you can create a custom version of ListBox() in your code without using the clipper.
	bool value_changed = false;
	if (scroll_to_item != -1)
	{
		ImGui::SetScrollY(scroll_to_item * ImGui::GetTextLineHeightWithSpacing());
	}
	ImGuiListClipper clipper(items_count, ImGui::GetTextLineHeightWithSpacing());
	for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
	{
		const bool item_selected = (i == *current_item);
		const char* item_text;
		if (!items_getter(data, i, &item_text))
			item_text = "*Unknown item*";

		ImGui::PushID(i);
		if (ImGui::Selectable(item_text, item_selected))
		{
			*current_item = i;
			value_changed = true;
		}
		ImGui::PopID();
	}
	clipper.End();
	ImGui::ListBoxFooter();
	return value_changed;
}


void ResetActiveID()
{
	SetActiveID(0);
}


void BringToFront()
{
	ImGuiState& g = *GImGui;

	auto* window = GImGui->CurrentWindow;

	if ((window->Flags & ImGuiWindowFlags_NoBringToFrontOnFocus) || g.Windows.back() == window)
	{
		return;
	}
	for (int i = 0; i < g.Windows.Size; i++)
	{
		if (g.Windows[i] == window)
		{
			g.Windows.erase(g.Windows.begin() + i);
			break;
		}
	}
	g.Windows.push_back(window);
}


ImVec2 GetWindowSizeContents()
{
	ImGuiWindow* window = GetCurrentWindowRead();
	return window->SizeContents;
}


static ImVec2 node_pos;
static ImGuiID last_node_id;


void BeginNode(ImGuiID id, ImVec2 screen_pos)
{
	PushID(id);
	last_node_id = id;
	node_pos = screen_pos;
	ImGui::SetCursorScreenPos(screen_pos + ImGui::GetStyle().WindowPadding);
	ImGui::PushItemWidth(200);
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	draw_list->ChannelsSplit(2);
	draw_list->ChannelsSetCurrent(1);
	ImGui::BeginGroup();
}


void EndNode(ImVec2& pos)
{
	ImDrawList* draw_list = ImGui::GetWindowDrawList();

	ImGui::EndGroup();
	ImGui::PopItemWidth();

	float height = ImGui::GetCursorScreenPos().y - node_pos.y;
	ImVec2 size(200, height + ImGui::GetStyle().WindowPadding.y);
	ImGui::SetCursorScreenPos(node_pos);

	ImGui::SetNextWindowPos(node_pos);
	ImGui::SetNextWindowSize(size);
	ImGui::BeginChild((ImGuiID)last_node_id, size, false , ImGuiWindowFlags_NoInputs);
	ImGui::EndChild();

	ImGui::SetCursorScreenPos(node_pos);
	ImGui::InvisibleButton("bg", size);
	if(ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
	{
		pos += ImGui::GetIO().MouseDelta;
	}

	draw_list->ChannelsSetCurrent(0);
	draw_list->AddRectFilled(node_pos, node_pos + size, ImColor(60, 60, 60), 4.0f);
	draw_list->AddRect(node_pos, node_pos + size, ImColor(100, 100, 100), 4.0f);

	PopID();
	draw_list->ChannelsMerge();
}


ImVec2 GetNodeInputPos(ImGuiID id, int input)
{
	ImGui::PushID(id);

	auto* parent_win = ImGui::GetCurrentWindow();
	char title[256];
	ImFormatString(title, IM_ARRAYSIZE(title), "%s.child_%08x", parent_win->Name, id);
	auto* win = FindWindowByName(title);
	if (!win)
	{
		ImGui::PopID();
		return ImVec2(0, 0);
	}

	ImVec2 pos = win->Pos;
	pos.x -= NODE_SLOT_RADIUS;
	auto& style = ImGui::GetStyle();
	pos.y += (ImGui::GetTextLineHeight() + style.ItemSpacing.y) * input;
	pos.y += style.WindowPadding.y + ImGui::GetTextLineHeight() * 0.5f;


	ImGui::PopID();
	return pos;
}


ImVec2 GetNodeOutputPos(ImGuiID id, int output)
{
	ImGui::PushID(id);

	auto* parent_win = ImGui::GetCurrentWindow();
	char title[256];
	ImFormatString(title, IM_ARRAYSIZE(title), "%s.child_%08x", parent_win->Name, id);
	auto* win = FindWindowByName(title);
	if (!win)
	{
		ImGui::PopID();
		return ImVec2(0, 0);
	}

	ImVec2 pos = win->Pos;
	pos.x += win->Size.x + NODE_SLOT_RADIUS;
	auto& style = ImGui::GetStyle();
	pos.y += (ImGui::GetTextLineHeight() + style.ItemSpacing.y) * output;
	pos.y += style.WindowPadding.y + ImGui::GetTextLineHeight() * 0.5f;

	ImGui::PopID();
	return pos;
}


bool NodePin(ImGuiID id, ImVec2 screen_pos)
{
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	ImGui::SetCursorScreenPos(screen_pos - ImVec2(NODE_SLOT_RADIUS, NODE_SLOT_RADIUS));
	ImGui::PushID(id);
	ImGui::InvisibleButton("", ImVec2(2 * NODE_SLOT_RADIUS, 2 * NODE_SLOT_RADIUS));
	bool hovered = ImGui::IsItemHovered();
	ImGui::PopID();
	draw_list->AddCircleFilled(screen_pos,
		NODE_SLOT_RADIUS,
		hovered ? ImColor(0, 150, 0, 150) : ImColor(150, 150, 150, 150));
	return hovered;
}


void NodeLink(ImVec2 from, ImVec2 to)
{
	ImVec2 p1 = from;
	ImVec2 t1 = ImVec2(+80.0f, 0.0f);
	ImVec2 p2 = to;
	ImVec2 t2 = ImVec2(+80.0f, 0.0f);
	const int STEPS = 12;
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	for(int step = 0; step <= STEPS; step++)
	{
		float t = (float)step / (float)STEPS;
		float h1 = +2 * t*t*t - 3 * t*t + 1.0f;
		float h2 = -2 * t*t*t + 3 * t*t;
		float h3 = t*t*t - 2 * t*t + t;
		float h4 = t*t*t - t*t;
		draw_list->PathLineTo(ImVec2(h1*p1.x + h2*p2.x + h3*t1.x + h4*t2.x, h1*p1.y + h2*p2.y + h3*t1.y + h4*t2.y));
	}
	draw_list->PathStroke(ImColor(200, 200, 100), false, 3.0f);
}


ImVec2 operator *(float f, const ImVec2& v)
{
	return ImVec2(f * v.x, f * v.y);
}


CurveEditor BeginCurveEditor(const char* label)
{
	CurveEditor editor;
	editor.valid = false;

	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems) return editor;

	ImGuiState& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	auto cursor_pos = ImGui::GetCursorScreenPos();

	const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
	ImVec2 graph_size;
	graph_size.x = CalcItemWidth() + (style.FramePadding.x * 2);
	graph_size.y = 100; // label_size.y + (style.FramePadding.y * 2);

	const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(graph_size.x, graph_size.y));
	const ImRect inner_bb(frame_bb.Min + style.FramePadding, frame_bb.Max - style.FramePadding);
	const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0));

	ItemSize(total_bb, style.FramePadding.y);
	if (!ItemAdd(total_bb, NULL))
		return editor;

	editor.valid = true;
	ImGui::PushID(label);

	RenderFrame(frame_bb.Min, frame_bb.Max, GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);
	RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, inner_bb.Min.y), label);

	editor.beg_pos = cursor_pos;
	ImGui::SetCursorScreenPos(cursor_pos);

	editor.point_idx = -1;

	return editor;
}


void EndCurveEditor(const CurveEditor& editor)
{
	ImGui::SetCursorScreenPos(editor.beg_pos);

	InvisibleButton("bg", ImVec2(ImGui::CalcItemWidth(), 100));
	ImGui::PopID();
}


bool CurvePoint(ImVec2* points, CurveEditor& editor)
{
	ImGuiWindow* window = GetCurrentWindow();
	ImGuiState& g = *GImGui;
	const ImGuiStyle& style = g.Style;

	auto cursor_pos_backup = ImGui::GetCursorScreenPos();

	ImVec2 graph_size;
	graph_size.x = CalcItemWidth() + (style.FramePadding.x * 2);
	graph_size.y = 100; // label_size.y + (style.FramePadding.y * 2);

	const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + graph_size);
	const ImRect inner_bb(frame_bb.Min + style.FramePadding, frame_bb.Max - style.FramePadding);
	const ImU32 col_base = GetColorU32(ImGuiCol_PlotLines);
	const ImU32 col_hovered = GetColorU32(ImGuiCol_PlotLinesHovered);

	auto left_tangent = points[0];
	auto right_tangent = points[2];
	auto p = points[1];
	auto transform = [inner_bb](const ImVec2& p) -> ImVec2
	{
		return ImVec2(inner_bb.Min.x * (1 - p.x) + inner_bb.Max.x * p.x,
			inner_bb.Min.y * p.y + inner_bb.Max.y * (1 - p.y));
	};

	auto pos = transform(p);
	if (editor.point_idx >= 0)
	{
		window->DrawList->AddBezierCurve(pos,
			transform(p + left_tangent),
			transform(editor.prev_point + editor.prev_tangent),
			transform(editor.prev_point),
			col_base,
			1.0f, 20);
	}
	editor.prev_point = p;
	editor.prev_tangent = right_tangent;

	static const float SIZE = 3;
	ImGui::SetCursorScreenPos(pos - ImVec2(SIZE, SIZE));
	ImGui::PushID(editor.point_idx);
	++editor.point_idx;
	ImGui::InvisibleButton("", ImVec2(2 * NODE_SLOT_RADIUS, 2 * NODE_SLOT_RADIUS));

	ImU32 col = ImGui::IsItemHovered() ? col_hovered : col_base;

	window->DrawList->AddLine(pos + ImVec2(-SIZE, 0), pos + ImVec2(0, SIZE), col);
	window->DrawList->AddLine(pos + ImVec2(SIZE, 0), pos + ImVec2(0, SIZE), col);
	window->DrawList->AddLine(pos + ImVec2(SIZE, 0), pos + ImVec2(0, -SIZE), col);
	window->DrawList->AddLine(pos + ImVec2(-SIZE, 0), pos + ImVec2(0, -SIZE), col);

	bool changed = false;
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
	{
		pos += ImGui::GetIO().MouseDelta;
		ImVec2 v;
		v.x = (pos.x - inner_bb.Min.x) / (inner_bb.Max.x - inner_bb.Min.x);
		v.y = (inner_bb.Max.y - pos.y) / (inner_bb.Max.y - inner_bb.Min.y);

		v = ImClamp(v, ImVec2(0, 0), ImVec2(1, 1));
		points[1] = v;
		changed = true;
	}
	ImGui::PopID();

	ImGui::SetCursorScreenPos(cursor_pos_backup);
	return changed;
}



} // namespace ImGui