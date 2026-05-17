#pragma once

#include "lumscript/lumscript_ast.h"
#include "lumscript/lumscript_diagnostics.h"

namespace Lumix::LumScript {

struct LocalInfo {
	StringView name;
	TypeRef type;
	bool is_const = false;
};

struct Checker {
	Checker(Module& module, Diagnostics& diagnostics)
		: m_module(module)
		, m_diagnostics(diagnostics)
		, m_locals(module.allocator)
		, m_scope_starts(module.allocator)
	{}

	bool check() {
		for (i32 i = 0; i < m_module.structs.size(); ++i) {
			for (i32 j = i + 1; j < m_module.structs.size(); ++j) {
				if (equalStrings(m_module.structs[i].name, m_module.structs[j].name)) {
					m_diagnostics.errorAt(m_module.structs[j].token, "Duplicate struct '", m_module.structs[j].name, "'");
					return false;
				}
			}
		}
		for (i32 i = 0; i < m_module.functions.size(); ++i) {
			for (i32 j = i + 1; j < m_module.functions.size(); ++j) {
				if (equalStrings(m_module.functions[i].name, m_module.functions[j].name)) {
					m_diagnostics.errorAt(m_module.functions[j].token, "Duplicate function '", m_module.functions[j].name, "'");
					return false;
				}
			}
			if (findNativeFunction(m_module.functions[i].name) >= 0) {
				m_diagnostics.errorAt(m_module.functions[i].token, "Duplicate function '", m_module.functions[i].name, "'");
				return false;
			}
			if (findGlobal(m_module.functions[i].name) >= 0) {
				m_diagnostics.errorAt(m_module.functions[i].token, "Duplicate declaration '", m_module.functions[i].name, "'");
				return false;
			}
		}
		for (i32 i = 0; i < m_module.globals.size(); ++i) {
			for (i32 j = i + 1; j < m_module.globals.size(); ++j) {
				if (equalStrings(m_module.globals[i].name, m_module.globals[j].name)) {
					m_diagnostics.errorAt(m_module.globals[j].token, "Duplicate global '", m_module.globals[j].name, "'");
					return false;
				}
			}
			if (findFunction(m_module.globals[i].name) >= 0 || findNativeFunction(m_module.globals[i].name) >= 0) {
				m_diagnostics.errorAt(m_module.globals[i].token, "Duplicate declaration '", m_module.globals[i].name, "'");
				return false;
			}
		}
		for (i32 i = 0; i < m_module.native_functions.size(); ++i) {
			for (i32 j = i + 1; j < m_module.native_functions.size(); ++j) {
				if (equalStrings(m_module.native_functions[i].name, m_module.native_functions[j].name)) {
					m_diagnostics.errorAt(m_module.native_functions[j].token, "Duplicate function '", m_module.native_functions[j].name, "'");
					return false;
				}
			}
		}
		for (i32 i = 0; i < m_module.structs.size(); ++i) {
			StructDecl& s = m_module.structs[i];
			for (i32 j = 0; j < s.fields.size(); ++j) {
				for (i32 k = j + 1; k < s.fields.size(); ++k) {
					if (equalStrings(s.fields[j].name, s.fields[k].name)) {
						m_diagnostics.errorAt(s.fields[k].token, "Duplicate field '", s.fields[k].name, "'");
						return false;
					}
				}
				resolveType(s.fields[j].type);
			}
		}
		for (i32 i = 0; i < m_module.enums.size(); ++i) {
			for (i32 j = i + 1; j < m_module.enums.size(); ++j) {
				if (equalStrings(m_module.enums[i].name, m_module.enums[j].name)) {
					m_diagnostics.errorAt(m_module.enums[j].token, "Duplicate enum '", m_module.enums[j].name, "'");
					return false;
				}
			}
			for (i32 j = 0; j < m_module.enums[i].members.size(); ++j) {
				for (i32 k = j + 1; k < m_module.enums[i].members.size(); ++k) {
					if (equalStrings(m_module.enums[i].members[j].name, m_module.enums[i].members[k].name)) {
						m_diagnostics.errorAt(m_module.enums[i].members[k].token, "Duplicate enum member '", m_module.enums[i].members[k].name, "'");
						return false;
					}
				}
			}
		}
		for (FunctionDecl& fn : m_module.functions) {
			for (i32 i = 0; i < fn.params.size(); ++i) {
				for (i32 j = i + 1; j < fn.params.size(); ++j) {
					if (equalStrings(fn.params[i].name, fn.params[j].name)) {
						m_diagnostics.errorAt(fn.params[j].token, "Duplicate parameter '", fn.params[j].name, "'");
						return false;
					}
				}
				resolveType(fn.params[i].type);
			}
			resolveType(fn.return_type);
		}
		for (NativeFunctionDecl& fn : m_module.native_functions) {
			for (Param& p : fn.params) resolveType(p.type);
			resolveType(fn.return_type);
		}
		checkGlobals();
		for (FunctionDecl& fn : m_module.functions) checkFunction(fn);
		return !m_diagnostics.has_error;
	}

