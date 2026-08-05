#include "mir_builder.h"

struct MirSlotBinding {
	StorageSlot* slot;
	MirLocalId local;
};

struct MirLoopBinding {
	ls_string_view label;
	MirBlockId header;
	MirBlockId continue_target;
	MirBlockId exit;
};

struct MirBuilder {
	ls_arena& arena;
	MirFunction& function;
	MirBlock* block;
	ExpArray<MirSlotBinding> slots;
	ExpArray<MirLoopBinding> loops;
	ExpArray<Statement*> deferreds;
	ExpArray<u32> defer_marks;
	ls_string_view loop_label;

	MirBuilder(ls_arena& arena, MirFunction& function)
		: arena(arena), function(function), slots(arena), loops(arena), deferreds(arena), defer_marks(arena) {
		loop_label = {};
		block = mirFunctionCreateBlock(function);
	}
};

static MirInstruction* mirAppend(MirBuilder& builder, MirOpcode opcode, ResolvedType* type, u32 operand_count) {
	MirInstruction& instruction = builder.block->instructions.emplace_back();
	instruction.opcode = opcode;
	instruction.type = type;
	instruction.operand_type = nullptr;
	instruction.result = MIR_INVALID_ID;
	instruction.source_location = MIR_INVALID_ID;
	instruction.operand_count = operand_count;
	instruction.immediate = 0;
	instruction.offset = 0;
	instruction.integer = 0;
	instruction.floating = 0;
	instruction.local = MIR_INVALID_ID;
	instruction.function = MIR_INVALID_ID;
	instruction.call_target = MIR_CALL_DIRECT;
	instruction.call_name = {};
	instruction.string = {};
	instruction.arguments.values = nullptr;
	instruction.arguments.sizes = nullptr;
	instruction.arguments.count = 0;
	for (u32 i = 0; i < 3; ++i) instruction.operands[i] = MIR_INVALID_ID;
	if (opcode != MIR_OP_STORE && opcode != MIR_OP_COPY) instruction.result = mirFunctionNewValue(builder.function);
	return &instruction;
}

static MirInstruction* mirAppendI32Zero(MirBuilder& builder, ResolvedType* type) {
	MirInstruction* instruction = mirAppend(builder, MIR_OP_CONST, type, 0);
	instruction->immediate = MIR_CONST_I32;
	return instruction;
}

static MirOpcode mirBinaryOpcode(Token::Type op) {
	switch (op) {
		case Token::PLUS: return MIR_OP_ADD;
		case Token::MINUS: return MIR_OP_SUB;
		case Token::STAR: return MIR_OP_MUL;
		case Token::SLASH: return MIR_OP_DIV;
		case Token::PERCENT: return MIR_OP_MOD;
		case Token::EQUAL_EQUAL:
		case Token::BANG_EQUAL:
		case Token::GT:
		case Token::LT:
		case Token::GT_EQUAL:
		case Token::LT_EQUAL: return MIR_OP_COMPARE;
		default: return MIR_OP_UNDEFINED;
	}
}

static MirLocalId mirFindSlot(MirBuilder& builder, StorageSlot* slot) {
	for (MirSlotBinding& binding : builder.slots) {
		if (binding.slot == slot) return binding.local;
	}
	return MIR_INVALID_ID;
}

static MirValueId mirBuildExpression(MirBuilder& builder, Expression* expression);
static void mirBuildStatement(MirBuilder& builder, Statement* statement);
static MirValueId mirBuildAddress(MirBuilder& builder, Expression* expression);
static bool mirFindFieldOffset(MemberExpression& member, u32& offset);

static ResolvedType* mirStructFieldType(StructResolvedType* structure, u32 index) {
	if (!structure) return nullptr;
	if (index < (u32)structure->field_types.size()) return structure->field_types[index];
	if (structure->decl && index < (u32)structure->decl->fields.size()) return structure->decl->fields[index].resolved_type;
	return nullptr;
}

static u32 mirArrayElementCount(ResolvedType* type) {
	if (!type || type->kind != ResolvedType::ARRAY) return 1;
	ArrayResolvedType* array = static_cast<ArrayResolvedType*>(type);
	return (u32)array->size * mirArrayElementCount(array->element_type);
}

static bool mirBuildArrayAccess(MirBuilder& builder, Expression* expression, MirValueId& base, MirValueId& index, ResolvedType*& element_type, u32& extent) {
	if (!expression || expression->kind != Expression::BRACKET) return false;
	BracketExpression& bracket = *static_cast<BracketExpression*>(expression);
	if (bracket.args.size() != 1 || !bracket.base || !bracket.base->resolved_type || bracket.base->resolved_type->kind != ResolvedType::ARRAY) return false;
	ArrayResolvedType* array = static_cast<ArrayResolvedType*>(bracket.base->resolved_type);
	MirValueId current_index = mirBuildExpression(builder, bracket.args[0]);
	if (bracket.base->kind == Expression::BRACKET) {
		if (!mirBuildArrayAccess(builder, bracket.base, base, index, element_type, extent)) return false;
		MirInstruction* stride = mirAppend(builder, MIR_OP_CONST, bracket.args[0]->resolved_type, 0);
		stride->integer = mirArrayElementCount(array->element_type);
		MirInstruction* scaled = mirAppend(builder, MIR_OP_MUL, bracket.args[0]->resolved_type, 2);
		scaled->operands[0] = index;
		scaled->operands[1] = stride->result;
		MirInstruction* combined = mirAppend(builder, MIR_OP_ADD, bracket.args[0]->resolved_type, 2);
		combined->operands[0] = scaled->result;
		combined->operands[1] = current_index;
		index = combined->result;
		extent *= (u32)array->size;
	}
	else {
		base = mirBuildAddress(builder, bracket.base);
		index = current_index;
		extent = (u32)array->size;
	}
	element_type = bracket.resolved_type;
	return base != MIR_INVALID_ID && index != MIR_INVALID_ID;
}

static void mirEmitActiveDefers(MirBuilder& builder) {
	for (i32 i = (i32)builder.deferreds.size() - 1; i >= 0; --i)
		mirBuildStatement(builder, builder.deferreds[(u32)i]);
}

static MirValueId mirBuildExpressionAsType(MirBuilder& builder, Expression* expression, ResolvedType* type) {
	if (!expression || !type || expression->resolved_type == type) return mirBuildExpression(builder, expression);
	if (expression->resolved_type && expression->resolved_type->kind == ResolvedType::ARRAY && type->kind == ResolvedType::SLICE && expression->kind == Expression::IDENTIFIER) {
		StorageSlot* slot = static_cast<IdentifierExpression*>(expression)->slot;
		MirLocalId local = mirFindSlot(builder, slot);
		if (local != MIR_INVALID_ID) {
			ArrayResolvedType* array = static_cast<ArrayResolvedType*>(expression->resolved_type);
			MirInstruction* address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, expression->resolved_type, 0);
			address->local = local;
			MirInstruction* make = mirAppend(builder, MIR_OP_MAKE_SLICE, type, 1);
			make->operands[0] = address->result;
			make->local = array->element_type ? typeByteSize(*array->element_type) : 0;
			make->function = (u32)array->size;
			return make->result;
		}
	}
	if (expression->kind == Expression::INT_LITERAL || expression->kind == Expression::FLOAT_LITERAL || expression->kind == Expression::BOOL_LITERAL) {
		MirInstruction* instruction = mirAppend(builder, MIR_OP_CONST, type, 0);
		if (expression->kind == Expression::INT_LITERAL) {
			const u64 value = static_cast<IntLiteralExpression*>(expression)->value;
			if (type->kind == ResolvedType::F32 || type->kind == ResolvedType::F64) instruction->floating = (f64)value;
			else instruction->integer = (i64)value;
		}
		else if (expression->kind == Expression::FLOAT_LITERAL) instruction->floating = static_cast<FloatLiteralExpression*>(expression)->value;
		else instruction->integer = static_cast<BoolLiteralExpression*>(expression)->value ? 1 : 0;
		return instruction->result;
	}
	return mirBuildExpression(builder, expression);
}

