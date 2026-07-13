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
	Expression* type_expr = nullptr;
	ResolvedType* resolved_type = nullptr;
	Expression* expression = nullptr;
	bool is_immutable = false;
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
};

struct MatchPattern {
	Expression* begin = nullptr;
	Expression* end = nullptr;
};

struct MatchArm {
	MatchArm(ls_arena& arena) : patterns(arena) {}

	bool is_fallback = false;
	ExpArray<MatchPattern> patterns;
	BlockStatement* body = nullptr;
};

struct MatchStatement : Statement {
	MatchStatement(ls_arena& arena) : Statement(MATCH), arms(arena) {}

	Expression* subject = nullptr;
	ExpArray<MatchArm> arms;
};

struct WhileStatement : Statement {
	WhileStatement() : Statement(WHILE) {}

	Expression* condition = nullptr;
	BlockStatement* body = nullptr;
};

struct ForStatement : Statement {
	ForStatement() : Statement(FOR) {}

	ls_string_view loop_var = {};
	Expression* begin = nullptr;
	Expression* end = nullptr;
	BlockStatement* body = nullptr;
	StorageSlot slot;
};

struct BreakStatement : Statement {
	BreakStatement() : Statement(BREAK) {}

	ls_string_view label = {};
};

struct ContinueStatement : Statement {
	ContinueStatement() : Statement(CONTINUE) {}

	ls_string_view label = {};
};

struct DeferStatement : Statement {
	DeferStatement() : Statement(DEFER) {}

	Statement* statement = nullptr;
};

struct LabelStatement : Statement {
	LabelStatement() : Statement(LABEL) {}

	ls_string_view name = {};
	Statement* statement = nullptr;
};
