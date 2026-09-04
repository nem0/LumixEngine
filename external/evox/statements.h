#pragma once

#include "capi.h"
#include "exparray.h"
#include "expressions.h"

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
	Token token = {};
};

struct BlockStatement : Statement {
	BlockStatement(ex_arena& arena) : Statement(BLOCK), statements(arena) {}

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

	ex_string_view name;
	Token name_token = {};
	Expression* type_expr = nullptr;
	ResolvedType* resolved_type = nullptr;
	Expression* expression = nullptr;
	ResolvedType* else_return_type = nullptr;
	bool else_return = false;
	bool else_return_zero = false;
	u64 else_return_target_mask = 0;
	bool is_immutable = false;
	bool is_comptime = false;
	StorageSlot slot;
};

struct AssignStatement : Statement {
	AssignStatement() : Statement(ASSIGN) {}

	Expression* lhs = nullptr;
	Expression* rhs = nullptr;
	Token::Type op = Token::EQUAL;
	FunctionExpression* resolved_op_fn = nullptr;
};

struct IfStatement : Statement {
	IfStatement() : Statement(IF) {}

	Expression* condition = nullptr;
	BlockStatement* body = nullptr;
	Statement* else_branch = nullptr;
	bool comptime_known = false;
	bool comptime_value = false;
};

struct MatchPattern {
	Expression* begin = nullptr;
	Expression* end = nullptr;
	// Set during semantic checking for union type patterns.
	i32 union_member_index = -1;
};

struct MatchArm {
	MatchArm(ex_arena& arena) : patterns(arena) {}

	bool is_fallback = false;
	ExpArray<MatchPattern> patterns;
	BlockStatement* body = nullptr;
};

struct MatchStatement : Statement {
	MatchStatement(ex_arena& arena) : Statement(MATCH), arms(arena) {}

	Expression* subject = nullptr;
	ExpArray<MatchArm> arms;
	bool comptime_known = false;
	i32 comptime_arm = -1;
};

struct WhileStatement : Statement {
	WhileStatement() : Statement(WHILE) {}

	Expression* condition = nullptr;
	BlockStatement* body = nullptr;
};

struct ForStatement : Statement {
	ForStatement() : Statement(FOR) {}

	ex_string_view key_var = {};
	ex_string_view value_var = {};
	Token key_token = {};
	Token value_token = {};
	Expression* begin = nullptr;
	Expression* end = nullptr;
	BlockStatement* body = nullptr;
	StorageSlot slot;
	StorageSlot index_slot; // only used for the `for i, v in xs` array/slice form
	bool is_key_value = false; // true for `for i, v in xs`; false for `for v in xs` and range loops
	bool value_by_reference = false; // true for `for &v in xs`
	bool is_custom_iterator = false;
	i32 iterator_next_field = -1;
	ResolvedType* iterator_element_type = nullptr;
	bool is_unroll = false;
	bool is_expanded = false;
	i64 unroll_begin = 0;
	i64 unroll_end = 0;
	ArrayLiteralExpression* unroll_elements = nullptr; // resolved compile-time source for `unroll for` over an array/slice
};

struct BreakStatement : Statement {
	BreakStatement() : Statement(BREAK) {}

	ex_string_view label = {};
};

struct ContinueStatement : Statement {
	ContinueStatement() : Statement(CONTINUE) {}

	ex_string_view label = {};
};

struct DeferStatement : Statement {
	DeferStatement() : Statement(DEFER) {}

	Statement* statement = nullptr;
};

struct LabelStatement : Statement {
	LabelStatement() : Statement(LABEL) {}

	ex_string_view name = {};
	Statement* statement = nullptr;
};
