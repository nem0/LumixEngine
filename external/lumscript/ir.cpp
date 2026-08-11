#include "ir.h"

#include "bytecode.h"
#include "compiler.h"
#include "utils.h"

#include <stdlib.h>
#include <float.h>

// TODO
/*
Multi-dimensional and nested aggregate access
Aggregate copies for arrays, structs, unions, and slices
Enum and union lowering
Pointer/address-of/dereference operations
Indirect calls and function values
Aggregate and reference call arguments
defer lowering
Global arrays, slices, structs, and complex initializers
Nullable values and null checks
Imported module integration
Native built-in metadata
Debug/source maps and local metadata
IR validation and diagnostics for unsupported AST forms
Optimization passes
Full module bytecode ownership and cleanup handling
Removal of the temporary 16-entry continue jump limit
*/

namespace {

struct ByteWriter {
	ls_arena& arena;
	u8* data = nullptr;
	u32 size = 0;
	u32 capacity = 0;

	ByteWriter(ls_arena& arena, u32 capacity)
		: arena(arena), capacity(capacity) {
		data = (u8*)arena.allocate(arena.user_data, capacity, alignof(u8));
	}

	bool write(const void* source, u32 count) {
		if (!data || size + count > capacity) return false;
		copyMemory(data + size, source, count);
		size += count;
		return true;
	}

	template <typename T>
	bool writeValue(T value) { return write(&value, sizeof(value)); }

	bool op(ls_op opcode) { return writeValue((u8)opcode); }
};

struct JumpPatch {
	u32 operand = 0;
	LsIrBlock target = LS_IR_INVALID_BLOCK;
};

static u32 typeRegisterSize(const ResolvedType* type) {
	if (!type) return 0;
	if (type->kind == ResolvedType::POINTER || type->kind == ResolvedType::CPTR || type->kind == ResolvedType::CSTR) return (u32)sizeof(void*);
	const u32 size = typeByteSize(*type);
	return size ? size : 1;
}

static i32 numericTypeIndex(const ResolvedType* type) {
	if (!type) return -1;
	switch (type->kind) {
		case ResolvedType::I8: return 0;
		case ResolvedType::U8: return 1;
		case ResolvedType::I16: return 2;
		case ResolvedType::U16: return 3;
		case ResolvedType::I32: return 4;
		case ResolvedType::U32: return 5;
		case ResolvedType::I64: return 6;
		case ResolvedType::ISIZE: return 6;
		case ResolvedType::U64: return 7;
		case ResolvedType::F32: return 8;
		case ResolvedType::F64: return 9;
		default: return -1;
	}
}

static u32 structFieldOffset(const StructResolvedType& structure, ls_string_view name, ResolvedType*& field_type) {
	u32 offset = 0;
	for (i32 i = 0; i < structure.decl->fields.size(); ++i) {
		field_type = structure.field_types[i];
		if (equalStrings(structure.decl->fields[i].name, name)) return offset;
		offset += typeByteSize(*field_type);
	}
	return LS_IR_INVALID_VALUE;
}

static u32 unionFieldOffset(const UnionResolvedType& union_type, ls_string_view name, ResolvedType*& field_type) {
	for (ResolvedType* member : union_type.members) {
		if (!member || member->kind != ResolvedType::STRUCT) continue;
		ResolvedType* candidate = nullptr;
		const u32 offset = structFieldOffset(*static_cast<StructResolvedType*>(member), name, candidate);
		if (offset != LS_IR_INVALID_VALUE) {
			field_type = candidate;
			return sizeof(i32) + offset;
		}
	}
	field_type = nullptr;
	return LS_IR_INVALID_VALUE;
}

static ls_type_kind numericBytecodeTypeKind(const ResolvedType* type) {
	if (!type) return LS_TYPE_INVALID;
	switch (type->kind) {
		case ResolvedType::BOOL: return LS_TYPE_BOOL;
		case ResolvedType::I8: return LS_TYPE_I8;
		case ResolvedType::U8: return LS_TYPE_U8;
		case ResolvedType::I16: return LS_TYPE_I16;
		case ResolvedType::U16: return LS_TYPE_U16;
		case ResolvedType::I32: return LS_TYPE_I32;
		case ResolvedType::U32: return LS_TYPE_U32;
		case ResolvedType::I64: return LS_TYPE_I64;
		case ResolvedType::ISIZE: return LS_TYPE_I64;
		case ResolvedType::U64: return LS_TYPE_U64;
		case ResolvedType::F32: return LS_TYPE_F32;
		case ResolvedType::F64: return LS_TYPE_F64;
		default: return LS_TYPE_INVALID;
	}
}

static ls_type_kind bytecodeTypeKind(const ResolvedType* type) {
	if (!type) return LS_TYPE_INVALID;
	if (ls_type_kind numeric = numericBytecodeTypeKind(type); numeric != LS_TYPE_INVALID) return numeric;
	switch (type->kind) {
		case ResolvedType::VOID: return LS_TYPE_VOID;
		case ResolvedType::BOOL: return LS_TYPE_BOOL;
		case ResolvedType::STRUCT: return LS_TYPE_STRUCT;
		case ResolvedType::ENUM: return LS_TYPE_ENUM;
		case ResolvedType::ARRAY: return LS_TYPE_ARRAY;
		case ResolvedType::SLICE: return LS_TYPE_SLICE;
		case ResolvedType::CPTR: return LS_TYPE_CPTR;
		case ResolvedType::POINTER: return LS_TYPE_CPTR;
		case ResolvedType::FUNCTION: return LS_TYPE_FUNCTION;
		case ResolvedType::NULLABLE: return LS_TYPE_NULLABLE;
		case ResolvedType::UNION: return LS_TYPE_TAGGED_UNION;
		default: return LS_TYPE_INVALID;
	}
}

static ls_op binaryOpcode(LsIrOpKind kind, i32 type_index) {
	const ls_op base[] = {LS_OP_ADD_I8, LS_OP_SUB_I8, LS_OP_MUL_I8, LS_OP_DIV_I8, LS_OP_MOD_I8};
	if (kind < LS_IR_OP_ADD || kind > LS_IR_OP_MOD || type_index < 0) return (ls_op)0;
	return (ls_op)((u32)base[(u32)kind - LS_IR_OP_ADD] + (u32)type_index);
}

static ls_op unaryOpcode(LsIrOpKind kind, i32 type_index) {
	if (kind != LS_IR_OP_NEG || type_index < 0) return (ls_op)0;
	return (ls_op)((u32)LS_OP_NEG_I8 + (u32)type_index);
}

struct IrBuilder {
	struct LocalBinding {
		StorageSlot* slot = nullptr;
		u32 offset = 0;
	};
	struct LoopTargets {
		LsIrBlock continue_target = LS_IR_INVALID_BLOCK;
		LsIrBlock break_target = LS_IR_INVALID_BLOCK;
		ls_string_view label = {};
		LsOpJump* continue_jumps[16] = {};
		u32 continue_jump_count = 0;
		u32 defer_mark = 0;
	};

	ls_arena& arena;
	LsIrFunctionData& function;
	LsIrBlockData* block = nullptr;
	ExpArray<LoopTargets> loops;
	ExpArray<LocalBinding> locals;
	ExpArray<Statement*> defers;
	ExpArray<u32> scope_marks;
	u32 next_local_offset = 0;
	bool failed = false;
	LsIrSourceLoc source_loc = LS_IR_INVALID_SOURCE_LOC;

	IrBuilder(ls_arena& arena, LsIrFunctionData& function)
		: arena(arena), function(function), loops(arena), locals(arena), defers(arena), scope_marks(arena) {
		if (function.source) {
			u32 offset = 0;
			for (FunctionParam& parameter : function.source->params) {
				if (parameter.is_comptime || !parameter.resolved_type) continue;
				parameter.slot.storage = StorageSlot::LOCAL;
				parameter.slot.offset = offset;
				parameter.slot.type = parameter.resolved_type;
				parameter.slot.byte_size = typeRegisterSize(parameter.resolved_type);
				LocalBinding& binding = locals.emplace_back();
				binding.slot = &parameter.slot;
				binding.offset = offset;
				offset += parameter.slot.byte_size;
			}
			function.param_size = offset;
			function.local_size = offset;
			next_local_offset = offset;
		}
		block = &function.blocks.emplace_back(arena, 0);
		function.entry = block->id;
	}

	LsIrValue newValue() {
		return function.next_value++;
	}

	LsIrBlock newBlock() {
		const LsIrBlock id = (LsIrBlock)function.blocks.size();
		function.blocks.emplace_back(arena, id);
		return id;
	}

	void selectBlock(LsIrBlock id) {
		block = &function.blocks[id];
	}

	template <typename T>
	T* append() {
		void* memory = arena.allocate(arena.user_data, sizeof(T), alignof(T));
		if (!memory) return nullptr;
		T* op = ::new (NewPlaceholder{}, memory) T();
		op->src_loc = source_loc;
		block->ops.push_back(op);
		return op;
	}

	u32 localOffset(StorageSlot* slot, ResolvedType* type) {
		if (!slot) return 0;
		for (LocalBinding& binding : locals) if (binding.slot == slot) return binding.offset;
		const u32 size = typeRegisterSize(type);
		LocalBinding& binding = locals.emplace_back();
		binding.slot = slot;
		binding.offset = next_local_offset;
		slot->offset = next_local_offset;
		slot->byte_size = size;
		slot->type = type;
		next_local_offset += size;
		if (next_local_offset > function.local_size) function.local_size = next_local_offset;
		return binding.offset;
	}

	u32 temporaryOffset(ResolvedType* type) {
		const u32 offset = next_local_offset;
		next_local_offset += typeRegisterSize(type);
		if (next_local_offset > function.local_size) function.local_size = next_local_offset;
		return offset;
	}

	template <typename T>
	T* terminate() {
		void* memory = arena.allocate(arena.user_data, sizeof(T), alignof(T));
		if (!memory) return nullptr;
		T* op = ::new (NewPlaceholder{}, memory) T();
		op->src_loc = source_loc;
		block->terminator = op;
		return op;
	}
};

static LsIrValue buildExpression(IrBuilder& builder, Expression* expression);
static void buildStatement(IrBuilder& builder, Statement* statement, ls_string_view pending_label = {});
static LsIrValue buildOperatorCall(IrBuilder& builder, FunctionExpression* target, Expression** expressions, u32 count, ResolvedType* result_type);
static LsIrValue buildExpressionAsType(IrBuilder& builder, Expression* expression, ResolvedType* expected);
static LsIrValue buildArrayAsSlice(IrBuilder& builder, Expression* expression, ResolvedType* target_type);
static LsIrValue buildComptimeBytes(IrBuilder& builder, ResolvedType* type, const u8* bytes);
static LsIrValue buildTypeMember(IrBuilder& builder, TypeMemberExpression& expression, ResolvedType* target = nullptr);

static LsIrValue buildComptimeValue(IrBuilder& builder, ResolvedType* type, const u8* bytes) {
	if (!type || !bytes) return LS_IR_INVALID_VALUE;
	const u32 size = typeRegisterSize(type);
	if (size > sizeof(u64) && !(size == 16u && type->kind == ResolvedType::SLICE))
		return buildComptimeBytes(builder, type, bytes);
	LsOpLoadConst* constant = builder.append<LsOpLoadConst>();
	if (!constant) return LS_IR_INVALID_VALUE;
	constant->type = type;
	constant->result = builder.newValue();
	copyMemory(&constant->value, bytes, size <= sizeof(u64) ? size : sizeof(u64));
	if (size == 16u) {
		copyMemory(&constant->second_value, bytes + sizeof(u64), sizeof(u64));
		constant->has_second_value = true;
	}
	return constant->result;
}

static void emitDefers(IrBuilder& builder, u32 mark) {
	for (i32 i = (i32)builder.defers.size() - 1; i >= (i32)mark; --i)
		buildStatement(builder, builder.defers[(u32)i]);
}

static LsIrValue buildLiteral(IrBuilder& builder, Expression& expression) {
	LsOpLoadConst* op = builder.append<LsOpLoadConst>();
	if (!op) return LS_IR_INVALID_VALUE;
	op->type = expression.resolved_type;
	op->result = builder.newValue();
		switch (expression.kind) {
		case Expression::INT_LITERAL:
			if (expression.resolved_type && expression.resolved_type->kind == ResolvedType::F32) {
				const float value = (float)static_cast<IntLiteralExpression&>(expression).value;
				copyMemory(&op->value, &value, sizeof(value));
			} else if (expression.resolved_type && expression.resolved_type->kind == ResolvedType::F64) {
				const double value = (double)static_cast<IntLiteralExpression&>(expression).value;
				copyMemory(&op->value, &value, sizeof(value));
			} else op->value = static_cast<IntLiteralExpression&>(expression).value;
			break;
		case Expression::FLOAT_LITERAL: {
			double value = static_cast<FloatLiteralExpression&>(expression).value;
			if (expression.resolved_type && expression.resolved_type->kind == ResolvedType::F32) {
				const float converted = (float)value;
				copyMemory(&op->value, &converted, sizeof(converted));
			} else {
				copyMemory(&op->value, &value, sizeof(value));
			}
			break;
		}
		case Expression::BOOL_LITERAL:
			op->value = static_cast<BoolLiteralExpression&>(expression).value ? 1u : 0u;
			break;
		case Expression::STRING_LITERAL:
			{
				const ls_string_view value = static_cast<StringLiteralExpression&>(expression).value;
				const bool is_cstr = expression.resolved_type && expression.resolved_type->kind == ResolvedType::CSTR;
				if (is_cstr) {
					const u32 length = (u32)(value.end - value.begin);
					char* storage = (char*)builder.arena.allocate(builder.arena.user_data, length + 1u, alignof(char));
					if (!storage) return LS_IR_INVALID_VALUE;
					copyMemory(storage, value.begin, length);
					storage[length] = '\0';
					op->value = (u64)(uintptr)storage;
				} else {
					op->value = (u64)(uintptr)value.begin;
				}
				if (expression.resolved_type && expression.resolved_type->kind == ResolvedType::SLICE) {
					op->second_value = (u64)(value.end - value.begin);
					op->has_second_value = true;
				}
			}
			break;
		case Expression::NULL_LITERAL:
		case Expression::UNDEFINED:
			op->value = 0;
			if (expression.resolved_type && expression.resolved_type->kind == ResolvedType::SLICE) {
				op->second_value = 0;
				op->has_second_value = true;
			}
			break;
		default:
			builder.failed = true;
			return LS_IR_INVALID_VALUE;
	}
	return op->result;
}

static LsIrValue buildIdentifier(IrBuilder& builder, IdentifierExpression& identifier) {
	FunctionExpression* function = identifier.resolved_fn;
	if (!function && identifier.symbol && identifier.symbol->expression && identifier.symbol->expression->kind == Expression::FUNCTION)
		function = static_cast<FunctionExpression*>(identifier.symbol->expression);
	if (function) {
		LsOpLoadConst* constant = builder.append<LsOpLoadConst>();
		if (!constant) return LS_IR_INVALID_VALUE;
		constant->type = identifier.resolved_type;
		constant->result = builder.newValue();
		constant->value = function->bytecode_index;
		return constant->result;
	}
	if (identifier.symbol && identifier.symbol->storage == Symbol::COMPTIME && identifier.symbol->expression && identifier.resolved_type &&
		(identifier.symbol->expression->kind == Expression::STRUCT_LITERAL || identifier.symbol->expression->kind == Expression::ARRAY_LITERAL))
		return buildExpressionAsType(builder, identifier.symbol->expression, identifier.resolved_type);
	if (identifier.comptime_bytes && identifier.resolved_type) {
		const u32 size = typeRegisterSize(identifier.resolved_type);
		if (size <= sizeof(u64) || (size == 16 && identifier.resolved_type->kind == ResolvedType::SLICE)) {
			LsOpLoadConst* constant = builder.append<LsOpLoadConst>();
			if (!constant) return LS_IR_INVALID_VALUE;
			constant->type = identifier.resolved_type;
			constant->result = builder.newValue();
			copyMemory(&constant->value, identifier.comptime_bytes, size == 16 ? 8 : size);
			if (size == 16) {
				copyMemory(&constant->second_value, identifier.comptime_bytes + 8, 8);
				constant->has_second_value = true;
			}
			return constant->result;
		}
	}
	if (!identifier.slot) {
		const u32 size = identifier.resolved_type ? typeRegisterSize(identifier.resolved_type) : 0;
		u8* bytes = identifier.comptime_bytes ? identifier.comptime_bytes : identifier.symbol ? identifier.symbol->comptime_bytes : nullptr;
		if (!bytes || !identifier.resolved_type || size == 0) return LS_IR_INVALID_VALUE;
		if (size > sizeof(u64) && !(size == 16 && identifier.resolved_type->kind == ResolvedType::SLICE))
			return buildComptimeBytes(builder, identifier.resolved_type, bytes);
		LsOpLoadConst* constant = builder.append<LsOpLoadConst>();
		if (!constant) return LS_IR_INVALID_VALUE;
		constant->type = identifier.resolved_type;
		constant->result = builder.newValue();
		copyMemory(&constant->value, bytes, size == 16 ? 8 : size);
		if (size == 16) {
			copyMemory(&constant->second_value, bytes + 8, 8);
			constant->has_second_value = true;
		}
		return constant->result;
	}
	if (identifier.slot->storage == StorageSlot::GLOBAL) {
		LsOpGlobalLoad* op = builder.append<LsOpGlobalLoad>();
		if (!op) return LS_IR_INVALID_VALUE;
		op->type = identifier.resolved_type;
		op->result = builder.newValue();
		op->offset = identifier.slot->offset;
		return op->result;
	}
	LsOpLocalLoad* load = builder.append<LsOpLocalLoad>();
	if (!load) return LS_IR_INVALID_VALUE;
	load->type = identifier.resolved_type;
	load->result = builder.newValue();
	load->offset = builder.localOffset(identifier.slot, identifier.slot->type ? identifier.slot->type : identifier.resolved_type);
	if (identifier.slot->type && identifier.slot->type->kind == ResolvedType::NULLABLE && identifier.resolved_type && identifier.resolved_type->kind != ResolvedType::NULLABLE)
		++load->offset;
	if (identifier.slot->type && identifier.slot->type->kind == ResolvedType::UNION && identifier.resolved_type && identifier.resolved_type->kind != ResolvedType::UNION)
		load->offset += sizeof(i32);
	return load->result;
}

static LsIrValue buildComptimeBytes(IrBuilder& builder, ResolvedType* type, const u8* bytes) {
	if (!type || !bytes) return LS_IR_INVALID_VALUE;
	const u32 size = typeRegisterSize(type);
	if (size <= sizeof(u64) || (size == 16 && type->kind == ResolvedType::SLICE)) {
		LsOpLoadConst* constant = builder.append<LsOpLoadConst>();
		if (!constant) return LS_IR_INVALID_VALUE;
		constant->type = type;
		constant->result = builder.newValue();
		copyMemory(&constant->value, bytes, size <= 8 ? size : 8);
		if (size == 16) {
			copyMemory(&constant->second_value, bytes + 8, 8);
			constant->has_second_value = true;
		}
		return constant->result;
	}
	if (type->kind != ResolvedType::STRUCT && type->kind != ResolvedType::ARRAY && type->kind != ResolvedType::NULLABLE && type->kind != ResolvedType::UNION)
		return LS_IR_INVALID_VALUE;
	u32 count = 0;
	if (type->kind == ResolvedType::STRUCT) count = (u32)static_cast<StructResolvedType*>(type)->field_types.size();
	else if (type->kind == ResolvedType::ARRAY) count = (u32)static_cast<ArrayResolvedType*>(type)->size;
	else if (type->kind == ResolvedType::NULLABLE) count = 2;
	else count = 2;
	LsIrValue* values = (LsIrValue*)builder.arena.allocate(builder.arena.user_data, sizeof(LsIrValue) * count, alignof(LsIrValue));
	u32* offsets = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * count, alignof(u32));
	u32* sizes = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * count, alignof(u32));
	if (!values || !offsets || !sizes) return LS_IR_INVALID_VALUE;
	u32 offset = 0;
	for (u32 i = 0; i < count; ++i) {
		ResolvedType* child = nullptr;
		const u8* child_bytes = nullptr;
		if (type->kind == ResolvedType::STRUCT) {
			child = static_cast<StructResolvedType*>(type)->field_types[(i32)i];
			child_bytes = bytes + offset;
		} else if (type->kind == ResolvedType::ARRAY) {
			child = static_cast<ArrayResolvedType*>(type)->element_type;
			child_bytes = bytes + offset;
		} else if (type->kind == ResolvedType::NULLABLE) {
			if (i == 0) { child = nullptr; child_bytes = bytes; }
			else { child = static_cast<NullableResolvedType*>(type)->inner; child_bytes = bytes + 1; }
		} else {
			if (i == 0) { static ResolvedType tag_type(ResolvedType::I32); child = &tag_type; child_bytes = bytes; }
			else { UnionResolvedType* union_type = static_cast<UnionResolvedType*>(type); child = union_type->members[0]; child_bytes = bytes + 4; }
		}
		if (type->kind == ResolvedType::NULLABLE && i == 0) {
			LsOpLoadConst* flag = builder.append<LsOpLoadConst>();
			if (!flag) return LS_IR_INVALID_VALUE;
			flag->type = child = type;
			flag->result = builder.newValue();
			flag->value = *child_bytes;
			values[i] = flag->result;
			sizes[i] = 1;
		} else {
			values[i] = buildComptimeBytes(builder, child, child_bytes);
			sizes[i] = typeByteSize(*child);
		}
		if (values[i] == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
		offsets[i] = offset;
		if (type->kind == ResolvedType::NULLABLE) offset = i == 0 ? 1 : offset + sizes[i];
		else if (type->kind == ResolvedType::UNION) offset = i == 0 ? 4 : offset;
		else offset += sizes[i];
	}
	LsOpAggregateInit* aggregate = builder.append<LsOpAggregateInit>();
	if (!aggregate) return LS_IR_INVALID_VALUE;
	aggregate->type = type;
	aggregate->result = builder.newValue();
	aggregate->values = values;
	aggregate->offsets = offsets;
	aggregate->sizes = sizes;
	aggregate->value_count = count;
	return aggregate->result;
}

static LsIrValue buildComptimeIdentifierAsType(IrBuilder& builder, IdentifierExpression& identifier, ResolvedType* target) {
	if (!target) return LS_IR_INVALID_VALUE;
	u8* bytes = identifier.comptime_bytes ? identifier.comptime_bytes : identifier.symbol ? identifier.symbol->comptime_value.value : identifier.comptime_value.value;
	ResolvedType* source_type = identifier.symbol ? identifier.symbol->comptime_value.type : identifier.comptime_value.type ? identifier.comptime_value.type : identifier.resolved_type;
	if (!bytes || !source_type) return LS_IR_INVALID_VALUE;
	const ResolvedType::Kind source_kind = source_type->kind;
	if (source_kind != ResolvedType::UNTYPED_INT && source_kind != ResolvedType::UNTYPED_FLOAT && source_kind != ResolvedType::F32 && source_kind != ResolvedType::F64) return LS_IR_INVALID_VALUE;
	LsOpLoadConst* constant = builder.append<LsOpLoadConst>();
	if (!constant) return LS_IR_INVALID_VALUE;
	constant->type = target;
	constant->result = builder.newValue();
	if (source_kind == ResolvedType::UNTYPED_FLOAT || source_kind == ResolvedType::F32 || source_kind == ResolvedType::F64) {
		double value = 0;
		if (source_kind == ResolvedType::F32) { float source = 0; copyMemory(&source, bytes, sizeof(source)); value = source; }
		else copyMemory(&value, bytes, sizeof(value));
		if (target->kind == ResolvedType::F32) { const float converted = (float)value; copyMemory(&constant->value, &converted, sizeof(converted)); }
		else if (target->kind == ResolvedType::F64) copyMemory(&constant->value, &value, sizeof(value));
		else return LS_IR_INVALID_VALUE;
	} else {
		i64 value = 0;
		copyMemory(&value, bytes, sizeof(value));
		constant->value = (u64)value;
	}
	return constant->result;
}

static LsIrValue buildAggregateLiteral(IrBuilder& builder, ExpArray<Expression*>& source, ResolvedType* type) {
	if (!type || (type->kind != ResolvedType::STRUCT && type->kind != ResolvedType::ARRAY)) return LS_IR_INVALID_VALUE;
	const u32 count = (u32)source.size();
	LsIrValue* values = count ? (LsIrValue*)builder.arena.allocate(builder.arena.user_data, sizeof(LsIrValue) * count, alignof(LsIrValue)) : nullptr;
	u32* offsets = count ? (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * count, alignof(u32)) : nullptr;
	u32* sizes = count ? (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * count, alignof(u32)) : nullptr;
	if (count && (!values || !offsets || !sizes)) return LS_IR_INVALID_VALUE;
	u32 offset = 0;
	for (u32 i = 0; i < count; ++i) {
		ResolvedType* element_type = type->kind == ResolvedType::STRUCT
			? static_cast<StructResolvedType*>(type)->field_types[(i32)i]
			: static_cast<ArrayResolvedType*>(type)->element_type;
		values[i] = buildExpressionAsType(builder, source[(i32)i], element_type);
		if (values[i] == LS_IR_INVALID_VALUE || !element_type) return LS_IR_INVALID_VALUE;
		offsets[i] = offset;
		sizes[i] = typeByteSize(*element_type);
		offset += sizes[i];
	}

	LsOpAggregateInit* op = builder.append<LsOpAggregateInit>();
	if (!op) return LS_IR_INVALID_VALUE;
	op->type = type;
	op->result = builder.newValue();
	op->values = values;
	op->offsets = offsets;
	op->sizes = sizes;
	op->value_count = count;
	return op->result;
}

static LsIrValue buildStructLiteral(IrBuilder& builder, StructLiteralExpression& expression) {
	return buildAggregateLiteral(builder, expression.values, expression.resolved_type);
}

static LsIrValue buildArrayLiteral(IrBuilder& builder, ArrayLiteralExpression& expression) {
	return buildAggregateLiteral(builder, expression.values, expression.resolved_type);
}

