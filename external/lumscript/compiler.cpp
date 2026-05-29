#pragma once

#include "ast.h"

bool parse(ls_module& module, ls_string_view source, ls_string_view declaration_prefix = {}, ls_string_view source_name = {});

static NativeFunctionDecl& addNativeFunction(
	ls_module& module,
	ls_string_view name,
	TypeRef return_type,
	std::span<const TypeRef> param_types
) {
	NativeFunctionDecl& fn = module.native_functions.emplace_back();
	fn.name = name;
	fn.return_type = return_type;
	for (TypeRef type : param_types) {
		Param& param = fn.params.emplace_back();
		param.type = type;
	}
	return fn;
}

struct LocalInfo {
	ls_string_view name;
	TypeRef type;
	bool is_const = false;
};

struct LocalFunctionInfo {
	ls_string_view name;
	i32 function_index = -1;
};

#include <string>
#include <unordered_map>
#include <unordered_set>

struct Checker {
	Checker(ls_module& module)
		: m_module(module)
		, m_output(module.host)
	{}

	bool check() {
		// Use hash tables to detect duplicates in linear time.
		std::unordered_set<std::string> struct_names;
		struct_names.reserve(m_module.structs.size() * 2 + 16);
		for (const StructDecl& s : m_module.structs) {
			std::string key(data(s.name), size(s.name));
			if (!struct_names.insert(key).second) {
				m_output.errorAt(s.token, "Duplicate struct '", s.name, "'");
				return false;
			}
		}

		// Collect native function names for quick lookup
		std::unordered_set<std::string> native_names;
		native_names.reserve(m_module.native_functions.size() * 2 + 16);
		for (const NativeFunctionDecl& nf : m_module.native_functions) {
			std::string key(data(nf.name), size(nf.name));
			if (!native_names.insert(key).second) {
				m_output.errorAt(nf.token, "Duplicate function '", nf.name, "'");
				return false;
			}
		}

		// Functions: check duplicates and collisions with natives/globals
		std::unordered_map<std::string, Token> function_map;
		function_map.reserve(m_module.functions.size() * 2 + 16);
		for (const FunctionDecl& fn : m_module.functions) {
			if (fn.is_nested) continue;
			std::string key(data(fn.name), size(fn.name));
			auto it = function_map.find(key);
			if (it != function_map.end()) {
				m_output.errorAt(fn.token, "Duplicate function '", fn.name, "'");
				return false;
			}
			if (native_names.find(key) != native_names.end()) {
				m_output.errorAt(fn.token, "Duplicate function '", fn.name, "'");
				return false;
			}
			function_map.emplace(std::move(key), fn.token);
		}

		// Globals: check duplicates and collisions with functions/natives
		std::unordered_map<std::string, Token> global_map;
		global_map.reserve(m_module.globals.size() * 2 + 16);
		for (const GlobalDecl& g : m_module.globals) {
			std::string key(data(g.name), size(g.name));
			if (!global_map.insert({key, g.token}).second) {
				m_output.errorAt(g.token, "Duplicate global '", g.name, "'");
				return false;
			}
			if (function_map.find(key) != function_map.end() || native_names.find(key) != native_names.end()) {
				m_output.errorAt(g.token, "Duplicate declaration '", g.name, "'");
				return false;
			}
		}

		// Struct fields: detect duplicates per-struct and resolve field types
		for (StructDecl& s : m_module.structs) {
			std::unordered_set<std::string> field_names;
			field_names.reserve(s.fields.size() * 2 + 4);
			for (FieldDecl& f : s.fields) {
				std::string key(data(f.name), size(f.name));
				if (!field_names.insert(key).second) {
					m_output.errorAt(f.token, "Duplicate field '", f.name, "'");
					return false;
				}
				resolveType(f.type);
			}
		}

		// Enums and enum members
		std::unordered_set<std::string> enum_names;
		enum_names.reserve(m_module.enums.size() * 2 + 16);
		for (const EnumDecl& e : m_module.enums) {
			std::string key(data(e.name), size(e.name));
			if (!enum_names.insert(key).second) {
				m_output.errorAt(e.token, "Duplicate enum '", e.name, "'");
				return false;
			}
			std::unordered_set<std::string> member_names;
			member_names.reserve(e.members.size() * 2 + 4);
			for (const EnumMember& m : e.members) {
				std::string mkey(data(m.name), size(m.name));
				if (!member_names.insert(mkey).second) {
					m_output.errorAt(m.token, "Duplicate enum member '", m.name, "'");
					return false;
				}
			}
		}
		for (FunctionDecl& fn : m_module.functions) {
			if (fn.is_nested) continue;
			if (!checkFunctionSignature(fn)) return false;
		}
		for (NativeFunctionDecl& fn : m_module.native_functions) {
			for (Param& p : fn.params) resolveType(p.type);
			resolveType(fn.return_type);
		}
		checkGlobals();
		for (FunctionDecl& fn : m_module.functions) {
			if (!fn.is_nested) checkFunction(fn);
		}
		return !m_output.has_error;
	}

	bool sameResolvedType(TypeRef a, TypeRef b) const {
		if (a.kind == LS_TYPE_FUNCTION || b.kind == LS_TYPE_FUNCTION) return functionTypesEqual(a, b);
		return sameBaseType(a, b);
	}

	bool functionTypesEqual(TypeRef a, TypeRef b) const {
		if (a.kind != LS_TYPE_FUNCTION || b.kind != LS_TYPE_FUNCTION) return false;
		if (a.struct_index < 0 || a.struct_index >= m_module.function_types.size()) return false;
		if (b.struct_index < 0 || b.struct_index >= m_module.function_types.size()) return false;
		const FunctionTypeDecl& fa = m_module.function_types[a.struct_index];
		const FunctionTypeDecl& fb = m_module.function_types[b.struct_index];
		if (!sameResolvedType(fa.return_type, fb.return_type)) return false;
		if (fa.params.size() != fb.params.size()) return false;
		for (i32 i = 0; i < fa.params.size(); ++i) {
			if (!sameResolvedType(fa.params[i], fb.params[i])) return false;
		}
		return true;
	}

	TypeRef functionTypeFromSignature(std::span<const Param> params, TypeRef return_type, Token token) {
		for (i32 i = 0; i < m_module.function_types.size(); ++i) {
			FunctionTypeDecl& existing = m_module.function_types[i];
			if (existing.params.size() != params.size()) continue;
			bool same = sameResolvedType(existing.return_type, return_type);
			for (u32 j = 0; same && j < params.size(); ++j) {
				same = sameResolvedType(existing.params[j], params[j].type);
			}
			if (same) return {LS_TYPE_FUNCTION, {}, i, token};
		}
		FunctionTypeDecl& fn_type = m_module.function_types.emplace_back();
		for (const Param& p : params) fn_type.params.push_back(p.type);
		fn_type.return_type = return_type;
		return {LS_TYPE_FUNCTION, {}, (i32)m_module.function_types.size() - 1, token};
	}

	TypeRef functionTypeFromFunction(FunctionDecl& fn) {
		return functionTypeFromSignature(std::span<const Param>(fn.params.begin(), fn.params.size()), fn.return_type, fn.token);
	}

	TypeRef functionTypeFromNativeFunction(NativeFunctionDecl& fn) {
		return functionTypeFromSignature(std::span<const Param>(fn.params.begin(), fn.params.size()), fn.return_type, fn.token);
	}

	ls_string_view getExpressionName(i32 expr_idx) {
		Expr& e = m_module.expressions[expr_idx];
		if (e.kind == Expr::VAR) return e.name;
		if (e.kind == Expr::FIELD) {
			if (empty(e.qualified_name)) e.qualified_name = m_module.makeQualifiedName(getExpressionName(e.left), e.name);
			return e.qualified_name;
		}
		return {};
	}

