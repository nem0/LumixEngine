#pragma once

#include "capi.h"
#include "exparray.h"

struct ResolvedType;
struct FunctionExpression;
struct ls_module;
struct ls_host;
struct ls_bytecode;

// compacted by later passes.
using LsIrSourceLoc = u32;

static constexpr LsIrSourceLoc LS_IR_INVALID_SOURCE_LOC = 0xffffffffu;

enum LsIrOpKind {
	LS_IR_OP_INVALID,
	LS_IR_OP_LOAD_CONST,
	LS_IR_OP_LOAD_BYTES,
	LS_IR_OP_COPY,
	LS_IR_OP_AGGREGATE_INIT,
	LS_IR_OP_COMPARE,
	LS_IR_OP_ADD,
	LS_IR_OP_SUB,
	LS_IR_OP_MUL,
	LS_IR_OP_DIV,
	LS_IR_OP_MOD,
	LS_IR_OP_BOUNDS_CHECK,
	LS_IR_OP_CAST,
	LS_IR_OP_TERNARY,
	LS_IR_OP_RETURN,
	LS_IR_OP_ALLOCA,
	LS_IR_OP_EQ,
	LS_IR_OP_NE,
	LS_IR_OP_LT,
	LS_IR_OP_LE,
	LS_IR_OP_GT,
	LS_IR_OP_GE,
	LS_IR_OP_AND,
	LS_IR_OP_OR,
	LS_IR_OP_NEG,
	LS_IR_OP_NOT,

	LS_IR_OP_NOP,
	LS_IR_OP_PUSH_LOCAL_ADDR,
	LS_IR_OP_MATERIALIZE_ADDR,
	LS_IR_OP_PUSH_GLOBAL_ADDR,
	LS_IR_OP_LOAD,
	LS_IR_OP_EXTRACT_VALUE,
	LS_IR_OP_CALL_DIRECT,
	LS_IR_OP_CALL_INDIRECT,
	LS_IR_OP_CONDITIONAL_JUMP,
	LS_IR_OP_JUMP,
	LS_IR_OP_NULL,
	LS_IR_OP_SLICE,
	LS_IR_OP_SLICE_LOAD,
	LS_IR_OP_SLICE_REF,
	LS_IR_OP_STRING_LITERAL
};

struct LsIrOp {
	virtual ~LsIrOp() = default; // TODO just for debugger, remove later
	enum ResultMode {
		ADDRESS,
		VALUE
	} result_mode = VALUE;
	LsIrOpKind kind = LS_IR_OP_INVALID;
	LsIrSourceLoc src_loc = LS_IR_INVALID_SOURCE_LOC;
	u32 bytecode_offset = 0xffffffffu;
	bool is_ref = false;

	explicit LsIrOp(LsIrOpKind kind) : kind(kind) {}
};

struct LsOpPushLocalAddr : LsIrOp {
	LsOpPushLocalAddr() : LsIrOp(LS_IR_OP_PUSH_LOCAL_ADDR) { result_mode = ADDRESS; }
	struct LsOpAlloca* alloca = nullptr;
};

struct LsOpMaterializeAddr : LsIrOp {
	LsOpMaterializeAddr() : LsIrOp(LS_IR_OP_MATERIALIZE_ADDR) { result_mode = ADDRESS; }
	LsIrOp* value = nullptr;
};

struct LsOpPushGlobalAddr : LsIrOp {
	LsOpPushGlobalAddr() : LsIrOp(LS_IR_OP_PUSH_GLOBAL_ADDR) { result_mode = ADDRESS; }
	u32 offset = 0;
};

struct LsOpNull : LsIrOp {
	LsOpNull() : LsIrOp(LS_IR_OP_NULL) { result_mode = VALUE; }
	u32 size;
};

struct LsOpLoad : LsIrOp {
	LsOpLoad() : LsIrOp(LS_IR_OP_LOAD) { result_mode = VALUE; }
	LsIrOp* addr = nullptr;
	u32 size = 0;
};

