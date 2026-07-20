#include "bytecode.h"
#include "compiler.h"
#include "exparray.h"
#include "utils.h"

#include <cstdlib>
#include <cstring>

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
		case ResolvedType::STRUCT: return LS_TYPE_STRUCT;
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
		case LS_TYPE_BOOL: return true;
		default: return false;
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
		case LS_TYPE_U8: return 1u;
		case LS_TYPE_I16:
		case LS_TYPE_U16: return 2u;
		case LS_TYPE_I32:
		case LS_TYPE_U32:
		case LS_TYPE_F32:
		case LS_TYPE_ENUM:
		case LS_TYPE_FUNCTION: return 4u;
		case LS_TYPE_I64:
		case LS_TYPE_U64:
		case LS_TYPE_F64:
		case LS_TYPE_STRING:
		case LS_TYPE_CPTR: return 8u;
		case LS_TYPE_SLICE: return 16u;
		default: return 8u;
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
	explicit ByteArray(ls_arena& arena)
		: arena(arena)
		, source_map(arena) {}

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
	u8& operator[](u32 index) {
		ASSERT(index < count);
		return data[index];
	}

	ls_arena& arena;
	u8* data = nullptr;
	u32 count = 0;
	u32 capacity = 0;
	ExpArray<ls_bytecode_source_map_entry> source_map;
	Token current_source = {};
	bool has_current_source = false;
};

static void recordSourceMap(ByteArray& code) {
	if (!code.has_current_source || code.current_source.type == Token::END_OF_FILE || code.current_source.line <= 0) return;
	if (!code.source_map.empty() && code.source_map.back().code_offset == code.count) {
		ls_bytecode_source_map_entry& entry = code.source_map.back();
		entry.source_name = code.current_source.source_name;
		entry.line = (u32)code.current_source.line;
		entry.column = (u32)code.current_source.column;
		return;
	}
	ls_bytecode_source_map_entry entry;
	entry.code_offset = code.count;
	entry.source_name = code.current_source.source_name;
	entry.line = (u32)code.current_source.line;
	entry.column = (u32)code.current_source.column;
	code.source_map.push_back(entry);
}

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

static void emitI16(ByteArray& code, i32 value) {
	ASSERT(value >= -32768 && value <= 32767);
	const i16 narrow = (i16)value;
	emitBytes(code, &narrow, sizeof(narrow));
}

static void emitU64(ByteArray& code, u64 value) {
	emitBytes(code, &value, sizeof(value));
}

static void emitOp(ByteArray& code, ls_op op) {
	recordSourceMap(code);
	emitU8(code, (u8)op);
}

// Nested expression/statement compilation temporarily owns the source
// location used by subsequent opcode emissions, then restores its caller.
struct SourceScope {
	ByteArray& code;
	Token previous_source;
	bool previous_valid;

	SourceScope(ByteArray& code, Token source)
		: code(code)
		, previous_source(code.current_source)
		, previous_valid(code.has_current_source) {
		code.current_source = source;
		code.has_current_source = source.type != Token::END_OF_FILE && source.line > 0;
	}

	~SourceScope() {
		code.current_source = previous_source;
		code.has_current_source = previous_valid;
	}
};

