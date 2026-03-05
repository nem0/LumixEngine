#pragma once

#include "core/array.h"
#include "core/color.h"
#include "core/math.h"
#include "core/string.h"
#include "gui_ng/ui.h"
#include "renderer/draw2d.h"
#include <imgui/imgui.h>

namespace Lumix::ui {

static bool g_show_element_outlines = false;
static bool g_show_margins = false;
static bool g_show_padding = false;
static bool g_show_all_visualizations = false; // If true, show for all elements; if false, only for hovered

static void debugElementGUI(const Document& document, u32 element_idx, int depth, u32& hovered_element_idx) {
	const Element* element = document.getElement(element_idx);
	if (!element) return;

	const char* tag_names[] = { "box", "image", "span", "invalid" };
	const char* tag_name = element->tag < Tag::INVALID ? tag_names[(u8)element->tag] : "unknown";

	StaticString<512> label;
	StaticString<64> size_str;
	size_str.append(" (", element->size.x, "x", element->size.y, ")");

	if (!element->classes.empty()) {
		size_str.append(" .");
		bool first = true;
		for (InternString cid : element->classes) {
			StringView class_name = document.m_intern_table.resolve(cid);
			if (!first) size_str.append(" ");
			size_str.append(class_name);
			first = false;
		}
	}

	if (!element->attributes.empty()) {
		size_str.append(" [", element->attributes.size(), " attrs]");
	}

	if (element->value.empty()) {
		label.append(tag_name, size_str, "##", element_idx);
	} else {
		StaticString<128> value_preview;
		u32 preview_len = minimum((u32)100, element->value.size());
		value_preview.append(StringView(element->value.begin, preview_len));
		if (element->value.size() > 100) {
			value_preview.append("...");
		}
		label.append(tag_name, size_str, " \"", value_preview, "\"##", element_idx);
	}

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
	if (element->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
	if (element_idx == hovered_element_idx) flags |= ImGuiTreeNodeFlags_Selected;

	ImVec4 type_color;
	switch (element->tag) {
		case Tag::BOX: type_color = ImVec4(0.8f, 0.6f, 0.4f, 1.0f); break;
		case Tag::IMAGE: type_color = ImVec4(0.4f, 0.8f, 0.6f, 1.0f); break;
		case Tag::SPAN: type_color = ImVec4(0.6f, 0.4f, 0.8f, 1.0f); break;
		default: type_color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); break;
	}

	ImGui::PushStyleColor(ImGuiCol_Text, type_color);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f, 0.6f, 1.0f, 0.8f));
	bool open = ImGui::TreeNodeEx(label, flags);
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	bool is_hovered = ImGui::IsItemHovered();
	if (is_hovered) {
		hovered_element_idx = element_idx;
		ImGui::BeginTooltip();
		ImGui::Text("Position: (%.1f, %.1f)", element->position.x, element->position.y);
		ImGui::Text("Size: (%.1f, %.1f)", element->size.x, element->size.y);
		ImGui::Text("Children: %d", element->children.size());

		if (!element->classes.empty()) {
			StaticString<256> class_str;
			bool first = true;
			for (InternString cid : element->classes) {
				StringView class_name = document.m_intern_table.resolve(cid);
				if (!first) class_str.append(" ");
				class_str.append(class_name);
				first = false;
			}
			ImGui::Text("Class: %s", class_str.data);
		}

		if (!element->attributes.empty()) {
			ImGui::Separator();
			ImGui::Text("Attributes:");
			for (const auto& attr : element->attributes) {
				const char* attr_names[] = { "id", "class", "visible", "font-size", "font", "color", "width", "height", "margin", "padding", "background-image", "background-fit", "bg-color", "direction", "wrap", "justify-content", "align-items", "value", "align", "src", "fit", "grow", "placeholder" };
				const char* attr_name = attr.type < AttributeName::PLACEHOLDER ? attr_names[(u8)attr.type] : "unknown";
				ImGui::Text("  %s: \"%.*s\"", attr_name, (int)attr.value.size(), attr.value.begin);
			}
		}

		ImGui::EndTooltip();
	}

	if (open) {
		for (u32 child_idx : element->children) {
			debugElementGUI(document, child_idx, depth + 1, hovered_element_idx);
		}
		ImGui::TreePop();
	}
}