static LsIrValue buildExpressionAsType(IrBuilder& builder, Expression* expression, ResolvedType* expected) {
	if (!expression || !expected) return LS_IR_INVALID_VALUE;
	if ((expression->kind == Expression::INT_LITERAL || expression->kind == Expression::FLOAT_LITERAL) &&
		(expected->kind == ResolvedType::F32 || expected->kind == ResolvedType::F64 ||
		 expected->kind == ResolvedType::I8 || expected->kind == ResolvedType::U8 || expected->kind == ResolvedType::I16 || expected->kind == ResolvedType::U16 ||
		 expected->kind == ResolvedType::I32 || expected->kind == ResolvedType::U32 || expected->kind == ResolvedType::I64 || expected->kind == ResolvedType::U64 || expected->kind == ResolvedType::ISIZE)) {
		LsOpLoadConst* value = builder.append<LsOpLoadConst>();
		if (!value) return LS_IR_INVALID_VALUE;
		value->type = expected;
		value->result = builder.newValue();
		if (expected->kind == ResolvedType::F32) {
			const float converted = expression->kind == Expression::INT_LITERAL
				? (float)static_cast<IntLiteralExpression*>(expression)->value
				: (float)static_cast<FloatLiteralExpression*>(expression)->value;
			copyMemory(&value->value, &converted, sizeof(converted));
		} else if (expected->kind == ResolvedType::F64) {
			const double converted = expression->kind == Expression::INT_LITERAL
				? (double)static_cast<IntLiteralExpression*>(expression)->value
				: static_cast<FloatLiteralExpression*>(expression)->value;
			copyMemory(&value->value, &converted, sizeof(converted));
		} else if (expression->kind == Expression::INT_LITERAL) {
			value->value = (u64)static_cast<IntLiteralExpression*>(expression)->value;
		} else {
			value->value = (u64)static_cast<i64>(static_cast<FloatLiteralExpression*>(expression)->value);
		}
		return value->result;
	}
	if (expression->kind == Expression::TYPE_MEMBER) {
		TypeMemberExpression& member = *static_cast<TypeMemberExpression*>(expression);
		if (member.kind == TypeMemberExpression::LENGTH || member.kind == TypeMemberExpression::MIN || member.kind == TypeMemberExpression::MAX)
			return buildTypeMember(builder, member, expected);
	}
	if (expression->kind != Expression::INT_LITERAL && (expression->resolved_type == expected ||
		(expression->resolved_type && expression->resolved_type->kind == expected->kind && expected->kind == ResolvedType::NULLABLE &&
		 static_cast<NullableResolvedType*>(expression->resolved_type)->inner->kind == static_cast<NullableResolvedType*>(expected)->inner->kind)))
		return buildExpression(builder, expression);
	if (expression->kind == Expression::IDENTIFIER &&
		(expected->kind == ResolvedType::I8 || expected->kind == ResolvedType::U8 || expected->kind == ResolvedType::I16 || expected->kind == ResolvedType::U16 ||
		 expected->kind == ResolvedType::I32 || expected->kind == ResolvedType::U32 || expected->kind == ResolvedType::I64 || expected->kind == ResolvedType::U64 ||
		 expected->kind == ResolvedType::ISIZE || expected->kind == ResolvedType::F32 || expected->kind == ResolvedType::F64)) {
		IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(expression);
		LsIrValue value = buildComptimeIdentifierAsType(builder, identifier, expected);
		if (value != LS_IR_INVALID_VALUE) return value;
	}
	if (expression->comptime_value.kind == ComptimeValue::VALUE && expression->comptime_value.value &&
		(expected->kind == ResolvedType::I8 || expected->kind == ResolvedType::U8 || expected->kind == ResolvedType::I16 || expected->kind == ResolvedType::U16 ||
		 expected->kind == ResolvedType::I32 || expected->kind == ResolvedType::U32 || expected->kind == ResolvedType::I64 || expected->kind == ResolvedType::U64 ||
		 expected->kind == ResolvedType::ISIZE || expected->kind == ResolvedType::F32 || expected->kind == ResolvedType::F64)) {
		LsOpLoadConst* value = builder.append<LsOpLoadConst>();
		if (!value) return LS_IR_INVALID_VALUE;
		value->type = expected;
		value->result = builder.newValue();
		ResolvedType* source_type = expression->comptime_value.type;
		if (expected->kind == ResolvedType::F32 || expected->kind == ResolvedType::F64) {
			double number = 0;
			if (source_type && source_type->kind == ResolvedType::UNTYPED_FLOAT) copyMemory(&number, expression->comptime_value.value, sizeof(number));
			else { i64 integer = 0; copyMemory(&integer, expression->comptime_value.value, sizeof(integer)); number = (double)integer; }
			if (expected->kind == ResolvedType::F32) { float converted = (float)number; copyMemory(&value->value, &converted, sizeof(converted)); }
			else copyMemory(&value->value, &number, sizeof(number));
		} else {
			copyMemory(&value->value, expression->comptime_value.value, typeByteSize(*expected) < sizeof(u64) ? typeByteSize(*expected) : sizeof(u64));
		}
		return value->result;
	}
	if (expression->kind == Expression::UNDEFINED) {
		LsOpLoadConst* zero = builder.append<LsOpLoadConst>();
		if (!zero) return LS_IR_INVALID_VALUE;
		zero->type = expected;
		zero->result = builder.newValue();
		return zero->result;
	}
	if (expected->kind == ResolvedType::NULLABLE) {
		NullableResolvedType& nullable = *static_cast<NullableResolvedType*>(expected);
		if (expression->kind == Expression::NULL_LITERAL) {
			LsOpLoadConst* value = builder.append<LsOpLoadConst>();
			if (!value) return LS_IR_INVALID_VALUE;
			value->type = expected;
			value->result = builder.newValue();
			return value->result;
		}
		if (expression->kind == Expression::INT_LITERAL && typeByteSize(*nullable.inner) <= 7u) {
			LsOpLoadConst* value = builder.append<LsOpLoadConst>();
			if (!value) return LS_IR_INVALID_VALUE;
			value->type = expected; value->result = builder.newValue();
			value->value = 1u | ((u64)static_cast<IntLiteralExpression*>(expression)->value << 8u);
			return value->result;
		}
		LsIrValue inner = buildExpressionAsType(builder, expression, nullable.inner);
		if (inner == LS_IR_INVALID_VALUE) return inner;
		LsOpLoadConst* present = builder.append<LsOpLoadConst>();
		if (!present) return LS_IR_INVALID_VALUE;
		present->type = expected;
		present->result = builder.newValue();
		present->value = 1;
		LsOpAggregateInit* aggregate = builder.append<LsOpAggregateInit>();
		if (!aggregate) return LS_IR_INVALID_VALUE;
		LsIrValue* values = (LsIrValue*)builder.arena.allocate(builder.arena.user_data, sizeof(LsIrValue) * 2, alignof(LsIrValue));
		u32* offsets = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * 2, alignof(u32));
		u32* sizes = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * 2, alignof(u32));
		if (!values || !offsets || !sizes) return LS_IR_INVALID_VALUE;
		values[0] = present->result; values[1] = inner;
		offsets[0] = 0; offsets[1] = 1;
		sizes[0] = 1; sizes[1] = typeByteSize(*nullable.inner);
		aggregate->type = expected;
		aggregate->result = builder.newValue();
		aggregate->values = values;
		aggregate->offsets = offsets;
		aggregate->sizes = sizes;
		aggregate->value_count = 2;
		return aggregate->result;
	}
	if (expected->kind == ResolvedType::UNION) {
		UnionResolvedType& union_type = *static_cast<UnionResolvedType*>(expected);
		ResolvedType* source_type = expression->resolved_type;
		if (source_type && source_type->kind == ResolvedType::UNION) return buildExpression(builder, expression);
		for (i32 member_index = 0; member_index < union_type.members.size(); ++member_index) {
			ResolvedType* member_type = union_type.members[member_index];
			bool same_type = source_type && member_type && member_type->kind == source_type->kind;
			if (same_type && member_type->kind == ResolvedType::STRUCT)
				same_type = static_cast<StructResolvedType*>(member_type)->decl == static_cast<StructResolvedType*>(source_type)->decl ||
					equalStrings(static_cast<StructResolvedType*>(member_type)->decl->cached_name, static_cast<StructResolvedType*>(source_type)->decl->cached_name);
			if (!same_type) continue;
			LsOpLoadConst* tag = builder.append<LsOpLoadConst>();
			LsIrValue payload = tag ? buildExpressionAsType(builder, expression, member_type) : LS_IR_INVALID_VALUE;
			LsOpAggregateInit* aggregate = payload == LS_IR_INVALID_VALUE ? nullptr : builder.append<LsOpAggregateInit>();
			if (!tag || !aggregate) return LS_IR_INVALID_VALUE;
			LsIrValue* values = (LsIrValue*)builder.arena.allocate(builder.arena.user_data, sizeof(LsIrValue) * 2, alignof(LsIrValue));
			u32* offsets = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * 2, alignof(u32));
			u32* sizes = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * 2, alignof(u32));
			if (!values || !offsets || !sizes) return LS_IR_INVALID_VALUE;
			static ResolvedType i32_type(ResolvedType::I32);
			tag->type = &i32_type; tag->result = builder.newValue(); tag->value = (u32)member_index;
			values[0] = tag->result; values[1] = payload; offsets[0] = 0; offsets[1] = 4; sizes[0] = 4; sizes[1] = typeByteSize(*member_type);
			aggregate->type = expected; aggregate->result = builder.newValue(); aggregate->values = values; aggregate->offsets = offsets; aggregate->sizes = sizes; aggregate->value_count = 2;
			return aggregate->result;
		}
	}
	if (expression->kind == Expression::INT_LITERAL || expression->kind == Expression::FLOAT_LITERAL || expression->kind == Expression::BOOL_LITERAL) {
		LsOpLoadConst* value = builder.append<LsOpLoadConst>();
		if (!value) return LS_IR_INVALID_VALUE;
		value->type = expected;
		value->result = builder.newValue();
		if (expression->kind == Expression::INT_LITERAL) value->value = (u64)static_cast<IntLiteralExpression*>(expression)->value;
		else if (expression->kind == Expression::BOOL_LITERAL) value->value = static_cast<BoolLiteralExpression*>(expression)->value ? 1u : 0u;
		else {
			double number = static_cast<FloatLiteralExpression*>(expression)->value;
			if (expected->kind == ResolvedType::F32) { float converted = (float)number; copyMemory(&value->value, &converted, sizeof(converted)); }
			else copyMemory(&value->value, &number, sizeof(number));
		}
		return value->result;
	}
	if (expected->kind == ResolvedType::SLICE && expression->resolved_type && expression->resolved_type->kind == ResolvedType::ARRAY)
		return buildArrayAsSlice(builder, expression, expected);
	return buildExpression(builder, expression);
}

