#include "mir_builder.h"
#include "bytecode.h"

#include <cstdio>
#include <cstdlib>

static ls_type_kind mirDebugKind(ResolvedType& type) {
	switch (type.kind) {
		case ResolvedType::BOOL: return LS_TYPE_BOOL;
		case ResolvedType::I8: return LS_TYPE_I8;
		case ResolvedType::I16: return LS_TYPE_I16;
		case ResolvedType::I32: return LS_TYPE_I32;
		case ResolvedType::I64: case ResolvedType::ISIZE: return LS_TYPE_I64;
		case ResolvedType::U8: case ResolvedType::BYTE: return LS_TYPE_U8;
		case ResolvedType::U16: return LS_TYPE_U16;
		case ResolvedType::U32: return LS_TYPE_U32;
		case ResolvedType::U64: return LS_TYPE_U64;
		case ResolvedType::F32: return LS_TYPE_F32;
		case ResolvedType::F64: return LS_TYPE_F64;
		case ResolvedType::STRUCT: return LS_TYPE_STRUCT;
		case ResolvedType::ENUM: return LS_TYPE_ENUM;
		case ResolvedType::UNION: return LS_TYPE_TAGGED_UNION;
		case ResolvedType::ARRAY: return LS_TYPE_ARRAY;
		case ResolvedType::SLICE: return LS_TYPE_SLICE;
		case ResolvedType::NULLABLE: return LS_TYPE_NULLABLE;
		case ResolvedType::POINTER: return LS_TYPE_CPTR;
		default: return LS_TYPE_INVALID;
	}
}

#include <float.h>
#include <string.h>

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
	Token current_source = {};
	bool has_current_source = false;

	MirBuilder(ls_arena& arena, MirFunction& function)
		: arena(arena)
		, function(function)
		, slots(arena)
		, loops(arena)
		, deferreds(arena)
		, defer_marks(arena) {
		loop_label = {};
		block = mirFunctionCreateBlock(function);
	}
};

struct MirSourceScope {
	MirBuilder& builder;
	Token previous_source;
	bool previous_has_source;

	MirSourceScope(MirBuilder& builder, Token source)
		: builder(builder), previous_source(builder.current_source), previous_has_source(builder.has_current_source) {
		builder.current_source = source;
		builder.has_current_source = source.line > 0;
	}

	~MirSourceScope() {
		builder.current_source = previous_source;
		builder.has_current_source = previous_has_source;
	}
};

template <typename T, typename... Args> T* mirAppend(MirBuilder& builder, ResolvedType* type, Args&&... args) {
	T* instruction = (T*)builder.arena.allocate(builder.arena.user_data, sizeof(T), alignof(T));
	::new (NewPlaceholder{}, (void*)instruction) T(args...);
	instruction->type = type;

	instruction->result = MIR_INVALID_ID;
	instruction->source_location = MIR_INVALID_ID;
	if (builder.has_current_source && builder.current_source.line > 0) {
		instruction->source_location = mirFunctionAddSourceLocation(builder.function, builder.current_source.source_name,
			(u32)builder.current_source.line, (u32)builder.current_source.column);
	}

	if (instruction->opcode != MIR_OP_STORE && instruction->opcode != MIR_OP_COPY) instruction->result = mirFunctionNewValue(builder.function);
	builder.block->instructions.push_back(instruction);
	return instruction;
}

static MirConstInstruction* mirAppendI32Zero(MirBuilder& builder, ResolvedType* type) {
	auto* instruction = mirAppend<MirConstInstruction>(builder, type);
	instruction->kind = MIR_CONST_I32;
	instruction->integer = 0;
	return instruction;
}

static MirValueId mirAppendZero(MirBuilder& builder, ResolvedType* type) {
	auto* instruction = mirAppend<MirConstInstruction>(builder, type);
	instruction->integer = 0;
	return instruction->result;
}

static MirOpcode mirBinaryOpcode(Token::Type op) {
	switch (op) {
		case Token::PLUS: return MIR_OP_ADD;
		case Token::MINUS: return MIR_OP_SUB;
		case Token::STAR: return MIR_OP_MUL;
		case Token::SLASH: return MIR_OP_DIV;
		case Token::PERCENT: return MIR_OP_MOD;
		case Token::EQUAL_EQUAL: return MIR_OP_EQ;
		case Token::BANG_EQUAL: return MIR_OP_NE;
		case Token::GT: return MIR_OP_GT;
		case Token::LT: return MIR_OP_LT;
		case Token::GT_EQUAL: return MIR_OP_GE;
		case Token::LT_EQUAL: return MIR_OP_LE;
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
static MirValueId mirBuildExpressionAsType(MirBuilder& builder, Expression* expression, ResolvedType* type);
static MirValueId mirBuildComptimeConstant(MirBuilder& builder, const u8* bytes, ResolvedType* type);
static void mirBuildStatement(MirBuilder& builder, Statement* statement);
static MirValueId mirBuildAddress(MirBuilder& builder, Expression* expression);
static bool mirFindFieldOffset(MemberExpression& member, u32& offset);
static bool mirFindFieldOffset(MirBuilder& builder, MemberExpression& member, u32& offset);
static bool mirBuildStoreAsUnion(MirBuilder& builder, MirValueId address, ResolvedType* target, Expression* expression, bool pointer = false);

static bool mirFindUnionFieldOffset(UnionResolvedType* union_type, ls_string_view name, u32& offset) {
	if (!union_type) return false;
	for (ResolvedType* candidate : union_type->members) {
		if (!candidate || candidate->kind != ResolvedType::STRUCT) continue;
		StructResolvedType* structure = static_cast<StructResolvedType*>(candidate);
		if (!structure->decl) continue;
		u32 payload_offset = 0;
		for (u32 i = 0; i < (u32)structure->decl->fields.size(); ++i) {
			if (equalStrings(structure->decl->fields[i].name, name)) {
				offset = 4 + payload_offset;
				return true;
			}
			ResolvedType* field_type = i < (u32)structure->field_types.size() ? structure->field_types[i] : structure->decl->fields[i].resolved_type;
			if (!field_type) return false;
			payload_offset += typeByteSize(*field_type);
		}
	}
	return false;
}

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
		auto* stride = mirAppend<MirConstInstruction>(builder, bracket.args[0]->resolved_type);
		stride->integer = mirArrayElementCount(array->element_type);
		auto* scaled = mirAppend<MirBinaryInstruction>(builder, bracket.args[0]->resolved_type, MIR_OP_MUL);
		scaled->lhs = index;
		scaled->rhs = stride->result;
		auto* combined = mirAppend<MirBinaryInstruction>(builder, bracket.args[0]->resolved_type, MIR_OP_ADD);
		combined->lhs = scaled->result;
		combined->rhs = current_index;
		index = combined->result;
		extent *= (u32)array->size;
	} else {
		base = mirBuildAddress(builder, bracket.base);
		index = current_index;
		extent = (u32)array->size;
	}
	element_type = bracket.resolved_type;
	return base != MIR_INVALID_ID && index != MIR_INVALID_ID;
}

static void mirEmitActiveDefers(MirBuilder& builder) {
	for (i32 i = (i32)builder.deferreds.size() - 1; i >= 0; --i) mirBuildStatement(builder, builder.deferreds[(u32)i]);
}

static MirValueId mirBuildArrayLiteral(MirBuilder& builder, ArrayLiteralExpression& literal, ResolvedType* target) {
	if (!target) return MIR_INVALID_ID;
	ArrayResolvedType* storage = nullptr;
	ResolvedType* element_type = nullptr;
	u32 count = (u32)literal.values.size();
	if (target->kind == ResolvedType::ARRAY) {
		storage = static_cast<ArrayResolvedType*>(target);
		element_type = storage->element_type;
		if (storage->size >= 0) count = (u32)storage->size;
	} else if (target->kind == ResolvedType::SLICE) {
		element_type = static_cast<SliceResolvedType*>(target)->element_type;
		storage = (ArrayResolvedType*)builder.arena.allocate(builder.arena.user_data, sizeof(ArrayResolvedType), alignof(ArrayResolvedType));
		::new (NewPlaceholder{}, (void*)storage) ArrayResolvedType();
		storage->element_type = element_type;
		storage->size = (i64)count;
	} else {
		return MIR_INVALID_ID;
	}
	MirLocalId local = mirFunctionAddLocal(builder.function, storage, {}, false, true);
	auto* address = mirAppend<MirAddressInstruction>(builder, storage, MIR_OP_LOCAL_ADDRESS);
	address->local = local;
	u32 offset = 0;
	for (u32 i = 0; i < count; ++i) {
		auto* index = mirAppendI32Zero(builder, element_type);
		index->integer = i;
		MirValueId value = i < (u32)literal.values.size() ? mirBuildExpressionAsType(builder, literal.values[(i32)i], element_type) : MIR_INVALID_ID;
		auto* store = mirAppend<MirStoreInstruction>(builder, element_type);
		store->address = address->result;
		store->index = index->result;
		store->value = value;
		store->access = MIR_ACCESS_INDEXED;
		store->element_size = element_type ? typeByteSize(*element_type) : 0;
		store->field_offset = offset;
		store->extent = 1;
		offset += element_type ? typeByteSize(*element_type) : 0;
	}
	if (target->kind == ResolvedType::SLICE) {
		auto* make = mirAppend<MirSliceInstruction>(builder, target);
		make->base = address->result;
		make->element_size = element_type ? typeByteSize(*element_type) : 0;
		make->length = count;
		return make->result;
	}
	auto* result = mirAppend<MirLoadInstruction>(builder, target);
	result->address = address->result;
	result->element_size = typeByteSize(*target);
	return result->result;
}

static MirValueId mirBuildExpressionAsType(MirBuilder& builder, Expression* expression, ResolvedType* type) {
	if (!expression || !type || (expression->resolved_type == type && expression->kind != Expression::IDENTIFIER)) return mirBuildExpression(builder, expression);
	if (expression->kind == Expression::IDENTIFIER) {
		IdentifierExpression* identifier = static_cast<IdentifierExpression*>(expression);
		if (identifier->symbol && identifier->symbol->expression && identifier->symbol->expression->kind == Expression::FLOAT_LITERAL &&
			(type->kind == ResolvedType::F32 || type->kind == ResolvedType::F64 || type->kind == ResolvedType::UNTYPED_FLOAT)) {
			auto* instruction = mirAppend<MirConstInstruction>(builder, type);
			instruction->floating = static_cast<FloatLiteralExpression*>(identifier->symbol->expression)->value;
			return instruction->result;
		}
		if (identifier->comptime_value.kind == ComptimeValue::VALUE && identifier->comptime_value.value &&
			(type->kind == ResolvedType::F32 || type->kind == ResolvedType::F64 || type->kind == ResolvedType::UNTYPED_FLOAT)) {
			f64 value = 0;
			if (identifier->comptime_value.type && identifier->comptime_value.type->kind == ResolvedType::F32) { f32 source; memcpy(&source, identifier->comptime_value.value, sizeof(source)); value = source; }
			else memcpy(&value, identifier->comptime_value.value, sizeof(value));
			auto* instruction = mirAppend<MirConstInstruction>(builder, type); instruction->floating = value; return instruction->result;
		}
		const u8* bytes = identifier->comptime_bytes;
		ResolvedType* source_type = identifier->symbol && identifier->symbol->resolved_type ? identifier->symbol->resolved_type : identifier->resolved_type;
		if (!bytes && identifier->symbol && identifier->symbol->storage == Symbol::COMPTIME) { bytes = identifier->symbol->comptime_bytes; source_type = identifier->symbol->resolved_type; }
		if (identifier->symbol && identifier->symbol->storage == Symbol::COMPTIME && identifier->symbol->comptime_value.kind == ComptimeValue::VALUE && identifier->symbol->comptime_value.value &&
			(identifier->symbol->comptime_value.type->kind == ResolvedType::UNTYPED_FLOAT || identifier->symbol->comptime_value.type->kind == ResolvedType::F32) &&
			(type->kind == ResolvedType::F32 || type->kind == ResolvedType::F64 || type->kind == ResolvedType::UNTYPED_FLOAT)) {
			auto* instruction = mirAppend<MirConstInstruction>(builder, type);
			if (identifier->symbol->comptime_value.type->kind == ResolvedType::F32) { f32 value; memcpy(&value, identifier->symbol->comptime_value.value, sizeof(value)); instruction->floating = value; }
			else memcpy(&instruction->floating, identifier->symbol->comptime_value.value, sizeof(instruction->floating));
			return instruction->result;
		}
		if (bytes && source_type && (source_type->kind == ResolvedType::UNTYPED_FLOAT || source_type->kind == ResolvedType::F32 || source_type->kind == ResolvedType::F64) && type->kind == ResolvedType::F32) {
			f64 value;
			if (source_type->kind == ResolvedType::F32) { f32 source; memcpy(&source, bytes, sizeof(source)); value = source; }
			else memcpy(&value, bytes, sizeof(value));
			auto* instruction = mirAppend<MirConstInstruction>(builder, type); instruction->floating = value; return instruction->result;
		}
		if (bytes && source_type && (source_type->kind == ResolvedType::UNTYPED_FLOAT || source_type->kind == ResolvedType::F32 || source_type->kind == ResolvedType::F64) && type->kind == ResolvedType::F64) {
			f64 value = 0;
			if (source_type->kind == ResolvedType::F32) { f32 source; memcpy(&source, bytes, sizeof(source)); value = source; }
			else memcpy(&value, bytes, sizeof(value));
			auto* instruction = mirAppend<MirConstInstruction>(builder, type); instruction->floating = value; return instruction->result;
		}
		if (bytes) return mirBuildComptimeConstant(builder, bytes, type);
	}
	if (type->kind == ResolvedType::UNION && expression->resolved_type && expression->resolved_type->kind != ResolvedType::UNION) {
		MirLocalId local = mirFunctionAddLocal(builder.function, type, {}, false, true);
		auto* address = mirAppend<MirAddressInstruction>(builder, type, MIR_OP_LOCAL_ADDRESS);
		address->local = local;
		if (mirBuildStoreAsUnion(builder, address->result, type, expression)) {
			auto* result = mirAppend<MirLoadInstruction>(builder, type);
			result->address = address->result;
			return result->result;
		}
	}
	if (expression->kind == Expression::FLOAT_LITERAL || expression->kind == Expression::INT_LITERAL || expression->kind == Expression::BOOL_LITERAL) {
		auto* instruction = mirAppend<MirConstInstruction>(builder, type);
		if (expression->kind == Expression::FLOAT_LITERAL) instruction->floating = static_cast<FloatLiteralExpression*>(expression)->value;
		else if (expression->kind == Expression::INT_LITERAL) instruction->integer = (i64)static_cast<IntLiteralExpression*>(expression)->value;
		else instruction->integer = static_cast<BoolLiteralExpression*>(expression)->value ? 1 : 0;
		return instruction->result;
	}
	if (type->kind == ResolvedType::NULLABLE && expression->resolved_type && expression->resolved_type->kind != ResolvedType::NULLABLE) {
		NullableResolvedType* nullable = static_cast<NullableResolvedType*>(type);
		MirLocalId local = mirFunctionAddLocal(builder.function, type, {}, false, true);
		auto* address = mirAppend<MirAddressInstruction>(builder, type, MIR_OP_LOCAL_ADDRESS);
		address->local = local;
		auto* tag = mirAppend<MirConstInstruction>(builder, nullable->inner);
		tag->integer = expression->kind == Expression::NULL_LITERAL ? 0 : 1;
		auto* tag_index = mirAppendI32Zero(builder, nullable->inner);
		auto* tag_store = mirAppend<MirStoreInstruction>(builder, nullable->inner);
		tag_store->address = address->result;
		tag_store->index = tag_index->result;
		tag_store->value = tag->result;
		tag_store->access = MIR_ACCESS_NULLABLE_TAG;
		tag_store->element_size = 1;
		tag_store->field_offset = 0;
		tag_store->extent = 1;
		if (tag->integer) {
			MirValueId payload_value = mirBuildExpressionAsType(builder, expression, nullable->inner);
			auto* payload = mirAppend<MirStoreInstruction>(builder, nullable->inner);
			payload->address = address->result;
			payload->index = tag_index->result;
			payload->value = payload_value;
			payload->access = MIR_ACCESS_INDEXED;
			payload->element_size = 1;
			payload->field_offset = 1;
			payload->extent = 1;
		}
		auto* result = mirAppend<MirLoadInstruction>(builder, type);
		result->address = address->result;
		return result->result;
	}
	if (expression->resolved_type && expression->resolved_type->kind == ResolvedType::ARRAY && type->kind == ResolvedType::SLICE && expression->kind == Expression::IDENTIFIER) {
		StorageSlot* slot = static_cast<IdentifierExpression*>(expression)->slot;
		MirLocalId local = mirFindSlot(builder, slot);
		if (local != MIR_INVALID_ID) {
			ArrayResolvedType* array = static_cast<ArrayResolvedType*>(expression->resolved_type);
			auto* address = mirAppend<MirAddressInstruction>(builder, expression->resolved_type, MIR_OP_LOCAL_ADDRESS);
			address->local = local;
			auto* make = mirAppend<MirSliceInstruction>(builder, type);
			make->base = address->result;
			make->element_size = array->element_type ? typeByteSize(*array->element_type) : 0;
			make->length = (u32)array->size;
			return make->result;
		}
	}
	if (expression->resolved_type && expression->resolved_type->kind == ResolvedType::ARRAY && type->kind == ResolvedType::SLICE && expression->kind == Expression::ARRAY_LITERAL) {
		return mirBuildArrayLiteral(builder, *static_cast<ArrayLiteralExpression*>(expression), type);
	}
	if (expression->kind == Expression::INT_LITERAL || expression->kind == Expression::FLOAT_LITERAL || expression->kind == Expression::BOOL_LITERAL) {
		auto* instruction = mirAppend<MirConstInstruction>(builder, type);
		if (expression->kind == Expression::INT_LITERAL) {
			const u64 value = static_cast<IntLiteralExpression*>(expression)->value;
			if (type->kind == ResolvedType::F32 || type->kind == ResolvedType::F64)
				instruction->floating = (f64)value;
			else
				instruction->integer = (i64)value;
		} else if (expression->kind == Expression::FLOAT_LITERAL)
			instruction->floating = static_cast<FloatLiteralExpression*>(expression)->value;
		else
			instruction->integer = static_cast<BoolLiteralExpression*>(expression)->value ? 1 : 0;
		return instruction->result;
	}
	return mirBuildExpression(builder, expression);
}

static bool mirSameType(const ResolvedType* a, const ResolvedType* b) {
	if (a == b) return true;
	if (!a || !b || a->kind != b->kind) return false;
	switch (a->kind) {
		case ResolvedType::FUNCTION: {
			const auto* fa = static_cast<const FunctionResolvedType*>(a);
			const auto* fb = static_cast<const FunctionResolvedType*>(b);
			if (fa->params.size() != fb->params.size()) return false;
			if (!mirSameType(fa->return_type, fb->return_type)) return false;
			for (i32 i = 0; i < fa->params.size(); ++i) {
				if (fa->params[i].is_comptime != fb->params[i].is_comptime) return false;
				if (!mirSameType(fa->params[i].type, fb->params[i].type)) return false;
			}
			return true;
		}
		case ResolvedType::ARRAY: {
			const auto* aa = static_cast<const ArrayResolvedType*>(a);
			const auto* ab = static_cast<const ArrayResolvedType*>(b);
			return aa->size == ab->size && mirSameType(aa->element_type, ab->element_type);
		}
		case ResolvedType::POINTER: {
			const auto* pa = static_cast<const PointerResolvedType*>(a);
			const auto* pb = static_cast<const PointerResolvedType*>(b);
			return pa->is_const == pb->is_const && mirSameType(pa->inner, pb->inner);
		}
		case ResolvedType::SLICE: {
			const auto* sa = static_cast<const SliceResolvedType*>(a);
			const auto* sb = static_cast<const SliceResolvedType*>(b);
			return sa->is_const == sb->is_const && mirSameType(sa->element_type, sb->element_type);
		}
		case ResolvedType::NULLABLE: {
			const auto* na = static_cast<const NullableResolvedType*>(a);
			const auto* nb = static_cast<const NullableResolvedType*>(b);
			return mirSameType(na->inner, nb->inner);
		}
		case ResolvedType::UNION: {
			const auto* ua = static_cast<const UnionResolvedType*>(a);
			const auto* ub = static_cast<const UnionResolvedType*>(b);
			if (ua->members.size() != ub->members.size()) return false;
			for (ResolvedType* member : ua->members) {
				bool found = false;
				for (ResolvedType* other : ub->members) {
					if (mirSameType(member, other)) { found = true; break; }
				}
				if (!found) return false;
			}
			return true;
		}
		default: return false;
	}
}