void debugViewGUI(Document& document, u32& hovered_element_idx) {
    ImGui::Text("Parse: %.2fms, Layout: %.2fms, Render: %.2fms",
        document.m_parse_duration * 1000.0f,
        document.m_layout_duration * 1000.0f,
        document.m_render_duration * 1000.0f);

    ImGui::Text("Visualization Options:");
    ImGui::Checkbox("Show Element Outlines", &g_show_element_outlines);
    ImGui::Checkbox("Show Margins", &g_show_margins);
    ImGui::Checkbox("Show Padding", &g_show_padding);
    ImGui::Checkbox("Show All Elements", &g_show_all_visualizations);

    for (u32 root_idx : document.m_roots) {
        debugElementGUI(document, root_idx, 0, hovered_element_idx);
    }
}

void drawDebugVisualizations(Draw2D& draw2d, const Document& document, u32 hovered_element_idx) {
	if (hovered_element_idx != 0xFFFFFFF) {
		const Element* hovered_element = document.getElement(hovered_element_idx);
		if (hovered_element) {
			Color highlight_fill(255, 128, 0, 76);
			Color highlight_border(255, 255, 0, 255);
			if (hovered_element->tag == Tag::SPAN && !hovered_element->lines.empty() && hovered_element->font_handle && document.m_font_manager) {
				float asc = document.m_font_manager->getAscender(hovered_element->font_handle);
				float height = document.m_font_manager->getHeight(hovered_element->font_handle);
				for (const SpanLine& line : hovered_element->lines) {
					Vec2 line_pos = line.pos;
					line_pos.y -= asc;
					Vec2 line_end = line_pos + Vec2(line.width, height);
					draw2d.addRectFilled(line_pos, line_end, highlight_fill);
					draw2d.addRect(line_pos, line_end, highlight_border, 2.0f);
				}
			} else {
				Vec2 pos = hovered_element->position;
				Vec2 size = hovered_element->size;
				if (hovered_element->tag == Tag::SPAN && hovered_element->font_handle && document.m_font_manager) {
					float asc = document.m_font_manager->getAscender(hovered_element->font_handle);
					pos.y -= asc;
				}
				draw2d.addRectFilled(pos, pos + size, highlight_fill);
				draw2d.addRect(pos, pos + size, highlight_border, 2.0f);
			}
		}
	}

	if (g_show_element_outlines || g_show_margins || g_show_padding) {
		auto drawElementVisualization = [&](const Element* element, bool is_hovered) {
			if (!element) return;

			Vec2 pos = element->position;
			Vec2 size = element->size;

			// For spans, position.y is the baseline, so adjust to top-left
			if (element->tag == Tag::SPAN && element->font_handle && document.m_font_manager) {
				float asc = document.m_font_manager->getAscender(element->font_handle);
				pos.y -= asc;
			}

			Vec2 end = pos + size;

			// Element outline
			if (g_show_element_outlines) {
				Color outline_color = is_hovered ? Color(255, 0, 0, 255) : Color(0, 255, 0, 128); // Yellow for hovered, cyan for others
				if (element->tag == Tag::SPAN && !element->lines.empty() && element->font_handle && document.m_font_manager) {
					float asc = document.m_font_manager->getAscender(element->font_handle);
					float height = document.m_font_manager->getHeight(element->font_handle);
					for (const SpanLine& line : element->lines) {
						Vec2 line_pos = line.pos;
						line_pos.y -= asc;
						Vec2 line_end = line_pos + Vec2(line.width, height);
						draw2d.addRect(line_pos, line_end, outline_color, 1.0f);
					}
				} else {
					draw2d.addRect(pos, end, outline_color, 1.0f);
				}
			}

			// Margins
			if (g_show_margins) {
				Color margin_color(255, 0, 255, 255); // Magenta with low alpha
				Vec2 margin_start = pos - Vec2(element->margins.left, element->margins.top);
				Vec2 margin_end = end + Vec2(element->margins.right, element->margins.bottom);
				draw2d.addRect(margin_start, margin_end, margin_color, 1.0f);
			}

			// Padding
			if (g_show_padding) {
				Color padding_color(0, 255, 0, 255); // Green with low alpha
				Vec2 padding_start = pos + Vec2(element->paddings.left, element->paddings.top);
				Vec2 padding_end = end - Vec2(element->paddings.right, element->paddings.bottom);
				if (padding_start.x < padding_end.x && padding_start.y < padding_end.y) {
					draw2d.addRect(padding_start, padding_end, padding_color, 1.0f);
				}
			}
		};

		if (g_show_all_visualizations) {
			for (const Element& element : document.m_elements) {
				bool is_hovered = (&element - &document.m_elements[0]) == hovered_element_idx;
				drawElementVisualization(&element, is_hovered);
			}
		} else if (hovered_element_idx != 0xFFFFFFF) {
			const Element* hovered_element = document.getElement(hovered_element_idx);
			drawElementVisualization(hovered_element, true);
		}
	}
}

} // namespace Lumix::ui

