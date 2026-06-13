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

// Prefer the instantiated type (templates/aggregates), else the declared type.
static ResolvedType* symbolType(const Symbol& sym) {
	return sym.instance_type ? sym.instance_type : sym.resolved_type;
}

// Literal type-checking resolves against the non-nullable destination: a hint of
// `i32?` still constrains an integer literal as an i32.
static ResolvedType* unwrapNullable(ResolvedType* t) {
	return (t && t->kind == ResolvedType::NULLABLE)
		? static_cast<NullableResolvedType*>(t)->inner
		: t;
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
		, declared_loop_kinds(arena)
	{}

	ExpArray<SemanticLocalBinding> locals;
	ExpArray<u32> scope_marks;
	ExpArray<ls_string_view> loop_labels;
	ExpArray<ls_string_view> label_names;
	ExpArray<ls_string_view> declared_loop_labels;
	ExpArray<Statement::Kind> declared_loop_kinds;
	bool comptime_only = false;
	const struct TemplateEnv* template_env = nullptr;
};

struct TemplateEnv {
	explicit TemplateEnv(ls_arena& arena)
		: names(arena)
		, types(arena)
		, value_names(arena)
		, int_values(arena) {}

	ResolvedType* find(ls_string_view name) const {
		for (u32 i = 0; i < names.size(); ++i) {
			if (equalStrings(names[i], name)) return types[i];
		}
		return nullptr;
	}

	bool findInt(ls_string_view name, i64& value) const {
		for (u32 i = 0; i < value_names.size(); ++i) {
			if (!equalStrings(value_names[i], name)) continue;
			value = int_values[i];
			return true;
		}
		return false;
	}

	ExpArray<ls_string_view> names;
	ExpArray<ResolvedType*> types;
	ExpArray<ls_string_view> value_names;
	ExpArray<i64> int_values;
};

enum class LookupPolicy { NameOnly, Checked };

// Result of a symbol lookup. `check_failed` is set only under LookupPolicy::Checked
// when the symbol was found but its checkSymbol() failed — distinguishing a genuine
// declaration error from an undeclared name (both used to collapse to nullptr).
struct SymbolRef {
	Unit* owner = nullptr;
	Symbol* symbol = nullptr;
	bool check_failed = false;
	explicit operator bool() const { return symbol && !check_failed; }
};

// Forward declarations.
static ls_result checkSymbol(ls_module& module, Unit& unit, Symbol& sym);
static ResolvedType* checkExprImpl(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression* expr, ResolvedType* hint);
static ResolvedType* checkExpr(ls_module& module, Unit& unit, Expression* expr, ResolvedType* hint);
static Expression* cloneExpression(Unit& unit, Expression* expr);
static Statement* cloneStatement(Unit& unit, Statement* statement);
static Unit* findUnitByPath(ls_module& module, ls_string_view path);
static Unit* findImportedUnitByAlias(ls_module& module, Unit& unit, ls_string_view alias);
static Symbol* findSymbol(Unit& unit, ls_string_view name);
static SymbolRef resolveSymbol(ls_module& module, Unit& unit, ls_string_view qualifier, ls_string_view name, LookupPolicy policy);
static ResolvedType* resolveParsedType(ls_module& module, Unit& unit, ParsedType* parsed, const TemplateEnv* env = nullptr);
static StructResolvedType* instantiateStruct(
	ls_module& module,
	Unit& owner,
	Symbol& symbol,
	StructExpression& decl,
	const ExpArray<ResolvedType*>& type_args,
	const ExpArray<i64>& value_args
);
static FunctionInstance* instantiateFunction(ls_module& module, Unit& owner, Symbol& symbol, FunctionExpression& decl, const ExpArray<ResolvedType*>& args);

static bool resolveComptimeIntValue(ls_module& module, Unit& unit, Expression* expr, i64& out, const TemplateEnv* env = nullptr) {
	if (!expr) return false;
	switch (expr->kind) {
		case Expression::INT_LITERAL:
			out = static_cast<IntLiteralExpression*>(expr)->value;
			return true;
		case Expression::IDENTIFIER: {
			IdentifierExpression* id = static_cast<IdentifierExpression*>(expr);
			if (env && env->findInt(id->name, out)) return true;
			SymbolRef ref = resolveSymbol(module, unit, {}, id->name, LookupPolicy::NameOnly);
			if (!ref.symbol || ref.symbol->storage != Symbol::COMPTIME) return false;
			if (checkSymbol(module, *ref.owner, *ref.symbol) == LS_RESULT_FAILURE) return false;
			return resolveComptimeIntValue(module, unit, ref.symbol->expression, out, env);
		}
		case Expression::UNARY: {
			UnaryExpression* un = static_cast<UnaryExpression*>(expr);
			if (un->op != Token::MINUS) return false;
			if (!resolveComptimeIntValue(module, unit, un->expression, out, env)) return false;
			out = -out;
			return true;
		}
		default:
			return false;
	}
}

// Name-only scan of unaliased imports. Ambiguity is rejected (returns {}) just as
// for ordinary lookup; choosing the first match would make resolution depend on
// import order.
static SymbolRef findInUnaliasedImports(ls_module& module, Unit& unit, ls_string_view name) {
	SymbolRef found;
	for (const Import& import : unit.imports) {
		if (!empty(import.alias)) continue;
		Unit* imported = findUnitByPath(module, import.path);
		if (!imported) continue;
		if (Symbol* candidate = findSymbol(*imported, name)) {
			if (found.symbol) return {};
			found = { imported, candidate };
		}
	}
	return found;
}

// Unified symbol resolution. With an empty qualifier a local symbol shadows
// unaliased imports; with a qualifier the lookup is confined to that aliased unit.
// Under LookupPolicy::Checked the result is also run through checkSymbol().
static SymbolRef resolveSymbol(ls_module& module, Unit& unit, ls_string_view qualifier, ls_string_view name, LookupPolicy policy) {
	SymbolRef ref;
	if (!empty(qualifier)) {
		if (Unit* owner = findImportedUnitByAlias(module, unit, qualifier)) {
			if (Symbol* candidate = findSymbol(*owner, name)) ref = { owner, candidate };
		}
	}
	else if (Symbol* local = findSymbol(unit, name)) {
		ref = { &unit, local };
	}
	else {
		ref = findInUnaliasedImports(module, unit, name);
	}
	if (!ref.symbol) return ref;

	if (policy == LookupPolicy::Checked
		&& checkSymbol(module, *ref.owner, *ref.symbol) == LS_RESULT_FAILURE) {
		ref.check_failed = true;
	}
	return ref;
}

static ResolvedType* resolveComptimeArgType(
	ls_module& module,
	Unit& unit,
	ComptimeArg& arg,
	const TemplateEnv* env
) {
	if (arg.kind == ComptimeArg::TYPE) return resolveParsedType(module, unit, arg.type, env);
	if (arg.kind != ComptimeArg::EXPRESSION || !arg.expression
		|| arg.expression->kind != Expression::IDENTIFIER) return nullptr;
	IdentifierExpression* id = static_cast<IdentifierExpression*>(arg.expression);
	if (env) {
		if (ResolvedType* substituted = env->find(id->name)) return substituted;
	}
	SymbolRef ref = resolveSymbol(module, unit, {}, id->name, LookupPolicy::Checked);
	if (!ref) return nullptr;
	return ref.symbol->instance_type;
}

