#pragma once

#include "capi.h"
#include "exparray.h"
#include "token.h"

struct Expression;
struct ResolvedType;

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
		ISIZE,
		F32,
		F64,
		STRING,
		CPTR,
		BYTE,
		// Type of values that exist only at compile time and describe runtime values.
		// Example: `comptime Color = enum { Red, Green };` gives Color this type.
		TYPE,
		// Function value or function signature, depending on whether this appears on a
		// symbol or inside parsed type syntax.
		FUNCTION,
		// Qualified type name such as `alias.Type`.
		QUALIFIED,
		// Generic type parameter introduced in a function signature, e.g. `$T`.
		GENERIC,
		// Prefix array syntax, e.g. `[N]T`. Nested arrays are represented by nested
		// ArrayParsedType nodes rather than by a lossy array-depth counter.
		ARRAY,
		// Prefix slice syntax such as `[]T`.
		SLICE,
		// Nullable type syntax such as `?T`.
		NULLABLE,
		// Template instantiation in type syntax, e.g. `Array[i32]` or
		// `StaticArray[f32, 16]`.
		TEMPLATE_INSTANTIATION,
		// Pre-resolved type from template substitution during cloning.
		PRE_RESOLVED
	};

	explicit ParsedType(Kind kind) : kind(kind) {}
	ParsedType() = default;

	Kind kind = INVALID;
	Token token = {};
};

struct QualifiedParsedType : ParsedType {
	QualifiedParsedType() : ParsedType(QUALIFIED) {}

	ls_string_view qualifier = {};
	ls_string_view name = {};
};

struct GenericParsedType : ParsedType {
	GenericParsedType() : ParsedType(GENERIC) {}

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

struct NullableParsedType : ParsedType {
	NullableParsedType() : ParsedType(NULLABLE) {}

	ParsedType* inner = nullptr;
};

struct TemplateInstantiationParsedType : ParsedType {
	TemplateInstantiationParsedType(ls_arena& arena) : ParsedType(TEMPLATE_INSTANTIATION), args(arena) {}

	// Always a qualified name; the parser only forms instantiations after one.
	QualifiedParsedType* base = nullptr;
	ExpArray<Expression*> args;
};

struct PreResolvedParsedType : ParsedType {
	PreResolvedParsedType() : ParsedType(PRE_RESOLVED) {}

	// A fully resolved type from template substitution.
	ResolvedType* type = nullptr;
};
