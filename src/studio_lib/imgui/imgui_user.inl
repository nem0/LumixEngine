#include "imgui.h"
#include "imgui_internal.h"
#include "core/delegate.h"
#include "core/fs/os_file.h"
#include "core/string.h"
#include <lua.hpp>


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
	if (!ImGui::ListBoxHeader(label, items_count, height_in_items)) return false;

	// Assume all items have even height (= 1 line of text). If you need items of different or
	// variable sizes you can create a custom version of ListBox() in your code without using the
	// clipper.
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
		if (!items_getter(data, i, &item_text)) item_text = "*Unknown item*";

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
	ImGui::BeginChild((ImGuiID)last_node_id, size, false, ImGuiWindowFlags_NoInputs);
	ImGui::EndChild();

	ImGui::SetCursorScreenPos(node_pos);
	ImGui::InvisibleButton("bg", size);
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
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
	for (int step = 0; step <= STEPS; step++)
	{
		float t = (float)step / (float)STEPS;
		float h1 = +2 * t * t * t - 3 * t * t + 1.0f;
		float h2 = -2 * t * t * t + 3 * t * t;
		float h3 = t * t * t - 2 * t * t + t;
		float h4 = t * t * t - t * t;
		draw_list->PathLineTo(ImVec2(h1 * p1.x + h2 * p2.x + h3 * t1.x + h4 * t2.x,
			h1 * p1.y + h2 * p2.y + h3 * t1.y + h4 * t2.y));
	}
	draw_list->PathStroke(ImColor(200, 200, 100), false, 3.0f);
}


ImVec2 operator*(float f, const ImVec2& v)
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

	const ImRect frame_bb(
		window->DC.CursorPos, window->DC.CursorPos + ImVec2(graph_size.x, graph_size.y));
	const ImRect inner_bb(frame_bb.Min + style.FramePadding, frame_bb.Max - style.FramePadding);
	const ImRect total_bb(frame_bb.Min,
		frame_bb.Max +
			ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0));

	ItemSize(total_bb, style.FramePadding.y);
	if (!ItemAdd(total_bb, NULL)) return editor;

	editor.valid = true;
	ImGui::PushID(label);

	RenderFrame(
		frame_bb.Min, frame_bb.Max, GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);
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
			1.0f,
			20);
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


struct DockContext
{

	enum class Slot
	{
		LEFT,
		RIGHT,
		TOP,
		BOTTOM,
		TAB,

		FLOAT,
		NONE
	};


	struct Dock
	{
		enum Status
		{
			DOCKED,
			FLOAT,
			DRAGGED
		};

		Dock(const char* _label)
			: id(ImHash(_label, 0))
			, next(nullptr)
			, prev(nullptr)
			, parent(nullptr)
		{
			pos = ImVec2(0, 0);
			size = ImVec2(-1, -1);
			active = true;
			children[0] = children[1] = nullptr;
			status = FLOAT;
			Lumix::copyString(label, _label);
		}


		void setParent(Dock* dock)
		{
			parent = dock;
			for (auto* tmp = prev; tmp; tmp = tmp->prev) tmp->parent = dock;
			for (auto* tmp = next; tmp; tmp = tmp->next) tmp->parent = dock;
		}

		Dock& getSibling()
		{
			IM_ASSERT(parent);
			if (parent->children[0] == &getFirst()) return *parent->children[1];
			return *parent->children[0];
		}

		Dock& getFirst()
		{
			auto* tmp = this;
			while (tmp->prev) tmp = tmp->prev;
			return *tmp;
		}

		void setActive()
		{
			active = true;
			for (auto* tmp = prev; tmp; tmp = tmp->prev) tmp->active = false;
			for (auto* tmp = next; tmp; tmp = tmp->next) tmp->active = false;
		}


		bool isContainer() const { return children[0] != nullptr; }


