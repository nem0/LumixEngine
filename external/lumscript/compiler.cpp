#include "compiler.h"
#include "utils.h"
#include <float.h>

template <typename T, typename... Args>
static T* makeType(Unit& unit, Args&&... args) {
	ls_arena& arena = *unit.arena.arena;
	void* mem = arena.allocate(arena.user_data, sizeof(T), alignof(T));
	return ::new(mem) T(static_cast<Args&&>(args)...);
}

static ResolvedType* primitiveType(ls_module& module, ResolvedType::Kind kind) {
	ASSERT(kind >= ResolvedType::VOID && kind <= ResolvedType::TYPE);
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
		// COMPTIME_CALL: resolved away before comparison.
		default:
			return false;
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
		case ResolvedType::I8:  case ResolvedType::I16:
		case ResolvedType::I32: case ResolvedType::I64:
		case ResolvedType::U8:  case ResolvedType::U16:
		case ResolvedType::U32: case ResolvedType::U64:
			return true;
		default: return false;
	}
}

static bool isFloatType(const ResolvedType* t) {
	if (!t) return false;
	return t->kind == ResolvedType::F32 || t->kind == ResolvedType::F64;
}

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
	{}

	ExpArray<SemanticLocalBinding> locals;
	ExpArray<u32> scope_marks;
	ExpArray<ls_string_view> loop_labels;
	ExpArray<ls_string_view> label_names;
	bool comptime_only = false;
};

// Forward declarations.
static ls_result checkSymbol(ls_module& module, Unit& unit, Symbol& sym);
static ResolvedType* checkExpr(ls_module& module, Unit& unit, Expression* expr, ResolvedType* hint);
static Unit* findUnitByPath(ls_module& module, ls_string_view path);
static Unit* findImportedUnitByAlias(ls_module& module, Unit& unit, ls_string_view alias);
static Symbol* findSymbolInUnit(ls_module& module, Unit& unit, ls_string_view name);
static Symbol* findImportedSymbol(ls_module& module, Unit& unit, ls_string_view name);
static Symbol* findImportedQualifiedSymbol(ls_module& module, Unit& unit, ls_string_view qualifier, ls_string_view name);

static bool resolveComptimeIntValue(ls_module& module, Unit& unit, Expression* expr, i64& out) {
	if (!expr) return false;
	switch (expr->kind) {
		case Expression::INT_LITERAL:
			out = static_cast<IntLiteralExpression*>(expr)->value;
			return true;
		case Expression::IDENTIFIER: {
			IdentifierExpression* id = static_cast<IdentifierExpression*>(expr);
			Symbol* sym = findSymbolInUnit(module, unit, id->name);
			if (!sym) sym = findImportedSymbol(module, unit, id->name);
			if (!sym || sym->storage != Symbol::COMPTIME) return false;
			if (checkSymbol(module, unit, *sym) == LS_RESULT_FAILURE) return false;
			return resolveComptimeIntValue(module, unit, sym->expression, out);
		}
		case Expression::UNARY: {
			UnaryExpression* un = static_cast<UnaryExpression*>(expr);
			if (un->op != Token::MINUS) return false;
			if (!resolveComptimeIntValue(module, unit, un->expression, out)) return false;
			out = -out;
			return true;
		}
		default:
			return false;
	}
}

static ResolvedType* resolveParsedType(ls_module& module, Unit& unit, ParsedType* parsed) {
	if (!parsed) return nullptr;
	ResolvedType* result = nullptr;
	switch (parsed->kind) {
		case ParsedType::VOID:   result = primitiveType(module, ResolvedType::VOID);   break;
		case ParsedType::BOOL:   result = primitiveType(module, ResolvedType::BOOL);   break;
		case ParsedType::I8:     result = primitiveType(module, ResolvedType::I8);     break;
		case ParsedType::I16:    result = primitiveType(module, ResolvedType::I16);    break;
		case ParsedType::I32:    result = primitiveType(module, ResolvedType::I32);    break;
		case ParsedType::I64:    result = primitiveType(module, ResolvedType::I64);    break;
		case ParsedType::U8:     result = primitiveType(module, ResolvedType::U8);     break;
		case ParsedType::U16:    result = primitiveType(module, ResolvedType::U16);    break;
		case ParsedType::U32:    result = primitiveType(module, ResolvedType::U32);    break;
		case ParsedType::U64:    result = primitiveType(module, ResolvedType::U64);    break;
		case ParsedType::F32:    result = primitiveType(module, ResolvedType::F32);    break;
		case ParsedType::F64:    result = primitiveType(module, ResolvedType::F64);    break;
		case ParsedType::STRING: result = primitiveType(module, ResolvedType::STRING); break;
		case ParsedType::CPTR:   result = primitiveType(module, ResolvedType::CPTR);   break;
		case ParsedType::TYPE:   result = primitiveType(module, ResolvedType::TYPE);   break;
		case ParsedType::FUNCTION: {
			FunctionParsedType* fn = static_cast<FunctionParsedType*>(parsed);
			FunctionResolvedType* resolved = makeType<FunctionResolvedType>(unit, *unit.arena.arena);
			for (ParsedType* param : fn->params) {
				ResolvedType* pt = resolveParsedType(module, unit, param);
				if (!pt) return nullptr;
				resolved->param_types.push(pt);
			}
			resolved->return_type = resolveParsedType(module, unit, fn->return_type);
			result = resolved;
			break;
		}
		case ParsedType::ARRAY: {
			ArrayParsedType* arr = static_cast<ArrayParsedType*>(parsed);
			ArrayResolvedType* resolved = makeType<ArrayResolvedType>(unit);
			resolved->element_type = resolveParsedType(module, unit, arr->element_type);
			i64 size = 0;
			if (!resolved->element_type || !resolveComptimeIntValue(module, unit, arr->size, size)) return nullptr;
			if (size <= 0) return nullptr;
			resolved->size = size;
			result = resolved;
			break;
		}
		case ParsedType::SLICE: {
			SliceParsedType* sl = static_cast<SliceParsedType*>(parsed);
			SliceResolvedType* resolved = makeType<SliceResolvedType>(unit);
			resolved->element_type = resolveParsedType(module, unit, sl->element_type);
			result = resolved;
			break;
		}
		case ParsedType::QUALIFIED: {
			QualifiedParsedType* q = static_cast<QualifiedParsedType*>(parsed);
			ResolvedType* named = nullptr;
			if (!empty(q->qualifier)) {
				Symbol* sym = findImportedQualifiedSymbol(module, unit, q->qualifier, q->name);
				named = sym ? (sym->instance_type ? sym->instance_type : sym->resolved_type) : nullptr;
			}
			else if (Symbol* sym = findSymbolInUnit(module, unit, q->name)) {
				named = sym->instance_type ? sym->instance_type : sym->resolved_type;
			}
			else if (Symbol* sym = findImportedSymbol(module, unit, q->name)) {
				named = sym->instance_type ? sym->instance_type : sym->resolved_type;
			}
			result = named;
			break;
		}
		case ParsedType::COMPTIME_CALL: {
			ComptimeCallParsedType* call = static_cast<ComptimeCallParsedType*>(parsed);
			ResolvedType* callee = resolveParsedType(module, unit, call->callee);
			if (!callee) return nullptr;
			if (call->args.size() == 1 && call->args[0].kind == ComptimeArg::EXPRESSION) {
				ArrayResolvedType* resolved = makeType<ArrayResolvedType>(unit);
				resolved->element_type = callee;
				i64 size = 0;
				if (!resolveComptimeIntValue(module, unit, call->args[0].expression, size)) return nullptr;
				if (size <= 0) return nullptr;
				resolved->size = size;
				result = resolved;
				break;
			}
			result = nullptr; // template instantiation — not yet
			break;
		}
		default:
			return nullptr;
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
		case ResolvedType::F64:
			return true;
		default:
			return false;
	}
}