static MirValueId mirBuildLogical(MirBuilder& builder, BinaryExpression& binary) {
	MirValueId lhs = mirBuildExpression(builder, binary.lhs);
	MirLocalId result_local = mirFunctionAddLocal(builder.function, binary.resolved_type, {}, false, true);
	MirBlock* rhs_block = mirFunctionCreateBlock(builder.function);
	MirBlock* short_block = mirFunctionCreateBlock(builder.function);
	MirBlock* merge_block = mirFunctionCreateBlock(builder.function);
	const bool is_and = binary.op == Token::AND;
	builder.block->terminator.kind = MIR_TERM_BRANCH;
	builder.block->terminator.value = lhs;
	builder.block->terminator.targets[0] = is_and ? rhs_block->id : short_block->id;
	builder.block->terminator.targets[1] = is_and ? short_block->id : rhs_block->id;
	builder.block->has_terminator = true;

	builder.block = short_block;
	MirInstruction* short_value = mirAppend(builder, MIR_OP_CONST, binary.resolved_type, 0);
	short_value->integer = is_and ? 0 : 1;
	MirInstruction* short_address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, binary.resolved_type, 0);
	short_address->local = result_local;
	MirInstruction* short_store = mirAppend(builder, MIR_OP_STORE, binary.resolved_type, 2);
	short_store->operands[0] = short_address->result;
	short_store->operands[1] = short_value->result;
	short_block->terminator.kind = MIR_TERM_JUMP;
	short_block->terminator.targets[0] = merge_block->id;
	short_block->has_terminator = true;

	builder.block = rhs_block;
	MirValueId rhs = mirBuildExpression(builder, binary.rhs);
	MirInstruction* rhs_address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, binary.resolved_type, 0);
	rhs_address->local = result_local;
	MirInstruction* rhs_store = mirAppend(builder, MIR_OP_STORE, binary.resolved_type, 2);
	rhs_store->operands[0] = rhs_address->result;
	rhs_store->operands[1] = rhs;
	rhs_block->terminator.kind = MIR_TERM_JUMP;
	rhs_block->terminator.targets[0] = merge_block->id;
	rhs_block->has_terminator = true;

	builder.block = merge_block;
	MirInstruction* result_address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, binary.resolved_type, 0);
	result_address->local = result_local;
	MirInstruction* result = mirAppend(builder, MIR_OP_LOAD, binary.resolved_type, 1);
	result->operands[0] = result_address->result;
	return result->result;
}

static MirValueId mirBuildTernary(MirBuilder& builder, TernaryExpression& ternary) {
	MirValueId condition = mirBuildExpression(builder, ternary.condition);
	MirLocalId result_local = mirFunctionAddLocal(builder.function, ternary.resolved_type, {}, false, true);
	MirBlock* true_block = mirFunctionCreateBlock(builder.function);
	MirBlock* false_block = mirFunctionCreateBlock(builder.function);
	MirBlock* merge_block = mirFunctionCreateBlock(builder.function);
	builder.block->terminator.kind = MIR_TERM_BRANCH;
	builder.block->terminator.value = condition;
	builder.block->terminator.targets[0] = true_block->id;
	builder.block->terminator.targets[1] = false_block->id;
	builder.block->has_terminator = true;

	builder.block = true_block;
	MirValueId true_value = mirBuildExpression(builder, ternary.true_expr);
	MirInstruction* true_address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, ternary.resolved_type, 0);
	true_address->local = result_local;
	MirInstruction* true_store = mirAppend(builder, MIR_OP_STORE, ternary.resolved_type, 2);
	true_store->operands[0] = true_address->result;
	true_store->operands[1] = true_value;
	true_block->terminator.kind = MIR_TERM_JUMP;
	true_block->terminator.targets[0] = merge_block->id;
	true_block->has_terminator = true;

	builder.block = false_block;
	MirValueId false_value = mirBuildExpression(builder, ternary.false_expr);
	MirInstruction* false_address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, ternary.resolved_type, 0);
	false_address->local = result_local;
	MirInstruction* false_store = mirAppend(builder, MIR_OP_STORE, ternary.resolved_type, 2);
	false_store->operands[0] = false_address->result;
	false_store->operands[1] = false_value;
	false_block->terminator.kind = MIR_TERM_JUMP;
	false_block->terminator.targets[0] = merge_block->id;
	false_block->has_terminator = true;

	builder.block = merge_block;
	MirInstruction* result_address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, ternary.resolved_type, 0);
	result_address->local = result_local;
	MirInstruction* result = mirAppend(builder, MIR_OP_LOAD, ternary.resolved_type, 1);
	result->operands[0] = result_address->result;
	return result->result;
}

static MirValueId mirBuildAddress(MirBuilder& builder, Expression* expression) {
	if (!expression) return MIR_INVALID_ID;
	if (expression->kind == Expression::IDENTIFIER) {
		IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(expression);
		if (identifier.slot && identifier.slot->storage == StorageSlot::GLOBAL) {
			if (identifier.slot->type && identifier.slot->type->kind == ResolvedType::NULLABLE) return MIR_INVALID_ID;
			MirInstruction* address = mirAppend(builder, MIR_OP_GLOBAL_ADDRESS, expression->resolved_type, 0);
			address->immediate = identifier.slot->offset;
			return address->result;
		}
		MirLocalId local = identifier.slot ? mirFindSlot(builder, identifier.slot) : MIR_INVALID_ID;
		if (local == MIR_INVALID_ID) return MIR_INVALID_ID;
		MirInstruction* address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, expression->resolved_type, 0);
		address->local = local;
		return address->result;
	}
	if (expression->kind == Expression::DEREFERENCE) {
		DereferenceExpression& dereference = *static_cast<DereferenceExpression*>(expression);
		return mirBuildExpression(builder, dereference.subject);
	}
	if (expression->kind == Expression::MEMBER) {
		u32 total_offset = 0;
		Expression* root = expression;
		while (root && root->kind == Expression::MEMBER) {
			MemberExpression& member = *static_cast<MemberExpression*>(root);
			u32 field_offset = 0;
			if (!mirFindFieldOffset(member, field_offset)) return MIR_INVALID_ID;
			total_offset += field_offset;
			root = member.expression;
		}
		MirInstruction* address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, expression->resolved_type, 0);
		MirLocalId local = MIR_INVALID_ID;
		if (root && root->kind == Expression::IDENTIFIER) {
			IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(root);
			local = identifier.slot ? mirFindSlot(builder, identifier.slot) : MIR_INVALID_ID;
		}
		if (local == MIR_INVALID_ID) return MIR_INVALID_ID;
		address->local = local;
		address->offset = total_offset;
		return address->result;
	}
	return MIR_INVALID_ID;
}

static bool mirFindFieldOffset(MemberExpression& member, u32& offset) {
	if (!member.expression || !member.expression->resolved_type || member.expression->resolved_type->kind != ResolvedType::STRUCT) return false;
	StructResolvedType* structure = static_cast<StructResolvedType*>(member.expression->resolved_type);
	if (!structure->decl) return false;
	offset = 0;
	for (u32 i = 0; i < (u32)structure->decl->fields.size(); ++i) {
		if (equalStrings(structure->decl->fields[i].name, member.name)) return true;
		ResolvedType* field_type = mirStructFieldType(structure, i);
		if (!field_type) return false;
		offset += typeByteSize(*field_type);
	}
	return false;
}

static ResolvedType* mirIndexType(ResolvedType* type) {
	while (type && (type->kind == ResolvedType::ARRAY || type->kind == ResolvedType::SLICE))
		type = type->kind == ResolvedType::ARRAY ? static_cast<ArrayResolvedType*>(type)->element_type : static_cast<SliceResolvedType*>(type)->element_type;
	return type;
}