		void setChildrenPosSize(const ImVec2& _pos, const ImVec2& _size)
		{
			if (children[0]->pos.x < children[1]->pos.x)
			{
				auto s = children[0]->size;
				s.y = _size.y;
				children[0]->setPosSize(_pos, s);

				s.x = _size.x - children[0]->size.x;
				auto p = _pos;
				p.x += children[0]->size.x;
				children[1]->setPosSize(p, s);
			}
			else if (children[0]->pos.x > children[1]->pos.x)
			{
				auto s = children[1]->size;
				s.y = _size.y;
				children[1]->setPosSize(_pos, s);

				s.x = _size.x - children[1]->size.x;
				auto p = _pos;
				p.x += children[1]->size.x;
				children[0]->setPosSize(p, s);
			}
			else if (children[0]->pos.y < children[1]->pos.y)
			{
				auto s = children[0]->size;
				s.x = _size.x;
				children[0]->setPosSize(_pos, s);

				s.y = _size.y - children[0]->size.y;
				auto p = _pos;
				p.y += children[0]->size.y;
				children[1]->setPosSize(p, s);
			}
			else
			{
				auto s = children[1]->size;
				s.x = _size.x;
				children[1]->setPosSize(_pos, s);

				s.y = _size.y - children[1]->size.y;
				auto p = _pos;
				p.y += children[1]->size.y;
				children[0]->setPosSize(p, s);
			}
		}


		void setPosSize(const ImVec2& _pos, const ImVec2& _size)
		{
			size = _size;
			pos = _pos;
			for (auto* tmp = prev; tmp; tmp = tmp->prev)
			{
				tmp->size = _size;
				tmp->pos = _pos;
			}
			for (auto* tmp = next; tmp; tmp = tmp->next)
			{
				tmp->size = _size;
				tmp->pos = _pos;
			}

			if (!isContainer()) return;
			setChildrenPosSize(_pos, _size);
		}

		char label[50];
		ImU32 id;
		Dock* next;
		Dock* prev;
		Dock* children[2];
		Dock* parent;
		bool active;
		ImVec2 pos;
		ImVec2 size;
		Status status;
	};


	Dock* docks[32];
	int count = 0;
	Dock* current = nullptr;
	int last_frame = 0;
	int end_action;


	Dock& getDock(const char* label, bool opened)
	{
		ImU32 id = ImHash(label, 0);
		for (int i = 0; i < count; ++i)
		{
			if (docks[i]->id == id) return *docks[i];
		}

		IM_ASSERT(count < IM_ARRAYSIZE(docks) - 1);

		++count;
		docks[count - 1] = (Dock*)ImGui::MemAlloc(sizeof(Dock));
		new (docks[count - 1]) Dock(label);
		static Dock* q = nullptr;
		docks[count - 1]->id = id;
		docks[count - 1]->setActive();
		if (opened)
		{
			if (q)
			{
				q->prev = docks[count - 1];
				docks[count - 1]->next = q;
				docks[count - 1]->setPosSize(q->pos, q->size);
			}
			docks[count - 1]->status = Dock::DOCKED;
			q = docks[count - 1];
		}
		else
		{
			docks[count - 1]->status = Dock::FLOAT;
		}
		return *docks[count - 1];
	}


