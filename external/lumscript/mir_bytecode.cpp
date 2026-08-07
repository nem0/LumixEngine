#include "bytecode.h"
#include "mir.h"
#include "expressions.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>

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

static ls_type_kind mirTypeKind(const ResolvedType* type);

static u32 mirTypeInfo(ls_bytecode& bc, ResolvedType* type) {
	if (!type) return LS_TYPE_INDEX_NONE;
	if (bc.type_info_count == bc.type_info_capacity) {
		u32 capacity = bc.type_info_capacity ? bc.type_info_capacity * 2u : 16u;
		ls_type* next = (ls_type*)bc.arena->allocate(bc.arena->user_data, sizeof(ls_type) * capacity, alignof(ls_type));
		if (!next) return LS_TYPE_INDEX_NONE;
		if (bc.type_info) memcpy(next, bc.type_info, sizeof(ls_type) * bc.type_info_count);
		bc.type_info = next; bc.type_info_capacity = capacity;
	}
	u32 index = bc.type_info_count++;
	memset(&bc.type_info[index], 0, sizeof(bc.type_info[index]));
	bc.type_info[index].bytecode = &bc; bc.type_info[index].kind = mirTypeKind(type); bc.type_info[index].byte_size = typeByteSize(*type);
	bc.type_info[index].element_type_index = LS_TYPE_INDEX_NONE; bc.type_info[index].array_length = LS_TYPE_INDEX_NONE;
	u32 element_type_index = LS_TYPE_INDEX_NONE;
	if (type->kind == ResolvedType::POINTER) element_type_index = mirTypeInfo(bc, static_cast<PointerResolvedType*>(type)->inner);
	if (type->kind == ResolvedType::SLICE) element_type_index = mirTypeInfo(bc, static_cast<SliceResolvedType*>(type)->element_type);
	if (type->kind == ResolvedType::ARRAY) { auto* a = static_cast<ArrayResolvedType*>(type); element_type_index = mirTypeInfo(bc, a->element_type); }
	if (type->kind == ResolvedType::NULLABLE) element_type_index = mirTypeInfo(bc, static_cast<NullableResolvedType*>(type)->inner);
	bc.type_info[index].element_type_index = element_type_index;
	if (type->kind == ResolvedType::ARRAY) bc.type_info[index].array_length = (u32)static_cast<ArrayResolvedType*>(type)->size;
	if (type->kind == ResolvedType::UNION) { auto* u = static_cast<UnionResolvedType*>(type); bc.type_info[index].kind = LS_TYPE_TAGGED_UNION; const u32 member_count = (u32)u->members.size(); bc.type_info[index].member_count = member_count; bc.type_info[index].first_member_index = bc.type_member_count; for (ResolvedType* m : u->members) { if (bc.type_member_count == bc.type_member_capacity) { u32 c = bc.type_member_capacity ? bc.type_member_capacity * 2u : 8u; u32* n = (u32*)bc.arena->allocate(bc.arena->user_data, sizeof(u32) * c, alignof(u32)); if (bc.type_member_indices) memcpy(n, bc.type_member_indices, sizeof(u32) * bc.type_member_count); bc.type_member_indices = n; bc.type_member_capacity = c; } bc.type_member_indices[bc.type_member_count++] = mirTypeInfo(bc, m); } }
	if (type->kind == ResolvedType::ENUM) { auto* e = static_cast<EnumResolvedType*>(type); bc.type_info[index].kind = LS_TYPE_ENUM; if (e->decl) { bc.type_info[index].name = e->decl->cached_name; const u32 value_count = (u32)e->decl->members.size(); bc.type_info[index].value_count = value_count; bc.type_info[index].first_value_index = bc.type_enum_value_count; i32 next = 0; for (u32 i = 0; i < value_count; ++i) { if (bc.type_enum_value_count == bc.type_enum_value_capacity) { u32 c = bc.type_enum_value_capacity ? bc.type_enum_value_capacity * 2u : 8u; auto* n = (ls_type_enum_value_info*)bc.arena->allocate(bc.arena->user_data, sizeof(ls_type_enum_value_info) * c, alignof(ls_type_enum_value_info)); if (bc.type_enum_values) memcpy(n, bc.type_enum_values, sizeof(ls_type_enum_value_info) * bc.type_enum_value_count); bc.type_enum_values = n; bc.type_enum_value_capacity = c; } auto& v = bc.type_enum_values[bc.type_enum_value_count++]; v.name = e->decl->members[i].name; v.value = e->decl->members[i].value && e->decl->members[i].value->kind == Expression::INT_LITERAL ? (i32)static_cast<IntLiteralExpression*>(e->decl->members[i].value)->value : next; next = v.value + 1; } } }
	if (type->kind == ResolvedType::STRUCT) {
		auto* s = static_cast<StructResolvedType*>(type);
		bc.type_info[index].kind = LS_TYPE_STRUCT;
		if (s->decl) {
			bc.type_info[index].name = s->decl->cached_name;
			const u32 field_count = (u32)s->decl->fields.size();
			bc.type_info[index].field_count = field_count;
			u32* field_indices = (u32*)bc.arena->allocate(bc.arena->user_data, sizeof(u32) * field_count, alignof(u32));
			ResolvedType** field_types = (ResolvedType**)bc.arena->allocate(bc.arena->user_data, sizeof(ResolvedType*) * field_count, alignof(ResolvedType*));
			for (u32 i = 0; i < field_count; ++i) {
				field_types[i] = i < (u32)s->field_types.size() ? s->field_types[i] : s->decl->fields[i].resolved_type;
				field_indices[i] = mirTypeInfo(bc, field_types[i]);
			}
			const u32 first_field_index = bc.type_field_count;
			for (u32 i = 0; i < field_count; ++i) {
				if (bc.type_field_count == bc.type_field_capacity) {
					u32 c = bc.type_field_capacity ? bc.type_field_capacity * 2u : 8u;
					auto* n = (ls_type_field_info*)bc.arena->allocate(bc.arena->user_data, sizeof(ls_type_field_info) * c, alignof(ls_type_field_info));
					if (bc.type_fields) memcpy(n, bc.type_fields, sizeof(ls_type_field_info) * bc.type_field_count);
					bc.type_fields = n; bc.type_field_capacity = c;
				}
				auto& f = bc.type_fields[bc.type_field_count++];
				f.name = s->decl->fields[i].name;
				f.type_index = field_indices[i];
				f.offset = 0;
				for (u32 j = 0; j < i; ++j) {
					if (field_types[j]) f.offset += typeByteSize(*field_types[j]);
				}
			}
			bc.type_info[index].first_field_index = first_field_index;
		}
	}
	return index;
}

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
static bool mirCodeI32(MirCode& code, i32 value) {
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
		case ResolvedType::ENUM: return LS_TYPE_I32;
		case ResolvedType::UNTYPED_INT: return LS_TYPE_I32;
		case ResolvedType::UNTYPED_FLOAT: return LS_TYPE_F64;
		case ResolvedType::POINTER: return LS_TYPE_CPTR;
		case ResolvedType::SLICE: return LS_TYPE_SLICE;
		case ResolvedType::ARRAY: return LS_TYPE_ARRAY;
		case ResolvedType::STRUCT: return LS_TYPE_STRUCT;
		case ResolvedType::NULLABLE: return LS_TYPE_NULLABLE;
		default: return LS_TYPE_INVALID;
	}
}

