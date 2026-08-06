#include "bytecode.h"
#include "mir.h"

#include <cstdlib>
#include <cstring>

struct MirCode {
	ls_arena& arena;
	u8* data;
	u32 size;
	u32 capacity;

	MirCode(ls_arena& arena)
		: arena(arena)
		, data(nullptr)
		, size(0)
		, capacity(0) {}
};

struct MirJumpPatch {
	u32 operand;
	MirBlockId target;
};

static bool mirCodeBytes(MirCode& code, const void* data, u32 size) {
	if (code.size + size > code.capacity) {
		u32 capacity = code.capacity ? code.capacity * 2u : 64u;
		while (capacity < code.size + size) capacity *= 2u;
		u8* next = (u8*)code.arena.allocate(code.arena.user_data, capacity, alignof(u8));
		if (!next) return false;
		if (code.data) memcpy(next, code.data, code.size);
		code.data = next;
		code.capacity = capacity;
	}
	memcpy(code.data + code.size, data, size);
	code.size += size;
	return true;
}

static bool mirCodeU8(MirCode& code, u8 value) {
	return mirCodeBytes(code, &value, sizeof(value));
}
static bool mirCodeU16(MirCode& code, u16 value) {
	return mirCodeBytes(code, &value, sizeof(value));
}
static bool mirCodeU32(MirCode& code, u32 value) {
	return mirCodeBytes(code, &value, sizeof(value));
}
static bool mirCodeU64(MirCode& code, u64 value) {
	return mirCodeBytes(code, &value, sizeof(value));
}

static u32 mirNumericIndex(ls_type_kind kind) {
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
		default: return MIR_INVALID_ID;
	}
}

static bool mirIsGlobalSlot(u32 slot) {
	return (slot & MIR_GLOBAL_SLOT) != 0;
}
static u32 mirGlobalOffset(u32 slot) {
	return slot & ~MIR_GLOBAL_SLOT;
}

static bool mirEmitBinaryI32(MirCode& code, ls_op op, MirBinaryInstruction& instruction, u32* slots) {
	return mirCodeU8(code, (u8)op) && mirCodeU32(code, slots[instruction.result]) && mirCodeU32(code, slots[instruction.lhs]) && mirCodeU32(code, slots[instruction.rhs]);
}

static ls_op mirCompareOpcode(MirOpcode op) {
	switch (op) {
		case MIR_OP_EQ: return LS_OP_EQ;
		case MIR_OP_NE: return LS_OP_NE;
		case MIR_OP_LT: return LS_OP_LT;
		case MIR_OP_LE: return LS_OP_LE;
		case MIR_OP_GT: return LS_OP_GT;
		case MIR_OP_GE: return LS_OP_GE;
		default: return LS_OP_EQ;
	}
}

static ls_type_kind mirTypeKind(const ResolvedType* type) {
	if (!type) return LS_TYPE_INVALID;
	switch (type->kind) {
		case ResolvedType::BOOL: return LS_TYPE_BOOL;
		case ResolvedType::I8: return LS_TYPE_I8;
		case ResolvedType::I16: return LS_TYPE_I16;
		case ResolvedType::I32: return LS_TYPE_I32;
		case ResolvedType::I64: return LS_TYPE_I64;
		case ResolvedType::U8:
		case ResolvedType::BYTE: return LS_TYPE_U8;
		case ResolvedType::U16: return LS_TYPE_U16;
		case ResolvedType::U32: return LS_TYPE_U32;
		case ResolvedType::U64: return LS_TYPE_U64;
		case ResolvedType::ISIZE: return LS_TYPE_I64;
		case ResolvedType::F32: return LS_TYPE_F32;
		case ResolvedType::F64: return LS_TYPE_F64;
		case ResolvedType::ENUM: return LS_TYPE_ENUM;
		case ResolvedType::UNTYPED_INT: return LS_TYPE_I32;
		case ResolvedType::UNTYPED_FLOAT: return LS_TYPE_F64;
		default: return LS_TYPE_INVALID;
	}
}