	i32 findStruct(StringView name) const {
		for (i32 i = 0; i < m_module.structs.size(); ++i) if (equalStrings(m_module.structs[i].name, name)) return i;
		return -1;
	}

	i32 findFunction(StringView name) const {
		for (i32 i = 0; i < m_module.functions.size(); ++i) if (equalStrings(m_module.functions[i].name, name)) return i;
		return -1;
	}

	i32 findNativeFunction(StringView name) const {
		for (i32 i = 0; i < m_module.native_functions.size(); ++i) if (equalStrings(m_module.native_functions[i].name, name)) return i;
		return -1;
	}

	i32 findGlobal(StringView name) const {
		for (i32 i = 0; i < m_module.globals.size(); ++i) if (equalStrings(m_module.globals[i].name, name)) return i;
		return -1;
	}

	i32 findEnum(StringView name) const {
		for (i32 i = 0; i < m_module.enums.size(); ++i) if (equalStrings(m_module.enums[i].name, name)) return i;
		return -1;
	}

	i32 findNativeType(StringView name) const {
		for (i32 i = 0; i < m_module.native_types.size(); ++i) if (equalStrings(m_module.native_types[i].name, name)) return i;
		return -1;
	}

	i32 findEnumMember(const EnumDecl& e, StringView name) const {
		for (i32 i = 0; i < e.members.size(); ++i) if (equalStrings(e.members[i].name, name)) return i;
		return -1;
	}

	StringView getExpressionName(i32 expr_idx) {
		Expr& e = m_module.expressions[expr_idx];
		if (e.kind == Expr::VAR) return e.name;
		if (e.kind == Expr::FIELD) {
			if (e.qualified_name.empty()) e.qualified_name = m_module.makeQualifiedName(getExpressionName(e.left), e.name);
			return e.qualified_name;
		}
		return {};
	}

	bool splitMemberName(StringView name, StringView* owner, StringView* member) const {
		for (const char* c = name.end; c != name.begin; --c) {
			if (*(c - 1) != '.') continue;
			*owner = StringView(name.begin, c - 1);
			*member = StringView(c, name.end);
			return true;
		}
		return false;
	}

	StringView getTypeNamespace(TypeRef type) const {
		StringView type_name = type.name;
		if (type.kind == TypeRef::NATIVE && type.struct_index >= 0) type_name = m_module.native_types[type.struct_index].name;
		else if (type.kind == TypeRef::NATIVE) {
			for (const NativeTypeDecl& native_type : m_module.native_types) {
				if (!equalStrings(native_type.id, type.name)) continue;
				type_name = native_type.name;
				break;
			}
		}
		StringView namespace_name;
		StringView member_name;
		if (!splitMemberName(type_name, &namespace_name, &member_name)) return {};
		return namespace_name;
	}

