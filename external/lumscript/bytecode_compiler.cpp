#include "bytecode.h"
#include "compiler.h"
#include "exparray.h"
#include "utils.h"

#include <cstdlib>

static u64 bitcastF64ToU64(double value) {
	u64 raw = 0;
	copyMemory(&raw, &value, sizeof(raw));
	return raw;
}

static u32 bitcastF32ToU32(float value) {
	u32 raw = 0;
	copyMemory(&raw, &value, sizeof(raw));
	return raw;
}

static u64 bitcastI64ToU64(i64 value) {
	u64 raw = 0;
	copyMemory(&raw, &value, sizeof(raw));
	return raw;
}

static ls_type_kind toTypeKind(const ResolvedType* type) {
	if (!type) return LS_TYPE_VOID;
	switch (type->kind) {
		case ResolvedType::VOID: return LS_TYPE_VOID;
		case ResolvedType::BOOL: return LS_TYPE_BOOL;
		case ResolvedType::I8: return LS_TYPE_I8;
		case ResolvedType::I16: return LS_TYPE_I16;
		case ResolvedType::I32: return LS_TYPE_I32;
		case ResolvedType::I64: return LS_TYPE_I64;
		case ResolvedType::U8: return LS_TYPE_U8;
		case ResolvedType::U16: return LS_TYPE_U16;
		case ResolvedType::U32: return LS_TYPE_U32;
		case ResolvedType::U64: return LS_TYPE_U64;
		case ResolvedType::F32: return LS_TYPE_F32;
		case ResolvedType::F64: return LS_TYPE_F64;
		case ResolvedType::STRING: return LS_TYPE_STRING;
		case ResolvedType::CPTR: return LS_TYPE_CPTR;
		case ResolvedType::FUNCTION: return LS_TYPE_FUNCTION;
		case ResolvedType::ARRAY: return LS_TYPE_ARRAY;
		case ResolvedType::SLICE: return LS_TYPE_SLICE;
		case ResolvedType::NULLABLE: return LS_TYPE_NULL_VALUE;
		default: return LS_TYPE_INVALID;
	}
}

static ls_type_kind parsedTypeToKind(const ParsedType* type) {
	if (!type) return LS_TYPE_INVALID;
	switch (type->kind) {
		case ParsedType::VOID: return LS_TYPE_VOID;
		case ParsedType::BOOL: return LS_TYPE_BOOL;
		case ParsedType::I8: return LS_TYPE_I8;
		case ParsedType::I16: return LS_TYPE_I16;
		case ParsedType::I32: return LS_TYPE_I32;
		case ParsedType::I64: return LS_TYPE_I64;
		case ParsedType::U8: return LS_TYPE_U8;
		case ParsedType::U16: return LS_TYPE_U16;
		case ParsedType::U32: return LS_TYPE_U32;
		case ParsedType::U64: return LS_TYPE_U64;
		case ParsedType::F32: return LS_TYPE_F32;
		case ParsedType::F64: return LS_TYPE_F64;
		case ParsedType::STRING: return LS_TYPE_STRING;
		case ParsedType::CPTR: return LS_TYPE_CPTR;
		case ParsedType::FUNCTION: return LS_TYPE_FUNCTION;
		default: return LS_TYPE_INVALID;
	}
}

static ls_type_kind semanticTypeToKind(const ResolvedType* type) {
	if (!type) return LS_TYPE_INVALID;
	if (type->kind == ResolvedType::ENUM) return LS_TYPE_ENUM;
	return toTypeKind(type);
}

static bool isIntegerKind(ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_I8:
		case LS_TYPE_I16:
		case LS_TYPE_I32:
		case LS_TYPE_I64:
		case LS_TYPE_U8:
		case LS_TYPE_U16:
		case LS_TYPE_U32:
		case LS_TYPE_U64:
		case LS_TYPE_BOOL:
			return true;
		default:
			return false;
	}
}

static bool isFloatKind(ls_type_kind kind) {
	return kind == LS_TYPE_F32 || kind == LS_TYPE_F64;
}

static bool isNumericKind(ls_type_kind kind) {
	return isIntegerKind(kind) || isFloatKind(kind);
}

static ls_type_kind defaultLiteralKind(const Expression* expr, ls_type_kind hint) {
	if (expr->kind == Expression::INT_LITERAL) {
		return isIntegerKind(hint) ? hint : LS_TYPE_I32;
	}
	if (expr->kind == Expression::FLOAT_LITERAL) {
		return isFloatKind(hint) ? hint : LS_TYPE_F64;
	}
	if (expr->kind == Expression::BOOL_LITERAL) return LS_TYPE_BOOL;
	return LS_TYPE_INVALID;
}

static ls_type_kind numericKindForOp(ls_type_kind lhs_kind, ls_type_kind hint) {
	if (isNumericKind(lhs_kind)) return lhs_kind;
	if (isNumericKind(hint)) return hint;
	return LS_TYPE_I32;
}

struct ByteArray {
	explicit ByteArray(ls_arena& arena) : arena(arena) {}

	void push_back(u8 value) {
		if (count == capacity) {
			const u32 new_capacity = capacity ? capacity * 2u : 64u;
			u8* new_data = static_cast<u8*>(arena.allocate(arena.user_data, new_capacity, alignof(u8)));
			ASSERT(new_data);
			if (data) copyMemory(new_data, data, count);
			data = new_data;
			capacity = new_capacity;
		}
		data[count++] = value;
	}

	i32 size() const { return (i32)count; }
	u8& operator[](u32 index) { ASSERT(index < count); return data[index]; }
	const u8& operator[](u32 index) const { ASSERT(index < count); return data[index]; }

	ls_arena& arena;
	u8* data = nullptr;
	u32 count = 0;
	u32 capacity = 0;
};

template <typename Array>
static void emitBytes(Array& code, const void* value, size_t size) {
	const u8* bytes = static_cast<const u8*>(value);
	for (size_t i = 0; i < size; ++i) code.push_back(bytes[i]);
}

template <typename Array>
static void emitU8(Array& code, u8 value) {
	emitBytes(code, &value, sizeof(value));
}

template <typename Array>
static void emitU32(Array& code, u32 value) {
	emitBytes(code, &value, sizeof(value));
}

template <typename Array>
static void emitI32(Array& code, i32 value) {
	emitBytes(code, &value, sizeof(value));
}

template <typename Array>
static void patchI32(Array& code, u32 offset, i32 value) {
	const u32 bits = (u32)value;
	code[offset + 0u] = (u8)(bits & 0xFFu);
	code[offset + 1u] = (u8)((bits >> 8u) & 0xFFu);
	code[offset + 2u] = (u8)((bits >> 16u) & 0xFFu);
	code[offset + 3u] = (u8)((bits >> 24u) & 0xFFu);
}

template <typename Array>
static void emitU64(Array& code, u64 value) {
	emitBytes(code, &value, sizeof(value));
}

template <typename Array>
static void emitOp(Array& code, ls_op op) {
	emitU8(code, (u8)op);
}

template <typename T>
static T* appendArenaArray(ls_arena* arena, T*& data, u32& count, u32& capacity) {
	if (count >= capacity) {
		const u32 new_capacity = capacity ? capacity * 2u : 4u;
		T* const new_data = static_cast<T*>(arena->allocate(arena->user_data, sizeof(T) * (size_t)new_capacity, alignof(T)));
		if (!new_data) return nullptr;
		if (data && count > 0) copyMemory(new_data, data, sizeof(T) * (size_t)count);
		data = new_data;
		capacity = new_capacity;
	}
	return &data[count++];
}

struct BytecodeLocalBinding {
	ls_string_view name = {};
	ResolvedType* type = nullptr;
	ls_type_kind kind = LS_TYPE_INVALID;
	u32 slot = 0;
	u32 slot_count = 1;
};

struct LoopBinding {
	ls_string_view label = {};
	u32 condition_pos = 0;
	u32 continue_pos = 0;
	u32 defer_mark = 0;
	ExpArray<u32>* break_jumps = nullptr;
	ExpArray<u32>* continue_jumps = nullptr;
};

struct FunctionInfo {
	ls_string_view name = {};
	FunctionExpression* fn = nullptr;
	FunctionResolvedType* type = nullptr;
	Unit* unit = nullptr;
	Symbol* symbol = nullptr;
	u32 index = 0;

	Token::Type operatorToken() const {
		return symbol ? tokenFromOperatorName(symbol->name) : Token::ERROR;
	}
};

struct GlobalBinding {
	Symbol* sym = nullptr;
	u32 slot = 0;
	u32 slot_count = 0;
};

static u32 typeSlotCount(ResolvedType* type);
struct FunctionCompiler;
static bool compileStatement(FunctionCompiler& ctx, Statement* st, ls_type_kind return_kind, ls_string_view current_label);
static void emitDeferredStatements(FunctionCompiler& ctx, u32 defer_mark, ls_type_kind return_kind, ls_string_view current_label);

struct FunctionCompiler {
	explicit FunctionCompiler(ls_bytecode* bytecode, ls_function_bc& out)
		: bytecode(bytecode)
		, out(out)
		, code(*bytecode->arena)
		, locals(*bytecode->arena)
		, scope_marks(*bytecode->arena)
		, deferreds(*bytecode->arena)
		, defer_marks(*bytecode->arena)
		, loops(*bytecode->arena)
		, ref_local_slots(*bytecode->arena) {}

	ls_bytecode* bytecode = nullptr;
	ls_module* module = nullptr;
	Unit* unit = nullptr;
	ResolvedType* return_type = nullptr;
	ls_function_bc& out;
	ByteArray code;
	ExpArray<BytecodeLocalBinding> locals;
	ExpArray<u32> scope_marks;
	ExpArray<Statement*> deferreds;
	ExpArray<u32> defer_marks;
	ExpArray<GlobalBinding>* globals = nullptr;
	ExpArray<LoopBinding> loops;
	ExpArray<u32> ref_local_slots;
	u32 max_local_count = 0;
	u32 next_local_slot = 0;
	const ExpArray<FunctionInfo>* functions = nullptr;

	bool isRefLocal(const BytecodeLocalBinding& local) const {
		for (u32 slot : ref_local_slots) {
			if (slot == local.slot) return true;
		}
		return false;
	}

	BytecodeLocalBinding* findLocal(ls_string_view name) {
		for (i32 i = (i32)locals.size() - 1; i >= 0; --i) {
			BytecodeLocalBinding& binding = locals[(u32)i];
			if (equalStrings(binding.name, name)) return &binding;
		}
		return nullptr;
	}

	const FunctionInfo* findFunction(ls_string_view name) const {
		if (!functions) return nullptr;
		for (const FunctionInfo& fn : *functions) {
			if (equalStrings(fn.name, name)) return &fn;
		}
		return nullptr;
	}

	u32 addLocal(ls_string_view name, ResolvedType* type, ls_type_kind kind) {
		BytecodeLocalBinding& binding = locals.emplace_back();
		binding.name = name;
		binding.type = type;
		binding.kind = kind;
		binding.slot_count = typeSlotCount(type);
		if (binding.slot_count == 0u) binding.slot_count = 1u;
		binding.slot = next_local_slot;
		next_local_slot += binding.slot_count;
		const u32 local_end = binding.slot + binding.slot_count;
		if (local_end > max_local_count) max_local_count = local_end;
		return binding.slot;
	}

	void pushScope() {
		scope_marks.push((u32)locals.size());
		defer_marks.push((u32)deferreds.size());
	}

	void popScope(ls_type_kind return_kind, ls_string_view current_label) {
		if (scope_marks.empty()) return;
		const u32 mark = scope_marks.back();
		const u32 defer_mark = defer_marks.back();
		emitDeferredStatements(*this, defer_mark, return_kind, current_label);
		while (deferreds.size() > defer_mark) deferreds.pop_back();
		defer_marks.pop_back();
		scope_marks.pop_back();
		while (locals.size() > mark) locals.pop_back();
	}

	LoopBinding* findLoop(ls_string_view label) {
		if (loops.empty()) return nullptr;
		if (label.begin == label.end) return &loops.back();
		for (i32 i = (i32)loops.size() - 1; i >= 0; --i) {
			LoopBinding& loop = loops[(u32)i];
			if (equalStrings(loop.label, label)) return &loop;
		}
		return nullptr;
	}
};

static Unit* findUnitByPath(ls_module& module, ls_string_view path) {
	for (Unit& unit : module.units) {
		if (equalStrings(unit.path, path)) return &unit;
	}
	return nullptr;
}

static Symbol* findSymbolInUnit(ls_module& module, Unit& unit, ls_string_view name) {
	for (Symbol& sym : unit.symbols) {
		if (!equalStrings(sym.name, name)) continue;
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
			if (found && found != &sym) return nullptr;
			found = &sym;
		}
	}
	return found;
}

static u32 typeSlotCount(ResolvedType* type) {
	if (!type) return 1u;
	switch (type->kind) {
		case ResolvedType::VOID:
			return 0u;
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
		case ResolvedType::FUNCTION:
		case ResolvedType::ENUM:
			return 1u;
		case ResolvedType::NULLABLE:
			return 2u;
		case ResolvedType::ARRAY: {
			ArrayResolvedType* arr = static_cast<ArrayResolvedType*>(type);
			return arr->size > 0 ? (u32)(arr->size * typeSlotCount(arr->element_type)) : 1u;
		}
		case ResolvedType::SLICE:
			return 2u;
		case ResolvedType::STRUCT: {
			StructResolvedType* st = static_cast<StructResolvedType*>(type);
			if (!st->decl) return 1u;
			u32 count = 0u;
			for (u32 i = 0; i < st->decl->fields.size(); ++i) {
				ResolvedType* field_type = i < st->field_types.size()
					? st->field_types[i]
					: st->decl->fields[i].resolved_type;
				count += typeSlotCount(field_type);
			}
			return count ? count : 1u;
		}
		default:
			return 1u;
	}
}

static ls_type_kind valueKindForType(ResolvedType* type, ls_type_kind fallback = LS_TYPE_I32) {
	if (!type) return fallback;
	if (type->kind == ResolvedType::ENUM) return LS_TYPE_I32;
	const ls_type_kind kind = toTypeKind(type);
	return kind != LS_TYPE_INVALID ? kind : fallback;
}

