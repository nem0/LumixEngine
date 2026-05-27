#pragma once

#include "token.h"
#include "string_utils.h"

struct Tokenizer {
	using TokenType = Token::Type;

	ls_string_view m_document;
	const char* m_start_token = nullptr;
	const char* m_current = nullptr;
	i32 m_line = 1;
	i32 m_column = 1;
	i32 m_start_line = 1;
	i32 m_start_column = 1;
	ls_string_view m_source_name;
	Token m_current_token;

	void init(ls_string_view document, ls_string_view source_name = {}) {
		m_document = document;
		m_source_name = source_name;
		m_current = data(document);
		m_line = 1;
		m_column = 1;
		m_current_token = nextToken();
	}

	static bool isDigit(char c) { return c >= '0' && c <= '9'; }
	static bool isIdentifierStart(char c) { return isLetter(c) || c == '_'; }
	static bool isIdentifierChar(char c) { return isIdentifierStart(c) || isDigit(c); }

	void skipWhitespaces() {
		for (;;) {
			const char* end = data(m_document) + size(m_document);
			while (m_current != end && isWhitespace(*m_current)) advance();
			if (m_current == end || m_current + 1 >= end || m_current[0] != '/' || m_current[1] != '/') return;
			advance();
			advance();
			while (m_current != end && *m_current != '\n') advance();
		}
	}

	Token makeToken(TokenType type) const {
		Token res;
		res.type = type;
		res.value = ls_string_view{m_start_token, m_current};
		res.source_name = m_source_name;
		res.line = m_start_line;
		res.column = m_start_column;
		return res;
	}

	char advance() {
		ASSERT(m_current < data(m_document) + size(m_document));
		const char c = *m_current;
		++m_current;
		if (c == '\n') {
			++m_line;
			m_column = 1;
		}
		else {
			++m_column;
		}
		return c;
	}

	char peekChar() const { return m_current == data(m_document) + size(m_document) ? 0 : *m_current; }
	char peekNextChar() const { return m_current + 1 >= data(m_document) + size(m_document) ? 0 : m_current[1]; }

	bool match(char c) {
		if (m_current == data(m_document) + size(m_document) || *m_current != c) return false;
		advance();
		return true;
	}

	Token numberToken() {
		while (isDigit(peekChar())) advance();
		if (peekChar() == '.' && isDigit(peekNextChar())) {
			advance();
			if (!isDigit(peekChar())) return makeToken(Token::ERROR);
			while (isDigit(peekChar())) advance();
		}
		return makeToken(Token::NUMBER);
	}

	Token stringToken() {
		const char* value_begin = m_current;
		while (m_current != data(m_document) + size(m_document) && peekChar() != '"') {
			if (peekChar() == '\n') return makeToken(Token::ERROR);
			advance();
		}
		if (m_current == data(m_document) + size(m_document)) return makeToken(Token::ERROR);
		const char* value_end = m_current;
		advance();
		Token res = makeToken(Token::STRING);
		res.value = ls_string_view{value_begin, value_end};
		return res;
	}

	Token checkKeyword(const char* rest, u32 start, u32 len, TokenType type) {
		if (u32(m_current - m_start_token) != start + len) return makeToken(Token::IDENTIFIER);
		if (memcmp(m_start_token + start, rest, len) != 0) return makeToken(Token::IDENTIFIER);
		return makeToken(type);
	}

