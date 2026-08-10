#pragma once

#include "capi.h"
#include "exparray.h"

struct ResolvedType;
struct FunctionExpression;
struct ls_module;
struct ls_host;
struct ls_bytecode;

// IR references are IDs rather than pointers so instructions can be moved or
// compacted by later passes.
using LsIrValue = u32;
using LsIrBlock = u32;
using LsIrFunction = u32;
using LsIrSourceLoc = u32;

static constexpr LsIrValue LS_IR_INVALID_VALUE = 0xffffffffu;
static constexpr LsIrBlock LS_IR_INVALID_BLOCK = 0xffffffffu;
static constexpr LsIrSourceLoc LS_IR_INVALID_SOURCE_LOC = 0xffffffffu;

enum LsIrOpKind {
	LS_IR_OP_INVALID,
	LS_IR_OP_LOAD_CONST,
	LS_IR_OP_COPY,
	LS_IR_OP_GLOBAL_LOAD,
	LS_IR_OP_GLOBAL_STORE,
	LS_IR_OP_LOCAL_REF,
	LS_IR_OP_LOCAL_LOAD,
	LS_IR_OP_LOCAL_STORE,
	LS_IR_OP_GLOBAL_REF,
	LS_IR_OP_LOAD_INDEXED,
	LS_IR_OP_STORE_INDEXED,
	LS_IR_OP_COPY_AT_LOCAL_I32,
	LS_IR_OP_REF_INDEXED,
	LS_IR_OP_BOUNDS_CHECK,
	LS_IR_OP_SLICE,
	LS_IR_OP_MAKE_SLICE,
	LS_IR_OP_SLICE_LOAD,
	LS_IR_OP_SLICE_STORE,
	LS_IR_OP_SLICE_LOAD_AT,
	LS_IR_OP_SLICE_STORE_AT,
	LS_IR_OP_SLICE_REF,
	LS_IR_OP_SLICE_LENGTH,
	LS_IR_OP_SLICE_EQ,
	LS_IR_OP_ADD,
	LS_IR_OP_SUB,
	LS_IR_OP_MUL,
	LS_IR_OP_DIV,
	LS_IR_OP_MOD,
	LS_IR_OP_INC,
	LS_IR_OP_DEC,
	LS_IR_OP_NEG,
	LS_IR_OP_NOT,
	LS_IR_OP_COMPARE,
	LS_IR_OP_JUMP,
	LS_IR_OP_CONDITIONAL_JUMP,
	LS_IR_OP_CALL_DIRECT,
	LS_IR_OP_CALL_NATIVE,
	LS_IR_OP_CALL_INDIRECT,
	LS_IR_OP_CAST,
	LS_IR_OP_RETURN,
};

enum LsIrCompareOp : u8 {
	LS_IR_COMPARE_EQ,
	LS_IR_COMPARE_NE,
	LS_IR_COMPARE_LT,
	LS_IR_COMPARE_LE,
	LS_IR_COMPARE_GT,
	LS_IR_COMPARE_GE,
};

struct LsIrOp {
	LsIrOpKind kind = LS_IR_OP_INVALID;
	LsIrSourceLoc src_loc = LS_IR_INVALID_SOURCE_LOC;

	explicit LsIrOp(LsIrOpKind kind)
		: kind(kind) {}
};

// LS_OP_LOAD_CONST_*
struct LsOpLoadConst : LsIrOp {
	LsOpLoadConst()
		: LsIrOp(LS_IR_OP_LOAD_CONST) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	u64 value = 0;
};

// LS_OP_COPY
struct LsOpCopy : LsIrOp {
	LsOpCopy()
		: LsIrOp(LS_IR_OP_COPY) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	LsIrValue source = LS_IR_INVALID_VALUE;
};

// LS_OP_GLOBAL_LOAD
struct LsOpGlobalLoad : LsIrOp {
	LsOpGlobalLoad()
		: LsIrOp(LS_IR_OP_GLOBAL_LOAD) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	u32 offset = 0;
};