static bool enumMemberIndex(EnumResolvedType* en, ls_string_view name, u32& out_index) {
	if (!en || !en->decl) return false;
	for (u32 i = 0; i < en->decl->members.size(); ++i) {
		if (!equalStrings(en->decl->members[i].name, name)) continue;
		out_index = i;
		return true;
	}
	return false;
}

static bool structFieldSlotOffset(StructResolvedType* st, ls_string_view name, u32& out_offset, ResolvedType*& out_type) {
	if (!st || !st->decl) return false;
	u32 offset = 0u;
	for (u32 i = 0; i < st->decl->fields.size(); ++i) {
		NamedDecl& field = st->decl->fields[i];
		ResolvedType* field_type = i < st->field_types.size() ? st->field_types[i] : field.resolved_type;
		if (equalStrings(field.name, name)) {
			out_offset = offset;
			out_type = field_type;
			return true;
		}
		offset += typeSlotCount(field_type);
	}
	return false;
}

static Symbol* findImportedQualifiedSymbol(ls_module& module, Unit& unit, ls_string_view qualifier, ls_string_view name) {
	for (const Import& import : unit.imports) {
		if (!equalStrings(import.alias, qualifier)) continue;
		Unit* imported = findUnitByPath(module, import.path);
		if (!imported) return nullptr;
		for (Symbol& sym : imported->symbols) {
			if (!equalStrings(sym.name, name)) continue;
			return &sym;
		}
	}
	return nullptr;
}

static const FunctionInfo* findFunctionForSymbol(const ExpArray<FunctionInfo>& functions, Symbol* sym) {
	if (!sym || !sym->expression || sym->expression->kind != Expression::FUNCTION) return nullptr;
	FunctionExpression* fn = static_cast<FunctionExpression*>(sym->expression);
	for (const FunctionInfo& info : functions) {
		if (info.fn == fn) return &info;
	}
	return nullptr;
}

static const FunctionInfo* findFunctionForExpression(const ExpArray<FunctionInfo>& functions, FunctionExpression* fn) {
	for (const FunctionInfo& info : functions) {
		if (info.fn == fn) return &info;
	}
	return nullptr;
}

static const FunctionInfo* findMemberFunction(
	const ExpArray<FunctionInfo>& functions,
	ls_string_view name,
	ResolvedType* receiver_type,
	u32 arg_count
) {
	if (!receiver_type) return nullptr;
	for (const FunctionInfo& info : functions) {
		if (!equalStrings(info.name, name)) continue;
		FunctionResolvedType* fn_type = info.type;
		if (!fn_type || fn_type->param_types.size() != arg_count + 1u) continue;
		if (fn_type->param_types.empty() || fn_type->param_types[0] != receiver_type) continue;
		return &info;
	}
	return nullptr;
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
		default:
			return false;
	}
}

static bool operatorLookupArityMatches(u32 arity, u32 expected_arity) {
	return arity == expected_arity;
}

static const FunctionInfo* findOperatorFunction(
	const ExpArray<FunctionInfo>& functions,
	Token::Type op,
	ResolvedType* lhs_type,
	ResolvedType* rhs_type,
	u32 expected_arity
) {
	const FunctionInfo* found = nullptr;
	for (const FunctionInfo& info : functions) {
		if (info.operatorToken() != op) continue;
		const FunctionResolvedType* fn_type = info.type;
		if (!fn_type || !operatorLookupArityMatches((u32)fn_type->param_types.size(), expected_arity)) continue;
		if (fn_type->decl) {
			bool ref_param = false;
			for (const FunctionParam& param : fn_type->decl->runtime_params) {
				if (param.is_ref) {
					ref_param = true;
					break;
				}
			}
			if (ref_param) continue;
		}
		if (!lhs_type || !typesEqual(lhs_type, fn_type->param_types[0])) continue;
		if (expected_arity == 2u && (!rhs_type || !typesEqual(rhs_type, fn_type->param_types[1]))) continue;
		if (found) return nullptr;
		found = &info;
	}
	return found;
}

static ls_type_kind compileValueAsType(FunctionCompiler& ctx, Expression* expr, ResolvedType* expected_type);