static MirValueId mirBuildIdentifier(MirBuilder& builder, IdentifierExpression& expression) {
	if (!expression.slot) {
		FunctionExpression* function = expression.resolved_fn;
		if (!function && expression.symbol && expression.symbol->expression && expression.symbol->expression->kind == Expression::FUNCTION)
			function = static_cast<FunctionExpression*>(expression.symbol->expression);
		if (!function) return MIR_INVALID_ID;
		MirInstruction* value = mirAppend(builder, MIR_OP_CONST, expression.resolved_type, 0);
		value->integer = function->bytecode_index;
		return value->result;
	}
	if (expression.slot->storage == StorageSlot::GLOBAL) {
		if (expression.slot->type && expression.slot->type->kind == ResolvedType::NULLABLE) return MIR_INVALID_ID;
		MirInstruction* address = mirAppend(builder, MIR_OP_GLOBAL_ADDRESS, expression.resolved_type, 0);
		address->immediate = expression.slot->offset;
		MirInstruction* load = mirAppend(builder, MIR_OP_LOAD, expression.resolved_type, 1);
		load->operands[0] = address->result;
		return load->result;
	}
	MirLocalId array_local = mirFindSlot(builder, expression.slot);
	ResolvedType* storage_type = array_local != MIR_INVALID_ID ? builder.function.locals[array_local].type : expression.slot->type;
	if (storage_type && storage_type->kind == ResolvedType::ARRAY && expression.resolved_type && expression.resolved_type->kind == ResolvedType::SLICE) {
		ArrayResolvedType* array = static_cast<ArrayResolvedType*>(storage_type);
		MirInstruction* address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, storage_type, 0);
		address->local = array_local;
		MirInstruction* make = mirAppend(builder, MIR_OP_MAKE_SLICE, expression.resolved_type, 1);
		make->operands[0] = address->result;
		make->local = array->element_type ? typeByteSize(*array->element_type) : 0;
		make->function = (u32)array->size;
		return make->result;
	}
	MirLocalId nullable_local = mirFindSlot(builder, expression.slot);
	ResolvedType* nullable_type = nullable_local != MIR_INVALID_ID ? builder.function.locals[nullable_local].type : expression.slot->type;
	if (nullable_type && nullable_type->kind == ResolvedType::NULLABLE && expression.resolved_type && expression.resolved_type->kind != ResolvedType::NULLABLE) {
		MirLocalId local = nullable_local;
		ResolvedType* local_type = nullable_type;
		MirInstruction* address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, local_type, 0);
		address->local = local;
		MirInstruction* index = mirAppendI32Zero(builder, expression.resolved_type);
		MirInstruction* load = mirAppend(builder, MIR_OP_LOAD, expression.resolved_type, 2);
		load->operands[0] = address->result;
		load->operands[1] = index->result;
		load->immediate = 1;
		load->offset = 1;
		load->local = 1;
		load->function = 1;
		return load->result;
	}
	MirLocalId local = mirFindSlot(builder, expression.slot);
	if (local == MIR_INVALID_ID) return MIR_INVALID_ID;
	MirInstruction* address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, expression.resolved_type, 0);
	address->local = local;
	MirInstruction* load = mirAppend(builder, MIR_OP_LOAD, expression.resolved_type, 1);
	load->operands[0] = address->result;
	return load->result;
}

