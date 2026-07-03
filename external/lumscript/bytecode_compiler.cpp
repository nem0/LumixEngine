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
		case ResolvedType::ISIZE: return LS_TYPE_I64;
		case ResolvedType::F32: return LS_TYPE_F32;
		case ResolvedType::F64: return LS_TYPE_F64;
		case ResolvedType::STRING: return LS_TYPE_STRING;
		case ResolvedType::CPTR: return LS_TYPE_CPTR;
		case ResolvedType::BYTE: return LS_TYPE_U8;
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
		case ParsedType::ISIZE: return LS_TYPE_I64;
		case ParsedType::F32: return LS_TYPE_F32;
		case ParsedType::F64: return LS_TYPE_F64;
		case ParsedType::STRING: return LS_TYPE_STRING;
		case ParsedType::CPTR: return LS_TYPE_CPTR;
		case ParsedType::BYTE: return LS_TYPE_U8;
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

static u32 typeKindByteSize(ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_VOID: return 0u;
		case LS_TYPE_BOOL:
		case LS_TYPE_I8:
		case LS_TYPE_U8:
			return 1u;
		case LS_TYPE_I16:
		case LS_TYPE_U16:
			return 2u;
		case LS_TYPE_I32:
		case LS_TYPE_U32:
		case LS_TYPE_F32:
		case LS_TYPE_ENUM:
		case LS_TYPE_FUNCTION:
			return 4u;
		case LS_TYPE_I64:
		case LS_TYPE_U64:
		case LS_TYPE_F64:
		case LS_TYPE_STRING:
		case LS_TYPE_CPTR:
			return 8u;
		case LS_TYPE_SLICE:
			return 16u;
		default:
			return 8u;
	}
}

