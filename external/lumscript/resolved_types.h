#pragma once

#include "exparray.h"

struct EnumExpression;
struct StructExpression;
struct FunctionExpression;
struct Expression;

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
		ISIZE,
		F32,
		F64,
		STRING,
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
		, field_types(arena) {}

	StructExpression* decl = nullptr;
	// A generic declaration is shared by every specialization, therefore its
	// NamedDecl::resolved_type cannot describe the fields of a concrete value.
	// Keep the substituted field types on the canonical struct instance instead.
	ExpArray<ResolvedType*> field_types;
};

struct FunctionResolvedParam {
	ls_string_view name;
	ResolvedType* type = nullptr;
	bool is_ref = false;
	bool is_comptime = false;
};

struct FunctionResolvedType : ResolvedType {
	FunctionResolvedType(ls_arena& arena) : ResolvedType(FUNCTION), params(arena) {}

	ExpArray<FunctionResolvedParam> params;
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

struct UnionResolvedType : ResolvedType {
	UnionResolvedType(ls_arena& arena) : ResolvedType(UNION), members(arena) {}

	ExpArray<ResolvedType*> members;
};

struct ComptimeResult {
	enum Kind {
		FAILURE,
		VALUE,
		TYPE,
		VOID
	};
	Kind kind = FAILURE;
	ResolvedType* type = nullptr; // type of value if kind == VALUE, or the type itself if kind == TYPE
	u8* value = nullptr; // pointer to the value bytes if kind == VALUE, otherwise nullptr

	operator bool() const { return kind != FAILURE; }
};

struct TemplateStructInstance {
	TemplateStructInstance(ls_arena& arena) : args(arena) {}

	ExpArray<ComptimeResult> args;
	StructResolvedType* type = nullptr;
	bool check_failed = false;
};

struct TemplateFunctionInstance {
	TemplateFunctionInstance(ls_arena& arena) : args(arena) {}

	ExpArray<ComptimeResult> args;
	FunctionExpression* instance = nullptr;
	FunctionResolvedType* type = nullptr;
	bool check_failed = false;
};


u32 typeByteSize(const ResolvedType& t);
