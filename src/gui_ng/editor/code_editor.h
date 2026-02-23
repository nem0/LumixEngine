#pragma once

#include "editor/studio_app.h"
#include "editor/utils.h"

namespace Lumix {

namespace GUITokens {

static inline const u32 token_colors[] = {
	IM_COL32(0xFF, 0x00, 0xFF, 0xff), // EMPTY
	IM_COL32(0xe1, 0xe1, 0xe1, 0xff), // IDENTIFIER
	IM_COL32(0x93, 0xDD, 0xFA, 0xff), // NUMBER
	IM_COL32(0xE5, 0x8A, 0xC9, 0xff), // STRING
	IM_COL32(0xf7, 0xc9, 0x5c, 0xff), // KEYWORD
	IM_COL32(0x4A, 0x90, 0xE2, 0xff), // TAG
	IM_COL32(0xC7, 0x78, 0xDD, 0xff), // ATTRIBUTE
	IM_COL32(0xFF, 0xA9, 0x4D, 0xff), // OPERATOR
	IM_COL32(0x67, 0x6b, 0x6f, 0xff), // COMMENT
	IM_COL32(0x67, 0x6b, 0x6f, 0xff), // COMMENT_MULTI
	IM_COL32(0xFF, 0x6E, 0x59, 0xff), // COLOR
	IM_COL32(0xFF, 0x00, 0xFF, 0xff)  // ERROR
};

enum class TokenType : u8 {
	EMPTY,
	IDENTIFIER,
	NUMBER,
	STRING,
	KEYWORD,
	TAG,
	ATTRIBUTE,
	OPERATOR,
	COMMENT,
	COMMENT_MULTI,
	COLOR,
	ERROR
};

static bool isWhitespace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool isNumeric(char c) { return c >= '0' && c <= '9'; }
static bool isHexDigit(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
static bool isIdentifierStart(char c) { return c == '_' || isLetter(c); }
static bool isIdentifierChar(char c) { return c == '_' || c == '-' || isLetter(c) || isNumeric(c); }

static bool tokenize(const char* str, u32& token_len, u8& token_type, u8 prev_token_type) {
	static const char* tags[] = {
		"panel",
		"image",
		"span",
	};
	static const char* keywords[] = {
		"fit-content",
		"row",
		"column",
		"true",
		"false"
	};
	static const char* attributes[] = {
		"id",
		"class",
		"visible",
		"width",
		"height",
		"margin",
		"padding",
		"align",
		"background-image",
		"background-fit",
		"bg-color",
		"direction",
		"wrap",
		"justify-content",
		"align-items",
		"src",
		"fit",
		"value",
		"color",
		"font",
		"font-size"
	};

	const char* c = str;
	if (!*c) {
		switch (prev_token_type) {
			case (u8)TokenType::COMMENT_MULTI:
				token_type = prev_token_type;
				break;
			default:
				token_type = (u8)TokenType::EMPTY;
				break;
		}
		token_len = 0;
		return false;
	}

	if (prev_token_type == (u8)TokenType::COMMENT_MULTI) {
		token_type = (u8)TokenType::COMMENT;
		while (*c) {
			if (c[0] == '*' && c[1] == '/') {
				c += 2;
				token_len = u32(c - str);
				return *c;
			}
			++c;
		}

		token_type = (u8)TokenType::COMMENT_MULTI;
		token_len = u32(c - str);
		return *c;
	}

	if (isWhitespace(*c)) {
		const char* start = c;
		while (*c && isWhitespace(*c)) ++c;
		token_type = (u8)TokenType::EMPTY;
		token_len = u32(c - start);
		return *c != 0;
	}

	const char* start = c;

	if (*c == '/' && c[1] == '*') {
		token_type = (u8)TokenType::COMMENT_MULTI;
		c += 2;
		while (*c) {
			if (c[0] == '*' && c[1] == '/') {
				c += 2;
				token_type = (u8)TokenType::COMMENT;
				token_len = u32(c - str);
				return *c;
			}
			++c;
		}
		token_type = (u8)TokenType::COMMENT_MULTI;
		token_len = u32(c - str);
		return *c;
	}

	if (*c == '/' && c[1] == '/') {
		token_type = (u8)TokenType::COMMENT;
		while (*c) ++c;
		token_len = u32(c - str);
		return *c;
	}

	if (*c == '"') {
		token_type = (u8)TokenType::STRING;
		++c;
		while (*c && *c != '"') {
			if (*c == '\\') ++c;
			if (*c) ++c;
		}
		if (*c == '"') ++c;
		token_len = u32(c - str);
		return *c;
	}

	if (*c == '#') {
		++c;
		int len = 0;
		while (*c && isHexDigit(*c) && len < 6) { ++c; ++len; }
		if (len == 6) {
			token_type = (u8)TokenType::COLOR;
		} else {
			token_type = (u8)TokenType::ERROR;
		}
		token_len = u32(c - str);
		return *c;
	}

	const char operators[] = "=:{};";
	for (char op : operators) {
		if (*c == op) {
			token_type = (u8)TokenType::OPERATOR;
			token_len = 1;
			return *c;
		}
	}

	if (isNumeric(*c)) {
		token_type = (u8)TokenType::NUMBER;
		while (*c && isNumeric(*c)) ++c;
		if (*c == '.') {
			++c;
			while (*c && isNumeric(*c)) ++c;
		}
		if (*c == '%') ++c;
		else if (*c == 'e' && c[1] == 'm') { c += 2; }
		token_len = u32(c - str);
		return *c;
	}

	if (isIdentifierStart(*c)) {
		while (*c && isIdentifierChar(*c)) ++c;
		token_len = u32(c - str);
		StringView token_view(str, str + token_len);
		for (const char* tag : tags) {
			if (equalStrings(tag, token_view)) {
				token_type = (u8)TokenType::TAG;
				return *c;
			}
		}
		for (const char* kw : keywords) {
			if (equalStrings(kw, token_view)) {
				token_type = (u8)TokenType::KEYWORD;
				return *c;
			}
		}
		for (const char* attr : attributes) {
			if (equalStrings(attr, token_view)) {
				token_type = (u8)TokenType::ATTRIBUTE;
				return *c;
			}
		}
		token_type = (u8)TokenType::IDENTIFIER;
		return *c;
	}

	token_type = (u8)TokenType::ERROR;
	token_len = 1;
	++c;
	return *c;
}

} // namespace GUITokens


inline UniquePtr<CodeEditor> createGUICodeEditor(StudioApp& app) {
	UniquePtr<CodeEditor> editor = createCodeEditor(app);
	editor->setTokenColors(GUITokens::token_colors);
	editor->setTokenizer(&GUITokens::tokenize);
	return editor.move();
}

}