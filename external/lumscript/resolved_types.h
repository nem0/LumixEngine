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

// A compile-time constant: a template argument or the result of comptime
// evaluation. Types are comptime values too (TYPE kind).
struct ComptimeValue {
	enum Kind { INVALID, TYPE, INT, FLOAT, BOOL, STRING } kind;
	union {
		ResolvedType* type;
		i64 int_value;
		double float_value;
		bool bool_value;
	};
	ls_string_view string_value;

	ComptimeValue() : kind(INVALID), int_value(0) {}
	ComptimeValue(ResolvedType* t) : kind(TYPE), type(t) {}
	ComptimeValue(i64 i) : kind(INT), int_value(i) {}
	ComptimeValue(double f) : kind(FLOAT), float_value(f) {}
	ComptimeValue(bool b) : kind(BOOL), bool_value(b) {}
	ComptimeValue(ls_string_view s) : kind(STRING), string_value(s) {}

	double asFloat() const { return kind == FLOAT ? float_value : (double)int_value; }
	i64 asInt() const { return kind == INT ? int_value : (i64)float_value; }
	bool asBool() const { return kind == BOOL ? bool_value : (bool)int_value; }
};

struct TemplateStructInstance {
	TemplateStructInstance(ls_arena& arena) : args(arena) {}

	ExpArray<ComptimeValue> args;
	StructResolvedType* type = nullptr;
	bool check_failed = false;
};

struct TemplateFunctionInstance {
	TemplateFunctionInstance(ls_arena& arena) : args(arena) {}

	ExpArray<ComptimeValue> args;
	FunctionExpression* instance = nullptr;
	FunctionResolvedType* type = nullptr;
	bool check_failed = false;
};


u32 typeByteSize(const ResolvedType& t);
