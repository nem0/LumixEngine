#include "tokenizer.h"
#include "core/log.h"
#include "core/crt.h"
#include "core/math.h"

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
	if (t.type == Token::EOF) {
		logError(filename, "(", getLine(), "): unexpected end of file."); 
	}
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
		++cursor;
		token.value.begin = cursor;
		while (cursor < content.end && *cursor != '`') {
			++cursor;
		}
		if (cursor >= content.end) {
			logError(filename, "(", getLine(), "): unexpected end of file.");
			return Token::ERROR;
		}
		token.value.end = cursor;
		++cursor;
		return token;
	}

	if (*cursor == '"') {
		// string
		Token token(Token::STRING);
		++cursor;
		token.value.begin = cursor;
		while (cursor < content.end && *cursor != '"') {
			++cursor;
		}
		if (cursor >= content.end) {
			logError(filename, "(", getLine(), "): unexpected end of file.");
			return Token::ERROR;
		}
		token.value.end = cursor;
		++cursor;
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

bool Tokenizer::consume(Vec3& out) {
	float tmp[4];
	u32 size;
	if (!consumeVector(tmp, size)) return false;
	if (size != 3) {
		logError(filename, "(", getLine(), "): Vec3 expected.");
		logErrorPosition(cursor);
		return false;
	}
	memcpy(&out, tmp, sizeof(out));
	return true;
}

bool Tokenizer::consume(i32& out) {
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

bool Tokenizer::consume(u32& out) {
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
	if (token.type != Token::STRING && token.type != Token::IDENTIFIER) {
		logError(filename, "(", getLine(), "): string expected.");
		logErrorPosition(token.value.begin);
		return false;
	}
	out = token.value;
	return true;
}	

bool parse(StringView content, const char* path, Span<const ParseItemDesc> descs) {
	Tokenizer t(content, path);
	for (;;) {
		Tokenizer::Token token = t.tryNextToken(Tokenizer::Token::IDENTIFIER);
		if (!token) return true;

		bool found_any = false;
		for (const ParseItemDesc& desc : descs) {
			if (token == desc.name) {
				found_any = true;
				switch(desc.type) {
					case ParseItemDesc::BOOL:
						if (!t.consume("=", *desc.bool_value)) return false;
						break;
					case ParseItemDesc::I32:
						if (!t.consume("=", *desc.i32_value)) return false;
						break;
					case ParseItemDesc::U32:
						if (!t.consume("=", *desc.u32_value)) return false;
						break;
					case ParseItemDesc::STRING:
						if (!t.consume("=", *desc.string_value)) return false;
						break;
					case ParseItemDesc::FLOAT: 
						if (!t.consume("=", *desc.float_value)) return false;
						break;
					case ParseItemDesc::ARRAY: {
						if (!t.consume("=")) return false;
						Tokenizer::Token next = t.nextToken();
						if (!next) return false;
						if (next.value[0] != '[') {
							logError(t.filename, "(", t.getLine(), "): '[' expected, got ", next.value);
							t.logErrorPosition(next.value.begin);
							return false;
						}
						*desc.string_value = next.value;
						u32 depth = 1;
						for (;;) {
							next = t.nextToken();
							if (!next) return false;
							if (next.value[0] == '[') ++depth;
							else if (next.value[0] == ']') {
								--depth;
								if (depth == 0) {
									desc.string_value->end = next.value.end;
									break;
								}
							}
						}
						break;
					}
				}
			}
		}
		if (!found_any) {
			logError(t.filename, "(", t.getLine(), "): Unknown token ", token.value);
			t.logErrorPosition(token.value.begin);
		}
	}
}

} // namespace