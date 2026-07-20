#pragma once

#include "capi.h"
#include "exparray.h"
#include "resolved_types.h"
#include "token.h"

struct Statement;
struct Symbol;
struct FunctionExpression;
struct Expression;

struct NamedDecl {
	ls_string_view name;
	// Shared by compile-time template parameters and runtime function parameters;
	// the owning expression determines which phase the parameter belongs to.
	// Type annotations are ordinary expressions resolved in a comptime context.
	Expression* type_expr = nullptr;
	ResolvedType* resolved_type = nullptr;
};

// Storage location assigned by the bytecode compiler to runtime declarations
// (var, function parameter, loop variable, or global). Identifier expressions and
// global symbols point at a StorageSlot for O(1) lookups. Frame slot values are only
// meaningful during function compilation; the checker guarantees a declaration is
// compiled before any identifier that reads it. Global slot values are stable across
// the entire compilation.
struct StorageSlot {
	enum Storage : u8 {
		LOCAL,      // Frame-local storage (typical locals and loop variables).
		LOCAL_REF,  // Frame-local storage passed by reference (function parameters only).
		GLOBAL,     // Global data segment storage.
	};

	u32 offset = 0;
	u32 byte_size = 0;
	ls_type_kind kind = LS_TYPE_INVALID;
	ResolvedType* type = nullptr;
	Storage storage = LOCAL;
};

struct FunctionParam {
	ls_string_view name;
	// Runtime parameters can be passed by reference with `ref`.
	bool is_ref = false;
	// Compile-time value parameters require constant arguments.
	bool is_comptime = false;
	bool is_generic = false;
	Expression* type_expr = nullptr;
	ResolvedType* resolved_type = nullptr;
	StorageSlot slot;
};

struct Expression {
	enum Kind {
		INVALID,
		IDENTIFIER,
		INT_LITERAL,
		FLOAT_LITERAL,
		BOOL_LITERAL,
		STRING_LITERAL,
		NULL_LITERAL,
		TYPE_LITERAL,
		// Runtime call: `foo(a, b)`.
		CALL,
		// Unary operator expression such as `-x`, `not x`, or `ref x` at a call site.
		UNARY,
		// Binary operator expression such as `a + b` or `x == y`.
		BINARY,
		// Explicit cast expression such as `x as f32`.
		CAST,
		// Field or namespace access such as `a.x` or `.Running`.
		MEMBER,
		// Generic bracket postfix used before semantic disambiguation
		// (indexing vs. template instantiation).
		BRACKET,
		// Slice of an array or slice such as `a[b:e]`; either bound may be omitted.
		SLICE,
		// Struct literal such as `Vec3 { 1, 2, 3 }`.
		STRUCT_LITERAL,
		// `fn (...) ... { ... }` creates a function value. A named function is just a
		// symbol bound to one of these expressions.
		FUNCTION,
		// `enum { ... }` creates a comptime type value.
		ENUM,
		// `struct { ... }` creates a comptime type value.
		STRUCT,
		UNDEFINED, // var a : i32 = undefined;
		// `sizeof(T)` / `alignof(T)` - produces an untyped integer constant.
		SIZEOF,
		// $T
		GENERIC_IDENTIFIER,
		// Type-constructor syntax; these appear in type positions (annotations,
		// casts, sizeof) and resolve to types in a comptime context.
		ARRAY_TYPE,    // [N]T
		SLICE_TYPE,    // []T
		NULLABLE_TYPE, // ?T
		FUNCTION_TYPE, // fn(A, B) : R used as a type
		UNION_TYPE,    // A | B used as a type
		// A fully resolved type injected by template substitution during cloning.
		RESOLVED_TYPE,
		// Ternary conditional operator: `condition ? true_expr : false_expr`
		TERNARY,
	};

	Expression() = default;
	explicit Expression(Kind kind)
		: kind(kind) {}

	Kind kind = INVALID;
	ResolvedType* resolved_type = nullptr;
	Token token = {};
};

struct IdentifierExpression : Expression {
	IdentifierExpression() : Expression(IDENTIFIER) {}

	ls_string_view name = {};
	Symbol* symbol = nullptr;
	FunctionExpression* resolved_fn = nullptr;
	// Set by the checker when this identifier resolves to runtime storage (local,
	// parameter, or global); null for compile-time symbols (functions, types, etc.).
	StorageSlot* slot = nullptr;
};

struct GenericIdentifierExpression : Expression {
	GenericIdentifierExpression() : Expression(GENERIC_IDENTIFIER) {}

	ls_string_view name = {};
};

struct IntLiteralExpression : Expression {
	IntLiteralExpression() : Expression(INT_LITERAL) {}

	u64 value = 0;
};

struct FloatLiteralExpression : Expression {
	FloatLiteralExpression() : Expression(FLOAT_LITERAL) {}

	double value = 0;
};

struct BoolLiteralExpression : Expression {
	BoolLiteralExpression(bool value) : Expression(BOOL_LITERAL), value(value) {}

	bool value = false;
};

struct StringLiteralExpression : Expression {
	StringLiteralExpression() : Expression(STRING_LITERAL) {}

	ls_string_view value = {};
};

struct NullLiteralExpression : Expression {
	NullLiteralExpression() : Expression(NULL_LITERAL) {}
};

struct UndefinedExpression : Expression {
	UndefinedExpression() : Expression(UNDEFINED) {}
};

struct TypeLiteralExpression : Expression {
	// VOID..BYTE name a primitive type; META is the `type` keyword.
	TypeLiteralExpression(ResolvedType::Kind kind) : Expression(TYPE_LITERAL), type(kind) {}