template <typename T> static T* appendArenaArray(ls_arena& arena, T*& data, u32& count, u32& capacity) {
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

// During bytecode compilation, walks every type referenced by a local,
// parameter, global, or return and builds the ls_type[] /
// ls_type_field_info[] arrays in the bytecode. Uses ResolvedType*
// pointer identity for deduplication so each unique type appears once.

static ls_string_view copyStringViewToArena(ls_arena& arena, ls_string_view src) {
	if (empty(src)) return src;
	const usize len = size(src);
	char* mem = (char*)arena.allocate(arena.user_data, len, 1);
	if (!mem) return src;
	memcpy(mem, src.begin, len);
	return { mem, mem + len };
}

struct TypeInfoBuilder {
	ls_bytecode* bc;
	ExpArray<void*> type_map;  // keys (ResolvedType*), parallel to bc->type_info[]

	TypeInfoBuilder(ls_arena& arena, ls_bytecode* bc)
		: bc(bc), type_map(arena) {}

	u32 resolve(ResolvedType* type) {
		if (!type) return LS_TYPE_INDEX_NONE;

		for (u32 i = 0; i < bc->type_info_count; ++i) {
			if (type_map[(i32)i] == type) return i;
		}

		const u32 old_count = bc->type_info_count;
		if (!appendArenaArray(*bc->arena, bc->type_info, bc->type_info_count, bc->type_info_capacity))
			return LS_TYPE_INDEX_NONE;
		type_map.push_back((void*)type);
		const u32 index = old_count;

		ls_type* info = &bc->type_info[index];
		info->bytecode = bc;
		info->kind = LS_TYPE_INVALID;
		info->byte_size = typeByteSize(*type);
		info->field_count = 0u;
		info->first_field_index = 0u;
		info->element_type_index = LS_TYPE_INDEX_NONE;
		info->array_length = LS_TYPE_INDEX_NONE;

		switch (type->kind) {
			case ResolvedType::STRUCT: {
				StructResolvedType* st = static_cast<StructResolvedType*>(type);
				info->kind = LS_TYPE_STRUCT;
				if (!st->decl) break;
				info->field_count = (u32)st->decl->fields.size();
				// First pass: resolve all field types (may recursively append
				// type_fields for nested structs, which would shift first_field_index).
				ExpArray<u32> ft_indices(*bc->arena);
				ExpArray<ResolvedType*> ft_types(*bc->arena);
				for (i32 i = 0; i < (i32)info->field_count; ++i) {
					NamedDecl& field = st->decl->fields[i];
					ResolvedType* ft = i < st->field_types.size() ? st->field_types[i] : field.resolved_type;
					ft_indices.push_back(ft ? resolve(ft) : LS_TYPE_INDEX_NONE);
					ft_types.push_back(ft);
				}
				// Now safe to record start index after all recursive appends.
				info->first_field_index = bc->type_field_count;
				u32 running_offset = 0u;
				for (i32 i = 0; i < (i32)info->field_count; ++i) {
					NamedDecl& field = st->decl->fields[i];
					ls_type_field_info fi;
					fi.name = copyStringViewToArena(*bc->arena, field.name);
					fi.type_index = ft_indices[i];
					fi.offset = running_offset;
					ls_type_field_info* dst = appendArenaArray(*bc->arena, bc->type_fields, bc->type_field_count, bc->type_field_capacity);
					if (dst) *dst = fi;
					ResolvedType* ft = ft_types[i];
					if (ft) running_offset += typeByteSize(*ft);
				}
				break;
			}
			case ResolvedType::ARRAY: {
				ArrayResolvedType* arr = static_cast<ArrayResolvedType*>(type);
				info->kind = LS_TYPE_ARRAY;
				info->element_type_index = arr->element_type ? resolve(arr->element_type) : LS_TYPE_INDEX_NONE;
				info->array_length = arr->size > 0 ? (u32)arr->size : 0u;
				break;
			}
			case ResolvedType::SLICE: {
				SliceResolvedType* sl = static_cast<SliceResolvedType*>(type);
				info->kind = LS_TYPE_SLICE;
				info->element_type_index = sl->element_type ? resolve(sl->element_type) : LS_TYPE_INDEX_NONE;
				info->array_length = 0u;
				break;
			}
			case ResolvedType::NULLABLE: {
				NullableResolvedType* nl = static_cast<NullableResolvedType*>(type);
				info->kind = LS_TYPE_NULLABLE;
				info->element_type_index = nl->inner ? resolve(nl->inner) : LS_TYPE_INDEX_NONE;
				break;
			}
			default:
				info->kind = toTypeKind(*type);
				break;
		}
		return index;
	}
};

static void compileStatement(FunctionCompiler& ctx, Statement& st, ls_type_kind return_kind, ls_string_view current_label);
static void emitDeferredStatements(FunctionCompiler& ctx, u32 defer_mark, ls_type_kind return_kind, ls_string_view current_label);
static void compileExpressionAsType(FunctionCompiler& ctx, Expression& expr, ResolvedType& expected_type);
static ls_type_kind compileExpression(FunctionCompiler& ctx, Expression& expr, ls_type_kind hint);
static bool tryEmitReference(FunctionCompiler& ctx, Expression& expr);
static void emitCallDirect(FunctionCompiler& ctx, u32 callee_index, u32 arg_size, u32 return_size);
static ls_type_kind compileMember(FunctionCompiler& ctx, MemberExpression& expr);
static bool patchJumpRelative(FunctionCompiler& ctx, u32 operand_pos, u32 target_pos);
static u32 emitJumpPlaceholder(FunctionCompiler& ctx, ls_op op);

struct FunctionCompiler {
	explicit FunctionCompiler(ls_bytecode* bytecode, ls_function_bc& out)
		: bytecode(bytecode)
		, out(out)
		, code(*bytecode->arena)
		, deferreds(*bytecode->arena)
		, defer_marks(*bytecode->arena)
		, loops(*bytecode->arena)
		, locals_debug(*bytecode->arena) {
		diagnostics.host = bytecode->host;
	}

	ls_bytecode* bytecode = nullptr;
	TypeInfoBuilder* type_builder = nullptr;
	ResolvedType* return_type = nullptr;
	ls_function_bc& out;
	ByteArray code;
	ExpArray<Statement*> deferreds;
	ExpArray<i32> defer_marks;
	ExpArray<LoopBinding> loops;
	ExpArray<ls_bytecode_local_debug_entry> locals_debug;
	OutputFormatter diagnostics;
	bool failed = false;
	// Locals and temporaries share one absolute frame offset space. `next_local_offset`
	// is the floor below which temporaries must not be rewound, while `temp_top`
	// is the next free byte for expression temporaries.
	u32 temp_top = 0;
	u32 frame_high_water = 0;
	u32 next_local_offset = 0;

	void reportError(const char* message) {
		if (failed) return;
		failed = true;
		diagnostics.error("Function ", out.name, ": ", message);
	}

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

	// Records a named parameter/local for `ls_debug_frame_local_*`. Call
	// after the slot's fields (offset/byte_size/kind) are filled - by
	// `addLocal` for a `var`/loop variable, or directly for a function
	// parameter (see the param loop in compileFunctionBytecode). Scope begins
	// at the current bytecode offset; it's treated as open until the end of
	// the function (see ls_bytecode_local_debug_entry in bytecode.h) since
	// there's no block-scope-exit tracking to give a tighter bound.
	void debugLocal(ls_string_view name, const StorageSlot& slot) {
		ls_bytecode_local_debug_entry entry;
		entry.name = name;
		entry.offset = slot.offset;
		entry.byte_size = slot.byte_size;
		entry.kind = slot.kind;
		entry.type_index = type_builder ? type_builder->resolve(slot.type) : LS_TYPE_INDEX_NONE;
		entry.scope_begin_offset = (u32)code.size();
		locals_debug.push_back(entry);
	}

	void pushScope() { defer_marks.push(deferreds.size()); }

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
		} else {
			const ls_type_kind receiver_kind = !fn_type.param_types.empty() ? valueKindForType(*fn_type.param_types[0]) : LS_TYPE_INVALID;
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
	*entry = copyStringViewToArena(*bytecode.arena, value);
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
	while (byte_size >= 8u) {
		emitConst8(ctx, 0u);
		byte_size -= 8u;
	}
	if (byte_size >= 4u) {
		emitConst4(ctx, 0u);
		byte_size -= 4u;
	}
	if (byte_size >= 2u) {
		emitConst2(ctx, 0u);
		byte_size -= 2u;
	}
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

// ---------------------------------------------------------------------------
// Value descriptors
//
// compileValue() reports where an expression's result lives instead of always
// materializing it at the temp top. Constants and frame-resident locals stay
// deferred (no code emitted), so consumers that read operands in place - 
// arithmetic, comparisons, stores, indexed access - encode the frame offsets
// directly and the copy-through-temp traffic disappears. Everything else
// compiles onto the temp stack exactly like compileExpression and is reported
// as a temp register.
struct Value {
	enum Kind : u8 {
		INVALID,
		CONST_INT,	 // deferred integer/bool constant; raw payload in `bits`
		CONST_FLOAT, // deferred float constant; value in `fval`
		REG,		 // live at frame byte offset `reg`
	};
	Kind kind = INVALID;
	// Value kind the expression evaluates to. Deferred constants may be
	// re-materialized at a different numeric kind chosen by the consumer.
	ls_type_kind type = LS_TYPE_INVALID;
	u32 reg = 0;
	bool is_temp = false; // REG only: occupies the temp stack
	u64 bits = 0;
	double fval = 0.0;
};

static Value makeRegValue(u32 reg, ls_type_kind type, bool is_temp) {
	Value v;
	v.kind = Value::REG;
	v.type = type;
	v.reg = reg;
	v.is_temp = is_temp;
	return v;
}

static Value makeConstIntValue(u64 bits, ls_type_kind type) {
	Value v;
	v.kind = Value::CONST_INT;
	v.type = type;
	v.bits = bits;
	return v;
}

static bool isConstValue(const Value& v) {
	return v.kind == Value::CONST_INT || v.kind == Value::CONST_FLOAT;
}

// Emit a conditional branch taken when `lhs cmp rhs` == `jump_if_true`;
// returns the jump operand position for patching. Defined with the other
// branch emitters below.
static u32 emitCompareJumpValues(FunctionCompiler& ctx, ls_op cmp_op, ls_type_kind kind, const Value& lhs, const Value& rhs, bool jump_if_true);

// Emit a deferred constant into `dst` as `kind`-typed bytes.
static void emitConstValueAt(FunctionCompiler& ctx, const Value& v, ls_type_kind kind, u32 dst) {
	if (isFloatKind(kind)) {
		double d;
		if (v.kind == Value::CONST_FLOAT)
			d = v.fval;
		else
			d = isIntegerKind(v.type) && v.type != LS_TYPE_U64 ? (double)(i64)v.bits : (double)v.bits;
		if (kind == LS_TYPE_F32)
			emitConst4At(ctx, dst, bitcastF32ToU32((float)d));
		else
			emitConst8At(ctx, dst, bitcastF64ToU64(d));
		return;
	}
	ASSERT(v.kind == Value::CONST_INT);
	emitIntegerConstantAt(ctx, dst, kind, v.bits);
}

// Return a frame register holding `v`. Deferred constants materialize as
// `kind` on the temp stack; registers are returned as-is.
static u32 valueReg(FunctionCompiler& ctx, const Value& v, ls_type_kind kind) {
	if (v.kind == Value::REG) return v.reg;
	const u32 dst = ctx.temp_top;
	emitConstValueAt(ctx, v, kind, dst);
	setTempTop(ctx, dst + typeKindByteSize(kind));
	return dst;
}

// Lowest temp-stack byte consumed by the operands, i.e. where the temp top
// rewinds to once they are consumed (and where a stack result lands).
static u32 valueReleasePoint(FunctionCompiler& ctx, const Value& a, const Value& b) {
	u32 release_to = ctx.temp_top;
	if (a.kind == Value::REG && a.is_temp && a.reg < release_to) release_to = a.reg;
	if (b.kind == Value::REG && b.is_temp && b.reg < release_to) release_to = b.reg;
	return release_to;
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
	bool swap_operands = false;
	if (op == LS_OP_GT) {
		op = LS_OP_LT;
		swap_operands = true;
	} else if (op == LS_OP_GE) {
		op = LS_OP_LE;
		swap_operands = true;
	}
	emitOp(ctx.code, op);
	emitTempReg(ctx, lhs);
	emitTempReg(ctx, swap_operands ? rhs : lhs);
	emitTempReg(ctx, swap_operands ? lhs : rhs);
	emitU8(ctx.code, (u8)kind);
	ctx.temp_top = lhs + 1u;
}

static void emitCompareOpAt(FunctionCompiler& ctx, ls_op op, ls_type_kind kind, u32 dst, u32 lhs, u32 rhs) {
	bool swap_operands = false;
	if (op == LS_OP_GT) {
		op = LS_OP_LT;
		swap_operands = true;
	} else if (op == LS_OP_GE) {
		op = LS_OP_LE;
		swap_operands = true;
	}
	emitOp(ctx.code, op);
	emitFixedReg(ctx, dst);
	emitFixedReg(ctx, swap_operands ? rhs : lhs);
	emitFixedReg(ctx, swap_operands ? lhs : rhs);
	emitU8(ctx.code, (u8)kind);
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

static void emitCastFromLocal(FunctionCompiler& ctx, u32 src, ls_type_kind src_kind, ls_type_kind dst_kind) {
	const u32 dst = ctx.temp_top;
	emitOp(ctx.code, LS_OP_CAST);
	emitTempReg(ctx, dst);
	emitFixedReg(ctx, src);
	emitU8(ctx.code, (u8)src_kind);
	emitU8(ctx.code, (u8)dst_kind);
	setTempTop(ctx, dst + typeKindByteSize(dst_kind));
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
	emitOp(ctx.code, LS_OP_LOAD_INDEXED);
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
	emitOp(ctx.code, LS_OP_STORE_INDEXED);
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
	emitOp(ctx.code, LS_OP_REF_INDEXED);
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

static bool patchI16(ByteArray& code, u32 offset, i32 value) {
	if (value < -32768 || value > 32767) return false;
	const u16 bits = (u16)(i16)value;
	code[offset + 0u] = (u8)(bits & 0xFFu);
	code[offset + 1u] = (u8)((bits >> 8u) & 0xFFu);
	return true;
}

static void emitSliceOp(FunctionCompiler& ctx, u32 element_size) {
	const u32 end = ctx.temp_top - typeKindByteSize(LS_TYPE_I64);
	const u32 begin = end - typeKindByteSize(LS_TYPE_I64);
	const u32 slice = begin - typeKindByteSize(LS_TYPE_SLICE);
	emitOp(ctx.code, LS_OP_SLICE);
	emitTempReg(ctx, slice);
	emitTempReg(ctx, begin);
	emitTempReg(ctx, end);
	emitU32(ctx.code, element_size);
	setTempTop(ctx, slice + typeKindByteSize(LS_TYPE_SLICE));
}

// Consumes a slice value and an index, produces a bounds-checked pointer to the
// element. The pointer overwrites the slice value in place, so the op has no
// separate destination operand.
static void emitSliceRef(FunctionCompiler& ctx, u32 element_size) {
	const u32 index = ctx.temp_top - typeKindByteSize(LS_TYPE_I64);
	const u32 slice = index - typeKindByteSize(LS_TYPE_SLICE);
	emitOp(ctx.code, LS_OP_SLICE_REF);
	emitTempReg(ctx, slice);
	emitTempReg(ctx, index);
	emitU32(ctx.code, element_size);
	setTempTop(ctx, slice + typeKindByteSize(LS_TYPE_CPTR));
}

static void emitSliceLength(FunctionCompiler& ctx) {
	const u32 slice = ctx.temp_top - typeKindByteSize(LS_TYPE_SLICE);
	emitOp(ctx.code, LS_OP_SLICE_LENGTH);
	emitTempReg(ctx, slice);
	emitTempReg(ctx, slice);
	setTempTop(ctx, slice + typeKindByteSize(LS_TYPE_I64));
}

static void emitSliceLengthLocal(FunctionCompiler& ctx, u32 slice_offset) {
	const u32 dst = ctx.temp_top;
	emitOp(ctx.code, LS_OP_SLICE_LENGTH);
	emitTempReg(ctx, dst);
	emitFixedReg(ctx, slice_offset);
	setTempTop(ctx, dst + typeKindByteSize(LS_TYPE_I64));
}

static void emitSliceLengthToLocal(FunctionCompiler& ctx, u32 dst, u32 slice_offset) {
	emitOp(ctx.code, LS_OP_SLICE_LENGTH);
	emitFixedReg(ctx, dst);
	emitFixedReg(ctx, slice_offset);
}

static void emitCallDirect(FunctionCompiler& ctx, u32 callee_index, u32 arg_size, u32 return_size) {
	const u32 arg = ctx.temp_top - arg_size;
	emitOp(ctx.code, LS_OP_CALL_DIRECT);
	emitU32(ctx.code, callee_index);
	emitTempReg(ctx, arg);
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
	const u32 source = return_size > 0u ? ctx.temp_top - return_size : 0u;
	emitOp(ctx.code, source == 0u ? LS_OP_RETURN_BASE : LS_OP_RETURN);
	if (source != 0u) emitFixedReg(ctx, source);
	if (source != 0u) emitU32(ctx.code, return_size);
	ctx.temp_top = ctx.next_local_offset;
}

static void emitReturnFromLocal(FunctionCompiler& ctx, u32 source) {
	emitOp(ctx.code, source == 0u ? LS_OP_RETURN_BASE : LS_OP_RETURN);
	if (source != 0u && ctx.out.return_size > 0u) emitFixedReg(ctx, source);
	if (source != 0u) emitU32(ctx.code, ctx.out.return_size);
	ctx.temp_top = ctx.next_local_offset;
}

static bool tryEmitDirectLocalReturn(FunctionCompiler& ctx, Expression& expr) {
	if (expr.kind != Expression::IDENTIFIER || expr.resolved_type != ctx.return_type) return false;
	IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
	if (!id.slot || id.slot->storage != StorageSlot::LOCAL || id.slot->type != expr.resolved_type) return false;
	if (!ctx.deferreds.empty()) return false;
	emitReturnFromLocal(ctx, id.slot->offset);
	return true;
}


// Size the frame and retain the compiler buffer until all functions can be
// packed into one contiguous bytecode allocation.
static void finalizeFunctionCode(FunctionCompiler& ctx, ls_function_bc& function, ls_arena& arena) {
	function.frame_size = ctx.frame_high_water;
	function.code_size = (u32)ctx.code.size();
	function.source_map_count = (u32)ctx.code.source_map.size();
	if (function.source_map_count > 0u) {
		function.source_map =
			static_cast<ls_bytecode_source_map_entry*>(arena.allocate(arena.user_data, sizeof(ls_bytecode_source_map_entry) * function.source_map_count, alignof(ls_bytecode_source_map_entry)));
		for (u32 i = 0; i < function.source_map_count; ++i) {
			function.source_map[i] = ctx.code.source_map[(i32)i];
			ls_bytecode_source_map_entry& dst = function.source_map[i];
			dst.source_name = copyStringViewToArena(arena, dst.source_name);
		}
	}
	if (function.code_size > 0u) {
		function.code = ctx.code.data;
	}
	function.local_count = (u32)ctx.locals_debug.size();
	if (function.local_count > 0u) {
		function.locals =
			static_cast<ls_bytecode_local_debug_entry*>(arena.allocate(arena.user_data, sizeof(ls_bytecode_local_debug_entry) * function.local_count, alignof(ls_bytecode_local_debug_entry)));
		for (u32 i = 0; i < function.local_count; ++i) {
			function.locals[i] = ctx.locals_debug[(i32)i];
			function.locals[i].name = copyStringViewToArena(arena, function.locals[i].name);
		}
	}
}

static bool packFunctionCode(ls_bytecode& bytecode, ls_arena& arena) {
	size_t total_size = 0;
	for (u32 i = 0; i < bytecode.function_count; ++i) {
		const ls_function_bc& function = bytecode.functions[i];
		if (function.code_size > SIZE_MAX - total_size) return false;
		total_size += function.code_size;
	}
	if (total_size == 0u) return true;

	u8* code = static_cast<u8*>(arena.allocate(arena.user_data, total_size, alignof(u8)));
	if (!code) return false;
	for (u32 i = 0; i < bytecode.function_count; ++i) {
		ls_function_bc& function = bytecode.functions[i];
		if (function.code_size == 0u) continue;
		copyMemory(code, function.code, function.code_size);
		function.code = code;
		code += function.code_size;
	}
	return true;
}

static void compileIndexExpression(FunctionCompiler& ctx, Expression& expr) {
	const ls_type_kind kind = compileExpression(ctx, expr, LS_TYPE_I64);
	if (kind != LS_TYPE_I64) emitCast(ctx, kind, LS_TYPE_I64);
}

static void emitStaticBoundsCheck(FunctionCompiler& ctx, ResolvedType& type) {
	emitOp(ctx.code, LS_OP_BOUNDS_CHECK);
	emitTempReg(ctx, ctx.temp_top - typeKindByteSize(LS_TYPE_I64));
	emitU64(ctx.code, (u64) static_cast<ArrayResolvedType&>(type).size);
}

static bool getSliceMemberAccess(MemberExpression& member, BracketExpression*& bracket, u32& element_size, i32& field_offset, ResolvedType*& field_type) {
	if (!member.expression || member.expression->kind != Expression::BRACKET) return false;
	BracketExpression* br = static_cast<BracketExpression*>(member.expression);
	if (!br->base->resolved_type || br->base->resolved_type->kind != ResolvedType::SLICE) return false;
	if (!member.expression->resolved_type || member.expression->resolved_type->kind != ResolvedType::STRUCT) return false;
	StructResolvedType* st = static_cast<StructResolvedType*>(member.expression->resolved_type);
	field_offset = (i32)structFieldByteOffset(*st, member.name, field_type);
	element_size = typeByteSize(*st);
	bracket = br;
	return true;
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
			if (br->base->resolved_type->kind == ResolvedType::SLICE) {
				compileExpression(ctx, *br->base, LS_TYPE_SLICE);
				compileIndexExpression(ctx, *br->args[0]);
				emitSliceRef(ctx, typeByteSize(*br->resolved_type));
				return true;
			}
			if (!tryEmitReference(ctx, *br->base)) return false;
			compileIndexExpression(ctx, *br->args[0]);
			emitStaticBoundsCheck(ctx, *br->base->resolved_type);
			emitRefAt(ctx, typeByteSize(*br->resolved_type), 0);
			return true;
		}
		default: return false;
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
					if (source.members[i] != un.members[i]) {
						same_layout = false;
						break;
					}
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
					if (source.members[i] == un.members[j]) {
						target_index = j;
						break;
					}
				}
				ASSERT(target_index >= 0);

				// The union tag is the first 4 bytes of the source local.
				const u32 next_member_jump = emitCompareJumpValues(ctx, LS_OP_EQ, LS_TYPE_I32, makeRegValue(source_offset, LS_TYPE_I32, false), makeConstIntValue((u64)i, LS_TYPE_I32), false);

				emitIntegerConstant(ctx, LS_TYPE_I32, target_index);
				const u32 member_size = typeByteSize(*source.members[i]);
				emitLoadLocalBytes(ctx, source_offset + 4, member_size);
				const u32 payload_size = typeByteSize(expected_type) - 4;
				if (payload_size > member_size) emitZeroBytes(ctx, payload_size - member_size);
				end_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP));

				patchJumpRelative(ctx, next_member_jump, (u32)ctx.code.size());
				ctx.temp_top = result_top;
			}
			emitZeroBytes(ctx, typeByteSize(expected_type));
			const u32 end = (u32)ctx.code.size();
			for (u32 jump : end_jumps) patchJumpRelative(ctx, jump, end);
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
		} else {
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
		case LS_TYPE_I8: return 0;
		case LS_TYPE_U8: return 1;
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
		case Token::PLUS_EQUAL: emitNumericBinary(ctx, LS_OP_ADD_I8, kind); return;
		case Token::MINUS_EQUAL: emitNumericBinary(ctx, LS_OP_SUB_I8, kind); return;
		case Token::STAR_EQUAL: emitNumericBinary(ctx, LS_OP_MUL_I8, kind); return;
		case Token::SLASH_EQUAL: emitNumericBinary(ctx, LS_OP_DIV_I8, kind); return;
		default: ASSERT(false); return;
	}
}

// Compile an expression into a Value descriptor. Local identifiers and
// numeric/bool literals stay deferred; everything else materializes at the
// current temp top.
static Value compileValue(FunctionCompiler& ctx, Expression& expr, ls_type_kind hint) {
	switch (expr.kind) {
		case Expression::INT_LITERAL: {
			Value v;
			const ls_type_kind kind = toTypeKind(*expr.resolved_type);
			const u64 value = static_cast<IntLiteralExpression&>(expr).value;
			if (isFloatKind(kind)) {
				v.kind = Value::CONST_FLOAT;
				v.fval = (double)value;
			} else {
				v.kind = Value::CONST_INT;
				v.bits = value;
			}
			v.type = kind;
			return v;
		}
		case Expression::FLOAT_LITERAL: {
			Value v;
			v.kind = Value::CONST_FLOAT;
			v.type = defaultLiteralKind(expr, hint);
			v.fval = static_cast<FloatLiteralExpression&>(expr).value;
			return v;
		}
		case Expression::BOOL_LITERAL: {
			Value v;
			v.kind = Value::CONST_INT;
			v.type = LS_TYPE_BOOL;
			v.bits = static_cast<BoolLiteralExpression&>(expr).value ? 1u : 0u;
			return v;
		}
		case Expression::UNARY: {
			UnaryExpression& un = static_cast<UnaryExpression&>(expr);
			if (!un.resolved_fn && un.op == Token::MINUS && (un.expression->kind == Expression::INT_LITERAL || un.expression->kind == Expression::FLOAT_LITERAL)) {
				Value v = compileValue(ctx, *un.expression, hint);
				if (v.kind == Value::CONST_FLOAT)
					v.fval = -v.fval;
				else
					v.bits = 0u - v.bits;
				return v;
			}
			break;
		}
		case Expression::IDENTIFIER: {
			IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
			StorageSlot* slot = id.slot;
			const bool union_payload = slot && slot->type && slot->type->kind == ResolvedType::UNION && expr.resolved_type && expr.resolved_type->kind != ResolvedType::UNION;
			if (slot && slot->storage == StorageSlot::LOCAL && !union_payload) {
				return makeRegValue(slot->offset, slot->kind != LS_TYPE_INVALID ? slot->kind : LS_TYPE_I32, false);
			}
			break;
		}
		default: break;
	}
	const ls_type_kind kind = compileExpression(ctx, expr, hint);
	return makeRegValue(ctx.temp_top - typeKindByteSize(kind), kind, true);
}