static ls_type_kind emitOperatorCall(
	FunctionCompiler& ctx,
	const FunctionInfo* fn,
	Expression* lhs,
	Expression* rhs,
	Token::Type op,
	ls_type_kind hint
) {
	if (!fn || !fn->fn || !fn->type) return LS_TYPE_INVALID;
	const FunctionResolvedType* fn_type = fn->type;
	if (fn_type->param_types.size() == 1u) {
		if (compileValueAsType(ctx, lhs, fn_type->param_types[0]) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
	}
	else {
		if (compileValueAsType(ctx, lhs, fn_type->param_types[0]) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
		if (compileValueAsType(ctx, rhs, fn_type->param_types[1]) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
	}
	emitOp(ctx.code, LS_OP_CALL_DIRECT);
	emitU32(ctx.code, fn->index);
	const ls_type_kind return_hint = hint != LS_TYPE_INVALID ? hint : LS_TYPE_I32;
	return fn_type->return_type ? valueKindForType(fn_type->return_type, return_hint) : return_hint;
}

static void appendStringLiteral(ls_bytecode* bytecode, const ls_string_view& value, u32& out_index) {
	ASSERT(bytecode->arena);
	ls_arena* arena = bytecode->arena;
	ls_string_view* slot = appendArenaArray(arena, bytecode->strings, bytecode->string_count, bytecode->string_capacity);
	ASSERT(slot);
	*slot = value;
	out_index = bytecode->string_count - 1u;
}

static ls_function_bc* appendFunction(ls_bytecode* bytecode) {
	ASSERT(bytecode->arena);
	return appendArenaArray(bytecode->arena, bytecode->functions, bytecode->function_count, bytecode->function_capacity);
}

static void emitLoadLocalSlots(FunctionCompiler& ctx, u32 slot, u32 slot_count) {
	for (u32 i = 0; i < slot_count; ++i) {
		emitOp(ctx.code, LS_OP_LOAD_LOCAL);
		emitU32(ctx.code, slot + i);
	}
}

static void emitStoreLocalSlots(FunctionCompiler& ctx, u32 slot, u32 slot_count) {
	for (u32 i = 0; i < slot_count; ++i) {
		const u32 store_slot = slot + (slot_count - 1u - i);
		emitOp(ctx.code, LS_OP_STORE_LOCAL);
		emitU32(ctx.code, store_slot);
	}
}

static void emitLoadGlobalSlots(FunctionCompiler& ctx, u32 slot, u32 slot_count) {
	for (u32 i = 0; i < slot_count; ++i) {
		emitOp(ctx.code, LS_OP_LOAD_GLOBAL);
		emitU32(ctx.code, slot + i);
	}
}

static void emitStoreGlobalSlots(FunctionCompiler& ctx, u32 slot, u32 slot_count) {
	for (u32 i = 0; i < slot_count; ++i) {
		const u32 store_slot = slot + (slot_count - 1u - i);
		emitOp(ctx.code, LS_OP_STORE_GLOBAL);
		emitU32(ctx.code, store_slot);
	}
}

static const GlobalBinding* findGlobalBinding(const ExpArray<GlobalBinding>& globals, Symbol* sym);
static ls_type_kind compileExpression(FunctionCompiler& ctx, Expression* expr, ls_type_kind hint);
static bool emitReference(FunctionCompiler& ctx, Expression* expr);

static void emitZeroIndex(FunctionCompiler& ctx) {
	emitOp(ctx.code, LS_OP_LOAD_CONST_8);
	emitU64(ctx.code, 0u);
}

static bool emitReference(FunctionCompiler& ctx, Expression* expr) {
	if (!expr) return false;
	switch (expr->kind) {
		case Expression::IDENTIFIER: {
			IdentifierExpression* id = static_cast<IdentifierExpression*>(expr);
			if (BytecodeLocalBinding* local = ctx.findLocal(id->name)) {
				if (ctx.isRefLocal(*local)) {
					emitLoadLocalSlots(ctx, local->slot, 1u);
				}
				else {
					emitOp(ctx.code, LS_OP_LOCAL_REF);
					emitU32(ctx.code, local->slot);
				}
				return true;
			}

			Symbol* global_sym = id->symbol;
			if (!global_sym && ctx.module && ctx.unit) {
				global_sym = findSymbolInUnit(*ctx.module, *ctx.unit, id->name);
				if (!global_sym) global_sym = findImportedSymbol(*ctx.module, *ctx.unit, id->name);
			}
			if (global_sym && ctx.globals) {
				if (const GlobalBinding* global = findGlobalBinding(*ctx.globals, global_sym)) {
					emitOp(ctx.code, LS_OP_GLOBAL_REF);
					emitU32(ctx.code, global->slot);
					return true;
				}
			}
			return false;
		}
		case Expression::MEMBER: {
			MemberExpression* member = static_cast<MemberExpression*>(expr);
			if (member->expression && member->expression->kind == Expression::IDENTIFIER
				&& ctx.module && ctx.unit && ctx.globals) {
				IdentifierExpression* qualifier = static_cast<IdentifierExpression*>(member->expression);
				Symbol* symbol = findImportedQualifiedSymbol(*ctx.module, *ctx.unit, qualifier->name, member->name);
				if (symbol) {
					if (const GlobalBinding* global = findGlobalBinding(*ctx.globals, symbol)) {
						emitOp(ctx.code, LS_OP_GLOBAL_REF);
						emitU32(ctx.code, global->slot);
						return true;
					}
				}
			}
			if (!member->expression || !member->expression->resolved_type) return false;
			if (member->expression->resolved_type->kind != ResolvedType::STRUCT) return false;
			StructResolvedType* st = static_cast<StructResolvedType*>(member->expression->resolved_type);
			u32 offset = 0u;
			ResolvedType* field_type = nullptr;
			if (!structFieldSlotOffset(st, member->name, offset, field_type)) return false;
			if (!emitReference(ctx, member->expression)) return false;
			emitZeroIndex(ctx);
			emitOp(ctx.code, LS_OP_REF_AT);
			emitU32(ctx.code, 1u);
			emitI32(ctx.code, (i32)offset);
			return true;
		}
		case Expression::BRACKET: {
			BracketExpression* br = static_cast<BracketExpression*>(expr);
			if (br->has_colon || br->args.size() != 1u || !br->resolved_type) return false;
			if (!emitReference(ctx, br->base)) return false;
			if (compileExpression(ctx, br->args[0], LS_TYPE_I32) == LS_TYPE_INVALID) return false;
			emitOp(ctx.code, LS_OP_REF_AT);
			emitU32(ctx.code, typeSlotCount(br->resolved_type));
			emitI32(ctx.code, 0);
			return true;
		}
		default:
			return false;
	}
}

static bool emitReferenceLoad(FunctionCompiler& ctx, Expression* expr, u32 slot_count) {
	if (slot_count == 0u) slot_count = 1u;
	const u32 ref_slot = ctx.addLocal({}, nullptr, LS_TYPE_I64);
	if (!emitReference(ctx, expr)) return false;
	emitStoreLocalSlots(ctx, ref_slot, 1u);
	for (u32 i = 0; i < slot_count; ++i) {
		emitLoadLocalSlots(ctx, ref_slot, 1u);
		emitZeroIndex(ctx);
		emitOp(ctx.code, LS_OP_LOAD_AT);
		emitU32(ctx.code, 1u);
		emitI32(ctx.code, (i32)i);
	}
	return true;
}

static bool emitArrayBaseRef(FunctionCompiler& ctx, Expression* base) {
	return emitReference(ctx, base);
}

static ls_type_kind compileValueAsType(FunctionCompiler& ctx, Expression* expr, ResolvedType* expected_type) {
	// Slice conversions change representation, not just the reported type. Arrays
	// are inline values, whereas slices are an absolute backing reference and length.
	if (expected_type && expected_type->kind == ResolvedType::SLICE) {
		if (expr && expr->kind == Expression::NULL_LITERAL) {
			emitOp(ctx.code, LS_OP_LOAD_CONST_8);
			emitU64(ctx.code, 0u);
			emitOp(ctx.code, LS_OP_LOAD_CONST_8);
			emitU64(ctx.code, 0u);
			return LS_TYPE_SLICE;
		}
		if (expr && expr->resolved_type && expr->resolved_type->kind == ResolvedType::ARRAY) {
			ArrayResolvedType* array = static_cast<ArrayResolvedType*>(expr->resolved_type);
			if (!emitReference(ctx, expr)) return LS_TYPE_INVALID;
			emitOp(ctx.code, LS_OP_LOAD_CONST_8);
			emitU64(ctx.code, (u64)array->size);
			return LS_TYPE_SLICE;
		}
	}
	return compileExpression(ctx, expr, valueKindForType(expected_type, LS_TYPE_INVALID));
}

static bool emitNumericStoreOp(FunctionCompiler& ctx, ls_type_kind kind, Token::Type op) {
	switch (op) {
		case Token::PLUS_EQUAL:
			switch (kind) {
				case LS_TYPE_I8:  emitOp(ctx.code, LS_OP_ADD_I8);  return true;
				case LS_TYPE_I16: emitOp(ctx.code, LS_OP_ADD_I16); return true;
				case LS_TYPE_I32: emitOp(ctx.code, LS_OP_ADD_I32); return true;
				case LS_TYPE_I64: emitOp(ctx.code, LS_OP_ADD_I64); return true;
				case LS_TYPE_U8:  emitOp(ctx.code, LS_OP_ADD_U8);  return true;
				case LS_TYPE_U16: emitOp(ctx.code, LS_OP_ADD_U16); return true;
				case LS_TYPE_U32: emitOp(ctx.code, LS_OP_ADD_U32); return true;
				case LS_TYPE_U64: emitOp(ctx.code, LS_OP_ADD_U64); return true;
				case LS_TYPE_F32: emitOp(ctx.code, LS_OP_ADD_F32); return true;
				case LS_TYPE_F64: emitOp(ctx.code, LS_OP_ADD_F64); return true;
				default: return false;
			}
		case Token::MINUS_EQUAL:
			switch (kind) {
				case LS_TYPE_I8:  emitOp(ctx.code, LS_OP_SUB_I8);  return true;
				case LS_TYPE_I16: emitOp(ctx.code, LS_OP_SUB_I16); return true;
				case LS_TYPE_I32: emitOp(ctx.code, LS_OP_SUB_I32); return true;
				case LS_TYPE_I64: emitOp(ctx.code, LS_OP_SUB_I64); return true;
				case LS_TYPE_U8:  emitOp(ctx.code, LS_OP_SUB_U8);  return true;
				case LS_TYPE_U16: emitOp(ctx.code, LS_OP_SUB_U16); return true;
				case LS_TYPE_U32: emitOp(ctx.code, LS_OP_SUB_U32); return true;
				case LS_TYPE_U64: emitOp(ctx.code, LS_OP_SUB_U64); return true;
				case LS_TYPE_F32: emitOp(ctx.code, LS_OP_SUB_F32); return true;
				case LS_TYPE_F64: emitOp(ctx.code, LS_OP_SUB_F64); return true;
				default: return false;
			}
		case Token::STAR_EQUAL:
			switch (kind) {
				case LS_TYPE_I8:  emitOp(ctx.code, LS_OP_MUL_I8);  return true;
				case LS_TYPE_I16: emitOp(ctx.code, LS_OP_MUL_I16); return true;
				case LS_TYPE_I32: emitOp(ctx.code, LS_OP_MUL_I32); return true;
				case LS_TYPE_I64: emitOp(ctx.code, LS_OP_MUL_I64); return true;
				case LS_TYPE_U8:  emitOp(ctx.code, LS_OP_MUL_U8);  return true;
				case LS_TYPE_U16: emitOp(ctx.code, LS_OP_MUL_U16); return true;
				case LS_TYPE_U32: emitOp(ctx.code, LS_OP_MUL_U32); return true;
				case LS_TYPE_U64: emitOp(ctx.code, LS_OP_MUL_U64); return true;
				case LS_TYPE_F32: emitOp(ctx.code, LS_OP_MUL_F32); return true;
				case LS_TYPE_F64: emitOp(ctx.code, LS_OP_MUL_F64); return true;
				default: return false;
			}
		case Token::SLASH_EQUAL:
			switch (kind) {
				case LS_TYPE_I8:  emitOp(ctx.code, LS_OP_DIV_I8);  return true;
				case LS_TYPE_I16: emitOp(ctx.code, LS_OP_DIV_I16); return true;
				case LS_TYPE_I32: emitOp(ctx.code, LS_OP_DIV_I32); return true;
				case LS_TYPE_I64: emitOp(ctx.code, LS_OP_DIV_I64); return true;
				case LS_TYPE_U8:  emitOp(ctx.code, LS_OP_DIV_U8);  return true;
				case LS_TYPE_U16: emitOp(ctx.code, LS_OP_DIV_U16); return true;
				case LS_TYPE_U32: emitOp(ctx.code, LS_OP_DIV_U32); return true;
				case LS_TYPE_U64: emitOp(ctx.code, LS_OP_DIV_U64); return true;
				case LS_TYPE_F32: emitOp(ctx.code, LS_OP_DIV_F32); return true;
				case LS_TYPE_F64: emitOp(ctx.code, LS_OP_DIV_F64); return true;
				default: return false;
			}
		default:
			return false;
	}
}

static bool emitBracketLoad(FunctionCompiler& ctx, BracketExpression* br) {
	if (!br || br->has_colon || br->args.size() != 1) return false;
	const u32 element_slots = typeSlotCount(br->resolved_type);
	if (element_slots != 1u) return false;
	if (br->base->resolved_type && br->base->resolved_type->kind == ResolvedType::SLICE) {
		if (compileExpression(ctx, br->base, LS_TYPE_SLICE) == LS_TYPE_INVALID) return false;
		if (compileExpression(ctx, br->args[0], LS_TYPE_I32) == LS_TYPE_INVALID) return false;
		emitOp(ctx.code, LS_OP_SLICE_LOAD);
		emitU32(ctx.code, element_slots);
		return true;
	}
	if (!emitArrayBaseRef(ctx, br->base)) return false;
	if (compileExpression(ctx, br->args[0], LS_TYPE_I32) == LS_TYPE_INVALID) return false;
	emitOp(ctx.code, LS_OP_LOAD_AT);
	emitU32(ctx.code, element_slots);
	emitI32(ctx.code, 0);
	return true;
}

static bool emitBracketStore(FunctionCompiler& ctx, BracketExpression* br, Expression* rhs, ls_type_kind value_kind, Token::Type op) {
	if (!br || br->has_colon || br->args.size() != 1) return false;
	const u32 element_slots = typeSlotCount(br->resolved_type);
	if (element_slots != 1u) return false;
	const bool is_slice = br->base->resolved_type && br->base->resolved_type->kind == ResolvedType::SLICE;

	if (op == Token::EQUAL) {
		if (is_slice) {
			if (compileExpression(ctx, br->base, LS_TYPE_SLICE) == LS_TYPE_INVALID) return false;
		}
		else if (!emitArrayBaseRef(ctx, br->base)) return false;
		if (compileExpression(ctx, br->args[0], LS_TYPE_I32) == LS_TYPE_INVALID) return false;
		if (compileExpression(ctx, rhs, value_kind) == LS_TYPE_INVALID) return false;
		emitOp(ctx.code, is_slice ? LS_OP_SLICE_STORE : LS_OP_STORE_AT);
		emitU32(ctx.code, element_slots);
		if (!is_slice) emitI32(ctx.code, 0);
		return true;
	}

	if (is_slice) {
		// Evaluate the base and index once. Compound assignment needs them for both
		// the checked load and checked store, and either expression may have effects.
		const u32 slice_slot = ctx.addLocal({}, br->base->resolved_type, LS_TYPE_SLICE);
		const u32 index_slot = ctx.addLocal({}, nullptr, LS_TYPE_I32);
		const u32 value_slot = ctx.addLocal({}, br->resolved_type, value_kind);
		if (compileExpression(ctx, br->base, LS_TYPE_SLICE) == LS_TYPE_INVALID) return false;
		emitStoreLocalSlots(ctx, slice_slot, 2u);
		if (compileExpression(ctx, br->args[0], LS_TYPE_I32) == LS_TYPE_INVALID) return false;
		emitStoreLocalSlots(ctx, index_slot, 1u);
		emitLoadLocalSlots(ctx, slice_slot, 2u);
		emitLoadLocalSlots(ctx, index_slot, 1u);
		emitOp(ctx.code, LS_OP_SLICE_LOAD);
		emitU32(ctx.code, element_slots);
		if (compileExpression(ctx, rhs, value_kind) == LS_TYPE_INVALID) return false;
		if (!emitNumericStoreOp(ctx, value_kind, op)) return false;
		emitStoreLocalSlots(ctx, value_slot, 1u);
		emitLoadLocalSlots(ctx, slice_slot, 2u);
		emitLoadLocalSlots(ctx, index_slot, 1u);
		emitLoadLocalSlots(ctx, value_slot, 1u);
		emitOp(ctx.code, LS_OP_SLICE_STORE);
		emitU32(ctx.code, element_slots);
		return true;
	}

	const u32 ref_slot = ctx.addLocal({}, nullptr, LS_TYPE_I64);
	const u32 index_slot = ctx.addLocal({}, nullptr, LS_TYPE_I32);

	if (!emitArrayBaseRef(ctx, br->base)) return false;
	emitStoreLocalSlots(ctx, ref_slot, 1u);
	if (compileExpression(ctx, br->args[0], LS_TYPE_I32) == LS_TYPE_INVALID) return false;
	emitStoreLocalSlots(ctx, index_slot, 1u);

	emitLoadLocalSlots(ctx, ref_slot, 1u);
	emitLoadLocalSlots(ctx, index_slot, 1u);
	emitOp(ctx.code, LS_OP_LOAD_AT);
	emitU32(ctx.code, element_slots);
	emitI32(ctx.code, 0);

	if (compileExpression(ctx, rhs, value_kind) == LS_TYPE_INVALID) return false;
	if (!emitNumericStoreOp(ctx, value_kind, op)) return false;

	emitLoadLocalSlots(ctx, ref_slot, 1u);
	emitLoadLocalSlots(ctx, index_slot, 1u);
	emitOp(ctx.code, LS_OP_STORE_AT);
	emitU32(ctx.code, element_slots);
	emitI32(ctx.code, 0);
	return true;
}

static bool emitSlice(FunctionCompiler& ctx, BracketExpression* br) {
	if (!br || !br->has_colon || !br->base || !br->base->resolved_type) return false;
	ResolvedType* base_type = br->base->resolved_type;
	ResolvedType* element_type = nullptr;
	if (base_type->kind == ResolvedType::ARRAY) {
		ArrayResolvedType* array = static_cast<ArrayResolvedType*>(base_type);
		element_type = array->element_type;
		if (!emitReference(ctx, br->base)) return false;
		emitOp(ctx.code, LS_OP_LOAD_CONST_8);
		emitU64(ctx.code, (u64)array->size);
	}
	else if (base_type->kind == ResolvedType::SLICE) {
		element_type = static_cast<SliceResolvedType*>(base_type)->element_type;
		if (compileExpression(ctx, br->base, LS_TYPE_SLICE) == LS_TYPE_INVALID) return false;
	}
	else {
		return false;
	}

	// Save the source pair because an omitted end bound reuses its dynamic length,
	// while explicit bounds may themselves evaluate arbitrary expressions.
	const u32 source_slot = ctx.addLocal({}, br->resolved_type, LS_TYPE_SLICE);
	emitStoreLocalSlots(ctx, source_slot, 2u);
	emitLoadLocalSlots(ctx, source_slot, 2u);
	if (!br->args.empty()) {
		if (compileExpression(ctx, br->args[0], LS_TYPE_I32) == LS_TYPE_INVALID) return false;
	}
	else {
		emitZeroIndex(ctx);
	}
	if (br->end) {
		if (compileExpression(ctx, br->end, LS_TYPE_I32) == LS_TYPE_INVALID) return false;
	}
	else {
		emitLoadLocalSlots(ctx, source_slot + 1u, 1u);
	}
	emitOp(ctx.code, LS_OP_SLICE);
	emitU32(ctx.code, typeSlotCount(element_type));
	return true;
}

template <typename Array>
static void patchJumpRelative(Array& code, u32 operand_pos, u32 target_pos) {
	patchI32(code, operand_pos, (i32)((i32)target_pos - (i32)(operand_pos + 4u)));
}

static u32 emitJumpPlaceholder(FunctionCompiler& ctx, ls_op op) {
	const u32 operand_pos = (u32)ctx.code.size() + 1u;
	emitOp(ctx.code, op);
	emitI32(ctx.code, 0);
	return operand_pos;
}

static void emitDeferredStatements(FunctionCompiler& ctx, u32 defer_mark, ls_type_kind return_kind, ls_string_view current_label) {
	for (i32 i = (i32)ctx.deferreds.size() - 1; i >= (i32)defer_mark; --i) {
		(void)compileStatement(ctx, ctx.deferreds[(u32)i], return_kind, current_label);
	}
}

static const GlobalBinding* findGlobalBinding(const ExpArray<GlobalBinding>& globals, Symbol* sym) {
	if (!sym) return nullptr;
	for (const GlobalBinding& binding : globals) {
		if (binding.sym == sym) return &binding;
	}
	return nullptr;
}

static ls_type_kind compileExpression(FunctionCompiler& ctx, Expression* expr, ls_type_kind hint);

static ls_type_kind compileBinary(FunctionCompiler& ctx, BinaryExpression* expr, ls_type_kind hint) {
	if (expr->op == Token::AND || expr->op == Token::OR) {
		if (compileExpression(ctx, expr->lhs, LS_TYPE_BOOL) != LS_TYPE_BOOL) return LS_TYPE_INVALID;
		const ls_op short_circuit_op = expr->op == Token::AND ? LS_OP_JUMP_IF_FALSE : LS_OP_JUMP_IF_TRUE;
		const u32 short_circuit_jump = emitJumpPlaceholder(ctx, short_circuit_op);

		if (compileExpression(ctx, expr->rhs, LS_TYPE_BOOL) != LS_TYPE_BOOL) return LS_TYPE_INVALID;
		const u32 end_jump = emitJumpPlaceholder(ctx, LS_OP_JUMP);

		patchJumpRelative(ctx.code, short_circuit_jump, (u32)ctx.code.size());
		emitOp(ctx.code, LS_OP_LOAD_CONST_8);
		emitU64(ctx.code, expr->op == Token::AND ? 0u : 1u);
		patchJumpRelative(ctx.code, end_jump, (u32)ctx.code.size());
		return LS_TYPE_BOOL;
	}

	if (const FunctionInfo* fn = findOperatorFunction(*ctx.functions, expr->op, expr->lhs ? expr->lhs->resolved_type : nullptr, expr->rhs ? expr->rhs->resolved_type : nullptr, 2u)) {
		return emitOperatorCall(ctx, fn, expr->lhs, expr->rhs, expr->op, hint);
	}

	// Null check: `nullable == null` or `nullable != null` — only check has_value slot.
	if (expr->op == Token::EQUAL_EQUAL || expr->op == Token::BANG_EQUAL) {
		Expression* nullable_side = nullptr;
		if (expr->rhs && expr->rhs->kind == Expression::NULL_LITERAL) nullable_side = expr->lhs;
		else if (expr->lhs && expr->lhs->kind == Expression::NULL_LITERAL) nullable_side = expr->rhs;
		if (nullable_side && nullable_side->resolved_type && nullable_side->resolved_type->kind == ResolvedType::NULLABLE) {
			// Compile nullable (pushes has_value, value); pop value, compare has_value to 0.
			if (compileExpression(ctx, nullable_side, LS_TYPE_NULL_VALUE) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
			emitOp(ctx.code, LS_OP_POP); // discard value slot, has_value remains
			emitOp(ctx.code, LS_OP_LOAD_CONST_8);
			emitU64(ctx.code, 0u);
			emitOp(ctx.code, expr->op == Token::EQUAL_EQUAL ? LS_OP_EQ : LS_OP_NE);
			emitU8(ctx.code, (u8)LS_TYPE_U64);
			return LS_TYPE_BOOL;
		}
	}

	if (const FunctionInfo* fn = findOperatorFunction(*ctx.functions, expr->op, expr->lhs ? expr->lhs->resolved_type : nullptr, expr->rhs ? expr->rhs->resolved_type : nullptr, 2u)) {
		return emitOperatorCall(ctx, fn, expr->lhs, expr->rhs, expr->op, hint);
	}

	const ls_type_kind lhs_hint = numericKindForOp(defaultLiteralKind(expr->lhs, hint), hint);
	const ls_type_kind rhs_hint = numericKindForOp(defaultLiteralKind(expr->rhs, lhs_hint), lhs_hint);
	ls_type_kind lhs_kind = compileExpression(ctx, expr->lhs, lhs_hint);
	ls_type_kind rhs_kind = compileExpression(ctx, expr->rhs, rhs_hint);
	if (lhs_kind == LS_TYPE_INVALID) lhs_kind = rhs_kind;
	const ls_type_kind kind = numericKindForOp(lhs_kind, rhs_kind);

	switch (expr->op) {
		case Token::PLUS:
			switch (kind) {
				case LS_TYPE_I8:  emitOp(ctx.code, LS_OP_ADD_I8);  break;
				case LS_TYPE_I16: emitOp(ctx.code, LS_OP_ADD_I16); break;
				case LS_TYPE_I32: emitOp(ctx.code, LS_OP_ADD_I32); break;
				case LS_TYPE_I64: emitOp(ctx.code, LS_OP_ADD_I64); break;
				case LS_TYPE_U8:  emitOp(ctx.code, LS_OP_ADD_U8);  break;
				case LS_TYPE_U16: emitOp(ctx.code, LS_OP_ADD_U16); break;
				case LS_TYPE_U32: emitOp(ctx.code, LS_OP_ADD_U32); break;
				case LS_TYPE_U64: emitOp(ctx.code, LS_OP_ADD_U64); break;
				case LS_TYPE_F32: emitOp(ctx.code, LS_OP_ADD_F32); break;
				case LS_TYPE_F64: emitOp(ctx.code, LS_OP_ADD_F64); break;
				default: return LS_TYPE_INVALID;
			}
			return kind;
		case Token::MINUS:
			switch (kind) {
				case LS_TYPE_I8:  emitOp(ctx.code, LS_OP_SUB_I8);  break;
				case LS_TYPE_I16: emitOp(ctx.code, LS_OP_SUB_I16); break;
				case LS_TYPE_I32: emitOp(ctx.code, LS_OP_SUB_I32); break;
				case LS_TYPE_I64: emitOp(ctx.code, LS_OP_SUB_I64); break;
				case LS_TYPE_U8:  emitOp(ctx.code, LS_OP_SUB_U8);  break;
				case LS_TYPE_U16: emitOp(ctx.code, LS_OP_SUB_U16); break;
				case LS_TYPE_U32: emitOp(ctx.code, LS_OP_SUB_U32); break;
				case LS_TYPE_U64: emitOp(ctx.code, LS_OP_SUB_U64); break;
				case LS_TYPE_F32: emitOp(ctx.code, LS_OP_SUB_F32); break;
				case LS_TYPE_F64: emitOp(ctx.code, LS_OP_SUB_F64); break;
				default: return LS_TYPE_INVALID;
			}
			return kind;
		case Token::STAR:
			switch (kind) {
				case LS_TYPE_I8:  emitOp(ctx.code, LS_OP_MUL_I8);  break;
				case LS_TYPE_I16: emitOp(ctx.code, LS_OP_MUL_I16); break;
				case LS_TYPE_I32: emitOp(ctx.code, LS_OP_MUL_I32); break;
				case LS_TYPE_I64: emitOp(ctx.code, LS_OP_MUL_I64); break;
				case LS_TYPE_U8:  emitOp(ctx.code, LS_OP_MUL_U8);  break;
				case LS_TYPE_U16: emitOp(ctx.code, LS_OP_MUL_U16); break;
				case LS_TYPE_U32: emitOp(ctx.code, LS_OP_MUL_U32); break;
				case LS_TYPE_U64: emitOp(ctx.code, LS_OP_MUL_U64); break;
				case LS_TYPE_F32: emitOp(ctx.code, LS_OP_MUL_F32); break;
				case LS_TYPE_F64: emitOp(ctx.code, LS_OP_MUL_F64); break;
				default: return LS_TYPE_INVALID;
			}
			return kind;
		case Token::SLASH:
			switch (kind) {
				case LS_TYPE_I8:  emitOp(ctx.code, LS_OP_DIV_I8);  break;
				case LS_TYPE_I16: emitOp(ctx.code, LS_OP_DIV_I16); break;
				case LS_TYPE_I32: emitOp(ctx.code, LS_OP_DIV_I32); break;
				case LS_TYPE_I64: emitOp(ctx.code, LS_OP_DIV_I64); break;
				case LS_TYPE_U8:  emitOp(ctx.code, LS_OP_DIV_U8);  break;
				case LS_TYPE_U16: emitOp(ctx.code, LS_OP_DIV_U16); break;
				case LS_TYPE_U32: emitOp(ctx.code, LS_OP_DIV_U32); break;
				case LS_TYPE_U64: emitOp(ctx.code, LS_OP_DIV_U64); break;
				case LS_TYPE_F32: emitOp(ctx.code, LS_OP_DIV_F32); break;
				case LS_TYPE_F64: emitOp(ctx.code, LS_OP_DIV_F64); break;
				default: return LS_TYPE_INVALID;
			}
			return kind;
		case Token::PERCENT:
			switch (kind) {
				case LS_TYPE_I8:  emitOp(ctx.code, LS_OP_MOD_I8);  break;
				case LS_TYPE_I16: emitOp(ctx.code, LS_OP_MOD_I16); break;
				case LS_TYPE_I32: emitOp(ctx.code, LS_OP_MOD_I32); break;
				case LS_TYPE_I64: emitOp(ctx.code, LS_OP_MOD_I64); break;
				case LS_TYPE_U8:  emitOp(ctx.code, LS_OP_MOD_U8);  break;
				case LS_TYPE_U16: emitOp(ctx.code, LS_OP_MOD_U16); break;
				case LS_TYPE_U32: emitOp(ctx.code, LS_OP_MOD_U32); break;
				case LS_TYPE_U64: emitOp(ctx.code, LS_OP_MOD_U64); break;
				default: return LS_TYPE_INVALID;
			}
			return kind;
		case Token::EQUAL_EQUAL:
		case Token::BANG_EQUAL:
		case Token::LT:
		case Token::LT_EQUAL:
		case Token::GT:
		case Token::GT_EQUAL:
		{
			const ls_type_kind cmp_kind = lhs_kind != LS_TYPE_INVALID ? lhs_kind : rhs_kind;
			// Comparisons always produce a boolean. Runtime handles the exact kind.
			if (expr->op == Token::EQUAL_EQUAL || expr->op == Token::BANG_EQUAL) {
				switch (cmp_kind) {
					case LS_TYPE_STRING:
					case LS_TYPE_I8:
					case LS_TYPE_U8:
					case LS_TYPE_BOOL:
					case LS_TYPE_I16:
					case LS_TYPE_U16:
					case LS_TYPE_I32:
					case LS_TYPE_U32:
					case LS_TYPE_I64:
					case LS_TYPE_U64:
					case LS_TYPE_F32:
					case LS_TYPE_F64:
						break;
					default:
						return LS_TYPE_INVALID;
				}
			}
			else {
				switch (cmp_kind) {
					case LS_TYPE_I8:
					case LS_TYPE_U8:
					case LS_TYPE_BOOL:
					case LS_TYPE_I16:
					case LS_TYPE_U16:
					case LS_TYPE_I32:
					case LS_TYPE_U32:
					case LS_TYPE_I64:
					case LS_TYPE_U64:
					case LS_TYPE_F32:
					case LS_TYPE_F64:
						break;
					default:
						return LS_TYPE_INVALID;
				}
			}
			switch (expr->op) {
				case Token::EQUAL_EQUAL: emitOp(ctx.code, LS_OP_EQ); break;
				case Token::BANG_EQUAL: emitOp(ctx.code, LS_OP_NE); break;
				case Token::LT: emitOp(ctx.code, LS_OP_LT); break;
				case Token::LT_EQUAL: emitOp(ctx.code, LS_OP_LE); break;
				case Token::GT: emitOp(ctx.code, LS_OP_GT); break;
				case Token::GT_EQUAL: emitOp(ctx.code, LS_OP_GE); break;
				default: return LS_TYPE_INVALID;
			}
			emitU8(ctx.code, (u8)cmp_kind);
			return LS_TYPE_BOOL;
		}
		default:
			return LS_TYPE_INVALID;
	}
}

static ls_type_kind compileCall(FunctionCompiler& ctx, CallExpression* expr, ls_type_kind hint) {
	auto compileArgs = [&](const FunctionResolvedType* fn_type, u32 arg_offset) -> bool {
		for (u32 i = 0; i < expr->args.size(); ++i) {
			const u32 param_index = arg_offset + i;
			ResolvedType* param_type = fn_type && param_index < fn_type->param_types.size()
				? fn_type->param_types[param_index]
				: nullptr;
			const bool is_ref = fn_type
				&& fn_type->decl
				&& param_index < fn_type->decl->runtime_params.size()
				&& fn_type->decl->runtime_params[param_index].is_ref;
			if (is_ref) {
				Expression* arg = expr->args[i];
				if (!arg || arg->kind != Expression::UNARY) return false;
				UnaryExpression* un = static_cast<UnaryExpression*>(arg);
				if (un->op != Token::REF || !emitReference(ctx, un->expression)) return false;
				continue;
			}
			if (compileValueAsType(ctx, expr->args[i], param_type) == LS_TYPE_INVALID) return false;
		}
		return true;
	};

	if (expr->callee && expr->callee->kind == Expression::IDENTIFIER) {
		IdentifierExpression* id = static_cast<IdentifierExpression*>(expr->callee);
		if (equalStrings(id->name, makeStringView("length"))) {
			if (expr->args.size() != 1u || !expr->args[0]->resolved_type) return LS_TYPE_INVALID;
			ResolvedType* arg_type = expr->args[0]->resolved_type;
			if (arg_type->kind == ResolvedType::ARRAY) {
				emitOp(ctx.code, LS_OP_LOAD_CONST_8);
				emitU64(ctx.code, (u64)static_cast<ArrayResolvedType*>(arg_type)->size);
			}
			else if (arg_type->kind == ResolvedType::SLICE) {
				if (compileExpression(ctx, expr->args[0], LS_TYPE_SLICE) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
				emitOp(ctx.code, LS_OP_SLICE_LENGTH);
			}
			else {
				return LS_TYPE_INVALID;
			}
			return LS_TYPE_I32;
		}
		if (BytecodeLocalBinding* local = ctx.findLocal(id->name)) {
			if (local->kind != LS_TYPE_FUNCTION) return LS_TYPE_INVALID;
		}
		else {
			const FunctionInfo* fn = id->symbol
				? findFunctionForSymbol(*ctx.functions, id->symbol)
				: ctx.findFunction(id->name);
			if (!fn) return LS_TYPE_INVALID;
			const FunctionResolvedType* fn_type = fn->type;
			if (!compileArgs(fn_type, 0u)) return LS_TYPE_INVALID;
			emitOp(ctx.code, LS_OP_CALL_DIRECT);
			emitU32(ctx.code, fn->index);
			const ls_type_kind return_hint = hint != LS_TYPE_INVALID ? hint : LS_TYPE_I32;
			return fn_type && fn_type->return_type ? valueKindForType(fn_type->return_type, return_hint) : return_hint;
		}
	}

	if (expr->callee && expr->callee->kind == Expression::MEMBER) {
		MemberExpression* member = static_cast<MemberExpression*>(expr->callee);
		if (member->expression && member->expression->kind == Expression::IDENTIFIER && ctx.module && ctx.unit) {
			IdentifierExpression* base = static_cast<IdentifierExpression*>(member->expression);
			Symbol* sym = findImportedQualifiedSymbol(*ctx.module, *ctx.unit, base->name, member->name);
			if (sym && sym->expression && sym->expression->kind == Expression::FUNCTION) {
				const FunctionInfo* fn = findFunctionForSymbol(*ctx.functions, sym);
				const FunctionResolvedType* fn_type = fn ? fn->type : nullptr;
				if (fn && fn_type) {
					if (!compileArgs(fn_type, 0u)) return LS_TYPE_INVALID;
					emitOp(ctx.code, LS_OP_CALL_DIRECT);
					emitU32(ctx.code, fn->index);
					const ls_type_kind return_hint = hint != LS_TYPE_INVALID ? hint : LS_TYPE_I32;
					return fn_type->return_type ? valueKindForType(fn_type->return_type, return_hint) : return_hint;
				}
			}
		}
		if (member->expression && member->expression->resolved_type && ctx.functions) {
			// Use the specific function chosen by the type checker when available (avoids
			// picking the wrong overload when a local function shadows a namespace function).
			const FunctionInfo* fn = expr->ufcs_fn
				? findFunctionForExpression(*ctx.functions, expr->ufcs_fn)
				: findMemberFunction(*ctx.functions, member->name, member->expression->resolved_type, (u32)expr->args.size());
			if (fn) {
				const FunctionResolvedType* fn_type = fn->type;
				if (compileExpression(ctx, member->expression, fn_type && !fn_type->param_types.empty() ? valueKindForType(fn_type->param_types[0], LS_TYPE_INVALID) : LS_TYPE_INVALID) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
				if (!compileArgs(fn_type, 1u)) return LS_TYPE_INVALID;
				emitOp(ctx.code, LS_OP_CALL_DIRECT);
				emitU32(ctx.code, fn->index);
				const ls_type_kind return_hint = hint != LS_TYPE_INVALID ? hint : LS_TYPE_I32;
				return fn_type && fn_type->return_type ? valueKindForType(fn_type->return_type, return_hint) : return_hint;
			}
		}
	}

	// Indirect call: callee value sits below the argument list.
	if (compileExpression(ctx, expr->callee, LS_TYPE_FUNCTION) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
	const FunctionResolvedType* fn_type = expr->callee && expr->callee->resolved_type
		&& expr->callee->resolved_type->kind == ResolvedType::FUNCTION
		? static_cast<FunctionResolvedType*>(expr->callee->resolved_type)
		: nullptr;
	if (!fn_type || !compileArgs(fn_type, 0u)) return LS_TYPE_INVALID;
	u32 arg_slot_count = 0;
	for (u32 i = 0; i < fn_type->param_types.size(); ++i) {
		const bool is_ref = fn_type->decl
			&& i < fn_type->decl->runtime_params.size()
			&& fn_type->decl->runtime_params[i].is_ref;
		const u32 slot_count = is_ref ? 1u : typeSlotCount(fn_type->param_types[i]);
		arg_slot_count += slot_count == 0u ? 1u : slot_count;
	}
	emitOp(ctx.code, LS_OP_CALL_INDIRECT);
	emitU32(ctx.code, arg_slot_count);
	return fn_type->return_type
		? valueKindForType(fn_type->return_type, hint != LS_TYPE_INVALID ? hint : LS_TYPE_I32)
		: hint;
}

static ls_type_kind compileExpression(FunctionCompiler& ctx, Expression* expr, ls_type_kind hint) {
	if (!expr) return LS_TYPE_VOID;
	switch (expr->kind) {
		case Expression::INT_LITERAL: {
			const ls_type_kind kind = expr->resolved_type ? toTypeKind(expr->resolved_type) : defaultLiteralKind(expr, hint);
			const i64 int_value = static_cast<IntLiteralExpression*>(expr)->value;
			if (kind == LS_TYPE_F32) {
				emitOp(ctx.code, LS_OP_LOAD_CONST_4);
				emitU32(ctx.code, bitcastF32ToU32((float)int_value));
			} else if (kind == LS_TYPE_F64) {
				emitOp(ctx.code, LS_OP_LOAD_CONST_8);
				emitU64(ctx.code, bitcastF64ToU64((double)int_value));
			} else {
				emitOp(ctx.code, LS_OP_LOAD_CONST_8);
				emitU64(ctx.code, bitcastI64ToU64(int_value));
			}
			return kind;
		}
		case Expression::FLOAT_LITERAL: {
			const ls_type_kind kind = defaultLiteralKind(expr, hint);
			if (kind == LS_TYPE_F32) {
				emitOp(ctx.code, LS_OP_LOAD_CONST_4);
				const float value = static_cast<float>(static_cast<FloatLiteralExpression*>(expr)->value);
				emitU32(ctx.code, bitcastF32ToU32(value));
			}
			else {
				emitOp(ctx.code, LS_OP_LOAD_CONST_8);
				emitU64(ctx.code, bitcastF64ToU64(static_cast<FloatLiteralExpression*>(expr)->value));
			}
			return kind;
		}
		case Expression::BOOL_LITERAL: {
			emitOp(ctx.code, LS_OP_LOAD_CONST_8);
			emitU64(ctx.code, static_cast<BoolLiteralExpression*>(expr)->value ? 1u : 0u);
			return LS_TYPE_BOOL;
		}
		case Expression::STRING_LITERAL: {
			u32 string_index = 0;
			appendStringLiteral(ctx.bytecode, static_cast<StringLiteralExpression*>(expr)->value, string_index);
			emitOp(ctx.code, LS_OP_LOAD_CONST_STRING);
			emitU32(ctx.code, string_index);
			return LS_TYPE_STRING;
		}
		case Expression::NULL_LITERAL: {
			emitOp(ctx.code, LS_OP_LOAD_CONST_8);
			emitU64(ctx.code, 0);
			if (hint == LS_TYPE_SLICE) {
				emitOp(ctx.code, LS_OP_LOAD_CONST_8);
				emitU64(ctx.code, 0);
			}
			return hint == LS_TYPE_INVALID ? LS_TYPE_VOID : hint;
		}
		case Expression::UNDEFINED: {
			emitOp(ctx.code, LS_OP_LOAD_CONST_8);
			emitU64(ctx.code, 0);
			return hint == LS_TYPE_INVALID ? LS_TYPE_VOID : hint;
		}
		case Expression::TYPE_LITERAL: {
			emitOp(ctx.code, LS_OP_LOAD_CONST_8);
			emitU64(ctx.code, (u64)static_cast<TypeLiteralExpression*>(expr)->type);
			return LS_TYPE_I32;
		}
		case Expression::IDENTIFIER: {
			IdentifierExpression* id = static_cast<IdentifierExpression*>(expr);
			if (BytecodeLocalBinding* local = ctx.findLocal(id->name)) {
				if (ctx.isRefLocal(*local)) {
					if (!emitReferenceLoad(ctx, expr, typeSlotCount(local->type))) return LS_TYPE_INVALID;
					return local->kind != LS_TYPE_INVALID ? local->kind : LS_TYPE_I32;
				}
				const u32 slot_count = typeSlotCount(local->type);
				emitLoadLocalSlots(ctx, local->slot, slot_count == 0u ? 1u : slot_count);
				return local->kind != LS_TYPE_INVALID ? local->kind : LS_TYPE_I32;
			}
			Symbol* global_sym = id->symbol;
			if (!global_sym && ctx.module && ctx.unit) {
				global_sym = findSymbolInUnit(*ctx.module, *ctx.unit, id->name);
				if (!global_sym) global_sym = findImportedSymbol(*ctx.module, *ctx.unit, id->name);
			}
			if (global_sym && ctx.globals) {
				if (const GlobalBinding* global = findGlobalBinding(*ctx.globals, global_sym)) {
					emitLoadGlobalSlots(ctx, global->slot, global->slot_count);
					ResolvedType* sym_type = global_sym->resolved_type;
					if (sym_type && sym_type->kind == ResolvedType::META) sym_type = static_cast<MetaType*>(sym_type)->inner;
					return valueKindForType(sym_type);
				}
			}
			if (const FunctionInfo* fn = ctx.findFunction(id->name)) {
				emitOp(ctx.code, LS_OP_LOAD_CONST_8);
				emitU64(ctx.code, fn->index);
				return LS_TYPE_FUNCTION;
			}
			return LS_TYPE_INVALID;
		}
		case Expression::BINARY:
			return compileBinary(ctx, static_cast<BinaryExpression*>(expr), hint);
		case Expression::CAST: {
			CastExpression* cast = static_cast<CastExpression*>(expr);
			const ls_type_kind dst_kind = semanticTypeToKind(expr->resolved_type ? expr->resolved_type : cast->expression ? cast->expression->resolved_type : nullptr);
			const ls_type_kind src_kind = compileExpression(ctx, cast->expression, semanticTypeToKind(cast->expression ? cast->expression->resolved_type : nullptr));
			emitOp(ctx.code, LS_OP_CAST);
			emitU8(ctx.code, (u8)src_kind);
			emitU8(ctx.code, (u8)dst_kind);
			return dst_kind;
		}
		case Expression::UNARY: {
			UnaryExpression* un = static_cast<UnaryExpression*>(expr);
			if (un->op == Token::MINUS) {
				if (const FunctionInfo* fn = findOperatorFunction(*ctx.functions, un->op, un->expression ? un->expression->resolved_type : nullptr, nullptr, 1u)) {
					return emitOperatorCall(ctx, fn, un->expression, nullptr, un->op, hint);
				}
			}
			const ls_type_kind kind = compileExpression(ctx, un->expression, hint);
			switch (un->op) {
				case Token::MINUS:
					switch (kind) {
						case LS_TYPE_I8:  emitOp(ctx.code, LS_OP_NEG_I8); break;
						case LS_TYPE_I16: emitOp(ctx.code, LS_OP_NEG_I16); break;
						case LS_TYPE_I32: emitOp(ctx.code, LS_OP_NEG_I32); break;
						case LS_TYPE_I64: emitOp(ctx.code, LS_OP_NEG_I64); break;
						case LS_TYPE_U8:  emitOp(ctx.code, LS_OP_NEG_U8); break;
						case LS_TYPE_U16: emitOp(ctx.code, LS_OP_NEG_U16); break;
						case LS_TYPE_U32: emitOp(ctx.code, LS_OP_NEG_U32); break;
						case LS_TYPE_U64: emitOp(ctx.code, LS_OP_NEG_U64); break;
						case LS_TYPE_F32: emitOp(ctx.code, LS_OP_NEG_F32); break;
						case LS_TYPE_F64: emitOp(ctx.code, LS_OP_NEG_F64); break;
						default: return LS_TYPE_INVALID;
					}
					return kind;
				case Token::NOT:
					emitOp(ctx.code, LS_OP_NOT);
					return LS_TYPE_BOOL;
				default:
					return LS_TYPE_INVALID;
			}
		}
		case Expression::CALL:
			return compileCall(ctx, static_cast<CallExpression*>(expr), hint);
		case Expression::MEMBER: {
			MemberExpression* member = static_cast<MemberExpression*>(expr);
			if (!member->expression && expr->resolved_type && expr->resolved_type->kind == ResolvedType::ENUM) {
				EnumResolvedType* en = static_cast<EnumResolvedType*>(expr->resolved_type);
				u32 enum_index = 0;
				if (!enumMemberIndex(en, member->name, enum_index)) return LS_TYPE_INVALID;
				emitOp(ctx.code, LS_OP_LOAD_CONST_8);
				emitU64(ctx.code, enum_index);
				return LS_TYPE_I32;
			}
			if (member->expression) {
				ResolvedType* base_rt = member->expression->resolved_type;
				EnumResolvedType* enum_via_meta = (base_rt && base_rt->kind == ResolvedType::META && static_cast<MetaType*>(base_rt)->inner->kind == ResolvedType::ENUM)
					? static_cast<EnumResolvedType*>(static_cast<MetaType*>(base_rt)->inner) : nullptr;
				if (enum_via_meta) {
					u32 enum_index = 0;
					if (!enumMemberIndex(enum_via_meta, member->name, enum_index)) return LS_TYPE_INVALID;
					emitOp(ctx.code, LS_OP_LOAD_CONST_8);
					emitU64(ctx.code, enum_index);
					return LS_TYPE_I32;
				}
				if (member->expression->kind == Expression::IDENTIFIER) {
					IdentifierExpression* base = static_cast<IdentifierExpression*>(member->expression);
					if (ctx.module && ctx.unit) {
						Symbol* sym = findImportedQualifiedSymbol(*ctx.module, *ctx.unit, base->name, member->name);
						if (sym && sym->expression) {
							switch (sym->expression->kind) {
								case Expression::FUNCTION:
									if (const FunctionInfo* fn = findFunctionForSymbol(*ctx.functions, sym)) {
										emitOp(ctx.code, LS_OP_LOAD_CONST_8);
										emitU64(ctx.code, fn->index);
										return LS_TYPE_FUNCTION;
									}
									return LS_TYPE_INVALID;
								case Expression::INT_LITERAL:
									emitOp(ctx.code, LS_OP_LOAD_CONST_8);
									emitU64(ctx.code, bitcastI64ToU64(static_cast<IntLiteralExpression*>(sym->expression)->value));
									return valueKindForType(sym->resolved_type);
								case Expression::FLOAT_LITERAL: {
									FloatLiteralExpression* fl = static_cast<FloatLiteralExpression*>(sym->expression);
									const ls_type_kind kind = valueKindForType(sym->resolved_type);
									if (kind == LS_TYPE_F32) {
										emitOp(ctx.code, LS_OP_LOAD_CONST_4);
										emitU32(ctx.code, bitcastF32ToU32(static_cast<float>(fl->value)));
									}
									else {
										emitOp(ctx.code, LS_OP_LOAD_CONST_8);
										emitU64(ctx.code, bitcastF64ToU64(fl->value));
									}
									return kind;
								}
								case Expression::BOOL_LITERAL:
									emitOp(ctx.code, LS_OP_LOAD_CONST_8);
									emitU64(ctx.code, static_cast<BoolLiteralExpression*>(sym->expression)->value ? 1u : 0u);
									return LS_TYPE_BOOL;
								case Expression::STRING_LITERAL:
									{
										u32 string_index = 0;
										appendStringLiteral(ctx.bytecode, static_cast<StringLiteralExpression*>(sym->expression)->value, string_index);
										emitOp(ctx.code, LS_OP_LOAD_CONST_STRING);
										emitU32(ctx.code, string_index);
									}
									return LS_TYPE_STRING;
								case Expression::NULL_LITERAL:
								case Expression::UNDEFINED:
									emitOp(ctx.code, LS_OP_LOAD_CONST_8);
									emitU64(ctx.code, 0);
									return valueKindForType(sym->resolved_type);
								default:
									break;
							}
						}
					}
					if (BytecodeLocalBinding* local = ctx.findLocal(base->name)) {
						ResolvedType* value_type = local->type;
						u32 value_offset = 0u;
						if (value_type && value_type->kind == ResolvedType::NULLABLE
							&& member->expression->resolved_type
							&& member->expression->resolved_type->kind == ResolvedType::STRUCT) {
							value_type = member->expression->resolved_type;
							value_offset = 1u;
						}
						if (!value_type || value_type->kind != ResolvedType::STRUCT) return LS_TYPE_INVALID;
						StructResolvedType* st = static_cast<StructResolvedType*>(value_type);
						u32 offset = 0u;
						ResolvedType* field_type = nullptr;
						if (!structFieldSlotOffset(st, member->name, offset, field_type)) return LS_TYPE_INVALID;
						if (ctx.isRefLocal(*local)) {
							if (!emitReferenceLoad(ctx, expr, typeSlotCount(field_type))) return LS_TYPE_INVALID;
							return valueKindForType(field_type);
						}
						emitOp(ctx.code, LS_OP_LOAD_LOCAL);
						emitU32(ctx.code, local->slot + value_offset + offset);
						return valueKindForType(field_type);
					}
				}
			}
			if (member->expression && member->expression->kind == Expression::IDENTIFIER && ctx.module && ctx.unit) {
				IdentifierExpression* base = static_cast<IdentifierExpression*>(member->expression);
				Symbol* sym = findImportedQualifiedSymbol(*ctx.module, *ctx.unit, base->name, member->name);
				if (sym && sym->expression) {
					switch (sym->expression->kind) {
						case Expression::FUNCTION:
							if (const FunctionInfo* fn = findFunctionForSymbol(*ctx.functions, sym)) {
								emitOp(ctx.code, LS_OP_LOAD_CONST_8);
								emitU64(ctx.code, fn->index);
								return LS_TYPE_FUNCTION;
							}
							return LS_TYPE_INVALID;
						case Expression::INT_LITERAL:
							emitOp(ctx.code, LS_OP_LOAD_CONST_8);
							emitU64(ctx.code, bitcastI64ToU64(static_cast<IntLiteralExpression*>(sym->expression)->value));
							return valueKindForType(sym->resolved_type);
						case Expression::FLOAT_LITERAL: {
							FloatLiteralExpression* fl = static_cast<FloatLiteralExpression*>(sym->expression);
							const ls_type_kind kind = valueKindForType(sym->resolved_type);
							if (kind == LS_TYPE_F32) {
								emitOp(ctx.code, LS_OP_LOAD_CONST_4);
								emitU32(ctx.code, bitcastF32ToU32(static_cast<float>(fl->value)));
							}
							else {
								emitOp(ctx.code, LS_OP_LOAD_CONST_8);
								emitU64(ctx.code, bitcastF64ToU64(fl->value));
							}
							return kind;
						}
						case Expression::BOOL_LITERAL:
							emitOp(ctx.code, LS_OP_LOAD_CONST_8);
							emitU64(ctx.code, static_cast<BoolLiteralExpression*>(sym->expression)->value ? 1u : 0u);
							return LS_TYPE_BOOL;
						case Expression::STRING_LITERAL:
							{
								u32 string_index = 0;
								appendStringLiteral(ctx.bytecode, static_cast<StringLiteralExpression*>(sym->expression)->value, string_index);
								emitOp(ctx.code, LS_OP_LOAD_CONST_STRING);
								emitU32(ctx.code, string_index);
							}
							return LS_TYPE_STRING;
						case Expression::NULL_LITERAL:
						case Expression::UNDEFINED:
							emitOp(ctx.code, LS_OP_LOAD_CONST_8);
							emitU64(ctx.code, 0);
							return valueKindForType(sym->resolved_type);
						default:
							break;
					}
				}
			}
			if (member->expression && member->expression->resolved_type && member->expression->resolved_type->kind == ResolvedType::STRUCT) {
				StructResolvedType* st = static_cast<StructResolvedType*>(member->expression->resolved_type);
				u32 offset = 0u;
				ResolvedType* field_type = nullptr;
				if (!structFieldSlotOffset(st, member->name, offset, field_type)) return LS_TYPE_INVALID;
				// Try reference-based load first (works for addressable lvalues).
				if (emitReferenceLoad(ctx, expr, typeSlotCount(expr->resolved_type))) return valueKindForType(expr->resolved_type);
				// For temporaries (e.g. call results), compile the base onto the stack,
				// store it in a temp local, then load the desired field slot.
				const u32 struct_slot_count = typeSlotCount(member->expression->resolved_type);
				if (compileExpression(ctx, member->expression, LS_TYPE_INVALID) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
				const u32 temp = ctx.addLocal({}, member->expression->resolved_type, valueKindForType(member->expression->resolved_type));
				emitStoreLocalSlots(ctx, temp, struct_slot_count);
				emitOp(ctx.code, LS_OP_LOAD_LOCAL);
				emitU32(ctx.code, temp + offset);
				return valueKindForType(field_type);
			}
			return LS_TYPE_INVALID;
		}
		case Expression::BRACKET: {
			BracketExpression* br = static_cast<BracketExpression*>(expr);
			if (br->has_colon) {
				if (!emitSlice(ctx, br)) return LS_TYPE_INVALID;
				return LS_TYPE_SLICE;
			}
			if (br->args.size() != 1) return LS_TYPE_INVALID;
			if (!br->resolved_type || typeSlotCount(br->resolved_type) != 1u) return LS_TYPE_INVALID;
			if (!emitBracketLoad(ctx, br)) return LS_TYPE_INVALID;
			return valueKindForType(br->resolved_type);
		}
		case Expression::STRUCT_LITERAL: {
			StructLiteralExpression* lit = static_cast<StructLiteralExpression*>(expr);
			ResolvedType* type = lit->type ? lit->type->resolved_type : expr->resolved_type;
			if (type && type->kind == ResolvedType::STRUCT) {
				StructResolvedType* st = static_cast<StructResolvedType*>(type);
				if (!st->decl || st->decl->fields.size() != lit->values.size()) return LS_TYPE_INVALID;
				for (i32 i = 0; i < lit->values.size(); ++i) {
					ResolvedType* field_type = (u32)i < st->field_types.size()
						? st->field_types[(u32)i]
						: st->decl->fields[(u32)i].resolved_type;
					if (compileValueAsType(ctx, lit->values[i], field_type) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
				}
			}
			else {
				for (Expression* value : lit->values) {
					if (compileExpression(ctx, value, LS_TYPE_INVALID) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
				}
			}
			return hint != LS_TYPE_INVALID ? hint : LS_TYPE_I32;
		}
		default:
			return LS_TYPE_INVALID;
	}
}

static bool compileStatement(FunctionCompiler& ctx, Statement* st, ls_type_kind return_kind, ls_string_view current_label) {
	if (!st) return true;
	switch (st->kind) {
		case Statement::VAR_DECL: {
			VarDeclStatement* var = static_cast<VarDeclStatement*>(st);
			ResolvedType* value_type = var->resolved_type ? var->resolved_type : (var->expression ? var->expression->resolved_type : nullptr);
			const ls_type_kind kind = valueKindForType(value_type, parsedTypeToKind(var->parsed_type));
			const u32 slot = ctx.addLocal(var->name, value_type, kind);
			const u32 slot_count = ctx.findLocal(var->name)->slot_count;
			if (var->expression) {
				if (var->expression->kind == Expression::UNDEFINED && slot_count > 1u) {
					for (u32 i = 0; i < slot_count; ++i) {
						emitOp(ctx.code, LS_OP_LOAD_CONST_8);
						emitU64(ctx.code, 0);
					}
				}
				else {
					ls_type_kind expr_kind = compileValueAsType(ctx, var->expression, value_type);
					if (expr_kind == LS_TYPE_INVALID) return false;
				}
			} else {
				for (u32 i = 0; i < slot_count; ++i) {
					emitOp(ctx.code, LS_OP_LOAD_CONST_8);
					emitU64(ctx.code, 0);
				}
			}
			if (slot_count == 1u) {
				emitOp(ctx.code, LS_OP_STORE_LOCAL);
				emitU32(ctx.code, slot);
			}
			else {
				emitStoreLocalSlots(ctx, slot, slot_count);
			}
			return true;
		}
		case Statement::ASSIGN: {
			AssignStatement* assign = static_cast<AssignStatement*>(st);
			if (!assign->lhs) return false;
			if (assign->lhs->kind == Expression::IDENTIFIER) {
				IdentifierExpression* id = static_cast<IdentifierExpression*>(assign->lhs);
				BytecodeLocalBinding* local = ctx.findLocal(id->name);
				const GlobalBinding* global = nullptr;
				Symbol* global_sym = id->symbol;
				if (!local && !global_sym && ctx.module && ctx.unit) {
					global_sym = findSymbolInUnit(*ctx.module, *ctx.unit, id->name);
					if (!global_sym) global_sym = findImportedSymbol(*ctx.module, *ctx.unit, id->name);
				}
				if (!local && global_sym && ctx.globals) {
					global = findGlobalBinding(*ctx.globals, global_sym);
				}
				if (!local && !global) return false;

				ResolvedType* raw_type = global_sym ? global_sym->resolved_type : nullptr;
				if (raw_type && raw_type->kind == ResolvedType::META) raw_type = static_cast<MetaType*>(raw_type)->inner;
				ResolvedType* value_type = local ? local->type : raw_type;
				const ls_type_kind value_kind = local ? local->kind : valueKindForType(value_type);

				if (local && ctx.isRefLocal(*local)) {
					switch (assign->op) {
						case Token::EQUAL:
							emitLoadLocalSlots(ctx, local->slot, 1u);
							emitZeroIndex(ctx);
							if (compileExpression(ctx, assign->rhs, value_kind) == LS_TYPE_INVALID) return false;
							emitOp(ctx.code, LS_OP_STORE_AT);
							emitU32(ctx.code, 1u);
							emitI32(ctx.code, 0);
							return true;
						case Token::PLUS_EQUAL:
						case Token::MINUS_EQUAL:
						case Token::STAR_EQUAL:
						case Token::SLASH_EQUAL:
						{
							const Token::Type binary_op = assign->op == Token::PLUS_EQUAL ? Token::PLUS :
								assign->op == Token::MINUS_EQUAL ? Token::MINUS :
								assign->op == Token::STAR_EQUAL ? Token::STAR : Token::SLASH;
							const FunctionInfo* op_fn = findOperatorFunction(*ctx.functions, binary_op, value_type, assign->rhs ? assign->rhs->resolved_type : nullptr, 2u);
							if (op_fn && op_fn->fn && op_fn->fn->resolved_type) {
								const FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(op_fn->fn->resolved_type);
								emitLoadLocalSlots(ctx, local->slot, 1u);
								emitZeroIndex(ctx);
								emitLoadLocalSlots(ctx, local->slot, 1u);
								emitZeroIndex(ctx);
								emitOp(ctx.code, LS_OP_LOAD_AT);
								emitU32(ctx.code, 1u);
								emitI32(ctx.code, 0);
								if (compileValueAsType(ctx, assign->rhs, fn_type->param_types[1]) == LS_TYPE_INVALID) return false;
								emitOp(ctx.code, LS_OP_CALL_DIRECT);
								emitU32(ctx.code, op_fn->index);
								emitOp(ctx.code, LS_OP_STORE_AT);
								emitU32(ctx.code, 1u);
								emitI32(ctx.code, 0);
								return true;
							}
						}
							emitLoadLocalSlots(ctx, local->slot, 1u);
							emitZeroIndex(ctx);
							emitLoadLocalSlots(ctx, local->slot, 1u);
							emitZeroIndex(ctx);
							emitOp(ctx.code, LS_OP_LOAD_AT);
							emitU32(ctx.code, 1u);
							emitI32(ctx.code, 0);
							if (compileExpression(ctx, assign->rhs, value_kind) == LS_TYPE_INVALID) return false;
							if (!emitNumericStoreOp(ctx, value_kind, assign->op)) return false;
							emitOp(ctx.code, LS_OP_STORE_AT);
							emitU32(ctx.code, 1u);
							emitI32(ctx.code, 0);
							return true;
						default:
							return false;
					}
				}

				switch (assign->op) {
					case Token::EQUAL:
						if (compileValueAsType(ctx, assign->rhs, value_type) == LS_TYPE_INVALID) return false;
						if (local) {
							emitStoreLocalSlots(ctx, local->slot, local->slot_count);
						}
						else {
							emitStoreGlobalSlots(ctx, global->slot, global->slot_count);
						}
						return true;
					case Token::PLUS_EQUAL:
					case Token::MINUS_EQUAL:
					case Token::STAR_EQUAL:
					case Token::SLASH_EQUAL: {
						const Token::Type binary_op = assign->op == Token::PLUS_EQUAL ? Token::PLUS :
							assign->op == Token::MINUS_EQUAL ? Token::MINUS :
							assign->op == Token::STAR_EQUAL ? Token::STAR : Token::SLASH;
						const FunctionInfo* op_fn = findOperatorFunction(*ctx.functions, binary_op, value_type, assign->rhs ? assign->rhs->resolved_type : nullptr, 2u);
						if (op_fn && op_fn->fn && op_fn->fn->resolved_type) {
							const FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(op_fn->fn->resolved_type);
							if (local) {
								emitLoadLocalSlots(ctx, local->slot, local->slot_count);
							}
							else {
								emitLoadGlobalSlots(ctx, global->slot, global->slot_count);
							}
							if (compileValueAsType(ctx, assign->rhs, fn_type->param_types[1]) == LS_TYPE_INVALID) return false;
							emitOp(ctx.code, LS_OP_CALL_DIRECT);
							emitU32(ctx.code, op_fn->index);
							if (local) {
								emitStoreLocalSlots(ctx, local->slot, local->slot_count);
							}
							else {
								emitStoreGlobalSlots(ctx, global->slot, global->slot_count);
							}
							return true;
						}
						if (local) {
							emitOp(ctx.code, LS_OP_LOAD_LOCAL);
							emitU32(ctx.code, local->slot);
						}
						else {
							emitLoadGlobalSlots(ctx, global->slot, global->slot_count);
						}
						if (compileExpression(ctx, assign->rhs, value_kind) == LS_TYPE_INVALID) return false;
						if (!emitNumericStoreOp(ctx, value_kind, assign->op)) return false;
						if (local) {
							emitOp(ctx.code, LS_OP_STORE_LOCAL);
							emitU32(ctx.code, local->slot);
						}
						else {
							emitStoreGlobalSlots(ctx, global->slot, global->slot_count);
						}
						return true;
					}
					default:
						return false;
				}
			}
			if (assign->lhs->kind == Expression::BRACKET) {
				BracketExpression* br = static_cast<BracketExpression*>(assign->lhs);
				const ls_type_kind value_kind = valueKindForType(assign->lhs->resolved_type, LS_TYPE_INVALID);
				if (value_kind == LS_TYPE_INVALID) return false;
				return emitBracketStore(ctx, br, assign->rhs, value_kind, assign->op);
			}
			if (assign->lhs->kind == Expression::MEMBER) {
				MemberExpression* member = static_cast<MemberExpression*>(assign->lhs);
				if (!member->expression || !member->expression->resolved_type) return false;
				if (member->expression->resolved_type->kind != ResolvedType::STRUCT) return false;
				StructResolvedType* st = static_cast<StructResolvedType*>(member->expression->resolved_type);
				u32 field_offset = 0u;
				ResolvedType* field_type = nullptr;
				if (!structFieldSlotOffset(st, member->name, field_offset, field_type)) return false;
				const u32 field_slots = typeSlotCount(field_type);
				const ls_type_kind value_kind = valueKindForType(field_type, LS_TYPE_INVALID);
				if (assign->op == Token::EQUAL) {
					if (!emitReference(ctx, member->expression)) return false;
					emitZeroIndex(ctx);
					if (compileExpression(ctx, assign->rhs, value_kind) == LS_TYPE_INVALID) return false;
					emitOp(ctx.code, LS_OP_STORE_AT);
					emitU32(ctx.code, field_slots ? field_slots : 1u);
					emitI32(ctx.code, (i32)field_offset);
					return true;
				}
				// Compound assignment: load existing value, apply op, store back.
				// Stack order for STORE_AT is: base, index, value.
				const u32 ref_slot = ctx.addLocal({}, nullptr, LS_TYPE_I64);
				const u32 val_slot = ctx.addLocal({}, field_type, value_kind);
				if (!emitReference(ctx, member->expression)) return false;
				emitStoreLocalSlots(ctx, ref_slot, 1u);
				emitLoadLocalSlots(ctx, ref_slot, 1u);
				emitZeroIndex(ctx);
				emitOp(ctx.code, LS_OP_LOAD_AT);
				emitU32(ctx.code, 1u);
				emitI32(ctx.code, (i32)field_offset);
				if (compileExpression(ctx, assign->rhs, value_kind) == LS_TYPE_INVALID) return false;
				if (!emitNumericStoreOp(ctx, value_kind, assign->op)) return false;
				emitStoreLocalSlots(ctx, val_slot, 1u);
				emitLoadLocalSlots(ctx, ref_slot, 1u);
				emitZeroIndex(ctx);
				emitLoadLocalSlots(ctx, val_slot, 1u);
				emitOp(ctx.code, LS_OP_STORE_AT);
				emitU32(ctx.code, 1u);
				emitI32(ctx.code, (i32)field_offset);
				return true;
			}
			return false;
		}
		case Statement::IF: {
			IfStatement* ifst = static_cast<IfStatement*>(st);
			if (!ifst->condition || !ifst->body) return false;
			if (compileExpression(ctx, ifst->condition, LS_TYPE_BOOL) == LS_TYPE_INVALID) return false;
			const u32 jump_false_pos = (u32)ctx.code.size();
			emitOp(ctx.code, LS_OP_JUMP_IF_FALSE);
			emitI32(ctx.code, 0);
			if (!compileStatement(ctx, ifst->body, return_kind, current_label)) return false;
			if (ifst->else_branch) {
				const u32 jump_end_pos = (u32)ctx.code.size();
				emitOp(ctx.code, LS_OP_JUMP);
				emitI32(ctx.code, 0);
				patchI32(ctx.code, jump_false_pos + 1u, (i32)((i32)ctx.code.size() - (i32)(jump_false_pos + 5u)));
				if (!compileStatement(ctx, ifst->else_branch, return_kind, current_label)) return false;
				patchI32(ctx.code, jump_end_pos + 1u, (i32)((i32)ctx.code.size() - (i32)(jump_end_pos + 5u)));
			}
			else {
				patchI32(ctx.code, jump_false_pos + 1u, (i32)((i32)ctx.code.size() - (i32)(jump_false_pos + 5u)));
			}
			return true;
		}
		case Statement::WHILE: {
			WhileStatement* ws = static_cast<WhileStatement*>(st);
			if (!ws->condition || !ws->body) return false;

			const u32 condition_pos = (u32)ctx.code.size();
			if (compileExpression(ctx, ws->condition, LS_TYPE_BOOL) == LS_TYPE_INVALID) return false;

			const u32 jump_false_pos = (u32)ctx.code.size();
			emitOp(ctx.code, LS_OP_JUMP_IF_FALSE);
			emitI32(ctx.code, 0);

			LoopBinding& loop = ctx.loops.emplace_back();
			loop.label = current_label;
			loop.condition_pos = condition_pos;
			loop.continue_pos = condition_pos;
			loop.defer_mark = (u32)ctx.deferreds.size();
			void* break_storage = ctx.bytecode->arena->allocate(ctx.bytecode->arena->user_data, sizeof(ExpArray<u32>), alignof(ExpArray<u32>));
			if (!break_storage) {
				ctx.loops.pop_back();
				return false;
			}
			loop.break_jumps = ::new (break_storage) ExpArray<u32>(*ctx.bytecode->arena);
			void* continue_storage = ctx.bytecode->arena->allocate(ctx.bytecode->arena->user_data, sizeof(ExpArray<u32>), alignof(ExpArray<u32>));
			if (!continue_storage) {
				ctx.loops.pop_back();
				return false;
			}
			loop.continue_jumps = ::new (continue_storage) ExpArray<u32>(*ctx.bytecode->arena);

			if (!compileStatement(ctx, ws->body, return_kind, {})) {
				ctx.loops.pop_back();
				return false;
			}

			const u32 jump_back_pos = (u32)ctx.code.size();
			emitOp(ctx.code, LS_OP_JUMP);
			emitI32(ctx.code, (i32)((i32)condition_pos - (i32)(jump_back_pos + 5u)));

			const u32 loop_end = (u32)ctx.code.size();
			patchI32(ctx.code, jump_false_pos + 1u, (i32)((i32)loop_end - (i32)(jump_false_pos + 5u)));
			if (loop.break_jumps) {
				for (u32 break_pos : *loop.break_jumps) {
					patchI32(ctx.code, break_pos, (i32)((i32)loop_end - (i32)(break_pos + 4u)));
				}
			}
			if (loop.continue_jumps) {
				for (u32 continue_pos : *loop.continue_jumps) {
					patchI32(ctx.code, continue_pos, (i32)((i32)condition_pos - (i32)(continue_pos + 4u)));
				}
			}
			ctx.loops.pop_back();
			return true;
		}
		case Statement::FOR: {
			ForStatement* fs = static_cast<ForStatement*>(st);
			if (!fs->begin || !fs->end || !fs->body) return false;
			ResolvedType* value_type = fs->begin->resolved_type;
			const ls_type_kind value_kind = valueKindForType(value_type, LS_TYPE_I32);
			const u32 slot_count = typeSlotCount(value_type) == 0u ? 1u : typeSlotCount(value_type);
			if (compileExpression(ctx, fs->begin, value_kind) == LS_TYPE_INVALID) return false;
			if (compileExpression(ctx, fs->end, value_kind) == LS_TYPE_INVALID) return false;

			ctx.pushScope();
			const u32 loop_slot = ctx.addLocal(fs->loop_var, value_type, value_kind);
			const u32 end_slot = ctx.addLocal({}, value_type, value_kind);
			emitStoreLocalSlots(ctx, end_slot, slot_count);
			emitStoreLocalSlots(ctx, loop_slot, slot_count);

			const u32 condition_pos = (u32)ctx.code.size();
			emitLoadLocalSlots(ctx, loop_slot, slot_count);
			emitLoadLocalSlots(ctx, end_slot, slot_count);
			emitOp(ctx.code, LS_OP_LE);
			emitU8(ctx.code, (u8)value_kind);
			const u32 jump_false_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_FALSE);

			LoopBinding& loop = ctx.loops.emplace_back();
			loop.label = current_label;
			loop.condition_pos = condition_pos;
			loop.continue_pos = 0u;
			loop.defer_mark = (u32)ctx.deferreds.size();
			void* break_storage = ctx.bytecode->arena->allocate(ctx.bytecode->arena->user_data, sizeof(ExpArray<u32>), alignof(ExpArray<u32>));
			if (!break_storage) {
				ctx.loops.pop_back();
				ctx.popScope(return_kind, current_label);
				return false;
			}
			loop.break_jumps = ::new (break_storage) ExpArray<u32>(*ctx.bytecode->arena);
			void* continue_storage = ctx.bytecode->arena->allocate(ctx.bytecode->arena->user_data, sizeof(ExpArray<u32>), alignof(ExpArray<u32>));
			if (!continue_storage) {
				ctx.loops.pop_back();
				ctx.popScope(return_kind, current_label);
				return false;
			}
			loop.continue_jumps = ::new (continue_storage) ExpArray<u32>(*ctx.bytecode->arena);

			if (!compileStatement(ctx, fs->body, return_kind, {})) {
				ctx.loops.pop_back();
				ctx.popScope(return_kind, current_label);
				return false;
			}

			const u32 increment_pos = (u32)ctx.code.size();
			emitLoadLocalSlots(ctx, loop_slot, slot_count);
			emitOp(ctx.code, LS_OP_LOAD_CONST_8);
			emitU64(ctx.code, 1u);
			switch (value_kind) {
				case LS_TYPE_I8:  emitOp(ctx.code, LS_OP_ADD_I8); break;
				case LS_TYPE_I16: emitOp(ctx.code, LS_OP_ADD_I16); break;
				case LS_TYPE_I32: emitOp(ctx.code, LS_OP_ADD_I32); break;
				case LS_TYPE_I64: emitOp(ctx.code, LS_OP_ADD_I64); break;
				case LS_TYPE_U8:  emitOp(ctx.code, LS_OP_ADD_U8); break;
				case LS_TYPE_U16: emitOp(ctx.code, LS_OP_ADD_U16); break;
				case LS_TYPE_U32: emitOp(ctx.code, LS_OP_ADD_U32); break;
				case LS_TYPE_U64: emitOp(ctx.code, LS_OP_ADD_U64); break;
				default:
					ctx.loops.pop_back();
					ctx.popScope(return_kind, current_label);
					return false;
			}
			emitStoreLocalSlots(ctx, loop_slot, slot_count);
			loop.continue_pos = increment_pos;

			const u32 jump_back_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP);
			patchJumpRelative(ctx.code, jump_back_pos, condition_pos);

			const u32 loop_end = (u32)ctx.code.size();
			patchJumpRelative(ctx.code, jump_false_pos, loop_end);
			if (loop.break_jumps) {
				for (u32 break_pos : *loop.break_jumps) {
					patchJumpRelative(ctx.code, break_pos, loop_end);
				}
			}
			if (loop.continue_jumps) {
				for (u32 continue_pos : *loop.continue_jumps) {
					patchJumpRelative(ctx.code, continue_pos, increment_pos);
				}
			}
			ctx.loops.pop_back();
			ctx.popScope(return_kind, current_label);
			return true;
		}
		case Statement::MATCH: {
			MatchStatement* ms = static_cast<MatchStatement*>(st);
			if (!ms->subject) return false;
			ResolvedType* subject_type = ms->subject->resolved_type;
			const ls_type_kind subject_kind = valueKindForType(subject_type, LS_TYPE_I32);
			const u32 subject_slot_count = typeSlotCount(subject_type) == 0u ? 1u : typeSlotCount(subject_type);
			if (compileExpression(ctx, ms->subject, subject_kind) == LS_TYPE_INVALID) return false;
			const u32 subject_slot = ctx.addLocal({}, subject_type, subject_kind);
			emitStoreLocalSlots(ctx, subject_slot, subject_slot_count);

			ExpArray<u32> match_end_jumps(*ctx.bytecode->arena);
			ExpArray<u32> pending_false_jumps(*ctx.bytecode->arena);

			for (MatchArm& arm : ms->arms) {
				ExpArray<u32> arm_body_jumps(*ctx.bytecode->arena);

				if (arm.is_fallback) {
					for (u32 false_jump : pending_false_jumps) patchJumpRelative(ctx.code, false_jump, (u32)ctx.code.size());
					pending_false_jumps.clear();
					if (!compileStatement(ctx, arm.body, return_kind, current_label)) return false;
					match_end_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
					continue;
				}

				for (MatchPattern& pattern : arm.patterns) {
					for (u32 false_jump : pending_false_jumps) patchJumpRelative(ctx.code, false_jump, (u32)ctx.code.size());
					pending_false_jumps.clear();

					if (pattern.end) {
						emitLoadLocalSlots(ctx, subject_slot, subject_slot_count);
						if (compileExpression(ctx, pattern.begin, subject_kind) == LS_TYPE_INVALID) return false;
						switch (subject_kind) {
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
								break;
							default:
								return false;
						}
						emitOp(ctx.code, LS_OP_GE);
						emitU8(ctx.code, (u8)subject_kind);
						pending_false_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_FALSE));

						emitLoadLocalSlots(ctx, subject_slot, subject_slot_count);
						if (compileExpression(ctx, pattern.end, subject_kind) == LS_TYPE_INVALID) return false;
						emitOp(ctx.code, LS_OP_LE);
						emitU8(ctx.code, (u8)subject_kind);
						pending_false_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_FALSE));
						arm_body_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
					}
					else {
						emitLoadLocalSlots(ctx, subject_slot, subject_slot_count);
						if (compileExpression(ctx, pattern.begin, subject_kind) == LS_TYPE_INVALID) return false;
						emitOp(ctx.code, LS_OP_EQ);
						emitU8(ctx.code, (u8)subject_kind);
						arm_body_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_TRUE));
					}
				}

				const u32 skip_jump_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP);
				const u32 body_start = (u32)ctx.code.size();
				for (u32 jump_pos : arm_body_jumps) patchJumpRelative(ctx.code, jump_pos, body_start);
				for (u32 false_jump : pending_false_jumps) patchJumpRelative(ctx.code, false_jump, skip_jump_pos - 1u);
				pending_false_jumps.clear();
				if (!compileStatement(ctx, arm.body, return_kind, current_label)) return false;
				match_end_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
				const u32 arm_end_pos = (u32)ctx.code.size();
				patchJumpRelative(ctx.code, skip_jump_pos, arm_end_pos);
			}

			const u32 end_pos = (u32)ctx.code.size();
			for (u32 jump_pos : match_end_jumps) patchJumpRelative(ctx.code, jump_pos, end_pos);
			return true;
		}
		case Statement::DEFER: {

			DeferStatement* df = static_cast<DeferStatement*>(st);
			if (!df->statement || df->statement->kind == Statement::RETURN) return false;
			ctx.deferreds.push(df->statement);
			return true;
		}
		case Statement::BREAK:
		case Statement::CONTINUE: {
			const bool is_break = st->kind == Statement::BREAK;
			const ls_string_view label = is_break ? static_cast<BreakStatement*>(st)->label : static_cast<ContinueStatement*>(st)->label;
			LoopBinding* loop = ctx.findLoop(label);
			if (!loop) return false;
			emitDeferredStatements(ctx, loop->defer_mark, return_kind, current_label);
			if (is_break) {
				const u32 jump_pos = (u32)ctx.code.size();
				emitOp(ctx.code, LS_OP_JUMP);
				emitI32(ctx.code, 0);
				if (!loop->break_jumps) return false;
				loop->break_jumps->push(jump_pos + 1u);
				return true;
			}
			const u32 jump_pos = (u32)ctx.code.size();
			emitOp(ctx.code, LS_OP_JUMP);
			emitI32(ctx.code, 0);
			if (!loop->continue_jumps) return false;
			loop->continue_jumps->push(jump_pos + 1u);
			return true;
		}
		case Statement::LABEL: {
			LabelStatement* label = static_cast<LabelStatement*>(st);
			if (!label->statement) return false;
			const ls_string_view next_label = label->statement->kind == Statement::WHILE || label->statement->kind == Statement::FOR
				? label->name
				: current_label;
			return compileStatement(ctx, label->statement, return_kind, next_label);
		}
	case Statement::RETURN: {
		ReturnStatement* ret = static_cast<ReturnStatement*>(st);
		if (return_kind == LS_TYPE_NULL_VALUE) {
			const bool is_null = !ret->expression
				|| ret->expression->kind == Expression::NULL_LITERAL
				|| ret->expression->kind == Expression::UNDEFINED;
			emitOp(ctx.code, LS_OP_LOAD_CONST_8);
			emitU64(ctx.code, is_null ? 0u : 1u);
			if (ret->expression) {
				if (compileExpression(ctx, ret->expression, LS_TYPE_INVALID) == LS_TYPE_INVALID) return false;
			}
			else {
				emitOp(ctx.code, LS_OP_LOAD_CONST_8);
				emitU64(ctx.code, 0u);
			}
			emitDeferredStatements(ctx, 0u, return_kind, current_label);
			emitOp(ctx.code, LS_OP_RETURN);
			return true;
		}
		if (ret->expression) {
			if (compileValueAsType(ctx, ret->expression, ctx.return_type) == LS_TYPE_INVALID) return false;
		}
		emitDeferredStatements(ctx, 0u, return_kind, current_label);
		emitOp(ctx.code, LS_OP_RETURN);
		return true;
		}
		case Statement::EXPRESSION: {
			ExpressionStatement* expr = static_cast<ExpressionStatement*>(st);
			const ls_type_kind kind = compileExpression(ctx, expr->expression, LS_TYPE_INVALID);
			if (kind == LS_TYPE_INVALID) return false;
			const u32 slot_count = kind == LS_TYPE_VOID ? 0u : typeSlotCount(expr->expression->resolved_type);
			for (u32 i = 0; i < slot_count; ++i) emitOp(ctx.code, LS_OP_POP);
			return true;
		}
		case Statement::BLOCK: {
			BlockStatement* block = static_cast<BlockStatement*>(st);
			ctx.pushScope();
			for (Statement* child : block->statements) {
				if (!compileStatement(ctx, child, return_kind, current_label)) {
					ctx.popScope(return_kind, current_label);
					return false;
				}
			}
			ctx.popScope(return_kind, current_label);
			return true;
		}
		default:
			return false;
	}
}

static bool isSimpleReturnLiteral(BlockStatement* body, Expression*& out_expr) {
	if (!body || body->statements.size() != 1) return false;
	Statement* st = body->statements[0];
	if (!st || st->kind != Statement::RETURN) return false;
	ReturnStatement* ret = static_cast<ReturnStatement*>(st);
	if (!ret->expression) return false;
	switch (ret->expression->kind) {
		case Expression::INT_LITERAL:
		case Expression::FLOAT_LITERAL:
		case Expression::BOOL_LITERAL:
			out_expr = ret->expression;
			return true;
		default:
			return false;
	}
}

ls_bytecode* ls_bytecode_compile(
	ls_module* module,
	ls_host* host
) {
	if (!module || !host) return nullptr;

	ls_bytecode* bytecode = static_cast<ls_bytecode*>(std::calloc(1, sizeof(ls_bytecode)));
	if (!bytecode) return nullptr;
	bytecode->host = host;
	bytecode->arena = host->create_arena ? host->create_arena() : nullptr;
	if (!bytecode->arena) {
		std::free(bytecode);
		return nullptr;
	}

	ASSERT(bytecode->arena);
	ls_arena* arena = bytecode->arena;

	ExpArray<GlobalBinding> globals(*arena);
	bytecode->global_slot_count = 0u;
	for (Unit& unit : module->units) {
		for (Symbol& sym : unit.symbols) {
			if (!sym.expression) continue;
			if (sym.expression->kind == Expression::FUNCTION || sym.expression->kind == Expression::STRUCT || sym.expression->kind == Expression::ENUM) continue;
			GlobalBinding& binding = globals.emplace_back();
			binding.sym = &sym;
			binding.slot = bytecode->global_slot_count;
			binding.slot_count = typeSlotCount(sym.resolved_type);
			if (binding.slot_count == 0u) binding.slot_count = 1u;
			bytecode->global_slot_count += binding.slot_count;
		}
	}

	ExpArray<FunctionInfo> functions(*arena);
	for (Unit& unit : module->units) {
		for (Symbol& sym : unit.symbols) {
			if (!sym.expression || sym.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* fn = static_cast<FunctionExpression*>(sym.expression);
			FunctionInfo& info = functions.emplace_back();
			info.name = sym.name;
			info.fn = fn;
			info.type = fn->resolved_type ? static_cast<FunctionResolvedType*>(fn->resolved_type) : nullptr;
			info.unit = &unit;
			info.symbol = &sym;
			info.index = (u32)functions.size() - 1;
		}
	}
	for (Unit& unit : module->units) {
		for (Symbol& sym : unit.symbols) {
			if (!sym.expression || sym.expression->kind != Expression::FUNCTION) continue;

			FunctionExpression* fn = static_cast<FunctionExpression*>(sym.expression);
			ls_function_bc* out = appendFunction(bytecode);
			if (!out) {
				ls_bytecode_destroy(bytecode);
				return nullptr;
			}
			ls_function_bc& function = *out;
			function.name = sym.name;
			function.kind = fn->is_extern ? LS_FUNCTION_NATIVE : LS_FUNCTION_SCRIPT;
			function.is_builtin_native = fn->is_extern && equalStrings(unit.path, makeStringView("std:math"));
			function.index = (u32)(bytecode->function_count - 1);
			function.param_count = (u32)fn->runtime_params.size();
			function.param_slot_count = 0;
			ResolvedType* return_type = fn->resolved_type ? static_cast<FunctionResolvedType*>(fn->resolved_type)->return_type : nullptr;
			function.return_kind = toTypeKind(return_type);
			// Calls move raw slots, so aggregate return metadata must describe the
			// representation width rather than assuming every value is one slot.
			function.return_slot_count = typeSlotCount(return_type);
			function.local_slot_count = 0;
			function.max_stack = function.return_slot_count;

			if (fn->is_extern) {
				for (FunctionParam& param : fn->runtime_params) {
					const u32 slot_count = param.is_ref ? 1u : typeSlotCount(param.resolved_type);
					function.param_slot_count += slot_count == 0u ? 1u : slot_count;
				}
				continue;
			}
			if (!fn->body || fn->body->kind != Statement::BLOCK) {
				ls_bytecode_destroy(bytecode);
				return nullptr;
			}

			BlockStatement* body = static_cast<BlockStatement*>(fn->body);
			Expression* literal = nullptr;
			if (function.return_slot_count == 1u && isSimpleReturnLiteral(body, literal)) {
				// Build the tiny literal function directly to keep the old fast path.
				for (FunctionParam& param : fn->runtime_params) {
					const u32 slot_count = param.is_ref ? 1u : typeSlotCount(param.resolved_type);
					function.param_slot_count += slot_count == 0u ? 1u : slot_count;
				}
				ExpArray<u8> temp(*arena);
				switch (literal->kind) {
					case Expression::INT_LITERAL:
						emitOp(temp, LS_OP_LOAD_CONST_8);
						emitU64(temp, bitcastI64ToU64(static_cast<IntLiteralExpression*>(literal)->value));
						break;
					case Expression::FLOAT_LITERAL:
						if (function.return_kind == LS_TYPE_F32) {
							emitOp(temp, LS_OP_LOAD_CONST_4);
							const float value = static_cast<float>(static_cast<FloatLiteralExpression*>(literal)->value);
							emitU32(temp, bitcastF32ToU32(value));
						}
						else {
							emitOp(temp, LS_OP_LOAD_CONST_8);
							emitU64(temp, bitcastF64ToU64(static_cast<FloatLiteralExpression*>(literal)->value));
						}
						break;
					case Expression::BOOL_LITERAL:
						emitOp(temp, LS_OP_LOAD_CONST_8);
						emitU64(temp, static_cast<BoolLiteralExpression*>(literal)->value ? 1u : 0u);
						break;
					default:
						ls_bytecode_destroy(bytecode);
						return nullptr;
				}
				emitOp(temp, LS_OP_RETURN);
				function.code_size = (u32)temp.size();
				function.code_capacity = function.code_size;
				function.code = static_cast<u8*>(arena->allocate(arena->user_data, function.code_size, alignof(u8)));
				if (!function.code) {
					ls_bytecode_destroy(bytecode);
					return nullptr;
				}
				for (i32 i = 0; i < temp.size(); ++i) function.code[i] = temp[i];
				function.max_stack = function.param_slot_count + function.return_slot_count;
				continue;
			}

			FunctionCompiler ctx(bytecode, function);
			ctx.module = module;
			ctx.unit = &unit;
			ctx.return_type = return_type;
			ctx.functions = &functions;
			ctx.globals = &globals;
			for (FunctionParam& param : fn->runtime_params) {
				const ls_type_kind kind = valueKindForType(param.resolved_type, parsedTypeToKind(param.parsed_type));
				BytecodeLocalBinding& binding = ctx.locals.emplace_back();
				binding.name = param.name;
				binding.type = param.resolved_type;
				binding.kind = kind;
				binding.slot_count = param.is_ref ? 1u : typeSlotCount(param.resolved_type);
				if (binding.slot_count == 0u) binding.slot_count = 1u;
				binding.slot = function.param_slot_count;
				if (param.is_ref) ctx.ref_local_slots.push(binding.slot);
				function.param_slot_count += binding.slot_count;
				ctx.next_local_slot = function.param_slot_count;
				if (function.param_slot_count > ctx.max_local_count) ctx.max_local_count = function.param_slot_count;
			}
			for (Statement* st : body->statements) {
				if (!compileStatement(ctx, st, function.return_kind, {})) {
					ls_bytecode_destroy(bytecode);
					return nullptr;
				}
			}
			function.local_slot_count = ctx.max_local_count > function.param_slot_count ? ctx.max_local_count - function.param_slot_count : 0u;
			function.max_stack = function.return_slot_count + function.param_slot_count + function.local_slot_count + 8;
			function.code_size = (u32)ctx.code.size();
			function.code_capacity = function.code_size;
			if (function.code_size > 0) {
				function.code = static_cast<u8*>(arena->allocate(arena->user_data, function.code_size, alignof(u8)));
				if (!function.code) {
					ls_bytecode_destroy(bytecode);
					return nullptr;
				}
				for (u32 i = 0; i < function.code_size; ++i) function.code[i] = ctx.code[i];
			}
		}
	}

	if (bytecode->global_slot_count > 0u) {
		ls_function_bc* out = appendFunction(bytecode);
		if (!out) {
			ls_bytecode_destroy(bytecode);
			return nullptr;
		}
		ls_function_bc& function = *out;
		function.name = {};
		function.kind = LS_FUNCTION_SCRIPT;
		function.is_builtin_native = false;
		function.index = (u32)(bytecode->function_count - 1);
		function.param_count = 0;
		function.param_slot_count = 0;
		function.return_kind = LS_TYPE_VOID;
		function.return_slot_count = 0;
		function.local_slot_count = 0;
		function.max_stack = 8u;

		FunctionCompiler ctx(bytecode, function);
		ctx.module = module;
		ctx.functions = &functions;
		ctx.globals = &globals;
		for (Unit& unit : module->units) {
			ctx.unit = &unit;
			for (Symbol& sym : unit.symbols) {
				if (!sym.expression) continue;
				if (sym.expression->kind == Expression::FUNCTION || sym.expression->kind == Expression::STRUCT || sym.expression->kind == Expression::ENUM) continue;
				const GlobalBinding* binding = findGlobalBinding(globals, &sym);
				if (!binding) continue;
				const ls_type_kind kind = valueKindForType(sym.resolved_type, LS_TYPE_INVALID);
				if (sym.expression) {
					if (compileValueAsType(ctx, sym.expression, sym.resolved_type) == LS_TYPE_INVALID) {
						ls_bytecode_destroy(bytecode);
						return nullptr;
					}
				}
				else {
					for (u32 i = 0; i < binding->slot_count; ++i) {
						emitOp(ctx.code, LS_OP_LOAD_CONST_8);
						emitU64(ctx.code, 0);
					}
				}
				emitStoreGlobalSlots(ctx, binding->slot, binding->slot_count);
			}
		}
		emitOp(ctx.code, LS_OP_RETURN);
		function.max_stack = ctx.max_local_count + 8u;
		function.code_size = (u32)ctx.code.size();
		function.code_capacity = function.code_size;
		function.code = static_cast<u8*>(arena->allocate(arena->user_data, function.code_size, alignof(u8)));
		if (!function.code) {
			ls_bytecode_destroy(bytecode);
			return nullptr;
		}
		for (u32 i = 0; i < function.code_size; ++i) function.code[i] = ctx.code[i];
		bytecode->has_global_init = true;
	}

	return bytecode;
}

void ls_bytecode_destroy(ls_bytecode* bytecode) {
	if (!bytecode) return;
	if (bytecode->host && bytecode->host->destroy_arena && bytecode->arena) bytecode->host->destroy_arena(bytecode->arena);
	std::free(bytecode);
}

