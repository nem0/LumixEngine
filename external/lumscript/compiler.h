#pragma once

#include "capi.h"
#include "exparray.h"
#include "token.h"
#include "utils.h"

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
		F32,
		F64,
		STRING,
		CPTR,
		// Type of values that exist only at compile time and describe runtime values.
		// Example: `comptime Color = enum { Red, Green };` gives Color this type.
		TYPE,
		// Function value or function signature, depending on whether this appears on a
		// symbol or inside parsed type syntax.
		FUNCTION,
		// Type written as an identifier in source. Semantic resolution must prove that
		// the name refers to a comptime value whose value is a type.
		NAMED,
		// Suffix array syntax, e.g. `T[]`. Nested arrays are represented by nested
		// ArrayParsedType nodes rather than by a lossy array-depth counter.
		ARRAY,
		// Compile-time application in type syntax, e.g. `Array[i32]` or
		// `StaticArray[f32, 16]`.
		COMPTIME_CALL
	};

	explicit ParsedType(Kind kind) : kind(kind) {}
	ParsedType() = default;

	bool is_nullable = false;
	Kind kind = INVALID;
};

struct NamedParsedType : ParsedType {
	NamedParsedType() : ParsedType(NAMED) {}

	ls_string_view name = {};
};

struct ArrayParsedType : ParsedType {
	ArrayParsedType() : ParsedType(ARRAY) {}

	ParsedType* element_type = nullptr;
};

struct ComptimeArg {
	// Template arguments can be either types or values. This keeps `Array[i32]`
	// and `StaticArray[f32, 16]` in the same representation.
	enum Kind { INVALID, TYPE, EXPRESSION };

	Kind kind = INVALID;
	ParsedType* type = nullptr;
	struct Expression* expression = nullptr;
};

struct ComptimeCallParsedType : ParsedType {
	ComptimeCallParsedType() : ParsedType(COMPTIME_CALL) {}

	ParsedType* callee = nullptr;
	span<ComptimeArg> args;
};

struct ResolvedType {
	// Canonical semantic types produced after resolving ParsedType syntax. These
	// are compiler facts, not necessarily the spelling used in source.
	enum Kind { INVALID, VOID, BOOL, I8, I16, I32, I64, U8, U16, U32, U64, F32, F64, STRING, CPTR, TYPE, FUNCTION, ENUM, STRUCT };

	Kind kind = INVALID;
};

struct NamedDecl {
	ls_string_view name;
	// Shared by compile-time template parameters and runtime function parameters;
	// the owning expression determines which phase the parameter belongs to.
	ParsedType* parsed_type = nullptr;
	ResolvedType* resolved_type = nullptr;
};

struct Symbol {
	enum Storage {
		// Runtime storage. The initializer expression is evaluated at runtime unless
		// later analysis proves it can be folded.
		VARIABLE,
		// Runtime storage initialized once and rejected as an assignment target.
		CONST,
		// Compile-time binding. Functions, structs and enums are represented
		// as values bound here instead of as separate declaration categories.
		COMPTIME
	};

	Storage storage;
	ls_string_view name; // as written in unit
	// Syntax-level type annotation. This preserves aliases and unresolved names
	// exactly as written; semantic resolution converts it to resolved_type later.
	ParsedType* parsed_type = nullptr;
	// Canonical semantic type. For example, `var x : MyInt` can keep MyInt in
	// parsed_type while resolving here to the same ResolvedType as builtin i32.
	ResolvedType* resolved_type = nullptr;
	// Symbols bind names to expressions. Top-level `fn`, `struct`, and `enum`
	// syntax should eventually desugar to COMPTIME symbols with expression values.
	struct Expression* expression = nullptr; // expression used to initialize the symbol
};

// so we can pass arenas to ExpArrays defined in the same struct as the arena
struct ArenaOwner {
	ArenaOwner(const ls_host* host)
		: host(host) {
		arena = host->create_arena();
	}

	~ArenaOwner() { host->destroy_arena(arena); }

	void operator=(const ArenaOwner&) = delete;
	void operator=(ArenaOwner&&) = delete;
	ArenaOwner(const ArenaOwner&) = delete;
	ArenaOwner(ArenaOwner&&) = delete;

	operator ls_arena&() { return *arena; }

	const ls_host* host;
	ls_arena* arena;
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
		// Runtime call: `foo(a, b)`.
		CALL,
		// Unary operator expression such as `-x` or `not x`.
		UNARY,
		// Binary operator expression such as `a + b` or `x == y`.
		BINARY,
		// Explicit cast expression such as `x as f32`.
		CAST,
		// Field or namespace access such as `a.x` or `.Running`.
		MEMBER,
		// Compile-time/template application: `Array[i32]` or `max[i32]`.
		COMPTIME_CALL,
		// Struct literal such as `Vec3 { 1, 2, 3 }`.
		STRUCT_LITERAL,
		// Indexing such as `values[i]`.
		INDEX,
		// Slice expression such as `values[start:end]`.
		SLICE,
		// Range expression such as `0..9`.
		RANGE,
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

struct CallExpression : Expression {
	CallExpression() : Expression(CALL) {}