// Generic arithmetic on two Values: `dst = lhs op rhs` with operands read in
// place. Consumed temps are released; the result becomes a new temp, or goes
// to `dst_override` (a frame offset outside the temp stack) when >= 0.
static Value emitBinaryOpValues(FunctionCompiler& ctx, ls_op group_base, ls_type_kind kind, const Value& lhs, const Value& rhs, i64 dst_override = -1) {
	const u32 size = typeKindByteSize(kind);
	const u32 release_to = valueReleasePoint(ctx, lhs, rhs);
	const u32 lreg = valueReg(ctx, lhs, kind);
	const u32 rreg = valueReg(ctx, rhs, kind);
	const u32 dst = dst_override >= 0 ? (u32)dst_override : release_to;
	emitOp(ctx.code, (ls_op)((u32)group_base + numericKindIndex(kind)));
	emitFixedReg(ctx, dst);
	emitFixedReg(ctx, lreg);
	emitFixedReg(ctx, rreg);
	if (dst_override >= 0) {
		ctx.temp_top = release_to;
		return makeRegValue(dst, kind, false);
	}
	setTempTop(ctx, release_to + size);
	return makeRegValue(dst, kind, true);
}

// Generic comparison on two Values, producing a bool. Same operand/result
// placement rules as emitBinaryOpValues.
static Value emitCompareOpValues(FunctionCompiler& ctx, ls_op compare_op, ls_type_kind kind, const Value& lhs, const Value& rhs, i64 dst_override = -1) {
	const u32 release_to = valueReleasePoint(ctx, lhs, rhs);
	const u32 lreg = valueReg(ctx, lhs, kind);
	const u32 rreg = valueReg(ctx, rhs, kind);
	const u32 dst = dst_override >= 0 ? (u32)dst_override : release_to;
	emitCompareOpAt(ctx, compare_op, kind, dst, lreg, rreg);
	if (dst_override >= 0) {
		ctx.temp_top = release_to;
		return makeRegValue(dst, LS_TYPE_BOOL, false);
	}
	setTempTop(ctx, release_to + 1u);
	return makeRegValue(dst, LS_TYPE_BOOL, true);
}

static bool isI32Index(Expression& expr) {
	return expr.resolved_type && valueKindForType(*expr.resolved_type) == LS_TYPE_I32;
}

static bool isSideEffectFreeExpression(Expression& expr) {
	switch (expr.kind) {
		case Expression::INT_LITERAL:
		case Expression::FLOAT_LITERAL:
		case Expression::BOOL_LITERAL:
		case Expression::NULL_LITERAL:
		case Expression::UNDEFINED:
		case Expression::IDENTIFIER: return true;
		case Expression::CAST: return isSideEffectFreeExpression(*static_cast<CastExpression&>(expr).expression);
		case Expression::UNARY: return isSideEffectFreeExpression(*static_cast<UnaryExpression&>(expr).expression);
		case Expression::BINARY: {
			BinaryExpression& binary = static_cast<BinaryExpression&>(expr);
			return isSideEffectFreeExpression(*binary.lhs) && isSideEffectFreeExpression(*binary.rhs);
		}
		case Expression::BRACKET: {
			BracketExpression& bracket = static_cast<BracketExpression&>(expr);
			if (!isSideEffectFreeExpression(*bracket.base)) return false;
			for (Expression* arg : bracket.args)
				if (!isSideEffectFreeExpression(*arg)) return false;
			return true;
		}
		case Expression::MEMBER: {
			// `.enum_member` has no base expression and is a constant.
			Expression* base = static_cast<MemberExpression&>(expr).expression;
			return !base || isSideEffectFreeExpression(*base);
		}
		default: return false;
	}
}

// ---------------------------------------------------------------------------
// Element access descriptors
//
// An access descriptor captures where an element access's base and index
// live. One load emitter and one store emitter then pick the opcode from the
// descriptor instead of the caller pattern-matching operand shapes.

// Normalize an index Value: i32 register/constant (is_i32) or i64 register.
static Value coerceIndexValue(FunctionCompiler& ctx, Value v, bool& is_i32) {
	if (v.kind == Value::CONST_INT) {
		is_i32 = (i64)v.bits >= 0 && (i64)v.bits <= 0x7fffffffll;
		v.type = is_i32 ? LS_TYPE_I32 : LS_TYPE_I64;
		return v;
	}
	if (v.kind == Value::CONST_FLOAT) {
		const u32 reg = valueReg(ctx, v, v.type);
		v = makeRegValue(reg, v.type, true);
	}
	if (v.type == LS_TYPE_I32) {
		is_i32 = true;
		return v;
	}
	is_i32 = false;
	if (v.type == LS_TYPE_I64) return v;
	if (!v.is_temp) {
		const u32 dst = ctx.temp_top;
		emitCastFromLocal(ctx, v.reg, v.type, LS_TYPE_I64);
		return makeRegValue(dst, LS_TYPE_I64, true);
	}
	emitCast(ctx, v.type, LS_TYPE_I64);
	return makeRegValue(ctx.temp_top - typeKindByteSize(LS_TYPE_I64), LS_TYPE_I64, true);
}

// Force an i32 index into an i64 register (the pointer-based indexed ops and
// BOUNDS_CHECK read a full 8-byte index).
static Value widenIndexToI64(FunctionCompiler& ctx, Value v, bool& is_i32) {
	if (!is_i32) return v;
	is_i32 = false;
	if (v.kind == Value::CONST_INT) {
		v.type = LS_TYPE_I64;
		return v;
	}
	if (!v.is_temp) {
		const u32 dst = ctx.temp_top;
		emitCastFromLocal(ctx, v.reg, LS_TYPE_I32, LS_TYPE_I64);
		return makeRegValue(dst, LS_TYPE_I64, true);
	}
	emitCast(ctx, LS_TYPE_I32, LS_TYPE_I64);
	return makeRegValue(ctx.temp_top - typeKindByteSize(LS_TYPE_I64), LS_TYPE_I64, true);
}

// If the value can change while other code runs (it names a frame local),
// snapshot it into a temp. Used before evaluating a store's rhs, whose side
// effects must not retroactively change an already-evaluated base or index.
static void snapshotValue(FunctionCompiler& ctx, Value& v, ls_type_kind kind) {
	if (v.kind != Value::REG || v.is_temp) return;
	const u32 size = typeKindByteSize(kind);
	const u32 dst = ctx.temp_top;
	emitOp(ctx.code, LS_OP_COPY);
	emitTempReg(ctx, dst);
	emitFixedReg(ctx, v.reg);
	emitU32(ctx.code, size);
	setTempTop(ctx, dst + size);
	v = makeRegValue(dst, kind, true);
}

struct SliceAccess {
	Value slice;
	Value index;
	bool index_is_i32 = false;
	u32 element_size = 0;
	i32 field_offset = 0;
	u32 field_size = 0; // bytes actually accessed; == element_size for whole elements
	bool is_field = false;
};

// Evaluate a slice element access's base and index; both stay in place (or
// deferred) for the load/store emitters.
static SliceAccess compileSliceAccess(FunctionCompiler& ctx, BracketExpression& br) {
	SliceAccess a;
	a.element_size = typeByteSize(*br.resolved_type);
	a.field_size = a.element_size;
	a.slice = compileValue(ctx, *br.base, LS_TYPE_SLICE);
	a.index = coerceIndexValue(ctx, compileValue(ctx, *br.args[0], LS_TYPE_I64), a.index_is_i32);
	return a;
}

static ls_type_kind sliceIndexKind(const SliceAccess& a) {
	return a.index_is_i32 ? LS_TYPE_I32 : LS_TYPE_I64;
}

// Load an element (or element field) through a slice access. The result goes
// to `dst_override` when >= 0, else onto the temp stack. `keep_operands`
// leaves the base/index temps live so a compound assignment can store back
// through the same access.
static void emitSliceAccessLoad(FunctionCompiler& ctx, SliceAccess& a, i64 dst_override = -1, bool keep_operands = false) {
	const u32 release_to = valueReleasePoint(ctx, a.slice, a.index);
	const u32 slice_reg = valueReg(ctx, a.slice, LS_TYPE_SLICE);
	const u32 index_reg = valueReg(ctx, a.index, sliceIndexKind(a));
	if (a.index.kind != Value::REG) a.index = makeRegValue(index_reg, sliceIndexKind(a), true);
	const u32 result_size = a.field_size;
	const u32 dst = dst_override >= 0 ? (u32)dst_override : (keep_operands ? ctx.temp_top : release_to);
	if (a.is_field) {
		emitOp(ctx.code, a.index_is_i32 ? LS_OP_SLICE_LOAD_AT_LOCAL_I32 : LS_OP_SLICE_LOAD_AT_LOCAL);
		emitFixedReg(ctx, dst);
		emitFixedReg(ctx, slice_reg);
		emitFixedReg(ctx, index_reg);
		emitU32(ctx.code, a.element_size);
		emitI32(ctx.code, a.field_offset);
		emitU32(ctx.code, a.field_size);
	} else {
		emitOp(ctx.code, a.index_is_i32 ? LS_OP_SLICE_LOAD_LOCAL_I32 : LS_OP_SLICE_LOAD_LOCAL);
		emitFixedReg(ctx, dst);
		emitFixedReg(ctx, slice_reg);
		emitFixedReg(ctx, index_reg);
		emitU32(ctx.code, a.element_size);
	}
	if (dst_override >= 0) {
		if (!keep_operands) ctx.temp_top = release_to;
	} else if (keep_operands)
		setTempTop(ctx, dst + result_size);
	else
		setTempTop(ctx, release_to + result_size);
}

// Store `value` into an element (or element field) through a slice access,
// releasing all consumed temps.
static void emitSliceAccessStore(FunctionCompiler& ctx, SliceAccess& a, const Value& value, ls_type_kind value_kind) {
	u32 release_to = valueReleasePoint(ctx, a.slice, a.index);
	if (value.kind == Value::REG && value.is_temp && value.reg < release_to) release_to = value.reg;
	const u32 slice_reg = valueReg(ctx, a.slice, LS_TYPE_SLICE);
	const u32 index_reg = valueReg(ctx, a.index, sliceIndexKind(a));
	const u32 value_reg = valueReg(ctx, value, value_kind);
	if (a.is_field) {
		emitOp(ctx.code, a.index_is_i32 ? LS_OP_SLICE_STORE_AT_LOCAL_I32 : LS_OP_SLICE_STORE_AT_LOCAL);
		emitFixedReg(ctx, slice_reg);
		emitFixedReg(ctx, index_reg);
		emitFixedReg(ctx, value_reg);
		emitU32(ctx.code, a.element_size);
		emitI32(ctx.code, a.field_offset);
		emitU32(ctx.code, a.field_size);
	} else {
		emitOp(ctx.code, a.index_is_i32 ? LS_OP_SLICE_STORE_LOCAL_I32 : LS_OP_SLICE_STORE_LOCAL);
		emitFixedReg(ctx, slice_reg);
		emitFixedReg(ctx, index_reg);
		emitFixedReg(ctx, value_reg);
		emitU32(ctx.code, a.element_size);
	}
	ctx.temp_top = release_to;
}

struct ArrayAccess {
	// A local array indexed by i32 (or constant) is addressed directly in the
	// frame with a static bound; anything else goes through a pointer.
	bool direct = false;
	u32 base_offset = 0; // direct
	Value base_ref;		 // !direct: cptr register
	Value index;
	bool index_is_i32 = false;
	u32 length = 0;						// direct: static array length
	ResolvedType* array_type = nullptr; // !direct: bounds check source
	u32 element_size = 0;
};

static void emitBoundsCheckReg(FunctionCompiler& ctx, u32 index_reg, ResolvedType& type) {
	emitOp(ctx.code, LS_OP_BOUNDS_CHECK);
	emitFixedReg(ctx, index_reg);
	emitU64(ctx.code, (u64) static_cast<ArrayResolvedType&>(type).size);
}

// Evaluate an array element access's base and index.
static ArrayAccess compileArrayAccess(FunctionCompiler& ctx, BracketExpression& br) {
	ArrayAccess a;
	a.element_size = typeByteSize(*br.resolved_type);
	a.array_type = br.base->resolved_type;
	IdentifierExpression* base_id = br.base->kind == Expression::IDENTIFIER ? static_cast<IdentifierExpression*>(br.base) : nullptr;
	StorageSlot* base_slot = base_id ? base_id->slot : nullptr;
	if (base_slot && base_slot->storage == StorageSlot::LOCAL && base_slot->type->kind == ResolvedType::ARRAY) {
		a.index = coerceIndexValue(ctx, compileValue(ctx, *br.args[0], LS_TYPE_I32), a.index_is_i32);
		if (a.index_is_i32) {
			a.direct = true;
			a.base_offset = base_slot->offset;
			a.length = (u32) static_cast<ArrayResolvedType*>(br.base->resolved_type)->size;
			return a;
		}
		// 64-bit dynamic index: address the local through a pointer. The base
		// reference lands above the already-compiled index; both are plain
		// registers so ordering does not matter.
		emitSlotRef(ctx, *base_slot);
		a.base_ref = makeRegValue(ctx.temp_top - typeKindByteSize(LS_TYPE_CPTR), LS_TYPE_CPTR, true);
		return a;
	}
	if (!tryEmitReference(ctx, *br.base)) {
		compileExpression(ctx, *br.base, LS_TYPE_INVALID);
		emitLocalRef(ctx, ctx.temp_top - typeByteSize(*br.base->resolved_type));
	}
	a.base_ref = makeRegValue(ctx.temp_top - typeKindByteSize(LS_TYPE_CPTR), LS_TYPE_CPTR, true);
	a.index = coerceIndexValue(ctx, compileValue(ctx, *br.args[0], LS_TYPE_I64), a.index_is_i32);
	return a;
}

// A direct access with an in-bounds constant index is just a frame register.
static bool arrayAccessConstReg(const ArrayAccess& a, u32& out_reg) {
	if (!a.direct || a.index.kind != Value::CONST_INT || a.index.bits >= a.length) return false;
	out_reg = a.base_offset + (u32)a.index.bits * a.element_size;
	return true;
}

static void emitArrayAccessLoad(FunctionCompiler& ctx, ArrayAccess& a, i64 dst_override = -1, bool keep_operands = false) {
	u32 const_reg = 0;
	if (arrayAccessConstReg(a, const_reg)) {
		const u32 dst = dst_override >= 0 ? (u32)dst_override : ctx.temp_top;
		emitOp(ctx.code, LS_OP_COPY);
		emitFixedReg(ctx, dst);
		emitFixedReg(ctx, const_reg);
		emitU32(ctx.code, a.element_size);
		if (dst_override < 0) setTempTop(ctx, dst + a.element_size);
		return;
	}
	if (a.direct) {
		const u32 release_to = valueReleasePoint(ctx, a.index, a.index);
		const u32 index_reg = valueReg(ctx, a.index, LS_TYPE_I32);
		if (a.index.kind != Value::REG) a.index = makeRegValue(index_reg, LS_TYPE_I32, true);
		const u32 dst = dst_override >= 0 ? (u32)dst_override : (keep_operands ? ctx.temp_top : release_to);
		emitOp(ctx.code, LS_OP_LOAD_INDEXED_LOCAL_I32);
		emitFixedReg(ctx, dst);
		emitFixedReg(ctx, a.base_offset);
		emitFixedReg(ctx, index_reg);
		emitU32(ctx.code, a.element_size);
		emitI32(ctx.code, 0);
		emitU32(ctx.code, a.length);
		emitU32(ctx.code, a.element_size);
		if (dst_override >= 0) {
			if (!keep_operands) ctx.temp_top = release_to;
		} else if (keep_operands)
			setTempTop(ctx, dst + a.element_size);
		else
			setTempTop(ctx, release_to + a.element_size);
		return;
	}
	a.index = widenIndexToI64(ctx, a.index, a.index_is_i32);
	const u32 release_to = valueReleasePoint(ctx, a.base_ref, a.index);
	const u32 base_reg = valueReg(ctx, a.base_ref, LS_TYPE_CPTR);
	const u32 index_reg = valueReg(ctx, a.index, LS_TYPE_I64);
	if (a.index.kind != Value::REG) a.index = makeRegValue(index_reg, LS_TYPE_I64, true);
	emitBoundsCheckReg(ctx, index_reg, *a.array_type);
	const u32 dst = dst_override >= 0 ? (u32)dst_override : (keep_operands ? ctx.temp_top : release_to);
	emitOp(ctx.code, LS_OP_LOAD_INDEXED);
	emitFixedReg(ctx, dst);
	emitFixedReg(ctx, base_reg);
	emitFixedReg(ctx, index_reg);
	emitU32(ctx.code, a.element_size);
	emitI32(ctx.code, 0);
	emitU32(ctx.code, a.element_size);
	if (dst_override >= 0) {
		if (!keep_operands) ctx.temp_top = release_to;
	} else if (keep_operands)
		setTempTop(ctx, dst + a.element_size);
	else
		setTempTop(ctx, release_to + a.element_size);
}