static bool intLiteralFitsType(i64 value, ResolvedType::Kind kind) {
	switch (kind) {
		case ResolvedType::I8:  return value >= -128 && value <= 127;
		case ResolvedType::U8:  return value >= 0 && value <= 255;
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

static bool isIntegralType(const ResolvedType* type) {
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
			return true;
		default:
			return false;
	}
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

static void pushScope(FunctionCheckContext& ctx) {
	ctx.scope_marks.push((u32)ctx.locals.size());
}

static void popScope(FunctionCheckContext& ctx) {
	if (ctx.scope_marks.empty()) return;
	const u32 mark = ctx.scope_marks.back();
	ctx.scope_marks.pop_back();
	while (ctx.locals.size() > mark) ctx.locals.pop_back();
}

static bool isFunctionLikeType(const ResolvedType* type) {
	return type && type->kind == ResolvedType::FUNCTION;
}

static ResolvedType* checkExprImpl(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression* expr, ResolvedType* hint);

static ResolvedType* checkAssignableExpr(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression* expr, bool& is_writable);

static bool checkFunctionBody(ls_module& module, Unit& unit, FunctionExpression* fn);

static bool checkCallArguments(ls_module& module, Unit& unit, FunctionCheckContext* ctx, FunctionResolvedType* fn_type, CallExpression* call, ResolvedType* receiver_type);

static ResolvedType* checkExpr(ls_module& module, Unit& unit, Expression* expr, ResolvedType* hint) {
	return checkExprImpl(module, unit, nullptr, expr, hint);
}

static ResolvedType* checkExprImpl(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression* expr, ResolvedType* hint) {
	if (!expr) return nullptr;
	switch (expr->kind) {
		case Expression::INT_LITERAL: {
			ResolvedType* int_hint = hint;
			if (int_hint && int_hint->kind == ResolvedType::NULLABLE)
				int_hint = static_cast<NullableResolvedType*>(int_hint)->inner;
			if (int_hint && isNumericType(int_hint)) {
				const i64 value = static_cast<IntLiteralExpression*>(expr)->value;
				if (!intLiteralFitsType(value, int_hint->kind)) return nullptr;
				expr->resolved_type = int_hint;
			} else {
				expr->resolved_type = primitiveType(module, ResolvedType::I32);
			}
			return expr->resolved_type;
		}
		case Expression::FLOAT_LITERAL: {
			ResolvedType* float_hint = hint;
			if (float_hint && float_hint->kind == ResolvedType::NULLABLE)
				float_hint = static_cast<NullableResolvedType*>(float_hint)->inner;
			if (float_hint && float_hint->kind == ResolvedType::F32) {
				const double value = static_cast<FloatLiteralExpression*>(expr)->value;
				if (value > (double)FLT_MAX || value < -(double)FLT_MAX) return nullptr;
				expr->resolved_type = float_hint;
			} else {
				expr->resolved_type = (float_hint && isFloatType(float_hint)) ? float_hint : primitiveType(module, ResolvedType::F64);
			}
			return expr->resolved_type;
		}
		case Expression::BOOL_LITERAL:
			expr->resolved_type = primitiveType(module, ResolvedType::BOOL);
			return expr->resolved_type;
		case Expression::STRING_LITERAL:
			expr->resolved_type = primitiveType(module, ResolvedType::STRING);
			return expr->resolved_type;
		case Expression::NULL_LITERAL:
			if (!hint || (hint->kind != ResolvedType::NULLABLE && hint->kind != ResolvedType::SLICE)) return nullptr;
			expr->resolved_type = hint;
			return expr->resolved_type;
		case Expression::UNDEFINED:
			expr->resolved_type = hint;
			return expr->resolved_type;
		case Expression::TYPE_LITERAL:
			return ctx && ctx->comptime_only ? primitiveType(module, static_cast<ResolvedType::Kind>(static_cast<TypeLiteralExpression*>(expr)->type)) : nullptr;
		case Expression::IDENTIFIER: {
			IdentifierExpression* id = static_cast<IdentifierExpression*>(expr);
			if (ctx) {
				if (SemanticLocalBinding* local = findLocal(*ctx, id->name)) {
					id->symbol = nullptr;
					expr->resolved_type = local->type;
					return expr->resolved_type;
				}
			}
			Symbol* sym = findSymbolInUnit(module, unit, id->name);
			if (!sym) sym = findImportedSymbol(module, unit, id->name);
			if (!sym) return nullptr;
			if (ctx && ctx->comptime_only && sym->storage != Symbol::COMPTIME) return nullptr;
			id->symbol = sym;
			expr->resolved_type = sym->instance_type ? sym->instance_type : sym->resolved_type;
			return expr->resolved_type;
		}
		case Expression::FUNCTION: {
			FunctionExpression* fn = static_cast<FunctionExpression*>(expr);
			if (fn->function_type) {
				expr->resolved_type = fn->function_type;
				return expr->resolved_type;
			}
			if (!fn->comptime_params.empty()) return nullptr;
			FunctionResolvedType* fn_type = makeType<FunctionResolvedType>(unit, *unit.arena.arena);
			fn_type->decl = fn;
			for (FunctionParam& param : fn->runtime_params) {
				param.resolved_type = resolveParsedType(module, unit, param.parsed_type);
				if (!param.resolved_type) return nullptr;
				if (param.is_ref && param.resolved_type->kind == ResolvedType::NULLABLE) return nullptr;
				fn_type->param_types.push(param.resolved_type);
			}
			fn_type->return_type = resolveParsedType(module, unit, fn->return_type);
			if (!fn_type->return_type) return nullptr;
			fn->function_type = fn_type;
			fn->resolved_type = fn_type;
			expr->resolved_type = fn_type;
			if (fn->body && fn->comptime_params.empty()) {
				if (!checkFunctionBody(module, unit, fn)) return nullptr;
			}
			return fn_type;
		}
		case Expression::CALL: {
			CallExpression* call = static_cast<CallExpression*>(expr);
			if (call->callee && call->callee->kind == Expression::IDENTIFIER) {
				IdentifierExpression* id = static_cast<IdentifierExpression*>(call->callee);
				if (equalStrings(id->name, makeStringView("length"))) {
					if (call->args.size() != 1u) return nullptr;
					ResolvedType* arg_type = checkExprImpl(module, unit, ctx, call->args[0], nullptr);
					if (!arg_type || (arg_type->kind != ResolvedType::ARRAY && arg_type->kind != ResolvedType::SLICE)) return nullptr;
					expr->resolved_type = primitiveType(module, ResolvedType::I32);
					return expr->resolved_type;
				}
			}
			if (call->callee && call->callee->kind == Expression::MEMBER) {
				MemberExpression* member = static_cast<MemberExpression*>(call->callee);
				if (!member->expression) break;
				if (member->expression->kind == Expression::IDENTIFIER) {
					IdentifierExpression* base_id = static_cast<IdentifierExpression*>(member->expression);
					if (Symbol* sym = findImportedQualifiedSymbol(module, unit, base_id->name, member->name)) {
						ResolvedType* sym_type = sym->instance_type ? sym->instance_type : sym->resolved_type;
						if (!sym_type) return nullptr;
						if (sym_type->kind == ResolvedType::FUNCTION) {
							FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(sym_type);
						if (!checkCallArguments(module, unit, ctx, fn_type, call, nullptr)) {
								return nullptr;
							}
							expr->resolved_type = fn_type->return_type;
							return expr->resolved_type;
						}
						expr->resolved_type = sym_type;
						return expr->resolved_type;
					}
				}
				ResolvedType* receiver_type = checkExprImpl(module, unit, ctx, member->expression, nullptr);
				if (!receiver_type) return nullptr;
				if (receiver_type->kind != ResolvedType::STRUCT && receiver_type->kind != ResolvedType::ENUM) return nullptr;

				FunctionResolvedType* found_fn = nullptr;
				ResolvedType* found_type = nullptr;
				auto tryUnit = [&](Unit& search_unit) -> bool {
					for (Symbol& sym : search_unit.symbols) {
						if (!equalStrings(sym.name, member->name)) continue;
						if (ctx && ctx->comptime_only && sym.storage != Symbol::COMPTIME) continue;
						if (sym.check_state == Symbol::CHECKING && sym.resolved_type) {
							ResolvedType* sym_type = sym.instance_type ? sym.instance_type : sym.resolved_type;
							if (!sym_type || sym_type->kind != ResolvedType::FUNCTION) continue;
							FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(sym_type);
							if (!checkCallArguments(module, search_unit, ctx, fn_type, call, receiver_type)) continue;
							if (found_fn && found_fn != fn_type) return false;
							found_fn = fn_type;
							found_type = fn_type->return_type;
							continue;
						}
						if (checkSymbol(module, search_unit, sym) == LS_RESULT_FAILURE) return false;
						ResolvedType* sym_type = sym.instance_type ? sym.instance_type : sym.resolved_type;
						if (!sym_type || sym_type->kind != ResolvedType::FUNCTION) continue;
						FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(sym_type);
						if (!checkCallArguments(module, search_unit, ctx, fn_type, call, receiver_type)) continue;
						if (found_fn && found_fn != fn_type) return false;
						found_fn = fn_type;
						found_type = fn_type->return_type;
					}
					return true;
				};

				if (!tryUnit(unit)) return nullptr;
				for (const Import& import : unit.imports) {
					Unit* imported = findUnitByPath(module, import.path);
					if (!imported) continue;
					if (!tryUnit(*imported)) return nullptr;
				}
				if (found_type) {
					expr->resolved_type = found_type;
					return found_type;
				}
				// No free function found; fall through to first-class function field call.
			}

			ResolvedType* callee_type = checkExprImpl(module, unit, ctx, call->callee, nullptr);
			if (!callee_type && call->callee && call->callee->kind == Expression::IDENTIFIER && !call->args.empty()) {
				IdentifierExpression* id = static_cast<IdentifierExpression*>(call->callee);
				ResolvedType* first_arg_type = checkExprImpl(module, unit, ctx, call->args[0], nullptr);
				Symbol* found = nullptr;
				FunctionResolvedType* found_type = nullptr;
				for (const Import& import : unit.imports) {
					Unit* imported = findUnitByPath(module, import.path);
					if (!imported) continue;
					for (Symbol& sym : imported->symbols) {
						if (!equalStrings(sym.name, id->name)) continue;
						if (checkSymbol(module, *imported, sym) == LS_RESULT_FAILURE) return nullptr;
						ResolvedType* sym_type = sym.instance_type ? sym.instance_type : sym.resolved_type;
						if (!sym_type || sym_type->kind != ResolvedType::FUNCTION) continue;
						FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(sym_type);
						if (fn_type->param_types.empty() || !canImplicitlyConvert(first_arg_type, fn_type->param_types[0])) continue;
						if (!checkCallArguments(module, *imported, ctx, fn_type, call, nullptr)) continue;
						if (found && found != &sym) return nullptr;
						found = &sym;
						found_type = fn_type;
					}
				}
				if (found) {
					id->symbol = found;
					call->callee->resolved_type = found_type;
					expr->resolved_type = found_type->return_type;
					return expr->resolved_type;
				}
			}
			if (!callee_type || callee_type->kind != ResolvedType::FUNCTION) return nullptr;
			FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(callee_type);
			if (!checkCallArguments(module, unit, ctx, fn_type, call, nullptr)) return nullptr;
			expr->resolved_type = fn_type->return_type;
			return expr->resolved_type;
		}
		case Expression::UNARY: {
			UnaryExpression* un = static_cast<UnaryExpression*>(expr);
			if (un->op == Token::MINUS && un->expression && un->expression->kind == Expression::INT_LITERAL) {
				// Range-check the negated value against the expected type.
				IntLiteralExpression* lit = static_cast<IntLiteralExpression*>(un->expression);
				ResolvedType* int_hint = hint;
				if (int_hint && int_hint->kind == ResolvedType::NULLABLE)
					int_hint = static_cast<NullableResolvedType*>(int_hint)->inner;
				const i64 negated = -lit->value;
				if (int_hint && isNumericType(int_hint)) {
					if (!intLiteralFitsType(negated, int_hint->kind)) return nullptr;
					lit->resolved_type = int_hint;
					expr->resolved_type = int_hint;
					return int_hint;
				}
				lit->resolved_type = primitiveType(module, ResolvedType::I32);
				expr->resolved_type = lit->resolved_type;
				return expr->resolved_type;
			}
			ResolvedType* inner = checkExprImpl(module, unit, ctx, un->expression, hint);
			if (!inner) return nullptr;
			switch (un->op) {
				case Token::MINUS: {
					if (!isNumericType(inner)) return nullptr;
					const ResolvedType::Kind k = inner->kind;
					if (k == ResolvedType::U8 || k == ResolvedType::U16 || k == ResolvedType::U32 || k == ResolvedType::U64) return nullptr;
					expr->resolved_type = inner;
					return inner;
				}
				case Token::NOT:
					if (!typesEqual(inner, primitiveType(module, ResolvedType::BOOL))) return nullptr;
					expr->resolved_type = primitiveType(module, ResolvedType::BOOL);
					return expr->resolved_type;
				case Token::REF:
					return nullptr;
				default:
					return nullptr;
			}
		}
		case Expression::BINARY: {
			BinaryExpression* bin = static_cast<BinaryExpression*>(expr);
			ResolvedType* lhs = checkExprImpl(module, unit, ctx, bin->lhs, hint);
			ResolvedType* rhs = checkExprImpl(module, unit, ctx, bin->rhs, lhs ? lhs : hint);
			if (!lhs || !rhs) return nullptr;
			switch (bin->op) {
				case Token::PLUS:
				case Token::MINUS:
				case Token::STAR:
				case Token::SLASH:
					if (!isNumericType(lhs) || !isNumericType(rhs) || !typesEqual(lhs, rhs)) return nullptr;
					expr->resolved_type = lhs;
					return lhs;
				case Token::PERCENT:
					if (!isIntegralType(lhs) || !isIntegralType(rhs) || !typesEqual(lhs, rhs)) return nullptr;
					expr->resolved_type = lhs;
					return lhs;
				case Token::EQUAL_EQUAL:
				case Token::BANG_EQUAL:
					if (!typesEqual(lhs, rhs) && !(isNumericType(lhs) && isNumericType(rhs))) return nullptr;
					expr->resolved_type = primitiveType(module, ResolvedType::BOOL);
					return expr->resolved_type;
				case Token::LT:
				case Token::LT_EQUAL:
				case Token::GT:
				case Token::GT_EQUAL:
					if (!isNumericType(lhs) || !isNumericType(rhs) || !typesEqual(lhs, rhs)) return nullptr;
					expr->resolved_type = primitiveType(module, ResolvedType::BOOL);
					return expr->resolved_type;
				case Token::AND:
				case Token::OR:
					if (!typesEqual(lhs, primitiveType(module, ResolvedType::BOOL)) || !typesEqual(rhs, primitiveType(module, ResolvedType::BOOL))) return nullptr;
					expr->resolved_type = primitiveType(module, ResolvedType::BOOL);
					return expr->resolved_type;
				default:
					return nullptr;
			}
		}
		case Expression::CAST: {
			CastExpression* cast = static_cast<CastExpression*>(expr);
			ResolvedType* dst_type = resolveParsedType(module, unit, cast->parsed_type);
			if (!dst_type) return nullptr;
			// Don't pass dst_type as hint: explicit casts allow out-of-range values and
			// the operand resolves independently (e.g. `-1 as u8` should work).
			ResolvedType* src_type = checkExprImpl(module, unit, ctx, cast->expression, nullptr);
			if (!src_type) return nullptr;
			const bool src_numeric = isNumericType(src_type);
			const bool dst_numeric = isNumericType(dst_type);
			const bool src_bool = typesEqual(src_type, primitiveType(module, ResolvedType::BOOL));
			const bool dst_bool = typesEqual(dst_type, primitiveType(module, ResolvedType::BOOL));
			const bool src_enum = src_type->kind == ResolvedType::ENUM;
			const bool dst_enum = dst_type->kind == ResolvedType::ENUM;
			if (!(src_numeric && dst_numeric) && !(src_bool && dst_bool) && !(src_enum && dst_numeric) && !(src_numeric && dst_enum) && !(src_bool && dst_numeric) && !(src_numeric && dst_bool) && !typesEqual(src_type, dst_type)) {
				return nullptr;
			}
			expr->resolved_type = dst_type;
			return dst_type;
		}
		case Expression::MEMBER: {
			MemberExpression* member = static_cast<MemberExpression*>(expr);
			if (member->expression) {
				if (member->expression->kind == Expression::IDENTIFIER) {
					IdentifierExpression* base_id = static_cast<IdentifierExpression*>(member->expression);
					if (Symbol* sym = findImportedQualifiedSymbol(module, unit, base_id->name, member->name)) {
						expr->resolved_type = sym->instance_type ? sym->instance_type : sym->resolved_type;
						return expr->resolved_type;
					}
				}
				ResolvedType* base_type = checkExprImpl(module, unit, ctx, member->expression, nullptr);
				if (!base_type) return nullptr;
				if (base_type->kind == ResolvedType::STRUCT) {
					StructResolvedType* st = static_cast<StructResolvedType*>(base_type);
					for (NamedDecl& field : st->decl->fields) {
						if (!equalStrings(field.name, member->name)) continue;
						expr->resolved_type = field.resolved_type;
						return expr->resolved_type;
					}
				}
				else if (base_type->kind == ResolvedType::ENUM) {
					EnumResolvedType* en = static_cast<EnumResolvedType*>(base_type);
					for (EnumMember& em : en->decl->members) {
						if (!equalStrings(em.name, member->name)) continue;
						expr->resolved_type = base_type;
						return expr->resolved_type;
					}
				}
				return nullptr;
			}

			if (hint && hint->kind == ResolvedType::ENUM) {
				EnumResolvedType* en = static_cast<EnumResolvedType*>(hint);
				for (EnumMember& em : en->decl->members) {
					if (!equalStrings(em.name, member->name)) continue;
					expr->resolved_type = hint;
					return hint;
				}
			}

			ResolvedType* found_type = nullptr;
			for (Symbol& sym : unit.symbols) {
				if (ctx && ctx->comptime_only && sym.storage != Symbol::COMPTIME) continue;
				if (!sym.instance_type || sym.instance_type->kind != ResolvedType::ENUM) continue;
				EnumResolvedType* en = static_cast<EnumResolvedType*>(sym.instance_type);
				for (EnumMember& em : en->decl->members) {
					if (!equalStrings(em.name, member->name)) continue;
					if (found_type && found_type != sym.instance_type) return nullptr;
					found_type = sym.instance_type;
				}
			}
			expr->resolved_type = found_type;
			return found_type;
		}
		case Expression::BRACKET: {
			BracketExpression* br = static_cast<BracketExpression*>(expr);
			ResolvedType* base_type = checkExprImpl(module, unit, ctx, br->base, nullptr);
			if (!base_type) return nullptr;
			if (base_type->kind == ResolvedType::NULLABLE) return nullptr;
			if (base_type->kind != ResolvedType::ARRAY && base_type->kind != ResolvedType::SLICE) return nullptr;
			if (br->has_colon) {
				for (Expression* arg : br->args) {
					ResolvedType* arg_type = checkExprImpl(module, unit, ctx, arg, primitiveType(module, ResolvedType::I32));
					if (!arg_type || !isIntegralType(arg_type)) return nullptr;
				}
				if (br->end) {
					ResolvedType* end_type = checkExprImpl(module, unit, ctx, br->end, primitiveType(module, ResolvedType::I32));
					if (!end_type || !isIntegralType(end_type)) return nullptr;
				}
				const ArrayResolvedType* arr = base_type->kind == ResolvedType::ARRAY ? static_cast<const ArrayResolvedType*>(base_type) : nullptr;
				i64 begin = 0;
				i64 end = arr ? arr->size : 0;
				const bool has_begin = !br->args.empty() && resolveComptimeIntValue(module, unit, br->args[0], begin);
				const bool has_end = br->end ? resolveComptimeIntValue(module, unit, br->end, end) : !!arr;
				if (arr) {
					if (has_begin && (begin < 0 || begin > arr->size)) return nullptr;
					if (has_end && (end < 0 || end > arr->size)) return nullptr;
					if (has_begin && has_end && begin > end) return nullptr;
				}
				SliceResolvedType* slice = makeType<SliceResolvedType>(unit);
				slice->element_type = base_type->kind == ResolvedType::ARRAY ? static_cast<ArrayResolvedType*>(base_type)->element_type : static_cast<SliceResolvedType*>(base_type)->element_type;
				expr->resolved_type = slice;
				return slice;
			}
			if (br->args.size() != 1) return nullptr;
			ResolvedType* index_type = checkExprImpl(module, unit, ctx, br->args[0], primitiveType(module, ResolvedType::I32));
			if (!index_type || !isIntegralType(index_type)) return nullptr;
			if (base_type->kind == ResolvedType::ARRAY) {
				const ArrayResolvedType* arr = static_cast<const ArrayResolvedType*>(base_type);
				i64 index = 0;
				if (resolveComptimeIntValue(module, unit, br->args[0], index)) {
					if (index < 0 || index >= arr->size) return nullptr;
				}
				expr->resolved_type = arr->element_type;
			} else {
				expr->resolved_type = static_cast<SliceResolvedType*>(base_type)->element_type;
			}
			return expr->resolved_type;
		}
		case Expression::STRUCT_LITERAL: {
			StructLiteralExpression* lit = static_cast<StructLiteralExpression*>(expr);
			ResolvedType* type = checkExprImpl(module, unit, ctx, lit->type, hint);
			if (!type) type = hint;
			if (!type || type->kind != ResolvedType::STRUCT) return nullptr;
			StructResolvedType* st = static_cast<StructResolvedType*>(type);
			if (!st->decl || st->decl->fields.size() != lit->values.size()) return nullptr;
			for (i32 i = 0; i < lit->values.size(); ++i) {
				NamedDecl& field = st->decl->fields[i];
				ResolvedType* value_type = checkExprImpl(module, unit, ctx, lit->values[i], field.resolved_type);
				if (!value_type || !typesEqual(value_type, field.resolved_type)) return nullptr;
			}
			expr->resolved_type = type;
			return type;
		}
		default:
			return nullptr;
	}

	return nullptr;
}

static ResolvedType* checkAssignableExpr(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression* expr, bool& is_writable) {
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
			for (Symbol& sym : unit.symbols) {
				if (!equalStrings(sym.name, id->name)) continue;
				if (sym.check_state == Symbol::CHECKING && sym.resolved_type) {
					is_writable = sym.storage == Symbol::VARIABLE;
					return sym.instance_type ? sym.instance_type : sym.resolved_type;
				}
				if (checkSymbol(module, unit, sym) == LS_RESULT_FAILURE) {
					is_writable = false;
					return nullptr;
				}
				is_writable = sym.storage == Symbol::VARIABLE;
				return sym.instance_type ? sym.instance_type : sym.resolved_type;
			}
			is_writable = false;
			return nullptr;
		}
		case Expression::MEMBER: {
			MemberExpression* member = static_cast<MemberExpression*>(expr);
			if (!member->expression) {
				is_writable = false;
				return nullptr;
			}
			bool base_writable = false;
			ResolvedType* base_type = checkAssignableExpr(module, unit, ctx, member->expression, base_writable);
			if (!base_type) {
				is_writable = false;
				return nullptr;
			}
			if (!base_writable) {
				is_writable = false;
				return nullptr;
			}
			ResolvedType* field_type = checkExprImpl(module, unit, ctx, expr, nullptr);
			is_writable = field_type != nullptr;
			return field_type;
		}
		case Expression::BRACKET: {
			BracketExpression* br = static_cast<BracketExpression*>(expr);
			bool base_writable = false;
			ResolvedType* base_type = checkAssignableExpr(module, unit, ctx, br->base, base_writable);
			if (!base_type || !base_writable) {
				is_writable = false;
				return nullptr;
			}
			ResolvedType* value_type = checkExprImpl(module, unit, ctx, expr, nullptr);
			is_writable = value_type != nullptr;
			return value_type;
		}
		default:
			is_writable = false;
			return nullptr;
	}
}