static MirValueId mirBuildExpression(MirBuilder& builder, Expression* expression) {
	if (!expression) return MIR_INVALID_ID;
	switch (expression->kind) {
		case Expression::TERNARY:
			return mirBuildTernary(builder, *static_cast<TernaryExpression*>(expression));
		case Expression::INT_LITERAL: {
			MirInstruction* instruction = mirAppend(builder, MIR_OP_CONST, expression->resolved_type, 0);
			const u64 value = static_cast<IntLiteralExpression*>(expression)->value;
			if (expression->resolved_type && (expression->resolved_type->kind == ResolvedType::F32 || expression->resolved_type->kind == ResolvedType::F64))
				instruction->floating = (f64)value;
			else instruction->integer = (i64)value;
			return instruction->result;
		}
		case Expression::FLOAT_LITERAL: {
			MirInstruction* instruction = mirAppend(builder, MIR_OP_CONST, expression->resolved_type, 0);
			instruction->floating = static_cast<FloatLiteralExpression*>(expression)->value;
			return instruction->result;
		}
		case Expression::BOOL_LITERAL: {
			MirInstruction* instruction = mirAppend(builder, MIR_OP_CONST, expression->resolved_type, 0);
			instruction->integer = static_cast<BoolLiteralExpression*>(expression)->value ? 1 : 0;
			return instruction->result;
		}
		case Expression::STRING_LITERAL: {
			MirInstruction* instruction = mirAppend(builder, MIR_OP_CONST, expression->resolved_type, 0);
			instruction->string = static_cast<StringLiteralExpression*>(expression)->value;
			return instruction->result;
		}
		case Expression::UNDEFINED: {
			MirInstruction* instruction = mirAppend(builder, MIR_OP_UNDEFINED, expression->resolved_type, 0);
			return instruction->result;
		}
		case Expression::NULL_LITERAL: {
			MirInstruction* instruction = mirAppend(builder, MIR_OP_CONST, expression->resolved_type, 0);
			return instruction->result;
		}
		case Expression::IDENTIFIER:
			return mirBuildIdentifier(builder, *static_cast<IdentifierExpression*>(expression));
		case Expression::STRUCT_LITERAL: {
			StructLiteralExpression& literal = *static_cast<StructLiteralExpression*>(expression);
			if (!expression->resolved_type || expression->resolved_type->kind != ResolvedType::STRUCT) return MIR_INVALID_ID;
			StructResolvedType* structure = static_cast<StructResolvedType*>(expression->resolved_type);
			MirLocalId local = mirFunctionAddLocal(builder.function, expression->resolved_type, {}, false, true);
			MirInstruction* address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, expression->resolved_type, 0);
			address->local = local;
			u32 offset = 0;
			for (u32 i = 0; i < (u32)literal.values.size(); ++i) {
				ResolvedType* field_type = mirStructFieldType(structure, i);
				if (!field_type) return MIR_INVALID_ID;
				ResolvedType* index_type = field_type;
				while (index_type && index_type->kind == ResolvedType::ARRAY) index_type = static_cast<ArrayResolvedType*>(index_type)->element_type;
				MirInstruction* index = mirAppendI32Zero(builder, index_type);
				MirValueId value = mirBuildExpression(builder, literal.values[i]);
				MirInstruction* store = mirAppend(builder, MIR_OP_STORE, field_type, 3);
				store->operands[0] = address->result;
				store->operands[1] = index->result;
				store->operands[2] = value;
				store->immediate = 1;
				store->local = typeByteSize(*field_type);
				store->offset = offset;
				store->function = 1;
				offset += typeByteSize(*field_type);
			}
			MirInstruction* result = mirAppend(builder, MIR_OP_LOAD, expression->resolved_type, 1);
			result->operands[0] = address->result;
			return result->result;
		}
		case Expression::BRACKET: {
			BracketExpression& bracket = *static_cast<BracketExpression*>(expression);
		MirValueId base = MIR_INVALID_ID;
		MirValueId index = MIR_INVALID_ID;
		ResolvedType* element_type = nullptr;
		u32 extent = 0;
		u32 field_offset = 0;
		if (bracket.base && bracket.base->kind == Expression::MEMBER)
			mirFindFieldOffset(*static_cast<MemberExpression*>(bracket.base), field_offset);
		if (!mirBuildArrayAccess(builder, expression, base, index, element_type, extent)) {
			if (!bracket.base || bracket.base->resolved_type->kind != ResolvedType::SLICE || bracket.args.size() != 1) return MIR_INVALID_ID;
			SliceResolvedType* slice = static_cast<SliceResolvedType*>(bracket.base->resolved_type);
			base = mirBuildExpression(builder, bracket.base);
			index = mirBuildExpression(builder, bracket.args[0]);
			element_type = slice->element_type;
			MirInstruction* load = mirAppend(builder, MIR_OP_LOAD, expression->resolved_type, 2);
			load->operands[0] = base;
			load->operands[1] = index;
			load->immediate = 3;
			load->local = element_type ? typeByteSize(*element_type) : 0;
			return load->result;
		}
			MirInstruction* load = mirAppend(builder, MIR_OP_LOAD, expression->resolved_type, 2);
			load->operands[0] = base;
			load->operands[1] = index;
			load->immediate = 1;
			load->local = element_type ? typeByteSize(*element_type) : 0;
			load->offset = field_offset;
			load->function = extent;
			return load->result;
		}
		case Expression::SLICE: {
			SliceExpression& slice = *static_cast<SliceExpression*>(expression);
			if (!slice.base || !slice.base->resolved_type) return MIR_INVALID_ID;
			ResolvedType* element_type = nullptr;
			MirValueId base_slice = MIR_INVALID_ID;
			u32 length = 0;
			bool array_element_view = false;
			if (slice.base->resolved_type->kind == ResolvedType::ARRAY) {
				ArrayResolvedType* array = static_cast<ArrayResolvedType*>(slice.base->resolved_type);
				element_type = array->element_type;
				length = (u32)array->size;
				MirInstruction* full = mirAppend(builder, MIR_OP_MAKE_SLICE, expression->resolved_type, 1);
				full->operands[0] = mirBuildAddress(builder, slice.base);
				full->local = element_type ? typeByteSize(*element_type) : 0;
				full->function = length;
				base_slice = full->result;
			}
			else if (slice.base->resolved_type->kind == ResolvedType::SLICE) {
				SliceResolvedType* source = static_cast<SliceResolvedType*>(slice.base->resolved_type);
				element_type = source->element_type;
				base_slice = mirBuildExpression(builder, slice.base);
			}
			else {
				if (slice.base->kind == Expression::BRACKET) {
					BracketExpression& bracket = *static_cast<BracketExpression*>(slice.base);
					if (bracket.base && bracket.base->resolved_type && bracket.base->resolved_type->kind == ResolvedType::ARRAY && bracket.args.size() == 1) {
						ArrayResolvedType* array = static_cast<ArrayResolvedType*>(bracket.base->resolved_type);
						element_type = array->element_type;
						length = (u32)array->size;
						MirInstruction* full = mirAppend(builder, MIR_OP_MAKE_SLICE, expression->resolved_type, 1);
						full->operands[0] = mirBuildAddress(builder, bracket.base);
						full->local = typeByteSize(*element_type);
						full->function = length;
						base_slice = full->result;
						array_element_view = true;
					}
				}
				if (array_element_view) {
					// The scalar array element is represented as a one-element slice view.
				} else {
				element_type = slice.base->resolved_type;
				length = 1;
				MirInstruction* full = mirAppend(builder, MIR_OP_MAKE_SLICE, expression->resolved_type, 1);
				full->operands[0] = mirBuildAddress(builder, slice.base);
				full->local = typeByteSize(*element_type);
				full->function = 1;
				base_slice = full->result;
				}
			}
			MirValueId begin = MIR_INVALID_ID;
			MirValueId end = MIR_INVALID_ID;
			ResolvedType* range_type = slice.begin && slice.begin->resolved_type ? slice.begin->resolved_type : element_type;
			if (array_element_view) begin = mirBuildExpression(builder, static_cast<BracketExpression*>(slice.base)->args[0]);
			else if (slice.begin) begin = mirBuildExpression(builder, slice.begin);
			else {
				MirInstruction* zero = mirAppend(builder, MIR_OP_CONST, range_type, 0);
				zero->integer = 0;
				begin = zero->result;
			}
			if (array_element_view) {
				MirInstruction* one = mirAppend(builder, MIR_OP_CONST, range_type, 0);
				one->integer = 1;
				end = mirAppend(builder, MIR_OP_ADD, range_type, 2)->result;
				MirInstruction& add = builder.block->instructions.back();
				add.operands[0] = begin; add.operands[1] = one->result;
			}
			else if (slice.end) end = mirBuildExpression(builder, slice.end);
			else {
				if (slice.base->resolved_type->kind == ResolvedType::SLICE) {
					MirInstruction* limit = mirAppend(builder, MIR_OP_SLICE_LENGTH, range_type, 1);
					limit->operands[0] = base_slice;
					end = limit->result;
				}
				else {
					MirInstruction* limit = mirAppend(builder, MIR_OP_CONST, range_type, 0);
					limit->integer = length;
					end = limit->result;
				}
			}
			if (!slice.begin && !slice.end && !array_element_view) return base_slice;
			MirInstruction* sub = mirAppend(builder, MIR_OP_MAKE_SLICE, expression->resolved_type, 3);
			sub->operands[0] = base_slice;
			sub->operands[1] = begin;
			sub->operands[2] = end;
			sub->immediate = 1;
			sub->local = element_type ? typeByteSize(*element_type) : 0;
			return sub->result;
		}
		case Expression::MEMBER: {
			MemberExpression& member = *static_cast<MemberExpression*>(expression);
			if (equalStrings(member.name, makeStringView("length")) && member.expression && member.expression->resolved_type && member.expression->resolved_type->kind == ResolvedType::ARRAY) {
				ArrayResolvedType* array = static_cast<ArrayResolvedType*>(member.expression->resolved_type);
				MirInstruction* length = mirAppend(builder, MIR_OP_CONST, expression->resolved_type, 0);
				length->integer = array->size;
				return length->result;
			}
			if (equalStrings(member.name, makeStringView("length")) && member.expression && member.expression->resolved_type && member.expression->resolved_type->kind == ResolvedType::SLICE) {
				MirValueId slice = mirBuildExpression(builder, member.expression);
				MirInstruction* length = mirAppend(builder, MIR_OP_SLICE_LENGTH, expression->resolved_type, 1);
				length->operands[0] = slice;
				return length->result;
			}
			MemberExpression* field = &member;
			u32 field_offset = 0;
			if (field->expression && field->expression->kind == Expression::BRACKET && field->expression->resolved_type && field->expression->resolved_type->kind == ResolvedType::STRUCT) {
				BracketExpression& bracket = *static_cast<BracketExpression*>(field->expression);
				if (bracket.base && bracket.base->resolved_type && bracket.base->resolved_type->kind == ResolvedType::SLICE && bracket.args.size() == 1 && mirFindFieldOffset(*field, field_offset)) {
					MirValueId slice = mirBuildExpression(builder, bracket.base);
					MirValueId index = mirBuildExpression(builder, bracket.args[0]);
					MirInstruction* load = mirAppend(builder, MIR_OP_LOAD, expression->resolved_type, 2);
					load->operands[0] = slice; load->operands[1] = index; load->immediate = 4;
					load->local = typeByteSize(*field->expression->resolved_type); load->offset = field_offset; load->function = typeByteSize(*expression->resolved_type);
					return load->result;
				}
			}
			MirValueId base = mirBuildAddress(builder, field->expression);
			if (base == MIR_INVALID_ID || !mirFindFieldOffset(*field, field_offset)) return MIR_INVALID_ID;
			MirInstruction* index = mirAppendI32Zero(builder, mirIndexType(expression->resolved_type));
			MirInstruction* load = mirAppend(builder, MIR_OP_LOAD, expression->resolved_type, 2);
			load->operands[0] = base;
			load->operands[1] = index->result;
			load->immediate = 1;
			load->local = typeByteSize(*expression->resolved_type);
			load->offset = field_offset;
			load->function = 1;
			return load->result;
		}
		case Expression::CAST: {
			CastExpression& cast = *static_cast<CastExpression*>(expression);
			MirValueId value = mirBuildExpression(builder, cast.expression);
			MirInstruction* instruction = mirAppend(builder, MIR_OP_CAST, expression->resolved_type, 1);
			instruction->operand_type = cast.expression ? cast.expression->resolved_type : nullptr;
			instruction->operands[0] = value;
			return instruction->result;
		}
		case Expression::UNARY: {
			UnaryExpression& unary = *static_cast<UnaryExpression*>(expression);
			MirValueId value = mirBuildExpression(builder, unary.expression);
			MirOpcode opcode = unary.op == Token::NOT ? MIR_OP_NOT : MIR_OP_NEG;
			MirInstruction* instruction = mirAppend(builder, opcode, expression->resolved_type, 1);
			instruction->operands[0] = value;
			return instruction->result;
		}
		case Expression::BINARY: {
			BinaryExpression& binary = *static_cast<BinaryExpression*>(expression);
			if (binary.op == Token::AND || binary.op == Token::OR) return mirBuildLogical(builder, binary);
			if ((binary.op == Token::EQUAL_EQUAL || binary.op == Token::BANG_EQUAL) && binary.lhs && binary.lhs->kind == Expression::IDENTIFIER && binary.rhs && binary.rhs->kind == Expression::NULL_LITERAL) {
				IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(binary.lhs);
				if (!identifier.slot || identifier.slot->storage == StorageSlot::GLOBAL) return MIR_INVALID_ID;
				MirLocalId local = mirFindSlot(builder, identifier.slot);
				ResolvedType* nullable_type = local != MIR_INVALID_ID ? builder.function.locals[local].type : identifier.slot->type;
				MirInstruction* address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, nullable_type, 0);
				address->local = local;
				NullableResolvedType* nullable = static_cast<NullableResolvedType*>(nullable_type);
				MirInstruction* index = mirAppendI32Zero(builder, nullable->inner);
				MirInstruction* value = mirAppend(builder, MIR_OP_NULLABLE_HAS_VALUE, binary.resolved_type, 2);
				value->operands[0] = address->result;
				value->operands[1] = index->result;
				if (binary.op == Token::BANG_EQUAL) return value->result;
				MirInstruction* result = mirAppend(builder, MIR_OP_NOT, binary.resolved_type, 1);
				result->operands[0] = value->result;
				return result->result;
			}
			MirValueId lhs = mirBuildExpression(builder, binary.lhs);
			MirValueId rhs = mirBuildExpression(builder, binary.rhs);
			MirInstruction* instruction = mirAppend(builder, mirBinaryOpcode(binary.op), expression->resolved_type, 2);
			instruction->operand_type = binary.lhs ? binary.lhs->resolved_type : nullptr;
			switch (binary.op) {
				case Token::EQUAL_EQUAL: instruction->immediate = MIR_COMPARE_EQ; break;
				case Token::BANG_EQUAL: instruction->immediate = MIR_COMPARE_NE; break;
				case Token::LT: instruction->immediate = MIR_COMPARE_LT; break;
				case Token::LT_EQUAL: instruction->immediate = MIR_COMPARE_LE; break;
				case Token::GT: instruction->immediate = MIR_COMPARE_GT; break;
				case Token::GT_EQUAL: instruction->immediate = MIR_COMPARE_GE; break;
				default: instruction->immediate = MIR_COMPARE_EQ; break;
			}
			instruction->operands[0] = lhs;
			instruction->operands[1] = rhs;
			return instruction->result;
		}
		case Expression::CALL: {
			CallExpression& call = *static_cast<CallExpression*>(expression);
			FunctionExpression* direct = call.resolved_fn;
			if (!direct && call.callee && call.callee->kind == Expression::IDENTIFIER) {
				IdentifierExpression* identifier = static_cast<IdentifierExpression*>(call.callee);
				direct = identifier->resolved_fn;
				if (!direct && identifier->symbol && identifier->symbol->expression && identifier->symbol->expression->kind == Expression::FUNCTION)
					 direct = static_cast<FunctionExpression*>(identifier->symbol->expression);
			}
			if (!direct && call.callee && call.callee->kind == Expression::MEMBER) {
				MemberExpression* member = static_cast<MemberExpression*>(call.callee);
				direct = member->resolved_fn;
				if (!direct && member->resolved_symbol && member->resolved_symbol->expression && member->resolved_symbol->expression->kind == Expression::FUNCTION)
					direct = static_cast<FunctionExpression*>(member->resolved_symbol->expression);
			}
			MirValueId callee = MIR_INVALID_ID;
			if (!direct) callee = mirBuildExpression(builder, call.callee);
			FunctionResolvedType* direct_type = direct && direct->resolved_type && direct->resolved_type->kind == ResolvedType::FUNCTION
				? static_cast<FunctionResolvedType*>(direct->resolved_type) : nullptr;
			const u32 argument_count = (u32)call.args.size();
			MirValueId* arguments = nullptr;
			u32* argument_sizes = nullptr;
			if (argument_count) {
				arguments = (MirValueId*)builder.arena.allocate(builder.arena.user_data, sizeof(MirValueId) * argument_count, alignof(MirValueId));
				argument_sizes = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * argument_count, alignof(u32));
				for (u32 i = 0; i < argument_count; ++i) {
					ResolvedType* parameter_type = direct_type && i < (u32)direct_type->params.size() ? direct_type->params[i].type : nullptr;
					arguments[i] = parameter_type ? mirBuildExpressionAsType(builder, call.args[(i32)i], parameter_type) : mirBuildExpression(builder, call.args[(i32)i]);
					argument_sizes[i] = parameter_type ? typeByteSize(*parameter_type) : (call.args[(i32)i]->resolved_type ? typeByteSize(*call.args[(i32)i]->resolved_type) : 0);
				}
			}
			MirInstruction* instruction = mirAppend(builder, MIR_OP_CALL, expression->resolved_type, 0);
			instruction->arguments.values = arguments;
			instruction->arguments.sizes = argument_sizes;
			instruction->arguments.count = argument_count;
			for (u32 i = 0; i < argument_count; ++i) instruction->immediate += argument_sizes[i];
			if (direct) instruction->function = direct->bytecode_index;
			else {
				instruction->call_target = MIR_CALL_INDIRECT;
				instruction->operand_count = 1;
				instruction->operands[0] = callee;
			}
			if (call.callee && call.callee->kind == Expression::IDENTIFIER)
				instruction->call_name = static_cast<IdentifierExpression*>(call.callee)->name;
			else if (call.callee && call.callee->kind == Expression::MEMBER)
				instruction->call_name = static_cast<MemberExpression*>(call.callee)->name;
			return instruction->result;
		}
		default: return MIR_INVALID_ID;
	}
}