static bool mirAppendStringLiteral(ls_bytecode& bytecode, ls_string_view value, u32& index) {
	if (!bytecode.arena) return false;
	if (bytecode.string_count == bytecode.string_capacity) {
		const u32 capacity = bytecode.string_capacity ? bytecode.string_capacity * 2u : 8u;
		ls_string_view* strings = (ls_string_view*)bytecode.arena->allocate(bytecode.arena->user_data, sizeof(ls_string_view) * capacity, alignof(ls_string_view));
		if (!strings) return false;
		if (bytecode.string_count) memcpy(strings, bytecode.strings, sizeof(ls_string_view) * bytecode.string_count);
		bytecode.strings = strings;
		bytecode.string_capacity = capacity;
	}
	const size_t length = value.begin && value.end && value.end >= value.begin ? (size_t)(value.end - value.begin) : 0;
	char* copy = (char*)bytecode.arena->allocate(bytecode.arena->user_data, length + 1u, 1);
	if (!copy) return false;
	if (length) memcpy(copy, value.begin, length);
	copy[length] = '\0';
	index = bytecode.string_count++;
	bytecode.strings[index] = {copy, copy + length};
	return true;
}

static bool mirEmitInstruction(ls_bytecode& bytecode, MirCode& code, MirInstruction& instruction, u32* slots) {
	if (!instruction.type) return false;
	const u32 result = instruction.result == MIR_INVALID_ID ? 0 : slots[instruction.result];
	switch (instruction.opcode) {
		case MIR_OP_CONST: {
			MirConstInstruction& mci = static_cast<MirConstInstruction&>(instruction);
			if (mci.kind == MIR_CONST_I32) return mirCodeU8(code, LS_OP_LOAD_CONST_4) && mirCodeU32(code, result) && mirCodeU32(code, (u32)mci.integer);
			const u32 size = typeByteSize(*mci.type);
			switch (size) {
				case 16: {
					if (mci.type->kind != ResolvedType::SLICE) return false;
					if (!mci.string.begin)
						return mirCodeU8(code, LS_OP_LOAD_CONST_8) && mirCodeU32(code, result) && mirCodeU64(code, 0) && mirCodeU8(code, LS_OP_LOAD_CONST_8) && mirCodeU32(code, result + 8) &&
							   mirCodeU64(code, 0);
					u32 string_index = 0;
					if (!mirAppendStringLiteral(bytecode, mci.string, string_index)) return false;
					const ls_string_view value = bytecode.strings[string_index];
					return mirCodeU8(code, LS_OP_LOAD_CONST_8) && mirCodeU32(code, result) && mirCodeU64(code, (u64)(uintptr)value.begin) && mirCodeU8(code, LS_OP_LOAD_CONST_8) &&
						   mirCodeU32(code, result + 8) && mirCodeU64(code, (u64)(value.end - value.begin));
				}
				case 1: return mirCodeU8(code, LS_OP_LOAD_CONST_1) && mirCodeU32(code, result) && mirCodeU8(code, (u8)mci.integer);
				case 2: return mirCodeU8(code, LS_OP_LOAD_CONST_2) && mirCodeU32(code, result) && mirCodeU16(code, (u16)mci.integer);
				case 4: {
					u32 bits = (u32)mci.integer;
					if (mci.type->kind == ResolvedType::F32) {
						f32 value = (f32)mci.floating;
						memcpy(&bits, &value, sizeof(bits));
					}
					return mirCodeU8(code, LS_OP_LOAD_CONST_4) && mirCodeU32(code, result) && mirCodeU32(code, bits);
				}
				case 8: {
					u64 bits = mci.integer;
					if (mci.type->kind == ResolvedType::F64) memcpy(&bits, &mci.floating, sizeof(bits));
					return mirCodeU8(code, LS_OP_LOAD_CONST_8) && mirCodeU32(code, result) && mirCodeU64(code, bits);
				}
				default: return false;
			}
		}
		case MIR_OP_UNDEFINED:
			switch (typeByteSize(*instruction.type)) {
				case 1: return mirCodeU8(code, LS_OP_LOAD_CONST_1) && mirCodeU32(code, result) && mirCodeU8(code, 0);
				case 2: return mirCodeU8(code, LS_OP_LOAD_CONST_2) && mirCodeU32(code, result) && mirCodeU16(code, 0);
				case 4: return mirCodeU8(code, LS_OP_LOAD_CONST_4) && mirCodeU32(code, result) && mirCodeU32(code, 0);
				case 8: return mirCodeU8(code, LS_OP_LOAD_CONST_8) && mirCodeU32(code, result) && mirCodeU64(code, 0);
				default: return false;
			}
		case MIR_OP_LOCAL_ADDRESS:
		case MIR_OP_GLOBAL_ADDRESS: return true;
		case MIR_OP_MAKE_SLICE: {
			auto& slice = static_cast<MirSliceInstruction&>(instruction);
			if (typeByteSize(*instruction.type) != 16) return false;
			if (slice.mode == MIR_SLICE_PARTIAL)
				return mirCodeU8(code, LS_OP_COPY) && mirCodeU32(code, result) && mirCodeU32(code, slots[slice.base]) && mirCodeU32(code, 16) && mirCodeU8(code, LS_OP_SLICE) &&
					   mirCodeU32(code, result) && mirCodeU32(code, slots[slice.begin]) && mirCodeU32(code, slots[slice.end]) && mirCodeU32(code, slice.element_size);
			return mirCodeU8(code, LS_OP_LOCAL_REF) && mirCodeU32(code, result) && mirCodeU32(code, slots[slice.base]) && mirCodeU8(code, LS_OP_LOAD_CONST_8) && mirCodeU32(code, result + 8) &&
				   mirCodeU64(code, slice.length);
		}
		case MIR_OP_SLICE_LENGTH: {
			auto& unary = static_cast<MirUnaryInstruction&>(instruction);
			return mirCodeU8(code, LS_OP_SLICE_LENGTH) && mirCodeU32(code, result) && mirCodeU32(code, slots[unary.operand]);
		}
		case MIR_OP_LOAD: {
			auto& load = static_cast<MirLoadInstruction&>(instruction);
			switch (load.access) {
				case MIR_ACCESS_INDEXED:
					return mirCodeU8(code, LS_OP_LOAD_INDEXED_LOCAL_I32) && mirCodeU32(code, result) && mirCodeU32(code, slots[load.address]) && mirCodeU32(code, slots[load.index]) &&
						   mirCodeU32(code, load.element_size) && mirCodeU32(code, load.field_offset) && mirCodeU32(code, load.extent) && mirCodeU32(code, typeByteSize(*instruction.type));
				case MIR_ACCESS_SLICE_ELEMENT:
					return mirCodeU8(code, LS_OP_SLICE_LOAD_LOCAL_I32) && mirCodeU32(code, result) && mirCodeU32(code, slots[load.address]) && mirCodeU32(code, slots[load.index]) &&
						   mirCodeU32(code, load.element_size);
				case MIR_ACCESS_SLICE_FIELD:
					return mirCodeU8(code, LS_OP_SLICE_LOAD_AT_LOCAL_I32) && mirCodeU32(code, result) && mirCodeU32(code, slots[load.address]) && mirCodeU32(code, slots[load.index]) &&
						   mirCodeU32(code, load.element_size) && mirCodeU32(code, load.field_offset) && mirCodeU32(code, load.extent);
				default:
					if (mirIsGlobalSlot(slots[load.address]))
						return mirCodeU8(code, LS_OP_GLOBAL_LOAD) && mirCodeU32(code, result) && mirCodeU32(code, mirGlobalOffset(slots[load.address])) &&
							   mirCodeU32(code, typeByteSize(*instruction.type));
					return mirCodeU8(code, LS_OP_COPY) && mirCodeU32(code, result) && mirCodeU32(code, slots[load.address]) && mirCodeU32(code, typeByteSize(*instruction.type));
			}
		}
		case MIR_OP_STORE: {
			auto& store = static_cast<MirStoreInstruction&>(instruction);
			switch (store.access) {
				case MIR_ACCESS_SLICE_ELEMENT:
					return mirCodeU8(code, LS_OP_SLICE_STORE_LOCAL_I32) && mirCodeU32(code, slots[store.address]) && mirCodeU32(code, slots[store.index]) && mirCodeU32(code, slots[store.value]) &&
						   mirCodeU32(code, store.element_size);
				case MIR_ACCESS_SLICE_FIELD:
					return mirCodeU8(code, LS_OP_SLICE_STORE_AT_LOCAL_I32) && mirCodeU32(code, slots[store.address]) && mirCodeU32(code, slots[store.index]) && mirCodeU32(code, slots[store.value]) &&
						   mirCodeU32(code, store.element_size) && mirCodeU32(code, store.field_offset) && mirCodeU32(code, store.extent);
				case MIR_ACCESS_NULLABLE_TAG:
					return mirCodeU8(code, LS_OP_STORE_INDEXED_LOCAL_I32) && mirCodeU32(code, slots[store.address]) && mirCodeU32(code, slots[store.index]) && mirCodeU32(code, slots[store.value]) &&
						   mirCodeU32(code, store.element_size) && mirCodeU32(code, store.field_offset) && mirCodeU32(code, store.extent) && mirCodeU32(code, 1);
				case MIR_ACCESS_INDEXED:
					return mirCodeU8(code, LS_OP_STORE_INDEXED_LOCAL_I32) && mirCodeU32(code, slots[store.address]) && mirCodeU32(code, slots[store.index]) && mirCodeU32(code, slots[store.value]) &&
						   mirCodeU32(code, store.element_size) && mirCodeU32(code, store.field_offset) && mirCodeU32(code, store.extent) && mirCodeU32(code, typeByteSize(*instruction.type));
				default:
					if (mirIsGlobalSlot(slots[store.address]))
						return mirCodeU8(code, LS_OP_GLOBAL_STORE) && mirCodeU32(code, mirGlobalOffset(slots[store.address])) && mirCodeU32(code, slots[store.value]) &&
							   mirCodeU32(code, typeByteSize(*instruction.type));
					return mirCodeU8(code, LS_OP_COPY) && mirCodeU32(code, slots[store.address]) && mirCodeU32(code, slots[store.value]) && mirCodeU32(code, typeByteSize(*instruction.type));
			}
		}
		case MIR_OP_ADD:
		case MIR_OP_SUB:
		case MIR_OP_MUL:
		case MIR_OP_DIV:
		case MIR_OP_MOD: {
			const ls_type_kind kind = mirTypeKind(instruction.type);
			const u32 index = mirNumericIndex(kind);
			if (index == MIR_INVALID_ID) return false;
			ls_op base = LS_OP_MOD_I8;
			switch (instruction.opcode) {
				case MIR_OP_ADD: base = LS_OP_ADD_I8; break;
				case MIR_OP_SUB: base = LS_OP_SUB_I8; break;
				case MIR_OP_MUL: base = LS_OP_MUL_I8; break;
				case MIR_OP_DIV: base = LS_OP_DIV_I8; break;
				case MIR_OP_MOD: base = LS_OP_MOD_I8; break;
			}
			return mirEmitBinaryI32(code, (ls_op)(base + index), static_cast<MirBinaryInstruction&>(instruction), slots);
		}
		case MIR_OP_NE:
		case MIR_OP_EQ:
		case MIR_OP_LT:
		case MIR_OP_LE:
		case MIR_OP_GT:
		case MIR_OP_GE: {
			auto& binary = static_cast<MirBinaryInstruction&>(instruction);
			if (binary.operand_type && binary.operand_type->kind == ResolvedType::SLICE) {
				SliceResolvedType* slice = static_cast<SliceResolvedType*>(binary.operand_type);
				ls_type_kind element_kind = mirTypeKind(slice->element_type);
				if (element_kind == LS_TYPE_INVALID) return false;
				if (!mirCodeU8(code, LS_OP_SLICE_EQ) || !mirCodeU32(code, result) || !mirCodeU32(code, slots[binary.lhs]) || !mirCodeU32(code, slots[binary.rhs]) ||
					!mirCodeU32(code, slice->element_type ? typeByteSize(*slice->element_type) : 0) || !mirCodeU8(code, (u8)element_kind))
					return false;
				if (binary.opcode == MIR_OP_NE) return mirCodeU8(code, LS_OP_NOT) && mirCodeU32(code, result);
				return true;
			}
			if (mirNumericIndex(mirTypeKind(binary.operand_type)) == MIR_INVALID_ID) return false;
			{
				const bool swap = binary.opcode == MIR_OP_GT || binary.opcode == MIR_OP_GE;
				const MirValueId lhs = swap ? binary.rhs : binary.lhs;
				const MirValueId rhs = swap ? binary.lhs : binary.rhs;
				return mirCodeU8(code, (u8)mirCompareOpcode(binary.opcode)) && mirCodeU32(code, result) && mirCodeU32(code, slots[lhs]) && mirCodeU32(code, slots[rhs]) &&
					   mirCodeU8(code, (u8)mirTypeKind(binary.operand_type));
			}
		}
		case MIR_OP_NULLABLE_HAS_VALUE: {
			auto& nullable = static_cast<MirNullableInstruction&>(instruction);
			return mirCodeU8(code, LS_OP_LOAD_INDEXED_LOCAL_I32) && mirCodeU32(code, result) && mirCodeU32(code, slots[nullable.address]) && mirCodeU32(code, slots[nullable.index]) &&
				   mirCodeU32(code, 1) && mirCodeU32(code, 0) && mirCodeU32(code, 1) && mirCodeU32(code, 1);
		}
		case MIR_OP_NEG: {
			auto& unary = static_cast<MirUnaryInstruction&>(instruction);
			const ls_type_kind kind = mirTypeKind(unary.type);
			const u32 index = mirNumericIndex(kind);
			const u32 size = typeByteSize(*unary.type);
			if (index == MIR_INVALID_ID) return false;
			if (!mirCodeU8(code, LS_OP_COPY) || !mirCodeU32(code, result) || !mirCodeU32(code, slots[unary.operand]) || !mirCodeU32(code, size)) return false;
			return mirCodeU8(code, (u8)(LS_OP_NEG_I8 + index)) && mirCodeU32(code, result);
		}
		case MIR_OP_NOT: {
			auto& unary = static_cast<MirUnaryInstruction&>(instruction);
			if (mirTypeKind(unary.type) != LS_TYPE_BOOL) return false;
			if (!mirCodeU8(code, LS_OP_COPY) || !mirCodeU32(code, result) || !mirCodeU32(code, slots[unary.operand]) || !mirCodeU32(code, 1)) return false;
			return mirCodeU8(code, LS_OP_NOT) && mirCodeU32(code, result);
		}
		case MIR_OP_CAST: {
			auto& cast = static_cast<MirCastInstruction&>(instruction);
			if (mirTypeKind(cast.operand_type) == LS_TYPE_INVALID || mirTypeKind(cast.type) == LS_TYPE_INVALID) return false;
			if (cast.operand == MIR_INVALID_ID) return false;
			return mirCodeU8(code, LS_OP_CAST) && mirCodeU32(code, result) && mirCodeU32(code, slots[cast.operand]) && mirCodeU8(code, (u8)mirTypeKind(cast.operand_type)) &&
				   mirCodeU8(code, (u8)mirTypeKind(cast.type));
		}
		case MIR_OP_CALL: {
			auto& call = static_cast<MirCallInstruction&>(instruction);
			if (call.call_target == MIR_CALL_INDIRECT) {
				if (call.callee == MIR_INVALID_ID) return false;
				const u32 arg_base = result + 4;
				if (!mirCodeU8(code, LS_OP_COPY) || !mirCodeU32(code, result) || !mirCodeU32(code, slots[call.callee]) || !mirCodeU32(code, 4)) return false;
				for (u32 i = 0, offset = 0; i < call.arguments.count; ++i) {
					const u32 size = call.arguments.sizes[i];
					if (!mirCodeU8(code, LS_OP_COPY) || !mirCodeU32(code, arg_base + offset) || !mirCodeU32(code, slots[call.arguments.values[i]]) || !mirCodeU32(code, size)) return false;
					offset += size;
				}
				return mirCodeU8(code, LS_OP_CALL_INDIRECT) && mirCodeU32(code, result) && mirCodeU32(code, call.args_size) && mirCodeU32(code, typeByteSize(*call.type));
			}
			if (call.function == MIR_INVALID_ID) return false;
			for (u32 i = 0, offset = 0; i < call.arguments.count; ++i) {
				const u32 size = call.arguments.sizes[i];
				if (!mirCodeU8(code, LS_OP_COPY) || !mirCodeU32(code, result + offset) || !mirCodeU32(code, slots[call.arguments.values[i]]) || !mirCodeU32(code, size)) return false;
				offset += size;
			}
			return mirCodeU8(code, LS_OP_CALL_DIRECT) && mirCodeU32(code, call.function) && mirCodeU32(code, result);
		}
		default: return false;
	}
}

