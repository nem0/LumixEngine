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

static ls_type_kind toTypeKind(const ResolvedType& type) {
	switch (type.kind) {
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
		case ResolvedType::CSTR: return LS_TYPE_CPTR;
		case ResolvedType::CPTR: return LS_TYPE_CPTR;
		case ResolvedType::BYTE: return LS_TYPE_U8;
		case ResolvedType::FUNCTION: return LS_TYPE_FUNCTION;
		case ResolvedType::ARRAY: return LS_TYPE_ARRAY;
		case ResolvedType::SLICE: return LS_TYPE_SLICE;
		case ResolvedType::NULLABLE: return LS_TYPE_NULL_VALUE;
		case ResolvedType::ENUM: return LS_TYPE_ENUM;
		default: return LS_TYPE_INVALID;
	}
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

struct LoopBinding {
	ls_string_view label = {};
	u32 defer_mark = 0;
	ExpArray<u32>* break_jumps = nullptr;
	ExpArray<u32>* continue_jumps = nullptr;
};

struct FunctionCompiler;
static void compileStatement(FunctionCompiler& ctx, Statement& st, ls_type_kind return_kind, ls_string_view current_label);
static void emitDeferredStatements(FunctionCompiler& ctx, u32 defer_mark, ls_type_kind return_kind, ls_string_view current_label);
static void compileExpressionAsType(FunctionCompiler& ctx, Expression& expr, ResolvedType& expected_type);
static ls_type_kind compileExpression(FunctionCompiler& ctx, Expression& expr, ls_type_kind hint);
static bool tryEmitReference(FunctionCompiler& ctx, Expression& expr);
static void emitCallDirect(FunctionCompiler& ctx, u32 callee_index, u32 arg_size, u32 return_size);
static ls_type_kind compileMember(FunctionCompiler& ctx, MemberExpression& expr);
static void patchJumpRelative(ByteArray& code, u32 operand_pos, u32 target_pos);
static u32 emitJumpPlaceholder(FunctionCompiler& ctx, ls_op op);

struct FunctionCompiler {
	explicit FunctionCompiler(ls_bytecode* bytecode, ls_function_bc& out)
		: bytecode(bytecode)
		, out(out)
		, code(*bytecode->arena)
		, deferreds(*bytecode->arena)
		, defer_marks(*bytecode->arena)
		, loops(*bytecode->arena) {}

	ls_bytecode* bytecode = nullptr;
	ResolvedType* return_type = nullptr;
	ls_function_bc& out;
	ByteArray code;
	ExpArray<Statement*> deferreds;
	ExpArray<i32> defer_marks;
	ExpArray<LoopBinding> loops;
	// Locals and temporaries share one absolute frame offset space. `next_local_offset`
	// is the floor below which temporaries must not be rewound, while `temp_top`
	// is the next free byte for expression temporaries.
	u32 temp_top = 0;
	u32 frame_high_water = 0;
	u32 next_local_offset = 0;

	// Allocates frame space. Named locals pass their declaration's slot so that
	// identifier expressions can read the location back without a lookup;
	// temporaries pass no slot.
	u32 addLocal(ResolvedType* type, ls_type_kind kind, bool preserve_temp_top = false, StorageSlot* slot = nullptr) {
		u32 byte_size = u32(type ? typeByteSize(*type) : typeKindByteSize(kind));
		if (byte_size == 0u) byte_size = 1u;
		const bool has_live_temps = temp_top > next_local_offset;
		const u32 offset = has_live_temps ? temp_top : next_local_offset;
		next_local_offset = offset + byte_size;
		if (!preserve_temp_top) temp_top = next_local_offset;
		const u32 local_end = offset + byte_size;
		if (local_end > frame_high_water) frame_high_water = local_end;
		if (slot) {
			slot->offset = offset;
			slot->byte_size = byte_size;
			slot->kind = kind;
			slot->type = type;
			slot->storage = StorageSlot::LOCAL;
		}
		return offset;
	}

	void pushScope() {
		defer_marks.push(deferreds.size());
	}

	void popScope(ls_type_kind return_kind, ls_string_view current_label) {
		if (defer_marks.empty()) return;
		const i32 defer_mark = defer_marks.back();
		emitDeferredStatements(*this, defer_mark, return_kind, current_label);
		while (deferreds.size() > defer_mark) deferreds.pop_back();
		defer_marks.pop_back();
	}

	LoopBinding* findLoop(ls_string_view label) {
		if (label.begin == label.end) return &loops.back();
		for (i32 i = (i32)loops.size() - 1; i >= 0; --i) {
			LoopBinding& loop = loops[(u32)i];
			if (equalStrings(loop.label, label)) return &loop;
		}
		ASSERT(false);
		return nullptr;
	}
};

// TODO
static ls_type_kind valueKindForType(ResolvedType& type) {
	if (type.kind == ResolvedType::ENUM) return LS_TYPE_I32;
	return toTypeKind(type);
}

static u64 enumMemberValue(EnumResolvedType& en, ls_string_view name) {
	u64 implicit_value = 0;
	for (i32 i = 0; i < en.decl->members.size(); ++i) {
		if (equalStrings(en.decl->members[i].name, name)) {
			if (en.decl->members[i].value) {
				ASSERT(en.decl->members[i].value->kind == Expression::INT_LITERAL);
				return static_cast<IntLiteralExpression*>(en.decl->members[i].value)->value;
			}
			return implicit_value;
		}
		if (en.decl->members[i].value) {
			ASSERT(en.decl->members[i].value->kind == Expression::INT_LITERAL);
			implicit_value = static_cast<IntLiteralExpression*>(en.decl->members[i].value)->value + 1;
		} else {
			implicit_value = implicit_value + 1;
		}
	}
	ASSERT(false);
	return 0xffFFffFFffFFffFFull;
}

static u32 structFieldByteOffset(StructResolvedType& st, ls_string_view name, ResolvedType*& out_type) {
	u32 offset = 0u;
	for (i32 i = 0; i < st.decl->fields.size(); ++i) {
		NamedDecl& field = st.decl->fields[i];
		ResolvedType* field_type = i < st.field_types.size() ? st.field_types[i] : field.resolved_type;
		if (equalStrings(field.name, name)) {
			out_type = field_type;
			return offset;
		}
		offset += u32(typeByteSize(*field_type));
	}
	ASSERT(false);
	return 0xffFFffFF;
}

static bool paramIsRef(const FunctionResolvedType& fn_type, u32 param_index) {
	return fn_type.decl && param_index < (u32)fn_type.decl->params.size() && fn_type.decl->params[param_index].is_ref;
}

static bool paramIsComptime(const FunctionResolvedType& fn_type, u32 param_index) {
	return fn_type.decl && param_index < (u32)fn_type.decl->params.size() && fn_type.decl->params[param_index].is_comptime;
}

static void compileCallArgs(FunctionCompiler& ctx, CallExpression& expr, const FunctionResolvedType& fn_type, u32 arg_offset) {
	for (i32 i = 0; i < expr.args.size(); ++i) {
		const u32 param_index = arg_offset + i;
		ResolvedType* param_type = fn_type.param_types[param_index];
		if (paramIsComptime(fn_type, param_index)) continue;
		if (paramIsRef(fn_type, param_index)) {
			Expression* arg = expr.args[i];
			UnaryExpression* un = static_cast<UnaryExpression*>(arg);
			tryEmitReference(ctx, *un->expression);
			continue;
		}
		compileExpressionAsType(ctx, *expr.args[i], *param_type);
	}
}


// Byte width of a callee's argument window, matching how arguments are pushed
// (reference parameters occupy a pointer, everything else its value width).
static u32 callArgWindowSize(const FunctionResolvedType& fn_type) {
	u32 total = 0u;
	for (i32 i = 0; i < fn_type.param_types.size(); ++i) {
		if (paramIsComptime(fn_type, i)) continue;
		const u32 byte_size = u32(paramIsRef(fn_type, i) ? typeKindByteSize(LS_TYPE_CPTR) : typeByteSize(*fn_type.param_types[i]));
		total += byte_size == 0u ? 1u : byte_size;
	}
	return total;
}

static ls_type_kind emitDirectCall(FunctionCompiler& ctx, CallExpression& expr, FunctionExpression& fn, Expression* receiver, u32 arg_offset, ls_type_kind hint) {
	FunctionResolvedType& fn_type = *static_cast<FunctionResolvedType*>(fn.resolved_type);
	if (receiver) {
		if (paramIsRef(fn_type, 0)) {
			tryEmitReference(ctx, *receiver);
		}
		else {
			const ls_type_kind receiver_kind = !fn_type.param_types.empty()
				? valueKindForType(*fn_type.param_types[0])
				: LS_TYPE_INVALID;
			compileExpression(ctx, *receiver, receiver_kind);
		}
	}
	compileCallArgs(ctx, expr, fn_type, arg_offset);
	emitCallDirect(ctx, fn.bytecode_index, callArgWindowSize(fn_type), typeByteSize(*fn_type.return_type));
	return valueKindForType(*fn_type.return_type);
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

static void emitConst1At(FunctionCompiler& ctx, u32 dst, u8 value) {
	emitOp(ctx.code, LS_OP_LOAD_CONST_1);
	emitFixedReg(ctx, dst);
	emitU8(ctx.code, value);
}

static void emitConst1(FunctionCompiler& ctx, u8 value) {
	const u32 dst = ctx.temp_top;
	emitConst1At(ctx, dst, value);
	setTempTop(ctx, dst + 1u);
}

static void emitConst2At(FunctionCompiler& ctx, u32 dst, u16 value) {
	emitOp(ctx.code, LS_OP_LOAD_CONST_2);
	emitFixedReg(ctx, dst);
	emitBytes(ctx.code, &value, sizeof(value));
}

static void emitConst2(FunctionCompiler& ctx, u16 value) {
	const u32 dst = ctx.temp_top;
	emitConst2At(ctx, dst, value);
	setTempTop(ctx, dst + 2u);
}

static void emitConst4At(FunctionCompiler& ctx, u32 dst, u32 value) {
	emitOp(ctx.code, LS_OP_LOAD_CONST_4);
	emitFixedReg(ctx, dst);
	emitU32(ctx.code, value);
}

static void emitConst4(FunctionCompiler& ctx, u32 value) {
	const u32 dst = ctx.temp_top;
	emitConst4At(ctx, dst, value);
	setTempTop(ctx, dst + 4u);
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

static void emitStringToCStr(FunctionCompiler& ctx) {
	const u32 src = ctx.temp_top - typeKindByteSize(LS_TYPE_STRING);
	emitOp(ctx.code, LS_OP_STRING_TO_CSTR);
	emitTempReg(ctx, src);
	emitTempReg(ctx, src);
}

static void emitCStrToString(FunctionCompiler& ctx) {
	const u32 src = ctx.temp_top - typeKindByteSize(LS_TYPE_CPTR);
	emitOp(ctx.code, LS_OP_CSTR_TO_STRING);
	emitTempReg(ctx, src);
	emitTempReg(ctx, src);
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

// Load from storage slot (LOCAL, LOCAL_REF, or GLOBAL) onto temp stack.
static void emitLoadSlot(FunctionCompiler& ctx, const StorageSlot& slot) {
	switch (slot.storage) {
		case StorageSlot::LOCAL_REF: {
			const u32 ref_size = typeKindByteSize(LS_TYPE_CPTR);
			emitLoadLocalBytes(ctx, slot.offset, ref_size);
			emitConst8(ctx, 0u);
			emitLoadAt(ctx, 1u, 0, slot.byte_size);
			break;
		}
		case StorageSlot::LOCAL: emitLoadLocalBytes(ctx, slot.offset, slot.byte_size); break;
		case StorageSlot::GLOBAL: emitLoadGlobalBytes(ctx, slot.offset, slot.byte_size); break;
	}
}

// Store from temp stack to storage slot (LOCAL, LOCAL_REF, or GLOBAL).
static void emitStoreSlot(FunctionCompiler& ctx, const StorageSlot& slot) {
	switch (slot.storage) {
		case StorageSlot::LOCAL_REF: {
			const u32 ref_size = typeKindByteSize(LS_TYPE_CPTR);
			emitLoadLocalBytes(ctx, slot.offset, ref_size);
			emitConst8(ctx, 0u);
			emitStoreAt(ctx, 1u, 0, slot.byte_size);
			break;
		}
		case StorageSlot::LOCAL: emitStoreLocalBytes(ctx, slot.offset, slot.byte_size); break;
		case StorageSlot::GLOBAL: emitStoreGlobalBytes(ctx, slot.offset, slot.byte_size); break;
	}
}

// Emit a reference to storage slot (LOCAL, LOCAL_REF, or GLOBAL).
static void emitSlotRef(FunctionCompiler& ctx, const StorageSlot& slot) {
	switch (slot.storage) {
		case StorageSlot::LOCAL_REF: emitLoadLocalBytes(ctx, slot.offset, typeKindByteSize(LS_TYPE_CPTR)); break;
		case StorageSlot::LOCAL: emitLocalRef(ctx, slot.offset); break;
		case StorageSlot::GLOBAL: emitGlobalRef(ctx, slot.offset); break;
	}
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
static void finalizeFunctionCode(FunctionCompiler& ctx, ls_function_bc& function, ls_arena& arena) {
	function.frame_size = ctx.frame_high_water;
	function.code_size = (u32)ctx.code.size();
	if (function.code_size > 0u) {
		function.code = static_cast<u8*>(arena.allocate(arena.user_data, function.code_size, alignof(u8)));
		copyMemory(function.code, ctx.code.data, function.code_size);
	}
}

static void compileIndexExpression(FunctionCompiler& ctx, Expression& expr) {
	const ls_type_kind kind = compileExpression(ctx, expr, LS_TYPE_I64);
	if (kind != LS_TYPE_I64) emitCast(ctx, kind, LS_TYPE_I64);
}

static void emitStaticBoundsCheck(FunctionCompiler& ctx, ResolvedType& type) {
	emitOp(ctx.code, LS_OP_BOUNDS_CHECK);
	emitTempReg(ctx, ctx.temp_top - typeKindByteSize(LS_TYPE_I64));
	emitU64(ctx.code, (u64)static_cast<ArrayResolvedType&>(type).size);
}

static bool tryEmitReference(FunctionCompiler& ctx, Expression& expr) {
	switch (expr.kind) {
		case Expression::IDENTIFIER: {
			IdentifierExpression* id = static_cast<IdentifierExpression*>(&expr);
			StorageSlot* slot = id->slot;
			emitSlotRef(ctx, *slot);
			return true;
		}
		case Expression::MEMBER: {
			MemberExpression* member = static_cast<MemberExpression*>(&expr);
			if (member->expression->kind == Expression::IDENTIFIER && member->resolved_symbol) {
				if (member->resolved_symbol->slot.storage == StorageSlot::GLOBAL) {
					emitGlobalRef(ctx, member->resolved_symbol->slot.offset);
					return true;
				}
			}
			if (member->expression->resolved_type->kind != ResolvedType::STRUCT) return false;
			StructResolvedType* st = static_cast<StructResolvedType*>(member->expression->resolved_type);
			ResolvedType* field_type = nullptr;
			u32 offset = structFieldByteOffset(*st, member->name, field_type);
			if (!tryEmitReference(ctx, *member->expression)) return false;
			emitConst8(ctx, 0u);
			emitRefAt(ctx, 1u, (i32)offset);
			return true;
		}
		case Expression::BRACKET: {
			BracketExpression* br = static_cast<BracketExpression*>(&expr);
			if (!tryEmitReference(ctx, *br->base)) return false;
			compileIndexExpression(ctx, *br->args[0]);
			emitStaticBoundsCheck(ctx, *br->base->resolved_type);
			emitRefAt(ctx, typeByteSize(*br->resolved_type), 0);
			return true;
		}
		default:
			return false;
	}
}

// Some expressions are not directly addressable, but later code still needs a
// stable pointer to their value (for slices, indexing, compound stores, etc.).
// Example: `foo().items[0]` needs a pointer even though `foo()` is a temporary
// value. If the expression can already be referenced, use that; otherwise
// materialize it into a temporary and reference the temporary instead.
static bool emitAddressableReference(FunctionCompiler& ctx, Expression& expr, u32 reserved_prefix_bytes = 0) {
	if (tryEmitReference(ctx, expr)) return true;
	compileExpression(ctx, expr, LS_TYPE_INVALID);
	const u32 byte_size = typeByteSize(*expr.resolved_type);
	const u32 value_offset = ctx.temp_top - byte_size;
	if (reserved_prefix_bytes > 0u) {
		ctx.temp_top = value_offset + reserved_prefix_bytes;
	}
	const u32 temp = ctx.addLocal(expr.resolved_type, valueKindForType(*expr.resolved_type), true);
	emitOp(ctx.code, LS_OP_COPY);
	emitFixedReg(ctx, temp);
	emitFixedReg(ctx, value_offset);
	emitU32(ctx.code, byte_size);
	ctx.temp_top = value_offset;
	emitLocalRef(ctx, temp);
	return true;
}

static bool tryEmitReferenceLoad(FunctionCompiler& ctx, Expression& expr, u32 byte_size) {
	ASSERT(byte_size > 0);
	const u32 result_offset = ctx.temp_top;
	const u32 saved_next_local_offset = ctx.next_local_offset;
	const u32 ref_offset = ctx.addLocal(nullptr, LS_TYPE_I64, true);
	ctx.temp_top = ctx.next_local_offset;
	if (!tryEmitReference(ctx, expr)) {
		// Nothing was emitted; release the ref slot so the failed attempt
		// does not inflate the frame.
		ctx.next_local_offset = saved_next_local_offset;
		ctx.temp_top = result_offset;
		return false;
	}
	emitStoreLocalBytes(ctx, ref_offset, typeKindByteSize(LS_TYPE_CPTR));
	emitLoadLocalBytes(ctx, ref_offset, typeKindByteSize(LS_TYPE_CPTR));
	emitConst8(ctx, 0u);
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


static void compileExpressionAsType(FunctionCompiler& ctx, Expression& expr, ResolvedType& expected_type) {
	if (expected_type.kind == ResolvedType::UNION) {
		UnionResolvedType& un = static_cast<UnionResolvedType&>(expected_type);
		if (expr.resolved_type && expr.resolved_type->kind == ResolvedType::UNION) {
			UnionResolvedType& source = static_cast<UnionResolvedType&>(*expr.resolved_type);
			if (source.members.size() == un.members.size()) {
				bool same_layout = true;
				for (i32 i = 0; i < un.members.size(); ++i) {
					if (source.members[i] != un.members[i]) { same_layout = false; break; }
				}
				if (same_layout) {
					compileExpression(ctx, expr, valueKindForType(expected_type));
					return;
				}
			}

			const u32 source_size = typeByteSize(source);
			const u32 source_offset = ctx.addLocal(&source, valueKindForType(source));
			compileExpression(ctx, expr, valueKindForType(source));
			emitStoreLocalBytes(ctx, source_offset, source_size);

			const u32 result_top = ctx.temp_top;
			ExpArray<u32> end_jumps(*ctx.bytecode->arena);
			for (i32 i = 0; i < source.members.size(); ++i) {
				i32 target_index = -1;
				for (i32 j = 0; j < un.members.size(); ++j) {
					if (source.members[i] == un.members[j]) { target_index = j; break; }
				}
				ASSERT(target_index >= 0);

				emitLoadLocalBytes(ctx, source_offset, 4);
				emitIntegerConstant(ctx, LS_TYPE_I32, i);
				emitCompareOp(ctx, LS_OP_EQ, LS_TYPE_I32);
				const u32 next_member_jump = emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_FALSE);

				emitIntegerConstant(ctx, LS_TYPE_I32, target_index);
				const u32 member_size = typeByteSize(*source.members[i]);
				emitLoadLocalBytes(ctx, source_offset + 4, member_size);
				const u32 payload_size = typeByteSize(expected_type) - 4;
				if (payload_size > member_size) emitZeroBytes(ctx, payload_size - member_size);
				end_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP));

				patchJumpRelative(ctx.code, next_member_jump, (u32)ctx.code.size());
				ctx.temp_top = result_top;
			}
			emitZeroBytes(ctx, typeByteSize(expected_type));
			const u32 end = (u32)ctx.code.size();
			for (u32 jump : end_jumps) patchJumpRelative(ctx.code, jump, end);
			return;
		}
		for (i32 i = 0; i < un.members.size(); ++i) {
			if (expr.resolved_type != un.members[i]) continue;
			emitIntegerConstant(ctx, LS_TYPE_I32, i);
			compileExpressionAsType(ctx, expr, *un.members[i]);
			const u32 payload_size = typeByteSize(expected_type) - 4;
			const u32 member_size = typeByteSize(*un.members[i]);
			if (payload_size > member_size) emitZeroBytes(ctx, payload_size - member_size);
			return;
		}
		ASSERT(false);
	}
	if (expected_type.kind == ResolvedType::NULLABLE) {
		NullableResolvedType& nullable = static_cast<NullableResolvedType&>(expected_type);
		const bool is_null = expr.kind == Expression::NULL_LITERAL || expr.kind == Expression::UNDEFINED;
		emitIntegerConstant(ctx, LS_TYPE_BOOL, is_null ? 0u : 1u);
		if (is_null) {
			emitZeroBytes(ctx, typeByteSize(*nullable.inner));
		}
		else {
			compileExpressionAsType(ctx, expr, *nullable.inner);
		}
		return;
	}
	// Slice conversions change representation, not just the reported type. Arrays
	// are inline values, whereas slices are an absolute backing reference and length.
	if (expected_type.kind == ResolvedType::SLICE) {
		if (expr.kind == Expression::NULL_LITERAL) {
			emitConst8(ctx, 0u);
			emitConst8(ctx, 0u);
			return;
		}
		if (expr.resolved_type && expr.resolved_type->kind == ResolvedType::ARRAY) {
			ArrayResolvedType* array = static_cast<ArrayResolvedType*>(expr.resolved_type);
			if (!tryEmitReference(ctx, expr)) {
				compileExpression(ctx, expr, LS_TYPE_INVALID);
				emitLocalRef(ctx, ctx.temp_top - typeByteSize(*expr.resolved_type));
			}
			emitConst8(ctx, (u64)array->size);
			return;
		}
	}
	if (expected_type.kind == ResolvedType::CSTR && expr.resolved_type && expr.resolved_type->kind == ResolvedType::STRING) {
		compileExpression(ctx, expr, LS_TYPE_STRING);
		emitStringToCStr(ctx);
		return;
	}
	if (expr.kind == Expression::UNDEFINED) {
		emitZeroBytes(ctx, typeByteSize(expected_type));
		return;
	}
	compileExpression(ctx, expr, valueKindForType(expected_type));
}

// Numeric opcode groups (ADD/SUB/MUL/DIV/MOD/NEG) are laid out in the same kind
// order, so the concrete opcode is the group base plus this per-kind index.
static u32 numericKindIndex(ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_I8:  return 0;
		case LS_TYPE_U8:  return 1;
		case LS_TYPE_I16: return 2;
		case LS_TYPE_U16: return 3;
		case LS_TYPE_I32: return 4;
		case LS_TYPE_U32: return 5;
		case LS_TYPE_I64: return 6;
		case LS_TYPE_U64: return 7;
		case LS_TYPE_F32: return 8;
		case LS_TYPE_F64: return 9;
		default: ASSERT(false); return -1;
	}
}

static void emitNumericBinary(FunctionCompiler& ctx, ls_op group_base, ls_type_kind kind) {
	u32 index = numericKindIndex(kind);
	emitBinaryOp(ctx, (ls_op)((u32)group_base + index), typeKindByteSize(kind));
}

static void emitNumericStoreOp(FunctionCompiler& ctx, ls_type_kind kind, Token::Type op) {
	switch (op) {
		case Token::PLUS_EQUAL:  emitNumericBinary(ctx, LS_OP_ADD_I8, kind); return;
		case Token::MINUS_EQUAL: emitNumericBinary(ctx, LS_OP_SUB_I8, kind); return;
		case Token::STAR_EQUAL:  emitNumericBinary(ctx, LS_OP_MUL_I8, kind); return;
		case Token::SLASH_EQUAL: emitNumericBinary(ctx, LS_OP_DIV_I8, kind); return;
		default: ASSERT(false); return;
	}
}

// Compound assignment compute step: the current lhs value is already on top of
// temporaries; combine it with `rhs`, either through an overloaded operator or a
// numeric opcode, leaving the result in the lhs value's place.
static void emitCompoundValue(FunctionCompiler& ctx, Expression& rhs, ls_type_kind value_kind, Token::Type op, FunctionExpression* op_fn) {
	if (op_fn) {
		const FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(op_fn->resolved_type);
		compileExpressionAsType(ctx, rhs, *fn_type->param_types[1]);
		emitCallDirect(ctx, op_fn->bytecode_index, callArgWindowSize(*fn_type), typeByteSize(*fn_type->return_type));
		return;
	}
	compileExpression(ctx, rhs, value_kind);
	emitNumericStoreOp(ctx, value_kind, op);
}

static void emitBracketStore(FunctionCompiler& ctx, BracketExpression& br, Expression& rhs, ls_type_kind value_kind, Token::Type op, FunctionExpression* op_fn) {
	const u32 element_size = typeByteSize(*br.resolved_type);
	const bool is_slice = br.base->resolved_type && br.base->resolved_type->kind == ResolvedType::SLICE;

	if (op == Token::EQUAL) {
		if (is_slice) {
			compileExpression(ctx, *br.base, LS_TYPE_SLICE);
			compileIndexExpression(ctx, *br.args[0]);
			compileExpressionAsType(ctx, rhs, *br.resolved_type);
			emitSliceStore(ctx, element_size);
		}
		else {
			if (!tryEmitReference(ctx, *br.base)) {
				compileExpression(ctx, *br.base, LS_TYPE_INVALID);
				emitLocalRef(ctx, ctx.temp_top - typeByteSize(*br.base->resolved_type));
			}
			compileIndexExpression(ctx, *br.args[0]);
			emitStaticBoundsCheck(ctx, *br.base->resolved_type);
			compileExpressionAsType(ctx, rhs, *br.resolved_type);
			emitStoreAt(ctx, element_size, 0, element_size);
		}
		return;
	}

	if (is_slice) {
		// Evaluate the base and index once. Compound assignment needs them for both
		// the checked load and checked store, and either expression may have effects.
		const u32 slice_offset = ctx.addLocal(br.base->resolved_type, LS_TYPE_SLICE);
		const u32 index_offset = ctx.addLocal(nullptr, LS_TYPE_I64);
		const u32 value_offset = ctx.addLocal(br.resolved_type, value_kind);
		const u32 slice_size = typeByteSize(*br.base->resolved_type);
		const u32 index_size = typeKindByteSize(LS_TYPE_I64);
		const u32 value_size = typeByteSize(*br.resolved_type);
		compileExpression(ctx, *br.base, LS_TYPE_SLICE);
		emitStoreLocalBytes(ctx, slice_offset, slice_size);
		compileIndexExpression(ctx, *br.args[0]);
		emitStoreLocalBytes(ctx, index_offset, index_size);
		emitLoadLocalBytes(ctx, slice_offset, slice_size);
		emitLoadLocalBytes(ctx, index_offset, index_size);
		emitSliceLoad(ctx, element_size);
		emitCompoundValue(ctx, rhs, value_kind, op, op_fn);
		emitStoreLocalBytes(ctx, value_offset, value_size);
		emitLoadLocalBytes(ctx, slice_offset, slice_size);
		emitLoadLocalBytes(ctx, index_offset, index_size);
		emitLoadLocalBytes(ctx, value_offset, value_size);
		emitSliceStore(ctx, element_size);
		return;
	}

	const u32 ref_size = typeKindByteSize(LS_TYPE_CPTR);
	const u32 index_size = typeKindByteSize(LS_TYPE_I64);
	const u32 ref_offset = ctx.addLocal(nullptr, LS_TYPE_I64, true);
	if (!tryEmitReference(ctx, *br.base)) {
		compileExpression(ctx, *br.base, LS_TYPE_INVALID);
		emitLocalRef(ctx, ctx.temp_top - typeByteSize(*br.base->resolved_type));
	}
	emitStoreLocalBytes(ctx, ref_offset, ref_size);
	const u32 index_offset = ctx.addLocal(nullptr, LS_TYPE_I64);
	const u32 value_offset = ctx.addLocal(br.resolved_type, value_kind);

	compileIndexExpression(ctx, *br.args[0]);
	emitStoreLocalBytes(ctx, index_offset, index_size);

	emitLoadLocalBytes(ctx, ref_offset, ref_size);
	emitLoadLocalBytes(ctx, index_offset, index_size);
	emitStaticBoundsCheck(ctx, *br.base->resolved_type);
	emitLoadAt(ctx, element_size, 0, element_size);

	emitCompoundValue(ctx, rhs, value_kind, op, op_fn);
	emitStoreLocalBytes(ctx, value_offset, element_size);

	emitLoadLocalBytes(ctx, ref_offset, ref_size);
	emitLoadLocalBytes(ctx, index_offset, index_size);
	emitStaticBoundsCheck(ctx, *br.base->resolved_type);
	emitLoadLocalBytes(ctx, value_offset, element_size);
	emitStoreAt(ctx, element_size, 0, element_size);
}

static void emitSlice(FunctionCompiler& ctx, SliceExpression& br) {
	ResolvedType* base_type = br.base->resolved_type;
	ResolvedType* element_type = nullptr;
	if (base_type->kind == ResolvedType::ARRAY) {
		ArrayResolvedType* array = static_cast<ArrayResolvedType*>(base_type);
		element_type = array->element_type;
		if (!tryEmitReference(ctx, *br.base)) {
			compileExpression(ctx, *br.base, LS_TYPE_INVALID);
			emitLocalRef(ctx, ctx.temp_top - typeByteSize(*base_type));
		}
		emitConst8(ctx, (u64)array->size);
	}
	else if (base_type->kind == ResolvedType::SLICE) {
		element_type = static_cast<SliceResolvedType*>(base_type)->element_type;
		compileExpression(ctx, *br.base, LS_TYPE_SLICE);
	}

	// Save the source pair because an omitted end bound reuses its dynamic length,
	// while explicit bounds may themselves evaluate arbitrary expressions.
	const u32 source_offset = ctx.addLocal(br.resolved_type, LS_TYPE_SLICE, true);
	const u32 slice_size = typeByteSize(*br.resolved_type);
	emitStoreLocalBytes(ctx, source_offset, slice_size);
	emitLoadLocalBytes(ctx, source_offset, slice_size);
	if (br.begin) {
		compileIndexExpression(ctx, *br.begin);
	}
	else {
		emitIntegerConstant(ctx, LS_TYPE_I64, 0u);
	}
	if (br.end) {
		compileIndexExpression(ctx, *br.end);
	}
	else {
		emitLoadLocalBytes(ctx, source_offset + typeKindByteSize(LS_TYPE_CPTR), typeKindByteSize(LS_TYPE_I64));
	}
	emitSliceOp(ctx, typeByteSize(*element_type));
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
		compileStatement(ctx, *ctx.deferreds[(u32)i], return_kind, current_label);
	}
}

static ls_type_kind compileBinary(FunctionCompiler& ctx, BinaryExpression& expr, ls_type_kind hint) {
	if (expr.op == Token::AND || expr.op == Token::OR) {
		compileExpression(ctx, *expr.lhs, LS_TYPE_BOOL);
		const ls_op short_circuit_op = expr.op == Token::AND ? LS_OP_JUMP_IF_FALSE : LS_OP_JUMP_IF_TRUE;
		const u32 short_circuit_jump = emitJumpPlaceholder(ctx, short_circuit_op);
		// The short-circuit result and the evaluated-rhs result share one register,
		// so both paths must produce it at the same temporary offset.
		const u32 result_top = ctx.temp_top;

		compileExpression(ctx, *expr.rhs, LS_TYPE_BOOL);
		const u32 end_jump = emitJumpPlaceholder(ctx, LS_OP_JUMP);

		patchJumpRelative(ctx.code, short_circuit_jump, (u32)ctx.code.size());
		ctx.temp_top = result_top;
		emitConst1(ctx, expr.op == Token::AND ? 0u : 1u);
		patchJumpRelative(ctx.code, end_jump, (u32)ctx.code.size());
		return LS_TYPE_BOOL;
	}

	if (expr.resolved_fn) {
		FunctionExpression& fn = *expr.resolved_fn;
		FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(fn.resolved_type);
		compileExpressionAsType(ctx, *expr.lhs, *fn_type->param_types[0]);
		compileExpressionAsType(ctx, *expr.rhs, *fn_type->param_types[1]);
		emitCallDirect(ctx, fn.bytecode_index, callArgWindowSize(*fn_type), typeByteSize(*fn_type->return_type));
		return valueKindForType(*fn_type->return_type);
	}
	if (expr.op == Token::IS) {
		UnionResolvedType& source = static_cast<UnionResolvedType&>(*expr.lhs->resolved_type);
		ResolvedType* member = static_cast<MetaType*>(expr.rhs->resolved_type)->inner;
		i32 member_index = -1;
		for (i32 i = 0; i < source.members.size(); ++i) {
			if (source.members[i] == member) { member_index = i; break; }
		}
		ASSERT(member_index >= 0);
		const u32 source_size = typeByteSize(source);
		const u32 source_offset = ctx.addLocal(&source, valueKindForType(source));
		compileExpression(ctx, *expr.lhs, valueKindForType(source));
		emitStoreLocalBytes(ctx, source_offset, source_size);
		emitLoadLocalBytes(ctx, source_offset, 4);
		emitIntegerConstant(ctx, LS_TYPE_I32, member_index);
		emitCompareOp(ctx, LS_OP_EQ, LS_TYPE_I32);
		return LS_TYPE_BOOL;
	}

	// Null check: `nullable == null` or `nullable != null` — only check has_value offset.
	if (expr.op == Token::EQUAL_EQUAL || expr.op == Token::BANG_EQUAL) {
		Expression* nullable_side = nullptr;
		if (expr.rhs && expr.rhs->kind == Expression::NULL_LITERAL) nullable_side = expr.lhs;
		else if (expr.lhs && expr.lhs->kind == Expression::NULL_LITERAL) nullable_side = expr.rhs;
		if (nullable_side && nullable_side->resolved_type && nullable_side->resolved_type->kind == ResolvedType::NULLABLE) {
			// Compile nullable (pushes has_value, value); pop value, compare has_value to 0.
			compileExpression(ctx, *nullable_side, LS_TYPE_NULL_VALUE);
			NullableResolvedType* nullable_type = static_cast<NullableResolvedType*>(nullable_side->resolved_type);
			emitPop(ctx, typeByteSize(*nullable_type->inner)); // discard value bytes, has_value remains
			emitIntegerConstant(ctx, LS_TYPE_BOOL, 0u);
			emitCompareOp(ctx, expr.op == Token::EQUAL_EQUAL ? LS_OP_EQ : LS_OP_NE, LS_TYPE_BOOL);
			return LS_TYPE_BOOL;
		}
	}

	const ls_type_kind lhs_hint = numericKindForOp(expr.lhs ? defaultLiteralKind(*expr.lhs, hint) : LS_TYPE_INVALID, hint);
	const ls_type_kind rhs_hint = numericKindForOp(expr.rhs ? defaultLiteralKind(*expr.rhs, lhs_hint) : LS_TYPE_INVALID, lhs_hint);
	ls_type_kind lhs_kind = compileExpression(ctx, *expr.lhs, lhs_hint);
	ls_type_kind rhs_kind = compileExpression(ctx, *expr.rhs, rhs_hint);
	if (lhs_kind == LS_TYPE_INVALID) lhs_kind = rhs_kind;
	const ls_type_kind kind = numericKindForOp(lhs_kind, rhs_kind);

	switch (expr.op) {
		case Token::PLUS: emitNumericBinary(ctx, LS_OP_ADD_I8, kind); return kind;
		case Token::MINUS: emitNumericBinary(ctx, LS_OP_SUB_I8, kind); return kind;
		case Token::STAR: emitNumericBinary(ctx, LS_OP_MUL_I8, kind); return kind;
		case Token::SLASH: emitNumericBinary(ctx, LS_OP_DIV_I8, kind); return kind;
		case Token::PERCENT: emitNumericBinary(ctx, LS_OP_MOD_I8, kind); return kind;
		case Token::EQUAL_EQUAL:
		case Token::BANG_EQUAL:
		case Token::LT:
		case Token::LT_EQUAL:
		case Token::GT:
		case Token::GT_EQUAL: {
			ls_op cmp_op;
			switch (expr.op) {
				case Token::EQUAL_EQUAL: cmp_op = LS_OP_EQ; break;
				case Token::BANG_EQUAL: cmp_op = LS_OP_NE; break;
				case Token::LT: cmp_op = LS_OP_LT; break;
				case Token::LT_EQUAL: cmp_op = LS_OP_LE; break;
				case Token::GT: cmp_op = LS_OP_GT; break;
				case Token::GT_EQUAL: cmp_op = LS_OP_GE; break;
				default: return LS_TYPE_INVALID;
			}
			emitCompareOp(ctx, cmp_op, lhs_kind);
			return LS_TYPE_BOOL;
		}
		default:
			return LS_TYPE_INVALID;
	}
}

static ls_type_kind compileCall(FunctionCompiler& ctx, CallExpression& expr, ls_type_kind hint) {
	ASSERT(expr.callee);

	// If the type checker already resolved the direct call target, use it and return.
	// Handles both template instantiation and UFCS function selection without
	// duplicating the lookup in each callee-shape branch below.
	if (expr.resolved_fn) {
		u32 arg_offset = 0;
		Expression* receiver = nullptr;
		if (expr.callee && expr.callee->kind == Expression::MEMBER) {
			MemberExpression* member = static_cast<MemberExpression*>(expr.callee);
			if (member->expression->resolved_type) {
				receiver = member->expression;
				arg_offset = 1;
			}
		}
		return emitDirectCall(ctx, expr, *expr.resolved_fn, receiver, arg_offset, hint);
	}

	if (expr.callee->kind == Expression::IDENTIFIER) {
		IdentifierExpression* id = static_cast<IdentifierExpression*>(expr.callee);
		if (equalStrings(id->name, makeStringView("length"))
			&& expr.args.size() == 1
			&& expr.args[0]->resolved_type
			&& (expr.args[0]->resolved_type->kind == ResolvedType::ARRAY || expr.args[0]->resolved_type->kind == ResolvedType::SLICE))
		{
			ResolvedType* arg_type = expr.args[0]->resolved_type;
			if (arg_type->kind == ResolvedType::ARRAY) {
				emitConst8(ctx, (u64)static_cast<ArrayResolvedType*>(arg_type)->size);
			}
			else {
				ASSERT(arg_type->kind == ResolvedType::SLICE);
				compileExpression(ctx, *expr.args[0], LS_TYPE_SLICE);
				emitSliceLength(ctx);
			}
			return LS_TYPE_I64;
		}
		if (id->symbol) {
			// TODO why is expr.resolved_fn null here?
			FunctionExpression* fn = static_cast<FunctionExpression*>(id->symbol->expression);
			return emitDirectCall(ctx, expr, *fn, nullptr, 0u, hint);
		}
	}

	if (expr.callee->kind == Expression::MEMBER) {
		MemberExpression* member = static_cast<MemberExpression*>(expr.callee);
		if (member->resolved_fn) {
			// TODO why is expr.resolved_fn null here?
			return emitDirectCall(ctx, expr, *member->resolved_fn, nullptr, 0u, hint);
		}
	}

	if (expr.callee->kind == Expression::BRACKET && expr.callee->resolved_type && expr.callee->resolved_type->kind == ResolvedType::FUNCTION) {
		FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(expr.callee->resolved_type);
		if (fn_type->decl) {
			// TODO why is expr.resolved_fn null here?
			return emitDirectCall(ctx, expr, *fn_type->decl, nullptr, 0u, hint);
		}
	}

	// Indirect call: callee value sits below the argument list.
	compileExpression(ctx, *expr.callee, LS_TYPE_FUNCTION);
	const FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(expr.callee->resolved_type);
	compileCallArgs(ctx, expr, *fn_type, 0u);
	emitCallIndirect(ctx, callArgWindowSize(*fn_type), typeByteSize(*fn_type->return_type));
	return valueKindForType(*fn_type->return_type);
}

static ls_type_kind compileMember(FunctionCompiler& ctx, MemberExpression& member) {
	if (!member.expression) {
		// .enum_member
		ASSERT(member.resolved_type && member.resolved_type->kind == ResolvedType::ENUM);
		EnumResolvedType* en = static_cast<EnumResolvedType*>(member.resolved_type);
		u64 enum_value = enumMemberValue(*en, member.name);
		ls_type_kind kind = (i64)enum_value >= -2147483648LL && (i64)enum_value <= 2147483647LL ? LS_TYPE_I32 : LS_TYPE_I64;
		emitIntegerConstant(ctx, kind, enum_value);
		return kind;
	}
	ResolvedType* base_rt = member.expression->resolved_type;
	EnumResolvedType* enum_via_meta = (base_rt && base_rt->kind == ResolvedType::META && static_cast<MetaType*>(base_rt)->inner->kind == ResolvedType::ENUM)
		? static_cast<EnumResolvedType*>(static_cast<MetaType*>(base_rt)->inner) : nullptr;
	if (enum_via_meta) {
		u64 enum_value = enumMemberValue(*enum_via_meta, member.name);
		ls_type_kind kind = (i64)enum_value >= -2147483648LL && (i64)enum_value <= 2147483647LL ? LS_TYPE_I32 : LS_TYPE_I64;
		emitIntegerConstant(ctx, kind, enum_value);
		return kind;
	}
	if (member.expression->kind == Expression::IDENTIFIER) {
		IdentifierExpression* base = static_cast<IdentifierExpression*>(member.expression);
		if (member.resolved_symbol && member.resolved_symbol->expression) {
			switch (member.resolved_symbol->expression->kind) {
				case Expression::FUNCTION:
					emitIntegerConstant(ctx, LS_TYPE_FUNCTION, member.resolved_fn->bytecode_index);
					return LS_TYPE_FUNCTION;
				case Expression::INT_LITERAL:
					emitIntegerConstant(ctx, valueKindForType(*member.resolved_symbol->resolved_type), static_cast<IntLiteralExpression*>(member.resolved_symbol->expression)->value);
					return valueKindForType(*member.resolved_symbol->resolved_type);
				case Expression::FLOAT_LITERAL: {
					FloatLiteralExpression* fl = static_cast<FloatLiteralExpression*>(member.resolved_symbol->expression);
					const ls_type_kind kind = valueKindForType(*member.resolved_symbol->resolved_type);
					if (kind == LS_TYPE_F32) {
						emitConst4(ctx, bitcastF32ToU32(static_cast<float>(fl->value)));
					}
					else {
						emitConst8(ctx, bitcastF64ToU64(fl->value));
					}
					return kind;
				}
				case Expression::BOOL_LITERAL:
					emitIntegerConstant(ctx, LS_TYPE_BOOL, static_cast<BoolLiteralExpression*>(member.resolved_symbol->expression)->value ? 1u : 0u);
					return LS_TYPE_BOOL;
				case Expression::STRING_LITERAL: {
					u32 string_index = 0;
					appendStringLiteral(*ctx.bytecode, static_cast<StringLiteralExpression*>(member.resolved_symbol->expression)->value, string_index);
					emitConstString(ctx, string_index);
					return LS_TYPE_STRING;
				}
				case Expression::NULL_LITERAL:
				case Expression::UNDEFINED:
					emitZeroBytes(ctx, typeByteSize(*member.resolved_symbol->resolved_type));
					return valueKindForType(*member.resolved_symbol->resolved_type);
				default:
					break;
			}
		}
		// Direct frame access only applies to locals; global bases go through the
		// generic reference-based path below.
		StorageSlot* slot = base->slot;
		if (slot && slot->storage != StorageSlot::GLOBAL) {
			ResolvedType* value_type = slot->type;
			u32 value_offset = 0u;
			if (value_type->kind == ResolvedType::NULLABLE) {
				value_type = member.expression->resolved_type;
				value_offset = 1u;
			}
			else if (value_type->kind == ResolvedType::UNION) {
				value_type = member.expression->resolved_type;
				value_offset = 4u;
			}
			ASSERT(value_type->kind == ResolvedType::STRUCT);
			StructResolvedType* st = static_cast<StructResolvedType*>(value_type);
			ResolvedType* field_type = nullptr;
			u32 offset = structFieldByteOffset(*st, member.name, field_type);
			if (slot->storage == StorageSlot::LOCAL_REF) {
				if (slot->type->kind == ResolvedType::UNION) {
					emitLoadLocalBytes(ctx, slot->offset, typeKindByteSize(LS_TYPE_CPTR));
					emitConst8(ctx, value_offset + offset);
					emitLoadAt(ctx, 1u, 0, typeByteSize(*field_type));
					return valueKindForType(*field_type);
				}
				tryEmitReferenceLoad(ctx, member, typeByteSize(*field_type));
				return valueKindForType(*field_type);
			}
			emitLoadLocalBytes(ctx, slot->offset + value_offset + offset, typeByteSize(*field_type));
			return valueKindForType(*field_type);
		}
	}

	ASSERT(member.expression->resolved_type && member.expression->resolved_type->kind == ResolvedType::STRUCT);
	StructResolvedType* st = static_cast<StructResolvedType*>(member.expression->resolved_type);
	ResolvedType* field_type = nullptr;
	u32 offset = structFieldByteOffset(*st, member.name, field_type);
	// Try reference-based load first (works for addressable lvalues).
	if (tryEmitReferenceLoad(ctx, member, typeByteSize(*member.resolved_type))) return valueKindForType(*member.resolved_type);
	
	// For temporaries (e.g. call results), compile the base onto the stack,
	// store it in a temp local, then load the desired field offset.
	const u32 struct_byte_size = typeByteSize(*member.expression->resolved_type);
	compileExpression(ctx, *member.expression, LS_TYPE_INVALID);
	const u32 result_offset = ctx.temp_top - struct_byte_size;
	const u32 saved_next_local_offset = ctx.next_local_offset;
	const u32 temp = ctx.addLocal(member.expression->resolved_type, valueKindForType(*member.expression->resolved_type), true);
	emitStoreLocalBytes(ctx, temp, struct_byte_size, false);
	ctx.temp_top = ctx.next_local_offset;
	emitLoadLocalBytes(ctx, temp + offset, typeByteSize(*field_type));
	const u32 field_size = typeByteSize(*field_type);
	const u32 loaded_offset = ctx.temp_top - field_size;
	if (loaded_offset != result_offset) {
		emitOp(ctx.code, LS_OP_COPY);
		emitTempReg(ctx, result_offset);
		emitTempReg(ctx, loaded_offset);
		emitU32(ctx.code, field_size);
		ctx.temp_top = result_offset + field_size;
	}
	ctx.next_local_offset = saved_next_local_offset;
	return valueKindForType(*field_type);
}

static ls_type_kind compileExpression(FunctionCompiler& ctx, Expression& expr, ls_type_kind hint) {
	switch (expr.kind) {
		case Expression::INT_LITERAL: {
			const ls_type_kind kind = toTypeKind(*expr.resolved_type);
			const u64 int_value = static_cast<IntLiteralExpression&>(expr).value;
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
			const ls_type_kind kind = defaultLiteralKind(expr, hint);
			if (kind == LS_TYPE_F32) {
				const float value = static_cast<float>(static_cast<FloatLiteralExpression&>(expr).value);
				emitConst4(ctx, bitcastF32ToU32(value));
			}
			else {
				emitConst8(ctx, bitcastF64ToU64(static_cast<FloatLiteralExpression&>(expr).value));
			}
			return kind;
		}
		case Expression::BOOL_LITERAL: {
			emitIntegerConstant(ctx, LS_TYPE_BOOL, static_cast<BoolLiteralExpression&>(expr).value ? 1u : 0u);
			return LS_TYPE_BOOL;
		}
		case Expression::STRING_LITERAL: {
			u32 string_index = 0;
			appendStringLiteral(*ctx.bytecode, static_cast<StringLiteralExpression&>(expr).value, string_index);
			emitConstString(ctx, string_index);
			return LS_TYPE_STRING;
		}
		case Expression::NULL_LITERAL: {
			emitConst8(ctx, 0);
			if (hint == LS_TYPE_SLICE) {
				emitConst8(ctx, 0);
			}
			ASSERT(hint != LS_TYPE_INVALID);
			return hint;
		}
		case Expression::UNDEFINED: {
			emitZeroBytes(ctx, typeKindByteSize(hint)); // TODO do we need to emit zero bytes here?
			ASSERT(hint != LS_TYPE_INVALID);
			return hint;
		}
		case Expression::TYPE_LITERAL: {
			emitIntegerConstant(ctx, LS_TYPE_I32, (u64)(uintptr)static_cast<TypeLiteralExpression&>(expr).type);
			return LS_TYPE_I32;
		}
		case Expression::IDENTIFIER: {
			IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
			if (StorageSlot* slot = id.slot) {
				if (slot->type && slot->type->kind == ResolvedType::UNION && expr.resolved_type->kind != ResolvedType::UNION) {
					const u32 payload_size = typeByteSize(*expr.resolved_type);
					switch (slot->storage) {
						case StorageSlot::LOCAL: emitLoadLocalBytes(ctx, slot->offset + 4, payload_size); break;
						case StorageSlot::GLOBAL: emitLoadGlobalBytes(ctx, slot->offset + 4, payload_size); break;
						case StorageSlot::LOCAL_REF:
							emitLoadLocalBytes(ctx, slot->offset, typeKindByteSize(LS_TYPE_CPTR));
							emitConst8(ctx, 4u);
							emitLoadAt(ctx, 1u, 0, payload_size);
							break;
					}
					return valueKindForType(*expr.resolved_type);
				}
				if (slot->storage == StorageSlot::LOCAL_REF) {
					tryEmitReferenceLoad(ctx, expr, typeByteSize(*slot->type));
					return valueKindForType(*slot->type);
				}
				emitLoadSlot(ctx, *slot);
				return slot->kind != LS_TYPE_INVALID ? slot->kind : LS_TYPE_I32;
			}
			// function template instance
			FunctionExpression* fn = id.resolved_fn ? id.resolved_fn : static_cast<FunctionExpression*>(id.symbol->expression);
			emitIntegerConstant(ctx, LS_TYPE_FUNCTION, fn->bytecode_index);
			return LS_TYPE_FUNCTION;
		}
		case Expression::BINARY: return compileBinary(ctx, static_cast<BinaryExpression&>(expr), hint);
		case Expression::CAST: {
			CastExpression& cast = static_cast<CastExpression&>(expr);
			if (cast.expression->resolved_type->kind == ResolvedType::UNION && expr.resolved_type->kind == ResolvedType::NULLABLE) {
				UnionResolvedType& source = static_cast<UnionResolvedType&>(*cast.expression->resolved_type);
				NullableResolvedType& nullable = static_cast<NullableResolvedType&>(*expr.resolved_type);
				i32 member_index = -1;
				for (i32 i = 0; i < source.members.size(); ++i) {
					if (source.members[i] == nullable.inner) { member_index = i; break; }
				}
				ASSERT(member_index >= 0);
				const u32 source_size = typeByteSize(source);
				const u32 source_offset = ctx.addLocal(&source, valueKindForType(source));
				compileExpression(ctx, *cast.expression, valueKindForType(source));
				emitStoreLocalBytes(ctx, source_offset, source_size);
				emitLoadLocalBytes(ctx, source_offset, 4);
				emitIntegerConstant(ctx, LS_TYPE_I32, member_index);
				emitCompareOp(ctx, LS_OP_EQ, LS_TYPE_I32);
				emitLoadLocalBytes(ctx, source_offset + 4, typeByteSize(*nullable.inner));
				return LS_TYPE_NULL_VALUE;
			}
			const ls_type_kind dst_kind = toTypeKind(*expr.resolved_type);
			const ls_type_kind src_kind = compileExpression(ctx, *cast.expression, toTypeKind(*cast.expression->resolved_type));
			if (cast.expression->resolved_type->kind == ResolvedType::STRING && expr.resolved_type->kind == ResolvedType::CSTR) {
				emitStringToCStr(ctx); return dst_kind;
			}
			if (cast.expression->resolved_type->kind == ResolvedType::CSTR && expr.resolved_type->kind == ResolvedType::STRING) {
				emitCStrToString(ctx); return dst_kind;
			}
			// Slice reinterpret (`byte[] as T[]` / `T[] as byte[]`) keeps the same backing
			// reference (base offset). The length is in elements, so it rescales by the ratio
			// of element offset counts: new_len = old_len * src_size / dst_size. One side is
			// always `byte` (1 offset), so exactly one of the two adjustments is non-trivial.
			if (src_kind == LS_TYPE_SLICE && dst_kind == LS_TYPE_SLICE) {
				ResolvedType* src_t = cast.expression->resolved_type;
				ResolvedType* dst_t = expr.resolved_type;
				ResolvedType* src_elem = static_cast<SliceResolvedType*>(src_t)->element_type;
				ResolvedType* dst_elem = static_cast<SliceResolvedType*>(dst_t)->element_type;
				const u32 src_size = typeByteSize(*src_elem);
				const u32 dst_size = typeByteSize(*dst_elem);
				if (src_size > dst_size && dst_size != 0u && src_size % dst_size == 0u) {
					emitConst8(ctx, src_size / dst_size);
					emitBinaryOp(ctx, LS_OP_MUL_I64, typeKindByteSize(LS_TYPE_I64));
				} else if (dst_size > src_size && src_size != 0u && dst_size % src_size == 0u) {
					emitConst8(ctx, dst_size / src_size);
					emitBinaryOp(ctx, LS_OP_DIV_I64, typeKindByteSize(LS_TYPE_I64));
				}
				return dst_kind;
			}
			if (src_kind == dst_kind) return dst_kind;
			emitCast(ctx, src_kind, dst_kind);
			return dst_kind;
		}
		case Expression::SIZEOF: {
			ls_type_kind kind = toTypeKind(*expr.resolved_type);
			if (!isNumericKind(kind)) kind = isIntegerKind(hint) ? hint : LS_TYPE_I32;
			const u64 v = static_cast<SizeofExpression&>(expr).value;
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
			UnaryExpression& un = static_cast<UnaryExpression&>(expr);
			if (un.resolved_fn) {
				FunctionExpression& fn = *un.resolved_fn;
				FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(fn.resolved_type);
				compileExpressionAsType(ctx, *un.expression, *fn_type->param_types[0]);
				emitCallDirect(ctx, fn.bytecode_index, callArgWindowSize(*fn_type), typeByteSize(*fn_type->return_type));
				return valueKindForType(*fn_type->return_type);
			}
			const ls_type_kind kind = compileExpression(ctx, *un.expression, hint);
			switch (un.op) {
				case Token::MINUS: {
					u32 index = numericKindIndex(kind);
					emitUnaryOp(ctx, (ls_op)((u32)LS_OP_NEG_I8 + index), typeKindByteSize(kind));
					return kind;
				}
				case Token::NOT:
					emitUnaryOp(ctx, LS_OP_NOT, 1u);
					return LS_TYPE_BOOL;
				default: ASSERT(false); return LS_TYPE_INVALID;
			}
		}
		case Expression::CALL: return compileCall(ctx, static_cast<CallExpression&>(expr), hint);
		case Expression::MEMBER:
			return compileMember(ctx, static_cast<MemberExpression&>(expr));
		case Expression::BRACKET: {
			BracketExpression& br = static_cast<BracketExpression&>(expr);
			if (br.resolved_type->kind == ResolvedType::FUNCTION) {
				FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(br.resolved_type);
				if (fn_type->decl) { // TODO when can this be null
					emitIntegerConstant(ctx, LS_TYPE_FUNCTION, fn_type->decl->bytecode_index);
					return LS_TYPE_FUNCTION;
				}
			}
			const u32 element_size = typeByteSize(*br.resolved_type);
			if (br.base->resolved_type->kind == ResolvedType::SLICE) {
				compileExpression(ctx, *br.base, LS_TYPE_SLICE);
				compileIndexExpression(ctx, *br.args[0]);
				emitSliceLoad(ctx, element_size);
			} else {
				if (!tryEmitReference(ctx, *br.base)) {
					compileExpression(ctx, *br.base, LS_TYPE_INVALID);
					emitLocalRef(ctx, ctx.temp_top - typeByteSize(*br.base->resolved_type));
				}
				compileIndexExpression(ctx, *br.args[0]);
				emitStaticBoundsCheck(ctx, *br.base->resolved_type);
				emitLoadAt(ctx, element_size, 0, element_size);
			}
			return valueKindForType(*br.resolved_type);
		}
		case Expression::SLICE: {
			emitSlice(ctx, static_cast<SliceExpression&>(expr));
			return LS_TYPE_SLICE;
		}
		case Expression::STRUCT_LITERAL: {
			StructLiteralExpression& lit = static_cast<StructLiteralExpression&>(expr);
			ResolvedType* type = lit.type ? lit.type->resolved_type : expr.resolved_type;
			if (type->kind == ResolvedType::STRUCT) {
				StructResolvedType* st = static_cast<StructResolvedType*>(type);
				for (i32 i = 0; i < lit.values.size(); ++i) {
					ResolvedType* field_type = i < st->field_types.size()
						? st->field_types[i]
						: st->decl->fields[i].resolved_type;
					compileExpressionAsType(ctx, *lit.values[i], *field_type);
				}
			}
			else {
				for (Expression* value : lit.values) {
					compileExpression(ctx, *value, LS_TYPE_INVALID);
				}
			}
			return hint;
		}
		default: ASSERT(false); return LS_TYPE_INVALID;
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

static void compileAssign(FunctionCompiler& ctx, AssignStatement& assign) {
	if (assign.lhs->kind == Expression::IDENTIFIER) {
		IdentifierExpression* id = static_cast<IdentifierExpression*>(assign.lhs);
		StorageSlot* slot = id->slot;

		ResolvedType* value_type = slot ? slot->type : id->symbol->resolved_type;
		if (value_type && value_type->kind == ResolvedType::META) value_type = static_cast<MetaType*>(value_type)->inner;
		const ls_type_kind value_kind = slot ? slot->kind : valueKindForType(*value_type);

		if (slot && slot->storage == StorageSlot::LOCAL_REF) {
			const u32 ref_size = typeKindByteSize(LS_TYPE_CPTR);
			const u32 value_size = typeByteSize(*value_type);
			if (assign.op == Token::EQUAL) {
				emitLoadLocalBytes(ctx, slot->offset, ref_size);
				emitConst8(ctx, 0u);
				compileExpressionAsType(ctx, *assign.rhs, *value_type);
				emitStoreAt(ctx, 1u, 0, value_size);
				return;
			}
			if (assign.resolved_op_fn) {
				const FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(assign.resolved_op_fn->resolved_type);
				emitLoadLocalBytes(ctx, slot->offset, ref_size);
				emitConst8(ctx, 0u);
				emitLoadLocalBytes(ctx, slot->offset, ref_size);
				emitConst8(ctx, 0u);
				emitLoadAt(ctx, 1u, 0, value_size);
				compileExpressionAsType(ctx, *assign.rhs, *fn_type->param_types[1]);
				emitCallDirect(ctx, assign.resolved_op_fn->bytecode_index, callArgWindowSize(*fn_type), typeByteSize(*fn_type->return_type));
				emitStoreAt(ctx, 1u, 0, value_size);
				return;
			}
			emitLoadLocalBytes(ctx, slot->offset, ref_size);
			emitConst8(ctx, 0u);
			emitLoadLocalBytes(ctx, slot->offset, ref_size);
			emitConst8(ctx, 0u);
			emitLoadAt(ctx, 1u, 0, value_size);
			compileExpression(ctx, *assign.rhs, value_kind);
			emitNumericStoreOp(ctx, value_kind, assign.op);
			emitStoreAt(ctx, 1u, 0, value_size);
			return;
		}

		if (assign.op == Token::EQUAL) {
			compileExpressionAsType(ctx, *assign.rhs, *value_type);
			emitStoreSlot(ctx, *slot);
			return;
		}
	
		emitLoadSlot(ctx, *slot);

		if (assign.resolved_op_fn) {
			const FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(assign.resolved_op_fn->resolved_type);
			compileExpressionAsType(ctx, *assign.rhs, *fn_type->param_types[1]);
			emitCallDirect(ctx, assign.resolved_op_fn->bytecode_index, callArgWindowSize(*fn_type), typeByteSize(*fn_type->return_type));
		}
		else {
			compileExpression(ctx, *assign.rhs, value_kind);
			emitNumericStoreOp(ctx, value_kind, assign.op);
		}
		emitStoreSlot(ctx, *slot);
		return;
	}
	if (assign.lhs->kind == Expression::BRACKET) {
		BracketExpression* br = static_cast<BracketExpression*>(assign.lhs);
		const ls_type_kind value_kind = valueKindForType(*assign.lhs->resolved_type);
		emitBracketStore(ctx, *br, *assign.rhs, value_kind, assign.op, assign.resolved_op_fn);
		return;
	}
	if (assign.lhs->kind == Expression::MEMBER) {
		MemberExpression* member = static_cast<MemberExpression*>(assign.lhs);
		StructResolvedType* st = static_cast<StructResolvedType*>(member->expression->resolved_type);
		ResolvedType* field_type = nullptr;
		u32 field_offset = structFieldByteOffset(*st, member->name, field_type);
		const u32 field_size = typeByteSize(*field_type);
		const ls_type_kind value_kind = valueKindForType(*field_type);
		emitAddressableReference(ctx, *member->expression);
		
		if (assign.op == Token::EQUAL) {
			emitConst8(ctx, 0u);
			compileExpressionAsType(ctx, *assign.rhs, *field_type);
			emitStoreAt(ctx, field_size, (i32)field_offset, field_size);
			return;
		}
		// Compound assignment: load existing value, apply op, store back.
		// Stack order for STORE_AT is: base, index, value.
		const u32 ref_size = typeKindByteSize(LS_TYPE_CPTR);
		const u32 ref_offset = ctx.addLocal(nullptr, LS_TYPE_I64, true);
		emitStoreLocalBytes(ctx, ref_offset, ref_size);
		const u32 val_offset = ctx.addLocal(field_type, value_kind);
		emitLoadLocalBytes(ctx, ref_offset, ref_size);
		emitConst8(ctx, 0u);
		emitLoadAt(ctx, 1u, (i32)field_offset, field_size);
		emitCompoundValue(ctx, *assign.rhs, value_kind, assign.op, assign.resolved_op_fn);
		emitStoreLocalBytes(ctx, val_offset, field_size);
		emitLoadLocalBytes(ctx, ref_offset, ref_size);
		emitConst8(ctx, 0u);
		emitLoadLocalBytes(ctx, val_offset, field_size);
		emitStoreAt(ctx, 1u, (i32)field_offset, field_size);
		return;
	}
	ASSERT(false);
}

static void compileStatement(FunctionCompiler& ctx, Statement& st, ls_type_kind return_kind, ls_string_view current_label) {
	switch (st.kind) {
		case Statement::VAR_DECL: {
			VarDeclStatement& var = static_cast<VarDeclStatement&>(st);
			ResolvedType* value_type = var.resolved_type;
			const ls_type_kind kind = valueKindForType(*value_type);
			const u32 offset = ctx.addLocal(value_type, kind, false, &var.slot);
			const u32 byte_size = var.slot.byte_size;
			if (emitLocalLiteralInitializer(ctx, offset, *var.expression, kind)) return;
			if (var.expression->kind == Expression::UNDEFINED) return;

			compileExpressionAsType(ctx, *var.expression, *value_type);
			emitStoreLocalBytes(ctx, offset, byte_size);
			return;
		}
		case Statement::ASSIGN: {
			AssignStatement& assign = static_cast<AssignStatement&>(st);
			compileAssign(ctx, assign);
			return;
		}
		case Statement::IF: {
			IfStatement& ifst = static_cast<IfStatement&>(st);
			compileExpression(ctx, *ifst.condition, LS_TYPE_BOOL);
			const u32 jump_false_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_FALSE);
			compileStatement(ctx, *ifst.body, return_kind, current_label);
			if (ifst.else_branch) {
				const u32 jump_end_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP);
				patchJumpRelative(ctx.code, jump_false_pos, (u32)ctx.code.size());
				compileStatement(ctx, *ifst.else_branch, return_kind, current_label);
				patchJumpRelative(ctx.code, jump_end_pos, (u32)ctx.code.size());
			}
			else {
				patchJumpRelative(ctx.code, jump_false_pos, (u32)ctx.code.size());
			}
			return;
		}
		case Statement::WHILE: {
			WhileStatement& ws = static_cast<WhileStatement&>(st);

			const u32 condition_pos = (u32)ctx.code.size();
			compileExpression(ctx, *ws.condition, LS_TYPE_BOOL);

			const u32 jump_false_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_FALSE);

			LoopBinding& loop = ctx.loops.emplace_back();
			loop.label = current_label;
			loop.defer_mark = (u32)ctx.deferreds.size();
			void* break_storage = ctx.bytecode->arena->allocate(ctx.bytecode->arena->user_data, sizeof(ExpArray<u32>), alignof(ExpArray<u32>));
			loop.break_jumps = ::new (break_storage) ExpArray<u32>(*ctx.bytecode->arena);
			void* continue_storage = ctx.bytecode->arena->allocate(ctx.bytecode->arena->user_data, sizeof(ExpArray<u32>), alignof(ExpArray<u32>));
			loop.continue_jumps = ::new (continue_storage) ExpArray<u32>(*ctx.bytecode->arena);

			compileStatement(ctx, *ws.body, return_kind, {});

			const u32 jump_back_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP);
			patchJumpRelative(ctx.code, jump_back_pos, condition_pos);

			const u32 loop_end = (u32)ctx.code.size();
			patchJumpRelative(ctx.code, jump_false_pos, loop_end);
			for (u32 break_pos : *loop.break_jumps) {
				patchJumpRelative(ctx.code, break_pos, loop_end);
			}
			for (u32 continue_pos : *loop.continue_jumps) {
				patchJumpRelative(ctx.code, continue_pos, condition_pos);
			}
			ctx.loops.pop_back();
			return;
		}
		case Statement::FOR: {
			ForStatement& fs = static_cast<ForStatement&>(st);
			ResolvedType* value_type = fs.begin->resolved_type;
			const ls_type_kind value_kind = valueKindForType(*value_type);
			const u32 byte_size = typeByteSize(*value_type) == 0u ? 1u : typeByteSize(*value_type);
			compileExpression(ctx, *fs.begin, value_kind);
			compileExpression(ctx, *fs.end, value_kind);
			const u32 range_value_top = ctx.temp_top;

			ctx.pushScope();
			const u32 loop_offset = ctx.addLocal(value_type, value_kind, true, &fs.slot);
			const u32 end_offset = ctx.addLocal(value_type, value_kind, true);
			ctx.temp_top = range_value_top;
			emitStoreLocalBytes(ctx, end_offset, byte_size, false);
			emitStoreLocalBytes(ctx, loop_offset, byte_size, false);
			ctx.temp_top = ctx.next_local_offset;

			const u32 condition_pos = (u32)ctx.code.size();
			emitLoadLocalBytes(ctx, loop_offset, byte_size);
			emitLoadLocalBytes(ctx, end_offset, byte_size);
			emitCompareOp(ctx, LS_OP_LT, value_kind);
			const u32 jump_false_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_FALSE);

			LoopBinding& loop = ctx.loops.emplace_back();
			loop.label = current_label;
			loop.defer_mark = (u32)ctx.deferreds.size();
			void* break_storage = ctx.bytecode->arena->allocate(ctx.bytecode->arena->user_data, sizeof(ExpArray<u32>), alignof(ExpArray<u32>));
			loop.break_jumps = ::new (break_storage) ExpArray<u32>(*ctx.bytecode->arena);
			void* continue_storage = ctx.bytecode->arena->allocate(ctx.bytecode->arena->user_data, sizeof(ExpArray<u32>), alignof(ExpArray<u32>));
			loop.continue_jumps = ::new (continue_storage) ExpArray<u32>(*ctx.bytecode->arena);

			compileStatement(ctx, *fs.body, return_kind, {});

			const u32 increment_pos = (u32)ctx.code.size();
			emitLoadLocalBytes(ctx, loop_offset, byte_size);
			emitIntegerConstant(ctx, value_kind, 1u);
			emitNumericBinary(ctx, LS_OP_ADD_I8, value_kind);
			emitStoreLocalBytes(ctx, loop_offset, byte_size);

			const u32 jump_back_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP);
			patchJumpRelative(ctx.code, jump_back_pos, condition_pos);

			const u32 loop_end = (u32)ctx.code.size();
			patchJumpRelative(ctx.code, jump_false_pos, loop_end);
			for (u32 break_pos : *loop.break_jumps) {
				patchJumpRelative(ctx.code, break_pos, loop_end);
			}
			for (u32 continue_pos : *loop.continue_jumps) {
				patchJumpRelative(ctx.code, continue_pos, increment_pos);
			}
			ctx.loops.pop_back();
			ctx.popScope(return_kind, current_label);
			return;
		}
		case Statement::MATCH: {
			MatchStatement& ms = static_cast<MatchStatement&>(st);
			ResolvedType* subject_type = ms.subject->resolved_type;
			const ls_type_kind subject_kind = valueKindForType(*subject_type);
			const u32 subject_byte_size = typeByteSize(*subject_type) == 0u ? 1u : typeByteSize(*subject_type);
			compileExpression(ctx, *ms.subject, subject_kind);
			const u32 subject_offset = ctx.addLocal(subject_type, subject_kind, true);
			emitStoreLocalBytes(ctx, subject_offset, subject_byte_size);
			if (subject_type->kind == ResolvedType::UNION) {
				UnionResolvedType& subject_union = static_cast<UnionResolvedType&>(*subject_type);
				ExpArray<u32> match_end_jumps(*ctx.bytecode->arena);
				for (MatchArm& arm : ms.arms) {
					if (arm.is_fallback) {
						compileStatement(ctx, *arm.body, return_kind, current_label);
						match_end_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
						continue;
					}

					ExpArray<u32> arm_body_jumps(*ctx.bytecode->arena);
					for (MatchPattern& pattern : arm.patterns) {
						ResolvedType* member = static_cast<MetaType*>(pattern.begin->resolved_type)->inner;
						i32 member_index = -1;
						for (i32 i = 0; i < subject_union.members.size(); ++i) {
							if (subject_union.members[i] == member) { member_index = i; break; }
						}
						ASSERT(member_index >= 0);
						emitLoadLocalBytes(ctx, subject_offset, 4);
						emitIntegerConstant(ctx, LS_TYPE_I32, member_index);
						emitCompareOp(ctx, LS_OP_EQ, LS_TYPE_I32);
						arm_body_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_TRUE));
					}

					const u32 skip_jump = emitJumpPlaceholder(ctx, LS_OP_JUMP);
					const u32 body_start = (u32)ctx.code.size();
					for (u32 jump : arm_body_jumps) patchJumpRelative(ctx.code, jump, body_start);
					compileStatement(ctx, *arm.body, return_kind, current_label);
					match_end_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
					patchJumpRelative(ctx.code, skip_jump, (u32)ctx.code.size());
				}
				const u32 end = (u32)ctx.code.size();
				for (u32 jump : match_end_jumps) patchJumpRelative(ctx.code, jump, end);
				return;
			}

			ExpArray<u32> match_end_jumps(*ctx.bytecode->arena);
			ExpArray<u32> pending_false_jumps(*ctx.bytecode->arena);
			ExpArray<u32> arm_body_jumps(*ctx.bytecode->arena);

			for (MatchArm& arm : ms.arms) {
				arm_body_jumps.clear();

				if (arm.is_fallback) {
					for (u32 false_jump : pending_false_jumps) patchJumpRelative(ctx.code, false_jump, (u32)ctx.code.size());
					pending_false_jumps.clear();
					compileStatement(ctx, *arm.body, return_kind, current_label);
					match_end_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
					continue;
				}

				for (MatchPattern& pattern : arm.patterns) {
					for (u32 false_jump : pending_false_jumps) patchJumpRelative(ctx.code, false_jump, (u32)ctx.code.size());
					pending_false_jumps.clear();

					if (pattern.end) {
						emitLoadLocalBytes(ctx, subject_offset, subject_byte_size);
						compileExpression(ctx, *pattern.begin, subject_kind);
						emitCompareOp(ctx, LS_OP_GE, subject_kind);
						pending_false_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_FALSE));

						emitLoadLocalBytes(ctx, subject_offset, subject_byte_size);
						compileExpression(ctx, *pattern.end, subject_kind);
						emitCompareOp(ctx, LS_OP_LE, subject_kind);
						pending_false_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_FALSE));
						arm_body_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
					}
					else {
						emitLoadLocalBytes(ctx, subject_offset, subject_byte_size);
						compileExpression(ctx, *pattern.begin, subject_kind);
						emitCompareOp(ctx, LS_OP_EQ, subject_kind);
						arm_body_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP_IF_TRUE));
					}
				}

				const u32 skip_jump_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP);
				const u32 body_start = (u32)ctx.code.size();
				for (u32 jump_pos : arm_body_jumps) patchJumpRelative(ctx.code, jump_pos, body_start);
				for (u32 false_jump : pending_false_jumps) patchJumpRelative(ctx.code, false_jump, skip_jump_pos - 1u);
				pending_false_jumps.clear();
				compileStatement(ctx, *arm.body, return_kind, current_label);
				match_end_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
				const u32 arm_end_pos = (u32)ctx.code.size();
				patchJumpRelative(ctx.code, skip_jump_pos, arm_end_pos);
			}

			const u32 end_pos = (u32)ctx.code.size();
			for (u32 jump_pos : match_end_jumps) patchJumpRelative(ctx.code, jump_pos, end_pos);
			return;
		}
		case Statement::DEFER: {
			DeferStatement& df = static_cast<DeferStatement&>(st);
			ctx.deferreds.push(df.statement);
			return;
		}
		case Statement::BREAK:
		case Statement::CONTINUE: {
			const bool is_break = st.kind == Statement::BREAK;
			const ls_string_view label = is_break ? static_cast<BreakStatement&>(st).label : static_cast<ContinueStatement&>(st).label;
			LoopBinding* loop = ctx.findLoop(label);
			emitDeferredStatements(ctx, loop->defer_mark, return_kind, current_label);
			if (is_break) {
				loop->break_jumps->push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
				return;
			}
			loop->continue_jumps->push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
			return;
		}
		case Statement::LABEL: {
			LabelStatement& label = static_cast<LabelStatement&>(st);
			const ls_string_view next_label = label.statement->kind == Statement::WHILE || label.statement->kind == Statement::FOR
				? label.name
				: current_label;
			compileStatement(ctx, *label.statement, return_kind, next_label);
			return;
		}
		case Statement::RETURN: {
			ReturnStatement& ret = static_cast<ReturnStatement&>(st);
			if (return_kind == LS_TYPE_NULL_VALUE) {
				const bool is_null = !ret.expression || ret.expression->kind == Expression::NULL_LITERAL || ret.expression->kind == Expression::UNDEFINED;
				NullableResolvedType* nullable_type = ctx.return_type && ctx.return_type->kind == ResolvedType::NULLABLE ? static_cast<NullableResolvedType*>(ctx.return_type) : nullptr;
				ResolvedType* inner_type = nullable_type ? nullable_type->inner : nullptr;
				emitIntegerConstant(ctx, LS_TYPE_BOOL, is_null ? 0u : 1u);
				if (!is_null && ret.expression) {
					compileExpressionAsType(ctx, *ret.expression, *inner_type);
				} else {
					emitZeroBytes(ctx, typeByteSize(*inner_type));
				}
				emitDeferredStatements(ctx, 0u, return_kind, current_label);
				emitReturn(ctx);
				return;
			}
			if (ret.expression) {
				compileExpressionAsType(ctx, *ret.expression, *ctx.return_type);
			}
			emitDeferredStatements(ctx, 0u, return_kind, current_label);
			emitReturn(ctx);
			return;
		}
		case Statement::EXPRESSION: {
			ExpressionStatement& expr = static_cast<ExpressionStatement&>(st);
			compileExpression(ctx, *expr.expression, LS_TYPE_INVALID);
			const u32 byte_size = typeByteSize(*expr.expression->resolved_type);
			if (byte_size > 0u) {
				emitPop(ctx, byte_size);
			}
			return;
		}
		case Statement::BLOCK: {
			BlockStatement& block = static_cast<BlockStatement&>(st);
			ctx.pushScope();
			for (Statement* child : block.statements) {
				compileStatement(ctx, *child, return_kind, current_label);
			}
			ctx.popScope(return_kind, current_label);
			return;
		}
	}
	ASSERT(false);
}

