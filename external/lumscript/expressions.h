#pragma once

#include "capi.h"
#include "exparray.h"
#include "resolved_types.h"
#include "token.h"

struct Statement;
struct Symbol;
struct FunctionExpression;
struct Expression;

struct Attribute;

struct StructFieldDecl {
	ls_string_view name;
	ExpArray<Attribute>* attributes = nullptr;
	Expression* type_expr = nullptr;
};

// Storage location assigned by the bytecode compiler to runtime declarations
// (var, function parameter, loop variable, or global). Identifier expressions and
// global symbols point at a StorageSlot for O(1) lookups. Frame slot values are only
// meaningful during function compilation; the checker guarantees a declaration is
// compiled before any identifier that reads it. Global slot values are stable across
// the entire compilation.
struct StorageSlot {
	enum Storage : u8 {
		LOCAL,
		GLOBAL,
	};

	u32 offset = 0;
	ResolvedType* type = nullptr;
	Storage storage = LOCAL;
};

struct FunctionParam {
	ls_string_view name;
	bool is_comptime = false;
	Expression* type_expr = nullptr;
	ResolvedType* resolved_type = nullptr;
	StorageSlot slot;
};

struct Expression {
	enum EvalStage : u8 {
		// The expression is not statically known while this AST is checked. It may
		// still fold when an evaluator supplies known parameter bindings.
		RUNTIME,
		// The expression is statically known and can be materialized into runtime
		// code as a constant.
		COMPTIME_VALUE,
		// The expression is statically known, but has no runtime representation
		// (for example `type` and `TypeKind` values).
		COMPTIME_ONLY,
	};

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
		// Non-returning built-in panic(msg).
		PANIC,
		// Unary operator expression such as `-x`, `not x`, or `ref x` at a call site.
		UNARY,
		// Binary operator expression such as `a + b` or `x == y`.
		BINARY,
		// Explicit cast expression such as `x as f32`.
		CAST,
		// Field or namespace access such as `a.x` or `.Running`.
		MEMBER,
		// Compile-time reflection access such as `T::name`.
		TYPE_MEMBER,
		// Generic bracket postfix used before semantic disambiguation
		// (indexing vs. template instantiation).
		BRACKET,
		// Slice of an array or slice such as `a[b:e]`; either bound may be omitted.
		SLICE,
		// Struct literal such as `Vec3 { 1, 2, 3 }`.
		STRUCT_LITERAL,
		ARRAY_LITERAL,
		// `fn (...) ... { ... }` creates a function value. A named function is just a
		// symbol bound to one of these expressions.
		FUNCTION,
		// `enum { ... }` creates a comptime type value.
		ENUM,
		// `struct { ... }` creates a comptime type value.
		STRUCT,
		UNDEFINED, // var a : i32 = undefined;
		TYPEOF,
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
		POINTER_TYPE, // *T
		DEREFERENCE, // .*
		ADDRESSOF, // &
	};

	Expression() = default;
	explicit Expression(Kind kind)
		: kind(kind) {}

	Kind kind = INVALID;
	ResolvedType* resolved_type = nullptr;
	ComptimeValue comptime_value;
	EvalStage eval_stage = RUNTIME;
	Token token = {};
	bool parenthesized = false;
};

struct IdentifierExpression : Expression {
	IdentifierExpression() : Expression(IDENTIFIER) {}

	ls_string_view name = {};
	Symbol* symbol = nullptr;
	FunctionExpression* resolved_fn = nullptr;
	StorageSlot* slot = nullptr;

	// for comptime locals, we need to store folded value for bytecode compiler to use
	u8* comptime_bytes = nullptr;
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
	TypeLiteralExpression(ResolvedTypeKind kind) : Expression(TYPE_LITERAL), type(kind) {}

	ResolvedTypeKind type = ResolvedTypeKind::INVALID;
};

struct ArrayTypeExpression : Expression {
	ArrayTypeExpression() : Expression(ARRAY_TYPE) {}

	Expression* size = nullptr;
	Expression* element_type = nullptr;
};

struct SliceTypeExpression : Expression {
	SliceTypeExpression() : Expression(SLICE_TYPE) {}

	Expression* element_type = nullptr;
	bool is_const = false;
};

struct DereferenceExpression : Expression {
	DereferenceExpression() : Expression(DEREFERENCE) {}
	Expression* subject = nullptr;
};

struct AddressOfExpression : Expression {
	AddressOfExpression() : Expression(ADDRESSOF) {}
	Expression* subject = nullptr;
};

struct PointerTypeExpression : Expression {
	PointerTypeExpression() : Expression(POINTER_TYPE) {}

	Expression* inner = nullptr;
	bool is_const = false;
};

struct NullableTypeExpression : Expression {
	NullableTypeExpression() : Expression(NULLABLE_TYPE) {}

	Expression* inner = nullptr;
};

