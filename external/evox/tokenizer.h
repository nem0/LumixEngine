#pragma once

#include "token.h"
#include "utils.h"

struct Tokenizer {
	using TokenType = Token::Type;

	ex_string_view m_document;
	const char* m_start_token = nullptr;
	const char* m_current = nullptr;
	i32 m_line = 1;
	i32 m_column = 1;
	i32 m_start_line = 1;
	i32 m_start_column = 1;
	ex_string_view m_source_name;
	SourceLocTable& m_src_locs;
	Token m_current_token;

	Tokenizer(SourceLocTable& src_locs)
		: m_src_locs(src_locs) {}

	void init(ex_string_view document, ex_string_view source_name) {
		m_document = document;
		m_source_name = source_name;
		m_current = data(document);
		m_line = 1;
		m_column = 1;
		m_current_token = nextToken();
	}

	static bool isDigit(char c) { return c >= '0' && c <= '9'; }
	static bool isHexDigit(char c) { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
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

	Token makeToken(TokenType type) {
		Token res;
		res.type = type;
		res.value = ex_string_view{m_start_token, m_current - m_start_token};
		if (type != Token::END_OF_FILE) {
			res.src_loc = m_src_locs.add(m_source_name, (u32)m_start_line, (u32)m_start_column);
		}
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

	bool numberDigits(bool hex = false) {
		auto is_number_digit = [hex](char c) { return hex ? isHexDigit(c) : isDigit(c); };
		while (is_number_digit(peekChar())) advance();
		while (peekChar() == '_') {
			advance();
			if (!is_number_digit(peekChar())) return false;
			while (is_number_digit(peekChar())) advance();
		}
		return true;
	}

	Token numberToken() {
		if (m_start_token[0] == '0' && (peekChar() == 'x' || peekChar() == 'X')) {
			advance();
			if (!isHexDigit(peekChar())) return makeToken(Token::ERROR);
			if (!numberDigits(true)) return makeToken(Token::ERROR);
			return makeToken(Token::NUMBER);
		}
		if (!numberDigits()) return makeToken(Token::ERROR);
		if (peekChar() == '.' && isDigit(peekNextChar())) {
			advance();
			if (!numberDigits()) return makeToken(Token::ERROR);
		}
		if (peekChar() == 'e' || peekChar() == 'E') {
			advance();
			if (peekChar() == '+' || peekChar() == '-') advance();
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
		res.value = ex_string_view{value_begin, value_end - value_begin};
		return res;
	}

	Token runeToken() {
		const char* value_begin = m_current;
		while (m_current != data(m_document) + size(m_document) && peekChar() != '\'') {
			if (peekChar() == '\n') return makeToken(Token::ERROR);
			advance();
		}
		if (m_current == data(m_document) + size(m_document)) return makeToken(Token::ERROR);
		const char* value_end = m_current;
		advance();
		Token res = makeToken(Token::RUNE);
		res.value = ex_string_view{value_begin, value_end - value_begin};
		return res;
	}

	Token checkKeyword(const char* rest, u32 start, u32 len, TokenType type) {
		if (u32(m_current - m_start_token) != start + len) return makeToken(Token::IDENTIFIER);
		if (compareMemory(m_start_token + start, rest, len) != 0) return makeToken(Token::IDENTIFIER);
		return makeToken(type);
	}

	Token identifierOrKeywordToken() {
		while (isIdentifierChar(peekChar())) advance();
		const ex_string_view ident{m_start_token, m_current - m_start_token};
		if (equalStrings(ident, "break")) return makeToken(Token::BREAK);
		if (equalStrings(ident, "continue")) return makeToken(Token::CONTINUE);
		switch (m_start_token[0]) {
			case 'a': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				if (u32(m_current - m_start_token) == 3 && compareMemory(m_start_token + 1, "ny", 2) == 0) return makeToken(Token::ANY);
				switch (m_start_token[1]) {
					case 'n': return checkKeyword("d", 2, 1, Token::AND);
					case 's': return checkKeyword("", 2, 0, Token::AS);
					case 'l': return checkKeyword("ignof", 2, 5, Token::ALIGNOF);
				}
				break;
			}
			case 'b': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case 'o': return checkKeyword("ol", 2, 2, Token::BOOL);
					case 'r': return checkKeyword("eak", 2, 3, Token::BREAK);
					case 'y': return checkKeyword("te", 2, 2, Token::BYTE);
				}
				break;
			}
			case 'c': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case 'a': return checkKeyword("se", 2, 2, Token::CASE);
					case 'o': {
						if (u32(m_current - m_start_token) < 3) return makeToken(Token::IDENTIFIER);
						switch (m_start_token[2]) {
							case 'm': return checkKeyword("ptime", 3, 5, Token::COMPTIME);
							case 'n': return checkKeyword("st", 3, 2, Token::CONST);
						}
						break;
					}
					case 'p': return checkKeyword("tr", 2, 2, Token::CPTR);
					case 's': return checkKeyword("tr", 2, 2, Token::CSTR);
				}
				return checkKeyword("ontinue", 1, 7, Token::CONTINUE);
			}
			case 'd': return checkKeyword("efer", 1, 4, Token::DEFER);
			case 'e': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case 'l': return checkKeyword("se", 2, 2, Token::ELSE);
					case 'n': return checkKeyword("um", 2, 2, Token::ENUM);
					case 'x': return checkKeyword("tern", 2, 4, Token::EXTERN);
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
					case 'n': return checkKeyword("", 2, 0, Token::IN_KW);
					case 'm': return checkKeyword("port", 2, 4, Token::IMPORT);
					case 's':
						if (u32(m_current - m_start_token) == 2) return checkKeyword("", 2, 0, Token::IS);
						return checkKeyword("ize", 2, 3, Token::ISIZE);
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
			case 'o':
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				if (m_start_token[1] == 'r') return checkKeyword("r", 1, 1, Token::OR);
				return checkKeyword("perator", 1, 7, Token::OPERATOR);
			case 'r': return checkKeyword("eturn", 1, 5, Token::RETURN);
			case 's': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				if (m_start_token[1] == 'i') return checkKeyword("zeof", 2, 4, Token::SIZEOF);
				return checkKeyword("truct", 1, 5, Token::STRUCT);
				break;
			}
			case 't': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case 'r': return checkKeyword("ue", 2, 2, Token::TRUE);
					case 'y': {
						if (u32(m_current - m_start_token) == 6 && compareMemory(m_start_token + 2, "peof", 4) == 0)
							return makeToken(Token::TYPEOF);
						return checkKeyword("pe", 2, 2, Token::TYPE_KW);
					}
				}
				break;
			}
			case 'u': {
				if (u32(m_current - m_start_token) < 2) return makeToken(Token::IDENTIFIER);
				switch (m_start_token[1]) {
					case 'n': {
						if (u32(m_current - m_start_token) < 3) return makeToken(Token::IDENTIFIER);
						switch (m_start_token[2]) {
							case 'd': return checkKeyword("efined", 3, 6, Token::UNDEFINED);
							case 'r': return checkKeyword("oll", 3, 3, Token::UNROLL);
						}
						break;
					}
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
			case '\'': return runeToken();
			case '(': return makeToken(Token::LEFT_PAREN);
			case ')': return makeToken(Token::RIGHT_PAREN);
			case '{': return makeToken(Token::LEFT_BRACE);
			case '}': return makeToken(Token::RIGHT_BRACE);
			case '[': return makeToken(Token::LEFT_BRACKET);
			case ']': return makeToken(Token::RIGHT_BRACKET);
			case ';': return makeToken(Token::SEMICOLON);
			case ':': return makeToken(match(':') ? Token::DOUBLE_COLON : Token::COLON);
			case ',': return makeToken(Token::COMMA);
			case '.': {
				if (match('.')) {
					if (match('.')) return makeToken(Token::ELLIPSIS);
					return makeToken(match('=') ? Token::RANGE_INCLUSIVE : Token::RANGE);
				}
				return makeToken(Token::DOT);
			}
			case '&': return makeToken(Token::AMPERSAND);
			case '?': return makeToken(Token::QUESTION);
			case '$': return makeToken(Token::DOLLAR);
			case '#': return makeToken(Token::HASH);
			case '|': return makeToken(Token::PIPE);
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
