#pragma once

#include "exparray.h"

struct EnumExpression;
struct StructExpression;
struct FunctionExpression;
struct Expression;

enum class ResolvedTypeKind {
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
	ISIZE,
	F32,
	F64,
	CSTR,
	CPTR,
	BYTE,
	UNTYPED_INT,
	UNTYPED_FLOAT,
	META, // the type of a type: resolved_type of enum/struct/fn declarations
	ENUM,
	STRUCT,
	FUNCTION,
	ARRAY,
	SLICE,
	NULLABLE,
	UNION,
	POINTER
};

struct ResolvedType {
	explicit ResolvedType(ResolvedTypeKind kind) : kind(kind) {}
	ResolvedType() = default;
	virtual ~ResolvedType() = default;

	ResolvedTypeKind kind = ResolvedTypeKind::INVALID;
};

struct MetaType : ResolvedType {
	MetaType() : ResolvedType(ResolvedTypeKind::META) {}
	ResolvedType* inner = nullptr; // the actual type (EnumResolvedType*, StructResolvedType*, etc.)
};

struct EnumResolvedType : ResolvedType {
	EnumResolvedType() : ResolvedType(ResolvedTypeKind::ENUM) {}

	EnumExpression* decl = nullptr;
};

struct ResolvedStructField {
	ResolvedStructField() = default;
	ResolvedStructField(ResolvedType* type) : type(type) {}

	ResolvedType* type = nullptr;
	u32 offset = 0;
};

struct StructResolvedType : ResolvedType {
	StructResolvedType(ls_arena& arena)
		: ResolvedType(ResolvedTypeKind::STRUCT)
		, fields(arena) {}

	StructExpression* decl = nullptr;
	ExpArray<ResolvedStructField> fields;
	u32 byte_size = 1;
	u32 alignment = 1;
};

struct FunctionResolvedParam {
	ls_string_view name;
	ResolvedType* type = nullptr;
	bool is_comptime = false;
};

struct FunctionResolvedType : ResolvedType {
	FunctionResolvedType(ls_arena& arena) : ResolvedType(ResolvedTypeKind::FUNCTION), params(arena) {}

	ExpArray<FunctionResolvedParam> params;
	ResolvedType* return_type = nullptr;
	FunctionExpression* decl = nullptr;
};

struct PointerResolvedType : ResolvedType {
	PointerResolvedType() : ResolvedType(ResolvedTypeKind::POINTER) {}

	ResolvedType* inner = nullptr;
	bool is_const = false;
};

struct ArrayResolvedType : ResolvedType {
	ArrayResolvedType() : ResolvedType(ResolvedTypeKind::ARRAY) {}

	ResolvedType* element_type = nullptr;
	i64 size = 0;
};

struct SliceResolvedType : ResolvedType {
	SliceResolvedType() : ResolvedType(ResolvedTypeKind::SLICE) {}

	ResolvedType* element_type = nullptr;
	bool is_const = false;
};

struct NullableResolvedType : ResolvedType {
	NullableResolvedType() : ResolvedType(ResolvedTypeKind::NULLABLE) {}

	ResolvedType* inner = nullptr;
};

struct UnionResolvedType : ResolvedType {
	UnionResolvedType(ls_arena& arena) : ResolvedType(ResolvedTypeKind::UNION), members(arena) {}

	ExpArray<ResolvedType*> members;
};

struct ComptimeValue {
	enum Kind {
		FAILURE,
		VALUE,
		TYPE,
		VOID
	};
	Kind kind = FAILURE;
	ResolvedType* type = nullptr; // value type, or the represented type when kind == TYPE
	u8* value = nullptr; // pointer to scalar value bytes if kind == VALUE

	operator bool() const { return kind != FAILURE; }
};

struct TemplateFunctionInstance {
	TemplateFunctionInstance(ls_arena& arena) : args(arena) {}

	ExpArray<ComptimeValue> args;
	FunctionExpression* instance = nullptr;
	FunctionResolvedType* type = nullptr;
	// Type produced by a `: type` function, recorded when the call is folded. Template
	// argument inference through a factory call pattern matches against it.
	ResolvedType* produced_type = nullptr;
	bool check_failed = false;
};


u32 typeByteSize(const ResolvedType& t);