static ResolvedType* resolveParsedType(ls_module& module, Unit& unit, ParsedType* parsed, const TemplateEnv* env) {
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
				ResolvedType* pt = resolveParsedType(module, unit, param, env);
				if (!pt) return nullptr;
				resolved->param_types.push(pt);
			}
			resolved->return_type = resolveParsedType(module, unit, fn->return_type, env);
			result = resolved;
			break;
		}
		case ParsedType::ARRAY: {
			ArrayParsedType* arr = static_cast<ArrayParsedType*>(parsed);
			ArrayResolvedType* resolved = makeType<ArrayResolvedType>(unit);
			resolved->element_type = resolveParsedType(module, unit, arr->element_type, env);
			i64 size = 0;
			if (!resolved->element_type || !resolveComptimeIntValue(module, unit, arr->size, size, env)) return nullptr;
			if (size <= 0) return nullptr;
			resolved->size = size;
			result = resolved;
			break;
		}
		case ParsedType::SLICE: {
			SliceParsedType* sl = static_cast<SliceParsedType*>(parsed);
			SliceResolvedType* resolved = makeType<SliceResolvedType>(unit);
			resolved->element_type = resolveParsedType(module, unit, sl->element_type, env);
			result = resolved;
			break;
		}
		case ParsedType::QUALIFIED: {
			QualifiedParsedType* q = static_cast<QualifiedParsedType*>(parsed);
			if (env && empty(q->qualifier)) {
				if (ResolvedType* substituted = env->find(q->name)) {
					result = substituted;
					break;
				}
			}
			SymbolRef ref = resolveSymbol(module, unit, q->qualifier, q->name, LookupPolicy::Checked);
			result = ref ? symbolType(*ref.symbol) : nullptr;
			break;
		}
		case ParsedType::COMPTIME_CALL: {
			ComptimeCallParsedType* call = static_cast<ComptimeCallParsedType*>(parsed);
			QualifiedParsedType* callee_name = call->callee && call->callee->kind == ParsedType::QUALIFIED
				? static_cast<QualifiedParsedType*>(call->callee)
				: nullptr;
			SymbolRef ref = callee_name
				? resolveSymbol(module, unit, callee_name->qualifier, callee_name->name, LookupPolicy::NameOnly)
				: SymbolRef{};
			if (ref.symbol
				&& ref.symbol->expression
				&& ref.symbol->expression->kind == Expression::STRUCT) {
				StructExpression& decl = static_cast<StructExpression&>(*ref.symbol->expression);
				if (decl.comptime_params.size() != call->args.size()) return nullptr;
				ExpArray<ResolvedType*> type_args(unit.arena);
				ExpArray<i64> value_args(unit.arena);
				for (u32 i = 0; i < call->args.size(); ++i) {
					ComptimeArg& arg = call->args[i];
					if (!decl.comptime_params[i].parsed_type) {
						ResolvedType* type_arg = resolveComptimeArgType(module, unit, arg, env);
						if (!type_arg) return nullptr;
						type_args.push(type_arg);
						value_args.push(0);
					}
					else {
						if (arg.kind != ComptimeArg::EXPRESSION) return nullptr;
						i64 value = 0;
						if (!resolveComptimeIntValue(module, unit, arg.expression, value, env)) return nullptr;
						type_args.push(nullptr);
						value_args.push(value);
					}
				}
				result = instantiateStruct(module, *ref.owner, *ref.symbol, decl, type_args, value_args);
				break;
			}

			ResolvedType* callee = resolveParsedType(module, unit, call->callee, env);
			if (!callee) return nullptr;
			if (call->args.size() == 1 && call->args[0].kind == ComptimeArg::EXPRESSION) {
				ArrayResolvedType* resolved = makeType<ArrayResolvedType>(unit);
				resolved->element_type = callee;
				i64 size = 0;
				if (!resolveComptimeIntValue(module, unit, call->args[0].expression, size, env)) return nullptr;
				if (size <= 0) return nullptr;
				resolved->size = size;
				result = resolved;
				break;
			}
			result = nullptr; // Neither a generic struct application nor a static array.
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

static bool sameTypeArguments(const ExpArray<ResolvedType*>& lhs, const ExpArray<ResolvedType*>& rhs) {
	if (lhs.size() != rhs.size()) return false;
	for (u32 i = 0; i < lhs.size(); ++i) {
		if (!typesEqual(lhs[i], rhs[i])) return false;
	}
	return true;
}

static bool sameValueArguments(const ExpArray<i64>& lhs, const ExpArray<i64>& rhs) {
	if (lhs.size() != rhs.size()) return false;
	for (u32 i = 0; i < lhs.size(); ++i) {
		if (lhs[i] != rhs[i]) return false;
	}
	return true;
}

static StructResolvedType* instantiateStruct(
	ls_module& module,
	Unit& owner,
	Symbol& symbol,
	StructExpression& decl,
	const ExpArray<ResolvedType*>& type_args,
	const ExpArray<i64>& value_args
) {
	for (StructResolvedType* instance : owner.struct_instances) {
		if (instance->decl != &decl) continue;
		if (!sameTypeArguments(instance->type_args, type_args)) continue;
		if (!sameValueArguments(instance->value_args, value_args)) continue;
		return instance;
	}
	if (decl.comptime_params.size() != type_args.size() || type_args.size() != value_args.size()) return nullptr;

	StructResolvedType* instance = makeType<StructResolvedType>(owner, *owner.arena.arena);
	instance->decl = &decl;
	for (ResolvedType* arg : type_args) instance->type_args.push(arg);
	for (i64 arg : value_args) instance->value_args.push(arg);
	owner.struct_instances.push(instance);

	TemplateEnv env(owner.arena);
	for (u32 i = 0; i < type_args.size(); ++i) {
		if (!decl.comptime_params[i].parsed_type) {
			if (!type_args[i]) return nullptr;
			env.names.push(decl.comptime_params[i].name);
			env.types.push(type_args[i]);
		}
		else {
			ResolvedType* value_type = resolveParsedType(module, owner, decl.comptime_params[i].parsed_type);
			if (!value_type || !isIntegerType(value_type)) return nullptr;
			env.value_names.push(decl.comptime_params[i].name);
			env.int_values.push(value_args[i]);
		}
	}

	// Insert the instance into the cache before resolving fields. A direct or
	// indirect by-value recursion will find the incomplete instance, and the
	// missing field list below makes that cycle fail instead of recursing forever.
	for (NamedDecl& field : decl.fields) {
		ResolvedType* field_type = resolveParsedType(module, owner, field.parsed_type, &env);
		if (!field_type || field_type == instance) {
			owner.struct_instances.pop_back();
			return nullptr;
		}
		if (field_type->kind == ResolvedType::STRUCT) {
			StructResolvedType* nested = static_cast<StructResolvedType*>(field_type);
			if (nested->field_types.empty() && !nested->decl->fields.empty()) {
				owner.struct_instances.pop_back();
				return nullptr;
			}
		}
		instance->field_types.push(field_type);
	}

	symbol.resolved_type = primitiveType(module, ResolvedType::TYPE);
	decl.resolved_type = symbol.resolved_type;
	return instance;
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
		case ResolvedType::TYPE:
			return true;
		default:
			return false;
	}
}

