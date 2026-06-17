#include "compiler.h"
#include "utils.h"
#include <float.h>

struct Checker {

	ls_module& module;
	OutputFormatter error_stream;
	i32 suppress_errors = 0;

	Checker(ls_module& module)
		: module(module) {
		error_stream.host = module.host;
	}

	template <typename T, typename... Args> static T* makeType(Unit& unit, Args&&... args) {
		// Semantic nodes live as long as their owning unit. Allocating them from the
		// unit arena also keeps cached types and template instances pointer-stable.
		ls_arena& arena = *unit.arena.arena;
		void* mem = arena.allocate(arena.user_data, sizeof(T), alignof(T));
		return ::new (mem) T(static_cast<Args&&>(args)...);
	}

	ResolvedType* primitiveType(ResolvedType::Kind kind) {
		ASSERT(kind >= ResolvedType::VOID && kind < ResolvedType::META);
		return &module.primitives[kind];
	}

	static bool typesEqual(const ResolvedType* a, const ResolvedType* b) {
		if (a == b) return true;
		if (!a || !b) return false;
		if (a->kind != b->kind) return false;
		switch (a->kind) {
			case ResolvedType::FUNCTION: {
				const auto* fa = static_cast<const FunctionResolvedType*>(a);
				const auto* fb = static_cast<const FunctionResolvedType*>(b);
				if (fa->param_types.size() != fb->param_types.size()) return false;
				if (!typesEqual(fa->return_type, fb->return_type)) return false;
				for (i32 i = 0; i < fa->param_types.size(); ++i)
					if (!typesEqual(fa->param_types[i], fb->param_types[i])) return false;
				return true;
			}
			case ResolvedType::ARRAY: {
				const auto* aa = static_cast<const ArrayResolvedType*>(a);
				const auto* ab = static_cast<const ArrayResolvedType*>(b);
				return aa->size == ab->size && typesEqual(aa->element_type, ab->element_type);
			}
			case ResolvedType::SLICE: {
				const auto* sa = static_cast<const SliceResolvedType*>(a);
				const auto* sb = static_cast<const SliceResolvedType*>(b);
				return typesEqual(sa->element_type, sb->element_type);
			}
			case ResolvedType::NULLABLE: {
				const auto* na = static_cast<const NullableResolvedType*>(a);
				const auto* nb = static_cast<const NullableResolvedType*>(b);
				return typesEqual(na->inner, nb->inner);
			}
			// STRUCT/ENUM: one instance per declaration; a==b above is definitive.
			// BRACKET_TYPE: resolved away before comparison.
			default: return false;
		}
	}

	static bool canImplicitlyConvert(const ResolvedType* src, const ResolvedType* dst) {
		if (typesEqual(src, dst)) return true;
		if (!src || !dst) return false;
		if (src->kind == ResolvedType::ARRAY && dst->kind == ResolvedType::SLICE) {
			const auto* arr = static_cast<const ArrayResolvedType*>(src);
			const auto* slice = static_cast<const SliceResolvedType*>(dst);
			return typesEqual(arr->element_type, slice->element_type);
		}
		if (dst->kind == ResolvedType::NULLABLE) {
			const auto* nb = static_cast<const NullableResolvedType*>(dst);
			return typesEqual(src, nb->inner);
		}
		return false;
	}

	static bool isIntegerType(const ResolvedType* t) {
		if (!t) return false;
		switch (t->kind) {
			case ResolvedType::I8:
			case ResolvedType::I16:
			case ResolvedType::I32:
			case ResolvedType::I64:
			case ResolvedType::U8:
			case ResolvedType::U16:
			case ResolvedType::U32:
			case ResolvedType::U64: return true;
			default: return false;
		}
	}

	static bool isFloatType(const ResolvedType* t) {
		if (!t) return false;
		return t->kind == ResolvedType::F32 || t->kind == ResolvedType::F64;
	}

	// Unwrap MetaType to get the actual type for value use; pass through for value symbols.
	static ResolvedType* unwrapMeta(ResolvedType* t) { return t && t->kind == ResolvedType::META ? static_cast<MetaType*>(t)->inner : t; }

	// Literal type-checking resolves against the non-nullable destination: a hint of
	// `i32?` still constrains an integer literal as an i32.
	static ResolvedType* unwrapNullable(ResolvedType* t) { return (t && t->kind == ResolvedType::NULLABLE) ? static_cast<NullableResolvedType*>(t)->inner : t; }

	struct SemanticLocalBinding {
		ls_string_view name = {};
		ResolvedType* type = nullptr;
		bool is_immutable = false;
	};

	struct FunctionCheckContext {
		explicit FunctionCheckContext(ls_arena& arena)
			: locals(arena)
			, scope_marks(arena)
			, loop_labels(arena)
			, label_names(arena)
			, declared_loop_labels(arena)
			, declared_loop_kinds(arena) {}

		ExpArray<SemanticLocalBinding> locals;
		ExpArray<u32> scope_marks;
		ExpArray<ls_string_view> loop_labels;
		ExpArray<ls_string_view> label_names;
		ExpArray<ls_string_view> declared_loop_labels;
		ExpArray<Statement::Kind> declared_loop_kinds;
		bool comptime_only = false;
		i32 in_defer = 0;
	};

	// TODO
	void printI64(i64 value) {
		char buffer[32];
		char* end = buffer + sizeof(buffer);
		char* cursor = end;
		u64 magnitude = value < 0 ? (u64)(-(value + 1)) + 1u : (u64)value;
		do {
			*--cursor = (char)('0' + magnitude % 10u);
			magnitude /= 10u;
		} while (magnitude);
		if (value < 0) *--cursor = '-';
		error(ls_string_view{cursor, end});
	}

	ls_string_view findTypeName(const ResolvedType& type) {
		for (const Unit& candidate_unit : module.units) {
			for (const Symbol& symbol : candidate_unit.symbols) {
				if (unwrapMeta(symbol.resolved_type) == &type) return symbol.name;
				if (!symbol.expression) continue;
				if (type.kind == ResolvedType::STRUCT && symbol.expression->kind == Expression::STRUCT && static_cast<const StructResolvedType&>(type).decl == symbol.expression) {
					return symbol.name;
				}
				if (type.kind == ResolvedType::ENUM && symbol.expression->kind == Expression::ENUM && static_cast<const EnumResolvedType&>(type).decl == symbol.expression) {
					return symbol.name;
				}
			}
		}
		return {};
	}

	void error(ResolvedType* type) { error(static_cast<const ResolvedType*>(type)); }

	void error(const ResolvedType* type) {
		if (!type) {
			error("<unresolved>");
			return;
		}
		switch (type->kind) {
			case ResolvedType::VOID: error("void"); return;
			case ResolvedType::BOOL: error("bool"); return;
			case ResolvedType::I8: error("i8"); return;
			case ResolvedType::I16: error("i16"); return;
			case ResolvedType::I32: error("i32"); return;
			case ResolvedType::I64: error("i64"); return;
			case ResolvedType::U8: error("u8"); return;
			case ResolvedType::U16: error("u16"); return;
			case ResolvedType::U32: error("u32"); return;
			case ResolvedType::U64: error("u64"); return;
			case ResolvedType::F32: error("f32"); return;
			case ResolvedType::F64: error("f64"); return;
			case ResolvedType::STRING: error("string"); return;
			case ResolvedType::CPTR: error("cptr"); return;
			case ResolvedType::META: error("type"); return;
			case ResolvedType::ENUM:
			case ResolvedType::STRUCT: {
				ls_string_view name = findTypeName(*type);
				error(empty(name) ? makeStringView("<anonymous>") : name);
				if (type->kind != ResolvedType::STRUCT) return;
				const StructResolvedType* st = static_cast<const StructResolvedType*>(type);
				if (st->type_args.empty() && st->value_args.empty()) return;
				error("[");
				for (u32 i = 0; i < st->type_args.size(); ++i) {
					if (i > 0) error(", ");
					if (st->type_args[i])
						error(st->type_args[i]);
					else
						printI64(st->value_args[i]);
				}
				error("]");
				return;
			}
			case ResolvedType::FUNCTION: {
				const FunctionResolvedType* fn = static_cast<const FunctionResolvedType*>(type);
				error("fn(");
				for (u32 i = 0; i < fn->param_types.size(); ++i) {
					if (i > 0) error(", ");
					error(fn->param_types[i]);
				}
				error(") : ");
				error(fn->return_type);
				return;
			}
			case ResolvedType::ARRAY: {
				const ArrayResolvedType* array = static_cast<const ArrayResolvedType*>(type);
				error(array->element_type);
				error("[");
				printI64(array->size);
				error("]");
				return;
			}
			case ResolvedType::SLICE:
				error(static_cast<const SliceResolvedType*>(type)->element_type);
				error("[]");
				return;
			case ResolvedType::NULLABLE:
				error("?");
				error(static_cast<const NullableResolvedType*>(type)->inner);
				return;
			default: error("<invalid>"); return;
		}
	}

	template <typename T> void error(T&& arg) {
		if (suppress_errors == 0) {
			error_stream.print(static_cast<T&&>(arg));
		}
	}

	template <typename... Args> void errorLine(Token token, Args&&... args) {
		if (suppress_errors != 0) return;
		if (!empty(token.source_name)) {
			error_stream.print(token.source_name);
			error_stream.print(": ");
		}
		if (token.line > 0) {
			error_stream.print("line ");
			error_stream.print(token.line);
			error_stream.print(": ");
		}
		int dummy[] = {(error(static_cast<Args&&>(args)), 0)...};
		(void)dummy;
		error("\n");
	}

	enum class LookupPolicy { NameOnly, Checked };

	// Result of a symbol lookup. `check_failed` is set only under LookupPolicy::Checked
	// when the symbol was found but its checkSymbol() failed — distinguishing a genuine
	// declaration error from an undeclared name (both used to collapse to nullptr).
	struct SymbolRef {
		Unit* owner = nullptr;
		Symbol* symbol = nullptr;
		bool ambiguous = false;
		bool check_failed = false;
		explicit operator bool() const { return symbol && !ambiguous && !check_failed; }
	};

	bool resolveComptimeIntValue(Unit& unit, Expression* expr, i64& out) {
		if (!expr) return false;
		switch (expr->kind) {
			case Expression::INT_LITERAL: out = static_cast<IntLiteralExpression*>(expr)->value; return true;
			case Expression::IDENTIFIER: {
				IdentifierExpression* id = static_cast<IdentifierExpression*>(expr);
				SymbolRef ref = resolveSymbol(unit, {}, id->name, LookupPolicy::NameOnly);
				if (!ref.symbol || ref.symbol->storage != Symbol::COMPTIME) return false;
				if (checkSymbol(*ref.owner, *ref.symbol) == LS_RESULT_FAILURE) return false;
				return resolveComptimeIntValue(unit, ref.symbol->expression, out);
			}
			case Expression::UNARY: {
				UnaryExpression* un = static_cast<UnaryExpression*>(expr);
				if (un->op != Token::MINUS) return false;
				if (!resolveComptimeIntValue(unit, un->expression, out)) return false;
				out = -out;
				return true;
			}
			default: return false;
		}
	}

	// Name-only scan of unaliased imports. Preserve ambiguity instead of choosing a
	// match based on import order.
	SymbolRef findInUnaliasedImports(Unit& unit, ls_string_view name) {
		SymbolRef found;
		for (const Import& import : unit.imports) {
			if (!empty(import.alias)) continue;
			Unit* imported = findUnitByPath(import.path);
			if (!imported) continue;
			if (Symbol* candidate = findSymbol(*imported, name)) {
				if (found.symbol) return {nullptr, nullptr, true};
				found = {imported, candidate};
			}
		}
		return found;
	}

