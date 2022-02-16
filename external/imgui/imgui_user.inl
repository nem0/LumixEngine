#include "engine/math.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <math.h>


static constexpr float HANDLE_RADIUS = 4;
using namespace ImGui;

ImVec2::ImVec2(const Lumix::Vec2& f) 
	: x(f.x)
	, y(f.y) 
{}

ImVec2::operator Lumix::Vec2() const {
	return {x, y};
}                                               

namespace ImGuiEx {

	constexpr float NODE_PIN_RADIUS = 5.f;
	struct NodeEditorState {
		ImVec2* node_pos;
		float node_w = 120;
		ImGuiID last_node_id;
		ImVec2 node_editor_pos;
		ImGuiID new_link_from = 0;
		ImGuiID new_link_to = 0;
		bool new_link_from_input;
		bool link_hovered = false;
		ImDrawList* draw_list = nullptr;
		bool is_pin_hovered = false;
		ImVec2* canvas_offset = nullptr;
	} g_node_editor;

	ImVec2 GetNodeEditorOffset() {
		return *g_node_editor.canvas_offset;
	}

	void BeginNodeEditor(const char* title, ImVec2* offset) {
		g_node_editor.canvas_offset = offset;
		BeginChild(title, ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		g_node_editor.node_editor_pos = GetCursorScreenPos() + *g_node_editor.canvas_offset;
		g_node_editor.link_hovered = false;
		g_node_editor.draw_list = GetWindowDrawList();
		g_node_editor.draw_list->ChannelsSplit(2);
	}

	void EndNodeEditor() {
		g_node_editor.draw_list->ChannelsMerge();

		if (g_node_editor.new_link_from != 0) {
			ImGuiStorage* storage = GetStateStorage();
			PushID(g_node_editor.new_link_from);
			const ImVec2 from(storage->GetFloat(GetID("pin-x"), 0), storage->GetFloat(GetID("pin-y"), 0));
			PopID();
			ImDrawList* dl = g_node_editor.draw_list;
			const ImVec2 to = GetMousePos();
			if (g_node_editor.new_link_from_input) {
				dl->AddBezierCubic(from, from - ImVec2(20, 0), to + ImVec2(20, 0), to, GetColorU32(ImGuiCol_Tab), 3.f);
			}
			else {
				dl->AddBezierCubic(from, from + ImVec2(20, 0), to - ImVec2(20, 0), to, GetColorU32(ImGuiCol_Tab), 3.f);
			}
		}
		EndChild();
		if (IsMouseReleased(0)) {
			g_node_editor.new_link_from = 0;
			g_node_editor.new_link_to = 0;
		}

		if (IsMouseDragging(ImGuiMouseButton_Middle) && IsItemHovered()) {
			const ImVec2 delta = GetIO().MouseDelta;
			*g_node_editor.canvas_offset += delta;
		}
	}

	bool GetNewLink(ImGuiID* from, ImGuiID* to) {
		if (g_node_editor.new_link_to) {
			*from = g_node_editor.new_link_from;
			*to = g_node_editor.new_link_to;
			return true;
		}
		return false;
	}

	void Pin(ImGuiID id, bool is_input, PinShape shape) {
		PopID(); // pop node id, we want pin id to not include node id
		ImDrawList* draw_list = GetWindowDrawList();
		ImVec2 screen_pos = ImGui::GetCursorScreenPos();
		
		const ImVec2 center = [&](){
			if (is_input) return screen_pos + ImVec2(-GetStyle().WindowPadding.x, GetTextLineHeightWithSpacing() * 0.5f);
			return ImVec2(g_node_editor.node_pos->x + g_node_editor.node_w + 2 * GetStyle().WindowPadding.x, screen_pos.y + GetTextLineHeightWithSpacing() * 0.5f);
		}();
		const ImVec2 half_extents(NODE_PIN_RADIUS, NODE_PIN_RADIUS);
		ItemAdd(ImRect(center - half_extents, center + half_extents), id);
		const bool hovered = IsItemHovered();
		ImGuiStyle& style = ImGui::GetStyle();
		const ImU32 color = GetColorU32(hovered ? ImGuiCol_TabHovered : ImGuiCol_Tab);
		switch(shape) {
			case PinShape::TRIANGLE:
				draw_list->AddTriangleFilled(center - ImVec2(NODE_PIN_RADIUS, -NODE_PIN_RADIUS), center - half_extents, center + ImVec2(NODE_PIN_RADIUS, 0), GetColorU32(ImGuiCol_Text));
				break;
			default:
				draw_list->AddCircleFilled(center, NODE_PIN_RADIUS, color);
				break;
		}

		g_node_editor.is_pin_hovered = g_node_editor.is_pin_hovered || hovered;

		ImGuiStorage* storage = GetStateStorage();
		PushID(id);
		storage->SetFloat(GetID("pin-x"), center.x);
		storage->SetFloat(GetID("pin-y"), center.y);
		PopID();

		if (hovered && ImGui::IsMouseClicked(0)) {
			g_node_editor.new_link_from = id;
			g_node_editor.new_link_from_input = is_input;
		}

		if (hovered && ImGui::IsMouseReleased(0) && g_node_editor.new_link_from != 0) {
			g_node_editor.new_link_to = id;
			if (!is_input) {
				ImSwap(g_node_editor.new_link_to, g_node_editor.new_link_from);
			}
		}
		PushID(g_node_editor.last_node_id);
	}

	bool IsLinkHovered() {
		return g_node_editor.link_hovered;
	}

	void NodeLink(ImGuiID from_id, ImGuiID to_id) {
		ImGuiStorage* storage = GetStateStorage();
		PushID(from_id);
		const ImVec2 from(storage->GetFloat(GetID("pin-x"), 0), storage->GetFloat(GetID("pin-y"), 0));
		PopID();

		PushID(to_id);
		const ImVec2 to(storage->GetFloat(GetID("pin-x"), 0), storage->GetFloat(GetID("pin-y"), 0));
		PopID();

		ImVec2 p1 = from;
		float d = ImMax(20.f, ImAbs(from.x - to.x)) * 0.75f;
		ImVec2 t1 = ImVec2(d, 0.0f);
		ImVec2 p2 = to;
		ImVec2 t2 = ImVec2(d, 0.0f);
		const int STEPS = 12;
		ImDrawList* draw_list = GetWindowDrawList();
	    const ImGuiStyle& style = ImGui::GetStyle();
		const ImVec2 closest_point = ImBezierCubicClosestPointCasteljau(p1, p1 + t1, p2 - t2, p2, ImGui::GetMousePos(), style.CurveTessellationTol);
		const float dist_squared = ImFabs(ImLengthSqr(ImGui::GetMousePos() - closest_point));
		g_node_editor.link_hovered = dist_squared < 3 * 3 + 1;
		
		draw_list->AddBezierCubic(p1, p1 + t1, p2 - t2, p2, GetColorU32(g_node_editor.link_hovered ? ImGuiCol_TabActive : ImGuiCol_Tab), 3.f);
	}

	void BeginNode(ImGuiID id, ImVec2& pos) {
		g_node_editor.last_node_id = id;
		pos += g_node_editor.node_editor_pos;
		g_node_editor.node_pos = &pos;
		SetCursorScreenPos(pos + GetStyle().WindowPadding);
		g_node_editor.draw_list->ChannelsSetCurrent(1);
		BeginGroup();
		PushID(id);
		g_node_editor.node_w = GetStateStorage()->GetFloat(GetID("node-width"), 120);
		PushItemWidth(80);
		g_node_editor.is_pin_hovered = false;
	}

	void EndNode()
	{
		PopItemWidth();
		EndGroup();
		const ImGuiStyle& style = GetStyle();
		const ImRect rect(GetItemRectMin() - style.WindowPadding, GetItemRectMax() + style.WindowPadding);
		const ImVec2 size = rect.GetSize();
		
		GetStateStorage()->SetFloat(GetID("node-width"), size.x - style.WindowPadding.x * 2);

		const ImGuiID dragger_id = GetID("##_node_dragger");
		ItemAdd(rect, dragger_id);
		const bool is_hovered = IsItemHovered();
		if (is_hovered && IsMouseClicked(0) && !g_node_editor.is_pin_hovered) {
			SetActiveID(dragger_id, GetCurrentWindow());
		}
		if (IsItemActive() && IsMouseReleased(0)) {
			ResetActiveID();
		}
		if (IsItemActive() && IsMouseDragging(0)) {
			*g_node_editor.node_pos += GetIO().MouseDelta;
		}

		g_node_editor.draw_list->ChannelsSetCurrent(0);
		ImVec2 np = *g_node_editor.node_pos;
		g_node_editor.draw_list->AddRectFilled(np, np + size, ImColor(style.Colors[ImGuiCol_WindowBg]), 4.0f);
		g_node_editor.draw_list->AddRect(np, np + size, GetColorU32(is_hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Border), 4.0f);

		PopID();
		*g_node_editor.node_pos -= g_node_editor.node_editor_pos;
	}

	bool ToolbarButton(ImFont* font, const char* font_icon, const ImVec4& bg_color, const char* tooltip)
	{
		auto frame_padding = GetStyle().FramePadding;
		PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
		PushStyleColor(ImGuiCol_Text, bg_color);
		PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
		PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, GetStyle().FramePadding.y));
		PushStyleVar(ImGuiStyleVar_WindowPadding, frame_padding);
		PushStyleVar(ImGuiStyleVar_FrameRounding, 0);