static bool isOverloadableBinaryOperator(Token::Type op) {
	switch (op) {
		case Token::PLUS:
		case Token::MINUS:
		case Token::STAR:
		case Token::SLASH:
		case Token::PERCENT:
		case Token::EQUAL_EQUAL:
		case Token::BANG_EQUAL:
		case Token::LT:
		case Token::LT_EQUAL:
		case Token::GT:
		case Token::GT_EQUAL:
			return true;
		default:
			return false;
	}
}

static bool isOverloadableUnaryOperator(Token::Type op) {
	return op == Token::MINUS;
}

static FunctionResolvedType* buildFunctionType(
	ls_module& module,
	Unit& unit,
	FunctionExpression& fn,
	bool reject_nullable_refs
) {
	if (fn.function_type) return static_cast<FunctionResolvedType*>(fn.function_type);
	if (!fn.comptime_params.empty()) return nullptr;

	FunctionResolvedType* fn_type = makeType<FunctionResolvedType>(unit, *unit.arena.arena);
	fn_type->decl = &fn;
	for (FunctionParam& param : fn.runtime_params) {
		param.resolved_type = resolveParsedType(module, unit, param.parsed_type);
		if (!param.resolved_type) return nullptr;
		if (reject_nullable_refs && param.is_ref && param.resolved_type->kind == ResolvedType::NULLABLE) return nullptr;
		fn_type->param_types.push(param.resolved_type);
	}
	fn_type->return_type = resolveParsedType(module, unit, fn.return_type);
	if (!fn_type->return_type) return nullptr;
	fn.function_type = fn_type;
	fn.resolved_type = fn_type;
	return fn_type;
}

static FunctionResolvedType* ensureOperatorFunctionType(ls_module& module, Unit& unit, OperatorDecl& op_decl) {
	FunctionExpression* fn = op_decl.function;
	if (!fn) return nullptr;
	return buildFunctionType(module, unit, *fn, false);
}

static bool operatorHasPrimitiveSignature(const FunctionResolvedType* fn_type) {
	if (!fn_type) return false;
	if (fn_type->param_types.empty()) return false;
	for (ResolvedType* param : fn_type->param_types) {
		if (!isPrimitiveValueType(param)) return false;
	}
	return true;
}

static bool operatorDeclArityMatches(Token::Type op, i32 arity) {
	if (op == Token::MINUS) return arity == 1 || arity == 2;
	return arity == 2;
}

static bool accessibleUnitSeen(ExpArray<Unit*>& units, Unit* candidate) {
	for (Unit* unit : units) {
		if (unit == candidate) return true;
	}
	return false;
}

static void collectAccessibleUnits(ls_module& module, Unit& unit, ExpArray<Unit*>& out) {
	out.push(&unit);
	for (const Import& import : unit.imports) {
		if (Unit* imported = findUnitByPath(module, import.path)) {
			if (!accessibleUnitSeen(out, imported)) out.push(imported);
		}
	}
}

// Probe-and-commit overload resolution for n-ary operators.
// Clones each operand expression to type-check without mutating the originals,
// then re-checks the winners in-place once a unique match is confirmed.
static bool resolveOperatorOverload(
	ls_module& module,
	Unit& unit,
	FunctionCheckContext* ctx,
	Token::Type op,
	i32 arity,
	Expression** operands, // array of `arity` expression pointers (in/out)
	ResolvedType*& result_type
) {
	ExpArray<Unit*> search_units(unit.arena);
	collectAccessibleUnits(module, unit, search_units);

	FunctionResolvedType* found_type = nullptr;
	bool found = false;

	for (Unit* search_unit : search_units) {
		for (OperatorDecl& decl : search_unit->operators) {
			if (decl.op != op) continue;
			FunctionResolvedType* fn_type = ensureOperatorFunctionType(module, *search_unit, decl);
			if (!fn_type || (i32)fn_type->param_types.size() != arity) continue;

			bool match = true;
			for (i32 i = 0; i < arity && match; ++i) {
				Expression* probe = cloneExpression(unit, operands[i]);
				if (!probe) { match = false; break; }
				ResolvedType* t = checkExprImpl(module, unit, ctx, probe, fn_type->param_types[(u32)i]);
				if (!t || !typesEqual(t, fn_type->param_types[(u32)i])) match = false;
			}
			if (!match) continue;

			if (found) return false; // ambiguous
			found = true;
			found_type = fn_type;
		}
	}

	if (!found) return false;
	for (i32 i = 0; i < arity; ++i) {
		ResolvedType* t = checkExprImpl(module, unit, ctx, operands[i], found_type->param_types[(u32)i]);
		if (!t || !typesEqual(t, found_type->param_types[(u32)i])) return false;
	}
	result_type = found_type->return_type;
	return true;
}

static bool resolveBinaryOperator(
	ls_module& module,
	Unit& unit,
	FunctionCheckContext* ctx,
	Token::Type op,
	Expression*& lhs_expr,
	Expression*& rhs_expr,
	ResolvedType*& result_type
) {
	if (!isOverloadableBinaryOperator(op)) return false;
	Expression* operands[2] = { lhs_expr, rhs_expr };
	if (!resolveOperatorOverload(module, unit, ctx, op, 2, operands, result_type)) return false;
	lhs_expr = operands[0];
	rhs_expr = operands[1];
	return true;
}

static bool resolveUnaryOperator(
	ls_module& module,
	Unit& unit,
	FunctionCheckContext* ctx,
	Token::Type op,
	Expression*& expr,
	ResolvedType*& result_type
) {
	if (!isOverloadableUnaryOperator(op)) return false;
	Expression* operands[1] = { expr };
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
		case Expression::BOOL_LITERAL:
			return makeType<BoolLiteralExpression>(unit, static_cast<BoolLiteralExpression*>(expr)->value);
		case Expression::STRING_LITERAL: {
			StringLiteralExpression* dst = makeType<StringLiteralExpression>(unit);
			dst->value = static_cast<StringLiteralExpression*>(expr)->value;
			return dst;
		}
		case Expression::NULL_LITERAL: return makeType<NullLiteralExpression>(unit);
		case Expression::UNDEFINED: return makeType<UndefinedExpression>(unit);
		case Expression::TYPE_LITERAL:
			return makeType<TypeLiteralExpression>(unit, static_cast<TypeLiteralExpression*>(expr)->type);
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
		default:
			return nullptr;
	}
}