	// Unified symbol resolution. A bare name is ambiguous when it matches multiple
	// declarations across the current module and unaliased imports. A qualified
	// lookup is confined to the aliased unit.
	SymbolRef resolveSymbol(Unit& unit, ls_string_view qualifier, ls_string_view name, LookupPolicy policy, ResolvedType* first_arg_type = nullptr) {
		SymbolRef ref;
		if (!empty(qualifier)) {
			if (Unit* owner = findImportedUnitByAlias(unit, qualifier)) {
				if (Symbol* candidate = findSymbol(*owner, name)) ref = {owner, candidate};
			}
		} else {
			SymbolRef namespaced;
			if (first_arg_type) {
				if (Unit* namespace_unit = findTypeNamespaceUnit(*first_arg_type)) {
					if (Symbol* candidate = findSymbol(*namespace_unit, name)) {
						namespaced = {namespace_unit, candidate};
					}
				}
			}

			Symbol* local = findSymbol(unit, name);
			SymbolRef imported = findInUnaliasedImports(unit, name);
			bool ambiguous = imported.ambiguous || (local && imported.symbol);
			// ADL: namespaced is only used when no local or unaliased-import match exists
			if (local) {
				ref.owner = &unit;
				ref.symbol = local;
				ref.ambiguous = ambiguous;
			} else if (imported.symbol) {
				ref = imported;
				ref.ambiguous = ambiguous;
			} else if (namespaced.symbol) {
				ref = namespaced;
			}
		}
		if (!ref.symbol) return ref;

		if (policy == LookupPolicy::Checked && checkSymbol(*ref.owner, *ref.symbol) == LS_RESULT_FAILURE) {
			ref.check_failed = true;
		}
		return ref;
	}

	ResolvedType* resolveParsedType(Unit& unit, ParsedType* parsed) {
		if (!parsed) return nullptr;
		ResolvedType* result = nullptr;
		switch (parsed->kind) {
			case ParsedType::VOID: result = primitiveType(ResolvedType::VOID); break;
			case ParsedType::BOOL: result = primitiveType(ResolvedType::BOOL); break;
			case ParsedType::I8: result = primitiveType(ResolvedType::I8); break;
			case ParsedType::I16: result = primitiveType(ResolvedType::I16); break;
			case ParsedType::I32: result = primitiveType(ResolvedType::I32); break;
			case ParsedType::I64: result = primitiveType(ResolvedType::I64); break;
			case ParsedType::U8: result = primitiveType(ResolvedType::U8); break;
			case ParsedType::U16: result = primitiveType(ResolvedType::U16); break;
			case ParsedType::U32: result = primitiveType(ResolvedType::U32); break;
			case ParsedType::U64: result = primitiveType(ResolvedType::U64); break;
			case ParsedType::F32: result = primitiveType(ResolvedType::F32); break;
			case ParsedType::F64: result = primitiveType(ResolvedType::F64); break;
			case ParsedType::STRING: result = primitiveType(ResolvedType::STRING); break;
			case ParsedType::CPTR: result = primitiveType(ResolvedType::CPTR); break;
			case ParsedType::TYPE: {
				result = makeType<MetaType>(unit);
				break;
			}
			case ParsedType::FUNCTION: {
				FunctionParsedType* fn = static_cast<FunctionParsedType*>(parsed);
				FunctionResolvedType* resolved = makeType<FunctionResolvedType>(unit, *unit.arena.arena);
				for (ParsedType* param : fn->params) {
					ResolvedType* pt = resolveParsedType(unit, param);
					if (!pt) return nullptr;
					resolved->param_types.push(pt);
				}
				resolved->return_type = resolveParsedType(unit, fn->return_type);
				result = resolved;
				break;
			}
			case ParsedType::ARRAY: {
				ArrayParsedType* arr = static_cast<ArrayParsedType*>(parsed);
				ArrayResolvedType* resolved = makeType<ArrayResolvedType>(unit);
				resolved->element_type = resolveParsedType(unit, arr->element_type);
				i64 size = 0;
				if (!resolved->element_type || !resolveComptimeIntValue(unit, arr->size, size)) return nullptr;
				if (size <= 0) return nullptr;
				resolved->size = size;
				result = resolved;
				break;
			}
			case ParsedType::SLICE: {
				SliceParsedType* sl = static_cast<SliceParsedType*>(parsed);
				SliceResolvedType* resolved = makeType<SliceResolvedType>(unit);
				resolved->element_type = resolveParsedType(unit, sl->element_type);
				result = resolved;
				break;
			}
			case ParsedType::QUALIFIED: {
				QualifiedParsedType* q = static_cast<QualifiedParsedType*>(parsed);
				SymbolRef ref = resolveSymbol(unit, q->qualifier, q->name, LookupPolicy::Checked);
				result = ref ? unwrapMeta(ref.symbol->resolved_type) : nullptr;
				break;
			}
			case ParsedType::BRACKET_TYPE: {
				BracketTypeParsedType* call = static_cast<BracketTypeParsedType*>(parsed);
				ResolvedType* callee = resolveParsedType(unit, call->callee);
				if (!callee) return nullptr;
				if (call->args.size() == 1 && call->args[0].kind == ComptimeArg::EXPRESSION) {
					ArrayResolvedType* resolved = makeType<ArrayResolvedType>(unit);
					resolved->element_type = callee;
					i64 size = 0;
					if (!resolveComptimeIntValue(unit, call->args[0].expression, size)) return nullptr;
					if (size <= 0) return nullptr;
					resolved->size = size;
					result = resolved;
					break;
				}
				result = nullptr;
				break;
			}
			default: return nullptr;
		}
		if (result && parsed->is_nullable) {
			NullableResolvedType* nullable = makeType<NullableResolvedType>(unit);
			nullable->inner = result;
			result = nullable;
		}
		return result;
	}

	static bool isNumericType(const ResolvedType* type) {
		if (!type) return false;
		switch (type->kind) {
			case ResolvedType::I8:
			case ResolvedType::I16:
			case ResolvedType::I32:
			case ResolvedType::I64:
			case ResolvedType::U8:
			case ResolvedType::U16:
			case ResolvedType::U32:
			case ResolvedType::U64:
			case ResolvedType::F32:
			case ResolvedType::F64: return true;
			default: return false;
		}
	}

	static bool intLiteralFitsType(i64 value, ResolvedType::Kind kind) {
		switch (kind) {
			case ResolvedType::I8: return value >= -128 && value <= 127;
			case ResolvedType::U8: return value >= 0 && value <= 255;
			case ResolvedType::I16: return value >= -32768 && value <= 32767;
			case ResolvedType::U16: return value >= 0 && value <= 65535;
			case ResolvedType::I32: return value >= (i64)-2147483648LL && value <= (i64)2147483647LL;
			case ResolvedType::U32: return value >= 0 && value <= (i64)4294967295LL;
			case ResolvedType::I64: return true;
			case ResolvedType::U64: return value >= 0;
			case ResolvedType::F32: {
				// Value must be exactly representable as f32.
				if (value < -(1LL << 24) || value > (1LL << 24)) return false;
				float as_f32 = (float)value;
				return (i64)as_f32 == value;
			}
			case ResolvedType::F64: {
				// Value must be exactly representable as f64.
				double as_f64 = (double)value;
				return (i64)as_f64 == value;
			}
			default: return false;
		}
	}

	static bool isPrimitiveValueType(const ResolvedType* type) {
		if (!type) return false;
		switch (type->kind) {
			case ResolvedType::VOID:
			case ResolvedType::BOOL:
			case ResolvedType::I8:
			case ResolvedType::I16:
			case ResolvedType::I32:
			case ResolvedType::I64:
			case ResolvedType::U8:
			case ResolvedType::U16:
			case ResolvedType::U32:
			case ResolvedType::U64:
			case ResolvedType::F32:
			case ResolvedType::F64:
			case ResolvedType::STRING:
			case ResolvedType::CPTR: return true;
			default: return false;
		}
	}

	static bool isOverloadableBinaryOperator(Token::Type op) { return operatorSymbolName(op) != nullptr; }

	static bool isOverloadableUnaryOperator(Token::Type op) { return op == Token::MINUS; }

	FunctionResolvedType* buildFunctionType(Unit& unit, FunctionExpression& fn, bool reject_nullable_refs) {
		// The AST owns the canonical signature. Besides avoiding duplicate arena
		// allocations, publishing it here gives recursive checking a stable identity.
		if (fn.resolved_type) return static_cast<FunctionResolvedType*>(fn.resolved_type);
		if (!fn.comptime_params.empty()) return nullptr;

		FunctionResolvedType* fn_type = makeType<FunctionResolvedType>(unit, *unit.arena.arena);
		fn_type->decl = &fn;
		for (FunctionParam& param : fn.runtime_params) {
			param.resolved_type = resolveParsedType(unit, param.parsed_type);
			if (!param.resolved_type) {
				// TODO error msg
				return nullptr;
			}
			if (reject_nullable_refs && param.is_ref && param.resolved_type->kind == ResolvedType::NULLABLE) {
				errorLine(fn.token, "Function parameter ", param.name, " cannot be a nullable reference");
				return nullptr;
			}
			fn_type->param_types.push(param.resolved_type);
		}
		fn_type->return_type = resolveParsedType(unit, fn.return_type);
		if (!fn_type->return_type) {
			// TODO error msg
			return nullptr;
		}
		fn.resolved_type = fn_type;
		return fn_type;
	}

	static bool operatorHasPrimitiveSignature(const FunctionResolvedType& fn_type) {
		if (fn_type.param_types.empty()) return false;
		for (ResolvedType* param : fn_type.param_types) {
			if (!isPrimitiveValueType(param)) return false;
		}
		return true;
	}

	static bool operatorDeclArityMatches(Token::Type op, i32 arity) {
		if (op == Token::MINUS) return arity == 1 || arity == 2;
		return arity == 2;
	}

	// Probe-and-commit overload resolution for n-ary operators.
	// Clones each operand expression to type-check without mutating the originals,
	// then re-checks the winners in-place once a unique match is confirmed.
	// Searches the current unit and its direct unaliased imports (imports of imports
	// are deliberately not re-exported).
	enum class OverloadResult { NOT_FOUND, FOUND, AMBIGUOUS };

	OverloadResult resolveOperatorOverload(Unit& unit,
		FunctionCheckContext* ctx,
		Token::Type op,
		i32 arity,
		Expression** operands, // array of `arity` expression pointers (in/out)
		ResolvedType*& result_type
	) {
		FunctionResolvedType* found_type = nullptr;
		bool found = false;

		auto searchUnit = [&](Unit& search_unit) -> bool {
			for (Symbol& sym : search_unit.symbols) {
				if (tokenFromOperatorName(sym.name) != op) continue;
				if (!sym.expression || sym.expression->kind != Expression::FUNCTION) continue;
				FunctionResolvedType* fn_type = buildFunctionType(search_unit, *static_cast<FunctionExpression*>(sym.expression), false);
				if (!fn_type || (i32)fn_type->param_types.size() != arity) continue;

				bool match = true;
				for (i32 i = 0; i < arity && match; ++i) {
					Expression* probe = cloneExpression(unit, operands[i]);
					if (!probe) {
						match = false;
						break;
					}
					ResolvedType* t = checkExpr(unit, ctx, *probe, fn_type->param_types[(u32)i]);
					if (!t || !typesEqual(t, fn_type->param_types[(u32)i])) match = false;
				}
				if (!match) continue;

				if (found) return false; // ambiguous
				found = true;
				found_type = fn_type;
			}
			return true;
		};

		if (!searchUnit(unit)) return OverloadResult::AMBIGUOUS;
		for (const Import& import : unit.imports) {
			if (Unit* imported = findUnitByPath(import.path)) {
				if (!searchUnit(*imported)) return OverloadResult::AMBIGUOUS;
			}
		}

		if (!found) return OverloadResult::NOT_FOUND;
		for (i32 i = 0; i < arity; ++i) {
			ResolvedType* t = checkExpr(unit, ctx, *operands[i], found_type->param_types[(u32)i]);
			if (!t || !typesEqual(t, found_type->param_types[(u32)i])) return OverloadResult::NOT_FOUND;
		}
		result_type = found_type->return_type;
		return OverloadResult::FOUND;
	}