static ResolvedType* mirPrimitiveType(ls_arena& arena, ResolvedType::Kind kind) {
	ResolvedType* type = (ResolvedType*)arena.allocate(arena.user_data, sizeof(ResolvedType), alignof(ResolvedType));
	::new (NewPlaceholder{}, (void*)type) ResolvedType(kind);
	return type;
}

static void mirBuildZeroFill(MirBuilder& builder, MirValueId address, MirValueId index, u32 offset, u32 size) {
	u32 remaining = size;
	u32 fill_offset = offset;
	while (remaining >= 4) {
		ResolvedType* i32_type = mirPrimitiveType(builder.arena, ResolvedType::I32);
		auto* zero = mirAppend<MirConstInstruction>(builder, i32_type);
		zero->integer = 0;
		auto* store = mirAppend<MirStoreInstruction>(builder, i32_type);
		store->address = address;
		store->index = index;
		store->value = zero->result;
		store->access = MIR_ACCESS_INDEXED;
		store->element_size = 4;
		store->field_offset = fill_offset;
		store->extent = 1;
		fill_offset += 4;
		remaining -= 4;
	}
	while (remaining >= 2) {
		ResolvedType* i16_type = mirPrimitiveType(builder.arena, ResolvedType::I16);
		auto* zero = mirAppend<MirConstInstruction>(builder, i16_type);
		zero->integer = 0;
		auto* store = mirAppend<MirStoreInstruction>(builder, i16_type);
		store->address = address;
		store->index = index;
		store->value = zero->result;
		store->access = MIR_ACCESS_INDEXED;
		store->element_size = 2;
		store->field_offset = fill_offset;
		store->extent = 1;
		fill_offset += 2;
		remaining -= 2;
	}
	if (remaining >= 1) {
		ResolvedType* u8_type = mirPrimitiveType(builder.arena, ResolvedType::U8);
		auto* zero = mirAppend<MirConstInstruction>(builder, u8_type);
		zero->integer = 0;
		auto* store = mirAppend<MirStoreInstruction>(builder, u8_type);
		store->address = address;
		store->index = index;
		store->value = zero->result;
		store->access = MIR_ACCESS_INDEXED;
		store->element_size = 1;
		store->field_offset = fill_offset;
		store->extent = 1;
	}
}

// Store `expression` into the union-typed value at `address` (tag at offset 0,
// payload at offset 4). Returns false when the expression is not a member of
// the union, or the union is not the target type.
static bool mirBuildStoreAsUnion(MirBuilder& builder, MirValueId address, ResolvedType* target, Expression* expression, bool pointer) {
	if (!target || target->kind != ResolvedType::UNION || !expression || !expression->resolved_type) return false;
	ResolvedType* source = expression->resolved_type;
	if (source->kind == ResolvedType::UNION) return false;
	UnionResolvedType* un = static_cast<UnionResolvedType*>(target);
	i32 matched = -1;
	for (i32 i = 0; i < un->members.size(); ++i) {
		if (mirSameType(un->members[i], source)) { matched = i; break; }
	}
	if (matched < 0) return false;
	ResolvedType* member = un->members[matched];
	ResolvedType* i32_type = mirPrimitiveType(builder.arena, ResolvedType::I32);
	auto* tag = mirAppend<MirConstInstruction>(builder, i32_type);
	tag->integer = matched;
	MirValueId tag_index = pointer ? mirAppendZero(builder, mirPrimitiveType(builder.arena, ResolvedType::I64)) : mirAppendI32Zero(builder, i32_type)->result;
	auto* tag_store = mirAppend<MirStoreInstruction>(builder, i32_type);
	tag_store->address = address;
	tag_store->index = tag_index;
	tag_store->value = tag->result;
	tag_store->access = pointer ? MIR_ACCESS_POINTER : MIR_ACCESS_INDEXED;
	tag_store->element_size = 4;
	tag_store->field_offset = 0;
	tag_store->extent = 1;
	MirValueId value = mirBuildExpressionAsType(builder, expression, member);
	auto* payload = mirAppend<MirStoreInstruction>(builder, member);
	payload->address = address;
	payload->index = tag_index;
	payload->value = value;
	payload->access = pointer ? MIR_ACCESS_POINTER : MIR_ACCESS_INDEXED;
					payload->element_size = typeByteSize(*target);
	payload->field_offset = 4;
	payload->extent = 1;
	const u32 payload_size = typeByteSize(*target) - 4;
	const u32 member_size = typeByteSize(*member);
	if (payload_size > member_size) mirBuildZeroFill(builder, address, tag_index, 4 + member_size, payload_size - member_size);
	return true;
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
	auto* short_value = mirAppend<MirConstInstruction>(builder, binary.resolved_type);
	short_value->integer = is_and ? 0 : 1;
	auto* short_address = mirAppend<MirAddressInstruction>(builder, binary.resolved_type, MIR_OP_LOCAL_ADDRESS);
	short_address->local = result_local;
	auto* short_store = mirAppend<MirStoreInstruction>(builder, binary.resolved_type);
	short_store->address = short_address->result;
	short_store->value = short_value->result;
	short_block->terminator.kind = MIR_TERM_JUMP;
	short_block->terminator.targets[0] = merge_block->id;
	short_block->has_terminator = true;

	builder.block = rhs_block;
	MirValueId rhs = mirBuildExpression(builder, binary.rhs);
	MirBlock* rhs_exit = builder.block;
	if (!rhs_exit->has_terminator) {
		auto* rhs_address = mirAppend<MirAddressInstruction>(builder, binary.resolved_type, MIR_OP_LOCAL_ADDRESS);
		rhs_address->local = result_local;
		auto* rhs_store = mirAppend<MirStoreInstruction>(builder, binary.resolved_type);
		rhs_store->address = rhs_address->result;
		rhs_store->value = rhs;
		rhs_exit->terminator.kind = MIR_TERM_JUMP;
		rhs_exit->terminator.targets[0] = merge_block->id;
		rhs_exit->has_terminator = true;
	}

	builder.block = merge_block;
	auto* result_address = mirAppend<MirAddressInstruction>(builder, binary.resolved_type, MIR_OP_LOCAL_ADDRESS);
	result_address->local = result_local;
	auto* result = mirAppend<MirLoadInstruction>(builder, binary.resolved_type);
	result->address = result_address->result;
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
	MirBlock* true_exit = builder.block;
	if (!true_exit->has_terminator) {
		auto* true_address = mirAppend<MirAddressInstruction>(builder, ternary.resolved_type, MIR_OP_LOCAL_ADDRESS);
		true_address->local = result_local;
		auto* true_store = mirAppend<MirStoreInstruction>(builder, ternary.resolved_type);
		true_store->address = true_address->result;
		true_store->value = true_value;
		true_exit->terminator.kind = MIR_TERM_JUMP;
		true_exit->terminator.targets[0] = merge_block->id;
		true_exit->has_terminator = true;
	}

	builder.block = false_block;
	MirValueId false_value = mirBuildExpression(builder, ternary.false_expr);
	MirBlock* false_exit = builder.block;
	if (!false_exit->has_terminator) {
		auto* false_address = mirAppend<MirAddressInstruction>(builder, ternary.resolved_type, MIR_OP_LOCAL_ADDRESS);
		false_address->local = result_local;
		auto* false_store = mirAppend<MirStoreInstruction>(builder, ternary.resolved_type);
		false_store->address = false_address->result;
		false_store->value = false_value;
		false_exit->terminator.kind = MIR_TERM_JUMP;
		false_exit->terminator.targets[0] = merge_block->id;
		false_exit->has_terminator = true;
	}

	builder.block = merge_block;
	auto* result_address = mirAppend<MirAddressInstruction>(builder, ternary.resolved_type, MIR_OP_LOCAL_ADDRESS);
	result_address->local = result_local;
	auto* result = mirAppend<MirLoadInstruction>(builder, ternary.resolved_type);
	result->address = result_address->result;
	return result->result;
}

static MirValueId mirBuildAddress(MirBuilder& builder, Expression* expression) {
	if (!expression) return MIR_INVALID_ID;
	if (expression->kind == Expression::IDENTIFIER) {
		IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(expression);
		StorageSlot* slot = identifier.slot;
		if ((!slot || slot->storage != StorageSlot::GLOBAL) && identifier.symbol && identifier.symbol->slot.storage == StorageSlot::GLOBAL) slot = &identifier.symbol->slot;
		if (slot && slot->storage == StorageSlot::GLOBAL) {
			if (slot->type && slot->type->kind == ResolvedType::NULLABLE) return MIR_INVALID_ID;
			auto* address = mirAppend<MirAddressInstruction>(builder, expression->resolved_type, MIR_OP_GLOBAL_ADDRESS);
			address->global_offset = slot->offset;
			return address->result;
		}
		MirLocalId local = identifier.slot ? mirFindSlot(builder, identifier.slot) : MIR_INVALID_ID;
		if (local == MIR_INVALID_ID) return MIR_INVALID_ID;
		auto* address = mirAppend<MirAddressInstruction>(builder, expression->resolved_type, MIR_OP_LOCAL_ADDRESS);
		address->local = local;
		return address->result;
	}
	if (expression->kind == Expression::DEREFERENCE) {
		DereferenceExpression& dereference = *static_cast<DereferenceExpression*>(expression);
		return mirBuildExpression(builder, dereference.subject);
	}
	if (expression->kind == Expression::BRACKET) {
		BracketExpression& bracket = *static_cast<BracketExpression*>(expression);
		if (bracket.args.size() != 1 || !bracket.base || bracket.args[0]->kind != Expression::INT_LITERAL) return MIR_INVALID_ID;
		MirValueId address = mirBuildAddress(builder, bracket.base);
		if (address == MIR_INVALID_ID || builder.block->instructions.empty()) return MIR_INVALID_ID;
		MirInstruction* last = builder.block->instructions[builder.block->instructions.size() - 1];
		const u32 element_size = bracket.base->resolved_type && bracket.base->resolved_type->kind == ResolvedType::ARRAY
			? typeByteSize(*static_cast<ArrayResolvedType*>(bracket.base->resolved_type)->element_type) : 0;
		if (element_size && last->result == address) {
			const u32 offset = (u32)static_cast<IntLiteralExpression*>(bracket.args[0])->value * element_size;
			if (last->opcode == MIR_OP_LOCAL_ADDRESS) static_cast<MirAddressInstruction*>(last)->byte_offset += offset;
			else if (last->opcode == MIR_OP_GLOBAL_ADDRESS) static_cast<MirAddressInstruction*>(last)->global_offset += offset;
		}
		return address;
	}
	if (expression->kind == Expression::MEMBER) {
		u32 total_offset = 0;
		Expression* root = expression;
		while (root && root->kind == Expression::MEMBER) {
			MemberExpression& member = *static_cast<MemberExpression*>(root);
			u32 field_offset = 0;
			if (!mirFindFieldOffset(builder, member, field_offset)) {
				if (member.resolved_symbol && member.resolved_symbol->slot.storage == StorageSlot::GLOBAL && member.resolved_symbol->slot.type &&
					member.resolved_symbol->slot.type->kind != ResolvedType::NULLABLE) {
					auto* address = mirAppend<MirAddressInstruction>(builder, expression->resolved_type, MIR_OP_GLOBAL_ADDRESS);
					address->global_offset = member.resolved_symbol->slot.offset + total_offset;
					return address->result;
				}
				return MIR_INVALID_ID;
			}
			total_offset += field_offset;
			root = member.expression;
		}
		u32 payload_offset = 0;
		if (root && root->kind == Expression::IDENTIFIER && static_cast<IdentifierExpression*>(root)->slot && static_cast<IdentifierExpression*>(root)->slot->type &&
			static_cast<IdentifierExpression*>(root)->slot->type->kind == ResolvedType::NULLABLE && root->resolved_type &&
			root->resolved_type->kind != ResolvedType::NULLABLE)
			payload_offset = 1;
		if (root && root->kind == Expression::IDENTIFIER &&
			((static_cast<IdentifierExpression*>(root)->slot && static_cast<IdentifierExpression*>(root)->slot->storage == StorageSlot::GLOBAL) ||
			 (static_cast<IdentifierExpression*>(root)->symbol && static_cast<IdentifierExpression*>(root)->symbol->slot.storage == StorageSlot::GLOBAL))) {
			StorageSlot* root_slot = static_cast<IdentifierExpression*>(root)->slot && static_cast<IdentifierExpression*>(root)->slot->storage == StorageSlot::GLOBAL
				? static_cast<IdentifierExpression*>(root)->slot : &static_cast<IdentifierExpression*>(root)->symbol->slot;
			auto* address = mirAppend<MirAddressInstruction>(builder, expression->resolved_type, MIR_OP_GLOBAL_ADDRESS);
			address->global_offset = root_slot->offset + total_offset + payload_offset;
			return address->result;
		}
		MirLocalId local = MIR_INVALID_ID;
		if (root && root->kind == Expression::IDENTIFIER) {
			IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(root);
			local = identifier.slot ? mirFindSlot(builder, identifier.slot) : MIR_INVALID_ID;
		}
		if (local == MIR_INVALID_ID) return MIR_INVALID_ID;
		auto* address = mirAppend<MirAddressInstruction>(builder, expression->resolved_type, MIR_OP_LOCAL_ADDRESS);
		address->local = local;
		address->byte_offset = total_offset + payload_offset;
		return address->result;
	}
	return MIR_INVALID_ID;
}

