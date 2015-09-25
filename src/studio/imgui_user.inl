#include "ocornut-imgui/imgui.h"


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
		frame_bb.Min, frame_bb.Max, window->Color(ImGuiCol_FrameBg), true, style.FrameRounding);

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
		const float v1 = values_getter(data, (v_idx + 1 + values_offset) % values_count);
		ImGui::SetTooltip("%d: %8.4g", v_idx, v0);
		v_hovered = v_idx;
	}

	const float t_step = 1.0f / (float)res_w;

	float v0 = values_getter(data, (0 + values_offset) % values_count);
	float t0 = 0.0f;
	ImVec2 p0 = ImVec2(t0, 1.0f - ImSaturate((v0 - scale_min) / (scale_max - scale_min)));

	const ImU32 col_base = window->Color(ImGuiCol_PlotHistogram);
	const ImU32 col_hovered = window->Color(ImGuiCol_PlotHistogramHovered);

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


} // namespace ImGui