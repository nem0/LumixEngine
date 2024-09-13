#pragma once

#include "core/string.h"

namespace Lumix {

struct Tokenizer {
	struct Token {
		enum Type {
			NONE,
			ERROR,
			EOF,

			NUMBER,
			STRING,
			IDENTIFIER,
			SYMBOL
		};
		Token() : type(NONE) {}
		Token(Type type) : type(type) {}
		Token(StringView value, Type type) : type(type), value(value) {}
	
		operator bool() const { return type != ERROR && type != EOF; }
		bool operator == (const char* rhs) const { return equalStrings(value, rhs); }

		Type type;
		StringView value;
	};

	StringView content;
	const char* cursor;
	const char* filename;

	Tokenizer(StringView content, const char* filename);
	u32 getLine() const;

	// get next token, prints error if there's EOF
	Token nextToken();

	// like tryNextToken(), but prints error if there's token that's not `type`
	Token tryNextToken(Token::Type type);

	// like nextToken, but does NOT print error if there's EOF
	Token tryNextToken();

	// prints in error log line where the error occurred and a cursor pointing to the error e.g.
	// some code with error here
	//                ^
	void logErrorPosition(const char* pos);

	static float toFloat(Token token);

	// if token is a vec2/3/4, put it in `out_vec` and its size in out_size and returns true
	// otherwise prints error and returns false
	[[nodiscard]] bool consumeVector(float* out_vec, u32& out_size);

	// if token is a string, put it in `out` (excluding ") and returns true
	// otherwise prints error and returns false
	[[nodiscard]] bool consume(StringView& out);

	// if token is a number, put it in `out` and returns true
	// otherwise prints error and returns false
	[[nodiscard]] bool consume(int& out);

	// if token is a number, put it in `out` and returns true
	// otherwise prints error and returns false
	[[nodiscard]] bool consume(float& out);

	// if token is a boolean, put it in `out` and returns true
	// otherwise prints error and returns false
	[[nodiscard]] bool consume(bool& out);

	template <int SIZE>
	[[nodiscard]] bool consume(char (&out)[SIZE]) {
		StringView tmp;
		if (!consume(tmp)) return false;
		copyString(out, tmp);
		return true;
	}

	// if `token` == `value` returns true
	// otherwise prints error and returns false
	[[nodiscard]] bool consume(const char* value);

	template <typename... Args>
	[[nodiscard]] bool consume(Args&... args) {
		return (consume(args) && ...);
	}
};

}