static bool isSimpleReturnLiteral(BlockStatement& body, Expression*& out_expr) {
	if (body.statements.size() != 1) return false;
	Statement* st = body.statements[0];
	if (st->kind != Statement::RETURN) return false;
	ReturnStatement* ret = static_cast<ReturnStatement*>(st);
	if (!ret->expression) return false;
	switch (ret->expression->kind) {
		case Expression::INT_LITERAL:
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
		const u32 byte_size = param.is_ref ? typeKindByteSize(LS_TYPE_CPTR) : typeByteSize(*param.resolved_type);
		ASSERT(byte_size > 0);
		count += byte_size;
	}
	return count;
}

static bool compileFunctionBytecode(
	ls_bytecode* bytecode,
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
	function.param_size = 0;
	ResolvedType* return_type = fn_type->return_type;
	function.return_kind = toTypeKind(*return_type);
	// Calls move raw offsets, so aggregate return metadata must describe the
	// representation width rather than assuming every value is one offset.
	function.return_size = typeByteSize(*return_type);
	function.frame_size = 0;

	if (fn->is_extern) {
		function.param_size = computeParamSize(fn->params);
		return true;
	}
	if (!fn->body || fn->body->kind != Statement::BLOCK) return false;

	BlockStatement* body = static_cast<BlockStatement*>(fn->body);
	Expression* literal = nullptr;
	if (function.return_size == 1u && isSimpleReturnLiteral(*body, literal)) {
		// Tiny literal function: emit the constant and return without setting up
		// parameter locals (the literal references none).
		function.param_size = computeParamSize(fn->params);
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
		finalizeFunctionCode(ctx, function, *arena);
		return true;
	}

	FunctionCompiler ctx(bytecode, function);
	ctx.return_type = return_type;
	for (FunctionParam& param : fn->params) {
		if (param.is_comptime) continue;
		StorageSlot& slot = param.slot;
		slot.type = param.resolved_type;
		slot.kind = valueKindForType(*param.resolved_type);
		slot.byte_size = param.is_ref ? typeKindByteSize(LS_TYPE_CPTR) : typeByteSize(*param.resolved_type);
		if (slot.byte_size == 0u) slot.byte_size = 1u;
		slot.offset = function.param_size;
		slot.storage = param.is_ref ? StorageSlot::LOCAL_REF : StorageSlot::LOCAL;
		function.param_size += slot.byte_size;
		ctx.next_local_offset = function.param_size;
	}
	ctx.temp_top = function.param_size;
	ctx.frame_high_water = function.param_size;
	for (Statement* st : body->statements) {
		compileStatement(ctx, *st, function.return_kind, {});
	}
	// Every code path in a non-void function is guaranteed by checkFunctionBody
	// to already end in an explicit `return`, so this is unreachable for those.
	// For void functions that fall off the end without one, this is the only
	// return emitted; it keeps the bytecode self-terminating so the runtime's
	// interpreter loop never needs to fall through past the end of fn->code.
	emitReturn(ctx);
	finalizeFunctionCode(ctx, function, *arena);
	return true;
}

