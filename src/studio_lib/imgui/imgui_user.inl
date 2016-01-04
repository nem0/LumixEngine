#include "imgui.h"
#include "imgui_internal.h"
#include "core/fs/os_file.h"
#include <lua.hpp>


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

    const ImVec2 label_size = CalcTextSize(label, NULL, true);
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
        SetTooltip("%d: %8.4g", v_idx, v0);
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


bool ListBox(const char* label,
    int* current_item,
    int scroll_to_item,
    bool (*items_getter)(void*, int, const char**),
    void* data,
    int items_count,
    int height_in_items)
{
    if (!ListBoxHeader(label, items_count, height_in_items)) return false;

    // Assume all items have even height (= 1 line of text). If you need items of different or
    // variable sizes you can create a custom version of ListBox() in your code without using the
    // clipper.
    bool value_changed = false;
    if (scroll_to_item != -1)
    {
        SetScrollY(scroll_to_item * GetTextLineHeightWithSpacing());
    }
    ImGuiListClipper clipper(items_count, GetTextLineHeightWithSpacing());
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
    {
        const bool item_selected = (i == *current_item);
        const char* item_text;
        if (!items_getter(data, i, &item_text)) item_text = "*Unknown item*";

        PushID(i);
        if (Selectable(item_text, item_selected))
        {
            *current_item = i;
            value_changed = true;
        }
        PopID();
    }
    clipper.End();
    ListBoxFooter();
    return value_changed;
}


void ResetActiveID()
{
    SetActiveID(0);
}


void BringToFront()
{
    ImGuiState& g = *GImGui;

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
    SetCursorScreenPos(screen_pos + GetStyle().WindowPadding);
    PushItemWidth(200);
    ImDrawList* draw_list = GetWindowDrawList();
    draw_list->ChannelsSplit(2);
    draw_list->ChannelsSetCurrent(1);
    BeginGroup();
}


void EndNode(ImVec2& pos)
{
    ImDrawList* draw_list = GetWindowDrawList();

    EndGroup();
    PopItemWidth();

    float height = GetCursorScreenPos().y - node_pos.y;
    ImVec2 size(200, height + GetStyle().WindowPadding.y);
    SetCursorScreenPos(node_pos);

    SetNextWindowPos(node_pos);
    SetNextWindowSize(size);
    BeginChild((ImGuiID)last_node_id, size, false, ImGuiWindowFlags_NoInputs);
    EndChild();

    SetCursorScreenPos(node_pos);
    InvisibleButton("bg", size);
    if (IsItemActive() && IsMouseDragging(0))
    {
        pos += GetIO().MouseDelta;
    }

    draw_list->ChannelsSetCurrent(0);
    draw_list->AddRectFilled(node_pos, node_pos + size, ImColor(60, 60, 60), 4.0f);
    draw_list->AddRect(node_pos, node_pos + size, ImColor(100, 100, 100), 4.0f);

    PopID();
    draw_list->ChannelsMerge();
}


ImVec2 GetNodeInputPos(ImGuiID id, int input)
{
    PushID(id);

    ImGuiWindow* parent_win = GetCurrentWindow();
    char title[256];
    ImFormatString(title, IM_ARRAYSIZE(title), "%s.child_%08x", parent_win->Name, id);
    ImGuiWindow* win = FindWindowByName(title);
    if (!win)
    {
        PopID();
        return ImVec2(0, 0);
    }

    ImVec2 pos = win->Pos;
    pos.x -= NODE_SLOT_RADIUS;
    ImGuiStyle& style = GetStyle();
    pos.y += (GetTextLineHeight() + style.ItemSpacing.y) * input;
    pos.y += style.WindowPadding.y + GetTextLineHeight() * 0.5f;


    PopID();
    return pos;
}


ImVec2 GetNodeOutputPos(ImGuiID id, int output)
{
    PushID(id);

    ImGuiWindow* parent_win = GetCurrentWindow();
    char title[256];
    ImFormatString(title, IM_ARRAYSIZE(title), "%s.child_%08x", parent_win->Name, id);
    ImGuiWindow* win = FindWindowByName(title);
    if (!win)
    {
        PopID();
        return ImVec2(0, 0);
    }

    ImVec2 pos = win->Pos;
    pos.x += win->Size.x + NODE_SLOT_RADIUS;
    ImGuiStyle& style = GetStyle();
    pos.y += (GetTextLineHeight() + style.ItemSpacing.y) * output;
    pos.y += style.WindowPadding.y + GetTextLineHeight() * 0.5f;

    PopID();
    return pos;
}


