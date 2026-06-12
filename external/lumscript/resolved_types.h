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
		TYPE,
		ENUM,
		STRUCT,
		FUNCTION,
		ARRAY,
		SLICE,
		NULLABLE,
		COMPTIME_CALL,
	};

	explicit ResolvedType(Kind kind) : kind(kind) {}
	ResolvedType() = default;
	virtual ~ResolvedType() = default;

	Kind kind = INVALID;
};

struct EnumResolvedType : ResolvedType {
	EnumResolvedType() : ResolvedType(ENUM) {}

	EnumExpression* decl = nullptr;
};

struct StructResolvedType : ResolvedType {
	StructResolvedType(ls_arena& arena) : ResolvedType(STRUCT), type_args(arena) {}

	StructExpression* decl = nullptr;
	ExpArray<ResolvedType*> type_args;
};

struct FunctionResolvedType : ResolvedType {
	FunctionResolvedType(ls_arena& arena) : ResolvedType(FUNCTION), param_types(arena) {}

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

struct ComptimeCallResolvedType : ResolvedType {
	ComptimeCallResolvedType(ls_arena& arena) : ResolvedType(COMPTIME_CALL), args(arena) {}

	ResolvedType* callee = nullptr;
	ExpArray<ResolvedType*> args;
};
