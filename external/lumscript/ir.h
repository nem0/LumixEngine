#pragma once

#include "capi.h"
#include "exparray.h"
#include "token.h"

struct ResolvedType;
struct FunctionExpression;
struct ls_module;
struct ls_host;
struct ls_bytecode;

// Index into the per-compile SourceLocTable (token.h). INVALID when the
// op is not tied to a source location.
using LsIrSourceLoc = u32;

static constexpr LsIrSourceLoc LS_IR_INVALID_SOURCE_LOC = LS_INVALID_SOURCE_LOC;

enum class LsIrOpKind {
	INVALID,
	LOAD_CONST,
	LOAD_BYTES,
	COPY,
	AGGREGATE_INIT,
	UNION_CONVERT,
	COMPARE,
	ADD,
	SUB,
	MUL,
	DIV,
	MOD,
	BOUNDS_CHECK,
	CAST,
	TERNARY,
	RETURN,
	ALLOCA,
	EQ,
	NE,
	LT,
	LE,
	GT,
	GE,
	AND,
	OR,
	NEG,
	NOT,

	NOP,
	PUSH_LOCAL_ADDR,
	MATERIALIZE_ADDR,
	PUSH_GLOBAL_ADDR,
	LOAD,
	EXTRACT_VALUE,
	CALL_DIRECT,
	CALL_INDIRECT,
	CONDITIONAL_JUMP,
	JUMP,
	NULL_VALUE,
	SLICE,
	SLICE_LOAD,
	SLICE_REF,
	SLICE_FIELD_LOAD,
	SLICE_FIELD_STORE,
	STRING_LITERAL,
	FRAME_PTR
};

struct LsIrOp {
	virtual ~LsIrOp() = default; // TODO just for debugger, remove later
	enum ResultMode {
		ADDRESS,
		VALUE
	} result_mode = VALUE;
	LsIrOpKind kind = LsIrOpKind::INVALID;
	LsIrSourceLoc src_loc = LS_IR_INVALID_SOURCE_LOC;
	u32 bytecode_offset = 0xffffffffu;
	bool is_ref = false;

	explicit LsIrOp(LsIrOpKind kind) : kind(kind) {}
};

struct LsOpPushLocalAddr : LsIrOp {
	LsOpPushLocalAddr() : LsIrOp(LsIrOpKind::PUSH_LOCAL_ADDR) { result_mode = ADDRESS; }
	struct LsOpAlloca* alloca = nullptr;
};

struct LsOpMaterializeAddr : LsIrOp {
	LsOpMaterializeAddr() : LsIrOp(LsIrOpKind::MATERIALIZE_ADDR) { result_mode = ADDRESS; }
	LsIrOp* value = nullptr;
};

struct LsOpPushGlobalAddr : LsIrOp {
	LsOpPushGlobalAddr() : LsIrOp(LsIrOpKind::PUSH_GLOBAL_ADDR) { result_mode = ADDRESS; }
	u32 offset = 0;
};

struct LsOpNull : LsIrOp {
	LsOpNull() : LsIrOp(LsIrOpKind::NULL_VALUE) { result_mode = VALUE; }
	u32 size;
};

struct LsOpLoad : LsIrOp {
	LsOpLoad() : LsIrOp(LsIrOpKind::LOAD) { result_mode = VALUE; }
	LsIrOp* addr = nullptr;
	u32 size = 0;
};

struct LsOpCallDirect : LsIrOp {
	LsOpCallDirect() : LsIrOp(LsIrOpKind::CALL_DIRECT) {}
	FunctionExpression* function = nullptr;
	LsIrOp** args = nullptr;
	u32 arg_count = 0;
	u32 return_size = 0;
};

struct LsOpExtractValue : LsIrOp {
	LsOpExtractValue() : LsIrOp(LsIrOpKind::EXTRACT_VALUE) {}
	LsIrOp* value = nullptr;
	u32 offset = 0;
	u32 size = 0;
};

struct LsOpCallIndirect : LsIrOp {
	LsOpCallIndirect() : LsIrOp(LsIrOpKind::CALL_INDIRECT) {}
	LsIrOp* callee = nullptr;
	LsIrOp** args = nullptr;
	u32* arg_sizes = nullptr;
	u32 arg_count = 0;
	u32 return_size = 0;
};

struct LsOpConditionalJump : LsIrOp {
    LsOpConditionalJump() : LsIrOp(LsIrOpKind::CONDITIONAL_JUMP) {}