bool NodePin(ImGuiID id, ImVec2 screen_pos)
{
    ImDrawList* draw_list = GetWindowDrawList();
    SetCursorScreenPos(screen_pos - ImVec2(NODE_SLOT_RADIUS, NODE_SLOT_RADIUS));
    PushID(id);
    InvisibleButton("", ImVec2(2 * NODE_SLOT_RADIUS, 2 * NODE_SLOT_RADIUS));
    bool hovered = IsItemHovered();
    PopID();
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
    ImDrawList* draw_list = GetWindowDrawList();
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
    ImVec2 cursor_pos = GetCursorScreenPos();

    const ImVec2 label_size = CalcTextSize(label, NULL, true);
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
    PushID(label);

    RenderFrame(
        frame_bb.Min, frame_bb.Max, GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);
    RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, inner_bb.Min.y), label);

    editor.beg_pos = cursor_pos;
    SetCursorScreenPos(cursor_pos);

    editor.point_idx = -1;

    return editor;
}


void EndCurveEditor(const CurveEditor& editor)
{
    SetCursorScreenPos(editor.beg_pos);

    InvisibleButton("bg", ImVec2(CalcItemWidth(), 100));
    PopID();
}


bool CurvePoint(ImVec2* points, CurveEditor& editor)
{
    ImGuiWindow* window = GetCurrentWindow();
    ImGuiState& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    ImVec2 cursor_pos_backup = GetCursorScreenPos();

    ImVec2 graph_size;
    graph_size.x = CalcItemWidth() + (style.FramePadding.x * 2);
    graph_size.y = 100; // label_size.y + (style.FramePadding.y * 2);

    const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + graph_size);
    const ImRect inner_bb(frame_bb.Min + style.FramePadding, frame_bb.Max - style.FramePadding);
    const ImU32 col_base = GetColorU32(ImGuiCol_PlotLines);
    const ImU32 col_hovered = GetColorU32(ImGuiCol_PlotLinesHovered);

    ImVec2 left_tangent = points[0];
    ImVec2 right_tangent = points[2];
    ImVec2 p = points[1];
    auto transform = [inner_bb](const ImVec2& p) -> ImVec2
    {
        return ImVec2(inner_bb.Min.x * (1 - p.x) + inner_bb.Max.x * p.x,
            inner_bb.Min.y * p.y + inner_bb.Max.y * (1 - p.y));
    };

    ImVec2 pos = transform(p);
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
    SetCursorScreenPos(pos - ImVec2(SIZE, SIZE));
    PushID(editor.point_idx);
    ++editor.point_idx;
    InvisibleButton("", ImVec2(2 * NODE_SLOT_RADIUS, 2 * NODE_SLOT_RADIUS));

    ImU32 col = IsItemHovered() ? col_hovered : col_base;

    window->DrawList->AddLine(pos + ImVec2(-SIZE, 0), pos + ImVec2(0, SIZE), col);
    window->DrawList->AddLine(pos + ImVec2(SIZE, 0), pos + ImVec2(0, SIZE), col);
    window->DrawList->AddLine(pos + ImVec2(SIZE, 0), pos + ImVec2(0, -SIZE), col);
    window->DrawList->AddLine(pos + ImVec2(-SIZE, 0), pos + ImVec2(0, -SIZE), col);

    bool changed = false;
    if (IsItemActive() && IsMouseDragging(0))
    {
        pos += GetIO().MouseDelta;
        ImVec2 v;
        v.x = (pos.x - inner_bb.Min.x) / (inner_bb.Max.x - inner_bb.Min.x);
        v.y = (inner_bb.Max.y - pos.y) / (inner_bb.Max.y - inner_bb.Min.y);

        v = ImClamp(v, ImVec2(0, 0), ImVec2(1, 1));
        points[1] = v;
        changed = true;
    }
    PopID();

    SetCursorScreenPos(cursor_pos_backup);
    return changed;
}


struct DockContext
{
    enum Slot_
    {
        Slot_Left,
        Slot_Right,
        Slot_Top,
        Slot_Bottom,
        Slot_Tab,

        Slot_Float,
        Slot_None
    };


    enum EndAction_
    {
        EndAction_None,
        EndAction_Panel,
        EndAction_End,
        EndAction_EndChild
    };


    enum Status_
    {
        Status_Docked,
        Status_Float,
        Status_Dragged
    };


    struct Dock
    {
        Dock()
            : id(0)
            , next_tab(nullptr)
            , prev_tab(nullptr)
            , parent(nullptr)
            , pos(0, 0)
            , size(-1, -1)
            , active(true)
            , status(Status_Float)
            , label(nullptr)
        {
            children[0] = children[1] = nullptr;
        }


        ~Dock()
        {
            MemFree(label);
        }


        void setParent(Dock* dock)
        {
            parent = dock;
            for (Dock* tmp = prev_tab; tmp; tmp = tmp->prev_tab) tmp->parent = dock;
            for (Dock* tmp = next_tab; tmp; tmp = tmp->next_tab) tmp->parent = dock;
        }