static void emitArrayAccessStore(FunctionCompiler& ctx, ArrayAccess& a, const Value& value, ls_type_kind value_kind) {
	u32 const_reg = 0;
	if (arrayAccessConstReg(a, const_reg)) {
		u32 release_to = ctx.temp_top;
		if (value.kind == Value::REG && value.is_temp && value.reg < release_to) release_to = value.reg;
		if (isConstValue(value)) {
			emitConstValueAt(ctx, value, value_kind, const_reg);
		} else {
			emitOp(ctx.code, LS_OP_COPY);
			emitFixedReg(ctx, const_reg);
			emitFixedReg(ctx, value.reg);
			emitU32(ctx.code, a.element_size);
		}
		ctx.temp_top = release_to;
		return;
	}
	if (a.direct) {
		u32 release_to = valueReleasePoint(ctx, a.index, a.index);
		if (value.kind == Value::REG && value.is_temp && value.reg < release_to) release_to = value.reg;
		const u32 index_reg = valueReg(ctx, a.index, LS_TYPE_I32);
		const u32 value_reg = valueReg(ctx, value, value_kind);
		emitOp(ctx.code, LS_OP_STORE_INDEXED_LOCAL_I32);
		emitFixedReg(ctx, a.base_offset);
		emitFixedReg(ctx, index_reg);
		emitFixedReg(ctx, value_reg);
		emitU32(ctx.code, a.element_size);
		emitI32(ctx.code, 0);
		emitU32(ctx.code, a.length);
		emitU32(ctx.code, a.element_size);
		ctx.temp_top = release_to;
		return;
	}
	a.index = widenIndexToI64(ctx, a.index, a.index_is_i32);
	u32 release_to = valueReleasePoint(ctx, a.base_ref, a.index);
	if (value.kind == Value::REG && value.is_temp && value.reg < release_to) release_to = value.reg;
	const u32 base_reg = valueReg(ctx, a.base_ref, LS_TYPE_CPTR);
	const u32 index_reg = valueReg(ctx, a.index, LS_TYPE_I64);
	const u32 value_reg = valueReg(ctx, value, value_kind);
	emitBoundsCheckReg(ctx, index_reg, *a.array_type);
	emitOp(ctx.code, LS_OP_STORE_INDEXED);
	emitFixedReg(ctx, base_reg);
	emitFixedReg(ctx, index_reg);
	emitFixedReg(ctx, value_reg);
	emitU32(ctx.code, a.element_size);
	emitI32(ctx.code, 0);
	emitU32(ctx.code, a.element_size);
	ctx.temp_top = release_to;
}

// Compile a store's rhs as `type`, deferring simple values so the store
// emitters can read them in place.
static Value compileValueAsType(FunctionCompiler& ctx, Expression& expr, ResolvedType& type) {
	const ls_type_kind kind = valueKindForType(type);
	const bool simple_kind = isNumericKind(kind) || kind == LS_TYPE_BOOL;
	if (simple_kind) {
		switch (expr.kind) {
			case Expression::INT_LITERAL:
			case Expression::FLOAT_LITERAL:
			case Expression::BOOL_LITERAL: return compileValue(ctx, expr, kind);
			case Expression::UNARY: {
				UnaryExpression& un = static_cast<UnaryExpression&>(expr);
				if (!un.resolved_fn && un.op == Token::MINUS && (un.expression->kind == Expression::INT_LITERAL || un.expression->kind == Expression::FLOAT_LITERAL)) {
					return compileValue(ctx, expr, kind);
				}
				break;
			}
			default: break;
		}
	}
	if (expr.kind == Expression::IDENTIFIER && expr.resolved_type == &type) {
		IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
		if (id.slot && id.slot->storage == StorageSlot::LOCAL && id.slot->byte_size == typeByteSize(type)) {
			return makeRegValue(id.slot->offset, kind, false);
		}
	}
	compileExpressionAsType(ctx, expr, type);
	u32 byte_size = typeByteSize(type);
	if (byte_size == 0u) byte_size = 1u;
	return makeRegValue(ctx.temp_top - byte_size, kind, true);
}

// Group base for a compound-assignment token, or 0 when the op has no
// numeric opcode group.
static ls_op numericOpGroupForAssign(Token::Type op) {
	switch (op) {
		case Token::PLUS_EQUAL: return LS_OP_ADD_I8;
		case Token::MINUS_EQUAL: return LS_OP_SUB_I8;
		case Token::STAR_EQUAL: return LS_OP_MUL_I8;
		case Token::SLASH_EQUAL: return LS_OP_DIV_I8;
		default: return (ls_op)0;
	}
}

// `local op= 1` with an integer kind compiles to INC/DEC.
static bool isIncrementCandidate(const Value& rhs, Token::Type op) {
	return rhs.kind == Value::CONST_INT && rhs.bits == 1u && (op == Token::PLUS_EQUAL || op == Token::MINUS_EQUAL);
}

// Add one to a local (loop increments); INC/DEC when possible, else ADD.
static void emitIncrementOrAddOne(FunctionCompiler& ctx, u32 offset, ls_type_kind kind);

static bool emitIncrementRegister(FunctionCompiler& ctx, u32 offset, ls_type_kind kind, bool decrement = false) {
	if (kind != LS_TYPE_I32 && kind != LS_TYPE_I64) return false;
	if (decrement)
		emitOp(ctx.code, kind == LS_TYPE_I32 ? LS_OP_DEC_I32 : LS_OP_DEC_I64);
	else
		emitOp(ctx.code, kind == LS_TYPE_I32 ? LS_OP_INC_I32 : LS_OP_INC_I64);
	emitFixedReg(ctx, offset);
	return true;
}

static bool emitIncrementLocal(FunctionCompiler& ctx, u32 offset, ls_type_kind kind, bool decrement = false) {
	return emitIncrementRegister(ctx, offset, kind, decrement);
}

static void emitIncrementOrAddOne(FunctionCompiler& ctx, u32 offset, ls_type_kind kind) {
	if (emitIncrementLocal(ctx, offset, kind)) return;
	Value one;
	if (isFloatKind(kind)) {
		one.kind = Value::CONST_FLOAT;
		one.fval = 1.0;
	} else {
		one.kind = Value::CONST_INT;
		one.bits = 1u;
	}
	one.type = kind;
	emitBinaryOpValues(ctx, LS_OP_ADD_I8, kind, makeRegValue(offset, kind, false), one, offset);
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
	if (rhs.kind == Expression::INT_LITERAL && static_cast<IntLiteralExpression&>(rhs).value == 1u && (op == Token::PLUS_EQUAL || op == Token::MINUS_EQUAL)) {
		const u32 lhs = ctx.temp_top - typeKindByteSize(value_kind);
		if (emitIncrementRegister(ctx, lhs, value_kind, op == Token::MINUS_EQUAL)) return;
	}
	compileExpression(ctx, rhs, value_kind);
	emitNumericStoreOp(ctx, value_kind, op);
}

static void emitBracketStore(FunctionCompiler& ctx, BracketExpression& br, Expression& rhs, ls_type_kind value_kind, Token::Type op, FunctionExpression* op_fn) {
	const u32 element_size = typeByteSize(*br.resolved_type);
	const bool is_slice = br.base->resolved_type && br.base->resolved_type->kind == ResolvedType::SLICE;
	const bool rhs_pure = isSideEffectFreeExpression(rhs);

	if (op == Token::EQUAL) {
		if (is_slice) {
			SliceAccess a = compileSliceAccess(ctx, br);
			// The rhs must not observe/alter an already-evaluated base or index.
			if (!rhs_pure) {
				snapshotValue(ctx, a.index, sliceIndexKind(a));
				snapshotValue(ctx, a.slice, LS_TYPE_SLICE);
			}
			Value v = compileValueAsType(ctx, rhs, *br.resolved_type);
			emitSliceAccessStore(ctx, a, v, value_kind);
			return;
		}
		ArrayAccess a = compileArrayAccess(ctx, br);
		// array[i] = array[j] with matching layout copies in one instruction.
		if (a.direct && a.index.kind == Value::REG && rhs.kind == Expression::BRACKET && rhs_pure) {
			BracketExpression& rhs_br = static_cast<BracketExpression&>(rhs);
			IdentifierExpression* rhs_base_id = rhs_br.base->kind == Expression::IDENTIFIER ? static_cast<IdentifierExpression*>(rhs_br.base) : nullptr;
			StorageSlot* rhs_base_slot = rhs_base_id ? rhs_base_id->slot : nullptr;
			if (rhs_base_slot && rhs_base_slot->storage == StorageSlot::LOCAL && rhs_base_slot->type->kind == ResolvedType::ARRAY && rhs_br.resolved_type == br.resolved_type &&
				static_cast<ArrayResolvedType*>(rhs_br.base->resolved_type)->size == a.length && isI32Index(*rhs_br.args[0])) {
				bool src_is_i32 = false;
				Value src_index = coerceIndexValue(ctx, compileValue(ctx, *rhs_br.args[0], LS_TYPE_I32), src_is_i32);
				if (src_is_i32 && src_index.kind == Value::REG) {
					const u32 release_to = valueReleasePoint(ctx, a.index, src_index);
					emitOp(ctx.code, LS_OP_COPY_AT_LOCAL_I32);
					emitFixedReg(ctx, rhs_base_slot->offset);
					emitFixedReg(ctx, src_index.reg);
					emitFixedReg(ctx, a.base_offset);
					emitFixedReg(ctx, a.index.reg);
					emitU32(ctx.code, element_size);
					emitU32(ctx.code, a.length);
					emitU32(ctx.code, element_size);
					ctx.temp_top = release_to;
					return;
				}
				// A constant source index emitted nothing; the generic path
				// handles it just as well.
			}
		}
		if (!rhs_pure) {
			snapshotValue(ctx, a.index, a.index_is_i32 ? LS_TYPE_I32 : LS_TYPE_I64);
		}
		Value v = compileValueAsType(ctx, rhs, *br.resolved_type);
		emitArrayAccessStore(ctx, a, v, value_kind);
		return;
	}

	// Compound assignment: evaluate the access once, then load, combine, and
	// store back through the same registers.
	if (is_slice) {
		SliceAccess a = compileSliceAccess(ctx, br);
		if (!rhs_pure) {
			snapshotValue(ctx, a.index, sliceIndexKind(a));
			snapshotValue(ctx, a.slice, LS_TYPE_SLICE);
		}
		emitSliceAccessLoad(ctx, a, -1, true);
		emitCompoundValue(ctx, rhs, value_kind, op, op_fn);
		emitSliceAccessStore(ctx, a, makeRegValue(ctx.temp_top - element_size, value_kind, true), value_kind);
		return;
	}
	ArrayAccess a = compileArrayAccess(ctx, br);
	if (!rhs_pure) {
		snapshotValue(ctx, a.index, a.index_is_i32 ? LS_TYPE_I32 : LS_TYPE_I64);
	}
	emitArrayAccessLoad(ctx, a, -1, true);
	emitCompoundValue(ctx, rhs, value_kind, op, op_fn);
	emitArrayAccessStore(ctx, a, makeRegValue(ctx.temp_top - element_size, value_kind, true), value_kind);
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
	} else if (base_type->kind == ResolvedType::SLICE) {
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
	} else {
		emitIntegerConstant(ctx, LS_TYPE_I64, 0u);
	}
	if (br.end) {
		compileIndexExpression(ctx, *br.end);
	} else {
		emitLoadLocalBytes(ctx, source_offset + typeKindByteSize(LS_TYPE_CPTR), typeKindByteSize(LS_TYPE_I64));
	}
	emitSliceOp(ctx, typeByteSize(*element_type));
}

static bool patchJumpRelative(FunctionCompiler& ctx, u32 operand_pos, u32 target_pos) {
	const i64 distance = (i64)target_pos - (i64)(operand_pos + 2u);
	if (distance < -32768 || distance > 32767 || !patchI16(ctx.code, operand_pos, (i32)distance)) {
		ctx.reportError("branch target is too far away (maximum range is 32767 bytes)");
		return false;
	}
	return true;
}

static ls_op compareJumpTypeBase(ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_BOOL: return LS_OP_JE_U8;
		case LS_TYPE_I8: return LS_OP_JE_I8;
		case LS_TYPE_U8: return LS_OP_JE_U8;
		case LS_TYPE_I16: return LS_OP_JE_I16;
		case LS_TYPE_U16: return LS_OP_JE_U16;
		case LS_TYPE_I32: return LS_OP_JE_I32;
		case LS_TYPE_U32: return LS_OP_JE_U32;
		case LS_TYPE_I64: return LS_OP_JE_I64;
		case LS_TYPE_U64: return LS_OP_JE_U64;
		case LS_TYPE_F32: return LS_OP_JE_F32;
		case LS_TYPE_F64: return LS_OP_JE_F64;
		case LS_TYPE_STRING: return LS_OP_JE_STRING;
		case LS_TYPE_ENUM: return LS_OP_JUMP;
		default: return LS_OP_JUMP;
	}
}

