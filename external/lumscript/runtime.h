#pragma once

#include "ast.h"
#include "core/allocator.h"
#include "core/array.h"
#include "core/span.h"
#include "lumscript.h"
#include "value.h"

namespace Lumix::LumScript {
	

struct Runtime {
	friend struct Checker;

	Runtime(Module& module, IAllocator& allocator);
	~Runtime();

	bool call(StringView function_name, Span<const Value> args, Value* result, Callbacks& diagnostics);
	
	i32 findFunction(StringView name) const;
	
private:
	enum class FlowSignal : u8 { NONE, RETURN, BREAK, CONTINUE };
	
	struct Binding {
		StringView name;
		Value value;
		Value* alias = nullptr;
		bool is_const = false;
	};

	struct Frame {
		explicit Frame(IAllocator& allocator)
			: bindings(allocator)
		{}
		Array<Binding> bindings;
	};

	Value makeDefault(TypeRef type);
	Value makeStruct(TypeRef type, Expr& e);
	i32 findEnum(StringView name) const;
	i32 findNativeFunction(StringView name) const;
	bool initializeGlobals();
	StringView getExpressionName(i32 expr_idx);
	bool splitMemberName(StringView name, StringView* owner, StringView* member) const;
	bool evalQualifiedEnumMember(StringView name, Value* value);
	Binding* findBinding(StringView name);
	bool callFunction(FunctionDecl& fn, Span<const Value> args, Span<Value*> ref_args, Value* result);
	bool callNativeFunction(NativeFunctionDecl& fn, Span<const Value> args, Value* result);
	const Value& bindingValue(const Binding& binding) const;
	Value* bindingValuePtr(Binding& binding) const;
	Value evalExpr(i32 expr_idx);
	Value evalBinary(Expr& e);
	Value concatStrings(StringView a, StringView b);
	Value castValue(Value value, TypeRef type);
	Value* resolveLValue(i32 expr_idx, bool* is_const);
	bool equalValues(const Value& a, const Value& b) const;
	bool matchPattern(const Value& subject, MatchPattern& pattern);
	bool matchArm(const Value& subject, MatchArm& arm);
	void assign(i32 left_expr, Token::Type op, Value value);
	void execStmt(i32 stmt_idx, Value* ret, FlowSignal* flow, StringView* flow_label, bool allow_after_return = false);

	Module& m_module;
	IAllocator& m_allocator;
	Callbacks* m_callbacks = nullptr;
	Array<Frame> m_frames;
	Array<Binding> m_globals;
	Array<i32> m_deferred_statements;
	Array<Array<Value>*> m_owned_arrays;
	Array<char*> m_owned_strings;
	bool m_globals_initialized = false;
};

} // namespace Lumix::LumScript