        Dock& getSibling()
        {
            IM_ASSERT(parent);
            if (parent->children[0] == &getFirstTab()) return *parent->children[1];
            return *parent->children[0];
        }


        Dock& getFirstTab()
        {
            Dock* tmp = this;
            while (tmp->prev_tab) tmp = tmp->prev_tab;
            return *tmp;
        }


        void setActive()
        {
            active = true;
            for (Dock* tmp = prev_tab; tmp; tmp = tmp->prev_tab) tmp->active = false;
            for (Dock* tmp = next_tab; tmp; tmp = tmp->next_tab) tmp->active = false;
        }


        bool isContainer() const { return children[0] != nullptr; }


        void setChildrenPosSize(const ImVec2& _pos, const ImVec2& _size)
        {
            if (children[0]->pos.x < children[1]->pos.x)
            {
                ImVec2 s = children[0]->size;
                s.y = _size.y;
				s.x = (float)int(_size.x * children[0]->size.x / (children[0]->size.x + children[1]->size.x));
                children[0]->setPosSize(_pos, s);

                s.x = _size.x - children[0]->size.x;
                ImVec2 p = _pos;
                p.x += children[0]->size.x;
                children[1]->setPosSize(p, s);
            }
            else if (children[0]->pos.x > children[1]->pos.x)
            {
                ImVec2 s = children[1]->size;
                s.y = _size.y;
				s.x = (float)int(_size.x * children[1]->size.x / (children[0]->size.x + children[1]->size.x));
				children[1]->setPosSize(_pos, s);

                s.x = _size.x - children[1]->size.x;
                ImVec2 p = _pos;
                p.x += children[1]->size.x;
                children[0]->setPosSize(p, s);
            }
            else if (children[0]->pos.y < children[1]->pos.y)
            {
                ImVec2 s = children[0]->size;
                s.x = _size.x;
				s.y = (float)int(_size.y * children[0]->size.y / (children[0]->size.y + children[1]->size.y));
				children[0]->setPosSize(_pos, s);

                s.y = _size.y - children[0]->size.y;
                ImVec2 p = _pos;
                p.y += children[0]->size.y;
                children[1]->setPosSize(p, s);
            }
            else
            {
                ImVec2 s = children[1]->size;
                s.x = _size.x;
				s.y = (float)int(_size.y * children[1]->size.y / (children[0]->size.y + children[1]->size.y));
				children[1]->setPosSize(_pos, s);

                s.y = _size.y - children[1]->size.y;
                ImVec2 p = _pos;
                p.y += children[1]->size.y;
                children[0]->setPosSize(p, s);
            }
        }


        void setPosSize(const ImVec2& _pos, const ImVec2& _size)
        {
            size = _size;
            pos = _pos;
            for (Dock* tmp = prev_tab; tmp; tmp = tmp->prev_tab)
            {
                tmp->size = _size;
                tmp->pos = _pos;
            }
            for (Dock* tmp = next_tab; tmp; tmp = tmp->next_tab)
            {
                tmp->size = _size;
                tmp->pos = _pos;
            }

            if (!isContainer()) return;
            setChildrenPosSize(_pos, _size);
        }


        char*	label;
        ImU32	id;
        Dock*	next_tab;
        Dock*	prev_tab;
        Dock*	children[2];
        Dock*	parent;
        bool	active;
        ImVec2	pos;
        ImVec2	size;
        Status_	status;
    };


    ImVector<Dock*>		m_docks;
    ImVec2				m_drag_offset;
    Dock*				m_current = nullptr;
    int					m_last_frame = 0;
    EndAction_			m_end_action;


    ~DockContext()
    {
    }


    Dock& getDock(const char* label, bool opened)
    {
        ImU32 id = ImHash(label, 0);
        for (int i = 0; i < m_docks.size(); ++i)
        {
            if (m_docks[i]->id == id) return *m_docks[i];
        }

        Dock* new_dock = (Dock*)MemAlloc(sizeof(Dock));
        new (new_dock) Dock();
        m_docks.push_back(new_dock);
        static Dock* q = nullptr; // TODO
        new_dock->label = ImStrdup(label);
        new_dock->id = id;
        new_dock->setActive();
        if (opened)
        {
            if (q)
            {
                q->prev_tab = new_dock;
                new_dock->next_tab = q;
                new_dock->setPosSize(q->pos, q->size);
            }
            new_dock->status = Status_Docked;
            q = new_dock;
        }
        else
        {
            new_dock->status = Status_Float;
        }
        return *new_dock;
    }