	Token identifierOrKeywordToken() {
		while (isIdentifierChar(peekChar())) advance();
		const ls_string_view ident{m_start_token, m_current};
		if (equalStrings(ident, "break")) return makeToken(Token::BREAK);
		if (equalStrings(ident, "continue")) return makeToken(Token::CONTINUE);
		switch (m_start_token[0]) {
			case 'a': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case 'n': return checkKeyword("d", 2, 1, Token::AND);
					case 's': return checkKeyword("", 2, 0, Token::AS);
				}
				break;
			}
			case 'b': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case 'o': return checkKeyword("ol", 2, 2, Token::BOOL);
					case 'r': return checkKeyword("eak", 2, 3, Token::BREAK);
				}
				break;
			}
			case 'c': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case 'a': return checkKeyword("se", 2, 2, Token::CASE);
					case 'o': return checkKeyword("nst", 2, 3, Token::CONST);
				}
				return checkKeyword("ontinue", 1, 7, Token::CONTINUE);
			}
			case 'd': return checkKeyword("efer", 1, 4, Token::DEFER);
			case 'e': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case 'l': return checkKeyword("se", 2, 2, Token::ELSE);
					case 'n': return checkKeyword("um", 2, 2, Token::ENUM);
				}
				break;
			}
			case 'f': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case '3': return checkKeyword("2", 2, 1, Token::F32);
					case '6': return checkKeyword("4", 2, 1, Token::F64);
					case 'a': return checkKeyword("lse", 2, 3, Token::FALSE);
					case 'o': return checkKeyword("r", 2, 1, Token::FOR);
					case 'n': return checkKeyword("", 2, 0, Token::FN);
				}
				break;
			}
			case 'i': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case '8': return checkKeyword("", 2, 0, Token::I8);
					case '1': return checkKeyword("6", 2, 1, Token::I16);
					case '3': return checkKeyword("2", 2, 1, Token::I32);
					case '6': return checkKeyword("4", 2, 1, Token::I64);
					case 'f': return checkKeyword("", 2, 0, Token::IF);
					case 'm': return checkKeyword("port", 2, 4, Token::IMPORT);
				}
				break;
			}
			case 'm': return checkKeyword("atch", 1, 4, Token::MATCH);
			case 'n': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case 'o': return checkKeyword("t", 2, 1, Token::NOT);
					case 'u': return checkKeyword("ll", 2, 2, Token::NULL_KW);
				}
				break;
			}
			case 'o': return checkKeyword("r", 1, 1, Token::OR);
			case 'r': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				if (u32(m_current - m_start_token) == 3) return checkKeyword("ef", 1, 2, Token::REF);
				return checkKeyword("eturn", 1, 5, Token::RETURN);
			}
			case 's': return checkKeyword("truct", 1, 5, Token::STRUCT);
			case 't': return checkKeyword("rue", 1, 3, Token::TRUE);
			case 'u': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case '8': return checkKeyword("", 2, 0, Token::U8);
					case '1': return checkKeyword("6", 2, 1, Token::U16);
					case '3': return checkKeyword("2", 2, 1, Token::U32);
					case '6': return checkKeyword("4", 2, 1, Token::U64);
				}
				break;
			}
			case 'v': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case 'a': return checkKeyword("r", 2, 1, Token::VAR);
					case 'o': return checkKeyword("id", 2, 2, Token::VOID);
				}
				break;
			}
			case 'w': return checkKeyword("hile", 1, 4, Token::WHILE);
		}
		return makeToken(Token::IDENTIFIER);
	}

	Token nextToken() {
		skipWhitespaces();
		m_start_token = m_current;
		m_start_line = m_line;
		m_start_column = m_column;
		if (m_current == data(m_document) + size(m_document)) return makeToken(Token::END_OF_FILE);

		const char c = advance();
		if (isDigit(c)) return numberToken();
		if (isIdentifierStart(c)) return identifierOrKeywordToken();

		switch (c) {
			case '"': return stringToken();
			case '(': return makeToken(Token::LEFT_PAREN);
			case ')': return makeToken(Token::RIGHT_PAREN);
			case '{': return makeToken(Token::LEFT_BRACE);
			case '}': return makeToken(Token::RIGHT_BRACE);
			case '[': return makeToken(Token::LEFT_BRACKET);
			case ']': return makeToken(Token::RIGHT_BRACKET);
			case ';': return makeToken(Token::SEMICOLON);
			case ':': return makeToken(Token::COLON);
			case ',': return makeToken(Token::COMMA);
			case '.': return makeToken(match('.') ? Token::RANGE : Token::DOT);
			case '?': return makeToken(Token::QUESTION);
			case '+': {
				if (match('=')) return makeToken(Token::PLUS_EQUAL);
				if (match('+')) return makeToken(Token::PLUS_PLUS);
				return makeToken(Token::PLUS);
			}
			case '-': {
				if (match('=')) return makeToken(Token::MINUS_EQUAL);
				if (match('-')) return makeToken(Token::MINUS_MINUS);
				return makeToken(Token::MINUS);
			}
			case '*': return makeToken(match('=') ? Token::STAR_EQUAL : Token::STAR);
			case '/': return makeToken(match('=') ? Token::SLASH_EQUAL : Token::SLASH);
			case '%': return makeToken(Token::PERCENT);
			case '=': return makeToken(match('=') ? Token::EQUAL_EQUAL : Token::EQUAL);
			case '!': return makeToken(match('=') ? Token::BANG_EQUAL : Token::ERROR);
			case '>': return makeToken(match('=') ? Token::GT_EQUAL : Token::GT);
			case '<': return makeToken(match('=') ? Token::LT_EQUAL : Token::LT);
		}

		return makeToken(Token::ERROR);
	}

	Token consumeToken() {
		Token res = m_current_token;
		m_current_token = nextToken();
		return res;
	}

	Token peekToken() const { return m_current_token; }
};
