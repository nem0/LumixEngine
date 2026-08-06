#pragma once

#include "resolved_types.h"

typedef u32 MirValueId;
typedef u32 MirBlockId;
typedef u32 MirLocalId;
typedef u32 MirFunctionId;
typedef u32 MirConstantId;
typedef u32 MirSourceLocationId;

#define MIR_INVALID_ID 0xffFFffFFu
#define MIR_GLOBAL_SLOT 0x80000000u

enum MirOpcode {
	MIR_OP_CONST,
	MIR_OP_UNDEFINED,
	MIR_OP_CAST,
	MIR_OP_NEG,
	MIR_OP_NOT,
	MIR_OP_ADD,
	MIR_OP_SUB,
	MIR_OP_MUL,
	MIR_OP_DIV,
	MIR_OP_MOD,
	MIR_OP_EQ,
	MIR_OP_NE,
	MIR_OP_LT,
	MIR_OP_LE,
	MIR_OP_GT,
	MIR_OP_GE,
	MIR_OP_LOCAL_ADDRESS,
	MIR_OP_GLOBAL_ADDRESS,
	MIR_OP_CONSTANT_ADDRESS,
	MIR_OP_FIELD_ADDRESS,
	MIR_OP_INDEX_ADDRESS,
	MIR_OP_POINTER_OFFSET,
	MIR_OP_LOAD,
	MIR_OP_STORE,
	MIR_OP_COPY,
	MIR_OP_MAKE_SLICE,
	MIR_OP_SLICE_DATA,
	MIR_OP_SLICE_LENGTH,
	MIR_OP_UNION_TAG,
	MIR_OP_UNION_PAYLOAD_ADDRESS,
	MIR_OP_NULLABLE_HAS_VALUE,
	MIR_OP_NULLABLE_PAYLOAD_ADDRESS,
	MIR_OP_CALL,
	MIR_OP_CHECK_BOUNDS,
	MIR_OP_CHECK_NONZERO,
	MIR_OP_CHECK_NONNULL,
	MIR_OP_CHECK_ALIVE,
	MIR_OP_PHI
};

enum MirTerminatorKind {
	MIR_TERM_JUMP,
	MIR_TERM_BRANCH,
	MIR_TERM_RETURN,
	MIR_TERM_RETURN_VALUE,
	MIR_TERM_RETURN_COPY,
	MIR_TERM_TRAP,
	MIR_TERM_UNREACHABLE
};

enum MirCallTargetKind {
	MIR_CALL_DIRECT,
	MIR_CALL_INDIRECT
};

enum MirConstKind {
	MIR_CONST_DEFAULT,
	MIR_CONST_I32
};

enum MirSliceMode {
	MIR_SLICE_FULL = 0,
	MIR_SLICE_PARTIAL = 1
};

enum MirAccessMode {
	MIR_ACCESS_PLAIN = 0,
	MIR_ACCESS_INDEXED = 1,
	MIR_ACCESS_NULLABLE_TAG = 2,
	MIR_ACCESS_SLICE_ELEMENT = 3,
	MIR_ACCESS_SLICE_FIELD = 4
};

struct MirSourceLocation {
	ls_string_view source_name;
	u32 line;
	u32 column;
};

struct MirValueRange {
	MirValueId* values;
	u32* sizes;
	u32 count;
};

struct MirLocal {
	ResolvedType* type;
	ls_string_view name;
	u32 offset;
	u32 scope;
	MirSourceLocationId source_location;
	bool is_mutable;
	bool compiler_generated;
};

struct MirInstruction {
	MirOpcode opcode;
	ResolvedType* type;
	MirValueId result;
	MirSourceLocationId source_location;
};

struct MirBinaryInstruction : MirInstruction {
	MirBinaryInstruction(MirOpcode oc) { opcode = oc; }

	MirValueId lhs;
	MirValueId rhs;
	ResolvedType* operand_type = nullptr;
};

struct MirUnaryInstruction : MirInstruction {
	MirUnaryInstruction(MirOpcode oc) { opcode = oc; }

	MirValueId operand;
};

struct MirCastInstruction : MirInstruction {
	MirCastInstruction() { opcode = MIR_OP_CAST; }

	MirValueId operand = MIR_INVALID_ID;
	ResolvedType* operand_type = nullptr;
};

struct MirLoadInstruction : MirInstruction {
	MirLoadInstruction() { opcode = MIR_OP_LOAD; }

	MirValueId address = MIR_INVALID_ID;
	MirValueId index = MIR_INVALID_ID;
	MirAccessMode access = MIR_ACCESS_PLAIN;
	u32 element_size = 0;
	u32 field_offset = 0;
	u32 extent = 0;
};

struct MirStoreInstruction : MirInstruction {
	MirStoreInstruction() { opcode = MIR_OP_STORE; }

	MirValueId address = MIR_INVALID_ID;
	MirValueId index = MIR_INVALID_ID;
	MirValueId value = MIR_INVALID_ID;
	MirAccessMode access = MIR_ACCESS_PLAIN;
	u32 element_size = 0;
	u32 field_offset = 0;
	u32 extent = 0;
};

struct MirSliceInstruction : MirInstruction {
	MirSliceInstruction() { opcode = MIR_OP_MAKE_SLICE; }