	bool splitMemberName(ls_string_view name, ls_string_view* owner, ls_string_view* member) const {
		for (const char* c = data(name) + size(name); c != data(name); --c) {
			if (*(c - 1) != '.') continue;
			*owner = ls_string_view{data(name), c - 1};
			*member = ls_string_view{c, data(name) + size(name)};
			return true;
		}
		return false;
	}

	ls_string_view getTypeNamespace(TypeRef type) const {
		ls_string_view type_name = type.name;
		if (type.kind == LS_TYPE_NATIVE && type.struct_index >= 0) type_name = m_module.native_types[type.struct_index].name;
		else if (type.kind == LS_TYPE_NATIVE) {
			for (const NativeTypeDecl& native_type : m_module.native_types) {
				if (!equalStrings(native_type.id, type.name)) continue;
				type_name = native_type.name;
				break;
			}
		}
		ls_string_view namespace_name;
		ls_string_view member_name;
		if (!splitMemberName(type_name, &namespace_name, &member_name)) return {};
		return namespace_name;
	}

	ls_string_view resolveCallName(Expr& call, i32* fn_idx, i32* native_idx) {
		ls_string_view callee_name = empty(call.qualified_name) ? getExpressionName(call.left) : call.qualified_name;
		*fn_idx = m_module.findFunction(callee_name);
		*native_idx = m_module.findNativeFunction(callee_name);
		if (*fn_idx >= 0 || *native_idx >= 0) {
			call.qualified_name = callee_name;
			return callee_name;
		}

		Expr& callee = m_module.expressions[call.left];
		if (callee.kind == Expr::FIELD) {
			TypeRef receiver_type = checkExpr(callee.left);
			const ls_string_view namespace_name = getTypeNamespace(receiver_type);
			if (!empty(namespace_name)) {
				ls_string_view method_name = m_module.makeQualifiedName(namespace_name, callee.name);
				*fn_idx = m_module.findFunction(method_name);
				*native_idx = m_module.findNativeFunction(method_name);
				if (*fn_idx >= 0 || *native_idx >= 0) {
					call.qualified_name = method_name;
					call.method_receiver = callee.left;
					return method_name;
				}
			}
		}

		if (callee.kind != Expr::FIELD && !empty(callee_name) && !call.args.empty()) {
			TypeRef first_arg_type = checkExpr(call.args[0]);
			const ls_string_view namespace_name = getTypeNamespace(first_arg_type);
			if (!empty(namespace_name)) {
				ls_string_view method_name = m_module.makeQualifiedName(namespace_name, callee_name);
				*fn_idx = m_module.findFunction(method_name);
				*native_idx = m_module.findNativeFunction(method_name);
				if (*fn_idx >= 0 || *native_idx >= 0) {
					call.qualified_name = method_name;
					return method_name;
				}
			}
		}
		return callee_name;
	}

	bool checkQualifiedEnumMember(Expr& e, ls_string_view name) {
		ls_string_view enum_name;
		ls_string_view member_name;
		if (!splitMemberName(name, &enum_name, &member_name)) return false;
		const i32 enum_idx = m_module.findEnum(enum_name);
		if (enum_idx < 0) return false;
		EnumDecl& en = m_module.enums[enum_idx];
		const i32 member_idx = m_module.findEnumMember(en, member_name);
		if (member_idx < 0) {
			m_output.errorAt(e.token, "Unknown enum member '", member_name, "'");
			return true;
		}
		e.kind = Expr::ENUM_LITERAL;
		e.name = member_name;
		e.type = {LS_TYPE_ENUM, enum_name, enum_idx};
		return true;
	}

	i32 findLocal(ls_string_view name) const {
		for (i32 i = (i32)m_locals.size() - 1; i >= 0; --i) if (equalStrings(m_locals[i].name, name)) return i;
		return -1;
	}

	i32 findLocalFunction(ls_string_view name) const {
		for (i32 i = (i32)m_local_functions.size() - 1; i >= 0; --i) {
			if (equalStrings(m_local_functions[i].name, name)) return m_local_functions[i].function_index;
		}
		return -1;
	}

	bool localFunctionInCurrentScope(ls_string_view name) const {
		const i32 scope_start = m_function_scope_starts.empty() ? 0 : m_function_scope_starts.back();
		for (i32 i = scope_start; i < m_local_functions.size(); ++i) {
			if (equalStrings(m_local_functions[i].name, name)) return true;
		}
		return false;
	}

	bool isAssignableExpr(i32 expr_idx) {
		if (expr_idx < 0) return false;
		Expr& e = m_module.expressions[expr_idx];
		if (e.kind == Expr::VAR) return true;
		if (e.kind == Expr::FIELD) return isAssignableExpr(e.left);
		if (e.kind == Expr::INDEX) return isAssignableExpr(e.left);
		return false;
	}

	bool isConstExpr(i32 expr_idx) {
		if (expr_idx < 0) return false;
		Expr& e = m_module.expressions[expr_idx];
		if (e.kind == Expr::VAR) {
			const i32 idx = findLocal(e.name);
			if (idx >= 0) return m_locals[idx].is_const;
			const i32 global_idx = m_module.findGlobal(e.name);
			if (global_idx >= 0) return m_module.globals[global_idx].is_const;
			return false;
		}
		if (e.kind == Expr::FIELD) return isConstExpr(e.left);
		if (e.kind == Expr::INDEX) return isConstExpr(e.left);
		return false;
	}

	void resolveType(TypeRef& type) {
		if (type.kind == LS_TYPE_ARRAY) {
			TypeRef elem(type.element_kind, type.element_name, type.struct_index, type.token, false);
			resolveType(elem);
			type.element_kind = elem.kind;
			type.element_name = elem.name;
			type.struct_index = elem.struct_index;
		}
		else if (type.kind == LS_TYPE_STRUCT) {
			// First try to find as a struct
			type.struct_index = m_module.findStruct(type.name);
			if (type.struct_index < 0) {
				// If not a struct, try to find as an enum
				type.struct_index = m_module.findEnum(type.name);
				if (type.struct_index >= 0) {
					type.kind = LS_TYPE_ENUM;
				} else {
					type.struct_index = m_module.findNativeType(type.name);
					if (type.struct_index >= 0) {
						type.kind = LS_TYPE_NATIVE;
						type.name = m_module.native_types[type.struct_index].id;
					}
					else {
						m_output.errorAt(type.token, "Unknown type '", type.name, "'");
					}
				}
			}
		} else if (type.kind == LS_TYPE_ENUM) {
			type.struct_index = m_module.findEnum(type.name);
			if (type.struct_index < 0) m_output.errorAt(type.token, "Unknown type '", type.name, "'");
		} else if (type.kind == LS_TYPE_FUNCTION) {
			if (type.struct_index < 0 || type.struct_index >= m_module.function_types.size()) {
				m_output.errorAt(type.token, "Invalid function type");
				return;
			}
			FunctionTypeDecl& fn_type = m_module.function_types[type.struct_index];
			for (TypeRef& param : fn_type.params) resolveType(param);
			resolveType(fn_type.return_type);
		}
	}

