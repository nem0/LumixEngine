#pragma once

#include "core/string.h"

namespace Lumix {

struct UIToken {
	enum Type {
		UNDEFINED,
		EOF,
		ERROR,

		IDENTIFIER, // element and attribute names
		STRING,     // double-quoted strings
		NUMBER,     // integer or float
		PERCENTAGE, // %
		EM,         // em
		EQUALS,     // =
		LBRACE,     // {
		RBRACE,     // }
		LBRACKET,   // [
		RBRACKET,   // ]
		COLON, 		// :
		SEMICOLON, 	// ;
		DOT,		// .
		COLOR,		// color
		DOLLAR,		// $
	};

	UIToken() : type(UNDEFINED) {}
	UIToken(Type type) : type(type) {}
	UIToken(StringView value, Type type) : type(type), value(value) {}

	operator bool() const { return type != ERROR && type != EOF; }
	bool operator == (const char* rhs) const { return equalStrings(value, rhs); }

	Type type;
	StringView value;
};

struct UITokenizer {
	using Token = UIToken;

	StringView m_document;
	const char* m_token_start;
	const char* m_current;
	const char* m_filename;
	Token m_current_token;

	static bool isWhitespace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
	static bool isDigit(char c) { return c >= '0' && c <= '9'; }
	static bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
	static bool isHexDigit(char c) { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
	static bool isIdentifierStart(char c) { return isLetter(c) || c == '_'; }
	static bool isIdentifierChar(char c) { return isIdentifierStart(c) || isDigit(c) || c == '-'; }

	void skipWhitespaces() {
		while (m_current != m_document.end && isWhitespace(*m_current)) ++m_current;
		if (m_current >= m_document.end - 1 || m_current[0] != '/') return;

		if (m_current[1] == '/') {
			m_current += 2;
			while (m_current != m_document.end && *m_current != '\n') ++m_current;
			if (m_current != m_document.end) ++m_current; // consume \n
			skipWhitespaces();
		} else if (m_current[1] == '*') {
			m_current += 2;
			while (m_current < m_document.end) {
				if (m_current[0] == '*' && m_current < m_document.end - 1 && m_current[1] == '/') {
					m_current += 2;
					break;
				}
				++m_current;
			}
			skipWhitespaces();
		}
	}

	Token makeToken(UIToken::Type type) {
		Token res;
		res.type = type;
		res.value.begin = m_token_start;
		res.value.end = m_current;
		if (type == UIToken::STRING) {
			++res.value.begin;
			--res.value.end;
		}
		return res;
	}

	char advance() {
		ASSERT(m_current < m_document.end);
		char c = *m_current;
		++m_current;
		return c;
	}

	char peekChar() {
		if (m_current == m_document.end) return 0;
		return *m_current;
	}

	char peekNextChar() {
		if (m_current + 1 >= m_document.end) return 0;
		return *(m_current + 1);
	}

	Token numericToken() {
		UIToken::Type type = UIToken::NUMBER;
		while (isDigit(peekChar())) advance();
		if (peekChar() == '.') {
			advance();
			if (!isDigit(peekChar())) return makeToken(UIToken::ERROR);
			while (isDigit(peekChar())) advance();
		}
		char peek = peekChar();
		if (peek == '%') {
			advance();
			type = UIToken::PERCENTAGE;
		} else if (peek == 'e' && peekNextChar() == 'm') {
			advance();
			advance();
			type = UIToken::EM;
		}
		if (m_current == m_document.end || isWhitespace(*m_current) || *m_current == ';' || *m_current == ']') return makeToken(type);
		return makeToken(UIToken::ERROR);
	}

	Token stringToken() {
		while (m_current != m_document.end && *m_current != '"') {
			if (*m_current == '\\' && m_current + 1 != m_document.end) {
				advance(); // skip backslash
			}
			advance();
		}
		if (m_current == m_document.end) return makeToken(UIToken::ERROR);
		advance(); // skip closing "
		return makeToken(UIToken::STRING);
	}

	Token identifierToken() {
		while (isIdentifierChar(peekChar())) advance();
		return makeToken(UIToken::IDENTIFIER);
	}

	Token colorToken() {
		for (int i = 0; i < 6; ++i) {
			if (!isHexDigit(peekChar())) return makeToken(UIToken::ERROR);
			advance();
		}
		if (m_current != m_document.end && !isWhitespace(*m_current) && *m_current != ';' && *m_current != ']') return makeToken(UIToken::ERROR);
		return makeToken(UIToken::COLOR);
	}

	Token consumeToken() {
		Token t = m_current_token;
		m_current_token = nextToken();
		return t;
	}

	Token nextToken() {
		skipWhitespaces();
		m_token_start = m_current;
		if (m_current == m_document.end) return makeToken(UIToken::EOF);

		char c = advance();
		if (isIdentifierStart(c)) return identifierToken();
		if (isDigit(c)) return numericToken();

		switch (c) {
			case '#': return colorToken();
			case '"': return stringToken();
			case '=': return makeToken(UIToken::EQUALS);
			case '{': return makeToken(UIToken::LBRACE);
			case '}': return makeToken(UIToken::RBRACE);
			case '[': return makeToken(UIToken::LBRACKET);
			case ']': return makeToken(UIToken::RBRACKET);
			case ':': return makeToken(UIToken::COLON);
			case ';': return makeToken(UIToken::SEMICOLON);
			case '.': return makeToken(UIToken::DOT);
			case '$': return makeToken(UIToken::DOLLAR);
		}

		return makeToken(UIToken::ERROR);
	}

	Token peekToken() {
		return m_current_token;
	}

	int getLine() const {
		int line = 1;
		const char* p = m_document.begin;
		while (p < m_current) {
			if (*p == '\n') ++line;
			++p;
		}
		return line;
	}

	int getLine(StringView location) const {
		int line = 1;
		const char* p = m_document.begin;
		while (p < location.begin) {
			if (*p == '\n') ++line;
			++p;
		}
		return line;
	}
};

} // namespace Lumix
