#include "tokenizer.h"
#include "core/log.h"
#include "core/crt.h"

namespace Lumix {

Tokenizer::Tokenizer(StringView content, const char* filename)
	: content(content)
	, filename(filename)
{
	cursor = content.begin;
}

u32 Tokenizer::getLine() const {
	u32 line = 1;
	for (const char* c = content.begin; c < cursor; ++c) {
		if (*c == '\n') ++line;
	}
	return line;
}

static bool isSpace(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

static bool isIdentifierChar(char c) {
	return c == '_' || isLetter(c) || isNumeric(c);
}

Tokenizer::Token Tokenizer::nextToken() {
	Token t = tryNextToken();
	if (t.type == Token::EOF) logError(filename, "(", getLine(), "): unexpected end of file."); 
	return t;
}

Tokenizer::Token Tokenizer::tryNextToken(Token::Type type) {
	Token token = tryNextToken();
	if (!token) return token;
	if (token.type != type) {
		logError(filename, "(", getLine(), "): unexpected token ", token.value);
		logErrorPosition(token.value.begin);
		return Token::ERROR;
	}
	return token;
}

Tokenizer::Token Tokenizer::tryNextToken() {
	// skip whitespaces
	while (cursor < content.end && isSpace(*cursor)) {
		++cursor;
	}
	if (cursor >= content.end) return Token::EOF;

	if (*cursor == '`') {
		// string
		Token token(Token::STRING);
		token.value.begin = cursor;
		++cursor;
		while (cursor < content.end && *cursor != '`') {
			++cursor;
		}
		if (cursor >= content.end) {
			logError(filename, "(", getLine(), "): unexpected end of file.");
			return Token::ERROR;
		}
		++cursor;
		token.value.end = cursor;
		return token;
	}

	if (*cursor == '"') {
		// string
		Token token(Token::STRING);
		token.value.begin = cursor;
		++cursor;
		while (cursor < content.end && *cursor != '"') {
			++cursor;
		}
		if (cursor >= content.end) {
			logError(filename, "(", getLine(), "): unexpected end of file.");
			return Token::ERROR;
		}
		++cursor;
		token.value.end = cursor;
		return token;
	}


	bool is_negative_num = false;
	if (*cursor == '-') {
		++cursor;
		if (cursor >= content.end || !isNumeric(*cursor)) {
			Token token(Token::SYMBOL);
			token.value.begin = cursor - 1;
			token.value.end = cursor;
			return token;
		}
		is_negative_num = true;
	}

	if (isNumeric(*cursor)) {
		// number
		Token token(Token::NUMBER);
		token.value.begin = is_negative_num ? cursor - 1 : cursor;
		while (cursor < content.end && isNumeric(*cursor)) {
			++cursor;
		}
		if (cursor < content.end) {
			// decimal
			if (*cursor == '.') {
				++cursor;
				while (cursor < content.end && isNumeric(*cursor)) {
					++cursor;
				}
			}
			if (cursor < content.end && isIdentifierChar(*cursor)) {
				logError(filename, "(", getLine(), "): unexpected character ", *cursor);
				logErrorPosition(cursor);
				return Token::ERROR;
			}
		}
		token.value.end = cursor;
		return token;
	}

	if (!isIdentifierChar(*cursor)) {
		// symbol
		Token token(Token::SYMBOL);
		token.value.begin = cursor;
		++cursor;
		token.value.end = cursor;
		return token;
	}

	// identifier
	Token token(Token::IDENTIFIER);
	token.value.begin = cursor;
	while (cursor < content.end && isIdentifierChar(*cursor)) {
		++cursor;
	}
	token.value.end = cursor;
	return token;
}

void Tokenizer::logErrorPosition(const char* pos ) {
	ASSERT(pos >= content.begin && pos <= content.end);
	StringView line;
	line.begin = pos;
	line.end = pos;
	while (line.begin > content.begin && line.begin[-1] != '\n') --line.begin;
	while (line.end < content.end && *line.end != '\n') ++line.end;
	logError(line);
	char tmp[1024];
	const u32 offset = u32(pos - line.begin);
	if (offset + 1 < lengthOf(tmp)) {
		for (u32 i = 0; i < offset; ++i) tmp[i] = ' ';
		tmp[offset] = '^';
		tmp[offset + 1] = '\0';
		logError(tmp);
	}
}

float Tokenizer::toFloat(Token token) {
	ASSERT(token.type == Token::NUMBER);
	char tmp[64];
	copyString(tmp, token.value);
	return (float)atof(tmp);
}

bool Tokenizer::consume(bool& out) {
	Token tmp;

	Token token = nextToken();
	if (!token) return false;
	
	if (equalStrings(token.value, "true")) {
		out = true;
		return true;
	} 
	
	if (equalStrings(token.value, "false")) {
		out = false;
		return true;
	}
	
	logError(filename, "(", getLine(), "): boolean expected.");
	logErrorPosition(token.value.begin);
	return false;
}

bool Tokenizer::consume(int& out) {
	Token token = nextToken();
	if (!token) return false;
	
	if (token.type == Token::NUMBER) {
		fromCString(token.value, out);
		return true;
	}

	logError(filename, "(", getLine(), "): number expected.");
	logErrorPosition(token.value.begin);
	return false;
}

bool Tokenizer::consume(float& out) {
	Token token = nextToken();
	if (!token) return false;
	
	if (token.type == Token::NUMBER) {
		char tmp[64];
		copyString(tmp, token.value);
		out = (float)atof(tmp);
		return true;
	}

	logError(filename, "(", getLine(), "): number expected.");
	logErrorPosition(token.value.begin);
	return false;
}

Tokenizer::Variant Tokenizer::consumeVariant() {
	Variant v;
	Token token = nextToken();
	if (!token) return v;

	if (token.type == Token::NUMBER) {
		v.type = Variant::NUMBER;
		fromCString(token.value, v.number);
		return v;
	}

	if (token.type == Token::STRING) {
		v.type = Variant::STRING;
		v.string = token.value;
		return v;
	}

	if (token.value[0] == '{') {
		if (!consume(v.vector[0], ",", v.vector[1])) return {};
		Tokenizer::Token iter = tryNextToken();
		if (iter == "}") {
			v.type = Variant::VEC2;
			return v;
		}
		else if (iter != ",") {
			logError(filename, "(", getLine(), "): expected ',' or '}', got ", iter.value);
			logErrorPosition(iter.value.begin);
			return {};
		}
		if (!consume(v.vector[2])) return {};
		
		iter = tryNextToken();
		if (iter == "}") {
			v.type = Variant::VEC3;
			return v;
		}
		else if (iter != ",") {
			logError(filename, "(", getLine(), "): expected ',' or '}', got ", iter.value);
			logErrorPosition(iter.value.begin);
			return {};
		}
		if (!consume(v.vector[3], "}")) return {};
		v.type = Variant::VEC4;
		return v;
	}

	logError(filename, "(", getLine(), "): unexpected token ", token.value);
	logErrorPosition(token.value.begin);
	return v;
}

bool Tokenizer::consume(const char* literal) {
	Token token = nextToken();
	if (!token) return false;

	if (equalStrings(token.value, literal)) return true;

	logError(filename, "(", getLine(), "): ", literal, " expected.");
	logErrorPosition(token.value.begin);
	return false;
}

bool Tokenizer::consumeVector(float* out_vec, u32& out_size) {
	u32 i;
	for (i = 0; i < 5; ++i) {
		Token value = nextToken();
		if (!value) return false;
		if (i > 0) {
			if (value.value[0] == '}') break;
			if (i == 4) {
				logError(filename, "(", getLine(), "): expected '}'");
				logErrorPosition(value.value.begin);
				return false;
			}
			if (value.value[0] != ',') {
				logError(filename, "(", getLine(), "): expected ','");
				logErrorPosition(value.value.begin);
				return false;
			}
			value = nextToken();
			if (!value) return false;
		}
		else if (value.value[0] == '}') { 
			logError(filename, "(", getLine(), "): expected number");
			logErrorPosition(value.value.begin);
			return false;
		}

		if (value.type != Tokenizer::Token::NUMBER) {
			logError(filename, "(", getLine(), "): expected number");
			logErrorPosition(value.value.begin);
			return false;
		}
		char tmp[64];
		copyString(tmp, value.value);
		out_vec[i] = (float)atof(tmp);
	}
	out_size = i;
	return true;
}

bool Tokenizer::consume(StringView& out) {
	Token token = nextToken();
	if (!token) return false;
	if (token.type != Token::STRING) {
		logError(filename, "(", getLine(), "): string expected.");
		logErrorPosition(token.value.begin);
		return false;
	}
	out = token.value;
	++out.begin;
	--out.end;
	return true;
}	

} // namespace