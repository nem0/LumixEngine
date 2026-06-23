#pragma once

#include "exparray.h"

struct EnumExpression;
struct StructExpression;
struct FunctionExpression;

struct ResolvedType {
	enum Kind {
		INVALID,
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
		UNTYPED_INT,
		UNTYPED_FLOAT,
		META, // the type of a type: resolved_type of enum/struct/fn declarations
		ENUM,
		STRUCT,
		FUNCTION,
		ARRAY,
		SLICE,
		NULLABLE,
		BRACKET_TYPE,
	};

	explicit ResolvedType(Kind kind) : kind(kind) {}
	ResolvedType() = default;
	virtual ~ResolvedType() = default;

	Kind kind = INVALID;
};

struct MetaType : ResolvedType {
	MetaType() : ResolvedType(META) {}
	ResolvedType* inner = nullptr; // the actual type (EnumResolvedType*, StructResolvedType*, etc.)
};

struct EnumResolvedType : ResolvedType {
	EnumResolvedType() : ResolvedType(ENUM) {}

	EnumExpression* decl = nullptr;
};

struct StructResolvedType : ResolvedType {
	StructResolvedType(ls_arena& arena)
		: ResolvedType(STRUCT)
		, type_args(arena)
		, value_args(arena)
		, field_types(arena) {}

	StructExpression* decl = nullptr;
	ExpArray<ResolvedType*> type_args;
	ExpArray<i64> value_args;
	// A generic declaration is shared by every specialization, therefore its
	// NamedDecl::resolved_type cannot describe the fields of a concrete value.
	// Keep the substituted field types on the canonical struct instance instead.
	ExpArray<ResolvedType*> field_types;
};

struct FunctionResolvedType : ResolvedType {
	FunctionResolvedType(ls_arena& arena) : ResolvedType(FUNCTION), type_args(arena), param_types(arena) {}

	ExpArray<ResolvedType*> type_args;
	ExpArray<ResolvedType*> param_types;
	ResolvedType* return_type = nullptr;
	FunctionExpression* decl = nullptr;
};

struct ArrayResolvedType : ResolvedType {
	ArrayResolvedType() : ResolvedType(ARRAY) {}

	ResolvedType* element_type = nullptr;
	i64 size = 0;
};

struct SliceResolvedType : ResolvedType {
	SliceResolvedType() : ResolvedType(SLICE) {}

	ResolvedType* element_type = nullptr;
};

struct NullableResolvedType : ResolvedType {
	NullableResolvedType() : ResolvedType(NULLABLE) {}

	ResolvedType* inner = nullptr;
};

struct BracketTypeResolvedType : ResolvedType {
	BracketTypeResolvedType(ls_arena& arena) : ResolvedType(BRACKET_TYPE), args(arena) {}

	ResolvedType* callee = nullptr;
	ExpArray<ResolvedType*> args;
};