    void putInBackground()
    {
        ImGuiWindow* win = GetCurrentWindow();
        ImGuiState& g = *GImGui;
        if (g.Windows[0] == win) return;

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


    void drawSplits()
    {
        if (GetFrameCount() == m_last_frame) return;
        m_last_frame = GetFrameCount();

        putInBackground();

        ImU32 color = GetColorU32(ImGuiCol_Button);
        ImU32 color_hovered = GetColorU32(ImGuiCol_ButtonHovered);
        ImDrawList* draw_list = GetWindowDrawList();
        ImGuiIO& io = GetIO();
        for (int i = 0; i < m_docks.size(); ++i)
        {
            Dock& dock = *m_docks[i];
            if (!dock.isContainer()) continue;

            PushID(i);
            if (!IsMouseDown(0)) dock.status = Status_Docked;

            ImVec2 p0 = dock.children[0]->pos;
            ImVec2 p1 = dock.children[1]->pos;
            ImVec2 size = dock.children[0]->size;
            if (p0.x < p1.x)
            {
                SetCursorScreenPos(p1);
                InvisibleButton("split", ImVec2(3, size.y));
                if (dock.status == Status_Dragged)
                {
                    dock.children[0]->size.x += io.MouseDelta.x;
                    dock.children[1]->size.x -= io.MouseDelta.x;
                    dock.children[1]->pos.x += io.MouseDelta.x;
                }
            }
            else if (p0.x > p1.x)
            {
                SetCursorScreenPos(p0);
                InvisibleButton("split", ImVec2(3, size.y));
                if (dock.status == Status_Dragged)
                {
                    dock.children[1]->size.x += io.MouseDelta.x;
                    dock.children[0]->size.x -= io.MouseDelta.x;
                    dock.children[0]->pos.x += io.MouseDelta.x;
                }
            }
            else if (p0.y < p1.y)
            {
                SetCursorScreenPos(p1);
                InvisibleButton("split", ImVec2(size.x, 3));
                if (dock.status == Status_Dragged)
                {
                    dock.children[0]->size.y += io.MouseDelta.y;
                    dock.children[1]->size.y -= io.MouseDelta.y;
                    dock.children[1]->pos.y += io.MouseDelta.y;
                }
            }
            else
            {
                SetCursorScreenPos(p0);
                InvisibleButton("split", ImVec2(size.x, 3));
                if (dock.status == Status_Dragged)
                {
                    dock.children[1]->size.y += io.MouseDelta.y;
                    dock.children[0]->size.y -= io.MouseDelta.y;
                    dock.children[0]->pos.y += io.MouseDelta.y;
                }
            }

            if (IsItemHoveredRect() && IsMouseClicked(0))
            {
                dock.status = Status_Dragged;
            }
            if (dock.status == Status_Dragged)
            {
                dock.children[0]->setPosSize(dock.children[0]->pos, dock.children[0]->size);
                dock.children[1]->setPosSize(dock.children[1]->pos, dock.children[1]->size);
            }

            draw_list->AddRectFilled(GetItemRectMin(),
                GetItemRectMax(),
                IsItemHoveredRect() ? color_hovered : color);
            PopID();
        }
    }


    void beginPanel()
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse;
        ImVec2 pos(0, GetTextLineHeightWithSpacing());
        SetNextWindowPos(pos);
        SetNextWindowSize(GetIO().DisplaySize - pos);
        Begin("###DockPanel", nullptr, flags);
        drawSplits();
    }


    void endPanel() { End(); }


    Dock* getDockAt(const ImVec2& pos) const
    {
        for (int i = 0; i < m_docks.size(); ++i)
        {
            Dock& dock = *m_docks[i];
            if (dock.isContainer()) continue;
            if (dock.status != Status_Docked) continue;
            if (IsMouseHoveringRect(dock.pos, dock.pos + dock.size, false))
            {
                return &dock;
            }
        }

        return nullptr;
    }


    static ImRect getDockedRect(const ImRect& rect, Slot_ dock_slot)
    {
        ImVec2 half_size = rect.GetSize() * 0.5f;
        switch (dock_slot)
        {
            default: return rect;
			case Slot_Top: return ImRect(rect.Min, rect.Min + ImVec2(rect.Max.x, half_size.y));
            case Slot_Right:
				return ImRect(rect.Min + ImVec2(half_size.x, 0), rect.Max);
            case Slot_Bottom:
				return ImRect(rect.Min + ImVec2(0, half_size.y), rect.Max);
			case Slot_Left: return ImRect(rect.Min, rect.Min + ImVec2(half_size.x, rect.Max.y));
        }
    }