// ---- lowering-time optimizations -------------------------------------------
//
// The bytecode contract already gives the MIR backend the same primitives the
// legacy compiler uses (in-place argument windows, compare-and-branch
// opcodes, RETURN_BASE). These helpers let the MIR lowering recover that code
// quality without changing the SSA MIR: alias parameter reads onto the param
// slot, fuse a numeric comparison feeding a branch into a single
// compare-jump, and write single-use pure results directly into the callee's
// argument window or frame base.

struct MirFusedBranch {
	bool active;
	ls_op opcode;
	MirValueId lhs;
	MirValueId rhs;
	MirBlockId jump_target;
	bool need_trailing_jump;
	MirBlockId trailing_target;

	MirFusedBranch()
		: active(false), opcode(LS_OP_JUMP), lhs(MIR_INVALID_ID), rhs(MIR_INVALID_ID),
		  jump_target(MIR_INVALID_ID), need_trailing_jump(false), trailing_target(MIR_INVALID_ID) {}
};

static bool mirIsCompareOpcode(MirOpcode op) {
	return op >= MIR_OP_EQ && op <= MIR_OP_GE;
}

static MirOpcode mirNegateCompare(MirOpcode op) {
	switch (op) {
		case MIR_OP_EQ: return MIR_OP_NE;
		case MIR_OP_NE: return MIR_OP_EQ;
		case MIR_OP_LT: return MIR_OP_GE;
		case MIR_OP_LE: return MIR_OP_GT;
		case MIR_OP_GT: return MIR_OP_LE;
		case MIR_OP_GE: return MIR_OP_LT;
		default: return op;
	}
}

static ls_op mirCompareJumpOpcode(MirOpcode cmp, u32 type_index) {
	if (type_index == MIR_INVALID_ID) return LS_OP_JUMP;
	switch (cmp) {
		case MIR_OP_EQ: return (ls_op)((u32)LS_OP_JE_I8 + type_index * 5u + 0u);
		case MIR_OP_GE: return (ls_op)((u32)LS_OP_JE_I8 + type_index * 5u + 1u);
		case MIR_OP_GT: return (ls_op)((u32)LS_OP_JE_I8 + type_index * 5u + 2u);
		case MIR_OP_LT: return (ls_op)((u32)LS_OP_JE_I8 + type_index * 5u + 3u);
		case MIR_OP_LE: return (ls_op)((u32)LS_OP_JE_I8 + type_index * 5u + 4u);
		default: return LS_OP_JUMP;
	}
}

// True for pure instructions whose result slot can be remapped to the caller
// of a value without changing program semantics.
static bool mirIsMovableProducer(MirOpcode op) {
	switch (op) {
		case MIR_OP_CONST:
		case MIR_OP_UNDEFINED:
		case MIR_OP_ADD:
		case MIR_OP_SUB:
		case MIR_OP_MUL:
		case MIR_OP_DIV:
		case MIR_OP_MOD:
		case MIR_OP_NEG:
		case MIR_OP_NOT:
		case MIR_OP_CAST:
		case MIR_OP_LOAD:
		case MIR_OP_SLICE_LENGTH:
		case MIR_OP_NULLABLE_HAS_VALUE:
			return true;
		default:
			return mirIsCompareOpcode(op);
	}
}

static u32* mirComputeUseCounts(ls_bytecode& bytecode, MirFunction* mir) {
	const u32 value_count = mir->next_value ? mir->next_value : 1u;
	u32* uses = (u32*)bytecode.arena->allocate(bytecode.arena->user_data, sizeof(u32) * value_count, alignof(u32));
	if (!uses) return nullptr;
	memset(uses, 0, sizeof(u32) * value_count);
	auto count = [&](MirValueId v) {
		if (v != MIR_INVALID_ID && v < mir->next_value) ++uses[v];
	};
	for (MirBlock& block : mir->blocks) {
		for (MirInstruction* instr : block.instructions) {
			switch (instr->opcode) {
				case MIR_OP_LOAD: {
					auto* l = static_cast<MirLoadInstruction*>(instr);
					count(l->address); count(l->index);
					break;
				}
				case MIR_OP_STORE: {
					auto* s = static_cast<MirStoreInstruction*>(instr);
					count(s->address); count(s->index); count(s->value);
					break;
				}
				case MIR_OP_CALL: {
					auto* c = static_cast<MirCallInstruction*>(instr);
					count(c->callee);
					for (u32 i = 0; i < c->arguments.count; ++i) count(c->arguments.values[i]);
					break;
				}
				case MIR_OP_CAST: count(static_cast<MirCastInstruction*>(instr)->operand); break;
				case MIR_OP_NEG:
				case MIR_OP_NOT:
				case MIR_OP_SLICE_LENGTH: count(static_cast<MirUnaryInstruction*>(instr)->operand); break;
				case MIR_OP_MAKE_SLICE: {
					auto* s = static_cast<MirSliceInstruction*>(instr);
					count(s->base); count(s->begin); count(s->end);
					break;
				}
				case MIR_OP_NULLABLE_HAS_VALUE: {
					auto* n = static_cast<MirNullableInstruction*>(instr);
					count(n->address); count(n->index);
					break;
				}
				default: break;
			}
			if (mirIsCompareOpcode(instr->opcode) ||
				instr->opcode == MIR_OP_ADD || instr->opcode == MIR_OP_SUB || instr->opcode == MIR_OP_MUL ||
				instr->opcode == MIR_OP_DIV || instr->opcode == MIR_OP_MOD) {
				auto* b = static_cast<MirBinaryInstruction*>(instr);
				count(b->lhs); count(b->rhs);
			}
		}
		for (MirPhi& phi : block.phis) {
			for (MirPhiIncoming& incoming : phi.incoming) count(incoming.value);
		}
		if (block.terminator.kind == MIR_TERM_BRANCH || block.terminator.kind == MIR_TERM_RETURN_VALUE ||
			block.terminator.kind == MIR_TERM_RETURN_COPY)
			count(block.terminator.value);
	}
	return uses;
}

