#pragma once

#include "capi.h"
#include "exparray.h"

// Sentinel for "no source location". Used by Token::src_loc, ExIrSourceLoc
// and ex_bytecode_source_map_entry::location_index (the bytecode location
// table is 0-based, so this never collides with a real entry).
static constexpr u32 EX_INVALID_SOURCE_LOC = 0xffffffffu;

// Append-only per-compile table of token source locations. Every token gets a
// unique index into `entries` (no dedup): two tokens can never share a slot.
// The bytecode location table (see ex_bytecode_compile) is a verbatim copy of
// `entries`, so token indices are reused as bytecode location indices. Owned by
// ex_module so the tokenizer, parser diagnostics, checker and IR compiler all
// resolve through it.
struct SourceLocTable {
	struct Entry {
		ex_string_view source_name;
		u32 line = 0;
		u32 column = 0;
	};
	explicit SourceLocTable(ex_arena& arena) : entries(arena) {}

	u32 add(ex_string_view source_name, u32 line, u32 column) {
		Entry entry;
		entry.source_name = source_name;
		entry.line = line;
		entry.column = column;
		entries.push(entry);
		return (u32)(entries.size() - 1);
	}

	ExpArray<Entry> entries;
};

struct Token {
	enum Type {
		END_OF_FILE,
		ERROR,
		IDENTIFIER,
		NUMBER,
		STRING,
		INTERPOLATED_STRING_SEGMENT,
		RUNE,

		LEFT_PAREN,
		RIGHT_PAREN,
		LEFT_BRACE,
		RIGHT_BRACE,
		LEFT_BRACKET,
		RIGHT_BRACKET,
		SEMICOLON,
		COLON,
		DOUBLE_COLON,
		COMMA,
		DOT,
		ELLIPSIS,
		RANGE,
		RANGE_INCLUSIVE,
		QUESTION,
		DOLLAR,
		HASH,
		PIPE,

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
		WHILE,
		FOR,
		UNROLL,
		IN_KW,
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
		I8,
		BOOL,
		I16,
		I32,
		I64,
		U8,
		U16,
		U32,
		U64,
		ISIZE,
		IS,
		F32,
		F64,
		CPTR,
		CSTR,
		BYTE,
		ANY,
		SIZEOF,
		ALIGNOF,
		COMPTIME,
		TYPE_KW,
		TYPEOF,
		AMPERSAND
	};

	Type type = END_OF_FILE;
	ex_string_view value;

	// Index into the per-compile SourceLocTable (token.h). EX_INVALID_SOURCE_LOC
	// when the token has no usable source position (EOF or a synthesized token).
	u32 src_loc = EX_INVALID_SOURCE_LOC;
};