static void mirBuildStatement(MirBuilder& builder, Statement* statement) {
	if (!statement) return;
	if (builder.block->has_terminator && statement->kind != Statement::BLOCK) return;
	switch (statement->kind) {
		case Statement::BLOCK: {
			BlockStatement& block = *static_cast<BlockStatement*>(statement);
			builder.defer_marks.push((u32)builder.deferreds.size());
			for (Statement* child : block.statements) mirBuildStatement(builder, child);
			const u32 mark = builder.defer_marks.back();
			if (!builder.block->has_terminator) {
				for (i32 i = (i32)builder.deferreds.size() - 1; i >= (i32)mark; --i)
					mirBuildStatement(builder, builder.deferreds[(u32)i]);
			}
			while (builder.deferreds.size() > (i32)mark) builder.deferreds.pop_back();
			builder.defer_marks.pop_back();
			break;
		}
		case Statement::EXPRESSION:
			mirBuildExpression(builder, static_cast<ExpressionStatement*>(statement)->expression);
			break;
		case Statement::RETURN: {
			ReturnStatement& result = *static_cast<ReturnStatement*>(statement);
			const MirValueId value = result.expression ? mirBuildExpression(builder, result.expression) : MIR_INVALID_ID;
			mirEmitActiveDefers(builder);
			builder.block->terminator.kind = result.expression ? MIR_TERM_RETURN_VALUE : MIR_TERM_RETURN;
			builder.block->terminator.value = value;
			builder.block->has_terminator = true;
			break;
		}
		case Statement::ASSIGN: {
			AssignStatement& assignment = *static_cast<AssignStatement*>(statement);
			if (assignment.lhs && assignment.lhs->kind == Expression::MEMBER) {
				MemberExpression& member = *static_cast<MemberExpression*>(assignment.lhs);
				u32 field_offset = 0;
				if (member.expression && member.expression->kind == Expression::BRACKET && member.expression->resolved_type && member.expression->resolved_type->kind == ResolvedType::STRUCT) {
					BracketExpression& bracket = *static_cast<BracketExpression*>(member.expression);
					if (bracket.base && bracket.base->resolved_type && bracket.base->resolved_type->kind == ResolvedType::SLICE && bracket.args.size() == 1 && mirFindFieldOffset(member, field_offset)) {
						MirValueId slice = mirBuildExpression(builder, bracket.base);
						MirValueId index_value = mirBuildExpression(builder, bracket.args[0]);
						MirValueId old_value = MIR_INVALID_ID;
						if (assignment.op != Token::EQUAL) {
							MirInstruction* load = mirAppend(builder, MIR_OP_LOAD, assignment.lhs->resolved_type, 2);
							load->operands[0] = slice; load->operands[1] = index_value; load->immediate = 4;
							load->local = typeByteSize(*member.expression->resolved_type); load->offset = field_offset; load->function = typeByteSize(*assignment.lhs->resolved_type);
							old_value = load->result;
						}
						MirValueId value = mirBuildExpression(builder, assignment.rhs);
						if (assignment.op != Token::EQUAL) {
							Token::Type op = assignment.op == Token::PLUS_EQUAL ? Token::PLUS : assignment.op == Token::MINUS_EQUAL ? Token::MINUS : assignment.op == Token::STAR_EQUAL ? Token::STAR : Token::SLASH;
							MirInstruction* operation = mirAppend(builder, mirBinaryOpcode(op), assignment.lhs->resolved_type, 2);
							operation->operands[0] = old_value; operation->operands[1] = value; value = operation->result;
						}
						MirInstruction* store = mirAppend(builder, MIR_OP_STORE, assignment.lhs->resolved_type, 3);
						store->operands[0] = slice; store->operands[1] = index_value; store->operands[2] = value; store->immediate = 4;
						store->local = typeByteSize(*member.expression->resolved_type); store->offset = field_offset; store->function = typeByteSize(*assignment.lhs->resolved_type);
						break;
					}
				}
				MirValueId base = mirBuildAddress(builder, member.expression);
				if (base == MIR_INVALID_ID || !mirFindFieldOffset(member, field_offset)) break;
				MirInstruction* index = mirAppendI32Zero(builder, mirIndexType(assignment.lhs->resolved_type));
				MirValueId old_value = MIR_INVALID_ID;
				if (assignment.op != Token::EQUAL) {
					MirInstruction* load = mirAppend(builder, MIR_OP_LOAD, assignment.lhs->resolved_type, 2);
					load->operands[0] = base;
					load->operands[1] = index->result;
					load->immediate = 1;
					load->local = typeByteSize(*assignment.lhs->resolved_type);
					load->offset = field_offset;
					load->function = 1;
					old_value = load->result;
				}
				MirValueId value = mirBuildExpressionAsType(builder, assignment.rhs, assignment.lhs->resolved_type);
				if (assignment.op != Token::EQUAL) {
					Token::Type op = assignment.op == Token::PLUS_EQUAL ? Token::PLUS
						: assignment.op == Token::MINUS_EQUAL ? Token::MINUS
						: assignment.op == Token::STAR_EQUAL ? Token::STAR : Token::SLASH;
					MirInstruction* operation = mirAppend(builder, mirBinaryOpcode(op), assignment.lhs->resolved_type, 2);
					operation->operands[0] = old_value;
					operation->operands[1] = value;
					value = operation->result;
				}
				MirInstruction* store = mirAppend(builder, MIR_OP_STORE, assignment.lhs->resolved_type, 3);
				store->operands[0] = base;
				store->operands[1] = index->result;
				store->operands[2] = value;
				store->immediate = 1;
				store->local = typeByteSize(*assignment.lhs->resolved_type);
				store->offset = field_offset;
				store->function = 1;
				break;
			}
			if (assignment.lhs && assignment.lhs->kind == Expression::BRACKET) {
				BracketExpression& bracket = *static_cast<BracketExpression*>(assignment.lhs);
				MirValueId base = MIR_INVALID_ID;
				MirValueId index = MIR_INVALID_ID;
				ResolvedType* element_type = nullptr;
				u32 extent = 0;
				if (!mirBuildArrayAccess(builder, assignment.lhs, base, index, element_type, extent)) {
					if (!bracket.base || !bracket.base->resolved_type || bracket.base->resolved_type->kind != ResolvedType::SLICE || bracket.args.size() != 1) break;
					SliceResolvedType* slice = static_cast<SliceResolvedType*>(bracket.base->resolved_type);
					base = mirBuildExpression(builder, bracket.base);
					index = mirBuildExpression(builder, bracket.args[0]);
					MirValueId value = mirBuildExpressionAsType(builder, assignment.rhs, assignment.lhs->resolved_type);
					if (assignment.op != Token::EQUAL) {
						MirInstruction* load = mirAppend(builder, MIR_OP_LOAD, assignment.lhs->resolved_type, 2);
						load->operands[0] = base; load->operands[1] = index; load->immediate = 3;
						load->local = slice->element_type ? typeByteSize(*slice->element_type) : 0;
						MirInstruction* operation = mirAppend(builder, mirBinaryOpcode(assignment.op == Token::PLUS_EQUAL ? Token::PLUS : assignment.op == Token::MINUS_EQUAL ? Token::MINUS : assignment.op == Token::STAR_EQUAL ? Token::STAR : Token::SLASH), assignment.lhs->resolved_type, 2);
						operation->operands[0] = load->result; operation->operands[1] = value; value = operation->result;
					}
					MirInstruction* store = mirAppend(builder, MIR_OP_STORE, assignment.lhs->resolved_type, 3);
					store->operands[0] = base;
					store->operands[1] = index;
					store->operands[2] = value;
					store->immediate = 3;
					store->local = slice->element_type ? typeByteSize(*slice->element_type) : 0;
					break;
				}
				MirValueId value = mirBuildExpression(builder, assignment.rhs);
				if (assignment.op != Token::EQUAL) {
					MirInstruction* load = mirAppend(builder, MIR_OP_LOAD, assignment.lhs->resolved_type, 2);
					load->operands[0] = base;
					load->operands[1] = index;
					load->immediate = 1;
					load->local = element_type ? typeByteSize(*element_type) : 0;
					load->function = extent;
					MirInstruction* operation = mirAppend(builder, mirBinaryOpcode(assignment.op == Token::PLUS_EQUAL ? Token::PLUS
						: assignment.op == Token::MINUS_EQUAL ? Token::MINUS : assignment.op == Token::STAR_EQUAL ? Token::STAR : Token::SLASH), assignment.lhs->resolved_type, 2);
					operation->operands[0] = load->result;
					operation->operands[1] = value;
					value = operation->result;
				}
				MirInstruction* store = mirAppend(builder, MIR_OP_STORE, assignment.lhs->resolved_type, 3);
				store->operands[0] = base;
				store->operands[1] = index;
				store->operands[2] = value;
				store->immediate = 1;
				store->local = element_type ? typeByteSize(*element_type) : 0;
				store->function = extent;
				break;
			}
			MirValueId address = mirBuildAddress(builder, assignment.lhs);
			MirValueId old_value = MIR_INVALID_ID;
			if (assignment.op != Token::EQUAL) {
				MirInstruction* load = mirAppend(builder, MIR_OP_LOAD, assignment.lhs ? assignment.lhs->resolved_type : nullptr, 1);
				load->operands[0] = address;
				old_value = load->result;
			}
			MirValueId value = mirBuildExpressionAsType(builder, assignment.rhs, assignment.lhs ? assignment.lhs->resolved_type : nullptr);
			if (assignment.op != Token::EQUAL) {
				MirInstruction* operation = mirAppend(builder, mirBinaryOpcode(assignment.op == Token::PLUS_EQUAL ? Token::PLUS
					: assignment.op == Token::MINUS_EQUAL ? Token::MINUS
					: assignment.op == Token::STAR_EQUAL ? Token::STAR : Token::SLASH), assignment.lhs->resolved_type, 2);
				operation->operands[0] = old_value;
				operation->operands[1] = value;
				value = operation->result;
			}
			MirInstruction* store = mirAppend(builder, MIR_OP_STORE, assignment.lhs ? assignment.lhs->resolved_type : nullptr, 2);
			store->operands[0] = address;
			store->operands[1] = value;
			break;
		}
		case Statement::IF: {
			IfStatement& conditional = *static_cast<IfStatement*>(statement);
			MirValueId condition = mirBuildExpression(builder, conditional.condition);
			MirBlock* then_block = mirFunctionCreateBlock(builder.function);
			MirBlock* else_block = conditional.else_branch ? mirFunctionCreateBlock(builder.function) : nullptr;
			MirBlock* merge_block = mirFunctionCreateBlock(builder.function);
			builder.block->terminator.kind = MIR_TERM_BRANCH;
			builder.block->terminator.value = condition;
			builder.block->terminator.targets[0] = then_block->id;
			builder.block->terminator.targets[1] = else_block ? else_block->id : merge_block->id;
			builder.block->has_terminator = true;
			builder.block = then_block;
			mirBuildStatement(builder, conditional.body);
			if (!builder.block->has_terminator) {
				builder.block->terminator.kind = MIR_TERM_JUMP;
				builder.block->terminator.targets[0] = merge_block->id;
				builder.block->has_terminator = true;
			}
			if (conditional.else_branch) {
				builder.block = else_block;
				mirBuildStatement(builder, conditional.else_branch);
				if (!builder.block->has_terminator) {
					builder.block->terminator.kind = MIR_TERM_JUMP;
					builder.block->terminator.targets[0] = merge_block->id;
					builder.block->has_terminator = true;
				}
			}
			builder.block = merge_block;
			break;
		}
		case Statement::WHILE: {
			WhileStatement& loop = *static_cast<WhileStatement*>(statement);
			MirBlock* header = mirFunctionCreateBlock(builder.function);
			MirBlock* body = mirFunctionCreateBlock(builder.function);
			MirBlock* exit = mirFunctionCreateBlock(builder.function);
			builder.block->terminator.kind = MIR_TERM_JUMP;
			builder.block->terminator.targets[0] = header->id;
			builder.block->has_terminator = true;
			builder.block = header;
			MirValueId condition = mirBuildExpression(builder, loop.condition);
			builder.block->terminator.kind = MIR_TERM_BRANCH;
			builder.block->terminator.value = condition;
			builder.block->terminator.targets[0] = body->id;
			builder.block->terminator.targets[1] = exit->id;
			builder.block->has_terminator = true;
			builder.block = body;
			MirLoopBinding& loop_binding = builder.loops.emplace_back();
			loop_binding.label = builder.loop_label;
			loop_binding.header = header->id;
			loop_binding.continue_target = header->id;
			loop_binding.exit = exit->id;
			mirBuildStatement(builder, loop.body);
			builder.loops.pop_back();
			if (!builder.block->has_terminator) {
				builder.block->terminator.kind = MIR_TERM_JUMP;
				builder.block->terminator.targets[0] = header->id;
				builder.block->has_terminator = true;
			}
			builder.block = exit;
			break;
		}
		case Statement::BREAK:
			if (builder.loops.empty()) return;
			mirEmitActiveDefers(builder);
			{
				BreakStatement& break_statement = *static_cast<BreakStatement*>(statement);
				MirLoopBinding* loop = nullptr;
				for (i32 i = (i32)builder.loops.size() - 1; i >= 0; --i) {
					if (break_statement.label.begin == break_statement.label.end || equalStrings(builder.loops[(u32)i].label, break_statement.label)) { loop = &builder.loops[(u32)i]; break; }
				}
				if (!loop) return;
				builder.block->terminator.kind = MIR_TERM_JUMP;
				builder.block->terminator.targets[0] = loop->exit;
			}
			builder.block->has_terminator = true;
			break;
		case Statement::CONTINUE:
			if (builder.loops.empty()) return;
			mirEmitActiveDefers(builder);
			{
				ContinueStatement& continue_statement = *static_cast<ContinueStatement*>(statement);
				MirLoopBinding* loop = nullptr;
				for (i32 i = (i32)builder.loops.size() - 1; i >= 0; --i) {
					if (continue_statement.label.begin == continue_statement.label.end || equalStrings(builder.loops[(u32)i].label, continue_statement.label)) { loop = &builder.loops[(u32)i]; break; }
				}
				if (!loop) return;
				builder.block->terminator.kind = MIR_TERM_JUMP;
				builder.block->terminator.targets[0] = loop->continue_target;
			}
			builder.block->has_terminator = true;
			break;
		case Statement::LABEL: {
			LabelStatement& label = *static_cast<LabelStatement*>(statement);
			const ls_string_view previous = builder.loop_label;
			builder.loop_label = label.name;
			mirBuildStatement(builder, label.statement);
			builder.loop_label = previous;
			break;
		}
		case Statement::FOR: {
			ForStatement& loop = *static_cast<ForStatement*>(statement);
			if (loop.is_unroll || !loop.begin || !loop.end || loop.is_key_value) break;
			ResolvedType* type = loop.slot.type ? loop.slot.type : loop.begin->resolved_type;
			MirLocalId index_local = mirFunctionAddLocal(builder.function, type, loop.value_var, true, false);
			MirSlotBinding& binding = builder.slots.emplace_back();
			binding.slot = &loop.slot;
			binding.local = index_local;
			MirLocalId end_local = mirFunctionAddLocal(builder.function, type, {}, false, true);
			MirValueId begin = mirBuildExpression(builder, loop.begin);
			MirValueId end = mirBuildExpression(builder, loop.end);
			MirInstruction* index_address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, type, 0);
			index_address->local = index_local;
			MirInstruction* index_store = mirAppend(builder, MIR_OP_STORE, type, 2);
			index_store->operands[0] = index_address->result;
			index_store->operands[1] = begin;
			MirInstruction* end_address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, type, 0);
			end_address->local = end_local;
			MirInstruction* end_store = mirAppend(builder, MIR_OP_STORE, type, 2);
			end_store->operands[0] = end_address->result;
			end_store->operands[1] = end;
			MirBlock* header = mirFunctionCreateBlock(builder.function);
			MirBlock* body = mirFunctionCreateBlock(builder.function);
			MirBlock* increment = mirFunctionCreateBlock(builder.function);
			MirBlock* exit = mirFunctionCreateBlock(builder.function);
			builder.block->terminator.kind = MIR_TERM_JUMP;
			builder.block->terminator.targets[0] = header->id;
			builder.block->has_terminator = true;
			builder.block = header;
			MirInstruction* index_load_address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, type, 0);
			index_load_address->local = index_local;
			MirInstruction* index_load = mirAppend(builder, MIR_OP_LOAD, type, 1);
			index_load->operands[0] = index_load_address->result;
			MirInstruction* end_load_address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, type, 0);
			end_load_address->local = end_local;
			MirInstruction* end_load = mirAppend(builder, MIR_OP_LOAD, type, 1);
			end_load->operands[0] = end_load_address->result;
			MirInstruction* condition = mirAppend(builder, MIR_OP_COMPARE, builder.function.return_type, 2);
			condition->operand_type = type;
			condition->immediate = MIR_COMPARE_LT;
			condition->operands[0] = index_load->result;
			condition->operands[1] = end_load->result;
			builder.block->terminator.kind = MIR_TERM_BRANCH;
			builder.block->terminator.value = condition->result;
			builder.block->terminator.targets[0] = body->id;
			builder.block->terminator.targets[1] = exit->id;
			builder.block->has_terminator = true;
			builder.block = body;
			MirLoopBinding& loop_binding = builder.loops.emplace_back();
			loop_binding.label = builder.loop_label;
			loop_binding.header = header->id;
			loop_binding.continue_target = increment->id;
			loop_binding.exit = exit->id;
			mirBuildStatement(builder, loop.body);
			builder.loops.pop_back();
			if (!builder.block->has_terminator) {
				builder.block->terminator.kind = MIR_TERM_JUMP;
				builder.block->terminator.targets[0] = increment->id;
				builder.block->has_terminator = true;
			}
			builder.block = increment;
			MirInstruction* one = mirAppend(builder, MIR_OP_CONST, type, 0);
			one->integer = 1;
			MirInstruction* next = mirAppend(builder, MIR_OP_ADD, type, 2);
			next->operands[0] = index_load->result;
			next->operands[1] = one->result;
			MirInstruction* next_address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, type, 0);
			next_address->local = index_local;
			MirInstruction* next_store = mirAppend(builder, MIR_OP_STORE, type, 2);
			next_store->operands[0] = next_address->result;
			next_store->operands[1] = next->result;
			builder.block->terminator.kind = MIR_TERM_JUMP;
			builder.block->terminator.targets[0] = header->id;
			builder.block->has_terminator = true;
			builder.block = exit;
			break;
		}
		case Statement::DEFER:
			builder.deferreds.push(static_cast<DeferStatement*>(statement)->statement);
			break;
		case Statement::VAR_DECL: {
			VarDeclStatement& declaration = *static_cast<VarDeclStatement*>(statement);
			ResolvedType* local_type = declaration.slot.type ? declaration.slot.type : declaration.resolved_type;
			MirLocalId local = mirFunctionAddLocal(builder.function, local_type, declaration.name, !declaration.is_immutable, false);
			MirSlotBinding& binding = builder.slots.emplace_back();
			binding.slot = &declaration.slot;
			binding.local = local;
			MirInstruction* address = mirAppend(builder, MIR_OP_LOCAL_ADDRESS, local_type, 0);
			address->local = local;
			if (declaration.expression && local_type && local_type->kind == ResolvedType::NULLABLE) {
				NullableResolvedType* nullable = static_cast<NullableResolvedType*>(local_type);
				MirInstruction* tag = mirAppend(builder, MIR_OP_CONST, nullable->inner, 0);
				tag->integer = declaration.expression->kind == Expression::NULL_LITERAL ? 0 : 1;
				MirInstruction* tag_index = mirAppendI32Zero(builder, nullable->inner);
				MirInstruction* tag_store = mirAppend(builder, MIR_OP_STORE, nullable->inner, 3);
				tag_store->operands[0] = address->result;
				tag_store->operands[1] = tag_index->result;
				tag_store->operands[2] = tag->result;
				tag_store->immediate = 2;
				tag_store->offset = 0;
				tag_store->local = 1;
				tag_store->function = 1;
				if (declaration.expression->kind != Expression::NULL_LITERAL) {
					MirValueId value = mirBuildExpressionAsType(builder, declaration.expression, nullable->inner);
					MirInstruction* payload = mirAppend(builder, MIR_OP_STORE, nullable->inner, 3);
					payload->operands[0] = address->result;
					payload->operands[1] = tag_index->result;
					payload->operands[2] = value;
					payload->immediate = 1;
					payload->offset = 1;
					payload->local = 1;
					payload->function = 1;
				}
			}
			else if (declaration.expression && declaration.expression->kind == Expression::ARRAY_LITERAL && local_type && local_type->kind == ResolvedType::ARRAY) {
				ArrayResolvedType* array = static_cast<ArrayResolvedType*>(local_type);
				ArrayLiteralExpression& literal = *static_cast<ArrayLiteralExpression*>(declaration.expression);
				for (u32 i = 0; i < (u32)literal.values.size(); ++i) {
					MirInstruction* index = mirAppendI32Zero(builder, array->element_type);
					index->integer = i;
					MirValueId value = mirBuildExpressionAsType(builder, literal.values[(i32)i], array->element_type);
					MirInstruction* store = mirAppend(builder, MIR_OP_STORE, array->element_type, 3);
					store->operands[0] = address->result;
					store->operands[1] = index->result;
					store->operands[2] = value;
					store->immediate = 1;
					store->local = array->element_type ? typeByteSize(*array->element_type) : 0;
					store->function = (u32)array->size;
				}
			}
			else if (declaration.expression && declaration.expression->kind != Expression::UNDEFINED) {
				MirValueId value = mirBuildExpressionAsType(builder, declaration.expression, local_type);
				MirInstruction* store = mirAppend(builder, MIR_OP_STORE, local_type, 2);
				store->operands[0] = address->result;
				store->operands[1] = value;
			}
			break;
		}
		default: break;
	}
}

