#include "core/log.h"
#include "core/math.h"
#include "core/os.h"
#include "core/profiler.h"
#include "core/stack_array.h"
#include "core/string.h"
#include "engine/resource_manager.h"
#include "gui/sprite.h"
#include "renderer/draw2d.h"
#include "renderer/font.h"
#include "renderer/texture.h"
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

static bool isInlineTag(Tag tag) {
	return tag == Tag::SPAN || tag == Tag::IMAGE;
}

Tag parseTag(StringView str) {
	const u32 len = (u32)str.size();
	if (len == 0) return Tag::INVALID;
	const char* s = str.begin;

	switch (len) {
		case 3: if (memcmp(s, "box", 3) == 0) return Tag::BOX; break;
		case 4: if (memcmp(s, "span", 4) == 0) return Tag::SPAN; break;
		case 5: if (memcmp(s, "image", 5) == 0) return Tag::IMAGE; break;
	}
	return Tag::INVALID;
}

AttributeName parseAttributeName(StringView str) {
	const u32 len = (u32)str.size();
	if (len == 0) return AttributeName::INVALID;
	const char* s = str.begin;

	switch (s[0]) {
		case 'a':
			if (len == 5 && memcmp(s, "align", 5) == 0) return AttributeName::ALIGN;
			if (len == 11 && memcmp(s, "align-items", 11) == 0) return AttributeName::ALIGN_ITEMS;
			break;
		case 'b':
			if (len == 6 && memcmp(s, "bg-fit", 6) == 0) return AttributeName::BG_FIT;
			if (len == 8) {
				if (memcmp(s, "bg-color", 8) == 0) return AttributeName::BG_COLOR;
				if (memcmp(s, "bg-image", 8) == 0) return AttributeName::BG_IMAGE;
			}
			break;
		case 'c':
			if (len == 5) {
				if (memcmp(s, "class", 5) == 0) return AttributeName::CLASS;
				if (memcmp(s, "color", 5) == 0) return AttributeName::COLOR;
			}
			if (len == 8 && memcmp(s, "clipping", 8) == 0) return AttributeName::CLIPPING;
			break;
		case 'd': if (len == 9 && memcmp(s, "direction", 9) == 0) return AttributeName::DIRECTION; break;
		case 'f':
			if (len == 3 && memcmp(s, "fit", 3) == 0) return AttributeName::FIT;
			if (len == 4 && memcmp(s, "font", 4) == 0) return AttributeName::FONT;
			if (len == 9 && memcmp(s, "font-size", 9) == 0) return AttributeName::FONT_SIZE;
			break;
		case 'g': if (len == 4 && memcmp(s, "grow", 4) == 0) return AttributeName::GROW; break;
		case 'h': if (len == 6 && memcmp(s, "height", 6) == 0) return AttributeName::HEIGHT; break;
		case 'i': if (len == 2 && memcmp(s, "id", 2) == 0) return AttributeName::ID; break;
		case 'j': if (len == 15 && memcmp(s, "justify-content", 15) == 0) return AttributeName::JUSTIFY_CONTENT; break;
		case 'l': if (len == 4 && memcmp(s, "left", 4) == 0) return AttributeName::LEFT; break;
		case 'm':
			if (len == 6 && memcmp(s, "margin", 6) == 0) return AttributeName::MARGIN;
			if (len == 11 && memcmp(s, "margin-left", 11) == 0) return AttributeName::MARGIN_LEFT;
			if (len == 12 && memcmp(s, "margin-right", 12) == 0) return AttributeName::MARGIN_RIGHT;
			if (len == 10 && memcmp(s, "margin-top", 10) == 0) return AttributeName::MARGIN_TOP;
			if (len == 13 && memcmp(s, "margin-bottom", 13) == 0) return AttributeName::MARGIN_BOTTOM;
			break;
		case 'o':
			if (len == 7 && memcmp(s, "opacity", 7) == 0) return AttributeName::OPACITY;
			if (len == 8 && memcmp(s, "on-click", 8) == 0) return AttributeName::ON_CLICK;
			break;
		case 'p':
			if (len == 7 && memcmp(s, "pivot-x", 7) == 0) return AttributeName::PIVOT_X;
			if (len == 7 && memcmp(s, "pivot-y", 7) == 0) return AttributeName::PIVOT_Y;
			if (len == 7 && memcmp(s, "padding", 7) == 0) return AttributeName::PADDING;
			if (len == 8 && memcmp(s, "position", 8) == 0) return AttributeName::POSITION;
			if (len == 12 && memcmp(s, "padding-left", 12) == 0) return AttributeName::PADDING_LEFT;
			if (len == 13 && memcmp(s, "padding-right", 13) == 0) return AttributeName::PADDING_RIGHT;
			if (len == 14 && memcmp(s, "padding-bottom", 14) == 0) return AttributeName::PADDING_BOTTOM;
			if (len == 11) {
				if (memcmp(s, "padding-top", 11) == 0) return AttributeName::PADDING_TOP;
				if (memcmp(s, "placeholder", 11) == 0) return AttributeName::PLACEHOLDER;
			}
			break;
		case 's': if (len == 3 && memcmp(s, "src", 3) == 0) return AttributeName::SRC; break;
		case 't':
			if (len == 4 && memcmp(s, "text", 4) == 0) return AttributeName::TEXT;
			if (len == 3 && memcmp(s, "top", 3) == 0) return AttributeName::TOP;
			break;
		case 'v': if (len == 7 && memcmp(s, "visible", 7) == 0) return AttributeName::VISIBLE; break;
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

static bool parseBool(StringView value, bool& out) {
	if (value == "true") {
		out = true;
		return true;
	}
	if (value == "false") {
		out = false;
		return true;
	}
	return false;
}

static bool parseColor(StringView str, Color& out) {
	if ((str.size() != 7 && str.size() != 9) || str[0] != '#') return false;
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
	int a = str.size() == 9 ? hexToInt(str.begin + 7, 2) : 255;
	if (r < 0 || g < 0 || b < 0 || a < 0) return false;
	out = Color(u8(r), u8(g), u8(b), u8(a));
	return true;
}

static bool parseOpacity(StringView value, float& out) {
	float opacity = 0.0f;
	const char* end = fromCString(value, opacity);
	if (!end) return false;

	if (end == value.end - 1 && *end == '%') {
		opacity *= 0.01f;
	}
	else if (end != value.end) {
		return false;
	}

	out = clamp(opacity, 0.f, 1.f);
	return true;
}

static const char* attributeNameToString(AttributeName name) {
	switch (name) {
		case AttributeName::ALIGN: return "align";
		case AttributeName::ALIGN_ITEMS: return "align-items";
		case AttributeName::BG_COLOR: return "bg-color";
		case AttributeName::CLIPPING: return "clipping";
		case AttributeName::COLOR: return "color";
		case AttributeName::DIRECTION: return "direction";
		case AttributeName::JUSTIFY_CONTENT: return "justify-content";
		case AttributeName::OPACITY: return "opacity";
		case AttributeName::VISIBLE: return "visible";
		case AttributeName::WRAP: return "wrap";
		default: return "attribute";
	}
}

static bool parseUnit(StringView str, ParsedUnit& out) {
	out.value = 0.0f;
	out.unit = Unit::PIXELS;
	if (str.empty()) return false;

	if (str == "fit-content") {
		out.value = 0.0f;
		out.unit = Unit::FIT_CONTENT;
		return true;
	}

	Unit unit = Unit::PIXELS;
	StringView number(str);
	u32 len = (u32)str.size();
	if (len > 0 && str.back() == '%') {
		if (len == 1) return false;
		unit = Unit::PERCENT;
		number = StringView(str.begin, str.end - 1);
	} else if (len >= 2 && *(str.end - 2) == 'e' && str.back() == 'm') {
		if (len == 2) return false;
		unit = Unit::EM;
		number = StringView(str.begin, str.end - 2);
	}

	if (number.empty()) return false;

	const char* end = fromCString(number, out.value);
	if (!end || end != number.end) return false;

	out.unit = unit;
	return true;
}

static bool parseTypedAttributeValue(Document& doc, AttributeName name, StringView value, Attribute& out) {
	switch (name) {
		case AttributeName::ALIGN: {
			const u32 len = (u32)value.size();
			if (len == 0) return false;
			const char* s = value.begin;
			switch (len) {
				case 4: if (memcmp(s, "left", 4) == 0) { out.align = Align::LEFT; return true; } break;
				case 5: if (memcmp(s, "right", 5) == 0) { out.align = Align::RIGHT; return true; } break;
				case 6: if (memcmp(s, "center", 6) == 0) { out.align = Align::CENTER; return true; } break;
			}
			return false;
		}
		case AttributeName::ALIGN_ITEMS: {
			const u32 len = (u32)value.size();
			const char* s = value.begin;
			switch (len) {
				case 3: if (memcmp(s, "end", 3) == 0) { out.align_items = AlignItems::END; return true; } break;
				case 5: if (memcmp(s, "start", 5) == 0) { out.align_items = AlignItems::START; return true; } break;
				case 6: if (memcmp(s, "center", 6) == 0) { out.align_items = AlignItems::CENTER; return true; } break;
				case 7: if (memcmp(s, "stretch", 7) == 0) { out.align_items = AlignItems::STRETCH; return true; } break;
			}
			return false;
		}
		case AttributeName::DIRECTION: {
			const u32 len = (u32)value.size();
			const char* s = value.begin;
			switch (len) {
				case 3: if (memcmp(s, "row", 3) == 0) { out.direction = Direction::ROW; return true; } break;
				case 6: if (memcmp(s, "column", 6) == 0) {  out.direction = Direction::COLUMN; return true; } break;
			}
			return false;
		}
		case AttributeName::JUSTIFY_CONTENT: {
			const u32 len = (u32)value.size();
			if (len == 0) return false;
			const char* s = value.begin;
			switch (value[0]) {
				case 's':
					if (len == 5 && memcmp(s, "start", 5) == 0) { out.justify = JustifyContent::START; return true; }
					if (len == 13 && memcmp(s, "space-between", 13) == 0) { out.justify = JustifyContent::SPACE_BETWEEN; return true; }
					if (len == 12 && memcmp(s, "space-around", 12) == 0) { out.justify = JustifyContent::SPACE_AROUND; return true; }
					break;
				case 'c': if (len == 6 && memcmp(s, "center", 6) == 0) { out.justify = JustifyContent::CENTER; return true; } break;
				case 'e': if (len == 3 && memcmp(s, "end", 3) == 0) { out.justify = JustifyContent::END; return true; } break;
			}
			return false;
		}
		case AttributeName::GROW: {
			float grow = 0;
			if (!fromCString(value, grow) || grow < 0 || grow != grow) return false;
			out.grow = grow;
			return true;
		}
		case AttributeName::FONT_SIZE: {
			float font_size = 12.0f;
			const char* end = fromCString(value, font_size);
			if (!end || end != value.end || font_size < 0 || font_size != font_size) return false;
			out.font_size = font_size;
			return true;
		}
		case AttributeName::WIDTH:
		case AttributeName::HEIGHT:
		case AttributeName::TOP:
		case AttributeName::LEFT:
		case AttributeName::PIVOT_X:
		case AttributeName::PIVOT_Y:
		case AttributeName::MARGIN:
		case AttributeName::MARGIN_TOP:
		case AttributeName::MARGIN_RIGHT:
		case AttributeName::MARGIN_BOTTOM:
		case AttributeName::MARGIN_LEFT:
		case AttributeName::PADDING:
		case AttributeName::PADDING_TOP:
		case AttributeName::PADDING_RIGHT:
		case AttributeName::PADDING_BOTTOM:
		case AttributeName::PADDING_LEFT: {
			if (!parseUnit(value, out.parsed_unit)) return false;
			return true;
		}
		case AttributeName::VISIBLE: return parseBool(value, out.visible);
		case AttributeName::WRAP: return parseBool(value, out.wrap);
		case AttributeName::CLIPPING: return parseBool(value, out.clip);
		case AttributeName::OPACITY: return parseOpacity(value, out.opacity);
		case AttributeName::COLOR:
		case AttributeName::BG_COLOR: return parseColor(value, out.color);
		default:
			out.value = value;
			return true;
	}
}

bool Document::parseAttributeValue(Element& elem, AttributeName name, StringView value) {
	if (name == AttributeName::ON_CLICK && elem.tag != Tag::BOX) {
		error(m_tokenizer.m_current_token.value, m_tokenizer, "attribute 'on-click' is allowed only on [box]");
		return false;
	}
	if (name == AttributeName::TEXT) {
		elem.text = value;
		return true;
	}

	Attribute attr;
	attr.type = name;
	attr.source = AttributeSource::ELEMENT;
	if (!parseTypedAttributeValue(*this, name, value, attr)) {
		error(value, m_tokenizer, "invalid value '", value, "' for attribute '", attributeNameToString(name), "'");
		return false;
	}
	elem.attributes.push(attr);
	return true;
}

template <AttributeName N, typename T>
static void upsertAttribute(Element& elem, const T& value, AttributeSource source) {
	const int incoming_prio = sourcePriority(source);
	
	for (const Attribute& attr : elem.attributes) {
		if (attr.type != N) continue;
		if (sourcePriority(attr.source) > incoming_prio) return;
	}
	
	for (i32 i = elem.attributes.size() - 1; i >= 0; --i) {
		const Attribute& attr = elem.attributes[i];
		if (attr.type != N) continue;
		if (sourcePriority(attr.source) <= incoming_prio) {
			elem.attributes.erase(i);
		}
	}
	
	Attribute& attr = elem.attributes.emplace();
	attr.type = N;
	if constexpr (N == AttributeName::DIRECTION) attr.direction = value;
	else if constexpr (N == AttributeName::ALIGN) attr.align = value;
	else if constexpr (N == AttributeName::JUSTIFY_CONTENT) attr.justify = value;
	else if constexpr (N == AttributeName::ALIGN_ITEMS) attr.align_items = value;
	else if constexpr (N == AttributeName::VISIBLE) attr.visible = value;
	else if constexpr (N == AttributeName::WRAP) attr.wrap = value;
	else if constexpr (N == AttributeName::CLIPPING) attr.clip = value;
	else if constexpr (N == AttributeName::OPACITY) attr.opacity = value;
	else if constexpr (N == AttributeName::WIDTH) {
		if (!parseUnit(value, attr.parsed_unit)) {
			logError("invalid value '", value, "' for attribute '", attributeNameToString(N), "'");
		}
	}
	else attr.value = value;
	attr.source = source;
}

static void upsertAttribute(Element& elem, const Attribute& incoming, AttributeSource source) {
	const int incoming_prio = sourcePriority(source);

	for (const Attribute& attr : elem.attributes) {
		if (attr.type != incoming.type) continue;
		if (sourcePriority(attr.source) > incoming_prio) return;
	}

	for (i32 i = elem.attributes.size() - 1; i >= 0; --i) {
		const Attribute& attr = elem.attributes[i];
		if (attr.type != incoming.type) continue;
		if (sourcePriority(attr.source) <= incoming_prio) {
			elem.attributes.erase(i);
		}
	}

	Attribute& attr = elem.attributes.emplace();
	attr = incoming;
	attr.source = source;
}

enum class PositionAnchor : u8 {
	START,
	CENTER,
	END
};

static ParsedUnit toAnchorUnit(PositionAnchor anchor) {
	switch (anchor) {
		case PositionAnchor::START: return {0, Unit::PERCENT};
		case PositionAnchor::CENTER: return {50, Unit::PERCENT};
		case PositionAnchor::END: return {100, Unit::PERCENT};
	}
	return {0, Unit::PERCENT};
}

static bool parsePositionPreset(StringView value, PositionAnchor& horizontal, PositionAnchor& vertical) {
	if (value == "center" || value == "middle-center") {
		horizontal = PositionAnchor::CENTER;
		vertical = PositionAnchor::CENTER;
		return true;
	}
	if (value == "top-left") {
		horizontal = PositionAnchor::START;
		vertical = PositionAnchor::START;
		return true;
	}
	if (value == "top-center") {
		horizontal = PositionAnchor::CENTER;
		vertical = PositionAnchor::START;
		return true;
	}
	if (value == "top-right") {
		horizontal = PositionAnchor::END;
		vertical = PositionAnchor::START;
		return true;
	}
	if (value == "middle-left") {
		horizontal = PositionAnchor::START;
		vertical = PositionAnchor::CENTER;
		return true;
	}
	if (value == "middle-right") {
		horizontal = PositionAnchor::END;
		vertical = PositionAnchor::CENTER;
		return true;
	}
	if (value == "bottom-left") {
		horizontal = PositionAnchor::START;
		vertical = PositionAnchor::END;
		return true;
	}
	if (value == "bottom-center") {
		horizontal = PositionAnchor::CENTER;
		vertical = PositionAnchor::END;
		return true;
	}
	if (value == "bottom-right") {
		horizontal = PositionAnchor::END;
		vertical = PositionAnchor::END;
		return true;
	}
	return false;
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
		while(token.type != Token::LBRACE) {
			switch (token.type) {
				case Token::DOLLAR:
				case Token::IDENTIFIER:
					ASSERT(false); // TODO
					break;
				case Token::COLON:
				case Token::DOT: {
					Token id_token;
					if (!consume(Token::IDENTIFIER, &id_token)) return false;
					rule.classes.push(m_intern_table.intern(StringView(token.value.begin, id_token.value.end)));
					break;
				}
				default:
					error(token.value, m_tokenizer, "expected selector, got ", tokenTypeToString(token.type));
					return false;
			}
			token = m_tokenizer.consumeToken();
		}

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
			attr.type = parseAttributeName(name);
			attr.source = AttributeSource::STYLESHEET;
			if (attr.type == AttributeName::INVALID) {
				error(name, m_tokenizer, "unknown attribute '", name, "'");
				return false;
			}
			if (!parseTypedAttributeValue(*this, attr.type, token.value, attr)) {
				error(token.value, m_tokenizer, "invalid value '", token.value, "' for attribute '", attributeNameToString(attr.type), "'");
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
				Element& elem = m_elements.emplace(Tag::SPAN, m_allocator, *this);
				u32 elem_idx = m_elements.size() - 1;
				if (parent_index != 0xFFFF'FFFF) {
					m_elements[parent_index].children.push(elem_idx);
				} else {
					m_roots.push(elem_idx);
				}
				elem.text = token.value;
				
				// Accumulate consecutive text tokens
				bool is_break = false;
				while (!is_break) {
					Token next = m_tokenizer.peekToken();
					switch (next.type) {
						case Token::TEXT:
							elem.text.end = next.value.end;
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
							elem.text.end = next.value.end;
							if (next.type == Token::STRING) {
								elem.text.end += 1;
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

				Element& elem = m_elements.emplace(tag, m_allocator, *this);
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
					Token peeked = m_tokenizer.peekToken();
					// id
					if (peeked.type == Token::DOLLAR) {
						if (!consume(Token::DOLLAR)) return false;
						if (!consume(Token::IDENTIFIER, &name_token)) return false;	
						elem.id = name_token.value;
						continue;
					}

					// class
					if (peeked.type == Token::DOT) {
						const char* dot = m_tokenizer.peekToken().value.begin;
						if (!consume(Token::DOT)) return false;
						if (!consume(Token::IDENTIFIER, &name_token)) return false;	
						InternString class_id = m_intern_table.intern({dot, name_token.value.end});
						if (!hasClassId(elem.classes, class_id)) {
							elem.classes.push(class_id);
						}
						continue;
					}

					// attribute
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
					if ((name == AttributeName::COLOR || name == AttributeName::BG_COLOR) && value.type != Token::COLOR) {
						error(value.value, m_tokenizer, "invalid value '", value.value, "' for attribute '", attributeNameToString(name), "'");
						return false;
					}
					if (!parseAttributeValue(elem, name, value.value)) return false;
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
				Element& elem = m_elements.emplace(Tag::SPAN, m_allocator, *this);
				u32 elem_idx = m_elements.size() - 1;
				if (parent_index != 0xFFFF'FFFF) {
					m_elements[parent_index].children.push(elem_idx);
				} else {
					m_roots.push(elem_idx);
				}
				if (token.type == Token::STRING) {
					elem.text = StringView{token.value.begin - 1, token.value.end + 1};
				} else {
					elem.text = token.value;
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
							elem.text.end = next.value.end;
							m_tokenizer.consumeToken();
							break;
						default: 
							elem.text.end = next.value.end;
							if (next.type == Token::STRING) {
								elem.text.end += 1;
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

void Element::setVisible(bool show) {
	upsertAttribute<AttributeName::VISIBLE>(*this, show, AttributeSource::ELEMENT);

	if (visible == show) return;
	visible = show;
	m_document.computeLayout(m_document.m_canvas_size);
}

void Element::setBGImage(const Path& path) {
	// since we can't ensure path lifetime, we store the path in intern string table
	InternString s = m_document.m_intern_table.intern(path);
	StringView sv = m_document.m_intern_table.resolve(s);
	upsertAttribute<AttributeName::BG_IMAGE>(*this, sv, AttributeSource::ELEMENT);
	if (m_document.m_resource_manager) {
		bg_sprite = m_document.m_resource_manager->load<Sprite>(path);
	}

}

void Element::setText(StringView v) {
	upsertAttribute<AttributeName::TEXT>(*this, v, AttributeSource::ELEMENT);

	text = v;
	m_document.computeLayout(m_document.m_canvas_size);
}

void Element::setWidth(StringView value) {
	ParsedUnit parsed;
	if (!parseUnit(value, parsed)) {
		logError("invalid value '", value, "' for attribute '", attributeNameToString(AttributeName::WIDTH), "'");
		return;
	}

	upsertAttribute<AttributeName::WIDTH>(*this, value, AttributeSource::ELEMENT);
	width_unit = parsed;
	m_document.computeLayout(m_document.m_canvas_size);
}

static float computeAbsoluteSize(const ParsedUnit& unit, float parent_size, float font_size, float dpi_scale) {
	switch (unit.unit) {
		case Unit::PIXELS: return unit.value * dpi_scale;
		case Unit::PERCENT: return unit.value / 100.0f * parent_size;
		case Unit::EM: return unit.value * font_size;
		case Unit::FIT_CONTENT: return 0.0f; // TODO
		default: return unit.value;
	}
}

static u32 offsetInlineRun(Document& doc, Element& parent, u32 first_span_idx, float x, float y) {
	u32 count = 0;
	for (i32 i = first_span_idx; i < parent.children.size(); ++i) {
		u32 child_idx = parent.children[i];
		Element& child = doc.m_elements[child_idx];
		switch (child.tag) {
			default: return count;
			case Tag::IMAGE:
				if (child.lines.empty()) {
					child.position.x += x;
					child.position.y += y;
				}
				else {
					child.position.x = child.lines[0].pos.x + x;
					child.position.y = child.lines[0].pos.y + y;
				}
				++count;
				break;
			case Tag::SPAN: {
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
				break;
			}
		}
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
		Element& child = doc.m_elements[child_idx];
		if (child.position_mode == PositionMode::ABSOLUTE) continue;
		if (!child.visible) continue;

		if (isInlineTag(doc.m_elements[child_idx].tag)) {
			// inline runs
			i += offsetInlineRun(doc, parent, i, start_x, y) - 1;
			y += child.size.y;
		}
		else { // boxes
			struct BoxRow {
				i32 from;
				i32 to;
				float free_width;
				float height;
			};

			BoxRow row;
			row.from = i;
			row.free_width = parent.size.x - parent.paddings.right - parent.paddings.left;
			row.height = 0;
			
			float prev_margin = 0;
			i32 last_flow_child = -1;
			i32 flow_count = 0;
			for (row.to = i; row.to < parent.children.size(); ++row.to) {
				Element& row_child = doc.m_elements[parent.children[row.to]];
				if (row_child.position_mode == PositionMode::ABSOLUTE) continue;
				if (!row_child.visible) continue;

				float wtmp = row_child.size.x + maximum(prev_margin, row_child.margins.left);
				bool overflow = row.free_width < wtmp + row_child.margins.right && parent.wrap;				
				if (overflow || isInlineTag(row_child.tag)) break;

				row.height = maximum(row.height, row_child.size.y + row_child.margins.top + row_child.margins.bottom);
				row.free_width -= wtmp;
				prev_margin = row_child.margins.right;
				last_flow_child = row.to;
				++flow_count;
			}
			if (flow_count == 0) continue;

			row.free_width -= doc.m_elements[parent.children[last_flow_child]].margins.right;

			float x = start_x;
			float space = 0;
			switch (parent.justify_content) {
				case JustifyContent::END: x += row.free_width; break;
				case JustifyContent::CENTER: x += row.free_width / 2; break;
				case JustifyContent::SPACE_BETWEEN:
					if (flow_count > 1) space = row.free_width / (flow_count - 1);
					break;
				case JustifyContent::SPACE_AROUND: 
					space = row.free_width / (flow_count + 1);
					x += space;
					break;
				default: break;
			}

			prev_margin = 0;
			for (i32 j = row.from; j < row.to; ++j) {
				Element& row_child = doc.m_elements[parent.children[j]];
				if (row_child.position_mode == PositionMode::ABSOLUTE) continue;
				row_child.position.x = x + maximum(prev_margin, row_child.margins.left);
				row_child.position.y = y + row_child.margins.top;
				switch (parent.align_items) {
					case AlignItems::START: break;
					case AlignItems::END: row_child.position.y = y + row.height - row_child.size.y - row_child.margins.bottom; break;
					case AlignItems::CENTER: row_child.position.y = y + (row.height - row_child.size.y) / 2; break;
					case AlignItems::STRETCH: row_child.size.y = row.height - row_child.margins.top - row_child.margins.bottom; break;
				}
				prev_margin = row_child.margins.right;
				x = row_child.position.x + row_child.size.x + space;
			}
			
			y += row.height;
			i = row.to - 1;
		}
	}
}

static void layoutChildrenVertical(Document& doc, Element& parent) {
	float x = parent.position.x + parent.paddings.left;
	float available_w = parent.size.x - parent.paddings.right - parent.paddings.left;

	// First, compute total height used
	float total_height = 0;
	u32 flow_count = 0;
	float prev_margin = 0;
	for (u32 i = 0, n = parent.children.size(); i < n; ++i) {
		u32 child_idx = parent.children[i];
		Element& child = doc.m_elements[child_idx];
		if (child.position_mode == PositionMode::ABSOLUTE) continue;
		if (!child.visible) continue;

		if (isInlineTag(child.tag)) {
			total_height += child.size.y + prev_margin;
			prev_margin = 0;
			++flow_count;
			while (i < n && isInlineTag(doc.m_elements[parent.children[i]].tag)) {
				++i;
			}
			--i;
		}
		else {
			total_height += child.size.y + maximum(prev_margin, child.margins.top);
			prev_margin = child.margins.bottom;
			++flow_count;
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
			if (flow_count > 1) space = remaining / (flow_count - 1);
			break;
		case JustifyContent::SPACE_AROUND:
			space = remaining / (flow_count + 1);
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
		if (child.position_mode == PositionMode::ABSOLUTE) continue;
		if (isInlineTag(child.tag)) {
			i += offsetInlineRun(doc, parent, i, x + prev_margin, y) - 1;
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
	if (!parent.visible) return;
	if (parent.position_mode == PositionMode::RELATIVE) {
		parent.position.x += parent.left;
		parent.position.y += parent.top;
	}
	
	if (parent.direction == Direction::ROW) layoutChildrenHorizontal(doc, parent);
	else layoutChildrenVertical(doc, parent);

	for (u32 child_idx : parent.children) {
		Element& child = doc.m_elements[child_idx];
		if (child.position_mode == PositionMode::ABSOLUTE) {
			const float pivot_x = computeAbsoluteSize(child.pivot_x_unit, child.size.x, child.font_size, doc.m_dpi_scale);
			const float pivot_y = computeAbsoluteSize(child.pivot_y_unit, child.size.y, child.font_size, doc.m_dpi_scale);
			child.position.x = parent.position.x + child.left - pivot_x;
			child.position.y = parent.position.y + child.top - pivot_y;
		}
		layoutChildren(doc, child);
	}
}

struct ParentContext {
	// inheritable
	float font_size = 12;
	StringView font;
	Color color = Color::BLACK;
	Align align = Align::LEFT;
	JustifyContent justify = JustifyContent::START;
	float opacity = 1.0f;
	// not inheritable
	Vec2 size;
	Vec2 content_size;
};

// distribute grow and compute %-based widths
static void computeParentRelativeWidth(Document& doc, Element& elem) {
	if (!elem.visible) return;
	if (elem.children.empty()) return;

	if (elem.direction == Direction::ROW) {
		float sum_grow = 0;
		float remaining_w = elem.size.x - elem.paddings.right - elem.paddings.left;
		float prev_margin = 0;
		for (u32 child_idx : elem.children) {
			const Element& child = doc.m_elements[child_idx];
			if (child.position_mode == PositionMode::ABSOLUTE) continue;
			sum_grow += child.grow;
			remaining_w -= child.size.x + maximum(prev_margin, child.margins.left);
			prev_margin = child.margins.right;
		
		}
		remaining_w -= prev_margin;

		if (sum_grow > 0) {
			if (remaining_w > 0) {
				for (u32 child_idx : elem.children) {
					Element& child = doc.m_elements[child_idx];
					if (child.position_mode == PositionMode::ABSOLUTE) continue;
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
			if (child.position_mode == PositionMode::ABSOLUTE) continue;
			if (isInlineTag(child.tag)) continue;
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
			child.size.x = computeAbsoluteSize(child.width_unit, elem.size.x, child.font_size, doc.m_dpi_scale);
		}
		computeParentRelativeWidth(doc, doc.m_elements[child_idx]);
	}
}

// compute heights of inline run starting at child_idx
static float computeInlineRunHeight(Document& doc, Element& element, i32 child_idx) {
	// TODO images
	i32 end_idx = child_idx;
	while (end_idx < element.children.size() && isInlineTag(doc.m_elements[element.children[end_idx]].tag)) {
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


static void computeFitContentHeights(Document& doc, Element& elem) {
	if (!elem.visible) return;

	for (u32 child_idx : elem.children) {
		computeFitContentHeights(doc, doc.m_elements[child_idx]);
	}
	
	if (elem.height_unit.unit == Unit::FIT_CONTENT && !isInlineTag(elem.tag)) {
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
				if (isInlineTag(child.tag)) {
					sum_height += computeInlineRunHeight(doc, elem, i);
					++i;
					// skip inline run
					while (i < n && isInlineTag(doc.m_elements[elem.children[i]].tag)) ++i;
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
	if (!elem.visible) return;
	if (elem.direction == Direction::COLUMN) {
		float sum_grow = 0;
		float remaining_h = elem.size.y - elem.paddings.top - elem.paddings.bottom;
		float prev_margin = 0;
		for (u32 child_idx : elem.children) {
			const Element& child = doc.m_elements[child_idx];
			if (child.position_mode == PositionMode::ABSOLUTE) continue;
			sum_grow += child.grow;
			remaining_h -= child.size.y + maximum(prev_margin, child.margins.top);
			prev_margin = child.margins.bottom;
		}
		remaining_h -= prev_margin;

		if (sum_grow > 0 && remaining_h > 0) {
			for (u32 child_idx : elem.children) {
				Element& child = doc.m_elements[child_idx];
				if (child.position_mode == PositionMode::ABSOLUTE) continue;
				if (child.grow > 0) {
					child.size.y += remaining_h * child.grow / sum_grow;
				}
			}
		}
	}

	for (u32 child_idx : elem.children) {
		Element& child = doc.m_elements[child_idx];
		if (child.height_unit.unit == Unit::PERCENT) {
			child.size.y = computeAbsoluteSize(child.height_unit, elem.size.y, child.font_size, doc.m_dpi_scale);
		}
		computeParentRelativeHeights(doc, child);
	}
}

static void computeBaseSizes(Document& doc, Element& elem, const ParentContext& parent) {
	elem.left = elem.top = 0;
	elem.position = Vec2(0, 0);
	elem.size = Vec2(0, 0);
	memset(&elem.margins, 0, sizeof(elem.margins));
	memset(&elem.paddings, 0, sizeof(elem.paddings));
	if (!elem.visible) return;

	ParentContext ctx = parent;
	ctx.size = Vec2(0);

	ParsedUnit margin_x_unit[2] = {{0, Unit::PIXELS}, {0, Unit::PIXELS}};
	ParsedUnit margin_y_unit[2] = {{0, Unit::PIXELS}, {0, Unit::PIXELS}};
	ParsedUnit padding_x_unit[2] = {{0, Unit::PIXELS}, {0, Unit::PIXELS}};
	ParsedUnit padding_y_unit[2] = {{0, Unit::PIXELS}, {0, Unit::PIXELS}};
	ParsedUnit left_unit = {0, Unit::PIXELS};
	ParsedUnit top_unit = {0, Unit::PIXELS};

	for (const Attribute& attr : elem.attributes) {
		switch (attr.type) {
			case AttributeName::FONT: ctx.font = attr.value; break;
			case AttributeName::PADDING:
				padding_x_unit[0] = padding_x_unit[1] = attr.parsed_unit;
				padding_y_unit[0] = padding_y_unit[1] = attr.parsed_unit;
				break;
			case AttributeName::MARGIN:
				margin_x_unit[0] = margin_x_unit[1] = attr.parsed_unit;
				margin_y_unit[0] = margin_y_unit[1] = attr.parsed_unit;
				break;
			case AttributeName::PADDING_LEFT: padding_x_unit[0] = attr.parsed_unit; break;
			case AttributeName::PADDING_RIGHT: padding_x_unit[1] = attr.parsed_unit; break;
			case AttributeName::MARGIN_LEFT: margin_x_unit[0] = attr.parsed_unit; break;
			case AttributeName::MARGIN_RIGHT: margin_x_unit[1] = attr.parsed_unit; break;
			case AttributeName::PADDING_TOP: padding_y_unit[0] = attr.parsed_unit; break;
			case AttributeName::PADDING_BOTTOM: padding_y_unit[1] = attr.parsed_unit; break;
			case AttributeName::MARGIN_TOP: margin_y_unit[0] = attr.parsed_unit; break;
			case AttributeName::MARGIN_BOTTOM: margin_y_unit[1] = attr.parsed_unit; break;
			case AttributeName::LEFT: left_unit = attr.parsed_unit; break;
			case AttributeName::TOP: top_unit = attr.parsed_unit; break;
			case AttributeName::POSITION: {
				PositionAnchor horizontal;
				PositionAnchor vertical;
				if (parsePositionPreset(attr.value, horizontal, vertical)) {
					left_unit = toAnchorUnit(horizontal);
					top_unit = toAnchorUnit(vertical);
				}
				break;
			}
			default: break;
		}
	}

	if (elem.tag == Tag::SPAN) {
		if (doc.m_font_manager && elem.font_handle && !elem.text.empty()) {
			elem.size = doc.m_font_manager->measureTextA(elem.font_handle, elem.text);
		}
		return;
	}

	if (elem.width_unit.unit != Unit::FIT_CONTENT) {
		elem.size.x = computeAbsoluteSize(elem.width_unit, elem.position_mode == PositionMode::ABSOLUTE ? parent.size.x : parent.content_size.x, elem.font_size, doc.m_dpi_scale);
	}
	if (elem.height_unit.unit != Unit::FIT_CONTENT) {
		elem.size.y = computeAbsoluteSize(elem.height_unit, elem.position_mode == PositionMode::ABSOLUTE ? parent.size.y : parent.content_size.y, elem.font_size, doc.m_dpi_scale);
	}
	elem.margins.left = computeAbsoluteSize(margin_x_unit[0], parent.content_size.x, elem.font_size, doc.m_dpi_scale);
	elem.margins.right = computeAbsoluteSize(margin_x_unit[1], parent.content_size.x, elem.font_size, doc.m_dpi_scale);
	elem.margins.top = computeAbsoluteSize(margin_y_unit[0], parent.content_size.y, elem.font_size, doc.m_dpi_scale);
	elem.margins.bottom = computeAbsoluteSize(margin_y_unit[1], parent.content_size.y, elem.font_size, doc.m_dpi_scale);
	elem.paddings.left = computeAbsoluteSize(padding_x_unit[0], parent.content_size.x, elem.font_size, doc.m_dpi_scale);
	elem.paddings.right = computeAbsoluteSize(padding_x_unit[1], parent.content_size.x, elem.font_size, doc.m_dpi_scale);
	elem.paddings.top = computeAbsoluteSize(padding_y_unit[0], parent.content_size.y, elem.font_size, doc.m_dpi_scale);
	elem.paddings.bottom = computeAbsoluteSize(padding_y_unit[1], parent.content_size.y, elem.font_size, doc.m_dpi_scale);
	elem.left = computeAbsoluteSize(left_unit, parent.size.x, elem.font_size, doc.m_dpi_scale);
	elem.top = computeAbsoluteSize(top_unit, parent.size.y, elem.font_size, doc.m_dpi_scale);

	ctx.size = elem.size;
	ctx.content_size = elem.size - Vec2(elem.paddings.left + elem.paddings.right, elem.paddings.top + elem.paddings.bottom);
	for (u32 child_idx : elem.children) {
		computeBaseSizes(doc, doc.m_elements[child_idx], ctx);
	}

	// bottom-up size computation	
	// fit-content
	bool is_row = elem.direction == Direction::ROW;
	if (elem.width_unit.unit == Unit::FIT_CONTENT) {
		if (elem.tag == Tag::IMAGE) {
			if (doc.m_image_manager && elem.image_handle && doc.m_image_manager->isReady(elem.image_handle)) {
				const Vec2 intrinsic_size = doc.m_image_manager->getIntrinsicSize(elem.image_handle);
				if (elem.height_unit.unit != Unit::FIT_CONTENT && intrinsic_size.y > 0) {
					elem.size.x = elem.size.y * intrinsic_size.x / intrinsic_size.y;
				}
				else {
					elem.size.x = intrinsic_size.x;
				}
			}
		}
		else if (is_row) {
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
				if (isInlineTag(child.tag)) {
					// TODO margins
					child_width = 0;
					while (i < n && isInlineTag(doc.m_elements[elem.children[i]].tag)) {
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

	if (elem.height_unit.unit == Unit::FIT_CONTENT && elem.tag == Tag::IMAGE) {
		if (doc.m_image_manager && elem.image_handle && doc.m_image_manager->isReady(elem.image_handle)) {
			const Vec2 intrinsic_size = doc.m_image_manager->getIntrinsicSize(elem.image_handle);
			if (elem.width_unit.unit != Unit::FIT_CONTENT && intrinsic_size.x > 0) {
				elem.size.y = elem.size.x * intrinsic_size.y / intrinsic_size.x;
			}
			else {
				elem.size.y = intrinsic_size.y;
			}
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

static void applyStylesheet(Document& doc, u32 element_index, const ParentContext& parent) {
	Element& elem = doc.m_elements[element_index];
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
				upsertAttribute(elem, attr, AttributeSource::STYLESHEET);
			}
		}
	}

	ParentContext ctx = parent;
	float local_opacity = 1.0f;
	elem.clipping = false;
	for (const Attribute& attr : elem.attributes) {
		switch (attr.type) {
			case AttributeName::POSITION: {
				PositionAnchor horizontal;
				PositionAnchor vertical;
				if (attr.value == "absolute") {
					elem.position_mode = PositionMode::ABSOLUTE;
				}
				else if (parsePositionPreset(attr.value, horizontal, vertical)) {
					elem.position_mode = PositionMode::ABSOLUTE;
					elem.pivot_x_unit = toAnchorUnit(horizontal);
					elem.pivot_y_unit = toAnchorUnit(vertical);
				}
				else {
					elem.position_mode = PositionMode::RELATIVE;
				}
				break;
			}
			case AttributeName::PIVOT_X: elem.pivot_x_unit = attr.parsed_unit; break;
			case AttributeName::PIVOT_Y: elem.pivot_y_unit = attr.parsed_unit; break;
			case AttributeName::WIDTH: elem.width_unit = attr.parsed_unit; break;
			case AttributeName::HEIGHT: elem.height_unit = attr.parsed_unit; break;
			case AttributeName::GROW: elem.grow = attr.grow; break;
			case AttributeName::DIRECTION: {
				elem.direction = attr.direction;
				break;
			}
			case AttributeName::VISIBLE: elem.visible = attr.visible; break;
			case AttributeName::ALIGN: ctx.align = attr.align; break;
			case AttributeName::COLOR:
				elem.color = attr.color;
				ctx.color = elem.color;
				break;
			case AttributeName::OPACITY: local_opacity = attr.opacity; break;
			case AttributeName::CLIPPING: elem.clipping = attr.clip; break;
			case AttributeName::BG_COLOR: elem.bg_color = attr.color; break;
			case AttributeName::TEXT: elem.text = attr.value; break;
			case AttributeName::JUSTIFY_CONTENT: elem.justify_content = attr.justify; break;
			case AttributeName::ALIGN_ITEMS: elem.align_items = attr.align_items; break;
			case AttributeName::FONT_SIZE: ctx.font_size = attr.font_size * doc.m_dpi_scale; break;
			case AttributeName::WRAP: elem.wrap = attr.wrap; break;
			default: break; // TODO remove the default case
		}
	}
	elem.font_size = ctx.font_size;
	elem.color = ctx.color;
	elem.text_align = ctx.align;
	elem.opacity = minimum(1.0f, maximum(0.0f, ctx.opacity * local_opacity));
	ctx.opacity = elem.opacity;

	for (u32 child_idx : elem.children) {
		applyStylesheet(doc, child_idx, ctx);
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
    float baseline = 0, desc = 0;
	for (RowLine& rl : row_lines) {
        if (rl.child->tag == Tag::IMAGE) {
            baseline = maximum(baseline, rl.child->size.y);
        }
        else {
            float asc = doc.m_font_manager->getAscender(rl.child->font_handle);
            float height = doc.m_font_manager->getHeight(rl.child->font_handle);
            baseline = maximum(baseline, asc);
			desc = maximum(desc, height - asc);
        }
    }
    for (RowLine& rl : row_lines) {
        if (rl.child->tag == Tag::IMAGE) {
            rl.line->pos.y = row_y_pos + baseline - rl.child->size.y;
        }
        else {
            rl.line->pos.y = row_y_pos + baseline;
        }
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
    return baseline + desc;
}

static StringView trimLeadingWhitespace(StringView text) {
	while (!text.empty() && isWhitespace(text[0])) {
		text.removePrefix(1);
	}
	return text;
}

// word wrapping and line breaking based on the parent's width and wrap setting.
static void wrapInlineRun(Document& doc, Element& parent, i32 start_span_idx, i32 end_span_idx) {
	float x = 0;
	const float content_width = parent.size.x - parent.paddings.left - parent.paddings.right;
	if (content_width <= 0) return;
	bool wrap_enabled = parent.wrap && parent.size.x > 0;
	// First pass: wrap width
	bool is_prev_space = false;
	for (i32 child_idx = start_span_idx; child_idx < end_span_idx; ++child_idx) {
		Element& child = doc.m_elements[parent.children[child_idx]];
		child.lines.clear();
		if (!child.visible) continue;

		if (child.tag == Tag::IMAGE) {
			if (wrap_enabled && x > 0 && child.size.x > content_width - x) {
				x = 0;
			}
			SpanLine& line = child.lines.emplace();
			line.text = StringView();
			line.pos.x = x;
			line.pos.y = 0;
			line.width = child.size.x;
			x += child.size.x;
			is_prev_space = false;
			continue;
		}

		Element& span = child;
		if (!span.font_handle || span.text.empty()) continue;

		StringView space(" ", 1);
		const float space_w = doc.m_font_manager->measureTextA(span.font_handle, space).x;

		StringView text = span.text;
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
		if (child.tag != Tag::IMAGE) {
			child.size.y = 0;
		}
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
		if (span.tag == Tag::IMAGE) {
			if (!span.lines.empty()) {
				span.position = span.lines[0].pos;
			}
		}
		else {
			span.size.y = row_y;
		}
	}
}

// compute wrapping on word boundaries, fill Element::lines
static void wrapText(Document& doc, Element& elem) {
	elem.lines.clear();
	if (!elem.visible) return;

	if (elem.tag == Tag::SPAN) {
		// TODO top level span without container
		ASSERT(false);
	}

	for (u32 i = 0, n = elem.children.size(); i < n; ++i) {
		Element& child = doc.m_elements[elem.children[i]];
		if (!isInlineTag(child.tag)) {
			wrapText(doc, doc.m_elements[elem.children[i]]);
			continue;
		}

		i32 start_i = i; 
		i32 end_i = i + 1;
		while (end_i < elem.children.size() && isInlineTag(doc.m_elements[elem.children[end_i]].tag)) {
			++end_i;
		}
		i = end_i - 1;

		wrapInlineRun(doc, elem, start_i, end_i);
	}
}

Element* Document::getElementByID(const char* id) {
	if (!id || !id[0]) return nullptr;

	StringView id_view(id);
	if (id[0] == '$') {
		if (!id[1]) return nullptr;
		id_view = StringView(id + 1);
	}

	for (Element& elem : m_elements) {
		if (!elem.id.empty() && elem.id == id_view) {
			return &elem;
		}
	}

	return nullptr;
}

void Document::computeLayout(Vec2 canvas_size) {
	PROFILE_FUNCTION();
	
	os::Timer timer;
	m_layout_duration = 0;
	m_canvas_size = canvas_size;
	ParentContext root_inherit;
	root_inherit.size = canvas_size;
	root_inherit.content_size = canvas_size;
	for (u32 root_idx : m_roots) {
		Element& elem = m_elements[root_idx];
		computeBaseSizes(*this, elem, root_inherit);
		computeParentRelativeWidth(*this, elem);
		wrapText(*this, elem);
		computeFitContentHeights(*this, elem);		
		computeParentRelativeHeights(*this, elem);
	}

	// Layout root elements as if in a panel with direction=column
	float y_offset = 0;
	float prev_bottom_margin = 0;
	for (u32 root_idx : m_roots) {
		Element& root = m_elements[root_idx];
		if (!root.visible) continue;
		if (root.height_unit.unit == Unit::PERCENT) {
			root.size.y = computeAbsoluteSize(root.height_unit, canvas_size.y, root.font_size, m_dpi_scale);
		}
		if (root.position_mode == PositionMode::ABSOLUTE) {
			const float pivot_x = computeAbsoluteSize(root.pivot_x_unit, root.size.x, root.font_size, m_dpi_scale);
			const float pivot_y = computeAbsoluteSize(root.pivot_y_unit, root.size.y, root.font_size, m_dpi_scale);
			root.position.x = root.left - pivot_x;
			root.position.y = root.top - pivot_y;
			continue;
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

static u8 applyOpacityToAlpha(u8 alpha, float opacity) {
	const float a = (float)alpha * opacity;
	return (u8)minimum(255.0f, maximum(0.0f, a));
}

static void renderElement(Draw2D& draw, const Document& doc, u32 element_idx, const Element* parent) {
	const Element& element = *doc.getElement(element_idx);
	if (!element.visible) return;
	Vec2 pos = Vec2(element.position.x, element.position.y);
	Vec2 size = Vec2(element.size.x, element.size.y);
	const bool use_clipping = element.clipping && size.x > 0 && size.y > 0;
	if (use_clipping) {
		draw.pushClipRect(pos, pos + size);
	}

	switch (element.tag) {
		case Tag::IMAGE: {
			if (doc.m_image_manager && element.image_handle && doc.m_image_manager->isReady(element.image_handle)) {
				const u8 alpha = applyOpacityToAlpha(255, element.opacity);
				if (alpha > 0) {
					draw.addImage(static_cast<Texture*>(element.image_handle)->handle, pos, pos + size, Vec2(0, 0), Vec2(1, 1), Color(255, 255, 255, alpha));
				}
			}
			break;
		}
		case Tag::BOX: {
			if (element.bg_sprite) {
				const u8 alpha = applyOpacityToAlpha(255, element.opacity);
				if (alpha > 0) {
					element.bg_sprite->render(draw, pos.x, pos.y, pos.x + size.x, pos.y + size.y, Color(255, 255, 255, alpha));
				}
			}
			else if (element.bg_color.a > 0) {
				Color bg = element.bg_color;
				bg.a = applyOpacityToAlpha(bg.a, element.opacity);
				if (bg.a > 0) {
					draw.addRectFilled(pos, pos + size, bg);
				}
			}
			break;
		}
		case Tag::SPAN: {
			if (element.font_handle && !element.text.empty()) {
				const Font& font = *(const Font*)element.font_handle;
				Color text_color = element.color;
				text_color.a = applyOpacityToAlpha(text_color.a, element.opacity);
				if (text_color.a == 0) break;
				if (!element.lines.empty()) {
					for (const SpanLine& line : element.lines) {
						draw.addText(font, line.pos, text_color, line.text);
					}
				} else {
					// fallback
					Vec2 text_pos = pos + Vec2(0, getAscender(font));
					draw.addText(font, text_pos, text_color, element.text);
				}
			}
			break;
		}
		default: break;
	}

	for (u32 child_idx : element.children) {
		renderElement(draw, doc, child_idx, &element);
	}

	if (use_clipping) {
		draw.popClipRect();
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

static StringView getAttributeValue(const Element& elem, AttributeName name) {
	for (const Attribute& attr : elem.attributes) {
		if (attr.type == name) return attr.value;
	}
	return {};
}

static Element* getActionTargetAt(Document& doc, Vec2 pos) {
	for (u32 root_id : doc.m_roots) {
		Element* root = &doc.m_elements[root_id];
		if (!contains(*root, pos, doc.m_font_manager)) continue;

		StackArray<u32, 16> path(doc.m_allocator);
		path.push(root_id);

		Element* elem = root;
		for (;;) {
			bool found_child = false;
			for (u32 child_id : elem->children) {
				Element* child = &doc.m_elements[child_id];
				if (contains(*child, pos, doc.m_font_manager)) {
					path.push(child_id);
					elem = child;
					found_child = true;
					break;
				}
			}
			if (!found_child) break;
		}

		for (i32 i = (i32)path.size() - 1; i >= 0; --i) {
			Element& candidate = doc.m_elements[path[i]];
			if (candidate.tag != Tag::BOX) continue;
			if (!getAttributeValue(candidate, AttributeName::ON_CLICK).empty()) {
				return &candidate;
			}
		}

		return nullptr;
	}

	return nullptr;
}

void Document::addClass(u32 element_index, StringView classname) {
	StaticString<256> tmp(".", classname);
	addClassRaw(element_index, tmp);
}

void Document::addClassRaw(u32 element_index, StringView classname) {
	if (element_index >= (u32)m_elements.size()) return;
	if (classname.empty()) return;
	
	Element& elem = m_elements[element_index];
	
	InternString class_id = m_intern_table.intern(classname);
	if (hasClassId(elem.classes, class_id)) return;
	
	elem.classes.push(class_id);
	
	recomputeStyles();
	computeLayout(m_canvas_size);
}

void Document::removeClass(u32 element_index, StringView classname) {
	StaticString<256> tmp(".", classname);
	removeClassRaw(element_index, tmp);
}

void Document::removeClassRaw(u32 element_index, StringView classname) {
	if (element_index >= (u32)m_elements.size()) return;
	if (classname.empty()) return;
	
	Element& elem = m_elements[element_index];
	InternString class_id = m_intern_table.intern(classname);
	
	for (i32 i = 0; i < (i32)elem.classes.size(); ++i) {
		if (elem.classes[i] == class_id) {
			elem.classes.erase(i);
			break;
		}
	}
	
	recomputeStyles();
	computeLayout(m_canvas_size);
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

	for (const Attribute& attr : elem.attributes) {
		switch (attr.type) {
			case AttributeName::FONT: ctx.font = attr.value; break;
			case AttributeName::FONT_SIZE:
				elem.font_size = attr.font_size * doc.m_dpi_scale;
				ctx.font_size = elem.font_size;
				break;
			case AttributeName::SRC: {
				if (elem.tag == Tag::IMAGE && doc.m_image_manager && !attr.value.empty()) {
					elem.image_handle = doc.m_image_manager->loadImage(attr.value);
				}
				break;
			}
			case AttributeName::BG_IMAGE: {
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
	m_stylesheet.m_rules.clear();
	m_content = content;
	m_tokenizer.m_filename = filename;
	m_tokenizer.m_document = m_content;
	m_tokenizer.m_current = m_content.c_str();
	m_tokenizer.m_current_token = m_tokenizer.nextToken();
	if (!parseElements(0xFFFF'FFFF)) return false;
	m_stylesheet.buildIndex();
	recomputeStyles();

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
					Element* action_elem = getActionTargetAt(*this, ui_event.position);
					const StringView action = action_elem ? getAttributeValue(*action_elem, AttributeName::ON_CLICK) : StringView();
					if (!action.empty()) {
						Event action_event;
						action_event.type = EventType::ACTION;
						action_event.position = ui_event.position;
						action_event.element_index = action_elem ? u32(action_elem - m_elements.begin()) : 0;
						action_event.key_code = ui_event.key_code;
						action_event.action = action;
						m_events.push(action_event);
					}
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
				StackArray<u32, 16> new_hovered_path(m_allocator);
				for (u32 root_id : m_roots) {
					Element* root = &m_elements[root_id];
					if (!contains(*root, pos, m_font_manager)) continue;

					new_hovered_path.push(root_id);
					Element* elem = root;
					for (;;) {
						bool found_child = false;
						for (u32 child_id : elem->children) {
							Element* child = &m_elements[child_id];
							if (contains(*child, pos, m_font_manager)) {
								new_hovered_path.push(child_id);
								elem = child;
								found_child = true;
								break;
							}
						}
						if (!found_child) break;
					}
					break;
				}

				u32 common_prefix = 0;
				const u32 old_size = (u32)m_hovered_elements.size();
				const u32 new_size = (u32)new_hovered_path.size();
				const u32 min_size = minimum(old_size, new_size);
				while (common_prefix < min_size && m_hovered_elements[common_prefix] == new_hovered_path[common_prefix]) {
					++common_prefix;
				}

				for (i32 i = (i32)old_size - 1; i >= (i32)common_prefix; --i) {
					Event leave_event;
					leave_event.type = EventType::MOUSE_LEAVE;
					leave_event.position = pos;
					leave_event.element_index = m_hovered_elements[i];
					m_events.push(leave_event);
					removeClassRaw(leave_event.element_index, ":hover");
				}

				for (u32 i = common_prefix; i < new_size; ++i) {
					Event enter_event;
					enter_event.type = EventType::MOUSE_ENTER;
					enter_event.position = pos;
					enter_event.element_index = new_hovered_path[i];
					m_events.push(enter_event);
					addClassRaw(enter_event.element_index, ":hover");
				}

				m_hovered_elements.clear();
				for (u32 hovered_idx : new_hovered_path) {
					m_hovered_elements.push(hovered_idx);
				}

				u32 new_hovered_index = new_hovered_path.empty() ? 0xFFFF'FFFF : new_hovered_path.back();

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
		if (m_image_manager && elem.image_handle && !m_image_manager->isReady(elem.image_handle)) return false;
		if (elem.font_handle && !m_font_manager->isReady(elem.font_handle)) return false;
	}
	return true;
}

void Document::setDPIScale(float scale) {
	if (scale <= 0) scale = 1.0f;
	if (m_dpi_scale == scale) return;
	m_dpi_scale = scale;
	recomputeStyles();
	computeLayout(m_canvas_size);
}

void Document::recomputeStyles() {
	for (Element& elem : m_elements) {
		for (i32 i = elem.attributes.size() - 1; i >= 0; --i) {
			if (elem.attributes[i].source == AttributeSource::STYLESHEET) {
				elem.attributes.erase(i);
			}
		}
	}

	ParentContext ctx;
	ctx.font_size = 12.0f * m_dpi_scale;
	ctx.font = "/engine/editor/fonts/JetBrainsMono-Regular.ttf";
	for (u32 root_idx : m_roots) {
		applyStylesheet(*this, root_idx, ctx);
	}
	for (u32 root_idx : m_roots) {
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