// LS_OP_GLOBAL_STORE
struct LsOpGlobalStore : LsIrOp {
	LsOpGlobalStore()
		: LsIrOp(LS_IR_OP_GLOBAL_STORE) {}
	ResolvedType* type = nullptr;
	LsIrValue source = LS_IR_INVALID_VALUE;
	u32 offset = 0;
};

// LS_OP_LOCAL_REF
struct LsOpLocalRef : LsIrOp {
	LsOpLocalRef()
		: LsIrOp(LS_IR_OP_LOCAL_REF) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	u32 offset = 0;
};

// LS_OP_COPY from a local frame slot
struct LsOpLocalLoad : LsIrOp {
	LsOpLocalLoad()
		: LsIrOp(LS_IR_OP_LOCAL_LOAD) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	u32 offset = 0;
};

// LS_OP_COPY into a local frame slot
struct LsOpLocalStore : LsIrOp {
	LsOpLocalStore()
		: LsIrOp(LS_IR_OP_LOCAL_STORE) {}
	ResolvedType* type = nullptr;
	LsIrValue source = LS_IR_INVALID_VALUE;
	u32 offset = 0;
};

// LS_OP_GLOBAL_REF
struct LsOpGlobalRef : LsIrOp {
	LsOpGlobalRef()
		: LsIrOp(LS_IR_OP_GLOBAL_REF) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	u32 offset = 0;
};

// LS_OP_LOAD_INDEXED
struct LsOpLoadIndexed : LsIrOp {
	LsOpLoadIndexed()
		: LsIrOp(LS_IR_OP_LOAD_INDEXED) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	LsIrValue base = LS_IR_INVALID_VALUE;
	LsIrValue index = LS_IR_INVALID_VALUE;
	u32 base_offset = LS_IR_INVALID_VALUE;
	u32 scale = 0;
	u32 offset = 0;
	u32 length = 0;
};

// LS_OP_STORE_INDEXED
struct LsOpStoreIndexed : LsIrOp {
	LsOpStoreIndexed()
		: LsIrOp(LS_IR_OP_STORE_INDEXED) {}
	ResolvedType* type = nullptr;
	LsIrValue base = LS_IR_INVALID_VALUE;
	LsIrValue index = LS_IR_INVALID_VALUE;
	LsIrValue source = LS_IR_INVALID_VALUE;
	u32 base_offset = LS_IR_INVALID_VALUE;
	u32 scale = 0;
	u32 offset = 0;
	u32 length = 0;
};

// LS_OP_LOAD_INDEXED_LOCAL_I32
struct LsOpLoadIndexedLocalI32 : LsOpLoadIndexed {
	LsOpLoadIndexedLocalI32()
		: LsOpLoadIndexed() {}
};
// LS_OP_STORE_INDEXED_LOCAL_I32
struct LsOpStoreIndexedLocalI32 : LsOpStoreIndexed {
	LsOpStoreIndexedLocalI32()
		: LsOpStoreIndexed() {}
};

// LS_OP_COPY_AT_LOCAL_I32
struct LsOpCopyAtLocalI32 : LsIrOp {
	LsOpCopyAtLocalI32()
		: LsIrOp(LS_IR_OP_COPY_AT_LOCAL_I32) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	LsIrValue base = LS_IR_INVALID_VALUE;
	LsIrValue index = LS_IR_INVALID_VALUE;
	u32 scale = 0;
	u32 offset = 0;
};

// LS_OP_REF_INDEXED
struct LsOpRefIndexed : LsIrOp {
	LsOpRefIndexed()
		: LsIrOp(LS_IR_OP_REF_INDEXED) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	LsIrValue base = LS_IR_INVALID_VALUE;
	LsIrValue index = LS_IR_INVALID_VALUE;
	u32 scale = 0;
	u32 offset = 0;
};

// LS_OP_BOUNDS_CHECK
struct LsOpBoundsCheck : LsIrOp {
	LsOpBoundsCheck()
		: LsIrOp(LS_IR_OP_BOUNDS_CHECK) {}
	LsIrValue index = LS_IR_INVALID_VALUE;
	u32 length = 0;
};

// LS_OP_SLICE
struct LsOpSlice : LsIrOp {
	LsOpSlice()
		: LsIrOp(LS_IR_OP_SLICE) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	LsIrValue source = LS_IR_INVALID_VALUE;
	LsIrValue begin = LS_IR_INVALID_VALUE;
	LsIrValue end = LS_IR_INVALID_VALUE;
};