MirFunction* mirBuildFunction(ls_arena& arena, FunctionExpression* source, ls_string_view name) {
	if (!source || !source->body || source->body->kind != Statement::BLOCK) return nullptr;
	MirFunction* function = (MirFunction*)arena.allocate(arena.user_data, sizeof(MirFunction), alignof(MirFunction));
	if (!function) return nullptr;
	::new (NewPlaceholder{}, (void*)function) MirFunction(arena);
	function->name = name;
	FunctionResolvedType* function_type = source->resolved_type && source->resolved_type->kind == ResolvedType::FUNCTION
		? static_cast<FunctionResolvedType*>(source->resolved_type) : nullptr;
	function->return_type = function_type ? function_type->return_type : nullptr;
	MirBuilder builder(arena, *function);
	for (FunctionParam& parameter : source->params) {
		if (parameter.is_comptime) continue;
		function->param_size += parameter.resolved_type ? typeByteSize(*parameter.resolved_type) : 0;
		MirLocalId local = mirFunctionAddLocal(*function, parameter.resolved_type, parameter.name, false, false);
		MirSlotBinding& binding = builder.slots.emplace_back();
		binding.slot = &parameter.slot;
		binding.local = local;
	}
	mirBuildStatement(builder, source->body);
	if (!builder.block->has_terminator) {
		builder.block->terminator.kind = MIR_TERM_RETURN;
		builder.block->has_terminator = true;
	}
	return function;
}