static bool checkCallArguments(ls_module& module, Unit& unit, FunctionCheckContext* ctx, FunctionResolvedType* fn_type, CallExpression* call, ResolvedType* receiver_type) {
	if (!fn_type) return false;

	const i32 arg_offset = receiver_type ? 1 : 0;
	if (fn_type->param_types.size() != call->args.size() + arg_offset) return false;

	if (receiver_type) {
		if (!canImplicitlyConvert(receiver_type, fn_type->param_types[0])) return false;
	}

	for (i32 i = 0; i < call->args.size(); ++i) {
		const i32 param_index = i + arg_offset;
		ResolvedType* param_type = fn_type->param_types[(u32)param_index];
		Expression* arg = call->args[(u32)i];

		if (fn_type->decl && fn_type->decl->runtime_params.size() > (u32)param_index && fn_type->decl->runtime_params[(u32)param_index].is_ref) {
			if (!arg || arg->kind != Expression::UNARY) return false;
			UnaryExpression* un = static_cast<UnaryExpression*>(arg);
			if (un->op != Token::REF) return false;
			bool writable = false;
			ResolvedType* arg_type = checkAssignableExpr(module, unit, ctx, un->expression, writable);
			if (!arg_type || !writable || !canImplicitlyConvert(arg_type, param_type)) return false;
			continue;
		}

		if (arg && arg->kind == Expression::UNARY) {
			UnaryExpression* un = static_cast<UnaryExpression*>(arg);
			if (un->op == Token::REF) return false;
		}
		ResolvedType* arg_type = checkExprImpl(module, unit, ctx, arg, param_type);
		if (!arg_type || !canImplicitlyConvert(arg_type, param_type)) return false;
	}

	return true;
}