	void drawSplits()
	{
		if (ImGui::GetFrameCount() == last_frame) return;
		last_frame = ImGui::GetFrameCount();

		auto* win = ImGui::GetCurrentWindow();
		auto& g = *GImGui;
		if (g.Windows[0] != win)
		{
			for (int i = 0; i < g.Windows.Size; i++)
			{
				if (g.Windows[i] == win)
				{
					for (int j = i - 1; j >= 0; --j)
					{
						g.Windows[j + 1] = g.Windows[j];
					}
					g.Windows[0] = win;
					break;
				}
			}
		}

		ImU32 color = ImGui::GetColorU32(ImGuiCol_Button);
		ImU32 color_hovered = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
		char str_id[20];
		auto* draw_list = ImGui::GetWindowDrawList();
		for (int i = 0; i < count; ++i)
		{
			if (!docks[i]->isContainer()) continue;

			Lumix::toCString(i, str_id, IM_ARRAYSIZE(str_id));
			Lumix::catString(str_id, "split");

			if (!ImGui::GetIO().MouseDown[0]) docks[i]->status = Dock::DOCKED;

			auto p0 = docks[i]->children[0]->pos;
			auto p1 = docks[i]->children[1]->pos;
			auto size = docks[i]->children[0]->size;
			if (p0.x < p1.x)
			{
				ImGui::SetCursorScreenPos(p1);
				ImGui::InvisibleButton(str_id, ImVec2(3, size.y));
				if (docks[i]->status == Dock::DRAGGED)
				{
					docks[i]->children[0]->size.x += ImGui::GetIO().MouseDelta.x;
					docks[i]->children[1]->size.x -= ImGui::GetIO().MouseDelta.x;
					docks[i]->children[1]->pos.x += ImGui::GetIO().MouseDelta.x;
				}
			}
			else if (p0.x > p1.x)
			{
				ImGui::SetCursorScreenPos(p0);
				ImGui::InvisibleButton(str_id, ImVec2(3, size.y));
				if (docks[i]->status == Dock::DRAGGED)
				{
					docks[i]->children[1]->size.x += ImGui::GetIO().MouseDelta.x;
					docks[i]->children[0]->size.x -= ImGui::GetIO().MouseDelta.x;
					docks[i]->children[0]->pos.x += ImGui::GetIO().MouseDelta.x;
				}
			}
			else if (p0.y < p1.y)
			{
				ImGui::SetCursorScreenPos(p1);
				ImGui::InvisibleButton(str_id, ImVec2(size.x, 3));
				if (docks[i]->status == Dock::DRAGGED)
				{
					docks[i]->children[0]->size.y += ImGui::GetIO().MouseDelta.y;
					docks[i]->children[1]->size.y -= ImGui::GetIO().MouseDelta.y;
					docks[i]->children[1]->pos.y += ImGui::GetIO().MouseDelta.y;
				}
			}
			else
			{
				ImGui::SetCursorScreenPos(p0);
				ImGui::InvisibleButton(str_id, ImVec2(size.x, 3));
				if (docks[i]->status == Dock::DRAGGED)
				{
					docks[i]->children[1]->size.y += ImGui::GetIO().MouseDelta.y;
					docks[i]->children[0]->size.y -= ImGui::GetIO().MouseDelta.y;
					docks[i]->children[0]->pos.y += ImGui::GetIO().MouseDelta.y;
				}
			}

			if (ImGui::IsItemHoveredRect() && ImGui::IsMouseClicked(0))
			{
				docks[i]->status = Dock::DRAGGED;
			}
			if (docks[i]->status == Dock::DRAGGED)
			{
				docks[i]->children[0]->setPosSize(
					docks[i]->children[0]->pos, docks[i]->children[0]->size);
				docks[i]->children[1]->setPosSize(
					docks[i]->children[1]->pos, docks[i]->children[1]->size);
			}

			draw_list->AddRectFilled(ImGui::GetItemRectMin(),
				ImGui::GetItemRectMax(),
				ImGui::IsItemHoveredRect() ? color_hovered : color);
		}
	}