// Alias plain full-value loads of immutable parameters to the parameter's own
// frame slot. The loads become no-ops (and are skipped at emission), so the
// value is read straight from the param slot wherever it is used.
static u32* mirComputeLoadAliases(ls_bytecode& bytecode, MirFunction* mir) {
	const u32 count = mir->next_value ? mir->next_value : 1u;
	u32* alias = (u32*)bytecode.arena->allocate(bytecode.arena->user_data, sizeof(u32) * count, alignof(u32));
	if (!alias) return nullptr;
	for (u32 i = 0; i < count; ++i) alias[i] = MIR_INVALID_ID;
	u32* address_local = (u32*)bytecode.arena->allocate(bytecode.arena->user_data, sizeof(u32) * count, alignof(u32));
	if (!address_local) return nullptr;
	for (u32 i = 0; i < count; ++i) address_local[i] = MIR_INVALID_ID;
	for (MirBlock& block : mir->blocks) {
		for (MirInstruction* instr : block.instructions) {
			if (instr->opcode == MIR_OP_LOCAL_ADDRESS) {
				auto* address = static_cast<MirAddressInstruction*>(instr);
				if (address->result < mir->next_value) address_local[address->result] = address->local;
			}
		}
	}
	for (MirBlock& block : mir->blocks) {
		for (MirInstruction* instr : block.instructions) {
			if (instr->opcode != MIR_OP_LOAD) continue;
			auto* load = static_cast<MirLoadInstruction*>(instr);
			if (load->access != MIR_ACCESS_PLAIN || load->index != MIR_INVALID_ID) continue;
			if (load->address == MIR_INVALID_ID || load->address >= mir->next_value) continue;
			const u32 local = address_local[load->address];
			if (local == MIR_INVALID_ID || local >= (u32)mir->locals.size()) continue;
			const MirLocal& info = mir->locals[local];
			if (info.is_mutable) continue;
			if (info.offset >= mir->param_size) continue; // parameters only
			if (!load->type || !info.type) continue;
			if (typeByteSize(*load->type) != typeByteSize(*info.type)) continue;
			alias[load->result] = info.offset;
		}
	}
	return alias;
}

static MirInstruction* mirFindProducerInBlock(MirBlock& block, u32 end_exclusive, MirValueId value) {
	for (u32 i = 0; i < end_exclusive; ++i) {
		MirInstruction* instr = block.instructions[i];
		if (instr->result == value) return instr;
	}
	return nullptr;
}

// Rewrite slots so single-use pure results are produced directly where a call
// expects its argument, and so the final value of a function is produced at
// the frame base (letting the terminator become RETURN_BASE). Both are valid
// because the bytecode call contract makes the callee's argument window and
// frame base the caller's result location, and the remapped values are dead
// by the time the destination slot is live again.
static void mirCoalesceSlots(MirFunction* mir, u32* slots, u32* uses, u32* alias) {
	for (MirBlock& block : mir->blocks) {
		const u32 count = (u32)block.instructions.size();
		for (u32 i = 0; i < count; ++i) {
			MirInstruction* instr = block.instructions[i];
			if (instr->opcode != MIR_OP_CALL) continue;
			auto* call = static_cast<MirCallInstruction*>(instr);
			if (call->result == MIR_INVALID_ID || call->result >= mir->next_value) continue;
			const u32 call_slot = slots[call->result];
			if (call_slot == MIR_INVALID_ID) continue;
			const u32 arg_base = call_slot + (call->call_target == MIR_CALL_INDIRECT ? 4u : 0u);
			u32 offset = 0;
			for (u32 a = 0; a < call->arguments.count; ++a) {
				const MirValueId v = call->arguments.values[a];
				if (v != MIR_INVALID_ID && v < mir->next_value && uses[v] == 1u && alias[v] == MIR_INVALID_ID &&
					slots[v] != MIR_INVALID_ID) {
					u32 producer_index = count;
					for (u32 j = 0; j < i; ++j) {
						if (block.instructions[j]->result == v) { producer_index = j; break; }
					}
					// A nested call between the producer and this call raises its
					// frame above the producer and can clobber the staged slot
					// (the callee frame covers everything at/above its argument
					// window). Only fold when the producer is directly adjacent.
					bool intervening_call = false;
					for (u32 j = producer_index + 1u; j < i; ++j) {
						if (block.instructions[j]->opcode == MIR_OP_CALL) { intervening_call = true; break; }
					}
					if (!intervening_call && mirIsMovableProducer(block.instructions[producer_index]->opcode)) {
						slots[v] = arg_base + offset;
					}
				}
				offset += call->arguments.sizes[a];
			}
		}
	}
	for (MirBlock& block : mir->blocks) {
		if (block.terminator.kind != MIR_TERM_RETURN_VALUE) continue;
		const MirValueId v = block.terminator.value;
		if (v == MIR_INVALID_ID || v >= mir->next_value) continue;
		if (slots[v] == 0u) continue;
		if (uses[v] != 1u || alias[v] != MIR_INVALID_ID) continue;
		MirInstruction* producer = mirFindProducerInBlock(block, (u32)block.instructions.size(), v);
		// Deferred statements and other cleanups run between the return
		// expression and the actual return; they may write the frame base
		// (e.g. a defer storing to the local at offset 0), which would
		// corrupt a value folded into slot 0. Only fold when the producer is
		// the final instruction, so nothing can run after it.
		if (!producer || producer != block.instructions.back()) continue;
		if (!mirIsMovableProducer(producer->opcode)) continue;
		slots[v] = 0u;
	}
}