static bool isPrimitiveShadowName(ls_string_view name) {
	static const char* names[] = {
		"void", "bool", "i8", "i16", "i32", "i64",
		"u8", "u16", "u32", "u64", "f32", "f64",
		"string", "cptr", "type"
	};
	for (const char* primitive : names) {
		if (equalStrings(name, makeStringView(primitive))) return true;
	}
	return false;
}

static Unit* findUnitByPath(ls_module& module, ls_string_view path) {
	for (Unit& unit : module.units) {
		if (equalStrings(unit.path, path)) return &unit;
	}
	return nullptr;
}

static Unit* findImportedUnitByAlias(ls_module& module, Unit& unit, ls_string_view alias) {
	for (const Import& import : unit.imports) {
		if (!equalStrings(import.alias, alias)) continue;
		return findUnitByPath(module, import.path);
	}
	return nullptr;
}

static Symbol* findSymbolInUnit(ls_module& module, Unit& unit, ls_string_view name) {
	for (Symbol& sym : unit.symbols) {
		if (!equalStrings(sym.name, name)) continue;
		if (checkSymbol(module, unit, sym) == LS_RESULT_FAILURE) return nullptr;
		return &sym;
	}
	return nullptr;
}

static Symbol* findImportedSymbol(ls_module& module, Unit& unit, ls_string_view name) {
	Symbol* found = nullptr;
	for (const Import& import : unit.imports) {
		if (!empty(import.alias)) continue;
		Unit* imported = findUnitByPath(module, import.path);
		if (!imported) continue;
		for (Symbol& sym : imported->symbols) {
			if (!equalStrings(sym.name, name)) continue;
			if (checkSymbol(module, *imported, sym) == LS_RESULT_FAILURE) return nullptr;
			if (found && found != &sym) return nullptr;
			found = &sym;
		}
	}
	return found;
}