static bool mirFindFieldOffset(MemberExpression& member, u32& offset) {
	if (!member.expression || !member.expression->resolved_type) return false;
	ResolvedType* owner_type = member.expression->resolved_type;
	if (owner_type->kind == ResolvedType::POINTER) owner_type = static_cast<PointerResolvedType*>(owner_type)->inner;
	if (member.expression->kind == Expression::IDENTIFIER) {
		IdentifierExpression* identifier = static_cast<IdentifierExpression*>(member.expression);
		if (identifier->slot && identifier->slot->type && identifier->slot->type->kind == ResolvedType::UNION) owner_type = identifier->slot->type;
	}
	if (owner_type->kind == ResolvedType::UNION) {
		UnionResolvedType* union_type = static_cast<UnionResolvedType*>(owner_type);
		return mirFindUnionFieldOffset(union_type, member.name, offset);
	}
	if (owner_type->kind != ResolvedType::STRUCT) return false;
	StructResolvedType* structure = static_cast<StructResolvedType*>(owner_type);
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

static bool mirFindFieldOffset(MirBuilder& builder, MemberExpression& member, u32& offset) {
	if (member.expression && member.expression->kind == Expression::DEREFERENCE) {
		DereferenceExpression* dereference = static_cast<DereferenceExpression*>(member.expression);
		if (dereference->subject && dereference->subject->resolved_type && dereference->subject->resolved_type->kind == ResolvedType::POINTER) {
			ResolvedType* inner = static_cast<PointerResolvedType*>(dereference->subject->resolved_type)->inner;
			if (inner->kind == ResolvedType::UNION)
				return mirFindUnionFieldOffset(static_cast<UnionResolvedType*>(inner), member.name, offset);
		}
	}
	if (member.expression && member.expression->kind == Expression::IDENTIFIER) {
		IdentifierExpression* identifier = static_cast<IdentifierExpression*>(member.expression);
		MirLocalId local = identifier->slot ? mirFindSlot(builder, identifier->slot) : MIR_INVALID_ID;
		if (local != MIR_INVALID_ID && builder.function.locals[local].type && builder.function.locals[local].type->kind == ResolvedType::UNION)
			return mirFindUnionFieldOffset(static_cast<UnionResolvedType*>(builder.function.locals[local].type), member.name, offset);
	}
	return mirFindFieldOffset(member, offset);
}

static ResolvedType* mirIndexType(ResolvedType* type) {
	while (type && (type->kind == ResolvedType::ARRAY || type->kind == ResolvedType::SLICE))
		type = type->kind == ResolvedType::ARRAY ? static_cast<ArrayResolvedType*>(type)->element_type : static_cast<SliceResolvedType*>(type)->element_type;
	return type;
}

static MirValueId mirBuildComptimeConstant(MirBuilder& builder, const u8* bytes, ResolvedType* type) {
	if (!bytes || !type) return MIR_INVALID_ID;
	if (type->kind == ResolvedType::SLICE && typeByteSize(*type) == 16) {
		struct SliceBytes {
			const char* data;
			i64 count;
		};
		SliceBytes value;
		memcpy(&value, bytes, sizeof(value));
		auto* instruction = mirAppend<MirConstInstruction>(builder, type);
		instruction->string = {value.data, value.data + value.count};
		return instruction->result;
	}
	const u32 size = typeByteSize(*type);
	if (size <= 8 && (type->kind == ResolvedType::F32 || type->kind == ResolvedType::F64 || type->kind == ResolvedType::UNTYPED_FLOAT)) {
		auto* instruction = mirAppend<MirConstInstruction>(builder, type);
		f64 value = 0;
		if (type->kind == ResolvedType::F32) { f32 source; memcpy(&source, bytes, sizeof(source)); value = source; }
		else memcpy(&value, bytes, sizeof(value));
		instruction->floating = value;
		return instruction->result;
	}
	if (size <= 8) {
		u64 bits = 0;
		memcpy(&bits, bytes, size);
		auto* instruction = mirAppend<MirConstInstruction>(builder, type);
		instruction->integer = (i64)bits;
		return instruction->result;
	}
	auto* instruction = mirAppend<MirConstInstruction>(builder, type);
	instruction->kind = MIR_CONST_BYTES;
	instruction->bytes = bytes;
	instruction->byte_size = size;
	return instruction->result;
}

static MirValueId mirBuildIdentifier(MirBuilder& builder, IdentifierExpression& expression) {
	const u8* comptime_bytes = expression.comptime_bytes ? expression.comptime_bytes : (expression.symbol ? expression.symbol->comptime_bytes : nullptr);
	if (expression.comptime_value.kind == ComptimeValue::VALUE && expression.comptime_value.value && expression.resolved_type &&
		(expression.resolved_type->kind == ResolvedType::F32 || expression.resolved_type->kind == ResolvedType::F64 || expression.resolved_type->kind == ResolvedType::UNTYPED_FLOAT)) {
		f64 value = 0;
		if (expression.comptime_value.type && expression.comptime_value.type->kind == ResolvedType::F32) { f32 source; memcpy(&source, expression.comptime_value.value, sizeof(source)); value = source; }
		else memcpy(&value, expression.comptime_value.value, sizeof(value));
		auto* instruction = mirAppend<MirConstInstruction>(builder, expression.resolved_type); instruction->floating = value; return instruction->result;
	}
	if (comptime_bytes && expression.resolved_type && (expression.resolved_type->kind == ResolvedType::F32 || expression.resolved_type->kind == ResolvedType::F64 || expression.resolved_type->kind == ResolvedType::UNTYPED_FLOAT)) {
		f64 value = 0;
		ResolvedType* source_type = expression.comptime_value.type ? expression.comptime_value.type : (expression.symbol && expression.symbol->comptime_value.type ? expression.symbol->comptime_value.type : (expression.symbol ? expression.symbol->resolved_type : expression.resolved_type));
		if (source_type && source_type->kind == ResolvedType::F32) { f32 source; memcpy(&source, comptime_bytes, sizeof(source)); value = source; }
		else memcpy(&value, comptime_bytes, sizeof(value));
		auto* instruction = mirAppend<MirConstInstruction>(builder, expression.resolved_type); instruction->floating = value; return instruction->result;
	}
	if (expression.symbol && expression.symbol->storage == Symbol::COMPTIME && expression.symbol->comptime_value.kind == ComptimeValue::VALUE && expression.symbol->comptime_value.value && expression.resolved_type &&
		(expression.resolved_type->kind == ResolvedType::F32 || expression.resolved_type->kind == ResolvedType::F64 || expression.resolved_type->kind == ResolvedType::UNTYPED_FLOAT)) {
		f64 value = 0;
		ResolvedType* source_type = expression.symbol->comptime_value.type;
		if (source_type && source_type->kind == ResolvedType::F32) {
			f32 source = 0;
			memcpy(&source, expression.symbol->comptime_value.value, sizeof(source));
			value = source;
		} else {
			memcpy(&value, expression.symbol->comptime_value.value, sizeof(value));
		}
		auto* instruction = mirAppend<MirConstInstruction>(builder, expression.resolved_type); instruction->floating = value; return instruction->result;
	}
	if (expression.symbol && expression.symbol->storage == Symbol::COMPTIME && expression.symbol->expression && expression.symbol->expression->kind == Expression::FLOAT_LITERAL && expression.resolved_type &&
		(expression.resolved_type->kind == ResolvedType::F32 || expression.resolved_type->kind == ResolvedType::F64 || expression.resolved_type->kind == ResolvedType::UNTYPED_FLOAT)) {
		auto* instruction = mirAppend<MirConstInstruction>(builder, expression.resolved_type);
		instruction->floating = static_cast<FloatLiteralExpression*>(expression.symbol->expression)->value;
		return instruction->result;
	}
	if (expression.symbol && expression.symbol->expression && expression.symbol->expression->kind == Expression::FLOAT_LITERAL && expression.resolved_type && (expression.resolved_type->kind == ResolvedType::F32 || expression.resolved_type->kind == ResolvedType::F64 || expression.resolved_type->kind == ResolvedType::UNTYPED_FLOAT)) {
		auto* instruction = mirAppend<MirConstInstruction>(builder, expression.resolved_type);
		instruction->floating = static_cast<FloatLiteralExpression*>(expression.symbol->expression)->value;
		return instruction->result;
	}
	if (expression.comptime_bytes && !expression.symbol && expression.resolved_type && (expression.resolved_type->kind == ResolvedType::F32 || expression.resolved_type->kind == ResolvedType::F64 || expression.resolved_type->kind == ResolvedType::UNTYPED_FLOAT)) {
		f64 value;
		if (expression.resolved_type->kind == ResolvedType::F32) { f32 source; memcpy(&source, expression.comptime_bytes, sizeof(source)); value = source; }
		else memcpy(&value, expression.comptime_bytes, sizeof(value));
		auto* instruction = mirAppend<MirConstInstruction>(builder, expression.resolved_type); instruction->floating = value; return instruction->result;
	}
	if (expression.comptime_bytes)
		return mirBuildComptimeConstant(builder, expression.comptime_bytes, expression.resolved_type);
	if (expression.symbol && expression.symbol->storage == Symbol::COMPTIME && expression.symbol->comptime_bytes)
		if (expression.resolved_type && (expression.resolved_type->kind == ResolvedType::F32 || expression.resolved_type->kind == ResolvedType::F64 || expression.resolved_type->kind == ResolvedType::UNTYPED_FLOAT)) {
			f64 value;
			if (expression.symbol->resolved_type && expression.symbol->resolved_type->kind == ResolvedType::F32) { f32 source; memcpy(&source, expression.symbol->comptime_bytes, sizeof(source)); value = source; }
			else memcpy(&value, expression.symbol->comptime_bytes, sizeof(value));
			auto* instruction = mirAppend<MirConstInstruction>(builder, expression.resolved_type); instruction->floating = value; return instruction->result;
		}
	if (expression.symbol && expression.symbol->storage == Symbol::COMPTIME && expression.symbol->comptime_bytes)
		return mirBuildComptimeConstant(builder, expression.symbol->comptime_bytes, expression.resolved_type ? expression.resolved_type : expression.symbol->resolved_type);
	if (!expression.slot) {
		FunctionExpression* function = expression.resolved_fn;
		if (!function && expression.symbol && expression.symbol->expression && expression.symbol->expression->kind == Expression::FUNCTION)
			function = static_cast<FunctionExpression*>(expression.symbol->expression);
		if (!function) return MIR_INVALID_ID;
		auto* value = mirAppend<MirConstInstruction>(builder, expression.resolved_type);
		value->integer = function->bytecode_index;
		return value->result;
	}
	if (expression.slot->storage == StorageSlot::GLOBAL) {
		if (expression.slot->type && expression.slot->type->kind == ResolvedType::NULLABLE) {
			if (expression.resolved_type && expression.resolved_type->kind == ResolvedType::NULLABLE) return MIR_INVALID_ID;
			auto* address = mirAppend<MirAddressInstruction>(builder, expression.resolved_type, MIR_OP_GLOBAL_ADDRESS);
			address->global_offset = expression.slot->offset;
			MirInstruction* index = mirAppendI32Zero(builder, expression.resolved_type);
			auto* load = mirAppend<MirLoadInstruction>(builder, expression.resolved_type);
			load->address = address->result;
			load->index = index->result;
			load->access = MIR_ACCESS_INDEXED;
			load->field_offset = 1;
			load->element_size = 1;
			load->extent = 1;
			return load->result;
		}
		auto* address = mirAppend<MirAddressInstruction>(builder, expression.resolved_type, MIR_OP_GLOBAL_ADDRESS);
		address->global_offset = expression.slot->offset;
		auto* load = mirAppend<MirLoadInstruction>(builder, expression.resolved_type);
		load->address = address->result;
		return load->result;
	}
	MirLocalId array_local = mirFindSlot(builder, expression.slot);
	ResolvedType* storage_type = array_local != MIR_INVALID_ID ? builder.function.locals[array_local].type : expression.slot->type;
	if (storage_type && storage_type->kind == ResolvedType::ARRAY && expression.resolved_type && expression.resolved_type->kind == ResolvedType::SLICE) {
		ArrayResolvedType* array = static_cast<ArrayResolvedType*>(storage_type);
		auto* address = mirAppend<MirAddressInstruction>(builder, storage_type, MIR_OP_LOCAL_ADDRESS);
		address->local = array_local;
		auto* make = mirAppend<MirSliceInstruction>(builder, expression.resolved_type);
		make->base = address->result;
		make->element_size = array->element_type ? typeByteSize(*array->element_type) : 0;
		make->length = (u32)array->size;
		return make->result;
	}
	MirLocalId nullable_local = mirFindSlot(builder, expression.slot);
	ResolvedType* nullable_type = nullable_local != MIR_INVALID_ID ? builder.function.locals[nullable_local].type : expression.slot->type;
	if (nullable_type && nullable_type->kind == ResolvedType::NULLABLE && expression.resolved_type && expression.resolved_type->kind != ResolvedType::NULLABLE) {
		MirLocalId local = nullable_local;
		ResolvedType* local_type = nullable_type;
		auto* address = mirAppend<MirAddressInstruction>(builder, local_type, MIR_OP_LOCAL_ADDRESS);
		address->local = local;
		MirInstruction* index = mirAppendI32Zero(builder, expression.resolved_type);
		auto* load = mirAppend<MirLoadInstruction>(builder, expression.resolved_type);
		load->address = address->result;
		load->index = index->result;
		load->access = MIR_ACCESS_INDEXED;
		load->field_offset = 1;
		load->element_size = 1;
		load->extent = 1;
		return load->result;
	}
	MirLocalId union_local = mirFindSlot(builder, expression.slot);
	ResolvedType* union_type = union_local != MIR_INVALID_ID ? builder.function.locals[union_local].type : expression.slot->type;
	if (union_type && union_type->kind == ResolvedType::UNION && expression.resolved_type && expression.resolved_type->kind != ResolvedType::UNION) {
		auto* address = mirAppend<MirAddressInstruction>(builder, union_type, MIR_OP_LOCAL_ADDRESS);
		address->local = union_local;
		MirInstruction* index = mirAppendI32Zero(builder, expression.resolved_type);
		auto* load = mirAppend<MirLoadInstruction>(builder, expression.resolved_type);
		load->address = address->result;
		load->index = index->result;
		load->access = MIR_ACCESS_INDEXED;
		load->field_offset = 4;
		load->element_size = 1;
		load->extent = 1;
		return load->result;
	}
	MirLocalId local = mirFindSlot(builder, expression.slot);
	if (local == MIR_INVALID_ID) return MIR_INVALID_ID;
	auto* address = mirAppend<MirAddressInstruction>(builder, expression.resolved_type, MIR_OP_LOCAL_ADDRESS);
	address->local = local;
	auto* load = mirAppend<MirLoadInstruction>(builder, expression.resolved_type);
	load->address = address->result;
	return load->result;
}

static MirValueId mirEnumMemberConstant(MirBuilder& builder, EnumResolvedType& en, ls_string_view name) {
	if (!en.decl) return MIR_INVALID_ID;
	i64 implicit_value = 0;
	for (i32 i = 0; i < (i32)en.decl->members.size(); ++i) {
		EnumMember& member = en.decl->members[i];
		i64 value = implicit_value;
		if (member.value && member.value->kind == Expression::INT_LITERAL)
			value = (i64)static_cast<IntLiteralExpression*>(member.value)->value;
		if (equalStrings(member.name, name)) {
			auto* instruction = mirAppend<MirConstInstruction>(builder, &en);
			instruction->integer = value;
			return instruction->result;
		}
		implicit_value = member.value && member.value->kind == Expression::INT_LITERAL ? (i64)static_cast<IntLiteralExpression*>(member.value)->value + 1 : implicit_value + 1;
	}
	return MIR_INVALID_ID;
}

static MirValueId mirBuildExpression(MirBuilder& builder, Expression* expression) {
	if (!expression) return MIR_INVALID_ID;
	switch (expression->kind) {
		case Expression::TERNARY: return mirBuildTernary(builder, *static_cast<TernaryExpression*>(expression));
		case Expression::INT_LITERAL: {
			auto* instruction = mirAppend<MirConstInstruction>(builder, expression->resolved_type);
			const u64 value = static_cast<IntLiteralExpression*>(expression)->value;
			if (expression->resolved_type && (expression->resolved_type->kind == ResolvedType::F32 || expression->resolved_type->kind == ResolvedType::F64))
				instruction->floating = (f64)value;
			else
				instruction->integer = (i64)value;
			return instruction->result;
		}
		case Expression::FLOAT_LITERAL: {
			auto* instruction = mirAppend<MirConstInstruction>(builder, expression->resolved_type);
			instruction->floating = static_cast<FloatLiteralExpression*>(expression)->value;
			return instruction->result;
		}
		case Expression::BOOL_LITERAL: {
			auto* instruction = mirAppend<MirConstInstruction>(builder, expression->resolved_type);
			instruction->integer = static_cast<BoolLiteralExpression*>(expression)->value ? 1 : 0;
			return instruction->result;
		}
		case Expression::STRING_LITERAL: {
			auto* instruction = mirAppend<MirConstInstruction>(builder, expression->resolved_type);
			instruction->string = static_cast<StringLiteralExpression*>(expression)->value;
			return instruction->result;
		}
		case Expression::ARRAY_LITERAL: {
			return mirBuildArrayLiteral(builder, *static_cast<ArrayLiteralExpression*>(expression), expression->resolved_type);
		}
		case Expression::UNDEFINED: {
			auto* instruction = mirAppend<MirUndefinedInstruction>(builder, expression->resolved_type);
			return instruction->result;
		}
		case Expression::NULL_LITERAL: {
			auto* instruction = mirAppend<MirConstInstruction>(builder, expression->resolved_type);
			if (expression->resolved_type && typeByteSize(*expression->resolved_type) > 8) {
				const u32 size = typeByteSize(*expression->resolved_type);
				u8* bytes = (u8*)builder.arena.allocate(builder.arena.user_data, size, 1);
				if (!bytes) return MIR_INVALID_ID;
				memset(bytes, 0, size);
				instruction->kind = MIR_CONST_BYTES;
				instruction->bytes = bytes;
				instruction->byte_size = size;
			}
			return instruction->result;
		}
		case Expression::TYPE_LITERAL: {
			// Type literals have a runtime representation used by reflection APIs.
			ResolvedType* i32_type = mirPrimitiveType(builder.arena, ResolvedType::I32);
			auto* instruction = mirAppend<MirConstInstruction>(builder, i32_type);
			instruction->integer = (i64)static_cast<TypeLiteralExpression*>(expression)->type;
			return instruction->result;
		}
		case Expression::SIZEOF: {
			const SizeofExpression& size_expr = *static_cast<SizeofExpression*>(expression);
			ResolvedType* const_type = expression->resolved_type;
			if (!const_type || const_type->kind == ResolvedType::UNTYPED_INT || typeByteSize(*const_type) == 0 || typeByteSize(*const_type) > 8)
				const_type = mirPrimitiveType(builder.arena, ResolvedType::I32);
			u8 bytes[8] = {};
			const u32 size = typeByteSize(*const_type);
			memcpy(bytes, &size_expr.value, size);
			return mirBuildComptimeConstant(builder, bytes, const_type);
		}
		case Expression::TYPE_MEMBER: {
			TypeMemberExpression& member = *static_cast<TypeMemberExpression*>(expression);
			if (member.kind == TypeMemberExpression::NAME) {
				auto* instruction = mirAppend<MirConstInstruction>(builder, expression->resolved_type);
				instruction->string = member.comptime_string;
				return instruction->result;
			}
			if (member.kind == TypeMemberExpression::LENGTH && member.reflected_type && member.reflected_type->kind == ResolvedType::ARRAY) {
				auto* instruction = mirAppend<MirConstInstruction>(builder, expression->resolved_type);
				instruction->integer = (i64)static_cast<ArrayResolvedType*>(member.reflected_type)->size;
				return instruction->result;
			}
			if (member.kind == TypeMemberExpression::MIN || member.kind == TypeMemberExpression::MAX) {
				const bool is_min = member.kind == TypeMemberExpression::MIN;
				auto* instruction = mirAppend<MirConstInstruction>(builder, expression->resolved_type);
				if (member.reflected_type && (member.reflected_type->kind == ResolvedType::F32 || member.reflected_type->kind == ResolvedType::F64)) {
					instruction->floating = is_min ? -DBL_MAX : DBL_MAX;
				} else {
					instruction->integer = 0;
					if (member.reflected_type) {
						switch (member.reflected_type->kind) {
							case ResolvedType::I8: instruction->integer = is_min ? -128 : 127; break;
							case ResolvedType::I16: instruction->integer = is_min ? -32768 : 32767; break;
							case ResolvedType::I32: instruction->integer = is_min ? (i64)-2147483648LL : 2147483647LL; break;
							case ResolvedType::I64:
							case ResolvedType::ISIZE: instruction->integer = is_min ? ((i64)-9223372036854775807LL - 1) : (i64)9223372036854775807LL; break;
							case ResolvedType::U8:
							case ResolvedType::BYTE: instruction->integer = is_min ? 0 : 255; break;
							case ResolvedType::U16: instruction->integer = is_min ? 0 : 65535; break;
							case ResolvedType::U32: instruction->integer = is_min ? 0 : 4294967295u; break;
							case ResolvedType::U64: instruction->integer = is_min ? 0 : (i64)0xffffffffffffffffULL; break;
							default: break;
						}
					}
				}
				return instruction->result;
			}
			if (member.comptime_value.kind == ComptimeValue::VALUE && member.comptime_value.value)
				return mirBuildComptimeConstant(builder, member.comptime_value.value, expression->resolved_type);
	if (expression->kind == Expression::BRACKET) {
		u32 offset = 0;
		Expression* current = expression;
		while (current && current->kind == Expression::BRACKET) {
			BracketExpression& bracket = *static_cast<BracketExpression*>(current);
			if (bracket.args.size() != 1 || !bracket.base || !bracket.base->resolved_type || bracket.base->resolved_type->kind != ResolvedType::ARRAY) return MIR_INVALID_ID;
			ArrayResolvedType* array = static_cast<ArrayResolvedType*>(bracket.base->resolved_type);
			if (!array->element_type || bracket.args[0]->kind != Expression::INT_LITERAL) return MIR_INVALID_ID;
			u64 index = static_cast<IntLiteralExpression*>(bracket.args[0])->value;
			offset += (u32)(index * typeByteSize(*array->element_type));
			current = bracket.base;
		}
		MirValueId base = mirBuildAddress(builder, current);
		if (base == MIR_INVALID_ID || offset == 0 || builder.block->instructions.empty()) return base;
		MirInstruction* last = builder.block->instructions[builder.block->instructions.size() - 1];
		if (last->result == base && (last->opcode == MIR_OP_LOCAL_ADDRESS || last->opcode == MIR_OP_GLOBAL_ADDRESS)) {
			MirAddressInstruction* address = static_cast<MirAddressInstruction*>(last);
			if (last->opcode == MIR_OP_LOCAL_ADDRESS) address->byte_offset += offset;
			else address->global_offset += offset;
		}
		return base;
	}
	return MIR_INVALID_ID;
}
		case Expression::IDENTIFIER: return mirBuildIdentifier(builder, *static_cast<IdentifierExpression*>(expression));
		case Expression::STRUCT_LITERAL: {
			StructLiteralExpression& literal = *static_cast<StructLiteralExpression*>(expression);
			if (!expression->resolved_type || expression->resolved_type->kind != ResolvedType::STRUCT) return MIR_INVALID_ID;
			StructResolvedType* structure = static_cast<StructResolvedType*>(expression->resolved_type);
			MirLocalId local = mirFunctionAddLocal(builder.function, expression->resolved_type, {}, false, true);
			auto* address = mirAppend<MirAddressInstruction>(builder, expression->resolved_type, MIR_OP_LOCAL_ADDRESS);
			address->local = local;
			u32 offset = 0;
			for (u32 i = 0; i < (u32)literal.values.size(); ++i) {
				ResolvedType* field_type = mirStructFieldType(structure, i);
				if (!field_type) return MIR_INVALID_ID;
				if (field_type->kind == ResolvedType::NULLABLE) {
					NullableResolvedType* nullable = static_cast<NullableResolvedType*>(field_type);
					auto* index = mirAppendI32Zero(builder, nullable->inner);
					auto* tag = mirAppend<MirConstInstruction>(builder, nullable->inner);
					tag->integer = literal.values[i] && literal.values[i]->kind != Expression::NULL_LITERAL ? 1 : 0;
					auto* tag_store = mirAppend<MirStoreInstruction>(builder, nullable->inner);
					tag_store->address = address->result;
					tag_store->index = index->result;
					tag_store->value = tag->result;
					tag_store->access = MIR_ACCESS_NULLABLE_TAG;
					tag_store->element_size = 1;
					tag_store->field_offset = offset;
					tag_store->extent = 1;
					if (tag->integer) {
						MirValueId payload_value = mirBuildExpressionAsType(builder, literal.values[i], nullable->inner);
						auto* payload = mirAppend<MirStoreInstruction>(builder, nullable->inner);
						payload->address = address->result;
						payload->index = index->result;
						payload->value = payload_value;
						payload->access = MIR_ACCESS_INDEXED;
						payload->element_size = 1;
						payload->field_offset = offset + 1;
						payload->extent = 1;
					}
					offset += typeByteSize(*field_type);
					continue;
				}
				if (field_type->kind == ResolvedType::UNION && mirBuildStoreAsUnion(builder, [&]() {
					auto* field_address = mirAppend<MirAddressInstruction>(builder, field_type, MIR_OP_LOCAL_ADDRESS);
					field_address->local = local;
					field_address->byte_offset = offset;
					return field_address->result;
				}(), field_type, literal.values[i])) {
					offset += typeByteSize(*field_type);
					continue;
				}
				ResolvedType* index_type = field_type;
				while (index_type && index_type->kind == ResolvedType::ARRAY) index_type = static_cast<ArrayResolvedType*>(index_type)->element_type;
				MirInstruction* index = mirAppendI32Zero(builder, index_type);
				MirValueId value = mirBuildExpression(builder, literal.values[i]);
				auto* store = mirAppend<MirStoreInstruction>(builder, field_type);
				store->address = address->result;
				store->index = index->result;
				store->value = value;
				store->access = MIR_ACCESS_INDEXED;
				store->element_size = typeByteSize(*field_type);
				store->field_offset = offset;
				store->extent = 1;
				offset += typeByteSize(*field_type);
			}
			auto* result = mirAppend<MirLoadInstruction>(builder, expression->resolved_type);
			result->address = address->result;
			return result->result;
		}
		case Expression::BRACKET: {
			BracketExpression& bracket = *static_cast<BracketExpression*>(expression);
			if (bracket.struct_field_name.begin && bracket.base && bracket.base->resolved_type && bracket.base->resolved_type->kind == ResolvedType::STRUCT) {
				StructResolvedType* structure = static_cast<StructResolvedType*>(bracket.base->resolved_type);
				u32 field_offset = 0;
				bool found = false;
				if (structure->decl) {
					for (u32 i = 0; i < (u32)structure->decl->fields.size(); ++i) {
						if (equalStrings(structure->decl->fields[i].name, bracket.struct_field_name)) { found = true; break; }
						ResolvedType* field_type = mirStructFieldType(structure, i);
						if (!field_type) break;
						field_offset += typeByteSize(*field_type);
					}
				}
				if (found) {
					MirValueId base = mirBuildAddress(builder, bracket.base);
					if (base != MIR_INVALID_ID) {
						MirInstruction* index_inst = mirAppendI32Zero(builder, bracket.resolved_type);
						auto* load = mirAppend<MirLoadInstruction>(builder, bracket.resolved_type);
						load->address = base;
						load->index = index_inst->result;
						load->access = MIR_ACCESS_INDEXED;
						load->element_size = typeByteSize(*bracket.base->resolved_type);
						load->field_offset = field_offset;
						load->extent = 1;
						return load->result;
					}
				}
			}
			if (bracket.base && bracket.base->kind == Expression::IDENTIFIER && bracket.args.size() == 1 && bracket.base->resolved_type &&
				bracket.base->resolved_type->kind == ResolvedType::ARRAY && bracket.args[0]->kind == Expression::INT_LITERAL) {
				IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(bracket.base);
				const u8* bytes = nullptr;
				if (identifier.comptime_bytes) bytes = identifier.comptime_bytes;
				else if (identifier.symbol && identifier.symbol->comptime_bytes) bytes = identifier.symbol->comptime_bytes;
				if (bytes) {
					ArrayResolvedType* array = static_cast<ArrayResolvedType*>(bracket.base->resolved_type);
					const u64 element_index = static_cast<IntLiteralExpression*>(bracket.args[0])->value;
					const u32 element_size = array->element_type ? typeByteSize(*array->element_type) : 0;
					const u32 byte_offset = (u32)element_index * element_size;
					if (element_size > 0 && byte_offset + element_size <= typeByteSize(*array))
						return mirBuildComptimeConstant(builder, bytes + byte_offset, bracket.resolved_type);
				}
			}
			MirValueId base = MIR_INVALID_ID;
			MirValueId index = MIR_INVALID_ID;
			ResolvedType* element_type = nullptr;
			u32 extent = 0;
			u32 field_offset = 0;
				if (bracket.base && bracket.base->kind == Expression::MEMBER) mirFindFieldOffset(builder, *static_cast<MemberExpression*>(bracket.base), field_offset);
			if (!mirBuildArrayAccess(builder, expression, base, index, element_type, extent)) {
				if (!bracket.base || bracket.base->resolved_type->kind != ResolvedType::SLICE || bracket.args.size() != 1) return MIR_INVALID_ID;
				SliceResolvedType* slice = static_cast<SliceResolvedType*>(bracket.base->resolved_type);
				base = mirBuildExpression(builder, bracket.base);
				index = mirBuildExpression(builder, bracket.args[0]);
				element_type = slice->element_type;
				auto* load = mirAppend<MirLoadInstruction>(builder, expression->resolved_type);
				load->address = base;
				load->index = index;
				load->access = MIR_ACCESS_SLICE_ELEMENT;
				load->element_size = element_type ? typeByteSize(*element_type) : 0;
				return load->result;
			}
			auto* load = mirAppend<MirLoadInstruction>(builder, expression->resolved_type);
			load->address = base;
			load->index = index;
			load->access = MIR_ACCESS_INDEXED;
			load->element_size = element_type ? typeByteSize(*element_type) : 0;
			load->field_offset = field_offset;
			load->extent = extent;
			return load->result;
		}
		case Expression::SLICE: {
			SliceExpression& slice = *static_cast<SliceExpression*>(expression);
			if (!slice.base || !slice.base->resolved_type) return MIR_INVALID_ID;
			ResolvedType* element_type = nullptr;
			MirValueId base_slice = MIR_INVALID_ID;
			u32 length = 0;
			bool array_element_view = false;
			if (slice.base->kind == Expression::SLICE) {
				base_slice = mirBuildExpression(builder, slice.base);
				if (slice.base->resolved_type->kind == ResolvedType::SLICE)
					element_type = static_cast<SliceResolvedType*>(slice.base->resolved_type)->element_type;
				else if (slice.base->resolved_type->kind == ResolvedType::ARRAY)
					element_type = static_cast<ArrayResolvedType*>(slice.base->resolved_type)->element_type;
			} else if (slice.base->resolved_type->kind == ResolvedType::ARRAY) {
				ArrayResolvedType* array = static_cast<ArrayResolvedType*>(slice.base->resolved_type);
				element_type = array->element_type;
				length = (u32)array->size;
				auto* full = mirAppend<MirSliceInstruction>(builder, expression->resolved_type);
				full->base = mirBuildAddress(builder, slice.base);
				if (getenv("MIR_TRACE") && full->base == MIR_INVALID_ID) fprintf(stderr, "[mir] SLICE array base invalid, kind=%d\n", (int)slice.base->kind);
				full->element_size = element_type ? typeByteSize(*element_type) : 0;
				full->length = length;
				base_slice = full->result;
			} else if (slice.base->resolved_type->kind == ResolvedType::SLICE) {
				SliceResolvedType* source = static_cast<SliceResolvedType*>(slice.base->resolved_type);
				element_type = source->element_type;
				base_slice = mirBuildExpression(builder, slice.base);
			} else {
				if (slice.base->kind == Expression::BRACKET) {
					BracketExpression& bracket = *static_cast<BracketExpression*>(slice.base);
					if (bracket.base && bracket.base->resolved_type && bracket.base->resolved_type->kind == ResolvedType::ARRAY && bracket.args.size() == 1) {
						ArrayResolvedType* array = static_cast<ArrayResolvedType*>(bracket.base->resolved_type);
						element_type = array->element_type;
						length = (u32)array->size;
						auto* full = mirAppend<MirSliceInstruction>(builder, expression->resolved_type);
						full->base = mirBuildAddress(builder, slice.base);
						full->element_size = typeByteSize(*element_type);
						full->length = 1;
					full->base_is_pointer = false;
						base_slice = full->result;
						array_element_view = true;
					}
				}
				if (array_element_view) {
					// The scalar array element is represented as a one-element slice view.
				} else {
					element_type = slice.base->resolved_type;
					length = 1;
					const bool base_is_pointer = slice.base->kind == Expression::DEREFERENCE;
					MirValueId base = base_is_pointer
						? mirBuildExpression(builder, static_cast<DereferenceExpression*>(slice.base)->subject)
						: mirBuildAddress(builder, slice.base);
					auto* full = mirAppend<MirSliceInstruction>(builder, expression->resolved_type);
					full->base = base;
					full->element_size = typeByteSize(*element_type);
					full->length = 1;
					full->base_is_pointer = base_is_pointer;
					base_slice = full->result;
				}
			}
			MirValueId begin = MIR_INVALID_ID;
			MirValueId end = MIR_INVALID_ID;
			ResolvedType* range_type = slice.begin && slice.begin->resolved_type ? slice.begin->resolved_type : element_type;
			if (array_element_view)
				begin = mirAppendI32Zero(builder, range_type)->result;
			else if (slice.begin)
				begin = mirBuildExpression(builder, slice.begin);
			else {
				auto* zero = mirAppend<MirConstInstruction>(builder, range_type);
				zero->integer = 0;
				begin = zero->result;
			}
			if (array_element_view) {
				auto* one = mirAppend<MirConstInstruction>(builder, range_type);
				one->integer = 1;
				end = mirAppend<MirBinaryInstruction>(builder, range_type, MIR_OP_ADD)->result;
				auto& add = static_cast<MirBinaryInstruction&>(*builder.block->instructions.back());
				add.lhs = begin;
				add.rhs = one->result;
			} else if (slice.end)
				end = mirBuildExpression(builder, slice.end);
			else {
				if (slice.base->resolved_type->kind == ResolvedType::SLICE) {
					auto* limit = mirAppend<MirUnaryInstruction>(builder, range_type, MIR_OP_SLICE_LENGTH);
					limit->operand = base_slice;
					end = limit->result;
				} else {
					auto* limit = mirAppend<MirConstInstruction>(builder, range_type);
					limit->integer = length;
					end = limit->result;
				}
			}
			if (!slice.begin && !slice.end) return base_slice;
			auto* sub = mirAppend<MirSliceInstruction>(builder, expression->resolved_type);
			sub->base = base_slice;
			sub->begin = begin;
			sub->end = end;
			sub->mode = MIR_SLICE_PARTIAL;
			sub->element_size = element_type ? typeByteSize(*element_type) : 0;
			return sub->result;
		}
		case Expression::MEMBER: {
			MemberExpression& member = *static_cast<MemberExpression*>(expression);
			if (member.expression && member.expression->kind == Expression::DEREFERENCE &&
				static_cast<DereferenceExpression*>(member.expression)->subject && static_cast<DereferenceExpression*>(member.expression)->subject->resolved_type &&
				static_cast<DereferenceExpression*>(member.expression)->subject->resolved_type->kind == ResolvedType::POINTER) {
				DereferenceExpression& dereference = *static_cast<DereferenceExpression*>(member.expression);
				ResolvedType* owner_type = dereference.subject && dereference.subject->resolved_type && dereference.subject->resolved_type->kind == ResolvedType::POINTER
					? static_cast<PointerResolvedType*>(dereference.subject->resolved_type)->inner : nullptr;
				if (owner_type && owner_type->kind != ResolvedType::UNION && dereference.subject->kind == Expression::IDENTIFIER) {
					IdentifierExpression* identifier = static_cast<IdentifierExpression*>(dereference.subject);
					ResolvedType* storage_type = identifier->slot ? identifier->slot->type : (identifier->symbol ? identifier->symbol->slot.type : nullptr);
					if (storage_type && storage_type->kind == ResolvedType::POINTER)
						owner_type = static_cast<PointerResolvedType*>(storage_type)->inner;
					MirLocalId local = identifier->slot ? mirFindSlot(builder, identifier->slot) : MIR_INVALID_ID;
					if (local != MIR_INVALID_ID && builder.function.locals[local].type && builder.function.locals[local].type->kind == ResolvedType::POINTER)
						owner_type = static_cast<PointerResolvedType*>(builder.function.locals[local].type)->inner;
				}
				if (owner_type && owner_type->kind == ResolvedType::UNION) {
					MirValueId pointer = mirBuildExpression(builder, dereference.subject);
					u32 field_offset = 0;
					if (pointer == MIR_INVALID_ID || !mirFindUnionFieldOffset(static_cast<UnionResolvedType*>(owner_type), member.name, field_offset)) return MIR_INVALID_ID;
					MirValueId index = mirAppendZero(builder, dereference.subject->resolved_type);
					auto* load = mirAppend<MirLoadInstruction>(builder, expression->resolved_type);
					load->address = pointer;
					load->index = index;
					load->access = MIR_ACCESS_POINTER;
					load->element_size = typeByteSize(*owner_type);
					load->field_offset = field_offset;
					load->extent = 1;
					return load->result;
				}
			}
			if (member.resolved_symbol && member.resolved_symbol->comptime_bytes)
				return mirBuildComptimeConstant(builder, member.resolved_symbol->comptime_bytes, expression->resolved_type);
			if (member.resolved_symbol && member.resolved_symbol->expression && member.resolved_symbol->expression->kind == Expression::FUNCTION) {
				FunctionExpression* function = static_cast<FunctionExpression*>(member.resolved_symbol->expression);
				auto* value = mirAppend<MirConstInstruction>(builder, expression->resolved_type);
				value->integer = function->bytecode_index;
				return value->result;
			}
			if (member.comptime_value.kind == ComptimeValue::VALUE && member.comptime_value.value) {
				ResolvedType* value_type = member.comptime_value.type;
				if (!value_type) value_type = member.resolved_type;
				if (member.resolved_type && value_type && value_type->kind == ResolvedType::UNTYPED_INT)
					value_type = member.resolved_type;
				return mirBuildComptimeConstant(builder, member.comptime_value.value, value_type ? value_type : member.resolved_type);
			}
			if (!member.expression && member.resolved_type && member.resolved_type->kind == ResolvedType::ENUM) {
				EnumResolvedType* en = static_cast<EnumResolvedType*>(member.resolved_type);
				MirValueId value = mirEnumMemberConstant(builder, *en, member.name);
				if (value != MIR_INVALID_ID) return value;
			}
			if (member.expression && member.expression->resolved_type && member.expression->resolved_type->kind == ResolvedType::META) {
				MetaType* meta = static_cast<MetaType*>(member.expression->resolved_type);
				if (meta->inner && meta->inner->kind == ResolvedType::ENUM) {
					EnumResolvedType* en = static_cast<EnumResolvedType*>(meta->inner);
					MirValueId value = mirEnumMemberConstant(builder, *en, member.name);
					if (value != MIR_INVALID_ID) return value;
				}
			}
			if (equalStrings(member.name, makeStringView("length")) && member.expression && member.expression->resolved_type && member.expression->resolved_type->kind == ResolvedType::ARRAY) {
				ArrayResolvedType* array = static_cast<ArrayResolvedType*>(member.expression->resolved_type);
				auto* length = mirAppend<MirConstInstruction>(builder, expression->resolved_type);
				length->integer = array->size;
				return length->result;
			}

			if (equalStrings(member.name, makeStringView("length")) && member.expression && member.expression->resolved_type && member.expression->resolved_type->kind == ResolvedType::SLICE) {
				MirValueId slice = mirBuildExpression(builder, member.expression);
				auto* length = mirAppend<MirUnaryInstruction>(builder, expression->resolved_type, MIR_OP_SLICE_LENGTH);
				length->operand = slice;
				return length->result;
			}

			MemberExpression* field = &member;
			u32 field_offset = 0;
			if (field->expression && field->expression->kind == Expression::BRACKET && field->expression->resolved_type && field->expression->resolved_type->kind == ResolvedType::STRUCT) {
				BracketExpression& bracket = *static_cast<BracketExpression*>(field->expression);
				if (bracket.base && bracket.base->resolved_type && bracket.base->resolved_type->kind == ResolvedType::SLICE && bracket.args.size() == 1 && mirFindFieldOffset(builder, *field, field_offset)) {
					MirValueId slice = mirBuildExpression(builder, bracket.base);
					MirValueId index = mirBuildExpression(builder, bracket.args[0]);
					auto* load = mirAppend<MirLoadInstruction>(builder, expression->resolved_type);
					load->address = slice;
					load->index = index;
					load->access = MIR_ACCESS_SLICE_FIELD;
					load->element_size = typeByteSize(*field->expression->resolved_type);
					load->field_offset = field_offset;
					load->extent = typeByteSize(*expression->resolved_type);
					return load->result;
				}
			}
			
			const bool pointer_member = field->expression && field->expression->kind == Expression::DEREFERENCE;
			MirValueId base = pointer_member ? mirBuildExpression(builder, static_cast<DereferenceExpression*>(field->expression)->subject) : mirBuildAddress(builder, field->expression);
			if (!pointer_member && field->expression && field->expression->resolved_type && field->expression->resolved_type->kind == ResolvedType::UNION) {
				base = mirBuildAddress(builder, field->expression);
			}
			if (base == MIR_INVALID_ID && field->resolved_symbol && field->resolved_symbol->slot.storage == StorageSlot::GLOBAL && field->resolved_symbol->slot.type &&
				field->resolved_symbol->slot.type->kind != ResolvedType::NULLABLE) {
				auto* address = mirAppend<MirAddressInstruction>(builder, expression->resolved_type, MIR_OP_GLOBAL_ADDRESS);
				address->global_offset = field->resolved_symbol->slot.offset;
				auto* load = mirAppend<MirLoadInstruction>(builder, expression->resolved_type);
				load->address = address->result;
				return load->result;
			}
			if (base == MIR_INVALID_ID || !mirFindFieldOffset(builder, *field, field_offset)) {
				if (!field->expression || !field->expression->resolved_type || !mirFindFieldOffset(builder, *field, field_offset)) return MIR_INVALID_ID;
				MirValueId value = mirBuildExpression(builder, field->expression);
				if (value == MIR_INVALID_ID) return MIR_INVALID_ID;
				MirLocalId temp = mirFunctionAddLocal(builder.function, field->expression->resolved_type, {}, false, true);
				auto* temp_address = mirAppend<MirAddressInstruction>(builder, field->expression->resolved_type, MIR_OP_LOCAL_ADDRESS);
				temp_address->local = temp;
				auto* temp_store = mirAppend<MirStoreInstruction>(builder, field->expression->resolved_type);
				temp_store->address = temp_address->result;
				temp_store->value = value;
				MirInstruction* index = mirAppendI32Zero(builder, mirIndexType(expression->resolved_type));
				auto* load = mirAppend<MirLoadInstruction>(builder, expression->resolved_type);
				load->address = temp_address->result;
				load->index = index->result;
				load->access = MIR_ACCESS_INDEXED;
				load->element_size = typeByteSize(*expression->resolved_type);
				load->field_offset = field_offset;
				load->extent = 1;
				return load->result;
			}
			MirInstruction* index = mirAppendI32Zero(builder, mirPrimitiveType(builder.arena, ResolvedType::I64));
			auto* load = mirAppend<MirLoadInstruction>(builder, expression->resolved_type);
			load->address = base;
			load->index = index->result;
			load->access = pointer_member ? MIR_ACCESS_POINTER : MIR_ACCESS_INDEXED;
			load->element_size = pointer_member ? typeByteSize(*field->expression->resolved_type) : (field->expression && field->expression->resolved_type && field->expression->resolved_type->kind == ResolvedType::UNION ? typeByteSize(*field->expression->resolved_type) : typeByteSize(*expression->resolved_type));
			load->field_offset = field_offset;
			load->extent = 1;
			return load->result;
		}
		case Expression::ADDRESSOF: {
			AddressOfExpression& address = *static_cast<AddressOfExpression*>(expression);
			if (address.subject && address.subject->kind == Expression::DEREFERENCE) return mirBuildExpression(builder, address.subject);
			MirValueId base = mirBuildAddress(builder, address.subject);
			if (base == MIR_INVALID_ID) return MIR_INVALID_ID;
			MirInstruction* base_instr = builder.block->instructions.empty() ? nullptr : builder.block->instructions.back();
			if (!base_instr || base_instr->result != base) return MIR_INVALID_ID;
			auto* reference = mirAppend<MirAddressInstruction>(builder, expression->resolved_type, MIR_OP_REFERENCE);
			if (base_instr->opcode == MIR_OP_LOCAL_ADDRESS) {
				MirAddressInstruction& addr = static_cast<MirAddressInstruction&>(*base_instr);
				reference->local = addr.local;
				reference->byte_offset = addr.byte_offset;
			} else if (base_instr->opcode == MIR_OP_GLOBAL_ADDRESS) {
				reference->global_offset = static_cast<MirAddressInstruction&>(*base_instr).global_offset;
			} else {
				return MIR_INVALID_ID;
			}
			return reference->result;
		}
		case Expression::DEREFERENCE: {
			DereferenceExpression& dereference = *static_cast<DereferenceExpression*>(expression);
			MirValueId pointer = mirBuildExpression(builder, dereference.subject);
			if (pointer == MIR_INVALID_ID) return MIR_INVALID_ID;
			if (expression->resolved_type && expression->resolved_type->kind == ResolvedType::UNION) {
				auto* load = mirAppend<MirLoadInstruction>(builder, expression->resolved_type);
				load->address = pointer;
				load->index = mirAppendZero(builder, dereference.subject->resolved_type);
				load->access = MIR_ACCESS_POINTER;
				load->element_size = typeByteSize(*expression->resolved_type);
				load->field_offset = 0;
				load->extent = 1;
				return load->result;
			}
			MirValueId index = mirAppendZero(builder, dereference.subject->resolved_type);
			auto* load = mirAppend<MirLoadInstruction>(builder, expression->resolved_type);
			load->address = pointer;
			load->index = index;
			load->access = MIR_ACCESS_POINTER;
			load->element_size = typeByteSize(*expression->resolved_type);
			load->field_offset = 0;
			load->extent = 1;
			return load->result;
		}
		case Expression::CAST: {
			CastExpression& cast = *static_cast<CastExpression*>(expression);
			ResolvedType* src_type = cast.expression ? cast.expression->resolved_type : nullptr;
			ResolvedType* dst_type = expression->resolved_type;
			if (src_type && dst_type && src_type->kind == ResolvedType::SLICE && dst_type->kind == ResolvedType::SLICE) {
				SliceResolvedType* src_slice = static_cast<SliceResolvedType*>(src_type);
				SliceResolvedType* dst_slice = static_cast<SliceResolvedType*>(dst_type);
				const u32 src_size = src_slice->element_type ? typeByteSize(*src_slice->element_type) : 0;
				const u32 dst_size = dst_slice->element_type ? typeByteSize(*dst_slice->element_type) : 0;
				MirValueId value = mirBuildExpression(builder, cast.expression);
				if (value == MIR_INVALID_ID) return MIR_INVALID_ID;
				MirLocalId temp = mirFunctionAddLocal(builder.function, src_type, {}, false, true);
				auto* temp_address = mirAppend<MirAddressInstruction>(builder, src_type, MIR_OP_LOCAL_ADDRESS);
				temp_address->local = temp;
				auto* store = mirAppend<MirStoreInstruction>(builder, src_type);
				store->address = temp_address->result;
				store->value = value;
				auto* length = mirAppend<MirUnaryInstruction>(builder, src_type, MIR_OP_SLICE_LENGTH);
				length->operand = value;
				MirValueId new_length = length->result;
				ResolvedType* i64_type = mirPrimitiveType(builder.arena, ResolvedType::I64);
				if (src_size > dst_size && dst_size != 0 && src_size % dst_size == 0) {
					auto* ratio = mirAppend<MirConstInstruction>(builder, i64_type);
					ratio->integer = src_size / dst_size;
					auto* op = mirAppend<MirBinaryInstruction>(builder, i64_type, MIR_OP_MUL);
					op->lhs = new_length;
					op->rhs = ratio->result;
					new_length = op->result;
				} else if (dst_size > src_size && src_size != 0 && dst_size % src_size == 0) {
					auto* ratio = mirAppend<MirConstInstruction>(builder, i64_type);
					ratio->integer = dst_size / src_size;
					auto* op = mirAppend<MirBinaryInstruction>(builder, i64_type, MIR_OP_DIV);
					op->lhs = new_length;
					op->rhs = ratio->result;
					new_length = op->result;
				}
				MirInstruction* index = mirAppendI32Zero(builder, i64_type);
				auto* len_store = mirAppend<MirStoreInstruction>(builder, i64_type);
				len_store->address = temp_address->result;
				len_store->index = index->result;
				len_store->value = new_length;
				len_store->access = MIR_ACCESS_INDEXED;
				len_store->element_size = 1;
				len_store->field_offset = 8;
				len_store->extent = 1;
				auto* load = mirAppend<MirLoadInstruction>(builder, dst_type);
				load->address = temp_address->result;
				load->element_size = 16;
				return load->result;
			}
			MirValueId value = mirBuildExpression(builder, cast.expression);
			if (value == MIR_INVALID_ID) return MIR_INVALID_ID;
			auto* instruction = mirAppend<MirCastInstruction>(builder, expression->resolved_type);
			instruction->operand_type = cast.expression ? cast.expression->resolved_type : nullptr;
			instruction->operand = value;
			return instruction->result;
		}
		case Expression::UNARY: {
			UnaryExpression& unary = *static_cast<UnaryExpression*>(expression);
			if (unary.resolved_fn) {
				FunctionExpression* operator_fn = unary.resolved_fn;
				FunctionResolvedType* operator_type =
					operator_fn->resolved_type && operator_fn->resolved_type->kind == ResolvedType::FUNCTION ? static_cast<FunctionResolvedType*>(operator_fn->resolved_type) : nullptr;
				MirValueId* arguments = nullptr;
				u32* argument_sizes = nullptr;
				const u32 argument_count = 1;
				arguments = (MirValueId*)builder.arena.allocate(builder.arena.user_data, sizeof(MirValueId) * argument_count, alignof(MirValueId));
				argument_sizes = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * argument_count, alignof(u32));
				ResolvedType* param_type = operator_type && operator_type->params.size() > 0 ? operator_type->params[0].type : nullptr;
				arguments[0] = param_type ? mirBuildExpressionAsType(builder, unary.expression, param_type) : mirBuildExpression(builder, unary.expression);
				argument_sizes[0] = param_type ? typeByteSize(*param_type) : (unary.expression->resolved_type ? typeByteSize(*unary.expression->resolved_type) : 0);
				auto* instruction = mirAppend<MirCallInstruction>(builder, expression->resolved_type);
				instruction->arguments.values = arguments;
				instruction->arguments.sizes = argument_sizes;
				instruction->arguments.count = argument_count;
				instruction->args_size = argument_sizes[0];
				instruction->function = operator_fn->bytecode_index;
				return instruction->result;
			}
			MirValueId value = mirBuildExpression(builder, unary.expression);
			MirOpcode opcode = unary.op == Token::NOT ? MIR_OP_NOT : MIR_OP_NEG;
			auto* instruction = mirAppend<MirUnaryInstruction>(builder, expression->resolved_type, opcode);
			instruction->operand = value;
			return instruction->result;
		}
		case Expression::BINARY: {
			BinaryExpression& binary = *static_cast<BinaryExpression*>(expression);
			if (binary.resolved_fn) {
				FunctionExpression* operator_fn = binary.resolved_fn;
				FunctionResolvedType* operator_type =
					operator_fn->resolved_type && operator_fn->resolved_type->kind == ResolvedType::FUNCTION ? static_cast<FunctionResolvedType*>(operator_fn->resolved_type) : nullptr;
				MirValueId* arguments = nullptr;
				u32* argument_sizes = nullptr;
				const u32 argument_count = 2;
				arguments = (MirValueId*)builder.arena.allocate(builder.arena.user_data, sizeof(MirValueId) * argument_count, alignof(MirValueId));
				argument_sizes = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * argument_count, alignof(u32));
				ResolvedType* lhs_type = operator_type && operator_type->params.size() > 0 ? operator_type->params[0].type : nullptr;
				ResolvedType* rhs_type = operator_type && operator_type->params.size() > 1 ? operator_type->params[1].type : nullptr;
				arguments[0] = lhs_type ? mirBuildExpressionAsType(builder, binary.lhs, lhs_type) : mirBuildExpression(builder, binary.lhs);
				arguments[1] = rhs_type ? mirBuildExpressionAsType(builder, binary.rhs, rhs_type) : mirBuildExpression(builder, binary.rhs);
				argument_sizes[0] = lhs_type ? typeByteSize(*lhs_type) : (binary.lhs->resolved_type ? typeByteSize(*binary.lhs->resolved_type) : 0);
				argument_sizes[1] = rhs_type ? typeByteSize(*rhs_type) : (binary.rhs->resolved_type ? typeByteSize(*binary.rhs->resolved_type) : 0);
				auto* instruction = mirAppend<MirCallInstruction>(builder, expression->resolved_type);
				instruction->arguments.values = arguments;
				instruction->arguments.sizes = argument_sizes;
				instruction->arguments.count = argument_count;
				instruction->args_size = argument_sizes[0] + argument_sizes[1];
				instruction->function = operator_fn->bytecode_index;
				return instruction->result;
			}
			if (binary.op == Token::AND || binary.op == Token::OR) return mirBuildLogical(builder, binary);
			if (binary.op == Token::IS) {
				MirValueId subject = mirBuildAddress(builder, binary.lhs);
				ResolvedType* subject_type = binary.lhs ? binary.lhs->resolved_type : nullptr;
				if (subject == MIR_INVALID_ID || !subject_type || subject_type->kind != ResolvedType::UNION) return MIR_INVALID_ID;
				ResolvedType* member = binary.rhs && binary.rhs->resolved_type && binary.rhs->resolved_type->kind == ResolvedType::META
					? static_cast<MetaType*>(binary.rhs->resolved_type)->inner : nullptr;
				if (!member) return MIR_INVALID_ID;
				i32 index = -1;
				for (i32 i = 0; i < static_cast<UnionResolvedType*>(subject_type)->members.size(); ++i)
					if (mirSameType(static_cast<UnionResolvedType*>(subject_type)->members[i], member)) { index = i; break; }
				if (index < 0) return MIR_INVALID_ID;
				ResolvedType* i32_type = mirPrimitiveType(builder.arena, ResolvedType::I32);
				auto* index_value = mirAppendI32Zero(builder, i32_type);
				auto* tag = mirAppend<MirLoadInstruction>(builder, i32_type);
				tag->address = subject;
				tag->index = index_value->result;
				tag->access = binary.lhs->kind == Expression::DEREFERENCE ? MIR_ACCESS_POINTER : MIR_ACCESS_INDEXED;
				tag->element_size = 4;
				tag->extent = 1;
				auto* expected = mirAppend<MirConstInstruction>(builder, tag->type);
				expected->integer = index;
				auto* result = mirAppend<MirBinaryInstruction>(builder, binary.resolved_type, MIR_OP_EQ);
				result->operand_type = tag->type;
				result->lhs = tag->result;
				result->rhs = expected->result;
				return result->result;
			}
			if ((binary.op == Token::EQUAL_EQUAL || binary.op == Token::BANG_EQUAL) && binary.lhs && binary.lhs->kind == Expression::BRACKET && binary.rhs &&
				binary.rhs->kind == Expression::NULL_LITERAL) {
				MirValueId base = MIR_INVALID_ID;
				MirValueId index = MIR_INVALID_ID;
				ResolvedType* element_type = nullptr;
				u32 extent = 0;
				if (mirBuildArrayAccess(builder, binary.lhs, base, index, element_type, extent) && element_type && element_type->kind == ResolvedType::NULLABLE) {
					MirInstruction* tag_index = mirAppendI32Zero(builder, static_cast<NullableResolvedType*>(element_type)->inner);
					auto* value = mirAppend<MirNullableInstruction>(builder, binary.resolved_type);
					value->address = base;
					value->index = tag_index->result;
					if (binary.op == Token::BANG_EQUAL) return value->result;
					auto* result = mirAppend<MirUnaryInstruction>(builder, binary.resolved_type, MIR_OP_NOT);
					result->operand = value->result;
					return result->result;
				}
			}
			if ((binary.op == Token::EQUAL_EQUAL || binary.op == Token::BANG_EQUAL) && binary.lhs && binary.lhs->kind == Expression::IDENTIFIER && binary.rhs &&
				binary.rhs->kind == Expression::NULL_LITERAL) {
				IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(binary.lhs);
				if (!identifier.slot) return MIR_INVALID_ID;
				ResolvedType* nullable_type = identifier.slot->type;
				MirValueId address = MIR_INVALID_ID;
				if (identifier.slot->storage == StorageSlot::GLOBAL) {
					if (!nullable_type || nullable_type->kind == ResolvedType::NULLABLE) {
						auto* addr = mirAppend<MirAddressInstruction>(builder, nullable_type, MIR_OP_GLOBAL_ADDRESS);
						addr->global_offset = identifier.slot->offset;
						address = addr->result;
					}
				} else {
					MirLocalId local = mirFindSlot(builder, identifier.slot);
					nullable_type = local != MIR_INVALID_ID ? builder.function.locals[local].type : identifier.slot->type;
					if (nullable_type && nullable_type->kind == ResolvedType::NULLABLE) {
						auto* addr = mirAppend<MirAddressInstruction>(builder, nullable_type, MIR_OP_LOCAL_ADDRESS);
						addr->local = local;
						address = addr->result;
					}
				}
				if (address == MIR_INVALID_ID || !nullable_type || nullable_type->kind != ResolvedType::NULLABLE) return MIR_INVALID_ID;
				NullableResolvedType* nullable = static_cast<NullableResolvedType*>(nullable_type);
				MirInstruction* index = mirAppendI32Zero(builder, nullable->inner);
				auto* value = mirAppend<MirNullableInstruction>(builder, binary.resolved_type);
				value->address = address;
				value->index = index->result;
				if (binary.op == Token::BANG_EQUAL) return value->result;
				auto* result = mirAppend<MirUnaryInstruction>(builder, binary.resolved_type, MIR_OP_NOT);
				result->operand = value->result;
				return result->result;
			}
			MirValueId lhs = mirBuildExpression(builder, binary.lhs);
			MirValueId rhs = mirBuildExpression(builder, binary.rhs);
			auto* instruction = mirAppend<MirBinaryInstruction>(builder, expression->resolved_type, mirBinaryOpcode(binary.op));
			instruction->operand_type = binary.lhs ? binary.lhs->resolved_type : nullptr;
			instruction->lhs = lhs;
			instruction->rhs = rhs;
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
			FunctionResolvedType* direct_type =
				direct && direct->resolved_type && direct->resolved_type->kind == ResolvedType::FUNCTION ? static_cast<FunctionResolvedType*>(direct->resolved_type) : nullptr;
			Expression* receiver = nullptr;
			if (direct && call.callee && call.callee->kind == Expression::MEMBER) {
				MemberExpression* member = static_cast<MemberExpression*>(call.callee);
				if (member->expression && member->expression->resolved_type) receiver = member->expression;
			}
			const u32 argument_count = (u32)call.args.size() + (receiver ? 1u : 0u);
			MirValueId* arguments = nullptr;
			u32* argument_sizes = nullptr;
			u32 runtime_count = 0;
			if (argument_count) {
				for (u32 i = 0; i < argument_count; ++i) {
					if (direct_type && i < (u32)direct_type->params.size() && direct_type->params[i].is_comptime) continue;
					++runtime_count;
				}
				if (runtime_count) {
					arguments = (MirValueId*)builder.arena.allocate(builder.arena.user_data, sizeof(MirValueId) * runtime_count, alignof(MirValueId));
					argument_sizes = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * runtime_count, alignof(u32));
					u32 slot = 0;
					for (u32 i = 0; i < argument_count; ++i) {
						if (direct_type && i < (u32)direct_type->params.size() && direct_type->params[i].is_comptime) continue;
						Expression* argument = receiver && i == 0 ? receiver : call.args[(i32)(i - (receiver ? 1u : 0u))];
						ResolvedType* parameter_type = direct_type && i < (u32)direct_type->params.size() ? direct_type->params[i].type : nullptr;
						const bool preserve_pointer_receiver = receiver && i == 0 && parameter_type && parameter_type->kind == ResolvedType::POINTER;
						if (preserve_pointer_receiver && argument->kind == Expression::DEREFERENCE) argument = static_cast<DereferenceExpression*>(argument)->subject;
						arguments[slot] = parameter_type && !preserve_pointer_receiver ? mirBuildExpressionAsType(builder, argument, parameter_type) : mirBuildExpression(builder, argument);
						argument_sizes[slot] = parameter_type ? typeByteSize(*parameter_type) : (argument->resolved_type ? typeByteSize(*argument->resolved_type) : 0);
						++slot;
					}
				}
			}
			auto* instruction = mirAppend<MirCallInstruction>(builder, expression->resolved_type);
			instruction->arguments.values = arguments;
			instruction->arguments.sizes = argument_sizes;
			instruction->arguments.count = runtime_count;
			for (u32 i = 0; i < runtime_count; ++i) instruction->args_size += argument_sizes[i];
			if (direct)
				instruction->function = direct->bytecode_index;
			else {
				instruction->call_target = MIR_CALL_INDIRECT;
				instruction->callee = callee;
			}
			if (call.callee && call.callee->kind == Expression::IDENTIFIER)
				instruction->call_name = static_cast<IdentifierExpression*>(call.callee)->name;
			else if (call.callee && call.callee->kind == Expression::MEMBER)
				instruction->call_name = static_cast<MemberExpression*>(call.callee)->name;
			return instruction->result;
		}
		default:
			if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] expr kind=%d unhandled\n", (int)expression->kind);
			return MIR_INVALID_ID;
	}
}

