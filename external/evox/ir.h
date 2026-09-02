#pragma once

#include "capi.h"
#include "exparray.h"
#include "token.h"

struct ResolvedType;
struct FunctionExpression;
struct ex_module;
struct ex_host;
struct ex_bytecode;

// Index into the per-compile SourceLocTable (token.h). INVALID when the
// op is not tied to a source location.
using ExIrSourceLoc = u32;

static constexpr ExIrSourceLoc EX_IR_INVALID_SOURCE_LOC = EX_INVALID_SOURCE_LOC;

enum class ExIrOpKind {
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
	PANIC,
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

struct ExIrOp {
	virtual ~ExIrOp() = default; // TODO just for debugger, remove later
	enum ResultMode {
		ADDRESS,
		VALUE
	} result_mode = VALUE;
	ExIrOpKind kind = ExIrOpKind::INVALID;
	ExIrSourceLoc src_loc = EX_IR_INVALID_SOURCE_LOC;
	u32 bytecode_offset = 0xffffffffu;
	bool is_ref = false;

	explicit ExIrOp(ExIrOpKind kind) : kind(kind) {}
};

struct ExOpPushLocalAddr : ExIrOp {
	ExOpPushLocalAddr() : ExIrOp(ExIrOpKind::PUSH_LOCAL_ADDR) { result_mode = ADDRESS; }
	struct ExOpAlloca* alloca = nullptr;
};

struct ExOpMaterializeAddr : ExIrOp {
	ExOpMaterializeAddr() : ExIrOp(ExIrOpKind::MATERIALIZE_ADDR) { result_mode = ADDRESS; }
	ExIrOp* value = nullptr;
};

struct ExOpPushGlobalAddr : ExIrOp {
	ExOpPushGlobalAddr() : ExIrOp(ExIrOpKind::PUSH_GLOBAL_ADDR) { result_mode = ADDRESS; }
	u32 offset = 0;
};

struct ExOpNull : ExIrOp {
	ExOpNull() : ExIrOp(ExIrOpKind::NULL_VALUE) { result_mode = VALUE; }
	u32 size;
};

struct ExOpLoad : ExIrOp {
	ExOpLoad() : ExIrOp(ExIrOpKind::LOAD) { result_mode = VALUE; }
	ExIrOp* addr = nullptr;
	u32 size = 0;
};

struct ExOpCallDirect : ExIrOp {
	ExOpCallDirect() : ExIrOp(ExIrOpKind::CALL_DIRECT) {}
	FunctionExpression* function = nullptr;
	ExIrOp** args = nullptr;
	u32 arg_count = 0;
	u32 return_size = 0;
};

struct ExOpExtractValue : ExIrOp {
	ExOpExtractValue() : ExIrOp(ExIrOpKind::EXTRACT_VALUE) {}
	ExIrOp* value = nullptr;
	u32 offset = 0;
	u32 size = 0;
};

struct ExOpPanic : ExIrOp {
	ExOpPanic() : ExIrOp(ExIrOpKind::PANIC) {}
	ExIrOp* message = nullptr;
};

struct ExOpCallIndirect : ExIrOp {
	ExOpCallIndirect() : ExIrOp(ExIrOpKind::CALL_INDIRECT) {}
	ExIrOp* callee = nullptr;
	ExIrOp** args = nullptr;
	u32* arg_sizes = nullptr;
	u32 arg_count = 0;
	u32 return_size = 0;
};

struct ExOpConditionalJump : ExIrOp {
    ExOpConditionalJump() : ExIrOp(ExIrOpKind::CONDITIONAL_JUMP) {}

    ExIrOp* condition = nullptr;
    struct ExIrBlockData* true_block = nullptr;
    ExIrBlockData* false_block = nullptr;

    // Loop layout: the condition tree is shared by two conditional jumps - an
    // entry check (normal if-style lowering, skips the body when initially
    // false) and a bottom check emitted after the body in the parent block,
    // which branches straight into `body_start` while the condition holds.
    // One conditional branch per iteration, no unconditional back edge.
    bool bottom_tested = false;
    ExIrOp* body_start = nullptr;
};

struct ExOpJump : ExIrOp {
	ExOpJump() : ExIrOp(ExIrOpKind::JUMP) {}