// LS_OP_LOCAL_REF followed by LOAD_CONST_8
struct LsOpMakeSlice : LsIrOp {
	LsOpMakeSlice()
		: LsIrOp(LS_IR_OP_MAKE_SLICE) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	u32 base_offset = 0;
	u64 length = 0;
};

// LS_OP_SLICE_LOAD_LOCAL, LS_OP_SLICE_LOAD_LOCAL_I32
struct LsOpSliceLoad : LsIrOp {
	LsOpSliceLoad()
		: LsIrOp(LS_IR_OP_SLICE_LOAD) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	LsIrValue slice = LS_IR_INVALID_VALUE;
	LsIrValue index = LS_IR_INVALID_VALUE;
	u32 slice_offset = LS_IR_INVALID_VALUE;
	u32 element_size = 0;
};

// LS_OP_SLICE_STORE_LOCAL, LS_OP_SLICE_STORE_LOCAL_I32
struct LsOpSliceStore : LsIrOp {
	LsOpSliceStore()
		: LsIrOp(LS_IR_OP_SLICE_STORE) {}
	ResolvedType* type = nullptr;
	LsIrValue slice = LS_IR_INVALID_VALUE;
	LsIrValue index = LS_IR_INVALID_VALUE;
	LsIrValue source = LS_IR_INVALID_VALUE;
	u32 slice_offset = LS_IR_INVALID_VALUE;
	u32 element_size = 0;
};

// LS_OP_SLICE_LOAD_LOCAL
struct LsOpSliceLoadLocal : LsOpSliceLoad {
	LsOpSliceLoadLocal()
		: LsOpSliceLoad() {}
};
// LS_OP_SLICE_STORE_LOCAL
struct LsOpSliceStoreLocal : LsOpSliceStore {
	LsOpSliceStoreLocal()
		: LsOpSliceStore() {}
};
// LS_OP_SLICE_LOAD_LOCAL_I32
struct LsOpSliceLoadLocalI32 : LsOpSliceLoad {
	LsOpSliceLoadLocalI32()
		: LsOpSliceLoad() {}
};
// LS_OP_SLICE_STORE_LOCAL_I32
struct LsOpSliceStoreLocalI32 : LsOpSliceStore {
	LsOpSliceStoreLocalI32()
		: LsOpSliceStore() {}
};

// LS_OP_SLICE_LOAD_AT_LOCAL, LS_OP_SLICE_LOAD_AT_LOCAL_I32
struct LsOpSliceLoadAt : LsIrOp {
	LsOpSliceLoadAt()
		: LsIrOp(LS_IR_OP_SLICE_LOAD_AT) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	LsIrValue slice = LS_IR_INVALID_VALUE;
	LsIrValue index = LS_IR_INVALID_VALUE;
	u32 element_offset = 0;
	u32 element_size = 0;
};

// LS_OP_SLICE_STORE_AT_LOCAL, LS_OP_SLICE_STORE_AT_LOCAL_I32
struct LsOpSliceStoreAt : LsIrOp {
	LsOpSliceStoreAt()
		: LsIrOp(LS_IR_OP_SLICE_STORE_AT) {}
	ResolvedType* type = nullptr;
	LsIrValue slice = LS_IR_INVALID_VALUE;
	LsIrValue index = LS_IR_INVALID_VALUE;
	LsIrValue source = LS_IR_INVALID_VALUE;
	u32 element_offset = 0;
	u32 element_size = 0;
};

// LS_OP_SLICE_LOAD_AT_LOCAL
struct LsOpSliceLoadAtLocal : LsOpSliceLoadAt {
	LsOpSliceLoadAtLocal()
		: LsOpSliceLoadAt() {}
};
// LS_OP_SLICE_STORE_AT_LOCAL
struct LsOpSliceStoreAtLocal : LsOpSliceStoreAt {
	LsOpSliceStoreAtLocal()
		: LsOpSliceStoreAt() {}
};
// LS_OP_SLICE_LOAD_AT_LOCAL_I32
struct LsOpSliceLoadAtLocalI32 : LsOpSliceLoadAt {
	LsOpSliceLoadAtLocalI32()
		: LsOpSliceLoadAt() {}
};
// LS_OP_SLICE_STORE_AT_LOCAL_I32
struct LsOpSliceStoreAtLocalI32 : LsOpSliceStoreAt {
	LsOpSliceStoreAtLocalI32()
		: LsOpSliceStoreAt() {}
};