	OverloadResult resolveBinaryOperator(Unit& unit, FunctionCheckContext* ctx, Token::Type op, Expression*& lhs_expr, Expression*& rhs_expr, ResolvedType*& result_type) {
		if (!isOverloadableBinaryOperator(op)) return OverloadResult::NOT_FOUND;
		Expression* operands[2] = {lhs_expr, rhs_expr};
		OverloadResult r = resolveOperatorOverload(unit, ctx, op, 2, operands, result_type);
		if (r == OverloadResult::FOUND) {
			lhs_expr = operands[0];
			rhs_expr = operands[1];
		}
		return r;
	}

	OverloadResult resolveUnaryOperator(Unit& unit, FunctionCheckContext* ctx, Token::Type op, Expression*& expr, ResolvedType*& result_type) {
		if (!isOverloadableUnaryOperator(op)) return OverloadResult::NOT_FOUND;
		Expression* operands[1] = {expr};
		OverloadResult r = resolveOperatorOverload(unit, ctx, op, 1, operands, result_type);
		if (r == OverloadResult::FOUND) expr = operands[0];
		return r;
	}

	static Symbol* findSymbol(Unit& unit, ls_string_view name) {
		for (Symbol& sym : unit.symbols) {
			if (equalStrings(sym.name, name)) return &sym;
		}
		return nullptr;
	}

	static SemanticLocalBinding* findLocal(FunctionCheckContext& ctx, ls_string_view name) {
		for (i32 i = (i32)ctx.locals.size() - 1; i >= 0; --i) {
			SemanticLocalBinding& binding = ctx.locals[(u32)i];
			if (equalStrings(binding.name, name)) return &binding;
		}
		return nullptr;
	}

	static bool inCurrentScope(FunctionCheckContext& ctx, ls_string_view name) {
		const u32 mark = ctx.scope_marks.empty() ? 0u : ctx.scope_marks.back();
		for (u32 i = mark; i < ctx.locals.size(); ++i) {
			if (equalStrings(ctx.locals[i].name, name)) return true;
		}
		return false;
	}

	static void pushScope(FunctionCheckContext& ctx) { ctx.scope_marks.push((u32)ctx.locals.size()); }

	static void popScope(FunctionCheckContext& ctx) {
		if (ctx.scope_marks.empty()) return;
		const u32 mark = ctx.scope_marks.back();
		ctx.scope_marks.pop_back();
		while (ctx.locals.size() > mark) ctx.locals.pop_back();
	}

	static bool isFunctionLikeType(const ResolvedType* type) { return type && type->kind == ResolvedType::FUNCTION; }

	static BlockStatement* cloneBlock(Unit& unit, BlockStatement* block) {
		if (!block) return nullptr;
		BlockStatement* copy = makeType<BlockStatement>(unit, *unit.arena.arena);
		for (Statement* statement : block->statements) copy->statements.push(cloneStatement(unit, statement));
		return copy;
	}

	static Expression* cloneExpression(Unit& unit, Expression* expr) {
		if (!expr) return nullptr;
		switch (expr->kind) {
			case Expression::IDENTIFIER: {
				IdentifierExpression* src = static_cast<IdentifierExpression*>(expr);
				IdentifierExpression* dst = makeType<IdentifierExpression>(unit);
				dst->name = src->name;
				return dst;
			}
			case Expression::INT_LITERAL: {
				IntLiteralExpression* dst = makeType<IntLiteralExpression>(unit);
				dst->value = static_cast<IntLiteralExpression*>(expr)->value;
				return dst;
			}
			case Expression::FLOAT_LITERAL: {
				FloatLiteralExpression* dst = makeType<FloatLiteralExpression>(unit);
				dst->value = static_cast<FloatLiteralExpression*>(expr)->value;
				return dst;
			}
			case Expression::BOOL_LITERAL: return makeType<BoolLiteralExpression>(unit, static_cast<BoolLiteralExpression*>(expr)->value);
			case Expression::STRING_LITERAL: {
				StringLiteralExpression* dst = makeType<StringLiteralExpression>(unit);
				dst->value = static_cast<StringLiteralExpression*>(expr)->value;
				return dst;
			}
			case Expression::NULL_LITERAL: return makeType<NullLiteralExpression>(unit);
			case Expression::UNDEFINED: return makeType<UndefinedExpression>(unit);
			case Expression::TYPE_LITERAL: return makeType<TypeLiteralExpression>(unit, static_cast<TypeLiteralExpression*>(expr)->type);
			case Expression::CALL: {
				CallExpression* src = static_cast<CallExpression*>(expr);
				CallExpression* dst = makeType<CallExpression>(unit, *unit.arena.arena);
				dst->callee = cloneExpression(unit, src->callee);
				for (Expression* arg : src->args) dst->args.push(cloneExpression(unit, arg));
				return dst;
			}
			case Expression::UNARY: {
				UnaryExpression* src = static_cast<UnaryExpression*>(expr);
				UnaryExpression* dst = makeType<UnaryExpression>(unit);
				dst->op = src->op;
				dst->expression = cloneExpression(unit, src->expression);
				return dst;
			}
			case Expression::BINARY: {
				BinaryExpression* src = static_cast<BinaryExpression*>(expr);
				BinaryExpression* dst = makeType<BinaryExpression>(unit);
				dst->op = src->op;
				dst->lhs = cloneExpression(unit, src->lhs);
				dst->rhs = cloneExpression(unit, src->rhs);
				return dst;
			}
			case Expression::CAST: {
				CastExpression* src = static_cast<CastExpression*>(expr);
				CastExpression* dst = makeType<CastExpression>(unit);
				dst->parsed_type = src->parsed_type;
				dst->expression = cloneExpression(unit, src->expression);
				return dst;
			}
			case Expression::MEMBER: {
				MemberExpression* src = static_cast<MemberExpression*>(expr);
				MemberExpression* dst = makeType<MemberExpression>(unit);
				dst->name = src->name;
				dst->expression = cloneExpression(unit, src->expression);
				return dst;
			}
			case Expression::BRACKET: {
				BracketExpression* src = static_cast<BracketExpression*>(expr);
				BracketExpression* dst = makeType<BracketExpression>(unit, *unit.arena.arena);
				dst->base = cloneExpression(unit, src->base);
				dst->has_colon = src->has_colon;
				dst->end = cloneExpression(unit, src->end);
				for (Expression* arg : src->args) dst->args.push(cloneExpression(unit, arg));
				return dst;
			}
			case Expression::STRUCT_LITERAL: {
				StructLiteralExpression* src = static_cast<StructLiteralExpression*>(expr);
				StructLiteralExpression* dst = makeType<StructLiteralExpression>(unit, *unit.arena.arena);
				dst->type = cloneExpression(unit, src->type);
				for (Expression* value : src->values) dst->values.push(cloneExpression(unit, value));
				return dst;
			}
			case Expression::FUNCTION:
			case Expression::ENUM:
			case Expression::STRUCT:
			default: return nullptr;
		}
	}

	static Statement* cloneStatement(Unit& unit, Statement* statement) {
		if (!statement) return nullptr;
		switch (statement->kind) {
			case Statement::BLOCK: return cloneBlock(unit, static_cast<BlockStatement*>(statement));
			case Statement::EXPRESSION: {
				ExpressionStatement* dst = makeType<ExpressionStatement>(unit);
				dst->expression = cloneExpression(unit, static_cast<ExpressionStatement*>(statement)->expression);
				return dst;
			}
			case Statement::RETURN: {
				ReturnStatement* dst = makeType<ReturnStatement>(unit);
				dst->expression = cloneExpression(unit, static_cast<ReturnStatement*>(statement)->expression);
				return dst;
			}
			case Statement::VAR_DECL: {
				VarDeclStatement* src = static_cast<VarDeclStatement*>(statement);
				VarDeclStatement* dst = makeType<VarDeclStatement>(unit);
				dst->name = src->name;
				dst->parsed_type = src->parsed_type;
				dst->expression = cloneExpression(unit, src->expression);
				dst->is_immutable = src->is_immutable;
				return dst;
			}
			case Statement::ASSIGN: {
				AssignStatement* src = static_cast<AssignStatement*>(statement);
				AssignStatement* dst = makeType<AssignStatement>(unit);
				dst->lhs = cloneExpression(unit, src->lhs);
				dst->rhs = cloneExpression(unit, src->rhs);
				dst->op = src->op;
				return dst;
			}
			case Statement::IF: {
				IfStatement* src = static_cast<IfStatement*>(statement);
				IfStatement* dst = makeType<IfStatement>(unit);
				dst->condition = cloneExpression(unit, src->condition);
				dst->body = cloneBlock(unit, src->body);
				dst->else_branch = cloneStatement(unit, src->else_branch);
				return dst;
			}
			case Statement::MATCH: {
				MatchStatement* src = static_cast<MatchStatement*>(statement);
				MatchStatement* dst = makeType<MatchStatement>(unit, *unit.arena.arena);
				dst->subject = cloneExpression(unit, src->subject);
				for (MatchArm& src_arm : src->arms) {
					MatchArm& dst_arm = dst->arms.emplace_back(*unit.arena.arena);
					dst_arm.is_fallback = src_arm.is_fallback;
					dst_arm.body = cloneBlock(unit, src_arm.body);
					for (MatchPattern& src_pattern : src_arm.patterns) {
						MatchPattern& dst_pattern = dst_arm.patterns.emplace_back();
						dst_pattern.begin = cloneExpression(unit, src_pattern.begin);
						dst_pattern.end = cloneExpression(unit, src_pattern.end);
					}
				}
				return dst;
			}
			case Statement::WHILE: {
				WhileStatement* src = static_cast<WhileStatement*>(statement);
				WhileStatement* dst = makeType<WhileStatement>(unit);
				dst->condition = cloneExpression(unit, src->condition);
				dst->body = cloneBlock(unit, src->body);
				return dst;
			}
			case Statement::FOR: {
				ForStatement* src = static_cast<ForStatement*>(statement);
				ForStatement* dst = makeType<ForStatement>(unit);
				dst->loop_var = src->loop_var;
				dst->begin = cloneExpression(unit, src->begin);
				dst->end = cloneExpression(unit, src->end);
				dst->body = cloneBlock(unit, src->body);
				return dst;
			}
			case Statement::BREAK: {
				BreakStatement* dst = makeType<BreakStatement>(unit);
				dst->label = static_cast<BreakStatement*>(statement)->label;
				return dst;
			}
			case Statement::CONTINUE: {
				ContinueStatement* dst = makeType<ContinueStatement>(unit);
				dst->label = static_cast<ContinueStatement*>(statement)->label;
				return dst;
			}
			case Statement::DEFER: {
				DeferStatement* dst = makeType<DeferStatement>(unit);
				dst->statement = cloneStatement(unit, static_cast<DeferStatement*>(statement)->statement);
				return dst;
			}
			case Statement::LABEL: {
				LabelStatement* src = static_cast<LabelStatement*>(statement);
				LabelStatement* dst = makeType<LabelStatement>(unit);
				dst->name = src->name;
				dst->statement = cloneStatement(unit, src->statement);
				return dst;
			}
			default: return nullptr;
		}
	}

	bool findSymbolForNameExpression(Unit& unit, const Expression& expression, Unit*& owner, Symbol*& symbol) {
		ls_string_view qualifier = {};
		ls_string_view name = {};
		if (expression.kind == Expression::IDENTIFIER) {
			name = static_cast<const IdentifierExpression&>(expression).name;
		} else if (expression.kind == Expression::MEMBER) {
			const MemberExpression& member = static_cast<const MemberExpression&>(expression);
			if (!member.expression || member.expression->kind != Expression::IDENTIFIER) return false;
			qualifier = static_cast<IdentifierExpression*>(member.expression)->name;
			name = member.name;
		} else {
			return false;
		}
		SymbolRef ref = resolveSymbol(unit, qualifier, name, LookupPolicy::NameOnly);
		owner = ref.owner;
		symbol = ref.symbol;
		return ref.symbol != nullptr;
	}