static Symbol* findImportedQualifiedSymbol(ls_module& module, Unit& unit, ls_string_view qualifier, ls_string_view name) {
	Unit* imported = findImportedUnitByAlias(module, unit, qualifier);
	if (!imported) return nullptr;
	return findSymbolInUnit(module, *imported, name);
}

static bool checkStatement(ls_module& module, Unit& unit, FunctionCheckContext& ctx, Statement* st, ResolvedType* return_type, ls_string_view pending_label);

static bool checkFunctionBody(ls_module& module, Unit& unit, FunctionExpression* fn) {
	if (!fn || !fn->body) return true;
	if (fn->body->kind != Statement::BLOCK) return false;

	ResolvedType* return_type = fn->function_type ? static_cast<FunctionResolvedType*>(fn->function_type)->return_type : nullptr;
	FunctionCheckContext ctx(*unit.arena.arena);
	pushScope(ctx);
	for (FunctionParam& param : fn->runtime_params) {
		SemanticLocalBinding& binding = ctx.locals.emplace_back();
		binding.name = param.name;
		binding.type = param.resolved_type;
		binding.is_immutable = false;
	}

	BlockStatement* body = static_cast<BlockStatement*>(fn->body);
	for (Statement* st : body->statements) {
		if (!checkStatement(module, unit, ctx, st, return_type, {})) return false;
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

static bool checkStatement(ls_module& module, Unit& unit, FunctionCheckContext& ctx, Statement* st, ResolvedType* return_type, ls_string_view pending_label) {
	if (!st) return true;

	switch (st->kind) {
		case Statement::BLOCK: {
			BlockStatement* block = static_cast<BlockStatement*>(st);
			pushScope(ctx);
			for (Statement* child : block->statements) {
				if (!checkStatement(module, unit, ctx, child, return_type, {})) {
					popScope(ctx);
					return false;
				}
			}
			popScope(ctx);
			return true;
		}
		case Statement::EXPRESSION: {
			ExpressionStatement* expr = static_cast<ExpressionStatement*>(st);
			return checkExprImpl(module, unit, &ctx, expr->expression, nullptr) != nullptr;
		}
		case Statement::RETURN: {
			ReturnStatement* ret = static_cast<ReturnStatement*>(st);
			if (!return_type) return false;
			if (return_type->kind == ResolvedType::VOID) {
				return ret->expression == nullptr;
			}
			if (!ret->expression) return false;
			ResolvedType* expr_type = checkExprImpl(module, unit, &ctx, ret->expression, return_type);
			return expr_type && canImplicitlyConvert(expr_type, return_type);
		}
		case Statement::VAR_DECL: {
			VarDeclStatement* var = static_cast<VarDeclStatement*>(st);
			if (inCurrentScope(ctx, var->name)) return false;
			for (Symbol& sym : unit.symbols) {
				if (!equalStrings(sym.name, var->name)) continue;
				if (sym.storage == Symbol::COMPTIME && sym.expression) {
					switch (sym.expression->kind) {
						case Expression::FUNCTION:
						case Expression::STRUCT:
						case Expression::ENUM:
							break;
						default:
							return false;
					}
				}
			}

			ResolvedType* annotation = resolveParsedType(module, unit, var->parsed_type);
			if (var->expression && var->expression->kind == Expression::UNDEFINED) {
				if (!annotation || var->is_immutable) return false;
			}
			ResolvedType* expr_type = var->expression ? checkExprImpl(module, unit, &ctx, var->expression, annotation) : nullptr;
			if (var->expression && !expr_type) return false;
			if (annotation && expr_type && !canImplicitlyConvert(expr_type, annotation)) return false;
			ResolvedType* final_type = annotation ? annotation : expr_type;
			if (!final_type) return false;
			// Preserve the destination type: bytecode lowering needs it to materialize
			// implicit conversions such as an array initializer becoming a slice.
			var->resolved_type = final_type;

			SemanticLocalBinding& binding = ctx.locals.emplace_back();
			binding.name = var->name;
			binding.type = final_type;
			binding.is_immutable = var->is_immutable;
			return true;
		}
		case Statement::ASSIGN: {
			AssignStatement* assign = static_cast<AssignStatement*>(st);
			bool writable = false;
			ResolvedType* lhs_type = checkAssignableExpr(module, unit, &ctx, assign->lhs, writable);
			if (!lhs_type || !writable) return false;
			ResolvedType* rhs_type = checkExprImpl(module, unit, &ctx, assign->rhs, lhs_type);
			if (!rhs_type || !canImplicitlyConvert(rhs_type, lhs_type)) return false;
			switch (assign->op) {
				case Token::EQUAL:
					return true;
				case Token::PLUS_EQUAL:
				case Token::MINUS_EQUAL:
				case Token::STAR_EQUAL:
				case Token::SLASH_EQUAL:
					return isNumericType(lhs_type);
				default:
					return false;
			}
		}
		case Statement::IF: {
			IfStatement* ifst = static_cast<IfStatement*>(st);
			ResolvedType* cond = checkExprImpl(module, unit, &ctx, ifst->condition, primitiveType(module, ResolvedType::BOOL));
			if (!cond || !typesEqual(cond, primitiveType(module, ResolvedType::BOOL))) return false;

			// Detect `x != null` / `x == null` to narrow x inside the respective branch.
			ls_string_view narrowed_name = {};
			ResolvedType* narrowed_type = nullptr;
			bool narrow_in_true = false;
			if (ifst->condition && ifst->condition->kind == Expression::BINARY) {
				BinaryExpression* bin = static_cast<BinaryExpression*>(ifst->condition);
				if (bin->op == Token::BANG_EQUAL || bin->op == Token::EQUAL_EQUAL) {
					Expression* id_side = nullptr;
					if (bin->rhs && bin->rhs->kind == Expression::NULL_LITERAL) id_side = bin->lhs;
					else if (bin->lhs && bin->lhs->kind == Expression::NULL_LITERAL) id_side = bin->rhs;
					if (id_side && id_side->kind == Expression::IDENTIFIER) {
						ResolvedType* id_type = id_side->resolved_type;
						if (id_type && id_type->kind == ResolvedType::NULLABLE) {
							narrowed_name = static_cast<IdentifierExpression*>(id_side)->name;
							narrowed_type = static_cast<NullableResolvedType*>(id_type)->inner;
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
					nb.is_immutable = false;
					bool ok = checkStatement(module, unit, ctx, branch, return_type, {});
					popScope(ctx);
					return ok;
				}
				return checkStatement(module, unit, ctx, branch, return_type, {});
			};

			if (!checkBranchWithNarrowing(ifst->body, narrow_in_true)) return false;
			if (!checkBranchWithNarrowing(ifst->else_branch, !narrow_in_true)) return false;
			return true;
		}
		case Statement::WHILE: {
			WhileStatement* ws = static_cast<WhileStatement*>(st);
			ResolvedType* cond = checkExprImpl(module, unit, &ctx, ws->condition, primitiveType(module, ResolvedType::BOOL));
			if (!cond || !typesEqual(cond, primitiveType(module, ResolvedType::BOOL))) return false;
			ctx.loop_labels.push(pending_label);
			bool ok = checkStatement(module, unit, ctx, ws->body, return_type, {});
			ctx.loop_labels.pop_back();
			return ok;
		}
		case Statement::FOR: {
			ForStatement* fs = static_cast<ForStatement*>(st);
			ResolvedType* begin_type = checkExprImpl(module, unit, &ctx, fs->begin, primitiveType(module, ResolvedType::I32));
			ResolvedType* end_type = checkExprImpl(module, unit, &ctx, fs->end, begin_type ? begin_type : primitiveType(module, ResolvedType::I32));
					if (!begin_type || !end_type || !isIntegralType(begin_type) || !isIntegralType(end_type)) return false;
			if (!typesEqual(begin_type, end_type)) return false;

			pushScope(ctx);
			SemanticLocalBinding& binding = ctx.locals.emplace_back();
			binding.name = fs->loop_var;
			binding.type = begin_type;
			binding.is_immutable = true;
			ctx.loop_labels.push(pending_label);
			bool ok = checkStatement(module, unit, ctx, fs->body, return_type, {});
			ctx.loop_labels.pop_back();
			popScope(ctx);
			return ok;
		}
		case Statement::BREAK:
		case Statement::CONTINUE: {
			BreakStatement* br = static_cast<BreakStatement*>(st);
			return checkLabelTarget(ctx, st->kind == Statement::BREAK ? br->label : static_cast<ContinueStatement*>(st)->label);
		}
		case Statement::DEFER: {
			DeferStatement* df = static_cast<DeferStatement*>(st);
			if (!df->statement || df->statement->kind == Statement::RETURN) return false;
			return checkStatement(module, unit, ctx, df->statement, return_type, {});
		}
		case Statement::LABEL: {
			LabelStatement* label = static_cast<LabelStatement*>(st);
			for (i32 i = (i32)ctx.label_names.size() - 1; i >= 0; --i) {
				if (equalStrings(ctx.label_names[(u32)i], label->name)) return false;
			}
			ctx.label_names.push(label->name);
			const bool labeled_loop = label->statement && (label->statement->kind == Statement::WHILE || label->statement->kind == Statement::FOR);
			const bool ok = checkStatement(module, unit, ctx, label->statement, return_type, labeled_loop ? label->name : ls_string_view{});
			ctx.label_names.pop_back();
			return ok;
		}
		case Statement::MATCH: {
			MatchStatement* ms = static_cast<MatchStatement*>(st);
			ResolvedType* subject = checkExprImpl(module, unit, &ctx, ms->subject, nullptr);
			if (!subject) return false;

			// Subject must be a scalar numeric type, enum, or string.
			const bool subject_is_numeric = isNumericType(subject);
			const bool subject_is_enum = subject->kind == ResolvedType::ENUM;
			const bool subject_is_string = subject->kind == ResolvedType::STRING;
			if (!subject_is_numeric && !subject_is_enum && !subject_is_string) return false;

			bool has_fallback = false;
			// Track covered enum members for exhaustiveness checking.
			const EnumResolvedType* subject_enum = subject_is_enum ? static_cast<const EnumResolvedType*>(subject) : nullptr;
			u32 covered_enum_mask = 0;

			for (MatchArm& arm : ms->arms) {
				if (arm.is_fallback) {
					if (has_fallback) return false;
					has_fallback = true;
				}
				for (MatchPattern& pattern : arm.patterns) {
					ResolvedType* begin = checkExprImpl(module, unit, &ctx, pattern.begin, subject);
					if (!begin || !typesEqual(begin, subject)) return false;
					if (pattern.end) {
						// Range patterns are only valid for numeric types.
						if (!subject_is_numeric) return false;
						ResolvedType* end = checkExprImpl(module, unit, &ctx, pattern.end, subject);
						if (!end || !typesEqual(end, subject)) return false;
					}
					// Track enum coverage and detect duplicates.
					if (subject_enum && pattern.begin && pattern.begin->kind == Expression::MEMBER) {
						MemberExpression* mem = static_cast<MemberExpression*>(pattern.begin);
						for (u32 i = 0; i < (u32)subject_enum->decl->members.size(); ++i) {
							if (!equalStrings(subject_enum->decl->members[i].name, mem->name)) continue;
							const u32 bit = 1u << i;
							if (covered_enum_mask & bit) return false; // duplicate
							covered_enum_mask |= bit;
							break;
						}
					}
				}
				if (!checkStatement(module, unit, ctx, arm.body, return_type, {})) return false;
			}

			// Enum match must cover all variants or have a fallback.
			if (subject_enum && !has_fallback) {
				const u32 all_bits = (1u << (u32)subject_enum->decl->members.size()) - 1u;
				if (covered_enum_mask != all_bits) return false;
			}

			return true;
		}
		default:
			return false;
	}
}

static const char builtin_math_source[] = R"(
	extern fn sin(v : f32) : f32;
	extern fn cos(v : f32) : f32;
	extern fn sqrt(v : f32) : f32;
	extern fn sin_f64(v : f64) : f64;
	extern fn cos_f64(v : f64) : f64;
	extern fn sqrt_f64(v : f64) : f64;
)";

static ls_result resolveImportsForUnit(
	ls_module* module,
	Unit& unit,
	ls_import_resolver_fn import_resolver,
	void* import_resolver_userdata,
	OutputFormatter& out
) {
	if (unit.import_state == Unit::IMPORT_DONE) return LS_RESULT_OK;
	if (unit.import_state == Unit::IMPORT_RESOLVING) {
		out.error("Import cycle detected: ", unit.path);
		return LS_RESULT_FAILURE;
	}
	unit.import_state = Unit::IMPORT_RESOLVING;

	// Check for duplicate aliases within this unit.
	for (i32 i = 0; i < unit.imports.size(); ++i) {
		const Import& a = unit.imports[i];
		for (i32 j = i + 1; j < unit.imports.size(); ++j) {
			const Import& b = unit.imports[j];
			if (!empty(a.alias) && equalStrings(a.alias, b.alias)) {
				out.error("Duplicate import alias: ", a.alias);
				return LS_RESULT_FAILURE;
			}
			if (equalStrings(a.path, b.path)) {
				out.error("Duplicate import: ", a.path);
				return LS_RESULT_FAILURE;
			}
		}
	}

	for (i32 i = 0; i < unit.imports.size(); ++i) {
		const Import& import = unit.imports[i];
		Unit* imported = findUnitByPath(*module, import.path);
		if (!imported) {
			ls_string_view source = {};
			if (equalStrings(import.path, makeStringView("std:math"))) {
				source = makeStringView(builtin_math_source);
			}
			else {
				if (!import_resolver) {
					out.error("No import resolver for: ", import.path);
					return LS_RESULT_FAILURE;
				}
				if (!import_resolver(import_resolver_userdata, import.path, import.alias, &source)) {
					out.error("Import not found: ", import.path);
					return LS_RESULT_FAILURE;
				}
			}
			if (ls_module_parse(module, source, import.path) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
			imported = findUnitByPath(*module, import.path);
		}
		if (imported && resolveImportsForUnit(module, *imported, import_resolver, import_resolver_userdata, out) == LS_RESULT_FAILURE)
			return LS_RESULT_FAILURE;
	}

	unit.import_state = Unit::IMPORT_DONE;
	return LS_RESULT_OK;
}

static ls_result resolveImports(
	ls_module* module,
	ls_import_resolver_fn import_resolver,
	void* import_resolver_userdata
){
	if (!module) return LS_RESULT_FAILURE;
	OutputFormatter out = {};
	if (!module->units.empty()) out.host = module->units[0].arena.host;
	for (u32 unit_index = 0; unit_index < module->units.size(); ++unit_index) {
		Unit& unit = module->units[unit_index];
		if (resolveImportsForUnit(module, unit, import_resolver, import_resolver_userdata, out) == LS_RESULT_FAILURE)
			return LS_RESULT_FAILURE;
	}
	return LS_RESULT_OK;
}

static ls_result checkSymbol(ls_module& module, Unit& unit, Symbol& sym) {
	if (sym.check_state == Symbol::CHECKED) return LS_RESULT_OK;

	OutputFormatter out = {};
	out.host = unit.arena.host;

	if (sym.storage == Symbol::COMPTIME && isPrimitiveShadowName(sym.name)) {
		out.error("Can not shadow primitive type: ", sym.name);
		return LS_RESULT_FAILURE;
	}

	if (sym.check_state == Symbol::CHECKING) {
		// Function literals need to be visible while their own body is being
		// checked so recursive calls can resolve to the provisional function type.
		if (sym.expression && sym.expression->kind == Expression::FUNCTION && sym.resolved_type) {
			return LS_RESULT_OK;
		}
		out.error("Cyclic definition: ", sym.name);
		return LS_RESULT_FAILURE;
	}

	sym.check_state = Symbol::CHECKING;

	if (sym.storage == Symbol::COMPTIME && sym.expression) {
		switch (sym.expression->kind) {
			case Expression::FUNCTION: {
				FunctionExpression& fn = static_cast<FunctionExpression&>(*sym.expression);
				if (!fn.comptime_params.empty()) break; // template — skip
				FunctionResolvedType* fn_type = makeType<FunctionResolvedType>(unit, *unit.arena.arena);
				fn_type->decl = &fn;
				for (FunctionParam& param : fn.runtime_params) {
					param.resolved_type = resolveParsedType(module, unit, param.parsed_type);
					if (!param.resolved_type) return LS_RESULT_FAILURE;
					if (param.is_ref && param.resolved_type->kind == ResolvedType::NULLABLE) return LS_RESULT_FAILURE;
					fn_type->param_types.push(param.resolved_type);
				}
				fn_type->return_type = resolveParsedType(module, unit, fn.return_type);
				if (!fn_type->return_type) return LS_RESULT_FAILURE;
				fn.function_type = fn_type;
				fn.resolved_type = fn_type;
				sym.resolved_type = fn_type;
				sym.instance_type = fn_type;
				if (fn.body && checkFunctionBody(module, unit, &fn) == false) return LS_RESULT_FAILURE;
				break;
			}
			case Expression::STRUCT: {
				StructExpression& st = static_cast<StructExpression&>(*sym.expression);
				if (!st.comptime_params.empty()) break; // template — skip
				StructResolvedType* st_type = makeType<StructResolvedType>(unit, *unit.arena.arena);
				st_type->decl = &st;
				for (NamedDecl& field : st.fields)
					field.resolved_type = resolveParsedType(module, unit, field.parsed_type);
				st.produced_type = primitiveType(module, ResolvedType::TYPE);
				st.resolved_type = st.produced_type;
				sym.instance_type = st_type;
				sym.resolved_type = primitiveType(module, ResolvedType::TYPE);
				break;
			}
			case Expression::ENUM: {
				EnumExpression& en = static_cast<EnumExpression&>(*sym.expression);
				EnumResolvedType* en_type = makeType<EnumResolvedType>(unit);
				en_type->decl = &en;
				en.produced_type = primitiveType(module, ResolvedType::TYPE);
				en.resolved_type = en.produced_type;
				sym.instance_type = en_type;
				sym.resolved_type = primitiveType(module, ResolvedType::TYPE);
				break;
			}
			default: {
				// Plain comptime value: comptime N = expr;
				ResolvedType* annotation = resolveParsedType(module, unit, sym.parsed_type);
				FunctionCheckContext comptime_ctx(unit.arena);
				comptime_ctx.comptime_only = true;
				ResolvedType* expr_type = checkExprImpl(module, unit, &comptime_ctx, sym.expression, annotation);
				if (!expr_type) {
					out.error("Unresolved initializer for: ", sym.name);
					return LS_RESULT_FAILURE;
				}
				if (annotation && expr_type && !canImplicitlyConvert(expr_type, annotation)) {
					out.error("Type mismatch in comptime declaration: ", sym.name);
					return LS_RESULT_FAILURE;
				}
				if (sym.expression && sym.expression->kind == Expression::TYPE_LITERAL) {
					sym.instance_type = expr_type;
					sym.resolved_type = primitiveType(module, ResolvedType::TYPE);
				}
				else {
					sym.resolved_type = annotation ? annotation : expr_type;
				}
				break;
			}
		}
	} else {
		// VAR or CONST global (includes extern fn, which is VARIABLE + FunctionExpression).
		ResolvedType* annotation = resolveParsedType(module, unit, sym.parsed_type);

		if (sym.expression && sym.expression->kind == Expression::UNDEFINED) {
			if (!annotation) {
				out.error("'undefined' initializer requires an explicit type annotation: ", sym.name);
				return LS_RESULT_FAILURE;
			}
			if (sym.storage == Symbol::CONST) {
				out.error("const cannot be initialized with 'undefined': ", sym.name);
				return LS_RESULT_FAILURE;
			}
		}

		ResolvedType* expr_type = nullptr;
		if (sym.expression && sym.expression->kind == Expression::FUNCTION) {
			FunctionExpression& fn = static_cast<FunctionExpression&>(*sym.expression);
			if (!fn.comptime_params.empty()) {
				expr_type = checkExpr(module, unit, sym.expression, annotation);
			}
			else {
				FunctionResolvedType* fn_type = makeType<FunctionResolvedType>(unit, *unit.arena.arena);
				fn_type->decl = &fn;
				for (FunctionParam& param : fn.runtime_params) {
					param.resolved_type = resolveParsedType(module, unit, param.parsed_type);
					if (!param.resolved_type) return LS_RESULT_FAILURE;
					if (param.is_ref && param.resolved_type->kind == ResolvedType::NULLABLE) return LS_RESULT_FAILURE;
					fn_type->param_types.push(param.resolved_type);
				}
				fn_type->return_type = resolveParsedType(module, unit, fn.return_type);
				if (!fn_type->return_type) return LS_RESULT_FAILURE;
				fn.function_type = fn_type;
				fn.resolved_type = fn_type;
				sym.resolved_type = fn_type;
				sym.instance_type = fn_type;
				expr_type = fn_type;
				if (annotation && !canImplicitlyConvert(expr_type, annotation)) {
					out.error("Type mismatch in initializer for: ", sym.name);
					return LS_RESULT_FAILURE;
				}
				if (fn.body && checkFunctionBody(module, unit, &fn) == false) return LS_RESULT_FAILURE;
			}
		}
		else {
			expr_type = sym.expression ? checkExpr(module, unit, sym.expression, annotation) : nullptr;
			if (sym.expression && !expr_type) {
				out.error("Unresolved initializer for: ", sym.name);
				return LS_RESULT_FAILURE;
			}
		}

		if (annotation && expr_type && !canImplicitlyConvert(expr_type, annotation)) {
			out.error("Type mismatch in initializer for: ", sym.name);
			return LS_RESULT_FAILURE;
		}

		sym.resolved_type = annotation ? annotation : expr_type;
	}

	sym.check_state = Symbol::CHECKED;
	return LS_RESULT_OK;
}

static ls_result checkImportCollisions(ls_module& module, Unit& unit) {
	OutputFormatter out = {};
	out.host = unit.arena.host;
	for (const Import& import : unit.imports) {
		if (!empty(import.alias)) continue;
		Unit* imported = findUnitByPath(module, import.path);
		if (!imported) continue;
		for (Symbol& imported_sym : imported->symbols) {
			for (Symbol& local_sym : unit.symbols) {
				if (equalStrings(local_sym.name, imported_sym.name)) {
					out.error("Symbol collision between local and unaliased import: ", local_sym.name);
					return LS_RESULT_FAILURE;
				}
			}
		}
	}
	return LS_RESULT_OK;
}

static ls_result checkNamespaceCollisions(ls_module& module, Unit& unit) {
	OutputFormatter out = {};
	out.host = unit.arena.host;
	for (Symbol& local_sym : unit.symbols) {
		if (!local_sym.resolved_type || local_sym.resolved_type->kind != ResolvedType::FUNCTION) continue;
		FunctionResolvedType* local_fn = static_cast<FunctionResolvedType*>(local_sym.instance_type ? local_sym.instance_type : local_sym.resolved_type);
		if (!local_fn || local_fn->param_types.empty()) continue;
		ResolvedType* first_param = local_fn->param_types[0];
		for (const Import& import : unit.imports) {
			if (empty(import.alias)) continue;
			Unit* imported = findUnitByPath(module, import.path);
			if (!imported) continue;
			for (Symbol& imported_sym : imported->symbols) {
				if (!equalStrings(imported_sym.name, local_sym.name)) continue;
				if (checkSymbol(module, *imported, imported_sym) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
				if (!imported_sym.instance_type || imported_sym.instance_type->kind != ResolvedType::FUNCTION) continue;
				FunctionResolvedType* imp_fn = static_cast<FunctionResolvedType*>(imported_sym.instance_type);
				if (imp_fn->param_types.empty()) continue;
				if (!canImplicitlyConvert(first_param, imp_fn->param_types[0])) continue;
				out.error("Namespace collision: local '", local_sym.name, "' conflicts with aliased import function of same name and first-param type");
				return LS_RESULT_FAILURE;
			}
		}
	}
	return LS_RESULT_OK;
}

ls_result ls_module_typecheck(ls_module* module) {
	if (!module) return LS_RESULT_FAILURE;
	for (Unit& unit : module->units) {
		if (checkImportCollisions(*module, unit) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
		for (Symbol& sym : unit.symbols) {
			if (checkSymbol(*module, unit, sym) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
		}
		if (checkNamespaceCollisions(*module, unit) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	}
	return LS_RESULT_OK;
}

ls_result ls_module_compile(
	ls_module* module,
	ls_string_view source,
	ls_string_view source_name,
	ls_import_resolver_fn import_resolver,
	void* import_resolver_userdata
){
	ls_result parse_result = ls_module_parse(module, source, source_name);
	if (parse_result == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	if (resolveImports(module, import_resolver, import_resolver_userdata) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	if (ls_module_typecheck(module) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	return LS_RESULT_OK;
}