MirFunction* mirBuildGlobalInit(ls_arena& arena, ls_module* module) {
	MirFunction* function = (MirFunction*)arena.allocate(arena.user_data, sizeof(MirFunction), alignof(MirFunction));
	if (!function) return nullptr;
	::new (NewPlaceholder{}, (void*)function) MirFunction(arena);
	MirBuilder builder(arena, *function);
	for (Unit& unit : module->units) {
		for (Symbol& symbol : unit.symbols) {
			if (!symbolHasGlobalStorage(symbol) || !symbol.expression || symbol.expression->kind == Expression::UNDEFINED) continue;
			if (!symbol.resolved_type) return nullptr;
			MirInstruction* address = mirAppend(builder, MIR_OP_GLOBAL_ADDRESS, symbol.resolved_type, 0);
			address->immediate = symbol.slot.offset;
			MirValueId value = mirBuildExpression(builder, symbol.expression);
			if (value == MIR_INVALID_ID) return nullptr;
			MirInstruction* store = mirAppend(builder, MIR_OP_STORE, symbol.resolved_type, 2);
			store->operands[0] = address->result;
			store->operands[1] = value;
		}
	}
	builder.block->terminator.kind = MIR_TERM_RETURN;
	builder.block->has_terminator = true;
	return function;
}