    LsIrOp* condition = nullptr;
    struct LsIrBlockData* true_block = nullptr;
    LsIrBlockData* false_block = nullptr;

    // Loop layout: the condition tree is shared by two conditional jumps - an
    // entry check (normal if-style lowering, skips the body when initially
    // false) and a bottom check emitted after the body in the parent block,
    // which branches straight into `body_start` while the condition holds.
    // One conditional branch per iteration, no unconditional back edge.
    bool bottom_tested = false;
    LsIrOp* body_start = nullptr;
};

struct LsOpJump : LsIrOp {
	LsOpJump() : LsIrOp(LsIrOpKind::JUMP) {}

	LsIrOp* target = nullptr;
	u32 bytecode_patch_offset = 0xffffffffu;
};

struct LsOpNop : LsIrOp { LsOpNop() : LsIrOp(LsIrOpKind::NOP) {} };

struct LsOpFramePtr : LsIrOp {
	LsOpFramePtr() : LsIrOp(LsIrOpKind::FRAME_PTR) {}
	// Resolved to the slot at emit time, once emitAlloca has assigned stack_sp.
	struct LsOpAlloca* alloca = nullptr;
};

struct LsOpStringLiteral : LsIrOp {
	LsOpStringLiteral() : LsIrOp(LsIrOpKind::STRING_LITERAL) {}
	u32 index;
	u32 length;
};

struct LsOpLoadConst : LsIrOp {
	LsOpLoadConst() : LsIrOp(LsIrOpKind::LOAD_CONST) {}
	ResolvedType* type = nullptr;
	// Non-null for a runtime type literal; resolved to a bytecode type-table
	// index by BytecodeCompiler, after all types are collected.
	ResolvedType* represented_type = nullptr;
	u8 value[8] = {};
};

struct LsOpCopy : LsIrOp {
	LsOpCopy() : LsIrOp(LsIrOpKind::COPY) {}
	ResolvedType* type = nullptr;
	LsIrOp* src;
	LsIrOp* dst;
};

struct LsOpAggregateInit : LsIrOp {
	LsOpAggregateInit()
		: LsIrOp(LsIrOpKind::AGGREGATE_INIT) {}
	ResolvedType* type = nullptr;
	LsIrOp** values = nullptr;
	u32* offsets = nullptr;
	u32* sizes = nullptr;
	u32 value_count = 0;
};

// Widen a tagged union to a superset. The payload is copied; the tag is remapped
// because interned member order (and therefore tag values) can differ.
struct LsOpUnionConvert : LsIrOp {
	LsOpUnionConvert() : LsIrOp(LsIrOpKind::UNION_CONVERT) {}
	ResolvedType* source_type = nullptr;
	ResolvedType* target_type = nullptr;
	LsIrOp* value = nullptr;
};

struct LsOpBinary : LsIrOp {
	LsOpBinary(LsIrOpKind op) : LsIrOp(op) {}
	ResolvedType* operand_type = nullptr;
	LsIrOp* lhs = nullptr;
	LsIrOp* rhs = nullptr;
};

struct LsOpLoadBytes : LsIrOp {
	LsOpLoadBytes() : LsIrOp(LsIrOpKind::LOAD_BYTES) {}
	ResolvedType* type = nullptr;
	const u8* value = nullptr;
	u32 size = 0;
};

struct LsOpTernary : LsIrOp {
	LsOpTernary() : LsIrOp(LsIrOpKind::TERNARY) {}
	ResolvedType* type = nullptr;
	LsIrOp* condition = nullptr;
	LsIrOp* true_value = nullptr;
	LsIrOp* false_value = nullptr;
};

struct LsOpBoundsCheck : LsIrOp {
	LsOpBoundsCheck() : LsIrOp(LsIrOpKind::BOUNDS_CHECK) {}
	ResolvedType* index_type = nullptr;
	LsIrOp* index = nullptr;
	u64 length = 0;
};

struct LsOpSlice : LsIrOp {
	LsOpSlice() : LsIrOp(LsIrOpKind::SLICE) {}
	LsIrOp* source = nullptr;
	LsIrOp* begin = nullptr;
	LsIrOp* end = nullptr;
	u32 element_size = 0;
	i64 source_length = 0;
	bool source_is_array = false;
	bool source_is_scalar = false;
};