// Detect `if/while` conditions that are a pure numeric comparison whose result
// feeds only the block terminator, so the comparison can be folded into a
// compare-and-branch opcode.
static void mirFindFusedBranch(MirFunction* mir, u32 block_index, MirBlock& block, u32* uses, u32* slots, MirFusedBranch& out) {
	out.active = false;
	if (block.terminator.kind != MIR_TERM_BRANCH || block.instructions.empty()) return;
	MirInstruction* last = block.instructions.back();
	if (last->result == MIR_INVALID_ID || last->result != block.terminator.value) return;
	if (!mirIsCompareOpcode(last->opcode)) return;
	if (uses[last->result] != 1u) return;
	auto* bin = static_cast<MirBinaryInstruction*>(last);
	const ResolvedType* operand_type = bin->operand_type;
	if (!operand_type || operand_type->kind == ResolvedType::SLICE) return;
	const u32 type_index = mirNumericIndex(mirTypeKind(operand_type));
	if (type_index == MIR_INVALID_ID) return;
	if (bin->lhs >= mir->next_value || bin->rhs >= mir->next_value) return;
	if (slots[bin->lhs] == MIR_INVALID_ID || slots[bin->rhs] == MIR_INVALID_ID) return;

	const MirBlockId next = block_index + 1u < (u32)mir->blocks.size() ? mir->blocks[block_index + 1u].id : MIR_INVALID_ID;
	const bool next_is_true = block.terminator.targets[0] == next;
	const bool next_is_false = block.terminator.targets[1] == next;
	MirOpcode op = bin->opcode;
	if (next_is_true) op = mirNegateCompare(op);
	const ls_op jump_op = mirCompareJumpOpcode(op, type_index);
	if (jump_op == LS_OP_JUMP) return;

	out.active = true;
	out.opcode = jump_op;
	out.lhs = bin->lhs;
	out.rhs = bin->rhs;
	if (next_is_true) {
		out.jump_target = block.terminator.targets[1];
		out.need_trailing_jump = false;
	} else if (next_is_false) {
		out.jump_target = block.terminator.targets[0];
		out.need_trailing_jump = false;
	} else {
		out.jump_target = block.terminator.targets[0];
		out.need_trailing_jump = true;
		out.trailing_target = block.terminator.targets[1];
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

static bool mirEmitInstruction(ls_bytecode& bytecode, MirCode& code, MirInstruction& instruction, u32* slots, const MirFunction& mir) {
	if (!instruction.type) return false;
	const u32 result = instruction.result == MIR_INVALID_ID ? 0 : slots[instruction.result];
	switch (instruction.opcode) {
		case MIR_OP_CONST: {
			MirConstInstruction& mci = static_cast<MirConstInstruction&>(instruction);
			if (mci.string.begin && mci.type->kind == ResolvedType::CSTR) {
				u32 string_index = 0;
				if (!mirAppendStringLiteral(bytecode, mci.string, string_index)) return false;
				return mirCodeU8(code, LS_OP_LOAD_CONST_8) && mirCodeU32(code, result) &&
					mirCodeU64(code, (u64)(uintptr)bytecode.strings[string_index].begin);
			}
			if (mci.kind == MIR_CONST_I32) return mirCodeU8(code, LS_OP_LOAD_CONST_4) && mirCodeU32(code, result) && mirCodeU32(code, (u32)mci.integer);
			if (mci.type->kind == ResolvedType::F32) { f32 value = (f32)mci.floating; u32 bits; memcpy(&bits, &value, 4); return mirCodeU8(code, LS_OP_LOAD_CONST_4) && mirCodeU32(code, result) && mirCodeU32(code, bits); }
			if (mci.type->kind == ResolvedType::F64 || mci.type->kind == ResolvedType::UNTYPED_FLOAT) { u64 bits; memcpy(&bits, &mci.floating, 8); return mirCodeU8(code, LS_OP_LOAD_CONST_8) && mirCodeU32(code, result) && mirCodeU64(code, bits); }
			if (mci.kind == MIR_CONST_BYTES) {
				if (!mci.bytes) return false;
				u32 off = 0;
				u32 dst = result;
				const u32 size = mci.byte_size ? mci.byte_size : typeByteSize(*mci.type);
				while (size - off >= 8) {
					u64 v;
					memcpy(&v, mci.bytes + off, 8);
					if (!mirCodeU8(code, LS_OP_LOAD_CONST_8) || !mirCodeU32(code, dst) || !mirCodeU64(code, v)) return false;
					off += 8;
					dst += 8;
				}
				if (size - off >= 4) {
					u32 v;
					memcpy(&v, mci.bytes + off, 4);
					if (!mirCodeU8(code, LS_OP_LOAD_CONST_4) || !mirCodeU32(code, dst) || !mirCodeU32(code, v)) return false;
					off += 4;
					dst += 4;
				}
				if (size - off >= 2) {
					u16 v;
					memcpy(&v, mci.bytes + off, 2);
					if (!mirCodeU8(code, LS_OP_LOAD_CONST_2) || !mirCodeU32(code, dst) || !mirCodeU16(code, v)) return false;
					off += 2;
					dst += 2;
				}
				if (size - off >= 1) {
					if (!mirCodeU8(code, LS_OP_LOAD_CONST_1) || !mirCodeU32(code, dst) || !mirCodeU8(code, mci.bytes[off])) return false;
				}
				return true;
			}
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
					if (mci.type->kind == ResolvedType::NULLABLE || mci.type->kind == ResolvedType::UNION || mci.type->kind == ResolvedType::STRUCT || mci.type->kind == ResolvedType::ARRAY) return mirCodeU8(code, LS_OP_LOAD_CONST_4) && mirCodeU32(code, result) && mirCodeU32(code, bits);
					return mirCodeU8(code, LS_OP_LOAD_CONST_4) && mirCodeU32(code, result) && mirCodeU32(code, bits);
				}
				case 5: return mirCodeU8(code, LS_OP_LOAD_CONST_1) && mirCodeU32(code, result) && mirCodeU8(code, (u8)mci.integer) && mirCodeU8(code, LS_OP_LOAD_CONST_4) && mirCodeU32(code, result + 1) && mirCodeU32(code, (u32)(mci.integer >> 8));
				case 8: {
					u64 bits = mci.integer;
					if (mci.type->kind == ResolvedType::F64 || mci.type->kind == ResolvedType::UNTYPED_FLOAT) memcpy(&bits, &mci.floating, sizeof(bits));
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
		case MIR_OP_REFERENCE: {
			auto& address = static_cast<MirAddressInstruction&>(instruction);
			if (address.local != MIR_INVALID_ID)
				return mirCodeU8(code, LS_OP_LOCAL_REF) && mirCodeU32(code, result) && mirCodeU32(code, mir.locals[address.local].offset + address.byte_offset);
			return mirCodeU8(code, LS_OP_GLOBAL_REF) && mirCodeU32(code, result) && mirCodeU32(code, address.global_offset);
		}
		case MIR_OP_MAKE_SLICE: {
			auto& slice = static_cast<MirSliceInstruction&>(instruction);
			if (typeByteSize(*instruction.type) != 16) return false;
			if (slice.mode == MIR_SLICE_PARTIAL)
				return mirCodeU8(code, LS_OP_COPY) && mirCodeU32(code, result) && mirCodeU32(code, slots[slice.base]) && mirCodeU32(code, 16) && mirCodeU8(code, LS_OP_SLICE) &&
					   mirCodeU32(code, result) && mirCodeU32(code, slots[slice.begin]) && mirCodeU32(code, slots[slice.end]) && mirCodeU32(code, slice.element_size);
			if (slice.base_is_pointer) {
				// The pointer value is already in the frame; construct the slice around it.
				return mirCodeU8(code, LS_OP_COPY) && mirCodeU32(code, result) && mirCodeU32(code, slots[slice.base]) && mirCodeU32(code, sizeof(void*)) && mirCodeU8(code, LS_OP_LOAD_CONST_8) && mirCodeU32(code, result + 8) &&
					mirCodeU64(code, slice.length);
			}
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
					if (mirIsGlobalSlot(slots[load.address]))
						return mirCodeU8(code, LS_OP_GLOBAL_LOAD) && mirCodeU32(code, result) && mirCodeU32(code, mirGlobalOffset(slots[load.address]) + load.field_offset) &&
							   mirCodeU32(code, typeByteSize(*instruction.type));
					return mirCodeU8(code, LS_OP_LOAD_INDEXED_LOCAL_I32) && mirCodeU32(code, result) && mirCodeU32(code, slots[load.address]) && mirCodeU32(code, slots[load.index]) &&
						   mirCodeU32(code, load.element_size) && mirCodeU32(code, load.field_offset) && mirCodeU32(code, load.extent) && mirCodeU32(code, typeByteSize(*instruction.type));
				case MIR_ACCESS_SLICE_ELEMENT:
					return mirCodeU8(code, LS_OP_SLICE_LOAD_LOCAL_I32) && mirCodeU32(code, result) && mirCodeU32(code, slots[load.address]) && mirCodeU32(code, slots[load.index]) &&
						   mirCodeU32(code, load.element_size);
				case MIR_ACCESS_SLICE_FIELD:
					return mirCodeU8(code, LS_OP_SLICE_LOAD_AT_LOCAL_I32) && mirCodeU32(code, result) && mirCodeU32(code, slots[load.address]) && mirCodeU32(code, slots[load.index]) &&
						   mirCodeU32(code, load.element_size) && mirCodeU32(code, load.field_offset) && mirCodeU32(code, load.extent);
				case MIR_ACCESS_POINTER:
					if (mirIsGlobalSlot(slots[load.address])) return mirCodeU8(code, LS_OP_GLOBAL_LOAD) && mirCodeU32(code, result) && mirCodeU32(code, mirGlobalOffset(slots[load.address]) + load.field_offset) && mirCodeU32(code, typeByteSize(*instruction.type));
					return mirCodeU8(code, LS_OP_LOAD_INDEXED) && mirCodeU32(code, result) && mirCodeU32(code, slots[load.address]) && mirCodeU32(code, slots[load.index]) &&
						   mirCodeU32(code, load.element_size) && mirCodeI32(code, (i32)load.field_offset) && mirCodeU32(code, typeByteSize(*instruction.type));
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
				case MIR_ACCESS_POINTER:
					if (mirIsGlobalSlot(slots[store.address])) return mirCodeU8(code, LS_OP_GLOBAL_STORE) && mirCodeU32(code, mirGlobalOffset(slots[store.address]) + store.field_offset) && mirCodeU32(code, slots[store.value]) && mirCodeU32(code, typeByteSize(*instruction.type));
					return mirCodeU8(code, LS_OP_STORE_INDEXED) && mirCodeU32(code, slots[store.address]) && mirCodeU32(code, slots[store.index]) && mirCodeU32(code, slots[store.value]) &&
						   mirCodeU32(code, store.element_size) && mirCodeI32(code, (i32)store.field_offset) && mirCodeU32(code, typeByteSize(*instruction.type));
				case MIR_ACCESS_NULLABLE_TAG:
					if (mirIsGlobalSlot(slots[store.address]))
						return mirCodeU8(code, LS_OP_GLOBAL_STORE) && mirCodeU32(code, mirGlobalOffset(slots[store.address]) + store.field_offset) && mirCodeU32(code, slots[store.value]) &&
							   mirCodeU32(code, 1);
					return mirCodeU8(code, LS_OP_STORE_INDEXED_LOCAL_I32) && mirCodeU32(code, slots[store.address]) && mirCodeU32(code, slots[store.index]) && mirCodeU32(code, slots[store.value]) &&
						   mirCodeU32(code, store.element_size) && mirCodeU32(code, store.field_offset) && mirCodeU32(code, store.extent) && mirCodeU32(code, 1);
				case MIR_ACCESS_INDEXED:
					if (mirIsGlobalSlot(slots[store.address]))
						return mirCodeU8(code, LS_OP_GLOBAL_STORE) && mirCodeU32(code, mirGlobalOffset(slots[store.address]) + store.field_offset) && mirCodeU32(code, slots[store.value]) &&
							   mirCodeU32(code, typeByteSize(*instruction.type));
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
			const ls_type_kind operand_kind = mirTypeKind(binary.operand_type);
			if (operand_kind == LS_TYPE_BOOL && (binary.opcode == MIR_OP_EQ || binary.opcode == MIR_OP_NE)) {
				return mirCodeU8(code, (u8)mirCompareOpcode(binary.opcode)) && mirCodeU32(code, result) && mirCodeU32(code, slots[binary.lhs]) &&
					mirCodeU32(code, slots[binary.rhs]) && mirCodeU8(code, (u8)operand_kind);
			}
			if (mirNumericIndex(operand_kind) == MIR_INVALID_ID) return false;
			{
				ls_op op = mirCompareOpcode(binary.opcode);
				const bool swap = binary.opcode == MIR_OP_GT || binary.opcode == MIR_OP_GE;
				if (swap) op = binary.opcode == MIR_OP_GT ? LS_OP_LT : LS_OP_LE;
				const MirValueId lhs = swap ? binary.rhs : binary.lhs;
				const MirValueId rhs = swap ? binary.lhs : binary.rhs;
				return mirCodeU8(code, (u8)op) && mirCodeU32(code, result) && mirCodeU32(code, slots[lhs]) && mirCodeU32(code, slots[rhs]) &&
					   mirCodeU8(code, (u8)mirTypeKind(binary.operand_type));
			}
		}
		case MIR_OP_NULLABLE_HAS_VALUE: {
			auto& nullable = static_cast<MirNullableInstruction&>(instruction);
			if (mirIsGlobalSlot(slots[nullable.address]))
				return mirCodeU8(code, LS_OP_GLOBAL_LOAD) && mirCodeU32(code, result) && mirCodeU32(code, mirGlobalOffset(slots[nullable.address])) && mirCodeU32(code, 1);
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
			const ls_type_kind operand_kind = cast.operand_type->kind == ResolvedType::ENUM ? LS_TYPE_U32 : mirTypeKind(cast.operand_type);
			return mirCodeU8(code, LS_OP_CAST) && mirCodeU32(code, result) && mirCodeU32(code, slots[cast.operand]) && mirCodeU8(code, (u8)operand_kind) &&
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
					const u32 src = slots[call.arguments.values[i]];
					const u32 dst = arg_base + offset;
					if (dst != src && (!mirCodeU8(code, LS_OP_COPY) || !mirCodeU32(code, dst) || !mirCodeU32(code, src) || !mirCodeU32(code, size))) return false;
					offset += size;
				}
				return mirCodeU8(code, LS_OP_CALL_INDIRECT) && mirCodeU32(code, result) && mirCodeU32(code, call.args_size) && mirCodeU32(code, typeByteSize(*call.type));
			}
			if (call.function == MIR_INVALID_ID) return false;
			for (u32 i = 0, offset = 0; i < call.arguments.count; ++i) {
				const u32 size = call.arguments.sizes[i];
				const u32 src = slots[call.arguments.values[i]];
				const u32 dst = result + offset;
				if (dst != src && (!mirCodeU8(code, LS_OP_COPY) || !mirCodeU32(code, dst) || !mirCodeU32(code, src) || !mirCodeU32(code, size))) return false;
				offset += size;
			}
			return mirCodeU8(code, LS_OP_CALL_DIRECT) && mirCodeU32(code, call.function) && mirCodeU32(code, result);
		}
		default: return false;
	}
}

static bool mirEmitTerminator(MirCode& code, MirTerminator& terminator, u32* slots, u32 return_size, ExpArray<MirJumpPatch>& patches, const MirFusedBranch& fused) {
	switch (terminator.kind) {
		case MIR_TERM_JUMP: {
			if (!mirCodeU8(code, LS_OP_JUMP)) return false;
			MirJumpPatch& patch = patches.emplace_back();
			patch.operand = code.size;
			patch.target = terminator.targets[0];
			return mirCodeBytes(code, "\0\0", 2);
		}
		case MIR_TERM_BRANCH: {
			if (fused.active) {
				if (!mirCodeU8(code, (u8)fused.opcode)) return false;
				if (!mirCodeU32(code, slots[fused.lhs])) return false;
				if (!mirCodeU32(code, slots[fused.rhs])) return false;
				MirJumpPatch& patch = patches.emplace_back();
				patch.operand = code.size;
				patch.target = fused.jump_target;
				if (!mirCodeBytes(code, "\0\0", 2)) return false;
				if (fused.need_trailing_jump) {
					if (!mirCodeU8(code, LS_OP_JUMP)) return false;
					MirJumpPatch& false_patch = patches.emplace_back();
					false_patch.operand = code.size;
					false_patch.target = fused.trailing_target;
					return mirCodeBytes(code, "\0\0", 2);
				}
				return true;
			}
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
			if (source == MIR_INVALID_ID) return false;
			if (source == 0u) return mirCodeU8(code, LS_OP_RETURN_BASE);
			return mirCodeU8(code, LS_OP_RETURN) && mirCodeU32(code, source) && mirCodeU32(code, return_size);
		}
		default: return false;
	}
}

static bool mirCompileFunction(ls_bytecode& bytecode, MirFunction* mir) {
	if (!mir || mir->blocks.empty()) {
		if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL function '%s': no blocks\n", mir && mir->name.begin ? mir->name.begin : "?");
		return false;
	}

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
	function->local_count = 0;
	u32 frame_size = 0;
	for (MirLocal& local : mir->locals) { local.offset = frame_size; frame_size += typeByteSize(*local.type); if (frame_size == 0) frame_size = 1; }
	for (MirLocal& local : mir->locals) if (!local.compiler_generated) ++function->local_count;
	u32* alias = mirComputeLoadAliases(bytecode, mir);
	u32* uses = mirComputeUseCounts(bytecode, mir);
	if (!alias || !uses) return false;
	if (function->local_count) {
		function->locals = (ls_bytecode_local_debug_entry*)bytecode.arena->allocate(bytecode.arena->user_data, sizeof(ls_bytecode_local_debug_entry) * function->local_count, alignof(ls_bytecode_local_debug_entry));
		u32 debug_index = 0;
		for (MirLocal& local : mir->locals) {
			if (local.compiler_generated) continue;
			function->locals[debug_index].name = local.name;
			function->locals[debug_index].offset = local.offset;
			function->locals[debug_index].byte_size = typeByteSize(*local.type);
			function->locals[debug_index].kind = mirTypeKind(local.type);
			function->locals[debug_index].type_index = mirTypeInfo(bytecode, local.type);
			function->locals[debug_index].scope_begin_offset = 0;
			++debug_index;
		}
	}

	u32* slots = (u32*)bytecode.arena->allocate(bytecode.arena->user_data, sizeof(u32) * mir->next_value, alignof(u32));
	if (mir->next_value && !slots) return false;
	for (u32 i = 0; i < mir->next_value; ++i) slots[i] = MIR_INVALID_ID;
	u32 local_index = 0;
	for (MirLocal& local : mir->locals) {
		if (!local.type) {
				if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL function '%s': local %u without type\n", mir->name.begin ? mir->name.begin : "?", local_index);
			return false;
		}
		++local_index;
	}
	function->param_size = mir->param_size;
	for (MirBlock& b : mir->blocks) {
		for (MirInstruction* instruction : b.instructions) {
			if (instruction->result != MIR_INVALID_ID && !instruction->type) {
				if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL function '%s': instr opcode=%d has no type (result=%u)\n", mir->name.begin ? mir->name.begin : "?", (int)instruction->opcode, instruction->result);
				return false;
			}
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
			if (alias[instruction->result] != MIR_INVALID_ID) {
				slots[instruction->result] = alias[instruction->result];
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
	mirCoalesceSlots(mir, slots, uses, alias);

	MirCode code(*bytecode.arena);
	ExpArray<u32> block_offsets(*bytecode.arena);
	ExpArray<MirJumpPatch> patches(*bytecode.arena);
	ExpArray<ls_bytecode_source_map_entry> source_map(*bytecode.arena);
	for (u32 block_index = 0; block_index < (u32)mir->blocks.size(); ++block_index) {
		MirBlock& block = mir->blocks[block_index];
		block_offsets.push_back(code.size);
		MirFusedBranch fused;
		mirFindFusedBranch(mir, block_index, block, uses, slots, fused);
		u32 instruction_index = 0;
		for (MirInstruction* instruction : block.instructions) {
			if (instruction->result != MIR_INVALID_ID && alias[instruction->result] != MIR_INVALID_ID) {
				++instruction_index;
				continue;
			}
			if (fused.active && instruction->result == block.terminator.value) {
				++instruction_index;
				continue;
			}
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
				if (!call.arguments.values || !call.arguments.sizes || call.arguments.values[i] == MIR_INVALID_ID || call.arguments.values[i] >= mir->next_value) {
					if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL call arg %u invalid (next=%u)\n", i, mir->next_value);
					return false;
				}
			}

			switch (instruction->opcode) {
				case MIR_OP_MAKE_SLICE: {
					auto* slice = static_cast<MirSliceInstruction*>(instruction);
					if (slice->base == MIR_INVALID_ID || slice->base >= mir->next_value) {
						if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL validate MAKE_SLICE base=%u next=%u\n", slice->base, mir->next_value);
						return false;
					}
					if (slice->mode == MIR_SLICE_PARTIAL && (slice->begin == MIR_INVALID_ID || slice->begin >= mir->next_value || slice->end == MIR_INVALID_ID || slice->end >= mir->next_value)) {
						if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL validate MAKE_SLICE partial begin=%u end=%u next=%u\n", slice->begin, slice->end, mir->next_value);
						return false;
					}
					break;
				}
				case MIR_OP_STORE: {
					auto* store = static_cast<MirStoreInstruction*>(instruction);
					if (store->address == MIR_INVALID_ID || store->address >= mir->next_value) {
						if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL validate STORE address=%u next=%u\n", store->address, mir->next_value);
						return false;
					}
					if (store->access != MIR_ACCESS_PLAIN && (store->index == MIR_INVALID_ID || store->index >= mir->next_value)) {
						if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL validate STORE index=%u access=%d\n", store->index, (int)store->access);
						return false;
					}
					if (store->value == MIR_INVALID_ID || store->value >= mir->next_value) {
						if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL validate STORE value=%u next=%u\n", store->value, mir->next_value);
						return false;
					}
					break;
				}
				case MIR_OP_LOAD: {
					auto* load = static_cast<MirLoadInstruction*>(instruction);
					if (load->address == MIR_INVALID_ID || load->address >= mir->next_value) {
						if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL validate LOAD address=%u next=%u fn='%s' block=%u instr=%u\n", load->address, mir->next_value, mir->name.begin ? mir->name.begin : "?", block.id, instruction_index);
						return false;
					}
					if (load->access != MIR_ACCESS_PLAIN && (load->index == MIR_INVALID_ID || load->index >= mir->next_value)) {
						if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL validate LOAD index=%u access=%d\n", load->index, (int)load->access);
						return false;
					}
					break;
				}
				case MIR_OP_CAST: {
					auto* cast = static_cast<MirCastInstruction*>(instruction);
					if (cast->operand == MIR_INVALID_ID || cast->operand >= mir->next_value) {
						if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL validate CAST operand=%u next=%u\n", cast->operand, mir->next_value);
						return false;
					}
					break;
				}
				case MIR_OP_SLICE_LENGTH: {
					auto* unary = static_cast<MirUnaryInstruction*>(instruction);
					if (unary->operand == MIR_INVALID_ID || unary->operand >= mir->next_value) {
						if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL validate SLICE_LENGTH operand=%u\n", unary->operand);
						return false;
					}
					break;
				}
				case MIR_OP_NULLABLE_HAS_VALUE: {
					auto* nullable = static_cast<MirNullableInstruction*>(instruction);
					if (nullable->address == MIR_INVALID_ID || nullable->address >= mir->next_value) {
						if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL validate NULLABLE address=%u\n", nullable->address);
						return false;
					}
					if (nullable->index == MIR_INVALID_ID || nullable->index >= mir->next_value) {
						if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL validate NULLABLE index=%u\n", nullable->index);
						return false;
					}
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
					if (binary->lhs == MIR_INVALID_ID || binary->lhs >= mir->next_value) {
						if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL validate BINARY lhs=%u next=%u\n", binary->lhs, mir->next_value);
						return false;
					}
					if (binary->rhs == MIR_INVALID_ID || binary->rhs >= mir->next_value) {
						if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL validate BINARY rhs=%u next=%u\n", binary->rhs, mir->next_value);
						return false;
					}
					break;
				}
			}
		if (instruction->opcode == MIR_OP_LOCAL_ADDRESS) {
			auto& address = static_cast<MirAddressInstruction&>(*instruction);
			if (address.local >= (u32)mir->locals.size()) return false;
		}
			const u32 instruction_offset = code.size;
			if (!mirEmitInstruction(bytecode, code, *instruction, slots, *mir)) {
				if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL emit opcode=%d type=%d size=%u kind=%d in function '%s' (block %u)\n", (int)instruction->opcode, instruction->type ? (int)instruction->type->kind : -1, instruction->type ? typeByteSize(*instruction->type) : 0, instruction->opcode == MIR_OP_CONST ? (int)static_cast<MirConstInstruction*>(instruction)->kind : -1, mir->name.begin ? mir->name.begin : "?", block.id);
				return false;
			}
			if (instruction->opcode == MIR_OP_LOAD || instruction->opcode == MIR_OP_STORE) {
				MirLoadInstruction* load = instruction->opcode == MIR_OP_LOAD ? static_cast<MirLoadInstruction*>(instruction) : nullptr;
				MirStoreInstruction* store = instruction->opcode == MIR_OP_STORE ? static_cast<MirStoreInstruction*>(instruction) : nullptr;
				const MirValueId address = load ? load->address : store->address;
				if (address == MIR_INVALID_ID || address >= mir->next_value || slots[address] == MIR_INVALID_ID) return false;
			}
			if (code.size != instruction_offset && instruction->source_location != MIR_INVALID_ID && instruction->source_location < (u32)mir->source_locations.size()) {
				const MirSourceLocation& location = mir->source_locations[instruction->source_location];
				if (source_map.empty() || source_map.back().code_offset != instruction_offset) {
					ls_bytecode_source_map_entry entry;
					entry.code_offset = instruction_offset;
					entry.source_name = location.source_name;
					entry.line = location.line;
					entry.column = location.column;
					source_map.push_back(entry);
				}
			}
			++instruction_index;
		}
		if (block.terminator.kind == MIR_TERM_BRANCH && (block.terminator.value == MIR_INVALID_ID || block.terminator.value >= mir->next_value)) {
			if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL block %u BRANCH value=%u next=%u\n", block.id, block.terminator.value, mir->next_value);
			return false;
		}
		if (block.terminator.kind == MIR_TERM_RETURN_VALUE && (block.terminator.value == MIR_INVALID_ID || block.terminator.value >= mir->next_value)) {
			if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL block %u RETURN_VALUE value=%u next=%u in '%s'\n", block.id, block.terminator.value, mir->next_value, mir->name.begin ? mir->name.begin : "?");
			return false;
		}
		if (!block.has_terminator) {
			block.terminator.kind = MIR_TERM_RETURN;
			block.has_terminator = true;
		}
		if (!mirEmitTerminator(code, block.terminator, slots, function->return_size, patches, fused)) {
			if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL emit terminator block %u kind=%d\n", block.id, (int)block.terminator.kind);
			return false;
		}
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
	function->source_map_count = (u32)source_map.size();
	if (function->source_map_count) {
		function->source_map = (ls_bytecode_source_map_entry*)bytecode.arena->allocate(bytecode.arena->user_data,
			sizeof(ls_bytecode_source_map_entry) * function->source_map_count, alignof(ls_bytecode_source_map_entry));
		if (!function->source_map) return false;
		for (u32 i = 0; i < function->source_map_count; ++i) function->source_map[i] = source_map[(i32)i];
	}
	if (function->locals) {
		u32 debug_index = 0;
		for (MirLocal& local : mir->locals) {
			if (local.compiler_generated) continue;
			if (local.source_location != MIR_INVALID_ID && local.source_location < (u32)mir->source_locations.size()) {
				const MirSourceLocation& location = mir->source_locations[local.source_location];
				for (u32 i = 0; i < function->source_map_count; ++i) {
					if (function->source_map[i].line >= location.line) { function->locals[debug_index].scope_begin_offset = function->source_map[i].code_offset; break; }
				}
			}
			++debug_index;
		}
	}
	if (function->locals) {
		u32 debug_index = 0;
		for (MirLocal& local : mir->locals) {
			if (local.compiler_generated) continue;
			if (local.source_location != MIR_INVALID_ID && local.source_location < (u32)mir->source_locations.size()) {
				const u32 line = mir->source_locations[local.source_location].line;
				for (u32 i = 0; i < function->source_map_count; ++i) if (function->source_map[i].line > line) { function->locals[debug_index].scope_begin_offset = function->source_map[i].code_offset; break; }
			}
			++debug_index;
		}
	}
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
	function.is_builtin_native = source.is_builtin;
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
	bytecode->global_debug_count = 0;
	bytecode->global_debug = nullptr;
	bytecode->global_debug_count = mir_module->global_debug_count;
	if (bytecode->global_debug_count) {
		bytecode->global_debug = (ls_bytecode_global_debug_entry*)bytecode->arena->allocate(bytecode->arena->user_data,
			sizeof(ls_bytecode_global_debug_entry) * bytecode->global_debug_count, alignof(ls_bytecode_global_debug_entry));
		memcpy(bytecode->global_debug, mir_module->global_debug, sizeof(ls_bytecode_global_debug_entry) * bytecode->global_debug_count);
		for (u32 i = 0; i < bytecode->global_debug_count; ++i) bytecode->global_debug[i].type_index = mirTypeInfo(*bytecode, mir_module->global_debug_types[i]);
	}
	for (const MirModuleFunction& entry : mir_module->functions) {
		if (entry.is_native) {
			if (!mirAppendNativeFunction(*bytecode, entry.native)) {
				if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL native '%s'\n", entry.native.name.begin ? entry.native.name.begin : "?");
				ls_bytecode_destroy(bytecode);
				return nullptr;
			}
		} else if (!mirCompileFunction(*bytecode, entry.function)) {
			if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL compile function\n");
			ls_bytecode_destroy(bytecode);
			return nullptr;
		}
	}
	for (const MirModuleFunction& entry : mir_module->functions) if (!entry.is_native && entry.function) {
		mirTypeInfo(*bytecode, entry.function->return_type);
		for (MirLocal& local : entry.function->locals) mirTypeInfo(*bytecode, local.type);
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