	ExIrOp* target = nullptr;
	u32 bytecode_patch_offset = 0xffffffffu;
};

struct ExOpNop : ExIrOp { ExOpNop() : ExIrOp(ExIrOpKind::NOP) {} };

struct ExOpFramePtr : ExIrOp {
	ExOpFramePtr() : ExIrOp(ExIrOpKind::FRAME_PTR) {}
	// Resolved to the slot at emit time, once emitAlloca has assigned stack_sp.
	struct ExOpAlloca* alloca = nullptr;
};

struct ExOpStringLiteral : ExIrOp {
	ExOpStringLiteral() : ExIrOp(ExIrOpKind::STRING_LITERAL) {}
	u32 index;
	u32 length;
};

struct ExOpLoadConst : ExIrOp {
	ExOpLoadConst() : ExIrOp(ExIrOpKind::LOAD_CONST) {}
	ResolvedType* type = nullptr;
	// Non-null for a runtime type literal; resolved to a bytecode type-table
	// index by BytecodeCompiler, after all types are collected.
	ResolvedType* represented_type = nullptr;
	u8 value[8] = {};
};

struct ExOpCopy : ExIrOp {
	ExOpCopy() : ExIrOp(ExIrOpKind::COPY) {}
	ResolvedType* type = nullptr;
	ExIrOp* src;
	ExIrOp* dst;
};

struct ExOpAggregateInit : ExIrOp {
	ExOpAggregateInit()
		: ExIrOp(ExIrOpKind::AGGREGATE_INIT) {}
	ResolvedType* type = nullptr;
	ExIrOp** values = nullptr;
	u32* offsets = nullptr;
	u32* sizes = nullptr;
	u32 value_count = 0;
};

// Widen a tagged union to a superset. The payload is copied; the tag is remapped
// because interned member order (and therefore tag values) can differ.
struct ExOpUnionConvert : ExIrOp {
	ExOpUnionConvert() : ExIrOp(ExIrOpKind::UNION_CONVERT) {}
	ResolvedType* source_type = nullptr;
	ResolvedType* target_type = nullptr;
	ExIrOp* value = nullptr;
};

struct ExOpBinary : ExIrOp {
	ExOpBinary(ExIrOpKind op) : ExIrOp(op) {}
	ResolvedType* operand_type = nullptr;
	ExIrOp* lhs = nullptr;
	ExIrOp* rhs = nullptr;
};

struct ExOpLoadBytes : ExIrOp {
	ExOpLoadBytes() : ExIrOp(ExIrOpKind::LOAD_BYTES) {}
	ResolvedType* type = nullptr;
	const u8* value = nullptr;
	u32 size = 0;
};

struct ExOpTernary : ExIrOp {
	ExOpTernary() : ExIrOp(ExIrOpKind::TERNARY) {}
	ResolvedType* type = nullptr;
	ExIrOp* condition = nullptr;
	ExIrOp* true_value = nullptr;
	ExIrOp* false_value = nullptr;
};

struct ExOpBoundsCheck : ExIrOp {
	ExOpBoundsCheck() : ExIrOp(ExIrOpKind::BOUNDS_CHECK) {}
	ResolvedType* index_type = nullptr;
	ExIrOp* index = nullptr;
	u64 length = 0;
};

struct ExOpSlice : ExIrOp {
	ExOpSlice() : ExIrOp(ExIrOpKind::SLICE) {}
	ExIrOp* source = nullptr;
	ExIrOp* begin = nullptr;
	ExIrOp* end = nullptr;
	u32 element_size = 0;
	i64 source_length = 0;
	bool source_is_array = false;
	bool source_is_scalar = false;
};

struct ExOpSliceLoad : ExIrOp {
	ExOpSliceLoad() : ExIrOp(ExIrOpKind::SLICE_LOAD) {}
	ExIrOp* slice = nullptr;
	ExIrOp* index = nullptr;
	ResolvedType* index_type = nullptr;
	u32 element_size = 0;
};

struct ExOpSliceRef : ExIrOp {
	ExOpSliceRef() : ExIrOp(ExIrOpKind::SLICE_REF) { result_mode = ADDRESS; }
	ExIrOp* slice = nullptr;
	ExIrOp* index = nullptr;
	u32 element_size = 0;
	ResolvedType* index_type = nullptr;
};

struct ExOpSliceField : ExIrOp {
	ExOpSliceField(ExIrOpKind kind) : ExIrOp(kind) {}
	ExIrOp* slice = nullptr;
	ExIrOp* index = nullptr;
	u32 element_size = 0;
	ResolvedType* index_type = nullptr;
	u32 field_offset = 0;
	u32 field_size = 0;
};

struct ExOpSliceFieldLoad : ExOpSliceField {
	ExOpSliceFieldLoad() : ExOpSliceField(ExIrOpKind::SLICE_FIELD_LOAD) {}
};

struct ExOpSliceFieldStore : ExOpSliceField {
	ExOpSliceFieldStore() : ExOpSliceField(ExIrOpKind::SLICE_FIELD_STORE) { result_mode = ADDRESS; }
};

struct ExOpUnary : ExIrOp {
	ExOpUnary(ExIrOpKind op) : ExIrOp(op) {}
	ResolvedType* operand_type = nullptr;
	ExIrOp* operand = nullptr;
};

struct ExOpNeg : ExOpUnary { ExOpNeg() : ExOpUnary(ExIrOpKind::NEG) {} };
struct ExOpNot : ExOpUnary { ExOpNot() : ExOpUnary(ExIrOpKind::NOT) {} };

struct ExOpAdd : ExOpBinary { ExOpAdd() : ExOpBinary(ExIrOpKind::ADD) {} };
struct ExOpSub : ExOpBinary { ExOpSub() : ExOpBinary(ExIrOpKind::SUB) {} };
struct ExOpMul : ExOpBinary { ExOpMul() : ExOpBinary(ExIrOpKind::MUL) {} };
struct ExOpDiv : ExOpBinary { ExOpDiv() : ExOpBinary(ExIrOpKind::DIV) {} };
struct ExOpMod : ExOpBinary { ExOpMod() : ExOpBinary(ExIrOpKind::MOD) {} };

struct ExOpAnd : ExOpBinary { ExOpAnd() : ExOpBinary(ExIrOpKind::AND) {} };
struct ExOpOr : ExOpBinary { ExOpOr() : ExOpBinary(ExIrOpKind::OR) {} };

struct ExOpEq : ExOpBinary { ExOpEq() : ExOpBinary(ExIrOpKind::EQ) {} };
struct ExOpNe : ExOpBinary { ExOpNe() : ExOpBinary(ExIrOpKind::NE) {} };
struct ExOpLt : ExOpBinary { ExOpLt() : ExOpBinary(ExIrOpKind::LT) {} };
struct ExOpLe : ExOpBinary { ExOpLe() : ExOpBinary(ExIrOpKind::LE) {} };
struct ExOpGt : ExOpBinary { ExOpGt() : ExOpBinary(ExIrOpKind::GT) {} };
struct ExOpGe : ExOpBinary { ExOpGe() : ExOpBinary(ExIrOpKind::GE) {} };

struct ExOpCast : ExIrOp {
	ExOpCast()
		: ExIrOp(ExIrOpKind::CAST) {}
	ResolvedType* type = nullptr;
	ResolvedType* target_type = nullptr;
	ExIrOp* value = nullptr;
};

struct ExOpAlloca : ExIrOp {
	ExOpAlloca()
		: ExIrOp(ExIrOpKind::ALLOCA) {}
	ResolvedType* type = nullptr;
	ExIrOp* value = nullptr;
	u32 stack_sp = 0xffFFffFF;
	ex_string_view name = {}; // debug entry name; empty for compiler temporaries
};

struct ExOpReturn : ExIrOp {
	ExOpReturn() : ExIrOp(ExIrOpKind::RETURN) {}
	ExIrOp* expression = nullptr;
	u32 size = 0; 
};

struct ExIrBlockData {
	ExIrBlockData(ex_arena& arena) : ops(arena) {}
	ExpArray<ExIrOp*> ops;
};
