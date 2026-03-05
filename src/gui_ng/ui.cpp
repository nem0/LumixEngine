#include "core/log.h"
#include "core/os.h"
#include "core/profiler.h"
#include "core/stack_array.h"
#include "core/string.h"
#include "engine/resource_manager.h"
#include "gui/sprite.h"
#include "renderer/draw2d.h"
#include "renderer/font.h"
#include <cstring>
#include "ui.h"
#include "ui_tokenizer.h"

namespace Lumix::ui {

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
		case UITokenizer::Token::TEXT: return "TEXT";
	}
	return "UNKNOWN";
}

static Tag parseTag(StringView str) {
	const u32 len = (u32)str.size();
	if (len == 0) return Tag::INVALID;
	const char* s = str.begin;

	switch (s[0]) {
		case 'i': if (len == 5 && memcmp(s, "image", 5) == 0) return Tag::IMAGE; break;
		case 'p': if (len == 5 && memcmp(s, "panel", 5) == 0) return Tag::PANEL; break;
		case 's': if (len == 4 && memcmp(s, "span", 4) == 0) return Tag::SPAN; break;
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
			if (len == 6 && memcmp(s, "bg-fit", 6) == 0) return AttributeName::BACKGROUND_FIT;
			if (len == 8 && memcmp(s, "bg-image", 8) == 0) return AttributeName::BACKGROUND_IMAGE;
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
		case 'g':
			if (len == 4 && memcmp(s, "grow", 4) == 0) return AttributeName::GROW;
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

	return AttributeName::INVALID;
}

static bool hasClassId(Span<InternString> ids, InternString id) {
	for (InternString i : ids) if (i == id) return true;
	return false;
}

static int sourcePriority(AttributeSource source) {
	return source == AttributeSource::ELEMENT ? 2 : 1;
}

static void upsertAttribute(Element& elem, AttributeName type, StringView value, AttributeSource source) {
	const int incoming_prio = sourcePriority(source);

	for (const Attribute& attr : elem.attributes) {
		if (attr.type != type) continue;
		if (sourcePriority(attr.source) > incoming_prio) return;
	}

	for (i32 i = elem.attributes.size() - 1; i >= 0; --i) {
		const Attribute& attr = elem.attributes[i];
		if (attr.type != type) continue;
		if (sourcePriority(attr.source) <= incoming_prio) {
			elem.attributes.erase(i);
		}
	}

	Attribute& attr = elem.attributes.emplace();
	attr.type = type;
	attr.value = value;
	attr.source = source;
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
		error(m_tokenizer.m_current_token.value, m_tokenizer, "expected ", tokenTypeToString(type), ", got ", tokenTypeToString(t.type));
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
		
		StyleRule& rule = m_stylesheet.m_rules.emplace(m_allocator);

		const char* selector_start = token.value.begin;
		// Parse selector: supports .class, $id, or element.class combinations
		if (token.type == Token::DOT) {
			// Class selector: .classname
			Token id_token;
			if (!consume(Token::IDENTIFIER, &id_token)) return false;
			rule.classes.push(m_intern_table.intern(id_token.value));
			for (;;) {
				if (m_tokenizer.peekToken().type == Token::DOT) {
					m_tokenizer.consumeToken();
					if (!consume(Token::IDENTIFIER, &id_token)) return false;
					rule.classes.push(m_intern_table.intern(id_token.value));
				}
				else break;
			}
		} else if (token.type == Token::DOLLAR) {
			// ID selector: $idname
			Token id_token;
			if (!consume(Token::IDENTIFIER, &id_token)) return false;
			// TODO: support ID selectors in matcher
		} else if (token.type == Token::IDENTIFIER) {
			// Element selector, possibly with class/id modifiers
			for (;;) {
				Token next = m_tokenizer.peekToken();
				if (next.type == Token::DOT) {
					// Append class modifier
					if (!consume(Token::DOT)) return false;
					Token id_token;
					if (!consume(Token::IDENTIFIER, &id_token)) return false;
					rule.classes.push(m_intern_table.intern(id_token.value));
				} else if (next.type == Token::DOLLAR) {
					// Append ID modifier
					if (!consume(Token::DOLLAR)) return false;
					Token id_token;
					if (!consume(Token::IDENTIFIER, &id_token)) return false;
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
			
			Attribute& attr = rule.attributes.emplace();
			attr.value = token.value;
			attr.type = parseAttributeName(name);
			attr.source = AttributeSource::STYLESHEET;
			if (attr.type == AttributeName::INVALID) {
				error(name, m_tokenizer, "unknown attribute '", name, "'");
				return false;
			}
			
			if (!consume(Token::SEMICOLON)) return false;
		}
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
			case Token::TEXT: {
				Element& elem = m_elements.emplace(Tag::SPAN, m_allocator);
				u32 elem_idx = m_elements.size() - 1;
				if (parent_index != 0xFFFF'FFFF) {
					m_elements[parent_index].children.push(elem_idx);
				} else {
					m_roots.push(elem_idx);
				}
				elem.value = token.value;
				
				// Accumulate consecutive text tokens
				bool is_break = false;
				while (!is_break) {
					Token next = m_tokenizer.peekToken();
					switch (next.type) {
						case Token::TEXT:
							elem.value.end = next.value.end;
							m_tokenizer.consumeToken();
							break;
						case Token::LBRACKET: is_break = true; break;
						case Token::RBRACE:
							if (parent_index == 0xffff'FFFF) {
								error(token.value, m_tokenizer, "unexpected right brace");
								return false;
							}
							m_tokenizer.consumeToken();
							return true;
						case Token::EOF:
							if (parent_index == 0xffff'FFFF) return true;
							error(token.value, m_tokenizer, "unexpected EOF");
							return false;
						default:
							elem.value.end = next.value.end;
							if (next.type == Token::STRING) {
								elem.value.end += 1;
							}
							m_tokenizer.consumeToken();
							break;
					}
				}
				break;
			}
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
						if (parent_index != 0xffff'FFFF) {
							error(token.value, m_tokenizer, "[style] must be at the root level");
							return false;
						}
						if (!consume(Token::RBRACKET, &token)) return false;
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
					if (m_tokenizer.peekToken().type == Token::DOT) {
						if (!consume(Token::DOT)) return false;	
						if (!consume(Token::IDENTIFIER, &name_token)) return false;	
						InternString class_id = m_intern_table.intern(name_token.value);
						if (!hasClassId(elem.classes, class_id)) {
							elem.classes.push(class_id);
						}
						continue;
					}

					if (!consume(Token::IDENTIFIER, &name_token)) return false;
					if (!consume(Token::EQUALS)) return false;
			
					Token value = m_tokenizer.consumeToken();
					if (value.type != Token::STRING && value.type != Token::IDENTIFIER && value.type != Token::NUMBER && value.type != Token::PERCENTAGE && value.type != Token::EM && value.type != Token::COLOR) {
						error(value.value, m_tokenizer, "expected attribute value, got ", tokenTypeToString(value.type));
						return false;
					}

					AttributeName name = parseAttributeName(name_token.value);
					if (name == AttributeName::INVALID) {
						error(name_token.value, m_tokenizer, "unknown attribute '", name_token.value, "'");
						return false;
					}
					if (name == AttributeName::VALUE) {
						elem.value = value.value;
					}
					else {
						upsertAttribute(elem, name, value.value, AttributeSource::ELEMENT);
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
				Element& elem = m_elements.emplace(Tag::SPAN, m_allocator);
				u32 elem_idx = m_elements.size() - 1;
				if (parent_index != 0xFFFF'FFFF) {
					m_elements[parent_index].children.push(elem_idx);
				} else {
					m_roots.push(elem_idx);
				}
				if (token.type == Token::STRING) {
					elem.value = StringView{token.value.begin - 1, token.value.end + 1};
				} else {
					elem.value = token.value;
				}
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
						case Token::TEXT:
							// Include TEXT tokens in the element value
							elem.value.end = next.value.end;
							m_tokenizer.consumeToken();
							break;
						default: 
							elem.value.end = next.value.end;
							if (next.type == Token::STRING) {
								elem.value.end += 1;
							}
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

static u32 layoutSpans(Document& doc, Element& parent, u32 first_span_idx, float x, float y) {
	u32 count = 0;
	for (i32 i = first_span_idx; i < parent.children.size(); ++i) {
		u32 child_idx = parent.children[i];
		Element& child = doc.m_elements[child_idx];
		if (child.tag != Tag::SPAN) break;
		if (child.lines.empty()) {
			child.position.x = x;
			child.position.y = y;
		}
		else {
			for (SpanLine& line : child.lines) {
				line.pos.x += x;
				line.pos.y += y;
			}
			child.position = child.lines[0].pos;
		}
		++count;
	}
	return count;
}

// Positions child elements within their parent container based on layout direction and margins.
// For row direction, children are laid out horizontally; for column, vertically.
// Margins between adjacent children collapse to the maximum value.
static void layoutChildrenHorizontal(Document& doc, Element& parent) {
	float start_x = parent.position.x + parent.paddings.left;
	float y = parent.position.y + parent.paddings.top;

	for (u32 i = 0, n = parent.children.size(); i < n; ++i) {
		u32 child_idx = parent.children[i];
		if (doc.m_elements[child_idx].tag == Tag::SPAN) {
			i += layoutSpans(doc, parent, i, start_x, y) - 1;
			y += doc.m_elements[child_idx].size.y;
		}
		else {
			float prev_margin = 0;
			i32 row_start = i;
			i32 row_end;
			float max_h = 0;
			float available_w = parent.size.x - parent.paddings.right - parent.paddings.left;
			for (row_end = i; row_end < parent.children.size(); ++row_end) {
				Element& child = doc.m_elements[parent.children[row_end]];
				float wtmp = child.size.x + maximum(prev_margin, child.margins.left);
				if (available_w < wtmp + child.margins.right && parent.wrap || child.tag == Tag::SPAN) {
					break;
				}
				max_h = maximum(max_h, child.size.y + child.margins.top + child.margins.bottom);
				available_w -= wtmp;
				prev_margin = child.margins.right;
			}
			if (row_end == row_start) continue;
			available_w -= doc.m_elements[parent.children[row_end - 1]].margins.right;

			// layout row_start <-> row_end
			float x = start_x;
			float space = 0;
			switch (parent.justify_content) {
				case JustifyContent::END: x += available_w; break;
				case JustifyContent::CENTER: x += available_w / 2; break;
				case JustifyContent::SPACE_BETWEEN: space = available_w / (row_end - row_start - 1); break;
				case JustifyContent::SPACE_AROUND: 
					space = available_w / (row_end - row_start + 1);
					x += space;
					break;
				default: break;
			}

			prev_margin = 0;
			for (i32 j = row_start; j < row_end; ++j) {
				Element& child = doc.m_elements[parent.children[j]];
				child.position.x = x + maximum(prev_margin, child.margins.left);
				child.position.y = y + child.margins.top;
				switch (parent.align_items) {
					case AlignItems::START: break;
					case AlignItems::END: child.position.y = y + max_h - child.size.y - child.margins.bottom; break;
					case AlignItems::CENTER: child.position.y = y + (max_h - child.size.y) / 2; break;
					case AlignItems::STRETCH: child.size.y = max_h - child.margins.top - child.margins.bottom; break;
				}
				prev_margin = child.margins.right;
				x = child.position.x + child.size.x + space;
			}
			
			// prepare for next row or end
			y += max_h;
			i = row_end - 1;
		}
	}
}

static void layoutChildrenVertical(Document& doc, Element& parent) {
	float x = parent.position.x + parent.paddings.left;
	float available_w = parent.size.x - parent.paddings.right - parent.paddings.left;

	// First, compute total height used
	float total_height = 0;
	float prev_margin = 0;
	for (u32 i = 0, n = parent.children.size(); i < n; ++i) {
		u32 child_idx = parent.children[i];
		Element& child = doc.m_elements[child_idx];
		if (child.tag == Tag::SPAN) {
			total_height += child.size.y + prev_margin;
			prev_margin = 0;
			while (i < n && doc.m_elements[parent.children[i]].tag == Tag::SPAN) {
				++i;
			}
			--i;
		}
		else {
			total_height += child.size.y + maximum(prev_margin, child.margins.top);
			prev_margin = child.margins.bottom;
		}
	}
	total_height += prev_margin; // add the last bottom margin

	float available_h = parent.size.y - parent.paddings.top - parent.paddings.bottom;
	float remaining = available_h - total_height;

	// Apply justify_content
	float start_y = parent.position.y + parent.paddings.top;
	float space = 0;
	switch (parent.justify_content) {
		case JustifyContent::START: break;
		case JustifyContent::END: start_y += remaining; break;
		case JustifyContent::CENTER: start_y += remaining / 2; break;
		case JustifyContent::SPACE_BETWEEN:
			if (parent.children.size() > 1) space = remaining / (parent.children.size() - 1);
			break;
		case JustifyContent::SPACE_AROUND:
			space = remaining / (parent.children.size() + 1);
			start_y += space;
			break;
		default: break;
	}

	// Now position the children
	float y = start_y;
	prev_margin = 0;
	for (u32 i = 0, n = parent.children.size(); i < n; ++i) {
		u32 child_idx = parent.children[i];
		Element& child = doc.m_elements[child_idx];
		if (child.tag == Tag::SPAN) {
			i += layoutSpans(doc, parent, i, x + prev_margin, y) - 1;
			y += child.size.y + space;
		}
		else {
			child.position.y = y + maximum(prev_margin, child.margins.top);

			// Apply align-items for horizontal positioning
			switch (parent.align_items) {
				case AlignItems::START:
					child.position.x = x + child.margins.left;
					break;
				case AlignItems::END:
					child.position.x = parent.position.x + parent.size.x - parent.paddings.right - child.size.x - child.margins.right;
					break;
				case AlignItems::CENTER:
					child.position.x = x + (available_w - child.size.x) / 2;
					break;
				case AlignItems::STRETCH:
					child.position.x = x + child.margins.left;
					break;
			}

			prev_margin = child.margins.bottom;
			y = child.position.y + child.size.y + space;
			for (SpanLine& line : child.lines) {
				line.pos.x += child.position.x;
				line.pos.y += child.position.y;
			}
		}
	}
}

// Positions child elements within their parent container based on layout direction and margins.
// For row direction, children are laid out horizontally; for column, vertically.
// Margins between adjacent children collapse to the maximum value.
static void layoutChildren(Document& doc, Element& parent) {
	if (parent.direction == Direction::ROW) layoutChildrenHorizontal(doc, parent);
	else layoutChildrenVertical(doc, parent);

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
	Vec2 size;
};

// distribute grow and compute %-based widths
static void computeParentRelativeWidth(Document& doc, Element& elem) {
	if (elem.children.empty()) return;

	if (elem.direction == Direction::ROW) {
		float sum_grow = 0;
		float remaining_w = elem.size.x - elem.paddings.right - elem.paddings.left;
		float prev_margin = 0;
		for (u32 child_idx : elem.children) {
			const Element& child = doc.m_elements[child_idx];
			sum_grow += child.grow;
			remaining_w -= child.size.x + maximum(prev_margin, child.margins.left);
			prev_margin = child.margins.right;
		
		}
		remaining_w -= prev_margin;

		if (sum_grow > 0) {
			if (remaining_w > 0) {
				for (u32 child_idx : elem.children) {
					Element& child = doc.m_elements[child_idx];
					if (child.grow > 0) {
						child.size.x += remaining_w * child.grow / sum_grow;
					}
				}
			}
		}
	} else if (elem.direction == Direction::COLUMN && elem.align_items == AlignItems::STRETCH) {
		// In column direction, cross axis is width. Stretch children to fill parent's content width.
		float content_w = elem.size.x - elem.paddings.right - elem.paddings.left;
		for (u32 child_idx : elem.children) {
			Element& child = doc.m_elements[child_idx];
			if (child.tag == Tag::SPAN) continue;
			if (child.width_unit.unit == Unit::FIT_CONTENT) {
				child.size.x = maximum(0.0f, content_w - child.margins.right - child.margins.left);
			}
		}
	}

	for (u32 child_idx : elem.children) {
		Element& child = doc.m_elements[child_idx];
		if (child.width_unit.unit == Unit::PERCENT) {
			if (elem.width_unit.unit == Unit::FIT_CONTENT) {
				logError("Element with fit-content width has child with percent width");
			}
			child.size.x = computeAbsoluteSize(child.width_unit, elem.size.x, child.font_size);
		}
		computeParentRelativeWidth(doc, doc.m_elements[child_idx]);
	}
}

// compute heights of consecutive spans in element, starting from child_idx
static float computeSpansHeight(Document& doc, Element& element, i32 child_idx) {
	i32 end_idx = child_idx;
	while (end_idx < element.children.size() && doc.m_elements[element.children[end_idx]].tag == Tag::SPAN) {
		++end_idx;
	}
	if (end_idx == child_idx) return 0;

	const Element& last = doc.m_elements[element.children[end_idx - 1]];
	if (last.lines.empty()) {
		return doc.m_font_manager->getHeight(last.font_handle);
	}
	const SpanLine& last_line = last.lines.last();

	float asc = doc.m_font_manager->getAscender(last.font_handle);
	float height = doc.m_font_manager->getHeight(last.font_handle);
	return last_line.pos.y - asc + height;
}

static void computeBaseHeights(Document& doc, Element& elem, Element* parent_elem, const ParentContext& parent) {
	elem.height_unit = {0, Unit::FIT_CONTENT};
	ParsedUnit margin_unit = {0, Unit::PIXELS};
	ParsedUnit padding_unit = {0, Unit::PIXELS};

	for (const Attribute& attr : elem.attributes) {
		switch (attr.type) {
			case AttributeName::MARGIN: margin_unit = parseUnit(attr.value); break;
			case AttributeName::PADDING: padding_unit = parseUnit(attr.value); break;
			case AttributeName::HEIGHT: {
				elem.height_unit = parseUnit(attr.value);
				break;
			}
			default: break;
		}
	}

	if (elem.tag == Tag::SPAN) return;

	elem.margins.top = elem.margins.bottom = computeAbsoluteSize(margin_unit, parent.size.y, elem.font_size);
	elem.paddings.top = elem.paddings.bottom = computeAbsoluteSize(padding_unit, parent.size.y, elem.font_size);

	if (elem.height_unit.unit != Unit::FIT_CONTENT) {
		elem.size.y = computeAbsoluteSize(elem.height_unit, parent.size.y, elem.font_size);
	}

	ParentContext ctx = parent;
	ctx.size = elem.size;
	for (u32 child_idx : elem.children) {
		computeBaseHeights(doc, doc.m_elements[child_idx], &elem, ctx);
	}

	if (elem.height_unit.unit == Unit::FIT_CONTENT) {
		if (elem.direction == Direction::ROW) {
			float max_height = 0;
			for (u32 child_idx : elem.children) {
				Element& child = doc.m_elements[child_idx];
				float child_height = child.size.y + child.margins.top + child.margins.bottom;
				if (child_height > max_height) max_height = child_height;
			}
			elem.size.y = max_height + elem.paddings.top + elem.paddings.bottom;
		} else {
			float sum_height = 0;
			for (u32 i = 0, n = elem.children.size(); i < n; ++i) {
				Element& child = doc.m_elements[elem.children[i]];
				if (child.tag == Tag::SPAN) {
					sum_height += computeSpansHeight(doc, elem, i);
					i += 1; // skip the consecutive spans
					while (i < n && doc.m_elements[elem.children[i]].tag == Tag::SPAN) ++i;
					--i;
				} else {
					sum_height += child.size.y + child.margins.top + child.margins.bottom;
				}
			}
			elem.size.y = sum_height + elem.paddings.top + elem.paddings.bottom;
		}
	}
}

static void computeParentRelativeHeights(Document& doc, Element& elem) {
	if (elem.direction == Direction::COLUMN) {
		float sum_grow = 0;
		float remaining_h = elem.size.y - elem.paddings.top - elem.paddings.bottom;
		float prev_margin = 0;
		for (u32 child_idx : elem.children) {
			const Element& child = doc.m_elements[child_idx];
			sum_grow += child.grow;
			remaining_h -= child.size.y + maximum(prev_margin, child.margins.top);
			prev_margin = child.margins.bottom;
		}
		remaining_h -= prev_margin;

		if (sum_grow > 0 && remaining_h > 0) {
			for (u32 child_idx : elem.children) {
				Element& child = doc.m_elements[child_idx];
				if (child.grow > 0) {
					child.size.y += remaining_h * child.grow / sum_grow;
				}
			}
		}
	}

	for (u32 child_idx : elem.children) {
		Element& child = doc.m_elements[child_idx];
		if (child.height_unit.unit == Unit::PERCENT) {
			child.size.y = computeAbsoluteSize(child.height_unit, elem.size.y, child.font_size);
		}
		computeParentRelativeHeights(doc, child);
	}
}

static void computeBaseWidths(Document& doc, Element& elem, Element* parent_elem, const ParentContext& parent) {
	elem.position = Vec2(0, 0);
	elem.size = Vec2(0, 0);
	elem.wrap = true;
	elem.justify_content = JustifyContent::START;
	elem.align_items = AlignItems::START;
	elem.text_align = parent.align;
	elem.font_size = parent.font_size;
	elem.color = parent.color;
	elem.bg_color = Color(0);
	memset(&elem.margins, 0, sizeof(elem.margins));
	memset(&elem.paddings, 0, sizeof(elem.paddings));

	ParentContext ctx = parent;
	ctx.size = Vec2(0);

	elem.width_unit = {0, Unit::FIT_CONTENT};
	ParsedUnit margin_unit = {0, Unit::PIXELS};
	ParsedUnit padding_unit = {0, Unit::PIXELS};

	for (const Attribute& attr : elem.attributes) {
		switch (attr.type) {
			case AttributeName::WIDTH: {
				elem.width_unit = parseUnit(attr.value);
				break;
			}
			case AttributeName::GROW: fromCString(attr.value, elem.grow); break;
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
			case AttributeName::COLOR: {
				elem.color = parseColor(attr.value);
				ctx.color = elem.color;
				break;
			}
			case AttributeName::BG_COLOR: elem.bg_color = parseColor(attr.value); break;
			case AttributeName::VALUE: elem.value = attr.value; break;
			case AttributeName::JUSTIFY_CONTENT: elem.justify_content = parseJustifyContent(attr.value); break;
			case AttributeName::ALIGN_ITEMS: elem.align_items = parseAlignItems(attr.value); break;
			case AttributeName::FONT: ctx.font = attr.value; break;
			case AttributeName::FONT_SIZE: ctx.font_size = (float)atof(attr.value.begin); break;
			case AttributeName::WRAP: elem.wrap = attr.value == "true"; break;
			case AttributeName::PADDING: padding_unit = parseUnit(attr.value); break;
			case AttributeName::MARGIN: margin_unit = parseUnit(attr.value); break;
			default: break;
		}
	}

	elem.font_size = ctx.font_size;

	if (elem.tag == Tag::SPAN) {
		if (doc.m_font_manager && elem.font_handle && !elem.value.empty()) {
			elem.size = doc.m_font_manager->measureTextA(elem.font_handle, elem.value);
		}
		return;
	}

	if (elem.width_unit.unit != Unit::FIT_CONTENT) {
		elem.size.x = computeAbsoluteSize(elem.width_unit, parent.size.x, elem.font_size);
	}
	elem.margins.right = computeAbsoluteSize(margin_unit, parent.size.x, elem.font_size);
	elem.margins.left = elem.margins.right;
	elem.paddings.right = computeAbsoluteSize(padding_unit, parent.size.x, elem.font_size);
	elem.paddings.left = elem.paddings.right;

	ctx.size = elem.size;
	for (u32 child_idx : elem.children) {
		computeBaseWidths(doc, doc.m_elements[child_idx], &elem, ctx);
	}

	// bottom-up size computation	
	// fit-content
	bool is_row = elem.direction == Direction::ROW;
	if (elem.width_unit.unit == Unit::FIT_CONTENT) {
		if (is_row) {
			// In row direction, width is sum of child widths plus margins
			float sum_width = 0;
			for (u32 child_idx : elem.children) {
				Element& child = doc.m_elements[child_idx];
				sum_width += child.size.x + child.margins.right + child.margins.left;
			}
			elem.size.x = sum_width + elem.paddings.right + elem.paddings.left;
		} else {
			// In column direction, width is max of child widths plus margins
			float max_width = 0;
			for (u32 i = 0, n = elem.children.size(); i < n; ++i) {
				u32 child_idx = elem.children[i];
				Element& child = doc.m_elements[child_idx];
				float child_width;
				if (child.tag == Tag::SPAN) {
					child_width = 0;
					while (i < n && doc.m_elements[elem.children[i]].tag == Tag::SPAN) {
						child_width += doc.m_elements[elem.children[i]].size.x;
						++i;
					}
					--i;
				}
				else {
					 child_width = child.size.x + child.margins.right + child.margins.left;
				}
				if (child_width > max_width) max_width = child_width;
			}
			elem.size.x = max_width + elem.paddings.right + elem.paddings.left;
		}
	}
}

void Stylesheet::buildIndex() {
	m_class_index.clear();
	for (u32 rule_idx = 0; rule_idx < (u32)m_rules.size(); ++rule_idx) {
		const StyleRule& rule = m_rules[rule_idx];
		for (InternString class_id : rule.classes) {
			auto iter = m_class_index.find(class_id);
			if (iter.isValid()) {
				iter.value().push(rule_idx);
			} else {
				Array<u32> indices(m_allocator);
				indices.push(rule_idx);
				m_class_index.insert(class_id, indices.move());
			}
		}
	}
}

static void applyStylesheet(Document& doc, Element& elem) {
	if (elem.classes.empty()) return;

	StackArray<u32, 32> candidates(doc.m_allocator);

	for (InternString class_id : elem.classes) {
		auto iter = doc.m_stylesheet.m_class_index.find(class_id);
		if (iter.isValid()) {
			for (u32 rule_idx : iter.value()) {
				bool already_added = false;
				for (u32 existing : candidates) {
					if (existing == rule_idx) {
						already_added = true;
						break;
					}
				}
				if (!already_added) {
					candidates.push(rule_idx);
				}
			}
		}
	}

	for (u32 rule_idx : candidates) {
		const StyleRule& rule = doc.m_stylesheet.m_rules[rule_idx];
		if (rule.classes.empty()) continue;

		bool match = true;
		for (InternString cid : rule.classes) {
			if (!hasClassId(elem.classes, cid)) {
				match = false;
				break;
			}
		}

		if (match) {
			for (const Attribute& attr : rule.attributes) {
				upsertAttribute(elem, attr.type, attr.value, AttributeSource::STYLESHEET);
			}
		}
	}
}

struct WordWrap {
	enum Overflow {
		NO,
		SPACE,
		MIDWORD
	};
	StringView text;
	float width;
	Overflow overflow;
};

struct RowLine { Element* child; SpanLine* line; };

static float layoutRowVertical(Document& doc, Element& parent, StackArray<RowLine, 32>& row_lines, float row_y_pos) {
    if (row_lines.empty()) return 0;

    // baseline align
    float max_ascender = 0, max_height = 0;
	for (RowLine& rl : row_lines) {
        float asc = doc.m_font_manager->getAscender(rl.child->font_handle);
        float height = doc.m_font_manager->getHeight(rl.child->font_handle);
        max_ascender = maximum(max_ascender, asc);
        max_height = maximum(max_height, height);
        rl.child->size.y += height;
    }
    for (RowLine& rl : row_lines) {
        rl.line->pos.y = row_y_pos + max_ascender;
    }

	// horizontal align
    if (parent.text_align != Align::LEFT) {
        float min_x = FLT_MAX, max_x = -FLT_MAX;
        for (RowLine& rl : row_lines) {
            min_x = minimum(min_x, rl.line->pos.x);
            max_x = maximum(max_x, rl.line->pos.x + rl.line->width);
        }
        if (min_x <= max_x) {
            float used = max_x - min_x;
            float avail = parent.size.x - parent.paddings.right - parent.paddings.left;
            float shift = 0;
            switch (parent.text_align) {
                case Align::LEFT: break;
                case Align::CENTER: shift = (avail - used) * 0.5f - min_x; break;
                case Align::RIGHT: shift = avail - used - min_x; break;
            }
            if (shift != 0) {
                for (RowLine& rl : row_lines) {
                    rl.line->pos.x += shift;
                }
            }
        }
    }
	row_lines.clear();
    return max_height;
}

static StringView trimLeadingWhitespace(StringView text) {
	while (!text.empty() && isWhitespace(text[0])) {
		text.removePrefix(1);
	}
	return text;
}

// word wrapping and line breaking based on the parent's width and wrap setting.
static void wrapSpans(Document& doc, Element& parent, i32 start_span_idx, i32 end_span_idx) {
	float x = 0;
	const float content_width = parent.size.x - parent.paddings.left - parent.paddings.right;
	if (content_width <= 0) return;
	bool wrap_enabled = parent.wrap && parent.size.x > 0;
	// First pass: wrap width
	bool is_prev_space = false;
	for (i32 child_idx = start_span_idx; child_idx < end_span_idx; ++child_idx) {
		Element& span = doc.m_elements[parent.children[child_idx]];
		span.lines.clear();
		if (!span.font_handle || span.value.empty()) continue;

		StringView space(" ", 1);
		const float space_w = doc.m_font_manager->measureTextA(span.font_handle, space).x;

		StringView text = span.value;
		if (wrap_enabled) {
			SpanLine* line = nullptr;
			while (!text.empty()) {
				is_prev_space = is_prev_space || isWhitespace(*text.begin);
				SplitWord split = doc.m_font_manager->splitFirstWord(span.font_handle, text);
				if (split.head.empty()) {
					// this should only be possible if text ends with whitespaces
					ASSERT(split.tail.empty());
					is_prev_space = true;
					break;
				}
				// we wrap
				if ((is_prev_space ? space_w : 0) + split.head_width > content_width - x) {
					line = &span.lines.emplace();
					line->text = split.head;
					line->pos.x = 0;
					line->width = split.head_width;
					x = split.head_width;
				}
				// we fit
				else {
					if (is_prev_space) {
						x += space_w;
						if (line) {
							line->width += space_w;
						}
					}
					if (!line) {
						line = &span.lines.emplace();
						line->text.begin = split.head.begin;
						line->width = 0;
						line->pos.x = x;
					}
					
					line->text.end = split.head.end;
					line->width += split.head_width;
					x += split.head_width;
				}
				text = split.tail;
				if (text.empty()) is_prev_space = false;
			}
		} else {
			SpanLine& line = span.lines.emplace();
			line.text = text;
			line.pos.x = x;
			line.pos.y = 0;
			line.width = span.size.x;
			x += line.width;
		}
	}
	
	// Second pass: Group SpanLines into rows and layout each row
	float row_y = 0;
	StackArray<RowLine, 32> row_lines(doc.m_allocator);
	float prev_x = -1;
	for (i32 child_idx = start_span_idx; child_idx < end_span_idx; ++child_idx) {
		Element& child = doc.m_elements[parent.children[child_idx]];
		child.size.y = 0;
		for (SpanLine& line : child.lines) {
			bool is_new_row = line.pos.x <= prev_x;
			if (is_new_row) {
				row_y += layoutRowVertical(doc, parent, row_lines, row_y);
			}
			prev_x = line.pos.x;
			row_lines.emplace(&child, &line);
		}
	}
	// Layout last row
	row_y += layoutRowVertical(doc, parent, row_lines, row_y);
	
	// 
	for (i32 child_idx = start_span_idx; child_idx < end_span_idx; ++child_idx) {
		Element& span = doc.m_elements[parent.children[child_idx]];
		span.size.y = row_y;
	}
}

// compute wrapping on word boundaries, fill Element::lines
static void wrapText(Document& doc, u32 element_index) {
	Element& elem = doc.m_elements[element_index];
	elem.lines.clear();

	if (elem.tag == Tag::SPAN) {
		// TODO top level span without container
		ASSERT(false);
	}

	for (u32 i = 0, n = elem.children.size(); i < n; ++i) {
		Element& child = doc.m_elements[elem.children[i]];
		if (child.tag != Tag::SPAN) {
			wrapText(doc, elem.children[i]);
			continue;
		}

		i32 start_i = i; 
		i32 end_i = i + 1;
		while (end_i < elem.children.size() && doc.m_elements[elem.children[end_i]].tag == Tag::SPAN) {
			++end_i;
		}
		i = end_i - 1;

		wrapSpans(doc, elem, start_i, end_i);
	}
}


void Document::computeLayout(Vec2 canvas_size) {
	PROFILE_FUNCTION();
	
	os::Timer timer;
	m_layout_duration = 0;
	m_canvas_size = canvas_size;
	ParentContext root_inherit;
	root_inherit.size = canvas_size;
	for (u32 root_idx : m_roots) {
		computeBaseWidths(*this, m_elements[root_idx], nullptr, root_inherit);		
	}
	
	for (u32 root_idx : m_roots) {
		computeParentRelativeWidth(*this, m_elements[root_idx]);
	}

	for (u32 root_idx : m_roots) {
		wrapText(*this, root_idx);
	}

	for (u32 root_idx : m_roots) {
		computeBaseHeights(*this, m_elements[root_idx], nullptr, root_inherit);		
	}

	for (u32 root_idx : m_roots) {
		computeParentRelativeHeights(*this, m_elements[root_idx]);
	}

	// Layout root elements as if in a panel with direction=column
	float y_offset = 0;
	float prev_bottom_margin = 0;
	for (u32 root_idx : m_roots) {
		Element& root = m_elements[root_idx];
		if (root.height_unit.unit == Unit::PERCENT) {
			root.size.y = computeAbsoluteSize(root.height_unit, canvas_size.y, root.font_size);
		}
		float top_margin = root.margins.top;
		float gap = maximum(prev_bottom_margin, top_margin);
		root.position.y = y_offset + gap;
		root.position.x = root.margins.left;
		y_offset = root.position.y + root.size.y;
		prev_bottom_margin = root.margins.bottom;
	}
	
	// Layout children recursively
	for (u32 root_idx : m_roots) {
		layoutChildren(*this, m_elements[root_idx]);
	}
	
	m_layout_duration = timer.getTimeSinceStart();
}

static void renderElement(Draw2D& draw, const Document& doc, u32 element_idx, const Element* parent) {
	const Element& element = *doc.getElement(element_idx);
	Vec2 pos = Vec2(element.position.x, element.position.y);
	Vec2 size = Vec2(element.size.x, element.size.y);

	switch (element.tag) {
		case Tag::PANEL: {
			if (element.bg_sprite) {
				element.bg_sprite->render(draw, pos.x, pos.y, pos.x + size.x, pos.y + size.y, Color::WHITE);
			}
			else {
				draw.addRectFilled(pos, pos + size, element.bg_color);
			}
			break;
		}
		case Tag::SPAN: {
			if (element.font_handle && !element.value.empty()) {
				const Font& font = *(const Font*)element.font_handle;
				if (!element.lines.empty()) {
					for (const SpanLine& line : element.lines) {
						draw.addText(font, line.pos, element.color, line.text);
					}
				} else {
					// fallback
					Vec2 text_pos = pos + Vec2(0, getAscender(font));
					draw.addText(font, text_pos, element.color, element.value);
				}
			}
			break;
		}
		default: break;
	}

	for (u32 child_idx : element.children) {
		renderElement(draw, doc, child_idx, &element);
	}
}

void Document::render(Draw2D& draw) {
	PROFILE_FUNCTION();
	os::Timer timer;
	m_render_duration = 0;
	for (u32 root_idx : m_roots) {
		renderElement(draw, *this, root_idx, nullptr);
	}
	m_render_duration = timer.getTimeSinceStart();
}

static bool contains(const Element& elem, Vec2 pos, IFontManager* font_manager) {
	Vec2 elem_pos = elem.position;
	if (elem.tag == Tag::SPAN && elem.font_handle && font_manager) {
		float asc = font_manager->getAscender(elem.font_handle);
		elem_pos.y -= asc;
	}
	return pos.x >= elem_pos.x && pos.x <= elem_pos.x + elem.size.x &&
			pos.y >= elem_pos.y && pos.y <= elem_pos.y + elem.size.y;
}

static void refreshAfterClassMutation(Document& doc, u32 element_index) {
	doc.recomputeStyles();
}

void Document::addClass(u32 element_index, StringView classname) {
	if (element_index >= (u32)m_elements.size()) return;
	if (classname.empty()) return;
	
	Element& elem = m_elements[element_index];
	InternString class_id = m_intern_table.intern(classname);
	if (hasClassId(elem.classes, class_id)) return;
	
	elem.classes.push(class_id);
	
	refreshAfterClassMutation(*this, element_index);
}

void Document::removeClass(u32 element_index, StringView classname) {
	if (element_index >= (u32)m_elements.size()) return;
	if (classname.empty()) return;
	
	Element& elem = m_elements[element_index];
	InternString class_id = m_intern_table.intern(classname);
	
	// Find and remove the class_id
	for (i32 i = 0; i < (i32)elem.classes.size(); ++i) {
		if (elem.classes[i] == class_id) {
			elem.classes.erase(i);
			break;
		}
	}
	
	refreshAfterClassMutation(*this, element_index);
}

Element* Document::getElementAt(Vec2 pos) {
	for (u32 root_id : m_roots) {
		Element* root = &m_elements[root_id]; 
		if (!contains(*root, pos, m_font_manager)) continue;

		Element* elem = root;
		for (;;) {
			bool found_child = false;
			for (u32 child_id : elem->children) {
				Element* child = &m_elements[child_id];
				if (contains(*child, pos, m_font_manager)) {
					elem = child;
					found_child = true;
					break;
				}
			}
			if (!found_child) return elem;
		}
	}
	return nullptr;
}

static void loadResources(Document& doc, u32 element_index, const ParentContext& parent) {
	Element& elem = doc.m_elements[element_index];
	ParentContext ctx = parent;

	elem.font_size = ctx.font_size;
	elem.color = ctx.color;
	elem.text_align = ctx.align;

	for (const Attribute& attr : elem.attributes) {
		switch (attr.type) {
			case AttributeName::FONT: ctx.font = attr.value; break;
			case AttributeName::FONT_SIZE: 
				fromCString(attr.value, elem.font_size);
				ctx.font_size = elem.font_size;
				break;
			case AttributeName::BACKGROUND_IMAGE: {
				if (doc.m_resource_manager && !attr.value.empty()) {
					elem.bg_sprite = doc.m_resource_manager->load<Sprite>(Path(attr.value));
				}
				break;
			}
			default: break;
		}
	}

	if (!ctx.font.empty() && doc.m_font_manager) {
		elem.font_handle = doc.m_font_manager->loadFont(ctx.font, (i32)ctx.font_size);
	}

	for (u32 child_idx : elem.children) {
		loadResources(doc, child_idx, ctx);
	}
}

bool Document::parse(StringView content, const char* filename) {
	PROFILE_FUNCTION();
	os::Timer timer;
	m_parse_duration = 0;
	m_elements.clear();
	m_roots.clear();
	m_content = content;
	m_tokenizer.m_filename = filename;
	m_tokenizer.m_document = m_content;
	m_tokenizer.m_current = m_content.c_str();
	m_tokenizer.m_current_token = m_tokenizer.nextToken();
	if (!parseElements(0xFFFF'FFFF)) return false;
	m_stylesheet.buildIndex();
	recomputeStyles();

	for (u32 root_idx : m_roots) {
		ParentContext ctx;
		ctx.font = "/engine/editor/fonts/JetBrainsMono-Regular.ttf";
		loadResources(*this, root_idx, ctx);
	}

	m_parse_duration = timer.getTimeSinceStart();
	return true;
}

Span<const Event> Document::getEvents() {
	return Span(m_events.begin(), m_events.size());
}

void Document::clearEvents() {
	m_events.clear();
}

void Document::injectEvent(const InputSystem::Event& event) {
	switch (event.type) {
		case InputSystem::Event::BUTTON: {
			const auto& btn = event.data.button;
			if (event.device->type == InputSystem::Device::MOUSE) {
				Event ui_event;
				ui_event.type = btn.down ? EventType::MOUSE_DOWN : EventType::MOUSE_UP;
				ui_event.position = Vec2(btn.x, btn.y);
				ui_event.key_code = btn.key_id;
				Element* elem = getElementAt(ui_event.position);
				ui_event.element_index = elem ? u32(elem - m_elements.begin()) : 0;
				m_events.push(ui_event);
				if (!btn.down) {
					ui_event.type = EventType::CLICK;
					m_events.push(ui_event);
				}
			} else if (event.device->type == InputSystem::Device::KEYBOARD) {
				Event ui_event;
				ui_event.type = btn.down ? EventType::KEY_DOWN : EventType::KEY_UP;
				ui_event.key_code = btn.key_id;
				ui_event.position = Vec2(0, 0);
				ui_event.element_index = 0; // TODO: focused element
				m_events.push(ui_event);
			}
			break;
		}
		case InputSystem::Event::AXIS: {
			if (event.device->type == InputSystem::Device::MOUSE) {
				Vec2 pos(event.data.axis.x_abs, event.data.axis.y_abs);
				Element* elem = getElementAt(pos);
				u32 new_hovered_index = elem ? u32(elem - m_elements.begin()) : 0xFFFF'FFFF;

				if (new_hovered_index != m_hovered_element_index) {
					if (m_hovered_element_index != 0xFFFF'FFFF) {
						Event leave_event;
						leave_event.type = EventType::MOUSE_LEAVE;
						leave_event.position = pos;
						leave_event.element_index = m_hovered_element_index;
						m_events.push(leave_event);
					}
					if (new_hovered_index != 0xFFFF'FFFF) {
						Event enter_event;
						enter_event.type = EventType::MOUSE_ENTER;
						enter_event.position = pos;
						enter_event.element_index = new_hovered_index;
						m_events.push(enter_event);
					}
					m_hovered_element_index = new_hovered_index;
				}

				Event ui_event;
				ui_event.type = EventType::MOUSE_MOVE;
				ui_event.position = pos;
				ui_event.element_index = new_hovered_index != 0xFFFF'FFFF ? new_hovered_index : 0;
				m_events.push(ui_event);
			}
			break;
		}
		case InputSystem::Event::MOUSE_WHEEL: {
			Event ui_event;
			ui_event.type = EventType::MOUSE_WHEEL;
			ui_event.position = Vec2(event.data.mouse_wheel.x, event.data.mouse_wheel.y);
			ui_event.wheel_y = event.data.mouse_wheel.y;
			Element* elem = getElementAt(ui_event.position);
			ui_event.element_index = elem ? u32(elem - m_elements.begin()) : 0;
			m_events.push(ui_event);
			break;
		}
		case InputSystem::Event::TEXT_INPUT: {
			Event ui_event;
			ui_event.type = EventType::TEXT_INPUT;
			ui_event.text_utf8 = event.data.text.utf8;
			ui_event.element_index = 0; // TODO: focused element
			m_events.push(ui_event);
			break;
		}
		default:
			break;
	}
}

bool Document::areDependenciesReady() const {
	for (const Element& elem : m_elements) {
		if (elem.bg_sprite && !elem.bg_sprite->isReady()) return false;
		if (elem.font_handle && !m_font_manager->isReady(elem.font_handle)) return false;
	}
	return true;
}

void Document::recomputeStyles() {
	for (Element& elem : m_elements) {
		for (i32 i = elem.attributes.size() - 1; i >= 0; --i) {
			if (elem.attributes[i].source == AttributeSource::STYLESHEET) {
				elem.attributes.erase(i);
			}
		}
	}
	for (Element& elem : m_elements) {
		applyStylesheet(*this, elem);
	}
	for (u32 root_idx : m_roots) {
		ParentContext ctx;
		ctx.font = "/engine/editor/fonts/JetBrainsMono-Regular.ttf";
		loadResources(*this, root_idx, ctx);
	}
}

InternString InternTable::intern(StringView s) {
	auto iter = map.find(s);
	if (iter.isValid()) return (InternString)iter.value();
	
	InternString id = (InternString)(strings.size() + 1); // 0 is reserved for invalid
	strings.emplace(s, allocator);
	map.insert(strings.back(), (u32)id);
	return id;
}

StringView InternTable::resolve(InternString id) const {
	if (id == InternString::INVALID || (size_t)id > strings.size()) return StringView();
	return strings[(size_t)id - 1];
}

} // namespace Lumix::ui