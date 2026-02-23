#include "core/log.h"
#include "core/profiler.h"
#include "core/string.h"
#include "renderer/draw2d.h"
#include "renderer/font.h"
#include "ui.h"
#include "ui_tokenizer.h"

namespace Lumix::ui {

struct ParsedUnit {
	float value;
	Unit unit;
};

static const char* tokenTypeToString(UITokenizer::Token::Type type) {
	switch (type) {
		case UITokenizer::Token::UNDEFINED: return "UNDEFINED";
		case UITokenizer::Token::EOF: return "EOF";
		case UITokenizer::Token::ERROR: return "ERROR";
		case UITokenizer::Token::IDENTIFIER: return "IDENTIFIER";
		case UITokenizer::Token::STRING: return "STRING";
		case UITokenizer::Token::NUMBER: return "NUMBER";
		case UITokenizer::Token::PERCENTAGE: return "PERCENTAGE";
		case UITokenizer::Token::EM: return "EM";
		case UITokenizer::Token::EQUALS: return "EQUALS";
		case UITokenizer::Token::LBRACE: return "LBRACE";
		case UITokenizer::Token::RBRACE: return "RBRACE";
		case UITokenizer::Token::COLON: return "COLON";
		case UITokenizer::Token::SEMICOLON: return "SEMICOLON";
		case UITokenizer::Token::DOT: return "DOT";
		case UITokenizer::Token::COLOR: return "COLOR";
		case UITokenizer::Token::DOLLAR: return "DOLLAR";
		case UITokenizer::Token::LBRACKET: return "LBRACKET";
		case UITokenizer::Token::RBRACKET: return "RBRACKET";
	}
	return "UNKNOWN";
}

static Tag parseTag(StringView str) {
	const u32 len = (u32)str.size();
	if (len == 0) return Tag::INVALID;
	const char* s = str.begin;

	switch (s[0]) {
		case 'c':
			if (len == 6 && memcmp(s, "canvas", 6) == 0) return Tag::CANVAS;
			break;
		case 'i':
			if (len == 5) {
				if (s[1] == 'n' && memcmp(s, "input", 5) == 0) return Tag::INPUT;
				if (s[1] == 'm' && memcmp(s, "image", 5) == 0) return Tag::IMAGE;
			}
			break;
		case 'p':
			if (len == 5 && memcmp(s, "panel", 5) == 0) return Tag::PANEL;
			break;
		case 's':
			if (len == 4 && memcmp(s, "span", 4) == 0) return Tag::SPAN;
			break;
	}
	return Tag::INVALID;
}

static AttributeName parseAttributeName(StringView str) {
	const u32 len = (u32)str.size();
	if (len == 0) return AttributeName::INVALID;
	const char* s = str.begin;

	switch (s[0]) {
		case 'a':
			if (len == 5 && memcmp(s, "align", 5) == 0) return AttributeName::ALIGN;
			if (len == 11 && memcmp(s, "align-items", 11) == 0) return AttributeName::ALIGN_ITEMS;
			break;
		case 'b':
			if (len == 8 && memcmp(s, "bg-color", 8) == 0) return AttributeName::BG_COLOR;
			if (len == 14 && memcmp(s, "background-fit", 14) == 0) return AttributeName::BACKGROUND_FIT;
			if (len == 16 && memcmp(s, "background-image", 16) == 0) return AttributeName::BACKGROUND_IMAGE;
			break;
		case 'c':
			if (len == 5 && memcmp(s, "class", 5) == 0) return AttributeName::CLASS;
			if (len == 5 && memcmp(s, "color", 5) == 0) return AttributeName::COLOR;
			break;
		case 'd':
			if (len == 9 && memcmp(s, "direction", 9) == 0) return AttributeName::DIRECTION;
			break;
		case 'f':
			if (len == 3 && memcmp(s, "fit", 3) == 0) return AttributeName::FIT;
			if (len == 4 && memcmp(s, "font", 4) == 0) return AttributeName::FONT;
			if (len == 9 && memcmp(s, "font-size", 9) == 0) return AttributeName::FONT_SIZE;
			break;
		case 'h':
			if (len == 6 && memcmp(s, "height", 6) == 0) return AttributeName::HEIGHT;
			break;
		case 'i':
			if (len == 2 && memcmp(s, "id", 2) == 0) return AttributeName::ID;
			break;
		case 'j':
			if (len == 15 && memcmp(s, "justify-content", 15) == 0) return AttributeName::JUSTIFY_CONTENT;
			break;
		case 'm':
			if (len == 6 && memcmp(s, "margin", 6) == 0) return AttributeName::MARGIN;
			break;
		case 'p':
			if (len == 7 && memcmp(s, "padding", 7) == 0) return AttributeName::PADDING;
			if (len == 11 && memcmp(s, "placeholder", 11) == 0) return AttributeName::PLACEHOLDER;
			break;
		case 's':
			if (len == 3 && memcmp(s, "src", 3) == 0) return AttributeName::SRC;
			break;
		case 'v':
			if (len == 5 && memcmp(s, "value", 5) == 0) return AttributeName::VALUE;
			if (len == 7 && memcmp(s, "visible", 7) == 0) return AttributeName::VISIBLE;
			break;
		case 'w':
			if (len == 5 && memcmp(s, "width", 5) == 0) return AttributeName::WIDTH;
			if (len == 4 && memcmp(s, "wrap", 4) == 0) return AttributeName::WRAP;
			break;
	}

	logError("Unknown UI attribute: ", str);
	return AttributeName::INVALID;
}

static JustifyContent parseJustifyContent(StringView value) {
    const u32 len = (u32)value.size();
    if (len == 0) return JustifyContent::START;
    const char* s = value.begin;
    switch (value[0]) {
        case 's':
            if (len == 5 && memcmp(s, "start", 5) == 0) return JustifyContent::START;
            if (len == 13 && memcmp(s, "space-between", 13) == 0) return JustifyContent::SPACE_BETWEEN;
            if (len == 12 && memcmp(s, "space-around", 12) == 0) return JustifyContent::SPACE_AROUND;
            break;
        case 'c':
            if (len == 6 && memcmp(s, "center", 6) == 0) return JustifyContent::CENTER;
            break;
        case 'e':
            if (len == 3 && memcmp(s, "end", 3) == 0) return JustifyContent::END;
            break;
    }
    return JustifyContent::START;
}

static AlignItems parseAlignItems(StringView value) {
    const u32 len = (u32)value.size();
    const char* s = value.begin;
    switch (len) {
        case 0: return AlignItems::START;
		case 3: if (memcmp(s, "end", 3) == 0) return AlignItems::END; break;
        case 5: if (memcmp(s, "start", 5) == 0) return AlignItems::START; break;
        case 6: if (memcmp(s, "center", 6) == 0) return AlignItems::CENTER; break;
        case 7: if (memcmp(s, "stretch", 7) == 0) return AlignItems::STRETCH; break;
    }
    return AlignItems::START;
}

static Align parseAlign(StringView value) {
    const u32 len = (u32)value.size();
    const char* s = value.begin;
    switch (len) {
		case 0: return Align::LEFT;
        case 4: if (memcmp(s, "left", 4) == 0) return Align::LEFT; break;
        case 5: if (memcmp(s, "right", 5) == 0) return Align::RIGHT; break;
        case 6: if (memcmp(s, "center", 6) == 0) return Align::CENTER; break;
    }
    return Align::LEFT;
}

static Color parseColor(StringView str) {
	if (str.size() != 7 || str[0] != '#') return Color::BLACK;
	auto hexToInt = [](const char* s, int len) -> int {
		int res = 0;
		for (int i = 0; i < len; ++i) {
			char c = s[i];
			int d = 0;
			if (c >= '0' && c <= '9') d = c - '0';
			else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
			else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
			else return -1;
			res = res * 16 + d;
		}
		return res;
	};
	int r = hexToInt(str.begin + 1, 2);
	int g = hexToInt(str.begin + 3, 2);
	int b = hexToInt(str.begin + 5, 2);
	if (r < 0 || g < 0 || b < 0) return Color::BLACK;
	return Color(u8(r), u8(g), u8(b), 255);
}

bool Document::tryConsume(Token::Type type, Token* out_token) {
	if (m_tokenizer.m_current_token.type != type) return false;
	if (out_token) *out_token = m_tokenizer.m_current_token;
	m_tokenizer.m_current_token = m_tokenizer.nextToken();
	return true;
}

bool Document::consume(Token::Type type, Token* out_token) {
	Token t;
	if (!tryConsume(type, &t)) {
		error(t.value, m_tokenizer, "expected ", tokenTypeToString(type), ", got ", tokenTypeToString(t.type));
		return false;
	}
	if (out_token) *out_token = t;
	return true;
}

bool Document::parseStyleBlock() {
	if (!consume(Token::LBRACE)) return false;
	
	// Parse style rules inside the block
	for (;;) {
		Token token = m_tokenizer.consumeToken();
		// TODO test for unclosed style block
		if (token.type == Token::RBRACE) return true;
		
		StringView selector;
		const char* selector_start = token.value.begin;
		// Parse selector: supports .class, $id, or element.class combinations
		if (token.type == Token::DOT) {
			// Class selector: .classname
			Token id_token;
			if (!consume(Token::IDENTIFIER, &id_token)) return false;
			selector = StringView(selector_start, (u32)(id_token.value.end - selector_start));
		} else if (token.type == Token::DOLLAR) {
			// ID selector: $idname
			Token id_token;
			if (!consume(Token::IDENTIFIER, &id_token)) return false;
			selector = StringView(selector_start, (u32)(id_token.value.end - selector_start));
		} else if (token.type == Token::IDENTIFIER) {
			// Element selector, possibly with class/id modifiers
			selector = token.value;
			for (;;) {
				Token next = m_tokenizer.peekToken();
				if (next.type == Token::DOT) {
					// Append class modifier
					if (!consume(Token::DOT)) return false;
					Token id_token;
					if (!consume(Token::IDENTIFIER, &id_token)) return false;
					selector = StringView(selector_start, (u32)(id_token.value.end - selector_start));
				} else if (next.type == Token::DOLLAR) {
					// Append ID modifier
					if (!consume(Token::DOLLAR)) return false;
					Token id_token;
					if (!consume(Token::IDENTIFIER, &id_token)) return false;
					selector = StringView(selector_start, (u32)(id_token.value.end - selector_start));
				} else {
					break;
				}
			}
		} else {
			error(token.value, m_tokenizer, "expected selector (. $ or identifier), got ", tokenTypeToString(token.type));
			return false;
		}

		if (!consume(Token::LBRACE)) return false;

		// parse style attributes
		Array<Attribute> attrs(m_allocator);
		for (;;) {
			token = m_tokenizer.consumeToken();
			// TODO test for unclosed rule
			if (token.type == Token::RBRACE) break;
			
			if (token.type != Token::IDENTIFIER) {
				error(token.value, m_tokenizer, "expected style property name or '}', got ", tokenTypeToString(token.type));
				return false;
			}

			StringView name = token.value;
			if (!consume(Token::COLON)) return false;

			token = m_tokenizer.consumeToken();
			if (token.type != Token::IDENTIFIER && token.type != Token::STRING && token.type != Token::NUMBER && token.type != Token::PERCENTAGE && token.type != Token::COLOR && token.type != Token::EM) {
				error(token.value, m_tokenizer, "expected style property value, got ", tokenTypeToString(token.type));
				return false;
			}
			
			Attribute& attr = attrs.emplace();
			attr.value = token.value;
			attr.type = parseAttributeName(name);
			if (!consume(Token::SEMICOLON)) return false;
		}

		m_stylesheet.m_rules.push(StyleRule(selector, static_cast<Array<Attribute>&&>(attrs)));
	}
}

bool Document::parseElements(u32 parent_index) {
	for (;;) {
		Token token = m_tokenizer.consumeToken();

		switch (token.type) {
			case Token::EOF: 
				if (parent_index != 0xFFFF'FFFF) {
					error(token.value, m_tokenizer, "unexpected EOF, unclosed block");
					return false;
				}
				return true;
			case Token::RBRACKET: error(token.value, m_tokenizer, "unexpected ']'"); return false;
			case Token::RBRACE:
				if (parent_index == 0xFFFF'FFFF) {
					error(token.value, m_tokenizer, "unexpected '}' at root level");
					return false;
				}
				return true; // end of block
			case Token::LBRACKET: {
				if (!consume(Token::IDENTIFIER, &token)) return false;
				Tag tag = parseTag(token.value);
				if (tag == Tag::INVALID) {
					if (token.value == "style") {
						if (!parseStyleBlock()) return false;
						continue;
					}
					error(token.value, m_tokenizer, "unknown tag '", token.value, "'");
					return false;
				}

				Element& elem = m_elements.emplace(tag, m_allocator);
				u32 elem_idx = m_elements.size() - 1;
				if (parent_index != 0xFFFF'FFFF) {
					m_elements[parent_index].children.push(elem_idx);
				} else {
					m_roots.push(elem_idx);
				}

				// Parse element attributes: key="value" pairs enclosed in []
				for (;;) {
					if (tryConsume(Token::RBRACKET)) break;
					Token name_token;
					if (!consume(Token::IDENTIFIER, &name_token)) return false;
					if (!consume(Token::EQUALS)) return false;
			
					Token value = m_tokenizer.consumeToken();
					if (value.type != Token::STRING && value.type != Token::IDENTIFIER && value.type != Token::NUMBER && value.type != Token::PERCENTAGE && value.type != Token::EM && value.type != Token::COLOR) {
						error(value.value, m_tokenizer, "expected attribute value, got ", tokenTypeToString(value.type));
						return false;
					}

					AttributeName name = parseAttributeName(name_token.value);
					switch (name) {
						case AttributeName::VALUE: elem.value = value.value; break;
						case AttributeName::CLASS: elem.style_class = value.value; break;
						default: {
							Attribute& attr = elem.attributes.emplace();
							attr.type = name;
							attr.value = value.value;
							break;
						}
					}
					token = m_tokenizer.peekToken();
				}

				if (tryConsume(Token::LBRACE)) {
					// parse children
					if (!parseElements(elem_idx)) {
						logError(m_tokenizer.m_filename, "(", m_tokenizer.getLine(), "): failed to parse element children");
						return false;
					}
				}
				break;
			}
			default: {
				if (token.type == Token::IDENTIFIER && token.value == "style" && parent_index == 0xffff'FFFF) {
					if (!parseStyleBlock()) return false;
					break;
				}
				// treat as text
				Element& elem = m_elements.emplace(Tag::SPAN, m_allocator);
				u32 elem_idx = m_elements.size() - 1;
				if (parent_index != 0xFFFF'FFFF) {
					m_elements[parent_index].children.push(elem_idx);
				} else {
					m_roots.push(elem_idx);
				}
				elem.value = token.value;
				bool is_break = false;
				while (!is_break) {
					Token next = m_tokenizer.peekToken();
					switch (next.type) {
						case Token::ERROR: return false;
						case Token::UNDEFINED: return false;
						case Token::EOF: 
							if (parent_index == 0xffff'FFFF) return true;

							error(token.value, m_tokenizer, "unexpected EOF");
							return false;
						case Token::LBRACKET: is_break = true; break; // start of an element
						case Token::RBRACE: 
							if (parent_index == 0xffff'FFFF) {
								error(token.value, m_tokenizer, "unexpected right brace");
								return false;
							}
							m_tokenizer.consumeToken();
							return true; // end of the parent container
						default: 
							elem.value.end = next.value.end;
							m_tokenizer.consumeToken();
							break;
					}
				}
				break;
			}
		}
	}
}

static ParsedUnit parseUnit(StringView str) {
	float value = 0.0f;
	Unit unit = Unit::PIXELS;
	if (!str.empty()) {
		if (str == "fit-content") {
			unit = Unit::FIT_CONTENT;
		} else {
			fromCString(str, value);
			if (str.size() >= 1 && str.back() == '%') {
				unit = Unit::PERCENT;
			} else if (str.size() >= 2 && *(str.end - 2) == 'e' && str.back() == 'm') {
				unit = Unit::EM;
			} else {
				unit = Unit::PIXELS;
			}
		}
	}
	return {value, unit};
}

static float computeAbsoluteSize(const ParsedUnit& unit, float parent_size, float font_size) {
	switch (unit.unit) {
		case Unit::PIXELS: return unit.value;
		case Unit::PERCENT: return unit.value / 100.0f * parent_size;
		case Unit::EM: return unit.value * font_size;
		case Unit::FIT_CONTENT: return 0.0f; // TODO
		default: return unit.value;
	}
}

// Positions child elements within their parent container based on layout direction and margins.
// For row direction, children are laid out horizontally; for column, vertically.
// Margins between adjacent children collapse to the maximum value.
static void layoutChildren(Document& doc, Element& parent) {
	if (parent.children.empty()) return;

	const bool is_row = parent.direction == Direction::ROW;
	const bool wrap = parent.wrap;

	// Lambda functions to abstract main/cross axes for easier direction-agnostic code
	auto get_main_pos = [is_row](Element& e) -> float& { return is_row ? e.position.x : e.position.y; };
	auto get_main_size = [is_row](Element& e) -> float& { return is_row ? e.size.x : e.size.y; };
	auto get_cross_pos = [is_row](Element& e) -> float& { return is_row ? e.position.y : e.position.x; };
	auto get_cross_size = [is_row](Element& e) -> float& { return is_row ? e.size.y : e.size.x; };
	auto get_margin_start = [is_row](Element& e) { return is_row ? e.margins[3] : e.margins[0]; };
	auto get_margin_end = [is_row](Element& e) { return is_row ? e.margins[1] : e.margins[2]; };
	auto get_margin_cross_start = [is_row](Element& e) { return is_row ? e.margins[0] : e.margins[3]; };
	auto get_margin_cross_end = [is_row](Element& e) { return is_row ? e.margins[2] : e.margins[1]; };

	// Calculate container boundaries accounting for padding
	float main_start = is_row ? parent.position.x + parent.paddings[3] : parent.position.y + parent.paddings[0];
	float cross_start = is_row ? parent.position.y + parent.paddings[0] : parent.position.x + parent.paddings[3];
	float parent_main_size = is_row ? parent.size.x - parent.paddings[3] - parent.paddings[1] : parent.size.y - parent.paddings[0] - parent.paddings[2];
	float parent_main_end = main_start + parent_main_size;

	// Helper to process a line
	auto processLine = [&](u32 start, u32 end, float line_cross_pos, float line_max_cross) {
		// Check if this is an inline line (all elements are spans)
		bool is_inline_line = doc.m_elements[parent.children[start]].tag == Tag::SPAN;

		if (is_inline_line) {
			// Baseline alignment for inline elements
			float dominant_baseline = 0.0f;
			for (u32 i = start; i <= end; ++i) {
				Element& child = doc.m_elements[parent.children[i]];
				if (child.font_handle && doc.m_font_manager) {
					float ascender = doc.m_font_manager->getAscender(child.font_handle);
					dominant_baseline = maximum(dominant_baseline, ascender);
				}
			}
			for (u32 i = start; i <= end; ++i) {
				u32 child_idx = parent.children[i];
				Element& child = doc.m_elements[child_idx];
				float margin_cross_start = get_margin_cross_start(child);
				float ascender = child.font_handle && doc.m_font_manager ? doc.m_font_manager->getAscender(child.font_handle) : 0.0f;
				float cross_pos = line_cross_pos + margin_cross_start + (dominant_baseline - ascender);
				get_cross_pos(child) = cross_pos;
			}
		} else {
			// Apply cross-axis alignment (align-items)
			float available_cross = wrap ? line_max_cross : (is_row ? parent.size.y - parent.paddings[0] - parent.paddings[2] : parent.size.x - parent.paddings[3] - parent.paddings[1]);
			for (u32 i = start; i <= end; ++i) {
				u32 child_idx = parent.children[i];
				Element& child = doc.m_elements[child_idx];
				float child_cross_size = get_cross_size(child);
				float margin_cross_start = get_margin_cross_start(child);
				float margin_cross_end = get_margin_cross_end(child);
				float cross_pos;
				if (parent.align_items == AlignItems::START) {
					cross_pos = line_cross_pos + margin_cross_start;
				} else if (parent.align_items == AlignItems::CENTER) {
					cross_pos = line_cross_pos + margin_cross_start + (available_cross - child_cross_size - margin_cross_start - margin_cross_end) * 0.5f;
				} else if (parent.align_items == AlignItems::END) {
					cross_pos = line_cross_pos + available_cross - child_cross_size - margin_cross_end;
				} else if (parent.align_items == AlignItems::STRETCH) {
					cross_pos = line_cross_pos + margin_cross_start;
					if (is_row && child.fit_content_height) {
						child.size.y = maximum(0.0f, available_cross - margin_cross_start - margin_cross_end);
					} else if (!is_row && child.fit_content_width) {
						child.size.x = maximum(0.0f, available_cross - margin_cross_start - margin_cross_end);
					}
				}
				get_cross_pos(child) = cross_pos;
			}
		}

		// Apply main-axis justification (justify-content)
		if (parent.justify_content != JustifyContent::START) {
			float min_main = FLT_MAX, max_main = -FLT_MAX;
			for (u32 i = start; i <= end; ++i) {
				Element& child = doc.m_elements[parent.children[i]];
				float pos = get_main_pos(child), size = get_main_size(child);
				min_main = minimum(min_main, pos);
				max_main = maximum(max_main, pos + size);
			}
			float total_span = max_main - min_main;
			float available_main = parent_main_end - main_start;
			float shift = 0.0f;
			switch (parent.justify_content) {
				case JustifyContent::CENTER:
					shift = (available_main - total_span) * 0.5f - (min_main - main_start);
					break;
				case JustifyContent::END:
					shift = available_main - total_span - (min_main - main_start);
					break;
				case JustifyContent::SPACE_BETWEEN:
					if (end > start) {
						float space = (available_main - total_span) / (end - start);
						float current_pos = main_start;
						for (u32 i = start; i <= end; ++i) {
							Element& child = doc.m_elements[parent.children[i]];
							float size = get_main_size(child);
							get_main_pos(child) = current_pos;
							current_pos += size + space;
						}
					}
					break;
				case JustifyContent::SPACE_AROUND:
					if (end >= start) {
						float space = (available_main - total_span) / (end - start + 1);
						float current_pos = main_start + space * 0.5f;
						for (u32 i = start; i <= end; ++i) {
							Element& child = doc.m_elements[parent.children[i]];
							float size = get_main_size(child);
							get_main_pos(child) = current_pos;
							current_pos += size + space;
						}
					}
					break;
				default:
					break;
			}
			if (shift != 0.0f) {
				for (u32 i = start; i <= end; ++i) {
					get_main_pos(doc.m_elements[parent.children[i]]) += shift;
				}
			}
		}

		// Apply text alignment for inline lines
		if (is_inline_line && parent.text_align != Align::LEFT) {
			float min_x = FLT_MAX, max_x = -FLT_MAX;
			for (u32 i = start; i <= end; ++i) {
				Element& child = doc.m_elements[parent.children[i]];
				float pos = child.position.x, size = child.size.x;
				min_x = minimum(min_x, pos);
				max_x = maximum(max_x, pos + size);
			}
			float total_span = max_x - min_x;
			float available = parent.size.x - parent.paddings[3] - parent.paddings[1];
			float shift = 0.0f;
			switch (parent.text_align) {
				case Align::CENTER:
					shift = (available - total_span) * 0.5f - (min_x - (parent.position.x + parent.paddings[3]));
					break;
				case Align::RIGHT:
					shift = available - total_span - (min_x - (parent.position.x + parent.paddings[3]));
					break;
				default:
					break;
			}
			if (shift != 0.0f) {
				for (u32 i = start; i <= end; ++i) {
					doc.m_elements[parent.children[i]].position.x += shift;
				}
			}
		}

	};

	float current_main_pos = main_start;
	float current_cross_pos = cross_start;
	u32 current_line_start = 0;
	float prev_margin_end = 0;
	bool is_first_in_line = true;
	float current_line_max_cross = 0;

	for (u32 i = 0, n = (u32)parent.children.size(); i < n; ++i) {
		u32 child_idx = parent.children[i];
		Element& child = doc.m_elements[child_idx];
		// TODO do we need this? how can it happen?
		if (get_main_pos(child) != 0 || get_cross_pos(child) != 0) continue; // Skip already positioned elements

		float margin_start = get_margin_start(child);
		float margin_end = get_margin_end(child);
		float child_main_size = get_main_size(child);
		float effective_margin = is_first_in_line ? margin_start : maximum(prev_margin_end, margin_start);

		// Check if fits in current line
		if (!is_first_in_line) {
			Tag prev_tag = doc.m_elements[parent.children[i - 1]].tag;
			if (wrap && current_main_pos + effective_margin + child_main_size > parent_main_end
				|| child.tag == Tag::PANEL && prev_tag == Tag::SPAN // panel causes line break before itself
				|| child.tag == Tag::SPAN && prev_tag == Tag::PANEL // panel causes line break after itself
			) {
				// Process current line
				processLine(current_line_start, i - 1, current_cross_pos, current_line_max_cross);
				// Start new line
				current_line_start = i;
				current_main_pos = main_start;
				current_cross_pos += current_line_max_cross;
				current_line_max_cross = 0;
				effective_margin = 0;
				is_first_in_line = true;
			}
		}

		// Set main position
		get_main_pos(child) = current_main_pos + effective_margin;

		// Update line max cross size
		float child_cross_size = get_cross_size(child) + get_margin_cross_start(child) + get_margin_cross_end(child);
		if (child_cross_size > current_line_max_cross) current_line_max_cross = child_cross_size;

		current_main_pos += effective_margin + child_main_size;
		prev_margin_end = margin_end;
		is_first_in_line = false;
	}

	// Process last line
	if (current_line_start < (u32)parent.children.size()) {
		processLine(current_line_start, (u32)parent.children.size() - 1, current_cross_pos, current_line_max_cross);
	}

	// Recursively layout children's children
	for (u32 child_idx : parent.children) {
		layoutChildren(doc, doc.m_elements[child_idx]);
	}
}

struct ParentContext {
	// inheritable
	float font_size = 12;
	StringView font;
	Color color = Color::BLACK;
	Align align = Align::LEFT;
	// not inheritable
	bool wrap = false;
	Vec2 size;
};

static void applyAttributes(Document& doc, Element& elem, const ParentContext& parent) {
	elem.position = Vec2(0, 0);
	elem.size = Vec2(0, 0);
	elem.fit_content_width = true;
	elem.fit_content_height = true;
	elem.wrap = false;
	elem.justify_content = JustifyContent::START;
	elem.align_items = AlignItems::START;
	elem.text_align = parent.align;
	elem.font_size = parent.font_size;
	elem.color = parent.color;
	elem.bg_color = Color(0);
	for (int i = 0; i < 4; ++i) {
		elem.margins[i] = 0;
		elem.paddings[i] = 0;
	}

	ParentContext ctx = parent;
	ctx.size = Vec2(0);

	ParsedUnit width_unit = {0, Unit::PIXELS};
	ParsedUnit height_unit = {0, Unit::PIXELS};
	ParsedUnit margin_unit = {0, Unit::PIXELS};
	ParsedUnit padding_unit = {0, Unit::PIXELS};

	for (const Attribute& attr : elem.attributes) {
		switch (attr.type) {
			case AttributeName::WIDTH: {
				width_unit = parseUnit(attr.value);
				if (width_unit.unit == Unit::FIT_CONTENT) {
					elem.fit_content_width = true;
					elem.size.x = 0;
				} else {
					elem.fit_content_width = false;
				}
				break;
			}
			case AttributeName::HEIGHT: {
				height_unit = parseUnit(attr.value);
				if (height_unit.unit == Unit::FIT_CONTENT) {
					elem.fit_content_height = true;
					elem.size.y = 0;
				} else {
					elem.fit_content_height = false;
				}
				break;
			}
			case AttributeName::DIRECTION: {
				if (attr.value == "row") elem.direction = Direction::ROW;
				else elem.direction = Direction::COLUMN;
				break;
			}
			case AttributeName::ALIGN: {
				elem.text_align = parseAlign(attr.value);
				ctx.align = elem.text_align;
				break;
			}
			case AttributeName::FONT_SIZE: {
				fromCString(attr.value, elem.font_size);
				ctx.font_size = elem.font_size;
				break;
			}
			case AttributeName::COLOR: {
				elem.color = parseColor(attr.value);
				ctx.color = elem.color;
				break;
			}
			case AttributeName::FONT: ctx.font = attr.value; break;
			case AttributeName::BG_COLOR: elem.bg_color = parseColor(attr.value); break;
			case AttributeName::VALUE: elem.value = attr.value; break;
			case AttributeName::JUSTIFY_CONTENT: elem.justify_content = parseJustifyContent(attr.value); break;
			case AttributeName::ALIGN_ITEMS: elem.align_items = parseAlignItems(attr.value); break;
			case AttributeName::WRAP: elem.wrap = attr.value == "true"; break;
			case AttributeName::PADDING: padding_unit = parseUnit(attr.value); break;
			case AttributeName::MARGIN: margin_unit = parseUnit(attr.value); break;
			default: break;
		}
	}

	// Load font if both font and font_size are set
	if (doc.m_font_manager && !ctx.font.empty() && elem.font_size > 0) {
		elem.font_handle = doc.m_font_manager->loadFont(ctx.font, (i32)elem.font_size);
	}

	if (!elem.fit_content_width) {
		elem.size.x = computeAbsoluteSize(width_unit, parent.size.x, elem.font_size);
	}
	if (!elem.fit_content_height) {
		elem.size.y = computeAbsoluteSize(height_unit, parent.size.y, elem.font_size);
	}
	for (int i = 0; i < 4; ++i) {
		float ref_size = (i % 2 == 0) ? parent.size.y : parent.size.x;
		elem.margins[i] = computeAbsoluteSize(margin_unit, ref_size, elem.font_size);
		elem.paddings[i] = computeAbsoluteSize(padding_unit, ref_size, elem.font_size);
	}

	ctx.size = elem.size;
	ctx.wrap = elem.wrap;
	for (u32 child_idx : elem.children) {
		applyAttributes(doc, doc.m_elements[child_idx], ctx);
	}

	// Compute text sizes, handling word wrapping if enabled
	if (elem.tag == Tag::SPAN && doc.m_font_manager) {
		StringView value = elem.value;
		if (!value.empty()) {
			IFontManager::FontHandle font_handle = elem.font_handle;
			Vec2 size = doc.m_font_manager->measureTextA(font_handle, value);
			// Determine available width and whether wrapping is enabled
			float max_width = parent.size.x;
			if (parent.wrap && max_width > 0 && size.x > max_width) {
				// Perform simple greedy word wrapping
				float line_width = 0.f;
				int lines = 1;
				int start = 0;
				for (u32 i = 0; i < value.size(); ++i) {
					if (value[i] == ' ' || i == value.size() - 1) {
						u32 end = (i == value.size() - 1) ? i + 1 : i;
						StringView word(value.begin + start, value.begin + end);
						float word_width = doc.m_font_manager->measureTextA(font_handle, word).x;
						if (line_width + word_width > max_width && line_width > 0) {
							// Start new line with current word
							line_width = word_width + (value[i] == ' ' ? doc.m_font_manager->measureTextA(font_handle, StringView(" ")).x : 0);
							++lines;
						} else {
							line_width += word_width;
							if (value[i] == ' ') line_width += doc.m_font_manager->measureTextA(font_handle, StringView(" ")).x;
						}
						start = i + 1;
					}
				}
				elem.size.x = size.x; // Keep original width for alignment
				elem.size.y = lines * doc.m_font_manager->getHeight(font_handle);
			} else {
				elem.size = size;
			}
			elem.fit_content_width = false;
			elem.fit_content_height = false;
		}
	}

	// Compute sizes for elements with fit-content dimensions
	if (elem.fit_content_width) {
		bool is_row = elem.direction == Direction::ROW;
		if (is_row) {
			// In row direction, width is sum of child widths plus margins
			float sum_width = 0;
			for (u32 child_idx : elem.children) {
				Element& child = doc.m_elements[child_idx];
				sum_width += child.size.x + child.margins[1] + child.margins[3];
			}
			elem.size.x = sum_width + elem.paddings[1] + elem.paddings[3];
		} else {
			// In column direction, width is max of child widths plus margins
			// TODO handle wrapping
			float max_width = 0;
			for (u32 child_idx : elem.children) {
				Element& child = doc.m_elements[child_idx];
				float child_width = child.size.x + child.margins[1] + child.margins[3];
				if (child_width > max_width) max_width = child_width;
			}
			elem.size.x = max_width + elem.paddings[1] + elem.paddings[3];
		}
	}
	if (elem.fit_content_height) {
		bool is_row = elem.direction == Direction::ROW;
		if (is_row) {
			// In row direction, height is max of child heights plus margins
			// TODO handle wrapping
			float max_height = 0;
			for (u32 child_idx : elem.children) {
				Element& child = doc.m_elements[child_idx];
				float child_height = child.size.y + child.margins[0] + child.margins[2];
				if (child_height > max_height) max_height = child_height;
			}
			elem.size.y = max_height + elem.paddings[0] + elem.paddings[2];
		} else {
			// In column direction, height is sum of child heights plus margins
			float sum_height = 0;
			for (u32 child_idx : elem.children) {
				Element& child = doc.m_elements[child_idx];
				sum_height += child.size.y + child.margins[0] + child.margins[2];
			}
			elem.size.y = sum_height + elem.paddings[0] + elem.paddings[2];
		}
	}
}

static void applyStylesheet(Document& doc, Element& elem) {
	if (elem.style_class.empty()) return;

	// TODO this only matches single class, add the rest
	// TODO hashmap or something
	for (const StyleRule& rule : doc.m_stylesheet.m_rules) {
		if (rule.selector.begin[0] != '.') continue;
		if (equalStrings(rule.selector.withoutLeft(1), elem.style_class)) {
			elem.attributes.reserve(elem.attributes.size() + rule.attributes.size());
			for (const Attribute& attr : rule.attributes) {
				elem.attributes.emplace(attr);
			}
		}
	}
}

void Document::computeLayout(Vec2 canvas_size) {
	PROFILE_FUNCTION();
	m_canvas_size = canvas_size;
	ParentContext root_inherit;
	root_inherit.size = canvas_size;
	for (u32 root_idx : m_roots) {
		applyAttributes(*this, m_elements[root_idx], root_inherit);		
	}
	
	// Layout root elements as if in a panel with direction=column
	float y_offset = 0;
	float prev_bottom_margin = 0;
	for (u32 root_idx : m_roots) {
		Element& root = m_elements[root_idx];
		float top_margin = root.margins[0];
		float gap = maximum(prev_bottom_margin, top_margin);
		root.position.y = y_offset + gap;
		root.position.x = root.margins[3];
		y_offset = root.position.y + root.size.y;
		prev_bottom_margin = root.margins[2];
	}
	
	// Layout children recursively
	for (u32 root_idx : m_roots) {
		layoutChildren(*this, m_elements[root_idx]);
	}
}

static void renderElement(Draw2D& draw, const Document& doc, u32 element_idx, const Element* parent) {
	const Element& element = *doc.getElement(element_idx);
	Vec2 pos = Vec2(element.position.x, element.position.y);
	Vec2 size = Vec2(element.size.x, element.size.y);

	switch (element.tag) {
		case Tag::PANEL: draw.addRectFilled(pos, pos + size, element.bg_color); break;
		case Tag::SPAN: {
			if (element.font_handle && !element.value.empty()) {
				const Font& font = *(const Font*)element.font_handle;
				Vec2 text_size = doc.m_font_manager->measureTextA(element.font_handle, element.value);
				Vec2 text_pos = pos + Vec2(0, getAscender(font));
				draw.addText(font, text_pos, element.color, element.value);
			}
			break;
		}
		default: break;
	}

	for (u32 child_idx : element.children) {
		renderElement(draw, doc, child_idx, &element);
	}
}

void Document::render(Draw2D& draw) const {
	PROFILE_FUNCTION();
	for (u32 root_idx : m_roots) {
		renderElement(draw, *this, root_idx, nullptr);
	}
}

bool Document::parse(StringView content, const char* filename) {
	PROFILE_FUNCTION();
	m_elements.clear();
	m_roots.clear();
	m_content = content;
	m_tokenizer.m_filename = filename;
	m_tokenizer.m_document = m_content;
	m_tokenizer.m_current = m_content.c_str();
	m_tokenizer.m_current_token = m_tokenizer.nextToken();

	if (!parseElements(0xFFFF'FFFF)) return false;
	for (Element& elem : m_elements) {
		applyStylesheet(*this, elem);
	}
	return true;
}

} // namespace Lumix::ui