	void beginPanel()
	{
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
								 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
								 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
								 ImGuiWindowFlags_NoScrollWithMouse;
		ImVec2 pos(0, ImGui::GetTextLineHeightWithSpacing());
		ImGui::SetNextWindowPos(pos);
		ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize - pos);
		ImGui::Begin("###DockPanel", nullptr, flags);
		drawSplits();
	}


	void endPanel() { ImGui::End(); }


	Dock* getDockAt(const ImVec2& pos) const
	{
		for (int i = 0; i < count; ++i)
		{
			auto& dock = *docks[i];
			if (dock.isContainer()) continue;
			if (dock.status != Dock::DOCKED) continue;
			if (ImGui::IsMouseHoveringRect(dock.pos, dock.pos + dock.size, false))
			{
				return &dock;
			}
		}

		return nullptr;
	}


	static ImRect getDockedRect(Dock& dock, Slot dock_slot)
	{
		auto half_size = dock.size * 0.5f;
		switch (dock_slot)
		{
			default: return ImRect(dock.pos, dock.pos + dock.size);
			case Slot::TOP: return ImRect(dock.pos, dock.pos + ImVec2(dock.size.x, half_size.y));
			case Slot::RIGHT:
				return ImRect(dock.pos + ImVec2(half_size.x, 0), dock.pos + dock.size);
			case Slot::BOTTOM:
				return ImRect(dock.pos + ImVec2(0, half_size.y), dock.pos + dock.size);
			case Slot::LEFT: return ImRect(dock.pos, dock.pos + ImVec2(half_size.x, dock.size.y));
		}
	}

	static ImRect getRect(Dock& dock, Slot dock_slot)
	{
		auto center = dock.pos + dock.size * 0.5f;
		switch (dock_slot)
		{
			default: return ImRect(center - ImVec2(20, 20), center + ImVec2(20, 20));
			case Slot::TOP: return ImRect(center + ImVec2(-20, -50), center + ImVec2(20, -30));
			case Slot::RIGHT: return ImRect(center + ImVec2(30, -20), center + ImVec2(50, 20));
			case Slot::BOTTOM: return ImRect(center + ImVec2(-20, +30), center + ImVec2(20, 50));
			case Slot::LEFT: return ImRect(center + ImVec2(-50, -20), center + ImVec2(-30, 20));
		}
	}


	void handleDrag(Dock& dock)
	{
		auto* dest_dock = getDockAt(ImGui::GetIO().MousePos);
		if (!dest_dock)
		{
			if (!ImGui::GetIO().MouseDown[0]) dock.status = Dock::FLOAT;
			return;
		}

		ImGui::Begin("##Overlay",
			NULL,
			ImVec2(0, 0),
			0.f,
			ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_AlwaysAutoResize);
		auto* canvas = ImGui::GetWindowDrawList();
		auto a = dest_dock->pos + dest_dock->size * 0.5f - ImVec2(20, 20);
		auto b = a + ImVec2(40, 40);

		auto mouse_pos = ImGui::GetIO().MousePos;
		canvas->PushClipRectFullScreen();

		auto text_color = ImGui::GetColorU32(ImGuiCol_Text);
		auto color = ImGui::GetColorU32(ImGuiCol_Button);
		auto color_hovered = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
		auto docked_color = ImGui::GetColorU32(ImGuiCol_FrameBg);
		bool any_hovered = false;
		for (int i = 0; i < 5; ++i)
		{
			auto r = getRect(*dest_dock, (Slot)i);
			bool hovered = r.Contains(mouse_pos);
			canvas->AddRectFilled(r.Min, r.Max, hovered ? color_hovered : color);
			if (hovered)
			{
				any_hovered = true;
				if (!ImGui::GetIO().MouseDown[0])
				{
					doDock(dock, *dest_dock, (Slot)i);
					canvas->PopClipRect();
					ImGui::End();
					return;
				}
				auto docked_rect = getDockedRect(*dest_dock, (Slot)i);
				canvas->AddRectFilled(docked_rect.Min, docked_rect.Max, docked_color);
				canvas->AddText(docked_rect.Min, text_color, dock.label);
			}
		}
		if (!any_hovered)
		{
			auto mp = ImGui::GetMousePos();
			canvas->AddRectFilled(mp, mp + dock.size, docked_color);
			canvas->AddText(mp, text_color, dock.label);
		}
		canvas->PopClipRect();

		if (!ImGui::GetIO().MouseDown[0])
		{
			doDock(dock, *dest_dock, Slot::NONE);
		}
		ImGui::End();
	}


	void doUndock(Dock& dock)
	{
		bool remove_container = false;
		if (dock.prev)
			dock.prev->setActive();
		else if (dock.next)
			dock.next->setActive();
		else
			dock.active = false;

		if (dock.parent)
		{
			if (dock.parent->children[0] == &dock)
			{
				if (dock.next)
					dock.parent->children[0] = dock.next;
				else
				{
					IM_ASSERT(!dock.prev);
					remove_container = true;
				}
			}
			else if (dock.parent->children[1] == &dock)
			{
				if (dock.next)
					dock.parent->children[1] = dock.next;
				else
				{
					IM_ASSERT(!dock.prev);
					remove_container = true;
				}
			}
			else
			{
				IM_ASSERT(&dock.getFirst() == dock.parent->children[0] ||
					   &dock.getFirst() == dock.parent->children[1]);
			}
		}

		if (remove_container)
		{
			auto* container = dock.parent;
			if (container->parent)
			{
				if (container->parent->children[0] == container)
				{
					container->parent->children[0] = &dock.getSibling();
					container->parent->children[0]->setPosSize(container->pos, container->size);
					container->parent->children[0]->setParent(container->parent);
				}
				else
				{
					IM_ASSERT(container->parent->children[1] == container);
					container->parent->children[1] = &dock.getSibling();
					container->parent->children[1]->setPosSize(container->pos, container->size);
					container->parent->children[1]->setParent(container->parent);
				}
			}
			else
			{
				container->children[0]->setParent(nullptr);
				container->children[0]->setPosSize(container->pos, container->size);
				container->children[1]->setParent(nullptr);
				container->children[1]->setPosSize(container->pos, container->size);
			}
			container->~Dock();
			ImGui::MemFree(container);
			for (int i = 0; i < count; ++i)
			{
				if (docks[i] == container)
				{
					docks[i] = docks[count - 1];
					--count;
					break;
				}
			}
		}
		else if (dock.next)
		{
			if (dock.prev)
			{
				dock.next->prev = dock.prev;
				dock.prev->next = dock.next;
			}
			else
			{
				dock.next->prev = nullptr;
			}
		}
		else if (dock.prev)
		{
			dock.prev->next = nullptr;
		}
		dock.parent = nullptr;
		dock.prev = dock.next = nullptr;
	}


	void drawTabbarListButton(Dock& dock)
	{
		if (!dock.next) return;
		
		auto* draw_list = ImGui::GetWindowDrawList();
		if (ImGui::InvisibleButton("list", ImVec2(16, 16)))
		{
			ImGui::OpenPopup("tab_list_popup");
		}
		if (ImGui::BeginPopup("tab_list_popup"))
		{
			auto* tmp = &dock;
			while (tmp)
			{
				bool dummy = false;
				if (ImGui::Selectable(tmp->label, &dummy))
				{
					tmp->setActive();
				}
				tmp = tmp->next;
			}
			ImGui::EndPopup();
		}

		bool hovered = ImGui::IsItemHovered();
		auto min = ImGui::GetItemRectMin();
		auto max = ImGui::GetItemRectMax();
		auto center = (min + max) * 0.5f;
		ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);
		ImU32 color_active = ImGui::GetColorU32(ImGuiCol_FrameBgActive);
		draw_list->AddRectFilled(ImVec2(center.x - 4, min.y + 3),
			ImVec2(center.x + 4, min.y + 5),
			hovered ? color_active : text_color);
		draw_list->AddTriangleFilled(ImVec2(center.x - 4, min.y + 7),
			ImVec2(center.x + 4, min.y + 7),
			ImVec2(center.x, min.y + 12),
			hovered ? color_active : text_color);
	}


	void drawTabbar(Dock& dock)
	{
		auto tabbar_height = 2 * ImGui::GetTextLineHeightWithSpacing();
		auto size = ImVec2(dock.size.x, tabbar_height);

		ImGui::SetCursorScreenPos(dock.pos);
		char tmp[256];
		Lumix::toCString(dock.id, tmp, IM_ARRAYSIZE(tmp));
		Lumix::catString(tmp, "_tabs");
		if (ImGui::BeginChild(tmp, size, true))
		{
			auto* dock_tab = &dock;

			auto* draw_list = ImGui::GetWindowDrawList();
			ImU32 color = ImGui::GetColorU32(ImGuiCol_FrameBg);
			ImU32 color_active = ImGui::GetColorU32(ImGuiCol_FrameBgActive);
			ImU32 color_hovered = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
			ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);
			auto line_height = ImGui::GetTextLineHeightWithSpacing();
			float tab_base;
			
			drawTabbarListButton(dock);

			while (dock_tab)
			{
				ImGui::SameLine(0, 15);

				ImVec2 size(ImGui::CalcTextSize(dock_tab->label).x, line_height);
				if (ImGui::InvisibleButton(dock_tab->label, size))
				{
					dock_tab->setActive();
				}

				if (ImGui::IsItemActive() && ImGui::IsMouseDragging())
				{
					doUndock(*dock_tab);
					dock_tab->status = Dock::DRAGGED;
				}

				bool hovered = ImGui::IsItemHovered();
				auto pos = ImGui::GetItemRectMin();
				tab_base = pos.y;
				draw_list->PathClear();
				draw_list->PathLineTo(pos + ImVec2(-15, size.y));
				draw_list->PathBezierCurveTo(
					pos + ImVec2(-10, size.y), pos + ImVec2(-5, 0), pos + ImVec2(0, 0), 10);
				draw_list->PathLineTo(pos + ImVec2(size.x, 0));
				draw_list->PathBezierCurveTo(pos + ImVec2(size.x + 5, 0),
					pos + ImVec2(size.x + 10, size.y),
					pos + ImVec2(size.x + 15, size.y),
					10);
				draw_list->PathFill(
					hovered ? color_hovered : (dock_tab->active ? color_active : color));
				draw_list->AddText(pos, text_color, dock_tab->label);

				dock_tab = dock_tab->next;
			}
			ImVec2 cp(dock.pos.x, tab_base + line_height);
			draw_list->AddLine(cp, cp + ImVec2(dock.size.x, 0), color);
		}
		ImGui::EndChild();
	}


	static void setDockSizeAndPos(Dock& dest, Dock& dock, Slot dock_slot, Dock& container)
	{
		IM_ASSERT(!dock.prev && !dock.next && !dock.children[0] && !dock.children[1]);

		dest.pos = container.pos;
		dest.size = container.size;
		dock.pos = container.pos;
		dock.size = container.size;

		switch (dock_slot)
		{
			case Slot::BOTTOM:
				dest.size.y *= 0.5f;
				dock.size.y *= 0.5f;
				dock.pos.y += dest.size.y;
				break;
			case Slot::RIGHT:
				dest.size.x *= 0.5f;
				dock.size.x *= 0.5f;
				dock.pos.x += dest.size.x;
				break;
			case Slot::LEFT:
				dest.size.x *= 0.5f;
				dock.size.x *= 0.5f;
				dest.pos.x += dock.size.x;
				break;
			case Slot::TOP:
				dest.size.y *= 0.5f;
				dock.size.y *= 0.5f;
				dest.pos.y += dock.size.y;
				break;
			default: IM_ASSERT(false); break;
		}
		dest.setPosSize(dest.pos, dest.size);
	}


	void doDock(Dock& dock, Dock& dest, Slot dock_slot)
	{
		IM_ASSERT(!dock.parent);
		if (dock_slot == Slot::TAB)
		{
			auto* tmp = &dest;
			while (tmp->next)
			{
				tmp = tmp->next;
			}

			tmp->next = &dock;
			dock.prev = tmp;
			dock.size = tmp->size;
			dock.pos = tmp->pos;
			dock.parent = dest.parent;
			dock.status = Dock::DOCKED;
		}
		else if (dock_slot == Slot::NONE)
		{
			dock.pos = ImGui::GetIO().MousePos;
			dock.status = Dock::FLOAT;
		}
		else
		{
			docks[count] = (Dock*)ImGui::MemAlloc(sizeof(Dock));
			Dock* container = new (docks[count]) Dock("");
			++count;
			container->children[0] = &dest.getFirst();
			container->children[1] = &dock;
			container->next = nullptr;
			container->prev = nullptr;
			container->parent = dest.parent;
			container->size = dest.size;
			container->pos = dest.pos;
			container->status = Dock::DOCKED;

			if (!dest.parent)
			{
			}
			else if (&dest.getFirst() == dest.parent->children[0])
			{
				dest.parent->children[0] = container;
			}
			else
			{
				dest.parent->children[1] = container;
			}

			dest.setParent(container);
			dock.parent = container;
			dock.status = Dock::DOCKED;

			setDockSizeAndPos(dest, dock, dock_slot, *container);
		}
		dock.setActive();
	}


	bool begin(const char* label, bool* opened, ImGuiWindowFlags extra_flags)
	{
		auto& dock = getDock(label, !opened || *opened);
		end_action = -1;

		if (opened && !*opened)
		{
			if (dock.status != Dock::FLOAT)
			{
				doUndock(dock);
				dock.status = Dock::FLOAT;
			}
			return false;
		}

		end_action = 0;
		beginPanel();

		current = &dock;
		if (dock.status == Dock::DRAGGED) handleDrag(dock);

		bool is_float = dock.status == Dock::FLOAT;

		if (!dock.parent && dock.size.x < 0 && dock.status != Dock::DRAGGED)
		{
			dock.pos = ImVec2(0, ImGui::GetTextLineHeightWithSpacing() + 4);
			dock.size = ImGui::GetIO().DisplaySize;
			dock.size.y -= dock.pos.y;
		}

		if (is_float)
		{
			ImGui::SetNextWindowPos(dock.pos);
			ImGui::SetNextWindowSize(dock.size);
			bool ret = ImGui::Begin(label,
				opened,
				dock.size,
				-1.0f,
				ImGuiWindowFlags_NoCollapse | extra_flags);
			end_action = 1;
			dock.pos = ImGui::GetWindowPos();
			dock.size = ImGui::GetWindowSize();

			auto& g = *GImGui;

			if (g.ActiveId == GetCurrentWindow()->MoveID && g.IO.MouseDown[0])
			{
				doUndock(dock);
				dock.status = Dock::DRAGGED;
			}
			return ret;
		}

		if (!dock.active) return false;
		end_action = 2;

		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
		auto tabbar_height = ImGui::GetTextLineHeightWithSpacing();
		drawTabbar(dock.getFirst());
		auto pos = dock.pos;
		auto size = dock.size;
		pos.y += tabbar_height + ImGui::GetStyle().WindowPadding.y;
		size.y -= tabbar_height + ImGui::GetStyle().WindowPadding.y;

		ImGui::SetCursorScreenPos(pos);
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoSavedSettings | extra_flags;
		bool ret = ImGui::BeginChild(label, size, true, flags);
		auto* draw_list = ImGui::GetWindowDrawList();
		return ret;
	}


	void end()
	{
		if (end_action == 1)
		{
			ImGui::End();
		}
		else if (end_action == 2)
		{
			ImGui::EndChild();
			ImGui::PopStyleColor();
		}
		current = nullptr;
		if (end_action != -1) endPanel();
	}


	void save(Lumix::FS::OsFile& file)
	{
		auto getDockIndex = [this](Dock* dock) -> int
		{
			if (!dock) return -1;

			for (int i = 0; i < count; ++i)
			{
				if (dock == docks[i]) return i;
			}

			IM_ASSERT(false);
			return -1;
		};

		file << "docks = {\n";
		for (int i = 0; i < count; ++i)
		{
			auto& dock = *docks[i];
			file << "dock" << (Lumix::uint64)&dock << " = {\n";
			file << "index = " << i << ",\n";
			file << "label = \"" << dock.label << "\",\n";
			file << "x = " << (int)dock.pos.x << ",\n";
			file << "y = " << (int)dock.pos.y << ",\n";
			file << "size_x = " << (int)dock.size.x << ",\n";
			file << "size_y = " << (int)dock.size.y << ",\n";
			file << "status = " << (int)dock.status << ",\n";
			file << "active = " << (int)dock.active << ",\n";
			file << "prev = " << (int)getDockIndex(dock.prev) << ",\n";
			file << "next = " << (int)getDockIndex(dock.next) << ",\n";
			file << "child0 = " << (int)getDockIndex(dock.children[0]) << ",\n";
			file << "child1 = " << (int)getDockIndex(dock.children[1]) << ",\n";
			file << "parent = " << (int)getDockIndex(dock.parent) << "\n";
			if (i < count - 1)
				file << "},\n";
			else
				file << "}\n";
		}
		file << "}\n";
	}


	void load(lua_State* L)
	{
		for (int i = 0; i < count; ++i)
		{
			docks[i]->~Dock();
			ImGui::MemFree(docks[i]);
		}
		count = 0;

		if (lua_getglobal(L, "docks") == LUA_TTABLE)
		{
			lua_pushnil(L);
			while (lua_next(L, -2) != 0)
			{
				docks[count] = (Dock*)ImGui::MemAlloc(sizeof(Dock));
				new (docks[count]) Dock("");
				++count;
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);

		int i = 0;
		if (lua_getglobal(L, "docks") == LUA_TTABLE)
		{
			lua_pushnil(L);
			while (lua_next(L, -2) != 0)
			{
				if (lua_istable(L, -1))
				{
					int idx;
					if (lua_getfield(L, -1, "index") == LUA_TNUMBER)
						idx = (int)lua_tointeger(L, -1);
					auto& dock = *docks[idx];
					lua_pop(L, 1);

					if (lua_getfield(L, -1, "label") == LUA_TSTRING)
					{
						Lumix::copyString(dock.label, lua_tostring(L, -1));
						dock.id = ImHash(dock.label, 0);
					}
					lua_pop(L, 1);

					if (lua_getfield(L, -1, "x") == LUA_TNUMBER)
						dock.pos.x = (float)lua_tonumber(L, -1);
					if (lua_getfield(L, -2, "y") == LUA_TNUMBER)
						dock.pos.y = (float)lua_tonumber(L, -1);
					if (lua_getfield(L, -3, "size_x") == LUA_TNUMBER)
						dock.size.x = (float)lua_tonumber(L, -1);
					if (lua_getfield(L, -4, "size_y") == LUA_TNUMBER)
						dock.size.y = (float)lua_tonumber(L, -1);
					if (lua_getfield(L, -5, "active") == LUA_TNUMBER)
						dock.active = lua_tointeger(L, -1) != 0;
					if (lua_getfield(L, -6, "status") == LUA_TNUMBER)
					{
						dock.status = (Dock::Status)lua_tointeger(L, -1);
					}
					lua_pop(L, 6);

					auto getDockByIndex = [this](lua_Integer idx) -> Dock*
					{
						return idx < 0 ? nullptr : docks[idx];
					};
					if (lua_getfield(L, -1, "prev") == LUA_TNUMBER)
					{
						dock.prev = getDockByIndex(lua_tointeger(L, -1));
					}
					if (lua_getfield(L, -2, "next") == LUA_TNUMBER)
					{
						dock.next = getDockByIndex(lua_tointeger(L, -1));
					}
					if (lua_getfield(L, -3, "child0") == LUA_TNUMBER)
					{
						dock.children[0] = getDockByIndex(lua_tointeger(L, -1));
					}
					if (lua_getfield(L, -4, "child1") == LUA_TNUMBER)
					{
						dock.children[1] = getDockByIndex(lua_tointeger(L, -1));
					}
					if (lua_getfield(L, -5, "parent") == LUA_TNUMBER)
					{
						dock.parent = getDockByIndex(lua_tointeger(L, -1));
					}
					lua_pop(L, 5);
				}
				lua_pop(L, 1);
				++i;
			}
		}
		lua_pop(L, 1);
	}
};


static DockContext g_dock;


bool BeginDock(const char* label, bool* opened, ImGuiWindowFlags extra_flags)
{
	return g_dock.begin(label, opened, extra_flags);
}


void EndDock()
{
	g_dock.end();
}


void SaveDock(Lumix::FS::OsFile& file)
{
	g_dock.save(file);
}


void LoadDock(lua_State* L)
{
	g_dock.load(L);
}


} // namespace ImGui