	bool canAssign(TypeRef dst, TypeRef src) const {
		if (src.kind == LS_TYPE_NULL_VALUE) return dst.nullable;
		if (dst.kind == LS_TYPE_NULL_VALUE) return src.kind == LS_TYPE_NULL_VALUE;
		if (dst.kind == LS_TYPE_ARRAY || src.kind == LS_TYPE_ARRAY) return sameBaseType(dst, src);
		if (src.kind == LS_TYPE_STRING) return dst.kind == LS_TYPE_STRING;
		if (src.kind == LS_TYPE_UNTYPED_INT) return isNumeric(dst);
		if (src.kind == LS_TYPE_UNTYPED_FLOAT) return isFloat(dst);
		if (dst.nullable) {
			if (src.nullable) return sameBaseType(dst, src);
			TypeRef nonnull_dst = dst;
			nonnull_dst.nullable = false;
			return sameBaseType(nonnull_dst, src);
		}
		if (src.nullable) return false;
		if (dst.kind == LS_TYPE_FUNCTION || src.kind == LS_TYPE_FUNCTION) return functionTypesEqual(dst, src);
		return sameBaseType(dst, src);
	}

	bool canCompare(TypeRef left, TypeRef right) const {
		if (left.kind == LS_TYPE_NULL_VALUE || right.kind == LS_TYPE_NULL_VALUE) return true;
		if (left.kind == LS_TYPE_ENUM || right.kind == LS_TYPE_ENUM) return sameBaseType(left, right);
		if (left.kind == LS_TYPE_STRING || right.kind == LS_TYPE_STRING) return left.kind == LS_TYPE_STRING && right.kind == LS_TYPE_STRING;
		if (left.kind == LS_TYPE_BOOL || right.kind == LS_TYPE_BOOL) return left.kind == LS_TYPE_BOOL && right.kind == LS_TYPE_BOOL;
		return isNumeric(left) && isNumeric(right) && sameBaseType(left, right);
	}

	bool getNullablePromotion(i32 expr_idx, ls_string_view* var_name, TypeRef* promoted_type, bool* promote_true_branch) {
		if (expr_idx < 0) return false;
		Expr& cond = m_module.expressions[expr_idx];
		if (cond.kind != Expr::BINARY) return false;
		if (cond.token.type != Token::BANG_EQUAL && cond.token.type != Token::EQUAL_EQUAL) return false;

		i32 var_expr_idx = -1;
		i32 null_expr_idx = -1;
		Expr& left = m_module.expressions[cond.left];
		Expr& right = m_module.expressions[cond.right];
		if (left.kind == Expr::VAR && right.kind == Expr::NULL_LITERAL) {
			var_expr_idx = cond.left;
			null_expr_idx = cond.right;
		}
		else if (right.kind == Expr::VAR && left.kind == Expr::NULL_LITERAL) {
			var_expr_idx = cond.right;
			null_expr_idx = cond.left;
		}
		if (var_expr_idx < 0 || null_expr_idx < 0) return false;

		Expr& var_expr = m_module.expressions[var_expr_idx];
		const i32 local_idx = findLocal(var_expr.name);
		if (local_idx < 0) return false;
		if (!m_locals[local_idx].type.nullable) return false;

		*var_name = var_expr.name;
		*promoted_type = m_locals[local_idx].type;
		promoted_type->nullable = false;
		*promote_true_branch = cond.token.type == Token::BANG_EQUAL;
		return true;
	}

	void checkStmtWithPromotion(i32 stmt_idx, TypeRef return_type, ls_string_view var_name, TypeRef promoted_type) {
		if (stmt_idx < 0) return;
		const i32 old_size = (i32)m_locals.size();
		m_scope_starts.push_back(old_size);
		const i32 existing = findLocal(var_name);
		LocalInfo& promoted = m_locals.emplace_back();
		promoted.name = var_name;
		promoted.type = promoted_type;
		promoted.is_const = existing >= 0 ? m_locals[existing].is_const : false;
		checkStmt(stmt_idx, return_type);
		m_locals.resize(old_size);
		m_scope_starts.pop_back();
	}

	bool isScalar(TypeRef type) const {
		return type.kind == LS_TYPE_BOOL
			|| type.kind == LS_TYPE_I8 || type.kind == LS_TYPE_U8
			|| type.kind == LS_TYPE_I16 || type.kind == LS_TYPE_U16
			|| type.kind == LS_TYPE_I32 || type.kind == LS_TYPE_U32
			|| type.kind == LS_TYPE_I64 || type.kind == LS_TYPE_U64
			|| type.kind == LS_TYPE_F32 || type.kind == LS_TYPE_F64
			|| type.kind == LS_TYPE_UNTYPED_INT || type.kind == LS_TYPE_UNTYPED_FLOAT;
	}

	bool isIntegral(TypeRef type) const {
		return type.kind == LS_TYPE_I8 || type.kind == LS_TYPE_U8
			|| type.kind == LS_TYPE_I16 || type.kind == LS_TYPE_U16
			|| type.kind == LS_TYPE_I32 || type.kind == LS_TYPE_U32
			|| type.kind == LS_TYPE_I64 || type.kind == LS_TYPE_U64;
	}

	bool isFloat(TypeRef type) const {
		return type.kind == LS_TYPE_F32 || type.kind == LS_TYPE_F64;
	}

	bool isNumeric(TypeRef type) const {
		return isIntegral(type) || isFloat(type);
	}

	bool isCompileTimeZero(i32 expr_idx) {
		if (expr_idx < 0 || expr_idx >= m_module.expressions.size()) return false;
		Expr& e = m_module.expressions[expr_idx];
		switch (e.kind) {
			case Expr::NUMBER: return e.number == 0.0;
			case Expr::UNARY:
				if (e.token.type != Token::MINUS && e.token.type != Token::PLUS) return false;
				return isCompileTimeZero(e.right);
			case Expr::CAST: return isCompileTimeZero(e.left);
			case Expr::VAR: {
				const i32 global_idx = m_module.findGlobal(e.name);
				if (global_idx < 0) return false;
				const GlobalDecl& global = m_module.globals[global_idx];
				if (!global.is_const || global.expr < 0) return false;
				return isCompileTimeZero(global.expr);
			}
			default: return false;
		}
	}

	bool getCompileTimeInteger(i32 expr_idx, i64* value) {
		if (expr_idx < 0 || expr_idx >= m_module.expressions.size()) return false;
		Expr& e = m_module.expressions[expr_idx];
		switch (e.kind) {
			case Expr::NUMBER:
				*value = (i64)e.number;
				return true;
			case Expr::UNARY:
				if (e.token.type != Token::MINUS && e.token.type != Token::PLUS) return false;
				if (!getCompileTimeInteger(e.right, value)) return false;
				if (e.token.type == Token::MINUS) *value = -*value;
				return true;
			case Expr::CAST:
				return getCompileTimeInteger(e.left, value);
			default:
				return false;
		}
	}

	TypeRef concreteNumberType(TypeRef type, const TypeRef* expected) const {
		if (type.kind == LS_TYPE_UNTYPED_INT) {
			if (expected) {
				TypeRef target = *expected;
				target.nullable = false;
				if (isNumeric(target)) return target;
			}
			return {LS_TYPE_I32, {}, -1};
		}
		if (type.kind == LS_TYPE_UNTYPED_FLOAT) {
			if (expected) {
				TypeRef target = *expected;
				target.nullable = false;
				if (isFloat(target)) return target;
			}
			return {LS_TYPE_F32, {}, -1};
		}
		return type;
	}

	void checkFunction(FunctionDecl& fn) {
		checkFunctionSignature(fn);
		m_locals.clear();
		m_scope_starts.clear();
		m_function_scope_starts.clear();
		m_loop_labels.clear();
		m_loop_scope_starts.clear();
		m_declared_labels.clear();
		m_label_scope_starts.clear();
		m_scope_starts.push_back(0);
		m_function_scope_starts.push_back((i32)m_local_functions.size());
		for (Param& p : fn.params) {
			LocalInfo& local = m_locals.emplace_back();
			local.name = p.name;
			local.type = p.type;
			local.is_const = !p.is_ref;
		}
		checkStmt(fn.body, fn.return_type);
		m_local_functions.resize(m_function_scope_starts.back());
		m_function_scope_starts.pop_back();
	}