static Statement* cloneStatement(Unit& unit, Statement* statement) {
	if (!statement) return nullptr;
	switch (statement->kind) {
		case Statement::BLOCK:
			return cloneBlock(unit, static_cast<BlockStatement*>(statement));
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
		default:
			return nullptr;
	}
}

static bool findTemplateSymbolForExpression(
	ls_module& module,
	Unit& unit,
	Expression* expression,
	Unit*& owner,
	Symbol*& symbol
) {
	if (!expression) return false;
	ls_string_view qualifier = {};
	ls_string_view name = {};
	if (expression->kind == Expression::IDENTIFIER) {
		name = static_cast<IdentifierExpression*>(expression)->name;
	}
	else if (expression->kind == Expression::MEMBER) {
		MemberExpression* member = static_cast<MemberExpression*>(expression);
		if (!member->expression || member->expression->kind != Expression::IDENTIFIER) return false;
		qualifier = static_cast<IdentifierExpression*>(member->expression)->name;
		name = member->name;
	}
	else {
		return false;
	}
	SymbolRef ref = resolveSymbol(module, unit, qualifier, name, LookupPolicy::NameOnly);
	owner = ref.owner;
	symbol = ref.symbol;
	return ref.symbol != nullptr;
}

static ResolvedType* resolveExpressionAsType(
	ls_module& module,
	Unit& unit,
	FunctionCheckContext* ctx,
	Expression* expression,
	FunctionInstance** function_instance = nullptr
) {
	if (!expression) return nullptr;
	if (expression->kind == Expression::TYPE_LITERAL) {
		ParsedType::Kind parsed_kind = static_cast<TypeLiteralExpression*>(expression)->type;
		if (parsed_kind < ParsedType::VOID || parsed_kind > ParsedType::TYPE) return nullptr;
		return primitiveType(module, static_cast<ResolvedType::Kind>(parsed_kind));
	}
	if (expression->kind == Expression::IDENTIFIER) {
		IdentifierExpression* id = static_cast<IdentifierExpression*>(expression);
		if (ctx && ctx->template_env) {
			if (ResolvedType* substituted = ctx->template_env->find(id->name)) return substituted;
		}
		Unit* owner = nullptr;
		Symbol* symbol = nullptr;
		if (!findTemplateSymbolForExpression(module, unit, expression, owner, symbol)) return nullptr;
		if (checkSymbol(module, *owner, *symbol) == LS_RESULT_FAILURE) return nullptr;
		id->symbol = symbol;
		return symbol->instance_type;
	}
	if (expression->kind == Expression::MEMBER) {
		Unit* owner = nullptr;
		Symbol* symbol = nullptr;
		if (!findTemplateSymbolForExpression(module, unit, expression, owner, symbol)) return nullptr;
		if (checkSymbol(module, *owner, *symbol) == LS_RESULT_FAILURE) return nullptr;
		return symbol->instance_type;
	}
	if (expression->kind != Expression::BRACKET) return nullptr;

	BracketExpression* bracket = static_cast<BracketExpression*>(expression);
	if (bracket->has_colon) return nullptr;
	Unit* owner = nullptr;
	Symbol* symbol = nullptr;
	if (!findTemplateSymbolForExpression(module, unit, bracket->base, owner, symbol)
		|| !symbol->expression) return nullptr;

	ExpArray<ResolvedType*> args(unit.arena);
	for (Expression* arg : bracket->args) {
		ResolvedType* type_arg = resolveExpressionAsType(module, unit, ctx, arg);
		if (!type_arg) return nullptr;
		args.push(type_arg);
	}

	if (symbol->expression->kind == Expression::STRUCT) {
		StructExpression& decl = static_cast<StructExpression&>(*symbol->expression);
		ExpArray<i64> value_args(unit.arena);
		for (u32 i = 0; i < args.size(); ++i) value_args.push(0);
		StructResolvedType* type = instantiateStruct(module, *owner, *symbol, decl, args, value_args);
		if (!type) return nullptr;
		expression->resolved_type = type;
		return type;
	}
	if (symbol->expression->kind == Expression::FUNCTION) {
		FunctionExpression& decl = static_cast<FunctionExpression&>(*symbol->expression);
		FunctionInstance* instance = instantiateFunction(module, *owner, *symbol, decl, args);
		if (!instance) return nullptr;
		expression->resolved_type = instance->type;
		expression->function_instance = instance;
		if (function_instance) *function_instance = instance;
		return instance->type;
	}
	return nullptr;
}

static i32 templateParameterIndex(FunctionExpression& function, ls_string_view name) {
	for (u32 i = 0; i < function.comptime_params.size(); ++i) {
		if (equalStrings(function.comptime_params[i].name, name)) return (i32)i;
	}
	return -1;
}

static bool deduceTemplateType(
	ls_module& module,
	Unit& owner,
	FunctionExpression& function,
	ParsedType* pattern,
	ResolvedType* actual,
	ExpArray<ResolvedType*>& bindings
) {
	if (!pattern || !actual) return false;
	if (pattern->kind == ParsedType::QUALIFIED) {
		QualifiedParsedType* qualified = static_cast<QualifiedParsedType*>(pattern);
		if (empty(qualified->qualifier)) {
			const i32 index = templateParameterIndex(function, qualified->name);
			if (index >= 0) {
				ResolvedType*& binding = bindings[(u32)index];
				if (binding && !typesEqual(binding, actual)) return false;
				binding = actual;
				return true;
			}
		}
		ResolvedType* expected = resolveParsedType(module, owner, pattern);
		return expected && typesEqual(expected, actual);
	}
	if (pattern->kind == ParsedType::COMPTIME_CALL) {
		if (actual->kind != ResolvedType::STRUCT) return false;
		ComptimeCallParsedType* call = static_cast<ComptimeCallParsedType*>(pattern);
		StructResolvedType* concrete = static_cast<StructResolvedType*>(actual);
		if (!call->callee || call->callee->kind != ParsedType::QUALIFIED || !concrete->decl) return false;
		QualifiedParsedType* name = static_cast<QualifiedParsedType*>(call->callee);
		SymbolRef type_ref = resolveSymbol(module, owner, name->qualifier, name->name, LookupPolicy::NameOnly);
		if (!type_ref.symbol
			|| type_ref.symbol->expression != concrete->decl
			|| call->args.size() != concrete->type_args.size()) return false;
		for (u32 i = 0; i < call->args.size(); ++i) {
			ComptimeArg& arg = call->args[i];
			if (arg.kind == ComptimeArg::TYPE) {
				if (!deduceTemplateType(module, owner, function, arg.type, concrete->type_args[i], bindings)) return false;
				continue;
			}
			if (arg.kind != ComptimeArg::EXPRESSION || !arg.expression
				|| arg.expression->kind != Expression::IDENTIFIER) return false;
			IdentifierExpression* id = static_cast<IdentifierExpression*>(arg.expression);
			const i32 index = templateParameterIndex(function, id->name);
			if (index < 0) return false;
			ResolvedType*& binding = bindings[(u32)index];
			if (binding && !typesEqual(binding, concrete->type_args[i])) return false;
			binding = concrete->type_args[i];
		}
		return true;
	}
	if (pattern->kind == ParsedType::ARRAY) {
		if (actual->kind != ResolvedType::ARRAY) return false;
		return deduceTemplateType(
			module,
			owner,
			function,
			static_cast<ArrayParsedType*>(pattern)->element_type,
			static_cast<ArrayResolvedType*>(actual)->element_type,
			bindings
		);
	}
	if (pattern->kind == ParsedType::SLICE) {
		ResolvedType* element = actual->kind == ResolvedType::SLICE
			? static_cast<SliceResolvedType*>(actual)->element_type
			: actual->kind == ResolvedType::ARRAY ? static_cast<ArrayResolvedType*>(actual)->element_type : nullptr;
		return element && deduceTemplateType(module, owner, function, static_cast<SliceParsedType*>(pattern)->element_type, element, bindings);
	}
	ResolvedType* expected = resolveParsedType(module, owner, pattern);
	return expected && canImplicitlyConvert(actual, expected);
}