// LS_OP_SLICE_REF
struct LsOpSliceRef : LsIrOp {
	LsOpSliceRef()
		: LsIrOp(LS_IR_OP_SLICE_REF) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	LsIrValue slice = LS_IR_INVALID_VALUE;
	LsIrValue index = LS_IR_INVALID_VALUE;
};

// LS_OP_SLICE_LENGTH
struct LsOpSliceLength : LsIrOp {
	LsOpSliceLength()
		: LsIrOp(LS_IR_OP_SLICE_LENGTH) {}
	LsIrValue result = LS_IR_INVALID_VALUE;
	LsIrValue slice = LS_IR_INVALID_VALUE;
	u32 slice_offset = LS_IR_INVALID_VALUE;
};

// LS_OP_SLICE_EQ
struct LsOpSliceEq : LsIrOp {
	LsOpSliceEq()
		: LsIrOp(LS_IR_OP_SLICE_EQ) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	LsIrValue lhs = LS_IR_INVALID_VALUE;
	LsIrValue rhs = LS_IR_INVALID_VALUE;
};

// LS_OP_ADD_*
struct LsOpAdd : LsIrOp {
	LsOpAdd()
		: LsIrOp(LS_IR_OP_ADD) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	LsIrValue lhs = LS_IR_INVALID_VALUE;
	LsIrValue rhs = LS_IR_INVALID_VALUE;
};

// LS_OP_SUB_*
struct LsOpSub : LsOpAdd {
	LsOpSub()
		: LsOpAdd() {
		kind = LS_IR_OP_SUB;
	}
};
// LS_OP_MUL_*
struct LsOpMul : LsOpAdd {
	LsOpMul()
		: LsOpAdd() {
		kind = LS_IR_OP_MUL;
	}
};
// LS_OP_DIV_*
struct LsOpDiv : LsOpAdd {
	LsOpDiv()
		: LsOpAdd() {
		kind = LS_IR_OP_DIV;
	}
};
// LS_OP_MOD_*
struct LsOpMod : LsOpAdd {
	LsOpMod()
		: LsOpAdd() {
		kind = LS_IR_OP_MOD;
	}
};

// LS_OP_INC_I32, LS_OP_INC_I64
struct LsOpInc : LsIrOp {
	LsOpInc()
		: LsIrOp(LS_IR_OP_INC) {}
	ResolvedType* type = nullptr;
	LsIrValue value = LS_IR_INVALID_VALUE;
};

// LS_OP_DEC_I32, LS_OP_DEC_I64
struct LsOpDec : LsOpInc {
	LsOpDec()
		: LsOpInc() {
		kind = LS_IR_OP_DEC;
	}
};

// LS_OP_NEG_*
struct LsOpNeg : LsIrOp {
	LsOpNeg()
		: LsIrOp(LS_IR_OP_NEG) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	LsIrValue value = LS_IR_INVALID_VALUE;
};

// LS_OP_NOT
struct LsOpNot : LsOpNeg {
	LsOpNot()
		: LsOpNeg() {
		kind = LS_IR_OP_NOT;
	}
};

// LS_OP_EQ, LS_OP_NE, LS_OP_LT, LS_OP_LE, LS_OP_GT, LS_OP_GE
struct LsOpCompare : LsOpAdd {
	LsOpCompare()
		: LsOpAdd() {
		kind = LS_IR_OP_COMPARE;
	}
	LsIrCompareOp op = LS_IR_COMPARE_EQ;
};

// LS_OP_JUMP
struct LsOpJump : LsIrOp {
	LsOpJump()
		: LsIrOp(LS_IR_OP_JUMP) {}
	LsIrBlock target = LS_IR_INVALID_BLOCK;
};