	bool checkFunctionSignature(FunctionDecl& fn) {
		for (i32 i = 0; i < fn.params.size(); ++i) {
			for (i32 j = i + 1; j < fn.params.size(); ++j) {
				if (equalStrings(fn.params[i].name, fn.params[j].name)) {
					m_output.errorAt(fn.params[j].token, "Duplicate parameter '", fn.params[j].name, "'");
					return false;
				}
			}
			resolveType(fn.params[i].type);
			if (fn.params[i].is_ref && fn.params[i].type.nullable) {
				m_output.errorAt(fn.params[i].token, "Ref parameter type can not be nullable");
				return false;
			}
		}
		resolveType(fn.return_type);
		return !m_output.has_error;
	}

	void checkNestedFunction(FunctionDecl& fn) {
		checkFunctionSignature(fn);
		std::vector<LocalInfo> old_locals;
		std::vector<i32> old_scope_starts;
		for (LocalInfo local : m_locals) old_locals.push_back(local);
		for (i32 scope_start : m_scope_starts) old_scope_starts.push_back(scope_start);
		m_locals.clear();
		m_scope_starts.clear();
		m_scope_starts.push_back(0);
		for (Param& p : fn.params) {
			LocalInfo& local = m_locals.emplace_back();
			local.name = p.name;
			local.type = p.type;
			local.is_const = !p.is_ref;
		}
		checkStmt(fn.body, fn.return_type);
		m_locals.clear();
		m_scope_starts.clear();
		for (LocalInfo local : old_locals) m_locals.push_back(local);
		for (i32 scope_start : old_scope_starts) m_scope_starts.push_back(scope_start);
	}

	void checkGlobals() {
		m_locals.clear();
		m_scope_starts.clear();
		m_local_functions.clear();
		m_function_scope_starts.clear();
		m_scope_starts.push_back(0);
		m_function_scope_starts.push_back(0);
		for (GlobalDecl& global : m_module.globals) {
			TypeRef type = global.type;
			if (type.kind != LS_TYPE_INVALID) resolveType(type);
			if (global.expr >= 0) {
				TypeRef* expected = type.kind == LS_TYPE_INVALID ? nullptr : &type;
				TypeRef expr_type = checkExpr(global.expr, expected);
				if (type.kind == LS_TYPE_INVALID) type = expr_type;
				else if (!canAssign(type, expr_type)) m_output.errorAt(global.token, "Initializer type mismatch");
			}
			else if (global.is_undefined_init) {
				if (global.is_const) m_output.errorAt(global.token, "Const declaration can not use undefined initializer");
				if (type.kind == LS_TYPE_INVALID) m_output.errorAt(global.token, "Undefined initializer requires explicit type");
			}
			else if (type.kind == LS_TYPE_INVALID) {
				m_output.errorAt(global.token, "Global variable needs type or initializer");
			}
			else {
				m_output.errorAt(global.token, "Variable declaration requires initializer");
			}
			if (type.kind != LS_TYPE_INVALID) resolveType(type);
			global.type = type;
			LocalInfo& local = m_locals.emplace_back();
			local.name = global.name;
			local.type = type;
			local.is_const = global.is_const;
		}
	}