static LsIrValue buildMember(IrBuilder& builder, MemberExpression& expression) {
	if (expression.resolved_fn || (expression.resolved_symbol && expression.resolved_symbol->expression && expression.resolved_symbol->expression->kind == Expression::FUNCTION)) {
		FunctionExpression* function = expression.resolved_fn ? expression.resolved_fn : static_cast<FunctionExpression*>(expression.resolved_symbol->expression);
		LsOpLoadConst* constant = builder.append<LsOpLoadConst>();
		if (!constant) return LS_IR_INVALID_VALUE;
		constant->type = expression.resolved_type;
		constant->result = builder.newValue();
		constant->value = function->bytecode_index;
		return constant->result;
	}
	if (expression.resolved_symbol && expression.resolved_symbol->storage == Symbol::COMPTIME
		&& expression.resolved_symbol->comptime_bytes) {
		return buildComptimeValue(builder, expression.resolved_type, expression.resolved_symbol->comptime_bytes);
	}
	if (!expression.expression || !expression.expression->resolved_type) return LS_IR_INVALID_VALUE;
	if (expression.comptime_value.kind == ComptimeValue::VALUE && expression.comptime_value.value && expression.resolved_type) {
		ResolvedType* source_type = expression.comptime_value.type;
		if (source_type && (expression.resolved_type->kind == ResolvedType::F32 || expression.resolved_type->kind == ResolvedType::F64) &&
			(source_type->kind == ResolvedType::UNTYPED_INT || source_type->kind == ResolvedType::UNTYPED_FLOAT ||
			 source_type->kind == ResolvedType::F32 || source_type->kind == ResolvedType::F64)) {
			LsOpLoadConst* constant = builder.append<LsOpLoadConst>();
			if (!constant) return LS_IR_INVALID_VALUE;
			constant->type = expression.resolved_type;
			constant->result = builder.newValue();
			double number = 0;
			if (source_type->kind == ResolvedType::UNTYPED_FLOAT || source_type->kind == ResolvedType::F64) copyMemory(&number, expression.comptime_value.value, sizeof(number));
			else if (source_type->kind == ResolvedType::F32) { float value = 0; copyMemory(&value, expression.comptime_value.value, sizeof(value)); number = value; }
			else { i64 value = 0; copyMemory(&value, expression.comptime_value.value, sizeof(value)); number = (double)value; }
			if (expression.resolved_type->kind == ResolvedType::F32) { float value = (float)number; copyMemory(&constant->value, &value, sizeof(value)); }
			else copyMemory(&constant->value, &number, sizeof(number));
			return constant->result;
		}
		return buildComptimeValue(builder, expression.resolved_type, expression.comptime_value.value);
	}
	ResolvedType* base_type = expression.expression->resolved_type;
	ResolvedType* enum_type = base_type->kind == ResolvedType::META ? static_cast<MetaType*>(base_type)->inner : base_type;
	if (enum_type && enum_type->kind == ResolvedType::ENUM) {
		EnumResolvedType& en = *static_cast<EnumResolvedType*>(enum_type);
		for (i32 i = 0; i < en.decl->members.size(); ++i) {
			const EnumMember& member = en.decl->members[i];
			if (!equalStrings(member.name, expression.name)) continue;
			u64 value = (u64)i;
			if (member.value) {
				if (member.value->kind != Expression::INT_LITERAL) return LS_IR_INVALID_VALUE;
				value = (u64)static_cast<IntLiteralExpression*>(member.value)->value;
			}
			LsOpLoadConst* op = builder.append<LsOpLoadConst>();
			if (!op) return LS_IR_INVALID_VALUE;
			op->type = expression.resolved_type;
			op->result = builder.newValue();
			op->value = value;
			return op->result;
		}
	return LS_IR_INVALID_VALUE;
}

	if (base_type->kind == ResolvedType::ARRAY && equalStrings(expression.name, makeStringView("length"))) {
		LsOpLoadConst* op = builder.append<LsOpLoadConst>();
		if (!op) return LS_IR_INVALID_VALUE;
		op->type = expression.resolved_type;
		op->result = builder.newValue();
		op->value = (u64)static_cast<ArrayResolvedType*>(base_type)->size;
		return op->result;
	}
	if (base_type->kind == ResolvedType::SLICE && equalStrings(expression.name, makeStringView("length"))) {
		u32 slice_offset = LS_IR_INVALID_VALUE;
		bool slice_is_value = false;
		if (expression.expression->kind == Expression::IDENTIFIER && !static_cast<IdentifierExpression*>(expression.expression)->comptime_bytes) {
			IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(expression.expression);
			if (!identifier.slot || identifier.slot->storage == StorageSlot::GLOBAL) return LS_IR_INVALID_VALUE;
			slice_offset = builder.localOffset(identifier.slot, base_type);
		} else {
			LsIrValue slice = buildExpression(builder, expression.expression);
			if (slice == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
			slice_offset = slice;
			slice_is_value = true;
		}
		LsOpSliceLength* op = builder.append<LsOpSliceLength>();
		if (!op) return LS_IR_INVALID_VALUE;
		op->result = builder.newValue();
		op->slice_offset = slice_offset;
		op->slice_is_value = slice_is_value;
		return op->result;
	}
	if (base_type->kind == ResolvedType::POINTER) {
		PointerResolvedType& pointer = *static_cast<PointerResolvedType*>(base_type);
		if (pointer.inner && pointer.inner->kind == ResolvedType::UNION) {
			ResolvedType* field_type = nullptr;
			const u32 field_offset = unionFieldOffset(*static_cast<UnionResolvedType*>(pointer.inner), expression.name, field_type);
			if (field_offset == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
			LsIrValue pointer_value = buildExpression(builder, expression.expression);
			LsOpLoadConst* zero = pointer_value == LS_IR_INVALID_VALUE ? nullptr : builder.append<LsOpLoadConst>();
			LsOpLoadIndexed* load = zero ? builder.append<LsOpLoadIndexed>() : nullptr;
			if (!zero || !load) return LS_IR_INVALID_VALUE;
			static ResolvedType isize_type(ResolvedType::ISIZE);
			zero->type = &isize_type; zero->result = builder.newValue(); zero->value = 0;
			load->type = expression.resolved_type ? expression.resolved_type : field_type;
			load->result = builder.newValue(); load->base = pointer_value; load->index = zero->result;
			load->scale = 1; load->offset = field_offset; load->length = 1; load->base_is_value = true;
			return load->result;
		}
		if (pointer.inner && pointer.inner->kind == ResolvedType::STRUCT) {
			ResolvedType* field_type = nullptr;
			const u32 field_offset = structFieldOffset(*static_cast<StructResolvedType*>(pointer.inner), expression.name, field_type);
			if (field_offset == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
			LsIrValue pointer_value = buildExpression(builder, expression.expression);
			LsOpLoadConst* zero = pointer_value == LS_IR_INVALID_VALUE ? nullptr : builder.append<LsOpLoadConst>();
			LsOpLoadIndexed* load = zero ? builder.append<LsOpLoadIndexed>() : nullptr;
			if (!zero || !load) return LS_IR_INVALID_VALUE;
			static ResolvedType isize_type(ResolvedType::ISIZE);
			zero->type = &isize_type; zero->result = builder.newValue(); zero->value = 0;
			load->type = field_type; load->result = builder.newValue(); load->base = pointer_value; load->index = zero->result;
			load->scale = 1; load->offset = field_offset; load->length = 1; load->base_is_value = true;
			return load->result;
		}
	}
	if (base_type->kind == ResolvedType::UNION) {
		ResolvedType* field_type = nullptr;
		const u32 field_offset = unionFieldOffset(*static_cast<UnionResolvedType*>(base_type), expression.name, field_type);
		if (field_offset == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
		const LsIrValue aggregate = buildExpression(builder, expression.expression);
		if (aggregate == LS_IR_INVALID_VALUE) return aggregate;
		LsOpCopy* op = builder.append<LsOpCopy>();
		if (!op) return LS_IR_INVALID_VALUE;
		op->type = expression.resolved_type ? expression.resolved_type : field_type;
		op->result = builder.newValue();
		op->source = aggregate;
		op->source_offset = field_offset;
		return op->result;
	}
	if (base_type->kind != ResolvedType::STRUCT) return LS_IR_INVALID_VALUE;
	ResolvedType* field_type = nullptr;
	const u32 field_offset = structFieldOffset(*static_cast<StructResolvedType*>(base_type), expression.name, field_type);
	if (field_offset == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
	if (expression.expression->kind == Expression::BRACKET) {
		BracketExpression& bracket = *static_cast<BracketExpression*>(expression.expression);
		if (bracket.base && bracket.base->kind == Expression::IDENTIFIER && bracket.base->resolved_type && bracket.base->resolved_type->kind == ResolvedType::SLICE && bracket.args.size() == 1) {
			IdentifierExpression& base = *static_cast<IdentifierExpression*>(bracket.base);
			LsIrValue index = buildExpression(builder, bracket.args[0]);
			if (!base.slot || index == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
			LsOpSliceLoadAt* load = builder.append<LsOpSliceLoadAt>();
			if (!load) return LS_IR_INVALID_VALUE;
			SliceResolvedType& slice = *static_cast<SliceResolvedType*>(base.resolved_type);
			load->type = field_type;
			load->result = builder.newValue();
			load->slice = builder.localOffset(base.slot, base.resolved_type);
			load->index = index;
			load->element_offset = field_offset;
			load->element_size = typeByteSize(*slice.element_type);
			load->index_is_i32 = bracket.args[0]->resolved_type && (bracket.args[0]->resolved_type->kind == ResolvedType::I32 || bracket.args[0]->resolved_type->kind == ResolvedType::U32);
			return load->result;
		}
	}
	const LsIrValue aggregate = buildExpression(builder, expression.expression);
	if (aggregate == LS_IR_INVALID_VALUE) return aggregate;
	LsOpCopy* op = builder.append<LsOpCopy>();
	if (!op) return LS_IR_INVALID_VALUE;
	op->type = expression.resolved_type ? expression.resolved_type : field_type;
	op->result = builder.newValue();
	op->source = aggregate;
	op->source_offset = field_offset;
	if (field_type && field_type->kind == ResolvedType::NULLABLE && op->type != field_type)
		op->source_offset += 1;
	return op->result;
}

static LsIrValue buildTypeMember(IrBuilder& builder, TypeMemberExpression& expression, ResolvedType* target) {
	ResolvedType* reflected = expression.reflected_type;
	ResolvedType* result_type = target ? target : expression.resolved_type;
	if (!reflected || !result_type) return LS_IR_INVALID_VALUE;
	LsOpLoadConst* constant = builder.append<LsOpLoadConst>();
	if (!constant) return LS_IR_INVALID_VALUE;
	constant->type = result_type;
	constant->result = builder.newValue();
	if (expression.kind == TypeMemberExpression::NAME) {
		constant->value = (u64)(uintptr)expression.comptime_string.begin;
		constant->second_value = (u64)(expression.comptime_string.end - expression.comptime_string.begin);
		constant->has_second_value = true;
		return constant->result;
	}
	if (expression.kind == TypeMemberExpression::KIND) {
		static const ResolvedType::Kind kinds[] = { ResolvedType::BOOL, ResolvedType::I8, ResolvedType::I16, ResolvedType::I32, ResolvedType::I64, ResolvedType::ISIZE, ResolvedType::U8, ResolvedType::U16, ResolvedType::U32, ResolvedType::U64, ResolvedType::BYTE, ResolvedType::F32, ResolvedType::F64, ResolvedType::CSTR, ResolvedType::CPTR, ResolvedType::VOID, ResolvedType::META, ResolvedType::NULLABLE, ResolvedType::SLICE, ResolvedType::ARRAY, ResolvedType::ENUM, ResolvedType::STRUCT, ResolvedType::UNION, ResolvedType::FUNCTION, ResolvedType::POINTER };
		ResolvedType::Kind kind = reflected->kind;
		if (kind == ResolvedType::UNTYPED_INT) kind = ResolvedType::I32;
		if (kind == ResolvedType::UNTYPED_FLOAT) kind = ResolvedType::F64;
		for (u32 i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) if (kinds[i] == kind) { constant->value = i; return constant->result; }
		return LS_IR_INVALID_VALUE;
	}
	if (expression.kind == TypeMemberExpression::LENGTH) {
		if (reflected->kind != ResolvedType::ARRAY) return LS_IR_INVALID_VALUE;
		constant->value = (u64)static_cast<ArrayResolvedType*>(reflected)->size;
		return constant->result;
	}
	if (expression.kind != TypeMemberExpression::MIN && expression.kind != TypeMemberExpression::MAX) return LS_IR_INVALID_VALUE;
	const bool min = expression.kind == TypeMemberExpression::MIN;
	switch (reflected->kind) {
		case ResolvedType::I8: constant->value = (u64)(i64)(min ? -128 : 127); break;
		case ResolvedType::I16: constant->value = (u64)(i64)(min ? -32768 : 32767); break;
		case ResolvedType::I32: constant->value = (u64)(i64)(min ? -2147483648LL : 2147483647LL); break;
		case ResolvedType::I64:
		case ResolvedType::ISIZE: constant->value = (u64)(min ? (i64)-9223372036854775807LL - 1 : (i64)9223372036854775807LL); break;
		case ResolvedType::U8:
		case ResolvedType::BYTE: constant->value = min ? 0u : 255u; break;
		case ResolvedType::U16: constant->value = min ? 0u : 65535u; break;
		case ResolvedType::U32: constant->value = min ? 0u : 0xffffffffu; break;
		case ResolvedType::U64: constant->value = min ? 0u : 0xffffffffffffffffULL; break;
		case ResolvedType::F32: { float value = min ? -FLT_MAX : FLT_MAX; copyMemory(&constant->value, &value, sizeof(value)); break; }
		case ResolvedType::F64: { double value = min ? -DBL_MAX : DBL_MAX; copyMemory(&constant->value, &value, sizeof(value)); break; }
		default: return LS_IR_INVALID_VALUE;
	}
	return constant->result;
}

static LsIrValue buildLogicalBinary(IrBuilder& builder, BinaryExpression& expression) {
	const LsIrValue lhs = buildExpression(builder, expression.lhs);
	if (lhs == LS_IR_INVALID_VALUE || !expression.resolved_type) return LS_IR_INVALID_VALUE;
	const LsIrValue result = builder.newValue();
	const LsIrBlock first_branch = builder.newBlock();
	const LsIrBlock second_branch = builder.newBlock();
	const LsIrBlock short_block = expression.op == Token::AND ? first_branch : second_branch;
	const LsIrBlock rhs_block = expression.op == Token::AND ? second_branch : first_branch;
	const LsIrBlock merge_block = builder.newBlock();
	LsOpConditionalJump* branch = builder.terminate<LsOpConditionalJump>();
	if (!branch) return LS_IR_INVALID_VALUE;
	branch->condition = lhs;
	branch->target = expression.op == Token::AND ? rhs_block : short_block;

	builder.selectBlock(short_block);
	LsOpLoadConst* short_value = builder.append<LsOpLoadConst>();
	LsOpJump* short_jump = short_value ? builder.terminate<LsOpJump>() : nullptr;
	if (!short_value || !short_jump) return LS_IR_INVALID_VALUE;
	short_value->type = expression.resolved_type;
	short_value->result = result;
	short_value->value = expression.op == Token::AND ? 0u : 1u;
	short_jump->target = merge_block;

	builder.selectBlock(rhs_block);
	const LsIrValue rhs = buildExpression(builder, expression.rhs);
	if (rhs == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
	LsOpCopy* rhs_value = builder.append<LsOpCopy>();
	if (!rhs_value) return LS_IR_INVALID_VALUE;
	rhs_value->type = expression.resolved_type;
	rhs_value->result = result;
	rhs_value->source = rhs;
	if (!builder.block->terminator) {
		LsOpJump* rhs_jump = builder.terminate<LsOpJump>();
		if (!rhs_jump) return LS_IR_INVALID_VALUE;
		rhs_jump->target = merge_block;
	}
	builder.selectBlock(merge_block);
	return result;
}

static LsIrValue buildTernary(IrBuilder& builder, TernaryExpression& expression) {
	const LsIrValue condition = buildExpression(builder, expression.condition);
	if (condition == LS_IR_INVALID_VALUE || !expression.resolved_type) return LS_IR_INVALID_VALUE;
	const LsIrValue result = builder.newValue();
	const LsIrBlock false_block = builder.newBlock();
	LsOpConditionalJump* branch = builder.terminate<LsOpConditionalJump>();
	if (!branch) return LS_IR_INVALID_VALUE;
	branch->condition = condition;
	branch->target = LS_IR_INVALID_BLOCK;

	builder.selectBlock(false_block);
	const LsIrValue false_value = buildExpression(builder, expression.false_expr);
	if (false_value == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
	LsOpCopy* false_copy = builder.append<LsOpCopy>();
	LsOpJump* false_jump = false_copy ? builder.terminate<LsOpJump>() : nullptr;
	if (!false_copy || !false_jump) return LS_IR_INVALID_VALUE;
	false_copy->type = expression.resolved_type;
	false_copy->result = result;
		false_copy->source = false_value;
		false_jump->target = LS_IR_INVALID_BLOCK;

		const LsIrBlock true_block = builder.newBlock();
		const LsIrBlock merge_block = builder.newBlock();
	branch->target = true_block;
	false_jump->target = merge_block;
	builder.selectBlock(true_block);
	const LsIrValue true_value = buildExpression(builder, expression.true_expr);
	if (true_value == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
	LsOpCopy* true_copy = builder.append<LsOpCopy>();
	if (!true_copy) return LS_IR_INVALID_VALUE;
	true_copy->type = expression.resolved_type;
	true_copy->result = result;
	true_copy->source = true_value;
	if (!builder.block->terminator) {
		LsOpJump* true_jump = builder.terminate<LsOpJump>();
		if (!true_jump) return LS_IR_INVALID_VALUE;
		true_jump->target = merge_block;
	}
	builder.selectBlock(merge_block);
	return result;
}

static LsIrValue buildCast(IrBuilder& builder, CastExpression& expression) {
	if (!expression.expression || !expression.resolved_type) return LS_IR_INVALID_VALUE;
	LsIrValue value = buildExpression(builder, expression.expression);
	if (value == LS_IR_INVALID_VALUE) return value;
	if (expression.expression->resolved_type && expression.expression->resolved_type->kind == ResolvedType::SLICE && expression.resolved_type->kind == ResolvedType::SLICE) {
		SliceResolvedType& source_type = *static_cast<SliceResolvedType*>(expression.expression->resolved_type);
		SliceResolvedType& target_type = *static_cast<SliceResolvedType*>(expression.resolved_type);
		const u32 source_size = typeByteSize(*source_type.element_type);
		const u32 target_size = typeByteSize(*target_type.element_type);
		if (!source_size || !target_size) return LS_IR_INVALID_VALUE;
		LsOpCopy* copy = builder.append<LsOpCopy>();
		LsOpSliceLength* length = copy ? builder.append<LsOpSliceLength>() : nullptr;
		if (!copy || !length) return LS_IR_INVALID_VALUE;
		copy->type = expression.resolved_type; copy->result = builder.newValue(); copy->source = value;
		length->result = builder.newValue(); length->slice = value; length->slice_offset = value; length->slice_is_value = true;
		LsIrValue length_value = length->result;
		if (source_size != target_size) {
			LsOpLoadConst* factor = builder.append<LsOpLoadConst>();
			LsOpAdd* operation = source_size > target_size ? static_cast<LsOpAdd*>(builder.append<LsOpMul>()) : static_cast<LsOpAdd*>(builder.append<LsOpDiv>());
			if (!factor || !operation) return LS_IR_INVALID_VALUE;
			static ResolvedType isize_type(ResolvedType::ISIZE);
			factor->type = &isize_type; factor->result = builder.newValue(); factor->value = source_size > target_size ? source_size / target_size : target_size / source_size;
			operation->type = &isize_type; operation->result = builder.newValue(); operation->lhs = length_value; operation->rhs = factor->result;
			length_value = operation->result;
		}
		static ResolvedType isize_type(ResolvedType::ISIZE);
		LsOpFieldStore* store = builder.append<LsOpFieldStore>();
		if (!store) return LS_IR_INVALID_VALUE;
		store->type = &isize_type; store->aggregate = copy->result; store->source = length_value; store->offset = sizeof(void*);
		return copy->result;
	}
	LsOpCast* op = builder.append<LsOpCast>();
	if (!op) return LS_IR_INVALID_VALUE;
	op->type = expression.expression->resolved_type;
	op->target_type = expression.resolved_type;
	op->result = builder.newValue();
	op->value = value;
	return op->result;
}

static void storeValue(IrBuilder& builder, Expression* lhs, LsIrValue value) {
	if (value == LS_IR_INVALID_VALUE) return;
	if (lhs && lhs->kind == Expression::DEREFERENCE) {
		DereferenceExpression& dereference = *static_cast<DereferenceExpression*>(lhs);
		LsIrValue pointer = buildExpression(builder, dereference.subject);
		LsOpLoadConst* zero = pointer == LS_IR_INVALID_VALUE ? nullptr : builder.append<LsOpLoadConst>();
		LsOpStoreIndexed* store = zero ? builder.append<LsOpStoreIndexed>() : nullptr;
		if (!zero || !store) return;
		static ResolvedType isize_type(ResolvedType::ISIZE);
		zero->type = &isize_type;
		zero->result = builder.newValue();
		zero->value = 0;
		store->type = lhs->resolved_type;
		store->base = pointer;
		store->index = zero->result;
		store->source = value;
		store->scale = 1;
		store->offset = 0;
		if (dereference.subject->kind == Expression::IDENTIFIER) {
			IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(dereference.subject);
			if (identifier.slot && identifier.slot->type && identifier.slot->type->kind == ResolvedType::POINTER) {
				PointerResolvedType& pointer_type = *static_cast<PointerResolvedType*>(identifier.slot->type);
				if (pointer_type.inner && pointer_type.inner->kind == ResolvedType::UNION && lhs->resolved_type->kind != ResolvedType::UNION) store->offset = sizeof(i32);
			}
		}
		store->length = 1;
		store->base_is_value = true;
		return;
	}
	if (lhs && lhs->kind == Expression::MEMBER) {
		MemberExpression& member = *static_cast<MemberExpression*>(lhs);
		if (!member.expression || !member.expression->resolved_type) return;
		if (member.expression->resolved_type->kind == ResolvedType::POINTER) {
			PointerResolvedType& pointer = *static_cast<PointerResolvedType*>(member.expression->resolved_type);
			if (!pointer.inner || pointer.inner->kind != ResolvedType::STRUCT) return;
			ResolvedType* field_type = nullptr;
			const u32 field_offset = structFieldOffset(*static_cast<StructResolvedType*>(pointer.inner), member.name, field_type);
			LsIrValue pointer_value = buildExpression(builder, member.expression);
			LsOpLoadConst* zero = pointer_value == LS_IR_INVALID_VALUE ? nullptr : builder.append<LsOpLoadConst>();
			LsOpStoreIndexed* store = zero ? builder.append<LsOpStoreIndexed>() : nullptr;
			if (!zero || !store || value == LS_IR_INVALID_VALUE) return;
			static ResolvedType isize_type(ResolvedType::ISIZE);
			zero->type = &isize_type; zero->result = builder.newValue(); zero->value = 0;
			store->type = field_type; store->base = pointer_value; store->index = zero->result; store->source = value;
			store->scale = 1; store->offset = field_offset; store->length = 1; store->base_is_value = true;
			return;
		}
		if (member.expression->resolved_type->kind != ResolvedType::STRUCT) return;
		ResolvedType* field_type = nullptr;
		const u32 field_offset = structFieldOffset(*static_cast<StructResolvedType*>(member.expression->resolved_type), member.name, field_type);
		if (field_offset == LS_IR_INVALID_VALUE) return;
		if (member.expression->kind == Expression::BRACKET) {
			BracketExpression& bracket = *static_cast<BracketExpression*>(member.expression);
			if (bracket.base && bracket.base->kind == Expression::IDENTIFIER && bracket.base->resolved_type && bracket.base->resolved_type->kind == ResolvedType::SLICE && bracket.args.size() == 1) {
				IdentifierExpression& base = *static_cast<IdentifierExpression*>(bracket.base);
				LsIrValue index = buildExpression(builder, bracket.args[0]);
				if (!base.slot || index == LS_IR_INVALID_VALUE) return;
				LsOpSliceStoreAt* store = builder.append<LsOpSliceStoreAt>();
				if (!store) return;
				SliceResolvedType& slice = *static_cast<SliceResolvedType*>(base.resolved_type);
				store->type = field_type;
				store->slice = builder.localOffset(base.slot, base.resolved_type);
				store->index = index;
				store->source = value;
				store->element_offset = field_offset;
				store->element_size = typeByteSize(*slice.element_type);
				store->index_is_i32 = bracket.args[0]->resolved_type && (bracket.args[0]->resolved_type->kind == ResolvedType::I32 || bracket.args[0]->resolved_type->kind == ResolvedType::U32);
				return;
			}
		}
		if (member.expression->kind == Expression::IDENTIFIER) {
			IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(member.expression);
			if (identifier.slot && identifier.slot->storage != StorageSlot::GLOBAL) {
				LsOpLocalStore* store = builder.append<LsOpLocalStore>();
				if (!store) return;
				store->type = field_type;
				store->source = value;
				store->offset = builder.localOffset(identifier.slot, member.expression->resolved_type) + field_offset;
				return;
			}
		}
		const LsIrValue aggregate = buildExpression(builder, member.expression);
		if (aggregate == LS_IR_INVALID_VALUE) return;
		LsOpFieldStore* field_store = builder.append<LsOpFieldStore>();
		if (!field_store) return;
		field_store->type = field_type;
		field_store->aggregate = aggregate;
		field_store->source = value;
		field_store->offset = field_offset;
		storeValue(builder, member.expression, aggregate);
		return;
	}
	if (lhs && lhs->kind == Expression::BRACKET) {
		BracketExpression& bracket = *static_cast<BracketExpression*>(lhs);
		if (bracket.base && bracket.args.size() == 1) {
			if (bracket.base->kind == Expression::MEMBER) {
				MemberExpression& field = *static_cast<MemberExpression*>(bracket.base);
				IdentifierExpression* aggregate = field.expression && field.expression->kind == Expression::IDENTIFIER
					? static_cast<IdentifierExpression*>(field.expression) : nullptr;
				if (aggregate && aggregate->slot && aggregate->slot->storage != StorageSlot::GLOBAL
					&& field.expression->resolved_type && field.expression->resolved_type->kind == ResolvedType::STRUCT
					&& bracket.base->resolved_type && bracket.base->resolved_type->kind == ResolvedType::ARRAY) {
					ResolvedType* field_type = nullptr;
					const u32 field_offset = structFieldOffset(*static_cast<StructResolvedType*>(field.expression->resolved_type), field.name, field_type);
					ArrayResolvedType* array = static_cast<ArrayResolvedType*>(bracket.base->resolved_type);
					LsIrValue index = field_offset == LS_IR_INVALID_VALUE ? LS_IR_INVALID_VALUE : buildExpression(builder, bracket.args[0]);
					LsOpStoreIndexedLocalI32* store = index == LS_IR_INVALID_VALUE ? nullptr : builder.append<LsOpStoreIndexedLocalI32>();
					if (!store) return;
					store->type = lhs->resolved_type;
					store->base_offset = builder.localOffset(aggregate->slot, field.expression->resolved_type) + field_offset;
					store->index = index;
					store->source = value;
					store->scale = array->element_type ? typeByteSize(*array->element_type) : 0;
					store->length = (u32)array->size;
					return;
				}
			}
			StorageSlot* base_slot = nullptr;
			ResolvedType* base_type = bracket.base->resolved_type;
			if (bracket.base->kind == Expression::IDENTIFIER) base_slot = static_cast<IdentifierExpression*>(bracket.base)->slot;
			else if (bracket.base->kind == Expression::MEMBER) {
				MemberExpression& base = *static_cast<MemberExpression*>(bracket.base);
				base_slot = base.resolved_symbol ? &base.resolved_symbol->slot : nullptr;
			}
			if (base_slot && base_slot->storage != StorageSlot::GLOBAL && base_type && base_type->kind == ResolvedType::ARRAY) {
				ArrayResolvedType* array = static_cast<ArrayResolvedType*>(base_type);
				LsIrValue index = buildExpression(builder, bracket.args[0]);
				LsOpStoreIndexedLocalI32* store = builder.append<LsOpStoreIndexedLocalI32>();
				if (!store || index == LS_IR_INVALID_VALUE) return;
				store->type = lhs->resolved_type;
				store->base_offset = builder.localOffset(base_slot, base_type);
				store->index = index;
				store->source = value;
				store->scale = array->element_type ? typeByteSize(*array->element_type) : 0;
				store->length = (u32)array->size;
				return;
			}
			if (base_slot && base_slot->storage == StorageSlot::GLOBAL && base_type && base_type->kind == ResolvedType::ARRAY) {
				ArrayResolvedType* array = static_cast<ArrayResolvedType*>(base_type);
				static ResolvedType isize_type(ResolvedType::ISIZE);
				LsIrValue index = buildExpressionAsType(builder, bracket.args[0], &isize_type);
				LsOpGlobalRef* reference = index == LS_IR_INVALID_VALUE ? nullptr : builder.append<LsOpGlobalRef>();
				LsOpStoreIndexed* store = reference ? builder.append<LsOpStoreIndexed>() : nullptr;
				if (!reference || !store) return;
				static ResolvedType pointer_type(ResolvedType::POINTER);
				reference->type = &pointer_type;
				reference->result = builder.newValue();
				reference->offset = base_slot->offset;
				store->type = lhs->resolved_type;
				store->base = reference->result;
				store->index = index;
				store->source = value;
				store->scale = typeByteSize(*array->element_type);
				store->offset = 0;
				store->length = (u32)array->size;
				store->base_is_value = true;
				return;
			}
			if (base_slot && base_slot->storage != StorageSlot::GLOBAL && base_type && base_type->kind == ResolvedType::SLICE) {
				SliceResolvedType* slice = static_cast<SliceResolvedType*>(base_type);
				LsIrValue index = buildExpression(builder, bracket.args[0]);
				LsOpSliceStoreLocalI32* store = builder.append<LsOpSliceStoreLocalI32>();
				if (!store || index == LS_IR_INVALID_VALUE) return;
				store->type = lhs->resolved_type;
				store->slice_offset = builder.localOffset(base_slot, base_type);
				store->index = index;
				store->source = value;
				store->element_size = slice->element_type ? typeByteSize(*slice->element_type) : 0;
				return;
			}
		}
	}
	if (!lhs || lhs->kind != Expression::IDENTIFIER) return;
	IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(lhs);
	if (!identifier.slot) return;
	if (identifier.slot->storage == StorageSlot::GLOBAL) {
		LsOpGlobalStore* store = builder.append<LsOpGlobalStore>();
		if (!store) return;
		store->type = lhs->resolved_type;
		store->source = value;
		store->offset = identifier.slot->offset;
		return;
	}
	LsOpLocalStore* store = builder.append<LsOpLocalStore>();
	if (!store) return;
	store->type = lhs->resolved_type;
	store->source = value;
	store->offset = builder.localOffset(identifier.slot, lhs->resolved_type);
}

static LsIrValue buildUnary(IrBuilder& builder, UnaryExpression& expression) {
	if (expression.resolved_fn) {
		Expression* arguments[] = {expression.expression};
		return buildOperatorCall(builder, expression.resolved_fn, arguments, 1, expression.resolved_type);
	}
	const LsIrValue value = buildExpression(builder, expression.expression);
	if (value == LS_IR_INVALID_VALUE) return value;
	if (expression.op != Token::MINUS && expression.op != Token::NOT) return LS_IR_INVALID_VALUE;
	LsOpNeg* op = expression.op == Token::MINUS
		? builder.append<LsOpNeg>()
		: static_cast<LsOpNeg*>(builder.append<LsOpNot>());
	if (!op) return LS_IR_INVALID_VALUE;
	op->type = expression.resolved_type;
	op->result = builder.newValue();
	op->value = value;
	return op->result;
}

static LsIrValue buildBinary(IrBuilder& builder, BinaryExpression& expression) {
	if (expression.op == Token::AND || expression.op == Token::OR) return buildLogicalBinary(builder, expression);
	if (expression.op == Token::IS) {
		if (!expression.lhs || !expression.rhs || !expression.lhs->resolved_type || expression.lhs->resolved_type->kind != ResolvedType::UNION ||
			!expression.rhs->resolved_type || expression.rhs->resolved_type->kind != ResolvedType::META) return LS_IR_INVALID_VALUE;
		UnionResolvedType& source_type = *static_cast<UnionResolvedType*>(expression.lhs->resolved_type);
		ResolvedType* member = static_cast<MetaType*>(expression.rhs->resolved_type)->inner;
		i32 member_index = -1;
		for (i32 i = 0; i < source_type.members.size(); ++i) {
			ResolvedType* candidate = source_type.members[i];
			bool equal = candidate && member && candidate->kind == member->kind;
			if (equal && candidate->kind == ResolvedType::STRUCT)
				equal = static_cast<StructResolvedType*>(candidate)->decl == static_cast<StructResolvedType*>(member)->decl;
			if (equal) { member_index = i; break; }
		}
		if (member_index < 0) return LS_IR_INVALID_VALUE;
		LsIrValue source = buildExpression(builder, expression.lhs);
		LsOpCopy* tag = source == LS_IR_INVALID_VALUE ? nullptr : builder.append<LsOpCopy>();
		LsOpLoadConst* expected = tag ? builder.append<LsOpLoadConst>() : nullptr;
		LsOpCompare* compare = expected ? builder.append<LsOpCompare>() : nullptr;
		if (!tag || !expected || !compare) return LS_IR_INVALID_VALUE;
		static ResolvedType i32_type(ResolvedType::I32);
		tag->type = &i32_type; tag->result = builder.newValue(); tag->source = source; tag->source_offset = 0;
		expected->type = &i32_type; expected->result = builder.newValue(); expected->value = (u32)member_index;
		compare->type = &i32_type; compare->result = builder.newValue(); compare->lhs = tag->result; compare->rhs = expected->result; compare->op = LS_IR_COMPARE_EQ;
		return compare->result;
	}
	if (expression.resolved_fn) {
		Expression* arguments[] = {expression.lhs, expression.rhs};
		return buildOperatorCall(builder, expression.resolved_fn, arguments, 2, expression.resolved_type);
	}
	const bool lhs_nullable_null = expression.lhs && expression.lhs->resolved_type && expression.lhs->resolved_type->kind == ResolvedType::NULLABLE && expression.rhs && expression.rhs->kind == Expression::NULL_LITERAL;
	const bool rhs_nullable_null = expression.rhs && expression.rhs->resolved_type && expression.rhs->resolved_type->kind == ResolvedType::NULLABLE && expression.lhs && expression.lhs->kind == Expression::NULL_LITERAL;
	if (lhs_nullable_null || rhs_nullable_null) {
		Expression* nullable_expression = lhs_nullable_null ? expression.lhs : expression.rhs;
		LsIrValue source = buildExpression(builder, nullable_expression);
		if (source == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
		LsOpCopy* flag = builder.append<LsOpCopy>();
		LsOpLoadConst* expected = flag ? builder.append<LsOpLoadConst>() : nullptr;
		LsOpCompare* compare = expected ? builder.append<LsOpCompare>() : nullptr;
		if (!flag || !expected || !compare) return LS_IR_INVALID_VALUE;
		static ResolvedType bool_type(ResolvedType::BOOL);
		flag->type = &bool_type; flag->result = builder.newValue(); flag->source = source; flag->source_offset = 0;
		expected->type = &bool_type; expected->result = builder.newValue(); expected->value = 0u;
		compare->type = &bool_type; compare->result = builder.newValue(); compare->lhs = flag->result; compare->rhs = expected->result;
		compare->op = expression.op == Token::EQUAL_EQUAL ? LS_IR_COMPARE_EQ : LS_IR_COMPARE_NE;
		return compare->result;
	}
	const LsIrValue lhs = buildExpression(builder, expression.lhs);
	const LsIrValue rhs = buildExpression(builder, expression.rhs);
	if (lhs == LS_IR_INVALID_VALUE || rhs == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
	if (expression.lhs && expression.rhs && expression.lhs->resolved_type && expression.lhs->resolved_type->kind == ResolvedType::SLICE &&
		(expression.op == Token::EQUAL_EQUAL || expression.op == Token::BANG_EQUAL)) {
		SliceResolvedType& slice = *static_cast<SliceResolvedType*>(expression.lhs->resolved_type);
		LsOpSliceEq* compare = builder.append<LsOpSliceEq>();
		if (!compare) return LS_IR_INVALID_VALUE;
		compare->type = slice.element_type;
		compare->result = builder.newValue();
		compare->lhs = lhs;
		compare->rhs = rhs;
		if (expression.op == Token::BANG_EQUAL) {
			LsOpNot* not_equal = builder.append<LsOpNot>();
			if (!not_equal) return LS_IR_INVALID_VALUE;
			not_equal->type = expression.resolved_type;
			not_equal->result = builder.newValue();
			not_equal->value = compare->result;
			return not_equal->result;
		}
		return compare->result;
	}
	LsOpAdd* op = nullptr;
	switch (expression.op) {
		case Token::PLUS: op = builder.append<LsOpAdd>(); break;
		case Token::MINUS: op = builder.append<LsOpSub>(); break;
		case Token::STAR: op = builder.append<LsOpMul>(); break;
		case Token::SLASH: op = builder.append<LsOpDiv>(); break;
		case Token::PERCENT: op = builder.append<LsOpMod>(); break;
		case Token::EQUAL_EQUAL:
		case Token::BANG_EQUAL:
		case Token::LT:
		case Token::LT_EQUAL:
		case Token::GT:
		case Token::GT_EQUAL: op = builder.append<LsOpCompare>(); break;
		default:
			builder.failed = true;
			return LS_IR_INVALID_VALUE;
	}
	if (!op) return LS_IR_INVALID_VALUE;
	op->type = expression.lhs ? expression.lhs->resolved_type : expression.resolved_type;
	op->result = builder.newValue();
	op->lhs = lhs;
	op->rhs = rhs;
	if (op->kind == LS_IR_OP_COMPARE) {
		LsOpCompare& compare = *static_cast<LsOpCompare*>(op);
		switch (expression.op) {
			case Token::EQUAL_EQUAL: compare.op = LS_IR_COMPARE_EQ; break;
			case Token::BANG_EQUAL: compare.op = LS_IR_COMPARE_NE; break;
			case Token::LT: compare.op = LS_IR_COMPARE_LT; break;
			case Token::LT_EQUAL: compare.op = LS_IR_COMPARE_LE; break;
			case Token::GT: compare.op = LS_IR_COMPARE_GT; break;
			case Token::GT_EQUAL: compare.op = LS_IR_COMPARE_GE; break;
			default: break;
		}
	}
	return op->result;
}

static LsIrValue buildBracket(IrBuilder& builder, BracketExpression& expression) {
	if (!expression.base || expression.args.size() != 1) return LS_IR_INVALID_VALUE;
	if (expression.base->resolved_type && expression.base->resolved_type->kind == ResolvedType::ARRAY &&
		expression.base->kind != Expression::IDENTIFIER && expression.base->kind != Expression::MEMBER) {
		ArrayResolvedType& array = *static_cast<ArrayResolvedType*>(expression.base->resolved_type);
		LsIrValue base = buildExpression(builder, expression.base);
		LsIrValue index = buildExpression(builder, expression.args[0]);
		if (expression.args[0]->kind == Expression::INT_LITERAL) {
			const i64 literal_index = static_cast<IntLiteralExpression*>(expression.args[0])->value;
			if (literal_index < 0 || (u64)literal_index >= (u64)array.size) return LS_IR_INVALID_VALUE;
			LsOpCopy* copy = base == LS_IR_INVALID_VALUE ? nullptr : builder.append<LsOpCopy>();
			if (!copy) return LS_IR_INVALID_VALUE;
			copy->type = expression.resolved_type; copy->result = builder.newValue(); copy->source = base;
			copy->source_offset = (u32)literal_index * typeByteSize(*array.element_type);
			return copy->result;
		}
		LsOpLoadIndexed* load = (base == LS_IR_INVALID_VALUE || index == LS_IR_INVALID_VALUE) ? nullptr : builder.append<LsOpLoadIndexed>();
		if (!load) return LS_IR_INVALID_VALUE;
		load->type = expression.resolved_type; load->result = builder.newValue(); load->base = base; load->index = index;
		load->scale = typeByteSize(*array.element_type); load->offset = 0; load->length = (u32)array.size; load->base_is_value = true;
		return load->result;
	}
	StorageSlot* base_slot = nullptr;
	ResolvedType* base_type = expression.base->resolved_type;
	if (expression.base->kind == Expression::IDENTIFIER) {
		IdentifierExpression& base = *static_cast<IdentifierExpression*>(expression.base);
		base_slot = base.slot;
	} else if (expression.base->kind == Expression::MEMBER) {
		MemberExpression& base = *static_cast<MemberExpression*>(expression.base);
		base_slot = base.resolved_symbol ? &base.resolved_symbol->slot : nullptr;
	}
	if (!base_type || base_type->kind != ResolvedType::ARRAY) return LS_IR_INVALID_VALUE;
	ArrayResolvedType* array = static_cast<ArrayResolvedType*>(base_type);
	if ((!base_slot || base_slot->storage == StorageSlot::GLOBAL) && expression.args[0]->kind == Expression::INT_LITERAL) {
		const i64 literal_index = static_cast<IntLiteralExpression*>(expression.args[0])->value;
		if (literal_index < 0 || (u64)literal_index >= (u64)array->size) return LS_IR_INVALID_VALUE;
		LsIrValue source = buildExpression(builder, expression.base);
		LsOpCopy* copy = source == LS_IR_INVALID_VALUE ? nullptr : builder.append<LsOpCopy>();
		if (!copy) return LS_IR_INVALID_VALUE;
		copy->type = expression.resolved_type; copy->result = builder.newValue(); copy->source = source;
		copy->source_offset = (u32)literal_index * typeByteSize(*array->element_type);
		return copy->result;
	}
	LsIrValue index = buildExpression(builder, expression.args[0]);
	if (index == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
	if (base_slot && base_slot->storage == StorageSlot::GLOBAL) {
		static ResolvedType isize_type(ResolvedType::ISIZE);
		index = buildExpressionAsType(builder, expression.args[0], &isize_type);
		if (index == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
		LsOpGlobalRef* reference = builder.append<LsOpGlobalRef>();
		LsOpLoadIndexed* load = reference ? builder.append<LsOpLoadIndexed>() : nullptr;
		if (!reference || !load) return LS_IR_INVALID_VALUE;
		static ResolvedType pointer_type(ResolvedType::POINTER);
		reference->type = &pointer_type;
		reference->result = builder.newValue();
		reference->offset = base_slot->offset;
		load->type = expression.resolved_type;
		load->result = builder.newValue();
		load->base = reference->result;
		load->index = index;
		load->scale = typeByteSize(*array->element_type);
		load->offset = 0;
		load->length = (u32)array->size;
		load->base_is_value = true;
		return load->result;
	}
	if (!base_slot) return LS_IR_INVALID_VALUE;
	LsOpLoadIndexedLocalI32* load = builder.append<LsOpLoadIndexedLocalI32>();
	if (!load) return LS_IR_INVALID_VALUE;
	load->type = expression.resolved_type;
	load->result = builder.newValue();
	load->base_offset = builder.localOffset(base_slot, base_type);
	load->index = index;
	load->scale = array->element_type ? typeByteSize(*array->element_type) : 0;
	load->length = (u32)array->size;
	return load->result;
}

static LsIrValue buildSliceExpression(IrBuilder& builder, SliceExpression& expression) {
	if (!expression.base || !expression.base->resolved_type) return LS_IR_INVALID_VALUE;
	if (expression.base->kind == Expression::DEREFERENCE) {
		DereferenceExpression& dereference = *static_cast<DereferenceExpression*>(expression.base);
		if (!dereference.subject || !expression.resolved_type || expression.begin || expression.end) return LS_IR_INVALID_VALUE;
		LsIrValue pointer = buildExpression(builder, dereference.subject);
		LsOpMakeSlice* make = pointer == LS_IR_INVALID_VALUE ? nullptr : builder.append<LsOpMakeSlice>();
		if (!make) return LS_IR_INVALID_VALUE;
		make->type = expression.resolved_type;
		make->result = builder.newValue();
		make->base = pointer;
		make->length = 1;
		return make->result;
	}
	if (expression.base->resolved_type->kind == ResolvedType::POINTER && expression.base->kind == Expression::IDENTIFIER) {
		IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(expression.base);
		if (!identifier.slot || !expression.resolved_type || expression.begin || expression.end) return LS_IR_INVALID_VALUE;
		LsOpMakeSlice* make = builder.append<LsOpMakeSlice>();
		if (!make) return LS_IR_INVALID_VALUE;
		make->type = expression.resolved_type;
		make->result = builder.newValue();
		make->base_offset = builder.localOffset(identifier.slot, expression.base->resolved_type);
		make->length = 1;
		return make->result;
	}
	StorageSlot* base_slot = nullptr;
	u32 base_offset = 0;
	ResolvedType* base_type = expression.base->resolved_type;
	if (expression.base->kind == Expression::IDENTIFIER) {
		IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(expression.base);
		base_slot = identifier.slot;
	} else if (expression.base->kind == Expression::MEMBER) {
		MemberExpression& member = *static_cast<MemberExpression*>(expression.base);
		if (!member.expression || member.expression->kind != Expression::IDENTIFIER || !member.expression->resolved_type ||
			member.expression->resolved_type->kind != ResolvedType::STRUCT) return LS_IR_INVALID_VALUE;
		IdentifierExpression& aggregate = *static_cast<IdentifierExpression*>(member.expression);
		base_slot = aggregate.slot;
		ResolvedType* field_type = nullptr;
		const u32 field_offset = structFieldOffset(*static_cast<StructResolvedType*>(member.expression->resolved_type), member.name, field_type);
		if (field_offset == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
		base_offset = field_offset;
	} else if (expression.base->kind == Expression::BRACKET) {
		BracketExpression& bracket = *static_cast<BracketExpression*>(expression.base);
		if (!bracket.base || bracket.base->kind != Expression::IDENTIFIER || bracket.args.size() != 1 ||
			!bracket.base->resolved_type || bracket.base->resolved_type->kind != ResolvedType::ARRAY ||
			bracket.args[0]->kind != Expression::INT_LITERAL) return LS_IR_INVALID_VALUE;
		IdentifierExpression& aggregate = *static_cast<IdentifierExpression*>(bracket.base);
		base_slot = aggregate.slot;
		ArrayResolvedType& array = *static_cast<ArrayResolvedType*>(bracket.base->resolved_type);
		const i64 index = static_cast<IntLiteralExpression*>(bracket.args[0])->value;
		if (index < 0 || (u64)index >= (u64)array.size || !array.element_type) return LS_IR_INVALID_VALUE;
		base_offset = (u32)index * typeByteSize(*array.element_type);
	}
	if (!base_slot || base_slot->storage == StorageSlot::GLOBAL) return LS_IR_INVALID_VALUE;
	if (base_type->kind == ResolvedType::SLICE) {
		if (!expression.resolved_type) return LS_IR_INVALID_VALUE;
		LsOpLocalLoad* load = builder.append<LsOpLocalLoad>();
		if (!load) return LS_IR_INVALID_VALUE;
		load->type = base_type;
		load->result = builder.newValue();
		load->offset = builder.localOffset(base_slot, base_type) + base_offset;
		if (expression.begin || expression.end) {
			LsIrValue begin = expression.begin ? buildExpression(builder, expression.begin) : LS_IR_INVALID_VALUE;
			LsIrValue end = expression.end ? buildExpression(builder, expression.end) : LS_IR_INVALID_VALUE;
			if (!expression.begin && expression.end) {
				LsOpLoadConst* zero = builder.append<LsOpLoadConst>();
				if (!zero) return LS_IR_INVALID_VALUE;
				zero->type = expression.end->resolved_type; zero->result = builder.newValue(); zero->value = 0;
				begin = zero->result;
			}
			if (!expression.end) {
				LsOpSliceLength* length = builder.append<LsOpSliceLength>();
				if (!length) return LS_IR_INVALID_VALUE;
				length->result = builder.newValue(); length->slice_offset = load->result; length->slice_is_value = true;
				end = length->result;
			}
			LsOpSlice* slice = (begin == LS_IR_INVALID_VALUE || end == LS_IR_INVALID_VALUE) ? nullptr : builder.append<LsOpSlice>();
			if (!slice) return LS_IR_INVALID_VALUE;
			slice->type = expression.resolved_type;
			slice->result = load->result;
			slice->source = load->result;
			slice->begin = begin;
			slice->end = end;
		}
		return load->result;
	}
	if (base_type->kind != ResolvedType::ARRAY) {
		if (expression.begin || expression.end) return LS_IR_INVALID_VALUE;
		LsOpMakeSlice* make = builder.append<LsOpMakeSlice>();
		if (!make) return LS_IR_INVALID_VALUE;
		make->type = expression.resolved_type;
		make->result = builder.newValue();
		make->base_offset = builder.localOffset(base_slot, base_type) + base_offset;
		make->length = 1;
		return make->result;
	}
	ArrayResolvedType* array = static_cast<ArrayResolvedType*>(base_type);
	LsOpMakeSlice* make = builder.append<LsOpMakeSlice>();
	if (!make) return LS_IR_INVALID_VALUE;
	make->type = expression.resolved_type;
	make->result = builder.newValue();
	make->base_offset = builder.localOffset(base_slot, base_type) + base_offset;
	make->length = (u64)array->size;
	if (expression.begin || expression.end) {
		LsIrValue begin = expression.begin ? buildExpression(builder, expression.begin) : LS_IR_INVALID_VALUE;
		LsIrValue end = expression.end ? buildExpression(builder, expression.end) : LS_IR_INVALID_VALUE;
		if (!expression.begin) {
			LsOpLoadConst* zero = expression.end ? builder.append<LsOpLoadConst>() : nullptr;
			if (!zero) return LS_IR_INVALID_VALUE;
			zero->type = expression.end->resolved_type;
			zero->result = builder.newValue();
			zero->value = 0;
			begin = zero->result;
		}
		if (!expression.end) {
			LsOpLoadConst* length = expression.begin ? builder.append<LsOpLoadConst>() : nullptr;
			if (!length) return LS_IR_INVALID_VALUE;
			length->type = expression.begin->resolved_type;
			length->result = builder.newValue();
			length->value = (u64)array->size;
			end = length->result;
		}
		LsOpSlice* slice = builder.append<LsOpSlice>();
		if (!slice || begin == LS_IR_INVALID_VALUE || end == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
		slice->type = expression.resolved_type;
		slice->result = make->result;
		slice->source = make->result;
		slice->begin = begin;
		slice->end = end;
	}
	return make->result;
}

static LsIrValue buildArrayAsSlice(IrBuilder& builder, Expression* expression, ResolvedType* target_type) {
	if (!expression || expression->kind != Expression::IDENTIFIER || !target_type || target_type->kind != ResolvedType::SLICE ||
		!expression->resolved_type || expression->resolved_type->kind != ResolvedType::ARRAY) return LS_IR_INVALID_VALUE;
	IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(expression);
	ArrayResolvedType& array = *static_cast<ArrayResolvedType*>(expression->resolved_type);
	if (!identifier.slot || identifier.slot->storage == StorageSlot::GLOBAL) return LS_IR_INVALID_VALUE;
	LsOpMakeSlice* op = builder.append<LsOpMakeSlice>();
	if (!op) return LS_IR_INVALID_VALUE;
	op->type = target_type;
	op->result = builder.newValue();
	op->base_offset = builder.localOffset(identifier.slot, expression->resolved_type);
	op->length = (u64)array.size;
	return op->result;
}

static LsIrValue buildCall(IrBuilder& builder, CallExpression& expression) {
	FunctionExpression* target = expression.resolved_fn;
	if (!target && expression.callee && expression.callee->kind == Expression::IDENTIFIER) {
		IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(expression.callee);
		target = identifier.resolved_fn;
		if (!target && identifier.symbol && identifier.symbol->expression && identifier.symbol->expression->kind == Expression::FUNCTION)
			target = static_cast<FunctionExpression*>(identifier.symbol->expression);
	}
	if (!target && expression.callee && expression.callee->kind == Expression::MEMBER) {
		MemberExpression& member = *static_cast<MemberExpression*>(expression.callee);
		target = member.resolved_fn;
		if (!target && member.resolved_symbol && member.resolved_symbol->expression && member.resolved_symbol->expression->kind == Expression::FUNCTION)
			target = static_cast<FunctionExpression*>(member.resolved_symbol->expression);
	}
	if (!target) {
		if (!expression.callee || !expression.callee->resolved_type || expression.callee->resolved_type->kind != ResolvedType::FUNCTION) return LS_IR_INVALID_VALUE;
		FunctionResolvedType& function_type = *static_cast<FunctionResolvedType*>(expression.callee->resolved_type);
		LsIrValue function_value = buildExpression(builder, expression.callee);
		if (function_value == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
		const u32 argument_count = (u32)expression.args.size();
		LsIrValue* arguments = argument_count ? (LsIrValue*)builder.arena.allocate(builder.arena.user_data, sizeof(LsIrValue) * argument_count, alignof(LsIrValue)) : nullptr;
		u32* argument_sizes = argument_count ? (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * argument_count, alignof(u32)) : nullptr;
		if (argument_count && (!arguments || !argument_sizes)) return LS_IR_INVALID_VALUE;
		u32 parameter_index = 0, argument_size = 0;
		for (u32 i = 0; i < argument_count; ++i) {
			while (parameter_index < (u32)function_type.params.size() && function_type.params[(i32)parameter_index].is_comptime) ++parameter_index;
			if (parameter_index >= (u32)function_type.params.size()) return LS_IR_INVALID_VALUE;
			ResolvedType* expected = function_type.params[(i32)parameter_index].type;
			arguments[i] = buildExpressionAsType(builder, expression.args[(i32)i], expected);
			if (arguments[i] == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
			argument_sizes[i] = typeRegisterSize(expected);
			argument_size += argument_sizes[i];
			++parameter_index;
		}
		LsOpCallIndirect* call = builder.append<LsOpCallIndirect>();
		if (!call) return LS_IR_INVALID_VALUE;
		call->type = expression.resolved_type;
		call->result = builder.newValue();
		call->function = function_value;
		call->arguments = arguments;
		call->argument_sizes = argument_sizes;
		call->argument_count = argument_count;
		call->argument_size = argument_size;
		call->result_size = expression.resolved_type ? typeRegisterSize(expression.resolved_type) : 0;
		return call->result;
	}
	Expression* receiver = nullptr;
	if (expression.resolved_fn && expression.callee && expression.callee->kind == Expression::MEMBER) {
		MemberExpression& member = *static_cast<MemberExpression*>(expression.callee);
		if (member.expression && member.expression->resolved_type &&
			(member.expression->resolved_type->kind == ResolvedType::STRUCT || member.expression->resolved_type->kind == ResolvedType::POINTER))
			receiver = member.expression;
	}
	FunctionResolvedType* target_type = target->resolved_type && target->resolved_type->kind == ResolvedType::FUNCTION
		? static_cast<FunctionResolvedType*>(target->resolved_type) : nullptr;
	u32 argument_count = receiver ? 1u : 0u;
	for (Expression* argument : expression.args)
		if (argument->kind != Expression::TYPE_LITERAL && (!argument->resolved_type || argument->resolved_type->kind != ResolvedType::META)) ++argument_count;
	LsIrValue* arguments = nullptr;
	u32* argument_sizes = nullptr;
	Expression** runtime_expressions = nullptr;
	if (argument_count) {
		arguments = (LsIrValue*)builder.arena.allocate(builder.arena.user_data, sizeof(LsIrValue) * argument_count, alignof(LsIrValue));
		argument_sizes = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * argument_count, alignof(u32));
		runtime_expressions = (Expression**)builder.arena.allocate(builder.arena.user_data, sizeof(Expression*) * argument_count, alignof(Expression*));
		if (!arguments || !argument_sizes || !runtime_expressions) return LS_IR_INVALID_VALUE;
	}
	u32 runtime_index = 0;
	if (receiver) runtime_expressions[runtime_index++] = receiver;
	for (Expression* argument : expression.args)
		if (argument->kind != Expression::TYPE_LITERAL && (!argument->resolved_type || argument->resolved_type->kind != ResolvedType::META)) runtime_expressions[runtime_index++] = argument;
	u32 argument_size = 0;
	u32 parameter_index = 0u;
	for (u32 i = 0; i < argument_count; ++i) {
		Expression* argument = runtime_expressions[i];
		ResolvedType* expected = nullptr;
		if (target->resolved_type && target->resolved_type->kind == ResolvedType::FUNCTION) {
			FunctionResolvedType& function_type = *static_cast<FunctionResolvedType*>(target->resolved_type);
			while (parameter_index < (u32)function_type.params.size() && function_type.params[(i32)parameter_index].is_comptime) ++parameter_index;
			if (parameter_index < (u32)function_type.params.size()) expected = function_type.params[(i32)parameter_index].type;
		}
		if (expected && expected->kind == ResolvedType::META && argument->resolved_type && argument->resolved_type->kind != ResolvedType::META)
			expected = argument->resolved_type;
		arguments[i] = expected ? buildExpressionAsType(builder, argument, expected) : buildExpression(builder, argument);
		if (arguments[i] == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
		argument_sizes[i] = expected ? typeRegisterSize(expected) : (argument->resolved_type ? typeRegisterSize(argument->resolved_type) : 0);
		argument_size += argument_sizes[i];
		++parameter_index;
	}
	LsOpCallDirect* call = target->is_extern
		? static_cast<LsOpCallDirect*>(builder.append<LsOpCallNative>())
		: builder.append<LsOpCallDirect>();
	if (!call) return LS_IR_INVALID_VALUE;
	call->type = expression.resolved_type;
	call->result = builder.newValue();
	// Function indices are assigned by module assembly. A standalone function
	// uses index zero until that assembly layer supplies the final index.
	call->function = target->bytecode_index == ~0u ? 0u : target->bytecode_index;
	call->arguments = arguments;
	call->argument_sizes = argument_sizes;
	call->argument_count = argument_count;
	call->argument_size = argument_size;
	call->result_size = expression.resolved_type ? typeRegisterSize(expression.resolved_type) : 0;
	return call->result;
}

static LsIrValue buildOperatorCall(IrBuilder& builder, FunctionExpression* target, Expression** expressions, u32 count, ResolvedType* result_type) {
	if (!target || !target->resolved_type || target->resolved_type->kind != ResolvedType::FUNCTION) return LS_IR_INVALID_VALUE;
	LsIrValue* arguments = count ? (LsIrValue*)builder.arena.allocate(builder.arena.user_data, sizeof(LsIrValue) * count, alignof(LsIrValue)) : nullptr;
	u32* argument_sizes = count ? (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * count, alignof(u32)) : nullptr;
	if (count && (!arguments || !argument_sizes)) return LS_IR_INVALID_VALUE;
	FunctionResolvedType& function_type = *static_cast<FunctionResolvedType*>(target->resolved_type);
	u32 parameter_index = 0;
	u32 argument_size = 0;
	for (u32 i = 0; i < count; ++i) {
		while (parameter_index < (u32)function_type.params.size() && function_type.params[(i32)parameter_index].is_comptime) ++parameter_index;
		if (parameter_index >= (u32)function_type.params.size()) return LS_IR_INVALID_VALUE;
		ResolvedType* expected = function_type.params[(i32)parameter_index].type;
		arguments[i] = buildExpressionAsType(builder, expressions[i], expected);
		if (arguments[i] == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
		argument_sizes[i] = expected ? typeRegisterSize(expected) : typeRegisterSize(expressions[i]->resolved_type);
		argument_size += argument_sizes[i];
		++parameter_index;
	}
	LsOpCallDirect* call = target->is_extern ? static_cast<LsOpCallDirect*>(builder.append<LsOpCallNative>()) : builder.append<LsOpCallDirect>();
	if (!call) return LS_IR_INVALID_VALUE;
	call->type = result_type;
	call->result = builder.newValue();
	call->function = target->bytecode_index == ~0u ? 0u : target->bytecode_index;
	call->arguments = arguments;
	call->argument_sizes = argument_sizes;
	call->argument_count = count;
	call->argument_size = argument_size;
	call->result_size = result_type ? typeByteSize(*result_type) : 0;
	return call->result;
}

static LsIrValue buildExpression(IrBuilder& builder, Expression* expression) {
	if (!expression) return LS_IR_INVALID_VALUE;
		switch (expression->kind) {
		case Expression::INT_LITERAL:
		case Expression::FLOAT_LITERAL:
		case Expression::BOOL_LITERAL:
		case Expression::STRING_LITERAL:
		case Expression::NULL_LITERAL:
		case Expression::UNDEFINED:
			return buildLiteral(builder, *expression);
		case Expression::SIZEOF: {
			SizeofExpression& size = *static_cast<SizeofExpression*>(expression);
			LsOpLoadConst* value = builder.append<LsOpLoadConst>();
			if (!value) return LS_IR_INVALID_VALUE;
			value->type = expression->resolved_type;
			value->result = builder.newValue();
			value->value = size.value;
			return value->result;
		}
		case Expression::IDENTIFIER:
			return buildIdentifier(builder, *static_cast<IdentifierExpression*>(expression));
		case Expression::UNARY:
			return buildUnary(builder, *static_cast<UnaryExpression*>(expression));
		case Expression::ADDRESSOF: {
			AddressOfExpression& address = *static_cast<AddressOfExpression*>(expression);
			if (address.subject && address.subject->kind == Expression::MEMBER) {
				MemberExpression& member = *static_cast<MemberExpression*>(address.subject);
				if (!member.expression || member.expression->kind != Expression::IDENTIFIER || !member.expression->resolved_type ||
					member.expression->resolved_type->kind != ResolvedType::STRUCT) return LS_IR_INVALID_VALUE;
				IdentifierExpression& aggregate = *static_cast<IdentifierExpression*>(member.expression);
				ResolvedType* field_type = nullptr;
				const u32 field_offset = structFieldOffset(*static_cast<StructResolvedType*>(member.expression->resolved_type), member.name, field_type);
				if (field_offset == LS_IR_INVALID_VALUE || !aggregate.slot) return LS_IR_INVALID_VALUE;
				if (aggregate.slot->storage == StorageSlot::GLOBAL) {
					LsOpGlobalRef* reference = builder.append<LsOpGlobalRef>();
					if (!reference) return LS_IR_INVALID_VALUE;
					reference->type = expression->resolved_type; reference->result = builder.newValue();
					reference->offset = aggregate.slot->offset + field_offset;
					return reference->result;
				}
				LsOpLocalRef* reference = builder.append<LsOpLocalRef>();
				if (!reference) return LS_IR_INVALID_VALUE;
				reference->type = expression->resolved_type; reference->result = builder.newValue();
				reference->offset = builder.localOffset(aggregate.slot, member.expression->resolved_type) + field_offset;
				return reference->result;
			}
			if (!address.subject || address.subject->kind != Expression::IDENTIFIER) return LS_IR_INVALID_VALUE;
			IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(address.subject);
			if (!identifier.slot || identifier.slot->storage == StorageSlot::GLOBAL) return LS_IR_INVALID_VALUE;
			LsOpLocalRef* reference = builder.append<LsOpLocalRef>();
			if (!reference) return LS_IR_INVALID_VALUE;
			reference->type = expression->resolved_type;
			reference->result = builder.newValue();
			reference->offset = builder.localOffset(identifier.slot, address.subject->resolved_type);
			return reference->result;
		}
		case Expression::DEREFERENCE: {
			DereferenceExpression& dereference = *static_cast<DereferenceExpression*>(expression);
			LsIrValue pointer = buildExpression(builder, dereference.subject);
			if (pointer == LS_IR_INVALID_VALUE || !expression->resolved_type) return LS_IR_INVALID_VALUE;
			LsOpLoadConst* zero = builder.append<LsOpLoadConst>();
			LsOpLoadIndexed* load = zero ? builder.append<LsOpLoadIndexed>() : nullptr;
			if (!zero || !load) return LS_IR_INVALID_VALUE;
			static ResolvedType isize_type(ResolvedType::ISIZE);
			zero->type = &isize_type;
			zero->result = builder.newValue();
			zero->value = 0;
			load->type = expression->resolved_type;
			load->result = builder.newValue();
			load->base = pointer;
			load->index = zero->result;
			load->scale = 1;
			load->offset = 0;
			if (dereference.subject->kind == Expression::IDENTIFIER) {
				IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(dereference.subject);
				if (identifier.slot && identifier.slot->type && identifier.slot->type->kind == ResolvedType::POINTER) {
					PointerResolvedType& pointer = *static_cast<PointerResolvedType*>(identifier.slot->type);
					if (pointer.inner && pointer.inner->kind == ResolvedType::UNION && expression->resolved_type->kind != ResolvedType::UNION) load->offset = sizeof(i32);
				}
			}
			load->length = 1;
			load->base_is_value = true;
			return load->result;
		}
		case Expression::BINARY:
			return buildBinary(builder, *static_cast<BinaryExpression*>(expression));
		case Expression::CALL:
			return buildCall(builder, *static_cast<CallExpression*>(expression));
		case Expression::MEMBER:
			return buildMember(builder, *static_cast<MemberExpression*>(expression));
		case Expression::TYPE_MEMBER:
			return buildTypeMember(builder, *static_cast<TypeMemberExpression*>(expression));
		case Expression::TYPEOF:
			// typeof produces a compile-time type and has no runtime representation.
			return LS_IR_INVALID_VALUE;
		case Expression::STRUCT_LITERAL:
			return buildStructLiteral(builder, *static_cast<StructLiteralExpression*>(expression));
		case Expression::ARRAY_LITERAL:
			return buildArrayLiteral(builder, *static_cast<ArrayLiteralExpression*>(expression));
		case Expression::CAST:
			return buildCast(builder, *static_cast<CastExpression*>(expression));
		case Expression::TERNARY:
			return buildTernary(builder, *static_cast<TernaryExpression*>(expression));
		case Expression::BRACKET:
			if (!empty(static_cast<BracketExpression*>(expression)->struct_field_name)) {
				BracketExpression& bracket = *static_cast<BracketExpression*>(expression);
				MemberExpression member;
				member.expression = bracket.base;
				member.name = bracket.struct_field_name;
				member.resolved_type = bracket.resolved_type;
				return buildMember(builder, member);
			}
			if (expression->kind == Expression::BRACKET && static_cast<BracketExpression*>(expression)->base && static_cast<BracketExpression*>(expression)->base->resolved_type && static_cast<BracketExpression*>(expression)->base->resolved_type->kind == ResolvedType::SLICE) {
				BracketExpression& bracket = *static_cast<BracketExpression*>(expression);
				LsIrValue index = buildExpression(builder, bracket.args[0]);
				SliceResolvedType* slice = static_cast<SliceResolvedType*>(bracket.base->resolved_type);
				if (index == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
				u32 slice_offset = LS_IR_INVALID_VALUE;
				if (bracket.base->kind == Expression::IDENTIFIER) {
					IdentifierExpression& base = *static_cast<IdentifierExpression*>(bracket.base);
					slice_offset = builder.localOffset(base.slot, base.resolved_type);
				} else {
					LsIrValue slice_value = buildExpression(builder, bracket.base);
					if (slice_value == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
					slice_offset = builder.temporaryOffset(bracket.base->resolved_type);
					LsOpLocalStore* store = builder.append<LsOpLocalStore>();
					if (!store) return LS_IR_INVALID_VALUE;
					store->type = bracket.base->resolved_type;
					store->source = slice_value;
					store->offset = slice_offset;
				}
				LsOpSliceLoadLocalI32* load = builder.append<LsOpSliceLoadLocalI32>();
				if (!load) return LS_IR_INVALID_VALUE;
				load->type = expression->resolved_type;
				load->result = builder.newValue();
				load->slice_offset = slice_offset;
				load->index = index;
				load->element_size = slice->element_type ? typeByteSize(*slice->element_type) : 0;
				return load->result;
			}
			return buildBracket(builder, *static_cast<BracketExpression*>(expression));
		case Expression::SLICE:
			return buildSliceExpression(builder, *static_cast<SliceExpression*>(expression));
		default:
			builder.failed = true;
			return LS_IR_INVALID_VALUE;
	}
}

static void buildStatement(IrBuilder& builder, Statement* statement, ls_string_view pending_label) {
	if (!statement || builder.block->terminator) return;
	builder.source_loc = statement->token.line > 0 ? (LsIrSourceLoc)statement->token.line : LS_IR_INVALID_SOURCE_LOC;
	switch (statement->kind) {
		case Statement::BLOCK: {
			BlockStatement& block = *static_cast<BlockStatement*>(statement);
			const u32 mark = (u32)builder.defers.size();
			builder.scope_marks.push_back(mark);
			for (Statement* child : block.statements) {
				if (builder.block->terminator) break;
				buildStatement(builder, child);
			}
			if (!builder.block->terminator) emitDefers(builder, mark);
			builder.defers.resize((i32)mark);
			builder.scope_marks.pop_back();
			break;
		}
		case Statement::EXPRESSION:
			{
				Expression* expression = static_cast<ExpressionStatement*>(statement)->expression;
				bool compiletime_call = false;
				if (expression && expression->kind == Expression::CALL) {
					CallExpression& call = *static_cast<CallExpression*>(expression);
					compiletime_call = call.resolved_fn && call.resolved_fn->params.size() > 0 && call.resolved_fn->params[0].is_comptime;
					if (compiletime_call) for (Expression* arg : call.args) if (arg && arg->eval_stage == Expression::RUNTIME) { compiletime_call = false; break; }
				}
				if (expression->eval_stage != Expression::RUNTIME || compiletime_call) break;
				(void)buildExpression(builder, expression);
			}
			break;
		case Statement::RETURN: {
			ReturnStatement& return_statement = *static_cast<ReturnStatement*>(statement);
			if (return_statement.expression) {
				LsIrValue value = LS_IR_INVALID_VALUE;
				if (return_statement.expression->kind == Expression::IDENTIFIER && builder.function.return_type && return_statement.expression->resolved_type &&
					(return_statement.expression->resolved_type->kind == ResolvedType::UNTYPED_INT || return_statement.expression->resolved_type->kind == ResolvedType::UNTYPED_FLOAT ||
					 return_statement.expression->resolved_type->kind == ResolvedType::F32 || return_statement.expression->resolved_type->kind == ResolvedType::F64))
					value = buildComptimeIdentifierAsType(builder, *static_cast<IdentifierExpression*>(return_statement.expression), builder.function.return_type);
				if (value == LS_IR_INVALID_VALUE) value = buildExpressionAsType(builder, return_statement.expression, builder.function.return_type);
				if (value == LS_IR_INVALID_VALUE) return;
				emitDefers(builder, 0);
				LsOpReturn* op = builder.terminate<LsOpReturn>();
				if (!op) return;
				op->value = value;
				// The expression may still have an untyped compile-time type. The
				// returned value, however, is stored using the function signature.
				op->result_size = builder.function.return_type ? typeByteSize(*builder.function.return_type) : 0;
			} else {
				emitDefers(builder, 0);
				(void)builder.terminate<LsOpReturn>();
			}
			break;
		}
		case Statement::VAR_DECL: {
			VarDeclStatement& declaration = *static_cast<VarDeclStatement*>(statement);
			if (!declaration.expression || declaration.is_comptime) break;
			if (declaration.else_return && declaration.expression->resolved_type && declaration.expression->resolved_type->kind == ResolvedType::UNION &&
				declaration.resolved_type) {
				UnionResolvedType& source_type = *static_cast<UnionResolvedType*>(declaration.expression->resolved_type);
				LsIrValue source = buildExpression(builder, declaration.expression);
				if (source == LS_IR_INVALID_VALUE) return;
				i32 selected_members[64] = {};
				u32 selected_count = 0;
				for (i32 i = 0; i < source_type.members.size() && i < 64; ++i)
					if (declaration.else_return_target_mask & (1ull << (u32)i)) selected_members[selected_count++] = i;
				if (!selected_count) return;
				LsIrBlock checks[64] = {};
				for (u32 i = 1; i < selected_count; ++i) checks[i] = builder.newBlock();
				const LsIrBlock failure = builder.newBlock();
				LsIrBlock success[64] = {};
				for (u32 i = 0; i < selected_count; ++i) success[i] = builder.newBlock();
				const LsIrBlock success_merge = builder.newBlock();
				static ResolvedType i32_type(ResolvedType::I32);
				for (u32 i = 0; i < selected_count; ++i) {
					if (i) builder.selectBlock(checks[i]);
					LsOpCopy* tag = builder.append<LsOpCopy>();
					LsOpLoadConst* expected = tag ? builder.append<LsOpLoadConst>() : nullptr;
					LsOpCompare* compare = expected ? builder.append<LsOpCompare>() : nullptr;
					LsOpConditionalJump* branch = compare ? builder.terminate<LsOpConditionalJump>() : nullptr;
					if (!tag || !expected || !compare || !branch) return;
					tag->type = &i32_type; tag->result = builder.newValue(); tag->source = source; tag->source_offset = 0;
					expected->type = &i32_type; expected->result = builder.newValue(); expected->value = (u32)selected_members[i];
					compare->type = &i32_type; compare->result = builder.newValue(); compare->lhs = tag->result; compare->rhs = expected->result; compare->op = LS_IR_COMPARE_EQ;
					branch->condition = compare->result; branch->target = success[i];
				}
				builder.selectBlock(failure);
				i32 return_member_index = -1;
				if (builder.function.return_type) for (i32 i = 0; i < source_type.members.size(); ++i) {
					ResolvedType* member = source_type.members[i];
					if (member->kind == builder.function.return_type->kind &&
						(member->kind != ResolvedType::STRUCT || static_cast<StructResolvedType*>(member)->decl == static_cast<StructResolvedType*>(builder.function.return_type)->decl)) {
						return_member_index = i;
						break;
					}
				}
				if (return_member_index >= 0) {
					LsOpCopy* residual = builder.append<LsOpCopy>();
					if (!residual) return;
					residual->type = builder.function.return_type; residual->result = builder.newValue(); residual->source = source; residual->source_offset = sizeof(i32);
					emitDefers(builder, 0);
					LsOpReturn* ret = builder.terminate<LsOpReturn>();
					if (!ret) return;
					ret->value = residual->result; ret->result_size = typeByteSize(*builder.function.return_type);
				} else if (builder.function.return_type && builder.function.return_type->kind == ResolvedType::UNION) {
					UnionResolvedType& return_union = *static_cast<UnionResolvedType*>(builder.function.return_type);
					LsIrBlock next = failure;
					for (i32 source_index = 0; source_index < source_type.members.size(); ++source_index) {
						ResolvedType* source_member = source_type.members[source_index];
						i32 target_index = -1;
						for (i32 i = 0; i < return_union.members.size(); ++i) {
							ResolvedType* target_member = return_union.members[i];
							if (target_member->kind == source_member->kind && (target_member->kind != ResolvedType::STRUCT ||
								static_cast<StructResolvedType*>(target_member)->decl == static_cast<StructResolvedType*>(source_member)->decl)) { target_index = i; break; }
						}
						if (target_index < 0) continue;
						builder.selectBlock(next);
						static ResolvedType i32_type(ResolvedType::I32);
						LsOpCopy* tag = builder.append<LsOpCopy>();
						LsOpLoadConst* expected = tag ? builder.append<LsOpLoadConst>() : nullptr;
						LsOpCompare* compare = expected ? builder.append<LsOpCompare>() : nullptr;
						LsIrBlock payload_block = builder.newBlock();
						LsIrBlock next_block = builder.newBlock();
						LsOpConditionalJump* branch = compare ? builder.terminate<LsOpConditionalJump>() : nullptr;
						if (!tag || !expected || !compare || !branch) return;
						tag->type = &i32_type; tag->result = builder.newValue(); tag->source = source; tag->source_offset = 0;
						expected->type = &i32_type; expected->result = builder.newValue(); expected->value = (u32)source_index;
						compare->type = &i32_type; compare->result = builder.newValue(); compare->lhs = tag->result; compare->rhs = expected->result; compare->op = LS_IR_COMPARE_EQ;
						branch->condition = compare->result; branch->target = payload_block;
						builder.selectBlock(payload_block);
						LsOpLoadConst* target_tag = builder.append<LsOpLoadConst>();
						LsOpCopy* payload = target_tag ? builder.append<LsOpCopy>() : nullptr;
						LsOpAggregateInit* aggregate = payload ? builder.append<LsOpAggregateInit>() : nullptr;
						LsOpReturn* ret = aggregate ? builder.terminate<LsOpReturn>() : nullptr;
						if (!target_tag || !payload || !aggregate || !ret) return;
						target_tag->type = &i32_type; target_tag->result = builder.newValue(); target_tag->value = (u32)target_index;
						payload->type = source_member; payload->result = builder.newValue(); payload->source = source; payload->source_offset = sizeof(i32);
						LsIrValue* values = (LsIrValue*)builder.arena.allocate(builder.arena.user_data, sizeof(LsIrValue) * 2, alignof(LsIrValue));
						u32* offsets = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * 2, alignof(u32));
						u32* sizes = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * 2, alignof(u32));
						if (!values || !offsets || !sizes) return;
						values[0] = target_tag->result; values[1] = payload->result; offsets[0] = 0; offsets[1] = sizeof(i32); sizes[0] = sizeof(i32); sizes[1] = typeByteSize(*source_member);
						aggregate->type = builder.function.return_type; aggregate->result = builder.newValue(); aggregate->values = values; aggregate->offsets = offsets; aggregate->sizes = sizes; aggregate->value_count = 2;
						emitDefers(builder, 0);
						ret->value = aggregate->result; ret->result_size = typeByteSize(*builder.function.return_type);
						next = next_block;
					}
					builder.selectBlock(next);
					if (!declaration.else_return_zero) {
						emitDefers(builder, 0);
						if (!builder.terminate<LsOpReturn>()) return;
					} else {
						LsOpLoadConst* zero = builder.append<LsOpLoadConst>();
						LsOpReturn* ret = zero ? builder.terminate<LsOpReturn>() : nullptr;
						if (!zero || !ret) return;
						zero->type = builder.function.return_type; zero->result = builder.newValue(); zero->value = 0; ret->value = zero->result; ret->result_size = typeByteSize(*builder.function.return_type);
					}
				} else if (declaration.else_return_zero) {
					LsOpLoadConst* zero = builder.append<LsOpLoadConst>();
					if (!zero) return;
					zero->type = builder.function.return_type; zero->result = builder.newValue(); zero->value = 0;
					emitDefers(builder, 0);
					LsOpReturn* ret = builder.terminate<LsOpReturn>();
					if (!ret) return;
					ret->value = zero->result; ret->result_size = builder.function.return_type ? typeByteSize(*builder.function.return_type) : 0;
				} else {
					emitDefers(builder, 0);
					if (!builder.terminate<LsOpReturn>()) return;
				}
				for (u32 i = 0; i < selected_count; ++i) {
					builder.selectBlock(success[i]);
					ResolvedType* member = source_type.members[selected_members[i]];
					LsIrValue value = LS_IR_INVALID_VALUE;
					if (declaration.resolved_type->kind == ResolvedType::UNION) {
						UnionResolvedType& target_type = *static_cast<UnionResolvedType*>(declaration.resolved_type);
						i32 target_index = -1;
						for (i32 j = 0; j < target_type.members.size(); ++j) {
							ResolvedType* target_member = target_type.members[j];
							if (member->kind == target_member->kind && (member->kind != ResolvedType::STRUCT ||
								static_cast<StructResolvedType*>(member)->decl == static_cast<StructResolvedType*>(target_member)->decl)) { target_index = j; break; }
						}
						LsOpLoadConst* tag = target_index >= 0 ? builder.append<LsOpLoadConst>() : nullptr;
						LsOpCopy* payload = tag ? builder.append<LsOpCopy>() : nullptr;
						LsOpAggregateInit* aggregate = payload ? builder.append<LsOpAggregateInit>() : nullptr;
						if (!tag || !payload || !aggregate) return;
						tag->type = &i32_type; tag->result = builder.newValue(); tag->value = (u32)target_index;
						payload->type = member; payload->result = builder.newValue(); payload->source = source; payload->source_offset = sizeof(i32);
						LsIrValue* values = (LsIrValue*)builder.arena.allocate(builder.arena.user_data, sizeof(LsIrValue) * 2, alignof(LsIrValue));
						u32* offsets = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * 2, alignof(u32));
						u32* sizes = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * 2, alignof(u32));
						if (!values || !offsets || !sizes) return;
						values[0] = tag->result; values[1] = payload->result; offsets[0] = 0; offsets[1] = sizeof(i32); sizes[0] = sizeof(i32); sizes[1] = typeByteSize(*member);
						aggregate->type = declaration.resolved_type; aggregate->result = builder.newValue(); aggregate->values = values; aggregate->offsets = offsets; aggregate->sizes = sizes; aggregate->value_count = 2;
						value = aggregate->result;
					} else {
						LsOpCopy* payload = builder.append<LsOpCopy>();
						if (!payload) return;
						payload->type = declaration.resolved_type; payload->result = builder.newValue(); payload->source = source; payload->source_offset = sizeof(i32);
						value = payload->result;
					}
					LsOpLocalStore* store = builder.append<LsOpLocalStore>();
					LsOpJump* leave = store ? builder.terminate<LsOpJump>() : nullptr;
					if (!store || !leave) return;
					store->type = declaration.resolved_type; store->source = value; store->offset = builder.localOffset(&declaration.slot, declaration.resolved_type);
					leave->target = success_merge;
				}
				builder.selectBlock(success_merge);
				break;
			}
			if (declaration.expression->kind == Expression::UNDEFINED) {
				if (declaration.slot.storage != StorageSlot::GLOBAL)
					(void)builder.localOffset(&declaration.slot, declaration.resolved_type);
				break;
			}
			if (declaration.expression->kind == Expression::ARRAY_LITERAL && declaration.resolved_type && declaration.resolved_type->kind == ResolvedType::ARRAY && declaration.slot.storage != StorageSlot::GLOBAL) {
				ArrayResolvedType* array = static_cast<ArrayResolvedType*>(declaration.resolved_type);
				const u32 base_offset = builder.localOffset(&declaration.slot, declaration.resolved_type);
				ArrayLiteralExpression& literal = *static_cast<ArrayLiteralExpression*>(declaration.expression);
				for (u32 i = 0; i < (u32)literal.values.size(); ++i) {
					LsIrValue value = buildExpression(builder, literal.values[(i32)i]);
					LsOpLoadConst* index = builder.append<LsOpLoadConst>();
					LsOpStoreIndexedLocalI32* store = builder.append<LsOpStoreIndexedLocalI32>();
					if (!index || !store || value == LS_IR_INVALID_VALUE) return;
					index->type = literal.values[(i32)i]->resolved_type;
					index->result = builder.newValue();
					index->value = i;
					store->type = array->element_type;
					store->base_offset = base_offset;
					store->index = index->result;
					store->source = value;
					store->scale = array->element_type ? typeByteSize(*array->element_type) : 0;
					store->length = (u32)array->size;
				}
				break;
			}
			const LsIrValue value = buildExpressionAsType(builder, declaration.expression, declaration.resolved_type);
			if (value == LS_IR_INVALID_VALUE) return;
			// Declarations carry their storage slot separately from an identifier node.
			if (declaration.slot.storage == StorageSlot::GLOBAL) {
				LsOpGlobalStore* store = builder.append<LsOpGlobalStore>();
				if (!store) return;
				store->type = declaration.resolved_type;
				store->source = value;
				store->offset = declaration.slot.offset;
			} else {
				LsOpLocalStore* store = builder.append<LsOpLocalStore>();
				if (!store) return;
				store->type = declaration.resolved_type;
				store->source = value;
				store->offset = builder.localOffset(&declaration.slot, declaration.resolved_type);
			}
			break;
		}
		case Statement::ASSIGN: {
			AssignStatement& assignment = *static_cast<AssignStatement*>(statement);
			ResolvedType* expected = nullptr;
			if (assignment.lhs && assignment.lhs->kind == Expression::MEMBER) {
				MemberExpression& member = *static_cast<MemberExpression*>(assignment.lhs);
				if (member.expression && member.expression->resolved_type && member.expression->resolved_type->kind == ResolvedType::STRUCT)
					(void)structFieldOffset(*static_cast<StructResolvedType*>(member.expression->resolved_type), member.name, expected);
			}
			const bool builtin_numeric_assignment = assignment.lhs && assignment.lhs->resolved_type &&
				(assignment.lhs->resolved_type->kind == ResolvedType::I8 || assignment.lhs->resolved_type->kind == ResolvedType::U8 ||
				 assignment.lhs->resolved_type->kind == ResolvedType::I16 || assignment.lhs->resolved_type->kind == ResolvedType::U16 ||
				 assignment.lhs->resolved_type->kind == ResolvedType::I32 || assignment.lhs->resolved_type->kind == ResolvedType::U32 ||
				 assignment.lhs->resolved_type->kind == ResolvedType::I64 || assignment.lhs->resolved_type->kind == ResolvedType::U64 ||
				 assignment.lhs->resolved_type->kind == ResolvedType::ISIZE || assignment.lhs->resolved_type->kind == ResolvedType::F32 ||
				 assignment.lhs->resolved_type->kind == ResolvedType::F64);
			if (assignment.op != Token::EQUAL && assignment.resolved_op_fn && !builtin_numeric_assignment) {
				LsIrValue old_value = buildExpression(builder, assignment.lhs);
				LsIrValue rhs_value = buildExpression(builder, assignment.rhs);
				if (old_value == LS_IR_INVALID_VALUE || rhs_value == LS_IR_INVALID_VALUE) return;
				// Reuse the already-built values through a small synthetic call path.
				FunctionResolvedType& function_type = *static_cast<FunctionResolvedType*>(assignment.resolved_op_fn->resolved_type);
				LsIrValue values[] = {old_value, rhs_value};
				u32 sizes[] = {typeByteSize(*function_type.params[0].type), typeByteSize(*function_type.params[1].type)};
				LsOpCallDirect* call = assignment.resolved_op_fn->is_extern ? static_cast<LsOpCallDirect*>(builder.append<LsOpCallNative>()) : builder.append<LsOpCallDirect>();
				if (!call) return;
				call->type = assignment.lhs->resolved_type;
				call->result = builder.newValue();
				call->function = assignment.resolved_op_fn->bytecode_index == ~0u ? 0u : assignment.resolved_op_fn->bytecode_index;
				call->arguments = (LsIrValue*)builder.arena.allocate(builder.arena.user_data, sizeof(values), alignof(LsIrValue));
				call->argument_sizes = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(sizes), alignof(u32));
				if (!call->arguments || !call->argument_sizes) return;
				copyMemory(call->arguments, values, sizeof(values));
				copyMemory(call->argument_sizes, sizes, sizeof(sizes));
				call->argument_count = 2;
				call->argument_size = sizes[0] + sizes[1];
				call->result_size = typeByteSize(*assignment.lhs->resolved_type);
				storeValue(builder, assignment.lhs, call->result);
				break;
			}
			if (!expected && assignment.lhs) expected = assignment.lhs->resolved_type;
			const LsIrValue value = expected ? buildExpressionAsType(builder, assignment.rhs, expected) : buildExpression(builder, assignment.rhs);
			if (value == LS_IR_INVALID_VALUE) return;
			if (assignment.op == Token::EQUAL) {
				storeValue(builder, assignment.lhs, value);
				break;
			}
			const LsIrValue old_value = buildExpression(builder, assignment.lhs);
			if (old_value == LS_IR_INVALID_VALUE) return;
			LsOpAdd* operation = nullptr;
			switch (assignment.op) {
				case Token::PLUS_EQUAL: operation = builder.append<LsOpAdd>(); break;
				case Token::MINUS_EQUAL: operation = builder.append<LsOpSub>(); break;
				case Token::STAR_EQUAL: operation = builder.append<LsOpMul>(); break;
				case Token::SLASH_EQUAL: operation = builder.append<LsOpDiv>(); break;
				default: break;
			}
			if (!operation) return;
			operation->type = assignment.lhs ? assignment.lhs->resolved_type : nullptr;
			if (!operation->type && assignment.lhs && assignment.lhs->kind == Expression::MEMBER) {
				MemberExpression& member = *static_cast<MemberExpression*>(assignment.lhs);
				if (member.expression && member.expression->resolved_type && member.expression->resolved_type->kind == ResolvedType::STRUCT) {
					ResolvedType* field_type = nullptr;
					(void)structFieldOffset(*static_cast<StructResolvedType*>(member.expression->resolved_type), member.name, field_type);
					operation->type = field_type;
				}
			}
			if (!operation->type && assignment.rhs) operation->type = assignment.rhs->resolved_type;
			operation->result = builder.newValue();
			operation->lhs = old_value;
			operation->rhs = value;
			storeValue(builder, assignment.lhs, operation->result);
			break;
		}
		case Statement::WHILE: {
			WhileStatement& loop = *static_cast<WhileStatement*>(statement);
			const LsIrBlock header = builder.newBlock();
			const LsIrBlock exit = builder.newBlock();
			const LsIrBlock body = builder.newBlock();
			LsOpJump* enter = builder.terminate<LsOpJump>();
			if (!enter) return;
			enter->target = header;
			builder.selectBlock(header);
			const LsIrValue condition = buildExpression(builder, loop.condition);
			if (condition == LS_IR_INVALID_VALUE) return;
			LsOpConditionalJump* branch = builder.terminate<LsOpConditionalJump>();
			if (!branch) return;
			branch->type = loop.condition ? loop.condition->resolved_type : nullptr;
			branch->condition = condition;
			branch->target = body;
			builder.selectBlock(body);
			IrBuilder::LoopTargets& targets = builder.loops.emplace_back();
			targets.label = pending_label;
			targets.continue_target = header;
			targets.break_target = exit;
			targets.defer_mark = (u32)builder.defers.size();
			buildStatement(builder, loop.body);
			builder.loops.pop_back();
			if (!builder.block->terminator) {
				LsOpJump* back = builder.terminate<LsOpJump>();
				if (back) back->target = header;
			}
			const LsIrBlock merge = builder.newBlock();
			builder.selectBlock(exit);
			LsOpJump* leave = builder.terminate<LsOpJump>();
			if (!leave) return;
			leave->target = merge;
			builder.selectBlock(merge);
			break;
		}
		case Statement::BREAK: {
			BreakStatement& break_statement = *static_cast<BreakStatement*>(statement);
			for (i32 i = (i32)builder.loops.size() - 1; i >= 0; --i) if (break_statement.label.begin == break_statement.label.end || equalStrings(builder.loops[(u32)i].label, break_statement.label)) {
				emitDefers(builder, builder.loops[(u32)i].defer_mark);
				LsOpJump* jump = builder.terminate<LsOpJump>();
				if (jump) jump->target = builder.loops[(u32)i].break_target;
				break;
			}
			break;
		}
		case Statement::CONTINUE: {
			ContinueStatement& continue_statement = *static_cast<ContinueStatement*>(statement);
			for (i32 i = (i32)builder.loops.size() - 1; i >= 0; --i) if (continue_statement.label.begin == continue_statement.label.end || equalStrings(builder.loops[(u32)i].label, continue_statement.label)) {
				emitDefers(builder, builder.loops[(u32)i].defer_mark);
				LsOpJump* jump = builder.terminate<LsOpJump>();
				if (jump) {
					jump->target = builder.loops[(u32)i].continue_target;
					if (jump->target == LS_IR_INVALID_BLOCK && builder.loops[(u32)i].continue_jump_count < 16)
						builder.loops[(u32)i].continue_jumps[builder.loops[(u32)i].continue_jump_count++] = jump;
				}
				break;
			}
			break;
		}
		case Statement::DEFER: {
			DeferStatement& defer = *static_cast<DeferStatement*>(statement);
			if (!defer.statement || (defer.statement->kind != Statement::EXPRESSION && defer.statement->kind != Statement::VAR_DECL &&
				defer.statement->kind != Statement::ASSIGN && defer.statement->kind != Statement::BLOCK)) {
				builder.failed = true;
				return;
			}
			builder.defers.push_back(defer.statement);
			break;
		}
		case Statement::LABEL: {
			LabelStatement& label = *static_cast<LabelStatement*>(statement);
			buildStatement(builder, label.statement, label.name);
			break;
		}
		case Statement::FOR: {
			ForStatement& loop = *static_cast<ForStatement*>(statement);
			const bool simple_unroll_range = loop.is_unroll && loop.end && loop.begin->kind == Expression::INT_LITERAL && loop.end->kind == Expression::INT_LITERAL;
			if (loop.is_expanded) {
				const LsIrBlock exit = builder.newBlock();
				const LsIrBlock body = builder.newBlock();
				LsOpJump* enter = builder.terminate<LsOpJump>();
				if (!enter) return;
				enter->target = body;
				builder.selectBlock(body);
				IrBuilder::LoopTargets& targets = builder.loops.emplace_back();
				targets.label = pending_label;
				targets.break_target = exit;
				targets.defer_mark = (u32)builder.defers.size();
				BlockStatement* expanded = loop.body && loop.body->kind == Statement::BLOCK
					? static_cast<BlockStatement*>(loop.body) : nullptr;
				if (!expanded) return;
				for (Statement* copy : expanded->statements) {
					const u32 continue_start = targets.continue_jump_count;
					buildStatement(builder, copy);
					if (builder.block->terminator) {
						if (targets.continue_jump_count == continue_start) break;
						LsIrBlock next = builder.newBlock();
						for (u32 i = continue_start; i < targets.continue_jump_count; ++i) targets.continue_jumps[i]->target = next;
						builder.selectBlock(next);
						continue;
					}
					LsIrBlock next = builder.newBlock();
					for (u32 i = continue_start; i < targets.continue_jump_count; ++i) targets.continue_jumps[i]->target = next;
					LsOpJump* jump = builder.terminate<LsOpJump>();
					if (!jump) return;
					jump->target = next;
					builder.selectBlock(next);
				}
				builder.loops.pop_back();
				const LsIrBlock continuation = builder.newBlock();
				if (builder.block->id != exit && !builder.block->terminator) {
					LsOpJump* fallthrough = builder.terminate<LsOpJump>();
					if (!fallthrough) return;
					fallthrough->target = continuation;
				}
				builder.selectBlock(exit);
				if (!builder.block->terminator) {
					LsOpJump* leave = builder.terminate<LsOpJump>();
					if (!leave) return;
					leave->target = continuation;
				}
				builder.selectBlock(continuation);
				break;
			}
			if (loop.is_unroll && loop.unroll_elements) {
				ResolvedType* value_type = loop.slot.type ? loop.slot.type : static_cast<SliceResolvedType*>(loop.begin->resolved_type)->element_type;
				const u32 value_offset = builder.localOffset(&loop.slot, value_type);
				loop.slot.storage = StorageSlot::LOCAL;
				const LsIrBlock exit = builder.newBlock();
				const LsIrBlock body = builder.newBlock();
				LsOpJump* enter = builder.terminate<LsOpJump>();
				if (!enter) return;
				enter->target = body;
				builder.selectBlock(body);
				IrBuilder::LoopTargets& targets = builder.loops.emplace_back();
				targets.label = pending_label;
				targets.break_target = exit;
				targets.defer_mark = (u32)builder.defers.size();
				for (i32 i = 0; i < loop.unroll_elements->values.size(); ++i) {
					const u32 continue_start = targets.continue_jump_count;
					LsIrValue value = buildExpressionAsType(builder, loop.unroll_elements->values[i], value_type);
					LsOpLocalStore* store = value == LS_IR_INVALID_VALUE ? nullptr : builder.append<LsOpLocalStore>();
					if (!store) return;
					store->type = value_type; store->source = value; store->offset = value_offset;
					if (loop.is_key_value) {
						static ResolvedType isize_type(ResolvedType::ISIZE);
						ResolvedType* index_type = loop.index_slot.type ? loop.index_slot.type : &isize_type;
						const u32 index_offset = builder.localOffset(&loop.index_slot, index_type);
						loop.index_slot.storage = StorageSlot::LOCAL;
						LsOpLoadConst* index = builder.append<LsOpLoadConst>();
						LsOpLocalStore* index_store = index ? builder.append<LsOpLocalStore>() : nullptr;
						if (!index_store) return;
						index->type = index_type; index->result = builder.newValue(); index->value = (u32)i;
						index_store->type = index_type; index_store->source = index->result; index_store->offset = index_offset;
					}
					buildStatement(builder, loop.body, pending_label);
					if (builder.block->terminator) {
						if (targets.continue_jump_count == continue_start) break;
						LsIrBlock next = builder.newBlock();
						for (u32 j = continue_start; j < targets.continue_jump_count; ++j) targets.continue_jumps[j]->target = next;
						builder.selectBlock(next);
						continue;
					}
					LsIrBlock next = builder.newBlock();
					for (u32 j = continue_start; j < targets.continue_jump_count; ++j) targets.continue_jumps[j]->target = next;
					LsOpJump* jump = builder.terminate<LsOpJump>();
					if (!jump) return;
					jump->target = next;
					builder.selectBlock(next);
				}
				builder.loops.pop_back();
				const LsIrBlock continuation = builder.newBlock();
				LsIrBlock current = builder.block->id;
				if (current != exit && !builder.block->terminator) {
					LsOpJump* fallthrough = builder.terminate<LsOpJump>();
					if (!fallthrough) return;
					fallthrough->target = continuation;
				}
				builder.selectBlock(exit);
				if (!builder.block->terminator) {
					LsOpJump* leave = builder.terminate<LsOpJump>();
					if (!leave) return;
					leave->target = continuation;
				}
				builder.selectBlock(continuation);
				break;
			}
			if (!loop.begin || (loop.is_unroll && !simple_unroll_range)) break;
			if (!loop.end && loop.begin->kind == Expression::IDENTIFIER && loop.begin->resolved_type && loop.begin->resolved_type->kind == ResolvedType::ARRAY) {
				IdentifierExpression& source = *static_cast<IdentifierExpression*>(loop.begin);
				ArrayResolvedType* array = static_cast<ArrayResolvedType*>(loop.begin->resolved_type);
				if (!source.slot || source.slot->storage == StorageSlot::GLOBAL || !array->element_type) break;
				const u32 base_offset = builder.localOffset(source.slot, source.resolved_type);
				ResolvedType* index_type = loop.index_slot.type ? loop.index_slot.type : array->element_type;
				const u32 index_offset = builder.localOffset(&loop.index_slot, index_type);
				const u32 value_offset = builder.localOffset(&loop.slot, array->element_type);
				LsOpLoadConst* zero = builder.append<LsOpLoadConst>();
				LsOpLocalStore* initial = zero ? builder.append<LsOpLocalStore>() : nullptr;
				if (!zero || !initial) return;
				zero->type = index_type;
				zero->result = builder.newValue();
				zero->value = 0;
				initial->type = index_type;
				initial->source = zero->result;
				initial->offset = index_offset;
				const LsIrBlock header = builder.newBlock();
				const LsIrBlock exit = builder.newBlock();
				const LsIrBlock body = builder.newBlock();
				LsOpJump* enter = builder.terminate<LsOpJump>();
				if (!enter) return;
				enter->target = header;
				builder.selectBlock(header);
				LsOpLocalLoad* index = builder.append<LsOpLocalLoad>();
				LsOpLoadConst* length = index ? builder.append<LsOpLoadConst>() : nullptr;
				LsOpCompare* condition = length ? builder.append<LsOpCompare>() : nullptr;
				if (!index || !length || !condition) return;
				index->type = index_type;
				index->result = builder.newValue();
				index->offset = index_offset;
				length->type = index_type;
				length->result = builder.newValue();
				length->value = (u32)array->size;
				condition->type = index_type;
				condition->result = builder.newValue();
				condition->lhs = index->result;
				condition->rhs = length->result;
				condition->op = LS_IR_COMPARE_LT;
				LsOpConditionalJump* branch = builder.terminate<LsOpConditionalJump>();
				if (!branch) return;
				branch->type = index_type;
				branch->condition = condition->result;
				branch->target = body;
				builder.selectBlock(body);
				LsOpLoadIndexedLocalI32* value = builder.append<LsOpLoadIndexedLocalI32>();
				LsOpLocalStore* value_store = value ? builder.append<LsOpLocalStore>() : nullptr;
				if (!value || !value_store) return;
				value->type = array->element_type;
				value->result = builder.newValue();
				value->base_offset = base_offset;
				value->index = index->result;
				value->scale = typeByteSize(*array->element_type);
				value->length = (u32)array->size;
				value_store->type = array->element_type;
				value_store->source = value->result;
				value_store->offset = value_offset;
				IrBuilder::LoopTargets& targets = builder.loops.emplace_back();
				targets.break_target = exit;
				targets.defer_mark = (u32)builder.defers.size();
				buildStatement(builder, loop.body);
				const LsIrBlock increment = builder.newBlock();
				for (u32 i = 0; i < targets.continue_jump_count; ++i) targets.continue_jumps[i]->target = increment;
				builder.loops.pop_back();
				if (!builder.block->terminator) {
					LsOpJump* next = builder.terminate<LsOpJump>();
					if (next) next->target = increment;
				}
				builder.selectBlock(increment);
				LsOpLocalLoad* current = builder.append<LsOpLocalLoad>();
				LsOpLoadConst* one = current ? builder.append<LsOpLoadConst>() : nullptr;
				LsOpAdd* add = one ? builder.append<LsOpAdd>() : nullptr;
				LsOpLocalStore* update = add ? builder.append<LsOpLocalStore>() : nullptr;
				LsOpJump* back = update ? builder.terminate<LsOpJump>() : nullptr;
				if (!current || !one || !add || !update || !back) return;
				current->type = index_type; current->result = builder.newValue(); current->offset = index_offset;
				one->type = index_type; one->result = builder.newValue(); one->value = 1;
				add->type = index_type; add->result = builder.newValue(); add->lhs = current->result; add->rhs = one->result;
				update->type = index_type; update->source = add->result; update->offset = index_offset;
				back->target = header;
				const LsIrBlock merge = builder.newBlock();
				builder.selectBlock(exit);
				LsOpJump* leave = builder.terminate<LsOpJump>();
				if (!leave) return;
				leave->target = merge;
				builder.selectBlock(merge);
				break;
			}
			if (!loop.end && loop.begin && loop.begin->resolved_type && loop.begin->resolved_type->kind == ResolvedType::SLICE) {
				SliceResolvedType& slice = *static_cast<SliceResolvedType*>(loop.begin->resolved_type);
				static ResolvedType isize_type(ResolvedType::ISIZE);
				ResolvedType* index_type = loop.index_slot.type ? loop.index_slot.type : &isize_type;
				if (!index_type || !slice.element_type) return;
				u32 slice_offset = LS_IR_INVALID_VALUE;
				if (loop.begin->kind == Expression::IDENTIFIER) {
					IdentifierExpression& source = *static_cast<IdentifierExpression*>(loop.begin);
					if (!source.slot) return;
					slice_offset = builder.localOffset(source.slot, loop.begin->resolved_type);
				} else {
					LsIrValue source = buildExpression(builder, loop.begin);
					if (source == LS_IR_INVALID_VALUE) return;
					slice_offset = builder.temporaryOffset(loop.begin->resolved_type);
					LsOpLocalStore* store = builder.append<LsOpLocalStore>();
					if (!store) return;
					store->type = loop.begin->resolved_type;
					store->source = source;
					store->offset = slice_offset;
				}
				const u32 index_offset = builder.localOffset(&loop.index_slot, index_type);
				const u32 value_offset = builder.localOffset(&loop.slot, slice.element_type);
				LsOpLoadConst* zero = builder.append<LsOpLoadConst>();
				LsOpLocalStore* initial = zero ? builder.append<LsOpLocalStore>() : nullptr;
				if (!zero || !initial) return;
				zero->type = index_type; zero->result = builder.newValue(); zero->value = 0;
				initial->type = index_type; initial->source = zero->result; initial->offset = index_offset;
				const LsIrBlock header = builder.newBlock();
				const LsIrBlock exit = builder.newBlock();
				const LsIrBlock body = builder.newBlock();
				LsOpJump* enter = builder.terminate<LsOpJump>();
				if (!enter) return;
				enter->target = header;
				builder.selectBlock(header);
				LsOpLocalLoad* index = builder.append<LsOpLocalLoad>();
				LsOpSliceLength* length = index ? builder.append<LsOpSliceLength>() : nullptr;
				LsOpCompare* condition = length ? builder.append<LsOpCompare>() : nullptr;
				if (!index || !length || !condition) return;
				index->type = index_type; index->result = builder.newValue(); index->offset = index_offset;
				length->result = builder.newValue(); length->slice_offset = slice_offset;
				condition->type = index_type; condition->result = builder.newValue(); condition->lhs = index->result; condition->rhs = length->result; condition->op = LS_IR_COMPARE_LT;
				LsOpConditionalJump* branch = builder.terminate<LsOpConditionalJump>();
				if (!branch) return;
				branch->type = index_type; branch->condition = condition->result; branch->target = body;
				builder.selectBlock(body);
				LsOpSliceLoadLocalI32* value = builder.append<LsOpSliceLoadLocalI32>();
				LsOpLocalStore* value_store = value ? builder.append<LsOpLocalStore>() : nullptr;
				if (!value || !value_store) return;
				value->type = slice.element_type; value->result = builder.newValue(); value->slice_offset = slice_offset; value->index = index->result; value->element_size = typeByteSize(*slice.element_type);
				value_store->type = slice.element_type; value_store->source = value->result; value_store->offset = value_offset;
				IrBuilder::LoopTargets& targets = builder.loops.emplace_back();
				targets.label = pending_label;
				targets.break_target = exit;
				targets.defer_mark = (u32)builder.defers.size();
				buildStatement(builder, loop.body);
				const LsIrBlock increment = builder.newBlock();
				for (u32 i = 0; i < targets.continue_jump_count; ++i) targets.continue_jumps[i]->target = increment;
				builder.loops.pop_back();
				if (!builder.block->terminator) {
					LsOpJump* next = builder.terminate<LsOpJump>();
					if (next) next->target = increment;
				}
				builder.selectBlock(increment);
				LsOpLocalLoad* current = builder.append<LsOpLocalLoad>();
				LsOpLoadConst* one = current ? builder.append<LsOpLoadConst>() : nullptr;
				LsOpAdd* add = one ? builder.append<LsOpAdd>() : nullptr;
				LsOpLocalStore* update = add ? builder.append<LsOpLocalStore>() : nullptr;
				LsOpJump* back = update ? builder.terminate<LsOpJump>() : nullptr;
				if (!current || !one || !add || !update || !back) return;
				current->type = index_type; current->result = builder.newValue(); current->offset = index_offset;
				one->type = index_type; one->result = builder.newValue(); one->value = 1;
				add->type = index_type; add->result = builder.newValue(); add->lhs = current->result; add->rhs = one->result;
				update->type = index_type; update->source = add->result; update->offset = index_offset;
				back->target = header;
				const LsIrBlock merge = builder.newBlock();
				builder.selectBlock(exit);
				LsOpJump* leave = builder.terminate<LsOpJump>();
				if (!leave) return;
				leave->target = merge;
				builder.selectBlock(merge);
				break;
			}
			if (!loop.end) break;
			ResolvedType* type = loop.slot.type ? loop.slot.type : loop.begin->resolved_type;
			if (!type) return;
			const u32 local_offset = builder.localOffset(&loop.slot, type);
			const LsIrValue begin = buildExpression(builder, loop.begin);
			const LsIrValue end = buildExpression(builder, loop.end);
			if (begin == LS_IR_INVALID_VALUE || end == LS_IR_INVALID_VALUE) return;
			LsOpLocalStore* initial = builder.append<LsOpLocalStore>();
			if (!initial) return;
			initial->type = type;
			initial->source = begin;
			initial->offset = local_offset;
			const LsIrBlock header = builder.newBlock();
			const LsIrBlock exit = builder.newBlock();
			const LsIrBlock body = builder.newBlock();
			LsOpJump* enter = builder.terminate<LsOpJump>();
			if (!enter) return;
			enter->target = header;
			builder.selectBlock(header);
			LsOpLocalLoad* current = builder.append<LsOpLocalLoad>();
			if (!current) return;
			current->type = type;
			current->result = builder.newValue();
			current->offset = local_offset;
			LsOpCompare* condition = builder.append<LsOpCompare>();
			if (!condition) return;
			condition->type = type;
			condition->result = builder.newValue();
			condition->lhs = current->result;
			condition->rhs = end;
			condition->op = LS_IR_COMPARE_LT;
			LsOpConditionalJump* branch = builder.terminate<LsOpConditionalJump>();
			if (!branch) return;
			branch->type = type;
			branch->condition = condition->result;
			branch->target = body;
			builder.selectBlock(body);
			IrBuilder::LoopTargets& targets = builder.loops.emplace_back();
			targets.label = pending_label;
			targets.break_target = exit;
			targets.defer_mark = (u32)builder.defers.size();
			buildStatement(builder, loop.body);
			const LsIrBlock increment = builder.newBlock();
			for (u32 i = 0; i < targets.continue_jump_count; ++i) targets.continue_jumps[i]->target = increment;
			builder.loops.pop_back();
			if (!builder.block->terminator) {
				LsOpJump* next = builder.terminate<LsOpJump>();
				if (next) next->target = increment;
			}
			builder.selectBlock(increment);
			LsOpLocalLoad* value = builder.append<LsOpLocalLoad>();
			if (!value) return;
			value->type = type;
			value->result = builder.newValue();
			value->offset = local_offset;
			LsOpLoadConst* one = builder.append<LsOpLoadConst>();
			if (!one) return;
			one->type = type;
			one->result = builder.newValue();
			one->value = 1;
			LsOpAdd* add = builder.append<LsOpAdd>();
			if (!add) return;
			add->type = type;
			add->result = builder.newValue();
			add->lhs = value->result;
			add->rhs = one->result;
			LsOpLocalStore* update = builder.append<LsOpLocalStore>();
			if (!update) return;
			update->type = type;
			update->source = add->result;
			update->offset = local_offset;
			LsOpJump* back = builder.terminate<LsOpJump>();
			if (!back) return;
			back->target = header;
			const LsIrBlock merge = builder.newBlock();
			builder.selectBlock(exit);
			LsOpJump* leave = builder.terminate<LsOpJump>();
			if (!leave) return;
			leave->target = merge;
			builder.selectBlock(merge);
			break;
		}
		case Statement::IF: {
			IfStatement& conditional = *static_cast<IfStatement*>(statement);
			if (conditional.comptime_known) {
				bool value = conditional.comptime_value;
				if (conditional.condition && conditional.condition->comptime_value.kind == ComptimeValue::VALUE && conditional.condition->comptime_value.type && conditional.condition->comptime_value.type->kind == ResolvedType::BOOL)
					copyMemory(&value, conditional.condition->comptime_value.value, sizeof(value));
				buildStatement(builder, value ? conditional.body : conditional.else_branch);
				break;
			}
			const LsIrValue condition = buildExpression(builder, conditional.condition);
			if (condition == LS_IR_INVALID_VALUE) return;
			const LsIrBlock else_block = builder.newBlock();
			LsOpConditionalJump* jump = builder.terminate<LsOpConditionalJump>();
			if (!jump) return;
			jump->type = conditional.condition ? conditional.condition->resolved_type : nullptr;
			jump->condition = condition;
			builder.selectBlock(else_block);
			buildStatement(builder, conditional.else_branch);
			const LsIrBlock then_block = builder.newBlock();
			const LsIrBlock merge_block = builder.newBlock();
			jump->target = then_block;
			if (!builder.block->terminator) {
				LsOpJump* else_jump = builder.terminate<LsOpJump>();
				if (else_jump) else_jump->target = merge_block;
			}
			builder.selectBlock(then_block);
			buildStatement(builder, conditional.body);
			if (!builder.block->terminator) {
				LsOpJump* then_jump = builder.terminate<LsOpJump>();
				if (then_jump) then_jump->target = merge_block;
			}
			builder.selectBlock(merge_block);
			break;
		}
		case Statement::MATCH: {
			MatchStatement& match = *static_cast<MatchStatement*>(statement);
			if (!match.comptime_known && match.subject && match.subject->kind == Expression::TYPE_MEMBER &&
				static_cast<TypeMemberExpression*>(match.subject)->kind == TypeMemberExpression::KIND) {
				ResolvedType* reflected = static_cast<TypeMemberExpression*>(match.subject)->reflected_type;
				static const char* kind_names[] = { "Bool", "I8", "I16", "I32", "I64", "ISize", "U8", "U16", "U32", "U64", "Byte", "F32", "F64", "CStr", "CPtr", "Void", "Type", "Nullable", "Slice", "Array", "Enum", "Struct", "Union", "Fn", "Pointer" };
				ResolvedType::Kind kind = reflected ? reflected->kind : ResolvedType::VOID;
				if (kind == ResolvedType::UNTYPED_INT) kind = ResolvedType::I32;
				if (kind == ResolvedType::UNTYPED_FLOAT) kind = ResolvedType::F64;
				const char* name = nullptr;
				for (u32 i = 0; i < sizeof(kind_names) / sizeof(kind_names[0]); ++i) {
					if ((i32)kind == (i32)ResolvedType::VOID) {}
					if (kind_names[i] && ((i == 0 && kind == ResolvedType::BOOL) || (i == 1 && kind == ResolvedType::I8) || (i == 2 && kind == ResolvedType::I16) || (i == 3 && kind == ResolvedType::I32) || (i == 4 && kind == ResolvedType::I64) || (i == 5 && kind == ResolvedType::ISIZE) || (i == 6 && kind == ResolvedType::U8) || (i == 7 && kind == ResolvedType::U16) || (i == 8 && kind == ResolvedType::U32) || (i == 9 && kind == ResolvedType::U64) || (i == 10 && kind == ResolvedType::BYTE) || (i == 11 && kind == ResolvedType::F32) || (i == 12 && kind == ResolvedType::F64) || (i == 13 && kind == ResolvedType::CSTR) || (i == 14 && kind == ResolvedType::CPTR) || (i == 15 && kind == ResolvedType::VOID) || (i == 16 && kind == ResolvedType::META) || (i == 17 && kind == ResolvedType::NULLABLE) || (i == 18 && kind == ResolvedType::SLICE) || (i == 19 && kind == ResolvedType::ARRAY) || (i == 20 && kind == ResolvedType::ENUM) || (i == 21 && kind == ResolvedType::STRUCT) || (i == 22 && kind == ResolvedType::UNION) || (i == 23 && kind == ResolvedType::FUNCTION) || (i == 24 && kind == ResolvedType::POINTER))) { name = kind_names[i]; break; }
				}
				for (i32 i = 0; i < match.arms.size(); ++i) {
					MatchArm& arm = match.arms[i];
					for (MatchPattern& pattern : arm.patterns) if (pattern.begin && pattern.begin->kind == Expression::MEMBER && !static_cast<MemberExpression*>(pattern.begin)->expression && name && equalStrings(static_cast<MemberExpression*>(pattern.begin)->name, makeStringView(name))) { buildStatement(builder, arm.body); return; }
					if (arm.is_fallback) { buildStatement(builder, arm.body); return; }
				}
				break;
			}
			if (match.comptime_known) {
				if (match.comptime_arm >= 0 && match.comptime_arm < match.arms.size()) buildStatement(builder, match.arms[match.comptime_arm].body);
				break;
			}
			if (!match.subject || !match.subject->resolved_type) break;
			if (match.subject->resolved_type->kind == ResolvedType::UNION) {
				UnionResolvedType& union_type = *static_cast<UnionResolvedType*>(match.subject->resolved_type);
				LsIrValue subject = buildExpression(builder, match.subject);
				if (subject == LS_IR_INVALID_VALUE) return;
				const LsIrBlock merge = builder.newBlock();
				ExpArray<LsIrBlock> check_blocks(builder.arena);
				ExpArray<LsIrBlock> body_blocks(builder.arena);
				for (MatchArm& arm : match.arms) {
					const u32 check_count = arm.patterns.size() < 16 ? (u32)arm.patterns.size() : 16u;
					for (u32 i = 0; i < (check_count ? check_count : 1); ++i) check_blocks.push_back(builder.newBlock());
				}
				const LsIrBlock no_match = builder.newBlock();
				for (MatchArm& arm : match.arms) body_blocks.push_back(builder.newBlock());
				LsOpJump* enter = builder.terminate<LsOpJump>();
				if (!enter) return;
				enter->target = check_blocks.empty() ? no_match : check_blocks[0];
				u32 check_block_index = 0;
				for (u32 arm_index = 0; arm_index < (u32)match.arms.size(); ++arm_index) {
					MatchArm& arm = match.arms[(i32)arm_index];
					LsIrBlock checks[16] = {};
					const u32 check_count = arm.patterns.size() < 16 ? (u32)arm.patterns.size() : 16u;
					const u32 arm_check_count = check_count ? check_count : 1;
					for (u32 i = 0; i < check_count; ++i) checks[i] = check_blocks[check_block_index + i];
					const LsIrBlock entry = check_blocks[check_block_index];
					check_block_index += arm_check_count;
					const LsIrBlock failure = check_block_index < (u32)check_blocks.size() ? check_blocks[check_block_index] : no_match;
					const LsIrBlock body = body_blocks[arm_index];
					bool emitted = false;
					if (arm.is_fallback) {
						builder.selectBlock(entry);
						LsOpJump* enter_fallback = builder.terminate<LsOpJump>();
						if (!enter_fallback) return;
						enter_fallback->target = body;
						builder.selectBlock(body);
						buildStatement(builder, arm.body);
						if (!builder.block->terminator) {
							LsOpJump* leave = builder.terminate<LsOpJump>();
							if (leave) leave->target = merge;
						}
						continue;
					}
					for (i32 pattern_index = 0; pattern_index < arm.patterns.size(); ++pattern_index) {
						MatchPattern& pattern = arm.patterns[pattern_index];
						if (pattern_index >= 16) break;
						ResolvedType* pattern_type = pattern.begin ? pattern.begin->resolved_type : nullptr;
						if (pattern.begin && pattern.begin->kind == Expression::RESOLVED_TYPE)
							pattern_type = static_cast<ResolvedTypeExpression*>(pattern.begin)->resolved_type;
						if (pattern_type && pattern_type->kind == ResolvedType::META) pattern_type = static_cast<MetaType*>(pattern_type)->inner;
						if (!pattern_type && pattern.begin && pattern.begin->kind == Expression::IDENTIFIER) {
							ls_string_view name = static_cast<IdentifierExpression*>(pattern.begin)->name;
							for (ResolvedType* candidate : union_type.members) if (candidate->kind == ResolvedType::STRUCT && equalStrings(static_cast<StructResolvedType*>(candidate)->decl->cached_name, name)) { pattern_type = candidate; break; }
						}
						if (!pattern_type) continue;
						for (i32 member_index = 0; member_index < union_type.members.size(); ++member_index) {
							bool pattern_matches = pattern_type && union_type.members[member_index] && pattern_type->kind == union_type.members[member_index]->kind;
							if (pattern_matches && pattern_type->kind == ResolvedType::STRUCT)
								pattern_matches = static_cast<StructResolvedType*>(pattern_type)->decl == static_cast<StructResolvedType*>(union_type.members[member_index])->decl ||
									equalStrings(static_cast<StructResolvedType*>(pattern_type)->decl->cached_name, static_cast<StructResolvedType*>(union_type.members[member_index])->decl->cached_name);
							if (!pattern_matches) continue;
							builder.selectBlock(checks[pattern_index]);
							LsOpCopy* tag = builder.append<LsOpCopy>();
							LsOpLoadConst* expected = tag ? builder.append<LsOpLoadConst>() : nullptr;
							LsOpCompare* compare = expected ? builder.append<LsOpCompare>() : nullptr;
							LsOpConditionalJump* branch = compare ? builder.terminate<LsOpConditionalJump>() : nullptr;
							if (!tag || !expected || !compare || !branch) return;
							static ResolvedType i32_type(ResolvedType::I32);
							tag->type = &i32_type; tag->result = builder.newValue(); tag->source = subject; tag->source_offset = 0;
							expected->type = &i32_type; expected->result = builder.newValue(); expected->value = (u32)member_index;
							compare->type = &i32_type; compare->result = builder.newValue(); compare->lhs = tag->result; compare->rhs = expected->result; compare->op = LS_IR_COMPARE_EQ;
							branch->condition = compare->result;
							branch->target = body;
							branch->type = &i32_type;
							emitted = true;
							break;
						}
					}
					if (!emitted) { builder.selectBlock(entry); LsOpJump* skip = builder.terminate<LsOpJump>(); if (skip) skip->target = failure; }
					builder.selectBlock(body);
					buildStatement(builder, arm.body);
					if (!builder.block->terminator) { LsOpJump* leave = builder.terminate<LsOpJump>(); if (leave) leave->target = merge; }
				}
				builder.selectBlock(no_match);
				LsOpJump* fallback = builder.terminate<LsOpJump>();
				if (fallback) fallback->target = merge;
				builder.selectBlock(merge);
				break;
			}
			const LsIrValue subject = buildExpression(builder, match.subject);
			if (subject == LS_IR_INVALID_VALUE) return;

			ExpArray<LsIrBlock> pattern_blocks(builder.arena);
			ExpArray<LsIrBlock> arm_blocks(builder.arena);
			for (MatchArm& arm : match.arms) {
				for (MatchPattern& pattern : arm.patterns) {
					pattern_blocks.push_back(builder.newBlock());
					if (pattern.end) pattern_blocks.push_back(builder.newBlock());
				}
			}
			const LsIrBlock failure = builder.newBlock();
			for (MatchArm& arm : match.arms) {
				arm_blocks.push_back(builder.newBlock());
			}
			const LsIrBlock merge = builder.newBlock();

			LsOpJump* enter = builder.terminate<LsOpJump>();
			if (!enter) return;
			enter->target = pattern_blocks.empty() ? failure : pattern_blocks[0];
			auto buildPatternValue = [&](Expression* expression) {
				if (expression && expression->kind == Expression::MEMBER && !static_cast<MemberExpression*>(expression)->expression &&
					match.subject && match.subject->resolved_type && match.subject->resolved_type->kind == ResolvedType::ENUM) {
					EnumResolvedType& en = *static_cast<EnumResolvedType*>(match.subject->resolved_type);
					const ls_string_view name = static_cast<MemberExpression*>(expression)->name;
					for (i32 i = 0; i < en.decl->members.size(); ++i) {
						const EnumMember& member = en.decl->members[i];
						if (!equalStrings(member.name, name)) continue;
						u64 value = member.value && member.value->kind == Expression::INT_LITERAL
							? (u64)static_cast<IntLiteralExpression*>(member.value)->value : (u64)i;
						LsOpLoadConst* op = builder.append<LsOpLoadConst>();
						if (!op) return LS_IR_INVALID_VALUE;
						op->type = match.subject->resolved_type;
						op->result = builder.newValue();
						op->value = value;
						return op->result;
					}
				}
				return buildExpression(builder, expression);
			};

			u32 pattern_index = 0;
			for (u32 arm_index = 0; arm_index < (u32)match.arms.size(); ++arm_index) {
				MatchArm& arm = match.arms[(i32)arm_index];
				for (MatchPattern& pattern : arm.patterns) {
					const u32 block_count = pattern.end ? 2 : 1;
					const LsIrBlock next = pattern_index + block_count < (u32)pattern_blocks.size()
						? pattern_blocks[pattern_index + block_count] : failure;
					builder.selectBlock(pattern_blocks[pattern_index++]);
					const LsIrValue begin = buildPatternValue(pattern.begin);
					if (begin == LS_IR_INVALID_VALUE) return;
					LsOpCompare* compare = builder.append<LsOpCompare>();
					LsOpConditionalJump* branch = compare ? builder.terminate<LsOpConditionalJump>() : nullptr;
					if (!compare || !branch) return;
					compare->type = match.subject ? match.subject->resolved_type : nullptr;
					compare->result = builder.newValue();
					compare->lhs = subject;
					compare->rhs = begin;
					compare->op = pattern.end ? LS_IR_COMPARE_LT : LS_IR_COMPARE_EQ;
					branch->type = compare->type;
					branch->condition = compare->result;
					branch->target = pattern.end ? next : arm_blocks[arm_index];
					if (pattern.end) {
						builder.selectBlock(pattern_blocks[pattern_index++]);
						const LsIrValue end = buildExpression(builder, pattern.end);
						if (end == LS_IR_INVALID_VALUE) return;
						compare = builder.append<LsOpCompare>();
						branch = compare ? builder.terminate<LsOpConditionalJump>() : nullptr;
						if (!compare || !branch) return;
						compare->type = match.subject ? match.subject->resolved_type : nullptr;
						compare->result = builder.newValue();
						compare->lhs = subject;
						compare->rhs = end;
						compare->op = LS_IR_COMPARE_LE;
						branch->type = compare->type;
						branch->condition = compare->result;
						branch->target = arm_blocks[arm_index];
					}
				}
			}

			builder.selectBlock(failure);
			LsOpJump* no_match = builder.terminate<LsOpJump>();
			if (!no_match) return;
			no_match->target = merge;
			for (u32 i = 0; i < (u32)match.arms.size(); ++i) {
				if (match.arms[(i32)i].is_fallback) {
					no_match->target = arm_blocks[i];
					break;
				}
			}

			for (u32 i = 0; i < (u32)match.arms.size(); ++i) {
				builder.selectBlock(arm_blocks[i]);
				buildStatement(builder, match.arms[(i32)i].body);
				if (!builder.block->terminator) {
					LsOpJump* leave = builder.terminate<LsOpJump>();
					if (leave) leave->target = merge;
				}
			}
			builder.selectBlock(merge);
			break;
		}
		default:
			break;
	}
}

}

template <typename T>
static T* irAppendArena(ls_arena& arena, T*& data, u32& count, u32& capacity) {
	if (count >= capacity) {
		const u32 new_capacity = capacity ? capacity * 2u : 4u;
		T* next = (T*)arena.allocate(arena.user_data, sizeof(T) * new_capacity, alignof(T));
		if (!next) return nullptr;
		if (data && count) copyMemory(next, data, sizeof(T) * count);
		data = next;
		capacity = new_capacity;
	}
	return &data[count++];
}

struct IrDebugTypes {
	ls_bytecode* bytecode;
	ExpArray<ResolvedType*> types;

	IrDebugTypes(ls_arena& arena, ls_bytecode* bytecode) : bytecode(bytecode), types(arena) {}

	u32 resolve(ResolvedType* type) {
		if (!type) return LS_TYPE_INDEX_NONE;
		for (u32 i = 0; i < (u32)types.size(); ++i) if (types[(i32)i] == type) return i;
		ls_type* info = irAppendArena(*bytecode->arena, bytecode->type_info, bytecode->type_info_count, bytecode->type_info_capacity);
		if (!info) return LS_TYPE_INDEX_NONE;
		const u32 index = bytecode->type_info_count - 1u;
		types.push_back(type);
		*info = {};
		info->bytecode = bytecode;
		info->kind = bytecodeTypeKind(type);
		info->byte_size = typeByteSize(*type);
		info->element_type_index = LS_TYPE_INDEX_NONE;
		info->array_length = LS_TYPE_INDEX_NONE;
		if (type->kind == ResolvedType::STRUCT) {
			StructResolvedType& structure = *static_cast<StructResolvedType*>(type);
			info->kind = LS_TYPE_STRUCT;
			if (structure.decl) {
				info->name = structure.decl->cached_name;
				info->field_count = (u32)structure.decl->fields.size();
				ExpArray<u32> field_types(*bytecode->arena);
				for (i32 i = 0; i < (i32)info->field_count; ++i) field_types.push_back(resolve(structure.field_types[i]));
				info->first_field_index = bytecode->type_field_count;
				for (i32 i = 0; i < (i32)info->field_count; ++i) {
					ls_type_field_info* field = irAppendArena(*bytecode->arena, bytecode->type_fields, bytecode->type_field_count, bytecode->type_field_capacity);
					if (!field) return LS_TYPE_INDEX_NONE;
					field->name = structure.decl->fields[i].name;
					field->type_index = field_types[i];
					field->offset = 0;
					for (i32 j = 0; j < i; ++j) field->offset += typeByteSize(*structure.field_types[j]);
				}
			}
		} else if (type->kind == ResolvedType::ARRAY) {
			ArrayResolvedType& array = *static_cast<ArrayResolvedType*>(type);
			info->kind = LS_TYPE_ARRAY;
			info->element_type_index = resolve(array.element_type);
			info->array_length = (u32)array.size;
		} else if (type->kind == ResolvedType::SLICE) {
			SliceResolvedType& slice = *static_cast<SliceResolvedType*>(type);
			info->kind = LS_TYPE_SLICE;
			info->element_type_index = resolve(slice.element_type);
			info->array_length = 0;
			info->is_const = slice.is_const;
		} else if (type->kind == ResolvedType::NULLABLE) {
			NullableResolvedType& nullable = *static_cast<NullableResolvedType*>(type);
			info->kind = LS_TYPE_NULLABLE;
			info->element_type_index = resolve(nullable.inner);
		} else if (type->kind == ResolvedType::POINTER) {
			info->kind = LS_TYPE_CPTR;
			info->element_type_index = resolve(static_cast<PointerResolvedType*>(type)->inner);
		} else if (type->kind == ResolvedType::UNION) {
			UnionResolvedType& union_type = *static_cast<UnionResolvedType*>(type);
			info->kind = LS_TYPE_TAGGED_UNION;
			info->member_count = (u32)union_type.members.size();
			info->first_member_index = bytecode->type_member_count;
			for (ResolvedType* member : union_type.members) {
				u32* member_index = irAppendArena(*bytecode->arena, bytecode->type_member_indices,
					bytecode->type_member_count, bytecode->type_member_capacity);
				if (!member_index) return LS_TYPE_INDEX_NONE;
				*member_index = resolve(member);
			}
		} else if (type->kind == ResolvedType::ENUM) {
			EnumResolvedType& enumeration = *static_cast<EnumResolvedType*>(type);
			info->kind = LS_TYPE_ENUM;
			if (enumeration.decl) {
				info->name = enumeration.decl->cached_name;
				info->value_count = (u32)enumeration.decl->members.size();
				info->first_value_index = bytecode->type_enum_value_count;
				i64 next_value = 0;
				for (i32 i = 0; i < (i32)info->value_count; ++i) {
					EnumMember& member = enumeration.decl->members[i];
					i64 value = next_value;
					if (member.value && member.value->kind == Expression::INT_LITERAL)
						value = static_cast<IntLiteralExpression*>(member.value)->value;
					next_value = value + 1;
					ls_type_enum_value_info* entry = irAppendArena(*bytecode->arena, bytecode->type_enum_values,
						bytecode->type_enum_value_count, bytecode->type_enum_value_capacity);
					if (!entry) return LS_TYPE_INDEX_NONE;
					entry->name = member.name;
					entry->value = (i32)value;
				}
			}
		}
		return index;
	}
};

static void irDebugLocal(ls_bytecode& bytecode, u32 function_index, IrDebugTypes& types, ls_string_view name, StorageSlot& slot, u32 scope_begin) {
	if (!slot.type || slot.storage == StorageSlot::GLOBAL) return;
	ls_function_bc& function = bytecode.functions[function_index];
	ls_bytecode_local_debug_entry* locals = (ls_bytecode_local_debug_entry*)bytecode.arena->allocate(bytecode.arena->user_data,
		sizeof(ls_bytecode_local_debug_entry) * (function.local_count + 1u), alignof(ls_bytecode_local_debug_entry));
	if (!locals) return;
	if (function.locals && function.local_count) copyMemory(locals, function.locals, sizeof(ls_bytecode_local_debug_entry) * function.local_count);
	function.locals = locals;
	ls_bytecode_local_debug_entry* entry = &locals[function.local_count++];
	entry->name = name;
	entry->offset = slot.offset;
	entry->byte_size = slot.byte_size;
	entry->kind = bytecodeTypeKind(slot.type);
	entry->type_index = types.resolve(slot.type);
	entry->scope_begin_offset = scope_begin;
}

static u32 irDebugScopeBegin(const ls_function_bc& function, u32 line) {
	for (u32 i = 0; i < function.source_map_count; ++i)
		if (function.source_map[i].line > line) return function.source_map[i].code_offset;
	return function.source_map_count ? function.source_map[function.source_map_count - 1].code_offset : 0;
}

static bool irIsRuntimeFunction(FunctionExpression* function) {
	if (!function || function->is_template) return false;
	if (!function->resolved_type || function->resolved_type->kind != ResolvedType::FUNCTION) return false;
	FunctionResolvedType* type = static_cast<FunctionResolvedType*>(function->resolved_type);
	if (!function->is_extern && !function->body) return false;
	return !type->return_type || type->return_type->kind != ResolvedType::META;
}

static bool irModuleHasFunction(LsIrModuleData& module, FunctionExpression* source) {
	for (LsIrModuleEntry& entry : module.functions) if (entry.source == source) return true;
	return false;
}

LsIrFunctionData* lsIrBuildFunction(ls_arena& arena, FunctionExpression* source, ls_string_view name) {
	if (!source || !source->body || source->body->kind != Statement::BLOCK) return nullptr;
	void* memory = arena.allocate(arena.user_data, sizeof(LsIrFunctionData), alignof(LsIrFunctionData));
	if (!memory) return nullptr;
	LsIrFunctionData* function = ::new (NewPlaceholder{}, memory) LsIrFunctionData(arena);
	function->name = name;
	function->source = source;
	if (source->resolved_type && source->resolved_type->kind == ResolvedType::FUNCTION)
		function->return_type = static_cast<FunctionResolvedType*>(source->resolved_type)->return_type;
	IrBuilder builder(arena, *function);
	buildStatement(builder, source->body);
	if (builder.failed) return nullptr;
	if (!builder.block->terminator) builder.terminate<LsOpReturn>();
	return function;
}

LsIrModuleData* lsIrBuildModule(ls_arena& arena, ls_module* source) {
	if (!source) return nullptr;
	void* memory = arena.allocate(arena.user_data, sizeof(LsIrModuleData), alignof(LsIrModuleData));
	if (!memory) return nullptr;
	LsIrModuleData* module = ::new (NewPlaceholder{}, memory) LsIrModuleData(arena);
	module->source = source;
	u32 global_size = 0;
	for (Unit& unit : source->units) {
		for (Symbol& symbol : unit.symbols) {
			if (!symbolHasGlobalStorage(symbol) || !symbol.resolved_type) continue;
			symbol.slot.storage = StorageSlot::GLOBAL;
			symbol.slot.offset = global_size;
			symbol.slot.byte_size = typeByteSize(*symbol.resolved_type);
			symbol.slot.type = symbol.resolved_type;
			global_size += symbol.slot.byte_size ? symbol.slot.byte_size : 1u;
		}
	}
	module->global_size = global_size;
	u32 function_index = 0;
	for (Unit& unit : source->units) {
		for (Symbol& symbol : unit.symbols) {
			if (!symbol.expression || symbol.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* function = static_cast<FunctionExpression*>(symbol.expression);
			if (irIsRuntimeFunction(function) && function->bytecode_index == ~0u) function->bytecode_index = function_index++;
			for (TemplateFunctionInstance& instance : function->template_function_instances) {
				if (irIsRuntimeFunction(instance.instance) && instance.instance->bytecode_index == ~0u) instance.instance->bytecode_index = function_index++;
			}
		}
	}
	for (Unit& unit : source->units) {
		for (Symbol& symbol : unit.symbols) {
			if (!symbol.expression || symbol.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* function = static_cast<FunctionExpression*>(symbol.expression);
			if (irIsRuntimeFunction(function)) {
				if (irModuleHasFunction(*module, function)) continue;
				LsIrModuleEntry& entry = module->functions.emplace_back();
				entry.source = function;
				entry.name = symbol.name;
				entry.native = function->is_extern;
				entry.builtin_native = function->is_extern && (equalStrings(unit.path, makeStringView("std:math")) || equalStrings(unit.path, makeStringView("std:mem")));
				if (!entry.native) {
					entry.function = lsIrBuildFunction(arena, function, symbol.name);
					if (!entry.function) return nullptr;
				}
			}
			for (TemplateFunctionInstance& instance : function->template_function_instances) {
				if (!irIsRuntimeFunction(instance.instance)) continue;
				if (irModuleHasFunction(*module, instance.instance)) continue;
				LsIrModuleEntry& instance_entry = module->functions.emplace_back();
				instance_entry.source = instance.instance;
				instance_entry.name = symbol.name;
				instance_entry.native = instance.instance->is_extern;
				instance_entry.builtin_native = false;
				if (!instance_entry.native) {
					instance_entry.function = lsIrBuildFunction(arena, instance.instance, symbol.name);
					if (!instance_entry.function) return nullptr;
				}
			}
		}
	}
	void* init_memory = arena.allocate(arena.user_data, sizeof(LsIrFunctionData), alignof(LsIrFunctionData));
	if (!init_memory) return nullptr;
	LsIrFunctionData* init = ::new (NewPlaceholder{}, init_memory) LsIrFunctionData(arena);
	init->name = makeStringView("<global_init>");
	IrBuilder init_builder(arena, *init);
	for (Unit& unit : source->units) {
		for (Symbol& symbol : unit.symbols) {
			if (!symbolHasGlobalStorage(symbol) || !symbol.expression || symbol.expression->kind == Expression::UNDEFINED) continue;
			const LsIrValue value = buildExpression(init_builder, symbol.expression);
			if (value == LS_IR_INVALID_VALUE) return nullptr;
			LsOpGlobalStore* store = init_builder.append<LsOpGlobalStore>();
			if (!store) return nullptr;
			store->type = symbol.resolved_type;
			store->source = value;
			store->offset = symbol.slot.offset;
		}
	}
	init_builder.terminate<LsOpReturn>();
	LsIrModuleEntry& init_entry = module->functions.emplace_back();
	init_entry.function = init;
	init_entry.name = init->name;
	return module;
}

ls_bytecode* lsIrCompileFunction(LsIrFunctionData* function, ls_host* host) {
	if (!function || !host || !host->arena.allocate || function->blocks.empty()) return nullptr;
	ByteWriter code(host->arena, 64u * 1024u);
	if (!code.data) return nullptr;
	ExpArray<u32> registers(host->arena);
	ExpArray<u32> block_offsets(host->arena);
	ExpArray<JumpPatch> patches(host->arena);
	ExpArray<ls_bytecode_source_map_entry> source_map(host->arena);
	registers.resize(function->next_value, LS_IR_INVALID_VALUE);
	u32 frame_size = function->local_size > function->param_size ? function->local_size : function->param_size;
	for (LsIrBlockData& block : function->blocks) {
		for (LsIrOp* base : block.ops) {
			if (base->kind == LS_IR_OP_LOCAL_LOAD) {
				LsOpLocalLoad& op = *static_cast<LsOpLocalLoad*>(base);
				frame_size = (op.offset + typeRegisterSize(op.type) > frame_size) ? op.offset + typeRegisterSize(op.type) : frame_size;
			} else if (base->kind == LS_IR_OP_LOCAL_STORE) {
				LsOpLocalStore& op = *static_cast<LsOpLocalStore*>(base);
				frame_size = (op.offset + typeRegisterSize(op.type) > frame_size) ? op.offset + typeRegisterSize(op.type) : frame_size;
			} else if (base->kind == LS_IR_OP_LOAD_INDEXED) {
				LsOpLoadIndexed& op = *static_cast<LsOpLoadIndexed*>(base);
				if (!op.base_is_value) {
					const u32 end = op.base_offset + op.scale * op.length + typeRegisterSize(op.type);
					if (end > frame_size) frame_size = end;
				}
			} else if (base->kind == LS_IR_OP_STORE_INDEXED) {
				LsOpStoreIndexed& op = *static_cast<LsOpStoreIndexed*>(base);
				if (!op.base_is_value) {
					const u32 end = op.base_offset + op.scale * op.length + typeRegisterSize(op.type);
					if (end > frame_size) frame_size = end;
				}
			}
		}
	}
	for (LsIrBlockData& block : function->blocks) {
		for (LsIrOp* base : block.ops) {
			if (!base) return nullptr;
			LsIrValue result = LS_IR_INVALID_VALUE;
			ResolvedType* type = nullptr;
			switch (base->kind) {
				case LS_IR_OP_LOAD_CONST: result = static_cast<LsOpLoadConst*>(base)->result; type = static_cast<LsOpLoadConst*>(base)->type; break;
				case LS_IR_OP_COPY: result = static_cast<LsOpCopy*>(base)->result; type = static_cast<LsOpCopy*>(base)->type; break;
				case LS_IR_OP_AGGREGATE_INIT: result = static_cast<LsOpAggregateInit*>(base)->result; type = static_cast<LsOpAggregateInit*>(base)->type; break;
				case LS_IR_OP_GLOBAL_LOAD: result = static_cast<LsOpGlobalLoad*>(base)->result; type = static_cast<LsOpGlobalLoad*>(base)->type; break;
				case LS_IR_OP_LOCAL_LOAD: result = static_cast<LsOpLocalLoad*>(base)->result; type = static_cast<LsOpLocalLoad*>(base)->type; break;
				case LS_IR_OP_LOAD_INDEXED: result = static_cast<LsOpLoadIndexed*>(base)->result; type = static_cast<LsOpLoadIndexed*>(base)->type; break;
				case LS_IR_OP_MAKE_SLICE: result = static_cast<LsOpMakeSlice*>(base)->result; type = static_cast<LsOpMakeSlice*>(base)->type; break;
				case LS_IR_OP_SLICE: result = static_cast<LsOpSlice*>(base)->result; type = static_cast<LsOpSlice*>(base)->type; break;
				case LS_IR_OP_SLICE_LOAD: result = static_cast<LsOpSliceLoad*>(base)->result; type = static_cast<LsOpSliceLoad*>(base)->type; break;
				case LS_IR_OP_SLICE_LOAD_AT: result = static_cast<LsOpSliceLoadAt*>(base)->result; type = static_cast<LsOpSliceLoadAt*>(base)->type; break;
				case LS_IR_OP_SLICE_LENGTH: result = static_cast<LsOpSliceLength*>(base)->result; type = nullptr; break;
				case LS_IR_OP_SLICE_EQ: result = static_cast<LsOpSliceEq*>(base)->result; type = nullptr; break;
				case LS_IR_OP_LOCAL_REF: result = static_cast<LsOpLocalRef*>(base)->result; type = static_cast<LsOpLocalRef*>(base)->type; break;
				case LS_IR_OP_GLOBAL_REF: result = static_cast<LsOpGlobalRef*>(base)->result; type = static_cast<LsOpGlobalRef*>(base)->type; break;
				case LS_IR_OP_ADD:
				case LS_IR_OP_SUB:
				case LS_IR_OP_MUL:
				case LS_IR_OP_DIV:
				case LS_IR_OP_MOD:
				case LS_IR_OP_COMPARE: result = static_cast<LsOpAdd*>(base)->result; type = static_cast<LsOpAdd*>(base)->type; break;
				case LS_IR_OP_NEG:
				case LS_IR_OP_NOT: result = static_cast<LsOpNeg*>(base)->result; type = static_cast<LsOpNeg*>(base)->type; break;
				case LS_IR_OP_CALL_DIRECT:
				case LS_IR_OP_CALL_NATIVE: result = static_cast<LsOpCallDirect*>(base)->result; type = static_cast<LsOpCallDirect*>(base)->type; break;
				case LS_IR_OP_CALL_INDIRECT: result = static_cast<LsOpCallIndirect*>(base)->result; type = static_cast<LsOpCallIndirect*>(base)->type; break;
				case LS_IR_OP_CAST: result = static_cast<LsOpCast*>(base)->result; type = static_cast<LsOpCast*>(base)->target_type; break;
				default: break;
			}
			if (result == LS_IR_INVALID_VALUE) continue;
			if (result >= function->next_value || registers[result] != LS_IR_INVALID_VALUE) continue;
			const u32 size = base->kind == LS_IR_OP_COMPARE || base->kind == LS_IR_OP_SLICE_EQ ? 1u : base->kind == LS_IR_OP_SLICE_LENGTH ? 8u : typeRegisterSize(type);
			registers[result] = frame_size;
			frame_size += size;
			if (base->kind == LS_IR_OP_CALL_DIRECT || base->kind == LS_IR_OP_CALL_NATIVE) frame_size += static_cast<LsOpCallDirect*>(base)->argument_size;
			if (base->kind == LS_IR_OP_CALL_INDIRECT) frame_size += 4u + static_cast<LsOpCallIndirect*>(base)->argument_size;
		}
	}

	for (LsIrBlockData& block : function->blocks) {
		block_offsets.push_back(code.size);
		for (LsIrOp* base : block.ops) {
			if (base->src_loc != LS_IR_INVALID_SOURCE_LOC && (source_map.empty() || source_map[source_map.size() - 1].line != base->src_loc)) {
				ls_bytecode_source_map_entry& entry = source_map.emplace_back();
				entry.code_offset = code.size;
				entry.source_name = function->source ? function->source->token.source_name : ls_string_view{};
				entry.line = base->src_loc;
				entry.column = 0;
			}
			switch (base->kind) {
				case LS_IR_OP_LOAD_CONST: {
					LsOpLoadConst& op = *static_cast<LsOpLoadConst*>(base);
					if (op.result >= function->next_value) return nullptr;
					if (op.has_second_value) {
						if (!code.op(LS_OP_LOAD_CONST_8) || !code.writeValue(registers[op.result]) || !code.writeValue(op.value) ||
							!code.op(LS_OP_LOAD_CONST_8) || !code.writeValue(registers[op.result] + 8u) || !code.writeValue(op.second_value)) return nullptr;
						break;
					}
					const u32 size = typeRegisterSize(op.type);
					if (size == 1 || size == 2 || size == 4 || size == 8) {
						const ls_op opcode = size == 1 ? LS_OP_LOAD_CONST_1 : size == 2 ? LS_OP_LOAD_CONST_2 : size == 4 ? LS_OP_LOAD_CONST_4 : LS_OP_LOAD_CONST_8;
						if (!code.op(opcode) || !code.writeValue(registers[op.result])) return nullptr;
						if (size == 1 && !code.writeValue((u8)op.value)) return nullptr;
						if (size == 2 && !code.writeValue((u16)op.value)) return nullptr;
						if (size == 4 && !code.writeValue((u32)op.value)) return nullptr;
						if (size == 8 && !code.writeValue(op.value)) return nullptr;
					} else {
						u32 offset = 0;
						while (offset < size) {
							const u32 chunk = size - offset >= 4 ? 4u : size - offset >= 2 ? 2u : 1u;
							const u64 bits = offset < 8 ? op.value >> (offset * 8u) : op.second_value >> ((offset - 8u) * 8u);
							const ls_op opcode = chunk == 1 ? LS_OP_LOAD_CONST_1 : chunk == 2 ? LS_OP_LOAD_CONST_2 : LS_OP_LOAD_CONST_4;
							if (!code.op(opcode) || !code.writeValue(registers[op.result] + offset)) return nullptr;
							if (chunk == 1 && !code.writeValue((u8)bits)) return nullptr;
							if (chunk == 2 && !code.writeValue((u16)bits)) return nullptr;
							if (chunk == 4 && !code.writeValue((u32)bits)) return nullptr;
							offset += chunk;
						}
					}
					break;
				}
				case LS_IR_OP_COPY: {
					LsOpCopy& op = *static_cast<LsOpCopy*>(base);
					if (op.result >= function->next_value || op.source >= function->next_value || !code.op(LS_OP_COPY) ||
					!code.writeValue(registers[op.result]) || !code.writeValue(registers[op.source] + op.source_offset) || !code.writeValue(typeRegisterSize(op.type))) return nullptr;
					break;
				}
				case LS_IR_OP_AGGREGATE_INIT: {
					LsOpAggregateInit& op = *static_cast<LsOpAggregateInit*>(base);
					if (op.result >= function->next_value) return nullptr;
					for (u32 i = 0; i < op.value_count; ++i) {
						const u32 value_size = op.sizes ? op.sizes[i] : typeRegisterSize(op.type && op.type->kind == ResolvedType::STRUCT
							? static_cast<StructResolvedType*>(op.type)->field_types[(i32)i]
							: op.type && op.type->kind == ResolvedType::ARRAY ? static_cast<ArrayResolvedType*>(op.type)->element_type : nullptr);
						if (op.values[i] >= function->next_value || !code.op(LS_OP_COPY) ||
						!code.writeValue(registers[op.result] + op.offsets[i]) || !code.writeValue(registers[op.values[i]]) ||
						!code.writeValue(value_size)) return nullptr;
					}
					break;
				}
				case LS_IR_OP_FIELD_STORE: {
					LsOpFieldStore& op = *static_cast<LsOpFieldStore*>(base);
					if (op.aggregate >= function->next_value || op.source >= function->next_value || !code.op(LS_OP_COPY) ||
						!code.writeValue(registers[op.aggregate] + op.offset) || !code.writeValue(registers[op.source]) ||
						!code.writeValue(typeRegisterSize(op.type))) return nullptr;
					break;
				}
				case LS_IR_OP_LOCAL_LOAD: {
					LsOpLocalLoad& op = *static_cast<LsOpLocalLoad*>(base);
					if (op.result >= function->next_value || !code.op(LS_OP_COPY) || !code.writeValue(registers[op.result]) ||
						!code.writeValue(op.offset) || !code.writeValue(typeRegisterSize(op.type))) return nullptr;
					break;
				}
				case LS_IR_OP_LOCAL_REF: {
					LsOpLocalRef& op = *static_cast<LsOpLocalRef*>(base);
					if (op.result >= function->next_value || !code.op(LS_OP_LOCAL_REF) || !code.writeValue(registers[op.result]) || !code.writeValue(op.offset)) return nullptr;
					break;
				}
				case LS_IR_OP_LOCAL_STORE: {
					LsOpLocalStore& op = *static_cast<LsOpLocalStore*>(base);
					if (op.source >= function->next_value || !code.op(LS_OP_COPY) || !code.writeValue(op.offset) ||
						!code.writeValue(registers[op.source]) || !code.writeValue(typeRegisterSize(op.type))) return nullptr;
					break;
				}
				case LS_IR_OP_GLOBAL_LOAD: {
					LsOpGlobalLoad& op = *static_cast<LsOpGlobalLoad*>(base);
					if (op.result >= function->next_value || !code.op(LS_OP_GLOBAL_LOAD) || !code.writeValue(registers[op.result]) ||
						!code.writeValue(op.offset) || !code.writeValue(typeRegisterSize(op.type))) return nullptr;
					break;
				}
				case LS_IR_OP_GLOBAL_STORE: {
					LsOpGlobalStore& op = *static_cast<LsOpGlobalStore*>(base);
					if (op.source >= function->next_value || !code.op(LS_OP_GLOBAL_STORE) || !code.writeValue(op.offset) ||
						!code.writeValue(registers[op.source]) || !code.writeValue(typeRegisterSize(op.type))) return nullptr;
					break;
				}
				case LS_IR_OP_LOAD_INDEXED: {
					LsOpLoadIndexed& op = *static_cast<LsOpLoadIndexed*>(base);
					if (op.result >= function->next_value || op.index >= function->next_value || (!op.base_is_value && op.base != LS_IR_INVALID_VALUE) ||
						(op.base_is_value && op.base >= function->next_value) ||
						!code.op(op.base_is_value ? LS_OP_LOAD_INDEXED : LS_OP_LOAD_INDEXED_LOCAL_I32) || !code.writeValue(registers[op.result]) ||
						!code.writeValue(op.base_is_value ? registers[op.base] : op.base_offset) || !code.writeValue(registers[op.index]) ||
						!code.writeValue(op.scale) || !code.writeValue((i32)op.offset) ||
						(op.base_is_value ? !code.writeValue(typeRegisterSize(op.type)) : (!code.writeValue(op.length) || !code.writeValue(typeRegisterSize(op.type))))) return nullptr;
					break;
				}
				case LS_IR_OP_GLOBAL_REF: {
					LsOpGlobalRef& op = *static_cast<LsOpGlobalRef*>(base);
					if (op.result >= function->next_value || !code.op(LS_OP_GLOBAL_REF) || !code.writeValue(registers[op.result]) || !code.writeValue(op.offset)) return nullptr;
					break;
				}
				case LS_IR_OP_STORE_INDEXED: {
					LsOpStoreIndexed& op = *static_cast<LsOpStoreIndexed*>(base);
					if (op.source >= function->next_value || op.index >= function->next_value || (!op.base_is_value && op.base != LS_IR_INVALID_VALUE) ||
						(op.base_is_value && op.base >= function->next_value) ||
						!code.op(op.base_is_value ? LS_OP_STORE_INDEXED : LS_OP_STORE_INDEXED_LOCAL_I32) ||
						!code.writeValue(op.base_is_value ? registers[op.base] : op.base_offset) || !code.writeValue(registers[op.index]) ||
						!code.writeValue(registers[op.source]) || !code.writeValue(op.scale) || !code.writeValue((i32)op.offset) ||
						(op.base_is_value ? !code.writeValue(typeRegisterSize(op.type)) : (!code.writeValue(op.length) || !code.writeValue(typeRegisterSize(op.type))))) return nullptr;
					break;
				}
				case LS_IR_OP_MAKE_SLICE: {
					LsOpMakeSlice& op = *static_cast<LsOpMakeSlice*>(base);
					if (op.result >= function->next_value || (op.base != LS_IR_INVALID_VALUE && op.base >= function->next_value) ||
						!(op.base == LS_IR_INVALID_VALUE
							? code.op(LS_OP_LOCAL_REF) && code.writeValue(registers[op.result]) && code.writeValue(op.base_offset)
							: code.op(LS_OP_COPY) && code.writeValue(registers[op.result]) && code.writeValue(registers[op.base]) && code.writeValue((u32)sizeof(void*))) ||
						!code.op(LS_OP_LOAD_CONST_8) || !code.writeValue((u32)(registers[op.result] + sizeof(void*))) ||
						!code.writeValue(op.length)) return nullptr;
					break;
				}
				case LS_IR_OP_SLICE: {
					LsOpSlice& op = *static_cast<LsOpSlice*>(base);
					if (op.result >= function->next_value || op.begin >= function->next_value || op.end >= function->next_value ||
						!code.op(LS_OP_SLICE) || !code.writeValue(registers[op.result]) || !code.writeValue(registers[op.begin]) ||
						!code.writeValue(registers[op.end]) || !code.writeValue(op.type && op.type->kind == ResolvedType::SLICE ? typeByteSize(*static_cast<SliceResolvedType*>(op.type)->element_type) : 0)) return nullptr;
					break;
				}
				case LS_IR_OP_SLICE_LOAD: {
					LsOpSliceLoad& op = *static_cast<LsOpSliceLoad*>(base);
					if (op.result >= function->next_value || op.index >= function->next_value || op.slice_offset == LS_IR_INVALID_VALUE ||
						!code.op(LS_OP_SLICE_LOAD_LOCAL_I32) || !code.writeValue(registers[op.result]) || !code.writeValue(op.slice_offset) ||
						!code.writeValue(registers[op.index]) || !code.writeValue(op.element_size)) return nullptr;
					break;
				}
				case LS_IR_OP_SLICE_STORE: {
					LsOpSliceStore& op = *static_cast<LsOpSliceStore*>(base);
					if (op.source >= function->next_value || op.index >= function->next_value || op.slice_offset == LS_IR_INVALID_VALUE ||
						!code.op(LS_OP_SLICE_STORE_LOCAL_I32) || !code.writeValue(op.slice_offset) || !code.writeValue(registers[op.index]) ||
						!code.writeValue(registers[op.source]) || !code.writeValue(op.element_size)) return nullptr;
					break;
				}
				case LS_IR_OP_SLICE_LENGTH: {
					LsOpSliceLength& op = *static_cast<LsOpSliceLength*>(base);
					if (op.result >= function->next_value || op.slice_offset == LS_IR_INVALID_VALUE || !code.op(LS_OP_SLICE_LENGTH) ||
						!code.writeValue(registers[op.result]) || !code.writeValue(op.slice_is_value ? registers[op.slice_offset] : op.slice_offset)) return nullptr;
					break;
				}
				case LS_IR_OP_ADD:
				case LS_IR_OP_SUB:
				case LS_IR_OP_MUL:
				case LS_IR_OP_DIV:
				case LS_IR_OP_MOD: {
					LsOpAdd& op = *static_cast<LsOpAdd*>(base);
					const ls_op opcode = binaryOpcode(base->kind, numericTypeIndex(op.type));
					if (!opcode || op.result >= function->next_value || op.lhs >= function->next_value || op.rhs >= function->next_value ||
						!code.op(opcode) || !code.writeValue(registers[op.result]) || !code.writeValue(registers[op.lhs]) || !code.writeValue(registers[op.rhs])) return nullptr;
					break;
				}
				case LS_IR_OP_COMPARE: {
					LsOpCompare& op = *static_cast<LsOpCompare*>(base);
					const ls_op compare[] = {LS_OP_EQ, LS_OP_NE, LS_OP_LT, LS_OP_LE, LS_OP_GT, LS_OP_GE};
					if (op.result >= function->next_value || op.lhs >= function->next_value || op.rhs >= function->next_value ||
						!code.op(compare[op.op]) || !code.writeValue(registers[op.result]) || !code.writeValue(registers[op.lhs]) ||
						!code.writeValue(registers[op.rhs]) || !code.writeValue((u8)(op.type && op.type->kind == ResolvedType::ENUM ? LS_TYPE_ENUM : numericBytecodeTypeKind(op.type)))) return nullptr;
					break;
				}
				case LS_IR_OP_NEG: {
					LsOpNeg& op = *static_cast<LsOpNeg*>(base);
					const ls_op opcode = unaryOpcode(base->kind, numericTypeIndex(op.type));
					if (!opcode || op.result >= function->next_value || op.value >= function->next_value ||
						!code.op(LS_OP_COPY) || !code.writeValue(registers[op.result]) || !code.writeValue(registers[op.value]) ||
						!code.writeValue(typeRegisterSize(op.type)) || !code.op(opcode) || !code.writeValue(registers[op.result])) return nullptr;
					break;
				}
				case LS_IR_OP_SLICE_LOAD_AT: {
					LsOpSliceLoadAt& op = *static_cast<LsOpSliceLoadAt*>(base);
					const ls_op opcode = op.index_is_i32 ? LS_OP_SLICE_LOAD_AT_LOCAL_I32 : LS_OP_SLICE_LOAD_AT_LOCAL;
					if (op.result >= function->next_value || op.index >= function->next_value || !code.op(opcode) ||
						!code.writeValue(registers[op.result]) || !code.writeValue(op.slice) || !code.writeValue(registers[op.index]) ||
						!code.writeValue(op.element_size) || !code.writeValue((i32)op.element_offset) || !code.writeValue(typeRegisterSize(op.type))) return nullptr;
					break;
				}
				case LS_IR_OP_SLICE_EQ: {
					LsOpSliceEq& op = *static_cast<LsOpSliceEq*>(base);
					const ResolvedType* element = op.type;
					if (!element || op.result >= function->next_value || op.lhs >= function->next_value || op.rhs >= function->next_value || !code.op(LS_OP_SLICE_EQ) ||
						!code.writeValue(registers[op.result]) || !code.writeValue(registers[op.lhs]) || !code.writeValue(registers[op.rhs]) ||
						!code.writeValue(typeRegisterSize(element)) || !code.writeValue((u8)bytecodeTypeKind(element))) return nullptr;
					break;
				}
				case LS_IR_OP_SLICE_STORE_AT: {
					LsOpSliceStoreAt& op = *static_cast<LsOpSliceStoreAt*>(base);
					const ls_op opcode = op.index_is_i32 ? LS_OP_SLICE_STORE_AT_LOCAL_I32 : LS_OP_SLICE_STORE_AT_LOCAL;
					if (op.index >= function->next_value || op.source >= function->next_value || !code.op(opcode) ||
						!code.writeValue(op.slice) || !code.writeValue(registers[op.index]) || !code.writeValue(registers[op.source]) ||
						!code.writeValue(op.element_size) || !code.writeValue((i32)op.element_offset) || !code.writeValue(typeRegisterSize(op.type))) return nullptr;
					break;
				}
				case LS_IR_OP_NOT: {
					LsOpNot& op = *static_cast<LsOpNot*>(base);
					if (op.result >= function->next_value || op.value >= function->next_value ||
						!code.op(LS_OP_COPY) || !code.writeValue(registers[op.result]) || !code.writeValue(registers[op.value]) ||
						!code.writeValue(1u) || !code.op(LS_OP_NOT) || !code.writeValue(registers[op.result])) return nullptr;
					break;
				}
				case LS_IR_OP_CAST: {
					LsOpCast& op = *static_cast<LsOpCast*>(base);
					if (op.result >= function->next_value || op.value >= function->next_value || !code.op(LS_OP_CAST) ||
						!code.writeValue(registers[op.result]) || !code.writeValue(registers[op.value]) ||
						!code.writeValue((u8)bytecodeTypeKind(op.type)) || !code.writeValue((u8)bytecodeTypeKind(op.target_type))) return nullptr;
					break;
				}
				case LS_IR_OP_CALL_DIRECT:
				case LS_IR_OP_CALL_NATIVE: {
					LsOpCallDirect& op = *static_cast<LsOpCallDirect*>(base);
					if (op.result >= function->next_value || op.function == ~0u) return nullptr;
					u32 offset = 0;
					for (u32 i = 0; i < op.argument_count; ++i) {
						if (op.arguments[i] >= function->next_value) return nullptr;
						const u32 size = op.argument_sizes[i];
						if (!code.op(LS_OP_COPY) || !code.writeValue(registers[op.result] + offset) ||
							!code.writeValue(registers[op.arguments[i]]) || !code.writeValue(size)) return nullptr;
						offset += size;
					}
					if (!code.op(base->kind == LS_IR_OP_CALL_NATIVE ? LS_OP_CALL_NATIVE : LS_OP_CALL_DIRECT) || !code.writeValue(op.function) || !code.writeValue(registers[op.result])) return nullptr;
					break;
				}
				case LS_IR_OP_CALL_INDIRECT: {
					LsOpCallIndirect& op = *static_cast<LsOpCallIndirect*>(base);
					if (op.result >= function->next_value || op.function >= function->next_value) return nullptr;
					if (!code.op(LS_OP_COPY) || !code.writeValue(registers[op.result]) || !code.writeValue(registers[op.function]) || !code.writeValue(4u)) return nullptr;
					u32 offset = 4;
					for (u32 i = 0; i < op.argument_count; ++i) {
						if (op.arguments[i] >= function->next_value || !code.op(LS_OP_COPY) || !code.writeValue(registers[op.result] + offset) ||
							!code.writeValue(registers[op.arguments[i]]) || !code.writeValue(op.argument_sizes[i])) return nullptr;
						offset += op.argument_sizes[i];
					}
					if (!code.op(LS_OP_CALL_INDIRECT) || !code.writeValue(registers[op.result]) || !code.writeValue(op.argument_size) || !code.writeValue(op.result_size)) return nullptr;
					break;
				}
				default:
					return nullptr;
			}
		}
		if (!block.terminator) return nullptr;
		if (block.terminator && block.terminator->src_loc != LS_IR_INVALID_SOURCE_LOC && (source_map.empty() || source_map[source_map.size() - 1].line != block.terminator->src_loc)) {
			ls_bytecode_source_map_entry& entry = source_map.emplace_back();
			entry.code_offset = code.size;
			entry.source_name = function->source ? function->source->token.source_name : ls_string_view{};
			entry.line = block.terminator->src_loc;
			entry.column = 0;
		}
		switch (block.terminator->kind) {
			case LS_IR_OP_JUMP: {
				LsOpJump& op = *static_cast<LsOpJump*>(block.terminator);
				if (!code.op(LS_OP_JUMP)) return nullptr;
				JumpPatch& patch = patches.emplace_back();
				patch.operand = code.size;
				patch.target = op.target;
				if (!code.writeValue((i16)0)) return nullptr;
				break;
			}
			case LS_IR_OP_CONDITIONAL_JUMP: {
				LsOpConditionalJump& op = *static_cast<LsOpConditionalJump*>(block.terminator);
				if (op.condition >= function->next_value || !code.op(LS_OP_JNZ_U8) || !code.writeValue(registers[op.condition])) return nullptr;
				JumpPatch& patch = patches.emplace_back();
				patch.operand = code.size;
				patch.target = op.target;
				if (!code.writeValue((i16)0)) return nullptr;
				break;
			}
			case LS_IR_OP_RETURN: {
				LsOpReturn& op = *static_cast<LsOpReturn*>(block.terminator);
				if (op.value == LS_IR_INVALID_VALUE) {
					if (!code.op(LS_OP_RETURN_BASE)) return nullptr;
				} else if (op.value >= function->next_value || !code.op(LS_OP_RETURN) || !code.writeValue(registers[op.value]) || !code.writeValue(op.result_size)) return nullptr;
				break;
			}
			default:
				return nullptr;
		}
	}
	for (JumpPatch& patch : patches) {
		if (patch.target >= (LsIrBlock)block_offsets.size()) return nullptr;
		const i32 offset = (i32)block_offsets[patch.target] - (i32)(patch.operand + sizeof(i16));
		if (offset < -32768 || offset > 32767) return nullptr;
		const i16 encoded = (i16)offset;
		copyMemory(code.data + patch.operand, &encoded, sizeof(encoded));
	}

	ls_bytecode* bytecode = (ls_bytecode*)calloc(1, sizeof(ls_bytecode));
	if (!bytecode) return nullptr;
	bytecode->host = host;
	bytecode->arena = &host->arena;
	bytecode->function_count = 1;
	bytecode->function_capacity = 1;
	bytecode->functions = (ls_function_bc*)host->arena.allocate(host->arena.user_data, sizeof(ls_function_bc), alignof(ls_function_bc));
	if (!bytecode->functions) { free(bytecode); return nullptr; }
	ls_function_bc& output = bytecode->functions[0];
	output.name = function->name;
	output.kind = LS_FUNCTION_SCRIPT;
	output.param_size = function->param_size;
	output.return_size = function->return_type ? typeByteSize(*function->return_type) : 0;
	output.return_kind = function->return_type ? bytecodeTypeKind(function->return_type) : LS_TYPE_VOID;
	output.frame_size = frame_size;
	output.code = code.data;
	output.code_size = code.size;
	IrDebugTypes debug_types(host->arena, bytecode);
	if (function->source) {
		for (FunctionParam& parameter : function->source->params) {
			if (!parameter.is_comptime) irDebugLocal(*bytecode, 0, debug_types, parameter.name, parameter.slot, 0);
		}
		if (function->source->body->kind == Statement::BLOCK) {
			BlockStatement& body = *static_cast<BlockStatement*>(function->source->body);
			for (Statement* statement : body.statements) if (statement && statement->kind == Statement::VAR_DECL) {
				VarDeclStatement& declaration = *static_cast<VarDeclStatement*>(statement);
				if (!declaration.is_comptime) irDebugLocal(*bytecode, 0, debug_types, declaration.name, declaration.slot, irDebugScopeBegin(output, (u32)declaration.token.line));
			}
			if (!source_map.empty()) {
				output.source_map_count = (u32)source_map.size();
				output.source_map = (ls_bytecode_source_map_entry*)host->arena.allocate(host->arena.user_data,
					sizeof(ls_bytecode_source_map_entry) * source_map.size(), alignof(ls_bytecode_source_map_entry));
				if (!output.source_map) { ls_bytecode_destroy(bytecode); return nullptr; }
				copyMemory(output.source_map, source_map.data(), sizeof(ls_bytecode_source_map_entry) * source_map.size());
			} else if (!body.statements.empty()) {
				Statement* statement = body.statements[(i32)body.statements.size() - 1];
				ls_bytecode_source_map_entry* map = (ls_bytecode_source_map_entry*)host->arena.allocate(host->arena.user_data, sizeof(ls_bytecode_source_map_entry), alignof(ls_bytecode_source_map_entry));
				if (map) {
					map->code_offset = 0;
					map->source_name = statement->token.source_name;
					map->line = (u32)statement->token.line;
					map->column = (u32)statement->token.column;
					output.source_map = map;
					output.source_map_count = 1;
				}
			}
		}
	}
	return bytecode;
}

ls_bytecode* lsIrCompileModule(LsIrModuleData* module, ls_host* host) {
	if (!module || !host || !host->arena.allocate || module->functions.empty()) return nullptr;
	ls_bytecode* bytecode = (ls_bytecode*)calloc(1, sizeof(ls_bytecode));
	if (!bytecode) return nullptr;
	bytecode->host = host;
	bytecode->arena = &host->arena;
	bytecode->global_size = module->global_size;
	bytecode->has_global_init = true;
	bytecode->function_count = (u32)module->functions.size();
	bytecode->function_capacity = bytecode->function_count;
	bytecode->functions = (ls_function_bc*)host->arena.allocate(host->arena.user_data,
		sizeof(ls_function_bc) * bytecode->function_count, alignof(ls_function_bc));
	if (!bytecode->functions) { free(bytecode); return nullptr; }
	for (u32 i = 0; i < bytecode->function_count; ++i) {
		LsIrModuleEntry& entry = module->functions[(i32)i];
		if (entry.native) {
			ls_function_bc& output = bytecode->functions[i];
			output.name = entry.name;
			output.kind = LS_FUNCTION_NATIVE;
			output.is_builtin_native = entry.builtin_native;
			output.return_kind = LS_TYPE_VOID;
			if (entry.source && entry.source->resolved_type && entry.source->resolved_type->kind == ResolvedType::FUNCTION) {
				FunctionResolvedType* type = static_cast<FunctionResolvedType*>(entry.source->resolved_type);
				output.return_kind = type->return_type ? bytecodeTypeKind(type->return_type) : LS_TYPE_VOID;
				output.return_size = type->return_type ? typeByteSize(*type->return_type) : 0;
				for (FunctionResolvedParam& parameter : type->params)
					if (!parameter.is_comptime && parameter.type) output.param_size += typeByteSize(*parameter.type);
			}
		} else {
			ls_bytecode* single = lsIrCompileFunction(entry.function, host);
			if (!single) {
				free(bytecode);
				return nullptr;
			}
			bytecode->functions[i] = single->functions[0];
			free(single);
		}
	}
	IrDebugTypes debug_types(host->arena, bytecode);
	ExpArray<ls_bytecode_global_debug_entry> globals(host->arena);
	for (Unit& unit : module->source->units) {
		for (Symbol& symbol : unit.symbols) {
			if (!symbolHasGlobalStorage(symbol) || !symbol.expression || !symbol.resolved_type) continue;
			ls_bytecode_global_debug_entry& entry = globals.emplace_back();
			entry.name = symbol.name;
			entry.offset = symbol.slot.offset;
			entry.byte_size = symbol.slot.byte_size;
			entry.kind = bytecodeTypeKind(symbol.resolved_type);
			entry.type_index = debug_types.resolve(symbol.resolved_type);
		}
	}
	if (!globals.empty()) {
		bytecode->global_debug_count = (u32)globals.size();
		bytecode->global_debug = (ls_bytecode_global_debug_entry*)host->arena.allocate(host->arena.user_data,
			sizeof(ls_bytecode_global_debug_entry) * globals.size(), alignof(ls_bytecode_global_debug_entry));
		if (!bytecode->global_debug) return nullptr;
		copyMemory(bytecode->global_debug, globals.data(), sizeof(ls_bytecode_global_debug_entry) * globals.size());
	}
	for (u32 i = 0; i < module->functions.size(); ++i) {
		LsIrModuleEntry& entry = module->functions[(i32)i];
		if (!entry.source || entry.native || !entry.source->body || entry.source->body->kind != Statement::BLOCK) continue;
		bytecode->functions[i].locals = nullptr;
		bytecode->functions[i].local_count = 0;
		for (FunctionParam& parameter : entry.source->params)
			if (!parameter.is_comptime) irDebugLocal(*bytecode, i, debug_types, parameter.name, parameter.slot, 0);
		BlockStatement& body = *static_cast<BlockStatement*>(entry.source->body);
		for (Statement* statement : body.statements) if (statement && statement->kind == Statement::VAR_DECL) {
			VarDeclStatement& declaration = *static_cast<VarDeclStatement*>(statement);
			if (!declaration.is_comptime) {
				declaration.slot.type = declaration.resolved_type;
				declaration.slot.byte_size = typeRegisterSize(declaration.resolved_type);
				irDebugLocal(*bytecode, i, debug_types, declaration.name, declaration.slot, irDebugScopeBegin(bytecode->functions[i], (u32)declaration.token.line));
			}
		}
		if (bytecode->functions[i].source_map_count == 0 && !body.statements.empty()) {
			Statement* statement = body.statements[(i32)body.statements.size() - 1];
			ls_bytecode_source_map_entry* map = (ls_bytecode_source_map_entry*)host->arena.allocate(host->arena.user_data, sizeof(ls_bytecode_source_map_entry), alignof(ls_bytecode_source_map_entry));
			if (map) {
				map->code_offset = 0;
				map->source_name = statement->token.source_name;
				map->line = (u32)statement->token.line;
				map->column = (u32)statement->token.column;
				bytecode->functions[i].source_map = map;
				bytecode->functions[i].source_map_count = 1;
			}
		}
	}
	return bytecode;
}