static bool mirEmitTerminator(MirCode& code, MirTerminator& terminator, u32* slots, u32 return_size, ExpArray<MirJumpPatch>& patches) {
	switch (terminator.kind) {
		case MIR_TERM_JUMP: {
			if (!mirCodeU8(code, LS_OP_JUMP)) return false;
			MirJumpPatch& patch = patches.emplace_back();
			patch.operand = code.size;
			patch.target = terminator.targets[0];
			return mirCodeBytes(code, "\0\0", 2);
		}
		case MIR_TERM_BRANCH: {
			if (!mirCodeU8(code, LS_OP_JNZ_U8) || !mirCodeU32(code, slots[terminator.value])) return false;
			MirJumpPatch& patch = patches.emplace_back();
			patch.operand = code.size;
			patch.target = terminator.targets[0];
			if (!mirCodeBytes(code, "\0\0", 2)) return false;
			if (!mirCodeU8(code, LS_OP_JUMP)) return false;
			MirJumpPatch& false_patch = patches.emplace_back();
			false_patch.operand = code.size;
			false_patch.target = terminator.targets[1];
			return mirCodeBytes(code, "\0\0", 2);
		}
		case MIR_TERM_RETURN: return mirCodeU8(code, LS_OP_RETURN_BASE);
		case MIR_TERM_RETURN_VALUE: {
			const u32 source = slots[terminator.value];
			if (source == 0) return mirCodeU8(code, LS_OP_RETURN_BASE);
			return mirCodeU8(code, LS_OP_RETURN) && mirCodeU32(code, source) && mirCodeU32(code, return_size);
		}
		default: return false;
	}
}

