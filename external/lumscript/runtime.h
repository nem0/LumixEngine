#pragma once

#include <span>
#include <vector>

#include "ast.h"

struct Runtime {
	friend struct Checker;

	Runtime(Module& module);
	~Runtime();

	bool call(ls_string_view function_name, std::span<const Value> args, Value* result);
	
	i32 findFunction(ls_string_view name) const;
	
private:
	enum class FlowSignal : u8 { NONE, RETURN, BREAK, CONTINUE };
	
	struct Binding {
		ls_string_view name;
		Value value;
		Value* alias = nullptr;
		bool is_const = false;
	};

	struct Frame {
		std::vector<Binding> bindings;
	};

	Value makeDefault(TypeRef type);
	Value makeStruct(TypeRef type, Expr& e);
	i32 findEnum(ls_string_view name) const;
	i32 findNativeFunction(ls_string_view name) const;
	bool initializeGlobals();
	ls_string_view getExpressionName(i32 expr_idx);
	bool splitMemberName(ls_string_view name, ls_string_view* owner, ls_string_view* member) const;
	bool evalQualifiedEnumMember(ls_string_view name, Value* value);
	Binding* findBinding(ls_string_view name);
	bool callFunction(FunctionDecl& fn, std::span<const Value> args, std::span<Value*> ref_args, Value* result);
	bool callNativeFunction(NativeFunctionDecl& fn, std::span<const Value> args, Value* result);
	const Value& bindingValue(const Binding& binding) const;
	Value* bindingValuePtr(Binding& binding) const;
	Value evalExpr(i32 expr_idx);
	Value evalBinary(Expr& e);
	Value concatStrings(ls_string_view a, ls_string_view b);
	Value castValue(Value value, TypeRef type);
	Value* resolveLValue(i32 expr_idx, bool* is_const);
	bool equalValues(const Value& a, const Value& b) const;
	bool matchPattern(const Value& subject, MatchPattern& pattern);
	bool matchArm(const Value& subject, MatchArm& arm);
	void assign(i32 left_expr, Token::Type op, Value value);
	void execStmt(i32 stmt_idx, Value* ret, FlowSignal* flow, ls_string_view* flow_label, bool allow_after_return = false);

	Module& m_module;
	OutputFormatter m_output;
	std::vector<Frame> m_frames;
	std::vector<Binding> m_globals;
	std::vector<i32> m_deferred_statements;
	std::vector<std::vector<Value>*> m_owned_arrays;
	std::vector<char*> m_owned_strings;
	bool m_globals_initialized = false;
};

