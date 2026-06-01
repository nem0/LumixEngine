#include "ast.h"
#include <string>
#include <unordered_set>

bool parse(ls_module& module, ls_string_view source, ls_string_view declaration_prefix = {}, ls_string_view source_name = {});

struct LocalInfo {
	ls_string_view name;
	TypeRef type;
	bool is_const = false;
};

static ls_string_view stringView(const std::string& value) {
	return ls_string_view{value.c_str(), value.c_str() + value.size()};
}

static bool isOverloadableOperatorToken(Token::Type type) {
	// Keep this list explicit so the language surface stays easy to audit.
	// Anything not listed here is either a fixed built-in token or a boolean
	// short-circuit operator that intentionally keeps its special semantics.
	switch (type) {
		case Token::PLUS:
		case Token::MINUS:
		case Token::STAR:
		case Token::SLASH:
		case Token::PERCENT:
		case Token::EQUAL_EQUAL:
		case Token::BANG_EQUAL:
		case Token::GT:
		case Token::LT:
		case Token::GT_EQUAL:
		case Token::LT_EQUAL: return true;
		default: return false;
	}
}

static bool isBuiltinOperatorType(ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_BOOL:
		case LS_TYPE_ENUM:
		case LS_TYPE_STRING:
		case LS_TYPE_I8:
		case LS_TYPE_U8:
		case LS_TYPE_I16:
		case LS_TYPE_U16:
		case LS_TYPE_I32:
		case LS_TYPE_U32:
		case LS_TYPE_I64:
		case LS_TYPE_U64:
		case LS_TYPE_F32:
		case LS_TYPE_F64:
		case LS_TYPE_UNTYPED_INT:
		case LS_TYPE_UNTYPED_FLOAT: return true;
		default: return false;
	}
}

struct Checker {
	Checker(ls_module& module)
		: m_module(module)
		, m_output(&module.host) {}

	bool check() {
		m_native_functions.clear();
		m_functions.clear();
		m_globals.clear();
		m_structs.clear();
		m_enums.clear();

		for (Unit* unit_ptr : m_module.units) {
			Unit& unit = *unit_ptr;
			for (NativeFunctionDecl& fn : unit.native_functions) m_native_functions.push_back(&fn);
			for (FunctionDecl& fn : unit.functions) m_functions.push_back(&fn);
			for (GlobalDecl& global : unit.globals) m_globals.push_back(&global);
			for (StructDecl& s : unit.structs) m_structs.push_back(&s);
			for (EnumDecl& e : unit.enums) m_enums.push_back(&e);
		}

		for (Unit* unit_ptr : m_module.units) {
			Unit& unit = *unit_ptr;
			// Struct fields: detect duplicates per-struct and resolve field types
			std::unordered_set<std::string> tmp_names;
			for (StructDecl& s : unit.structs) {
				tmp_names.clear();
				tmp_names.reserve(s.fields.size() * 2 + 4);
				
				for (FieldDecl& f : s.fields) {
					std::string key(data(f.name), size(f.name));
					if (!tmp_names.insert(key).second) {
						m_output.errorAt(f.token, "Duplicate field '", f.name, "'");
						return false;
					}
					resolveType(unit, f.type);
				}
			}
			
			// Enums and enum members
			for (const EnumDecl& e : unit.enums) {
				tmp_names.clear();
				tmp_names.reserve(e.members.size() * 2 + 4);
				for (const EnumMember& m : e.members) {
					std::string mkey(data(m.name), size(m.name));
					if (!tmp_names.insert(mkey).second) {
						m_output.errorAt(m.token, "Duplicate enum member '", m.name, "'");
						return false;
					}
				}
			}
			
			for (FunctionDecl& fn : unit.functions) {
				if (!checkFunctionSignature(unit, fn)) return false;
			}

			for (NativeFunctionDecl& fn : unit.native_functions) {
				for (Param& p : fn.params) resolveType(unit, p.type);
				resolveType(unit, fn.return_type);
			}
		}

		for (Unit* unit_ptr : m_module.units) {
			Unit& unit = *unit_ptr;
			for (FunctionDecl& fn : unit.functions) {
				if (!fn.is_operator) continue;
				// Operator declarations are validated in a separate pass so normal
				// functions keep their existing rules and operator-specific policy stays
				// centralized in one place.
				if (!isOverloadableOperatorToken(fn.operator_token)) {
					m_output.errorAt(fn.token, "Operator is not overloadable");
					return false;
				}
				if (fn.operator_token == Token::MINUS && fn.params.size() != 1 && fn.params.size() != 2) {
					m_output.errorAt(fn.token, "Invalid operator declaration");
					return false;
				}
				if (fn.operator_token != Token::MINUS && fn.params.size() != 2) {
					m_output.errorAt(fn.token, "Invalid operator declaration");
					return false;
				}
				for (Param& p : fn.params) resolveType(unit, p.type);
				resolveType(unit, fn.return_type);
				const bool unary_minus = fn.operator_token == Token::MINUS && fn.params.size() == 1;
				// Built-in primitive operators are reserved: the overload system only
				// applies to user-defined types. This keeps numeric and boolean behavior
				// stable even when modules import extra operator declarations.
				const bool primitive_signature =
					unary_minus ? isBuiltinOperatorType(fn.params[0].type.kind) : isBuiltinOperatorType(fn.params[0].type.kind) && isBuiltinOperatorType(fn.params[1].type.kind);
				if (primitive_signature) {
					m_output.errorAt(fn.token, "Can not overload built-in primitive operator");
					return false;
				}
				// TODO
				// Duplicate detection is based on the resolved signature, not the source
				// spelling. That means imported aliases or equivalent canonical names do
				// not create distinct operator identities.
				/*for (const FunctionDecl* prev : m_functions) {
					if (prev == fn) break;
					if (!prev->is_operator) continue;
					if (prev->operator_token != fn.operator_token) continue;
					if (prev->params.size() != fn.params.size()) continue;
					bool same = true;
					for (i32 i = 0; i < fn.params.size(); ++i) {
						if (!sameResolvedType(prev->params[i].type, fn.params[i].type)) {
							same = false;
							break;
						}
					}
					if (!same) continue;
					m_output.errorAt(fn.token, "Duplicate operator declaration");
					return false;
				}*/
			}
			checkGlobals(unit);
		}
		for (Unit* unit : m_module.units) {
			for (FunctionDecl& fn : unit->functions) {
				checkFunction(*unit, fn);
			}
		}
		return !m_output.has_error;
	}

	bool sameResolvedType(TypeRef a, TypeRef b) const {
		if (a.kind == LS_TYPE_FUNCTION || b.kind == LS_TYPE_FUNCTION) return functionTypesEqual(a, b);
		return sameBaseType(a, b);
	}

	bool importAliasExists(Unit& unit, ls_string_view alias) const {
		for (const ImportDecl& import : unit.imports) {
			if (empty(import.alias) || !equalStrings(import.alias, alias)) continue;
			return true;
		}
		return false;
	}

	bool importsDeclaration(const ImportDecl& import, ls_string_view type) const {
		for (const Unit* unit_ptr : m_module.units) {
			const Unit& unit = *unit_ptr;
			if (equalStrings(import.path, unit.source_name)) {
				for (const Symbol& s : unit.symbols) {
					if (equalStrings(s.name.name, type)) {
						return s.kind == Symbol::EXTERN_FN || s.kind == Symbol::FN || s.kind == Symbol::GLOBAL_VAR;
					}
				}
				return false;
			}
		}
		return false;
	}

	Symbol findSymbol(const ImportDecl& import, ls_string_view type) const {
		for (const Unit* unit_ptr : m_module.units) {
			const Unit& unit = *unit_ptr;
			if (equalStrings(import.path, unit.source_name)) {
				for (const Symbol& s : unit.symbols) {
					if (equalStrings(s.name.name, type)) {
						return s;
					}
				}
				return {.kind = Symbol::NOT_FOUND};
			}
		}
		return {.kind = Symbol::NOT_FOUND};
	}