    static ImRect getSlotRect(ImRect parent_rect, Slot_ dock_slot)
    {
		ImVec2 size = parent_rect.Max - parent_rect.Min;
		ImVec2 center = parent_rect.Min + size * 0.5f;
		switch (dock_slot)
		{
			default: return ImRect(center - ImVec2(20, 20), center + ImVec2(20, 20));
			case Slot_Top: return ImRect(center + ImVec2(-20, -50), center + ImVec2(20, -30));
			case Slot_Right: return ImRect(center + ImVec2(30, -20), center + ImVec2(50, 20));
			case Slot_Bottom: return ImRect(center + ImVec2(-20, +30), center + ImVec2(20, 50));
			case Slot_Left: return ImRect(center + ImVec2(-50, -20), center + ImVec2(-30, 20));
		}
	}


	static ImRect getSlotRectOnBorder(ImRect parent_rect, Slot_ dock_slot)
	{
		ImVec2 size = parent_rect.Max - parent_rect.Min;
		ImVec2 center = parent_rect.Min + size * 0.5f;
		switch (dock_slot)
		{
			case Slot_Top:
				return ImRect(ImVec2(center.x - 20, parent_rect.Min.y + 10),
					ImVec2(center.x + 20, parent_rect.Min.y + 30));
			case Slot_Left:
				return ImRect(ImVec2(parent_rect.Min.x + 10, center.y - 20),
					ImVec2(parent_rect.Min.x + 30, center.y + 20));
			case Slot_Bottom:
				return ImRect(ImVec2(center.x - 20, parent_rect.Max.y - 30),
					ImVec2(center.x + 20, parent_rect.Max.y - 10));
			case Slot_Right:
				return ImRect(ImVec2(parent_rect.Max.x - 30, center.y - 20),
					ImVec2(parent_rect.Max.x - 10, center.y + 20));
		}
		IM_ASSERT(false);
		return ImRect();
	}


	Dock* getRootDock()
	{
		for (int i = 0; i < m_docks.size(); ++i)
		{
			if (!m_docks[i]->parent && m_docks[i]->status == Status_Docked)
			{
				return m_docks[i];
			}
		}
		return nullptr;
	}


	bool dockSlots(Dock& dock, Dock* dest_dock, const ImRect& rect, bool on_border)
	{
		ImDrawList* canvas = GetWindowDrawList();
		ImU32 text_color = GetColorU32(ImGuiCol_Text);
		ImU32 color = GetColorU32(ImGuiCol_Button);
		ImU32 color_hovered = GetColorU32(ImGuiCol_ButtonHovered);
		ImVec2 mouse_pos = GetIO().MousePos;
		for (int i = 0; i < (on_border ? 4 : 5); ++i)
		{
			ImRect r = on_border ? getSlotRectOnBorder(rect, (Slot_)i) : getSlotRect(rect, (Slot_)i);
			bool hovered = r.Contains(mouse_pos);
			canvas->AddRectFilled(r.Min, r.Max, hovered ? color_hovered : color);
			if (!hovered) continue;

			if (!IsMouseDown(0))
			{
				doDock(dock, dest_dock ? dest_dock : getRootDock(), (Slot_)i);
				return true;
			}
			ImRect docked_rect = getDockedRect(rect, (Slot_)i);
			canvas->AddRectFilled(docked_rect.Min, docked_rect.Max, GetColorU32(ImGuiCol_TitleBg));
		}
		return false;
	}


	void handleDrag(Dock& dock)
    {
        Dock* dest_dock = getDockAt(GetIO().MousePos);

        Begin("##Overlay",
            NULL,
            ImVec2(0, 0),
            0.f,
            ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_AlwaysAutoResize);
        ImDrawList* canvas = GetWindowDrawList();

        canvas->PushClipRectFullScreen();

        ImU32 docked_color = GetColorU32(ImGuiCol_FrameBg);
		dock.pos = GetIO().MousePos - m_drag_offset;
        if (dest_dock)
        {
			if (dockSlots(dock, dest_dock, ImRect(dest_dock->pos, dest_dock->pos + dest_dock->size), false))
			{
				canvas->PopClipRect();
				End();
				return;
			}
        }
		if(dockSlots(dock, nullptr, ImRect(ImVec2(0, 0), GetIO().DisplaySize), true))
		{
			canvas->PopClipRect();
			End();
			return;
		}
		canvas->AddRectFilled(dock.pos, dock.pos + dock.size, docked_color);
        canvas->PopClipRect();

        if (!IsMouseDown(0))
        {
            dock.status = Status_Float;
            dock.setActive();
        }

        End();
    }


