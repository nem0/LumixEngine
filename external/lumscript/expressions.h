#pragma once

#include "capi.h"
#include "exparray.h"
#include "parsed_types.h"
#include "resolved_types.h"
#include "token.h"

struct Statement;
struct Symbol;

struct NamedDecl {
	ls_string_view name;
	// Shared by compile-time template parameters and runtime function parameters;
	// the owning expression determines which phase the parameter belongs to.
	ParsedType* parsed_type = nullptr;
	ResolvedType* resolved_type = nullptr;
};

struct FunctionParam {
	ls_string_view name;
	// Runtime parameters can be passed by reference with `ref`.
	bool is_ref = false;
	ParsedType* parsed_type = nullptr;
	ResolvedType* resolved_type = nullptr;
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
		// Generic bracket postfix used before semantic disambiguation.
		BRACKET,
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
	};

	Expression() = default;
	explicit Expression(Kind kind)
		: kind(kind) {}

	Kind kind = INVALID;
	ResolvedType* resolved_type = nullptr;
};

struct IdentifierExpression : Expression {
	IdentifierExpression() : Expression(IDENTIFIER) {}

	ls_string_view name = {};
	Symbol* symbol = nullptr;
};

struct IntLiteralExpression : Expression {
	IntLiteralExpression() : Expression(INT_LITERAL) {}

	i64 value = 0;
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
	TypeLiteralExpression(ParsedType::Kind kind) : Expression(TYPE_LITERAL), type(kind) {}

	ParsedType::Kind type = ParsedType::INVALID;
};

struct CallExpression : Expression {
	CallExpression(ls_arena& arena) : Expression(CALL), args(arena) {}

	Expression* callee = nullptr;
	ExpArray<Expression*> args;
};

struct UnaryExpression : Expression {
	UnaryExpression() : Expression(UNARY) {}

	Expression* expression = nullptr;
	Token::Type op = Token::ERROR;
};

struct BinaryExpression : Expression {
	BinaryExpression() : Expression(BINARY) {}

	Expression* lhs = nullptr;
	Expression* rhs = nullptr;
	Token::Type op = Token::ERROR;
};

struct CastExpression : Expression {
	CastExpression() : Expression(CAST) {}

	Expression* expression = nullptr;
	ParsedType* parsed_type = nullptr;
};

struct MemberExpression : Expression {
	MemberExpression() : Expression(MEMBER) {}

	Expression* expression = nullptr;
	ls_string_view name = {};
};

struct BracketExpression : Expression {
	BracketExpression(ls_arena& arena) : Expression(BRACKET), args(arena) {}

	Expression* base = nullptr;
	ExpArray<Expression*> args;
	bool has_colon = false;
	Expression* end = nullptr;
};

struct StructLiteralExpression : Expression {
	StructLiteralExpression(ls_arena& arena) : Expression(STRUCT_LITERAL), values(arena) {}

	Expression* type = nullptr;
	ExpArray<Expression*> values;
};

struct FunctionExpression : Expression {
	FunctionExpression(ls_arena& arena) : Expression(FUNCTION), comptime_params(arena), runtime_params(arena) {}

	ExpArray<NamedDecl> comptime_params; // [...]
	ExpArray<FunctionParam> runtime_params; // (...)
	// Return type remains source syntax until semantic resolution so aliases and
	// generated comptime types can be resolved after symbol registration.
	ParsedType* return_type = nullptr;
	ResolvedType* function_type = nullptr;
	// Null for `extern fn` declarations; the host binds the implementation at runtime.
	Statement* body = nullptr;
	bool is_extern = false;
};

struct StructExpression : Expression {
	StructExpression(ls_arena& arena) : Expression(STRUCT), comptime_params(arena), fields(arena) {}

	ExpArray<NamedDecl> comptime_params; // [...]
	// Fields keep parsed type syntax so generic structs can be instantiated with
	// different comptime arguments before producing concrete resolved field types.
	ExpArray<NamedDecl> fields;
	ResolvedType* produced_type = nullptr;
};

struct EnumMember {
	ls_string_view name;
	Expression* value = nullptr;
};

struct EnumExpression : Expression {
	EnumExpression(ls_arena& arena) : Expression(ENUM), members(arena) {}

	ExpArray<EnumMember> members;
	ResolvedType* produced_type = nullptr;
};