struct LsOpCallDirect : LsIrOp {
	LsOpCallDirect() : LsIrOp(LS_IR_OP_CALL_DIRECT) {}
	FunctionExpression* function = nullptr;
	LsIrOp** args = nullptr;
	u32 arg_count = 0;
	u32 return_size = 0;
};

struct LsOpExtractValue : LsIrOp {
	LsOpExtractValue() : LsIrOp(LS_IR_OP_EXTRACT_VALUE) {}
	LsIrOp* value = nullptr;
	u32 offset = 0;
	u32 size = 0;
};

struct LsOpCallIndirect : LsIrOp {
	LsOpCallIndirect() : LsIrOp(LS_IR_OP_CALL_INDIRECT) {}
	LsIrOp* callee = nullptr;
	LsIrOp** args = nullptr;
	u32* arg_sizes = nullptr;
	u32 arg_count = 0;
	u32 return_size = 0;
};

struct LsOpConditionalJump : LsIrOp {
    LsOpConditionalJump() : LsIrOp(LS_IR_OP_CONDITIONAL_JUMP) {}

    LsIrOp* condition = nullptr;
    struct LsIrBlockData* true_block = nullptr;
    LsIrBlockData* false_block = nullptr;
};

struct LsOpJump : LsIrOp {
	LsOpJump() : LsIrOp(LS_IR_OP_JUMP) {}

	LsIrOp* target = nullptr;
	u32 bytecode_patch_offset = 0xffffffffu;
};

struct LsOpNop : LsIrOp { LsOpNop() : LsIrOp(LS_IR_OP_NOP) {} };

struct LsOpStringLiteral : LsIrOp {
	LsOpStringLiteral() : LsIrOp(LS_IR_OP_STRING_LITERAL) {}
	u32 index;
	u32 length;
};

struct LsOpLoadConst : LsIrOp {
	LsOpLoadConst() : LsIrOp(LS_IR_OP_LOAD_CONST) {}
	ResolvedType* type = nullptr;
	u8 value[8] = {};
};

struct LsOpCopy : LsIrOp {
	LsOpCopy() : LsIrOp(LS_IR_OP_COPY) {}
	ResolvedType* type = nullptr;
	LsIrOp* src;
	LsIrOp* dst;
};

struct LsOpAggregateInit : LsIrOp {
	LsOpAggregateInit()
		: LsIrOp(LS_IR_OP_AGGREGATE_INIT) {}
	ResolvedType* type = nullptr;
	LsIrOp** values = nullptr;
	u32* offsets = nullptr;
	u32* sizes = nullptr;
	u32 value_count = 0;
};

struct LsOpBinary : LsIrOp {
	LsOpBinary(LsIrOpKind op) : LsIrOp(op) {}
	ResolvedType* operand_type = nullptr;
	LsIrOp* lhs = nullptr;
	LsIrOp* rhs = nullptr;
};

struct LsOpLoadBytes : LsIrOp {
	LsOpLoadBytes() : LsIrOp(LS_IR_OP_LOAD_BYTES) {}
	ResolvedType* type = nullptr;
	const u8* value = nullptr;
	u32 size = 0;
};

struct LsOpTernary : LsIrOp {
	LsOpTernary() : LsIrOp(LS_IR_OP_TERNARY) {}
	ResolvedType* type = nullptr;
	LsIrOp* condition = nullptr;
	LsIrOp* true_value = nullptr;
	LsIrOp* false_value = nullptr;
};

struct LsOpBoundsCheck : LsIrOp {
	LsOpBoundsCheck() : LsIrOp(LS_IR_OP_BOUNDS_CHECK) {}
	ResolvedType* index_type = nullptr;
	LsIrOp* index = nullptr;
	u64 length = 0;
};