	TypeRef checkExpr(i32 expr_idx, const TypeRef* expected = nullptr) {
		if (expr_idx < 0) return {};
		Expr& e = m_module.expressions[expr_idx];
			switch (e.kind) {
			case Expr::NUMBER:
				e.type = concreteNumberType(e.type, expected);
				return e.type;
			case Expr::STRING_LITERAL:
				e.type = {LS_TYPE_STRING, {}, -1};
				return e.type;
			case Expr::BOOL_LITERAL: return e.type;
			case Expr::NULL_LITERAL: return e.type;
			case Expr::VAR: {
				const i32 local_idx = findLocal(e.name);
				if (local_idx >= 0) {
					e.type = m_locals[local_idx].type;
					return e.type;
				}
				const i32 global_idx = m_module.findGlobal(e.name);
				if (global_idx >= 0) {
					e.type = m_module.globals[global_idx].type;
					return e.type;
				}
				const i32 local_fn_idx = findLocalFunction(e.name);
				if (local_fn_idx >= 0) {
					e.kind = Expr::FUNCTION_REF;
					e.type = functionTypeFromFunction(m_module.functions[local_fn_idx]);
					e.left = local_fn_idx;
					e.boolean = false;
					return e.type;
				}
				const i32 fn_idx = m_module.findFunction(e.name);
				if (fn_idx >= 0) {
					e.kind = Expr::FUNCTION_REF;
					e.type = functionTypeFromFunction(m_module.functions[fn_idx]);
					e.left = fn_idx;
					e.boolean = false;
					return e.type;
				}
				const i32 native_idx = m_module.findNativeFunction(e.name);
				if (native_idx >= 0) {
					e.kind = Expr::FUNCTION_REF;
					e.type = functionTypeFromNativeFunction(m_module.native_functions[native_idx]);
					e.left = native_idx;
					e.boolean = true;
					return e.type;
				}
				m_output.errorAt(e.token, "Unknown variable '", e.name, "'");
				return {};
			}
			case Expr::FUNCTION_REF:
				return e.type;
			case Expr::FIELD: {
				const ls_string_view qualified_name = getExpressionName(expr_idx);
				const i32 fn_idx = m_module.findFunction(qualified_name);
				if (fn_idx >= 0) {
					e.kind = Expr::FUNCTION_REF;
					e.type = functionTypeFromFunction(m_module.functions[fn_idx]);
					e.left = fn_idx;
					e.boolean = false;
					return e.type;
				}
				const i32 native_idx = m_module.findNativeFunction(qualified_name);
				if (native_idx >= 0) {
					e.kind = Expr::FUNCTION_REF;
					e.type = functionTypeFromNativeFunction(m_module.native_functions[native_idx]);
					e.left = native_idx;
					e.boolean = true;
					return e.type;
				}
				if (checkQualifiedEnumMember(e, qualified_name)) return e.type;
				TypeRef base = checkExpr(e.left);
				if (base.nullable) {
					m_output.errorAt(e.token, "Nullable value must be checked for null");
					return {};
				}
				if (base.kind != LS_TYPE_STRUCT || base.struct_index < 0) {
					m_output.errorAt(e.token, "Field access on non-struct");
					return {};
				}
				StructDecl& s = m_module.structs[base.struct_index];
				for (FieldDecl& f : s.fields) {
					if (equalStrings(f.name, e.name)) {
						e.type = f.type;
						return e.type;
					}
				}
				m_output.errorAt(e.token, "Unknown field '", e.name, "'");
				return {};
			}
			case Expr::INDEX: {
				TypeRef base = checkExpr(e.left);
				if (base.nullable) {
					m_output.errorAt(e.token, "Nullable value must be checked for null");
					return {};
				}
				if (base.kind != LS_TYPE_ARRAY) {
					m_output.errorAt(e.token, "Indexing requires array type");
					return {};
				}
				TypeRef idx_type = checkExpr(e.right);
				if (!isIntegral(idx_type) && idx_type.kind != LS_TYPE_UNTYPED_INT) {
					m_output.errorAt(e.token, "Array index must be integer");
					return {};
				}
				i64 idx_value = 0;
				if (getCompileTimeInteger(e.right, &idx_value) && (idx_value < 0 || idx_value >= base.array_size)) {
					m_output.errorAt(e.token, "Array index out of range");
					return {};
				}
				e.type = {base.element_kind, base.element_name, base.struct_index, e.token, false};
				return e.type;
			}
			case Expr::UNARY: {
				TypeRef right = checkExpr(e.right, expected);
				if (right.nullable) {
					m_output.errorAt(e.token, "Nullable value must be checked for null");
					return {};
				}
				if (e.token.type == Token::NOT) e.type = {LS_TYPE_BOOL, {}, -1};
				else e.type = right;
				return e.type;
			}
			case Expr::REF: {
				m_output.errorAt(e.token, "'ref' can be only used for ref arguments");
				return {};
			}
			case Expr::BINARY: {
				const bool is_comparison = e.token.type == Token::GT || e.token.type == Token::LT || e.token.type == Token::GT_EQUAL || e.token.type == Token::LT_EQUAL
					|| e.token.type == Token::EQUAL_EQUAL || e.token.type == Token::BANG_EQUAL || e.token.type == Token::AND || e.token.type == Token::OR;
				const TypeRef* operand_expected = !is_comparison && expected && isNumeric(*expected) ? expected : nullptr;
				TypeRef left = checkExpr(e.left, operand_expected);
				// For comparisons, pass the left operand's type as expected type to the right
				TypeRef right = checkExpr(e.right, &left);
				if (left.kind == LS_TYPE_STRING || right.kind == LS_TYPE_STRING) {
					if (e.token.type != Token::PLUS || left.kind != LS_TYPE_STRING || right.kind != LS_TYPE_STRING) {
						m_output.errorAt(e.token, "String operation requires string operands");
						return {};
					}
					e.type = {LS_TYPE_STRING, {}, -1};
					return e.type;
				}
				const bool is_eq = e.token.type == Token::EQUAL_EQUAL || e.token.type == Token::BANG_EQUAL;
				const bool null_cmp = left.kind == LS_TYPE_NULL_VALUE || right.kind == LS_TYPE_NULL_VALUE;
				if (!is_eq && (left.nullable || right.nullable)) {
					m_output.errorAt(e.token, "Nullable value must be checked for null");
					return {};
				}
				if (is_eq && !null_cmp && (left.nullable || right.nullable)) {
					m_output.errorAt(e.token, "Nullable value must be checked for null");
					return {};
				}
				switch (e.token.type) {
					case Token::GT: case Token::LT: case Token::GT_EQUAL: case Token::LT_EQUAL:
					case Token::EQUAL_EQUAL: case Token::BANG_EQUAL:
						if (!canCompare(left, right)) {
							m_output.errorAt(e.token, "Comparison type mismatch");
							return {};
						}
						e.type = {LS_TYPE_BOOL, {}, -1};
						return e.type;
					case Token::AND: case Token::OR:
						if (left.kind != LS_TYPE_BOOL || right.kind != LS_TYPE_BOOL) {
							m_output.errorAt(e.token, "Boolean operation requires bool operands");
							return {};
						}
						e.type = {LS_TYPE_BOOL, {}, -1};
						return e.type;
					default:
						if (!isNumeric(left) || !isNumeric(right)) {
							m_output.errorAt(e.token, "Arithmetic operation requires numeric operands");
							return {};
						}
							if (!sameBaseType(left, right)) {
								m_output.errorAt(e.token, "Arithmetic operands must have the same type");
								return {};
							}
							if (e.token.type == Token::PERCENT && (!isIntegral(left) || !isIntegral(right))) {
								m_output.errorAt(e.token, "Modulo operation requires integer operands");
								return {};
							}
							if ((e.token.type == Token::SLASH || e.token.type == Token::PERCENT)
								&& isIntegral(left)
								&& isIntegral(right)
								&& isCompileTimeZero(e.right)) {
								m_output.errorAt(m_module.expressions[e.right].token, "Division or modulo by zero");
								return {};
							}
							e.type = left;
						return e.type;
				}
			}
			case Expr::CALL: {
				i32 fn_idx = -1;
				i32 native_idx = -1;
				const ls_string_view callee_name = resolveCallName(e, &fn_idx, &native_idx);
				if (fn_idx < 0 && native_idx < 0) {
					TypeRef callee_type = checkExpr(e.left);
					if (callee_type.kind != LS_TYPE_FUNCTION || callee_type.struct_index < 0 || callee_type.struct_index >= m_module.function_types.size()) {
						if (empty(callee_name)) m_output.errorAt(m_module.expressions[e.left].token, "Unsupported callee");
						else m_output.errorAt(m_module.expressions[e.left].token, "Unknown function '", callee_name, "'");
						return {};
					}
					FunctionTypeDecl& fn_type = m_module.function_types[callee_type.struct_index];
					if (fn_type.params.size() != e.args.size()) {
						m_output.errorAt(e.token, "Wrong number of arguments");
						return {};
					}
					for (i32 i = 0; i < fn_type.params.size(); ++i) {
						Expr& arg_expr = m_module.expressions[e.args[i]];
						if (arg_expr.kind == Expr::REF) {
							m_output.errorAt(arg_expr.token, "Unexpected ref argument");
							continue;
						}
						TypeRef arg_type = checkExpr(e.args[i], &fn_type.params[i]);
						if (!canAssign(fn_type.params[i], arg_type)) m_output.errorAt(arg_expr.token, "Argument type mismatch");
					}
					e.type = fn_type.return_type;
					return e.type;
				}
				const bool is_native = native_idx >= 0;
				const std::vector<Param>& params = is_native ? m_module.native_functions[native_idx].params : m_module.functions[fn_idx].params;
				const TypeRef return_type = is_native ? m_module.native_functions[native_idx].return_type : m_module.functions[fn_idx].return_type;
				const i32 receiver_arg_count = e.method_receiver >= 0 ? 1 : 0;
				if (params.size() != e.args.size() + receiver_arg_count) {
					m_output.errorAt(e.token, "Wrong number of arguments");
					return {};
				}
				for (i32 i = 0; i < params.size(); ++i) {
					const i32 arg_idx = i - receiver_arg_count;
					const i32 expr_idx = i == 0 && e.method_receiver >= 0 ? e.method_receiver : e.args[arg_idx];
					if (params[i].is_ref) {
						Expr& arg_expr = m_module.expressions[expr_idx];
						if (arg_expr.kind != Expr::REF) {
							m_output.errorAt(arg_expr.token, "Expected ref argument");
							continue;
						}
						const i32 target_expr = arg_expr.right;
						if (!isAssignableExpr(target_expr)) {
							m_output.errorAt(arg_expr.token, "Ref argument must be assignable");
							continue;
						}
						if (isConstExpr(target_expr)) {
							m_output.errorAt(arg_expr.token, "Can not pass const as ref argument");
							continue;
						}
						TypeRef arg_type = checkExpr(target_expr, &params[i].type);
						if (arg_type.nullable) {
							m_output.errorAt(m_module.expressions[target_expr].token, "Ref argument can not be nullable");
							continue;
						}
						if (!canAssign(params[i].type, arg_type)) m_output.errorAt(m_module.expressions[target_expr].token, "Argument type mismatch");
					}
					else {
						Expr& arg_expr = m_module.expressions[expr_idx];
						if (arg_expr.kind == Expr::REF) {
							m_output.errorAt(arg_expr.token, "Unexpected ref argument");
							continue;
						}
						TypeRef arg_type = checkExpr(expr_idx, &params[i].type);
						if (!canAssign(params[i].type, arg_type)) m_output.errorAt(m_module.expressions[expr_idx].token, "Argument type mismatch");
					}
				}
				e.type = return_type;
				return e.type;
			}
			case Expr::CAST: {
				TypeRef src = checkExpr(e.left);
				resolveType(e.cast_type);
				const bool is_enum_integer_cast = (src.kind == LS_TYPE_ENUM && isIntegral(e.cast_type)) || (isIntegral(src) && e.cast_type.kind == LS_TYPE_ENUM);
				if ((!isScalar(src) || !isScalar(e.cast_type)) && !is_enum_integer_cast) {
					m_output.errorAt(e.token, "Invalid cast");
					return {};
				}
				e.type = e.cast_type;
				return e.type;
			}
			case Expr::STRUCT_LITERAL:
			case Expr::CONSTRUCTOR: {
				TypeRef target = expected ? *expected : TypeRef{};
				if (e.kind == Expr::CONSTRUCTOR) {
					target = {LS_TYPE_STRUCT, e.name, m_module.findStruct(e.name)};
				}
				if (target.kind != LS_TYPE_STRUCT || target.struct_index < 0) {
					m_output.errorAt(e.token, "Can not infer struct literal type");
					return {};
				}
				StructDecl& s = m_module.structs[target.struct_index];
				if (s.fields.size() != e.args.size()) {
					m_output.errorAt(e.token, "Struct literal field count mismatch");
					return {};
				}
				for (i32 i = 0; i < e.args.size(); ++i) {
					TypeRef arg_type = checkExpr(e.args[i], &s.fields[i].type);
					if (!canAssign(s.fields[i].type, arg_type)) m_output.errorAt(m_module.expressions[e.args[i]].token, "Struct literal type mismatch");
				}
				e.type = target;
				return e.type;
			}
			case Expr::ENUM_LITERAL: {
				if (!expected || expected->kind != LS_TYPE_ENUM) {
					m_output.errorAt(e.token, "Can not infer enum literal type");
					return {};
				}
				const i32 enum_idx = expected->struct_index;
				if (enum_idx < 0 || enum_idx >= m_module.enums.size()) {
					m_output.errorAt(e.token, "Invalid enum type");
					return {};
				}
				EnumDecl& en = m_module.enums[enum_idx];
				const i32 member_idx = m_module.findEnumMember(en, e.name);
				if (member_idx < 0) {
					m_output.errorAt(e.token, "Unknown enum member '", e.name, "'");
					return {};
				}
				e.type = *expected;
				return e.type;
			}
		}
		ASSERT(false);
		return {};
	}