    void doUndock(Dock& dock)
    {
        if (dock.prev_tab) dock.prev_tab->setActive();
        else if (dock.next_tab) dock.next_tab->setActive();
        else dock.active = false;
        Dock* container = dock.parent;

        if (container)
        {
            Dock& sibling = dock.getSibling();
            if (container->children[0] == &dock)
            {
                container->children[0] = dock.next_tab;
            }
            else if (container->children[1] == &dock)
            {
                container->children[1] = dock.next_tab;
            }

            bool remove_container = !container->children[0] || !container->children[1];
            if (remove_container)
            {
                if (container->parent)
                {
                    Dock*& child = container->parent->children[0] == container
                        ? container->parent->children[0]
                        : container->parent->children[1];
                    child = &sibling;
                    child->setPosSize(container->pos, container->size);
                    child->setParent(container->parent);
                }
                else
                {
                    if (container->children[0])
                    {
                        container->children[0]->setParent(nullptr);
                        container->children[0]->setPosSize(container->pos, container->size);
                    }
                    if (container->children[1])
                    {
                        container->children[1]->setParent(nullptr);
                        container->children[1]->setPosSize(container->pos, container->size);
                    }
                }
                for (int i = 0; i < m_docks.size(); ++i)
                {
                    if (m_docks[i] == container)
                    {
                        m_docks.erase(m_docks.begin() + i);
                        break;
                    }
                }
                container->~Dock();
                MemFree(container);
            }
        }
        if (dock.prev_tab) dock.prev_tab->next_tab = dock.next_tab;
        if (dock.next_tab) dock.next_tab->prev_tab = dock.prev_tab;
        dock.parent = nullptr;
        dock.prev_tab = dock.next_tab = nullptr;
    }


    void drawTabbarListButton(Dock& dock)
    {
        if (!dock.next_tab) return;

        ImDrawList* draw_list = GetWindowDrawList();
        if (InvisibleButton("list", ImVec2(16, 16)))
        {
            OpenPopup("tab_list_popup");
        }
        if (BeginPopup("tab_list_popup"))
        {
            Dock* tmp = &dock;
            while (tmp)
            {
                bool dummy = false;
                if (Selectable(tmp->label, &dummy))
                {
                    tmp->setActive();
                }
                tmp = tmp->next_tab;
            }
            EndPopup();
        }

        bool hovered = IsItemHovered();
        ImVec2 min = GetItemRectMin();
        ImVec2 max = GetItemRectMax();
        ImVec2 center = (min + max) * 0.5f;
        ImU32 text_color = GetColorU32(ImGuiCol_Text);
        ImU32 color_active = GetColorU32(ImGuiCol_FrameBgActive);
        draw_list->AddRectFilled(ImVec2(center.x - 4, min.y + 3),
            ImVec2(center.x + 4, min.y + 5),
            hovered ? color_active : text_color);
        draw_list->AddTriangleFilled(ImVec2(center.x - 4, min.y + 7),
            ImVec2(center.x + 4, min.y + 7),
            ImVec2(center.x, min.y + 12),
            hovered ? color_active : text_color);
    }


    bool tabbar(Dock& dock, bool close_button)
    {
        float tabbar_height = 2 * GetTextLineHeightWithSpacing();
        ImVec2 size(dock.size.x, tabbar_height);
        bool tab_closed = false;

        SetCursorScreenPos(dock.pos);
		char tmp[20];
		ImFormatString(tmp, IM_ARRAYSIZE(tmp), "tabs%d", (int)dock.id);
        if (BeginChild(tmp, size, true))
        {
            Dock* dock_tab = &dock;

            ImDrawList* draw_list = GetWindowDrawList();
            ImU32 color = GetColorU32(ImGuiCol_FrameBg);
            ImU32 color_active = GetColorU32(ImGuiCol_FrameBgActive);
            ImU32 color_hovered = GetColorU32(ImGuiCol_FrameBgHovered);
            ImU32 text_color = GetColorU32(ImGuiCol_Text);
            float line_height = GetTextLineHeightWithSpacing();
            float tab_base;

            drawTabbarListButton(dock);

            while (dock_tab)
            {
                SameLine(0, 15);

                const char* text_end = FindTextDisplayEnd(dock_tab->label);
                ImVec2 size(CalcTextSize(dock_tab->label, text_end).x, line_height);
                if (InvisibleButton(dock_tab->label, size))
                {
                    dock_tab->setActive();
                }

                if (IsItemActive() && IsMouseDragging())
                {
                    m_drag_offset = GetMousePos() - dock_tab->pos;
                    doUndock(*dock_tab);
                    dock_tab->status = Status_Dragged;
                }

                bool hovered = IsItemHovered();
                ImVec2 pos = GetItemRectMin();
                if (dock_tab->active && close_button)
                {
                    size.x += 16 + GetStyle().ItemSpacing.x;
                    SameLine();
                    tab_closed = InvisibleButton("close", ImVec2(16, 16));
                    ImVec2 center = (GetItemRectMin() + GetItemRectMax()) * 0.5f;
                    draw_list->AddLine(center + ImVec2(-3.5f, -3.5f), center + ImVec2( 3.5f, 3.5f), text_color);
                    draw_list->AddLine(center + ImVec2( 3.5f, -3.5f), center + ImVec2(-3.5f, 3.5f), text_color);
                }
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
                draw_list->AddText(pos, text_color, dock_tab->label, text_end);

                dock_tab = dock_tab->next_tab;
            }
            ImVec2 cp(dock.pos.x, tab_base + line_height);
            draw_list->AddLine(cp, cp + ImVec2(dock.size.x, 0), color);
        }
        EndChild();
		return tab_closed;
    }