static ResolvedType* checkExprImpl(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression* expr, ResolvedType* hint);

static ResolvedType* checkAssignableExpr(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression* expr, bool& is_writable);

static bool checkFunctionBody(ls_module& module, Unit& unit, FunctionExpression* fn, const TemplateEnv* env = nullptr);

static bool checkCallArguments(ls_module& module, Unit& unit, FunctionCheckContext* ctx, FunctionResolvedType* fn_type, CallExpression* call, ResolvedType* receiver_type);

struct OverloadResult {
	Symbol* symbol = nullptr;
	FunctionResolvedType* fn_type = nullptr;
	bool abort = false; // ambiguous match or a checkSymbol() failure: caller must bail
};

// Resolves a unique function named `name` across `scope` whose signature accepts
// `call` (with optional `receiver` threaded in as an implicit first argument). When
// `first_arg_filter` is set, candidates must additionally accept it as their first
// parameter — this disambiguates overloaded free functions by their first argument's
// unhinted type. `abort` is set on ambiguity or a checkSymbol() failure.
static OverloadResult resolveOverload(
	ls_module& module, Unit& unit, FunctionCheckContext* ctx,
	const ExpArray<Unit*>& scope, ls_string_view name,
	CallExpression* call, ResolvedType* receiver, ResolvedType* first_arg_filter = nullptr)
{
	OverloadResult result;
	for (Unit* search_unit : scope) {
		for (Symbol& sym : search_unit->symbols) {
			if (!equalStrings(sym.name, name)) continue;
			if (ctx && ctx->comptime_only && sym.storage != Symbol::COMPTIME) continue;
			const bool checking_self = sym.check_state == Symbol::CHECKING && sym.resolved_type;
			if (!checking_self && checkSymbol(module, *search_unit, sym) == LS_RESULT_FAILURE)
				return { nullptr, nullptr, true };
			ResolvedType* sym_type = symbolType(sym);
			if (!sym_type || sym_type->kind != ResolvedType::FUNCTION) continue;
			FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(sym_type);
			if (first_arg_filter
				&& (fn_type->param_types.empty() || !canImplicitlyConvert(first_arg_filter, fn_type->param_types[0])))
				continue;
			if (!checkCallArguments(module, unit, ctx, fn_type, call, receiver)) continue;
			if (result.fn_type && result.fn_type != fn_type) return { nullptr, nullptr, true };
			result.symbol = &sym;
			result.fn_type = fn_type;
		}
	}
	return result;
}

// `base.name` where `base` is an identifier naming an aliased import (or other
// qualifier). Returns {} when `base` is not an identifier or names no such member,
// in which case the caller falls back to treating `base` as a value.
static SymbolRef resolveQualifiedMember(ls_module& module, Unit& unit, Expression* base, ls_string_view name) {
	if (!base || base->kind != Expression::IDENTIFIER) return {};
	return resolveSymbol(module, unit, static_cast<IdentifierExpression*>(base)->name, name, LookupPolicy::Checked);
}

// Look up member `name` on a STRUCT or ENUM value type. Returns the struct field's
// type, the enum type itself for an enum member, or nullptr when `base_type` is not
// an aggregate or has no such member.
static ResolvedType* lookupValueMember(ResolvedType* base_type, ls_string_view name) {
	if (!base_type) return nullptr;
	if (base_type->kind == ResolvedType::STRUCT) {
		StructResolvedType* st = static_cast<StructResolvedType*>(base_type);
		for (u32 i = 0; i < st->decl->fields.size(); ++i) {
			NamedDecl& field = st->decl->fields[i];
			if (!equalStrings(field.name, name)) continue;
			return i < st->field_types.size() ? st->field_types[i] : field.resolved_type;
		}
	}
	else if (base_type->kind == ResolvedType::ENUM) {
		EnumResolvedType* en = static_cast<EnumResolvedType*>(base_type);
		for (EnumMember& em : en->decl->members) {
			if (equalStrings(em.name, name)) return base_type;
		}
	}
	return nullptr;
}

static ResolvedType* checkExpr(ls_module& module, Unit& unit, Expression* expr, ResolvedType* hint) {
	return checkExprImpl(module, unit, nullptr, expr, hint);
}