	Symbol resolveSymbol(Unit& unit, ls_string_view unresolved_name, const Token& token) {
		// Resolve a raw source symbol name to a canonical module path.
		//
		// The input name comes from the parser, so it may be:
		// - alias-qualified in source: "x.Foo"
		// - unqualified: "Foo"
		//
		// The checker rewrites it to the actual imported module path if it can
		// prove a unique match. That keeps error messages and stored types stable
		// even when source uses aliases.
		ls_string_view owner;
		ls_string_view member;
		Symbol res;

		if (splitMemberName(unresolved_name, &owner, &member)) {
			// Try aliases only; raw import paths are not source-level namespaces.
			for (const ImportDecl& import : unit.imports) {
				if (equalStrings(owner, import.alias)) {
					res = findSymbol(import, member);
					if (res.kind != Symbol::NOT_FOUND) {
						res.name = {import.path, member};
						return res;
					}
					return {.kind = Symbol::NOT_FOUND};
				}
			}
			return {.kind = Symbol::NOT_FOUND};
		}

		// try unaliased imports
		bool found = false;
		ls_string_view resolved_path;
		for (const ImportDecl& import : unit.imports) {
			if (!empty(import.alias)) continue;
			res = findSymbol(import, unresolved_name);
			if (res.kind != Symbol::NOT_FOUND) {
				if (found && !equalStrings(resolved_path, import.path)) {
					m_output.errorAt(token, "Import symbol collision for '", unresolved_name, "'");
					return {.kind = Symbol::COLLISION};
				}
				found = true;
				resolved_path = import.path;
			}
		}

		// local symbols
		for (const Symbol& s : unit.symbols) {
			if (equalStrings(s.name.name, unresolved_name)) {
				if (found) {
					m_output.errorAt(token, "Import symbol collision for '", unresolved_name, "'");
					return {.kind = Symbol::COLLISION};
				}
				return s;
			}
		}
		
		if (found) {
			res.name = {resolved_path, unresolved_name};
			return res;
		}

		return {.kind = Symbol::NOT_FOUND};
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
			if (fa.params[i].is_ref != fb.params[i].is_ref) return false;
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
				same = existing.params[j].is_ref == params[j].is_ref && sameResolvedType(existing.params[j], params[j].type);
			}
			if (same) return {LS_TYPE_FUNCTION, {}, i, token};
		}
		FunctionTypeDecl& fn_type = m_module.function_types.emplace_back();
		for (const Param& p : params) {
			TypeRef param_type = p.type;
			param_type.is_ref = p.is_ref;
			fn_type.params.push_back(param_type);
		}
		fn_type.return_type = return_type;
		return {LS_TYPE_FUNCTION, {}, (i32)m_module.function_types.size() - 1, token};
	}

	TypeRef functionTypeFromFunction(FunctionDecl& fn) { return functionTypeFromSignature(std::span<const Param>(fn.params.begin(), fn.params.size()), fn.return_type, fn.token); }

	TypeRef functionTypeFromNativeFunction(NativeFunctionDecl& fn) { return functionTypeFromSignature(std::span<const Param>(fn.params.begin(), fn.params.size()), fn.return_type, fn.token); }

	ls_string_view getExpressionName(i32 expr_idx) {
		Expr& e = m_module.expressions[expr_idx];
		if (e.kind == Expr::VAR) return e.name;
		if (e.kind == Expr::FIELD) {
			if (empty(e.qualified_name)) e.qualified_name = {getExpressionName(e.left), e.name};
			return m_module.makeQualifiedName(e.qualified_name.path, e.qualified_name.name);
		}
		return {};
	}

	bool checkQualifiedEnumMember(Unit& unit, Expr& e, ls_string_view name) {
		ls_string_view enum_name;
		ls_string_view member_name;
		if (!splitMemberName(name, &enum_name, &member_name)) return false;
		i32 enum_idx = m_module.findEnum(enum_name);
		if (enum_idx < 0) {
			// Allow enum access through imported declarations such as
			// `import "core:Keycode"; Keycode.W`.
			for (const ImportDecl& import : unit.imports) {
				if (!empty(import.alias)) continue;
				Symbol s = findSymbol(import, enum_name);
				if (s.kind == Symbol::NOT_FOUND) continue;
				
				enum_idx = m_module.findEnum({import.path, enum_name});
				if (enum_idx >= 0) {
					enum_name = import.path;
					break;
				}
			}
		}
		if (enum_idx < 0) return false;
		const EnumDecl& en = *m_enums[enum_idx];
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
		for (i32 i = (i32)m_locals.size() - 1; i >= 0; --i)
			if (equalStrings(m_locals[i].name, name)) return i;
		return -1;
	}

	bool isAssignableExpr(i32 expr_idx) {
		if (expr_idx < 0) return false;
		Expr& e = m_module.expressions[expr_idx];
		if (e.kind == Expr::VAR) return true;
		if (e.kind == Expr::FIELD) return isAssignableExpr(e.left);
		if (e.kind == Expr::INDEX) return isAssignableExpr(e.left);
		return false;
	}

	bool isConstExpr(Unit& unit, i32 expr_idx) {
		if (expr_idx < 0) return false;

		Expr& e = m_module.expressions[expr_idx];
		if (e.kind == Expr::VAR) {
			// Constness can apply to either a local or a canonically resolved
			// imported/global declaration, so we resolve the symbol before asking
			// for the const flag.
			const i32 idx = findLocal(e.name);
			if (idx >= 0) return m_locals[idx].is_const;

			Symbol global_symbol = resolveSymbol(unit, e.name, e.token);
			if (global_symbol.kind == Symbol::GLOBAL_VAR) {
				const GlobalDecl& g = global_symbol.unit->globals[global_symbol.index];
				return g.is_const;
			}
			return false;
		}
		if (e.kind == Expr::FIELD) return isConstExpr(unit, e.left);
		if (e.kind == Expr::INDEX) return isConstExpr(unit, e.left);
		return false;
	}

	void resolveType(Unit& unit, TypeRef& type) {
		if (type.kind == LS_TYPE_ARRAY) {
			TypeRef elem(type.element_kind, type.element_name, type.struct_index, type.token, false);
			resolveType(unit, elem);
			type.element_kind = elem.kind;
			type.element_name = elem.unresolved_name;
			type.struct_index = elem.struct_index;
		} else if (type.kind == LS_TYPE_STRUCT) {
			if (empty(type.canonical_name.name)) {
				Symbol s = resolveSymbol(unit, type.unresolved_name, type.token);
				if (s.kind == Symbol::ENUM){
					type.canonical_name = s.name;
					type.struct_index = m_module.findEnum(type.canonical_name);
					type.kind = LS_TYPE_ENUM;
				}
				else if (s.kind == Symbol::STRUCT) {
					type.canonical_name = s.name;
					type.struct_index = m_module.findStruct(type.canonical_name);
				}
				else {
					m_output.errorAt(type.token, "Unknown type '", type.unresolved_name, "'");
				}
			}
		} else if (type.kind == LS_TYPE_ENUM) {
			type.struct_index = m_module.findEnum(type.canonical_name);
			if (type.struct_index < 0) {
				m_output.errorAt(type.token, "Unknown type '", type.unresolved_name, "'");
			}
		} else if (type.kind == LS_TYPE_FUNCTION) {
			if (type.struct_index < 0 || type.struct_index >= m_module.function_types.size()) {
				m_output.errorAt(type.token, "Invalid function type");
				return;
			}
			FunctionTypeDecl& fn_type = m_module.function_types[type.struct_index];
			for (TypeRef& param : fn_type.params) resolveType(unit, param);
			resolveType(unit, fn_type.return_type);
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

	bool resolveOperatorOverload(Token::Type op, std::span<const TypeRef> operands, Token token, i32* fn_idx) {
		*fn_idx = -1;
		// Overload lookup is deliberately strict: if the operand types do not
		// already match a declared signature exactly, we fall back to the built-in
		// operator path instead of inventing implicit conversions.
		for (i32 i = 0; i < m_functions.size(); ++i) {
			FunctionDecl& fn = *m_functions[i];
			if (!fn.is_operator || fn.operator_token != op) continue;
			if (fn.params.size() != operands.size()) continue;
			bool same = true;
			for (i32 j = 0; j < fn.params.size(); ++j) {
				if (!sameResolvedType(fn.params[j].type, operands[j])) {
					same = false;
					break;
				}
			}
			if (!same) continue;
			if (*fn_idx >= 0) {
				// Two equal matches would make the expression depend on declaration
				// order, so we reject it instead of guessing.
				m_output.errorAt(token, "Operator overload is ambiguous");
				return false;
			}
			*fn_idx = i;
		}
		return true;
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
		} else if (right.kind == Expr::VAR && left.kind == Expr::NULL_LITERAL) {
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

	void checkStmtWithPromotion(Unit& unit, i32 stmt_idx, TypeRef return_type, ls_string_view var_name, TypeRef promoted_type) {
		if (stmt_idx < 0) return;
		const i32 old_size = (i32)m_locals.size();
		m_scope_starts.push_back(old_size);
		const i32 existing = findLocal(var_name);
		LocalInfo& promoted = m_locals.emplace_back();
		promoted.name = var_name;
		promoted.type = promoted_type;
		promoted.is_const = existing >= 0 ? m_locals[existing].is_const : false;
		checkStmt(unit, stmt_idx, return_type);
		m_locals.resize(old_size);
		m_scope_starts.pop_back();
	}

	bool isScalar(TypeRef type) const {
		return type.kind == LS_TYPE_BOOL || type.kind == LS_TYPE_I8 || type.kind == LS_TYPE_U8 || type.kind == LS_TYPE_I16 || type.kind == LS_TYPE_U16 || type.kind == LS_TYPE_I32 ||
			   type.kind == LS_TYPE_U32 || type.kind == LS_TYPE_I64 || type.kind == LS_TYPE_U64 || type.kind == LS_TYPE_F32 || type.kind == LS_TYPE_F64 || type.kind == LS_TYPE_UNTYPED_INT ||
			   type.kind == LS_TYPE_UNTYPED_FLOAT;
	}

	bool isIntegral(TypeRef type) const {
		return type.kind == LS_TYPE_I8 || type.kind == LS_TYPE_U8 || type.kind == LS_TYPE_I16 || type.kind == LS_TYPE_U16 || type.kind == LS_TYPE_I32 || type.kind == LS_TYPE_U32 ||
			   type.kind == LS_TYPE_I64 || type.kind == LS_TYPE_U64;
	}

	bool isFloat(TypeRef type) const { return type.kind == LS_TYPE_F32 || type.kind == LS_TYPE_F64; }

	bool isNumeric(TypeRef type) const { return isIntegral(type) || isFloat(type); }

	bool getCompileTimeInteger(i32 expr_idx, i64* value) {
		if (expr_idx < 0 || expr_idx >= m_module.expressions.size()) return false;
		Expr& e = m_module.expressions[expr_idx];
		switch (e.kind) {
			case Expr::NUMBER: *value = (i64)e.number; return true;
			case Expr::UNARY:
				if (e.token.type != Token::MINUS && e.token.type != Token::PLUS) return false;
				if (!getCompileTimeInteger(e.right, value)) return false;
				if (e.token.type == Token::MINUS) *value = -*value;
				return true;
			case Expr::CAST: return getCompileTimeInteger(e.left, value);
			default: return false;
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

	void checkFunction(Unit& unit, FunctionDecl& fn) {
		checkFunctionSignature(unit, fn);
		const i32 locals_start = (i32)m_locals.size();
		m_scope_starts.clear();
		m_loop_labels.clear();
		m_loop_scope_starts.clear();
		m_declared_labels.clear();
		m_label_scope_starts.clear();
		m_scope_starts.push_back(0);
		for (Param& p : fn.params) {
			LocalInfo& local = m_locals.emplace_back();
			local.name = p.name;
			local.type = p.type;
			local.is_const = !p.is_ref;
		}
		checkStmt(unit, fn.body, fn.return_type);
		m_locals.resize(locals_start);
	}

	bool checkFunctionSignature(Unit& unit, FunctionDecl& fn) {
		for (i32 i = 0; i < fn.params.size(); ++i) {
			for (i32 j = i + 1; j < fn.params.size(); ++j) {
				if (equalStrings(fn.params[i].name, fn.params[j].name)) {
					m_output.errorAt(fn.params[j].token, "Duplicate parameter '", fn.params[j].name, "'");
					return false;
				}
			}
			resolveType(unit, fn.params[i].type);
			if (fn.params[i].is_ref && fn.params[i].type.nullable) {
				m_output.errorAt(fn.params[i].token, "Ref parameter type can not be nullable");
				return false;
			}
		}
		resolveType(unit, fn.return_type);
		return !m_output.has_error;
	}

	void checkGlobals(Unit& unit) {
		m_locals.clear();
		m_scope_starts.clear();
		m_scope_starts.push_back(0);
		for (GlobalDecl& global : unit.globals) {
			TypeRef type = global.type;
			if (type.kind != LS_TYPE_INVALID) resolveType(unit, type);
			if (global.expr >= 0) {
				TypeRef* expected = type.kind == LS_TYPE_INVALID ? nullptr : &type;
				TypeRef expr_type = checkExpr(unit, global.expr, expected);
				if (type.kind == LS_TYPE_INVALID)
					type = expr_type;
				else if (!canAssign(type, expr_type))
					m_output.errorAt(global.token, "Initializer type mismatch");
			} else if (global.is_undefined_init) {
				if (global.is_const) m_output.errorAt(global.token, "Const declaration can not use undefined initializer");
				if (type.kind == LS_TYPE_INVALID) m_output.errorAt(global.token, "Undefined initializer requires explicit type");
			} else if (type.kind == LS_TYPE_INVALID) {
				m_output.errorAt(global.token, "Global variable needs type or initializer");
			} else {
				m_output.errorAt(global.token, "Variable declaration requires initializer");
			}
			if (type.kind != LS_TYPE_INVALID) resolveType(unit, type);
			global.type = type;
			LocalInfo& local = m_locals.emplace_back();
			local.name = m_module.makeQualifiedName(global.canonical_name.path, global.canonical_name.name);
			local.type = type;
			local.is_const = global.is_const;
		}
	}

	TypeRef checkField(Unit& unit, Expr& e, Expr* call_expr = nullptr) {
		// alias.*
		// struct.field
		// enum.enum_member
		// .enum_member
		// variable.function, sugar for function(variable)
		// variable1.variable2, sugar for variable2(variable1) if variable2 is a function
		// foo().bar, foo returns enum
		// foo().bar, foo returns struct
		// foo().bar(), foo returns function

		Expr& left_expr = m_module.expressions[e.left];
		switch (left_expr.kind) {
			case Expr::CALL: // foo().*
			case Expr::VAR: { // v.*
				// TODO check for expressions first, they can potentionally shadow alias
				if (left_expr.kind == Expr::VAR) {
					Symbol s = resolveSymbol(unit, left_expr.name, left_expr.token);

					// Enum.*
					if (s.kind == Symbol::ENUM) {
						const i32 enum_idx = m_module.findEnum(s.name);
						if (enum_idx < 0) {
							m_output.errorAt(e.token, "Unknown enum '", left_expr.name, "'");
							return {};
						}
						const EnumDecl& en = *m_enums[enum_idx];
						const i32 member_idx = m_module.findEnumMember(en, e.name);
						if (member_idx < 0) {
							m_output.errorAt(e.token, "Unknown enum member '", e.name, "'");
							return {};
						}
						e.kind = Expr::ENUM_LITERAL;
						e.type.kind = LS_TYPE_ENUM;
						e.type.canonical_name = s.name;
						e.type.struct_index = enum_idx;
						return e.type;
					}			
				}

				TypeRef left_type = checkExpr(unit, e.left);
				switch (left_type.kind) {
					case LS_TYPE_INVALID: {
						m_output.errorAt(left_expr.token, "Unknown identifier ", left_expr.name);
						return {};
					}
					case LS_TYPE_NAMESPACE: {
						Symbol s = findSymbol(unit.imports[left_type.import_index], e.name);
						switch (s.kind) {
							case Symbol::EXTERN_FN:
								e.kind = Expr::FUNCTION_REF;
								e.boolean = true;
								e.left = m_module.findNativeFunction(s.name);
								if (e.left < 0) return {};
								e.type = functionTypeFromNativeFunction(*m_native_functions[e.left]);
								return e.type;
							case Symbol::FN:
								e.kind = Expr::FUNCTION_REF;
								e.boolean = false;
								e.left = m_module.findFunction(s.name);
								if (e.left < 0) return {};
								e.type = functionTypeFromFunction(*m_functions[e.left]);
								return e.type;
							case Symbol::GLOBAL_VAR:
								e.kind = Expr::VAR;
								e.name = m_module.makeQualifiedName(s.name.path, s.name.name);
								e.qualified_name = s.name;
								e.type = s.unit->globals[s.index].type;
								return e.type;
							case Symbol::NOT_FOUND: 
								m_output.errorAt(e.token, "Unknown identifier ", e.name);
								return {};
							case Symbol::STRUCT:
								// TODO proper error msg
								m_output.errorAt(e.token, "", e.name); 
								return {};
							case Symbol::ENUM: {
								const i32 enum_idx = m_module.findEnum(s.name);
								if (enum_idx < 0) {
									m_output.errorAt(e.token, "Unknown enum '", left_expr.name, "'");
									return {};
								}
								e.type.kind = LS_TYPE_ENUM;
								e.type.canonical_name = s.name;
								e.type.struct_index = enum_idx;
								return e.type;
							}
						}
					}
					case LS_TYPE_STRUCT: {
						// field access - s.f
						// or namespace resolution based on first arg - v.foo() -> foo(v)
						if (left_type.nullable) {
							m_output.errorAt(e.token, "Nullable value must be checked for null");
							return {};
						}
						StructDecl& s = *m_structs[left_type.struct_index];
						for (const FieldDecl& f : s.fields) {
							if (!equalStrings(f.name, e.name)) continue;
							if (empty(e.qualified_name)) e.qualified_name = {getExpressionName(e.left), e.name};
							// field access
							e.type = f.type;
							return e.type;
						}

						// namespace resolution
						ls_string_view path = left_type.canonical_name.path;
						for (const ImportDecl& import : unit.imports) {
							if (equalStrings(import.path, path)) {
								Symbol s = findSymbol(import, e.name);
								switch (s.kind) {
									case Symbol::NOT_FOUND:
										m_output.errorAt(e.token, "Unknown identifier ", e.name);
										return {};
									case Symbol::GLOBAL_VAR:
										e.kind = Expr::VAR;
										e.name = m_module.makeQualifiedName(s.name.path, s.name.name);
										e.qualified_name = s.name;
										e.type = s.unit->globals[s.index].type;
										if (call_expr && e.type.kind == LS_TYPE_FUNCTION) {
											const i32 receiver_expr = e.left;
											call_expr->args.insert(call_expr->args.begin(), receiver_expr);
										}
										return e.type;
									case Symbol::FN: {
										const i32 receiver_expr = e.left;
										e.kind = Expr::FUNCTION_REF;
										e.boolean = false;
										e.left = m_module.findFunction(s.name);
										if (e.left < 0) return {};
										e.type = functionTypeFromFunction(*m_functions[e.left]);

										if (call_expr) {
											call_expr->args.insert(call_expr->args.begin(), receiver_expr);
										}

										return e.type;
									}
									case Symbol::EXTERN_FN: {
										e.kind = Expr::FUNCTION_REF;
										e.boolean = true;
										e.left = m_module.findNativeFunction(s.name);
										if (e.left < 0) return {};
										e.type = functionTypeFromNativeFunction(*m_native_functions[e.left]);
										return e.type;
									}
									default:
										break;
								}
							}
						}
						
						m_output.errorAt(e.token, "Struct ", s.token.value, " has no field ", e.name);
						return {};
					}
					case LS_TYPE_ENUM: {
						if (left_type.nullable) {
							m_output.errorAt(e.token, "Nullable value must be checked for null");
							return {};
						}
						const i32 receiver_expr = e.left;
						for (i32 i = 0; i < (i32)m_functions.size(); ++i) {
							FunctionDecl& fn = *m_functions[i];
							if (!equalStrings(fn.canonical_name.name, e.name)) continue;
							if (fn.params.empty() || !sameResolvedType(fn.params[0].type, left_type)) continue;
							e.kind = Expr::FUNCTION_REF;
							e.boolean = false;
							e.left = i;
							e.type = functionTypeFromFunction(fn);
							if (call_expr) call_expr->args.insert(call_expr->args.begin(), receiver_expr);
							return e.type;
						}
						for (i32 i = 0; i < (i32)m_native_functions.size(); ++i) {
							NativeFunctionDecl& fn = *m_native_functions[i];
							if (!equalStrings(fn.canonical_name.name, e.name)) continue;
							if (fn.params.empty() || !sameResolvedType(fn.params[0].type, left_type)) continue;
							e.kind = Expr::FUNCTION_REF;
							e.boolean = true;
							e.left = i;
							e.type = functionTypeFromNativeFunction(fn);
							if (call_expr) call_expr->args.insert(call_expr->args.begin(), receiver_expr);
							return e.type;
						}
						m_output.errorAt(e.token, "Enum ", left_expr.token.value, " has no member ", e.name);
						return {};
					}
					default: {
						m_output.errorAt(left_expr.token, "Unexpected identifier ", left_expr.token.value);
						return {};
					}
				} // switch (left_type.kind)
				ASSERT(false);
			}
			case Expr::FIELD: {
				TypeRef left_type = checkField(unit, left_expr, nullptr);
				switch (left_type.kind) {
					case LS_TYPE_STRUCT: {
						if (left_type.nullable) {
							m_output.errorAt(e.token, "Nullable value must be checked for null");
							return {};
						}
						StructDecl& s = *m_structs[left_type.struct_index];
						for (const FieldDecl& f : s.fields) {
							if (!equalStrings(f.name, e.name)) continue;
							if (empty(e.qualified_name)) e.qualified_name = {getExpressionName(e.left), e.name};
							e.type = f.type;
							return e.type;
						}
						m_output.errorAt(e.token, "Struct ", s.token.value, " has no field ", e.name);
						return {};
					}
					case LS_TYPE_ENUM: {
						if (left_type.nullable) {
							m_output.errorAt(e.token, "Nullable value must be checked for null");
							return {};
						}
						const i32 enum_idx = left_type.struct_index;
						if (enum_idx >= 0 && enum_idx < (i32)m_enums.size() && m_module.findEnumMember(*m_enums[enum_idx], e.name) >= 0) {
							e.kind = Expr::ENUM_LITERAL;
							e.type = left_type;
							return e.type;
						}
						m_output.errorAt(e.token, "Unknown enum member '", e.name, "'");
						return {};
					}
					default: {
						m_output.errorAt(left_expr.token, "Unexpected identifier ", left_expr.token.value);
						return {};
					}
				}
			}
			default: {
				// should not be possible
				volatile int k = 2;
				return {};
			}
		}
	}

	TypeRef checkVar(Unit& unit, Expr& e, Expr* call_expr) {
		// Locals are deliberately resolved before module symbols. Nullable
		// promotion also injects a shadow local with the non-null type, so this
		// lookup is what makes `if x != null { x.field }` type-check without
		// mutating the original variable declaration.
		const i32 local_idx = findLocal(e.name);
		if (local_idx >= 0) {
			e.type = m_locals[local_idx].type;
			return e.type;
		}
		// Variable, function, and native function names all share the same
		// canonical resolution logic. If a bare source name doesn't match the
		// current scope, rewrite it through the import/module namespace rules
		// before giving up.
		Symbol symbol = resolveSymbol(unit, e.name, e.token);
		switch (symbol.kind) {
			case Symbol::GLOBAL_VAR: {
				e.name = m_module.makeQualifiedName(symbol.name.path, symbol.name.name);
				e.type = symbol.unit->globals[symbol.index].type;
				if (call_expr && call_expr->args.size() > 0 && e.type.kind == LS_TYPE_FUNCTION) {
					ls_string_view possible_namespace;
					TypeRef first_arg_type = checkExpr(unit, call_expr->args[0], nullptr);
					possible_namespace = first_arg_type.canonical_name.path;

					for (const ImportDecl& import : unit.imports) {
						if (!equalStrings(import.path, possible_namespace)) continue;
						Symbol s = findSymbol(import, symbol.name.name);
						switch (s.kind) {
							case Symbol::GLOBAL_VAR:
								e.name = m_module.makeQualifiedName(s.name.path, s.name.name);
								e.qualified_name = s.name;
								e.type = s.unit->globals[s.index].type;
								return e.type;
							case Symbol::FN:
								e.kind = Expr::FUNCTION_REF;
								e.type = functionTypeFromFunction(*m_functions[m_module.findFunction(s.name)]);
								e.left = m_module.findFunction(s.name);
								e.boolean = false;
								return e.type;
							case Symbol::EXTERN_FN:
								e.kind = Expr::FUNCTION_REF;
								e.type = functionTypeFromNativeFunction(*m_native_functions[m_module.findNativeFunction(s.name)]);
								e.left = m_module.findNativeFunction(s.name);
								e.boolean = true;
								return e.type;
							default:
								break;
						}
					}
				}
				return e.type;
			}
			case Symbol::FN: {
				const i32 fn_idx = m_module.findFunction(symbol.name);
				if (fn_idx >= 0) {
					// A bare function name is normalized into FUNCTION_REF so later
					// passes do not have to repeat lexical/module lookup. The original
					// spelling is no longer important once `left` points at the
					// canonical function table index.
					e.kind = Expr::FUNCTION_REF;
					e.type = functionTypeFromFunction(*m_functions[fn_idx]);
					e.left = fn_idx;
					e.boolean = false;
					return e.type;
				}
				break;
			}
			case Symbol::EXTERN_FN: {
				const i32 native_idx = m_module.findNativeFunction(symbol.name);
				if (native_idx >= 0) {
					// Native functions share the same AST representation as script
					// functions; `boolean` is used as the discriminator because this
					// node already stores the target index in `left`.
					e.kind = Expr::FUNCTION_REF;
					e.type = functionTypeFromNativeFunction(*m_native_functions[native_idx]);
					e.left = native_idx;
					e.boolean = true;
					return e.type;
				}
				break;
			}
			default: break;
		}

		if (call_expr && call_expr->args.size() > 0) {
			ls_string_view possible_namespace;
			TypeRef first_arg_type = checkExpr(unit, call_expr->args[0], nullptr);
			possible_namespace = first_arg_type.canonical_name.path;

			for (const ImportDecl& import : unit.imports) {
				if (equalStrings(import.path, possible_namespace)) {
					Symbol s = findSymbol(import, e.name);
					switch (s.kind) {
						case Symbol::EXTERN_FN: {
							e.kind = Expr::FUNCTION_REF;
							const i32 native_idx = m_module.findNativeFunction({possible_namespace, e.name});
							e.type = functionTypeFromNativeFunction(*m_native_functions[native_idx]);
							e.left = native_idx;
							e.boolean = true;
							return e.type;
						}
						case Symbol::FN: {
							e.kind = Expr::FUNCTION_REF;
							const i32 fn_idx = m_module.findFunction({possible_namespace, e.name});
							e.type = functionTypeFromFunction(*m_functions[fn_idx]);
							e.left = fn_idx;
							e.boolean = false;
							return e.type;
						}
						case Symbol::GLOBAL_VAR:
							e.name = m_module.makeQualifiedName(possible_namespace, e.name);
							e.type = s.unit->globals[s.index].type;
							return e.type;
						default: break;
					}
				}
			}
			volatile int i = 0;
		}

		const i32 fn_idx = m_module.findFunction(e.name);
		if (fn_idx >= 0) {
			e.kind = Expr::FUNCTION_REF;
			e.type = functionTypeFromFunction(*m_functions[fn_idx]);
			e.left = fn_idx;
			e.boolean = false;
			return e.type;
		}
		const i32 native_idx = m_module.findNativeFunction(e.name);
		if (native_idx >= 0) {
			e.kind = Expr::FUNCTION_REF;
			e.type = functionTypeFromNativeFunction(*m_native_functions[native_idx]);
			e.left = native_idx;
			e.boolean = true;
			return e.type;
		}

		for (const ImportDecl& import : unit.imports) {
			if (equalStrings(import.alias, e.name)) {
				e.type.kind = LS_TYPE_NAMESPACE;
				e.type.import_index = i32(&import - unit.imports.data());
				return e.type;
			}
		}

		m_output.errorAt(e.token, "Unknown variable '", e.name, "'");
		return {};
	}

	TypeRef checkCall(Unit& unit, Expr& e) {
		// Possible function calls:
		// foo(*); - foo being extern fn foo()
		// foo(*); - foo being fn foo()
		// foo(*); - foo being const foo = fn()
		// foo(*); - foo being var foo = fn()
		// v.foo(*); - v being alias
		// v.foo(*); - v being a variable/const, syntax sugar for foo(v)
		// v.foo(*); - v being a variable/const imported through alias, syntax sugar for alias.foo(v),

		TypeRef left_type = checkExpr(unit, e.left, nullptr, &e);
		Expr& left_expr = m_module.expressions[e.left];
		if (left_type.kind != LS_TYPE_FUNCTION) {
			m_output.errorAt(left_expr.token, left_expr.name, " must be a function");
			return {};
		}

		FunctionTypeDecl& fn_type = m_module.function_types[left_type.struct_index];

		if (fn_type.params.size() != e.args.size()) {
			m_output.errorAt(e.token, "Wrong number of arguments");
			return {};
		}

		// typecheck args
		for (i32 i = 0; i < fn_type.params.size(); ++i) {
			Expr& arg_expr = m_module.expressions[e.args[i]];
			if (fn_type.params[i].is_ref) {
				if (arg_expr.kind != Expr::REF) {
					m_output.errorAt(arg_expr.token, "Argument type mismatch");
					return {};
				}
				// check foo(ref x)
				if (!isAssignableExpr(arg_expr.right)) {
					m_output.errorAt(m_module.expressions[arg_expr.right].token, "Argument must be assignable");
					return {};
				}
				if (isConstExpr(unit, arg_expr.right)) {
					m_output.errorAt(arg_expr.token, "Can not pass const as ref argument");
					return {};
				}
				TypeRef arg_type = checkExpr(unit, arg_expr.right, &fn_type.params[i]);
				if (!canAssign(fn_type.params[i], arg_type)) {
					m_output.errorAt(m_module.expressions[arg_expr.right].token, "Argument type mismatch");
					return {};
				}
				continue;
			}
			
			TypeRef arg_type = checkExpr(unit, e.args[i], &fn_type.params[i]);
			if (!canAssign(fn_type.params[i], arg_type)) {
				m_output.errorAt(arg_expr.token, "Argument type mismatch");
				return {};
			}
		}

		e.type = fn_type.return_type;
		return fn_type.return_type;
	}

	TypeRef checkExpr(Unit& unit, i32 expr_idx, const TypeRef* expected = nullptr, Expr* call_expr = nullptr) {
		if (expr_idx < 0) return {};
		Expr& e = m_module.expressions[expr_idx];
		switch (e.kind) {
			case Expr::NUMBER:
				// Numeric literals start out as untyped integers/floats so the
				// surrounding expression can choose the smallest amount of explicit
				// syntax. `expected` is the contextual type from an assignment, call
				// argument, struct field, etc.; when it is numeric we commit the
				// literal to that type here. Without that context, default to the
				// language's normal concrete literal types.
				e.type = concreteNumberType(e.type, expected);
				return e.type;
			case Expr::STRING_LITERAL: e.type = {LS_TYPE_STRING, {}, -1}; return e.type;
			case Expr::BOOL_LITERAL: return e.type;
			case Expr::NULL_LITERAL: return e.type;
			case Expr::VAR: return checkVar(unit, e, call_expr);
			case Expr::FUNCTION_REF: {
				// Function references are produced from two
				// different paths:
				// - ordinary named function lookups during
				//   semantic analysis
				// - synthesized anonymous function literals
				//
				// The checker generally fills in `e.type` when it
				// resolves the expression, but we keep this fallback
				// so later passes can still recover the signature if
				// the expression was created earlier or copied in a
				// partially-resolved form.
				if (e.type.kind != LS_TYPE_INVALID) return e.type;
				if (e.boolean) {
						if (e.left < 0 || e.left >= m_native_functions.size()) return {};
						e.type = functionTypeFromNativeFunction(*m_native_functions[e.left]);
				}
				else {
						if (e.left < 0 || e.left >= m_functions.size()) return {};
						e.type = functionTypeFromFunction(*m_functions[e.left]);
				}
				return e.type;
			}
			case Expr::FIELD: return checkField(unit, e, call_expr);
			case Expr::INDEX: {
				TypeRef base = checkExpr(unit, e.left);
				if (base.nullable) {
					m_output.errorAt(e.token, "Nullable value must be checked for null");
					return {};
				}
				if (base.kind != LS_TYPE_ARRAY) {
					m_output.errorAt(e.token, "Indexing requires array type");
					return {};
				}
				TypeRef idx_type = checkExpr(unit, e.right);
				if (!isIntegral(idx_type) && idx_type.kind != LS_TYPE_UNTYPED_INT) {
					m_output.errorAt(e.token, "Array index must be integer");
					return {};
				}
				// Bounds checking here is intentionally opportunistic. Only expressions
				// that fold to an integer without running user code are rejected at
				// compile time; dynamic indices stay the runtime's responsibility.
				i64 idx_value = 0;
				if (getCompileTimeInteger(e.right, &idx_value) && (idx_value < 0 || idx_value >= base.array_size)) {
					m_output.errorAt(e.token, "Array index out of range");
					return {};
				}
				e.type = {base.element_kind, base.element_name, base.struct_index, e.token, false};
				return e.type;
			}
			case Expr::UNARY: {
				TypeRef right = checkExpr(unit, e.right, expected);
				if (right.nullable) {
					m_output.errorAt(e.token, "Nullable value must be checked for null");
					return {};
				}
				if (e.token.type == Token::MINUS) {
					i32 fn_idx = -1;
					// Unary minus can be overloaded for user types, but primitives still
					// use the built-in numeric lowering. That keeps `-i32` and `-f32`
					// predictable even when a module defines custom operator overloads.
					if (!isBuiltinOperatorType(right.kind) && resolveOperatorOverload(Token::MINUS, std::span<const TypeRef>(&right, 1), e.token, &fn_idx) && fn_idx >= 0) {
						e.resolved_function = fn_idx;
						e.type = m_functions[fn_idx]->return_type;
						return e.type;
					}
					if (!isNumeric(right)) {
						m_output.errorAt(e.token, "Unary minus requires numeric operand");
						return {};
					}
				}
				if (e.token.type == Token::NOT)
					e.type = {LS_TYPE_BOOL, {}, -1};
				else
					e.type = right;
				if (e.token.type == Token::NOT && right.kind != LS_TYPE_BOOL) {
					m_output.errorAt(e.token, "Boolean operation requires bool operand");
					return {};
				}
				return e.type;
			}
			case Expr::REF: {
				m_output.errorAt(e.token, "'ref' can be only used for ref arguments");
				return {};
			}
			case Expr::BINARY: {
				// Contextual numeric typing flows into arithmetic operands, but not
				// comparisons. For `let x: f32 = 1 + 2`, both literals should become
				// f32. For `1 == 1.0`, the right operand should be checked against the
				// already-deduced left operand so mixed numeric comparisons are rejected
				// instead of being silently widened.
				const bool is_comparison = e.token.type == Token::GT || e.token.type == Token::LT || e.token.type == Token::GT_EQUAL || e.token.type == Token::LT_EQUAL ||
										   e.token.type == Token::EQUAL_EQUAL || e.token.type == Token::BANG_EQUAL || e.token.type == Token::AND || e.token.type == Token::OR;
				const TypeRef* operand_expected = !is_comparison && expected && isNumeric(*expected) ? expected : nullptr;
				TypeRef left = checkExpr(unit, e.left, operand_expected);
				// The right side uses the left side as context even for arithmetic. This
				// is what makes `1 + 2` settle on a single type while still catching
				// `i32 + f32` as a same-base-type error below.
				TypeRef right = checkExpr(unit, e.right, &left);
				if (e.token.type == Token::PLUS && left.kind == LS_TYPE_STRING && right.kind == LS_TYPE_STRING) {
					e.type = {LS_TYPE_STRING, {}, -1};
					return e.type;
				}
				const bool is_eq = e.token.type == Token::EQUAL_EQUAL || e.token.type == Token::BANG_EQUAL;
				const bool null_cmp = left.kind == LS_TYPE_NULL_VALUE || right.kind == LS_TYPE_NULL_VALUE;
				// Nullable values may only participate directly in equality checks with
				// the null literal. Other uses require an explicit control-flow check
				// first, which lets statement checking introduce a non-null shadow local
				// for the guarded branch.
				if (!is_eq && (left.nullable || right.nullable)) {
					m_output.errorAt(e.token, "Nullable value must be checked for null");
					return {};
				}
				if (is_eq && !null_cmp && (left.nullable || right.nullable)) {
					m_output.errorAt(e.token, "Nullable value must be checked for null");
					return {};
				}
				if (isOverloadableOperatorToken(e.token.type)) {
					TypeRef operands[] = {left, right};
					i32 fn_idx = -1;
					// Operator expressions lower to a direct function call when an exact
					// match exists. If no overload matches, the checker continues into the
					// legacy built-in rules below.
					if (!resolveOperatorOverload(e.token.type, std::span<const TypeRef>(operands, 2), e.token, &fn_idx)) return {};
					if (fn_idx >= 0) {
						e.resolved_function = fn_idx;
						e.type = m_functions[fn_idx]->return_type;
						return e.type;
					}
				}
				switch (e.token.type) {
					case Token::GT:
					case Token::LT:
					case Token::GT_EQUAL:
					case Token::LT_EQUAL:
					case Token::EQUAL_EQUAL:
					case Token::BANG_EQUAL:
						if (!canCompare(left, right)) {
							m_output.errorAt(e.token, "Comparison type mismatch");
							return {};
						}
						e.type = {LS_TYPE_BOOL, {}, -1};
						return e.type;
					case Token::AND:
					case Token::OR:
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
						e.type = left;
						return e.type;
				}
			}
			case Expr::CALL: return checkCall(unit, e);
			case Expr::CAST: {
				TypeRef src = checkExpr(unit, e.left);
				resolveType(unit, e.cast_type);
				// Casts are kept narrow: scalar-to-scalar plus enum/integer bridges.
				// Structs, arrays, functions, strings, and nullable wrappers must use
				// explicit language constructs instead of being bitcast through here.
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
					// Constructor syntax is just an explicit type name plus literal
					// arguments, so it follows the same resolution path as cast targets.
					target = {LS_TYPE_STRUCT, e.name, -1, e.token, false};
					resolveType(unit, target);
				}
				if (target.kind != LS_TYPE_STRUCT || target.struct_index < 0) {
					m_output.errorAt(e.token, "Can not infer struct literal type");
					return {};
				}
				StructDecl& s = *m_structs[target.struct_index];
				if (s.fields.size() != e.args.size()) {
					m_output.errorAt(e.token, "Struct literal field count mismatch");
					return {};
				}
				for (i32 i = 0; i < e.args.size(); ++i) {
					TypeRef arg_type = checkExpr(unit, e.args[i], &s.fields[i].type);
					if (!canAssign(s.fields[i].type, arg_type)) m_output.errorAt(m_module.expressions[e.args[i]].token, "Struct literal type mismatch");
				}
				e.type = target;
				return e.type;
			}
			case Expr::ENUM_LITERAL: {
				// Short enum literals are intentionally context-dependent. `.Red` has
				// no unique type until an assignment target, parameter type, match
				// subject, or similar caller provides the expected enum.
				if (!expected || expected->kind != LS_TYPE_ENUM) {
					m_output.errorAt(e.token, "Can not infer enum literal type");
					return {};
				}
				const i32 enum_idx = expected->struct_index;
				if (enum_idx < 0 || enum_idx >= m_enums.size()) {
					m_output.errorAt(e.token, "Invalid enum type");
					return {};
				}
				EnumDecl& en = *m_enums[enum_idx];
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
		return m_module.findEnumMember(*m_enums[subject_type.struct_index], e.name);
	}

	void checkMatchPattern(Unit& unit, MatchPattern& pattern, TypeRef subject_type, bool* has_default) {
		if (pattern.kind == MatchPattern::DEFAULT) {
			if (*has_default) m_output.errorAt(pattern.token, "Duplicate match fallback");
			*has_default = true;
			return;
		}
		TypeRef start_type = checkExpr(unit, pattern.start_expr, &subject_type);
		if (!canAssign(subject_type, start_type)) {
			m_output.errorAt(pattern.token, "Match pattern type mismatch");
			return;
		}
		if (pattern.kind == MatchPattern::RANGE) {
			if (!isNumeric(subject_type)) {
				m_output.errorAt(pattern.token, "Match range requires numeric type");
				return;
			}
			TypeRef end_type = checkExpr(unit, pattern.end_expr, &subject_type);
			if (!canAssign(subject_type, end_type)) m_output.errorAt(pattern.token, "Match range type mismatch");
		}
	}

	void checkMatchStmt(Unit& unit, Stmt& stmt, TypeRef return_type) {
		TypeRef subject_type = checkExpr(unit, stmt.expr);
		if (!isScalar(subject_type) && subject_type.kind != LS_TYPE_ENUM && subject_type.kind != LS_TYPE_STRING) {
			m_output.errorAt(stmt.token, "Match requires scalar, enum or string value");
			return;
		}

		bool has_default = false;
		std::vector<u8> covered_enum_members;
		if (subject_type.kind == LS_TYPE_ENUM && subject_type.struct_index >= 0) {
			covered_enum_members.resize(m_enums[subject_type.struct_index]->members.size());
			for (u8& covered : covered_enum_members) covered = 0;
		}

		for (i32 arm_idx : stmt.children) {
			MatchArm& arm = m_module.match_arms[arm_idx];
			for (i32 pattern_idx : arm.patterns) {
				MatchPattern& pattern = m_module.match_patterns[pattern_idx];
				checkMatchPattern(unit, pattern, subject_type, &has_default);
				const i32 enum_member = enumPatternMember(pattern, subject_type);
				if (enum_member >= 0) {
					if (covered_enum_members[enum_member]) {
						m_output.errorAt(pattern.token, "Duplicate enum match case");
						return;
					}
					covered_enum_members[enum_member] = 1;
				}
			}
			checkStmt(unit, arm.stmt, return_type);
		}

		if (subject_type.kind == LS_TYPE_ENUM && !has_default) {
			for (u8 covered : covered_enum_members) {
				if (covered) continue;
				m_output.errorAt(stmt.token, "Enum match must be exhaustive or have fallback");
				return;
			}
		}
	}

	void checkStmt(Unit& unit, i32 stmt_idx, TypeRef return_type) {
		if (stmt_idx < 0 || m_output.has_error) return;
		Stmt& stmt = m_module.statements[stmt_idx];
		switch (stmt.kind) {
			case Stmt::BLOCK: {
				const i32 old_size = (i32)m_locals.size();
				const i32 old_loop_size = (i32)m_loop_labels.size();
				const i32 old_label_size = (i32)m_declared_labels.size();
				m_scope_starts.push_back(old_size);
				m_loop_scope_starts.push_back(old_loop_size);
				m_label_scope_starts.push_back(old_label_size);
				for (i32 child : stmt.children) checkStmt(unit, child, return_type);
				m_locals.resize(old_size);
				m_loop_labels.resize(old_loop_size);
				m_declared_labels.resize(old_label_size);
				m_scope_starts.pop_back();
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
				TypeRef type = stmt.type;
				if (type.kind != LS_TYPE_INVALID) resolveType(unit, type);
				if (stmt.expr >= 0) {
					TypeRef* expected = type.kind == LS_TYPE_INVALID ? nullptr : &type;
					TypeRef expr_type = checkExpr(unit, stmt.expr, expected);
					if (type.kind == LS_TYPE_INVALID)
						type = expr_type;
					else if (!canAssign(type, expr_type))
						m_output.errorAt(stmt.token, "Initializer type mismatch");
				} else if (stmt.is_undefined_init) {
					if (stmt.is_const) m_output.errorAt(stmt.token, "Const declaration can not use undefined initializer");
					if (type.kind == LS_TYPE_INVALID) m_output.errorAt(stmt.token, "Undefined initializer requires explicit type");
				} else {
					m_output.errorAt(stmt.token, "Variable declaration requires initializer");
				}
				if (type.kind != LS_TYPE_INVALID) resolveType(unit, type);
				stmt.type = type;
				stmt.local_index = (i32)m_locals.size();
				LocalInfo& local = m_locals.emplace_back();
				local.name = stmt.name;
				local.type = type;
				local.is_const = stmt.is_const;
				break;
			}
			case Stmt::EXPR: checkExpr(unit, stmt.expr); break;
			case Stmt::ASSIGN: {
				stmt.resolved_function = -1;
				TypeRef left = checkExpr(unit, stmt.left);
				TypeRef right = checkExpr(unit, stmt.right, &left);
				if (!isAssignableExpr(stmt.left)) {
					m_output.errorAt(m_module.expressions[stmt.left].token, "Assignment target must be assignable");
				}
				if (stmt.assign_op != Token::EQUAL && !isBuiltinOperatorType(left.kind)) {
					Token::Type op = Token::END_OF_FILE;
					switch (stmt.assign_op) {
						case Token::PLUS_EQUAL: op = Token::PLUS; break;
						case Token::MINUS_EQUAL: op = Token::MINUS; break;
						case Token::STAR_EQUAL: op = Token::STAR; break;
						case Token::SLASH_EQUAL: op = Token::SLASH; break;
						default: break;
					}
					if (op != Token::END_OF_FILE) {
						TypeRef operands[] = {left, right};
						i32 fn_idx = -1;
						// Compound assignment on user types is normalized to the matching
						// binary operator, then the result is written back through the same
						// assignment rules used for ordinary stores.
						if (!resolveOperatorOverload(op, std::span<const TypeRef>(operands, 2), stmt.token, &fn_idx)) return;
						if (fn_idx >= 0) {
							stmt.resolved_function = fn_idx;
							if (!canAssign(left, m_functions[fn_idx]->return_type)) {
								m_output.errorAt(stmt.token, "Assignment type mismatch");
							}
						}
					}
				}
				if (stmt.resolved_function < 0 && !canAssign(left, right)) m_output.errorAt(stmt.token, "Assignment type mismatch");
				Expr& lhs = m_module.expressions[stmt.left];
				if (lhs.kind == Expr::VAR) {
					// Assignment checks need canonical lookup too; otherwise a const pulled
					// in through imports would look mutable simply because it was not
					// spelled with the resolved module path in source.
					const i32 idx = findLocal(lhs.name);
					if (idx >= 0) {
						if (m_locals[idx].is_const) m_output.errorAt(lhs.token, "Can not assign to const '", lhs.name, "'");
					}
					else {
						Symbol global_symbol = resolveSymbol(unit, lhs.name, lhs.token);
						if (global_symbol.kind == Symbol::GLOBAL_VAR) {
							const GlobalDecl& g = global_symbol.unit->globals[global_symbol.index];
							if (g.is_const) m_output.errorAt(lhs.token, "Can not assign to const '", lhs.name, "'");
						}
					}
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
				TypeRef cond = checkExpr(unit, stmt.expr);
				if (cond.kind != LS_TYPE_BOOL) m_output.errorAt(stmt.token, "While condition must be bool");
				m_loop_labels.push_back(stmt.name);
				checkStmt(unit, stmt.right, return_type);
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
				TypeRef end_type = checkExpr(unit, stmt.left);
				if (!isNumeric(end_type)) {
					m_output.errorAt(stmt.token, "For range requires numeric bounds");
					return;
				}
				TypeRef start_type = checkExpr(unit, stmt.expr, &end_type);
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
				checkStmt(unit, stmt.right, return_type);
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
				TypeRef cond = checkExpr(unit, stmt.expr);
				if (cond.kind != LS_TYPE_BOOL) m_output.errorAt(stmt.token, "If condition must be bool");
				ls_string_view promoted_name;
				TypeRef promoted_type;
				bool promote_true_branch = false;
				if (getNullablePromotion(stmt.expr, &promoted_name, &promoted_type, &promote_true_branch)) {
					if (promote_true_branch) {
						checkStmtWithPromotion(unit, stmt.left, return_type, promoted_name, promoted_type);
						if (stmt.right >= 0) checkStmt(unit, stmt.right, return_type);
					} else {
						checkStmt(unit, stmt.left, return_type);
						if (stmt.right >= 0) checkStmtWithPromotion(unit, stmt.right, return_type, promoted_name, promoted_type);
					}
				} else {
					checkStmt(unit, stmt.left, return_type);
					if (stmt.right >= 0) checkStmt(unit, stmt.right, return_type);
				}
				break;
			}
			case Stmt::RETURN: {
				TypeRef actual = stmt.expr >= 0 ? checkExpr(unit, stmt.expr, &return_type) : TypeRef{LS_TYPE_VOID, {}, -1};
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
				checkStmt(unit, stmt.left, return_type);
				break;
			}
			case Stmt::MATCH: checkMatchStmt(unit, stmt, return_type); break;
		}
	}

	ls_module& m_module;
	OutputFormatter m_output;
	std::vector<NativeFunctionDecl*> m_native_functions;
	std::vector<FunctionDecl*> m_functions;
	std::vector<GlobalDecl*> m_globals;
	std::vector<StructDecl*> m_structs;
	std::vector<EnumDecl*> m_enums;
	std::vector<LocalInfo> m_locals;
	std::vector<i32> m_scope_starts;
	std::vector<ls_string_view> m_loop_labels;
	std::vector<i32> m_loop_scope_starts;
	std::vector<ls_string_view> m_declared_labels;
	std::vector<i32> m_label_scope_starts;
};

inline bool sameImportPathForPolicy(ls_string_view lhs, ls_string_view rhs) {
	return equalStrings(lhs, rhs);
}

static NativeFunctionDecl& addNativeFunction(Unit& unit, ls_string_view canonical_name, ls_string_view symbol_name, TypeRef return_type, std::span<const TypeRef> param_types) {
	NativeFunctionDecl& fn = unit.native_functions.emplace_back();
	splitMemberName(canonical_name, &fn.canonical_name.path, &fn.canonical_name.name);
	fn.return_type = return_type;
	for (TypeRef type : param_types) {
		Param& param = fn.params.emplace_back();
		param.type = type;
	}
	unit.symbols.push_back(Symbol{Symbol::EXTERN_FN, {unit.source_name, symbol_name}});
	return fn;
}

static void registerStdMath(ls_module& module, ls_string_view prefix) {
	(void)prefix;
	Unit& unit = module.addUnit(makeStringView("std:math"));
	const TypeRef f32_type(LS_TYPE_F32);
	const TypeRef f64_type(LS_TYPE_F64);
	const TypeRef f32_params[] = {f32_type};
	const TypeRef f64_params[] = {f64_type};
	const ls_string_view sin_name = makeStringView("std:math.sin");
	const ls_string_view cos_name = makeStringView("std:math.cos");
	const ls_string_view sqrt_name = makeStringView("std:math.sqrt");
	const ls_string_view sin_f64_name = makeStringView("std:math.sin_f64");
	const ls_string_view cos_f64_name = makeStringView("std:math.cos_f64");
	const ls_string_view sqrt_f64_name = makeStringView("std:math.sqrt_f64");

	addNativeFunction(unit, sin_name, makeStringView("sin"), f32_type, std::span<const TypeRef>(f32_params, 1));
	addNativeFunction(unit, cos_name, makeStringView("cos"), f32_type, std::span<const TypeRef>(f32_params, 1));
	addNativeFunction(unit, sqrt_name, makeStringView("sqrt"), f32_type, std::span<const TypeRef>(f32_params, 1));
	addNativeFunction(unit, sin_f64_name, makeStringView("sin_f64"), f64_type, std::span<const TypeRef>(f64_params, 1));
	addNativeFunction(unit, cos_f64_name, makeStringView("cos_f64"), f64_type, std::span<const TypeRef>(f64_params, 1));
	addNativeFunction(unit, sqrt_f64_name, makeStringView("sqrt_f64"), f64_type, std::span<const TypeRef>(f64_params, 1));
}

inline bool resolveImports(ls_module& module, ls_import_resolver_fn import_resolver, void* import_resolver_userdata) {
	std::vector<u8> state;
	std::vector<ImportDecl*> imports;
	std::vector<NativeFunctionDecl*> native_functions;

	struct Context {
		ls_module& module;
		ls_import_resolver_fn import_resolver;
		void* import_resolver_userdata;
		std::vector<u8>& state;
		std::vector<ImportDecl*>& imports;
		std::vector<NativeFunctionDecl*>& native_functions;
		OutputFormatter output;

		void ensureStateSize() {
			while (state.size() < imports.size()) state.push_back(0);
		}

		bool hasAliasCollision(i32 idx) {
			ImportDecl& import = *imports[idx];
			if (empty(import.alias)) return false;
			for (i32 i = 0; i < idx; ++i) {
				ImportDecl& previous = *imports[i];
				if (!equalStrings(import.alias, previous.alias)) continue;
				if (!sameImportPathForPolicy(import.path, previous.path)) return true;
			}
			return false;
		}

		bool resolveImport(i32 idx) {
			ensureStateSize();
			if (state[idx] == 2) return true;
			ImportDecl& import = *imports[idx];
			if (state[idx] == 1) {
				output.errorAt(import.token, "Import cycle detected at '", import.path, "'");
				return false;
			}
			for (i32 i = 0; i < idx; ++i) {
				ImportDecl& previous = *imports[i];
				if (!equalStrings(import.alias, previous.alias)) continue;
				if (!sameImportPathForPolicy(import.path, previous.path)) continue;
				if (state[i] == 1) {
					output.errorAt(import.token, "Import cycle detected at '", import.path, "'");
					return false;
				}
			}
			for (i32 i = 0; i < idx; ++i) {
				ImportDecl& previous = *imports[i];
				if (!equalStrings(import.alias, previous.alias)) continue;
				if (!sameImportPathForPolicy(import.path, previous.path)) continue;
				if (equalStrings(import.token.source_name, previous.token.source_name)) {
					output.errorAt(import.token, "Duplicate import of '", import.path, "'");
					return false;
				}
				state[idx] = 2;
				import.processed = true;
				return true;
			}
			for (i32 i = 0; i < idx; ++i) {
				ImportDecl& previous = *imports[i];
				if (!sameImportPathForPolicy(import.path, previous.path)) continue;
				state[idx] = 2;
				import.processed = true;
				return true;
			}
			if (hasAliasCollision(idx)) {
				output.errorAt(import.token, "Import alias collision for '", import.alias, "'");
				return false;
			}

			if (sameImportPathForPolicy(import.path, makeStringView("std:math"))) {
				registerStdMath(module, import.alias);
				state[idx] = 2;
				import.processed = true;
				return true;
			}

			if (!import_resolver) {
				output.errorAt(import.token, "Can not import '", import.path, "'");
				return false;
			}

			state[idx] = 1;
			const i32 old_native_count = native_functions.size();
			const i32 old_import_count = imports.size();
			ls_string_view source;
			if (!import_resolver(import_resolver_userdata, import.path, import.alias, &source)) {
				output.errorAt(import.token, "Can not import '", import.path, "'");
				return false;
			}
			if (!empty(source) && !parse(module, source, import.path, import.path)) return false;
			imports.clear();
			native_functions.clear();
			for (Unit* unit_ptr : module.units) {
				Unit& unit = *unit_ptr;
				for (ImportDecl& unit_import : unit.imports) imports.push_back(&unit_import);
				for (NativeFunctionDecl& fn : unit.native_functions) native_functions.push_back(&fn);
			}

			ensureStateSize();
			for (i32 i = old_import_count; i < (i32)imports.size(); ++i) {
				if (!resolveImport(i)) return false;
			}

			state[idx] = 2;
			import.processed = true;
			return true;
		}
	};

	Context ctx{module, import_resolver, import_resolver_userdata, state, imports, native_functions};
	ctx.output.host = &module.host;
	imports.clear();
	native_functions.clear();
	for (Unit* unit_ptr : module.units) {
		Unit& unit = *unit_ptr;
		for (ImportDecl& import : unit.imports) imports.push_back(&import);
		for (NativeFunctionDecl& fn : unit.native_functions) native_functions.push_back(&fn);
	}
	ctx.ensureStateSize();
	for (i32 i = 0; i < (i32)imports.size(); ++i) {
		if (!ctx.resolveImport(i)) return false;
	}
	return true;
}

bool compile(ls_module& module, ls_string_view source, ls_import_resolver_fn import_resolver, void* import_resolver_userdata, ls_string_view source_name) {
	if (!ls_module_parse(&module, source, source_name)) return false;
	if (!resolveImports(module, import_resolver, import_resolver_userdata)) return false;
	const ls_result result = ls_module_typecheck(&module);
	return result == LS_RESULT_OK;
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

ls_result ls_module_compile(ls_module* module, ls_string_view source, ls_string_view source_name, ls_import_resolver_fn import_resolver, void* import_resolver_userdata) {
	if (!module) return LS_RESULT_FAILURE;
	return compile(*module, source, import_resolver, import_resolver_userdata, source_name) ? LS_RESULT_OK : LS_RESULT_FAILURE;
}

}