    static void setDockPosSize(Dock& dest, Dock& dock, Slot_ dock_slot, Dock& container)
    {
        IM_ASSERT(!dock.prev_tab && !dock.next_tab && !dock.children[0] && !dock.children[1]);

        dest.pos = container.pos;
        dest.size = container.size;
        dock.pos = container.pos;
        dock.size = container.size;

        switch (dock_slot)
        {
            case Slot_Bottom:
                dest.size.y *= 0.5f;
                dock.size.y *= 0.5f;
                dock.pos.y += dest.size.y;
                break;
            case Slot_Right:
                dest.size.x *= 0.5f;
                dock.size.x *= 0.5f;
                dock.pos.x += dest.size.x;
                break;
            case Slot_Left:
                dest.size.x *= 0.5f;
                dock.size.x *= 0.5f;
                dest.pos.x += dock.size.x;
                break;
            case Slot_Top:
                dest.size.y *= 0.5f;
                dock.size.y *= 0.5f;
                dest.pos.y += dock.size.y;
                break;
            default: IM_ASSERT(false); break;
        }
        dest.setPosSize(dest.pos, dest.size);
    }


    void doDock(Dock& dock, Dock* dest, Slot_ dock_slot)
    {
        IM_ASSERT(!dock.parent);
		if (!dest)
		{
			dock.status = Status_Docked;
			ImVec2 pos = ImVec2(0, GetTextLineHeightWithSpacing());
			dock.setPosSize(pos, GetIO().DisplaySize - pos);
		}
        else if (dock_slot == Slot_Tab)
        {
            Dock* tmp = dest;
            while (tmp->next_tab)
            {
                tmp = tmp->next_tab;
            }

            tmp->next_tab = &dock;
            dock.prev_tab = tmp;
            dock.size = tmp->size;
            dock.pos = tmp->pos;
            dock.parent = dest->parent;
            dock.status = Status_Docked;
        }
        else if (dock_slot == Slot_None)
        {
            dock.status = Status_Float;
        }
        else
        {
            Dock* container = (Dock*)MemAlloc(sizeof(Dock));
            new (container) Dock();
            m_docks.push_back(container);
            container->children[0] = &dest->getFirstTab();
            container->children[1] = &dock;
            container->next_tab = nullptr;
            container->prev_tab = nullptr;
            container->parent = dest->parent;
            container->size = dest->size;
            container->pos = dest->pos;
            container->status = Status_Docked;

            if (!dest->parent)
            {
            }
            else if (&dest->getFirstTab() == dest->parent->children[0])
            {
                dest->parent->children[0] = container;
            }
            else
            {
                dest->parent->children[1] = container;
            }

            dest->setParent(container);
            dock.parent = container;
            dock.status = Status_Docked;

			setDockPosSize(*dest, dock, dock_slot, *container);
        }
        dock.setActive();
    }


    bool begin(const char* label, bool* opened, ImGuiWindowFlags extra_flags)
    {
        Dock& dock = getDock(label, !opened || *opened);
        m_end_action = EndAction_None;

        if (opened && !*opened)
        {
            if (dock.status != Status_Float)
            {
                doUndock(dock);
                dock.status = Status_Float;
            }
            return false;
        }

        m_end_action = EndAction_Panel;
        beginPanel();

        m_current = &dock;
        if (dock.status == Status_Dragged) handleDrag(dock);

        bool is_float = dock.status == Status_Float;

        if (!dock.parent && dock.size.x < 0 && dock.status != Status_Dragged)
        {
            dock.pos = ImVec2(0, GetTextLineHeightWithSpacing() + 4);
            dock.size = GetIO().DisplaySize;
            dock.size.y -= dock.pos.y;
        }

        if (is_float)
        {
            SetNextWindowPos(dock.pos);
            SetNextWindowSize(dock.size);
            bool ret = Begin(
                label, opened, dock.size, -1.0f, ImGuiWindowFlags_NoCollapse | extra_flags);
            m_end_action = EndAction_End;
            dock.pos = GetWindowPos();
            dock.size = GetWindowSize();

            ImGuiState& g = *GImGui;

            if (g.ActiveId == GetCurrentWindow()->MoveID && g.IO.MouseDown[0])
            {
                m_drag_offset = GetMousePos() - dock.pos;
                doUndock(dock);
                dock.status = Status_Dragged;
            }
            return ret;
        }

        if (!dock.active && dock.status != Status_Dragged) return false;
        m_end_action = EndAction_EndChild;

        PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        float tabbar_height = GetTextLineHeightWithSpacing();
        if (tabbar(dock.getFirstTab(), opened != nullptr))
        {
            *opened = false;
        }
        ImVec2 pos = dock.pos;
        ImVec2 size = dock.size;
        pos.y += tabbar_height + GetStyle().WindowPadding.y;
        size.y -= tabbar_height + GetStyle().WindowPadding.y;

        SetCursorScreenPos(pos);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoSavedSettings | extra_flags;
        bool ret = BeginChild(label, size, true, flags);
        ImDrawList* draw_list = GetWindowDrawList();
        return ret;
    }