	i32 enumPatternMember(MatchPattern& pattern, TypeRef subject_type) {
		if (subject_type.kind != LS_TYPE_ENUM || pattern.kind != MatchPattern::VALUE || pattern.start_expr < 0) return -1;
		Expr& e = m_module.expressions[pattern.start_expr];
		if (e.kind != Expr::ENUM_LITERAL) return -1;
		return m_module.findEnumMember(m_module.enums[subject_type.struct_index], e.name);
	}

	void checkMatchPattern(MatchPattern& pattern, TypeRef subject_type, bool* has_default) {
		if (pattern.kind == MatchPattern::DEFAULT) {
			if (*has_default) m_output.errorAt(pattern.token, "Duplicate match fallback");
			*has_default = true;
			return;
		}
		TypeRef start_type = checkExpr(pattern.start_expr, &subject_type);
		if (!canAssign(subject_type, start_type)) {
			m_output.errorAt(pattern.token, "Match pattern type mismatch");
			return;
		}
		if (pattern.kind == MatchPattern::RANGE) {
			if (!isNumeric(subject_type)) {
				m_output.errorAt(pattern.token, "Match range requires numeric type");
				return;
			}
			TypeRef end_type = checkExpr(pattern.end_expr, &subject_type);
			if (!canAssign(subject_type, end_type)) m_output.errorAt(pattern.token, "Match range type mismatch");
		}
	}

	void checkMatchStmt(Stmt& stmt, TypeRef return_type) {
		TypeRef subject_type = checkExpr(stmt.expr);
		if (!isScalar(subject_type) && subject_type.kind != LS_TYPE_ENUM && subject_type.kind != LS_TYPE_STRING) {
			m_output.errorAt(stmt.token, "Match requires scalar, enum or string value");
			return;
		}

		bool has_default = false;
		std::vector<u8> covered_enum_members;
		if (subject_type.kind == LS_TYPE_ENUM && subject_type.struct_index >= 0) {
			covered_enum_members.resize(m_module.enums[subject_type.struct_index].members.size());
			for (u8& covered : covered_enum_members) covered = 0;
		}

		for (i32 arm_idx : stmt.children) {
			MatchArm& arm = m_module.match_arms[arm_idx];
			for (i32 pattern_idx : arm.patterns) {
				MatchPattern& pattern = m_module.match_patterns[pattern_idx];
				checkMatchPattern(pattern, subject_type, &has_default);
				const i32 enum_member = enumPatternMember(pattern, subject_type);
				if (enum_member >= 0) {
					if (covered_enum_members[enum_member]) {
						m_output.errorAt(pattern.token, "Duplicate enum match case");
						return;
					}
					covered_enum_members[enum_member] = 1;
				}
			}
			checkStmt(arm.stmt, return_type);
		}

		if (subject_type.kind == LS_TYPE_ENUM && !has_default) {
			for (u8 covered : covered_enum_members) {
				if (covered) continue;
				m_output.errorAt(stmt.token, "Enum match must be exhaustive or have fallback");
				return;
			}
		}
	}