static void mirBuildStatement(MirBuilder& builder, Statement* statement) {
	if (!statement) return;
	MirSourceScope source_scope(builder, statement->token);
	if (builder.block->has_terminator && statement->kind != Statement::BLOCK) return;
	switch (statement->kind) {
		case Statement::BLOCK: {
			BlockStatement& block = *static_cast<BlockStatement*>(statement);
			builder.defer_marks.push((u32)builder.deferreds.size());
			for (Statement* child : block.statements) mirBuildStatement(builder, child);
			const u32 mark = builder.defer_marks.back();
			if (!builder.block->has_terminator) {
				for (i32 i = (i32)builder.deferreds.size() - 1; i >= (i32)mark; --i) mirBuildStatement(builder, builder.deferreds[(u32)i]);
			}
			while (builder.deferreds.size() > (i32)mark) builder.deferreds.pop_back();
			builder.defer_marks.pop_back();
			break;
		}
		case Statement::EXPRESSION: mirBuildExpression(builder, static_cast<ExpressionStatement*>(statement)->expression); break;
		case Statement::RETURN: {
			ReturnStatement& result = *static_cast<ReturnStatement*>(statement);
			const MirValueId value = result.expression ? mirBuildExpressionAsType(builder, result.expression, builder.function.return_type) : MIR_INVALID_ID;
			mirEmitActiveDefers(builder);
			builder.block->terminator.kind = result.expression ? MIR_TERM_RETURN_VALUE : MIR_TERM_RETURN;
			builder.block->terminator.value = value;
			builder.block->has_terminator = true;
			break;
		}
		case Statement::ASSIGN: {
			AssignStatement& assignment = *static_cast<AssignStatement*>(statement);
			if (assignment.op != Token::EQUAL && assignment.resolved_op_fn) {
				FunctionExpression* operator_fn = assignment.resolved_op_fn;
				FunctionResolvedType* operator_type =
					operator_fn->resolved_type && operator_fn->resolved_type->kind == ResolvedType::FUNCTION ? static_cast<FunctionResolvedType*>(operator_fn->resolved_type) : nullptr;
				MirValueId* arguments = (MirValueId*)builder.arena.allocate(builder.arena.user_data, sizeof(MirValueId) * 2, alignof(MirValueId));
				u32* argument_sizes = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * 2, alignof(u32));
				ResolvedType* lhs_type = operator_type && operator_type->params.size() > 0 ? operator_type->params[0].type : nullptr;
				ResolvedType* rhs_type = operator_type && operator_type->params.size() > 1 ? operator_type->params[1].type : nullptr;
				arguments[0] = lhs_type ? mirBuildExpressionAsType(builder, assignment.lhs, lhs_type) : mirBuildExpression(builder, assignment.lhs);
				arguments[1] = rhs_type ? mirBuildExpressionAsType(builder, assignment.rhs, rhs_type) : mirBuildExpression(builder, assignment.rhs);
				argument_sizes[0] = lhs_type ? typeByteSize(*lhs_type) : (assignment.lhs->resolved_type ? typeByteSize(*assignment.lhs->resolved_type) : 0);
				argument_sizes[1] = rhs_type ? typeByteSize(*rhs_type) : (assignment.rhs->resolved_type ? typeByteSize(*assignment.rhs->resolved_type) : 0);
				auto* call = mirAppend<MirCallInstruction>(builder, assignment.lhs->resolved_type);
				call->arguments.values = arguments;
				call->arguments.sizes = argument_sizes;
				call->arguments.count = 2;
				call->args_size = argument_sizes[0] + argument_sizes[1];
				call->function = operator_fn->bytecode_index;
				MirValueId target = mirBuildAddress(builder, assignment.lhs);
				if (target != MIR_INVALID_ID) {
					auto* store = mirAppend<MirStoreInstruction>(builder, assignment.lhs->resolved_type);
					store->address = target;
					store->value = call->result;
				}
				break;
			}
			if (assignment.lhs && assignment.lhs->kind == Expression::DEREFERENCE) {
				DereferenceExpression& dereference = *static_cast<DereferenceExpression*>(assignment.lhs);
				MirValueId pointer = mirBuildExpression(builder, dereference.subject);
				if (pointer == MIR_INVALID_ID) break;
				MirValueId index = mirAppendZero(builder, dereference.subject->resolved_type);
				MirValueId old_value = MIR_INVALID_ID;
				if (assignment.op != Token::EQUAL) {
					auto* load = mirAppend<MirLoadInstruction>(builder, assignment.lhs->resolved_type);
					load->address = pointer;
					load->index = index;
					load->access = MIR_ACCESS_POINTER;
					load->element_size = typeByteSize(*assignment.lhs->resolved_type);
					load->field_offset = 0;
					load->extent = 1;
					old_value = load->result;
				}
				if (assignment.op == Token::EQUAL && assignment.lhs->resolved_type && assignment.lhs->resolved_type->kind == ResolvedType::UNION &&
					mirBuildStoreAsUnion(builder, pointer, assignment.lhs->resolved_type, assignment.rhs, true)) break;
				MirValueId value = mirBuildExpressionAsType(builder, assignment.rhs, assignment.lhs->resolved_type);
				if (assignment.op != Token::EQUAL) {
					Token::Type op = assignment.op == Token::PLUS_EQUAL		? Token::PLUS
									 : assignment.op == Token::MINUS_EQUAL ? Token::MINUS
									 : assignment.op == Token::STAR_EQUAL	? Token::STAR
																		   : Token::SLASH;
					auto* operation = mirAppend<MirBinaryInstruction>(builder, assignment.lhs->resolved_type, mirBinaryOpcode(op));
					operation->lhs = old_value;
					operation->rhs = value;
					value = operation->result;
				}
				auto* store = mirAppend<MirStoreInstruction>(builder, assignment.lhs->resolved_type);
				store->address = pointer;
				store->index = index;
				store->value = value;
				store->access = MIR_ACCESS_POINTER;
				store->element_size = typeByteSize(*assignment.lhs->resolved_type);
				store->field_offset = 0;
				store->extent = 1;
				break;
			}
			if (assignment.lhs && assignment.lhs->kind == Expression::MEMBER) {
				MemberExpression& member = *static_cast<MemberExpression*>(assignment.lhs);
				u32 field_offset = 0;
				if (member.expression && member.expression->kind == Expression::BRACKET && member.expression->resolved_type && member.expression->resolved_type->kind == ResolvedType::STRUCT) {
					BracketExpression& bracket = *static_cast<BracketExpression*>(member.expression);
					if (bracket.base && bracket.base->resolved_type && bracket.base->resolved_type->kind == ResolvedType::SLICE && bracket.args.size() == 1 &&
						mirFindFieldOffset(builder, member, field_offset)) {
						MirValueId slice = mirBuildExpression(builder, bracket.base);
						MirValueId index_value = mirBuildExpression(builder, bracket.args[0]);
						MirValueId old_value = MIR_INVALID_ID;
						if (assignment.op != Token::EQUAL) {
							auto* load = mirAppend<MirLoadInstruction>(builder, assignment.lhs->resolved_type);
							load->address = slice;
							load->index = index_value;
							load->access = MIR_ACCESS_SLICE_FIELD;
							load->element_size = typeByteSize(*member.expression->resolved_type);
							load->field_offset = field_offset;
							load->extent = typeByteSize(*assignment.lhs->resolved_type);
							old_value = load->result;
						}
						MirValueId value = mirBuildExpression(builder, assignment.rhs);
						if (assignment.op != Token::EQUAL) {
							Token::Type op = assignment.op == Token::PLUS_EQUAL	   ? Token::PLUS
											 : assignment.op == Token::MINUS_EQUAL ? Token::MINUS
											 : assignment.op == Token::STAR_EQUAL  ? Token::STAR
																				   : Token::SLASH;
							auto* operation = mirAppend<MirBinaryInstruction>(builder, assignment.lhs->resolved_type, mirBinaryOpcode(op));
							operation->lhs = old_value;
							operation->rhs = value;
							value = operation->result;
						}
						auto* store = mirAppend<MirStoreInstruction>(builder, assignment.lhs->resolved_type);
						store->address = slice;
						store->index = index_value;
						store->value = value;
						store->access = MIR_ACCESS_SLICE_FIELD;
						store->element_size = typeByteSize(*member.expression->resolved_type);
						store->field_offset = field_offset;
						store->extent = typeByteSize(*assignment.lhs->resolved_type);
						break;
					}
				}
				MirValueId base = mirBuildAddress(builder, member.expression);
				if (assignment.lhs->resolved_type && assignment.lhs->resolved_type->kind == ResolvedType::NULLABLE) {
					MirValueId value = mirBuildExpressionAsType(builder, assignment.rhs, static_cast<NullableResolvedType*>(assignment.lhs->resolved_type)->inner);
					if (base != MIR_INVALID_ID && mirFindFieldOffset(builder, member, field_offset)) {
						auto* tag = mirAppend<MirConstInstruction>(builder, mirPrimitiveType(builder.arena, ResolvedType::I32)); tag->integer = 1;
						auto* tag_index = mirAppendI32Zero(builder, tag->type);
						auto* tag_store = mirAppend<MirStoreInstruction>(builder, tag->type); tag_store->address = base; tag_store->index = tag_index->result; tag_store->value = tag->result; tag_store->access = MIR_ACCESS_NULLABLE_TAG; tag_store->field_offset = field_offset; tag_store->element_size = 1; tag_store->extent = 1;
						auto* payload = mirAppend<MirStoreInstruction>(builder, assignment.lhs->resolved_type); payload->address = base; payload->index = tag_index->result; payload->value = value; payload->access = MIR_ACCESS_INDEXED; payload->field_offset = field_offset + 1; payload->element_size = 1; payload->extent = 1;
						break;
					}
				}
				if (member.expression && member.expression->kind == Expression::DEREFERENCE) {
					base = mirBuildExpression(builder, static_cast<DereferenceExpression*>(member.expression)->subject);
					if (base != MIR_INVALID_ID && mirFindFieldOffset(builder, member, field_offset)) {
						MirValueId value = mirBuildExpressionAsType(builder, assignment.rhs, assignment.lhs->resolved_type);
						MirValueId index = mirAppendZero(builder, static_cast<DereferenceExpression*>(member.expression)->subject->resolved_type);
						auto* store = mirAppend<MirStoreInstruction>(builder, assignment.lhs->resolved_type);
						store->address = base; store->index = index; store->value = value; store->access = MIR_ACCESS_POINTER; store->element_size = typeByteSize(*member.expression->resolved_type); store->field_offset = field_offset; store->extent = 1;
						break;
					}
				}
				if (member.expression && member.expression->resolved_type && member.expression->resolved_type->kind == ResolvedType::POINTER) {
					base = mirBuildExpression(builder, member.expression);
					if (base != MIR_INVALID_ID && mirFindFieldOffset(builder, member, field_offset)) {
						MirValueId index = mirAppendZero(builder, member.expression->resolved_type);
						MirValueId value = mirBuildExpressionAsType(builder, assignment.rhs, assignment.lhs->resolved_type);
						if (assignment.op != Token::EQUAL) {
							auto* old = mirAppend<MirLoadInstruction>(builder, assignment.lhs->resolved_type);
							old->address = base; old->index = index; old->access = MIR_ACCESS_POINTER; old->element_size = typeByteSize(*static_cast<PointerResolvedType*>(member.expression->resolved_type)->inner); old->field_offset = field_offset; old->extent = 1;
							auto* op = mirAppend<MirBinaryInstruction>(builder, assignment.lhs->resolved_type, mirBinaryOpcode(assignment.op == Token::PLUS_EQUAL ? Token::PLUS : assignment.op == Token::MINUS_EQUAL ? Token::MINUS : assignment.op == Token::STAR_EQUAL ? Token::STAR : Token::SLASH));
							op->operand_type = assignment.lhs->resolved_type; op->lhs = old->result; op->rhs = value; value = op->result;
						}
						auto* store = mirAppend<MirStoreInstruction>(builder, assignment.lhs->resolved_type);
						store->address = base; store->index = index; store->value = value; store->access = MIR_ACCESS_POINTER; store->element_size = typeByteSize(*static_cast<PointerResolvedType*>(member.expression->resolved_type)->inner); store->field_offset = field_offset; store->extent = 1;
						break;
					}
				}
				if (member.expression && member.expression->resolved_type && member.expression->resolved_type->kind == ResolvedType::POINTER) {
					base = mirBuildExpression(builder, member.expression);
					if (base != MIR_INVALID_ID && mirFindFieldOffset(builder, member, field_offset)) {
						MirValueId value = mirBuildExpressionAsType(builder, assignment.rhs, assignment.lhs->resolved_type);
						MirValueId index = mirAppendZero(builder, member.expression->resolved_type);
						auto* store = mirAppend<MirStoreInstruction>(builder, assignment.lhs->resolved_type);
						store->address = base; store->index = index; store->value = value; store->access = MIR_ACCESS_POINTER; store->element_size = typeByteSize(*static_cast<PointerResolvedType*>(member.expression->resolved_type)->inner); store->field_offset = field_offset; store->extent = 1;
						break;
					}
				}
				if (member.expression && member.expression->resolved_type && member.expression->resolved_type->kind == ResolvedType::POINTER) {
					base = mirBuildExpression(builder, member.expression);
					if (base != MIR_INVALID_ID && mirFindFieldOffset(builder, member, field_offset)) {
						MirValueId value = mirBuildExpressionAsType(builder, assignment.rhs, assignment.lhs->resolved_type);
						MirValueId index = mirAppendZero(builder, member.expression->resolved_type);
						auto* store = mirAppend<MirStoreInstruction>(builder, assignment.lhs->resolved_type);
						store->address = base; store->index = index; store->value = value; store->access = MIR_ACCESS_POINTER;
						store->element_size = typeByteSize(*static_cast<PointerResolvedType*>(member.expression->resolved_type)->inner); store->field_offset = field_offset; store->extent = 1;
						break;
					}
				}
				if (base == MIR_INVALID_ID || !mirFindFieldOffset(builder, member, field_offset)) break;
				MirInstruction* index = mirAppendI32Zero(builder, mirIndexType(assignment.lhs->resolved_type));
				MirValueId old_value = MIR_INVALID_ID;
				if (assignment.op != Token::EQUAL) {
					auto* load = mirAppend<MirLoadInstruction>(builder, assignment.lhs->resolved_type);
					load->address = base;
					load->index = index->result;
					load->access = MIR_ACCESS_INDEXED;
					load->element_size = typeByteSize(*assignment.lhs->resolved_type);
					load->field_offset = field_offset;
					load->extent = 1;
					old_value = load->result;
				}
				MirValueId value = mirBuildExpressionAsType(builder, assignment.rhs, assignment.lhs->resolved_type);
				if (assignment.op != Token::EQUAL) {
					Token::Type op = assignment.op == Token::PLUS_EQUAL	   ? Token::PLUS
									 : assignment.op == Token::MINUS_EQUAL ? Token::MINUS
									 : assignment.op == Token::STAR_EQUAL  ? Token::STAR
																		   : Token::SLASH;
					auto* operation = mirAppend<MirBinaryInstruction>(builder, assignment.lhs->resolved_type, mirBinaryOpcode(op));
					operation->lhs = old_value;
					operation->rhs = value;
					value = operation->result;
				}
				auto* store = mirAppend<MirStoreInstruction>(builder, assignment.lhs->resolved_type);
				store->address = base;
				store->index = index->result;
				store->value = value;
				store->access = MIR_ACCESS_INDEXED;
				store->element_size = typeByteSize(*assignment.lhs->resolved_type);
				store->field_offset = field_offset;
				store->extent = 1;
				break;
			}
			if (assignment.lhs && assignment.lhs->kind == Expression::BRACKET) {
				BracketExpression& bracket = *static_cast<BracketExpression*>(assignment.lhs);
				if (bracket.struct_field_name.begin && bracket.base && bracket.base->resolved_type && bracket.base->resolved_type->kind == ResolvedType::STRUCT) {
					StructResolvedType* structure = static_cast<StructResolvedType*>(bracket.base->resolved_type);
					u32 field_offset = 0;
					bool found = false;
					if (structure->decl) {
						for (u32 i = 0; i < (u32)structure->decl->fields.size(); ++i) {
							if (equalStrings(structure->decl->fields[i].name, bracket.struct_field_name)) { found = true; break; }
							ResolvedType* field_type = mirStructFieldType(structure, i);
							if (!field_type) break;
							field_offset += typeByteSize(*field_type);
						}
					}
					if (found) {
						MirValueId base = mirBuildAddress(builder, bracket.base);
						if (base != MIR_INVALID_ID) {
							MirInstruction* index_inst = mirAppendI32Zero(builder, assignment.lhs->resolved_type);
							MirValueId value = mirBuildExpressionAsType(builder, assignment.rhs, assignment.lhs->resolved_type);
							auto* store = mirAppend<MirStoreInstruction>(builder, assignment.lhs->resolved_type);
							store->address = base;
							store->index = index_inst->result;
							store->value = value;
							store->access = MIR_ACCESS_INDEXED;
							store->element_size = typeByteSize(*bracket.base->resolved_type);
							store->field_offset = field_offset;
							store->extent = 1;
							break;
						}
					}
				}
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
						auto* load = mirAppend<MirLoadInstruction>(builder, assignment.lhs->resolved_type);
						load->address = base;
						load->index = index;
						load->access = MIR_ACCESS_SLICE_ELEMENT;
						load->element_size = slice->element_type ? typeByteSize(*slice->element_type) : 0;
						auto* operation = mirAppend<MirBinaryInstruction>(builder,
							assignment.lhs->resolved_type,
							mirBinaryOpcode(assignment.op == Token::PLUS_EQUAL	  ? Token::PLUS
											: assignment.op == Token::MINUS_EQUAL ? Token::MINUS
											: assignment.op == Token::STAR_EQUAL  ? Token::STAR
																				  : Token::SLASH)
							);
						operation->lhs = load->result;
						operation->rhs = value;
						value = operation->result;
					}
					auto* store = mirAppend<MirStoreInstruction>(builder, assignment.lhs->resolved_type);
					store->address = base;
					store->index = index;
					store->value = value;
					store->access = MIR_ACCESS_SLICE_ELEMENT;
					store->element_size = slice->element_type ? typeByteSize(*slice->element_type) : 0;
					break;
				}
				if (assignment.op == Token::EQUAL && element_type && element_type->kind == ResolvedType::NULLABLE) {
					NullableResolvedType* nullable = static_cast<NullableResolvedType*>(element_type);
					MirInstruction* tag_index = mirAppendI32Zero(builder, nullable->inner);
					auto* tag = mirAppend<MirConstInstruction>(builder, nullable->inner);
					tag->integer = assignment.rhs && assignment.rhs->kind == Expression::NULL_LITERAL ? 0 : 1;
					auto* tag_store = mirAppend<MirStoreInstruction>(builder, nullable->inner);
					tag_store->address = base;
					tag_store->index = tag_index->result;
					tag_store->value = tag->result;
					tag_store->access = MIR_ACCESS_NULLABLE_TAG;
					tag_store->element_size = 1;
					tag_store->field_offset = 0;
					tag_store->extent = 1;
					if (assignment.rhs && assignment.rhs->kind != Expression::NULL_LITERAL) {
						MirValueId payload_value = mirBuildExpressionAsType(builder, assignment.rhs, nullable->inner);
						auto* payload = mirAppend<MirStoreInstruction>(builder, nullable->inner);
						payload->address = base;
						payload->index = tag_index->result;
						payload->value = payload_value;
						payload->access = MIR_ACCESS_INDEXED;
						payload->element_size = 1;
						payload->field_offset = 1;
						payload->extent = 1;
					}
					break;
				}
				MirValueId value = mirBuildExpression(builder, assignment.rhs);
				if (assignment.op != Token::EQUAL) {
					auto* load = mirAppend<MirLoadInstruction>(builder, assignment.lhs->resolved_type);
					load->address = base;
					load->index = index;
					load->access = MIR_ACCESS_INDEXED;
					load->element_size = element_type ? typeByteSize(*element_type) : 0;
					load->extent = extent;
					auto* operation = mirAppend<MirBinaryInstruction>(builder,
						assignment.lhs->resolved_type,
						mirBinaryOpcode(assignment.op == Token::PLUS_EQUAL	  ? Token::PLUS
										: assignment.op == Token::MINUS_EQUAL ? Token::MINUS
										: assignment.op == Token::STAR_EQUAL  ? Token::STAR
																			  : Token::SLASH)
						);
					operation->lhs = load->result;
					operation->rhs = value;
					value = operation->result;
				}
				auto* store = mirAppend<MirStoreInstruction>(builder, assignment.lhs->resolved_type);
				store->address = base;
				store->index = index;
				store->value = value;
				store->access = MIR_ACCESS_INDEXED;
				store->element_size = element_type ? typeByteSize(*element_type) : 0;
				store->extent = extent;
				break;
			}
			MirValueId address = mirBuildAddress(builder, assignment.lhs);
			if (address == MIR_INVALID_ID) break;
			MirValueId old_value = MIR_INVALID_ID;
			if (assignment.op != Token::EQUAL) {
				auto* load = mirAppend<MirLoadInstruction>(builder, assignment.lhs ? assignment.lhs->resolved_type : nullptr);
				load->address = address;
				old_value = load->result;
			}
			if (assignment.op == Token::EQUAL && mirBuildStoreAsUnion(builder, address, assignment.lhs ? assignment.lhs->resolved_type : nullptr, assignment.rhs))
				break;
			MirValueId value = mirBuildExpressionAsType(builder, assignment.rhs, assignment.lhs ? assignment.lhs->resolved_type : nullptr);
			if (assignment.op != Token::EQUAL) {
				auto* operation = mirAppend<MirBinaryInstruction>(builder,
					assignment.lhs->resolved_type,
					mirBinaryOpcode(assignment.op == Token::PLUS_EQUAL	  ? Token::PLUS
									: assignment.op == Token::MINUS_EQUAL ? Token::MINUS
									: assignment.op == Token::STAR_EQUAL  ? Token::STAR
																		  : Token::SLASH)
					);
				operation->lhs = old_value;
				operation->rhs = value;
				value = operation->result;
			}
			auto* store = mirAppend<MirStoreInstruction>(builder, assignment.lhs ? assignment.lhs->resolved_type : nullptr);
			store->address = address;
			store->value = value;
			break;
		}
		case Statement::IF: {
			IfStatement& conditional = *static_cast<IfStatement*>(statement);
			if (conditional.comptime_known) {
				Statement* selected = conditional.comptime_value ? static_cast<Statement*>(conditional.body) : conditional.else_branch;
				if (selected) mirBuildStatement(builder, selected);
				break;
			}
			MirValueId condition = mirBuildExpression(builder, conditional.condition);
			if (condition == MIR_INVALID_ID) {
				builder.block->terminator.kind = MIR_TERM_RETURN;
				builder.block->has_terminator = true;
				break;
			}
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
		case Statement::MATCH: {
			MatchStatement& ms = *static_cast<MatchStatement*>(statement);
			if (ms.comptime_known && ms.comptime_arm >= 0 && ms.comptime_arm < (i32)ms.arms.size()) {
				mirBuildStatement(builder, ms.arms[(u32)ms.comptime_arm].body);
				break;
			}
			ResolvedType* subject_type = ms.subject ? ms.subject->resolved_type : nullptr;
			if (!subject_type) break;
			MirValueId subject_value = mirBuildExpression(builder, ms.subject);
			if (subject_value == MIR_INVALID_ID) break;
			MirBlock* entry_block = builder.block;
			const bool is_union = subject_type->kind == ResolvedType::UNION;
			UnionResolvedType* un = is_union ? static_cast<UnionResolvedType*>(subject_type) : nullptr;
			ResolvedType* i32_type = mirPrimitiveType(builder.arena, ResolvedType::I32);
			MirValueId tag_value = MIR_INVALID_ID;
			if (is_union) {
				MirLocalId subject_local = mirFunctionAddLocal(builder.function, subject_type, {}, false, true);
				auto* subject_address = mirAppend<MirAddressInstruction>(builder, subject_type, MIR_OP_LOCAL_ADDRESS);
				subject_address->local = subject_local;
				auto* subject_store = mirAppend<MirStoreInstruction>(builder, subject_type);
				subject_store->address = subject_address->result;
				subject_store->value = subject_value;
				auto* tag_load = mirAppend<MirLoadInstruction>(builder, i32_type);
				tag_load->address = subject_address->result;
				tag_load->index = mirAppendI32Zero(builder, i32_type)->result;
				tag_load->access = MIR_ACCESS_INDEXED;
				tag_load->element_size = 4;
				tag_load->field_offset = 0;
				tag_load->extent = 1;
				tag_value = tag_load->result;
			}
			MirBlock* merge = mirFunctionCreateBlock(builder.function);
			MirBlock* fallback = nullptr;
			Statement* fallback_body = nullptr;
			ExpArray<u32> arm_index(builder.arena);
			ExpArray<MirBlock*> bodies(builder.arena);
			ExpArray<u32> arm_start(builder.arena);
			ExpArray<u32> arm_count(builder.arena);
			ExpArray<u32> pattern_check(builder.arena);
			ExpArray<MirBlock*> checks(builder.arena);
			for (i32 ai = 0; ai < (i32)ms.arms.size(); ++ai) {
				MatchArm& arm = ms.arms[ai];
				if (arm.is_fallback) {
					fallback = mirFunctionCreateBlock(builder.function);
					fallback_body = arm.body;
					continue;
				}
				arm_index.push((u32)ai);
				bodies.push(mirFunctionCreateBlock(builder.function));
				arm_start.push((u32)checks.size());
				arm_count.push((u32)arm.patterns.size());
				for (MatchPattern& pattern : arm.patterns) {
					pattern_check.push((u32)checks.size());
					checks.push(mirFunctionCreateBlock(builder.function));
					if (pattern.end) checks.push(mirFunctionCreateBlock(builder.function));
				}
			}
			const u32 body_count = (u32)bodies.size();
			for (u32 a = 0; a < body_count; ++a) {
				const u32 arm = arm_index[a];
				const u32 start = arm_start[a];
				const u32 count = arm_count[a];
				const MirBlockId body_id = bodies[a]->id;
				MirBlockId fall_through = a + 1 < body_count ? (arm_count[a + 1] > 0 ? checks[arm_start[a + 1]]->id : bodies[a + 1]->id) : (fallback ? fallback->id : merge->id);
				for (u32 p = 0; p < count; ++p) {
					MatchPattern& pattern = ms.arms[arm].patterns[(i32)p];
					MirBlock* check = checks[pattern_check[start + p]];
					builder.block = check;
					MirValueId cmp_operand = MIR_INVALID_ID;
					if (is_union) {
						ResolvedType* member = pattern.begin && pattern.begin->resolved_type && pattern.begin->resolved_type->kind == ResolvedType::META
							? static_cast<MetaType*>(pattern.begin->resolved_type)->inner
							: nullptr;
						i32 member_index = -1;
						for (i32 i = 0; i < un->members.size(); ++i) {
							if (mirSameType(un->members[i], member)) { member_index = i; break; }
						}
						if (member_index < 0) break;
						auto* tag = mirAppend<MirConstInstruction>(builder, i32_type);
						tag->integer = member_index;
						cmp_operand = tag->result;
					} else {
						cmp_operand = mirBuildExpressionAsType(builder, pattern.begin, subject_type);
					}
					if (cmp_operand == MIR_INVALID_ID) break;
					const MirValueId compare_lhs = is_union ? tag_value : subject_value;
					if (pattern.end) {
						auto* ge = mirAppend<MirBinaryInstruction>(builder, subject_type, MIR_OP_GE);
						ge->lhs = compare_lhs;
						ge->rhs = cmp_operand;
						ge->operand_type = subject_type;
						MirBlock* le_check = checks[pattern_check[start + p] + 1];
						builder.block->terminator.kind = MIR_TERM_BRANCH;
						builder.block->terminator.value = ge->result;
						builder.block->terminator.targets[0] = le_check->id;
						builder.block->terminator.targets[1] = fall_through;
						builder.block->has_terminator = true;
						MirValueId end_value = mirBuildExpressionAsType(builder, pattern.end, subject_type);
						auto* le = mirAppend<MirBinaryInstruction>(builder, subject_type, MIR_OP_LE);
						le->lhs = compare_lhs;
						le->rhs = end_value;
						le->operand_type = subject_type;
						builder.block = le_check;
						builder.block->terminator.kind = MIR_TERM_BRANCH;
						builder.block->terminator.value = le->result;
						builder.block->terminator.targets[0] = body_id;
						builder.block->terminator.targets[1] = fall_through;
						builder.block->has_terminator = true;
					} else {
						auto* eq = mirAppend<MirBinaryInstruction>(builder, subject_type, MIR_OP_EQ);
						eq->lhs = compare_lhs;
						eq->rhs = cmp_operand;
						eq->operand_type = is_union ? i32_type : subject_type;
						builder.block->terminator.kind = MIR_TERM_BRANCH;
						builder.block->terminator.value = eq->result;
						builder.block->terminator.targets[0] = body_id;
						builder.block->terminator.targets[1] = p + 1 < count ? checks[pattern_check[start + p + 1]]->id : fall_through;
						builder.block->has_terminator = true;
					}
				}
				builder.block = bodies[a];
				mirBuildStatement(builder, ms.arms[arm].body);
				if (!builder.block->has_terminator) {
					builder.block->terminator.kind = MIR_TERM_JUMP;
					builder.block->terminator.targets[0] = merge->id;
					builder.block->has_terminator = true;
				}
			}
			if (fallback) {
				builder.block = fallback;
				mirBuildStatement(builder, fallback_body);
				if (!builder.block->has_terminator) {
					builder.block->terminator.kind = MIR_TERM_JUMP;
					builder.block->terminator.targets[0] = merge->id;
					builder.block->has_terminator = true;
				}
			}
			const MirBlockId entry_target = body_count > 0 ? (arm_count[0] > 0 ? checks[arm_start[0]]->id : bodies[0]->id) : (fallback ? fallback->id : merge->id);
			entry_block->terminator.kind = MIR_TERM_JUMP;
			entry_block->terminator.targets[0] = entry_target;
			entry_block->has_terminator = true;
			builder.block = merge;
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
			const ls_string_view loop_label = builder.loop_label;
			builder.loop_label = {};
			mirBuildStatement(builder, loop.body);
			builder.loop_label = loop_label;
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
					if (break_statement.label.begin == break_statement.label.end || equalStrings(builder.loops[(u32)i].label, break_statement.label)) {
						loop = &builder.loops[(u32)i];
						break;
					}
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
					if (continue_statement.label.begin == continue_statement.label.end || equalStrings(builder.loops[(u32)i].label, continue_statement.label)) {
						loop = &builder.loops[(u32)i];
						break;
					}
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
			if (loop.is_unroll) {
				if (loop.is_expanded) {
					mirBuildStatement(builder, loop.body);
					break;
				}
				if (!loop.end) {
					if (!loop.unroll_elements) break;
					ResolvedType* value_type = loop.slot.type;
					if (!value_type && loop.unroll_elements->values.size()) value_type = loop.unroll_elements->values[0]->resolved_type;
					if (!value_type) break;
					MirLocalId index_local = MIR_INVALID_ID;
					if (loop.is_key_value) { ResolvedType* index_type = loop.index_slot.type ? loop.index_slot.type : mirPrimitiveType(builder.arena, ResolvedType::I64); index_local = mirFunctionAddLocal(builder.function, index_type, loop.key_var, true, false); MirSlotBinding& index_binding = builder.slots.emplace_back(); index_binding.slot = &loop.index_slot; index_binding.local = index_local; }
					MirLocalId value_local = mirFunctionAddLocal(builder.function, value_type, loop.value_var, true, false);
					MirSlotBinding& binding = builder.slots.emplace_back(); binding.slot = &loop.slot; binding.local = value_local;
					MirLoopBinding& unroll_loop = builder.loops.emplace_back(); unroll_loop.label = builder.loop_label; unroll_loop.exit = mirFunctionCreateBlock(builder.function)->id; unroll_loop.continue_target = unroll_loop.exit;
					u32 element_index = 0;
					for (Expression* element : loop.unroll_elements->values) {
						MirBlock* next_iteration = mirFunctionCreateBlock(builder.function);
						builder.loops[builder.loops.size() - 1].continue_target = next_iteration->id;
						if (index_local != MIR_INVALID_ID) { ResolvedType* index_type = builder.function.locals[index_local].type; auto* index_value = mirAppend<MirConstInstruction>(builder, index_type); index_value->integer = element_index; auto* index_address = mirAppend<MirAddressInstruction>(builder, index_type, MIR_OP_LOCAL_ADDRESS); index_address->local = index_local; auto* index_store = mirAppend<MirStoreInstruction>(builder, index_type); index_store->address = index_address->result; index_store->value = index_value->result; }
						MirValueId value = mirBuildExpressionAsType(builder, element, value_type);
						auto* address = mirAppend<MirAddressInstruction>(builder, value_type, MIR_OP_LOCAL_ADDRESS); address->local = value_local;
						auto* store = mirAppend<MirStoreInstruction>(builder, value_type); store->address = address->result; store->value = value;
						mirBuildStatement(builder, loop.body);
						if (!builder.block->has_terminator) { builder.block->terminator.kind = MIR_TERM_JUMP; builder.block->terminator.targets[0] = next_iteration->id; builder.block->has_terminator = true; }
						builder.block = next_iteration;
						++element_index;
					}
					builder.block = &builder.function.blocks[unroll_loop.exit]; builder.loops.pop_back(); break;
				}
				ResolvedType* value_type = loop.slot.type ? loop.slot.type : loop.begin->resolved_type;
				if (!value_type) break;
				MirLocalId value_local = mirFunctionAddLocal(builder.function, value_type, loop.value_var, true, false);
				MirSlotBinding& binding = builder.slots.emplace_back();
				binding.slot = &loop.slot;
				binding.local = value_local;
				MirLoopBinding& unroll_loop = builder.loops.emplace_back();
				unroll_loop.label = builder.loop_label;
				unroll_loop.exit = mirFunctionCreateBlock(builder.function)->id;
				unroll_loop.continue_target = unroll_loop.exit;
				for (i64 value = loop.unroll_begin; value < loop.unroll_end; ++value) {
					MirBlock* next_iteration = mirFunctionCreateBlock(builder.function);
					builder.loops[builder.loops.size() - 1].continue_target = next_iteration->id;
					auto* constant = mirAppend<MirConstInstruction>(builder, value_type);
					constant->integer = (u64)value;
					auto* address = mirAppend<MirAddressInstruction>(builder, value_type, MIR_OP_LOCAL_ADDRESS);
					address->local = value_local;
					auto* store = mirAppend<MirStoreInstruction>(builder, value_type);
					store->address = address->result;
					store->value = constant->result;
					mirBuildStatement(builder, loop.body);
					if (!builder.block->has_terminator) { builder.block->terminator.kind = MIR_TERM_JUMP; builder.block->terminator.targets[0] = next_iteration->id; builder.block->has_terminator = true; }
					builder.block = next_iteration;
				}
				builder.block = &builder.function.blocks[unroll_loop.exit];
				builder.loops.pop_back();
				break;
			}
			if (!loop.begin) break;
			if (!loop.end) {
				ResolvedType* container_type = loop.begin->resolved_type;
				if (!container_type || (container_type->kind != ResolvedType::ARRAY && container_type->kind != ResolvedType::SLICE)) break;
				if (loop.begin->kind == Expression::ARRAY_LITERAL) {
					// Array literals used only as compile-time loop sources are already
					// validated by the front end; no runtime storage is needed here.
					break;
				}
				ResolvedType* element_type = container_type->kind == ResolvedType::ARRAY
					? static_cast<ArrayResolvedType*>(container_type)->element_type
					: static_cast<SliceResolvedType*>(container_type)->element_type;
				if (!element_type) break;
				ResolvedType* index_type = loop.index_slot.type ? loop.index_slot.type : mirPrimitiveType(builder.arena, ResolvedType::I64);
				MirLocalId index_local = mirFunctionAddLocal(builder.function, index_type, loop.key_var, true, false);
				MirSlotBinding& index_binding = builder.slots.emplace_back();
				index_binding.slot = &loop.index_slot;
				index_binding.local = index_local;
				MirLocalId value_local = mirFunctionAddLocal(builder.function, element_type, loop.value_var, true, false);
				MirSlotBinding& value_binding = builder.slots.emplace_back();
				value_binding.slot = &loop.slot;
				value_binding.local = value_local;
				MirValueId zero = mirAppendZero(builder, index_type);
				auto* index_address = mirAppend<MirAddressInstruction>(builder, index_type, MIR_OP_LOCAL_ADDRESS);
				index_address->local = index_local;
				auto* index_store = mirAppend<MirStoreInstruction>(builder, index_type);
				index_store->address = index_address->result;
				index_store->value = zero;
				MirValueId collection = mirBuildExpression(builder, loop.begin);
				MirValueId length = MIR_INVALID_ID;
				if (container_type->kind == ResolvedType::ARRAY) {
					auto* length_constant = mirAppend<MirConstInstruction>(builder, index_type);
					length_constant->integer = static_cast<ArrayResolvedType*>(container_type)->size;
					length = length_constant->result;
				} else {
					auto* length_instruction = mirAppend<MirUnaryInstruction>(builder, index_type, MIR_OP_SLICE_LENGTH);
					length_instruction->operand = collection;
					length = length_instruction->result;
				}
				MirBlock* header = mirFunctionCreateBlock(builder.function);
				MirBlock* body = mirFunctionCreateBlock(builder.function);
				MirBlock* increment = mirFunctionCreateBlock(builder.function);
				MirBlock* exit = mirFunctionCreateBlock(builder.function);
				builder.block->terminator.kind = MIR_TERM_JUMP;
				builder.block->terminator.targets[0] = header->id;
				builder.block->has_terminator = true;
				builder.block = header;
				auto* current_index_address = mirAppend<MirAddressInstruction>(builder, index_type, MIR_OP_LOCAL_ADDRESS);
				current_index_address->local = index_local;
				auto* current_index = mirAppend<MirLoadInstruction>(builder, index_type);
				current_index->address = current_index_address->result;
				auto* condition = mirAppend<MirBinaryInstruction>(builder, builder.function.return_type, MIR_OP_LT);
				condition->operand_type = index_type;
				condition->lhs = current_index->result;
				condition->rhs = length;
				builder.block->terminator.kind = MIR_TERM_BRANCH;
				builder.block->terminator.value = condition->result;
				builder.block->terminator.targets[0] = body->id;
				builder.block->terminator.targets[1] = exit->id;
				builder.block->has_terminator = true;
				builder.block = body;
				MirValueId element = MIR_INVALID_ID;
				if (container_type->kind == ResolvedType::ARRAY) {
					auto* load = mirAppend<MirLoadInstruction>(builder, element_type);
					load->address = mirBuildAddress(builder, loop.begin);
					load->index = current_index->result;
					load->access = MIR_ACCESS_INDEXED;
					load->element_size = typeByteSize(*element_type);
					load->extent = static_cast<u32>(static_cast<ArrayResolvedType*>(container_type)->size);
					element = load->result;
				} else {
					auto* load = mirAppend<MirLoadInstruction>(builder, element_type);
					load->address = collection;
					load->index = current_index->result;
					load->access = MIR_ACCESS_SLICE_ELEMENT;
					load->element_size = typeByteSize(*element_type);
					element = load->result;
				}
				auto* value_address = mirAppend<MirAddressInstruction>(builder, element_type, MIR_OP_LOCAL_ADDRESS);
				value_address->local = value_local;
				auto* value_store = mirAppend<MirStoreInstruction>(builder, element_type);
				value_store->address = value_address->result;
				value_store->value = element;
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
				auto* one = mirAppend<MirConstInstruction>(builder, index_type);
				one->integer = 1;
				auto* next = mirAppend<MirBinaryInstruction>(builder, index_type, MIR_OP_ADD);
				next->operand_type = index_type;
				next->lhs = current_index->result;
				next->rhs = one->result;
				auto* next_address = mirAppend<MirAddressInstruction>(builder, index_type, MIR_OP_LOCAL_ADDRESS);
				next_address->local = index_local;
				auto* next_store = mirAppend<MirStoreInstruction>(builder, index_type);
				next_store->address = next_address->result;
				next_store->value = next->result;
				builder.block->terminator.kind = MIR_TERM_JUMP;
				builder.block->terminator.targets[0] = header->id;
				builder.block->has_terminator = true;
				builder.block = exit;
				break;
			}
			ResolvedType* type = loop.slot.type ? loop.slot.type : loop.begin->resolved_type;
			MirLocalId index_local = mirFunctionAddLocal(builder.function, type, loop.value_var, true, false);
			MirSlotBinding& binding = builder.slots.emplace_back();
			binding.slot = &loop.slot;
			binding.local = index_local;
			MirLocalId end_local = mirFunctionAddLocal(builder.function, type, {}, false, true);
			MirValueId begin = mirBuildExpressionAsType(builder, loop.begin, type);
			MirValueId end = mirBuildExpressionAsType(builder, loop.end, type);
			auto* index_address = mirAppend<MirAddressInstruction>(builder, type, MIR_OP_LOCAL_ADDRESS);
			index_address->local = index_local;
			auto* index_store = mirAppend<MirStoreInstruction>(builder, type);
			index_store->address = index_address->result;
			index_store->value = begin;
			auto* end_address = mirAppend<MirAddressInstruction>(builder, type, MIR_OP_LOCAL_ADDRESS);
			end_address->local = end_local;
			auto* end_store = mirAppend<MirStoreInstruction>(builder, type);
			end_store->address = end_address->result;
			end_store->value = end;
			MirBlock* header = mirFunctionCreateBlock(builder.function);
			MirBlock* body = mirFunctionCreateBlock(builder.function);
			MirBlock* increment = mirFunctionCreateBlock(builder.function);
			MirBlock* exit = mirFunctionCreateBlock(builder.function);
			builder.block->terminator.kind = MIR_TERM_JUMP;
			builder.block->terminator.targets[0] = header->id;
			builder.block->has_terminator = true;
			builder.block = header;
			auto* index_load_address = mirAppend<MirAddressInstruction>(builder, type, MIR_OP_LOCAL_ADDRESS);
			index_load_address->local = index_local;
			auto* index_load = mirAppend<MirLoadInstruction>(builder, type);
			index_load->address = index_load_address->result;
			auto* end_load_address = mirAppend<MirAddressInstruction>(builder, type, MIR_OP_LOCAL_ADDRESS);
			end_load_address->local = end_local;
			auto* end_load = mirAppend<MirLoadInstruction>(builder, type);
			end_load->address = end_load_address->result;
			auto* condition = mirAppend<MirBinaryInstruction>(builder, builder.function.return_type, MIR_OP_LT);
			condition->operand_type = type;
			condition->lhs = index_load->result;
			condition->rhs = end_load->result;
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
			auto* one = mirAppend<MirConstInstruction>(builder, type);
			one->integer = 1;
			auto* next = mirAppend<MirBinaryInstruction>(builder, type, MIR_OP_ADD);
			next->lhs = index_load->result;
			next->rhs = one->result;
			auto* next_address = mirAppend<MirAddressInstruction>(builder, type, MIR_OP_LOCAL_ADDRESS);
			next_address->local = index_local;
			auto* next_store = mirAppend<MirStoreInstruction>(builder, type);
			next_store->address = next_address->result;
			next_store->value = next->result;
			builder.block->terminator.kind = MIR_TERM_JUMP;
			builder.block->terminator.targets[0] = header->id;
			builder.block->has_terminator = true;
			builder.block = exit;
			break;
		}
		case Statement::DEFER: builder.deferreds.push(static_cast<DeferStatement*>(statement)->statement); break;
		case Statement::VAR_DECL: {
			VarDeclStatement& declaration = *static_cast<VarDeclStatement*>(statement);
			if (declaration.is_comptime) {
				if (declaration.expression && declaration.expression->eval_stage != Expression::RUNTIME && declaration.expression->resolved_type && (declaration.expression->resolved_type->kind == ResolvedType::F32 || declaration.expression->resolved_type->kind == ResolvedType::F64 || declaration.expression->resolved_type->kind == ResolvedType::UNTYPED_FLOAT)) {
					MirValueId value = mirBuildExpressionAsType(builder, declaration.expression, declaration.resolved_type);
					(void)value;
				}
				break;
			}
			ResolvedType* local_type = declaration.slot.type ? declaration.slot.type : declaration.resolved_type;
			MirLocalId local = mirFunctionAddLocal(builder.function, local_type, declaration.name, !declaration.is_immutable, false);
			if (builder.has_current_source) builder.function.locals[local].source_location = mirFunctionAddSourceLocation(builder.function, builder.current_source.source_name, (u32)builder.current_source.line, (u32)builder.current_source.column);
			MirSlotBinding& binding = builder.slots.emplace_back();
			binding.slot = &declaration.slot;
			binding.local = local;
			auto* address = mirAppend<MirAddressInstruction>(builder, local_type, MIR_OP_LOCAL_ADDRESS);
			address->local = local;
			if (declaration.else_return && declaration.expression && declaration.expression->resolved_type && declaration.expression->resolved_type->kind == ResolvedType::UNION) {
				UnionResolvedType* source_type = static_cast<UnionResolvedType*>(declaration.expression->resolved_type);
				MirLocalId source_local = mirFunctionAddLocal(builder.function, source_type, {}, false, true);
				auto* source_address = mirAppend<MirAddressInstruction>(builder, source_type, MIR_OP_LOCAL_ADDRESS);
				source_address->local = source_local;
				MirValueId source_value = mirBuildExpression(builder, declaration.expression);
				auto* source_store = mirAppend<MirStoreInstruction>(builder, source_type);
				source_store->address = source_address->result;
				source_store->value = source_value;
				MirValueId zero = mirAppendI32Zero(builder, mirPrimitiveType(builder.arena, ResolvedType::I32))->result;
				auto* tag_address = mirAppend<MirAddressInstruction>(builder, source_type, MIR_OP_LOCAL_ADDRESS);
				tag_address->local = source_local;
				auto* tag = mirAppend<MirLoadInstruction>(builder, mirPrimitiveType(builder.arena, ResolvedType::I32));
				tag->address = tag_address->result;
				tag->index = zero;
				tag->access = MIR_ACCESS_INDEXED;
				tag->element_size = 4;
				tag->field_offset = 0;
				tag->extent = 1;
				MirBlock* success = mirFunctionCreateBlock(builder.function);
				MirBlock* failure = mirFunctionCreateBlock(builder.function);
				MirBlock* current = builder.block;
				for (i32 i = 0; i < source_type->members.size(); ++i) {
					if ((declaration.else_return_target_mask & (1ull << (u32)i)) == 0) continue;
					MirBlock* next = mirFunctionCreateBlock(builder.function);
					MirValueId expected = mirAppendI32Zero(builder, tag->type)->result;
					static_cast<MirConstInstruction*>(builder.block->instructions.back())->integer = i;
					auto* compare = mirAppend<MirBinaryInstruction>(builder, tag->type, MIR_OP_EQ);
					compare->operand_type = tag->type;
					compare->lhs = tag->result;
					compare->rhs = expected;
					current->terminator.kind = MIR_TERM_BRANCH;
					current->terminator.value = compare->result;
					current->terminator.targets[0] = success->id;
					current->terminator.targets[1] = next->id;
					current->has_terminator = true;
					current = next;
					builder.block = current;
				}
				current->terminator.kind = MIR_TERM_JUMP;
				current->terminator.targets[0] = failure->id;
				current->has_terminator = true;
				builder.block = success;
				auto* payload = mirAppend<MirLoadInstruction>(builder, local_type);
				payload->address = source_address->result;
				payload->index = zero;
				payload->access = MIR_ACCESS_INDEXED;
				payload->element_size = typeByteSize(*local_type);
				payload->field_offset = 4;
				payload->extent = 1;
				if (local_type->kind == ResolvedType::UNION) {
					UnionResolvedType* target_type = static_cast<UnionResolvedType*>(local_type);
					i32 first_selected = -1;
					for (i32 i = 0; i < source_type->members.size(); ++i) {
						if (declaration.else_return_target_mask & (1ull << (u32)i)) { first_selected = i; break; }
					}
					(void)target_type;
					MirValueId target_tag = tag->result;
					if (first_selected > 0) {
						auto* base = mirAppend<MirConstInstruction>(builder, tag->type);
						base->integer = first_selected;
						auto* remap = mirAppend<MirBinaryInstruction>(builder, tag->type, MIR_OP_SUB);
						remap->operand_type = tag->type;
						remap->lhs = tag->result;
						remap->rhs = base->result;
						target_tag = remap->result;
					}
					auto* tag_store = mirAppend<MirStoreInstruction>(builder, tag->type);
					tag_store->address = address->result;
					tag_store->index = zero;
					tag_store->value = target_tag;
					tag_store->access = MIR_ACCESS_INDEXED;
					tag_store->element_size = 4;
					tag_store->field_offset = 0;
					tag_store->extent = 1;
					ResolvedType* payload_type = mirPrimitiveType(builder.arena, ResolvedType::I32);
					auto* payload_copy = mirAppend<MirLoadInstruction>(builder, payload_type);
					payload_copy->address = source_address->result;
					payload_copy->index = zero;
					payload_copy->access = MIR_ACCESS_INDEXED;
					payload_copy->element_size = 4;
					payload_copy->field_offset = 4;
					payload_copy->extent = 1;
					auto* target_store = mirAppend<MirStoreInstruction>(builder, payload_type);
					target_store->address = address->result;
					target_store->index = zero;
					target_store->value = payload_copy->result;
					target_store->access = MIR_ACCESS_INDEXED;
					target_store->element_size = 4;
					target_store->field_offset = 4;
					target_store->extent = 1;
				} else {
					auto* target_store = mirAppend<MirStoreInstruction>(builder, local_type);
					target_store->address = address->result;
					target_store->value = payload->result;
				}
				builder.block->terminator.kind = MIR_TERM_JUMP;
				MirBlock* merge = mirFunctionCreateBlock(builder.function);
				builder.block->terminator.targets[0] = merge->id;
				builder.block->has_terminator = true;
				builder.block = failure;
				mirEmitActiveDefers(builder);
				MirValueId failure_value = source_value;
				if (declaration.else_return_type && declaration.else_return_type->kind != ResolvedType::UNION) {
					auto* residual = mirAppend<MirLoadInstruction>(builder, declaration.else_return_type);
					residual->address = source_address->result;
					residual->index = zero;
					residual->access = MIR_ACCESS_INDEXED;
					residual->element_size = 1;
					residual->field_offset = 4;
					residual->extent = 1;
					failure_value = residual->result;
				} else if (declaration.else_return_type && declaration.else_return_type->kind == ResolvedType::UNION) {
					UnionResolvedType* target = static_cast<UnionResolvedType*>(declaration.else_return_type);
					MirLocalId target_local = mirFunctionAddLocal(builder.function, target, {}, false, true);
					auto* target_address = mirAppend<MirAddressInstruction>(builder, target, MIR_OP_LOCAL_ADDRESS); target_address->local = target_local;
					i32 selected_count = 0;
					for (i32 i = 0; i < source_type->members.size(); ++i) if (declaration.else_return_target_mask & (1ull << (u32)i)) ++selected_count;
					auto* selected_const = mirAppend<MirConstInstruction>(builder, tag->type); selected_const->integer = selected_count;
					auto* remap = mirAppend<MirBinaryInstruction>(builder, tag->type, MIR_OP_SUB); remap->operand_type = tag->type; remap->lhs = tag->result; remap->rhs = selected_const->result;
					auto* tag_store = mirAppend<MirStoreInstruction>(builder, tag->type); tag_store->address = target_address->result; tag_store->index = zero; tag_store->value = remap->result; tag_store->access = MIR_ACCESS_INDEXED; tag_store->element_size = 4; tag_store->field_offset = 0; tag_store->extent = 1;
					ResolvedType* payload_type = mirPrimitiveType(builder.arena, ResolvedType::I32);
					auto* payload = mirAppend<MirLoadInstruction>(builder, payload_type); payload->address = source_address->result; payload->index = zero; payload->access = MIR_ACCESS_INDEXED; payload->element_size = 4; payload->field_offset = 4; payload->extent = 1;
					auto* payload_store = mirAppend<MirStoreInstruction>(builder, payload_type); payload_store->address = target_address->result; payload_store->index = zero; payload_store->value = payload->result; payload_store->access = MIR_ACCESS_INDEXED; payload_store->element_size = 4; payload_store->field_offset = 4; payload_store->extent = 1;
					auto* result = mirAppend<MirLoadInstruction>(builder, target); result->address = target_address->result; failure_value = result->result;
				}
				builder.block->terminator.kind = MIR_TERM_RETURN_VALUE;
				builder.block->terminator.value = failure_value;
				builder.block->has_terminator = true;
				builder.block = merge;
				break;
			}
			if (declaration.expression && local_type && local_type->kind == ResolvedType::NULLABLE) {
				NullableResolvedType* nullable = static_cast<NullableResolvedType*>(local_type);
				const bool is_null = declaration.expression->kind == Expression::NULL_LITERAL || declaration.expression->kind == Expression::UNDEFINED;
				auto* tag = mirAppend<MirConstInstruction>(builder, nullable->inner);
				tag->integer = is_null ? 0 : 1;
				MirInstruction* tag_index = mirAppendI32Zero(builder, nullable->inner);
				auto* tag_store = mirAppend<MirStoreInstruction>(builder, nullable->inner);
				tag_store->address = address->result;
				tag_store->index = tag_index->result;
				tag_store->value = tag->result;
				tag_store->access = MIR_ACCESS_NULLABLE_TAG;
				tag_store->element_size = 1;
				tag_store->field_offset = 0;
				tag_store->extent = 1;
				if (!is_null) {
					MirValueId value = mirBuildExpressionAsType(builder, declaration.expression, nullable->inner);
					auto* payload = mirAppend<MirStoreInstruction>(builder, nullable->inner);
					payload->address = address->result;
					payload->index = tag_index->result;
					payload->value = value;
					payload->access = MIR_ACCESS_INDEXED;
					payload->element_size = 1;
					payload->field_offset = 1;
					payload->extent = 1;
				}
			} else if (declaration.expression && declaration.expression->kind == Expression::ARRAY_LITERAL && local_type && local_type->kind == ResolvedType::ARRAY) {
				ArrayResolvedType* array = static_cast<ArrayResolvedType*>(local_type);
				ArrayLiteralExpression& literal = *static_cast<ArrayLiteralExpression*>(declaration.expression);
				for (u32 i = 0; i < (u32)literal.values.size(); ++i) {
					auto* index = mirAppendI32Zero(builder, array->element_type);
					index->integer = i;
					MirValueId value = mirBuildExpressionAsType(builder, literal.values[(i32)i], array->element_type);
					auto* store = mirAppend<MirStoreInstruction>(builder, array->element_type);
					store->address = address->result;
					store->index = index->result;
					store->value = value;
					store->access = MIR_ACCESS_INDEXED;
					store->element_size = array->element_type ? typeByteSize(*array->element_type) : 0;
					store->extent = (u32)array->size;
				}
			} else if (declaration.expression && declaration.expression->kind != Expression::UNDEFINED) {
				if (mirBuildStoreAsUnion(builder, address->result, local_type, declaration.expression)) break;
				MirValueId value = mirBuildExpressionAsType(builder, declaration.expression, local_type);
				auto* store = mirAppend<MirStoreInstruction>(builder, local_type);
				store->address = address->result;
				store->value = value;
			}
			break;
		}
		default: break;
	}
}

