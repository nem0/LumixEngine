#pragma once

#include "capi.h"

struct Token {
	enum Type {
		END_OF_FILE,
		ERROR,
		IDENTIFIER,
		NUMBER,
		STRING,

		LEFT_PAREN,
		RIGHT_PAREN,
		LEFT_BRACE,
		RIGHT_BRACE,
		LEFT_BRACKET,
		RIGHT_BRACKET,
		SEMICOLON,
		COLON,
		COMMA,
		DOT,
		RANGE,
		QUESTION,

		PLUS,
		MINUS,
		STAR,
		SLASH,
		PERCENT,
		EQUAL,
		PLUS_EQUAL,
		MINUS_EQUAL,
		STAR_EQUAL,
		SLASH_EQUAL,
		PLUS_PLUS,
		MINUS_MINUS,
		EQUAL_EQUAL,
		BANG_EQUAL,
		GT,
		LT,
		GT_EQUAL,
		LT_EQUAL,

		EXTERN,
		OPERATOR,
		STRUCT,
		ENUM,
		FN,
		VAR,
		CONST,
		DEFER,
		RETURN,
		REF,
		WHILE,
		FOR,
		IF,
		ELSE,
		IMPORT,
		MATCH,
		CASE,
		BREAK,
		CONTINUE,
		AS,
		TRUE,
		FALSE,
		NULL_KW,
		UNDEFINED,
		AND,
		OR,
		NOT,
		VOID,
		STRING_KW,
		I8,
		BOOL,
		I16,
		I32,
		I64,
		U8,
		U16,
		U32,
		U64,
		F32,
		F64,
		CPTR,
		COMPTIME,
		TYPE_KW,
	};

	Type type = END_OF_FILE;
	ls_string_view value;
	
	// TODO remove
	ls_string_view source_name = {};
	i32 line = 1;
	i32 column = 1;
};

