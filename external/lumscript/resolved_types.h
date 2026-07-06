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

struct TemplateArgument {
	enum Kind {
		UNBOUND,
		TYPE,
		VALUE
	};

	TemplateArgument()
		: kind(UNBOUND)
		, type(nullptr) {}

	explicit TemplateArgument(ResolvedType* type)
		: kind(TYPE)
		, type(type) {}

	explicit TemplateArgument(Expression* value)
		: kind(VALUE)
		, value(value) {}

	Kind kind;
	union {
		ResolvedType* type;
		Expression* value;
	};
};

struct StructResolvedType : ResolvedType {
	StructResolvedType(ls_arena& arena)
		: ResolvedType(STRUCT)
		, template_args(arena)
		, field_types(arena) {}

	StructExpression* decl = nullptr;
	ExpArray<TemplateArgument> template_args;
	// A generic declaration is shared by every specialization, therefore its
	// NamedDecl::resolved_type cannot describe the fields of a concrete value.
	// Keep the substituted field types on the canonical struct instance instead.
	ExpArray<ResolvedType*> field_types;
};

struct FunctionResolvedType : ResolvedType {
	FunctionResolvedType(ls_arena& arena) : ResolvedType(FUNCTION), template_args(arena), param_types(arena) {}

	ExpArray<TemplateArgument> template_args;
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

struct TemplateFunctionInstance {
	ls_string_view name = {};
	FunctionExpression* instance = nullptr;
	FunctionResolvedType* type = nullptr;
};


i64 typeByteSize(const ResolvedType& t);