MirModule* mirBuildModule(ls_arena& arena, ls_module* module) {
	if (!module) return nullptr;
	MirModule* result = (MirModule*)arena.allocate(arena.user_data, sizeof(MirModule), alignof(MirModule));
	if (!result) return nullptr;
	::new (NewPlaceholder{}, (void*)result) MirModule(arena);
	for (Unit& unit : module->units) {
		for (Symbol& symbol : unit.symbols) {
			if (!symbolHasGlobalStorage(symbol)) continue;
			symbol.slot.storage = StorageSlot::GLOBAL;
			symbol.slot.offset = result->global_size;
			symbol.slot.byte_size = typeByteSize(*symbol.resolved_type);
			if (symbol.slot.byte_size == 0) symbol.slot.byte_size = 1;
			symbol.slot.type = symbol.resolved_type;
			result->global_size += symbol.slot.byte_size;
		}
	}
	u32 function_index = 0;
	for (Unit& unit : module->units) {
		for (Symbol& symbol : unit.symbols) {
			if (!symbol.expression || symbol.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* function = static_cast<FunctionExpression*>(symbol.expression);
			if (!function->is_template && function->is_extern) function->bytecode_index = function_index++;
		}
	}
	for (Unit& unit : module->units) {
		for (Symbol& symbol : unit.symbols) {
			if (!symbol.expression || symbol.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* function = static_cast<FunctionExpression*>(symbol.expression);
			if (!function->is_template && !function->is_extern) function->bytecode_index = function_index++;
		}
	}
	for (u32 pass = 0; pass < 2; ++pass) {
	for (Unit& unit : module->units) {
		for (Symbol& symbol : unit.symbols) {
			if (!symbol.expression || symbol.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* function = static_cast<FunctionExpression*>(symbol.expression);
			if (function->is_template) continue;
			if ((pass == 0) != function->is_extern) continue;
			if (function->is_extern) {
				if (!function->resolved_type || function->resolved_type->kind != ResolvedType::FUNCTION) return nullptr;
				MirModuleFunction& entry = result->functions.emplace_back();
				entry.is_native = true;
				entry.function = nullptr;
				entry.native.name = symbol.name;
				entry.native.type = static_cast<FunctionResolvedType*>(function->resolved_type);
				continue;
			}
			MirFunction* mir = mirBuildFunction(arena, function, symbol.name);
			if (!mir) return nullptr;
			MirModuleFunction& entry = result->functions.emplace_back();
			entry.is_native = false;
			entry.function = mir;
		}
	}
	}
	result->global_init = mirBuildGlobalInit(arena, module);
	return result->global_init ? result : nullptr;
}

ls_bytecode* ls_bytecode_compile_mir(ls_module* module, ls_host* host) {
	if (!module || !host || !host->arena.allocate) return nullptr;
	MirModule* mir = mirBuildModule(host->arena, module);
	return mir ? mirCompileModuleBytecode(mir, host) : nullptr;
}