	MirValueId base = MIR_INVALID_ID;
	MirValueId begin = MIR_INVALID_ID;
	MirValueId end = MIR_INVALID_ID;
	MirSliceMode mode = MIR_SLICE_FULL;
	u32 element_size = 0;
	u32 length = 0;
};

struct MirAddressInstruction : MirInstruction {
	MirAddressInstruction(MirOpcode oc) { opcode = oc; }

	MirLocalId local = MIR_INVALID_ID;
	u32 byte_offset = 0;
	u32 global_offset = 0;
};

struct MirCallInstruction : MirInstruction {
	MirCallInstruction() { opcode = MIR_OP_CALL; }

	MirFunctionId function = MIR_INVALID_ID;
	MirCallTargetKind call_target = MIR_CALL_DIRECT;
	MirValueId callee = MIR_INVALID_ID;
	u32 args_size = 0;
	ls_string_view call_name = {};
	MirValueRange arguments = {};
};

struct MirConstInstruction : MirInstruction {
	MirConstInstruction() { opcode = MIR_OP_CONST; }

	MirConstKind kind = MIR_CONST_DEFAULT;
	f64 floating;
	i64 integer;
	ls_string_view string;
};

struct MirUndefinedInstruction : MirInstruction {
	MirUndefinedInstruction() { opcode = MIR_OP_UNDEFINED; }
};

struct MirNullableInstruction : MirInstruction {
	MirNullableInstruction() { opcode = MIR_OP_NULLABLE_HAS_VALUE; }

	MirValueId address;
	MirValueId index;
};

struct MirPhiIncoming {
	MirBlockId block;
	MirValueId value;
};

struct MirPhi {
	MirValueId result;
	ResolvedType* type;
	MirSourceLocationId source_location;
	ExpArray<MirPhiIncoming> incoming;

	explicit MirPhi(ls_arena& arena) : incoming(arena) {
		result = MIR_INVALID_ID;
		type = nullptr;
		source_location = MIR_INVALID_ID;
	}
};

struct MirTerminator {
	MirTerminatorKind kind;
	MirSourceLocationId source_location;
	MirBlockId targets[2];
	MirValueId value;
	MirLocalId local;
	u32 trap_kind;
};

struct MirBlock {
	MirBlockId id;
	ExpArray<MirInstruction*> instructions;
	ExpArray<MirPhi> phis;
	MirTerminator terminator;
	bool has_terminator;

	MirBlock(ls_arena& arena, MirBlockId block_id)
		: id(block_id), instructions(arena), phis(arena) {
		terminator.kind = MIR_TERM_UNREACHABLE;
		terminator.source_location = MIR_INVALID_ID;
		terminator.targets[0] = MIR_INVALID_ID;
		terminator.targets[1] = MIR_INVALID_ID;
		terminator.value = MIR_INVALID_ID;
		terminator.local = MIR_INVALID_ID;
		terminator.trap_kind = 0;
		has_terminator = false;
	}
};

struct MirFunction {
	ls_arena& arena;
	ls_string_view name;
	ResolvedType* return_type;
	u32 param_size;
	ExpArray<MirLocal> locals;
	ExpArray<MirBlock> blocks;
	ExpArray<MirSourceLocation> source_locations;
	MirBlockId entry;
	MirValueId next_value;

	MirFunction(ls_arena& arena)
		: arena(arena), name({}), return_type(nullptr), param_size(0), locals(arena), blocks(arena), source_locations(arena) {
		entry = MIR_INVALID_ID;
		next_value = 0;
	}
};

struct MirNativeFunction {
	ls_string_view name;
	FunctionResolvedType* type;
};

struct MirModuleFunction {
	bool is_native;
	MirFunction* function;
	MirNativeFunction native;
};

struct MirModule {
	ls_arena& arena;
	ExpArray<MirModuleFunction> functions;
	MirFunction* global_init;
	u32 global_size;

	explicit MirModule(ls_arena& arena)
		: arena(arena), functions(arena), global_init(nullptr), global_size(0) {}
};

static inline MirBlock* mirFunctionCreateBlock(MirFunction& function) {
	MirBlock& block = function.blocks.emplace_back(function.arena, (MirBlockId)function.blocks.size());
	if (function.entry == MIR_INVALID_ID) function.entry = block.id;
	return &block;
}

static inline MirLocalId mirFunctionAddLocal(MirFunction& function, ResolvedType* type, ls_string_view name, bool is_mutable, bool compiler_generated) {
	MirLocal& local = function.locals.emplace_back();
	local.type = type;
	local.name = name;
	local.offset = 0;
	local.scope = 0;
	local.source_location = MIR_INVALID_ID;
	local.is_mutable = is_mutable;
	local.compiler_generated = compiler_generated;
	return (MirLocalId)(function.locals.size() - 1);
}

static inline MirValueId mirFunctionNewValue(MirFunction& function) {
	return function.next_value++;
}

static inline MirSourceLocationId mirFunctionAddSourceLocation(MirFunction& function, ls_string_view source_name, u32 line, u32 column) {
	MirSourceLocation& location = function.source_locations.emplace_back();
	location.source_name = source_name;
	location.line = line;
	location.column = column;
	return (MirSourceLocationId)(function.source_locations.size() - 1);
}