MirFunction* mirBuildFunction(ls_arena& arena, FunctionExpression* source, ls_string_view name) {
	if (!source || !source->body || source->body->kind != Statement::BLOCK) {
		if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL build '%s': no body\n", name.begin ? name.begin : "?");
		return nullptr;
	}
	MirFunction* function = (MirFunction*)arena.allocate(arena.user_data, sizeof(MirFunction), alignof(MirFunction));
	if (!function) return nullptr;
	::new (NewPlaceholder{}, (void*)function) MirFunction(arena);
	function->name = name;
	FunctionResolvedType* function_type = source->resolved_type && source->resolved_type->kind == ResolvedType::FUNCTION ? static_cast<FunctionResolvedType*>(source->resolved_type) : nullptr;
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
			auto* address = mirAppend<MirAddressInstruction>(builder, symbol.resolved_type, MIR_OP_GLOBAL_ADDRESS);
			address->global_offset = symbol.slot.offset;
			if (symbol.resolved_type->kind == ResolvedType::NULLABLE) {
				NullableResolvedType* nullable = static_cast<NullableResolvedType*>(symbol.resolved_type);
				const bool is_null = symbol.expression->kind == Expression::NULL_LITERAL || symbol.expression->kind == Expression::UNDEFINED;
				auto* tag = mirAppend<MirConstInstruction>(builder, nullable->inner);
				tag->integer = is_null ? 0 : 1;
				MirInstruction* tag_index = mirAppendI32Zero(builder, nullable->inner);
				auto* tag_store = mirAppend<MirStoreInstruction>(builder, nullable->inner);
				tag_store->address = address->result;
				tag_store->index = tag_index->result;
				tag_store->value = tag->result;
				tag_store->access = MIR_ACCESS_NULLABLE_TAG;
				tag_store->element_size = 1;
				tag_store->field_offset = 0;
				tag_store->extent = 1;
				if (!is_null) {
					MirValueId payload = mirBuildExpressionAsType(builder, symbol.expression, nullable->inner);
					auto* payload_store = mirAppend<MirStoreInstruction>(builder, nullable->inner);
					payload_store->address = address->result;
					payload_store->index = tag_index->result;
					payload_store->value = payload;
					payload_store->access = MIR_ACCESS_INDEXED;
					payload_store->element_size = 1;
					payload_store->field_offset = 1;
					payload_store->extent = 1;
				}
				continue;
			}
			if (symbol.resolved_type->kind == ResolvedType::UNION && mirBuildStoreAsUnion(builder, address->result, symbol.resolved_type, symbol.expression, true)) continue;
			MirValueId value = mirBuildExpressionAsType(builder, symbol.expression, symbol.resolved_type);
			if (value == MIR_INVALID_ID) return nullptr;
			auto* store = mirAppend<MirStoreInstruction>(builder, symbol.resolved_type);
			store->address = address->result;
			store->value = value;
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
	auto mirIsTypeFactory = [](const FunctionExpression* function) {
		if (!function || !function->resolved_type || function->resolved_type->kind != ResolvedType::FUNCTION) return false;
		const FunctionResolvedType* type = static_cast<const FunctionResolvedType*>(function->resolved_type);
		return type->return_type && type->return_type->kind == ResolvedType::META;
	};
	for (Unit& unit : module->units) {
		for (Symbol& symbol : unit.symbols) {
			if (!symbol.expression || symbol.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* function = static_cast<FunctionExpression*>(symbol.expression);
			if (!function->is_template && !mirIsTypeFactory(function)) function->bytecode_index = function_index++;
		}
	}
	for (Unit& unit : module->units) {
		for (Symbol& symbol : unit.symbols) {
			if (!symbol.expression || symbol.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* function = static_cast<FunctionExpression*>(symbol.expression);
			if (function->is_template || mirIsTypeFactory(function)) continue;
			MirModuleFunction& entry = result->functions.emplace_back();
			if (function->is_extern) {
				if (!function->resolved_type || function->resolved_type->kind != ResolvedType::FUNCTION) return nullptr;
				entry.is_native = true;
				entry.function = nullptr;
				entry.native.name = symbol.name;
				entry.native.type = static_cast<FunctionResolvedType*>(function->resolved_type);
				entry.native.is_builtin = unit.path.begin && (equalStrings(unit.path, makeStringView("std:math")) || equalStrings(unit.path, makeStringView("std:mem")));
				continue;
			}
			MirFunction* mir = mirBuildFunction(arena, function, symbol.name);
			if (!mir) {
				if (getenv("MIR_TRACE")) fprintf(stderr, "[mir] FAIL build function '%s'\n", symbol.name.begin ? symbol.name.begin : "?");
				return nullptr;
			}
			entry.is_native = false;
			entry.function = mir;
		}
	}
	result->global_debug_count = 0;
	for (Unit& unit : module->units) for (Symbol& symbol : unit.symbols) {
		if (!symbolHasGlobalStorage(symbol)) continue;
		++result->global_debug_count;
	}
	if (result->global_debug_count) {
		result->global_debug = (ls_bytecode_global_debug_entry*)arena.allocate(arena.user_data,
			sizeof(ls_bytecode_global_debug_entry) * result->global_debug_count, alignof(ls_bytecode_global_debug_entry));
		result->global_debug_types = (ResolvedType**)arena.allocate(arena.user_data, sizeof(ResolvedType*) * result->global_debug_count, alignof(ResolvedType*));
		u32 index = 0;
		for (Unit& unit : module->units) for (Symbol& symbol : unit.symbols) {
			if (!symbolHasGlobalStorage(symbol)) continue;
			ls_bytecode_global_debug_entry& entry = result->global_debug[index++];
			entry.name = symbol.name;
			entry.offset = symbol.slot.offset;
			entry.byte_size = symbol.slot.byte_size;
			entry.kind = mirDebugKind(*symbol.resolved_type);
			entry.type_index = LS_TYPE_INDEX_NONE;
			result->global_debug_types[index - 1] = symbol.resolved_type;
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