ls_bytecode* ls_bytecode_compile(
	ls_module* module,
	ls_host* host
) {
	if (!module || !host || !host->arena.allocate) return nullptr;

	ls_bytecode* bytecode = static_cast<ls_bytecode*>(std::calloc(1, sizeof(ls_bytecode)));
	if (!bytecode) return nullptr;
	bytecode->host = host;
	bytecode->arena = &host->arena;

	ASSERT(bytecode->arena);
	ls_arena* arena = bytecode->arena;

	bytecode->global_size = 0u;
	for (Unit& unit : module->units) {
		for (Symbol& sym : unit.symbols) {
			if (!symbolHasGlobalStorage(sym)) continue;
			sym.slot.storage = StorageSlot::GLOBAL;
			sym.slot.offset = bytecode->global_size;
			sym.slot.byte_size = typeByteSize(*sym.resolved_type);
			if (sym.slot.byte_size == 0u) sym.slot.byte_size = 1u;
			sym.slot.type = sym.resolved_type;
			sym.slot.kind = valueKindForType(*sym.resolved_type);
			bytecode->global_size += sym.slot.byte_size;
		}
	}

	u32 function_count = 0;
	for (Unit& unit : module->units) {
		for (Symbol& sym : unit.symbols) {
			if (!sym.expression || sym.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* fn = static_cast<FunctionExpression*>(sym.expression);
			if (fn->is_template) continue;
			fn->bytecode_index = function_count++;
		}
	}
	for (Unit& unit : module->units) {
		for (Symbol& sym : unit.symbols) {
			if (!sym.expression || sym.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* fn = static_cast<FunctionExpression*>(sym.expression);
			if (fn->is_template) continue;
			if (!compileFunctionBytecode(
				bytecode,
				fn,
				static_cast<FunctionResolvedType*>(fn->resolved_type),
				sym.name,
				fn->is_extern && (equalStrings(unit.path, makeStringView("std:math")) || equalStrings(unit.path, makeStringView("std:mem")))
			)) {
				ls_bytecode_destroy(bytecode);
				return nullptr;
			}
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
		function.param_size = 0;
		function.return_kind = LS_TYPE_VOID;
		function.return_size = 0;
		function.frame_size = 0u;

		FunctionCompiler ctx(bytecode, function);
		for (Unit& unit : module->units) {
			for (Symbol& sym : unit.symbols) {
				if (sym.slot.storage != StorageSlot::GLOBAL) continue;
				if (sym.expression->kind == Expression::UNDEFINED) continue;
				compileExpressionAsType(ctx, *sym.expression, *sym.resolved_type);
				emitStoreGlobalBytes(ctx, sym.slot.offset, sym.slot.byte_size);
			}
		}
		emitReturn(ctx);
		finalizeFunctionCode(ctx, function, *arena);
		bytecode->has_global_init = true;
	}

	return bytecode;
}

void ls_bytecode_destroy(ls_bytecode* bytecode) {
	if (!bytecode) return;
	std::free(bytecode);
}