	StringView resolveCallName(Expr& call, i32* fn_idx, i32* native_idx) {
		StringView callee_name = call.qualified_name.empty() ? getExpressionName(call.left) : call.qualified_name;
		*fn_idx = findFunction(callee_name);
		*native_idx = findNativeFunction(callee_name);
		if (*fn_idx >= 0 || *native_idx >= 0) {
			call.qualified_name = callee_name;
			return callee_name;
		}

		Expr& callee = m_module.expressions[call.left];
		if (callee.kind != Expr::FIELD) return callee_name;

		TypeRef receiver_type = checkExpr(callee.left);
		const StringView namespace_name = getTypeNamespace(receiver_type);
		if (namespace_name.empty()) return callee_name;

		StringView method_name = m_module.makeQualifiedName(namespace_name, callee.name);
		*fn_idx = findFunction(method_name);
		*native_idx = findNativeFunction(method_name);
		if (*fn_idx < 0 && *native_idx < 0) return callee_name;

		call.qualified_name = method_name;
		call.method_receiver = callee.left;
		return method_name;
	}

	bool checkQualifiedEnumMember(Expr& e, StringView name) {
		StringView enum_name;
		StringView member_name;
		if (!splitMemberName(name, &enum_name, &member_name)) return false;
		const i32 enum_idx = findEnum(enum_name);
		if (enum_idx < 0) return false;
		EnumDecl& en = m_module.enums[enum_idx];
		const i32 member_idx = findEnumMember(en, member_name);
		if (member_idx < 0) {
			m_diagnostics.errorAt(e.token, "Unknown enum member '", member_name, "'");
			return true;
		}
		e.kind = Expr::ENUM_LITERAL;
		e.name = member_name;
		e.type = {TypeRef::ENUM, enum_name, enum_idx};
		return true;
	}

	i32 findLocal(StringView name) const {
		for (i32 i = m_locals.size() - 1; i >= 0; --i) if (equalStrings(m_locals[i].name, name)) return i;
		return -1;
	}

	bool isAssignableExpr(i32 expr_idx) {
		if (expr_idx < 0) return false;
		Expr& e = m_module.expressions[expr_idx];
		if (e.kind == Expr::VAR) return true;
		if (e.kind == Expr::FIELD) return isAssignableExpr(e.left);
		return false;
	}

	bool isConstExpr(i32 expr_idx) {
		if (expr_idx < 0) return false;
		Expr& e = m_module.expressions[expr_idx];
		if (e.kind == Expr::VAR) {
			const i32 idx = findLocal(e.name);
			return idx >= 0 && m_locals[idx].is_const;
		}
		const i32 global_idx = e.kind == Expr::VAR ? findGlobal(e.name) : -1;
		if (global_idx >= 0) return m_module.globals[global_idx].is_const;
		if (e.kind == Expr::FIELD) return isConstExpr(e.left);
		return false;
	}

	void resolveType(TypeRef& type) {
		if (type.kind == TypeRef::STRUCT) {
			// First try to find as a struct
			type.struct_index = findStruct(type.name);
			if (type.struct_index < 0) {
				// If not a struct, try to find as an enum
				type.struct_index = findEnum(type.name);
				if (type.struct_index >= 0) {
					type.kind = TypeRef::ENUM;
				} else {
					type.struct_index = findNativeType(type.name);
					if (type.struct_index >= 0) {
						type.kind = TypeRef::NATIVE;
						type.name = m_module.native_types[type.struct_index].id;
					}
					else {
						m_diagnostics.errorAt(type.token, "Unknown type '", type.name, "'");
					}
				}
			}
		} else if (type.kind == TypeRef::ENUM) {
			type.struct_index = findEnum(type.name);
			if (type.struct_index < 0) m_diagnostics.errorAt(type.token, "Unknown type '", type.name, "'");
		}
	}