static ls_op compareJumpOp(ls_op compare_op, ls_type_kind kind, bool jump_if_true, bool& swap_operands) {
	swap_operands = false;
	if ((kind == LS_TYPE_BOOL || kind == LS_TYPE_STRING || kind == LS_TYPE_ENUM) && (compare_op == LS_OP_EQ || compare_op == LS_OP_NE)) {
		const bool jump_on_equal = jump_if_true ? compare_op == LS_OP_EQ : compare_op == LS_OP_NE;
		if (jump_on_equal) {
			if (kind == LS_TYPE_BOOL) return LS_OP_JE_U8;
			if (kind == LS_TYPE_STRING) return LS_OP_JE_STRING;
		}
		return LS_OP_JUMP;
	}
	if (jump_if_true) {
		const u32 base = (u32)compareJumpTypeBase(kind);
		if (base == (u32)LS_OP_JUMP) return LS_OP_JUMP;
		switch (compare_op) {
			case LS_OP_EQ: return (ls_op)(base + 0u);
			case LS_OP_GE: return (ls_op)(base + 1u);
			case LS_OP_GT: return (ls_op)(base + 2u);
			case LS_OP_LT: return (ls_op)(base + 3u);
			case LS_OP_LE: return (ls_op)(base + 4u);
			default: return LS_OP_JUMP;
		}
	}
	if (compare_op == LS_OP_NE)
		compare_op = LS_OP_EQ;
	else if (compare_op == LS_OP_GT)
		compare_op = LS_OP_LE;
	else if (compare_op == LS_OP_GE)
		compare_op = LS_OP_LT;
	else if (compare_op == LS_OP_LT)
		compare_op = LS_OP_GE;
	else if (compare_op == LS_OP_LE)
		compare_op = LS_OP_GT;
	else if (compare_op == LS_OP_EQ)
		return LS_OP_JUMP;
	if ((kind == LS_TYPE_BOOL || kind == LS_TYPE_STRING || kind == LS_TYPE_ENUM) && compare_op != LS_OP_EQ) return LS_OP_JUMP;
	const u32 base = (u32)compareJumpTypeBase(kind);
	if (base == (u32)LS_OP_JUMP) return LS_OP_JUMP;
	switch (compare_op) {
		case LS_OP_EQ: return (ls_op)(base + 0u);
		case LS_OP_GE: return (ls_op)(base + 1u);
		case LS_OP_GT: return (ls_op)(base + 2u);
		case LS_OP_LT: return (ls_op)(base + 3u);
		case LS_OP_LE: return (ls_op)(base + 4u);
		default: return LS_OP_JUMP;
	}
}

static u32 emitCompareJumpAtOp(FunctionCompiler& ctx, ls_type_kind kind, u32 lhs, u32 rhs, u32 op_offset) {
	const ls_op op = (ls_op)((u32)compareJumpTypeBase(kind) + op_offset);
	emitOp(ctx.code, op);
	emitFixedReg(ctx, lhs);
	emitFixedReg(ctx, rhs);
	const u32 operand_pos = (u32)ctx.code.size();
	emitI16(ctx.code, 0);
	return operand_pos;
}

static u32 emitCompareJumpAt(FunctionCompiler& ctx, ls_type_kind kind, u32 lhs, u32 rhs) {
	return emitCompareJumpAtOp(ctx, kind, lhs, rhs, 1u); // JGE
}

static bool isKnownNonEmptyRange(Expression& begin, Expression& end, ls_type_kind kind) {
	if (kind != LS_TYPE_I8 && kind != LS_TYPE_U8 && kind != LS_TYPE_I16 && kind != LS_TYPE_U16 && kind != LS_TYPE_I32 && kind != LS_TYPE_U32 && kind != LS_TYPE_I64 && kind != LS_TYPE_U64)
		return false;
	if (begin.kind != Expression::INT_LITERAL || end.kind != Expression::INT_LITERAL) return false;
	return static_cast<IntLiteralExpression&>(begin).value < static_cast<IntLiteralExpression&>(end).value;
}

// Emit a jump with a placeholder relative offset; returns the position of the
// offset operand for later patching.
static u32 emitJumpPlaceholder(FunctionCompiler& ctx, ls_op op) {
	emitOp(ctx.code, op);
	if (op == LS_OP_JZ_U8 || op == LS_OP_JNZ_U8) {
		emitTempReg(ctx, ctx.temp_top - 1u);
		ctx.temp_top -= 1u;
	}
	const u32 operand_pos = (u32)ctx.code.size();
	emitI16(ctx.code, 0);
	return operand_pos;
}

static ls_op negateCompareOp(ls_op op) {
	switch (op) {
		case LS_OP_EQ: return LS_OP_NE;
		case LS_OP_NE: return LS_OP_EQ;
		case LS_OP_LT: return LS_OP_GE;
		case LS_OP_LE: return LS_OP_GT;
		case LS_OP_GT: return LS_OP_LE;
		case LS_OP_GE: return LS_OP_LT;
		default: ASSERT(false); return op;
	}
}

// `a op b` == `b mirror(op) a`
static ls_op mirrorCompareOp(ls_op op) {
	switch (op) {
		case LS_OP_LT: return LS_OP_GT;
		case LS_OP_LE: return LS_OP_GE;
		case LS_OP_GT: return LS_OP_LT;
		case LS_OP_GE: return LS_OP_LE;
		default: return op;
	}
}

// Branch opcode testing a signed 32/64-bit register against zero, taken when
// `reg cmp 0` holds; 0 when no such opcode exists.
static ls_op zeroCompareJumpOp(ls_op cmp_op, ls_type_kind kind) {
	const bool is64 = kind == LS_TYPE_I64;
	switch (cmp_op) {
		case LS_OP_EQ: return is64 ? LS_OP_JZ_I64 : LS_OP_JZ_I32;
		case LS_OP_NE: return is64 ? LS_OP_JNZ_I64 : LS_OP_JNZ_I32;
		case LS_OP_GT: return is64 ? LS_OP_JGZ_I64 : LS_OP_JGZ_I32;
		case LS_OP_GE: return is64 ? LS_OP_JGEZ_I64 : LS_OP_JGEZ_I32;
		case LS_OP_LT: return is64 ? LS_OP_JLTZ_I64 : LS_OP_JLTZ_I32;
		case LS_OP_LE: return is64 ? LS_OP_JLEZ_I64 : LS_OP_JLEZ_I32;
		default: return (ls_op)0;
	}
}

static ls_op compareOpForToken(Token::Type op) {
	switch (op) {
		case Token::EQUAL_EQUAL: return LS_OP_EQ;
		case Token::BANG_EQUAL: return LS_OP_NE;
		case Token::LT: return LS_OP_LT;
		case Token::LT_EQUAL: return LS_OP_LE;
		case Token::GT: return LS_OP_GT;
		case Token::GT_EQUAL: return LS_OP_GE;
		default: return (ls_op)0;
	}
}

static u32 emitCompareJumpValues(FunctionCompiler& ctx, ls_op cmp_op, ls_type_kind kind, const Value& lhs, const Value& rhs, bool jump_if_true) {
	// Single-register test against zero for signed 32/64-bit operands.
	if (kind == LS_TYPE_I32 || kind == LS_TYPE_I64) {
		const Value* reg = nullptr;
		ls_op rel = cmp_op;
		if (rhs.kind == Value::CONST_INT && rhs.bits == 0u && lhs.kind == Value::REG) {
			reg = &lhs;
		} else if (lhs.kind == Value::CONST_INT && lhs.bits == 0u && rhs.kind == Value::REG) {
			reg = &rhs;
			rel = mirrorCompareOp(cmp_op);
		}
		if (reg) {
			if (!jump_if_true) rel = negateCompareOp(rel);
			const ls_op op = zeroCompareJumpOp(rel, kind);
			ASSERT(op != (ls_op)0);
			emitOp(ctx.code, op);
			emitFixedReg(ctx, reg->reg);
			if (reg->is_temp) ctx.temp_top = reg->reg;
			const u32 operand_pos = (u32)ctx.code.size();
			emitI16(ctx.code, 0);
			return operand_pos;
		}
	}
	bool swap_operands = false;
	const ls_op fused = compareJumpOp(cmp_op, kind, jump_if_true, swap_operands);
	if (fused != LS_OP_JUMP) {
		const u32 release_to = valueReleasePoint(ctx, lhs, rhs);
		u32 lreg = valueReg(ctx, lhs, kind);
		u32 rreg = valueReg(ctx, rhs, kind);
		if (swap_operands) {
			const u32 tmp = lreg;
			lreg = rreg;
			rreg = tmp;
		}
		emitOp(ctx.code, fused);
		emitFixedReg(ctx, lreg);
		emitFixedReg(ctx, rreg);
		ctx.temp_top = release_to;
		const u32 operand_pos = (u32)ctx.code.size();
		emitI16(ctx.code, 0);
		return operand_pos;
	}
	// No branch-compare opcode for this relation/kind: materialize the bool.
	Value cond = emitCompareOpValues(ctx, cmp_op, kind, lhs, rhs);
	emitOp(ctx.code, jump_if_true ? LS_OP_JNZ_U8 : LS_OP_JZ_U8);
	emitFixedReg(ctx, cond.reg);
	if (cond.is_temp) ctx.temp_top = cond.reg;
	const u32 operand_pos = (u32)ctx.code.size();
	emitI16(ctx.code, 0);
	return operand_pos;
}

// True when the null-comparison special form in compileBinary must handle the
// expression (nullable == null tests only the has_value byte).
static bool isNullableNullCheck(BinaryExpression& expr) {
	if (expr.op != Token::EQUAL_EQUAL && expr.op != Token::BANG_EQUAL) return false;
	return (expr.lhs && expr.lhs->kind == Expression::NULL_LITERAL) || (expr.rhs && expr.rhs->kind == Expression::NULL_LITERAL);
}

// Emit branches taken when `cond` evaluates to `jump_if_true`; control falls
// through otherwise. Appends the emitted jumps' operand positions to
// `out_jumps` (short-circuit chains produce several). Comparisons compile
// straight to branch-compare opcodes without materializing a boolean.
static void emitCondJumps(FunctionCompiler& ctx, Expression& cond, bool jump_if_true, ExpArray<u32>& out_jumps) {
	if (cond.kind == Expression::UNARY) {
		UnaryExpression& un = static_cast<UnaryExpression&>(cond);
		if (un.op == Token::NOT && !un.resolved_fn) {
			emitCondJumps(ctx, *un.expression, !jump_if_true, out_jumps);
			return;
		}
	}
	if (cond.kind == Expression::BINARY) {
		BinaryExpression& binary = static_cast<BinaryExpression&>(cond);
		if ((binary.op == Token::AND || binary.op == Token::OR) && !binary.resolved_fn) {
			const bool is_and = binary.op == Token::AND;
			if (is_and != jump_if_true) {
				// Either operand failing/succeeding decides: chain both to target.
				emitCondJumps(ctx, *binary.lhs, jump_if_true, out_jumps);
				emitCondJumps(ctx, *binary.rhs, jump_if_true, out_jumps);
			} else {
				// The lhs alone can only skip the rest of the chain.
				ExpArray<u32> skip_jumps(*ctx.bytecode->arena);
				emitCondJumps(ctx, *binary.lhs, !jump_if_true, skip_jumps);
				emitCondJumps(ctx, *binary.rhs, jump_if_true, out_jumps);
				for (u32 pos : skip_jumps) patchJumpRelative(ctx, pos, (u32)ctx.code.size());
			}
			return;
		}
		const ls_op cmp_op = compareOpForToken(binary.op);
		if (cmp_op != (ls_op)0 && !binary.resolved_fn && binary.lhs && binary.rhs && !isNullableNullCheck(binary)) {
			Value lv = compileValue(ctx, *binary.lhs, LS_TYPE_INVALID);
			Value rv = compileValue(ctx, *binary.rhs, isNumericKind(lv.type) ? lv.type : LS_TYPE_INVALID);
			ls_type_kind operand_kind = lv.type == LS_TYPE_INVALID ? rv.type : lv.type;
			if (isConstValue(lv) && rv.kind == Value::REG)
				operand_kind = rv.type;
			else if (isConstValue(lv) && isConstValue(rv) && isFloatKind(rv.type) && !isFloatKind(lv.type))
				operand_kind = rv.type;
			out_jumps.push(emitCompareJumpValues(ctx, cmp_op, operand_kind, lv, rv, jump_if_true));
			return;
		}
	}
	compileExpression(ctx, cond, LS_TYPE_BOOL);
	out_jumps.push(emitJumpPlaceholder(ctx, jump_if_true ? LS_OP_JNZ_U8 : LS_OP_JZ_U8));
}

// A condition whose operands are all frame-resident (locals or constants) can
// be re-emitted verbatim, e.g. as a loop's bottom-of-body exit test.
static bool isDeferrableOperand(Expression& expr) {
	switch (expr.kind) {
		case Expression::INT_LITERAL:
		case Expression::FLOAT_LITERAL:
		case Expression::BOOL_LITERAL: return true;
		case Expression::UNARY: {
			UnaryExpression& un = static_cast<UnaryExpression&>(expr);
			return !un.resolved_fn && un.op == Token::MINUS && (un.expression->kind == Expression::INT_LITERAL || un.expression->kind == Expression::FLOAT_LITERAL);
		}
		case Expression::IDENTIFIER: {
			IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
			return id.slot && id.slot->storage == StorageSlot::LOCAL;
		}
		default: return false;
	}
}

static bool isReemittableCondition(Expression& cond) {
	if (cond.kind != Expression::BINARY) return false;
	BinaryExpression& binary = static_cast<BinaryExpression&>(cond);
	if (binary.resolved_fn || compareOpForToken(binary.op) == (ls_op)0) return false;
	if (isNullableNullCheck(binary)) return false;
	return binary.lhs && binary.rhs && isDeferrableOperand(*binary.lhs) && isDeferrableOperand(*binary.rhs);
}

static void emitDeferredStatements(FunctionCompiler& ctx, u32 defer_mark, ls_type_kind return_kind, ls_string_view current_label) {
	for (i32 i = (i32)ctx.deferreds.size() - 1; i >= (i32)defer_mark; --i) {
		compileStatement(ctx, *ctx.deferreds[(u32)i], return_kind, current_label);
	}
}