static bool mirCompileFunction(ls_bytecode& bytecode, MirFunction* mir) {
	if (!mir || mir->blocks.empty()) return false;

	ls_function_bc* function = nullptr;
	if (bytecode.function_count >= bytecode.function_capacity) {
		u32 capacity = bytecode.function_capacity ? bytecode.function_capacity * 2u : 4u;
		ls_function_bc* next = (ls_function_bc*)bytecode.arena->allocate(bytecode.arena->user_data, sizeof(ls_function_bc) * capacity, alignof(ls_function_bc));
		if (!next) return false;
		if (bytecode.functions) memcpy(next, bytecode.functions, sizeof(ls_function_bc) * bytecode.function_count);
		bytecode.functions = next;
		bytecode.function_capacity = capacity;
	}
	function = &bytecode.functions[bytecode.function_count++];
	memset(function, 0, sizeof(*function));
	function->name = mir->name;
	function->kind = LS_FUNCTION_SCRIPT;
	function->return_kind = mirTypeKind(mir->return_type);
	function->return_size = mir->return_type ? typeByteSize(*mir->return_type) : 0;

	u32* slots = (u32*)bytecode.arena->allocate(bytecode.arena->user_data, sizeof(u32) * mir->next_value, alignof(u32));
	if (mir->next_value && !slots) return false;
	for (u32 i = 0; i < mir->next_value; ++i) slots[i] = MIR_INVALID_ID;
	u32 frame_size = 0;
	for (MirLocal& local : mir->locals) {
		if (!local.type) return false;
		local.offset = frame_size;
		frame_size += typeByteSize(*local.type);
		if (frame_size == 0) frame_size = 1;
	}
	function->param_size = mir->param_size;
	for (MirBlock& b : mir->blocks) {
		for (MirInstruction* instruction : b.instructions) {
			if (instruction->result != MIR_INVALID_ID && !instruction->type) return false;
			if (instruction->result == MIR_INVALID_ID || slots[instruction->result] != MIR_INVALID_ID) continue;
			if (instruction->opcode == MIR_OP_LOCAL_ADDRESS) {
				auto& address = static_cast<MirAddressInstruction&>(*instruction);
				slots[instruction->result] = mir->locals[address.local].offset + address.byte_offset;
				continue;
			}
			if (instruction->opcode == MIR_OP_GLOBAL_ADDRESS) {
				auto& address = static_cast<MirAddressInstruction&>(*instruction);
				slots[instruction->result] = MIR_GLOBAL_SLOT | address.global_offset;
				continue;
			}
			slots[instruction->result] = frame_size;
			u32 value_size = instruction->opcode == MIR_OP_CONST && static_cast<MirConstInstruction*>(instruction)->kind == MIR_CONST_I32 ? 4u : typeByteSize(*instruction->type);
			if (instruction->opcode == MIR_OP_SLICE_LENGTH && value_size < 8) value_size = 8;
			frame_size += value_size;
			if (instruction->opcode == MIR_OP_CALL) {
				auto& call = static_cast<MirCallInstruction&>(*instruction);
				const u32 call_extra = call.call_target == MIR_CALL_INDIRECT ? 4 : 0;
				if (call_extra + call.args_size > typeByteSize(*call.type)) frame_size += call_extra + call.args_size - typeByteSize(*call.type);
			}
			if (frame_size == 0) frame_size = 1;
		}
	}

	MirCode code(*bytecode.arena);
	ExpArray<u32> block_offsets(*bytecode.arena);
	ExpArray<MirJumpPatch> patches(*bytecode.arena);
	for (MirBlock& block : mir->blocks) {
		block_offsets.push_back(code.size);
		for (MirInstruction* instruction : block.instructions) {
			if (instruction->opcode == MIR_OP_CALL) {
				auto& call = static_cast<MirCallInstruction&>(*instruction);
				if (call.call_target == MIR_CALL_DIRECT && call.function == MIR_INVALID_ID && call.call_name.begin) {
					for (u32 i = 0; i < bytecode.function_count; ++i) {
						const ls_string_view name = bytecode.functions[i].name;
						const size_t length = (size_t)(call.call_name.end - call.call_name.begin);
						if ((size_t)(name.end - name.begin) == length && memcmp(name.begin, call.call_name.begin, length) == 0) {
							call.function = i;
							break;
						}
					}
				}
				for (u32 i = 0; i < call.arguments.count; ++i)
					if (!call.arguments.values || !call.arguments.sizes || call.arguments.values[i] == MIR_INVALID_ID || call.arguments.values[i] >= mir->next_value) return false;
			}

			switch (instruction->opcode) {
				case MIR_OP_MAKE_SLICE: {
					auto* slice = static_cast<MirSliceInstruction*>(instruction);
					if (slice->base == MIR_INVALID_ID || slice->base >= mir->next_value) return false;
					if (slice->mode == MIR_SLICE_PARTIAL && (slice->begin == MIR_INVALID_ID || slice->begin >= mir->next_value || slice->end == MIR_INVALID_ID || slice->end >= mir->next_value))
						return false;
					break;
				}
				case MIR_OP_STORE: {
					auto* store = static_cast<MirStoreInstruction*>(instruction);
					if (store->address == MIR_INVALID_ID || store->address >= mir->next_value) return false;
					if (store->access != MIR_ACCESS_PLAIN && (store->index == MIR_INVALID_ID || store->index >= mir->next_value)) return false;
					if (store->value == MIR_INVALID_ID || store->value >= mir->next_value) return false;
					break;
				}
				case MIR_OP_LOAD: {
					auto* load = static_cast<MirLoadInstruction*>(instruction);
					if (load->address == MIR_INVALID_ID || load->address >= mir->next_value) return false;
					if (load->access != MIR_ACCESS_PLAIN && (load->index == MIR_INVALID_ID || load->index >= mir->next_value)) return false;
					break;
				}
				case MIR_OP_CAST: {
					auto* cast = static_cast<MirCastInstruction*>(instruction);
					if (cast->operand == MIR_INVALID_ID || cast->operand >= mir->next_value) return false;
					break;
				}
				case MIR_OP_SLICE_LENGTH: {
					auto* unary = static_cast<MirUnaryInstruction*>(instruction);
					if (unary->operand == MIR_INVALID_ID || unary->operand >= mir->next_value) return false;
					break;
				}
				case MIR_OP_NULLABLE_HAS_VALUE: {
					auto* nullable = static_cast<MirNullableInstruction*>(instruction);
					if (nullable->address == MIR_INVALID_ID || nullable->address >= mir->next_value) return false;
					if (nullable->index == MIR_INVALID_ID || nullable->index >= mir->next_value) return false;
					break;
				}
				case MIR_OP_EQ:
				case MIR_OP_NE:
				case MIR_OP_LT:
				case MIR_OP_LE:
				case MIR_OP_GT:
				case MIR_OP_GE:
				case MIR_OP_DIV:
				case MIR_OP_MOD:
				case MIR_OP_ADD:
				case MIR_OP_MUL: {
					auto* binary = static_cast<MirBinaryInstruction*>(instruction);
					if (binary->lhs == MIR_INVALID_ID || binary->lhs >= mir->next_value) return false;
					if (binary->rhs == MIR_INVALID_ID || binary->rhs >= mir->next_value) return false;
					break;
				}
			}
			if (instruction->opcode == MIR_OP_LOCAL_ADDRESS) {
				auto& address = static_cast<MirAddressInstruction&>(*instruction);
				if (address.local >= (u32)mir->locals.size()) return false;
			}
			if (!mirEmitInstruction(bytecode, code, *instruction, slots)) return false;
		}
		if (block.terminator.kind == MIR_TERM_BRANCH && (block.terminator.value == MIR_INVALID_ID || block.terminator.value >= mir->next_value)) return false;
		if (block.terminator.kind == MIR_TERM_RETURN_VALUE && (block.terminator.value == MIR_INVALID_ID || block.terminator.value >= mir->next_value)) return false;
		if (!block.has_terminator || !mirEmitTerminator(code, block.terminator, slots, function->return_size, patches)) return false;
	}
	for (MirJumpPatch& patch : patches) {
		if (patch.target >= (u32)block_offsets.size()) return false;
		const i32 offset = (i32)block_offsets[patch.target] - (i32)(patch.operand + 2u);
		if (offset < -32768 || offset > 32767) return false;
		const i16 value = (i16)offset;
		memcpy(code.data + patch.operand, &value, sizeof(value));
	}
	function->frame_size = frame_size;
	function->code = code.data;
	function->code_size = code.size;
	return true;
}