		bool ret = false;
		PushFont(font);
		if (Button(font_icon)) {
			ret = true;
		}
		PopFont();
		PopStyleColor(4);
		PopStyleVar(3);
		if (IsItemHovered()) {
			BeginTooltip();
			TextUnformatted(tooltip);
			EndTooltip();
		}
		return ret;
	}


	bool BeginToolbar(const char* str_id, ImVec2 screen_pos, ImVec2 size)
	{
		bool is_global = GImGui->CurrentWindowStack.Size == 1;
		SetNextWindowPos(screen_pos);
		ImVec2 frame_padding = GetStyle().FramePadding;
		PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
		PushStyleVar(ImGuiStyleVar_WindowPadding, frame_padding);
		PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
		if (size.x == 0) size.x = GetContentRegionAvail().x;
		SetNextWindowSize(size);

		bool ret = is_global ? Begin(str_id, nullptr, flags) : BeginChild(str_id, size, false, flags);
		PopStyleVar(3);

		return ret;
	}


	void EndToolbar()
	{
		auto frame_padding = GetStyle().FramePadding;
		PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
		PushStyleVar(ImGuiStyleVar_WindowPadding, frame_padding);
		PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
		ImVec2 pos = GetWindowPos();
		ImVec2 size = GetWindowSize();
		if (GImGui->CurrentWindowStack.Size == 2) End(); else EndChild();
		PopStyleVar(3);
		if (GImGui->CurrentWindowStack.Size > 1) SetCursorScreenPos(pos + ImVec2(0, size.y + GetStyle().FramePadding.y * 2));
	}


	ImVec2 GetOsImePosRequest()
	{
		return GetCurrentContext()->PlatformImeData.InputPos;
	}


	void ResetActiveID()
	{
		SetActiveID(0, nullptr);
	}



	static inline bool IsWindowContentHoverableEx(ImGuiWindow* window, ImGuiHoveredFlags flags)
	{
		// An active popup disable hovering on other windows (apart from its own children)
		// FIXME-OPT: This could be cached/stored within the window.
		ImGuiContext& g = *GImGui;
		if (g.NavWindow)
			if (ImGuiWindow* focused_root_window = g.NavWindow->RootWindow)
				if (focused_root_window->WasActive && focused_root_window != window->RootWindow)
				{
					// For the purpose of those flags we differentiate "standard popup" from "modal popup"
					// NB: The order of those two tests is important because Modal windows are also Popups.
					if (focused_root_window->Flags & ImGuiWindowFlags_Modal)
						return false;
					if ((focused_root_window->Flags & ImGuiWindowFlags_Popup) && !(flags & ImGuiHoveredFlags_AllowWhenBlockedByPopup))
						return false;
				}

		return true;
	}


	void BringToFront()
	{
		ImGuiContext& g = *GImGui;

		ImGuiWindow* window = GImGui->CurrentWindow;

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


	ImVec2 operator*(float f, const ImVec2& v)
	{
		return ImVec2(f * v.x, f * v.y);
	}


	int CurveEditor(const char* label
		, float* values
		, int points_count
		, const ImVec2& editor_size
		, ImU32 flags
		, int* new_count
		, int* selected_point)
	{
		enum class StorageValues : ImGuiID
		{
			FROM_X = 100,
			FROM_Y,
			WIDTH,
			HEIGHT,
			IS_PANNING,
			POINT_START_X,
			POINT_START_Y
		};

		const float HEIGHT = 100;
		static ImVec2 start_pan;

		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		ImVec2 size = editor_size;
		size.x = size.x < 0 ? CalcItemWidth() + (style.FramePadding.x * 2) : size.x;
		size.y = size.y < 0 ? HEIGHT : size.y;

		ImGuiWindow* parent_window = GetCurrentWindow();
		ImGuiID id = parent_window->GetID(label);
		if (new_count) *new_count = points_count;
		if (!BeginChildFrame(id, size, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			EndChildFrame();
			return -1;
		}

		int hovered_idx = -1;

		ImGuiWindow* window = GetCurrentWindow();
		if (window->SkipItems)
		{
			EndChildFrame();
			return -1;
		}
		
		ImVec2 points_min(FLT_MAX, FLT_MAX);
		ImVec2 points_max(-FLT_MAX, -FLT_MAX);
		for (int point_idx = 0; point_idx < points_count; ++point_idx)
		{
			ImVec2 point;
			if (flags & (int)CurveEditorFlags::NO_TANGENTS)
			{
				point = ((ImVec2*)values)[point_idx];
			}
			else
			{
				point = ((ImVec2*)values)[1 + point_idx * 3];
			}
			points_max = ImMax(points_max, point);
			points_min = ImMin(points_min, point);
		}
		points_max.y = ImMax(points_max.y, points_min.y + 0.0001f);

		if (flags & (int)CurveEditorFlags::RESET) window->StateStorage.Clear();

		float from_x = window->StateStorage.GetFloat((ImGuiID)StorageValues::FROM_X, points_min.x);
		float from_y = window->StateStorage.GetFloat((ImGuiID)StorageValues::FROM_Y, points_min.y);
		float width = window->StateStorage.GetFloat((ImGuiID)StorageValues::WIDTH, points_max.x - points_min.x);
		float height = window->StateStorage.GetFloat((ImGuiID)StorageValues::HEIGHT, points_max.y - points_min.y);
		window->StateStorage.SetFloat((ImGuiID)StorageValues::FROM_X, from_x);
		window->StateStorage.SetFloat((ImGuiID)StorageValues::FROM_Y, from_y);
		window->StateStorage.SetFloat((ImGuiID)StorageValues::WIDTH, width);
		window->StateStorage.SetFloat((ImGuiID)StorageValues::HEIGHT, height);

		const ImRect inner_bb = window->InnerClipRect;
		const ImRect frame_bb(inner_bb.Min - style.FramePadding, inner_bb.Max + style.FramePadding);

		auto transform = [&](const ImVec2& pos) -> ImVec2
		{
			float x = (pos.x - from_x) / width;
			float y = (pos.y - from_y) / height;

			return ImVec2(
				inner_bb.Min.x * (1 - x) + inner_bb.Max.x * x,
				inner_bb.Min.y * y + inner_bb.Max.y * (1 - y)
			);
		};

		auto invTransform = [&](const ImVec2& pos) -> ImVec2
		{
			float x = (pos.x - inner_bb.Min.x) / (inner_bb.Max.x - inner_bb.Min.x);
			float y = (inner_bb.Max.y - pos.y) / (inner_bb.Max.y - inner_bb.Min.y);

			return ImVec2(
				from_x + width * x,
				from_y + height * y
			);
		};

		if (flags & (int)CurveEditorFlags::SHOW_GRID)
		{
			int exp;
			frexp(width / 5, &exp);
			float step_x = (float)ldexp(1.0, exp);
			int cell_cols = int(width / step_x);

			float x = step_x * int(from_x / step_x);
			for (int i = -1; i < cell_cols + 2; ++i)
			{
				ImVec2 a = transform({ x + i * step_x, from_y });
				ImVec2 b = transform({ x + i * step_x, from_y + height });
				window->DrawList->AddLine(a, b, 0x55000000);
				char buf[64];
				if (exp > 0)
				{
					ImFormatString(buf, sizeof(buf), " %d", int(x + i * step_x));
				}
				else
				{
					ImFormatString(buf, sizeof(buf), " %f", x + i * step_x);
				}
				window->DrawList->AddText(b, 0x55000000, buf);
			}

			frexp(height / 5, &exp);
			float step_y = (float)ldexp(1.0, exp);
			int cell_rows = int(height / step_y);

			float y = step_y * int(from_y / step_y);
			for (int i = -1; i < cell_rows + 2; ++i)
			{
				ImVec2 a = transform({ from_x, y + i * step_y });
				ImVec2 b = transform({ from_x + width, y + i * step_y });
				window->DrawList->AddLine(a, b, 0x55000000);
				char buf[64];
				if (exp > 0)
				{
					ImFormatString(buf, sizeof(buf), " %d", int(y + i * step_y));
				}
				else
				{
					ImFormatString(buf, sizeof(buf), " %f", y + i * step_y);
				}
				window->DrawList->AddText(a, 0x55000000, buf);
			}
		}

		if (GetIO().MouseWheel != 0 && IsItemHovered())
		{
			float scale = powf(2, GetIO().MouseWheel);
			width *= scale;
			height *= scale;
			window->StateStorage.SetFloat((ImGuiID)StorageValues::WIDTH, width);
			window->StateStorage.SetFloat((ImGuiID)StorageValues::HEIGHT, height);
		}
		if (IsMouseReleased(2))
		{
			window->StateStorage.SetBool((ImGuiID)StorageValues::IS_PANNING, false);
		}
		if (window->StateStorage.GetBool((ImGuiID)StorageValues::IS_PANNING, false))
		{
			ImVec2 drag_offset = GetMouseDragDelta(2);
			from_x = start_pan.x;
			from_y = start_pan.y;
			from_x -= drag_offset.x * width / (inner_bb.Max.x - inner_bb.Min.x);
			from_y += drag_offset.y * height / (inner_bb.Max.y - inner_bb.Min.y);
			window->StateStorage.SetFloat((ImGuiID)StorageValues::FROM_X, from_x);
			window->StateStorage.SetFloat((ImGuiID)StorageValues::FROM_Y, from_y);
		}
		else if (IsMouseDragging(2) && IsItemHovered())
		{
			window->StateStorage.SetBool((ImGuiID)StorageValues::IS_PANNING, true);
			start_pan.x = from_x;
			start_pan.y = from_y;
		}

		int changed_idx = -1;
		for (int point_idx = points_count - 2; point_idx >= 0; --point_idx)
		{
			ImVec2* points;
			if (flags & (int)CurveEditorFlags::NO_TANGENTS)
			{
				points = ((ImVec2*)values) + point_idx;
			}
			else
			{
				points = ((ImVec2*)values) + 1 + point_idx * 3;
			}
			
			ImVec2 p_prev = points[0];
			ImVec2 tangent_last;
			ImVec2 tangent;
			ImVec2 p;
			if (flags & (int)CurveEditorFlags::NO_TANGENTS)
			{
				p = points[1];
			}
			else
			{
				tangent_last = points[1];
				tangent = points[2];
				p = points[3];
			}

			auto handlePoint = [&](ImVec2& p, int idx) -> bool
			{
				static const float SIZE = 3;

				ImVec2 cursor_pos = GetCursorScreenPos();
				ImVec2 pos = transform(p);

				SetCursorScreenPos(pos - ImVec2(SIZE, SIZE));
				PushID(idx);
				InvisibleButton("", ImVec2(2 * HANDLE_RADIUS, 2 * HANDLE_RADIUS));

				bool is_selected = selected_point && *selected_point == point_idx + idx;
				float thickness = is_selected ? 2.0f : 1.0f;
				ImU32 col = IsItemActive() || IsItemHovered() ? GetColorU32(ImGuiCol_PlotLinesHovered) : GetColorU32(ImGuiCol_PlotLines);

				window->DrawList->AddLine(pos + ImVec2(-SIZE, 0), pos + ImVec2(0, SIZE), col, thickness);
				window->DrawList->AddLine(pos + ImVec2(SIZE, 0), pos + ImVec2(0, SIZE), col, thickness);
				window->DrawList->AddLine(pos + ImVec2(SIZE, 0), pos + ImVec2(0, -SIZE), col, thickness);
				window->DrawList->AddLine(pos + ImVec2(-SIZE, 0), pos + ImVec2(0, -SIZE), col, thickness);

				if (IsItemHovered()) hovered_idx = point_idx + idx;

				bool changed = false;
				if (IsItemActive() && IsMouseClicked(0))
				{
					if (selected_point) *selected_point = point_idx + idx;
					window->StateStorage.SetFloat((ImGuiID)StorageValues::POINT_START_X, pos.x);
					window->StateStorage.SetFloat((ImGuiID)StorageValues::POINT_START_Y, pos.y);
				}

				if (IsItemHovered() || IsItemActive() && IsMouseDragging(0))
				{
					char tmp[64];
					ImFormatString(tmp, sizeof(tmp), "%0.2f, %0.2f", p.x, p.y);
					window->DrawList->AddText({ pos.x, pos.y - GetTextLineHeight() }, 0xff000000, tmp);
				}

				if (IsItemActive() && IsMouseDragging(0))
				{
					pos.x = window->StateStorage.GetFloat((ImGuiID)StorageValues::POINT_START_X, pos.x);
					pos.y = window->StateStorage.GetFloat((ImGuiID)StorageValues::POINT_START_Y, pos.y);
					pos += GetMouseDragDelta();
					ImVec2 v = invTransform(pos);

					p = v;
					changed = true;
				}
				PopID();

				SetCursorScreenPos(cursor_pos);
				return changed;
			};

			auto handleTangent = [&](ImVec2& t, const ImVec2& p, int idx) -> bool
			{
				static const float SIZE = 2;
				static const float LENGTH = 18;

				auto normalized = [](const ImVec2& v) -> ImVec2
				{
					float len = 1.0f / sqrtf(v.x *v.x + v.y * v.y);
					return ImVec2(v.x * len, v.y * len);
				};

				ImVec2 cursor_pos = GetCursorScreenPos();
				ImVec2 pos = transform(p);
				ImVec2 tang = pos + normalized(ImVec2(t.x, -t.y)) * LENGTH;

				SetCursorScreenPos(tang - ImVec2(SIZE, SIZE));
				PushID(-idx);
				InvisibleButton("", ImVec2(2 * HANDLE_RADIUS, 2 * HANDLE_RADIUS));

				window->DrawList->AddLine(pos, tang, GetColorU32(ImGuiCol_PlotLines));

				ImU32 col = IsItemHovered() ? GetColorU32(ImGuiCol_PlotLinesHovered) : GetColorU32(ImGuiCol_PlotLines);

				window->DrawList->AddLine(tang + ImVec2(-SIZE, SIZE), tang + ImVec2(SIZE, SIZE), col);
				window->DrawList->AddLine(tang + ImVec2(SIZE, SIZE), tang + ImVec2(SIZE, -SIZE), col);
				window->DrawList->AddLine(tang + ImVec2(SIZE, -SIZE), tang + ImVec2(-SIZE, -SIZE), col);
				window->DrawList->AddLine(tang + ImVec2(-SIZE, -SIZE), tang + ImVec2(-SIZE, SIZE), col);

				bool changed = false;
				if (IsItemActive() && IsMouseDragging(0))
				{
					tang = GetIO().MousePos - pos;
					tang = normalized(tang);
					tang.y *= -1;

					t = tang;
					changed = true;
				}
				PopID();

				SetCursorScreenPos(cursor_pos);
				return changed;
			};

			PushID(point_idx);
			if ((flags & (int)CurveEditorFlags::NO_TANGENTS) == 0)
			{
				window->DrawList->AddBezierCubic(
					transform(p_prev),
					transform(p_prev + tangent_last),
					transform(p + tangent),
					transform(p),
					GetColorU32(ImGuiCol_PlotLines),
					1.0f,
					20);
				if (handleTangent(tangent_last, p_prev, 0))
				{
					points[1] = ImClamp(tangent_last, ImVec2(0, -1), ImVec2(1, 1));
					changed_idx = point_idx;
				}
				if (handleTangent(tangent, p, 1))
				{
					points[2] = ImClamp(tangent, ImVec2(-1, -1), ImVec2(0, 1));
					changed_idx = point_idx + 1;
				}
				if (handlePoint(p, 1))
				{
					if (p.x <= p_prev.x) p.x = p_prev.x + 0.001f;
					if (point_idx < points_count - 2 && p.x >= points[6].x)
					{
						p.x = points[6].x - 0.001f;
					}
					points[3] = p;
					changed_idx = point_idx + 1;
				}

			}
			else
			{
				window->DrawList->AddLine(transform(p_prev), transform(p), GetColorU32(ImGuiCol_PlotLines), 1.0f);
				if (handlePoint(p, 1))
				{
					if (p.x <= p_prev.x) p.x = p_prev.x + 0.001f;
					if (point_idx < points_count - 2 && p.x >= points[2].x)
					{
						p.x = points[2].x - 0.001f;
					}
					points[1] = p;
					changed_idx = point_idx + 1;
				}
			}
			if (point_idx == 0)
			{
				if (handlePoint(p_prev, 0))
				{
					if (p.x <= p_prev.x) p_prev.x = p.x - 0.001f;
					points[0] = p_prev;
					changed_idx = point_idx;
				}
			}
			PopID();
		}

		SetCursorScreenPos(inner_bb.Min);

		InvisibleButton("bg", inner_bb.Max - inner_bb.Min);

		if (IsItemActive() && IsMouseDoubleClicked(0) && new_count)
		{
			ImVec2 mp = GetMousePos();
			ImVec2 new_p = invTransform(mp);
			ImVec2* points = (ImVec2*)values;

			if ((flags & (int)CurveEditorFlags::NO_TANGENTS) == 0)
			{
				points[points_count * 3 + 0] = ImVec2(-0.2f, 0);
				points[points_count * 3 + 1] = new_p;
				points[points_count * 3 + 2] = ImVec2(0.2f, 0);;
				++*new_count;

				auto compare = [](const void* a, const void* b) -> int
				{
					float fa = (((const ImVec2*)a) + 1)->x;
					float fb = (((const ImVec2*)b) + 1)->x;
					return fa < fb ? -1 : (fa > fb) ? 1 : 0;
				};

				qsort(values, points_count + 1, sizeof(ImVec2) * 3, compare);

			}
			else
			{
				points[points_count] = new_p;
				++*new_count;

				auto compare = [](const void* a, const void* b) -> int
				{
					float fa = ((const ImVec2*)a)->x;
					float fb = ((const ImVec2*)b)->x;
					return fa < fb ? -1 : (fa > fb) ? 1 : 0;
				};

				qsort(values, points_count + 1, sizeof(ImVec2), compare);
			}
		}

		if (hovered_idx >= 0 && IsMouseDoubleClicked(0) && new_count && points_count > 2)
		{
			ImVec2* points = (ImVec2*)values;
			--*new_count;
			if ((flags & (int)CurveEditorFlags::NO_TANGENTS) == 0)
			{
				for (int j = hovered_idx * 3; j < points_count * 3 - 3; j += 3)
				{
					points[j + 0] = points[j + 3];
					points[j + 1] = points[j + 4];
					points[j + 2] = points[j + 5];
				}
			}
			else
			{
				for (int j = hovered_idx; j < points_count - 1; ++j)
				{
					points[j] = points[j + 1];
				}
			}
		}

		EndChildFrame();
		RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, inner_bb.Min.y), label);
		return changed_idx;
	}


	bool BeginResizablePopup(const char* str_id, const ImVec2& size_on_first_use)
	{
		if (!IsPopupOpen(str_id))
		{
			GImGui->NextWindowData.ClearFlags();
			return false;
		}

		ImGuiWindowFlags flags = ImGuiWindowFlags_Popup | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

		char name[32];
		ImFormatString(name, 20, "##popup_%s", str_id);

		SetNextWindowSize(size_on_first_use, ImGuiCond_FirstUseEver);
		bool opened = Begin(name, NULL, flags);
		if (!opened)
			EndPopup();

		return opened;
	}


	void Rect(float w, float h, ImU32 color)
	{
		ImGuiWindow* win = GetCurrentWindow();
		ImVec2 screen_pos = GetCursorScreenPos();
		ImVec2 end_pos = screen_pos + ImVec2(w, h);
		ImRect total_bb(screen_pos, end_pos);
		ItemSize(total_bb);
		if (!ItemAdd(total_bb, 0)) return;
		win->DrawList->AddRectFilled(screen_pos, end_pos, color);
	}


	void HSplitter(const char* str_id, ImVec2* size)
	{
		ImVec2 screen_pos = GetCursorScreenPos();
		InvisibleButton(str_id, ImVec2(-1, 3));
		ImVec2 end_pos = screen_pos + GetItemRectSize();
		ImGuiWindow* win = GetCurrentWindow();
		ImVec4* colors = GetStyle().Colors;
		ImU32 color = GetColorU32(IsItemActive() || IsItemHovered() ? colors[ImGuiCol_ButtonActive] : colors[ImGuiCol_Button]);
		win->DrawList->AddRectFilled(screen_pos, end_pos, color);
		if (IsItemActive())
		{
			size->y = ImMax(1.0f, GetIO().MouseDelta.y + size->y);
		}
	}


	void VSplitter(const char* str_id, ImVec2* size)
	{
		ImVec2 screen_pos = GetCursorScreenPos();
		InvisibleButton(str_id, ImVec2(3, -1));
		ImVec2 end_pos = screen_pos + GetItemRectSize();
		ImGuiWindow* win = GetCurrentWindow();
		ImVec4* colors = GetStyle().Colors;
		ImU32 color = GetColorU32(IsItemActive() || IsItemHovered() ? colors[ImGuiCol_ButtonActive] : colors[ImGuiCol_Button]);
		win->DrawList->AddRectFilled(screen_pos, end_pos, color);
		if (IsItemActive())
		{
			size->x = ImMax(1.0f, GetIO().MouseDelta.x + size->x);
		}
	}


	bool IconButton(const char* icon, const char* tooltip) {
		PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
		PushStyleColor(ImGuiCol_Button, GetStyle().Colors[ImGuiCol_WindowBg]);
		bool res = SmallButton(icon);
		if (IsItemHovered()) {
			SetTooltip("%s", tooltip);
		}
		PopStyleColor();
		PopStyleVar();
		return res;
	}
	
	void TextClipped(const char* text, float size) {
		ImDrawList* dl = GetWindowDrawList();
		const ImVec2 min = GetCursorScreenPos();
		ImVec2 max = min;
		max.y += GetTextLineHeight();
		max.x += size;
		RenderTextEllipsis(dl, min, max, max.x, max.x, text, nullptr, nullptr);
		ImRect text_rect;
		text_rect.Min = min;
		text_rect.Max = max;
		ItemSize(text_rect);
	}

	bool InputRotation(const char* label, float* feuler) {
		Lumix::Vec3 euler = radiansToDegrees(*(Lumix::Vec3*)feuler);
		const float rot_change_speed = ImGui::GetIO().KeyAlt ? 10.f : 1.f; // we won't have precision without this
		if (ImGui::DragFloat3("##rot", &euler.x, rot_change_speed, 0, 0, "%.2f")) {
			if (euler.x <= -90.0f || euler.x >= 90.0f) euler.y = 0;
			euler.x = Lumix::degreesToRadians(Lumix::clamp(euler.x, -90.0f, 90.0f));
			euler.y = Lumix::degreesToRadians(fmodf(euler.y + 180, 360.0f) - 180);
			euler.z = Lumix::degreesToRadians(fmodf(euler.z + 180, 360.0f) - 180);
			*(Lumix::Vec3*)feuler = euler;
			return true;
		}
		return false;
	}

	void Label(const char* label) {
		ImGuiWindow* window = GetCurrentWindow();
		const ImVec2 lineStart = GetCursorScreenPos();
		const ImGuiStyle& style = GetStyle();
		float fullWidth = GetContentRegionAvail().x;
		float itemWidth = fullWidth * 0.6f;
		ImVec2 textSize = CalcTextSize(label);
		ImRect textRect;
		textRect.Min = GetCursorScreenPos();
		textRect.Max = textRect.Min;
		textRect.Max.x += fullWidth - itemWidth;
		textRect.Max.y += textSize.y;

		SetCursorScreenPos(textRect.Min);

		AlignTextToFramePadding();
		textRect.Min.y += window->DC.CurrLineTextBaseOffset;
		textRect.Max.y += window->DC.CurrLineTextBaseOffset;

		ItemSize(textRect);
		if (ItemAdd(textRect, window->GetID(label)))
		{
			RenderTextEllipsis(GetWindowDrawList(), textRect.Min, textRect.Max, textRect.Max.x,
				textRect.Max.x, label, nullptr, &textSize);

			if (textRect.GetWidth() < textSize.x && IsItemHovered())
				SetTooltip("%s", label);
		}
		SetCursorScreenPos(textRect.Max - ImVec2{0, textSize.y + window->DC.CurrLineTextBaseOffset});
		SameLine();
		SetNextItemWidth(itemWidth);
	}

	bool Gradient4(const char* label, int max_count, int* count, float* keys, float* values) {
		PushID(label);
		IM_ASSERT(*count > 1);
		IM_ASSERT(keys[0] >= 0 && keys[0] <= 1);
		IM_ASSERT(max_count >= *count);
		
		ImDrawList* dl = GetWindowDrawList();
		const ImVec2 min = GetCursorScreenPos();
		const float w = CalcItemWidth();
		const ImVec2 max = min + ImVec2(w, GetTextLineHeight());

		ImColor c0(values[0], values[1], values[2], values[3]);
		ImVec2 to;
		to.x = min.x * (1 - keys[0]) + max.x * keys[0];
		to.y = max.y;
		dl->AddRectFilledMultiColor(min, to, c0, c0, c0, c0);

		for (int i = 0; i < *count - 1; ++i) {
			float t0 = keys[i];
			float t1 = keys[i + 1];
			
			IM_ASSERT(t0 <= t1);
			IM_ASSERT(t0 >= 0);
			IM_ASSERT(t1 <= 1);
			
			ImVec2 from = min * (1 - t0) + max * t0;
			from.y = min.y;
			ImVec2 to;
			to.x = min.x * (1 - t1) + max.x * t1;
			to.y = max.y;

			const int i1 = i + 1;
			const ImColor c1(values[i1 * 4 + 0], values[i1 * 4 + 1], values[i1 * 4 + 2], values[i1 * 4 + 3]);
			dl->AddRectFilledMultiColor(from, to, c0, c1, c1, c0);
			c0 = c1;
		}

		ImVec2 from;
		from.x = min.x * (1 - keys[*count - 1]) + max.x * keys[*count - 1];
		from.y = min.y;
		dl->AddRectFilledMultiColor(from, max, c0, c0, c0, c0);

		SetCursorScreenPos(min);
		InvisibleButton("gradient", max - min);
		if (IsItemActive() && IsMouseDoubleClicked(0) && *count < max_count) {
			const float x = GetMousePos().x;
			const float key = (x - min.x) / (max.x - min.x);
			bool found = false;
			for (int i = 0; i < *count; ++i) {
				if (key < keys[i]) {
					for (int j = *count; j >= i; --j) {
						keys[j + 1] = keys[j];
						values[j * 4 + 4] = values[j * 4 + 0];
						values[j * 4 + 5] = values[j * 4 + 1];
						values[j * 4 + 6] = values[j * 4 + 2];
						values[j * 4 + 7] = values[j * 4 + 3];
					}
					found = true;
					keys[i] = key;
					break;
				}
			}

			if (!found) {
				keys[*count] = key;
				values[*count] = values[*count - 1];
			}

			++*count;
		}

		bool changed = false;
		for (int i = 0; i < *count; ++i) {
			const float t = keys[i];
			ImVec2 p;
			p.x = min.x * (1 - t) + max.x * t;
			p.y = max.y;

			PushID(i);
			SetCursorScreenPos(p - ImVec2(5, 9));
			InvisibleButton("", ImVec2(10, 15));

			const bool hovered = IsItemHovered();
			const ImU32 col = hovered ? GetColorU32(ImGuiCol_SliderGrabActive) : GetColorU32(ImGuiCol_SliderGrab);
			dl->AddRectFilled(p - ImVec2(4, 4), p + ImVec2(4, 5), col);
			dl->AddTriangleFilled(p - ImVec2(-4, 4)
				, p - ImVec2(4, 4)
				, p - ImVec2(0, 8)
				, col);
			
			static float start_val;
			if (IsItemActive() && IsMouseClicked(0)) {
				start_val = keys[i];
			}

			if (IsItemActive() && IsMouseDragging(0)) {
				keys[i] = start_val + GetMouseDragDelta().x / (max.x - min.x);
				keys[i] = ImClamp(keys[i], 0.f, 1.f);
				changed = true;
			}
			if (IsItemActive() && IsMouseDoubleClicked(0)) {
				OpenPopup("edit");
			}

			if (BeginPopup("edit")) {
				ColorPicker4("Color", &values[i * 4]);
				EndPopup();
			}

			PopID();
		}

		PopID();
		SetCursorScreenPos(max);
//		NewLine();
		return changed;
	}

	void PushReadOnly() {
		PushItemFlag(ImGuiItemFlags_ReadOnly, true);
		PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
	}

	void PopReadOnly() {
		PopStyleColor();
		PopItemFlag();
	}

	static bool IsRootOfOpenMenuSet()
	{
		ImGuiContext& g = *GImGui;
		ImGuiWindow* window = g.CurrentWindow;
		if ((g.OpenPopupStack.Size <= g.BeginPopupStack.Size) || (window->Flags & ImGuiWindowFlags_ChildMenu))
			return false;

		const ImGuiPopupData* upper_popup = &g.OpenPopupStack[g.BeginPopupStack.Size];
		return (/*upper_popup->OpenParentId == window->IDStack.back() &&*/ upper_popup->Window && (upper_popup->Window->Flags & ImGuiWindowFlags_ChildMenu));
	}

	// copy-pasted from imgui with alwaysautoresize flag removed
	bool BeginResizableMenu(const char* label, const char* icon, bool enabled)
	{
		ImGuiWindow* window = GetCurrentWindow();
		if (window->SkipItems)
			return false;

		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		const ImGuiID id = window->GetID(label);
		bool menu_is_open = IsPopupOpen(id, ImGuiPopupFlags_None);

		// Sub-menus are ChildWindow so that mouse can be hovering across them (otherwise top-most popup menu would steal focus and not allow hovering on parent menu)
		// The first menu in a hierarchy isn't so hovering doesn't get accross (otherwise e.g. resizing borders with ImGuiButtonFlags_FlattenChildren would react), but top-most BeginMenu() will bypass that limitation.
		ImGuiWindowFlags flags = ImGuiWindowFlags_ChildMenu | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;
		if (window->Flags & ImGuiWindowFlags_ChildMenu)
			flags |= ImGuiWindowFlags_ChildWindow;

		// If a menu with same the ID was already submitted, we will append to it, matching the behavior of Begin().
		// We are relying on a O(N) search - so O(N log N) over the frame - which seems like the most efficient for the expected small amount of BeginMenu() calls per frame.
		// If somehow this is ever becoming a problem we can switch to use e.g. ImGuiStorage mapping key to last frame used.
		if (g.MenusIdSubmittedThisFrame.contains(id))
		{
			if (menu_is_open)
				menu_is_open = BeginPopupEx(id, flags); // menu_is_open can be 'false' when the popup is completely clipped (e.g. zero size display)
			else
				g.NextWindowData.ClearFlags();          // we behave like Begin() and need to consume those values
			return menu_is_open;
		}

		// Tag menu as used. Next time BeginMenu() with same ID is called it will append to existing menu
		g.MenusIdSubmittedThisFrame.push_back(id);

		ImVec2 label_size = CalcTextSize(label, NULL, true);

		// Odd hack to allow hovering across menus of a same menu-set (otherwise we wouldn't be able to hover parent without always being a Child window)
		const bool menuset_is_open = IsRootOfOpenMenuSet();
		ImGuiWindow* backed_nav_window = g.NavWindow;
		if (menuset_is_open)
			g.NavWindow = window;

		// The reference position stored in popup_pos will be used by Begin() to find a suitable position for the child menu,
		// However the final position is going to be different! It is chosen by FindBestWindowPosForPopup().
		// e.g. Menus tend to overlap each other horizontally to amplify relative Z-ordering.
		ImVec2 popup_pos, pos = window->DC.CursorPos;
		PushID(label);
		if (!enabled)
			BeginDisabled();
		const ImGuiMenuColumns* offsets = &window->DC.MenuColumns;
		bool pressed;
		const ImGuiSelectableFlags selectable_flags = ImGuiSelectableFlags_NoHoldingActiveID | ImGuiSelectableFlags_SelectOnClick | ImGuiSelectableFlags_DontClosePopups;
		if (window->DC.LayoutType == ImGuiLayoutType_Horizontal)
		{
			// Menu inside an horizontal menu bar
			// Selectable extend their highlight by half ItemSpacing in each direction.
			// For ChildMenu, the popup position will be overwritten by the call to FindBestWindowPosForPopup() in Begin()
			popup_pos = ImVec2(pos.x - 1.0f - IM_FLOOR(style.ItemSpacing.x * 0.5f), pos.y - style.FramePadding.y + window->MenuBarHeight());
			window->DC.CursorPos.x += IM_FLOOR(style.ItemSpacing.x * 0.5f);
			PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x * 2.0f, style.ItemSpacing.y));
			float w = label_size.x;
			ImVec2 text_pos(window->DC.CursorPos.x + offsets->OffsetLabel, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
			pressed = Selectable("", menu_is_open, selectable_flags, ImVec2(w, 0.0f));
			RenderText(text_pos, label);
			PopStyleVar();
			window->DC.CursorPos.x += IM_FLOOR(style.ItemSpacing.x * (-1.0f + 0.5f)); // -1 spacing to compensate the spacing added when Selectable() did a SameLine(). It would also work to call SameLine() ourselves after the PopStyleVar().
		}
		else
		{
			// Menu inside a regular/vertical menu
			// (In a typical menu window where all items are BeginMenu() or MenuItem() calls, extra_w will always be 0.0f.
			//  Only when they are other items sticking out we're going to add spacing, yet only register minimum width into the layout system.
			popup_pos = ImVec2(pos.x, pos.y - style.WindowPadding.y);
			float icon_w = (icon && icon[0]) ? CalcTextSize(icon, NULL).x : 0.0f;
			float checkmark_w = IM_FLOOR(g.FontSize * 1.20f);
			float min_w = window->DC.MenuColumns.DeclColumns(icon_w, label_size.x, 0.0f, checkmark_w); // Feedback to next frame
			float extra_w = ImMax(0.0f, GetContentRegionAvail().x - min_w);
			ImVec2 text_pos(window->DC.CursorPos.x + offsets->OffsetLabel, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
			pressed = Selectable("", menu_is_open, selectable_flags | ImGuiSelectableFlags_SpanAvailWidth, ImVec2(min_w, 0.0f));
			RenderText(text_pos, label);
			if (icon_w > 0.0f)
				RenderText(pos + ImVec2(offsets->OffsetIcon, 0.0f), icon);
			RenderArrow(window->DrawList, pos + ImVec2(offsets->OffsetMark + extra_w + g.FontSize * 0.30f, 0.0f), GetColorU32(ImGuiCol_Text), ImGuiDir_Right);
		}
		if (!enabled)
			EndDisabled();

		const bool hovered = (g.HoveredId == id) && enabled;
		if (menuset_is_open)
			g.NavWindow = backed_nav_window;

		bool want_open = false;
		bool want_close = false;
		if (window->DC.LayoutType == ImGuiLayoutType_Vertical) // (window->Flags & (ImGuiWindowFlags_Popup|ImGuiWindowFlags_ChildMenu))
		{
			// Close menu when not hovering it anymore unless we are moving roughly in the direction of the menu
			// Implement http://bjk5.com/post/44698559168/breaking-down-amazons-mega-dropdown to avoid using timers, so menus feels more reactive.
			bool moving_toward_other_child_menu = false;
			ImGuiWindow* child_menu_window = (g.BeginPopupStack.Size < g.OpenPopupStack.Size && g.OpenPopupStack[g.BeginPopupStack.Size].SourceWindow == window) ? g.OpenPopupStack[g.BeginPopupStack.Size].Window : NULL;
			if (g.HoveredWindow == window && child_menu_window != NULL && !(window->Flags & ImGuiWindowFlags_MenuBar))
			{
				float ref_unit = g.FontSize; // FIXME-DPI
				ImRect next_window_rect = child_menu_window->Rect();
				ImVec2 ta = (g.IO.MousePos - g.IO.MouseDelta);
				ImVec2 tb = (window->Pos.x < child_menu_window->Pos.x) ? next_window_rect.GetTL() : next_window_rect.GetTR();
				ImVec2 tc = (window->Pos.x < child_menu_window->Pos.x) ? next_window_rect.GetBL() : next_window_rect.GetBR();
				float extra = ImClamp(ImFabs(ta.x - tb.x) * 0.30f, ref_unit * 0.5f, ref_unit * 2.5f);   // add a bit of extra slack.
				ta.x += (window->Pos.x < child_menu_window->Pos.x) ? -0.5f : +0.5f;                     // to avoid numerical issues (FIXME: ??)
				tb.y = ta.y + ImMax((tb.y - extra) - ta.y, -ref_unit * 8.0f);                           // triangle is maximum 200 high to limit the slope and the bias toward large sub-menus // FIXME: Multiply by fb_scale?
				tc.y = ta.y + ImMin((tc.y + extra) - ta.y, +ref_unit * 8.0f);
				moving_toward_other_child_menu = ImTriangleContainsPoint(ta, tb, tc, g.IO.MousePos);
				//GetForegroundDrawList()->AddTriangleFilled(ta, tb, tc, moving_toward_other_child_menu ? IM_COL32(0,128,0,128) : IM_COL32(128,0,0,128)); // [DEBUG]
			}
			if (menu_is_open && !hovered && g.HoveredWindow == window && g.HoveredIdPreviousFrame != 0 && g.HoveredIdPreviousFrame != id && !moving_toward_other_child_menu)
				want_close = true;

			// Open
			if (!menu_is_open && pressed) // Click/activate to open
				want_open = true;
			else if (!menu_is_open && hovered && !moving_toward_other_child_menu) // Hover to open
				want_open = true;
			if (g.NavId == id && g.NavMoveDir == ImGuiDir_Right) // Nav-Right to open
			{
				want_open = true;
				NavMoveRequestCancel();
			}
		}
		else
		{
			// Menu bar
			if (menu_is_open && pressed && menuset_is_open) // Click an open menu again to close it
			{
				want_close = true;
				want_open = menu_is_open = false;
			}
			else if (pressed || (hovered && menuset_is_open && !menu_is_open)) // First click to open, then hover to open others
			{
				want_open = true;
			}
			else if (g.NavId == id && g.NavMoveDir == ImGuiDir_Down) // Nav-Down to open
			{
				want_open = true;
				NavMoveRequestCancel();
			}
		}

		if (!enabled) // explicitly close if an open menu becomes disabled, facilitate users code a lot in pattern such as 'if (BeginMenu("options", has_object)) { ..use object.. }'
			want_close = true;
		if (want_close && IsPopupOpen(id, ImGuiPopupFlags_None))
			ClosePopupToLevel(g.BeginPopupStack.Size, true);

		IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Openable | (menu_is_open ? ImGuiItemStatusFlags_Opened : 0));
		PopID();

		if (!menu_is_open && want_open && g.OpenPopupStack.Size > g.BeginPopupStack.Size)
		{
			// Don't recycle same menu level in the same frame, first close the other menu and yield for a frame.
			OpenPopup(label);
			return false;
		}

		menu_is_open |= want_open;
		if (want_open)
			OpenPopup(label);

		if (menu_is_open)
		{
			SetNextWindowPos(popup_pos, ImGuiCond_Always); // Note: this is super misleading! The value will serve as reference for FindBestWindowPosForPopup(), not actual pos.
			PushStyleVar(ImGuiStyleVar_ChildRounding, style.PopupRounding); // First level will use _PopupRounding, subsequent will use _ChildRounding
			menu_is_open = BeginPopupEx(id, flags); // menu_is_open can be 'false' when the popup is completely clipped (e.g. zero size display)
			PopStyleVar();
		}
		else
		{
			g.NextWindowData.ClearFlags(); // We behave like Begin() and need to consume those values
		}

		return menu_is_open;
	}

} // namespace ImGuiEx