    void end()
    {
        if (m_end_action == EndAction_End)
        {
            End();
        }
        else if (m_end_action == EndAction_EndChild)
        {
            EndChild();
            PopStyleColor();
        }
        m_current = nullptr;
        if (m_end_action > EndAction_None) endPanel();
    }


    int getDockIndex(Dock* dock)
    {
        if (!dock) return -1;

        for (int i = 0; i < m_docks.size(); ++i)
        {
            if (dock == m_docks[i]) return i;
        }

        IM_ASSERT(false);
        return -1;
    }


    void save(Lumix::FS::OsFile& file)
    {
        file << "m_docks = {\n";
        for (int i = 0; i < m_docks.size(); ++i)
        {
            Dock& dock = *m_docks[i];
            file << "dock" << (Lumix::uint64)&dock << " = {\n";
            file << "index = " << i << ",\n";
            file << "label = \"" << dock.label << "\",\n";
            file << "x = " << (int)dock.pos.x << ",\n";
            file << "y = " << (int)dock.pos.y << ",\n";
            file << "size_x = " << (int)dock.size.x << ",\n";
            file << "size_y = " << (int)dock.size.y << ",\n";
            file << "status = " << (int)dock.status << ",\n";
            file << "active = " << (int)dock.active << ",\n";
            file << "prev = " << (int)getDockIndex(dock.prev_tab) << ",\n";
            file << "next = " << (int)getDockIndex(dock.next_tab) << ",\n";
            file << "child0 = " << (int)getDockIndex(dock.children[0]) << ",\n";
            file << "child1 = " << (int)getDockIndex(dock.children[1]) << ",\n";
            file << "parent = " << (int)getDockIndex(dock.parent) << "\n";
            if (i < m_docks.size() - 1)
                file << "},\n";
            else
                file << "}\n";
        }
        file << "}\n";
    }


    Dock* getDockByIndex(lua_Integer idx)
    {
        return idx < 0 ? nullptr : m_docks[(int)idx];
    }


    void load(lua_State* L)
    {
        for (int i = 0; i < m_docks.size(); ++i)
        {
            m_docks[i]->~Dock();
            MemFree(m_docks[i]);
        }
        m_docks.clear();

        if (lua_getglobal(L, "m_docks") == LUA_TTABLE)
        {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0)
            {
                Dock* new_dock = (Dock*)MemAlloc(sizeof(Dock));
                m_docks.push_back(new (new_dock) Dock());
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        int i = 0;
        if (lua_getglobal(L, "m_docks") == LUA_TTABLE)
        {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0)
            {
                if (lua_istable(L, -1))
                {
                    int idx;
                    if (lua_getfield(L, -1, "index") == LUA_TNUMBER)
                        idx = (int)lua_tointeger(L, -1);
                    Dock& dock = *m_docks[idx];
                    lua_pop(L, 1);

                    if (lua_getfield(L, -1, "label") == LUA_TSTRING)
                    {
                        dock.label = ImStrdup(lua_tostring(L, -1));
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
                        dock.status = (Status_)lua_tointeger(L, -1);
                    }
                    lua_pop(L, 6);

                    if (lua_getfield(L, -1, "prev") == LUA_TNUMBER)
                    {
                        dock.prev_tab = getDockByIndex(lua_tointeger(L, -1));
                    }
                    if (lua_getfield(L, -2, "next") == LUA_TNUMBER)
                    {
                        dock.next_tab = getDockByIndex(lua_tointeger(L, -1));
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


void ShutdownDock()
{
    for (int i = 0; i < g_dock.m_docks.size(); ++i)
    {
        g_dock.m_docks[i]->~Dock();
        MemFree(g_dock.m_docks[i]);
    }
}


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