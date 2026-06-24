#pragma once

#include "capi.h"
#include "exparray.h"
#include "token.h"

struct Expression;

struct ParsedType {
	enum Kind {
		INVALID,
		// Primitive type names are parsed directly instead of going through symbol
		// lookup, so user code cannot shadow core numeric/string types.
		VOID,
		BOOL,
		I8,
		I16,
		I32,
		I64,
		U8,
		U16,
		U32,
		U64,
		F32,
		F64,
		STRING,
		CPTR,
		// Type of values that exist only at compile time and describe runtime values.
		// Example: `comptime Color = enum { Red, Green };` gives Color this type.
		TYPE,
		// Function value or function signature, depending on whether this appears on a
		// symbol or inside parsed type syntax.
		FUNCTION,
		// Qualified type name such as `alias.Type`.
		QUALIFIED,
		// Suffix array syntax, e.g. `T[]`. Nested arrays are represented by nested
		// ArrayParsedType nodes rather than by a lossy array-depth counter.
		ARRAY,
		// Slice syntax such as `T[]`.
		SLICE,
		// Compile-time application in type syntax, e.g. `Array[i32]` or
		// `StaticArray[f32, 16]`.
		BRACKET_TYPE
	};

	explicit ParsedType(Kind kind) : kind(kind) {}
	ParsedType() = default;

	bool is_nullable = false;
	Kind kind = INVALID;
	Token token = {};
};

struct QualifiedParsedType : ParsedType {
	QualifiedParsedType() : ParsedType(QUALIFIED) {}

	ls_string_view qualifier = {};
	ls_string_view name = {};
};

struct FunctionParsedType : ParsedType {
	FunctionParsedType(ls_arena& arena) : ParsedType(FUNCTION), params(arena) {}

	ExpArray<ParsedType*> params;
	ParsedType* return_type = nullptr;
};

struct ArrayParsedType : ParsedType {
	ArrayParsedType() : ParsedType(ARRAY) {}

	ParsedType* element_type = nullptr;
	Expression* size = nullptr;
};

struct SliceParsedType : ParsedType {
	SliceParsedType() : ParsedType(SLICE) {}

	ParsedType* element_type = nullptr;
};

struct BracketTypeParsedType : ParsedType {
	BracketTypeParsedType(ls_arena& arena) : ParsedType(BRACKET_TYPE), args(arena) {}

	ParsedType* callee = nullptr;
	ExpArray<Expression*> args;
};
