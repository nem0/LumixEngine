#include "ir.h"

#include "bytecode.h"
#include "compiler.h"
#include "utils.h"

#include <stdlib.h>

// TODO
/*
Slice iteration with for value in slice and for index, value in slice
Array key/value iteration
Multi-dimensional and nested aggregate access
Aggregate copies for arrays, structs, unions, and slices
Struct literals, fields, and member assignment
Enum and union lowering
Cast expressions
Pointer/address-of/dereference operations
Indirect calls and function values
Aggregate and reference call arguments
Logical and/or short-circuiting
Ternary expressions
Match statements
Labeled break/continue
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
		case ResolvedType::U64: return 7;
		case ResolvedType::F32: return 8;
		case ResolvedType::F64: return 9;
		default: return -1;
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
		LsOpJump* continue_jumps[16] = {};
		u32 continue_jump_count = 0;
	};

	ls_arena& arena;
	LsIrFunctionData& function;
	LsIrBlockData* block = nullptr;
	ExpArray<LoopTargets> loops;
	ExpArray<LocalBinding> locals;
	u32 next_local_offset = 0;

	IrBuilder(ls_arena& arena, LsIrFunctionData& function)
		: arena(arena), function(function), loops(arena), locals(arena) {
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
		next_local_offset += size;
		return binding.offset;
	}

	template <typename T>
	T* terminate() {
		void* memory = arena.allocate(arena.user_data, sizeof(T), alignof(T));
		if (!memory) return nullptr;
		T* op = ::new (NewPlaceholder{}, memory) T();
		block->terminator = op;
		return op;
	}
};

static LsIrValue buildExpression(IrBuilder& builder, Expression* expression);
static void buildStatement(IrBuilder& builder, Statement* statement);

static LsIrValue buildLiteral(IrBuilder& builder, Expression& expression) {
	LsOpLoadConst* op = builder.append<LsOpLoadConst>();
	if (!op) return LS_IR_INVALID_VALUE;
	op->type = expression.resolved_type;
	op->result = builder.newValue();
	switch (expression.kind) {
		case Expression::INT_LITERAL:
			op->value = static_cast<IntLiteralExpression&>(expression).value;
			break;
		case Expression::FLOAT_LITERAL: {
			double value = static_cast<FloatLiteralExpression&>(expression).value;
			copyMemory(&op->value, &value, sizeof(value));
			break;
		}
		case Expression::BOOL_LITERAL:
			op->value = static_cast<BoolLiteralExpression&>(expression).value ? 1u : 0u;
			break;
		case Expression::STRING_LITERAL:
			op->value = (u64)(uintptr)static_cast<StringLiteralExpression&>(expression).value.begin;
			break;
		case Expression::NULL_LITERAL:
		case Expression::UNDEFINED:
			op->value = 0;
			break;
		default:
			return LS_IR_INVALID_VALUE;
	}
	return op->result;
}

static LsIrValue buildIdentifier(IrBuilder& builder, IdentifierExpression& identifier) {
	if (!identifier.slot) return LS_IR_INVALID_VALUE;
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
	load->offset = builder.localOffset(identifier.slot, identifier.resolved_type);
	return load->result;
}

static void storeValue(IrBuilder& builder, Expression* lhs, LsIrValue value) {
	if (value == LS_IR_INVALID_VALUE) return;
	if (lhs && lhs->kind == Expression::BRACKET) {
		BracketExpression& bracket = *static_cast<BracketExpression*>(lhs);
		if (bracket.base && bracket.base->kind == Expression::IDENTIFIER && bracket.args.size() == 1) {
			IdentifierExpression& base = *static_cast<IdentifierExpression*>(bracket.base);
			if (base.slot && base.slot->storage != StorageSlot::GLOBAL && base.resolved_type && base.resolved_type->kind == ResolvedType::ARRAY) {
				ArrayResolvedType* array = static_cast<ArrayResolvedType*>(base.resolved_type);
				LsIrValue index = buildExpression(builder, bracket.args[0]);
				LsOpStoreIndexedLocalI32* store = builder.append<LsOpStoreIndexedLocalI32>();
				if (!store || index == LS_IR_INVALID_VALUE) return;
				store->type = lhs->resolved_type;
				store->base_offset = builder.localOffset(base.slot, base.resolved_type);
				store->index = index;
				store->source = value;
				store->scale = array->element_type ? typeByteSize(*array->element_type) : 0;
				store->length = (u32)array->size;
				return;
			}
			if (base.slot && base.slot->storage != StorageSlot::GLOBAL && base.resolved_type && base.resolved_type->kind == ResolvedType::SLICE) {
				SliceResolvedType* slice = static_cast<SliceResolvedType*>(base.resolved_type);
				LsIrValue index = buildExpression(builder, bracket.args[0]);
				LsOpSliceStoreLocalI32* store = builder.append<LsOpSliceStoreLocalI32>();
				if (!store || index == LS_IR_INVALID_VALUE) return;
				store->type = lhs->resolved_type;
				store->slice_offset = builder.localOffset(base.slot, base.resolved_type);
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
	const LsIrValue lhs = buildExpression(builder, expression.lhs);
	const LsIrValue rhs = buildExpression(builder, expression.rhs);
	if (lhs == LS_IR_INVALID_VALUE || rhs == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
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
		default: return LS_IR_INVALID_VALUE;
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
	if (!expression.base || expression.base->kind != Expression::IDENTIFIER || expression.args.size() != 1) return LS_IR_INVALID_VALUE;
	IdentifierExpression& base = *static_cast<IdentifierExpression*>(expression.base);
	if (!base.slot || base.slot->storage == StorageSlot::GLOBAL || !base.resolved_type || base.resolved_type->kind != ResolvedType::ARRAY) return LS_IR_INVALID_VALUE;
	ArrayResolvedType* array = static_cast<ArrayResolvedType*>(base.resolved_type);
	LsIrValue index = buildExpression(builder, expression.args[0]);
	if (index == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
	LsOpLoadIndexedLocalI32* load = builder.append<LsOpLoadIndexedLocalI32>();
	if (!load) return LS_IR_INVALID_VALUE;
	load->type = expression.resolved_type;
	load->result = builder.newValue();
	load->base_offset = builder.localOffset(base.slot, base.resolved_type);
	load->index = index;
	load->scale = array->element_type ? typeByteSize(*array->element_type) : 0;
	load->length = (u32)array->size;
	return load->result;
}

static LsIrValue buildSliceExpression(IrBuilder& builder, SliceExpression& expression) {
	if (!expression.base || expression.base->kind != Expression::IDENTIFIER) return LS_IR_INVALID_VALUE;
	IdentifierExpression& base = *static_cast<IdentifierExpression*>(expression.base);
	if (!base.slot || base.slot->storage == StorageSlot::GLOBAL || !base.resolved_type || base.resolved_type->kind != ResolvedType::ARRAY) return LS_IR_INVALID_VALUE;
	ArrayResolvedType* array = static_cast<ArrayResolvedType*>(base.resolved_type);
	LsOpMakeSlice* make = builder.append<LsOpMakeSlice>();
	if (!make) return LS_IR_INVALID_VALUE;
	make->type = expression.resolved_type;
	make->result = builder.newValue();
	make->base_offset = builder.localOffset(base.slot, base.resolved_type);
	make->length = (u64)array->size;
	if (expression.begin || expression.end) {
		if (!expression.begin || !expression.end) return LS_IR_INVALID_VALUE;
		LsIrValue begin = buildExpression(builder, expression.begin);
		LsIrValue end = buildExpression(builder, expression.end);
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

static LsIrValue buildCall(IrBuilder& builder, CallExpression& expression) {
	FunctionExpression* target = expression.resolved_fn;
	if (!target && expression.callee && expression.callee->kind == Expression::IDENTIFIER) {
		IdentifierExpression& identifier = *static_cast<IdentifierExpression*>(expression.callee);
		target = identifier.resolved_fn;
		if (!target && identifier.symbol && identifier.symbol->expression && identifier.symbol->expression->kind == Expression::FUNCTION)
			target = static_cast<FunctionExpression*>(identifier.symbol->expression);
	}
	if (!target) return LS_IR_INVALID_VALUE;
	const u32 argument_count = (u32)expression.args.size();
	LsIrValue* arguments = nullptr;
	u32* argument_sizes = nullptr;
	if (argument_count) {
		arguments = (LsIrValue*)builder.arena.allocate(builder.arena.user_data, sizeof(LsIrValue) * argument_count, alignof(LsIrValue));
		argument_sizes = (u32*)builder.arena.allocate(builder.arena.user_data, sizeof(u32) * argument_count, alignof(u32));
		if (!arguments || !argument_sizes) return LS_IR_INVALID_VALUE;
	}
	u32 argument_size = 0;
	for (u32 i = 0; i < argument_count; ++i) {
		arguments[i] = buildExpression(builder, expression.args[(i32)i]);
		if (arguments[i] == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
		argument_sizes[i] = expression.args[(i32)i]->resolved_type ? typeByteSize(*expression.args[(i32)i]->resolved_type) : 0;
		argument_size += argument_sizes[i];
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
		case Expression::IDENTIFIER:
			return buildIdentifier(builder, *static_cast<IdentifierExpression*>(expression));
		case Expression::UNARY:
			return buildUnary(builder, *static_cast<UnaryExpression*>(expression));
		case Expression::BINARY:
			return buildBinary(builder, *static_cast<BinaryExpression*>(expression));
		case Expression::CALL:
			return buildCall(builder, *static_cast<CallExpression*>(expression));
		case Expression::BRACKET:
			if (expression->kind == Expression::BRACKET && static_cast<BracketExpression*>(expression)->base && static_cast<BracketExpression*>(expression)->base->resolved_type && static_cast<BracketExpression*>(expression)->base->resolved_type->kind == ResolvedType::SLICE) {
				BracketExpression& bracket = *static_cast<BracketExpression*>(expression);
				IdentifierExpression& base = *static_cast<IdentifierExpression*>(bracket.base);
				LsIrValue index = buildExpression(builder, bracket.args[0]);
				SliceResolvedType* slice = static_cast<SliceResolvedType*>(base.resolved_type);
				LsOpSliceLoadLocalI32* load = builder.append<LsOpSliceLoadLocalI32>();
				if (!load || index == LS_IR_INVALID_VALUE) return LS_IR_INVALID_VALUE;
				load->type = expression->resolved_type;
				load->result = builder.newValue();
				load->slice_offset = builder.localOffset(base.slot, base.resolved_type);
				load->index = index;
				load->element_size = slice->element_type ? typeByteSize(*slice->element_type) : 0;
				return load->result;
			}
			return buildBracket(builder, *static_cast<BracketExpression*>(expression));
		case Expression::SLICE:
			return buildSliceExpression(builder, *static_cast<SliceExpression*>(expression));
		default:
			return LS_IR_INVALID_VALUE;
	}
}

static void buildStatement(IrBuilder& builder, Statement* statement) {
	if (!statement) return;
	switch (statement->kind) {
		case Statement::BLOCK: {
			BlockStatement& block = *static_cast<BlockStatement*>(statement);
			for (Statement* child : block.statements) buildStatement(builder, child);
			break;
		}
		case Statement::EXPRESSION:
			(void)buildExpression(builder, static_cast<ExpressionStatement*>(statement)->expression);
			break;
		case Statement::RETURN: {
			ReturnStatement& return_statement = *static_cast<ReturnStatement*>(statement);
			if (return_statement.expression) {
				const LsIrValue value = buildExpression(builder, return_statement.expression);
				if (value == LS_IR_INVALID_VALUE) return;
				LsOpReturn* op = builder.terminate<LsOpReturn>();
				if (!op) return;
				op->value = value;
				op->result_size = return_statement.expression->resolved_type ? typeByteSize(*return_statement.expression->resolved_type) : 0;
			} else {
				(void)builder.terminate<LsOpReturn>();
			}
			break;
		}
		case Statement::VAR_DECL: {
			VarDeclStatement& declaration = *static_cast<VarDeclStatement*>(statement);
			if (!declaration.expression || declaration.is_comptime) break;
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
			const LsIrValue value = buildExpression(builder, declaration.expression);
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
			const LsIrValue value = buildExpression(builder, assignment.rhs);
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
			targets.continue_target = header;
			targets.break_target = exit;
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
		case Statement::BREAK:
			if (!builder.loops.empty()) {
				LsOpJump* jump = builder.terminate<LsOpJump>();
				if (jump) jump->target = builder.loops.back().break_target;
			}
			break;
		case Statement::CONTINUE:
			if (!builder.loops.empty()) {
				LsOpJump* jump = builder.terminate<LsOpJump>();
				if (jump) {
					jump->target = builder.loops.back().continue_target;
					if (jump->target == LS_IR_INVALID_BLOCK && builder.loops.back().continue_jump_count < 16)
						builder.loops.back().continue_jumps[builder.loops.back().continue_jump_count++] = jump;
				}
			}
			break;
		case Statement::FOR: {
			ForStatement& loop = *static_cast<ForStatement*>(statement);
			if (!loop.begin || loop.is_unroll) break;
			if (!loop.end && loop.begin->kind == Expression::IDENTIFIER && loop.begin->resolved_type && loop.begin->resolved_type->kind == ResolvedType::ARRAY) {
				IdentifierExpression& source = *static_cast<IdentifierExpression*>(loop.begin);
				ArrayResolvedType* array = static_cast<ArrayResolvedType*>(loop.begin->resolved_type);
				if (!source.slot || source.slot->storage == StorageSlot::GLOBAL || !array->element_type) break;
				const u32 base_offset = builder.localOffset(source.slot, source.resolved_type);
				const u32 index_offset = builder.localOffset(&loop.index_slot, builder.function.return_type);
				ResolvedType* index_type = loop.index_slot.type ? loop.index_slot.type : array->element_type;
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
			targets.break_target = exit;
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
			const LsIrValue condition = buildExpression(builder, conditional.condition);
			if (condition == LS_IR_INVALID_VALUE) return;
			const LsIrBlock else_block = builder.newBlock();
			const LsIrBlock then_block = builder.newBlock();
			const LsIrBlock merge_block = builder.newBlock();
			LsOpConditionalJump* jump = builder.terminate<LsOpConditionalJump>();
			if (!jump) return;
			jump->type = conditional.condition ? conditional.condition->resolved_type : nullptr;
			jump->condition = condition;
			jump->target = then_block;
			builder.selectBlock(else_block);
			buildStatement(builder, conditional.else_branch);
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
		default:
			break;
	}
}

}

LsIrFunctionData* lsIrBuildFunction(ls_arena& arena, FunctionExpression* source, ls_string_view name) {
	if (!source || !source->body || source->body->kind != Statement::BLOCK) return nullptr;
	void* memory = arena.allocate(arena.user_data, sizeof(LsIrFunctionData), alignof(LsIrFunctionData));
	if (!memory) return nullptr;
	LsIrFunctionData* function = ::new (NewPlaceholder{}, memory) LsIrFunctionData(arena);
	function->name = name;
	if (source->resolved_type && source->resolved_type->kind == ResolvedType::FUNCTION)
		function->return_type = static_cast<FunctionResolvedType*>(source->resolved_type)->return_type;
	IrBuilder builder(arena, *function);
	buildStatement(builder, source->body);
	if (!builder.block->terminator) builder.terminate<LsOpReturn>();
	return function;
}

LsIrModuleData* lsIrBuildModule(ls_arena& arena, ls_module* source) {
	if (!source) return nullptr;
	void* memory = arena.allocate(arena.user_data, sizeof(LsIrModuleData), alignof(LsIrModuleData));
	if (!memory) return nullptr;
	LsIrModuleData* module = ::new (NewPlaceholder{}, memory) LsIrModuleData(arena);
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
			if (function->is_template || (!function->is_extern && !function->body)) continue;
			function->bytecode_index = function_index++;
		}
	}
	for (Unit& unit : source->units) {
		for (Symbol& symbol : unit.symbols) {
			if (!symbol.expression || symbol.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* function = static_cast<FunctionExpression*>(symbol.expression);
			if (function->is_template || (!function->is_extern && !function->body)) continue;
			LsIrModuleEntry& entry = module->functions.emplace_back();
			entry.source = function;
			entry.name = symbol.name;
			entry.native = function->is_extern;
			if (!entry.native) {
				entry.function = lsIrBuildFunction(arena, function, symbol.name);
				if (!entry.function) return nullptr;
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
	registers.resize(function->next_value, LS_IR_INVALID_VALUE);
	u32 frame_size = 0;
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
				const u32 end = op.base_offset + op.scale * op.length + typeRegisterSize(op.type);
				if (end > frame_size) frame_size = end;
			} else if (base->kind == LS_IR_OP_STORE_INDEXED) {
				LsOpStoreIndexed& op = *static_cast<LsOpStoreIndexed*>(base);
				const u32 end = op.base_offset + op.scale * op.length + typeRegisterSize(op.type);
				if (end > frame_size) frame_size = end;
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
				case LS_IR_OP_GLOBAL_LOAD: result = static_cast<LsOpGlobalLoad*>(base)->result; type = static_cast<LsOpGlobalLoad*>(base)->type; break;
				case LS_IR_OP_LOCAL_LOAD: result = static_cast<LsOpLocalLoad*>(base)->result; type = static_cast<LsOpLocalLoad*>(base)->type; break;
				case LS_IR_OP_LOAD_INDEXED: result = static_cast<LsOpLoadIndexed*>(base)->result; type = static_cast<LsOpLoadIndexed*>(base)->type; break;
				case LS_IR_OP_MAKE_SLICE: result = static_cast<LsOpMakeSlice*>(base)->result; type = static_cast<LsOpMakeSlice*>(base)->type; break;
				case LS_IR_OP_SLICE: result = static_cast<LsOpSlice*>(base)->result; type = static_cast<LsOpSlice*>(base)->type; break;
				case LS_IR_OP_SLICE_LOAD: result = static_cast<LsOpSliceLoad*>(base)->result; type = static_cast<LsOpSliceLoad*>(base)->type; break;
				case LS_IR_OP_SLICE_LENGTH: result = static_cast<LsOpSliceLength*>(base)->result; type = nullptr; break;
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
				default: break;
			}
			if (result == LS_IR_INVALID_VALUE) continue;
			if (result >= function->next_value || registers[result] != LS_IR_INVALID_VALUE) continue;
			const u32 size = base->kind == LS_IR_OP_COMPARE ? 1u : base->kind == LS_IR_OP_SLICE_LENGTH ? 8u : typeRegisterSize(type);
			registers[result] = frame_size;
			frame_size += size;
			if (base->kind == LS_IR_OP_CALL_DIRECT || base->kind == LS_IR_OP_CALL_NATIVE) frame_size += static_cast<LsOpCallDirect*>(base)->argument_size;
		}
	}

	for (LsIrBlockData& block : function->blocks) {
		block_offsets.push_back(code.size);
		for (LsIrOp* base : block.ops) {
			switch (base->kind) {
				case LS_IR_OP_LOAD_CONST: {
					LsOpLoadConst& op = *static_cast<LsOpLoadConst*>(base);
					if (op.result >= function->next_value) return nullptr;
					const u32 size = typeRegisterSize(op.type);
					const ls_op opcode = size == 1 ? LS_OP_LOAD_CONST_1 : size == 2 ? LS_OP_LOAD_CONST_2 : size == 4 ? LS_OP_LOAD_CONST_4 : LS_OP_LOAD_CONST_8;
					if (!code.op(opcode) || !code.writeValue(registers[op.result])) return nullptr;
					if (size == 1 && !code.writeValue((u8)op.value)) return nullptr;
					if (size == 2 && !code.writeValue((u16)op.value)) return nullptr;
					if (size == 4 && !code.writeValue((u32)op.value)) return nullptr;
					if (size == 8 && !code.writeValue(op.value)) return nullptr;
					break;
				}
				case LS_IR_OP_COPY: {
					LsOpCopy& op = *static_cast<LsOpCopy*>(base);
					if (op.result >= function->next_value || op.source >= function->next_value || !code.op(LS_OP_COPY) ||
						!code.writeValue(registers[op.result]) || !code.writeValue(registers[op.source]) || !code.writeValue(typeRegisterSize(op.type))) return nullptr;
					break;
				}
				case LS_IR_OP_LOCAL_LOAD: {
					LsOpLocalLoad& op = *static_cast<LsOpLocalLoad*>(base);
					if (op.result >= function->next_value || !code.op(LS_OP_COPY) || !code.writeValue(registers[op.result]) ||
						!code.writeValue(op.offset) || !code.writeValue(typeRegisterSize(op.type))) return nullptr;
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
					if (op.result >= function->next_value || op.index >= function->next_value || op.base_offset == LS_IR_INVALID_VALUE ||
						!code.op(LS_OP_LOAD_INDEXED_LOCAL_I32) || !code.writeValue(registers[op.result]) || !code.writeValue(op.base_offset) ||
						!code.writeValue(registers[op.index]) || !code.writeValue(op.scale) || !code.writeValue((i32)op.offset) ||
						!code.writeValue(op.length) || !code.writeValue(typeRegisterSize(op.type))) return nullptr;
					break;
				}
				case LS_IR_OP_STORE_INDEXED: {
					LsOpStoreIndexed& op = *static_cast<LsOpStoreIndexed*>(base);
					if (op.source >= function->next_value || op.index >= function->next_value || op.base_offset == LS_IR_INVALID_VALUE ||
						!code.op(LS_OP_STORE_INDEXED_LOCAL_I32) || !code.writeValue(op.base_offset) || !code.writeValue(registers[op.index]) ||
						!code.writeValue(registers[op.source]) || !code.writeValue(op.scale) || !code.writeValue((i32)op.offset) ||
						!code.writeValue(op.length) || !code.writeValue(typeRegisterSize(op.type))) return nullptr;
					break;
				}
				case LS_IR_OP_MAKE_SLICE: {
					LsOpMakeSlice& op = *static_cast<LsOpMakeSlice*>(base);
					if (op.result >= function->next_value || !code.op(LS_OP_LOCAL_REF) || !code.writeValue(registers[op.result]) ||
						!code.writeValue(op.base_offset) || !code.op(LS_OP_LOAD_CONST_8) || !code.writeValue((u32)(registers[op.result] + sizeof(void*))) ||
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
						!code.writeValue(registers[op.result]) || !code.writeValue(op.slice_offset)) return nullptr;
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
						!code.writeValue(registers[op.rhs]) || !code.writeValue((u8)numericTypeIndex(op.type))) return nullptr;
					break;
				}
				case LS_IR_OP_NEG: {
					LsOpNeg& op = *static_cast<LsOpNeg*>(base);
					const ls_op opcode = unaryOpcode(base->kind, numericTypeIndex(op.type));
					if (!opcode || op.result >= function->next_value || op.value >= function->next_value || !code.op(opcode) ||
						!code.writeValue(registers[op.result]) || !code.writeValue(registers[op.value])) return nullptr;
					break;
				}
				case LS_IR_OP_NOT: {
					LsOpNot& op = *static_cast<LsOpNot*>(base);
					if (op.result >= function->next_value || op.value >= function->next_value || !code.op(LS_OP_NOT) ||
						!code.writeValue(registers[op.result]) || !code.writeValue(registers[op.value])) return nullptr;
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
				default:
					return nullptr;
			}
		}
		if (!block.terminator) return nullptr;
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
	output.param_size = 0;
	output.return_size = function->return_type ? typeByteSize(*function->return_type) : 0;
	output.return_kind = function->return_type ? (ls_type_kind)function->return_type->kind : LS_TYPE_VOID;
	output.frame_size = frame_size;
	output.code = code.data;
	output.code_size = code.size;
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
			output.return_kind = LS_TYPE_VOID;
			if (entry.source && entry.source->resolved_type && entry.source->resolved_type->kind == ResolvedType::FUNCTION) {
				FunctionResolvedType* type = static_cast<FunctionResolvedType*>(entry.source->resolved_type);
				output.return_kind = type->return_type ? (ls_type_kind)type->return_type->kind : LS_TYPE_VOID;
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
	return bytecode;
}