	ResolvedType* resolveExpressionAsType(Unit& unit, FunctionCheckContext* ctx, Expression& expression) {
		if (expression.kind == Expression::TYPE_LITERAL) {
			ParsedType::Kind parsed_kind = static_cast<TypeLiteralExpression&>(expression).type;
			if (parsed_kind < ParsedType::VOID || parsed_kind > ParsedType::TYPE) return nullptr;
			if (parsed_kind == ParsedType::TYPE) {
				return makeType<MetaType>(unit);
			}
			return primitiveType(static_cast<ResolvedType::Kind>(parsed_kind));
		}
		if (expression.kind == Expression::IDENTIFIER || expression.kind == Expression::MEMBER) {
			Unit* owner = nullptr;
			Symbol* symbol = nullptr;
			if (!findSymbolForNameExpression(unit, expression, owner, symbol)) return nullptr;
			if (checkSymbol(*owner, *symbol) == LS_RESULT_FAILURE) return nullptr;
			if (expression.kind == Expression::IDENTIFIER) static_cast<IdentifierExpression&>(expression).symbol = symbol;
			return unwrapMeta(symbol->resolved_type);
		}
		return nullptr;
	}

	// `base.name` where `base` is an identifier naming an aliased import (or other
	// qualifier). Returns {} when `base` is not an identifier or names no such member,
	// in which case the caller falls back to treating `base` as a value.
	SymbolRef resolveQualifiedMember(Unit& unit, Expression& base, ls_string_view name) {
		if (base.kind != Expression::IDENTIFIER) return {};
		return resolveSymbol(unit, static_cast<IdentifierExpression&>(base).name, name, LookupPolicy::Checked);
	}

	ResolvedType* checkCallExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		CallExpression& call = static_cast<CallExpression&>(expr);

		// length(slice | array)
		if (call.callee->kind == Expression::IDENTIFIER && call.args.size() == 1) {
			ls_string_view name = static_cast<IdentifierExpression&>(*call.callee).name;
			if (equalStrings(name, makeStringView("length"))) {
				ResolvedType* arg = checkExpr(unit, ctx, *call.args[0], nullptr, nullptr);
				if (arg && (arg->kind == ResolvedType::ARRAY || arg->kind == ResolvedType::SLICE)) {
					expr.resolved_type = primitiveType(ResolvedType::I32);
					return expr.resolved_type;
				}
				return nullptr;
			}
		}

		// suppress errors because callee can be checked twice - one normal and one for ufcs
		// and because first arg can be checked twice - one normal and one for ADL
		++suppress_errors;
		ResolvedType* first_arg_type = nullptr;
		// try to resolve first arg for ADL
		if (!call.args.empty()) {
			first_arg_type = checkExpr(unit, ctx, *call.args[0], nullptr, nullptr);
		}

		// check callee
		ResolvedType* callee_type = checkExpr(unit, ctx, *call.callee, nullptr, first_arg_type);
		--suppress_errors;

		// UFCS: x.foo(a, b) -> foo(x, a, b)
		// When the callee is a member expression that didn't resolve as a field or namespace
		// member, try looking up the member name as a free function in the receiver type's
		// namespace unit. Restricted to struct/enum receivers to avoid UFCS on primitives.
		// Codegen's existing findMemberFunction path handles emission (receiver + args).
		u32 ufcs_param_offset = 0;
		if (!callee_type && call.callee->kind == Expression::MEMBER) {
			MemberExpression& mem = static_cast<MemberExpression&>(*call.callee);
			if (mem.expression) {
				ResolvedType* receiver_type = mem.expression->resolved_type;
				if (receiver_type) {
					Unit* ns = findTypeNamespaceUnit(*receiver_type);
					Symbol* ns_sym = ns ? findSymbol(*ns, mem.name) : nullptr;
					if (ns_sym && checkSymbol(*ns, *ns_sym) == LS_RESULT_FAILURE) ns_sym = nullptr;
					// Local preferred over namespace for struct/enum receivers
					const bool is_struct_or_enum = receiver_type->kind == ResolvedType::STRUCT || receiver_type->kind == ResolvedType::ENUM;
					Symbol* local_sym = is_struct_or_enum ? findSymbol(unit, mem.name) : nullptr;
					if (local_sym && checkSymbol(unit, *local_sym) == LS_RESULT_FAILURE) local_sym = nullptr;
					Symbol* resolved = local_sym ? local_sym : ns_sym;
					if (resolved) {
						callee_type = unwrapMeta(resolved->resolved_type);
						ufcs_param_offset = 1;
						if (resolved->expression && resolved->expression->kind == Expression::FUNCTION) {
							static_cast<CallExpression&>(expr).ufcs_fn = static_cast<FunctionExpression*>(resolved->expression);
						}
					}
				}
			}
		}

		if (!callee_type) return nullptr;

		if (callee_type->kind == ResolvedType::FUNCTION) {
			FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(callee_type);

			if (fn_type->param_types.size() != call.args.size() + ufcs_param_offset) {
				errorLine(expr.token, "Function call argument count mismatch: expected ", fn_type->param_types.size() - ufcs_param_offset, ", got ", call.args.size());
				return nullptr;
			}

			// check receiver as param[0] for UFCS calls
			if (ufcs_param_offset) {
				MemberExpression& mem = static_cast<MemberExpression&>(*call.callee);
				// resolved_type is set by checkExpr called in the UFCS probe above
				ResolvedType* receiver_type = mem.expression ? mem.expression->resolved_type : nullptr;
				if (!receiver_type || !canImplicitlyConvert(receiver_type, fn_type->param_types[0])) {
					return nullptr;
				}
			}

			// check args
			for (u32 i = 0; i < call.args.size(); ++i) {
				const u32 param_index = ufcs_param_offset + i;
				ResolvedType* param_type = fn_type->param_types[param_index];
				Expression* arg = call.args[i];

				// check ref arg
				if (fn_type->decl && fn_type->decl->runtime_params.size() > param_index && fn_type->decl->runtime_params[param_index].is_ref) {
					// TODO review ref and runtime_params for simplification
					if (arg->kind != Expression::UNARY) {
						errorLine(call.args[i]->token, "Cannot pass non-ref expression as ref argument ", i + 1, " of function call");
						return nullptr;
					}
					UnaryExpression* un = static_cast<UnaryExpression*>(arg);
					if (un->op != Token::REF) {
						return nullptr;
					}
					bool writable = false;
					ResolvedType* arg_type = checkAssignableExpr(unit, ctx, un->expression, writable);
					if (!arg_type) {
						// TODO error msg
						return nullptr;
					}
					if (!writable) {
						errorLine(call.args[i]->token, "Cannot pass non-writable expression as ref argument ", i + 1, " of function call");
						return nullptr;
					}
					if (!typesEqual(arg_type, param_type)) {
						errorLine(call.args[i]->token, "Cannot convert ", arg_type, " to ", param_type, " for ref argument ", i + 1, " of function call");
						return nullptr;
					}
					continue;
				}

				// check non-ref arg
				ResolvedType* arg_type = checkExpr(unit, ctx, *arg, param_type);
				if (!arg_type) return nullptr;
				if (!canImplicitlyConvert(arg_type, param_type)) {
					errorLine(call.args[i]->token, "Cannot convert ", arg_type, " to ", param_type, " for argument ", i + 1, " of function call");
					return nullptr;
				}
			}
			expr.resolved_type = fn_type->return_type;
			return fn_type->return_type;
		}

