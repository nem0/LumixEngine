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

	bool CheckboxEx(const char* label, bool* v)
	{
		ImGuiWindow* window = GetCurrentWindow();
		if (window->SkipItems)
			return false;

		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		const ImGuiID id = window->GetID(label);
		const ImVec2 label_size = CalcTextSize(label, NULL, true);
		ImVec2 dummy_size = ImVec2(CalcItemWidth() - label_size.y - style.FramePadding.y * 2 - style.ItemSpacing.x, label_size.y);
		Dummy(dummy_size);
		SameLine();
		const ImRect check_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(label_size.y + style.FramePadding.y * 2, label_size.y + style.FramePadding.y * 2)); // We want a square shape to we use Y twice
		ItemSize(check_bb, style.FramePadding.y);

		ImRect total_bb = check_bb;
		if (label_size.x > 0)
			SameLine(0, style.ItemInnerSpacing.x);
		const ImRect text_bb(window->DC.CursorPos + ImVec2(0, style.FramePadding.y), window->DC.CursorPos + ImVec2(0, style.FramePadding.y) + label_size);
		if (label_size.x > 0)
		{
			ItemSize(ImVec2(text_bb.GetWidth(), check_bb.GetHeight()), style.FramePadding.y);
			total_bb = ImRect(ImMin(check_bb.Min, text_bb.Min), ImMax(check_bb.Max, text_bb.Max));
		}

		if (!ItemAdd(total_bb, id))
			return false;

		bool hovered, held;
		bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);
		if (pressed)
			*v = !(*v);

		RenderFrame(check_bb.Min, check_bb.Max, GetColorU32((held && hovered) ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), true, style.FrameRounding);
		if (*v)
		{
			const float check_sz = ImMin(check_bb.GetWidth(), check_bb.GetHeight());
			const float pad = ImMax(1.0f, (float)(int)(check_sz / 6.0f));
			ImDrawList* draw_list = GetWindowDrawList();
			RenderCheckMark(draw_list, check_bb.Min + ImVec2(pad, pad), GetColorU32(ImGuiCol_CheckMark), check_bb.GetWidth() - pad*2.0f);
		}

		if (g.LogEnabled)
			LogRenderedText(&text_bb.Min, *v ? "[x]" : "[ ]");
		if (label_size.x > 0.0f)
			RenderText(text_bb.Min, label);

		return pressed;
	}
	

	bool ToolbarButton(ImFont* font, const char* font_icon, const ImVec4& bg_color, const char* tooltip)
	{
		auto frame_padding = ImGui::GetStyle().FramePadding;
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_Text, bg_color);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, frame_padding);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);

		bool ret = false;
		ImGui::PushFont(font);
		if (ImGui::Button(font_icon)) {
			ret = true;
		}
		ImGui::PopFont();
		ImGui::PopStyleColor(4);
		ImGui::PopStyleVar(3);
		if (ImGui::IsItemHovered()) {
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(tooltip);
			ImGui::EndTooltip();
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
		auto frame_padding = ImGui::GetStyle().FramePadding;
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
		return ImGui::GetCurrentContext()->PlatformImePos;
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

		if (ImGui::GetIO().MouseWheel != 0 && ImGui::IsItemHovered())
		{
			float scale = powf(2, ImGui::GetIO().MouseWheel);
			width *= scale;
			height *= scale;
			window->StateStorage.SetFloat((ImGuiID)StorageValues::WIDTH, width);
			window->StateStorage.SetFloat((ImGuiID)StorageValues::HEIGHT, height);
		}
		if (ImGui::IsMouseReleased(2))
		{
			window->StateStorage.SetBool((ImGuiID)StorageValues::IS_PANNING, false);
		}
		if (window->StateStorage.GetBool((ImGuiID)StorageValues::IS_PANNING, false))
		{
			ImVec2 drag_offset = ImGui::GetMouseDragDelta(2);
			from_x = start_pan.x;
			from_y = start_pan.y;
			from_x -= drag_offset.x * width / (inner_bb.Max.x - inner_bb.Min.x);
			from_y += drag_offset.y * height / (inner_bb.Max.y - inner_bb.Min.y);
			window->StateStorage.SetFloat((ImGuiID)StorageValues::FROM_X, from_x);
			window->StateStorage.SetFloat((ImGuiID)StorageValues::FROM_Y, from_y);
		}
		else if (ImGui::IsMouseDragging(2) && ImGui::IsItemHovered())
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
					pos += ImGui::GetMouseDragDelta();
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
				window->DrawList->AddBezierCurve(
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

		if (ImGui::IsItemActive() && ImGui::IsMouseDoubleClicked(0) && new_count)
		{
			ImVec2 mp = ImGui::GetMousePos();
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

		if (hovered_idx >= 0 && ImGui::IsMouseDoubleClicked(0) && new_count && points_count > 2)
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

		ImGui::SetNextWindowSize(size_on_first_use, ImGuiCond_FirstUseEver);
		bool opened = ImGui::Begin(name, NULL, flags);
		if (!opened)
			ImGui::EndPopup();

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
		if (ImGui::IsItemActive())
		{
			size->y = ImMax(1.0f, ImGui::GetIO().MouseDelta.y + size->y);
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
		if (ImGui::IsItemActive())
		{
			size->x = ImMax(1.0f, ImGui::GetIO().MouseDelta.x + size->x);
		}
	}


	bool IconButton(const char* icon, const char* tooltip) {
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);
		bool res = ImGui::SmallButton(icon);
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", tooltip);
		}
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		return res;
	}
	
	void TextClipped(const char* text, float size) {
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 min = ImGui::GetCursorScreenPos();
		ImVec2 max = min;
		max.y += ImGui::GetTextLineHeight();
		max.x += size;
		ImGui::RenderTextEllipsis(dl, min, max, max.x, max.x, text, nullptr, nullptr);
		ImRect text_rect;
		text_rect.Min = min;
		text_rect.Max = max;
		ImGui::ItemSize(text_rect);
	}

	void Label(const char* label) {
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		const ImVec2 lineStart = ImGui::GetCursorScreenPos();
		const ImGuiStyle& style = ImGui::GetStyle();
		float fullWidth = ImGui::GetContentRegionAvail().x;
		float itemWidth = fullWidth * 0.6f;
		ImVec2 textSize = ImGui::CalcTextSize(label);
		ImRect textRect;
		textRect.Min = ImGui::GetCursorScreenPos();
		textRect.Max = textRect.Min;
		textRect.Max.x += fullWidth - itemWidth;
		textRect.Max.y += textSize.y;

		ImGui::SetCursorScreenPos(textRect.Min);

		ImGui::AlignTextToFramePadding();
		textRect.Min.y += window->DC.CurrLineTextBaseOffset;
		textRect.Max.y += window->DC.CurrLineTextBaseOffset;

		ImGui::ItemSize(textRect);
		if (ImGui::ItemAdd(textRect, window->GetID(label)))
		{
			ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), textRect.Min, textRect.Max, textRect.Max.x,
				textRect.Max.x, label, nullptr, &textSize);

			if (textRect.GetWidth() < textSize.x && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", label);
		}
		ImGui::SetCursorScreenPos(textRect.Max - ImVec2{0, textSize.y + window->DC.CurrLineTextBaseOffset});
		ImGui::SameLine();
		ImGui::SetNextItemWidth(itemWidth);
	}

	bool Gradient4(const char* label, int max_count, int* count, float* keys, float* values) {
		ImGui::PushID(label);
		IM_ASSERT(*count > 1);
		IM_ASSERT(keys[0] >= 0 && keys[0] <= 1);
		IM_ASSERT(max_count >= *count);
		
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 min = ImGui::GetCursorScreenPos();
		const float w = ImGui::CalcItemWidth();
		const ImVec2 max = min + ImVec2(w, ImGui::GetTextLineHeight());

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

		ImGui::SetCursorScreenPos(min);
		ImGui::InvisibleButton("gradient", max - min);
		if (ImGui::IsItemActive() && ImGui::IsMouseDoubleClicked(0) && *count < max_count) {
			const float x = ImGui::GetMousePos().x;
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

			ImGui::PushID(i);
			ImGui::SetCursorScreenPos(p - ImVec2(5, 9));
			ImGui::InvisibleButton("", ImVec2(10, 15));

			const bool hovered = ImGui::IsItemHovered();
			const ImU32 col = hovered ? ImGui::GetColorU32(ImGuiCol_SliderGrabActive) : ImGui::GetColorU32(ImGuiCol_SliderGrab);
			dl->AddRectFilled(p - ImVec2(4, 4), p + ImVec2(4, 5), col);
			dl->AddTriangleFilled(p - ImVec2(-4, 4)
				, p - ImVec2(4, 4)
				, p - ImVec2(0, 8)
				, col);
			
			static float start_val;
			if (ImGui::IsItemActive() && ImGui::IsMouseClicked(0)) {
				start_val = keys[i];
			}

			if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
				keys[i] = start_val + ImGui::GetMouseDragDelta().x / (max.x - min.x);
				keys[i] = ImClamp(keys[i], 0.f, 1.f);
				changed = true;
			}
			if (ImGui::IsItemActive() && ImGui::IsMouseDoubleClicked(0)) {
				ImGui::OpenPopup("edit");
			}

			if (ImGui::BeginPopup("edit")) {
				ImGui::ColorPicker4("Color", &values[i * 4]);
				ImGui::EndPopup();
			}

			ImGui::PopID();
		}

		ImGui::PopID();
		ImGui::SetCursorScreenPos(max);
//		ImGui::NewLine();
		return changed;
	}
}