	bool canAssign(TypeRef dst, TypeRef src) const {
		if (src.kind == TypeRef::NULL_VALUE) return dst.nullable;
		if (dst.kind == TypeRef::NULL_VALUE) return src.kind == TypeRef::NULL_VALUE;
		if (src.kind == TypeRef::STRING) return dst.kind == TypeRef::STRING;
		if (src.kind == TypeRef::ENUM && isNumeric(dst)) return true;
		if (src.kind == TypeRef::UNTYPED_INT) return isNumeric(dst);
		if (src.kind == TypeRef::UNTYPED_FLOAT) return isFloat(dst);
		if (dst.nullable) {
			if (src.nullable) return sameBaseType(dst, src);
			TypeRef nonnull_dst = dst;
			nonnull_dst.nullable = false;
			return sameBaseType(nonnull_dst, src);
		}
		if (src.nullable) return false;
		return sameBaseType(dst, src);
	}

	bool getNullablePromotion(i32 expr_idx, StringView* var_name, TypeRef* promoted_type, bool* promote_true_branch) {
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

	void checkStmtWithPromotion(i32 stmt_idx, TypeRef return_type, StringView var_name, TypeRef promoted_type) {
		if (stmt_idx < 0) return;
		const i32 old_size = m_locals.size();
		m_scope_starts.push(old_size);
		const i32 existing = findLocal(var_name);
		LocalInfo& promoted = m_locals.emplace();
		promoted.name = var_name;
		promoted.type = promoted_type;
		promoted.is_const = existing >= 0 ? m_locals[existing].is_const : false;
		checkStmt(stmt_idx, return_type);
		m_locals.shrink(old_size);
		m_scope_starts.pop();
	}

	bool isScalar(TypeRef type) const {
		return type.kind == TypeRef::BOOL
			|| type.kind == TypeRef::I8 || type.kind == TypeRef::U8
			|| type.kind == TypeRef::I16 || type.kind == TypeRef::U16
			|| type.kind == TypeRef::I32 || type.kind == TypeRef::U32
			|| type.kind == TypeRef::I64 || type.kind == TypeRef::U64
			|| type.kind == TypeRef::F32 || type.kind == TypeRef::F64
			|| type.kind == TypeRef::UNTYPED_INT || type.kind == TypeRef::UNTYPED_FLOAT;
	}

	bool isIntegral(TypeRef type) const {
		return type.kind == TypeRef::I8 || type.kind == TypeRef::U8
			|| type.kind == TypeRef::I16 || type.kind == TypeRef::U16
			|| type.kind == TypeRef::I32 || type.kind == TypeRef::U32
			|| type.kind == TypeRef::I64 || type.kind == TypeRef::U64;
	}

	bool isFloat(TypeRef type) const {
		return type.kind == TypeRef::F32 || type.kind == TypeRef::F64;
	}

	bool isNumeric(TypeRef type) const {
		return isIntegral(type) || isFloat(type);
	}

	TypeRef concreteNumberType(TypeRef type, const TypeRef* expected) const {
		if (type.kind == TypeRef::UNTYPED_INT) {
			if (expected) {
				TypeRef target = *expected;
				target.nullable = false;
				if (isNumeric(target)) return target;
			}
			return {TypeRef::I32, {}, -1};
		}
		if (type.kind == TypeRef::UNTYPED_FLOAT) {
			if (expected) {
				TypeRef target = *expected;
				target.nullable = false;
				if (isFloat(target)) return target;
			}
			return {TypeRef::F32, {}, -1};
		}
		return type;
	}

	void checkFunction(FunctionDecl& fn) {
		m_locals.clear();
		m_scope_starts.clear();
		m_scope_starts.push(0);
		for (Param& p : fn.params) {
			LocalInfo& local = m_locals.emplace();
			local.name = p.name;
			local.type = p.type;
			local.is_const = !p.is_ref;
		}
		checkStmt(fn.body, fn.return_type);
	}

	void checkGlobals() {
		m_locals.clear();
		m_scope_starts.clear();
		m_scope_starts.push(0);
		for (GlobalDecl& global : m_module.globals) {
			TypeRef type = global.type;
			if (type.kind != TypeRef::INVALID) resolveType(type);
			if (global.expr >= 0) {
				TypeRef* expected = type.kind == TypeRef::INVALID ? nullptr : &type;
				TypeRef expr_type = checkExpr(global.expr, expected);
				if (type.kind == TypeRef::INVALID) type = expr_type;
				else if (!canAssign(type, expr_type)) m_diagnostics.errorAt(global.token, "Initializer type mismatch");
			}
			else if (type.kind == TypeRef::INVALID) {
				m_diagnostics.errorAt(global.token, "Global variable needs type or initializer");
			}
			if (type.kind != TypeRef::INVALID) resolveType(type);
			global.type = type;
			LocalInfo& local = m_locals.emplace();
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
				e.type = {TypeRef::STRING, {}, -1};
				return e.type;
			case Expr::BOOL_LITERAL: return e.type;
			case Expr::NULL_LITERAL: return e.type;
			case Expr::VAR: {
				const i32 local_idx = findLocal(e.name);
				if (local_idx >= 0) {
					e.type = m_locals[local_idx].type;
					return e.type;
				}
				const i32 global_idx = findGlobal(e.name);
				if (global_idx >= 0) {
					e.type = m_module.globals[global_idx].type;
					return e.type;
				}
				if (findFunction(e.name) >= 0 || findNativeFunction(e.name) >= 0) return {};
				m_diagnostics.errorAt(e.token, "Unknown variable '", e.name, "'");
				return {};
			}
			case Expr::FIELD: {
				if (checkQualifiedEnumMember(e, getExpressionName(expr_idx))) return e.type;
				TypeRef base = checkExpr(e.left);
				if (base.nullable) {
					m_diagnostics.errorAt(e.token, "Nullable value must be checked for null");
					return {};
				}
				if (base.kind != TypeRef::STRUCT || base.struct_index < 0) {
					m_diagnostics.errorAt(e.token, "Field access on non-struct");
					return {};
				}
				StructDecl& s = m_module.structs[base.struct_index];
				for (FieldDecl& f : s.fields) {
					if (equalStrings(f.name, e.name)) {
						e.type = f.type;
						return e.type;
					}
				}
				m_diagnostics.errorAt(e.token, "Unknown field '", e.name, "'");
				return {};
			}
			case Expr::UNARY: {
				TypeRef right = checkExpr(e.right, expected);
				if (right.nullable) {
					m_diagnostics.errorAt(e.token, "Nullable value must be checked for null");
					return {};
				}
				if (e.token.type == Token::NOT) e.type = {TypeRef::BOOL, {}, -1};
				else e.type = right;
				return e.type;
			}
			case Expr::REF: {
				m_diagnostics.errorAt(e.token, "'ref' can be only used for ref arguments");
				return {};
			}
			case Expr::BINARY: {
				const bool is_comparison = e.token.type == Token::GT || e.token.type == Token::LT || e.token.type == Token::GT_EQUAL || e.token.type == Token::LT_EQUAL
					|| e.token.type == Token::EQUAL_EQUAL || e.token.type == Token::BANG_EQUAL || e.token.type == Token::AND || e.token.type == Token::OR;
				const TypeRef* operand_expected = !is_comparison && expected && isNumeric(*expected) ? expected : nullptr;
				TypeRef left = checkExpr(e.left, operand_expected);
				// For comparisons, pass the left operand's type as expected type to the right
				TypeRef right = checkExpr(e.right, &left);
				if (left.kind == TypeRef::STRING || right.kind == TypeRef::STRING) {
					if (e.token.type != Token::PLUS || left.kind != TypeRef::STRING || right.kind != TypeRef::STRING) {
						m_diagnostics.errorAt(e.token, "String operation requires string operands");
						return {};
					}
					e.type = {TypeRef::STRING, {}, -1};
					return e.type;
				}
				const bool is_eq = e.token.type == Token::EQUAL_EQUAL || e.token.type == Token::BANG_EQUAL;
				const bool null_cmp = left.kind == TypeRef::NULL_VALUE || right.kind == TypeRef::NULL_VALUE;
				if (!is_eq && (left.nullable || right.nullable)) {
					m_diagnostics.errorAt(e.token, "Nullable value must be checked for null");
					return {};
				}
				if (is_eq && !null_cmp && (left.nullable || right.nullable)) {
					m_diagnostics.errorAt(e.token, "Nullable value must be checked for null");
					return {};
				}
				switch (e.token.type) {
					case Token::GT: case Token::LT: case Token::GT_EQUAL: case Token::LT_EQUAL:
					case Token::EQUAL_EQUAL: case Token::BANG_EQUAL:
					case Token::AND: case Token::OR:
						e.type = {TypeRef::BOOL, {}, -1};
						return e.type;
					default:
							e.type = left.kind == TypeRef::F64 || right.kind == TypeRef::F64
								? TypeRef{TypeRef::F64, {}, -1}
								: (isFloat(left) || isFloat(right) ? TypeRef{TypeRef::F32, {}, -1} : left);
						return e.type;
				}
			}
			case Expr::CALL: {
				i32 fn_idx = -1;
				i32 native_idx = -1;
				const StringView callee_name = resolveCallName(e, &fn_idx, &native_idx);
				if (callee_name.empty()) {
					m_diagnostics.errorAt(e.token, "Unsupported callee");
					return {};
				}
				if (fn_idx < 0 && native_idx < 0) {
					m_diagnostics.errorAt(m_module.expressions[e.left].token, "Unknown function '", callee_name, "'");
					return {};
				}
				const bool is_native = native_idx >= 0;
				const Array<Param>& params = is_native ? m_module.native_functions[native_idx].params : m_module.functions[fn_idx].params;
				const TypeRef return_type = is_native ? m_module.native_functions[native_idx].return_type : m_module.functions[fn_idx].return_type;
				const i32 receiver_arg_count = e.method_receiver >= 0 ? 1 : 0;
				if (params.size() != e.args.size() + receiver_arg_count) {
					m_diagnostics.errorAt(e.token, "Wrong number of arguments");
					return {};
				}
				for (i32 i = 0; i < params.size(); ++i) {
					const i32 arg_idx = i - receiver_arg_count;
					const i32 expr_idx = i == 0 && e.method_receiver >= 0 ? e.method_receiver : e.args[arg_idx];
					if (params[i].is_ref) {
						Expr& arg_expr = m_module.expressions[expr_idx];
						if (arg_expr.kind != Expr::REF) {
							m_diagnostics.errorAt(arg_expr.token, "Expected ref argument");
							continue;
						}
						const i32 target_expr = arg_expr.right;
						if (!isAssignableExpr(target_expr)) {
							m_diagnostics.errorAt(arg_expr.token, "Ref argument must be assignable");
							continue;
						}
						if (isConstExpr(target_expr)) {
							m_diagnostics.errorAt(arg_expr.token, "Can not pass const as ref argument");
							continue;
						}
						TypeRef arg_type = checkExpr(target_expr, &params[i].type);
						if (!canAssign(params[i].type, arg_type)) m_diagnostics.errorAt(m_module.expressions[target_expr].token, "Argument type mismatch");
					}
					else {
						Expr& arg_expr = m_module.expressions[expr_idx];
						if (arg_expr.kind == Expr::REF) {
							m_diagnostics.errorAt(arg_expr.token, "Unexpected ref argument");
							continue;
						}
						TypeRef arg_type = checkExpr(expr_idx, &params[i].type);
						if (!canAssign(params[i].type, arg_type)) m_diagnostics.errorAt(m_module.expressions[expr_idx].token, "Argument type mismatch");
					}
				}
				e.type = return_type;
				return e.type;
			}
			case Expr::CAST: {
				TypeRef src = checkExpr(e.left);
				resolveType(e.cast_type);
				if (!isScalar(src) || !isScalar(e.cast_type)) {
					m_diagnostics.errorAt(e.token, "Invalid cast");
					return {};
				}
				e.type = e.cast_type;
				return e.type;
			}
			case Expr::STRUCT_LITERAL:
			case Expr::CONSTRUCTOR: {
				TypeRef target = expected ? *expected : TypeRef{};
				if (e.kind == Expr::CONSTRUCTOR) {
					target = {TypeRef::STRUCT, e.name, findStruct(e.name)};
				}
				if (target.kind != TypeRef::STRUCT || target.struct_index < 0) {
					m_diagnostics.errorAt(e.token, "Can not infer struct literal type");
					return {};
				}
				StructDecl& s = m_module.structs[target.struct_index];
				if (s.fields.size() != e.args.size()) {
					m_diagnostics.errorAt(e.token, "Struct literal field count mismatch");
					return {};
				}
				for (i32 i = 0; i < e.args.size(); ++i) {
					TypeRef arg_type = checkExpr(e.args[i], &s.fields[i].type);
					if (!canAssign(s.fields[i].type, arg_type)) m_diagnostics.errorAt(m_module.expressions[e.args[i]].token, "Struct literal type mismatch");
				}
				e.type = target;
				return e.type;
			}
			case Expr::ENUM_LITERAL: {
				if (!expected || expected->kind != TypeRef::ENUM) {
					m_diagnostics.errorAt(e.token, "Can not infer enum literal type");
					return {};
				}
				const i32 enum_idx = expected->struct_index;
				if (enum_idx < 0 || enum_idx >= m_module.enums.size()) {
					m_diagnostics.errorAt(e.token, "Invalid enum type");
					return {};
				}
				EnumDecl& en = m_module.enums[enum_idx];
				const i32 member_idx = findEnumMember(en, e.name);
				if (member_idx < 0) {
					m_diagnostics.errorAt(e.token, "Unknown enum member '", e.name, "'");
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
		if (subject_type.kind != TypeRef::ENUM || pattern.kind != MatchPattern::VALUE || pattern.start_expr < 0) return -1;
		Expr& e = m_module.expressions[pattern.start_expr];
		if (e.kind != Expr::ENUM_LITERAL) return -1;
		return findEnumMember(m_module.enums[subject_type.struct_index], e.name);
	}

	void checkMatchPattern(MatchPattern& pattern, TypeRef subject_type, bool* has_default) {
		if (pattern.kind == MatchPattern::DEFAULT) {
			if (*has_default) m_diagnostics.errorAt(pattern.token, "Duplicate match fallback");
			*has_default = true;
			return;
		}
		TypeRef start_type = checkExpr(pattern.start_expr, &subject_type);
		if (!canAssign(subject_type, start_type)) {
			m_diagnostics.errorAt(pattern.token, "Match pattern type mismatch");
			return;
		}
		if (pattern.kind == MatchPattern::RANGE) {
			if (!isNumeric(subject_type)) {
				m_diagnostics.errorAt(pattern.token, "Match range requires numeric type");
				return;
			}
			TypeRef end_type = checkExpr(pattern.end_expr, &subject_type);
			if (!canAssign(subject_type, end_type)) m_diagnostics.errorAt(pattern.token, "Match range type mismatch");
		}
	}

	void checkMatchStmt(Stmt& stmt, TypeRef return_type) {
		TypeRef subject_type = checkExpr(stmt.expr);
		if (!isScalar(subject_type) && subject_type.kind != TypeRef::ENUM && subject_type.kind != TypeRef::STRING) {
			m_diagnostics.errorAt(stmt.token, "Match requires scalar, enum or string value");
			return;
		}

		bool has_default = false;
		Array<u8> covered_enum_members(m_module.allocator);
		if (subject_type.kind == TypeRef::ENUM && subject_type.struct_index >= 0) {
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
						m_diagnostics.errorAt(pattern.token, "Duplicate enum match case");
						return;
					}
					covered_enum_members[enum_member] = 1;
				}
			}
			checkStmt(arm.stmt, return_type);
		}

		if (subject_type.kind == TypeRef::ENUM && !has_default) {
			for (u8 covered : covered_enum_members) {
				if (covered) continue;
				m_diagnostics.errorAt(stmt.token, "Enum match must be exhaustive or have fallback");
				return;
			}
		}
	}

	void checkStmt(i32 stmt_idx, TypeRef return_type) {
		if (stmt_idx < 0 || m_diagnostics.has_error) return;
		Stmt& stmt = m_module.statements[stmt_idx];
		switch (stmt.kind) {
			case Stmt::BLOCK: {
				const i32 old_size = m_locals.size();
				m_scope_starts.push(old_size);
				for (i32 child : stmt.children) checkStmt(child, return_type);
				m_locals.shrink(old_size);
				m_scope_starts.pop();
				break;
			}
			case Stmt::VAR_DECL: {
				const i32 scope_start = m_scope_starts.empty() ? 0 : m_scope_starts.last();
				for (i32 i = scope_start; i < m_locals.size(); ++i) {
					if (equalStrings(m_locals[i].name, stmt.name)) {
						m_diagnostics.errorAt(stmt.token, "Duplicate local '", stmt.name, "'");
						return;
					}
				}
				TypeRef type = stmt.type;
				if (type.kind != TypeRef::INVALID) resolveType(type);
				if (stmt.expr >= 0) {
					TypeRef* expected = type.kind == TypeRef::INVALID ? nullptr : &type;
					TypeRef expr_type = checkExpr(stmt.expr, expected);
					if (type.kind == TypeRef::INVALID) type = expr_type;
					else if (!canAssign(type, expr_type)) m_diagnostics.errorAt(stmt.token, "Initializer type mismatch");
				}
				if (type.kind != TypeRef::INVALID) resolveType(type);
				stmt.type = type;
				stmt.local_index = m_locals.size();
				LocalInfo& local = m_locals.emplace();
				local.name = stmt.name;
				local.type = type;
				local.is_const = stmt.is_const;
				break;
			}
			case Stmt::EXPR: checkExpr(stmt.expr); break;
			case Stmt::ASSIGN: {
				TypeRef left = checkExpr(stmt.left);
				TypeRef right = checkExpr(stmt.right, &left);
				if (!canAssign(left, right)) m_diagnostics.errorAt(stmt.token, "Assignment type mismatch");
				Expr& lhs = m_module.expressions[stmt.left];
				if (lhs.kind == Expr::VAR) {
					const i32 idx = findLocal(lhs.name);
					if (idx >= 0 && m_locals[idx].is_const) m_diagnostics.errorAt(lhs.token, "Can not assign to const '", lhs.name, "'");
					const i32 global_idx = findGlobal(lhs.name);
					if (global_idx >= 0 && m_module.globals[global_idx].is_const) m_diagnostics.errorAt(lhs.token, "Can not assign to const '", lhs.name, "'");
				}
				break;
			}
			case Stmt::WHILE: {
				TypeRef cond = checkExpr(stmt.expr);
				if (cond.kind != TypeRef::BOOL) m_diagnostics.errorAt(stmt.token, "While condition must be bool");
				checkStmt(stmt.right, return_type);
				break;
			}
			case Stmt::IF: {
				TypeRef cond = checkExpr(stmt.expr);
				if (cond.kind != TypeRef::BOOL) m_diagnostics.errorAt(stmt.token, "If condition must be bool");
				StringView promoted_name;
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
				TypeRef actual = stmt.expr >= 0 ? checkExpr(stmt.expr, &return_type) : TypeRef{TypeRef::VOID, {}, -1};
				if (!canAssign(return_type, actual)) m_diagnostics.errorAt(stmt.token, "Return type mismatch");
				break;
			}
			case Stmt::DEFER: {
				if (stmt.left < 0) {
					m_diagnostics.errorAt(stmt.token, "Expected statement after defer");
					break;
				}
				Stmt& deferred = m_module.statements[stmt.left];
				if (deferred.kind == Stmt::RETURN) {
					m_diagnostics.errorAt(deferred.token, "Can not defer return");
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

	Module& m_module;
	Diagnostics& m_diagnostics;
	Array<LocalInfo> m_locals;
	Array<i32> m_scope_starts;
};

inline bool typecheck(Module& module, Diagnostics& diagnostics) {
	Checker checker(module, diagnostics);
	return checker.check();
}

} // namespace Lumix::LumScript