struct LsOpSlice : LsIrOp {
	LsOpSlice() : LsIrOp(LS_IR_OP_SLICE) {}
	LsIrOp* source = nullptr;
	LsIrOp* begin = nullptr;
	LsIrOp* end = nullptr;
	u32 element_size = 0;
	i64 source_length = 0;
	bool source_is_array = false;
	bool source_is_scalar = false;
};

struct LsOpSliceLoad : LsIrOp {
	LsOpSliceLoad() : LsIrOp(LS_IR_OP_SLICE_LOAD) {}
	LsIrOp* slice = nullptr;
	LsIrOp* index = nullptr;
	u32 element_size = 0;
};

struct LsOpSliceRef : LsIrOp {
	LsOpSliceRef() : LsIrOp(LS_IR_OP_SLICE_REF) { result_mode = ADDRESS; }
	LsIrOp* slice = nullptr;
	LsIrOp* index = nullptr;
	u32 element_size = 0;
};

struct LsOpUnary : LsIrOp {
	LsOpUnary(LsIrOpKind op) : LsIrOp(op) {}
	ResolvedType* operand_type = nullptr;
	LsIrOp* operand = nullptr;
};

struct LsOpNeg : LsOpUnary { LsOpNeg() : LsOpUnary(LS_IR_OP_NEG) {} };
struct LsOpNot : LsOpUnary { LsOpNot() : LsOpUnary(LS_IR_OP_NOT) {} };

struct LsOpAdd : LsOpBinary { LsOpAdd() : LsOpBinary(LS_IR_OP_ADD) {} };
struct LsOpSub : LsOpBinary { LsOpSub() : LsOpBinary(LS_IR_OP_SUB) {} };
struct LsOpMul : LsOpBinary { LsOpMul() : LsOpBinary(LS_IR_OP_MUL) {} };
struct LsOpDiv : LsOpBinary { LsOpDiv() : LsOpBinary(LS_IR_OP_DIV) {} };
struct LsOpMod : LsOpBinary { LsOpMod() : LsOpBinary(LS_IR_OP_MOD) {} };

struct LsOpAnd : LsOpBinary { LsOpAnd() : LsOpBinary(LS_IR_OP_AND) {} };
struct LsOpOr : LsOpBinary { LsOpOr() : LsOpBinary(LS_IR_OP_OR) {} };

struct LsOpEq : LsOpBinary { LsOpEq() : LsOpBinary(LS_IR_OP_EQ) {} };
struct LsOpNe : LsOpBinary { LsOpNe() : LsOpBinary(LS_IR_OP_NE) {} };
struct LsOpLt : LsOpBinary { LsOpLt() : LsOpBinary(LS_IR_OP_LT) {} };
struct LsOpLe : LsOpBinary { LsOpLe() : LsOpBinary(LS_IR_OP_LE) {} };
struct LsOpGt : LsOpBinary { LsOpGt() : LsOpBinary(LS_IR_OP_GT) {} };
struct LsOpGe : LsOpBinary { LsOpGe() : LsOpBinary(LS_IR_OP_GE) {} };

struct LsOpCast : LsIrOp {
	LsOpCast()
		: LsIrOp(LS_IR_OP_CAST) {}
	ResolvedType* type = nullptr;
	ResolvedType* target_type = nullptr;
	LsIrOp* value = nullptr;
};

struct LsOpAlloca : LsIrOp {
	LsOpAlloca()
		: LsIrOp(LS_IR_OP_ALLOCA) {}
	ResolvedType* type = nullptr;
	LsIrOp* value = nullptr;
	u32 stack_sp = 0xffFFffFF;
};

struct LsOpReturn : LsIrOp {
	LsOpReturn() : LsIrOp(LS_IR_OP_RETURN) {}
	LsIrOp* expression = nullptr;
	u32 size = 0; 
};

struct LsIrBlockData {
	LsIrBlockData(ls_arena& arena) : ops(arena) {}
	ExpArray<LsIrOp*> ops;
};