struct LsOpSliceLoad : LsIrOp {
	LsOpSliceLoad() : LsIrOp(LsIrOpKind::SLICE_LOAD) {}
	LsIrOp* slice = nullptr;
	LsIrOp* index = nullptr;
	ResolvedType* index_type = nullptr;
	u32 element_size = 0;
};

struct LsOpSliceRef : LsIrOp {
	LsOpSliceRef() : LsIrOp(LsIrOpKind::SLICE_REF) { result_mode = ADDRESS; }
	LsIrOp* slice = nullptr;
	LsIrOp* index = nullptr;
	u32 element_size = 0;
	ResolvedType* index_type = nullptr;
};

struct LsOpSliceField : LsIrOp {
	LsOpSliceField(LsIrOpKind kind) : LsIrOp(kind) {}
	LsIrOp* slice = nullptr;
	LsIrOp* index = nullptr;
	u32 element_size = 0;
	ResolvedType* index_type = nullptr;
	u32 field_offset = 0;
	u32 field_size = 0;
};

struct LsOpSliceFieldLoad : LsOpSliceField {
	LsOpSliceFieldLoad() : LsOpSliceField(LsIrOpKind::SLICE_FIELD_LOAD) {}
};

struct LsOpSliceFieldStore : LsOpSliceField {
	LsOpSliceFieldStore() : LsOpSliceField(LsIrOpKind::SLICE_FIELD_STORE) { result_mode = ADDRESS; }
};

struct LsOpUnary : LsIrOp {
	LsOpUnary(LsIrOpKind op) : LsIrOp(op) {}
	ResolvedType* operand_type = nullptr;
	LsIrOp* operand = nullptr;
};

struct LsOpNeg : LsOpUnary { LsOpNeg() : LsOpUnary(LsIrOpKind::NEG) {} };
struct LsOpNot : LsOpUnary { LsOpNot() : LsOpUnary(LsIrOpKind::NOT) {} };

struct LsOpAdd : LsOpBinary { LsOpAdd() : LsOpBinary(LsIrOpKind::ADD) {} };
struct LsOpSub : LsOpBinary { LsOpSub() : LsOpBinary(LsIrOpKind::SUB) {} };
struct LsOpMul : LsOpBinary { LsOpMul() : LsOpBinary(LsIrOpKind::MUL) {} };
struct LsOpDiv : LsOpBinary { LsOpDiv() : LsOpBinary(LsIrOpKind::DIV) {} };
struct LsOpMod : LsOpBinary { LsOpMod() : LsOpBinary(LsIrOpKind::MOD) {} };

struct LsOpAnd : LsOpBinary { LsOpAnd() : LsOpBinary(LsIrOpKind::AND) {} };
struct LsOpOr : LsOpBinary { LsOpOr() : LsOpBinary(LsIrOpKind::OR) {} };

struct LsOpEq : LsOpBinary { LsOpEq() : LsOpBinary(LsIrOpKind::EQ) {} };
struct LsOpNe : LsOpBinary { LsOpNe() : LsOpBinary(LsIrOpKind::NE) {} };
struct LsOpLt : LsOpBinary { LsOpLt() : LsOpBinary(LsIrOpKind::LT) {} };
struct LsOpLe : LsOpBinary { LsOpLe() : LsOpBinary(LsIrOpKind::LE) {} };
struct LsOpGt : LsOpBinary { LsOpGt() : LsOpBinary(LsIrOpKind::GT) {} };
struct LsOpGe : LsOpBinary { LsOpGe() : LsOpBinary(LsIrOpKind::GE) {} };

struct LsOpCast : LsIrOp {
	LsOpCast()
		: LsIrOp(LsIrOpKind::CAST) {}
	ResolvedType* type = nullptr;
	ResolvedType* target_type = nullptr;
	LsIrOp* value = nullptr;
};

struct LsOpAlloca : LsIrOp {
	LsOpAlloca()
		: LsIrOp(LsIrOpKind::ALLOCA) {}
	ResolvedType* type = nullptr;
	LsIrOp* value = nullptr;
	u32 stack_sp = 0xffFFffFF;
	ls_string_view name = {}; // debug entry name; empty for compiler temporaries
};

struct LsOpReturn : LsIrOp {
	LsOpReturn() : LsIrOp(LsIrOpKind::RETURN) {}
	LsIrOp* expression = nullptr;
	u32 size = 0; 
};

struct LsIrBlockData {
	LsIrBlockData(ls_arena& arena) : ops(arena) {}
	ExpArray<LsIrOp*> ops;
};