static ResolvedType* checkExprImpl(ls_module& module, Unit& unit, FunctionCheckContext* ctx, Expression* expr, ResolvedType* hint) {
	if (!expr) return nullptr;
	switch (expr->kind) {
		case Expression::INT_LITERAL: {
			ResolvedType* int_hint = unwrapNullable(hint);
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
			ResolvedType* float_hint = unwrapNullable(hint);
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
			SymbolRef ref = resolveSymbol(module, unit, {}, id->name, LookupPolicy::Checked);
			if (!ref) return nullptr;
			if (ctx && ctx->comptime_only && ref.symbol->storage != Symbol::COMPTIME) return nullptr;
			id->symbol = ref.symbol;
			expr->resolved_type = symbolType(*ref.symbol);
			return expr->resolved_type;
		}
		case Expression::FUNCTION: {
			FunctionExpression* fn = static_cast<FunctionExpression*>(expr);
			if (fn->function_type) {
				expr->resolved_type = fn->function_type;
				return expr->resolved_type;
			}
			FunctionResolvedType* fn_type = buildFunctionType(module, unit, *fn, true);
			if (!fn_type) return nullptr;
			expr->resolved_type = fn_type;
			if (fn->body) {
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

			// A bracketed callee is first offered to template instantiation. If it
			// is not a template, normal expression checking below still treats it
			// as indexing, preserving `functions[i](...)` and array element calls.
			if (call->callee && call->callee->kind == Expression::BRACKET) {
				FunctionInstance* instance = nullptr;
				ResolvedType* explicit_type = resolveExpressionAsType(module, unit, ctx, call->callee, &instance);
				if (explicit_type && instance) {
					if (!checkCallArguments(module, unit, ctx, instance->type, call, nullptr)) return nullptr;
					call->function_instance = instance;
					expr->resolved_type = instance->type->return_type;
					return expr->resolved_type;
				}
			}

			Unit* template_owner = nullptr;
			Symbol* template_symbol = nullptr;
			if (findTemplateSymbolForExpression(module, unit, call->callee, template_owner, template_symbol)
				&& template_symbol->expression
				&& template_symbol->expression->kind == Expression::FUNCTION) {
				FunctionExpression& template_fn = static_cast<FunctionExpression&>(*template_symbol->expression);
				if (!template_fn.comptime_params.empty()) {
					if (template_fn.runtime_params.size() != call->args.size()) return nullptr;
					ExpArray<ResolvedType*> bindings(unit.arena);
					for (u32 i = 0; i < template_fn.comptime_params.size(); ++i) bindings.push(nullptr);

					// Expected return types participate in deduction. Besides being
					// useful for zero-information literals (`identity(1.5)` in an
					// f32 return), this mirrors ordinary checking where literals are
					// resolved using the destination type rather than defaulting first.
					if (!call->args.empty() && hint && !deduceTemplateType(
						module,
						*template_owner,
						template_fn,
						template_fn.return_type,
						hint,
						bindings
					)) return nullptr;

					for (u32 i = 0; i < call->args.size(); ++i) {
						Expression* arg = call->args[i];
						ResolvedType* actual = nullptr;
						if (template_fn.runtime_params[i].is_ref) {
							if (!arg || arg->kind != Expression::UNARY) return nullptr;
							UnaryExpression* ref = static_cast<UnaryExpression*>(arg);
							if (ref->op != Token::REF) return nullptr;
							bool writable = false;
							actual = checkAssignableExpr(module, unit, ctx, ref->expression, writable);
							if (!writable) return nullptr;
						}
						else {
							TemplateEnv partial_env(unit.arena);
							for (u32 binding_index = 0; binding_index < bindings.size(); ++binding_index) {
								if (!bindings[binding_index]) continue;
								partial_env.names.push(template_fn.comptime_params[binding_index].name);
								partial_env.types.push(bindings[binding_index]);
							}
							ResolvedType* arg_hint = resolveParsedType(
								module,
								*template_owner,
								template_fn.runtime_params[i].parsed_type,
								&partial_env
							);
							// Generic calls need a concrete type before a signature
							// exists. LumScript's template tests define decimal
							// literals as f32 in that unconstrained deduction slot;
							// ordinary non-generic local inference remains f64.
							if (!arg_hint && arg && arg->kind == Expression::FLOAT_LITERAL) {
								arg_hint = primitiveType(module, ResolvedType::F32);
							}
							actual = checkExprImpl(module, unit, ctx, arg, arg_hint);
						}
						if (!actual || !deduceTemplateType(
							module,
							*template_owner,
							template_fn,
							template_fn.runtime_params[i].parsed_type,
							actual,
							bindings
						)) return nullptr;
					}
					for (ResolvedType* binding : bindings) {
						if (!binding) return nullptr;
					}

					FunctionInstance* instance = instantiateFunction(
						module,
						*template_owner,
						*template_symbol,
						template_fn,
						bindings
					);
					if (!instance || !checkCallArguments(module, unit, ctx, instance->type, call, nullptr)) return nullptr;
					if (call->callee->kind == Expression::IDENTIFIER) {
						static_cast<IdentifierExpression*>(call->callee)->symbol = template_symbol;
					}
					call->callee->resolved_type = instance->type;
					call->callee->function_instance = instance;
					call->function_instance = instance;
					expr->resolved_type = instance->type->return_type;
					return expr->resolved_type;
				}
			}

			if (call->callee && call->callee->kind == Expression::MEMBER) {
				MemberExpression* member = static_cast<MemberExpression*>(call->callee);
				if (!member->expression) break;
				if (SymbolRef ref = resolveQualifiedMember(module, unit, member->expression, member->name)) {
					ResolvedType* sym_type = symbolType(*ref.symbol);
					if (!sym_type) return nullptr;
					if (sym_type->kind == ResolvedType::FUNCTION) {
						FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(sym_type);
						if (!checkCallArguments(module, unit, ctx, fn_type, call, nullptr)) return nullptr;
						expr->resolved_type = fn_type->return_type;
						return expr->resolved_type;
					}
					expr->resolved_type = sym_type;
					return expr->resolved_type;
				}
				ResolvedType* receiver_type = checkExprImpl(module, unit, ctx, member->expression, nullptr);
				if (!receiver_type) return nullptr;
				if (receiver_type->kind != ResolvedType::STRUCT && receiver_type->kind != ResolvedType::ENUM) return nullptr;

				ExpArray<Unit*> scope(unit.arena);
				collectAccessibleUnits(module, unit, scope);
				OverloadResult ov = resolveOverload(module, unit, ctx, scope, member->name, call, receiver_type);
				if (ov.abort) return nullptr;
				if (ov.fn_type) {
					expr->resolved_type = ov.fn_type->return_type;
					return expr->resolved_type;
				}
				// No free function found; fall through to first-class function field call.
			}

			ResolvedType* callee_type = checkExprImpl(module, unit, ctx, call->callee, nullptr);
			if (!callee_type && call->callee && call->callee->kind == Expression::IDENTIFIER && !call->args.empty()) {
				IdentifierExpression* id = static_cast<IdentifierExpression*>(call->callee);
				ResolvedType* first_arg_type = checkExprImpl(module, unit, ctx, call->args[0], nullptr);
				ExpArray<Unit*> scope(unit.arena);
				for (const Import& import : unit.imports) {
					if (Unit* imported = findUnitByPath(module, import.path)) scope.push(imported);
				}
				OverloadResult ov = resolveOverload(module, unit, ctx, scope, id->name, call, nullptr, first_arg_type);
				if (ov.abort) return nullptr;
				if (ov.fn_type) {
					id->symbol = ov.symbol;
					call->callee->resolved_type = ov.fn_type;
					expr->resolved_type = ov.fn_type->return_type;
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
				ResolvedType* int_hint = unwrapNullable(hint);
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
			ResolvedType* overload_result = nullptr;
			if (resolveUnaryOperator(module, unit, ctx, un->op, un->expression, overload_result)) {
				expr->resolved_type = overload_result;
				return overload_result;
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
			ResolvedType* overload_result = nullptr;
			if (resolveBinaryOperator(module, unit, ctx, bin->op, bin->lhs, bin->rhs, overload_result)) {
				expr->resolved_type = overload_result;
				return overload_result;
			}
			ResolvedType* lhs = checkExprImpl(module, unit, ctx, bin->lhs, hint);
			ResolvedType* rhs = checkExprImpl(module, unit, ctx, bin->rhs, lhs ? lhs : hint);
			if (!lhs || !rhs) return nullptr;
			switch (bin->op) {
				case Token::PLUS:
					if (typesEqual(lhs, primitiveType(module, ResolvedType::STRING))
						&& typesEqual(rhs, primitiveType(module, ResolvedType::STRING))) {
						expr->resolved_type = lhs;
						return lhs;
					}
					if (!isNumericType(lhs) || !isNumericType(rhs) || !typesEqual(lhs, rhs)) return nullptr;
					expr->resolved_type = lhs;
					return lhs;
				case Token::MINUS:
				case Token::STAR:
				case Token::SLASH:
					if (!isNumericType(lhs) || !isNumericType(rhs) || !typesEqual(lhs, rhs)) return nullptr;
					expr->resolved_type = lhs;
					return lhs;
				case Token::PERCENT:
					if (!isIntegerType(lhs) || !isIntegerType(rhs) || !typesEqual(lhs, rhs)) return nullptr;
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
			ResolvedType* dst_type = resolveParsedType(module, unit, cast->parsed_type, ctx ? ctx->template_env : nullptr);
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
			const bool valid_cast =
				(src_numeric && dst_numeric) ||
				(src_bool    && dst_bool)    ||
				(src_enum    && dst_numeric) ||
				(src_numeric && dst_enum)    ||
				(src_bool    && dst_numeric) ||
				(src_numeric && dst_bool)    ||
				typesEqual(src_type, dst_type);
			if (!valid_cast) return nullptr;
			expr->resolved_type = dst_type;
			return dst_type;
		}
		case Expression::MEMBER: {
			MemberExpression* member = static_cast<MemberExpression*>(expr);
			if (member->expression) {
				if (SymbolRef ref = resolveQualifiedMember(module, unit, member->expression, member->name)) {
					expr->resolved_type = symbolType(*ref.symbol);
					return expr->resolved_type;
				}
				ResolvedType* base_type = checkExprImpl(module, unit, ctx, member->expression, nullptr);
				if (!base_type) return nullptr;
				ResolvedType* member_type = lookupValueMember(base_type, member->name);
				if (!member_type) return nullptr;
				expr->resolved_type = member_type;
				return expr->resolved_type;
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
			if (ResolvedType* template_type = resolveExpressionAsType(module, unit, ctx, expr)) {
				expr->resolved_type = template_type;
				return template_type;
			}
			ResolvedType* base_type = checkExprImpl(module, unit, ctx, br->base, nullptr);
			if (!base_type) return nullptr;
			if (base_type->kind == ResolvedType::NULLABLE) return nullptr;
			if (base_type->kind != ResolvedType::ARRAY && base_type->kind != ResolvedType::SLICE) return nullptr;
			if (br->has_colon) {
				for (Expression* arg : br->args) {
					ResolvedType* arg_type = checkExprImpl(module, unit, ctx, arg, primitiveType(module, ResolvedType::I32));
					if (!arg_type || !isIntegerType(arg_type)) return nullptr;
				}
				if (br->end) {
					ResolvedType* end_type = checkExprImpl(module, unit, ctx, br->end, primitiveType(module, ResolvedType::I32));
					if (!end_type || !isIntegerType(end_type)) return nullptr;
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
			if (!index_type || !isIntegerType(index_type)) return nullptr;
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
			// In the type position of a literal, a top-level type name wins over a
			// same-named local value. This is also what lets `template[x](x { ... })`
			// use `x` as a type argument and as an ordinary local index nearby.
			ResolvedType* type = resolveExpressionAsType(module, unit, ctx, lit->type);
			if (!type) type = checkExprImpl(module, unit, ctx, lit->type, hint);
			if (!type) type = hint;
			if (!type || type->kind != ResolvedType::STRUCT) return nullptr;
			if (lit->type) lit->type->resolved_type = type;
			StructResolvedType* st = static_cast<StructResolvedType*>(type);
			if (!st->decl || st->decl->fields.size() != lit->values.size()) return nullptr;
			for (i32 i = 0; i < lit->values.size(); ++i) {
				ResolvedType* field_type = (u32)i < st->field_types.size()
					? st->field_types[(u32)i]
					: st->decl->fields[(u32)i].resolved_type;
				ResolvedType* value_type = checkExprImpl(module, unit, ctx, lit->values[i], field_type);
				if (!value_type || !typesEqual(value_type, field_type)) return nullptr;
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
			if (SymbolRef ref = resolveQualifiedMember(module, unit, member->expression, member->name)) {
				is_writable = ref.symbol->storage == Symbol::VARIABLE;
				return symbolType(*ref.symbol);
			}
			bool base_writable = false;
			ResolvedType* base_type = checkAssignableExpr(module, unit, ctx, member->expression, base_writable);
			if (!base_type || !base_writable) {
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

static bool checkStatement(ls_module& module, Unit& unit, FunctionCheckContext& ctx, Statement* st, ResolvedType* return_type, ls_string_view pending_label);

static bool checkFunctionBody(ls_module& module, Unit& unit, FunctionExpression* fn, const TemplateEnv* env) {
	if (!fn || !fn->body) return true;
	if (fn->body->kind != Statement::BLOCK) return false;

	ResolvedType* return_type = fn->function_type ? static_cast<FunctionResolvedType*>(fn->function_type)->return_type : nullptr;
	FunctionCheckContext ctx(*unit.arena.arena);
	ctx.template_env = env;
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

static FunctionInstance* instantiateFunction(
	ls_module& module,
	Unit& owner,
	Symbol& symbol,
	FunctionExpression& decl,
	const ExpArray<ResolvedType*>& args
) {
	for (FunctionInstance* instance : owner.function_instances) {
		if (instance->declaration == &decl && sameTypeArguments(instance->type_args, args)) {
			return instance->check_state == FunctionInstance::FAILED ? nullptr : instance;
		}
	}
	if (decl.comptime_params.size() != args.size()) return nullptr;

	FunctionInstance* instance = makeType<FunctionInstance>(owner, *owner.arena.arena);
	instance->unit = &owner;
	instance->symbol = &symbol;
	instance->declaration = &decl;
	for (ResolvedType* arg : args) instance->type_args.push(arg);
	owner.function_instances.push(instance);

	TemplateEnv env(owner.arena);
	for (u32 i = 0; i < args.size(); ++i) {
		if (decl.comptime_params[i].parsed_type) {
			instance->check_state = FunctionInstance::FAILED;
			return nullptr;
		}
		env.names.push(decl.comptime_params[i].name);
		env.types.push(args[i]);
	}

	FunctionExpression* function = makeType<FunctionExpression>(owner, *owner.arena.arena);
	function->return_type = decl.return_type;
	function->is_extern = decl.is_extern;
	for (FunctionParam& src : decl.runtime_params) {
		FunctionParam& dst = function->runtime_params.emplace_back();
		dst.name = src.name;
		dst.is_ref = src.is_ref;
		dst.parsed_type = src.parsed_type;
	}
	function->body = cloneStatement(owner, decl.body);
	if (decl.body && !function->body) {
		instance->check_state = FunctionInstance::FAILED;
		return nullptr;
	}
	instance->function = function;

	FunctionResolvedType* type = makeType<FunctionResolvedType>(owner, *owner.arena.arena);
	type->decl = function;
	for (FunctionParam& param : function->runtime_params) {
		param.resolved_type = resolveParsedType(module, owner, param.parsed_type, &env);
		if (!param.resolved_type || (param.is_ref && param.resolved_type->kind == ResolvedType::NULLABLE)) {
			instance->check_state = FunctionInstance::FAILED;
			return nullptr;
		}
		type->param_types.push(param.resolved_type);
	}
	type->return_type = resolveParsedType(module, owner, function->return_type, &env);
	if (!type->return_type) {
		instance->check_state = FunctionInstance::FAILED;
		return nullptr;
	}

	instance->type = type;
	function->function_type = type;
	function->resolved_type = type;
	instance->check_state = FunctionInstance::CHECKING;
	if (function->body && !checkFunctionBody(module, owner, function, &env)) {
		instance->check_state = FunctionInstance::FAILED;
		return nullptr;
	}
	instance->check_state = FunctionInstance::READY;
	return instance;
}

static bool checkLabelTarget(FunctionCheckContext& ctx, ls_string_view label) {
	if (empty(label)) return !ctx.loop_labels.empty();
	for (i32 i = (i32)ctx.loop_labels.size() - 1; i >= 0; --i) {
		if (equalStrings(ctx.loop_labels[(u32)i], label)) return true;
	}
	return false;
}

static ls_result checkOperatorDecl(ls_module& module, Unit& unit, OperatorDecl& decl) {
	OutputFormatter out = {};
	out.host = unit.arena.host;

	FunctionExpression* fn = decl.function;
	if (!fn) return LS_RESULT_FAILURE;

	FunctionResolvedType* fn_type = ensureOperatorFunctionType(module, unit, decl);
	if (!fn_type) {
		out.error("Invalid operator declaration");
		return LS_RESULT_FAILURE;
	}

	if (!operatorDeclArityMatches(decl.op, (i32)fn_type->param_types.size())) {
		out.error("Invalid operator arity");
		return LS_RESULT_FAILURE;
	}
	if (operatorHasPrimitiveSignature(fn_type)) {
		out.error("Operator overloads for primitive signatures are not allowed");
		return LS_RESULT_FAILURE;
	}
	if (!checkFunctionBody(module, unit, fn)) return LS_RESULT_FAILURE;
	return LS_RESULT_OK;
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

			ResolvedType* annotation = resolveParsedType(module, unit, var->parsed_type, ctx.template_env);
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
			ResolvedType* op_result = nullptr;
			switch (assign->op) {
				case Token::EQUAL:
					return true;
				case Token::PLUS_EQUAL:
				case Token::MINUS_EQUAL:
				case Token::STAR_EQUAL:
				case Token::SLASH_EQUAL: {
					if (isNumericType(lhs_type)) return true;
					const Token::Type base_op = assign->op == Token::PLUS_EQUAL ? Token::PLUS :
						assign->op == Token::MINUS_EQUAL ? Token::MINUS :
						assign->op == Token::STAR_EQUAL ? Token::STAR : Token::SLASH;
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
					if (!begin_type || !end_type || !isIntegerType(begin_type) || !isIntegerType(end_type)) return false;
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
			const bool labeled_loop = label->statement && (label->statement->kind == Statement::WHILE || label->statement->kind == Statement::FOR);
			if (labeled_loop) {
				// Active labels catch lexical duplicates. Keep a second function-wide
				// registry because reusing a name for a different loop construct is
				// ambiguous to later control-flow lowering, while sequential loops of
				// the same construct intentionally reuse labels.
				bool known_label = false;
				for (u32 i = 0; i < ctx.declared_loop_labels.size(); ++i) {
					if (!equalStrings(ctx.declared_loop_labels[i], label->name)) continue;
					if (ctx.declared_loop_kinds[i] != label->statement->kind) return false;
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
			ExpArray<bool> covered_enum_members(unit.arena);
			if (subject_enum) covered_enum_members.resize(subject_enum->decl->members.size(), false);
			u32 covered_enum_count = 0;

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
							if (covered_enum_members[i]) return false;
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
				if (covered_enum_count != (u32)subject_enum->decl->members.size()) return false;
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

static bool parsedTypeReferencesName(ParsedType* type, ls_string_view name) {
	if (!type) return false;
	switch (type->kind) {
		case ParsedType::QUALIFIED: {
			QualifiedParsedType* qualified = static_cast<QualifiedParsedType*>(type);
			return empty(qualified->qualifier) && equalStrings(qualified->name, name);
		}
		case ParsedType::COMPTIME_CALL: {
			ComptimeCallParsedType* call = static_cast<ComptimeCallParsedType*>(type);
			if (parsedTypeReferencesName(call->callee, name)) return true;
			for (ComptimeArg& arg : call->args) {
				if (arg.kind == ComptimeArg::TYPE && parsedTypeReferencesName(arg.type, name)) return true;
			}
			return false;
		}
		case ParsedType::ARRAY:
			return parsedTypeReferencesName(static_cast<ArrayParsedType*>(type)->element_type, name);
		case ParsedType::SLICE:
			return parsedTypeReferencesName(static_cast<SliceParsedType*>(type)->element_type, name);
		case ParsedType::FUNCTION: {
			FunctionParsedType* function = static_cast<FunctionParsedType*>(type);
			for (ParsedType* param : function->params) {
				if (parsedTypeReferencesName(param, name)) return true;
			}
			return parsedTypeReferencesName(function->return_type, name);
		}
		default:
			return false;
	}
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
				FunctionResolvedType* fn_type = buildFunctionType(module, unit, fn, true);
				if (!fn_type) return LS_RESULT_FAILURE;
				sym.resolved_type = fn_type;
				sym.instance_type = fn_type;
				if (fn.body && checkFunctionBody(module, unit, &fn) == false) return LS_RESULT_FAILURE;
				break;
			}
			case Expression::STRUCT: {
				StructExpression& st = static_cast<StructExpression&>(*sym.expression);
				if (!st.comptime_params.empty()) {
					// Struct values are inline aggregates, so a generic declaration
					// containing itself cannot acquire a finite layout for any type
					// argument. Reject it even when no specialization is requested.
					for (NamedDecl& field : st.fields) {
						if (parsedTypeReferencesName(field.parsed_type, sym.name)) return LS_RESULT_FAILURE;
					}
				}
				if (!st.comptime_params.empty()) break; // template — skip
				StructResolvedType* st_type = makeType<StructResolvedType>(unit, *unit.arena.arena);
				st_type->decl = &st;
				for (NamedDecl& field : st.fields) {
					field.resolved_type = resolveParsedType(module, unit, field.parsed_type);
					if (!field.resolved_type) return LS_RESULT_FAILURE;
					st_type->field_types.push(field.resolved_type);
				}
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
				FunctionResolvedType* fn_type = buildFunctionType(module, unit, fn, true);
				if (!fn_type) return LS_RESULT_FAILURE;
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
		for (OperatorDecl& decl : unit.operators) {
			if (checkOperatorDecl(*module, unit, decl) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
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