static ls_type_kind defaultLiteralKind(const Expression& expr, ls_type_kind hint) {
	if (expr.kind == Expression::INT_LITERAL) {
		return isIntegerKind(hint) ? hint : LS_TYPE_I32;
	}
	if (expr.kind == Expression::FLOAT_LITERAL) {
		return isFloatKind(hint) ? hint : LS_TYPE_F64;
	}
	if (expr.kind == Expression::BOOL_LITERAL) return LS_TYPE_BOOL;
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

static void emitBytes(ByteArray& code, const void* value, size_t size) {
	const u8* bytes = static_cast<const u8*>(value);
	for (size_t i = 0; i < size; ++i) code.push_back(bytes[i]);
}

static void emitU8(ByteArray& code, u8 value) {
	emitBytes(code, &value, sizeof(value));
}

static void emitU32(ByteArray& code, u32 value) {
	emitBytes(code, &value, sizeof(value));
}

static void emitI32(ByteArray& code, i32 value) {
	emitBytes(code, &value, sizeof(value));
}

static void patchI32(ByteArray& code, u32 offset, i32 value) {
	const u32 bits = (u32)value;
	code[offset + 0u] = (u8)(bits & 0xFFu);
	code[offset + 1u] = (u8)((bits >> 8u) & 0xFFu);
	code[offset + 2u] = (u8)((bits >> 16u) & 0xFFu);
	code[offset + 3u] = (u8)((bits >> 24u) & 0xFFu);
}

static void emitU64(ByteArray& code, u64 value) {
	emitBytes(code, &value, sizeof(value));
}

static void emitOp(ByteArray& code, ls_op op) {
	emitU8(code, (u8)op);
}

template <typename T>
static T* appendArenaArray(ls_arena& arena, T*& data, u32& count, u32& capacity) {
	if (count >= capacity) {
		const u32 new_capacity = capacity ? capacity * 2u : 4u;
		T* const new_data = static_cast<T*>(arena.allocate(arena.user_data, sizeof(T) * (size_t)new_capacity, alignof(T)));
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
	u32 offset = 0;
	u32 byte_size = 1;
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
	u32 offset = 0;
	u32 byte_size = 0;
};

static u32 typeByteSize(ResolvedType* type);
struct FunctionCompiler;
static bool compileStatement(FunctionCompiler& ctx, Statement& st, ls_type_kind return_kind, ls_string_view current_label);
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
		, ref_local_offsets(*bytecode->arena) {}

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
	ExpArray<u32> ref_local_offsets;
	// Locals and temporaries share one absolute frame offset space. `next_local_offset`
	// is the floor below which temporaries must not be rewound, while `temp_top`
	// is the next free byte for expression temporaries.
	u32 temp_top = 0;
	u32 frame_high_water = 0;
	u32 max_local_count = 0;
	u32 next_local_offset = 0;
	const ExpArray<FunctionInfo>* functions = nullptr;

	bool isRefLocal(const BytecodeLocalBinding& local) const {
		for (u32 offset : ref_local_offsets) {
			if (offset == local.offset) return true;
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

	u32 addLocal(ls_string_view name, ResolvedType* type, ls_type_kind kind, bool preserve_temp_top = false) {
		BytecodeLocalBinding& binding = locals.emplace_back();
		binding.name = name;
		binding.type = type;
		binding.kind = kind;
		binding.byte_size = type ? typeByteSize(type) : typeKindByteSize(kind);
		if (binding.byte_size == 0u) binding.byte_size = 1u;
		const bool has_live_temps = temp_top > next_local_offset;
		binding.offset = has_live_temps ? temp_top : next_local_offset;
		next_local_offset = binding.offset + binding.byte_size;
		if (!preserve_temp_top) temp_top = next_local_offset;
		const u32 local_end = binding.offset + binding.byte_size;
		if (local_end > max_local_count) max_local_count = local_end;
		if (local_end > frame_high_water) frame_high_water = local_end;
		return binding.offset;
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

static u32 typeByteSize(ResolvedType* type) {
	if (!type) return 1u;
	switch (type->kind) {
		case ResolvedType::VOID:
			return 0u;
		case ResolvedType::BOOL:
		case ResolvedType::I8:
		case ResolvedType::U8:
		case ResolvedType::BYTE:
			return 1u;
		case ResolvedType::I16:
		case ResolvedType::U16:
			return 2u;
		case ResolvedType::I32:
		case ResolvedType::U32:
		case ResolvedType::F32:
		case ResolvedType::FUNCTION:
		case ResolvedType::ENUM:
			return 4u;
		case ResolvedType::I64:
		case ResolvedType::U64:
		case ResolvedType::ISIZE:
		case ResolvedType::F64:
		case ResolvedType::STRING:
		case ResolvedType::CPTR:
			return 8u;
		case ResolvedType::NULLABLE:
			return 1u + typeByteSize(static_cast<NullableResolvedType*>(type)->inner);
		case ResolvedType::ARRAY: {
			ArrayResolvedType* arr = static_cast<ArrayResolvedType*>(type);
			return arr->size > 0 ? (u32)(arr->size * typeByteSize(arr->element_type)) : 1u;
		}
		case ResolvedType::SLICE:
			return 16u;
		case ResolvedType::STRUCT: {
			StructResolvedType* st = static_cast<StructResolvedType*>(type);
			if (!st->decl) return 1u;
			u32 count = 0u;
			for (u32 i = 0; i < st->decl->fields.size(); ++i) {
				ResolvedType* field_type = i < st->field_types.size()
					? st->field_types[i]
					: st->decl->fields[i].resolved_type;
				count += typeByteSize(field_type);
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

static bool enumMemberIndex(EnumResolvedType& en, ls_string_view name, u32& out_index) {
	if (!en.decl) return false;
	for (u32 i = 0; i < en.decl->members.size(); ++i) {
		if (!equalStrings(en.decl->members[i].name, name)) continue;
		out_index = i;
		return true;
	}
	return false;
}

static bool structFieldByteOffset(StructResolvedType& st, ls_string_view name, u32& out_offset, ResolvedType*& out_type) {
	if (!st.decl) return false;
	u32 offset = 0u;
	for (u32 i = 0; i < st.decl->fields.size(); ++i) {
		NamedDecl& field = st.decl->fields[i];
		ResolvedType* field_type = i < st.field_types.size() ? st.field_types[i] : field.resolved_type;
		if (equalStrings(field.name, name)) {
			out_offset = offset;
			out_type = field_type;
			return true;
		}
		offset += typeByteSize(field_type);
	}
	return false;
}

static ls_type_kind compileValueAsType(FunctionCompiler& ctx, Expression* expr, ResolvedType* expected_type);
static ls_type_kind compileExpression(FunctionCompiler& ctx, Expression* expr, ls_type_kind hint);
static bool emitReference(FunctionCompiler& ctx, Expression& expr);

static bool compileCallArgs(FunctionCompiler& ctx, CallExpression& expr, const FunctionResolvedType& fn_type, u32 arg_offset) {
	for (u32 i = 0; i < expr.args.size(); ++i) {
		const u32 param_index = arg_offset + i;
		ResolvedType* param_type = param_index < fn_type.param_types.size()
			? fn_type.param_types[param_index]
			: nullptr;
		const bool is_ref = fn_type.decl
			&& param_index < fn_type.decl->runtime_params.size()
			&& fn_type.decl->runtime_params[param_index].is_ref;
		if (is_ref) {
			Expression* arg = expr.args[i];
			if (!arg || arg->kind != Expression::UNARY) return false;
			UnaryExpression* un = static_cast<UnaryExpression*>(arg);
			if (un->op != Token::REF || !un->expression || !emitReference(ctx, *un->expression)) return false;
			continue;
		}
		if (compileValueAsType(ctx, expr.args[i], param_type) == LS_TYPE_INVALID) return false;
	}
	return true;
}

static void emitCallDirect(FunctionCompiler& ctx, u32 callee_index, u32 arg_size, u32 return_size);

// Byte width of a callee's argument window, matching how arguments are pushed
// (reference parameters occupy a pointer, everything else its value width).
static u32 callArgWindowSize(const FunctionResolvedType& fn_type) {
	u32 total = 0u;
	for (u32 i = 0; i < fn_type.param_types.size(); ++i) {
		const bool is_ref = fn_type.decl
			&& i < fn_type.decl->runtime_params.size()
			&& fn_type.decl->runtime_params[i].is_ref;
		const u32 byte_size = is_ref ? typeKindByteSize(LS_TYPE_CPTR) : typeByteSize(fn_type.param_types[i]);
		total += byte_size == 0u ? 1u : byte_size;
	}
	return total;
}

static ls_type_kind emitDirectCall(FunctionCompiler& ctx, CallExpression& expr, FunctionExpression& fn, Expression* receiver, u32 arg_offset, ls_type_kind hint) {
	FunctionResolvedType* fn_type = fn.resolved_type ? static_cast<FunctionResolvedType*>(fn.resolved_type) : nullptr;
	if (!fn_type) return LS_TYPE_INVALID;
	if (receiver) {
		const ls_type_kind receiver_kind = fn_type && !fn_type->param_types.empty()
			? valueKindForType(fn_type->param_types[0], LS_TYPE_INVALID)
			: LS_TYPE_INVALID;
		if (compileExpression(ctx, receiver, receiver_kind) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
	}
	if (!compileCallArgs(ctx, expr, *fn_type, arg_offset)) return LS_TYPE_INVALID;
	emitCallDirect(ctx, fn.bytecode_index, callArgWindowSize(*fn_type), typeByteSize(fn_type->return_type));
	const ls_type_kind return_hint = hint != LS_TYPE_INVALID ? hint : LS_TYPE_I32;
	return fn_type->return_type ? valueKindForType(fn_type->return_type, return_hint) : return_hint;
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


static ls_type_kind compileValueAsType(FunctionCompiler& ctx, Expression* expr, ResolvedType* expected_type);

static ls_type_kind emitOperatorCall(
	FunctionCompiler& ctx,
	FunctionExpression& fn,
	Expression* lhs,
	Expression* rhs,
	Token::Type op,
	ls_type_kind hint
) {
	FunctionResolvedType* fn_type = fn.resolved_type ? static_cast<FunctionResolvedType*>(fn.resolved_type) : nullptr;
	if (!fn_type) return LS_TYPE_INVALID;
	if (fn_type->param_types.size() == 1u) {
		if (compileValueAsType(ctx, lhs, fn_type->param_types[0]) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
	}
	else {
		if (compileValueAsType(ctx, lhs, fn_type->param_types[0]) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
		if (compileValueAsType(ctx, rhs, fn_type->param_types[1]) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
	}
	emitCallDirect(ctx, fn.bytecode_index, callArgWindowSize(*fn_type), typeByteSize(fn_type->return_type));
	const ls_type_kind return_hint = hint != LS_TYPE_INVALID ? hint : LS_TYPE_I32;
	return fn_type->return_type ? valueKindForType(fn_type->return_type, return_hint) : return_hint;
}

static void appendStringLiteral(ls_bytecode& bytecode, const ls_string_view& value, u32& out_index) {
	ASSERT(bytecode.arena);
	ls_string_view* entry = appendArenaArray(*bytecode.arena, bytecode.strings, bytecode.string_count, bytecode.string_capacity);
	ASSERT(entry);
	*entry = value;
	out_index = bytecode.string_count - 1u;
}

static ls_function_bc* appendFunction(ls_bytecode& bytecode) {
	ASSERT(bytecode.arena);
	return appendArenaArray(*bytecode.arena, bytecode.functions, bytecode.function_count, bytecode.function_capacity);
}

// The compiler emits register-form bytecode directly. An expression's result
// occupies a temporary register at `ctx.temp_top`, which the bump/pop allocator
// advances and rewinds as operators produce and consume values.

// Set the temporary top and track the deepest allocation for frame sizing.
static void setTempTop(FunctionCompiler& ctx, u32 new_top) {
	ctx.temp_top = new_top;
	if (new_top > ctx.frame_high_water) ctx.frame_high_water = new_top;
}

// Emit a temporary register operand. Temporaries use absolute frame offsets
// because locals and temps are allocated from the same frame offset space.
static void emitTempReg(FunctionCompiler& ctx, u32 offset) {
	emitU32(ctx.code, offset);
}

// Emit a fixed frame/global operand: an absolute byte offset (parameter, local,
// global, or an immediate that must not be relocated).
static void emitFixedReg(FunctionCompiler& ctx, u32 offset) {
	emitU32(ctx.code, offset);
}

static void emitConst1(FunctionCompiler& ctx, u8 value) {
	const u32 dst = ctx.temp_top;
	emitOp(ctx.code, LS_OP_LOAD_CONST_1);
	emitTempReg(ctx, dst);
	emitU8(ctx.code, value);
	setTempTop(ctx, dst + 1u);
}

static void emitConst1At(FunctionCompiler& ctx, u32 dst, u8 value) {
	emitOp(ctx.code, LS_OP_LOAD_CONST_1);
	emitFixedReg(ctx, dst);
	emitU8(ctx.code, value);
}

static void emitConst2(FunctionCompiler& ctx, u16 value) {
	const u32 dst = ctx.temp_top;
	emitOp(ctx.code, LS_OP_LOAD_CONST_2);
	emitTempReg(ctx, dst);
	emitBytes(ctx.code, &value, sizeof(value));
	setTempTop(ctx, dst + 2u);
}

static void emitConst2At(FunctionCompiler& ctx, u32 dst, u16 value) {
	emitOp(ctx.code, LS_OP_LOAD_CONST_2);
	emitFixedReg(ctx, dst);
	emitBytes(ctx.code, &value, sizeof(value));
}

static void emitConst4(FunctionCompiler& ctx, u32 value) {
	const u32 dst = ctx.temp_top;
	emitOp(ctx.code, LS_OP_LOAD_CONST_4);
	emitTempReg(ctx, dst);
	emitU32(ctx.code, value);
	setTempTop(ctx, dst + 4u);
}

static void emitConst4At(FunctionCompiler& ctx, u32 dst, u32 value) {
	emitOp(ctx.code, LS_OP_LOAD_CONST_4);
	emitFixedReg(ctx, dst);
	emitU32(ctx.code, value);
}

static void emitConst8(FunctionCompiler& ctx, u64 value) {
	const u32 dst = ctx.temp_top;
	emitOp(ctx.code, LS_OP_LOAD_CONST_8);
	emitTempReg(ctx, dst);
	emitU64(ctx.code, value);
	setTempTop(ctx, dst + 8u);
}

static void emitConst8At(FunctionCompiler& ctx, u32 dst, u64 value) {
	emitOp(ctx.code, LS_OP_LOAD_CONST_8);
	emitFixedReg(ctx, dst);
	emitU64(ctx.code, value);
}

static void emitIntegerConstant(FunctionCompiler& ctx, ls_type_kind kind, u64 value) {
	switch (typeKindByteSize(kind)) {
		case 1u: emitConst1(ctx, (u8)value); break;
		case 2u: emitConst2(ctx, (u16)value); break;
		case 4u: emitConst4(ctx, (u32)value); break;
		default: emitConst8(ctx, value); break;
	}
}

static void emitIntegerConstantAt(FunctionCompiler& ctx, u32 dst, ls_type_kind kind, u64 value) {
	switch (typeKindByteSize(kind)) {
		case 1u: emitConst1At(ctx, dst, (u8)value); break;
		case 2u: emitConst2At(ctx, dst, (u16)value); break;
		case 4u: emitConst4At(ctx, dst, (u32)value); break;
		default: emitConst8At(ctx, dst, value); break;
	}
}

static void emitZeroBytes(FunctionCompiler& ctx, u32 byte_size) {
	while (byte_size >= 8u) { emitConst8(ctx, 0u); byte_size -= 8u; }
	if (byte_size >= 4u) { emitConst4(ctx, 0u); byte_size -= 4u; }
	if (byte_size >= 2u) { emitConst2(ctx, 0u); byte_size -= 2u; }
	if (byte_size == 1u) emitConst1(ctx, 0u);
}

static void emitConstString(FunctionCompiler& ctx, u32 string_index) {
	const u32 dst = ctx.temp_top;
	emitOp(ctx.code, LS_OP_LOAD_CONST_STRING);
	emitTempReg(ctx, dst);
	emitU32(ctx.code, string_index);
	setTempTop(ctx, dst + typeKindByteSize(LS_TYPE_STRING));
}

static void emitConstStringAt(FunctionCompiler& ctx, u32 dst, u32 string_index) {
	emitOp(ctx.code, LS_OP_LOAD_CONST_STRING);
	emitFixedReg(ctx, dst);
	emitU32(ctx.code, string_index);
}

static void emitLoadLocalBytes(FunctionCompiler& ctx, u32 offset, u32 byte_size) {
	const u32 dst = ctx.temp_top;
	emitOp(ctx.code, LS_OP_COPY);
	emitTempReg(ctx, dst);
	emitFixedReg(ctx, offset);
	emitU32(ctx.code, byte_size);
	setTempTop(ctx, dst + byte_size);
}

static void emitStoreLocalBytes(FunctionCompiler& ctx, u32 offset, u32 byte_size, bool clamp_to_locals = true) {
	const u32 src = ctx.temp_top - byte_size;
	emitOp(ctx.code, LS_OP_COPY);
	emitFixedReg(ctx, offset);
	emitTempReg(ctx, src);
	emitU32(ctx.code, byte_size);
	ctx.temp_top = clamp_to_locals && src < ctx.next_local_offset ? ctx.next_local_offset : src;
}

static void emitLoadGlobalBytes(FunctionCompiler& ctx, u32 offset, u32 byte_size) {
	const u32 dst = ctx.temp_top;
	emitOp(ctx.code, LS_OP_GLOBAL_LOAD);
	emitTempReg(ctx, dst);
	emitFixedReg(ctx, offset);
	emitU32(ctx.code, byte_size);
	setTempTop(ctx, dst + byte_size);
}

static void emitStoreGlobalBytes(FunctionCompiler& ctx, u32 offset, u32 byte_size) {
	const u32 src = ctx.temp_top - byte_size;
	emitOp(ctx.code, LS_OP_GLOBAL_STORE);
	emitFixedReg(ctx, offset);
	emitTempReg(ctx, src);
	emitU32(ctx.code, byte_size);
	ctx.temp_top = src > ctx.next_local_offset ? src : ctx.next_local_offset;
}

static void emitLocalRef(FunctionCompiler& ctx, u32 offset) {
	const u32 dst = ctx.temp_top;
	emitOp(ctx.code, LS_OP_LOCAL_REF);
	emitTempReg(ctx, dst);
	emitFixedReg(ctx, offset);
	setTempTop(ctx, dst + typeKindByteSize(LS_TYPE_CPTR));
}

static void emitGlobalRef(FunctionCompiler& ctx, u32 offset) {
	const u32 dst = ctx.temp_top;
	emitOp(ctx.code, LS_OP_GLOBAL_REF);
	emitTempReg(ctx, dst);
	emitFixedReg(ctx, offset);
	setTempTop(ctx, dst + typeKindByteSize(LS_TYPE_CPTR));
}

// Binary arithmetic/logic op: consumes two `size`-byte operands, produces one.
static void emitBinaryOp(FunctionCompiler& ctx, ls_op op, u32 size) {
	const u32 lhs = ctx.temp_top - size * 2u;
	const u32 rhs = ctx.temp_top - size;
	emitOp(ctx.code, op);
	emitTempReg(ctx, lhs);
	emitTempReg(ctx, lhs);
	emitTempReg(ctx, rhs);
	ctx.temp_top = lhs + size;
}

static void emitCompareOp(FunctionCompiler& ctx, ls_op op, ls_type_kind kind) {
	const u32 size = typeKindByteSize(kind);
	const u32 lhs = ctx.temp_top - size * 2u;
	const u32 rhs = ctx.temp_top - size;
	emitOp(ctx.code, op);
	emitTempReg(ctx, lhs);
	emitTempReg(ctx, lhs);
	emitTempReg(ctx, rhs);
	emitU8(ctx.code, (u8)kind);
	ctx.temp_top = lhs + 1u;
}

static void emitUnaryOp(FunctionCompiler& ctx, ls_op op, u32 size) {
	const u32 src = ctx.temp_top - size;
	emitOp(ctx.code, op);
	emitTempReg(ctx, src);
}

static void emitCast(FunctionCompiler& ctx, ls_type_kind src_kind, ls_type_kind dst_kind) {
	const u32 src_size = typeKindByteSize(src_kind);
	const u32 src = ctx.temp_top - src_size;
	emitOp(ctx.code, LS_OP_CAST);
	emitTempReg(ctx, src);
	emitTempReg(ctx, src);
	emitU8(ctx.code, (u8)src_kind);
	emitU8(ctx.code, (u8)dst_kind);
	setTempTop(ctx, src + typeKindByteSize(dst_kind));
}

// Discard the top `byte_size` bytes of temporaries (compile-time only).
static void emitPop(FunctionCompiler& ctx, u32 byte_size) {
	ctx.temp_top -= byte_size;
}

static void emitLoadAt(FunctionCompiler& ctx, u32 scale, i32 offset, u32 size) {
	const u32 index = ctx.temp_top - typeKindByteSize(LS_TYPE_I64);
	const u32 base = index - typeKindByteSize(LS_TYPE_CPTR);
	emitOp(ctx.code, LS_OP_LOAD_AT);
	emitTempReg(ctx, base);
	emitTempReg(ctx, base);
	emitTempReg(ctx, index);
	emitU32(ctx.code, scale);
	emitI32(ctx.code, offset);
	emitU32(ctx.code, size);
	setTempTop(ctx, base + size);
}

static void emitStoreAt(FunctionCompiler& ctx, u32 scale, i32 offset, u32 size) {
	const u32 value = ctx.temp_top - size;
	const u32 index = value - typeKindByteSize(LS_TYPE_I64);
	const u32 base = index - typeKindByteSize(LS_TYPE_CPTR);
	emitOp(ctx.code, LS_OP_STORE_AT);
	emitTempReg(ctx, base);
	emitTempReg(ctx, index);
	emitTempReg(ctx, value);
	emitU32(ctx.code, scale);
	emitI32(ctx.code, offset);
	emitU32(ctx.code, size);
	ctx.temp_top = base;
}

static void emitRefAt(FunctionCompiler& ctx, u32 scale, i32 offset) {
	const u32 index = ctx.temp_top - typeKindByteSize(LS_TYPE_I64);
	const u32 base = index - typeKindByteSize(LS_TYPE_CPTR);
	emitOp(ctx.code, LS_OP_REF_AT);
	emitTempReg(ctx, base);
	emitTempReg(ctx, base);
	emitTempReg(ctx, index);
	emitU32(ctx.code, scale);
	emitI32(ctx.code, offset);
	setTempTop(ctx, base + typeKindByteSize(LS_TYPE_CPTR));
}

static void emitSliceLoad(FunctionCompiler& ctx, u32 element_size) {
	const u32 index = ctx.temp_top - typeKindByteSize(LS_TYPE_I64);
	const u32 slice = index - typeKindByteSize(LS_TYPE_SLICE);
	emitOp(ctx.code, LS_OP_SLICE_LOAD);
	emitTempReg(ctx, slice);
	emitTempReg(ctx, slice);
	emitTempReg(ctx, index);
	emitU32(ctx.code, element_size);
	setTempTop(ctx, slice + element_size);
}

static void emitSliceStore(FunctionCompiler& ctx, u32 element_size) {
	const u32 value = ctx.temp_top - element_size;
	const u32 index = value - typeKindByteSize(LS_TYPE_I64);
	const u32 slice = index - typeKindByteSize(LS_TYPE_SLICE);
	emitOp(ctx.code, LS_OP_SLICE_STORE);
	emitTempReg(ctx, slice);
	emitTempReg(ctx, index);
	emitTempReg(ctx, value);
	emitU32(ctx.code, element_size);
	ctx.temp_top = slice;
}

static void emitSliceOp(FunctionCompiler& ctx, u32 element_size) {
	const u32 end = ctx.temp_top - typeKindByteSize(LS_TYPE_I64);
	const u32 begin = end - typeKindByteSize(LS_TYPE_I64);
	const u32 slice = begin - typeKindByteSize(LS_TYPE_SLICE);
	emitOp(ctx.code, LS_OP_SLICE);
	emitTempReg(ctx, slice);
	emitTempReg(ctx, slice);
	emitTempReg(ctx, begin);
	emitTempReg(ctx, end);
	emitU32(ctx.code, element_size);
	setTempTop(ctx, slice + typeKindByteSize(LS_TYPE_SLICE));
}

static void emitSliceLength(FunctionCompiler& ctx) {
	const u32 slice = ctx.temp_top - typeKindByteSize(LS_TYPE_SLICE);
	emitOp(ctx.code, LS_OP_SLICE_LENGTH);
	emitTempReg(ctx, slice);
	emitTempReg(ctx, slice);
	setTempTop(ctx, slice + typeKindByteSize(LS_TYPE_I64));
}

static void emitCallDirect(FunctionCompiler& ctx, u32 callee_index, u32 arg_size, u32 return_size) {
	const u32 arg = ctx.temp_top - arg_size;
	emitOp(ctx.code, LS_OP_CALL_DIRECT);
	emitU32(ctx.code, callee_index);
	emitTempReg(ctx, ctx.temp_top);
	setTempTop(ctx, arg + return_size);
}

static void emitCallIndirect(FunctionCompiler& ctx, u32 arg_size, u32 return_size) {
	const u32 arg = ctx.temp_top - arg_size;
	const u32 callee = arg - typeKindByteSize(LS_TYPE_FUNCTION);
	emitOp(ctx.code, LS_OP_CALL_INDIRECT);
	emitTempReg(ctx, callee);
	emitU32(ctx.code, arg_size);
	emitU32(ctx.code, return_size);
	setTempTop(ctx, callee + return_size);
}

static void emitReturn(FunctionCompiler& ctx) {
	const u32 return_size = ctx.out.return_size;
	emitOp(ctx.code, LS_OP_RETURN);
	if (return_size > 0u) emitTempReg(ctx, ctx.temp_top - return_size);
	else emitFixedReg(ctx, 0u);
	emitU32(ctx.code, return_size);
	ctx.temp_top = ctx.next_local_offset;
}


// Size the frame and copy the finished code into the function's arena storage.
static bool finalizeFunctionCode(FunctionCompiler& ctx, ls_function_bc& function, ls_arena& arena) {
	function.frame_size = ctx.frame_high_water;
	function.code_size = (u32)ctx.code.size();
	function.code_capacity = function.code_size;
	if (function.code_size > 0u) {
		function.code = static_cast<u8*>(arena.allocate(arena.user_data, function.code_size, alignof(u8)));
		if (!function.code) return false;
		copyMemory(function.code, ctx.code.data, function.code_size);
	}
	return true;
}

static const GlobalBinding* findGlobalBinding(const ExpArray<GlobalBinding>& globals, Symbol& sym);
static ls_type_kind compileExpression(FunctionCompiler& ctx, Expression* expr, ls_type_kind hint);
static bool emitReference(FunctionCompiler& ctx, Expression& expr);

static void emitZeroIndex(FunctionCompiler& ctx) {
	emitConst8(ctx, 0u);
}

static bool compileIndexExpression(FunctionCompiler& ctx, Expression* expr) {
	const ls_type_kind kind = compileExpression(ctx, expr, LS_TYPE_I64);
	if (kind == LS_TYPE_INVALID || !isIntegerKind(kind)) return false;
	if (kind != LS_TYPE_I64) {
		emitCast(ctx, kind, LS_TYPE_I64);
	}
	return true;
}

static void emitStaticBoundsCheck(FunctionCompiler& ctx, ResolvedType* type) {
	if (!type || type->kind != ResolvedType::ARRAY) return;
	emitOp(ctx.code, LS_OP_BOUNDS_CHECK);
	emitTempReg(ctx, ctx.temp_top - typeKindByteSize(LS_TYPE_I64));
	emitU64(ctx.code, (u64)static_cast<ArrayResolvedType*>(type)->size);
}

static bool emitReference(FunctionCompiler& ctx, Expression& expr) {
	switch (expr.kind) {
		case Expression::IDENTIFIER: {
			IdentifierExpression* id = static_cast<IdentifierExpression*>(&expr);
			if (BytecodeLocalBinding* local = ctx.findLocal(id->name)) {
				if (ctx.isRefLocal(*local)) {
					emitLoadLocalBytes(ctx, local->offset, typeKindByteSize(LS_TYPE_CPTR));
				}
				else {
					emitLocalRef(ctx, local->offset);
				}
				return true;
			}

			if (!id->symbol || !ctx.globals) return false;
			if (const GlobalBinding* global = findGlobalBinding(*ctx.globals, *id->symbol)) {
				emitGlobalRef(ctx, global->offset);
				return true;
			}
			return false;
		}
		case Expression::MEMBER: {
			MemberExpression* member = static_cast<MemberExpression*>(&expr);
			if (member->expression && member->expression->kind == Expression::IDENTIFIER
				&& ctx.module && ctx.unit && ctx.globals) {
				if (member->resolved_symbol) {
					if (const GlobalBinding* global = findGlobalBinding(*ctx.globals, *member->resolved_symbol)) {
						emitGlobalRef(ctx, global->offset);
						return true;
					}
				}
			}
			if (!member->expression || !member->expression->resolved_type) return false;
			if (member->expression->resolved_type->kind != ResolvedType::STRUCT) return false;
			StructResolvedType* st = static_cast<StructResolvedType*>(member->expression->resolved_type);
			u32 offset = 0u;
			ResolvedType* field_type = nullptr;
			if (!structFieldByteOffset(*st, member->name, offset, field_type)) return false;
			if (!emitReference(ctx, *member->expression)) return false;
			emitZeroIndex(ctx);
			emitRefAt(ctx, 1u, (i32)offset);
			return true;
		}
		case Expression::BRACKET: {
			BracketExpression* br = static_cast<BracketExpression*>(&expr);
			if (br->has_colon || br->args.size() != 1u || !br->resolved_type) return false;
			if (!br->base || !emitReference(ctx, *br->base)) return false;
			if (!compileIndexExpression(ctx, br->args[0])) return false;
			emitStaticBoundsCheck(ctx, br->base->resolved_type);
			emitRefAt(ctx, typeByteSize(br->resolved_type), 0);
			return true;
		}
		default:
			return false;
	}
}

static bool emitReferenceLoad(FunctionCompiler& ctx, Expression& expr, u32 byte_size) {
	if (byte_size == 0u) byte_size = 1u;
	const u32 result_offset = ctx.temp_top;
	const u32 ref_offset = ctx.addLocal({}, nullptr, LS_TYPE_I64, true);
	ctx.temp_top = ctx.next_local_offset;
	if (!emitReference(ctx, expr)) return false;
	emitStoreLocalBytes(ctx, ref_offset, typeKindByteSize(LS_TYPE_CPTR));
	emitLoadLocalBytes(ctx, ref_offset, typeKindByteSize(LS_TYPE_CPTR));
	emitZeroIndex(ctx);
	emitLoadAt(ctx, 1u, 0, byte_size);
	const u32 loaded_offset = ctx.temp_top - byte_size;
	if (loaded_offset != result_offset) {
		emitOp(ctx.code, LS_OP_COPY);
		emitTempReg(ctx, result_offset);
		emitTempReg(ctx, loaded_offset);
		emitU32(ctx.code, byte_size);
		ctx.temp_top = result_offset + byte_size;
	}
	return true;
}

static bool emitArrayBaseRef(FunctionCompiler& ctx, Expression& base) {
	return emitReference(ctx, base);
}

static ls_type_kind compileValueAsType(FunctionCompiler& ctx, Expression* expr, ResolvedType* expected_type) {
	if (expected_type && expected_type->kind == ResolvedType::NULLABLE) {
		NullableResolvedType* nullable = static_cast<NullableResolvedType*>(expected_type);
		const bool is_null = !expr || expr->kind == Expression::NULL_LITERAL || expr->kind == Expression::UNDEFINED;
		emitIntegerConstant(ctx, LS_TYPE_BOOL, is_null ? 0u : 1u);
		if (is_null) {
			emitZeroBytes(ctx, typeByteSize(nullable->inner));
		}
		else if (compileValueAsType(ctx, expr, nullable->inner) == LS_TYPE_INVALID) {
			return LS_TYPE_INVALID;
		}
		return LS_TYPE_NULL_VALUE;
	}
	// Slice conversions change representation, not just the reported type. Arrays
	// are inline values, whereas slices are an absolute backing reference and length.
	if (expected_type && expected_type->kind == ResolvedType::SLICE) {
		if (expr && expr->kind == Expression::NULL_LITERAL) {
			emitConst8(ctx, 0u);
			emitConst8(ctx, 0u);
			return LS_TYPE_SLICE;
		}
		if (expr && expr->resolved_type && expr->resolved_type->kind == ResolvedType::ARRAY) {
			ArrayResolvedType* array = static_cast<ArrayResolvedType*>(expr->resolved_type);
			if (!emitReference(ctx, *expr)) return LS_TYPE_INVALID;
			emitConst8(ctx, (u64)array->size);
			return LS_TYPE_SLICE;
		}
	}
	if (expected_type && expr && expr->kind == Expression::UNDEFINED) {
		emitZeroBytes(ctx, typeByteSize(expected_type));
		return valueKindForType(expected_type);
	}
	return compileExpression(ctx, expr, valueKindForType(expected_type, LS_TYPE_INVALID));
}

// Numeric opcode groups (ADD/SUB/MUL/DIV/MOD/NEG) are laid out in the same kind
// order, so the concrete opcode is the group base plus this per-kind index.
static bool numericKindIndex(ls_type_kind kind, u32& out_index) {
	switch (kind) {
		case LS_TYPE_I8:  out_index = 0u; return true;
		case LS_TYPE_U8:  out_index = 1u; return true;
		case LS_TYPE_I16: out_index = 2u; return true;
		case LS_TYPE_U16: out_index = 3u; return true;
		case LS_TYPE_I32: out_index = 4u; return true;
		case LS_TYPE_U32: out_index = 5u; return true;
		case LS_TYPE_I64: out_index = 6u; return true;
		case LS_TYPE_U64: out_index = 7u; return true;
		case LS_TYPE_F32: out_index = 8u; return true;
		case LS_TYPE_F64: out_index = 9u; return true;
		default: return false;
	}
}

static bool emitNumericBinary(FunctionCompiler& ctx, ls_op group_base, ls_type_kind kind, bool allow_float) {
	u32 index = 0u;
	if (!numericKindIndex(kind, index)) return false;
	if (!allow_float && index >= 8u) return false;
	emitBinaryOp(ctx, (ls_op)((u32)group_base + index), typeKindByteSize(kind));
	return true;
}

static bool emitNumericStoreOp(FunctionCompiler& ctx, ls_type_kind kind, Token::Type op) {
	switch (op) {
		case Token::PLUS_EQUAL:  return emitNumericBinary(ctx, LS_OP_ADD_I8, kind, true);
		case Token::MINUS_EQUAL: return emitNumericBinary(ctx, LS_OP_SUB_I8, kind, true);
		case Token::STAR_EQUAL:  return emitNumericBinary(ctx, LS_OP_MUL_I8, kind, true);
		case Token::SLASH_EQUAL: return emitNumericBinary(ctx, LS_OP_DIV_I8, kind, true);
		default: return false;
	}
}

static bool emitBracketLoad(FunctionCompiler& ctx, BracketExpression& br) {
	if (br.has_colon || br.args.size() != 1) return false;
	const u32 element_size = typeByteSize(br.resolved_type);
	if (element_size == 0u) return false;
	if (br.base->resolved_type && br.base->resolved_type->kind == ResolvedType::SLICE) {
		if (compileExpression(ctx, br.base, LS_TYPE_SLICE) == LS_TYPE_INVALID) return false;
		if (!compileIndexExpression(ctx, br.args[0])) return false;
		emitSliceLoad(ctx, element_size);
		return true;
	}
	if (!emitArrayBaseRef(ctx, *br.base)) return false;
	if (!compileIndexExpression(ctx, br.args[0])) return false;
	emitStaticBoundsCheck(ctx, br.base->resolved_type);
	emitLoadAt(ctx, element_size, 0, element_size);
	return true;
}

static bool emitBracketStore(FunctionCompiler& ctx, BracketExpression& br, Expression* rhs, ls_type_kind value_kind, Token::Type op) {
	if (br.has_colon || br.args.size() != 1) return false;
	const u32 element_size = typeByteSize(br.resolved_type);
	if (element_size == 0u) return false;
	const bool is_slice = br.base->resolved_type && br.base->resolved_type->kind == ResolvedType::SLICE;

	if (op == Token::EQUAL) {
		if (is_slice) {
			if (compileExpression(ctx, br.base, LS_TYPE_SLICE) == LS_TYPE_INVALID) return false;
		}
		else if (!emitArrayBaseRef(ctx, *br.base)) return false;
		if (!compileIndexExpression(ctx, br.args[0])) return false;
		if (!is_slice) emitStaticBoundsCheck(ctx, br.base->resolved_type);
		if (compileValueAsType(ctx, rhs, br.resolved_type) == LS_TYPE_INVALID) return false;
		if (is_slice) {
			emitSliceStore(ctx, element_size);
		}
		else {
			emitStoreAt(ctx, element_size, 0, element_size);
		}
		return true;
	}

	if (is_slice) {
		// Evaluate the base and index once. Compound assignment needs them for both
		// the checked load and checked store, and either expression may have effects.
		const u32 slice_offset = ctx.addLocal({}, br.base->resolved_type, LS_TYPE_SLICE);
		const u32 index_offset = ctx.addLocal({}, nullptr, LS_TYPE_I64);
		const u32 value_offset = ctx.addLocal({}, br.resolved_type, value_kind);
		const u32 slice_size = typeByteSize(br.base->resolved_type);
		const u32 index_size = typeKindByteSize(LS_TYPE_I64);
		const u32 value_size = typeByteSize(br.resolved_type);
		if (compileExpression(ctx, br.base, LS_TYPE_SLICE) == LS_TYPE_INVALID) return false;
		emitStoreLocalBytes(ctx, slice_offset, slice_size);
		if (!compileIndexExpression(ctx, br.args[0])) return false;
		emitStoreLocalBytes(ctx, index_offset, index_size);
		emitLoadLocalBytes(ctx, slice_offset, slice_size);
		emitLoadLocalBytes(ctx, index_offset, index_size);
		emitSliceLoad(ctx, element_size);
		if (compileExpression(ctx, rhs, value_kind) == LS_TYPE_INVALID) return false;
		if (!emitNumericStoreOp(ctx, value_kind, op)) return false;
		emitStoreLocalBytes(ctx, value_offset, value_size);
		emitLoadLocalBytes(ctx, slice_offset, slice_size);
		emitLoadLocalBytes(ctx, index_offset, index_size);
		emitLoadLocalBytes(ctx, value_offset, value_size);
		emitSliceStore(ctx, element_size);
		return true;
	}

	const u32 ref_offset = ctx.addLocal({}, nullptr, LS_TYPE_I64);
	const u32 index_offset = ctx.addLocal({}, nullptr, LS_TYPE_I64);
	const u32 ref_size = typeKindByteSize(LS_TYPE_CPTR);
	const u32 index_size = typeKindByteSize(LS_TYPE_I64);

	if (!emitArrayBaseRef(ctx, *br.base)) return false;
	emitStoreLocalBytes(ctx, ref_offset, ref_size);
	if (!compileIndexExpression(ctx, br.args[0])) return false;
	emitStoreLocalBytes(ctx, index_offset, index_size);

	emitLoadLocalBytes(ctx, ref_offset, ref_size);
	emitLoadLocalBytes(ctx, index_offset, index_size);
	emitStaticBoundsCheck(ctx, br.base->resolved_type);
	emitLoadAt(ctx, element_size, 0, element_size);

	if (compileExpression(ctx, rhs, value_kind) == LS_TYPE_INVALID) return false;
	if (!emitNumericStoreOp(ctx, value_kind, op)) return false;

	emitLoadLocalBytes(ctx, ref_offset, ref_size);
	emitLoadLocalBytes(ctx, index_offset, index_size);
	emitStaticBoundsCheck(ctx, br.base->resolved_type);
	emitStoreAt(ctx, element_size, 0, element_size);
	return true;
}

static bool emitSlice(FunctionCompiler& ctx, BracketExpression& br) {
	if (!br.has_colon || !br.base || !br.base->resolved_type) return false;
	ResolvedType* base_type = br.base->resolved_type;
	ResolvedType* element_type = nullptr;
	if (base_type->kind == ResolvedType::ARRAY) {
		ArrayResolvedType* array = static_cast<ArrayResolvedType*>(base_type);
		element_type = array->element_type;
		if (!emitReference(ctx, *br.base)) return false;
		emitConst8(ctx, (u64)array->size);
	}
	else if (base_type->kind == ResolvedType::SLICE) {
		element_type = static_cast<SliceResolvedType*>(base_type)->element_type;
		if (compileExpression(ctx, br.base, LS_TYPE_SLICE) == LS_TYPE_INVALID) return false;
	}
	else {
		return false;
	}

	// Save the source pair because an omitted end bound reuses its dynamic length,
	// while explicit bounds may themselves evaluate arbitrary expressions.
	const u32 source_offset = ctx.addLocal({}, br.resolved_type, LS_TYPE_SLICE, true);
	const u32 slice_size = typeByteSize(br.resolved_type);
	emitStoreLocalBytes(ctx, source_offset, slice_size);
	emitLoadLocalBytes(ctx, source_offset, slice_size);
	if (!br.args.empty()) {
		if (!compileIndexExpression(ctx, br.args[0])) return false;
	}
	else {
		emitIntegerConstant(ctx, LS_TYPE_I64, 0u);
	}
	if (br.end) {
		if (!compileIndexExpression(ctx, br.end)) return false;
	}
	else {
		emitLoadLocalBytes(ctx, source_offset + typeKindByteSize(LS_TYPE_CPTR), typeKindByteSize(LS_TYPE_I64));
	}
	emitSliceOp(ctx, typeByteSize(element_type));
	return true;
}

static void patchJumpRelative(ByteArray& code, u32 operand_pos, u32 target_pos) {
	patchI32(code, operand_pos, (i32)((i32)target_pos - (i32)(operand_pos + 4u)));
}

// Emit a jump with a placeholder relative offset; returns the position of the
// offset operand for later patching. Conditional jumps read (and consume) the
// topmost boolean temporary.
static u32 emitJumpPlaceholder(FunctionCompiler& ctx, ls_op op) {
	emitOp(ctx.code, op);
	if (op == LS_OP_JUMP_IF_FALSE || op == LS_OP_JUMP_IF_TRUE) {
		emitTempReg(ctx, ctx.temp_top - 1u);
		ctx.temp_top -= 1u;
	}
	const u32 operand_pos = (u32)ctx.code.size();
	emitI32(ctx.code, 0);
	return operand_pos;
}

static void emitDeferredStatements(FunctionCompiler& ctx, u32 defer_mark, ls_type_kind return_kind, ls_string_view current_label) {
	for (i32 i = (i32)ctx.deferreds.size() - 1; i >= (i32)defer_mark; --i) {
		(void)compileStatement(ctx, *ctx.deferreds[(u32)i], return_kind, current_label);
	}
}

static const GlobalBinding* findGlobalBinding(const ExpArray<GlobalBinding>& globals, Symbol& sym) {
	for (const GlobalBinding& binding : globals) {
		if (binding.sym == &sym) return &binding;
	}
	return nullptr;
}

static ls_type_kind compileExpression(FunctionCompiler& ctx, Expression* expr, ls_type_kind hint);

static ls_type_kind compileBinary(FunctionCompiler& ctx, BinaryExpression& expr_ref, ls_type_kind hint) {
	BinaryExpression* expr = &expr_ref;
	if (expr->op == Token::AND || expr->op == Token::OR) {
		if (compileExpression(ctx, expr->lhs, LS_TYPE_BOOL) != LS_TYPE_BOOL) return LS_TYPE_INVALID;
		const ls_op short_circuit_op = expr->op == Token::AND ? LS_OP_JUMP_IF_FALSE : LS_OP_JUMP_IF_TRUE;
		const u32 short_circuit_jump = emitJumpPlaceholder(ctx, short_circuit_op);
		// The short-circuit result and the evaluated-rhs result share one register,
		// so both paths must produce it at the same temporary offset.
		const u32 result_top = ctx.temp_top;

		if (compileExpression(ctx, expr->rhs, LS_TYPE_BOOL) != LS_TYPE_BOOL) return LS_TYPE_INVALID;
		const u32 end_jump = emitJumpPlaceholder(ctx, LS_OP_JUMP);

		patchJumpRelative(ctx.code, short_circuit_jump, (u32)ctx.code.size());
		ctx.temp_top = result_top;
		emitConst1(ctx, expr->op == Token::AND ? 0u : 1u);
		patchJumpRelative(ctx.code, end_jump, (u32)ctx.code.size());
		return LS_TYPE_BOOL;
	}

	if (expr->resolved_fn) {
		return emitOperatorCall(ctx, *expr->resolved_fn, expr->lhs, expr->rhs, expr->op, hint);
	}

	// Null check: `nullable == null` or `nullable != null` — only check has_value offset.
	if (expr->op == Token::EQUAL_EQUAL || expr->op == Token::BANG_EQUAL) {
		Expression* nullable_side = nullptr;
		if (expr->rhs && expr->rhs->kind == Expression::NULL_LITERAL) nullable_side = expr->lhs;
		else if (expr->lhs && expr->lhs->kind == Expression::NULL_LITERAL) nullable_side = expr->rhs;
		if (nullable_side && nullable_side->resolved_type && nullable_side->resolved_type->kind == ResolvedType::NULLABLE) {
			// Compile nullable (pushes has_value, value); pop value, compare has_value to 0.
			if (compileExpression(ctx, nullable_side, LS_TYPE_NULL_VALUE) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
			NullableResolvedType* nullable_type = static_cast<NullableResolvedType*>(nullable_side->resolved_type);
			emitPop(ctx, typeByteSize(nullable_type->inner)); // discard value bytes, has_value remains
			emitIntegerConstant(ctx, LS_TYPE_BOOL, 0u);
			emitCompareOp(ctx, expr->op == Token::EQUAL_EQUAL ? LS_OP_EQ : LS_OP_NE, LS_TYPE_BOOL);
			return LS_TYPE_BOOL;
		}
	}

	const ls_type_kind lhs_hint = numericKindForOp(expr->lhs ? defaultLiteralKind(*expr->lhs, hint) : LS_TYPE_INVALID, hint);
	const ls_type_kind rhs_hint = numericKindForOp(expr->rhs ? defaultLiteralKind(*expr->rhs, lhs_hint) : LS_TYPE_INVALID, lhs_hint);
	ls_type_kind lhs_kind = compileExpression(ctx, expr->lhs, lhs_hint);
	ls_type_kind rhs_kind = compileExpression(ctx, expr->rhs, rhs_hint);
	if (lhs_kind == LS_TYPE_INVALID) lhs_kind = rhs_kind;
	const ls_type_kind kind = numericKindForOp(lhs_kind, rhs_kind);

	switch (expr->op) {
		case Token::PLUS:
			return emitNumericBinary(ctx, LS_OP_ADD_I8, kind, true) ? kind : LS_TYPE_INVALID;
		case Token::MINUS:
			return emitNumericBinary(ctx, LS_OP_SUB_I8, kind, true) ? kind : LS_TYPE_INVALID;
		case Token::STAR:
			return emitNumericBinary(ctx, LS_OP_MUL_I8, kind, true) ? kind : LS_TYPE_INVALID;
		case Token::SLASH:
			return emitNumericBinary(ctx, LS_OP_DIV_I8, kind, true) ? kind : LS_TYPE_INVALID;
		case Token::PERCENT:
			return emitNumericBinary(ctx, LS_OP_MOD_I8, kind, false) ? kind : LS_TYPE_INVALID;
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
			ls_op cmp_op;
			switch (expr->op) {
				case Token::EQUAL_EQUAL: cmp_op = LS_OP_EQ; break;
				case Token::BANG_EQUAL: cmp_op = LS_OP_NE; break;
				case Token::LT: cmp_op = LS_OP_LT; break;
				case Token::LT_EQUAL: cmp_op = LS_OP_LE; break;
				case Token::GT: cmp_op = LS_OP_GT; break;
				case Token::GT_EQUAL: cmp_op = LS_OP_GE; break;
				default: return LS_TYPE_INVALID;
			}
			emitCompareOp(ctx, cmp_op, cmp_kind);
			return LS_TYPE_BOOL;
		}
		default:
			return LS_TYPE_INVALID;
	}
}

static ls_type_kind compileCall(FunctionCompiler& ctx, CallExpression& expr_ref, ls_type_kind hint) {
	CallExpression* expr = &expr_ref;
	// If the type checker already resolved the direct call target, use it and return.
	// Handles both template instantiation and UFCS function selection without
	// duplicating the lookup in each callee-shape branch below.
	if (expr->resolved_fn) {
		u32 arg_offset = 0;
		Expression* receiver = nullptr;
		if (expr->callee && expr->callee->kind == Expression::MEMBER) {
			MemberExpression* member = static_cast<MemberExpression*>(expr->callee);
			if (member->expression && member->expression->resolved_type) {
				receiver = member->expression;
				arg_offset = 1;
			}
		}
		return emitDirectCall(ctx, *expr, *expr->resolved_fn, receiver, arg_offset, hint);
	}

	if (expr->callee && expr->callee->kind == Expression::IDENTIFIER) {
		IdentifierExpression* id = static_cast<IdentifierExpression*>(expr->callee);
		if (equalStrings(id->name, makeStringView("length"))) {
			if (expr->args.size() != 1u || !expr->args[0]->resolved_type) return LS_TYPE_INVALID;
			ResolvedType* arg_type = expr->args[0]->resolved_type;
			if (arg_type->kind == ResolvedType::ARRAY) {
				emitConst8(ctx, (u64)static_cast<ArrayResolvedType*>(arg_type)->size);
			}
			else if (arg_type->kind == ResolvedType::SLICE) {
				if (compileExpression(ctx, expr->args[0], LS_TYPE_SLICE) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
				emitSliceLength(ctx);
			}
			else {
				return LS_TYPE_INVALID;
			}
			return LS_TYPE_I64;
		}
		if (BytecodeLocalBinding* local = ctx.findLocal(id->name)) {
			if (local->kind != LS_TYPE_FUNCTION) return LS_TYPE_INVALID;
		}
		else {
			if (!id->symbol || !id->symbol->expression || id->symbol->expression->kind != Expression::FUNCTION) return LS_TYPE_INVALID;
			FunctionExpression* fn = static_cast<FunctionExpression*>(id->symbol->expression);
			return emitDirectCall(ctx, *expr, *fn, nullptr, 0u, hint);
		}
	}

	if (expr->callee && expr->callee->kind == Expression::MEMBER) {
		MemberExpression* member = static_cast<MemberExpression*>(expr->callee);
		if (member->resolved_fn) {
			return emitDirectCall(ctx, *expr, *member->resolved_fn, nullptr, 0u, hint);
		}
	}

	if (expr->callee && expr->callee->kind == Expression::BRACKET && expr->callee->resolved_type && expr->callee->resolved_type->kind == ResolvedType::FUNCTION) {
		FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(expr->callee->resolved_type);
		if (fn_type->decl) {
			return emitDirectCall(ctx, *expr, *fn_type->decl, nullptr, 0u, hint);
		}
	}

	// Indirect call: callee value sits below the argument list.
	if (compileExpression(ctx, expr->callee, LS_TYPE_FUNCTION) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
	const FunctionResolvedType* fn_type = expr->callee && expr->callee->resolved_type
		&& expr->callee->resolved_type->kind == ResolvedType::FUNCTION
		? static_cast<FunctionResolvedType*>(expr->callee->resolved_type)
		: nullptr;
	if (!fn_type) return LS_TYPE_INVALID;
	if (!compileCallArgs(ctx, *expr, *fn_type, 0u)) return LS_TYPE_INVALID;
	emitCallIndirect(ctx, callArgWindowSize(*fn_type), typeByteSize(fn_type->return_type));
	return fn_type->return_type
		? valueKindForType(fn_type->return_type, hint != LS_TYPE_INVALID ? hint : LS_TYPE_I32)
		: hint;
}

static ls_type_kind compileExpression(FunctionCompiler& ctx, Expression* expr, ls_type_kind hint) {
	if (!expr) return LS_TYPE_VOID;
	switch (expr->kind) {
		case Expression::INT_LITERAL: {
			const ls_type_kind kind = expr->resolved_type ? toTypeKind(expr->resolved_type) : defaultLiteralKind(*expr, hint);
			const u64 int_value = static_cast<IntLiteralExpression*>(expr)->value;
			if (kind == LS_TYPE_F32) {
				emitConst4(ctx, bitcastF32ToU32((float)int_value));
			} else if (kind == LS_TYPE_F64) {
				emitConst8(ctx, bitcastF64ToU64((double)int_value));
			} else {
				emitIntegerConstant(ctx, kind, int_value);
			}
			return kind;
		}
		case Expression::FLOAT_LITERAL: {
			const ls_type_kind kind = defaultLiteralKind(*expr, hint);
			if (kind == LS_TYPE_F32) {
				const float value = static_cast<float>(static_cast<FloatLiteralExpression*>(expr)->value);
				emitConst4(ctx, bitcastF32ToU32(value));
			}
			else {
				emitConst8(ctx, bitcastF64ToU64(static_cast<FloatLiteralExpression*>(expr)->value));
			}
			return kind;
		}
		case Expression::BOOL_LITERAL: {
			emitIntegerConstant(ctx, LS_TYPE_BOOL, static_cast<BoolLiteralExpression*>(expr)->value ? 1u : 0u);
			return LS_TYPE_BOOL;
		}
		case Expression::STRING_LITERAL: {
			u32 string_index = 0;
			appendStringLiteral(*ctx.bytecode, static_cast<StringLiteralExpression*>(expr)->value, string_index);
			emitConstString(ctx, string_index);
			return LS_TYPE_STRING;
		}
		case Expression::NULL_LITERAL: {
			emitConst8(ctx, 0);
			if (hint == LS_TYPE_SLICE) {
				emitConst8(ctx, 0);
			}
			return hint == LS_TYPE_INVALID ? LS_TYPE_VOID : hint;
		}
		case Expression::UNDEFINED: {
			emitZeroBytes(ctx, typeKindByteSize(hint));
			return hint == LS_TYPE_INVALID ? LS_TYPE_VOID : hint;
		}
		case Expression::TYPE_LITERAL: {
			emitIntegerConstant(ctx, LS_TYPE_I32, (u64)(uintptr)static_cast<TypeLiteralExpression*>(expr)->type);
			return LS_TYPE_I32;
		}
		case Expression::IDENTIFIER: {
			IdentifierExpression* id = static_cast<IdentifierExpression*>(expr);
			if (BytecodeLocalBinding* local = ctx.findLocal(id->name)) {
				if (ctx.isRefLocal(*local)) {
					if (!emitReferenceLoad(ctx, *expr, typeByteSize(local->type))) return LS_TYPE_INVALID;
					return local->kind != LS_TYPE_INVALID ? local->kind : LS_TYPE_I32;
				}
				const u32 byte_size = typeByteSize(local->type);
				emitLoadLocalBytes(ctx, local->offset, byte_size == 0u ? 1u : byte_size);
				return local->kind != LS_TYPE_INVALID ? local->kind : LS_TYPE_I32;
			}
			if (!id->symbol) return LS_TYPE_INVALID;
			if (ctx.globals) {
				if (const GlobalBinding* global = findGlobalBinding(*ctx.globals, *id->symbol)) {
					emitLoadGlobalBytes(ctx, global->offset, global->byte_size);
					ResolvedType* sym_type = id->symbol->resolved_type;
					if (sym_type && sym_type->kind == ResolvedType::META) sym_type = static_cast<MetaType*>(sym_type)->inner;
					return valueKindForType(sym_type);
				}
			}
			if (id->symbol->expression && id->symbol->expression->kind == Expression::FUNCTION) {
				FunctionExpression* fn = static_cast<FunctionExpression*>(id->symbol->expression);
				emitIntegerConstant(ctx, LS_TYPE_FUNCTION, fn->bytecode_index);
				return LS_TYPE_FUNCTION;
			}
			return LS_TYPE_INVALID;
		}
		case Expression::BINARY:
			return compileBinary(ctx, *static_cast<BinaryExpression*>(expr), hint);
		case Expression::CAST: {
			CastExpression* cast = static_cast<CastExpression*>(expr);
			const ls_type_kind dst_kind = semanticTypeToKind(expr->resolved_type ? expr->resolved_type : cast->expression ? cast->expression->resolved_type : nullptr);
			const ls_type_kind src_kind = compileExpression(ctx, cast->expression, semanticTypeToKind(cast->expression ? cast->expression->resolved_type : nullptr));
			// Slice reinterpret (`byte[] as T[]` / `T[] as byte[]`) keeps the same backing
			// reference (base offset). The length is in elements, so it rescales by the ratio
			// of element offset counts: new_len = old_len * src_size / dst_size. One side is
			// always `byte` (1 offset), so exactly one of the two adjustments is non-trivial.
			if (src_kind == LS_TYPE_SLICE && dst_kind == LS_TYPE_SLICE) {
				ResolvedType* src_t = cast->expression ? cast->expression->resolved_type : nullptr;
				ResolvedType* dst_t = expr->resolved_type;
				ResolvedType* src_elem = src_t && src_t->kind == ResolvedType::SLICE ? static_cast<SliceResolvedType*>(src_t)->element_type : nullptr;
				ResolvedType* dst_elem = dst_t && dst_t->kind == ResolvedType::SLICE ? static_cast<SliceResolvedType*>(dst_t)->element_type : nullptr;
				const u32 src_size = typeByteSize(src_elem);
				const u32 dst_size = typeByteSize(dst_elem);
				if (src_size > dst_size && dst_size != 0u && src_size % dst_size == 0u) {
					emitConst8(ctx, src_size / dst_size);
					emitBinaryOp(ctx, LS_OP_MUL_I64, typeKindByteSize(LS_TYPE_I64));
				} else if (dst_size > src_size && src_size != 0u && dst_size % src_size == 0u) {
					emitConst8(ctx, dst_size / src_size);
					emitBinaryOp(ctx, LS_OP_DIV_I64, typeKindByteSize(LS_TYPE_I64));
				}
				return dst_kind;
			}
			emitCast(ctx, src_kind, dst_kind);
			return dst_kind;
		}
		case Expression::SIZEOF: {
			ls_type_kind kind = expr->resolved_type ? toTypeKind(expr->resolved_type) : LS_TYPE_INVALID;
			if (!isNumericKind(kind)) kind = isIntegerKind(hint) ? hint : LS_TYPE_I32;
			const u64 v = static_cast<SizeofExpression*>(expr)->value;
			if (kind == LS_TYPE_F32) {
				emitConst4(ctx, bitcastF32ToU32((float)v));
			} else if (kind == LS_TYPE_F64) {
				emitConst8(ctx, bitcastF64ToU64((double)v));
			} else {
				emitIntegerConstant(ctx, kind, v);
			}
			return kind;
		}
		case Expression::UNARY: {
			UnaryExpression* un = static_cast<UnaryExpression*>(expr);
			if (un->op == Token::MINUS && un->resolved_fn) {
				return emitOperatorCall(ctx, *un->resolved_fn, un->expression, nullptr, un->op, hint);
			}
			const ls_type_kind kind = compileExpression(ctx, un->expression, hint);
			switch (un->op) {
				case Token::MINUS: {
					u32 index = 0u;
					if (!numericKindIndex(kind, index)) return LS_TYPE_INVALID;
					emitUnaryOp(ctx, (ls_op)((u32)LS_OP_NEG_I8 + index), typeKindByteSize(kind));
					return kind;
				}
				case Token::NOT:
					emitUnaryOp(ctx, LS_OP_NOT, 1u);
					return LS_TYPE_BOOL;
				default:
					return LS_TYPE_INVALID;
			}
		}
		case Expression::CALL:
			return compileCall(ctx, *static_cast<CallExpression*>(expr), hint);
		case Expression::MEMBER: {
			MemberExpression* member = static_cast<MemberExpression*>(expr);
			if (!member->expression && expr->resolved_type && expr->resolved_type->kind == ResolvedType::ENUM) {
				EnumResolvedType* en = static_cast<EnumResolvedType*>(expr->resolved_type);
				u32 enum_index = 0;
				if (!enumMemberIndex(*en, member->name, enum_index)) return LS_TYPE_INVALID;
				emitIntegerConstant(ctx, LS_TYPE_I32, enum_index);
				return LS_TYPE_I32;
			}
			if (member->expression) {
				ResolvedType* base_rt = member->expression->resolved_type;
				EnumResolvedType* enum_via_meta = (base_rt && base_rt->kind == ResolvedType::META && static_cast<MetaType*>(base_rt)->inner->kind == ResolvedType::ENUM)
					? static_cast<EnumResolvedType*>(static_cast<MetaType*>(base_rt)->inner) : nullptr;
				if (enum_via_meta) {
					u32 enum_index = 0;
					if (!enumMemberIndex(*enum_via_meta, member->name, enum_index)) return LS_TYPE_INVALID;
					emitIntegerConstant(ctx, LS_TYPE_I32, enum_index);
					return LS_TYPE_I32;
				}
				if (member->expression->kind == Expression::IDENTIFIER) {
					IdentifierExpression* base = static_cast<IdentifierExpression*>(member->expression);
					if (member->resolved_symbol && member->resolved_symbol->expression) {
						switch (member->resolved_symbol->expression->kind) {
							case Expression::FUNCTION:
								if (member->resolved_fn) {
									emitIntegerConstant(ctx, LS_TYPE_FUNCTION, member->resolved_fn->bytecode_index);
									return LS_TYPE_FUNCTION;
								}
								break;
							case Expression::INT_LITERAL:
								emitIntegerConstant(ctx, valueKindForType(member->resolved_symbol->resolved_type), static_cast<IntLiteralExpression*>(member->resolved_symbol->expression)->value);
								return valueKindForType(member->resolved_symbol->resolved_type);
							case Expression::FLOAT_LITERAL: {
								FloatLiteralExpression* fl = static_cast<FloatLiteralExpression*>(member->resolved_symbol->expression);
								const ls_type_kind kind = valueKindForType(member->resolved_symbol->resolved_type);
								if (kind == LS_TYPE_F32) {
									emitConst4(ctx, bitcastF32ToU32(static_cast<float>(fl->value)));
								}
								else {
									emitConst8(ctx, bitcastF64ToU64(fl->value));
								}
								return kind;
							}
							case Expression::BOOL_LITERAL:
								emitIntegerConstant(ctx, LS_TYPE_BOOL, static_cast<BoolLiteralExpression*>(member->resolved_symbol->expression)->value ? 1u : 0u);
								return LS_TYPE_BOOL;
							case Expression::STRING_LITERAL:
								{
									u32 string_index = 0;
									appendStringLiteral(*ctx.bytecode, static_cast<StringLiteralExpression*>(member->resolved_symbol->expression)->value, string_index);
									emitConstString(ctx, string_index);
								}
								return LS_TYPE_STRING;
							case Expression::NULL_LITERAL:
							case Expression::UNDEFINED:
								emitZeroBytes(ctx, typeByteSize(member->resolved_symbol->resolved_type));
								return valueKindForType(member->resolved_symbol->resolved_type);
							default:
								break;
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
						if (!structFieldByteOffset(*st, member->name, offset, field_type)) return LS_TYPE_INVALID;
						if (ctx.isRefLocal(*local)) {
							if (!emitReferenceLoad(ctx, *expr, typeByteSize(field_type))) return LS_TYPE_INVALID;
							return valueKindForType(field_type);
						}
						emitLoadLocalBytes(ctx, local->offset + value_offset + offset, typeByteSize(field_type));
						return valueKindForType(field_type);
					}
				}
			}
			if (member->expression && member->expression->kind == Expression::IDENTIFIER && ctx.module && ctx.unit) {
				IdentifierExpression* base = static_cast<IdentifierExpression*>(member->expression);
				if (member->resolved_symbol && member->resolved_symbol->expression) {
					switch (member->resolved_symbol->expression->kind) {
						case Expression::FUNCTION:
							if (member->resolved_fn) {
								emitIntegerConstant(ctx, LS_TYPE_FUNCTION, member->resolved_fn->bytecode_index);
								return LS_TYPE_FUNCTION;
							}
							break;
						case Expression::INT_LITERAL:
							emitIntegerConstant(ctx, valueKindForType(member->resolved_symbol->resolved_type), static_cast<IntLiteralExpression*>(member->resolved_symbol->expression)->value);
							return valueKindForType(member->resolved_symbol->resolved_type);
						case Expression::FLOAT_LITERAL: {
							FloatLiteralExpression* fl = static_cast<FloatLiteralExpression*>(member->resolved_symbol->expression);
							const ls_type_kind kind = valueKindForType(member->resolved_symbol->resolved_type);
							if (kind == LS_TYPE_F32) {
								emitConst4(ctx, bitcastF32ToU32(static_cast<float>(fl->value)));
							}
							else {
								emitConst8(ctx, bitcastF64ToU64(fl->value));
							}
							return kind;
						}
						case Expression::BOOL_LITERAL:
							emitIntegerConstant(ctx, LS_TYPE_BOOL, static_cast<BoolLiteralExpression*>(member->resolved_symbol->expression)->value ? 1u : 0u);
							return LS_TYPE_BOOL;
						case Expression::STRING_LITERAL:
							{
								u32 string_index = 0;
								appendStringLiteral(*ctx.bytecode, static_cast<StringLiteralExpression*>(member->resolved_symbol->expression)->value, string_index);
								emitConstString(ctx, string_index);
							}
							return LS_TYPE_STRING;
						case Expression::NULL_LITERAL:
						case Expression::UNDEFINED:
							emitZeroBytes(ctx, typeByteSize(member->resolved_symbol->resolved_type));
							return valueKindForType(member->resolved_symbol->resolved_type);
						default:
							break;
					}
				}
			}
			if (member->expression && member->expression->resolved_type && member->expression->resolved_type->kind == ResolvedType::STRUCT) {
				StructResolvedType* st = static_cast<StructResolvedType*>(member->expression->resolved_type);
				u32 offset = 0u;
				ResolvedType* field_type = nullptr;
				if (!structFieldByteOffset(*st, member->name, offset, field_type)) return LS_TYPE_INVALID;
				// Try reference-based load first (works for addressable lvalues).
				if (emitReferenceLoad(ctx, *expr, typeByteSize(expr->resolved_type))) return valueKindForType(expr->resolved_type);
				// For temporaries (e.g. call results), compile the base onto the stack,
				// store it in a temp local, then load the desired field offset.
				const u32 struct_byte_size = typeByteSize(member->expression->resolved_type);
				if (compileExpression(ctx, member->expression, LS_TYPE_INVALID) == LS_TYPE_INVALID) return LS_TYPE_INVALID;
				const u32 result_offset = ctx.temp_top - struct_byte_size;
				const u32 temp = ctx.addLocal({}, member->expression->resolved_type, valueKindForType(member->expression->resolved_type), true);
				emitStoreLocalBytes(ctx, temp, struct_byte_size, false);
				ctx.temp_top = ctx.next_local_offset;
				emitLoadLocalBytes(ctx, temp + offset, typeByteSize(field_type));
				const u32 field_size = typeByteSize(field_type);
				const u32 loaded_offset = ctx.temp_top - field_size;
				if (loaded_offset != result_offset) {
					emitOp(ctx.code, LS_OP_COPY);
					emitTempReg(ctx, result_offset);
					emitTempReg(ctx, loaded_offset);
					emitU32(ctx.code, field_size);
					ctx.temp_top = result_offset + field_size;
				}
				return valueKindForType(field_type);
			}
			return LS_TYPE_INVALID;
		}
		case Expression::BRACKET: {
			BracketExpression* br = static_cast<BracketExpression*>(expr);
			if (br->resolved_type && br->resolved_type->kind == ResolvedType::FUNCTION) {
				FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(br->resolved_type);
				if (fn_type->decl) {
					emitIntegerConstant(ctx, LS_TYPE_FUNCTION, fn_type->decl->bytecode_index);
					return LS_TYPE_FUNCTION;
				}
			}
			if (br->has_colon) {
				if (!emitSlice(ctx, *br)) return LS_TYPE_INVALID;
				return LS_TYPE_SLICE;
			}
			if (br->args.size() != 1) return LS_TYPE_INVALID;
			if (!br->resolved_type || typeByteSize(br->resolved_type) == 0u) return LS_TYPE_INVALID;
			if (!emitBracketLoad(ctx, *br)) return LS_TYPE_INVALID;
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

static bool emitLocalLiteralInitializer(FunctionCompiler& ctx, u32 offset, Expression& expr, ls_type_kind kind) {
	switch (expr.kind) {
		case Expression::INT_LITERAL: {
			if (!isIntegerKind(kind) && !isFloatKind(kind)) return false;
			const u64 value = static_cast<IntLiteralExpression&>(expr).value;
			if (kind == LS_TYPE_F32) emitConst4At(ctx, offset, bitcastF32ToU32((float)value));
			else if (kind == LS_TYPE_F64) emitConst8At(ctx, offset, bitcastF64ToU64((double)value));
			else emitIntegerConstantAt(ctx, offset, kind, value);
			return true;
		}
		case Expression::FLOAT_LITERAL: {
			if (!isFloatKind(kind)) return false;
			const double value = static_cast<FloatLiteralExpression&>(expr).value;
			if (kind == LS_TYPE_F32) emitConst4At(ctx, offset, bitcastF32ToU32((float)value));
			else emitConst8At(ctx, offset, bitcastF64ToU64(value));
			return true;
		}
		case Expression::BOOL_LITERAL:
			if (kind != LS_TYPE_BOOL) return false;
			emitIntegerConstantAt(ctx, offset, LS_TYPE_BOOL, static_cast<BoolLiteralExpression&>(expr).value ? 1u : 0u);
			return true;
		case Expression::STRING_LITERAL: {
			if (kind != LS_TYPE_STRING) return false;
			u32 string_index = 0;
			appendStringLiteral(*ctx.bytecode, static_cast<StringLiteralExpression&>(expr).value, string_index);
			emitConstStringAt(ctx, offset, string_index);
			return true;
		}
		default:
			return false;
	}
}

static bool compileStatement(FunctionCompiler& ctx, Statement& st_ref, ls_type_kind return_kind, ls_string_view current_label) {
	Statement* st = &st_ref;
	switch (st->kind) {
		case Statement::VAR_DECL: {
			VarDeclStatement* var = static_cast<VarDeclStatement*>(st);
			ResolvedType* value_type = var->resolved_type ? var->resolved_type : (var->expression ? var->expression->resolved_type : nullptr);
			const ls_type_kind kind = valueKindForType(value_type, parsedTypeToKind(var->parsed_type));
			const u32 offset = ctx.addLocal(var->name, value_type, kind);
			const u32 byte_size = ctx.findLocal(var->name)->byte_size;
			if (var->expression) {
				if (emitLocalLiteralInitializer(ctx, offset, *var->expression, kind)) return true;
				if (var->expression->kind == Expression::UNDEFINED) return true;
				
				ls_type_kind expr_kind = compileValueAsType(ctx, var->expression, value_type);
				if (expr_kind == LS_TYPE_INVALID) return false;
			}
			emitStoreLocalBytes(ctx, offset, byte_size);
			return true;
		}
		case Statement::ASSIGN: {
			AssignStatement* assign = static_cast<AssignStatement*>(st);
			if (!assign->lhs) return false;
			if (assign->lhs->kind == Expression::IDENTIFIER) {
				IdentifierExpression* id = static_cast<IdentifierExpression*>(assign->lhs);
				BytecodeLocalBinding* local = ctx.findLocal(id->name);
				const GlobalBinding* global = nullptr;
				if (!local) {
					if (!id->symbol || !ctx.globals) return false;
					global = findGlobalBinding(*ctx.globals, *id->symbol);
					if (!global) return false;
				}

				ResolvedType* value_type = local ? local->type : id->symbol->resolved_type;
				if (value_type && value_type->kind == ResolvedType::META) value_type = static_cast<MetaType*>(value_type)->inner;
				const ls_type_kind value_kind = local ? local->kind : valueKindForType(value_type);

				if (local && ctx.isRefLocal(*local)) {
					const u32 ref_size = typeKindByteSize(LS_TYPE_CPTR);
					const u32 value_size = typeByteSize(value_type);
					switch (assign->op) {
						case Token::EQUAL:
							emitLoadLocalBytes(ctx, local->offset, ref_size);
							emitZeroIndex(ctx);
							if (compileExpression(ctx, assign->rhs, value_kind) == LS_TYPE_INVALID) return false;
							emitStoreAt(ctx, 1u, 0, value_size);
							return true;
						case Token::PLUS_EQUAL:
						case Token::MINUS_EQUAL:
						case Token::STAR_EQUAL:
						case Token::SLASH_EQUAL:
						{
							if (assign->resolved_op_fn) {
								const FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(assign->resolved_op_fn->resolved_type);
								emitLoadLocalBytes(ctx, local->offset, ref_size);
								emitZeroIndex(ctx);
								emitLoadLocalBytes(ctx, local->offset, ref_size);
								emitZeroIndex(ctx);
								emitLoadAt(ctx, 1u, 0, value_size);
								if (compileValueAsType(ctx, assign->rhs, fn_type->param_types[1]) == LS_TYPE_INVALID) return false;
								emitCallDirect(ctx, assign->resolved_op_fn->bytecode_index, callArgWindowSize(*fn_type), typeByteSize(fn_type->return_type));
								emitStoreAt(ctx, 1u, 0, value_size);
								return true;
							}
							emitLoadLocalBytes(ctx, local->offset, ref_size);
							emitZeroIndex(ctx);
							emitLoadLocalBytes(ctx, local->offset, ref_size);
							emitZeroIndex(ctx);
							emitLoadAt(ctx, 1u, 0, value_size);
							if (compileExpression(ctx, assign->rhs, value_kind) == LS_TYPE_INVALID) return false;
							if (!emitNumericStoreOp(ctx, value_kind, assign->op)) return false;
							emitStoreAt(ctx, 1u, 0, value_size);
							return true;
						}
						default:
							return false;
					}
				}

				switch (assign->op) {
					case Token::EQUAL:
						if (compileValueAsType(ctx, assign->rhs, value_type) == LS_TYPE_INVALID) return false;
						if (local) {
							emitStoreLocalBytes(ctx, local->offset, local->byte_size);
						}
						else {
							emitStoreGlobalBytes(ctx, global->offset, global->byte_size);
						}
						return true;
					case Token::PLUS_EQUAL:
					case Token::MINUS_EQUAL:
					case Token::STAR_EQUAL:
					case Token::SLASH_EQUAL: {
						if (assign->resolved_op_fn) {
							{
								const FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(assign->resolved_op_fn->resolved_type);
							if (local) {
								emitLoadLocalBytes(ctx, local->offset, local->byte_size);
							}
							else {
								emitLoadGlobalBytes(ctx, global->offset, global->byte_size);
							}
							if (compileValueAsType(ctx, assign->rhs, fn_type->param_types[1]) == LS_TYPE_INVALID) return false;
							emitCallDirect(ctx, assign->resolved_op_fn->bytecode_index, callArgWindowSize(*fn_type), typeByteSize(fn_type->return_type));
							if (local) {
								emitStoreLocalBytes(ctx, local->offset, local->byte_size);
							}
							else {
								emitStoreGlobalBytes(ctx, global->offset, global->byte_size);
							}
							return true;
							}
						}
						if (local) {
							emitLoadLocalBytes(ctx, local->offset, local->byte_size);
						}
						else {
							emitLoadGlobalBytes(ctx, global->offset, global->byte_size);
						}
						if (compileExpression(ctx, assign->rhs, value_kind) == LS_TYPE_INVALID) return false;
						if (!emitNumericStoreOp(ctx, value_kind, assign->op)) return false;
						if (local) {
							emitStoreLocalBytes(ctx, local->offset, local->byte_size);
						}
						else {
							emitStoreGlobalBytes(ctx, global->offset, global->byte_size);
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
				if (assign->op != Token::EQUAL && value_kind == LS_TYPE_INVALID) return false;
				return emitBracketStore(ctx, *br, assign->rhs, value_kind, assign->op);
			}
			if (assign->lhs->kind == Expression::MEMBER) {
				MemberExpression* member = static_cast<MemberExpression*>(assign->lhs);
				if (!member->expression || !member->expression->resolved_type) return false;
				if (member->expression->resolved_type->kind != ResolvedType::STRUCT) return false;
				StructResolvedType* st = static_cast<StructResolvedType*>(member->expression->resolved_type);
				u32 field_offset = 0u;
				ResolvedType* field_type = nullptr;
				if (!structFieldByteOffset(*st, member->name, field_offset, field_type)) return false;
				const u32 field_size = typeByteSize(field_type);
				const ls_type_kind value_kind = valueKindForType(field_type, LS_TYPE_INVALID);
				if (assign->op == Token::EQUAL) {
					if (!emitReference(ctx, *member->expression)) return false;
					emitZeroIndex(ctx);
					if (compileExpression(ctx, assign->rhs, value_kind) == LS_TYPE_INVALID) return false;
					emitStoreAt(ctx, field_size ? field_size : 1u, (i32)field_offset, field_size ? field_size : 1u);
					return true;
				}
				// Compound assignment: load existing value, apply op, store back.
				// Stack order for STORE_AT is: base, index, value.
				const u32 ref_offset = ctx.addLocal({}, nullptr, LS_TYPE_I64);
				const u32 val_offset = ctx.addLocal({}, field_type, value_kind);
				const u32 ref_size = typeKindByteSize(LS_TYPE_CPTR);
				const u32 value_size = field_size ? field_size : 1u;
				if (!emitReference(ctx, *member->expression)) return false;
				emitStoreLocalBytes(ctx, ref_offset, ref_size);
				emitLoadLocalBytes(ctx, ref_offset, ref_size);
				emitZeroIndex(ctx);
				emitLoadAt(ctx, 1u, (i32)field_offset, value_size);
				if (compileExpression(ctx, assign->rhs, value_kind) == LS_TYPE_INVALID) return false;
				if (!emitNumericStoreOp(ctx, value_kind, assign->op)) return false;
				emitStoreLocalBytes(ctx, val_offset, value_size);
				emitLoadLocalBytes(ctx, ref_offset, ref_size);
				emitZeroIndex(ctx);
				emitLoadLocalBytes(ctx, val_offset, value_size);
				emitStoreAt(ctx, 1u, (i32)field_offset, value_size);
				return true;
			}
			return false;
		}
		case Statement::IF: {
			IfStatement* ifst = static_cast<IfStatement*>(st);
			if (!ifst->condition || !ifst->body) return false;
			if (compileExpression(ctx, ifst->condition, LS_TYPE_BOOL) == LS_TYPE_INVALID) return false;
			const u32 jump_false_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_FALSE);
			if (!compileStatement(ctx, *ifst->body, return_kind, current_label)) return false;
			if (ifst->else_branch) {
				const u32 jump_end_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP);
				patchJumpRelative(ctx.code, jump_false_pos, (u32)ctx.code.size());
				if (!compileStatement(ctx, *ifst->else_branch, return_kind, current_label)) return false;
				patchJumpRelative(ctx.code, jump_end_pos, (u32)ctx.code.size());
			}
			else {
				patchJumpRelative(ctx.code, jump_false_pos, (u32)ctx.code.size());
			}
			return true;
		}
		case Statement::WHILE: {
			WhileStatement* ws = static_cast<WhileStatement*>(st);
			if (!ws->condition || !ws->body) return false;

			const u32 condition_pos = (u32)ctx.code.size();
			if (compileExpression(ctx, ws->condition, LS_TYPE_BOOL) == LS_TYPE_INVALID) return false;

			const u32 jump_false_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_FALSE);

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

			if (!compileStatement(ctx, *ws->body, return_kind, {})) {
				ctx.loops.pop_back();
				return false;
			}

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
					patchJumpRelative(ctx.code, continue_pos, condition_pos);
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
			const u32 byte_size = typeByteSize(value_type) == 0u ? 1u : typeByteSize(value_type);
			if (compileExpression(ctx, fs->begin, value_kind) == LS_TYPE_INVALID) return false;
			if (compileExpression(ctx, fs->end, value_kind) == LS_TYPE_INVALID) return false;
			const u32 range_value_top = ctx.temp_top;

			ctx.pushScope();
			const u32 loop_offset = ctx.addLocal(fs->loop_var, value_type, value_kind, true);
			const u32 end_offset = ctx.addLocal({}, value_type, value_kind, true);
			ctx.temp_top = range_value_top;
			emitStoreLocalBytes(ctx, end_offset, byte_size, false);
			emitStoreLocalBytes(ctx, loop_offset, byte_size, false);
			ctx.temp_top = ctx.next_local_offset;

			const u32 condition_pos = (u32)ctx.code.size();
			emitLoadLocalBytes(ctx, loop_offset, byte_size);
			emitLoadLocalBytes(ctx, end_offset, byte_size);
			emitCompareOp(ctx, LS_OP_LE, value_kind);
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

			if (!compileStatement(ctx, *fs->body, return_kind, {})) {
				ctx.loops.pop_back();
				ctx.popScope(return_kind, current_label);
				return false;
			}

			const u32 increment_pos = (u32)ctx.code.size();
			emitLoadLocalBytes(ctx, loop_offset, byte_size);
			emitIntegerConstant(ctx, value_kind, 1u);
			if (!emitNumericBinary(ctx, LS_OP_ADD_I8, value_kind, false)) {
				ctx.loops.pop_back();
				ctx.popScope(return_kind, current_label);
				return false;
			}
			emitStoreLocalBytes(ctx, loop_offset, byte_size);
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
			const u32 subject_byte_size = typeByteSize(subject_type) == 0u ? 1u : typeByteSize(subject_type);
			if (compileExpression(ctx, ms->subject, subject_kind) == LS_TYPE_INVALID) return false;
			const u32 subject_offset = ctx.addLocal({}, subject_type, subject_kind, true);
			emitStoreLocalBytes(ctx, subject_offset, subject_byte_size);

			ExpArray<u32> match_end_jumps(*ctx.bytecode->arena);
			ExpArray<u32> pending_false_jumps(*ctx.bytecode->arena);

			for (MatchArm& arm : ms->arms) {
				ExpArray<u32> arm_body_jumps(*ctx.bytecode->arena);

				if (arm.is_fallback) {
					for (u32 false_jump : pending_false_jumps) patchJumpRelative(ctx.code, false_jump, (u32)ctx.code.size());
					pending_false_jumps.clear();
					if (!compileStatement(ctx, *arm.body, return_kind, current_label)) return false;
					match_end_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
					continue;
				}

				for (MatchPattern& pattern : arm.patterns) {
					for (u32 false_jump : pending_false_jumps) patchJumpRelative(ctx.code, false_jump, (u32)ctx.code.size());
					pending_false_jumps.clear();

					if (pattern.end) {
						emitLoadLocalBytes(ctx, subject_offset, subject_byte_size);
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
						emitCompareOp(ctx, LS_OP_GE, subject_kind);
						pending_false_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_FALSE));

						emitLoadLocalBytes(ctx, subject_offset, subject_byte_size);
						if (compileExpression(ctx, pattern.end, subject_kind) == LS_TYPE_INVALID) return false;
						emitCompareOp(ctx, LS_OP_LE, subject_kind);
						pending_false_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_FALSE));
						arm_body_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
					}
					else {
						emitLoadLocalBytes(ctx, subject_offset, subject_byte_size);
						if (compileExpression(ctx, pattern.begin, subject_kind) == LS_TYPE_INVALID) return false;
						emitCompareOp(ctx, LS_OP_EQ, subject_kind);
						arm_body_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_TRUE));
					}
				}

				const u32 skip_jump_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP);
				const u32 body_start = (u32)ctx.code.size();
				for (u32 jump_pos : arm_body_jumps) patchJumpRelative(ctx.code, jump_pos, body_start);
				for (u32 false_jump : pending_false_jumps) patchJumpRelative(ctx.code, false_jump, skip_jump_pos - 1u);
				pending_false_jumps.clear();
				if (!compileStatement(ctx, *arm.body, return_kind, current_label)) return false;
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
				if (!loop->break_jumps) return false;
				loop->break_jumps->push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
				return true;
			}
			if (!loop->continue_jumps) return false;
			loop->continue_jumps->push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
			return true;
		}
		case Statement::LABEL: {
			LabelStatement* label = static_cast<LabelStatement*>(st);
			if (!label->statement) return false;
			const ls_string_view next_label = label->statement->kind == Statement::WHILE || label->statement->kind == Statement::FOR
				? label->name
				: current_label;
			return compileStatement(ctx, *label->statement, return_kind, next_label);
		}
	case Statement::RETURN: {
		ReturnStatement* ret = static_cast<ReturnStatement*>(st);
		if (return_kind == LS_TYPE_NULL_VALUE) {
			const bool is_null = !ret->expression
				|| ret->expression->kind == Expression::NULL_LITERAL
				|| ret->expression->kind == Expression::UNDEFINED;
			NullableResolvedType* nullable_type = ctx.return_type && ctx.return_type->kind == ResolvedType::NULLABLE
				? static_cast<NullableResolvedType*>(ctx.return_type)
				: nullptr;
			ResolvedType* inner_type = nullable_type ? nullable_type->inner : nullptr;
			emitIntegerConstant(ctx, LS_TYPE_BOOL, is_null ? 0u : 1u);
			if (!is_null && ret->expression) {
				if (compileValueAsType(ctx, ret->expression, inner_type) == LS_TYPE_INVALID) return false;
			}
			else {
				emitZeroBytes(ctx, typeByteSize(inner_type));
			}
			emitDeferredStatements(ctx, 0u, return_kind, current_label);
			emitReturn(ctx);
			return true;
		}
		if (ret->expression) {
			if (compileValueAsType(ctx, ret->expression, ctx.return_type) == LS_TYPE_INVALID) return false;
		}
		emitDeferredStatements(ctx, 0u, return_kind, current_label);
		emitReturn(ctx);
		return true;
		}
		case Statement::EXPRESSION: {
			ExpressionStatement* expr = static_cast<ExpressionStatement*>(st);
			const ls_type_kind kind = compileExpression(ctx, expr->expression, LS_TYPE_INVALID);
			if (kind == LS_TYPE_INVALID) return false;
			const u32 byte_size = kind == LS_TYPE_VOID ? 0u : typeByteSize(expr->expression->resolved_type);
			if (byte_size > 0u) {
				emitPop(ctx, byte_size);
			}
			return true;
		}
		case Statement::BLOCK: {
			BlockStatement* block = static_cast<BlockStatement*>(st);
			ctx.pushScope();
			for (Statement* child : block->statements) {
				if (!compileStatement(ctx, *child, return_kind, current_label)) {
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

static bool isSimpleReturnLiteral(BlockStatement& body, Expression*& out_expr) {
	if (body.statements.size() != 1) return false;
	Statement* st = body.statements[0];
	if (st->kind != Statement::RETURN) return false;
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

static u32 computeParamSize(const ExpArray<FunctionParam>& params) {
	u32 count = 0;
	for (const FunctionParam& param : params) {
		const u32 byte_size = param.is_ref ? typeKindByteSize(LS_TYPE_CPTR) : typeByteSize(param.resolved_type);
		count += byte_size == 0u ? 1u : byte_size;
	}
	return count;
}

static bool compileFunctionBytecode(
	ls_bytecode* bytecode,
	ls_module* module,
	Unit& unit,
	ExpArray<FunctionInfo>& functions,
	ExpArray<GlobalBinding>& globals,
	FunctionExpression* fn,
	FunctionResolvedType* fn_type,
	ls_string_view name,
	bool is_builtin_native
) {
	ls_arena* arena = bytecode->arena;
	ls_function_bc* out = appendFunction(*bytecode);
	if (!out) return false;

	ls_function_bc& function = *out;
	function.name = name;
	function.kind = fn->is_extern ? LS_FUNCTION_NATIVE : LS_FUNCTION_SCRIPT;
	function.is_builtin_native = is_builtin_native;
	function.index = (u32)(bytecode->function_count - 1);
	function.param_count = (u32)fn->runtime_params.size();
	function.param_size = 0;
	ResolvedType* return_type = fn_type ? fn_type->return_type : nullptr;
	function.return_kind = toTypeKind(return_type);
	// Calls move raw offsets, so aggregate return metadata must describe the
	// representation width rather than assuming every value is one offset.
	function.return_size = typeByteSize(return_type);
	function.local_size = 0;
	function.frame_size = function.param_size + function.local_size;

	if (fn->is_extern) {
		function.param_size = computeParamSize(fn->runtime_params);
		return true;
	}
	if (!fn->body || fn->body->kind != Statement::BLOCK) return false;

	BlockStatement* body = static_cast<BlockStatement*>(fn->body);
	Expression* literal = nullptr;
	if (function.return_size == 1u && isSimpleReturnLiteral(*body, literal)) {
		// Tiny literal function: emit the constant and return without setting up
		// parameter locals (the literal references none).
		function.param_size = computeParamSize(fn->runtime_params);
		FunctionCompiler ctx(bytecode, function);
		ctx.next_local_offset = function.param_size;
		ctx.temp_top = function.param_size;
		ctx.frame_high_water = function.param_size;
		switch (literal->kind) {
			case Expression::INT_LITERAL:
				emitIntegerConstant(ctx, function.return_kind, static_cast<IntLiteralExpression*>(literal)->value);
				break;
			case Expression::BOOL_LITERAL:
				emitIntegerConstant(ctx, LS_TYPE_BOOL, static_cast<BoolLiteralExpression*>(literal)->value ? 1u : 0u);
				break;
			default:
				// A 1-byte return excludes floats, so no other literal kind reaches here.
				return false;
		}
		emitReturn(ctx);
		function.local_size = 0u;
		return finalizeFunctionCode(ctx, function, *arena);
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
		binding.byte_size = param.is_ref ? typeKindByteSize(LS_TYPE_CPTR) : typeByteSize(param.resolved_type);
		if (binding.byte_size == 0u) binding.byte_size = 1u;
		binding.offset = function.param_size;
		if (param.is_ref) ctx.ref_local_offsets.push(binding.offset);
		function.param_size += binding.byte_size;
		ctx.next_local_offset = function.param_size;
		if (function.param_size > ctx.max_local_count) ctx.max_local_count = function.param_size;
	}
	ctx.temp_top = function.param_size;
	ctx.frame_high_water = function.param_size;
	for (Statement* st : body->statements) {
		if (!compileStatement(ctx, *st, function.return_kind, {})) return false;
	}
	// Every code path in a non-void function is guaranteed by checkFunctionBody
	// to already end in an explicit `return`, so this is unreachable for those.
	// For void functions that fall off the end without one, this is the only
	// return emitted; it keeps the bytecode self-terminating so the runtime's
	// interpreter loop never needs to fall through past the end of fn->code.
	emitReturn(ctx);
	function.local_size = ctx.max_local_count > function.param_size ? ctx.max_local_count - function.param_size : 0u;
	return finalizeFunctionCode(ctx, function, *arena);
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
	bytecode->global_size = 0u;
	for (Unit& unit : module->units) {
		for (Symbol& sym : unit.symbols) {
			if (!sym.expression) continue;
			if (sym.expression->kind == Expression::FUNCTION || sym.expression->kind == Expression::STRUCT || sym.expression->kind == Expression::ENUM) continue;
			GlobalBinding& binding = globals.emplace_back();
			binding.sym = &sym;
			binding.offset = bytecode->global_size;
			binding.byte_size = typeByteSize(sym.resolved_type);
			if (binding.byte_size == 0u) binding.byte_size = 1u;
			bytecode->global_size += binding.byte_size;
		}
	}

	ExpArray<FunctionInfo> functions(*arena);
	for (Unit& unit : module->units) {
		for (Symbol& sym : unit.symbols) {
			if (!sym.expression || sym.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* fn = static_cast<FunctionExpression*>(sym.expression);
			if (!fn->comptime_params.empty()) continue;
			FunctionInfo& info = functions.emplace_back();
			info.name = sym.name;
			info.fn = fn;
			info.type = fn->resolved_type ? static_cast<FunctionResolvedType*>(fn->resolved_type) : nullptr;
			info.unit = &unit;
			info.symbol = &sym;
			info.index = (u32)functions.size() - 1;
			fn->bytecode_index = info.index;
		}
	}

	for (const FunctionInfo& info : functions) {
		if (!compileFunctionBytecode(
			bytecode,
			module,
			*info.unit,
			functions,
			globals,
			info.fn,
			info.type,
			info.name,
			info.fn->is_extern && (equalStrings(info.unit->path, makeStringView("std:math")) || equalStrings(info.unit->path, makeStringView("std:mem")))
		)) {
			ls_bytecode_destroy(bytecode);
			return nullptr;
		}
	}

	if (bytecode->global_size > 0u) {
		ls_function_bc* out = appendFunction(*bytecode);
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
		function.param_size = 0;
		function.return_kind = LS_TYPE_VOID;
		function.return_size = 0;
		function.local_size = 0;
		function.frame_size = 0u;

		FunctionCompiler ctx(bytecode, function);
		ctx.module = module;
		ctx.functions = &functions;
		ctx.globals = &globals;
		for (Unit& unit : module->units) {
			ctx.unit = &unit;
			for (Symbol& sym : unit.symbols) {
				if (!sym.expression) continue;
				if (sym.expression->kind == Expression::FUNCTION || sym.expression->kind == Expression::STRUCT || sym.expression->kind == Expression::ENUM) continue;
				const GlobalBinding* binding = findGlobalBinding(globals, sym);
				if (!binding) continue;
				if (sym.expression->kind == Expression::UNDEFINED) continue;
				if (compileValueAsType(ctx, sym.expression, sym.resolved_type) == LS_TYPE_INVALID) {
					ls_bytecode_destroy(bytecode);
					return nullptr;
				}
				emitStoreGlobalBytes(ctx, binding->offset, binding->byte_size);
			}
		}
		emitReturn(ctx);
		function.local_size = ctx.max_local_count;
		if (!finalizeFunctionCode(ctx, function, *arena)) {
			ls_bytecode_destroy(bytecode);
			return nullptr;
		}
		bytecode->has_global_init = true;
	}

	return bytecode;
}

void ls_bytecode_destroy(ls_bytecode* bytecode) {
	if (!bytecode) return;
	if (bytecode->host && bytecode->host->destroy_arena && bytecode->arena) bytecode->host->destroy_arena(bytecode->arena);
	std::free(bytecode);
}

