#include "compiler.h"
#include "utils.h"
#include <float.h>

template <typename T, typename... Args> static T* makeType(Unit& unit, Args&&... args) {
	// Semantic nodes live as long as their owning unit. Allocating them from the
	// unit arena also keeps cached types and template instances pointer-stable.
	ls_arena& arena = *unit.arena.arena;
	void* mem = arena.allocate(arena.user_data, sizeof(T), alignof(T));
	return ::new (mem) T(static_cast<Args&&>(args)...);
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

// Prefer the instantiated type (templates/aggregates), else the declared type.
static ResolvedType* symbolType(const Symbol& sym) {
	return sym.instance_type ? sym.instance_type : sym.resolved_type;
}

// Literal type-checking resolves against the non-nullable destination: a hint of
// `i32?` still constrains an integer literal as an i32.
static ResolvedType* unwrapNullable(ResolvedType* t) {
	return (t && t->kind == ResolvedType::NULLABLE) ? static_cast<NullableResolvedType*>(t)->inner : t;
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
		, declared_loop_labels(arena)
		, declared_loop_kinds(arena) {}

	ExpArray<SemanticLocalBinding> locals;
	ExpArray<u32> scope_marks;
	ExpArray<ls_string_view> loop_labels;
	ExpArray<ls_string_view> label_names;
	ExpArray<ls_string_view> declared_loop_labels;
	ExpArray<Statement::Kind> declared_loop_kinds;
	bool comptime_only = false;
	bool report_errors = true;
	bool error_reported = false;
};

static void printI64(OutputFormatter& out, i64 value) {
	char buffer[32];
	char* end = buffer + sizeof(buffer);
	char* cursor = end;
	u64 magnitude = value < 0 ? (u64)(-(value + 1)) + 1u : (u64)value;
	do {
		*--cursor = (char)('0' + magnitude % 10u);
		magnitude /= 10u;
	} while (magnitude);
	if (value < 0) *--cursor = '-';
	out.print(ls_string_view{cursor, end});
}

static ls_string_view findTypeName(const ls_module& module, const ResolvedType& type) {
	for (const Unit& candidate_unit : module.units) {
		for (const Symbol& symbol : candidate_unit.symbols) {
			if (symbol.instance_type == &type) return symbol.name;
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

static void printResolvedType(OutputFormatter& out, const ls_module& module, const ResolvedType* type) {
	if (!type) {
		out.print("<unresolved>");
		return;
	}
	switch (type->kind) {
		case ResolvedType::VOID: out.print("void"); return;
		case ResolvedType::BOOL: out.print("bool"); return;
		case ResolvedType::I8: out.print("i8"); return;
		case ResolvedType::I16: out.print("i16"); return;
		case ResolvedType::I32: out.print("i32"); return;
		case ResolvedType::I64: out.print("i64"); return;
		case ResolvedType::U8: out.print("u8"); return;
		case ResolvedType::U16: out.print("u16"); return;
		case ResolvedType::U32: out.print("u32"); return;
		case ResolvedType::U64: out.print("u64"); return;
		case ResolvedType::F32: out.print("f32"); return;
		case ResolvedType::F64: out.print("f64"); return;
		case ResolvedType::STRING: out.print("string"); return;
		case ResolvedType::CPTR: out.print("cptr"); return;
		case ResolvedType::TYPE: out.print("type"); return;
		case ResolvedType::ENUM:
		case ResolvedType::STRUCT: {
			ls_string_view name = findTypeName(module, *type);
			out.print(empty(name) ? makeStringView("<anonymous>") : name);
			if (type->kind != ResolvedType::STRUCT) return;
			const StructResolvedType* st = static_cast<const StructResolvedType*>(type);
			if (st->type_args.empty() && st->value_args.empty()) return;
			out.print("[");
			for (u32 i = 0; i < st->type_args.size(); ++i) {
				if (i > 0) out.print(", ");
				if (st->type_args[i])
					printResolvedType(out, module, st->type_args[i]);
				else
					printI64(out, st->value_args[i]);
			}
			out.print("]");
			return;
		}
		case ResolvedType::FUNCTION: {
			const FunctionResolvedType* fn = static_cast<const FunctionResolvedType*>(type);
			out.print("fn(");
			for (u32 i = 0; i < fn->param_types.size(); ++i) {
				if (i > 0) out.print(", ");
				printResolvedType(out, module, fn->param_types[i]);
			}
			out.print(") : ");
			printResolvedType(out, module, fn->return_type);
			return;
		}
		case ResolvedType::ARRAY: {
			const ArrayResolvedType* array = static_cast<const ArrayResolvedType*>(type);
			printResolvedType(out, module, array->element_type);
			out.print("[");
			printI64(out, array->size);
			out.print("]");
			return;
		}
		case ResolvedType::SLICE:
			printResolvedType(out, module, static_cast<const SliceResolvedType*>(type)->element_type);
			out.print("[]");
			return;
		case ResolvedType::NULLABLE:
			out.print("?");
			printResolvedType(out, module, static_cast<const NullableResolvedType*>(type)->inner);
			return;
		default: out.print("<invalid>"); return;
	}
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

// Forward declarations.
static ls_result checkSymbol(ls_module& module, Unit& unit, Symbol& sym);
static ResolvedType* checkExpr(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint, ResolvedType* first_arg_type = nullptr);
static Expression* cloneExpression(Unit& unit, Expression* expr);
static Statement* cloneStatement(Unit& unit, Statement* statement);
static Unit* findUnitByPath(ls_module& module, ls_string_view path);
static Unit* findImportedUnitByAlias(ls_module& module, Unit& unit, ls_string_view alias);
static Unit* findTypeNamespaceUnit(ls_module& module, const ResolvedType& type);
static Symbol* findSymbol(Unit& unit, ls_string_view name);
static SymbolRef resolveSymbol(ls_module& module, Unit& unit, ls_string_view qualifier, ls_string_view name, LookupPolicy policy, ResolvedType* first_arg_type = nullptr);
static ResolvedType* resolveParsedType(ls_module& module, Unit& unit, ParsedType* parsed);

static bool resolveComptimeIntValue(ls_module& module, Unit& unit, Expression* expr, i64& out) {
	if (!expr) return false;
	switch (expr->kind) {
		case Expression::INT_LITERAL: out = static_cast<IntLiteralExpression*>(expr)->value; return true;
		case Expression::IDENTIFIER: {
			IdentifierExpression* id = static_cast<IdentifierExpression*>(expr);
			SymbolRef ref = resolveSymbol(module, unit, {}, id->name, LookupPolicy::NameOnly);
			if (!ref.symbol || ref.symbol->storage != Symbol::COMPTIME) return false;
			if (checkSymbol(module, *ref.owner, *ref.symbol) == LS_RESULT_FAILURE) return false;
			return resolveComptimeIntValue(module, unit, ref.symbol->expression, out);
		}
		case Expression::UNARY: {
			UnaryExpression* un = static_cast<UnaryExpression*>(expr);
			if (un->op != Token::MINUS) return false;
			if (!resolveComptimeIntValue(module, unit, un->expression, out)) return false;
			out = -out;
			return true;
		}
		default: return false;
	}
}

// Name-only scan of unaliased imports. Preserve ambiguity instead of choosing a
// match based on import order.
static SymbolRef findInUnaliasedImports(ls_module& module, Unit& unit, ls_string_view name) {
	SymbolRef found;
	for (const Import& import : unit.imports) {
		if (!empty(import.alias)) continue;
		Unit* imported = findUnitByPath(module, import.path);
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
static SymbolRef resolveSymbol(ls_module& module, Unit& unit, ls_string_view qualifier, ls_string_view name, LookupPolicy policy, ResolvedType* first_arg_type) {
	SymbolRef ref;
	if (!empty(qualifier)) {
		if (Unit* owner = findImportedUnitByAlias(module, unit, qualifier)) {
			if (Symbol* candidate = findSymbol(*owner, name)) ref = {owner, candidate};
		}
	} else {
		SymbolRef namespaced;
		if (first_arg_type) {
			if (Unit* namespace_unit = findTypeNamespaceUnit(module, *first_arg_type)) {
				if (Symbol* candidate = findSymbol(*namespace_unit, name)) {
					namespaced = {namespace_unit, candidate};
				}
			}
		}

		Symbol* local = findSymbol(unit, name);
		SymbolRef imported = findInUnaliasedImports(module, unit, name);
		if (namespaced.symbol) {
			ref = namespaced;
		}
		if (imported.ambiguous || (local && imported.symbol)) {
			ref.ambiguous = true;
		}
		if (namespaced.symbol && local && namespaced.symbol != local) {
			ref.ambiguous = true;
		}
		if (namespaced.symbol && imported.symbol && namespaced.symbol != imported.symbol) {
			ref.ambiguous = true;
		}
		if (!ref.symbol && local) {
			ref = {&unit, local};
		} else if (!ref.symbol) {
			ref = imported;
		}
	}
	if (!ref.symbol) return ref;

	if (policy == LookupPolicy::Checked && checkSymbol(module, *ref.owner, *ref.symbol) == LS_RESULT_FAILURE) {
		ref.check_failed = true;
	}
	return ref;
}

static ResolvedType* resolveParsedType(ls_module& module, Unit& unit, ParsedType* parsed) {
	if (!parsed) return nullptr;
	ResolvedType* result = nullptr;
	switch (parsed->kind) {
		case ParsedType::VOID: result = primitiveType(module, ResolvedType::VOID); break;
		case ParsedType::BOOL: result = primitiveType(module, ResolvedType::BOOL); break;
		case ParsedType::I8: result = primitiveType(module, ResolvedType::I8); break;
		case ParsedType::I16: result = primitiveType(module, ResolvedType::I16); break;
		case ParsedType::I32: result = primitiveType(module, ResolvedType::I32); break;
		case ParsedType::I64: result = primitiveType(module, ResolvedType::I64); break;
		case ParsedType::U8: result = primitiveType(module, ResolvedType::U8); break;
		case ParsedType::U16: result = primitiveType(module, ResolvedType::U16); break;
		case ParsedType::U32: result = primitiveType(module, ResolvedType::U32); break;
		case ParsedType::U64: result = primitiveType(module, ResolvedType::U64); break;
		case ParsedType::F32: result = primitiveType(module, ResolvedType::F32); break;
		case ParsedType::F64: result = primitiveType(module, ResolvedType::F64); break;
		case ParsedType::STRING: result = primitiveType(module, ResolvedType::STRING); break;
		case ParsedType::CPTR: result = primitiveType(module, ResolvedType::CPTR); break;
		case ParsedType::TYPE: result = primitiveType(module, ResolvedType::TYPE); break;
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
			SymbolRef ref = resolveSymbol(module, unit, q->qualifier, q->name, LookupPolicy::Checked);
			result = ref ? symbolType(*ref.symbol) : nullptr;
			break;
		}
		case ParsedType::BRACKET_TYPE: {
			BracketTypeParsedType* call = static_cast<BracketTypeParsedType*>(parsed);
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
		case ResolvedType::CPTR:
		case ResolvedType::TYPE: return true;
		default: return false;
	}
}

static bool isOverloadableBinaryOperator(Token::Type op) {
	return operatorSymbolName(op) != nullptr;
}

static bool isOverloadableUnaryOperator(Token::Type op) {
	return op == Token::MINUS;
}

static FunctionResolvedType* buildFunctionType(ls_module& module, Unit& unit, FunctionExpression& fn, bool reject_nullable_refs) {
	// The AST owns the canonical signature. Besides avoiding duplicate arena
	// allocations, publishing it here gives recursive checking a stable identity.
	if (fn.resolved_type) return static_cast<FunctionResolvedType*>(fn.resolved_type);
	if (!fn.comptime_params.empty()) return nullptr;

	FunctionResolvedType* fn_type = makeType<FunctionResolvedType>(unit, *unit.arena.arena);
	fn_type->decl = &fn;
	for (FunctionParam& param : fn.runtime_params) {
		param.resolved_type = resolveParsedType(module, unit, param.parsed_type);
		if (!param.resolved_type) {
			// TODO error msg
			return nullptr;
		}
		if (reject_nullable_refs && param.is_ref && param.resolved_type->kind == ResolvedType::NULLABLE) {
			// TODO error msg
			return nullptr;
		}
		fn_type->param_types.push(param.resolved_type);
	}
	fn_type->return_type = resolveParsedType(module, unit, fn.return_type);
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
static bool resolveOperatorOverload(ls_module& module,
	Unit& unit,
	FunctionCheckContext* ctx,
	Token::Type op,
	i32 arity,
	Expression** operands, // array of `arity` expression pointers (in/out)
	ResolvedType*& result_type) {
	FunctionResolvedType* found_type = nullptr;
	bool found = false;
	FunctionCheckContext silent_ctx(unit.arena);
	silent_ctx.report_errors = false;
	FunctionCheckContext* candidate_ctx = ctx ? ctx : &silent_ctx;

	auto searchUnit = [&](Unit& search_unit) -> bool {
		for (Symbol& sym : search_unit.symbols) {
			if (tokenFromOperatorName(sym.name) != op) continue;
			if (!sym.expression || sym.expression->kind != Expression::FUNCTION) continue;
			FunctionResolvedType* fn_type = buildFunctionType(module, search_unit, *static_cast<FunctionExpression*>(sym.expression), false);
			if (!fn_type || (i32)fn_type->param_types.size() != arity) continue;

			const bool old_report_errors = candidate_ctx->report_errors;
			candidate_ctx->report_errors = false;
			bool match = true;
			for (i32 i = 0; i < arity && match; ++i) {
				Expression* probe = cloneExpression(unit, operands[i]);
				if (!probe) {
					match = false;
					break;
				}
				ResolvedType* t = checkExpr(module, unit, candidate_ctx, *probe, fn_type->param_types[(u32)i]);
				if (!t || !typesEqual(t, fn_type->param_types[(u32)i])) match = false;
			}
			candidate_ctx->report_errors = old_report_errors;
			if (!match) continue;

			if (found) return false; // ambiguous
			found = true;
			found_type = fn_type;
		}
		return true;
	};

	if (!searchUnit(unit)) return false;
	for (const Import& import : unit.imports) {
		if (Unit* imported = findUnitByPath(module, import.path)) {
			if (!searchUnit(*imported)) return false;
		}
	}

	if (!found) return false;
	for (i32 i = 0; i < arity; ++i) {
		ResolvedType* t = checkExpr(module, unit, ctx, *operands[i], found_type->param_types[(u32)i]);
		if (!t || !typesEqual(t, found_type->param_types[(u32)i])) return false;
	}
	result_type = found_type->return_type;
	return true;
}

static bool resolveBinaryOperator(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Token::Type op, Expression*& lhs_expr, Expression*& rhs_expr, ResolvedType*& result_type) {
	if (!isOverloadableBinaryOperator(op)) return false;
	Expression* operands[2] = {lhs_expr, rhs_expr};
	if (!resolveOperatorOverload(module, unit, ctx, op, 2, operands, result_type)) return false;
	lhs_expr = operands[0];
	rhs_expr = operands[1];
	return true;
}

static bool resolveUnaryOperator(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Token::Type op, Expression*& expr, ResolvedType*& result_type) {
	if (!isOverloadableUnaryOperator(op)) return false;
	Expression* operands[1] = {expr};
	if (!resolveOperatorOverload(module, unit, ctx, op, 1, operands, result_type)) return false;
	expr = operands[0];
	return true;
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

static bool findSymbolForNameExpression(ls_module& module, Unit& unit, const Expression& expression, Unit*& owner, Symbol*& symbol) {
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
	SymbolRef ref = resolveSymbol(module, unit, qualifier, name, LookupPolicy::NameOnly);
	owner = ref.owner;
	symbol = ref.symbol;
	return ref.symbol != nullptr;
}

static ResolvedType* resolveExpressionAsType(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression& expression) {
	if (expression.kind == Expression::TYPE_LITERAL) {
		ParsedType::Kind parsed_kind = static_cast<TypeLiteralExpression&>(expression).type;
		if (parsed_kind < ParsedType::VOID || parsed_kind > ParsedType::TYPE) return nullptr;
		return primitiveType(module, static_cast<ResolvedType::Kind>(parsed_kind));
	}
	if (expression.kind == Expression::IDENTIFIER) {
		IdentifierExpression& id = static_cast<IdentifierExpression&>(expression);
		Unit* owner = nullptr;
		Symbol* symbol = nullptr;
		if (!findSymbolForNameExpression(module, unit, expression, owner, symbol)) return nullptr;
		if (checkSymbol(module, *owner, *symbol) == LS_RESULT_FAILURE) return nullptr;
		id.symbol = symbol;
		return symbol->instance_type;
	}
	if (expression.kind == Expression::MEMBER) {
		Unit* owner = nullptr;
		Symbol* symbol = nullptr;
		if (!findSymbolForNameExpression(module, unit, expression, owner, symbol)) return nullptr;
		if (checkSymbol(module, *owner, *symbol) == LS_RESULT_FAILURE) return nullptr;
		return symbol->instance_type;
	}
	return nullptr;
}

static ResolvedType* checkAssignableExpr(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression* expr, bool& is_writable);

static bool checkFunctionBody(ls_module& module, Unit& unit, FunctionExpression& fn);

struct OverloadResult {
	Symbol* symbol = nullptr;
	FunctionResolvedType* fn_type = nullptr;
	bool abort = false; // ambiguous match or a checkSymbol() failure: caller must bail
};

// `base.name` where `base` is an identifier naming an aliased import (or other
// qualifier). Returns {} when `base` is not an identifier or names no such member,
// in which case the caller falls back to treating `base` as a value.
static SymbolRef resolveQualifiedMember(ls_module& module, Unit& unit, Expression& base, ls_string_view name) {
	if (base.kind != Expression::IDENTIFIER) return {};
	return resolveSymbol(module, unit, static_cast<IdentifierExpression&>(base).name, name, LookupPolicy::Checked);
}

// Look up member `name` on a STRUCT or ENUM value type. Returns the struct field's
// type, the enum type itself for an enum member, or nullptr when `base_type` is not
// an aggregate or has no such member.
static ResolvedType* lookupValueMember(ResolvedType& base_type, ls_string_view name) {
	if (base_type.kind == ResolvedType::STRUCT) {
		StructResolvedType& st = static_cast<StructResolvedType&>(base_type);
		for (u32 i = 0; i < st.decl->fields.size(); ++i) {
			NamedDecl& field = st.decl->fields[i];
			if (!equalStrings(field.name, name)) continue;
			return i < st.field_types.size() ? st.field_types[i] : field.resolved_type;
		}
	} else if (base_type.kind == ResolvedType::ENUM) {
		EnumResolvedType& en = static_cast<EnumResolvedType&>(base_type);
		for (EnumMember& em : en.decl->members) {
			if (equalStrings(em.name, name)) return &base_type;
		}
	}
	return nullptr;
}

static ResolvedType* checkCallExpr(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
	CallExpression& call = static_cast<CallExpression&>(expr);

	// length(slice | array)
	if (call.callee->kind == Expression::IDENTIFIER && call.args.size() == 1) {
		ls_string_view name = static_cast<IdentifierExpression&>(*call.callee).name;
		if (equalStrings(name, makeStringView("length"))) {
			ResolvedType* arg = checkExpr(module, unit, ctx, *call.args[0], nullptr, nullptr);
			if (arg && (arg->kind == ResolvedType::ARRAY || arg->kind == ResolvedType::SLICE)) {
				expr.resolved_type = primitiveType(module, ResolvedType::I32);
				return expr.resolved_type;
			}
			return nullptr;
		}
	}

	ResolvedType* first_arg_type = nullptr;
	// try to resolve first arg for ADL
	if (call.args.size() > 0) {
		first_arg_type = checkExpr(module, unit, ctx, *call.args[0], nullptr, nullptr);
	}

	// check callee
	ResolvedType* callee_type = checkExpr(module, unit, ctx, *call.callee, nullptr, first_arg_type);

	// UFCS: x.foo(a, b) -> foo(x, a, b)
	// When the callee is a member expression that didn't resolve as a field or namespace
	// member, try looking up the member name as a free function in the receiver type's
	// namespace unit. Restricted to struct/enum receivers to avoid UFCS on primitives.
	// Codegen's existing findMemberFunction path handles emission (receiver + args).
	u32 ufcs_param_offset = 0;
	if (!callee_type && call.callee->kind == Expression::MEMBER) {
		MemberExpression& mem = static_cast<MemberExpression&>(*call.callee);
		if (mem.expression) {
			ResolvedType* receiver_type = checkExpr(module, unit, ctx, *mem.expression, nullptr);
			if (receiver_type) {
				if (Unit* ns = findTypeNamespaceUnit(module, *receiver_type)) {
					if (Symbol* sym = findSymbol(*ns, mem.name)) {
						if (checkSymbol(module, *ns, *sym) != LS_RESULT_FAILURE) {
							callee_type = symbolType(*sym);
							ufcs_param_offset = 1;
						}
					}
				}
			}
		}
	}

	if (!callee_type) return nullptr;

	if (callee_type->kind == ResolvedType::FUNCTION) {
		FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(callee_type);

		if (fn_type->param_types.size() != call.args.size() + ufcs_param_offset) return nullptr;

		// check receiver as param[0] for UFCS calls
		if (ufcs_param_offset) {
			MemberExpression& mem = static_cast<MemberExpression&>(*call.callee);
			// resolved_type is set by checkExpr called in the UFCS probe above
			ResolvedType* receiver_type = mem.expression ? mem.expression->resolved_type : nullptr;
			if (!receiver_type || !canImplicitlyConvert(receiver_type, fn_type->param_types[0])) return nullptr;
		}

		// check args
		for (u32 i = 0; i < call.args.size(); ++i) {
			const u32 param_index = ufcs_param_offset + i;
			ResolvedType* param_type = fn_type->param_types[param_index];
			Expression* arg = call.args[i];

			// check ref arg
			if (fn_type->decl && fn_type->decl->runtime_params.size() > param_index && fn_type->decl->runtime_params[param_index].is_ref) {
				// TODO review ref and runtime_params for simplification
				if (arg->kind != Expression::UNARY) return nullptr;
				UnaryExpression* un = static_cast<UnaryExpression*>(arg);
				if (un->op != Token::REF) return nullptr;
				bool writable = false;
				ResolvedType* arg_type = checkAssignableExpr(module, unit, ctx, un->expression, writable);
				if (!arg_type || !writable || !typesEqual(arg_type, param_type)) return nullptr;
				continue;
			}

			// check non-ref arg
			ResolvedType* arg_type = checkExpr(module, unit, ctx, *arg, param_type);
			if (!arg_type || !canImplicitlyConvert(arg_type, param_type)) {
				return nullptr;
			}
		}
		expr.resolved_type = fn_type->return_type;
		return fn_type->return_type;
	}

	// TODO error msg, can we even get here?
	return nullptr;
}

static ResolvedType* checkUnaryExpr(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
	UnaryExpression& un = static_cast<UnaryExpression&>(expr);
	if (un.op == Token::MINUS && un.expression && un.expression->kind == Expression::INT_LITERAL) {
		// Range-check the negated value against the expected type.
		IntLiteralExpression* lit = static_cast<IntLiteralExpression*>(un.expression);
		ResolvedType* int_hint = unwrapNullable(hint);
		const i64 negated = -lit->value;
		if (int_hint && isNumericType(int_hint)) {
			if (!intLiteralFitsType(negated, int_hint->kind)) return nullptr;
			lit->resolved_type = int_hint;
			expr.resolved_type = int_hint;
			return int_hint;
		}
		lit->resolved_type = primitiveType(module, ResolvedType::I32);
		expr.resolved_type = lit->resolved_type;
		return expr.resolved_type;
	}
	ResolvedType* overload_result = nullptr;
	if (resolveUnaryOperator(module, unit, ctx, un.op, un.expression, overload_result)) {
		expr.resolved_type = overload_result;
		return overload_result;
	}
	ResolvedType* inner = checkExpr(module, unit, ctx, *un.expression, hint);
	if (!inner) return nullptr;
	switch (un.op) {
		case Token::MINUS: {
			if (!isNumericType(inner)) {
				// TODO error msg
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
			if (!typesEqual(inner, primitiveType(module, ResolvedType::BOOL))) {
				// TODO error msg
				return nullptr;
			}
			expr.resolved_type = primitiveType(module, ResolvedType::BOOL);
			return expr.resolved_type;
		case Token::REF:
			// TODO error msg
			return nullptr;
		default:
			// TODO error msg
			return nullptr;
	}
}

static ResolvedType* checkBinaryExpr(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
	BinaryExpression& bin = static_cast<BinaryExpression&>(expr);
	ResolvedType* overload_result = nullptr;
	if (resolveBinaryOperator(module, unit, ctx, bin.op, bin.lhs, bin.rhs, overload_result)) {
		expr.resolved_type = overload_result;
		return overload_result;
	}
	ResolvedType* lhs = checkExpr(module, unit, ctx, *bin.lhs, hint); // TODO passing hint might not be correct `var b: bool = 1 > 2;` passed bool hint to 1
	ResolvedType* rhs = checkExpr(module, unit, ctx, *bin.rhs, lhs ? lhs : hint);
	// If lhs failed but rhs resolved (e.g. `.Idle == state`), retry lhs with rhs type as hint.
	if (!lhs && rhs) lhs = checkExpr(module, unit, ctx, *bin.lhs, rhs);
	if (!lhs || !rhs) return nullptr;
	auto invalidOperands = [&]() -> ResolvedType* {
		// TODO error msg
		return nullptr;
	};
	switch (bin.op) {
		case Token::PLUS:
			if (typesEqual(lhs, primitiveType(module, ResolvedType::STRING)) && typesEqual(rhs, primitiveType(module, ResolvedType::STRING))) {
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
			expr.resolved_type = primitiveType(module, ResolvedType::BOOL);
			return expr.resolved_type;
		case Token::LT:
		case Token::LT_EQUAL:
		case Token::GT:
		case Token::GT_EQUAL:
			if (!isNumericType(lhs) || !isNumericType(rhs) || !typesEqual(lhs, rhs)) return invalidOperands();
			expr.resolved_type = primitiveType(module, ResolvedType::BOOL);
			return expr.resolved_type;
		case Token::AND:
		case Token::OR:
			if (!typesEqual(lhs, primitiveType(module, ResolvedType::BOOL)) || !typesEqual(rhs, primitiveType(module, ResolvedType::BOOL))) return invalidOperands();
			expr.resolved_type = primitiveType(module, ResolvedType::BOOL);
			return expr.resolved_type;
		default:
			// TODO error msg
			return nullptr;
	}
}

static ResolvedType* checkCastExpr(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
	CastExpression& cast = static_cast<CastExpression&>(expr);
	ResolvedType* dst_type = resolveParsedType(module, unit, cast.parsed_type);
	if (!dst_type) {
		// TODO error msg
		return nullptr;
	}
	// Don't pass dst_type as hint: explicit casts allow out-of-range values and
	// the operand resolves independently (e.g. `-1 as u8` should work).
	ResolvedType* src_type = checkExpr(module, unit, ctx, *cast.expression, nullptr);
	if (!src_type) return nullptr;
	const bool src_numeric = isNumericType(src_type);
	const bool dst_numeric = isNumericType(dst_type);
	const bool src_bool = typesEqual(src_type, primitiveType(module, ResolvedType::BOOL));
	const bool dst_bool = typesEqual(dst_type, primitiveType(module, ResolvedType::BOOL));
	const bool src_enum = src_type->kind == ResolvedType::ENUM;
	const bool dst_enum = dst_type->kind == ResolvedType::ENUM;
	const bool valid_cast = (src_numeric && dst_numeric) || (src_bool && dst_bool) || (src_enum && dst_numeric) || (src_numeric && dst_enum) || (src_bool && dst_numeric) ||
							(src_numeric && dst_bool) || typesEqual(src_type, dst_type);
	if (!valid_cast) {
		// TODO error msg
		return nullptr;
	}
	expr.resolved_type = dst_type;
	return dst_type;
}

static ResolvedType* checkMemberExpr(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
	MemberExpression& member = static_cast<MemberExpression&>(expr);
	// TODO nullable

	// .enum_member
	if (!member.expression) {
		if (!hint || hint->kind != ResolvedType::ENUM) {
			// TODO error msg
			return nullptr;
		}
		EnumResolvedType* en = static_cast<EnumResolvedType*>(hint);
		for (const EnumMember& m : en->decl->members) {
			if (equalStrings(m.name, member.name)) {
				expr.resolved_type = hint;
				return hint;
			}
		}
		// TODO
		return nullptr;
	}

	ResolvedType* base_type = checkExpr(module, unit, ctx, *member.expression, nullptr);
	if (!base_type) {
		// alias.*
		if (member.expression->kind == Expression::IDENTIFIER) {
			IdentifierExpression* id = static_cast<IdentifierExpression*>(member.expression);
			Unit* imported_unit = findImportedUnitByAlias(module, unit, id->name);
			if (imported_unit) {
				SymbolRef sym = resolveSymbol(module, *imported_unit, {}, member.name, LookupPolicy::NameOnly);
				if (sym) {
					if (checkSymbol(module, *sym.owner, *sym.symbol) == LS_RESULT_FAILURE) return nullptr;
					expr.resolved_type = symbolType(*sym.symbol);
					return expr.resolved_type;
				}
				return nullptr;
			}
			return nullptr;
		}
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
			return nullptr;
		}
		case ResolvedType::ENUM: {
			// enum.enum_member
			EnumResolvedType* en = static_cast<EnumResolvedType*>(base_type);
			for (const EnumMember& m : en->decl->members) {
				if (equalStrings(m.name, member.name)) {
					expr.resolved_type = base_type;
					return base_type;
				}
			}
			// TODO
			return nullptr;
		}

		default: return nullptr;
	}
}

static ResolvedType* checkBracketExpr(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
	BracketExpression& br = static_cast<BracketExpression&>(expr);
	if (ResolvedType* template_type = resolveExpressionAsType(module, unit, ctx, expr)) {
		expr.resolved_type = template_type;
		return template_type;
	}
	ResolvedType* base_type = checkExpr(module, unit, ctx, *br.base, nullptr);
	if (!base_type) return nullptr;
	if (base_type->kind == ResolvedType::NULLABLE) {
		// TODO error msg
		return nullptr;
	}
	if (base_type->kind != ResolvedType::ARRAY && base_type->kind != ResolvedType::SLICE) {
		// TODO error msg
		return nullptr;
	}
	if (br.has_colon) {
		for (Expression* arg : br.args) {
			ResolvedType* arg_type = checkExpr(module, unit, ctx, *arg, primitiveType(module, ResolvedType::I32));
			if (!arg_type || !isIntegerType(arg_type)) {
				// TODO error msg
				return nullptr;
			}
		}
		if (br.end) {
			ResolvedType* end_type = checkExpr(module, unit, ctx, *br.end, primitiveType(module, ResolvedType::I32));
			if (!end_type || !isIntegerType(end_type)) {
				// TODO error msg
				return nullptr;
			}
		}
		const ArrayResolvedType* arr = base_type->kind == ResolvedType::ARRAY ? static_cast<const ArrayResolvedType*>(base_type) : nullptr;
		i64 begin = 0;
		i64 end = arr ? arr->size : 0;
		const bool has_begin = !br.args.empty() && resolveComptimeIntValue(module, unit, br.args[0], begin);
		const bool has_end = br.end ? resolveComptimeIntValue(module, unit, br.end, end) : !!arr;
		if (arr) {
			if (has_begin && (begin < 0 || begin > arr->size)) {
				// TODO error msg
				return nullptr;
			}
			if (has_end && (end < 0 || end > arr->size)) {
				// TODO error msg
				return nullptr;
			}
			if (has_begin && has_end && begin > end) {
				// TODO error msg
				return nullptr;
			}
		}
		SliceResolvedType* slice = makeType<SliceResolvedType>(unit);
		slice->element_type = base_type->kind == ResolvedType::ARRAY ? static_cast<ArrayResolvedType*>(base_type)->element_type : static_cast<SliceResolvedType*>(base_type)->element_type;
		expr.resolved_type = slice;
		return slice;
	}
	if (br.args.size() != 1) {
		// TODO error msg
		return nullptr;
	}
	ResolvedType* index_type = checkExpr(module, unit, ctx, *br.args[0], primitiveType(module, ResolvedType::I32));
	if (!index_type || !isIntegerType(index_type)) {
		// TODO error msg
		return nullptr;
	}
	if (base_type->kind == ResolvedType::ARRAY) {
		const ArrayResolvedType* arr = static_cast<const ArrayResolvedType*>(base_type);
		i64 index = 0;
		if (resolveComptimeIntValue(module, unit, br.args[0], index)) {
			if (index < 0 || index >= arr->size) {
				// TODO error msg
				return nullptr;
			}
		}
		expr.resolved_type = arr->element_type;
	} else {
		expr.resolved_type = static_cast<SliceResolvedType*>(base_type)->element_type;
	}
	return expr.resolved_type;
}

static ResolvedType* checkStructLiteralExpr(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
	StructLiteralExpression& lit = static_cast<StructLiteralExpression&>(expr);
	// In the type position of a literal, a top-level type name wins over a
	// same-named local value. This is also what lets `template[x](x { ... })`
	// use `x` as a type argument and as an ordinary local index nearby.
	ResolvedType* type = nullptr;
	if (lit.type) {
		type = resolveExpressionAsType(module, unit, ctx, *lit.type);
		if (!type) {
			// Happens when lit.type is a bracket expression (e.g. a template instantiation like Foo[T]).
			// resolveExpressionAsType only handles TYPE_LITERAL, IDENTIFIER, and MEMBER.
			type = checkExpr(module, unit, ctx, *lit.type, hint);
		}
	}
	if (!type) type = hint;
	if (!type || type->kind != ResolvedType::STRUCT) {
		// TODO error msg
		return nullptr;
	}
	if (lit.type) lit.type->resolved_type = type;
	StructResolvedType* st = static_cast<StructResolvedType*>(type);
	if (!st->decl || st->decl->fields.size() != lit.values.size()) {
		// TODO error msg
		return nullptr;
	}
	for (i32 i = 0; i < lit.values.size(); ++i) {
		ResolvedType* field_type = (u32)i < st->field_types.size() ? st->field_types[(u32)i] : st->decl->fields[(u32)i].resolved_type;
		ResolvedType* value_type = checkExpr(module, unit, ctx, *lit.values[i], field_type);
		if (!value_type || !canImplicitlyConvert(value_type, field_type)) {
			// TODO error msg
			return nullptr;
		}
	}
	expr.resolved_type = type;
	return type;
}

static ResolvedType* checkExpr(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint, ResolvedType* first_arg_type) {
	switch (expr.kind) {
		case Expression::INT_LITERAL: {
			ResolvedType* int_hint = unwrapNullable(hint);
			if (int_hint && isNumericType(int_hint)) {
				const i64 value = static_cast<IntLiteralExpression&>(expr).value;
				if (!intLiteralFitsType(value, int_hint->kind)) {
					// TODO error msg
					return nullptr;
				}
				expr.resolved_type = int_hint;
			} else {
				expr.resolved_type = primitiveType(module, ResolvedType::I32);
			}
			return expr.resolved_type;
		}
		case Expression::FLOAT_LITERAL: {
			ResolvedType* float_hint = unwrapNullable(hint);
			if (float_hint && float_hint->kind == ResolvedType::F32) {
				const double value = static_cast<FloatLiteralExpression&>(expr).value;
				if (value > (double)FLT_MAX || value < -(double)FLT_MAX) {
					// TODO error msg
					return nullptr;
				}
				expr.resolved_type = float_hint;
			} else {
				expr.resolved_type = (float_hint && isFloatType(float_hint)) ? float_hint : primitiveType(module, ResolvedType::F64);
			}
			return expr.resolved_type;
		}
		case Expression::BOOL_LITERAL: expr.resolved_type = primitiveType(module, ResolvedType::BOOL); return expr.resolved_type;
		case Expression::STRING_LITERAL: expr.resolved_type = primitiveType(module, ResolvedType::STRING); return expr.resolved_type;
		case Expression::NULL_LITERAL:
			if (!hint || (hint->kind != ResolvedType::NULLABLE && hint->kind != ResolvedType::SLICE)) {
				// TODO error msg
				return nullptr;
			}
			expr.resolved_type = hint;
			return expr.resolved_type;
		case Expression::UNDEFINED: expr.resolved_type = hint; return expr.resolved_type;
		case Expression::TYPE_LITERAL: return ctx && ctx->comptime_only ? primitiveType(module, static_cast<ResolvedType::Kind>(static_cast<TypeLiteralExpression&>(expr).type)) : nullptr;
		case Expression::IDENTIFIER: {
			IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
			if (ctx) {
				if (SemanticLocalBinding* local = findLocal(*ctx, id.name)) {
					id.symbol = nullptr;
					expr.resolved_type = local->type;
					return expr.resolved_type;
				}
			}
			SymbolRef ref = resolveSymbol(module, unit, {}, id.name, LookupPolicy::Checked, first_arg_type);
			if (!ref) {
				// TODO error msg
				return nullptr;
			}
			if (ctx && ctx->comptime_only && ref.symbol->storage != Symbol::COMPTIME) {
				// TODO error msg
				return nullptr;
			}
			id.symbol = ref.symbol;
			expr.resolved_type = symbolType(*ref.symbol);
			return expr.resolved_type;
		}
		case Expression::FUNCTION: {
			FunctionExpression& fn = static_cast<FunctionExpression&>(expr);
			if (fn.resolved_type) return fn.resolved_type;
			FunctionResolvedType* fn_type = buildFunctionType(module, unit, fn, true);
			if (!fn_type) return nullptr;
			expr.resolved_type = fn_type;
			if (fn.body) {
				if (!checkFunctionBody(module, unit, fn)) return nullptr;
			}
			return fn_type;
		}
		case Expression::CALL: return checkCallExpr(module, unit, ctx, expr, hint);
		case Expression::UNARY: return checkUnaryExpr(module, unit, ctx, expr, hint);
		case Expression::BINARY: return checkBinaryExpr(module, unit, ctx, expr, hint);
		case Expression::CAST: return checkCastExpr(module, unit, ctx, expr, hint);
		case Expression::MEMBER: return checkMemberExpr(module, unit, ctx, expr, hint);
		case Expression::BRACKET: return checkBracketExpr(module, unit, ctx, expr, hint);
		case Expression::STRUCT_LITERAL: return checkStructLiteralExpr(module, unit, ctx, expr, hint);
		default: return nullptr;
	}
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
			SymbolRef ref = resolveSymbol(module, unit, {}, id->name, LookupPolicy::Checked);
			if (!ref) {
				is_writable = false;
				return nullptr;
			}
			id->symbol = ref.symbol;
			is_writable = ref.symbol->storage == Symbol::VARIABLE;
			return symbolType(*ref.symbol);
		}
		case Expression::MEMBER: {
			MemberExpression* member = static_cast<MemberExpression*>(expr);
			if (!member->expression) {
				is_writable = false;
				return nullptr;
			}
			if (SymbolRef ref = resolveQualifiedMember(module, unit, *member->expression, member->name)) {
				is_writable = ref.symbol->storage == Symbol::VARIABLE;
				return symbolType(*ref.symbol);
			}
			bool base_writable = false;
			ResolvedType* base_type = checkAssignableExpr(module, unit, ctx, member->expression, base_writable);
			if (!base_type || !base_writable) {
				is_writable = false;
				return nullptr;
			}
			ResolvedType* field_type = checkExpr(module, unit, ctx, *expr, nullptr);
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
			ResolvedType* value_type = checkExpr(module, unit, ctx, *expr, nullptr);
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

static Unit* findTypeNamespaceUnit(ls_module& module, const ResolvedType& type) {
	const Expression* decl = nullptr;
	switch (type.kind) {
		case ResolvedType::STRUCT: decl = static_cast<const StructResolvedType&>(type).decl; break;
		case ResolvedType::ENUM: decl = static_cast<const EnumResolvedType&>(type).decl; break;
		default: return nullptr;
	}
	if (!decl) return nullptr;

	for (Unit& candidate_unit : module.units) {
		for (Symbol& symbol : candidate_unit.symbols) {
			if (symbol.instance_type == &type) return &candidate_unit;
			if (symbol.expression == decl) return &candidate_unit;
		}
	}
	return nullptr;
}

static bool checkStatement(ls_module& module, Unit& unit, FunctionCheckContext& ctx, Statement* st, ResolvedType* return_type, ls_string_view pending_label);

static bool checkFunctionBody(ls_module& module, Unit& unit, FunctionExpression& fn) {
	if (!fn.body) return true;
	if (fn.body->kind != Statement::BLOCK) {
		// TODO error msg
		return false;
	}

	ResolvedType* return_type = fn.resolved_type ? static_cast<FunctionResolvedType*>(fn.resolved_type)->return_type : nullptr;
	FunctionCheckContext ctx(*unit.arena.arena);
	pushScope(ctx);
	for (FunctionParam& param : fn.runtime_params) {
		SemanticLocalBinding& binding = ctx.locals.emplace_back();
		binding.name = param.name;
		binding.type = param.resolved_type;
		binding.is_immutable = !param.is_ref;
	}

	BlockStatement* body = static_cast<BlockStatement*>(fn.body);
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


static bool checkVarDeclStatement(ls_module& module, Unit& unit, FunctionCheckContext& ctx, VarDeclStatement* var) {
	if (inCurrentScope(ctx, var->name)) {
		// TODO error msg
		return false;
	}
	for (Symbol& sym : unit.symbols) {
		if (!equalStrings(sym.name, var->name)) continue;
		// TODO error msg
		return false;
	}

	ResolvedType* annotation = resolveParsedType(module, unit, var->parsed_type);
	if (var->expression && var->expression->kind == Expression::UNDEFINED) {
		if (!annotation) {
			// TODO error msg
			return false;
		}
		if (var->is_immutable) {
			// TODO error msg
			return false;
		}
	}
	ResolvedType* expr_type = var->expression ? checkExpr(module, unit, &ctx, *var->expression, annotation) : nullptr;
	if (var->expression && !expr_type) {
		// TODO error msg
		return false;
	}
	if (annotation && expr_type && !canImplicitlyConvert(expr_type, annotation)) {
		// TODO error msg
		return false;
	}
	ResolvedType* final_type = annotation ? annotation : expr_type;
	if (!final_type) {
		// TODO error msg
		return false;
	}
	// Preserve the destination type: bytecode lowering needs it to materialize
	// implicit conversions such as an array initializer becoming a slice.
	var->resolved_type = final_type;

	SemanticLocalBinding& binding = ctx.locals.emplace_back();
	binding.name = var->name;
	binding.type = final_type;
	binding.is_immutable = var->is_immutable;
	return true;
}

static bool checkAssignStatement(ls_module& module, Unit& unit, FunctionCheckContext& ctx, AssignStatement* assign) {
	bool writable = false;
	ResolvedType* lhs_type = checkAssignableExpr(module, unit, &ctx, assign->lhs, writable);
	if (!lhs_type || !writable) {
		// TODO error msg
		return false;
	}
	ResolvedType* rhs_type = checkExpr(module, unit, &ctx, *assign->rhs, lhs_type);
	if (!rhs_type) {
		// TODO error msg
		return false;
	}
	ResolvedType* op_result = nullptr;
	switch (assign->op) {
		case Token::EQUAL:
			if (!canImplicitlyConvert(rhs_type, lhs_type)) return false;
			return true;
		case Token::PLUS_EQUAL:
		case Token::MINUS_EQUAL:
		case Token::STAR_EQUAL:
		case Token::SLASH_EQUAL: {
			if (isNumericType(lhs_type)) return true;
			const Token::Type base_op = assign->op == Token::PLUS_EQUAL ? Token::PLUS : assign->op == Token::MINUS_EQUAL ? Token::MINUS : assign->op == Token::STAR_EQUAL ? Token::STAR : Token::SLASH;
			if (!isOverloadableBinaryOperator(base_op)) return false;
			{
				BinaryExpression synthetic;
				synthetic.lhs = assign->lhs;
				synthetic.rhs = assign->rhs;
				synthetic.op = base_op;
				if (!resolveBinaryOperator(module, unit, &ctx, synthetic.op, synthetic.lhs, synthetic.rhs, op_result)) return false;
			}
			return op_result && canImplicitlyConvert(op_result, lhs_type);
		}
		default: return false;
	}
}

static bool checkIfStatement(ls_module& module, Unit& unit, FunctionCheckContext& ctx, IfStatement* ifst, ResolvedType* return_type) {
	ResolvedType* cond = checkExpr(module, unit, &ctx, *ifst->condition, primitiveType(module, ResolvedType::BOOL));
	if (!cond || !typesEqual(cond, primitiveType(module, ResolvedType::BOOL))) {
		// TODO error msg
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

static bool checkForStatement(ls_module& module, Unit& unit, FunctionCheckContext& ctx, ForStatement* fs, ResolvedType* return_type, ls_string_view pending_label) {
	ResolvedType* begin_type = checkExpr(module, unit, &ctx, *fs->begin, primitiveType(module, ResolvedType::I32));
	ResolvedType* end_type = checkExpr(module, unit, &ctx, *fs->end, begin_type ? begin_type : primitiveType(module, ResolvedType::I32));
	if (!begin_type || !end_type || !isIntegerType(begin_type) || !isIntegerType(end_type)) {
		// TODO error msg
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
	bool ok = checkStatement(module, unit, ctx, fs->body, return_type, {});
	ctx.loop_labels.pop_back();
	popScope(ctx);
	return ok;
}

static bool checkLabelStatement(ls_module& module, Unit& unit, FunctionCheckContext& ctx, LabelStatement* label, ResolvedType* return_type) {
	for (i32 i = (i32)ctx.label_names.size() - 1; i >= 0; --i) {
		if (equalStrings(ctx.label_names[(u32)i], label->name)) {
			// TODO error msg
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
				// TODO error msg
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
	const bool ok = checkStatement(module, unit, ctx, label->statement, return_type, labeled_loop ? label->name : ls_string_view{});
	ctx.label_names.pop_back();
	return ok;
}

static bool checkMatchStatement(ls_module& module, Unit& unit, FunctionCheckContext& ctx, MatchStatement* ms, ResolvedType* return_type) {
	ResolvedType* subject = checkExpr(module, unit, &ctx, *ms->subject, nullptr);
	if (!subject) return false;

	// Subject must be a scalar numeric type, enum, or string.
	const bool subject_is_numeric = isNumericType(subject);
	const bool subject_is_enum = subject->kind == ResolvedType::ENUM;
	const bool subject_is_string = subject->kind == ResolvedType::STRING;
	if (!subject_is_numeric && !subject_is_enum && !subject_is_string) {
		// TODO error msg
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
			ResolvedType* begin = checkExpr(module, unit, &ctx, *pattern.begin, subject);
			if (!begin || !typesEqual(begin, subject)) return false;
			if (pattern.end) {
				// Range patterns are only valid for numeric types.
				if (!subject_is_numeric) {
					// TODO error msg
					return false;
				}
				ResolvedType* end = checkExpr(module, unit, &ctx, *pattern.end, subject);
				if (!end || !typesEqual(end, subject)) return false;
			}
			// Track enum coverage and detect duplicates.
			if (subject_enum && pattern.begin && pattern.begin->kind == Expression::MEMBER) {
				MemberExpression* mem = static_cast<MemberExpression*>(pattern.begin);
				for (u32 i = 0; i < (u32)subject_enum->decl->members.size(); ++i) {
					if (!equalStrings(subject_enum->decl->members[i].name, mem->name)) continue;
					if (covered_enum_members[i]) {
						// TODO error msg
						return false;
					}
					covered_enum_members[i] = true;
					++covered_enum_count;
					break;
				}
			}
		}
		if (!checkStatement(module, unit, ctx, arm.body, return_type, {})) return false;
	}

	// Enum match must cover all variants or have a fallback.
	if (subject_enum && !has_fallback) {
		if (covered_enum_count != (u32)subject_enum->decl->members.size()) {
			// TODO error msg
			return false;
		}
	}

	return true;
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
			if (!checkExpr(module, unit, &ctx, *expr->expression, nullptr)) {
				// TODO error msg
				return false;
			}
			return true;
		}
		case Statement::RETURN: {
			ReturnStatement* ret = static_cast<ReturnStatement*>(st);
			if (!return_type) {
				// TODO error msg
				return false;
			}
			if (return_type->kind == ResolvedType::VOID) {
				if (ret->expression) {
					// TODO error msg
					// TODO return?
				}
				return ret->expression == nullptr;
			}
			if (!ret->expression) {
				// TODO error msg
				return false;
			}
			ResolvedType* expr_type = checkExpr(module, unit, &ctx, *ret->expression, return_type);
			if (!expr_type || !canImplicitlyConvert(expr_type, return_type)) {
				// TODO error msg
				return false;
			}
			return true;
		}
		case Statement::WHILE: {
			WhileStatement* ws = static_cast<WhileStatement*>(st);
			ResolvedType* cond = checkExpr(module, unit, &ctx, *ws->condition, primitiveType(module, ResolvedType::BOOL));
			if (!cond || !typesEqual(cond, primitiveType(module, ResolvedType::BOOL))) {
				// TODO error msg
				return false;
			}
			ctx.loop_labels.push(pending_label);
			bool ok = checkStatement(module, unit, ctx, ws->body, return_type, {});
			ctx.loop_labels.pop_back();
			return ok;
		}
		case Statement::FOR: {
			return checkForStatement(module, unit, ctx, static_cast<ForStatement*>(st), return_type, pending_label);
		}
		case Statement::BREAK:
		case Statement::CONTINUE: {
			BreakStatement* br = static_cast<BreakStatement*>(st);
			ls_string_view label = st->kind == Statement::BREAK ? br->label : static_cast<ContinueStatement*>(st)->label;
			if (!checkLabelTarget(ctx, label)) {
				// TODO error msg
				return false;
			}
			return true;
		}
		case Statement::DEFER: {
			DeferStatement* df = static_cast<DeferStatement*>(st);
			if (!df->statement || df->statement->kind == Statement::RETURN) {
				// TODO error msg
				return false;
			}
			return checkStatement(module, unit, ctx, df->statement, return_type, {});
		}
		case Statement::VAR_DECL: return checkVarDeclStatement(module, unit, ctx, static_cast<VarDeclStatement*>(st));
		case Statement::ASSIGN: return checkAssignStatement(module, unit, ctx, static_cast<AssignStatement*>(st));
		case Statement::IF: return checkIfStatement(module, unit, ctx, static_cast<IfStatement*>(st), return_type);
		case Statement::LABEL: return checkLabelStatement(module, unit, ctx, static_cast<LabelStatement*>(st), return_type);
		case Statement::MATCH: return checkMatchStatement(module, unit, ctx, static_cast<MatchStatement*>(st), return_type);
		default: return false;
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

static ls_result resolveImportsForUnit(ls_module& module, Unit& unit, ls_import_resolver_fn import_resolver, void* import_resolver_userdata, OutputFormatter& out) {
	if (unit.import_state == Unit::IMPORT_DONE) return LS_RESULT_OK;
	if (unit.import_state == Unit::IMPORT_RESOLVING) {
		out.error("Import cycle detected: ", unit.path);
		return LS_RESULT_FAILURE;
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
		Unit* imported = findUnitByPath(module, import.path);
		if (!imported) {
			ls_string_view source = {};
			if (equalStrings(import.path, makeStringView("std:math"))) {
				source = makeStringView(builtin_math_source);
			} else {
				if (!import_resolver) {
					out.error("No import resolver for: ", import.path);
					return LS_RESULT_FAILURE;
				}
				if (!import_resolver(import_resolver_userdata, import.path, import.alias, &source)) {
					out.error("Import not found: ", import.path);
					return LS_RESULT_FAILURE;
				}
			}
			if (ls_module_parse(&module, source, import.path) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
			imported = findUnitByPath(module, import.path);
		}
		if (imported && resolveImportsForUnit(module, *imported, import_resolver, import_resolver_userdata, out) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	}

	unit.import_state = Unit::IMPORT_DONE;
	return LS_RESULT_OK;
}

static ls_result resolveImports(ls_module* module, ls_import_resolver_fn import_resolver, void* import_resolver_userdata) {
	if (!module) return LS_RESULT_FAILURE;
	OutputFormatter out = {};
	if (!module->units.empty()) out.host = module->units[0].arena.host;
	for (u32 unit_index = 0; unit_index < module->units.size(); ++unit_index) {
		Unit& unit = module->units[unit_index];
		if (resolveImportsForUnit(*module, unit, import_resolver, import_resolver_userdata, out) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	}
	return LS_RESULT_OK;
}

static ls_result checkSymbol(ls_module& module, Unit& unit, Symbol& sym) {
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
		out.error("Can not shadow primitive type: ", sym.name);
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
		if (sym.expression && sym.expression->kind == Expression::STRUCT && sym.instance_type && sym.resolved_type == primitiveType(module, ResolvedType::TYPE)) {
			return LS_RESULT_OK;
		}
		out.error("Cyclic definition: ", sym.name);
		return fail();
	}

	sym.check_state = Symbol::CHECKING;

	if (sym.storage == Symbol::COMPTIME && sym.expression) {
		switch (sym.expression->kind) {
			case Expression::FUNCTION: {
				FunctionExpression& fn = static_cast<FunctionExpression&>(*sym.expression);
				if (!fn.comptime_params.empty()) {
					out.error("Templates are not supported: ", sym.name);
					return fail();
				}
				FunctionResolvedType* fn_type = buildFunctionType(module, unit, fn, true);
				if (!fn_type) return fail();
				sym.resolved_type = fn_type;
				sym.instance_type = fn_type;
				if (const Token::Type op_token = tokenFromOperatorName(sym.name); op_token != Token::ERROR) {
					if (!operatorDeclArityMatches(op_token, (i32)fn_type->param_types.size())) {
						out.error("Invalid operator arity");
						return fail();
					}
					if (operatorHasPrimitiveSignature(*fn_type)) {
						out.error("Operator overloads for primitive signatures are not allowed");
						return fail();
					}
				}
				if (fn.body && checkFunctionBody(module, unit, fn) == false) return fail();
				break;
			}
			case Expression::STRUCT: {
				StructExpression& st = static_cast<StructExpression&>(*sym.expression);
				if (!st.comptime_params.empty()) {
					out.error("Templates are not supported: ", sym.name);
					return fail();
				}
				StructResolvedType* st_type = makeType<StructResolvedType>(unit, *unit.arena.arena);
				st_type->decl = &st;
				sym.instance_type = st_type;
				sym.resolved_type = primitiveType(module, ResolvedType::TYPE);
				for (NamedDecl& field : st.fields) {
					field.resolved_type = resolveParsedType(module, unit, field.parsed_type);
					if (!field.resolved_type) {
						out.error("Could not resolve type of field '", field.name, "' in struct: ", sym.name, "\n");
						return fail();
					}
					if (field.resolved_type == st_type) {
						out.error("Recursive by-value field '", field.name, "' in struct: ", sym.name, "\n");
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
				sym.instance_type = en_type;
				sym.resolved_type = primitiveType(module, ResolvedType::TYPE);
				break;
			}
			default: {
				// Plain comptime value: comptime N = expr;
				ResolvedType* annotation = resolveParsedType(module, unit, sym.parsed_type);
				FunctionCheckContext comptime_ctx(unit.arena);
				comptime_ctx.comptime_only = true;
				ResolvedType* expr_type = checkExpr(module, unit, &comptime_ctx, *sym.expression, annotation);
				if (!expr_type) {
					out.error("Unresolved initializer for: ", sym.name);
					return fail();
				}
				if (annotation && expr_type && !canImplicitlyConvert(expr_type, annotation)) {
					out.error("Type mismatch in comptime declaration: ", sym.name);
					return fail();
				}
				if (sym.expression && sym.expression->kind == Expression::TYPE_LITERAL) {
					sym.instance_type = expr_type;
					sym.resolved_type = primitiveType(module, ResolvedType::TYPE);
				} else {
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
				return fail();
			}
			if (sym.storage == Symbol::CONST) {
				out.error("const cannot be initialized with 'undefined': ", sym.name);
				return fail();
			}
		}

		ResolvedType* expr_type = nullptr;
		if (sym.expression && sym.expression->kind == Expression::FUNCTION) {
			FunctionExpression& fn = static_cast<FunctionExpression&>(*sym.expression);
			FunctionResolvedType* fn_type = buildFunctionType(module, unit, fn, true);
			if (!fn_type) return fail();
			sym.resolved_type = fn_type;
			sym.instance_type = fn_type;
			expr_type = fn_type;
			if (annotation && !canImplicitlyConvert(expr_type, annotation)) {
				out.error("Type mismatch in initializer for: ", sym.name);
				return fail();
			}
			if (fn.body && checkFunctionBody(module, unit, fn) == false) return fail();
		} else {
			expr_type = sym.expression ? checkExpr(module, unit, nullptr, *sym.expression, annotation) : nullptr;
			if (sym.expression && !expr_type) {
				out.error("Unresolved initializer for: ", sym.name);
				return fail();
			}
		}

		if (annotation && expr_type && !canImplicitlyConvert(expr_type, annotation)) {
			out.error("Type mismatch in initializer for: ", sym.name);
			return fail();
		}

		sym.resolved_type = annotation ? annotation : expr_type;
	}

	sym.check_state = Symbol::CHECKED;
	return LS_RESULT_OK;
}

ls_result ls_module_typecheck(ls_module* module) {
	if (!module) return LS_RESULT_FAILURE;
	for (Unit& unit : module->units) {
		for (Symbol& sym : unit.symbols) {
			if (checkSymbol(*module, unit, sym) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
		}
	}
	return LS_RESULT_OK;
}

ls_result ls_module_compile(ls_module* module, ls_string_view source, ls_string_view source_name, ls_import_resolver_fn import_resolver, void* import_resolver_userdata) {
	ls_result parse_result = ls_module_parse(module, source, source_name);
	if (parse_result == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	if (resolveImports(module, import_resolver, import_resolver_userdata) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	if (ls_module_typecheck(module) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	return LS_RESULT_OK;
}