static bool mirAppendNativeFunction(ls_bytecode& bytecode, const MirNativeFunction& source) {
	FunctionResolvedType* type = source.type;
	if (!type) return false;
	if (bytecode.function_count >= bytecode.function_capacity) {
		u32 capacity = bytecode.function_capacity ? bytecode.function_capacity * 2u : 4u;
		ls_function_bc* next = (ls_function_bc*)bytecode.arena->allocate(bytecode.arena->user_data, sizeof(ls_function_bc) * capacity, alignof(ls_function_bc));
		if (!next) return false;
		if (bytecode.functions) memcpy(next, bytecode.functions, sizeof(ls_function_bc) * bytecode.function_count);
		bytecode.functions = next;
		bytecode.function_capacity = capacity;
	}
	ls_function_bc& function = bytecode.functions[bytecode.function_count++];
	memset(&function, 0, sizeof(function));
	function.name = source.name;
	function.kind = LS_FUNCTION_NATIVE;
	function.is_builtin_native = true;
	function.return_kind = mirTypeKind(type->return_type);
	function.return_size = type->return_type ? typeByteSize(*type->return_type) : 0;
	for (FunctionResolvedParam& parameter : type->params) {
		if (!parameter.is_comptime && parameter.type) function.param_size += typeByteSize(*parameter.type);
	}
	return true;
}

ls_bytecode* mirCompileModuleBytecode(MirModule* mir_module, ls_host* host) {
	if (!mir_module || !host || !host->arena.allocate) return nullptr;
	ls_bytecode* bytecode = (ls_bytecode*)calloc(1, sizeof(ls_bytecode));
	if (!bytecode) return nullptr;
	bytecode->host = host;
	bytecode->arena = &host->arena;
	bytecode->global_size = mir_module->global_size;
	for (const MirModuleFunction& entry : mir_module->functions) {
		if (entry.is_native) {
			if (!mirAppendNativeFunction(*bytecode, entry.native)) {
				ls_bytecode_destroy(bytecode);
				return nullptr;
			}
		} else if (!mirCompileFunction(*bytecode, entry.function)) {
			ls_bytecode_destroy(bytecode);
			return nullptr;
		}
	}
	MirFunction* global_init = mir_module->global_init;
	if (!global_init) {
		ls_bytecode_destroy(bytecode);
		return nullptr;
	}
	if (!global_init->blocks[0].instructions.empty()) {
		if (!mirCompileFunction(*bytecode, global_init)) {
			ls_bytecode_destroy(bytecode);
			return nullptr;
		}
		bytecode->has_global_init = true;
	}
	return bytecode;
}
