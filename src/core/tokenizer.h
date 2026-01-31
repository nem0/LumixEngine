#pragma once

#include "core/string.h"

namespace black {

// tokenize a string inplace, i.e. it does not allocate memory
struct BLACK_CORE_API Tokenizer {
	// token is a string, number, identifier or symbol, tokens are separated by whitespaces
	struct Token {
		enum Type {
			UNDEFINED,
			EOF,
			ERROR, // error token, e.g. string that is not closed

			// number is a sequence of digits, optionally followed by a dot and another sequence of digits
			NUMBER,
			// string is a sequence of characters enclosed in "", token.value does not include quotation marks
			STRING,
			// identifier is a sequence of characters that starts with a letter or _ and is followed by letters, digits or _
			IDENTIFIER,
			// symbol is a single character that is not a letter, digit or _
			SYMBOL
		};
		Token() : type(UNDEFINED) {}
		Token(Type type) : type(type) {}
		Token(StringView value, Type type) : type(type), value(value) {}
	
		operator bool() const { return type != ERROR && type != EOF; }
		bool operator == (const char* rhs) const { return equalStrings(value, rhs); }
		bool operator == (char c) const = delete; // without this token == 'c' would call operator bool

		Type type;
		StringView value;
	};

	// helper to easily get basic types from tokens
	struct Variant {
		Variant() : type(NONE) {}
		enum Type {
			NONE,
			NUMBER, // token.type == NUMBER
			STRING, // token.type == STRING
			VEC2, // in format {x, y}
			VEC3, // {x, y, z}
			VEC4 // {x, y, z, w}
		};

		Type type;
		union {
			float number;
			StringView string;
			float vector[4];
		};
	};

	Tokenizer(StringView content, const char* filename);
	
	// get current line, starts from 1
	u32 getLine() const;

	// get next token, prints error if token is invalid (Token::Type == ERROR)
	Token tryNextToken();

	// like tryNextToken, but also prints error if there's EOF
	Token nextToken();

	// like tryNextToken(), but prints error if token.type != `type`
	Token tryNextToken(Token::Type type);

	// prints in error log line where the error occurred and a cursor pointing to the error e.g.
	// some code with error here
	//                ^
	void logErrorPosition(const char* pos);

	static float toFloat(Token token);

	// if token is a vec2/3/4, put it in `out_vec` and its size in `out_size` and returns true
	// otherwise prints error and returns false
	[[nodiscard]] bool consumeVector(float* out_vec, u32& out_size);

	// if token is a string or identifier, put it in `out` (excluding ") and returns true
	// otherwise prints error and returns false
	[[nodiscard]] bool consume(StringView& out);

	// if token is a number, put it in `out` and returns true
	// otherwise prints error and returns false
	[[nodiscard]] bool consume(i32& out);

	// if token is Vec3, put it in `out` and returns true
	// otherwise prints error and returns false
	[[nodiscard]] bool consume(struct Vec3& out);

	// if token is a number, put it in `out` and returns true
	// otherwise prints error and returns false
	[[nodiscard]] bool consume(u32& out);

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

	// if token(s) can be converted to a variant, returns valid variant
	// otherwise prints error and returns invalid variant
	Variant consumeVariant();

	// if token == `value` returns true
	// otherwise prints error and returns false
	[[nodiscard]] bool consume(const char* value);

	template <typename... Args>
	[[nodiscard]] bool consume(Args&... args) {
		return (consume(args) && ...);
	}

	StringView content;
	const char* cursor;
	const char* filename;
};

// description of a single item that can be parsed, see parse() for more info
struct ParseItemDesc {
	ParseItemDesc(const char* name, bool* value) : name(name), bool_value(value), type(BOOL) {}
	ParseItemDesc(const char* name, float* value) : name(name), float_value(value), type(FLOAT) {}
	ParseItemDesc(const char* name, i32* value) : name(name), i32_value(value), type(I32) {}
	ParseItemDesc(const char* name, u32* value) : name(name), u32_value(value), type(U32) {}
	ParseItemDesc(const char* name, StringView* value, bool is_array = false) : name(name), string_value(value), type(is_array ? ARRAY : STRING) {}

	const char* name;
	union {
		bool* bool_value;
		float* float_value;
		i32* i32_value;
		u32* u32_value;
		StringView* string_value;
	};
	enum Type {
		BOOL,
		FLOAT,
		I32,
		U32,
		STRING,
		ARRAY // [ ... ], returns in string_value
	} type;
};

// tokenize `content` and parse it according to `descs`
// `content` must be a sequence of key = value
// prints error if there's unexpected token
// uses linear search for `descs`, so it's better to have small number of items
BLACK_CORE_API bool parse(StringView content, const char* path, Span<const ParseItemDesc> descs);

}