static ls_type_kind compileBinary(FunctionCompiler& ctx, BinaryExpression& expr, ls_type_kind hint) {
	if (expr.op == Token::AND || expr.op == Token::OR) {
		// Value context: short-circuit straight to the constant result.
		ExpArray<u32> short_circuit_jumps(*ctx.bytecode->arena);
		emitCondJumps(ctx, *expr.lhs, expr.op == Token::OR, short_circuit_jumps);
		// The short-circuit result and the evaluated-rhs result share one register,
		// so both paths must produce it at the same temporary offset.
		const u32 result_top = ctx.temp_top;

		compileExpression(ctx, *expr.rhs, LS_TYPE_BOOL);
		const u32 end_jump = emitJumpPlaceholder(ctx, LS_OP_JUMP);

		for (u32 pos : short_circuit_jumps) patchJumpRelative(ctx, pos, (u32)ctx.code.size());
		ctx.temp_top = result_top;
		emitConst1(ctx, expr.op == Token::AND ? 0u : 1u);
		patchJumpRelative(ctx, end_jump, (u32)ctx.code.size());
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
			if (source.members[i] == member) {
				member_index = i;
				break;
			}
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

	// Null check: `nullable == null` or `nullable != null` - only check has_value offset.
	if (expr.op == Token::EQUAL_EQUAL || expr.op == Token::BANG_EQUAL) {
		Expression* nullable_side = nullptr;
		if (expr.rhs && expr.rhs->kind == Expression::NULL_LITERAL)
			nullable_side = expr.lhs;
		else if (expr.lhs && expr.lhs->kind == Expression::NULL_LITERAL)
			nullable_side = expr.rhs;
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
	Value lv = compileValue(ctx, *expr.lhs, lhs_hint);
	const ls_type_kind rhs_hint = isNumericKind(lv.type) ? lv.type : lhs_hint;
	Value rv = compileValue(ctx, *expr.rhs, rhs_hint);

	// Operating kind: a register operand's kind is fixed, deferred constants
	// adapt to it at materialization.
	ls_type_kind operand_kind = lv.type == LS_TYPE_INVALID ? rv.type : lv.type;
	if (isConstValue(lv) && rv.kind == Value::REG)
		operand_kind = rv.type;
	else if (isConstValue(lv) && isConstValue(rv) && isFloatKind(rv.type) && !isFloatKind(lv.type))
		operand_kind = rv.type;

	switch (expr.op) {
		case Token::PLUS:
		case Token::MINUS:
		case Token::STAR:
		case Token::SLASH:
		case Token::PERCENT: {
			const ls_op base = expr.op == Token::PLUS	 ? LS_OP_ADD_I8
							   : expr.op == Token::MINUS ? LS_OP_SUB_I8
							   : expr.op == Token::STAR	 ? LS_OP_MUL_I8
							   : expr.op == Token::SLASH ? LS_OP_DIV_I8
														 : LS_OP_MOD_I8;
			const ls_type_kind kind = numericKindForOp(operand_kind, rv.type);
			emitBinaryOpValues(ctx, base, kind, lv, rv);
			return kind;
		}
		case Token::EQUAL_EQUAL:
		case Token::BANG_EQUAL:
		case Token::LT:
		case Token::LT_EQUAL:
		case Token::GT:
		case Token::GT_EQUAL: {
			const ls_op cmp_op = expr.op == Token::EQUAL_EQUAL	? LS_OP_EQ
								 : expr.op == Token::BANG_EQUAL ? LS_OP_NE
								 : expr.op == Token::LT			? LS_OP_LT
								 : expr.op == Token::LT_EQUAL	? LS_OP_LE
								 : expr.op == Token::GT			? LS_OP_GT
																: LS_OP_GE;
			emitCompareOpValues(ctx, cmp_op, operand_kind, lv, rv);
			return LS_TYPE_BOOL;
		}
		default: return LS_TYPE_INVALID;
	}
}

static ls_type_kind compileTernary(FunctionCompiler& ctx, TernaryExpression& expr, ls_type_kind hint) {
	ExpArray<u32> false_jumps(*ctx.bytecode->arena);
	emitCondJumps(ctx, *expr.condition, false, false_jumps);
	const u32 result_top = ctx.temp_top;

	ls_type_kind true_kind = compileExpression(ctx, *expr.true_expr, hint);
	const u32 end_jump = emitJumpPlaceholder(ctx, LS_OP_JUMP);

	for (u32 pos : false_jumps) patchJumpRelative(ctx, pos, (u32)ctx.code.size());
	ctx.temp_top = result_top;
	ls_type_kind false_kind = compileExpression(ctx, *expr.false_expr, hint);
	patchJumpRelative(ctx, end_jump, (u32)ctx.code.size());

	// Both branches should produce the same type (ensured by type checker)
	ASSERT(true_kind == false_kind);
	return true_kind;
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
		if (equalStrings(id->name, makeStringView("length")) && expr.args.size() == 1 && expr.args[0]->resolved_type &&
			(expr.args[0]->resolved_type->kind == ResolvedType::ARRAY || expr.args[0]->resolved_type->kind == ResolvedType::SLICE)) {
			ResolvedType* arg_type = expr.args[0]->resolved_type;
			if (arg_type->kind == ResolvedType::ARRAY) {
				emitConst8(ctx, (u64) static_cast<ArrayResolvedType*>(arg_type)->size);
			} else {
				ASSERT(arg_type->kind == ResolvedType::SLICE);
				if (expr.args[0]->kind == Expression::IDENTIFIER) {
					IdentifierExpression& id = static_cast<IdentifierExpression&>(*expr.args[0]);
					if (id.slot && id.slot->storage == StorageSlot::LOCAL && id.slot->type->kind == ResolvedType::SLICE) {
						emitSliceLengthLocal(ctx, id.slot->offset);
					} else {
						compileExpression(ctx, *expr.args[0], LS_TYPE_SLICE);
						emitSliceLength(ctx);
					}
				} else {
					compileExpression(ctx, *expr.args[0], LS_TYPE_SLICE);
					emitSliceLength(ctx);
				}
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
										  ? static_cast<EnumResolvedType*>(static_cast<MetaType*>(base_rt)->inner)
										  : nullptr;
	if (enum_via_meta) {
		u64 enum_value = enumMemberValue(*enum_via_meta, member.name);
		ls_type_kind kind = (i64)enum_value >= -2147483648LL && (i64)enum_value <= 2147483647LL ? LS_TYPE_I32 : LS_TYPE_I64;
		emitIntegerConstant(ctx, kind, enum_value);
		return kind;
	}
	BracketExpression* bracket = nullptr;
	u32 element_size = 0;
	i32 slice_field_offset = 0;
	ResolvedType* slice_field_type = nullptr;
	if (getSliceMemberAccess(member, bracket, element_size, slice_field_offset, slice_field_type)) {
		SliceAccess a = compileSliceAccess(ctx, *bracket);
		a.is_field = true;
		a.field_offset = slice_field_offset;
		a.field_size = typeByteSize(*slice_field_type);
		emitSliceAccessLoad(ctx, a);
		return valueKindForType(*slice_field_type);
	}
	if (member.expression->kind == Expression::IDENTIFIER) {
		IdentifierExpression* base = static_cast<IdentifierExpression*>(member.expression);
		if (member.resolved_symbol && member.resolved_symbol->expression) {
			switch (member.resolved_symbol->expression->kind) {
				case Expression::FUNCTION: emitIntegerConstant(ctx, LS_TYPE_FUNCTION, member.resolved_fn->bytecode_index); return LS_TYPE_FUNCTION;
				case Expression::INT_LITERAL:
					emitIntegerConstant(ctx, valueKindForType(*member.resolved_symbol->resolved_type), static_cast<IntLiteralExpression*>(member.resolved_symbol->expression)->value);
					return valueKindForType(*member.resolved_symbol->resolved_type);
				case Expression::FLOAT_LITERAL: {
					FloatLiteralExpression* fl = static_cast<FloatLiteralExpression*>(member.resolved_symbol->expression);
					const ls_type_kind kind = valueKindForType(*member.resolved_symbol->resolved_type);
					if (kind == LS_TYPE_F32) {
						emitConst4(ctx, bitcastF32ToU32(static_cast<float>(fl->value)));
					} else {
						emitConst8(ctx, bitcastF64ToU64(fl->value));
					}
					return kind;
				}
				case Expression::BOOL_LITERAL: emitIntegerConstant(ctx, LS_TYPE_BOOL, static_cast<BoolLiteralExpression*>(member.resolved_symbol->expression)->value ? 1u : 0u); return LS_TYPE_BOOL;
				case Expression::STRING_LITERAL: {
					u32 string_index = 0;
					appendStringLiteral(*ctx.bytecode, static_cast<StringLiteralExpression*>(member.resolved_symbol->expression)->value, string_index);
					emitConstString(ctx, string_index);
					return LS_TYPE_STRING;
				}
				case Expression::NULL_LITERAL:
				case Expression::UNDEFINED: emitZeroBytes(ctx, typeByteSize(*member.resolved_symbol->resolved_type)); return valueKindForType(*member.resolved_symbol->resolved_type);
				default: break;
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
			} else if (value_type->kind == ResolvedType::UNION) {
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
	SourceScope source_scope(ctx.code, expr.token);
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
			} else {
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
			emitIntegerConstant(ctx, LS_TYPE_I32, (u64)(uintptr) static_cast<TypeLiteralExpression&>(expr).type);
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
		case Expression::TERNARY: return compileTernary(ctx, static_cast<TernaryExpression&>(expr), hint);
		case Expression::CAST: {
			CastExpression& cast = static_cast<CastExpression&>(expr);
			if (cast.expression->resolved_type->kind == ResolvedType::UNION && expr.resolved_type->kind == ResolvedType::NULLABLE) {
				UnionResolvedType& source = static_cast<UnionResolvedType&>(*cast.expression->resolved_type);
				NullableResolvedType& nullable = static_cast<NullableResolvedType&>(*expr.resolved_type);
				i32 member_index = -1;
				for (i32 i = 0; i < source.members.size(); ++i) {
					if (source.members[i] == nullable.inner) {
						member_index = i;
						break;
					}
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
			const ls_type_kind resolved_src_kind = toTypeKind(*cast.expression->resolved_type);
			if (isNumericKind(resolved_src_kind) && isNumericKind(dst_kind) && cast.expression->kind == Expression::IDENTIFIER) {
				IdentifierExpression& id = static_cast<IdentifierExpression&>(*cast.expression);
				if (id.slot && id.slot->storage == StorageSlot::LOCAL) {
					emitCastFromLocal(ctx, id.slot->offset, resolved_src_kind, dst_kind);
					return dst_kind;
				}
			}
			const ls_type_kind src_kind = compileExpression(ctx, *cast.expression, toTypeKind(*cast.expression->resolved_type));
			if (cast.expression->resolved_type->kind == ResolvedType::STRING && expr.resolved_type->kind == ResolvedType::CSTR) {
				emitStringToCStr(ctx);
				return dst_kind;
			}
			if (cast.expression->resolved_type->kind == ResolvedType::CSTR && expr.resolved_type->kind == ResolvedType::STRING) {
				emitCStrToString(ctx);
				return dst_kind;
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
			if (un.op == Token::MINUS && (un.expression->kind == Expression::INT_LITERAL || un.expression->kind == Expression::FLOAT_LITERAL)) {
				const ls_type_kind kind = un.expression->kind == Expression::INT_LITERAL ? toTypeKind(*un.expression->resolved_type) : defaultLiteralKind(*un.expression, hint);
				if (un.expression->kind == Expression::INT_LITERAL) {
					const u64 value = static_cast<IntLiteralExpression&>(*un.expression).value;
					if (kind == LS_TYPE_F32)
						emitConst4(ctx, bitcastF32ToU32(-(float)value));
					else if (kind == LS_TYPE_F64)
						emitConst8(ctx, bitcastF64ToU64(-(double)value));
					else
						emitIntegerConstant(ctx, kind, 0u - value);
					return kind;
				}
				if (kind == LS_TYPE_F32) {
					const float value = (float)static_cast<FloatLiteralExpression&>(*un.expression).value;
					emitConst4(ctx, bitcastF32ToU32(-value));
				} else {
					const double value = static_cast<FloatLiteralExpression&>(*un.expression).value;
					emitConst8(ctx, bitcastF64ToU64(-value));
				}
				return kind;
			}
			const ls_type_kind kind = compileExpression(ctx, *un.expression, hint);
			switch (un.op) {
				case Token::MINUS: {
					u32 index = numericKindIndex(kind);
					emitUnaryOp(ctx, (ls_op)((u32)LS_OP_NEG_I8 + index), typeKindByteSize(kind));
					return kind;
				}
				case Token::NOT: emitUnaryOp(ctx, LS_OP_NOT, 1u); return LS_TYPE_BOOL;
				default: ASSERT(false); return LS_TYPE_INVALID;
			}
		}
		case Expression::CALL: return compileCall(ctx, static_cast<CallExpression&>(expr), hint);
		case Expression::MEMBER: return compileMember(ctx, static_cast<MemberExpression&>(expr));
		case Expression::BRACKET: {
			BracketExpression& br = static_cast<BracketExpression&>(expr);
			if (br.resolved_type->kind == ResolvedType::FUNCTION) {
				FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(br.resolved_type);
				if (fn_type->decl) { // TODO when can this be null
					emitIntegerConstant(ctx, LS_TYPE_FUNCTION, fn_type->decl->bytecode_index);
					return LS_TYPE_FUNCTION;
				}
			}
			if (br.base->resolved_type->kind == ResolvedType::SLICE) {
				SliceAccess a = compileSliceAccess(ctx, br);
				emitSliceAccessLoad(ctx, a);
			} else {
				ArrayAccess a = compileArrayAccess(ctx, br);
				emitArrayAccessLoad(ctx, a);
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
					ResolvedType* field_type = i < st->field_types.size() ? st->field_types[i] : st->decl->fields[i].resolved_type;
					compileExpressionAsType(ctx, *lit.values[i], *field_type);
				}
			} else {
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
			if (kind == LS_TYPE_F32)
				emitConst4At(ctx, offset, bitcastF32ToU32((float)value));
			else if (kind == LS_TYPE_F64)
				emitConst8At(ctx, offset, bitcastF64ToU64((double)value));
			else
				emitIntegerConstantAt(ctx, offset, kind, value);
			return true;
		}
		case Expression::FLOAT_LITERAL: {
			if (!isFloatKind(kind)) return false;
			const double value = static_cast<FloatLiteralExpression&>(expr).value;
			if (kind == LS_TYPE_F32)
				emitConst4At(ctx, offset, bitcastF32ToU32((float)value));
			else
				emitConst8At(ctx, offset, bitcastF64ToU64(value));
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
		case Expression::UNARY: {
			UnaryExpression& unary = static_cast<UnaryExpression&>(expr);
			if (unary.op != Token::MINUS || !unary.expression) return false;
			if (unary.expression->kind == Expression::INT_LITERAL) {
				if (!isIntegerKind(kind) && !isFloatKind(kind)) return false;
				const u64 value = static_cast<IntLiteralExpression&>(*unary.expression).value;
				if (kind == LS_TYPE_F32)
					emitConst4At(ctx, offset, bitcastF32ToU32(-(float)value));
				else if (kind == LS_TYPE_F64)
					emitConst8At(ctx, offset, bitcastF64ToU64(-(double)value));
				else
					emitIntegerConstantAt(ctx, offset, kind, 0u - value);
				return true;
			}
			if (unary.expression->kind == Expression::FLOAT_LITERAL) {
				if (!isFloatKind(kind)) return false;
				const double value = static_cast<FloatLiteralExpression&>(*unary.expression).value;
				if (kind == LS_TYPE_F32)
					emitConst4At(ctx, offset, bitcastF32ToU32(-(float)value));
				else
					emitConst8At(ctx, offset, bitcastF64ToU64(-value));
				return true;
			}
			return false;
		}
		default: return false;
	}
}

static bool emitLocalLengthInitializer(FunctionCompiler& ctx, u32 offset, Expression& expr) {
	if (expr.kind != Expression::CALL) return false;
	CallExpression& call = static_cast<CallExpression&>(expr);
	if (call.args.size() != 1u || call.callee->kind != Expression::IDENTIFIER) return false;
	IdentifierExpression& callee = static_cast<IdentifierExpression&>(*call.callee);
	if (!equalStrings(callee.name, makeStringView("length"))) return false;
	Expression* arg = call.args[0];
	if (arg->kind != Expression::IDENTIFIER || !arg->resolved_type || arg->resolved_type->kind != ResolvedType::SLICE) return false;
	IdentifierExpression& id = static_cast<IdentifierExpression&>(*arg);
	if (!id.slot || id.slot->storage != StorageSlot::LOCAL || id.slot->type->kind != ResolvedType::SLICE) return false;
	emitSliceLengthToLocal(ctx, offset, id.slot->offset);
	return true;
}

// Numeric/comparison binary expressions can write their result straight into
// a frame destination. Returns false (emitting nothing) when the expression
// needs one of compileBinary's special forms.
static bool tryCompileBinaryIntoLocal(FunctionCompiler& ctx, BinaryExpression& expr, u32 dst, ls_type_kind kind) {
	if (expr.op == Token::AND || expr.op == Token::OR || expr.op == Token::IS || expr.resolved_fn) return false;
	if (!expr.lhs || !expr.rhs) return false;
	if (expr.lhs->kind == Expression::NULL_LITERAL || expr.rhs->kind == Expression::NULL_LITERAL) return false;
	bool is_compare = false;
	ls_op op = (ls_op)0;
	switch (expr.op) {
		case Token::PLUS: op = LS_OP_ADD_I8; break;
		case Token::MINUS: op = LS_OP_SUB_I8; break;
		case Token::STAR: op = LS_OP_MUL_I8; break;
		case Token::SLASH: op = LS_OP_DIV_I8; break;
		case Token::PERCENT: op = LS_OP_MOD_I8; break;
		case Token::EQUAL_EQUAL:
			op = LS_OP_EQ;
			is_compare = true;
			break;
		case Token::BANG_EQUAL:
			op = LS_OP_NE;
			is_compare = true;
			break;
		case Token::LT:
			op = LS_OP_LT;
			is_compare = true;
			break;
		case Token::LT_EQUAL:
			op = LS_OP_LE;
			is_compare = true;
			break;
		case Token::GT:
			op = LS_OP_GT;
			is_compare = true;
			break;
		case Token::GT_EQUAL:
			op = LS_OP_GE;
			is_compare = true;
			break;
		default: return false;
	}
	if (is_compare != (kind == LS_TYPE_BOOL)) return false;
	if (!is_compare && !isNumericKind(kind)) return false;

	Value lv = compileValue(ctx, *expr.lhs, is_compare ? LS_TYPE_INVALID : kind);
	Value rv = compileValue(ctx, *expr.rhs, isNumericKind(lv.type) ? lv.type : kind);
	ls_type_kind operand_kind = lv.type == LS_TYPE_INVALID ? rv.type : lv.type;
	if (isConstValue(lv) && rv.kind == Value::REG)
		operand_kind = rv.type;
	else if (isConstValue(lv) && isConstValue(rv) && isFloatKind(rv.type) && !isFloatKind(lv.type))
		operand_kind = rv.type;

	if (is_compare) {
		emitCompareOpValues(ctx, op, operand_kind, lv, rv, dst);
		return true;
	}
	const ls_type_kind op_kind = numericKindForOp(operand_kind, rv.type);
	if (op_kind == kind) {
		emitBinaryOpValues(ctx, op, kind, lv, rv, dst);
		return true;
	}
	// Result width differs from the destination; produce a temp and copy.
	emitBinaryOpValues(ctx, op, op_kind, lv, rv);
	emitStoreLocalBytes(ctx, dst, typeKindByteSize(op_kind));
	return true;
}

// Compile `expr` and place the result directly at frame offset `dst` (which
// holds a value of `type`), avoiding the compile-to-temp-then-copy detour of
// the generic path when the value can be produced in place.
static void compileExpressionIntoLocal(FunctionCompiler& ctx, Expression& expr, u32 dst, ResolvedType& type) {
	const ls_type_kind kind = valueKindForType(type);
	u32 byte_size = typeByteSize(type);
	if (byte_size == 0u) byte_size = 1u;
	if (emitLocalLiteralInitializer(ctx, dst, expr, kind)) return;
	if (expr.kind == Expression::IDENTIFIER && expr.resolved_type == &type) {
		IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
		if (id.slot && id.slot->storage == StorageSlot::LOCAL && id.slot->byte_size == byte_size) {
			emitOp(ctx.code, LS_OP_COPY);
			emitFixedReg(ctx, dst);
			emitFixedReg(ctx, id.slot->offset);
			emitU32(ctx.code, byte_size);
			return;
		}
	}
	if (expr.kind == Expression::BINARY && tryCompileBinaryIntoLocal(ctx, static_cast<BinaryExpression&>(expr), dst, kind)) return;
	const bool same_repr = expr.resolved_type && typeByteSize(*expr.resolved_type) == byte_size && valueKindForType(*expr.resolved_type) == kind;
	if (expr.kind == Expression::BRACKET && same_repr) {
		BracketExpression& br = static_cast<BracketExpression&>(expr);
		if (br.base->resolved_type && br.base->resolved_type->kind == ResolvedType::SLICE) {
			SliceAccess a = compileSliceAccess(ctx, br);
			emitSliceAccessLoad(ctx, a, dst);
			return;
		}
		if (br.base->resolved_type && br.base->resolved_type->kind == ResolvedType::ARRAY) {
			ArrayAccess a = compileArrayAccess(ctx, br);
			emitArrayAccessLoad(ctx, a, dst);
			return;
		}
	}
	if (expr.kind == Expression::MEMBER && same_repr) {
		MemberExpression& member = static_cast<MemberExpression&>(expr);
		BracketExpression* bracket = nullptr;
		u32 element_size = 0;
		i32 field_offset = 0;
		ResolvedType* field_type = nullptr;
		if (getSliceMemberAccess(member, bracket, element_size, field_offset, field_type)) {
			SliceAccess a = compileSliceAccess(ctx, *bracket);
			a.is_field = true;
			a.field_offset = field_offset;
			a.field_size = typeByteSize(*field_type);
			emitSliceAccessLoad(ctx, a, dst);
			return;
		}
	}
	compileExpressionAsType(ctx, expr, type);
	emitStoreLocalBytes(ctx, dst, byte_size);
}

static bool tryEmitDirectReturn(FunctionCompiler& ctx, Expression& expr) {
	if (!ctx.deferreds.empty()) return false;
	const ls_type_kind kind = valueKindForType(*ctx.return_type);
	if (expr.kind == Expression::BINARY && tryCompileBinaryIntoLocal(ctx, static_cast<BinaryExpression&>(expr), 0u, kind)) {
		emitReturnFromLocal(ctx, 0u);
		return true;
	}
	if (emitLocalLiteralInitializer(ctx, 0u, expr, kind)) {
		emitReturnFromLocal(ctx, 0u);
		return true;
	}
	return false;
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
			if (slot && slot->storage == StorageSlot::LOCAL && value_type) {
				compileExpressionIntoLocal(ctx, *assign.rhs, slot->offset, *value_type);
				return;
			}
			compileExpressionAsType(ctx, *assign.rhs, *value_type);
			emitStoreSlot(ctx, *slot);
			return;
		}
		if (slot && slot->storage == StorageSlot::LOCAL && !assign.resolved_op_fn && isNumericKind(value_kind)) {
			const ls_op group = numericOpGroupForAssign(assign.op);
			if (group != (ls_op)0) {
				Value rv = compileValue(ctx, *assign.rhs, value_kind);
				if (isIncrementCandidate(rv, assign.op) && emitIncrementLocal(ctx, slot->offset, value_kind, assign.op == Token::MINUS_EQUAL)) return;
				emitBinaryOpValues(ctx, group, value_kind, makeRegValue(slot->offset, value_kind, false), rv, slot->offset);
				return;
			}
		}

		emitLoadSlot(ctx, *slot);

		if (assign.resolved_op_fn) {
			const FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(assign.resolved_op_fn->resolved_type);
			compileExpressionAsType(ctx, *assign.rhs, *fn_type->param_types[1]);
			emitCallDirect(ctx, assign.resolved_op_fn->bytecode_index, callArgWindowSize(*fn_type), typeByteSize(*fn_type->return_type));
		} else {
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
		BracketExpression* bracket = nullptr;
		u32 element_size = 0;
		i32 slice_field_offset = 0;
		ResolvedType* slice_field_type = nullptr;
		if (getSliceMemberAccess(*member, bracket, element_size, slice_field_offset, slice_field_type)) {
			const u32 field_size = typeByteSize(*slice_field_type);
			const ls_type_kind value_kind = valueKindForType(*slice_field_type);
			SliceAccess a = compileSliceAccess(ctx, *bracket);
			a.is_field = true;
			a.field_offset = slice_field_offset;
			a.field_size = field_size;
			if (!isSideEffectFreeExpression(*assign.rhs)) {
				snapshotValue(ctx, a.index, sliceIndexKind(a));
				snapshotValue(ctx, a.slice, LS_TYPE_SLICE);
			}
			if (assign.op == Token::EQUAL) {
				Value v = compileValueAsType(ctx, *assign.rhs, *slice_field_type);
				emitSliceAccessStore(ctx, a, v, value_kind);
				return;
			}
			emitSliceAccessLoad(ctx, a, -1, true);
			emitCompoundValue(ctx, *assign.rhs, value_kind, assign.op, assign.resolved_op_fn);
			emitSliceAccessStore(ctx, a, makeRegValue(ctx.temp_top - field_size, value_kind, true), value_kind);
			return;
		}
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
		// Stack order for STORE_INDEXED is: base, index, value.
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
	SourceScope source_scope(ctx.code, st.token);
	switch (st.kind) {
		case Statement::VAR_DECL: {
			VarDeclStatement& var = static_cast<VarDeclStatement&>(st);
			ResolvedType* value_type = var.resolved_type;
			const ls_type_kind kind = valueKindForType(*value_type);
			const u32 offset = ctx.addLocal(value_type, kind, false, &var.slot);
			// Recorded after the initializer so scope_begin_offset is past this
			// statement's own bytecode: a breakpoint set on this exact line
			// (its code_offset equals where the initializer starts) must not
			// yet report this local, since it isn't holding a real value there.
			if (kind == LS_TYPE_I64 && emitLocalLengthInitializer(ctx, offset, *var.expression)) {
				ctx.debugLocal(var.name, var.slot);
				return;
			}
			if (var.expression->kind != Expression::UNDEFINED) compileExpressionIntoLocal(ctx, *var.expression, offset, *value_type);
			ctx.debugLocal(var.name, var.slot);
			return;
		}
		case Statement::ASSIGN: {
			AssignStatement& assign = static_cast<AssignStatement&>(st);
			compileAssign(ctx, assign);
			return;
		}
		case Statement::IF: {
			IfStatement& ifst = static_cast<IfStatement&>(st);
			ExpArray<u32> false_jumps(*ctx.bytecode->arena);
			emitCondJumps(ctx, *ifst.condition, false, false_jumps);
			compileStatement(ctx, *ifst.body, return_kind, current_label);
			if (ifst.else_branch) {
				const u32 jump_end_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP);
				for (u32 pos : false_jumps) patchJumpRelative(ctx, pos, (u32)ctx.code.size());
				compileStatement(ctx, *ifst.else_branch, return_kind, current_label);
				patchJumpRelative(ctx, jump_end_pos, (u32)ctx.code.size());
			} else {
				for (u32 pos : false_jumps) patchJumpRelative(ctx, pos, (u32)ctx.code.size());
			}
			return;
		}
		case Statement::WHILE: {
			WhileStatement& ws = static_cast<WhileStatement&>(st);

			const u32 condition_pos = (u32)ctx.code.size();
			const bool constant_true = ws.condition->kind == Expression::BOOL_LITERAL && static_cast<BoolLiteralExpression&>(*ws.condition).value;
			const bool retest_at_bottom = !constant_true && isReemittableCondition(*ws.condition);
			ExpArray<u32> exit_jumps(*ctx.bytecode->arena);
			if (!constant_true) emitCondJumps(ctx, *ws.condition, false, exit_jumps);
			const u32 body_pos = (u32)ctx.code.size();

			LoopBinding& loop = ctx.loops.emplace_back();
			loop.label = current_label;
			loop.defer_mark = (u32)ctx.deferreds.size();
			void* break_storage = ctx.bytecode->arena->allocate(ctx.bytecode->arena->user_data, sizeof(ExpArray<u32>), alignof(ExpArray<u32>));
			loop.break_jumps = ::new (break_storage) ExpArray<u32>(*ctx.bytecode->arena);
			void* continue_storage = ctx.bytecode->arena->allocate(ctx.bytecode->arena->user_data, sizeof(ExpArray<u32>), alignof(ExpArray<u32>));
			loop.continue_jumps = ::new (continue_storage) ExpArray<u32>(*ctx.bytecode->arena);

			compileStatement(ctx, *ws.body, return_kind, {});

			u32 continue_target_pos = condition_pos;
			if (retest_at_bottom) {
				// The condition reads only frame-resident operands, so re-test it
				// here and branch back while it holds instead of jumping to the
				// top-of-loop test.
				continue_target_pos = (u32)ctx.code.size();
				ExpArray<u32> back_jumps(*ctx.bytecode->arena);
				emitCondJumps(ctx, *ws.condition, true, back_jumps);
				for (u32 pos : back_jumps) patchJumpRelative(ctx, pos, body_pos);
			} else {
				const u32 jump_back_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP);
				patchJumpRelative(ctx, jump_back_pos, condition_pos);
			}

			const u32 loop_end = (u32)ctx.code.size();
			for (u32 pos : exit_jumps) patchJumpRelative(ctx, pos, loop_end);
			for (u32 break_pos : *loop.break_jumps) {
				patchJumpRelative(ctx, break_pos, loop_end);
			}
			for (u32 continue_pos : *loop.continue_jumps) {
				patchJumpRelative(ctx, continue_pos, continue_target_pos);
			}
			ctx.loops.pop_back();
			return;
		}
		case Statement::FOR: {
			ForStatement& fs = static_cast<ForStatement&>(st);
			ResolvedType* value_type = fs.begin->resolved_type;
			const ls_type_kind value_kind = valueKindForType(*value_type);
			const u32 byte_size = typeByteSize(*value_type) == 0u ? 1u : typeByteSize(*value_type);
			const auto isDirectRangeValue = [&](Expression& expr) {
				if (expr.kind == Expression::INT_LITERAL || expr.kind == Expression::FLOAT_LITERAL) return true;
				if (expr.kind != Expression::IDENTIFIER || expr.resolved_type != value_type) return false;
				IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
				return id.slot && id.slot->storage == StorageSlot::LOCAL && id.slot->byte_size == byte_size;
			};
			const bool direct_bounds = isDirectRangeValue(*fs.begin) && isDirectRangeValue(*fs.end);
			if (direct_bounds) {
				ctx.pushScope();
				const u32 loop_offset = ctx.addLocal(value_type, value_kind, true, &fs.slot);
				ctx.debugLocal(fs.loop_var, fs.slot);
				u32 end_offset = 0;
				bool end_snapshot = false;
				if (fs.end->kind == Expression::IDENTIFIER) {
					IdentifierExpression& end_id = static_cast<IdentifierExpression&>(*fs.end);
					if (end_id.slot && end_id.slot->storage == StorageSlot::LOCAL) {
						end_offset = ctx.addLocal(value_type, value_kind, true);
						end_snapshot = true;
					}
				}
				if (!end_snapshot) end_offset = ctx.addLocal(value_type, value_kind, true);
				const auto emitDirectRangeValue = [&](Expression& expr, u32 dst) {
					if (emitLocalLiteralInitializer(ctx, dst, expr, value_kind)) return;
					IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
					emitOp(ctx.code, LS_OP_COPY);
					emitFixedReg(ctx, dst);
					emitFixedReg(ctx, id.slot->offset);
					emitU32(ctx.code, byte_size);
				};
				emitDirectRangeValue(*fs.begin, loop_offset);
				emitDirectRangeValue(*fs.end, end_offset);
				ctx.temp_top = ctx.next_local_offset;

				const bool known_nonempty = isKnownNonEmptyRange(*fs.begin, *fs.end, value_kind);
				const bool post_test = true;
				const u32 condition_pos = (u32)ctx.code.size();
				const u32 jump_false_pos = known_nonempty ? 0u : emitCompareJumpAt(ctx, value_kind, loop_offset, end_offset);
				const u32 body_pos = (u32)ctx.code.size();
				LoopBinding& loop = ctx.loops.emplace_back();
				loop.label = current_label;
				loop.defer_mark = (u32)ctx.deferreds.size();
				void* break_storage = ctx.bytecode->arena->allocate(ctx.bytecode->arena->user_data, sizeof(ExpArray<u32>), alignof(ExpArray<u32>));
				loop.break_jumps = ::new (break_storage) ExpArray<u32>(*ctx.bytecode->arena);
				void* continue_storage = ctx.bytecode->arena->allocate(ctx.bytecode->arena->user_data, sizeof(ExpArray<u32>), alignof(ExpArray<u32>));
				loop.continue_jumps = ::new (continue_storage) ExpArray<u32>(*ctx.bytecode->arena);
				compileStatement(ctx, *fs.body, return_kind, {});
				const u32 increment_pos = (u32)ctx.code.size();
				emitIncrementOrAddOne(ctx, loop_offset, value_kind);
				if (post_test) {
					const u32 jump_back_pos = emitCompareJumpAtOp(ctx, value_kind, loop_offset, end_offset, 3u); // JLT
					patchJumpRelative(ctx, jump_back_pos, body_pos);
				} else {
					const u32 jump_back_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP);
					patchJumpRelative(ctx, jump_back_pos, condition_pos);
				}
				const u32 loop_end = (u32)ctx.code.size();
				if (!known_nonempty) patchJumpRelative(ctx, jump_false_pos, loop_end);
				for (u32 break_pos : *loop.break_jumps) patchJumpRelative(ctx, break_pos, loop_end);
				for (u32 continue_pos : *loop.continue_jumps) patchJumpRelative(ctx, continue_pos, increment_pos);
				ctx.loops.pop_back();
				ctx.popScope(return_kind, current_label);
				return;
			}
			const bool begin_is_literal = fs.begin->kind == Expression::INT_LITERAL || fs.begin->kind == Expression::FLOAT_LITERAL;
			if (!begin_is_literal) compileExpression(ctx, *fs.begin, value_kind);
			compileExpression(ctx, *fs.end, value_kind);
			const u32 range_value_top = ctx.temp_top;

			ctx.pushScope();
			const u32 loop_offset = ctx.addLocal(value_type, value_kind, true, &fs.slot);
			ctx.debugLocal(fs.loop_var, fs.slot);
			const u32 end_offset = range_value_top - byte_size;
			ctx.temp_top = range_value_top;
			if (!begin_is_literal) {
				ctx.temp_top -= byte_size;
				emitStoreLocalBytes(ctx, loop_offset, byte_size, false);
			} else if (!emitLocalLiteralInitializer(ctx, loop_offset, *fs.begin, value_kind))
				ASSERT(false);
			ctx.temp_top = ctx.next_local_offset;

			const u32 condition_pos = (u32)ctx.code.size();
			const u32 jump_false_pos = emitCompareJumpAt(ctx, value_kind, loop_offset, end_offset);
			const u32 body_pos = (u32)ctx.code.size();

			LoopBinding& loop = ctx.loops.emplace_back();
			loop.label = current_label;
			loop.defer_mark = (u32)ctx.deferreds.size();
			void* break_storage = ctx.bytecode->arena->allocate(ctx.bytecode->arena->user_data, sizeof(ExpArray<u32>), alignof(ExpArray<u32>));
			loop.break_jumps = ::new (break_storage) ExpArray<u32>(*ctx.bytecode->arena);
			void* continue_storage = ctx.bytecode->arena->allocate(ctx.bytecode->arena->user_data, sizeof(ExpArray<u32>), alignof(ExpArray<u32>));
			loop.continue_jumps = ::new (continue_storage) ExpArray<u32>(*ctx.bytecode->arena);

			compileStatement(ctx, *fs.body, return_kind, {});

			const u32 increment_pos = (u32)ctx.code.size();
			emitIncrementOrAddOne(ctx, loop_offset, value_kind);

			const u32 jump_back_pos = emitCompareJumpAtOp(ctx, value_kind, loop_offset, end_offset, 3u); // JLT
			patchJumpRelative(ctx, jump_back_pos, body_pos);

			const u32 loop_end = (u32)ctx.code.size();
			patchJumpRelative(ctx, jump_false_pos, loop_end);
			for (u32 break_pos : *loop.break_jumps) {
				patchJumpRelative(ctx, break_pos, loop_end);
			}
			for (u32 continue_pos : *loop.continue_jumps) {
				patchJumpRelative(ctx, continue_pos, increment_pos);
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
							if (subject_union.members[i] == member) {
								member_index = i;
								break;
							}
						}
						ASSERT(member_index >= 0);
						// The union tag is the first 4 bytes of the subject local.
						arm_body_jumps.push(
							emitCompareJumpValues(ctx, LS_OP_EQ, LS_TYPE_I32, makeRegValue(subject_offset, LS_TYPE_I32, false), makeConstIntValue((u64)member_index, LS_TYPE_I32), true));
					}

					const u32 skip_jump = emitJumpPlaceholder(ctx, LS_OP_JUMP);
					const u32 body_start = (u32)ctx.code.size();
					for (u32 jump : arm_body_jumps) patchJumpRelative(ctx, jump, body_start);
					compileStatement(ctx, *arm.body, return_kind, current_label);
					match_end_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
					patchJumpRelative(ctx, skip_jump, (u32)ctx.code.size());
				}
				const u32 end = (u32)ctx.code.size();
				for (u32 jump : match_end_jumps) patchJumpRelative(ctx, jump, end);
				return;
			}

			ExpArray<u32> match_end_jumps(*ctx.bytecode->arena);
			ExpArray<u32> pending_false_jumps(*ctx.bytecode->arena);
			ExpArray<u32> arm_body_jumps(*ctx.bytecode->arena);

			for (MatchArm& arm : ms.arms) {
				arm_body_jumps.clear();

				if (arm.is_fallback) {
					for (u32 false_jump : pending_false_jumps) patchJumpRelative(ctx, false_jump, (u32)ctx.code.size());
					pending_false_jumps.clear();
					compileStatement(ctx, *arm.body, return_kind, current_label);
					match_end_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
					continue;
				}

				for (MatchPattern& pattern : arm.patterns) {
					for (u32 false_jump : pending_false_jumps) patchJumpRelative(ctx, false_jump, (u32)ctx.code.size());
					pending_false_jumps.clear();

					const Value subject = makeRegValue(subject_offset, subject_kind, false);
					if (pattern.end) {
						Value begin = compileValue(ctx, *pattern.begin, subject_kind);
						pending_false_jumps.push(emitCompareJumpValues(ctx, LS_OP_GE, subject_kind, subject, begin, false));
						Value end = compileValue(ctx, *pattern.end, subject_kind);
						pending_false_jumps.push(emitCompareJumpValues(ctx, LS_OP_LE, subject_kind, subject, end, false));
						arm_body_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
					} else {
						Value pat = compileValue(ctx, *pattern.begin, subject_kind);
						arm_body_jumps.push(emitCompareJumpValues(ctx, LS_OP_EQ, subject_kind, subject, pat, true));
					}
				}

				const u32 skip_jump_pos = emitJumpPlaceholder(ctx, LS_OP_JUMP);
				const u32 body_start = (u32)ctx.code.size();
				for (u32 jump_pos : arm_body_jumps) patchJumpRelative(ctx, jump_pos, body_start);
				for (u32 false_jump : pending_false_jumps) patchJumpRelative(ctx, false_jump, skip_jump_pos - 1u);
				pending_false_jumps.clear();
				compileStatement(ctx, *arm.body, return_kind, current_label);
				match_end_jumps.push(emitJumpPlaceholder(ctx, LS_OP_JUMP));
				const u32 arm_end_pos = (u32)ctx.code.size();
				patchJumpRelative(ctx, skip_jump_pos, arm_end_pos);
			}

			const u32 end_pos = (u32)ctx.code.size();
			for (u32 jump_pos : match_end_jumps) patchJumpRelative(ctx, jump_pos, end_pos);
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
			const ls_string_view next_label = label.statement->kind == Statement::WHILE || label.statement->kind == Statement::FOR ? label.name : current_label;
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
				if (tryEmitDirectLocalReturn(ctx, *ret.expression)) return;
				if (tryEmitDirectReturn(ctx, *ret.expression)) return;
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
		case Expression::BOOL_LITERAL: out_expr = ret->expression; return true;
		default: return false;
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

static bool compileFunctionBytecode(ls_bytecode* bytecode, TypeInfoBuilder* type_builder, FunctionExpression* fn, FunctionResolvedType* fn_type, ls_string_view name, bool is_builtin_native) {
	ls_arena* arena = bytecode->arena;
	ls_function_bc* out = appendFunction(*bytecode);
	if (!out) return false;

	ls_function_bc& function = *out;
	function.name = copyStringViewToArena(*arena, name);
	function.kind = fn->is_extern ? LS_FUNCTION_NATIVE : LS_FUNCTION_SCRIPT;
	function.is_builtin_native = is_builtin_native;
	function.param_size = 0;
	ResolvedType* return_type = fn_type->return_type;
	function.return_kind = toTypeKind(*return_type);
	// Calls move raw offsets, so aggregate return metadata must describe the
	// representation width rather than assuming every value is one offset.
	function.return_size = typeByteSize(*return_type);
	function.frame_size = 0;
	function.code = nullptr;
	function.code_size = 0u;
	function.source_map = nullptr;
	function.source_map_count = 0u;
	function.locals = nullptr;
	function.local_count = 0u;

	if (fn->is_extern) {
		function.param_size = computeParamSize(fn->params);
		return true;
	}
	if (!fn->body || fn->body->kind != Statement::BLOCK) return false;

	BlockStatement* body = static_cast<BlockStatement*>(fn->body);
	Expression* literal = nullptr;
	if (function.return_size == 1u && isSimpleReturnLiteral(*body, literal)) {
		FunctionCompiler ctx(bytecode, function);
		ctx.type_builder = type_builder;
		ReturnStatement* ret = static_cast<ReturnStatement*>(body->statements[0]);
		SourceScope source_scope(ctx.code, ret->token);
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
			ctx.debugLocal(param.name, slot);
		}
		ctx.next_local_offset = function.param_size;
		ctx.temp_top = function.param_size;
		ctx.frame_high_water = function.param_size;
		switch (literal->kind) {
			case Expression::INT_LITERAL: emitIntegerConstant(ctx, function.return_kind, static_cast<IntLiteralExpression*>(literal)->value); break;
			case Expression::BOOL_LITERAL: emitIntegerConstant(ctx, LS_TYPE_BOOL, static_cast<BoolLiteralExpression*>(literal)->value ? 1u : 0u); break;
			default:
				// A 1-byte return excludes floats, so no other literal kind reaches here.
				return false;
		}
		if (ctx.failed) return false;
		emitReturn(ctx);
		finalizeFunctionCode(ctx, function, *arena);
		return true;
	}

	FunctionCompiler ctx(bytecode, function);
	ctx.type_builder = type_builder;
	SourceScope source_scope(ctx.code, fn->token);
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
		ctx.debugLocal(param.name, slot);
	}
	ctx.temp_top = function.param_size;
	ctx.frame_high_water = function.param_size;
	for (Statement* st : body->statements) {
		compileStatement(ctx, *st, function.return_kind, {});
	}
	if (ctx.failed) return false;
	// Every code path in a non-void function is guaranteed by checkFunctionBody
	// to already end in an explicit `return`, so this is unreachable for those.
	// For void functions that fall off the end without one, this is the only
	// return emitted; it keeps the bytecode self-terminating so the runtime's
	// interpreter loop never needs to fall through past the end of fn->code.
	emitReturn(ctx);
	finalizeFunctionCode(ctx, function, *arena);
	return true;
}

ls_bytecode* ls_bytecode_compile(ls_module* module, ls_host* host) {
	if (!module || !host || !host->arena.allocate) return nullptr;

	ls_bytecode* bytecode = static_cast<ls_bytecode*>(std::calloc(1, sizeof(ls_bytecode)));
	if (!bytecode) return nullptr;
	bytecode->host = host;
	bytecode->arena = &host->arena;

	ASSERT(bytecode->arena);
	ls_arena* arena = bytecode->arena;

	TypeInfoBuilder type_builder(*arena, bytecode);

	ExpArray<ls_bytecode_global_debug_entry> global_debug(*arena);
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

			ls_bytecode_global_debug_entry debug_entry;
			debug_entry.name = sym.name;
			debug_entry.offset = sym.slot.offset;
			debug_entry.byte_size = sym.slot.byte_size;
			debug_entry.kind = sym.slot.kind;
			debug_entry.type_index = type_builder.resolve(sym.resolved_type);
			global_debug.push_back(debug_entry);
		}
	}
	bytecode->global_debug_count = (u32)global_debug.size();
	if (bytecode->global_debug_count > 0u) {
		bytecode->global_debug = static_cast<ls_bytecode_global_debug_entry*>(
			arena->allocate(arena->user_data, sizeof(ls_bytecode_global_debug_entry) * bytecode->global_debug_count, alignof(ls_bytecode_global_debug_entry)));
		for (u32 i = 0; i < bytecode->global_debug_count; ++i) {
			bytecode->global_debug[i] = global_debug[(i32)i];
			bytecode->global_debug[i].name = copyStringViewToArena(*arena, bytecode->global_debug[i].name);
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
			if (!compileFunctionBytecode(bytecode, &type_builder,
					fn,
					static_cast<FunctionResolvedType*>(fn->resolved_type),
					sym.name,
					fn->is_extern && (equalStrings(unit.path, makeStringView("std:math")) || equalStrings(unit.path, makeStringView("std:mem"))))) {
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
		function.code = nullptr;
		function.code_size = 0u;
		function.source_map = nullptr;
		function.source_map_count = 0u;
		function.locals = nullptr;
		function.local_count = 0u;

		FunctionCompiler ctx(bytecode, function);
		ctx.type_builder = &type_builder;
		for (Unit& unit : module->units) {
			for (Symbol& sym : unit.symbols) {
				if (sym.slot.storage != StorageSlot::GLOBAL) continue;
				if (sym.expression->kind == Expression::UNDEFINED) continue;
				compileExpressionAsType(ctx, *sym.expression, *sym.resolved_type);
				emitStoreGlobalBytes(ctx, sym.slot.offset, sym.slot.byte_size);
			}
		}
		emitReturn(ctx);
		if (ctx.failed) {
			ls_bytecode_destroy(bytecode);
			return nullptr;
		}
		finalizeFunctionCode(ctx, function, *arena);
		bytecode->has_global_init = true;
	}

	if (!packFunctionCode(*bytecode, *arena)) {
		ls_bytecode_destroy(bytecode);
		return nullptr;
	}

	return bytecode;
}

void ls_bytecode_destroy(ls_bytecode* bytecode) {
	if (!bytecode) return;
	std::free(bytecode->breakpoints);
	std::free(bytecode);
}