	Expression* callee = nullptr;
	span<Expression*> args;
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

struct ComptimeCallExpression : Expression {
	ComptimeCallExpression() : Expression(COMPTIME_CALL) {}

	Expression* callee = nullptr;
	span<ComptimeArg> args;
};

struct StructLiteralExpression : Expression {
	StructLiteralExpression() : Expression(STRUCT_LITERAL) {}
};

struct IndexExpression : Expression {
	IndexExpression() : Expression(INDEX) {}
};

struct SliceExpression : Expression {
	SliceExpression() : Expression(SLICE) {}
};

struct RangeExpression : Expression {
	RangeExpression() : Expression(RANGE) {}
};

struct Statement {
	enum Kind {
		INVALID,
		BLOCK,
		EXPRESSION,
		RETURN,
		VAR_DECL,
		ASSIGN,
		IF,
		MATCH,
		WHILE,
		FOR,
		BREAK,
		CONTINUE,
		DEFER,
		LABEL
	};

	Statement() = default;
	explicit Statement(Kind kind)
		: kind(kind) {}

	Kind kind = INVALID;
};

struct BlockStatement : Statement {
	BlockStatement(ls_arena& arena) : Statement(BLOCK), statements(arena) {}

	ExpArray<Statement*> statements;
};

struct ExpressionStatement : Statement {
	ExpressionStatement() : Statement(EXPRESSION) {}

	Expression* expression = nullptr;
};

struct ReturnStatement : Statement {
	ReturnStatement() : Statement(RETURN) {}

	Expression* expression = nullptr;
};

struct VarDeclStatement : Statement {
	VarDeclStatement() : Statement(VAR_DECL) {}

	ls_string_view name;
	ParsedType* parsed_type = nullptr;
	Expression* expression = nullptr;
	bool is_immutable = false;
};

struct AssignStatement : Statement {
	AssignStatement() : Statement(ASSIGN) {}

	Expression* lhs = nullptr;
	Expression* rhs = nullptr;
	Token::Type op = Token::EQUAL;
};

struct IfStatement : Statement {
	IfStatement() : Statement(IF) {}
};

struct MatchStatement : Statement {
	MatchStatement() : Statement(MATCH) {}
};

struct WhileStatement : Statement {
	WhileStatement() : Statement(WHILE) {}
};

struct ForStatement : Statement {
	ForStatement() : Statement(FOR) {}
};

struct BreakStatement : Statement {
	BreakStatement() : Statement(BREAK) {}
};

struct ContinueStatement : Statement {
	ContinueStatement() : Statement(CONTINUE) {}
};

struct DeferStatement : Statement {
	DeferStatement() : Statement(DEFER) {}
};

struct LabelStatement : Statement {
	LabelStatement() : Statement(LABEL) {}
};

struct FunctionExpression : Expression {
	FunctionExpression(ls_arena& arena) : Expression(FUNCTION), comptime_params(arena), runtime_params(arena) {}

	ExpArray<NamedDecl> comptime_params; // [...]
	ExpArray<NamedDecl> runtime_params;	 // (...)
	// Return type remains source syntax until semantic resolution so aliases and
	// generated comptime types can be resolved after symbol registration.
	ParsedType* return_type = nullptr;
	ResolvedType* function_type = nullptr;
	Statement* body = nullptr;
};

struct StructExpression : Expression {
	StructExpression() : Expression(STRUCT) {}

	span<NamedDecl> comptime_params; // [...]
	// Fields keep parsed type syntax so generic structs can be instantiated with
	// different comptime arguments before producing concrete resolved field types.
	span<NamedDecl> fields;
	ResolvedType* produced_type = nullptr;
};

struct EnumExpression : Expression {
	EnumExpression() : Expression(ENUM) {}

	// Cases will live here once enum payloads and explicit values are designed.
	ResolvedType* produced_type = nullptr;
};

struct Unit {
	Unit(ls_string_view path, const ls_host* host)
		: arena(host)
		, symbols(arena)
		, types(arena)
		, path(path) {}

	ls_string_view path;
	ArenaOwner arena;

	// Parsed top-level bindings. Function/type declarations are represented as
	// symbols bound to AST expressions instead of separate declaration objects.
	ExpArray<Symbol> symbols; // top level symbols
	// Canonical semantic types allocated during resolution/instantiation.
	ExpArray<ResolvedType> types;
};

struct ls_module {
	ls_module(const ls_host* host)
		: host(host)
		, arena(host)
		, units(arena) {}

	const ls_host* host;
	ArenaOwner arena;
	ExpArray<Unit> units;
};