	ResolvedType::Kind type = ResolvedType::INVALID;
};

struct ArrayTypeExpression : Expression {
	ArrayTypeExpression() : Expression(ARRAY_TYPE) {}

	Expression* size = nullptr;
	Expression* element_type = nullptr;
};

struct SliceTypeExpression : Expression {
	SliceTypeExpression() : Expression(SLICE_TYPE) {}

	Expression* element_type = nullptr;
};

struct NullableTypeExpression : Expression {
	NullableTypeExpression() : Expression(NULLABLE_TYPE) {}

	Expression* inner = nullptr;
};

struct FunctionTypeExpression : Expression {
	FunctionTypeExpression(ls_arena& arena) : Expression(FUNCTION_TYPE), params(arena) {}

	ExpArray<Expression*> params;
	Expression* return_type = nullptr;
};

struct UnionTypeExpression : Expression {
	UnionTypeExpression(ls_arena& arena) : Expression(UNION_TYPE), members(arena) {}

	ExpArray<Expression*> members;
};

struct ResolvedTypeExpression : Expression {
	ResolvedTypeExpression() : Expression(RESOLVED_TYPE) {}

	ResolvedType* type = nullptr;
};

struct SizeofExpression : Expression {
	SizeofExpression() : Expression(SIZEOF) {}

	Expression* type_expr = nullptr;
	bool is_align = false;
	// Filled in during checking; behaves like an untyped integer literal afterwards.
	u64 value = 0;
};

struct CallExpression : Expression {
	CallExpression(ls_arena& arena) : Expression(CALL), args(arena) {}

	Expression* callee = nullptr;
	ExpArray<Expression*> args;
	// Set by the type checker when this call has a pre-resolved direct target - 
	// either a template instantiation or a UFCS-selected free function. The bytecode
	// compiler uses this instead of re-deriving resolution per callee shape.
	FunctionExpression* resolved_fn = nullptr;
};

struct UnaryExpression : Expression {
	UnaryExpression() : Expression(UNARY) {}

	Expression* expression = nullptr;
	Token::Type op = Token::ERROR;
	FunctionExpression* resolved_fn = nullptr;
};

struct BinaryExpression : Expression {
	BinaryExpression() : Expression(BINARY) {}

	Expression* lhs = nullptr;
	Expression* rhs = nullptr;
	Token::Type op = Token::ERROR;
	FunctionExpression* resolved_fn = nullptr;
};

struct CastExpression : Expression {
	CastExpression() : Expression(CAST) {}

	Expression* expression = nullptr;
	Expression* type_expr = nullptr;
};

struct MemberExpression : Expression {
	MemberExpression() : Expression(MEMBER) {}

	Expression* expression = nullptr;
	ls_string_view name = {};
	FunctionExpression* resolved_fn = nullptr;
	struct Symbol* resolved_symbol = nullptr;
};

struct BracketExpression : Expression {
	BracketExpression(ls_arena& arena) : Expression(BRACKET), args(arena) {}

	Expression* base = nullptr;
	ExpArray<Expression*> args;
};

struct SliceExpression : Expression {
	SliceExpression() : Expression(SLICE) {}

	Expression* base = nullptr;
	Expression* begin = nullptr; // null means 0
	Expression* end = nullptr; // null means base length
};

struct StructLiteralExpression : Expression {
	StructLiteralExpression(ls_arena& arena) : Expression(STRUCT_LITERAL), values(arena) {}

	Expression* type = nullptr;
	ExpArray<Expression*> values;
};

struct FunctionExpression : Expression {
	FunctionExpression(ls_arena& arena) : Expression(FUNCTION), params(arena), template_function_instances(arena) {}

	ExpArray<FunctionParam> params;
	Expression* return_type = nullptr;
	// Null for `extern fn` declarations; the host binds the implementation at runtime.
	Statement* body = nullptr;
	bool is_extern = false;
	bool is_template = false;
	// Canonical template specializations for this function declaration.
	ExpArray<TemplateFunctionInstance> template_function_instances;
	// Bytecode function index; assigned during bytecode compilation.
	u32 bytecode_index = ~0u;
};

struct StructOperator {
	Token::Type op;
	struct FunctionExpression* fn;
};

struct StructExpression : Expression {
	StructExpression(ls_arena& arena) : Expression(STRUCT), comptime_params(arena), fields(arena), operators(arena), template_struct_instances(arena) {}

	ExpArray<NamedDecl> comptime_params; // [...]
	// Fields keep type annotation syntax so generic structs can be instantiated with
	// different comptime arguments before producing concrete resolved field types.
	ExpArray<NamedDecl> fields;
	// Operator overloads hosted on this type (first struct operand).
	// Populated during symbol checking; used for O(1)-ish operator lookup.
	ExpArray<StructOperator> operators;
	// Canonical template specializations for this struct declaration.
	ExpArray<TemplateStructInstance> template_struct_instances;
	// Cached by symbol checking: name and owner unit for fast type printing.
	ls_string_view cached_name = {};
	struct Unit* cached_owner = nullptr;
};

struct EnumMember {
	ls_string_view name;
	Expression* value = nullptr;
};

struct EnumExpression : Expression {
	EnumExpression(ls_arena& arena) : Expression(ENUM), members(arena) {}

	ExpArray<EnumMember> members;
	// Cached by symbol checking: name and owner unit for fast type printing.
	ls_string_view cached_name = {};
	struct Unit* cached_owner = nullptr;
};

struct TernaryExpression : Expression {
	TernaryExpression() : Expression(TERNARY) {}

	Expression* condition = nullptr;
	Expression* true_expr = nullptr;
	Expression* false_expr = nullptr;
};