		if (callee_type) {
			errorLine(expr.token, "Cannot call non-function type ", callee_type);
			return nullptr;
		}
		// TODO error msg, can we even get here?
		return nullptr;
	}

	ResolvedType* checkUnaryExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		UnaryExpression& un = static_cast<UnaryExpression&>(expr);
		if (un.op == Token::MINUS && un.expression && un.expression->kind == Expression::INT_LITERAL) {
			// Range-check the negated value against the expected type.
			IntLiteralExpression* lit = static_cast<IntLiteralExpression*>(un.expression);
			ResolvedType* int_hint = unwrapNullable(hint);
			const i64 negated = -lit->value;
			if (int_hint && isNumericType(int_hint)) {
				if (!intLiteralFitsType(negated, int_hint->kind)) {
					errorLine(expr.token, "Integer literal ", negated, " does not fit in type ", int_hint);
					return nullptr;
				}
				lit->resolved_type = int_hint;
				expr.resolved_type = int_hint;
				return int_hint;
			}
			lit->resolved_type = primitiveType(ResolvedType::I32);
			expr.resolved_type = lit->resolved_type;
			return expr.resolved_type;
		}
		ResolvedType* overload_result = nullptr;
		switch (resolveUnaryOperator(unit, ctx, un.op, un.expression, overload_result)) {
			case OverloadResult::FOUND: expr.resolved_type = overload_result; return overload_result;
			case OverloadResult::AMBIGUOUS: errorLine(expr.token, "Ambiguous operator ", operatorSymbolName(un.op), " overload"); return nullptr;
			case OverloadResult::NOT_FOUND: break;
		}
		ResolvedType* inner = checkExpr(unit, ctx, *un.expression, hint);
		if (!inner) return nullptr;
		switch (un.op) {
			case Token::MINUS: {
				if (!isNumericType(inner)) {
					errorLine(expr.token, "Cannot apply negation operator to ", inner);
					return nullptr;
				}
				const ResolvedType::Kind k = inner->kind;
				if (k == ResolvedType::U8 || k == ResolvedType::U16 || k == ResolvedType::U32 || k == ResolvedType::U64) {
					// TODO error msg
					return nullptr;
				}
				expr.resolved_type = inner;
				return inner;
			}
			case Token::NOT:
				if (!typesEqual(inner, primitiveType(ResolvedType::BOOL))) {
					errorLine(expr.token, "Cannot apply not operator to ", inner);
					return nullptr;
				}
				expr.resolved_type = primitiveType(ResolvedType::BOOL);
				return expr.resolved_type;
			case Token::REF:
				// TODO error msg?
				return nullptr;
			default:
				// TODO error msg
				return nullptr;
		}
	}

	ResolvedType* checkBinaryExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		BinaryExpression& bin = static_cast<BinaryExpression&>(expr);
		ResolvedType* overload_result = nullptr;
		switch (resolveBinaryOperator(unit, ctx, bin.op, bin.lhs, bin.rhs, overload_result)) {
			case OverloadResult::FOUND: expr.resolved_type = overload_result; return overload_result;
			case OverloadResult::AMBIGUOUS: errorLine(expr.token, "Ambiguous operator ", operatorSymbolName(bin.op), " overload"); return nullptr;
			case OverloadResult::NOT_FOUND: break;
		}
		// TODO passing hint might not be correct `var b: bool = 1 > 2;` passed bool hint to 1
		++suppress_errors;
		ResolvedType* lhs = checkExpr(unit, ctx, *bin.lhs, hint);
		--suppress_errors;
		ResolvedType* rhs = checkExpr(unit, ctx, *bin.rhs, lhs ? lhs : hint);
		// If lhs failed but rhs resolved (e.g. `.Idle == state`), retry lhs with rhs type as hint.
		if (!lhs && rhs) lhs = checkExpr(unit, ctx, *bin.lhs, rhs);
		if (!lhs || !rhs) {
			// TODO error msg?
			return nullptr;
		}
		auto invalidOperands = [&]() -> ResolvedType* {
			errorLine(expr.token, "Cannot apply operator ", operatorSymbolName(bin.op), " to ", lhs, " and ", rhs);
			return nullptr;
		};
		switch (bin.op) {
			case Token::PLUS:
				if (typesEqual(lhs, primitiveType(ResolvedType::STRING)) && typesEqual(rhs, primitiveType(ResolvedType::STRING))) {
					expr.resolved_type = lhs;
					return lhs;
				}
				[[fallthrough]];
			case Token::MINUS:
			case Token::STAR:
			case Token::SLASH:
				if (!isNumericType(lhs) || !isNumericType(rhs) || !typesEqual(lhs, rhs)) return invalidOperands();
				expr.resolved_type = lhs;
				return lhs;
			case Token::PERCENT:
				if (!isIntegerType(lhs) || !isIntegerType(rhs) || !typesEqual(lhs, rhs)) return invalidOperands();
				expr.resolved_type = lhs;
				return lhs;
			case Token::EQUAL_EQUAL:
			case Token::BANG_EQUAL:
				if (!typesEqual(lhs, rhs)) return invalidOperands();
				expr.resolved_type = primitiveType(ResolvedType::BOOL);
				return expr.resolved_type;
			case Token::LT:
			case Token::LT_EQUAL:
			case Token::GT:
			case Token::GT_EQUAL:
				if (!isNumericType(lhs) || !isNumericType(rhs) || !typesEqual(lhs, rhs)) return invalidOperands();
				expr.resolved_type = primitiveType(ResolvedType::BOOL);
				return expr.resolved_type;
			case Token::AND:
			case Token::OR:
				if (!typesEqual(lhs, primitiveType(ResolvedType::BOOL)) || !typesEqual(rhs, primitiveType(ResolvedType::BOOL))) return invalidOperands();
				expr.resolved_type = primitiveType(ResolvedType::BOOL);
				return expr.resolved_type;
			default:
				// TODO error msg, can we even get here?
				return nullptr;
		}
	}

	ResolvedType* checkCastExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		CastExpression& cast = static_cast<CastExpression&>(expr);
		ResolvedType* dst_type = resolveParsedType(unit, cast.parsed_type);
		if (!dst_type) {
			errorLine(expr.token, "Cannot resolve cast type");
			return nullptr;
		}
		// Don't pass dst_type as hint: explicit casts allow out-of-range values and
		// the operand resolves independently (e.g. `-1 as u8` should work).
		ResolvedType* src_type = checkExpr(unit, ctx, *cast.expression, nullptr);
		if (!src_type) return nullptr;
		const bool src_numeric = isNumericType(src_type);
		const bool dst_numeric = isNumericType(dst_type);
		const bool src_bool = typesEqual(src_type, primitiveType(ResolvedType::BOOL));
		const bool dst_bool = typesEqual(dst_type, primitiveType(ResolvedType::BOOL));
		const bool src_enum = src_type->kind == ResolvedType::ENUM;
		const bool dst_enum = dst_type->kind == ResolvedType::ENUM;
		const bool valid_cast = (src_numeric && dst_numeric) || (src_bool && dst_bool) || (src_enum && dst_numeric) || (src_numeric && dst_enum) || (src_bool && dst_numeric) ||
								(src_numeric && dst_bool) || typesEqual(src_type, dst_type);
		if (!valid_cast) {
			errorLine(expr.token, "Cannot cast ", src_type, " to ", dst_type);
			return nullptr;
		}
		expr.resolved_type = dst_type;
		return dst_type;
	}

	ResolvedType* checkMemberExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		MemberExpression& member = static_cast<MemberExpression&>(expr);
		// .enum_member

		if (!member.expression) {
			if (!hint) {
				errorLine(expr.token, "Cannot resolve .", member.name, ", use EnumName.value syntax or provide a hint");
				return nullptr;
			}

			hint = unwrapNullable(hint);
			if (hint->kind == ResolvedType::ENUM) {
				EnumResolvedType* en = static_cast<EnumResolvedType*>(hint);
				if (!canImplicitlyConvert(en, hint)) {
					errorLine(expr.token, "Cannot convert .", member.name, " to ", hint);
					return nullptr;
				}
				for (const EnumMember& m : en->decl->members) {
					if (equalStrings(m.name, member.name)) {
						expr.resolved_type = en;
						return en;
					}
				}
				errorLine(expr.token, ".", member.name, " not found in ", hint);
				return nullptr;
			}
			errorLine(expr.token, "Cannot convert .", member.name, " to ", hint);
			return nullptr;
		}

		ResolvedType* base_type = checkExpr(unit, ctx, *member.expression, nullptr);
		if (!base_type) {
			// alias.*
			IdentifierExpression* id = static_cast<IdentifierExpression*>(member.expression); // parser makes sure member.expression is identifier
			Unit* imported_unit = findImportedUnitByAlias(unit, id->name);
			if (imported_unit) {
				SymbolRef sym = resolveSymbol(*imported_unit, {}, member.name, LookupPolicy::NameOnly);
				if (sym) {
					if (checkSymbol(*sym.owner, *sym.symbol) == LS_RESULT_FAILURE) {
						// TODO error msg?
						return nullptr;
					}
					expr.resolved_type = sym.symbol->resolved_type;
					return expr.resolved_type;
				}
				errorLine(expr.token, member.name, " not found in ", id->name);
				return nullptr;
			}
			errorLine(expr.token, "Unknown identifier ", id->name);
			return nullptr;
		}

		switch (base_type->kind) {
			case ResolvedType::STRUCT: {
				// struct.field
				StructResolvedType* st = static_cast<StructResolvedType*>(base_type);
				for (const NamedDecl& field : st->decl->fields) {
					if (equalStrings(field.name, member.name)) {
						expr.resolved_type = field.resolved_type;
						return field.resolved_type;
					}
				}
				errorLine(expr.token, member.name, " not found in ", base_type);
				return nullptr;
			}
			case ResolvedType::META: {
				// TypeName.member — only enums support member access through a type name
				ResolvedType* inner = static_cast<MetaType*>(base_type)->inner;
				if (inner->kind == ResolvedType::ENUM) {
					EnumResolvedType* en = static_cast<EnumResolvedType*>(inner);
					for (const EnumMember& m : en->decl->members) {
						if (equalStrings(m.name, member.name)) {
							expr.resolved_type = inner;
							return inner;
						}
					}
					errorLine(expr.token, member.name, " not found in ", inner);
					return nullptr;
				}
				errorLine(expr.token, "Cannot access member '", member.name, "' on type");
				return nullptr;
			}
			case ResolvedType::ENUM: {
				// If the name matches a variant, the user wrote instance.Variant — give a clear error.
				// Otherwise return nullptr silently so the call checker can try UFCS.
				EnumResolvedType* en = static_cast<EnumResolvedType*>(base_type);
				for (const EnumMember& m : en->decl->members) {
					if (equalStrings(m.name, member.name)) {
						errorLine(expr.token, "Cannot access enum member '", member.name, "' through an instance; use the enum type name instead");
						return nullptr;
					}
				}
				return nullptr;
			}
			case ResolvedType::NULLABLE: errorLine(expr.token, "Cannot access member ", member.name, " of nullable type without a null check"); return nullptr;

			default:
				if (isPrimitiveValueType(base_type)) {
					errorLine(expr.token, "Cannot access member ", member.name, " of primitive type ", base_type);
					return nullptr;
				}
				return nullptr; // TODO can we get here? / TODO error msg
		}
	}

	ResolvedType* checkBracketExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		BracketExpression& br = static_cast<BracketExpression&>(expr);
		if (ResolvedType* template_type = resolveExpressionAsType(unit, ctx, expr)) {
			expr.resolved_type = template_type;
			return template_type;
		}
		ResolvedType* base_type = checkExpr(unit, ctx, *br.base, nullptr);
		if (!base_type) return nullptr;
		if (base_type->kind == ResolvedType::NULLABLE) {
			errorLine(expr.token, "Cannot index nullable type without a null check");
			return nullptr;
		}
		if (base_type->kind != ResolvedType::ARRAY && base_type->kind != ResolvedType::SLICE) {
			errorLine(expr.token, "Cannot index type ", base_type);
			return nullptr;
		}
		if (br.has_colon) {
			for (Expression* arg : br.args) {
				ResolvedType* arg_type = checkExpr(unit, ctx, *arg, primitiveType(ResolvedType::I32));
				if (!arg_type || !isIntegerType(arg_type)) {
					// TODO error msg
					return nullptr;
				}
			}
			if (br.end) {
				ResolvedType* end_type = checkExpr(unit, ctx, *br.end, primitiveType(ResolvedType::I32));
				if (!end_type || !isIntegerType(end_type)) {
					// TODO error msg
					return nullptr;
				}
			}
			const ArrayResolvedType* arr = base_type->kind == ResolvedType::ARRAY ? static_cast<const ArrayResolvedType*>(base_type) : nullptr;
			i64 begin = 0;
			i64 end = arr ? arr->size : 0;
			const bool has_begin = !br.args.empty() && resolveComptimeIntValue(unit, br.args[0], begin);
			const bool has_end = br.end ? resolveComptimeIntValue(unit, br.end, end) : !!arr;
			if (arr) {
				if (has_begin && (begin < 0 || begin > arr->size)) {
					errorLine(expr.token, "Array slice begin index out of bounds: ", begin, " (array size: ", arr->size, ")");
					return nullptr;
				}
				if (has_end && (end < 0 || end > arr->size)) {
					errorLine(expr.token, "Array slice end index out of bounds: ", end, " (array size: ", arr->size, ")");
					return nullptr;
				}
				if (has_begin && has_end && begin > end) {
					errorLine(expr.token, "Array slice begin index ", begin, " is greater than end index ", end);
					return nullptr;
				}
			}
			SliceResolvedType* slice = makeType<SliceResolvedType>(unit);
			slice->element_type = arr ? arr->element_type : static_cast<SliceResolvedType*>(base_type)->element_type;
			expr.resolved_type = slice;
			return slice;
		}
		if (br.args.size() != 1) {
			// TODO error msg
			return nullptr;
		}
		ResolvedType* index_type = checkExpr(unit, ctx, *br.args[0], primitiveType(ResolvedType::I32));
		if (!index_type) {
			// TODO error msg
			return nullptr;
		}
		if (!isIntegerType(index_type)) {
			errorLine(expr.token, "Cannot index with type ", index_type, ", expected integer type");
			return nullptr;
		}
		if (base_type->kind == ResolvedType::ARRAY) {
			const ArrayResolvedType* arr = static_cast<const ArrayResolvedType*>(base_type);
			i64 index = 0;
			if (resolveComptimeIntValue(unit, br.args[0], index)) {
				if (index < 0 || index >= arr->size) {
					errorLine(expr.token, "Array index out of bounds: ", index, " (array size: ", arr->size, ")");
					return nullptr;
				}
			}
			expr.resolved_type = arr->element_type;
		} else {
			expr.resolved_type = static_cast<SliceResolvedType*>(base_type)->element_type;
		}
		return expr.resolved_type;
	}

	ResolvedType* checkStructLiteralExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		StructLiteralExpression& lit = static_cast<StructLiteralExpression&>(expr);
		// In the type position of a literal, a top-level type name wins over a
		// same-named local value. This is also what lets `template[x](x { ... })`
		// use `x` as a type argument and as an ordinary local index nearby.
		ResolvedType* type = nullptr;
		if (lit.type) {
			type = resolveExpressionAsType(unit, ctx, *lit.type);
			if (!type) {
				// Happens when lit.type is a bracket expression (e.g. a template instantiation like Foo[T]).
				// resolveExpressionAsType only handles TYPE_LITERAL, IDENTIFIER, and MEMBER.
				type = checkExpr(unit, ctx, *lit.type, hint);
			}
		}
		if (!type) type = hint;
		if (!type) {
			errorLine(expr.token, "Cannot resolve struct literal type");
			return nullptr;
		}
		if (type->kind != ResolvedType::STRUCT) {
			errorLine(expr.token, "Expected struct type, got ", type);
			return nullptr;
		}
		if (lit.type) lit.type->resolved_type = type;
		StructResolvedType* st = static_cast<StructResolvedType*>(type);
		if (st->decl->fields.size() != lit.values.size()) {
			errorLine(expr.token, "Struct literal has ", lit.values.size(), " values, but struct type has ", st->decl->fields.size(), " fields");
			return nullptr;
		}
		for (i32 i = 0; i < lit.values.size(); ++i) {
			ResolvedType* field_type = (u32)i < st->field_types.size() ? st->field_types[(u32)i] : st->decl->fields[(u32)i].resolved_type;
			ResolvedType* value_type = checkExpr(unit, ctx, *lit.values[i], field_type);
			if (!value_type || !canImplicitlyConvert(value_type, field_type)) {
				errorLine(lit.values[i]->token, "Cannot convert ", value_type, " to ", field_type, " for field ", st->decl->fields[(u32)i].name, " of struct literal");
				return nullptr;
			}
		}
		expr.resolved_type = type;
		return type;
	}

	ResolvedType* checkExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint, ResolvedType* first_arg_type = nullptr) {
		switch (expr.kind) {
			case Expression::INT_LITERAL: {
				ResolvedType* int_hint = unwrapNullable(hint);
				if (int_hint && isNumericType(int_hint)) {
					const i64 value = static_cast<IntLiteralExpression&>(expr).value;
					if (!intLiteralFitsType(value, int_hint->kind)) {
						errorLine(expr.token, "Integer literal does not fit in ", int_hint);
						return nullptr;
					}
					expr.resolved_type = int_hint;
				} else {
					expr.resolved_type = primitiveType(ResolvedType::I32);
				}
				return expr.resolved_type;
			}
			case Expression::FLOAT_LITERAL: {
				ResolvedType* float_hint = unwrapNullable(hint);
				if (float_hint && float_hint->kind == ResolvedType::F32) {
					const double value = static_cast<FloatLiteralExpression&>(expr).value;
					if (value > (double)FLT_MAX || value < -(double)FLT_MAX) {
						// TODO error msg
						errorLine(expr.token, "Float literal does not fit in f32");
						return nullptr;
					}
					expr.resolved_type = float_hint;
				} else {
					expr.resolved_type = (float_hint && isFloatType(float_hint)) ? float_hint : primitiveType(ResolvedType::F64);
				}
				return expr.resolved_type;
			}
			case Expression::BOOL_LITERAL: expr.resolved_type = primitiveType(ResolvedType::BOOL); return expr.resolved_type;
			case Expression::STRING_LITERAL: expr.resolved_type = primitiveType(ResolvedType::STRING); return expr.resolved_type;
			case Expression::NULL_LITERAL:
				if (!hint) {
					// TODO error msg
					return nullptr;
				}
				if (hint->kind != ResolvedType::NULLABLE && hint->kind != ResolvedType::SLICE) {
					errorLine(expr.token, "Cannot use null literal as ", hint);
					return nullptr;
				}
				expr.resolved_type = hint;
				return expr.resolved_type;
			case Expression::UNDEFINED: expr.resolved_type = hint; return expr.resolved_type;
			case Expression::TYPE_LITERAL: {
				if (!ctx || !ctx->comptime_only) return nullptr;
				ParsedType::Kind parsed_kind = static_cast<TypeLiteralExpression&>(expr).type;
				if (parsed_kind == ParsedType::TYPE) {
					expr.resolved_type = makeType<MetaType>(unit);
					return expr.resolved_type;
				}
				ResolvedType* t = primitiveType(static_cast<ResolvedType::Kind>(parsed_kind));
				expr.resolved_type = t;
				return t;
			}
			case Expression::IDENTIFIER: {
				IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
				if (ctx) {
					if (SemanticLocalBinding* local = findLocal(*ctx, id.name)) {
						id.symbol = nullptr;
						expr.resolved_type = local->type;
						return expr.resolved_type;
					}
				}
				SymbolRef ref = resolveSymbol(unit, {}, id.name, LookupPolicy::Checked, first_arg_type);
				if (!ref) {
					errorLine(expr.token, "Unknown identifier ", id.name);
					return nullptr;
				}
				if (ctx && ctx->comptime_only && ref.symbol->storage != Symbol::COMPTIME) {
					errorLine(expr.token, "Cannot use non-comptime symbol ", id.name, " in comptime context");
					return nullptr;
				}
				id.symbol = ref.symbol;
				expr.resolved_type = ref.symbol->resolved_type;
				return expr.resolved_type;
			}
			case Expression::FUNCTION: {
				FunctionExpression& fn = static_cast<FunctionExpression&>(expr);
				if (fn.resolved_type) return fn.resolved_type;
				FunctionResolvedType* fn_type = buildFunctionType(unit, fn, true);
				if (!fn_type) {
					// TODO error msg
					return nullptr;
				}
				expr.resolved_type = fn_type;
				if (fn.body) {
					if (!checkFunctionBody(unit, fn)) return nullptr;
				}
				return fn_type;
			}
			case Expression::CALL: return checkCallExpr(unit, ctx, expr, hint);
			case Expression::UNARY: return checkUnaryExpr(unit, ctx, expr, hint);
			case Expression::BINARY: return checkBinaryExpr(unit, ctx, expr, hint);
			case Expression::CAST: return checkCastExpr(unit, ctx, expr, hint);
			case Expression::MEMBER: return checkMemberExpr(unit, ctx, expr, hint);
			case Expression::BRACKET: return checkBracketExpr(unit, ctx, expr, hint);
			case Expression::STRUCT_LITERAL: return checkStructLiteralExpr(unit, ctx, expr, hint);
			default: return nullptr;
		}
	}

	ResolvedType* checkAssignableExpr(Unit& unit, FunctionCheckContext* ctx, Expression* expr, bool& is_writable) {
		if (!expr) {
			is_writable = false;
			return nullptr;
		}
		switch (expr->kind) {
			case Expression::IDENTIFIER: {
				IdentifierExpression* id = static_cast<IdentifierExpression*>(expr);
				if (ctx) {
					if (SemanticLocalBinding* local = findLocal(*ctx, id->name)) {
						is_writable = !local->is_immutable;
						return local->type;
					}
				}
				SymbolRef ref = resolveSymbol(unit, {}, id->name, LookupPolicy::Checked);
				if (!ref) {
					is_writable = false;
					return nullptr;
				}
				id->symbol = ref.symbol;
				is_writable = ref.symbol->storage == Symbol::VARIABLE;
				return unwrapMeta(ref.symbol->resolved_type);
			}
			case Expression::MEMBER: {
				MemberExpression* member = static_cast<MemberExpression*>(expr);
				if (!member->expression) {
					is_writable = false;
					return nullptr;
				}
				if (SymbolRef ref = resolveQualifiedMember(unit, *member->expression, member->name)) {
					is_writable = ref.symbol->storage == Symbol::VARIABLE;
					return unwrapMeta(ref.symbol->resolved_type);
				}
				bool base_writable = false;
				ResolvedType* base_type = checkAssignableExpr(unit, ctx, member->expression, base_writable);
				if (!base_type || !base_writable) {
					is_writable = false;
					return nullptr;
				}
				ResolvedType* field_type = checkExpr(unit, ctx, *expr, nullptr);
				is_writable = field_type != nullptr;
				return field_type;
			}
			case Expression::BRACKET: {
				BracketExpression* br = static_cast<BracketExpression*>(expr);
				bool base_writable = false;
				ResolvedType* base_type = checkAssignableExpr(unit, ctx, br->base, base_writable);
				if (!base_type || !base_writable) {
					is_writable = false;
					return nullptr;
				}
				ResolvedType* value_type = checkExpr(unit, ctx, *expr, nullptr);
				is_writable = value_type != nullptr;
				return value_type;
			}
			default: is_writable = false; return nullptr;
		}
	}

	static bool isPrimitiveShadowName(ls_string_view name) {
		static const char* names[] = {"void", "bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64", "string", "cptr", "type"};
		for (const char* primitive : names) {
			if (equalStrings(name, makeStringView(primitive))) return true;
		}
		return false;
	}

	Unit* findUnitByPath(ls_string_view path) {
		for (Unit& unit : module.units) {
			if (equalStrings(unit.path, path)) return &unit;
		}
		return nullptr;
	}

	Unit* findImportedUnitByAlias(Unit& unit, ls_string_view alias) {
		for (const Import& import : unit.imports) {
			if (!equalStrings(import.alias, alias)) continue;
			return findUnitByPath(import.path);
		}
		return nullptr;
	}

	Unit* findTypeNamespaceUnit(const ResolvedType& type) {
		const Expression* decl = nullptr;
		switch (type.kind) {
			case ResolvedType::STRUCT: decl = static_cast<const StructResolvedType&>(type).decl; break;
			case ResolvedType::ENUM: decl = static_cast<const EnumResolvedType&>(type).decl; break;
			default: return nullptr;
		}
		if (!decl) return nullptr;

		for (Unit& candidate_unit : module.units) {
			for (Symbol& symbol : candidate_unit.symbols) {
				if (unwrapMeta(symbol.resolved_type) == &type) return &candidate_unit;
				if (symbol.expression == decl) return &candidate_unit;
			}
		}
		return nullptr;
	}

	bool checkFunctionBody(Unit& unit, FunctionExpression& fn) {
		if (!fn.body) return true;
		if (fn.body->kind != Statement::BLOCK) {
			// TODO error msg
			return false;
		}

		ResolvedType* return_type = fn.resolved_type ? static_cast<FunctionResolvedType*>(fn.resolved_type)->return_type : nullptr;
		FunctionCheckContext ctx(*unit.arena.arena);
		pushScope(ctx);
		for (FunctionParam& param : fn.runtime_params) {
			if (findSymbol(unit, param.name)) {
				errorLine(fn.token, "Parameter ", param.name, " shadows a global symbol");
				return false;
			}
			SemanticLocalBinding& binding = ctx.locals.emplace_back();
			binding.name = param.name;
			binding.type = param.resolved_type;
			binding.is_immutable = !param.is_ref;
		}

		BlockStatement* body = static_cast<BlockStatement*>(fn.body);
		for (Statement* st : body->statements) {
			if (!checkStatement(unit, ctx, st, return_type, {})) return false;
		}
		return true;
	}

	static bool checkLabelTarget(FunctionCheckContext& ctx, ls_string_view label) {
		if (empty(label)) return !ctx.loop_labels.empty();
		for (i32 i = (i32)ctx.loop_labels.size() - 1; i >= 0; --i) {
			if (equalStrings(ctx.loop_labels[(u32)i], label)) return true;
		}
		return false;
	}


	bool checkVarDeclStatement(Unit& unit, FunctionCheckContext& ctx, VarDeclStatement* var) {
		if (findLocal(ctx, var->name)) {
			errorLine(var->token, "Variable ", var->name, " shadows an existing local or parameter");
			return false;
		}

		if (findSymbol(unit, var->name)) {
			errorLine(var->token, "Variable ", var->name, " conflicts with a symbol of the same name in the same unit");
			return false;
		}

		ResolvedType* annotation = resolveParsedType(unit, var->parsed_type);
		if (var->expression->kind == Expression::UNDEFINED) {
			if (!annotation) {
				errorLine(var->token, "Variable ", var->name, " must have a type annotation if initialized with undefined");
				return false;
			}
			if (var->is_immutable) {
				errorLine(var->token, "Variable ", var->name, " cannot be immutable if initialized with undefined");
				return false;
			}
		}

		ResolvedType* expr_type = checkExpr(unit, &ctx, *var->expression, annotation);
		if (!expr_type) return false;

		if (annotation && !canImplicitlyConvert(expr_type, annotation)) {
			errorLine(var->token, "Cannot convert ", expr_type, " to ", annotation, " for variable ", var->name);
			return false;
		}
		ResolvedType* final_type = annotation ? annotation : expr_type;
		var->resolved_type = final_type;

		SemanticLocalBinding& binding = ctx.locals.emplace_back();
		binding.name = var->name;
		binding.type = final_type;
		binding.is_immutable = var->is_immutable;
		return true;
	}

	bool checkAssignStatement(Unit& unit, FunctionCheckContext& ctx, AssignStatement* assign) {
		bool writable = false;
		ResolvedType* lhs_type = checkAssignableExpr(unit, &ctx, assign->lhs, writable);
		if (!lhs_type) return false;
		if (!writable) {
			errorLine(assign->token, "Epression is immutable and cannot be assigned to");
			return false;
		}
		ResolvedType* rhs_type = checkExpr(unit, &ctx, *assign->rhs, lhs_type);
		if (!rhs_type) return false;
		ResolvedType* op_result = nullptr;
		switch (assign->op) {
			case Token::EQUAL:
				if (!canImplicitlyConvert(rhs_type, lhs_type)) {
					errorLine(assign->token, "Cannot convert ", rhs_type, " to ", lhs_type, " for assignment");
					return false;
				}
				return true;
			case Token::PLUS_EQUAL:
			case Token::MINUS_EQUAL:
			case Token::STAR_EQUAL:
			case Token::SLASH_EQUAL: {
				if (isNumericType(lhs_type)) return true;
				const Token::Type base_op = assign->op == Token::PLUS_EQUAL	   ? Token::PLUS
											: assign->op == Token::MINUS_EQUAL ? Token::MINUS
											: assign->op == Token::STAR_EQUAL  ? Token::STAR
																			   : Token::SLASH;
				Expression* operands[2] = {assign->lhs, assign->rhs};
				switch (resolveOperatorOverload(unit, &ctx, base_op, 2, operands, op_result)) {
					case OverloadResult::FOUND: break;
					case OverloadResult::AMBIGUOUS: errorLine(assign->token, "Ambiguous operator overload for compound assignment on type ", lhs_type); return false;
					case OverloadResult::NOT_FOUND: errorLine(assign->token, "No matching operator overload for compound assignment on type ", lhs_type); return false;
				}
				assign->lhs = operands[0];
				assign->rhs = operands[1];

				if (!canImplicitlyConvert(op_result, lhs_type)) {
					errorLine(assign->token, "Compound assignment operator returns ", op_result, " which cannot be implicitly converted to the target type ", lhs_type);
					return false;
				}
				return true;
			}
			default: return false; // TODO can we even get here?
		}
	}

	bool checkIfStatement(Unit& unit, FunctionCheckContext& ctx, IfStatement* ifst, ResolvedType* return_type) {
		ResolvedType* cond = checkExpr(unit, &ctx, *ifst->condition, primitiveType(ResolvedType::BOOL));
		if (!cond) return false;
		if (!typesEqual(cond, primitiveType(ResolvedType::BOOL))) {
			errorLine(ifst->token, "If condition must be of type bool, got ", cond);
			return false;
		}

		// Detect `x != null` / `x == null` to narrow x inside the respective branch.
		ls_string_view narrowed_name = {};
		ResolvedType* narrowed_type = nullptr;
		bool narrowed_is_immutable = false;
		bool narrow_in_true = false;
		if (ifst->condition && ifst->condition->kind == Expression::BINARY) {
			BinaryExpression* bin = static_cast<BinaryExpression*>(ifst->condition);
			if (bin->op == Token::BANG_EQUAL || bin->op == Token::EQUAL_EQUAL) {
				Expression* id_side = nullptr;
				if (bin->rhs && bin->rhs->kind == Expression::NULL_LITERAL)
					id_side = bin->lhs;
				else if (bin->lhs && bin->lhs->kind == Expression::NULL_LITERAL)
					id_side = bin->rhs;
				if (id_side && id_side->kind == Expression::IDENTIFIER) {
					ResolvedType* id_type = id_side->resolved_type;
					if (id_type && id_type->kind == ResolvedType::NULLABLE) {
						IdentifierExpression* id = static_cast<IdentifierExpression*>(id_side);
						narrowed_name = id->name;
						narrowed_type = static_cast<NullableResolvedType*>(id_type)->inner;
						if (SemanticLocalBinding* local = findLocal(ctx, id->name)) {
							narrowed_is_immutable = local->is_immutable;
						} else if (id->symbol) {
							narrowed_is_immutable = id->symbol->storage != Symbol::VARIABLE;
						}
						narrow_in_true = (bin->op == Token::BANG_EQUAL);
					}
				}
			}
		}

		auto checkBranchWithNarrowing = [&](Statement* branch, bool apply_narrowing) -> bool {
			if (!branch) return true;
			if (apply_narrowing && narrowed_type) {
				pushScope(ctx);
				SemanticLocalBinding& nb = ctx.locals.emplace_back();
				nb.name = narrowed_name;
				nb.type = narrowed_type;
				nb.is_immutable = narrowed_is_immutable;
				bool ok = checkStatement(unit, ctx, branch, return_type, {});
				popScope(ctx);
				return ok;
			}
			return checkStatement(unit, ctx, branch, return_type, {});
		};

		if (!checkBranchWithNarrowing(ifst->body, narrow_in_true)) return false;
		if (!checkBranchWithNarrowing(ifst->else_branch, !narrow_in_true)) return false;
		return true;
	}

	bool checkForStatement(Unit& unit, FunctionCheckContext& ctx, ForStatement* fs, ResolvedType* return_type, ls_string_view pending_label) {
		ResolvedType* begin_type = checkExpr(unit, &ctx, *fs->begin, primitiveType(ResolvedType::I32));
		ResolvedType* end_type = checkExpr(unit, &ctx, *fs->end, begin_type ? begin_type : primitiveType(ResolvedType::I32));
		if (!begin_type || !end_type || !isIntegerType(begin_type) || !isIntegerType(end_type)) {
			errorLine(fs->token, "For loop bounds must be of integer type, got ", begin_type, " and ", end_type);
			return false;
		}
		if (!typesEqual(begin_type, end_type)) {
			// TODO error msg
			return false;
		}

		pushScope(ctx);
		SemanticLocalBinding& binding = ctx.locals.emplace_back();
		binding.name = fs->loop_var;
		binding.type = begin_type;
		binding.is_immutable = true;
		ctx.loop_labels.push(pending_label);
		bool ok = checkStatement(unit, ctx, fs->body, return_type, {});
		ctx.loop_labels.pop_back();
		popScope(ctx);
		return ok;
	}

	bool checkLabelStatement(Unit& unit, FunctionCheckContext& ctx, LabelStatement* label, ResolvedType* return_type) {
		for (i32 i = (i32)ctx.label_names.size() - 1; i >= 0; --i) {
			if (equalStrings(ctx.label_names[(u32)i], label->name)) {
				errorLine(label->token, "Label ", label->name, " already declared in this function"); // TODO isn't this already caught below?
				return false;
			}
		}
		const bool labeled_loop = label->statement && (label->statement->kind == Statement::WHILE || label->statement->kind == Statement::FOR);
		if (labeled_loop) {
			// Active labels catch lexical duplicates. Keep a second function-wide
			// registry because reusing a name for a different loop construct is
			// ambiguous to later control-flow lowering, while sequential loops of
			// the same construct intentionally reuse labels.
			bool known_label = false;
			for (u32 i = 0; i < ctx.declared_loop_labels.size(); ++i) {
				if (!equalStrings(ctx.declared_loop_labels[i], label->name)) continue;
				if (ctx.declared_loop_kinds[i] != label->statement->kind) {
					errorLine(label->token, "Label ", label->name, " already declared for a different loop construct");
					return false;
				}
				known_label = true;
				break;
			}
			if (!known_label) {
				ctx.declared_loop_labels.push(label->name);
				ctx.declared_loop_kinds.push(label->statement->kind);
			}
		}
		ctx.label_names.push(label->name);
		const bool ok = checkStatement(unit, ctx, label->statement, return_type, labeled_loop ? label->name : ls_string_view{});
		ctx.label_names.pop_back();
		return ok;
	}

	bool checkMatchStatement(Unit& unit, FunctionCheckContext& ctx, MatchStatement* ms, ResolvedType* return_type) {
		ResolvedType* subject = checkExpr(unit, &ctx, *ms->subject, nullptr);
		if (!subject) return false;

		// Subject must be a scalar numeric type, enum, or string.
		const bool subject_is_numeric = isNumericType(subject);
		const bool subject_is_enum = subject->kind == ResolvedType::ENUM;
		const bool subject_is_string = subject->kind == ResolvedType::STRING;
		if (!subject_is_numeric && !subject_is_enum && !subject_is_string) {
			errorLine(ms->token, "Match statement subject must be a numeric type, enum, or string, got ", subject);
			return false;
		}

		bool has_fallback = false;
		// Track covered enum members for exhaustiveness checking.
		const EnumResolvedType* subject_enum = subject_is_enum ? static_cast<const EnumResolvedType*>(subject) : nullptr;
		ExpArray<bool> covered_enum_members(unit.arena);
		if (subject_enum) covered_enum_members.resize(subject_enum->decl->members.size(), false);
		u32 covered_enum_count = 0;

		for (MatchArm& arm : ms->arms) {
			if (arm.is_fallback) {
				if (has_fallback) {
					// TODO error msg
					return false;
				}
				has_fallback = true;
			}
			for (MatchPattern& pattern : arm.patterns) {
				ResolvedType* begin = checkExpr(unit, &ctx, *pattern.begin, subject);
				if (!begin || !typesEqual(begin, subject)) return false;
				if (pattern.end) {
					// Range patterns are only valid for numeric types.
					if (!subject_is_numeric) {
						errorLine(pattern.begin->token, "Range patterns are only valid for numeric types, got ", subject);
						return false;
					}
					ResolvedType* end = checkExpr(unit, &ctx, *pattern.end, subject);
					if (!end || !typesEqual(end, subject)) return false;
				}
				// Track enum coverage and detect duplicates.
				if (subject_enum && pattern.begin && pattern.begin->kind == Expression::MEMBER) {
					MemberExpression* mem = static_cast<MemberExpression*>(pattern.begin);
					for (u32 i = 0; i < (u32)subject_enum->decl->members.size(); ++i) {
						if (!equalStrings(subject_enum->decl->members[i].name, mem->name)) continue;
						if (covered_enum_members[i]) {
							errorLine(pattern.begin->token, "Duplicate match arm for enum member ", mem->name);
							return false;
						}
						covered_enum_members[i] = true;
						++covered_enum_count;
						break;
					}
				}
			}
			if (!checkStatement(unit, ctx, arm.body, return_type, {})) return false;
		}

		// Enum match must cover all variants or have a fallback.
		if (subject_enum && !has_fallback) {
			if (covered_enum_count != (u32)subject_enum->decl->members.size()) {
				errorLine(ms->token, "Match statement on enum is not exhaustive");
				return false;
			}
		}

		return true;
	}

	bool checkStatement(Unit& unit, FunctionCheckContext& ctx, Statement* st, ResolvedType* return_type, ls_string_view pending_label) {
		if (!st) return true;

		switch (st->kind) {
			case Statement::BLOCK: {
				BlockStatement* block = static_cast<BlockStatement*>(st);
				pushScope(ctx);
				for (Statement* child : block->statements) {
					if (!checkStatement(unit, ctx, child, return_type, {})) {
						popScope(ctx);
						return false;
					}
				}
				popScope(ctx);
				return true;
			}
			case Statement::EXPRESSION: {
				ExpressionStatement* expr = static_cast<ExpressionStatement*>(st);
				if (!checkExpr(unit, &ctx, *expr->expression, nullptr)) return false;
				return true;
			}
			case Statement::RETURN: {
				ReturnStatement* ret = static_cast<ReturnStatement*>(st);
				if (ctx.in_defer) {
					errorLine(ret->token, "Defer statement cannot contain a return statement");
					return false;
				}
				if (!return_type) {
					// TODO error msg
					return false;
				}
				if (return_type->kind == ResolvedType::VOID) {
					if (ret->expression) {
						// TODO error msg
						// TODO return?
						return false;
					}
					return ret->expression == nullptr;
				}
				if (!ret->expression) {
					// TODO error msg
					return false;
				}
				ResolvedType* expr_type = checkExpr(unit, &ctx, *ret->expression, return_type);
				if (!expr_type) return false;
				if (!canImplicitlyConvert(expr_type, return_type)) {
					errorLine(ret->token, "Return expression type ", expr_type, " does not match function return type ", return_type);
					return false;
				}
				return true;
			}
			case Statement::WHILE: {
				WhileStatement* ws = static_cast<WhileStatement*>(st);
				ResolvedType* cond = checkExpr(unit, &ctx, *ws->condition, primitiveType(ResolvedType::BOOL));
				if (!cond || !typesEqual(cond, primitiveType(ResolvedType::BOOL))) {
					errorLine(ws->token, "While condition must be of type bool, got ", cond);
					return false;
				}
				ctx.loop_labels.push(pending_label);
				bool ok = checkStatement(unit, ctx, ws->body, return_type, {});
				ctx.loop_labels.pop_back();
				return ok;
			}
			case Statement::FOR: {
				return checkForStatement(unit, ctx, static_cast<ForStatement*>(st), return_type, pending_label);
			}
			case Statement::BREAK:
			case Statement::CONTINUE: {
				BreakStatement* br = static_cast<BreakStatement*>(st);
				ls_string_view label = st->kind == Statement::BREAK ? br->label : static_cast<ContinueStatement*>(st)->label;
				if (!checkLabelTarget(ctx, label)) {
					errorLine(br->token, "No matching loop to label ", label);
					return false;
				}
				return true;
			}
			case Statement::DEFER: {
				DeferStatement* df = static_cast<DeferStatement*>(st);
				++ctx.in_defer;
				bool ok = checkStatement(unit, ctx, df->statement, return_type, {});
				--ctx.in_defer;
				return ok;
			}
			case Statement::VAR_DECL: return checkVarDeclStatement(unit, ctx, static_cast<VarDeclStatement*>(st));
			case Statement::ASSIGN: return checkAssignStatement(unit, ctx, static_cast<AssignStatement*>(st));
			case Statement::IF: return checkIfStatement(unit, ctx, static_cast<IfStatement*>(st), return_type);
			case Statement::LABEL: return checkLabelStatement(unit, ctx, static_cast<LabelStatement*>(st), return_type);
			case Statement::MATCH: return checkMatchStatement(unit, ctx, static_cast<MatchStatement*>(st), return_type);
			default: return false;
		}
	}

	static inline const char builtin_math_source[] = R"(
		extern fn sin(v : f32) : f32;
		extern fn cos(v : f32) : f32;
		extern fn sqrt(v : f32) : f32;
		extern fn sin_f64(v : f64) : f64;
		extern fn cos_f64(v : f64) : f64;
		extern fn sqrt_f64(v : f64) : f64;
	)";

	bool resolveImportsForUnit(Unit& unit, ls_import_resolver_fn import_resolver, void* import_resolver_userdata, OutputFormatter& out) {
		if (unit.import_state == Unit::IMPORT_DONE) return LS_RESULT_OK;
		if (unit.import_state == Unit::IMPORT_RESOLVING) {
			errorLine({}, "Import cycle detected: ", unit.path);
			return false;
		}
		// This is the gray state of a depth-first traversal; reaching it again through
		// an import edge identifies a cycle, while IMPORT_DONE permits shared imports.
		unit.import_state = Unit::IMPORT_RESOLVING;

		// Check for duplicate aliases within this unit.
		for (i32 i = 0; i < unit.imports.size(); ++i) {
			const Import& a = unit.imports[i];
			for (i32 j = i + 1; j < unit.imports.size(); ++j) {
				const Import& b = unit.imports[j];
				if (!empty(a.alias) && equalStrings(a.alias, b.alias)) {
					errorLine({}, "Duplicate import alias: ", a.alias);
					return false;
				}
				if (equalStrings(a.path, b.path)) {
					errorLine({}, "Duplicate import: ", a.path);
					return false;
				}
			}
		}

		for (i32 i = 0; i < unit.imports.size(); ++i) {
			const Import& import = unit.imports[i];
			Unit* imported = findUnitByPath(import.path);
			if (!imported) {
				ls_string_view source = {};
				if (equalStrings(import.path, makeStringView("std:math"))) {
					source = makeStringView(builtin_math_source);
				} else {
					if (!import_resolver) {
						errorLine({}, "No import resolver for: ", import.path);
						return false;
					}
					if (!import_resolver(import_resolver_userdata, import.path, import.alias, &source)) {
						errorLine({}, "Import not found: ", import.path);
						return false;
					}
				}
				if (ls_module_parse(&module, source, import.path) == LS_RESULT_FAILURE) return false;
				imported = &module.units.back();
			}
			if (imported && !resolveImportsForUnit(*imported, import_resolver, import_resolver_userdata, out)) return false;
		}

		unit.import_state = Unit::IMPORT_DONE;
		return LS_RESULT_OK;
	}

	bool resolveImports(ls_import_resolver_fn import_resolver, void* import_resolver_userdata) {
		OutputFormatter out = {};
		if (!module.units.empty()) out.host = module.units[0].arena.host;
		for (u32 unit_index = 0; unit_index < module.units.size(); ++unit_index) {
			Unit& unit = module.units[unit_index];
			if (!resolveImportsForUnit(unit, import_resolver, import_resolver_userdata, out)) return false;
		}
		return true;
	}

	ls_result checkSymbol(Unit& unit, Symbol& sym) {
		if (sym.check_state == Symbol::CHECKED) return LS_RESULT_OK;
		if (sym.check_state == Symbol::FAILED) return LS_RESULT_FAILURE;

		OutputFormatter out = {};
		out.host = unit.arena.host;
		auto fail = [&]() -> ls_result {
			sym.check_state = Symbol::FAILED;
			return LS_RESULT_FAILURE;
		};

		if (sym.storage == Symbol::IMPORT) {
			sym.check_state = Symbol::CHECKED;
			return LS_RESULT_OK;
		}

		if (sym.storage == Symbol::COMPTIME && isPrimitiveShadowName(sym.name)) {
			errorLine(sym.token, "Can not shadow primitive type: ", sym.name);
			return fail();
		}

		if (sym.check_state == Symbol::CHECKING) {
			// Function literals need to be visible while their own body is being
			// checked so recursive calls can resolve to the provisional function type.
			if (sym.expression && sym.expression->kind == Expression::FUNCTION && sym.resolved_type) {
				return LS_RESULT_OK;
			}
			// Struct declarations publish their provisional instance before resolving
			// fields. This permits references through function signatures and other
			// non-inline wrappers; direct by-value recursion is rejected by the field
			// layout check after resolution.
			if (sym.expression && sym.expression->kind == Expression::STRUCT && sym.resolved_type && sym.resolved_type->kind == ResolvedType::META) {
				return LS_RESULT_OK;
			}
			errorLine(sym.token, "Cyclic definition: ", sym.name);
			return fail();
		}

		sym.check_state = Symbol::CHECKING;

		if (sym.storage == Symbol::COMPTIME && sym.expression) {
			switch (sym.expression->kind) {
				case Expression::FUNCTION: {
					FunctionExpression& fn = static_cast<FunctionExpression&>(*sym.expression);
					if (!fn.comptime_params.empty()) {
						errorLine(sym.token, "Templates are not supported: ", sym.name);
						return fail();
					}
					FunctionResolvedType* fn_type = buildFunctionType(unit, fn, true);
					if (!fn_type) return fail();
					sym.resolved_type = fn_type;
					if (const Token::Type op_token = tokenFromOperatorName(sym.name); op_token != Token::ERROR) {
						if (!operatorDeclArityMatches(op_token, (i32)fn_type->param_types.size())) {
							errorLine(sym.token, "Invalid operator arity");
							return fail();
						}
						if (operatorHasPrimitiveSignature(*fn_type)) {
							errorLine(sym.token, "Operator overloads for primitive signatures are not allowed");
							return fail();
						}
					}
					if (fn.body && checkFunctionBody(unit, fn) == false) return fail();
					break;
				}
				case Expression::STRUCT: {
					StructExpression& st = static_cast<StructExpression&>(*sym.expression);
					if (!st.comptime_params.empty()) {
						errorLine(sym.token, "Templates are not supported: ", sym.name);
						return fail();
					}
					StructResolvedType* st_type = makeType<StructResolvedType>(unit, *unit.arena.arena);
					st_type->decl = &st;
					MetaType* meta = makeType<MetaType>(unit);
					meta->inner = st_type;
					sym.resolved_type = meta;
					for (NamedDecl& field : st.fields) {
						field.resolved_type = resolveParsedType(unit, field.parsed_type);
						if (!field.resolved_type) {
							errorLine(sym.token, "Could not resolve type of field '", field.name, "' in struct: ", sym.name);
							return fail();
						}
						if (field.resolved_type == st_type) {
							errorLine(sym.token, "Recursive by-value field '", field.name, "' in struct: ", sym.name);
							return fail();
						}
						st_type->field_types.push(field.resolved_type);
					}
					break;
				}
				case Expression::ENUM: {
					EnumExpression& en = static_cast<EnumExpression&>(*sym.expression);
					EnumResolvedType* en_type = makeType<EnumResolvedType>(unit);
					en_type->decl = &en;
					MetaType* meta = makeType<MetaType>(unit);
					meta->inner = en_type;
					sym.resolved_type = meta;
					break;
				}
				default: {
					// Plain comptime value: comptime N = expr;
					ResolvedType* annotation = resolveParsedType(unit, sym.parsed_type);
					FunctionCheckContext comptime_ctx(unit.arena);
					comptime_ctx.comptime_only = true;
					ResolvedType* expr_type = checkExpr(unit, &comptime_ctx, *sym.expression, annotation);
					if (!expr_type) {
						errorLine(sym.token, "Unresolved initializer for: ", sym.name);
						return fail();
					}
					if (annotation && expr_type && !canImplicitlyConvert(expr_type, annotation)) {
						errorLine(sym.token, "Type mismatch in comptime declaration: ", sym.name);
						return fail();
					}
					if (sym.expression && sym.expression->kind == Expression::TYPE_LITERAL) {
						MetaType* meta = makeType<MetaType>(unit);
						meta->inner = expr_type;
						sym.resolved_type = meta;
					} else {
						sym.resolved_type = annotation ? annotation : expr_type;
					}
					break;
				}
			}
		} else {
			// VAR or CONST global (includes extern fn, which is VARIABLE + FunctionExpression).
			ResolvedType* annotation = resolveParsedType(unit, sym.parsed_type);

			if (sym.expression && sym.expression->kind == Expression::UNDEFINED) {
				if (!annotation) {
					errorLine(sym.token, "'undefined' initializer requires an explicit type annotation: ", sym.name);
					return fail();
				}
				if (sym.storage == Symbol::CONST) {
					errorLine(sym.token, "const cannot be initialized with 'undefined': ", sym.name);
					return fail();
				}
			}

			ResolvedType* expr_type = nullptr;
			if (sym.expression && sym.expression->kind == Expression::FUNCTION) {
				FunctionExpression& fn = static_cast<FunctionExpression&>(*sym.expression);
				FunctionResolvedType* fn_type = buildFunctionType(unit, fn, true);
				if (!fn_type) return fail();
				sym.resolved_type = fn_type;
				expr_type = fn_type;
				if (annotation && !canImplicitlyConvert(expr_type, annotation)) {
					errorLine(sym.token, "Type mismatch in initializer for: ", sym.name);
					return fail();
				}
				if (fn.body && checkFunctionBody(unit, fn) == false) return fail();
			} else {
				expr_type = sym.expression ? checkExpr(unit, nullptr, *sym.expression, annotation) : nullptr;
				if (sym.expression && !expr_type) {
					errorLine(sym.token, "Unresolved initializer for: ", sym.name);
					return fail();
				}
			}

			if (annotation && expr_type && !canImplicitlyConvert(expr_type, annotation)) {
				errorLine(sym.token, "Type mismatch in initializer for: ", sym.name);
				return fail();
			}

			sym.resolved_type = annotation ? annotation : expr_type;
		}

		sym.check_state = Symbol::CHECKED;
		return LS_RESULT_OK;
	}

}; // struct Checker

ls_result ls_module_typecheck(ls_module* module) {
	if (!module) return LS_RESULT_FAILURE;
	Checker checker(*module);
	for (Unit& unit : module->units) {
		for (Symbol& sym : unit.symbols) {
			if (checker.checkSymbol(unit, sym) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
		}
	}
	return LS_RESULT_OK;
}

ls_result ls_module_compile(ls_module* module, ls_string_view source, ls_string_view source_name, ls_import_resolver_fn import_resolver, void* import_resolver_userdata) {
	if (!module) return LS_RESULT_FAILURE;
	Checker checker(*module);
	if (ls_module_parse(module, source, source_name) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	if (checker.resolveImports(import_resolver, import_resolver_userdata) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	if (ls_module_typecheck(module) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	return LS_RESULT_OK;
}