struct FunctionTypeParam {
	// Empty for positional-only parameters.
	ls_string_view name;
	bool is_comptime = false;
	Expression* type_expr = nullptr;
};

struct FunctionTypeExpression : Expression {
	FunctionTypeExpression(ls_arena& arena) : Expression(FUNCTION_TYPE), params(arena) {}

	ExpArray<FunctionTypeParam> params;
	Expression* return_type = nullptr;
};

struct UnionTypeExpression : Expression {
	UnionTypeExpression(ls_arena& arena) : Expression(UNION_TYPE), members(arena) {}

	ExpArray<Expression*> members;
};

struct ResolvedTypeExpression : Expression {
	ResolvedTypeExpression() : Expression(RESOLVED_TYPE) {}
};

struct SizeofExpression : Expression {
	SizeofExpression() : Expression(SIZEOF) {}

	Expression* type_expr = nullptr;
	bool is_align = false;
	// Filled in during checking; behaves like an untyped integer literal afterwards.
	u64 value = 0;
};

struct PanicExpression : Expression {
	PanicExpression() : Expression(PANIC) {}
	Expression* message = nullptr;
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
	// Set when this unary operator resolves to an overloaded operator function.
	FunctionExpression* operator_fn = nullptr;
};

struct BinaryExpression : Expression {
	BinaryExpression() : Expression(BINARY) {}

	Expression* lhs = nullptr;
	Expression* rhs = nullptr;
	Token::Type op = Token::ERROR;
	// Set during semantic checking for union membership tests.
	i32 union_member_index = -1;
	// Set when this binary operator resolves to an overloaded operator function.
	FunctionExpression* operator_fn = nullptr;
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
	// Set during semantic checking for enum member expressions.
	i32 enum_member_index = -1;
	i64 enum_member_value = 0;
	i32 struct_field_index = -1;
};

struct TypeMemberExpression : Expression {
	enum Kind {
		NAME,
		KIND,
		RET,
		PARAMS,
		FIELDS,
		VALUES,
		TYPES,
		MIN,
		MAX,
		CHILD,
		LENGTH,
		ATTRIBUTE
	};

	TypeMemberExpression() : Expression(TYPE_MEMBER) {}

	Expression* expression = nullptr;
	Kind kind;
	ls_string_view comptime_string = {};
	ResolvedType* reflected_type = nullptr;
};

struct BracketExpression : Expression {
	BracketExpression(ls_arena& arena) : Expression(BRACKET), args(arena) {}

	Expression* base = nullptr;
	ExpArray<Expression*> args;
	// Set when this is compile-time string access to a struct field. Empty means
	// the bracket is ordinary array/slice/template access.
	ls_string_view struct_field_name = {};
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

// A typed attribute such as `tag { 42 }` in `#[tag { 42 }]`.
struct Attribute {
	Expression* type = nullptr;
	StructLiteralExpression* value = nullptr;
	Token token = {};
	ResolvedType* resolved_type = nullptr;
	u8* comptime_bytes = nullptr;
	u32 comptime_byte_size = 0;
};

struct TypeofExpression : Expression {
	TypeofExpression() : Expression(TYPEOF) {}

	Expression* operand = nullptr;
};

struct ArrayLiteralExpression : Expression {
	ArrayLiteralExpression(ls_arena& arena) : Expression(ARRAY_LITERAL), values(arena) {}

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
	StructExpression(ls_arena& arena) : Expression(STRUCT), fields(arena), operators(arena) {}

	ExpArray<StructFieldDecl> fields;
	ExpArray<Attribute>* attributes = nullptr;
	bool is_extern = false;
	// Operator overloads hosted on this type (first struct operand).
	// Populated during symbol checking; used for O(1)-ish operator lookup.
	ExpArray<StructOperator> operators;
	// Cached by symbol checking: name and owner unit for fast type printing.
	ls_string_view cached_name = {};
	struct Unit* cached_owner = nullptr;
};

struct EnumMember {
	ls_string_view name;
	Expression* value = nullptr;
};

struct EnumExpression : Expression {
	EnumExpression(ls_arena& arena) : Expression(ENUM), members(arena), cached_values(arena) {}

	ExpArray<EnumMember> members;
	// Cached by symbol checking: name and owner unit for fast type printing.
	ls_string_view cached_name = {};
	struct Unit* cached_owner = nullptr;
	// Integer discriminant of each member in declaration order, filled by the
	// checker after typechecking. Implicit members use their index (matching
	// runtime member-access semantics); explicit members use their evaluated
	// constant. Consumed by bytecode type metadata.
	ExpArray<i64> cached_values;
};

struct TernaryExpression : Expression {
	TernaryExpression() : Expression(TERNARY) {}

	Expression* condition = nullptr;
	Expression* true_expr = nullptr;
	Expression* false_expr = nullptr;
};