	void checkStmt(i32 stmt_idx, TypeRef return_type) {
		if (stmt_idx < 0 || m_output.has_error) return;
		Stmt& stmt = m_module.statements[stmt_idx];
		switch (stmt.kind) {
			case Stmt::BLOCK: {
				const i32 old_size = (i32)m_locals.size();
				const i32 old_function_size = (i32)m_local_functions.size();
				const i32 old_loop_size = (i32)m_loop_labels.size();
				const i32 old_label_size = (i32)m_declared_labels.size();
				m_scope_starts.push_back(old_size);
				m_function_scope_starts.push_back(old_function_size);
				m_loop_scope_starts.push_back(old_loop_size);
				m_label_scope_starts.push_back(old_label_size);
				for (i32 child : stmt.children) checkStmt(child, return_type);
				m_locals.resize(old_size);
				m_local_functions.resize(old_function_size);
				m_loop_labels.resize(old_loop_size);
				m_declared_labels.resize(old_label_size);
				m_scope_starts.pop_back();
				m_function_scope_starts.pop_back();
				m_loop_scope_starts.pop_back();
				m_label_scope_starts.pop_back();
				break;
			}
			case Stmt::VAR_DECL: {
				const i32 scope_start = m_scope_starts.empty() ? 0 : m_scope_starts.back();
				for (i32 i = scope_start; i < m_locals.size(); ++i) {
					if (equalStrings(m_locals[i].name, stmt.name)) {
						m_output.errorAt(stmt.token, "Duplicate local '", stmt.name, "'");
						return;
					}
				}
				if (localFunctionInCurrentScope(stmt.name)) {
					m_output.errorAt(stmt.token, "Duplicate local '", stmt.name, "'");
					return;
				}
				TypeRef type = stmt.type;
				if (type.kind != LS_TYPE_INVALID) resolveType(type);
				if (stmt.expr >= 0) {
					TypeRef* expected = type.kind == LS_TYPE_INVALID ? nullptr : &type;
					TypeRef expr_type = checkExpr(stmt.expr, expected);
					if (type.kind == LS_TYPE_INVALID) type = expr_type;
					else if (!canAssign(type, expr_type)) m_output.errorAt(stmt.token, "Initializer type mismatch");
				}
				else if (stmt.is_undefined_init) {
					if (stmt.is_const) m_output.errorAt(stmt.token, "Const declaration can not use undefined initializer");
					if (type.kind == LS_TYPE_INVALID) m_output.errorAt(stmt.token, "Undefined initializer requires explicit type");
				}
				else {
					m_output.errorAt(stmt.token, "Variable declaration requires initializer");
				}
				if (type.kind != LS_TYPE_INVALID) resolveType(type);
				stmt.type = type;
				stmt.local_index = (i32)m_locals.size();
				LocalInfo& local = m_locals.emplace_back();
				local.name = stmt.name;
				local.type = type;
				local.is_const = stmt.is_const;
				break;
			}
			case Stmt::FN_DECL: {
				if (stmt.left < 0 || stmt.left >= m_module.functions.size()) return;
				FunctionDecl& fn = m_module.functions[stmt.left];
				const i32 scope_start = m_scope_starts.empty() ? 0 : m_scope_starts.back();
				for (i32 i = scope_start; i < m_locals.size(); ++i) {
					if (equalStrings(m_locals[i].name, fn.local_name)) {
						m_output.errorAt(fn.token, "Duplicate local '", fn.local_name, "'");
						return;
					}
				}
				if (localFunctionInCurrentScope(fn.local_name)) {
					m_output.errorAt(fn.token, "Duplicate function '", fn.local_name, "'");
					return;
				}
				LocalFunctionInfo& local_fn = m_local_functions.emplace_back();
				local_fn.name = fn.local_name;
				local_fn.function_index = stmt.left;
				checkNestedFunction(fn);
				break;
			}
			case Stmt::EXPR: checkExpr(stmt.expr); break;
			case Stmt::ASSIGN: {
				TypeRef left = checkExpr(stmt.left);
				TypeRef right = checkExpr(stmt.right, &left);
				if (!canAssign(left, right)) m_output.errorAt(stmt.token, "Assignment type mismatch");
				if (stmt.assign_op == Token::SLASH_EQUAL
					&& isIntegral(left)
					&& isIntegral(right)
					&& isCompileTimeZero(stmt.right)) {
					m_output.errorAt(m_module.expressions[stmt.right].token, "Division or modulo by zero");
				}
				Expr& lhs = m_module.expressions[stmt.left];
				if (lhs.kind == Expr::VAR) {
					const i32 idx = findLocal(lhs.name);
					if (idx >= 0 && m_locals[idx].is_const) m_output.errorAt(lhs.token, "Can not assign to const '", lhs.name, "'");
					const i32 global_idx = m_module.findGlobal(lhs.name);
					if (global_idx >= 0 && m_module.globals[global_idx].is_const) m_output.errorAt(lhs.token, "Can not assign to const '", lhs.name, "'");
				}
				break;
			}
			case Stmt::WHILE: {
				if (!empty(stmt.name)) {
					const i32 label_scope_start = m_label_scope_starts.empty() ? 0 : m_label_scope_starts.back();
					for (i32 i = label_scope_start; i < m_declared_labels.size(); ++i) {
						if (!equalStrings(m_declared_labels[i], stmt.name)) continue;
						m_output.errorAt(stmt.token, "Duplicate loop label '", stmt.name, "'");
						return;
					}
					m_declared_labels.push_back(stmt.name);
				}
				TypeRef cond = checkExpr(stmt.expr);
				if (cond.kind != LS_TYPE_BOOL) m_output.errorAt(stmt.token, "While condition must be bool");
				m_loop_labels.push_back(stmt.name);
				checkStmt(stmt.right, return_type);
				m_loop_labels.pop_back();
				break;
			}
			case Stmt::FOR: {
				if (!empty(stmt.name)) {
					const i32 label_scope_start = m_label_scope_starts.empty() ? 0 : m_label_scope_starts.back();
					for (i32 i = label_scope_start; i < m_declared_labels.size(); ++i) {
						if (!equalStrings(m_declared_labels[i], stmt.name)) continue;
						m_output.errorAt(stmt.token, "Duplicate loop label '", stmt.name, "'");
						return;
					}
					m_declared_labels.push_back(stmt.name);
				}
				TypeRef end_type = checkExpr(stmt.left);
				if (!isNumeric(end_type)) {
					m_output.errorAt(stmt.token, "For range requires numeric bounds");
					return;
				}
				TypeRef start_type = checkExpr(stmt.expr, &end_type);
				if (!isNumeric(start_type) || !sameBaseType(start_type, end_type)) {
					m_output.errorAt(stmt.token, "For range bounds must have the same numeric type");
					return;
				}
				const i32 old_size = (i32)m_locals.size();
				LocalInfo& local = m_locals.emplace_back();
				local.name = stmt.label_name;
				local.type = start_type;
				local.is_const = true;
				m_loop_labels.push_back(stmt.name);
				checkStmt(stmt.right, return_type);
				m_loop_labels.pop_back();
				m_locals.resize(old_size);
				break;
			}
			case Stmt::BREAK:
			case Stmt::CONTINUE: {
				if (m_loop_labels.empty()) {
					m_output.errorAt(stmt.token, stmt.kind == Stmt::BREAK ? "'break' used outside loop" : "'continue' used outside loop");
					break;
				}
				if (!empty(stmt.name)) {
					bool found = false;
					for (i32 i = (i32)m_loop_labels.size() - 1; i >= 0; --i) {
						if (equalStrings(m_loop_labels[i], stmt.name)) {
							found = true;
							break;
						}
					}
					if (!found) {
						m_output.errorAt(stmt.token, "Unknown loop label '", stmt.name, "'");
					}
				}
				break;
			}
			case Stmt::IF: {
				TypeRef cond = checkExpr(stmt.expr);
				if (cond.kind != LS_TYPE_BOOL) m_output.errorAt(stmt.token, "If condition must be bool");
				ls_string_view promoted_name;
				TypeRef promoted_type;
				bool promote_true_branch = false;
				if (getNullablePromotion(stmt.expr, &promoted_name, &promoted_type, &promote_true_branch)) {
					if (promote_true_branch) {
						checkStmtWithPromotion(stmt.left, return_type, promoted_name, promoted_type);
						if (stmt.right >= 0) checkStmt(stmt.right, return_type);
					}
					else {
						checkStmt(stmt.left, return_type);
						if (stmt.right >= 0) checkStmtWithPromotion(stmt.right, return_type, promoted_name, promoted_type);
					}
				}
				else {
					checkStmt(stmt.left, return_type);
					if (stmt.right >= 0) checkStmt(stmt.right, return_type);
				}
				break;
			}
			case Stmt::RETURN: {
				TypeRef actual = stmt.expr >= 0 ? checkExpr(stmt.expr, &return_type) : TypeRef{LS_TYPE_VOID, {}, -1};
				if (!canAssign(return_type, actual)) m_output.errorAt(stmt.token, "Return type mismatch");
				break;
			}
			case Stmt::DEFER: {
				if (stmt.left < 0) {
					m_output.errorAt(stmt.token, "Expected statement after defer");
					break;
				}
				Stmt& deferred = m_module.statements[stmt.left];
				if (deferred.kind == Stmt::RETURN) {
					m_output.errorAt(deferred.token, "Can not defer return");
					break;
				}
				checkStmt(stmt.left, return_type);
				break;
			}
			case Stmt::MATCH:
				checkMatchStmt(stmt, return_type);
				break;
		}
	}

	ls_module& m_module;
	OutputFormatter m_output;
	std::vector<LocalInfo> m_locals;
	std::vector<i32> m_scope_starts;
	std::vector<LocalFunctionInfo> m_local_functions;
	std::vector<i32> m_function_scope_starts;
	std::vector<ls_string_view> m_loop_labels;
	std::vector<i32> m_loop_scope_starts;
	std::vector<ls_string_view> m_declared_labels;
	std::vector<i32> m_label_scope_starts;
};