// LS_OP_JZ_*, LS_OP_JNZ_*, LS_OP_JGZ_*, LS_OP_JGEZ_*, LS_OP_JLTZ_*, LS_OP_JLEZ_*
struct LsOpConditionalJump : LsIrOp {
	LsOpConditionalJump()
		: LsIrOp(LS_IR_OP_CONDITIONAL_JUMP) {}
	ResolvedType* type = nullptr;
	LsIrValue condition = LS_IR_INVALID_VALUE;
	LsIrBlock target = LS_IR_INVALID_BLOCK;
};

// LS_OP_CALL_DIRECT
struct LsOpCallDirect : LsIrOp {
	LsOpCallDirect()
		: LsIrOp(LS_IR_OP_CALL_DIRECT) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	LsIrFunction function = 0;
	LsIrValue* arguments = nullptr;
	u32* argument_sizes = nullptr;
	u32 argument_count = 0;
	u32 argument_size = 0;
	u32 result_size = 0;
};

// LS_OP_CALL_NATIVE
struct LsOpCallNative : LsOpCallDirect {
	LsOpCallNative()
		: LsOpCallDirect() {
		kind = LS_IR_OP_CALL_NATIVE;
	}
};

// LS_OP_CALL_INDIRECT
struct LsOpCallIndirect : LsIrOp {
	LsOpCallIndirect()
		: LsIrOp(LS_IR_OP_CALL_INDIRECT) {}
	ResolvedType* type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	LsIrValue function = LS_IR_INVALID_VALUE;
	LsIrValue arguments = LS_IR_INVALID_VALUE;
	u32 argument_size = 0;
	u32 result_size = 0;
};

// LS_OP_CAST
struct LsOpCast : LsIrOp {
	LsOpCast()
		: LsIrOp(LS_IR_OP_CAST) {}
	ResolvedType* type = nullptr;
	ResolvedType* target_type = nullptr;
	LsIrValue result = LS_IR_INVALID_VALUE;
	LsIrValue value = LS_IR_INVALID_VALUE;
};

// LS_OP_RETURN, LS_OP_RETURN_BASE when value is invalid
struct LsOpReturn : LsIrOp {
	LsOpReturn()
		: LsIrOp(LS_IR_OP_RETURN) {}
	LsIrValue value = LS_IR_INVALID_VALUE;
	u32 result_size = 0;
};

struct LsIrBlockData {
	LsIrBlock id = LS_IR_INVALID_BLOCK;
	ExpArray<LsIrOp*> ops;
	LsIrOp* terminator = nullptr;

	LsIrBlockData(ls_arena& arena, LsIrBlock id)
		: id(id), ops(arena) {}
};

struct LsIrFunctionData {
	ls_arena& arena;
	ls_string_view name = {};
	ResolvedType* return_type = nullptr;
	ExpArray<LsIrBlockData> blocks;
	LsIrBlock entry = LS_IR_INVALID_BLOCK;
	LsIrValue next_value = 0;

	LsIrFunctionData(ls_arena& arena)
		: arena(arena), blocks(arena) {}
};

struct LsIrModuleEntry {
	LsIrFunctionData* function = nullptr;
	FunctionExpression* source = nullptr;
	ls_string_view name = {};
	bool native = false;
};

struct LsIrModuleData {
	ls_arena& arena;
	ExpArray<LsIrModuleEntry> functions;
	u32 global_size = 0;

	LsIrModuleData(ls_arena& arena)
		: arena(arena), functions(arena) {}
};

// Lower a checked AST function to basic IR. This pass intentionally performs no
// optimization and leaves unsupported AST forms without emitted operations.
LsIrFunctionData* lsIrBuildFunction(ls_arena& arena, FunctionExpression* source, ls_string_view name = {});
LsIrModuleData* lsIrBuildModule(ls_arena& arena, ls_module* source);

// Emit the supported straight-line IR subset into a standalone bytecode
// function. Returns nullptr when an operation is not supported yet.
ls_bytecode* lsIrCompileFunction(LsIrFunctionData* function, ls_host* host);
ls_bytecode* lsIrCompileModule(LsIrModuleData* module, ls_host* host);