inline ls_string_view normalizeImportPathForPolicy(ls_string_view path) {
	if (!startsWith(path, "core:")) return path;
	ls_string_view name{data(path) + 5, data(path) + size(path)};
	if (!endsWith(name, ".lum")) return path;
	return ls_string_view{data(path), data(path) + size(path) - 4};
}

inline bool sameImportPathForPolicy(ls_string_view lhs, ls_string_view rhs) {
	return equalStrings(normalizeImportPathForPolicy(lhs), normalizeImportPathForPolicy(rhs));
}

static void registerCoreMath(ls_module& module, ls_string_view prefix) {
	const TypeRef f32_type(LS_TYPE_F32);
	const TypeRef f64_type(LS_TYPE_F64);
	const TypeRef f32_params[] = {f32_type};
	const TypeRef f64_params[] = {f64_type};
	const ls_string_view sin_name = makeStringView("sin");
	const ls_string_view cos_name = makeStringView("cos");
	const ls_string_view sqrt_name = makeStringView("sqrt");
	const ls_string_view sin_f64_name = makeStringView("sin_f64");
	const ls_string_view cos_f64_name = makeStringView("cos_f64");
	const ls_string_view sqrt_f64_name = makeStringView("sqrt_f64");

	addNativeFunction(module, module.makeQualifiedName(prefix, sin_name), f32_type, std::span<const TypeRef>(f32_params, 1));
	addNativeFunction(module, module.makeQualifiedName(prefix, cos_name), f32_type, std::span<const TypeRef>(f32_params, 1));
	addNativeFunction(module, module.makeQualifiedName(prefix, sqrt_name), f32_type, std::span<const TypeRef>(f32_params, 1));
	addNativeFunction(module, module.makeQualifiedName(prefix, sin_f64_name), f64_type, std::span<const TypeRef>(f64_params, 1));
	addNativeFunction(module, module.makeQualifiedName(prefix, cos_f64_name), f64_type, std::span<const TypeRef>(f64_params, 1));
	addNativeFunction(module, module.makeQualifiedName(prefix, sqrt_f64_name), f64_type, std::span<const TypeRef>(f64_params, 1));
}

inline bool resolveImports(ls_module& module, ls_import_resolver_fn import_resolver, void* import_resolver_userdata) {
	std::vector<u8> state;

	struct Context {
		ls_module& module;
		ls_import_resolver_fn import_resolver;
		void* import_resolver_userdata;
		std::vector<u8>& state;
		OutputFormatter output;

		void ensureStateSize() {
			while (state.size() < module.imports.size()) state.push_back(0);
		}

		bool isDuplicateImport(i32 idx) {
			ImportDecl& import = module.imports[idx];
			for (i32 i = 0; i < idx; ++i) {
				ImportDecl& previous = module.imports[i];
				if (!equalStrings(import.alias, previous.alias)) continue;
				if (sameImportPathForPolicy(import.path, previous.path)) return true;
			}
			return false;
		}

		bool hasAliasCollision(i32 idx) {
			ImportDecl& import = module.imports[idx];
			if (empty(import.alias)) return false;
			for (i32 i = 0; i < idx; ++i) {
				ImportDecl& previous = module.imports[i];
				if (!equalStrings(import.alias, previous.alias)) continue;
				if (!sameImportPathForPolicy(import.path, previous.path)) return true;
			}
			return false;
		}

		bool resolveImport(i32 idx) {
			ensureStateSize();
			if (state[idx] == 2) return true;
			ImportDecl& import = module.imports[idx];
			if (state[idx] == 1) {
				output.errorAt(import.token, "Import cycle detected at '", import.path, "'");
				return false;
			}
			for (i32 i = 0; i < idx; ++i) {
				ImportDecl& previous = module.imports[i];
				if (!equalStrings(import.alias, previous.alias)) continue;
				if (!sameImportPathForPolicy(import.path, previous.path)) continue;
				if (state[i] == 1) {
					output.errorAt(import.token, "Import cycle detected at '", import.path, "'");
					return false;
				}
			}
			if (isDuplicateImport(idx)) {
				state[idx] = 2;
				import.processed = true;
				return true;
			}
			if (hasAliasCollision(idx)) {
				output.errorAt(import.token, "Import alias collision for '", import.alias, "'");
				return false;
			}

			if (sameImportPathForPolicy(import.path, makeStringView("core:math"))) {
				registerCoreMath(module, import.alias);
				state[idx] = 2;
				import.processed = true;
				return true;
			}

			if (!import_resolver) {
				output.errorAt(import.token, "Can not import '", import.path, "'");
				return false;
			}

			state[idx] = 1;
			// remember native function and import counts so we can mark newly added
			// functions and continue resolving newly discovered imports
			const i32 old_native_count = (i32)module.native_functions.size();
			const i32 old_import_count = (i32)module.imports.size();
			ls_string_view source;
			if (!import_resolver(import_resolver_userdata, import.path, import.alias, &source)) {
				output.errorAt(import.token, "Can not import '", import.path, "'");
				return false;
			}
			if (!empty(source) && !parse(module, source, import.alias, import.path)) return false;
			// For any native functions that were added by parsing this import, set
			// their canonical name to the normalized import path + member name so
			// external code can register callbacks by canonical identity.
			for (i32 n = old_native_count; n < (i32)module.native_functions.size(); ++n) {
				NativeFunctionDecl& fn = module.native_functions[n];
				ls_string_view owner = {};
				ls_string_view member = {};
				// fn.name is alias-qualified (alias.member). Extract the member.
				for (const char* c = data(fn.name) + size(fn.name); c != data(fn.name); --c) {
					if (*(c - 1) != '.') continue;
					owner = ls_string_view{data(fn.name), c - 1};
					member = ls_string_view{c, data(fn.name) + size(fn.name)};
					break;
				}
				if (empty(member)) member = fn.name;
				ls_string_view norm_path = normalizeImportPathForPolicy(import.path);
				ls_string_view canonical = module.makeQualifiedName(norm_path, member);
				fn.canonical_name = canonical;
			}

			ensureStateSize();
			for (i32 i = old_import_count; i < module.imports.size(); ++i) {
				if (!resolveImport(i)) return false;
			}

			state[idx] = 2;
			import.processed = true;
			return true;
		}
	};

	Context ctx {module, import_resolver, import_resolver_userdata, state};
	ctx.output.host = module.host;
	ctx.ensureStateSize();
	for (i32 i = 0; i < module.imports.size(); ++i) {
		if (!ctx.resolveImport(i)) return false;
	}
	return true;
}

bool compile(ls_module& module, ls_string_view source, ls_import_resolver_fn import_resolver, void* import_resolver_userdata, ls_string_view source_name) {
	return ls_module_parse(&module, source, source_name) 
		&& resolveImports(module, import_resolver, import_resolver_userdata)
		&& ls_module_typecheck(&module);
}

void OutputFormatter::print(int v) {
	char tmp[32];
	if (toCString(v, std::span<char>(tmp))) {
		print(tmp);
	}
}
extern "C" {

ls_result ls_module_typecheck(ls_module* module) {
	Checker checker(*module);
	return checker.check() ? LS_RESULT_OK : LS_RESULT_FAILURE;
}

ls_result ls_module_compile(
	ls_module* module,
	ls_string_view source,
	ls_string_view source_name,
	ls_import_resolver_fn import_resolver,
	void* import_resolver_userdata
) {
	if (!module) return LS_RESULT_FAILURE;
	return compile(*module, source, import_resolver, import_resolver_userdata, source_name) ? LS_RESULT_OK : LS_RESULT_FAILURE;
}

}
