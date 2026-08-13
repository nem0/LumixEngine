#include "ir.h"
#include "bytecode.h"
#include "compiler.h"
#include <string.h>

namespace {

// AST to IR
struct IRBuilder {
	IRBuilder(ls_host& host)
		: host(host)
		, locals(host.arena)
		, defers(host.arena)
		, loops(host.arena) {}

	template <typename T, typename... Args>
	T& alloc(Args&&... args) {
		T* v = (T*)host.arena.allocate(host.arena.user_data, sizeof(T), alignof(T));
		new (NewPlaceholder(), v) T(static_cast<Args&&>(args)...);
		return *v;
	} 

	LsIrOp& buildExpressionIR(Expression& expr, bool as_rvalue) {
		switch (expr.kind) {
			case Expression::UNDEFINED: return alloc<LsOpNop>();
			case Expression::BRACKET: {
				auto& be = static_cast<BracketExpression&>(expr);
				ASSERT(be.base->resolved_type->kind == ResolvedType::ARRAY);
				LsIrOp& base = buildExpressionIR(*be.base, false);
				ASSERT(be.args.size() == 1);
				LsIrOp& index = buildExpressionIR(*be.args[0], true);
				auto& add = alloc<LsOpAdd>();
				auto& mul = alloc<LsOpMul>();
				auto& size = alloc<LsOpLoadConst>();
				static ResolvedType R(ResolvedType::U32);
				size.type = &R;
				mul.operand_type = &R;
				add.operand_type = &R;

				u32 elem_size = typeByteSize(*be.resolved_type);
				memcpy(size.value, &elem_size, sizeof(elem_size));

				mul.lhs = &index;
				mul.rhs = &size;
				add.lhs = &base;
				add.rhs = &mul;
				add.output_storage = base.output_storage;
				if (!as_rvalue) return add;

				auto& load = alloc<LsOpLocalLoad>();
				load.addr = &add;
				load.size = elem_size;
				return load;
			}
			case Expression::MEMBER: {
				auto& me = static_cast<MemberExpression&>(expr);
				auto* struct_type = static_cast<StructResolvedType*>(me.expression->resolved_type);
				auto& fields = struct_type->decl->fields;
				u32 offset = 0;
				for (u32 i = 0; i < fields.size(); ++i) {
					if (!equalStrings(fields[i].name, me.name)) {
						offset += typeByteSize(*struct_type->field_types[i]);
						continue;
					}

					if (as_rvalue) {
						auto& base = buildExpressionIR(*me.expression, false);
						switch (base.output_storage) {
							case LsIrOp::LOCAL_REF: {
								auto& add = alloc<LsOpAdd>();
								add.lhs = &base;
								auto& rhs = alloc<LsOpLoadConst>();
								memcpy(&rhs.value, &offset, sizeof(offset));
								static ResolvedType R(ResolvedType::U32);
								rhs.type = &R;
								add.rhs = &rhs;
								add.operand_type = &R;

								auto& load = alloc<LsOpLocalLoad>();
								load.addr = &add;
								load.size = typeByteSize(*me.resolved_type);
								return load;
							}
							case LsIrOp::GLOBAL_REF: {
								auto& add = alloc<LsOpAdd>();
								add.lhs = &base;
								auto& rhs = alloc<LsOpLoadConst>();
								memcpy(&rhs.value, &offset, sizeof(offset));
								static ResolvedType R(ResolvedType::U32);
								rhs.type = &R;
								add.rhs = &rhs;
								add.operand_type = &R;

								auto& load = alloc<LsOpGlobalLoad>();
								load.addr = &add;
								load.size = typeByteSize(*me.resolved_type);
								return load;
							}
							case LsIrOp::VALUE: {
								auto& extract = alloc<LsOpExtractValue>();
								extract.value = &buildExpressionIR(*me.expression, true);
								extract.offset = offset;
								extract.size = typeByteSize(*me.resolved_type);
								return extract;
							}
						}
						ASSERT(false);
					} 				
					else {
						auto& add = alloc<LsOpAdd>();
						auto& base = buildExpressionIR(*me.expression, false);
						add.lhs = &base;
						auto& rhs = alloc<LsOpLoadConst>();
						memcpy(&rhs.value, &offset, sizeof(offset));
						static ResolvedType R(ResolvedType::U32);
						rhs.type = &R;
						add.rhs = &rhs;
						add.operand_type = &R;
						add.output_storage = base.output_storage;
						
						return add;
					}
				}
				break;
			}
			case Expression::IDENTIFIER: {
				auto& ie = static_cast<IdentifierExpression&>(expr);
				FunctionExpression* value_function = ie.resolved_fn;
				if (!value_function && ie.symbol && ie.symbol->expression && ie.symbol->expression->kind == Expression::FUNCTION) {
					value_function = static_cast<FunctionExpression*>(ie.symbol->expression);
				}
				if (as_rvalue && value_function) {
					auto& op = alloc<LsOpLoadConst>();
					op.type = ie.resolved_type;
					memcpy(op.value, &value_function->bytecode_index, sizeof(value_function->bytecode_index));
					return op;
				}
				if (ie.symbol) {
					if (as_rvalue) {
						auto& addr = alloc<LsOpPushGlobalAddr>();
						addr.symbol = ie.symbol;
						auto& load = alloc<LsOpGlobalLoad>();
						load.addr = &addr;
						load.size = typeByteSize(*ie.resolved_type);
						return load;
					}
					else {
						auto& op = alloc<LsOpPushGlobalAddr>();
						op.symbol = ie.symbol;
						return op;
					}
				}

				for (i32 i = locals.size() - 1; i >= 0; --i) {
					if (!equalStrings(locals[i].name, ie.name)) continue;

					if (as_rvalue) {
						auto& addr = alloc<LsOpPushLocalAddr>();
						addr.alloca = locals[i].alloca;
						auto& load = alloc<LsOpLocalLoad>();
						load.addr = &addr;
						load.size = typeByteSize(*ie.resolved_type);
						return load;
					}
					else {
						auto& op = alloc<LsOpPushLocalAddr>();
						op.alloca = locals[i].alloca;
						return op;
					}
				}
				break;
			}
			case Expression::CALL: {
				auto& call = static_cast<CallExpression&>(expr);
				FunctionExpression* function = call.resolved_fn;
				if (!function && call.callee->kind == Expression::IDENTIFIER) {
					auto& callee = static_cast<IdentifierExpression&>(*call.callee);
					if (callee.symbol && callee.symbol->expression && callee.symbol->expression->kind == Expression::FUNCTION) {
						function = static_cast<FunctionExpression*>(callee.symbol->expression);
					}
				}
				if (function && function->bytecode_index != ~0u) {
					auto& op = alloc<LsOpCallDirect>();
					op.function = function;
					op.arg_count = call.args.size();
					op.return_size = typeByteSize(*call.resolved_type);
					op.args = static_cast<LsIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(LsIrOp*) * op.arg_count, alignof(LsIrOp*)));
					for (u32 i = 0; i < op.arg_count; ++i) op.args[i] = &buildExpressionIR(*call.args[i], true);
					return op;
				}

				ASSERT(call.callee->resolved_type && call.callee->resolved_type->kind == ResolvedType::FUNCTION);
				auto& fn_type = static_cast<FunctionResolvedType&>(*call.callee->resolved_type);
				auto& op = alloc<LsOpCallIndirect>();
				op.callee = &buildExpressionIR(*call.callee, true);
				op.arg_count = call.args.size();
				op.return_size = typeByteSize(*call.resolved_type);
				op.args = static_cast<LsIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(LsIrOp*) * op.arg_count, alignof(LsIrOp*)));
				op.arg_sizes = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * op.arg_count, alignof(u32)));
				for (u32 i = 0; i < op.arg_count; ++i) {
					op.args[i] = &buildExpressionIR(*call.args[i], true);
					op.arg_sizes[i] = typeByteSize(*fn_type.params[i].type);
				}
				return op;
			}
			case Expression::ARRAY_LITERAL: {
				auto& ale = static_cast<ArrayLiteralExpression&>(expr);
				auto& op = alloc<LsOpAggregateInit>();
				// TODO
				ASSERT(false);
				break;
			}
			case Expression::STRUCT_LITERAL: {
				auto& sle = static_cast<StructLiteralExpression&>(expr);
				auto& op = alloc<LsOpAggregateInit>();
				op.type = sle.resolved_type;
				op.value_count = sle.values.size();
				op.values = static_cast<LsIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(LsIrOp*) * op.value_count, alignof(LsIrOp*)));
				if (op.type->kind == ResolvedType::STRUCT) {
					StructResolvedType* st = static_cast<StructResolvedType*>(op.type);
					op.offsets = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * op.value_count, alignof(u32)));
					op.sizes = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * op.value_count, alignof(u32)));
					u32 offset = 0;
					for (u32 i = 0; i < op.value_count; ++i) {
						ResolvedType* field_type = st->field_types[i];
						op.offsets[i] = offset;
						op.sizes[i] = typeByteSize(*field_type);
						offset += op.sizes[i];
					}
				}
				for (u32 i = 0; i < op.value_count; ++i) op.values[i] = &buildExpressionIR(*sle.values[i], true);
				return op;
			}
			case Expression::CAST: {
				auto& ce = static_cast<CastExpression&>(expr);
				LsIrOp& inner = buildExpressionIR(*ce.expression, true);
				auto& op = alloc<LsOpCast>();
				op.type = ce.resolved_type;
				op.target_type = ce.expression->resolved_type;
				op.value = &inner;
				return op;
			}
			case Expression::NULL_LITERAL: {
				auto& op = alloc<LsOpNull>();
				op.size = typeByteSize(*expr.resolved_type);
				return op;
			}
			case Expression::BOOL_LITERAL: {
				auto& ble = static_cast<BoolLiteralExpression&>(expr);
				auto& op = alloc<LsOpLoadConst>();
				op.type = ble.resolved_type;
				u8 value = ble.value ? 1 : 0;
				memcpy(&op.value, &value, sizeof(value));
				return op;
			}
			case Expression::INT_LITERAL: {
				auto& ile = static_cast<IntLiteralExpression&>(expr);
				auto& op = alloc<LsOpLoadConst>();
				op.type = ile.resolved_type;
				if (ile.resolved_type->kind == ResolvedType::F32) {
					const float value = (float)ile.value;
					memcpy(&op.value, &value, sizeof(value));
				}
				else if (ile.resolved_type->kind == ResolvedType::F64) {
					const double value = (double)ile.value;
					memcpy(&op.value, &value, sizeof(value));
				}
				else {
					memcpy(&op.value, &ile.value, sizeof(ile.value));
				}
				return op;
			}
			case Expression::FLOAT_LITERAL: {
				auto& fle = static_cast<FloatLiteralExpression&>(expr);
				auto& op = alloc<LsOpLoadConst>();
				op.type = fle.resolved_type;
				if (fle.resolved_type->kind == ResolvedType::F32) {
					float value = (float)fle.value;
					memcpy(&op.value, &value, sizeof(value));
				}
				else {
					memcpy(&op.value, &fle.value, sizeof(fle.value));
				}
				return op;
			}
			case Expression::UNARY: {
				auto& unary = static_cast<UnaryExpression&>(expr);
				LsIrOp& operand = buildExpressionIR(*unary.expression, true);
				if (unary.op == Token::MINUS) {
					auto& op = alloc<LsOpNeg>();
					op.operand_type = unary.resolved_type;
					op.operand = &operand;
					return op;
				}
				if (unary.op == Token::NOT) {
					auto& op = alloc<LsOpNot>();
					op.operand_type = unary.resolved_type;
					op.operand = &operand;
					return op;
				}
				break;
			}
			case Expression::BINARY: {
				auto& be = static_cast<BinaryExpression&>(expr);
				LsIrOp& lhs = buildExpressionIR(*be.lhs, true);
				LsIrOp& rhs = buildExpressionIR(*be.rhs, true);
				if (be.operator_fn) {
					auto& op = alloc<LsOpCallDirect>();
					op.function = be.operator_fn;
					op.arg_count = 2;
					op.return_size = typeByteSize(*be.resolved_type);
					op.args = static_cast<LsIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(LsIrOp*) * op.arg_count, alignof(LsIrOp*)));
					op.args[0] = &lhs;
					op.args[1] = &rhs;
					return op;
				}
				LsOpBinary* op = nullptr;
				switch (be.op) {
					case Token::PLUS: op = &alloc<LsOpAdd>(); break;
					case Token::MINUS: op = &alloc<LsOpSub>(); break;
					case Token::STAR: op = &alloc<LsOpMul>(); break;
					case Token::SLASH: op = &alloc<LsOpDiv>(); break;
					case Token::PERCENT: op = &alloc<LsOpMod>(); break;
					case Token::GT: op = &alloc<LsOpGt>(); break;
					case Token::LT: op = &alloc<LsOpLt>(); break;
					case Token::GT_EQUAL: op = &alloc<LsOpGe>(); break;
					case Token::LT_EQUAL: op = &alloc<LsOpLe>(); break;
					case Token::EQUAL_EQUAL: op = &alloc<LsOpEq>(); break;
					case Token::BANG_EQUAL: op = &alloc<LsOpNe>(); break;
					case Token::AND: op = &alloc<LsOpAnd>(); break;
					case Token::OR: op = &alloc<LsOpOr>(); break;
					default: ASSERT(false); break;
				}
				op->operand_type = be.lhs->resolved_type;
				op->lhs = &lhs;
				op->rhs = &rhs;
				return *op;
			}
		}
		ASSERT(false);
		static LsIrOp dummy(LS_IR_OP_RETURN);
		return dummy;
	}

	void emitDefers(LsIrBlockData& parent, u32 mark) {
		for (i32 i = defers.size() - 1; i >= (i32)mark; --i) {
			buildStatementIR(*defers[i], parent);
		}
	}

	void buildStatementIR(Statement& st, LsIrBlockData& parent) {
		switch (st.kind) {
			case Statement::LABEL: {
				auto& label = static_cast<LabelStatement&>(st);
				const ls_string_view previous_label = pending_loop_label;
				pending_loop_label = label.name;
				buildStatementIR(*label.statement, parent);
				pending_loop_label = previous_label;
				break;
			}
			case Statement::WHILE: {
				auto& while_statement = static_cast<WhileStatement&>(st);
				auto& loop = alloc<LsOpConditionalJump>();
				auto& exit = alloc<LsOpNop>();
				loop.condition = &buildExpressionIR(*while_statement.condition, true);
				loop.true_block = &alloc<LsIrBlockData>(host.arena);
				loops.push({pending_loop_label, &loop, &exit, (u32)defers.size()});
				pending_loop_label = {};
				buildStatementIR(*while_statement.body, *loop.true_block);
				loops.pop_back();
				auto& back_edge = alloc<LsOpJump>();
				back_edge.target = &loop;
				loop.true_block->ops.push(&back_edge);
				parent.ops.push(&loop);
				parent.ops.push(&exit);
				break;
			}
			case Statement::BREAK:
			case Statement::CONTINUE: {
				const bool is_break = st.kind == Statement::BREAK;
				const ls_string_view label = is_break
					? static_cast<BreakStatement&>(st).label
					: static_cast<ContinueStatement&>(st).label;
				Loop* target = nullptr;
				if (size(label) == 0) {
					target = &loops.back();
				}
				else {
					for (i32 i = loops.size() - 1; i >= 0; --i) {
						if (equalStrings(label, loops[i].label)) {
							target = &loops[i];
							break;
						}
					}
				}
				ASSERT(target);
				emitDefers(parent, target->defer_watermark);
				auto& jump = alloc<LsOpJump>();
				jump.target = is_break ? target->break_target : target->continue_target;
				parent.ops.push(&jump);
				break;
			}
			case Statement::IF: {
				auto& ifs = static_cast<IfStatement&>(st);
				auto& if_ir = alloc<LsOpConditionalJump>();
				if_ir.condition = &buildExpressionIR(*ifs.condition, true);
				if_ir.true_block = &alloc<LsIrBlockData>(host.arena);
				buildStatementIR(*ifs.body, *if_ir.true_block);
				if (ifs.else_branch) {
					if_ir.false_block = &alloc<LsIrBlockData>(host.arena);
					buildStatementIR(*ifs.else_branch, *if_ir.false_block);
				}
				parent.ops.push(&if_ir);
				break;
			}
			case Statement::ASSIGN: {
				auto& as = static_cast<AssignStatement&>(st);
				auto& rhs = buildExpressionIR(*as.rhs, true);
				auto& lhs = buildExpressionIR(*as.lhs, false);
				if (as.op == Token::EQUAL) {
					auto& cpy = alloc<LsOpCopy>();
					cpy.src = &rhs;
					cpy.dst = &lhs;
					cpy.type = as.lhs->resolved_type;
					parent.ops.push(&cpy);
					return;
				}
				LsIrOp* lhs_value;
				if (lhs.output_storage == LsIrOp::GLOBAL_REF) {
					auto& load = alloc<LsOpGlobalLoad>();
					load.addr = &lhs;
					load.size = typeByteSize(*as.lhs->resolved_type);
					lhs_value = &load;
				}
				else {
					auto& load = alloc<LsOpLocalLoad>();
					load.addr = &lhs;
					load.size = typeByteSize(*as.lhs->resolved_type);
					lhs_value = &load;
				}
				LsOpBinary* op = nullptr;
				switch (as.op) {
					case Token::STAR_EQUAL: op = &alloc<LsOpMul>(); break;
					case Token::PLUS_EQUAL: op = &alloc<LsOpAdd>(); break;
					case Token::MINUS_EQUAL: op = &alloc<LsOpSub>(); break;
					case Token::SLASH_EQUAL: op = &alloc<LsOpDiv>(); break;
					default: ASSERT(false); return;
				}
				op->operand_type = as.lhs->resolved_type;
				op->lhs = lhs_value;
				op->rhs = &rhs;
				auto& cpy = alloc<LsOpCopy>();
				cpy.src = op;
				cpy.dst = &lhs;
				cpy.type = as.lhs->resolved_type;
				parent.ops.push(&cpy);
				break;
			}
			case Statement::VAR_DECL: {
				auto& vd = static_cast<VarDeclStatement&>(st);
				auto& alloca = alloc<LsOpAlloca>();
				alloca.type = vd.resolved_type;
				alloca.value = &buildExpressionIR(*vd.expression, true);
				locals.push({vd.name, &alloca});
				parent.ops.push(&alloca);
				break;
			}
			case Statement::BLOCK: {
				u32 local_watermark = locals.size();
				u32 defer_watermark = defers.size();
				auto& bl = static_cast<BlockStatement&>(st);
				for (Statement* s : bl.statements) {
					buildStatementIR(*s, parent);
				}
				emitDefers(parent, defer_watermark);
				defers.resize(defer_watermark);
				locals.resize(local_watermark);
				break;
			}
			case Statement::RETURN: {
				auto& ret = static_cast<ReturnStatement&>(st);
				auto& ir_ret = alloc<LsOpReturn>();
				if (ret.expression) {
					ir_ret.size = typeByteSize(*ret.expression->resolved_type);
					LsIrOp& expression = buildExpressionIR(*ret.expression, true);
					if (defers.empty()) {
						ir_ret.expression = &expression;
					}
					else {
						auto& value = alloc<LsOpAlloca>();
						value.type = ret.expression->resolved_type;
						value.value = &expression;
						parent.ops.push(&value);
						auto& addr = alloc<LsOpPushLocalAddr>();
						addr.alloca = &value;
						auto& load = alloc<LsOpLocalLoad>();
						load.addr = &addr;
						load.size = ir_ret.size;
						ir_ret.expression = &load;
					}
				}
				emitDefers(parent, 0);
				parent.ops.push(&ir_ret);
				break;
			}
			case Statement::DEFER: {
				auto& defer = static_cast<DeferStatement&>(st);
				if (defer.statement) defers.push(defer.statement);
				break;
			}
			default: ASSERT(false); break;
		}
	}

	LsIrBlockData& buildFunctionIR(FunctionExpression& expr) {
		ASSERT(locals.empty());
		LsIrBlockData& root = alloc<LsIrBlockData>(host.arena);
		u32 param_offset = 0;
		for (FunctionParam& param : expr.params) {
			if (param.is_comptime) continue;
			auto& alloca = alloc<LsOpAlloca>();
			alloca.type = param.resolved_type;
			alloca.stack_sp = param_offset;
			locals.push({param.name, &alloca});
			param.slot.storage = StorageSlot::LOCAL;
			param.slot.offset = param_offset;
			param.slot.byte_size = typeByteSize(*param.resolved_type);
			param.slot.type = param.resolved_type;
			param_offset += param.slot.byte_size;
		}
		buildStatementIR(*expr.body, root);
		locals.clear();
		return root;
	}

	struct Local {
		ls_string_view name;
		LsOpAlloca* alloca = nullptr;
	};
	struct Loop {
		ls_string_view label = {};
		LsIrOp* continue_target = nullptr;
		LsIrOp* break_target = nullptr;
		u32 defer_watermark = 0;
	};

	ls_host& host;
	ExpArray<Local> locals;
	ExpArray<Statement*> defers;
	ExpArray<Loop> loops;
	ls_string_view pending_loop_label = {};
};

struct ByteArray {
	explicit ByteArray(ls_arena& arena)
		: arena(arena)
		, source_map(arena) {}

	void push_back(u8 value) {
		if (count == capacity) {
			const u32 new_capacity = capacity ? capacity * 2u : 64u;
			u8* new_data = static_cast<u8*>(arena.allocate(arena.user_data, new_capacity, alignof(u8)));
			ASSERT(new_data);
			if (data) copyMemory(new_data, data, count);
			data = new_data;
			capacity = new_capacity;
		}
		data[count++] = value;
	}

	i32 size() const { return (i32)count; }
	u8& operator[](u32 index) {
		ASSERT(index < count);
		return data[index];
	}

	ls_arena& arena;
	u8* data = nullptr;
	u32 count = 0;
	u32 capacity = 0;
	ExpArray<ls_bytecode_source_map_entry> source_map;
	Token current_source = {};
	bool has_current_source = false;
};


// IR to bytecode
struct BytecodeCompiler {
	BytecodeCompiler(ls_host& host)
		: host(host)
		, code(host.arena)
	{}

	template <typename T, typename... Args>
	T& alloc(Args&&... args) {
		T* v = (T*)host.arena.allocate(host.arena.user_data, sizeof(T), alignof(T));
		new (NewPlaceholder(), v) T(static_cast<Args&&>(args)...);
		return *v;
	} 
	
	void emitOp(ls_op op) { u8 tmp = (u8)op; emit(&tmp, sizeof(tmp)); }
	
	template <typename T> void emit(T value) { emit(&value, sizeof(value)); }

	static u32 numericKindIndex(const ResolvedType& type) {
		switch (type.kind) {
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
			default: ASSERT(false); return -1;
		}
	}

	u32 emitCompare(const LsOpBinary& cmp) {
		u32 byte_size = typeByteSize(*cmp.operand_type);
		u32 lhs_offset = emit(*cmp.lhs);
		u32 rhs_offset = emit(*cmp.rhs);
		switch (cmp.kind) {
			case LS_IR_OP_EQ: emitOp(LS_OP_EQ); break;
			case LS_IR_OP_NE: emitOp(LS_OP_NE); break;
			case LS_IR_OP_LT: emitOp(LS_OP_LT); break;
			case LS_IR_OP_LE: emitOp(LS_OP_LE); break;
			case LS_IR_OP_GT: emitOp(LS_OP_GT); break;
			case LS_IR_OP_GE: emitOp(LS_OP_GE); break;
			default: ASSERT(false); break;
		}
		emit(stack_top);
		u32 result = stack_top++;
		emit(lhs_offset);
		emit(rhs_offset);
		emit((u8)toTypeKind(*cmp.operand_type));
		return result;
	}

	u32 emitUnary(const LsOpUnary& op) {
		const u32 operand = emit(*op.operand);
		if (op.kind == LS_IR_OP_NEG) emitOp(ls_op(LS_OP_NEG_I8 + numericKindIndex(*op.operand_type)));
		else emitOp(LS_OP_NOT);
		emit(operand);
		return operand;
	}

	void patchI16(u32 position, u32 target) {
		const i64 relative = (i64)target - (i64)(position + sizeof(i16));
		ASSERT(relative >= -32768 && relative <= 32767);
		const i16 offset = (i16)relative;
		memcpy(code.data + position, &offset, sizeof(offset));
	}

	u32 emitConditionalJump(const LsOpConditionalJump& conditional) {
		const u32 entry_stack_top = stack_top;
		const u32 condition = emit(*conditional.condition);
		emitOp(LS_OP_JZ_U8);
		emit(condition);
		const u32 false_jump = code.size();
		emit((i16)0);

		emit(*conditional.true_block);
		u32 high_water = stack_top;
		if (!conditional.false_block) {
			patchI16(false_jump, code.size());
			stack_top = high_water;
			return entry_stack_top;
		}

		emitOp(LS_OP_JUMP);
		const u32 end_jump = code.size();
		emit((i16)0);
		patchI16(false_jump, code.size());
		stack_top = entry_stack_top;
		emit(*conditional.false_block);
		if (stack_top > high_water) high_water = stack_top;
		patchI16(end_jump, code.size());
		stack_top = high_water;
		return entry_stack_top;
	}

	u32 emitJump(LsOpJump& jump) {
		ASSERT(jump.target);
		emitOp(LS_OP_JUMP);
		jump.bytecode_patch_offset = code.size();
		emit((i16)0);
		return stack_top;
	}

	u32 emitShortCircuit(const LsOpBinary& op) {
		const u32 lhs = emit(*op.lhs);
		emitOp(op.kind == LS_IR_OP_AND ? LS_OP_JZ_U8 : LS_OP_JNZ_U8);
		emit(lhs);
		const u32 short_jump = code.size();
		emit((i16)0);

		const u32 rhs = emit(*op.rhs);
		const u32 result = stack_top;
		stack_top++;
		emitOp(LS_OP_COPY);
		emit(result);
		emit(rhs);
		emit((u32)1);
		emitOp(LS_OP_JUMP);
		const u32 end_jump = code.size();
		emit((i16)0);

		patchI16(short_jump, code.size());
		emitOp(LS_OP_COPY);
		emit(result);
		emit(lhs);
		emit((u32)1);
		patchI16(end_jump, code.size());
		return result;
	}

	void patchJumps(LsIrBlockData& block) {
		for (LsIrOp* op : block.ops) {
			if (op->kind == LS_IR_OP_JUMP) {
				auto& jump = static_cast<LsOpJump&>(*op);
				ASSERT(jump.target);
				ASSERT(jump.target->bytecode_offset != 0xffffffffu);
				ASSERT(jump.bytecode_patch_offset != 0xffffffffu);
				patchI16(jump.bytecode_patch_offset, jump.target->bytecode_offset);
			}
			else if (op->kind == LS_IR_OP_CONDITIONAL_JUMP) {
				auto& conditional = static_cast<LsOpConditionalJump&>(*op);
				if (conditional.true_block) patchJumps(*conditional.true_block);
				if (conditional.false_block) patchJumps(*conditional.false_block);
			}
		}
	}

	u32 emitBinary(ls_op base_op, const LsOpBinary& ir_op) {
		u32 lhs = emit(*ir_op.lhs);
		u32 rhs = emit(*ir_op.rhs);
		emitOp(ls_op(base_op + numericKindIndex(*ir_op.operand_type)));
		emit(stack_top);
		u32 ret = stack_top;
		emit(lhs);
		emit(rhs);
		stack_top += typeByteSize(*ir_op.operand_type);
		return ret;
	}

	u32 emitLoadConst(const LsOpLoadConst& load) {
		u32 size = typeByteSize(*load.type);
		switch (size) {
			case 1:
				emitOp(LS_OP_LOAD_CONST_1);
				emit(stack_top);
				stack_top += 1;
				emit(&load.value, 1);
				return stack_top - 1;
			case 2:
				emitOp(LS_OP_LOAD_CONST_2);
				emit(stack_top);
				stack_top += 2;
				emit(&load.value, 2);
				return stack_top - 2;
			case 4:
				emitOp(LS_OP_LOAD_CONST_4);
				emit(stack_top);
				stack_top += 4;
				emit(&load.value, 4);
				return stack_top - 4;
			case 8:
				emitOp(LS_OP_LOAD_CONST_8);
				emit(stack_top);
				stack_top += 8;
				emit(&load.value, 8);
				return stack_top - 8;
		}
		ASSERT(false);
		return 0xffFFffFF;
	}

	u32 emitAlloca(LsOpAlloca& alloca) {
		if (alloca.value->kind != LS_IR_OP_NOP) {
			u32 ret = emit(*alloca.value);
			alloca.stack_sp = ret;
			return ret;
		}
		alloca.stack_sp = stack_top;
		stack_top += typeByteSize(*alloca.type);
		return alloca.stack_sp;
	}

	u32 emitCopy(const LsOpCopy& copy) {
		u32 src = emit(*copy.src);
		u32 dst = emit(*copy.dst);
		switch (copy.dst->output_storage) {
			case LsIrOp::GLOBAL_REF: {
				emitOp(LS_OP_STORE_GLOBAL_REF);
				emit(dst);
				emit(src);
				emit(typeByteSize(*copy.type));
				return dst;
			}
			case LsIrOp::LOCAL_REF: {
				emitOp(LS_OP_STORE_LOCAL_REF);
				emit(dst);
				emit(src);
				emit(typeByteSize(*copy.type));
				return dst;
			}
			case LsIrOp::VALUE: {
				emitOp(LS_OP_COPY);
				emit(dst);
				emit(src);
				u32 size = typeByteSize(*copy.type);
				emit(size);
				return dst;
			}
		}
		ASSERT(false);
		return 0xffFFffFF;
	}

	u32 emitExtractValue(const LsOpExtractValue& op) {
		const u32 value = emit(*op.value);
		const u32 ret = stack_top;
		emitOp(LS_OP_COPY);
		emit(ret);
		emit(value + op.offset);
		emit(op.size);
		stack_top += op.size;
		return ret;
	}

	u32 emitAggregateInit(const LsOpAggregateInit& aggregate) {
		const u32 result = stack_top;
		for (u32 i = 0; i < aggregate.value_count; ++i) emit(*aggregate.values[i]);
		return result;
	}

	static ls_type_kind toTypeKind(const ResolvedType& type) {
		switch (type.kind) {
			case ResolvedType::VOID: return LS_TYPE_VOID;
			case ResolvedType::BOOL: return LS_TYPE_BOOL;
			case ResolvedType::I8: return LS_TYPE_I8;
			case ResolvedType::I16: return LS_TYPE_I16;
			case ResolvedType::I32: return LS_TYPE_I32;
			case ResolvedType::I64: return LS_TYPE_I64;
			case ResolvedType::U8: return LS_TYPE_U8;
			case ResolvedType::U16: return LS_TYPE_U16;
			case ResolvedType::U32: return LS_TYPE_U32;
			case ResolvedType::U64: return LS_TYPE_U64;
			case ResolvedType::ISIZE: return LS_TYPE_I64;
			case ResolvedType::F32: return LS_TYPE_F32;
			case ResolvedType::F64: return LS_TYPE_F64;
			case ResolvedType::CSTR: return LS_TYPE_CPTR;
			case ResolvedType::CPTR: return LS_TYPE_CPTR;
			case ResolvedType::POINTER: return LS_TYPE_CPTR;
			case ResolvedType::BYTE: return LS_TYPE_U8;
			case ResolvedType::FUNCTION: return LS_TYPE_FUNCTION;
			case ResolvedType::ARRAY: return LS_TYPE_ARRAY;
			case ResolvedType::SLICE: return LS_TYPE_SLICE;
			case ResolvedType::NULLABLE: return LS_TYPE_NULL_VALUE;
			case ResolvedType::ENUM: return LS_TYPE_ENUM;
			case ResolvedType::STRUCT: return LS_TYPE_STRUCT;
			case ResolvedType::UNION: return LS_TYPE_TAGGED_UNION;
			default: return LS_TYPE_INVALID;
		}
	}

	u32 emitCast(const LsOpCast& cast) {
		u32 value_sp = emit(*cast.value);
		emitOp(LS_OP_CAST);
		emit(stack_top);
		emit(value_sp);
		emit((u8)toTypeKind(*cast.target_type));
		emit((u8)toTypeKind(*cast.type));

		u32 ret = stack_top;
		stack_top += typeByteSize(*cast.type);
		return ret;
	}

	u32 emitPushLocalAddr(const LsOpPushLocalAddr& op) {
		emitOp(LS_OP_LOAD_CONST_4);
		emit(stack_top);
		emit(op.alloca->stack_sp);
		stack_top += 4;
		return stack_top - 4;
	}

	u32 emitLocalLoad(const LsOpLocalLoad& op) {
		if (op.addr->kind == LS_IR_OP_ADD) {
			const auto& add = static_cast<const LsOpAdd&>(*op.addr);
			if (add.lhs->kind == LS_IR_OP_PUSH_LOCAL_ADDR && add.rhs->kind == LS_IR_OP_MUL) {
				const auto& base = static_cast<const LsOpPushLocalAddr&>(*add.lhs);
				const auto& mul = static_cast<const LsOpMul&>(*add.rhs);
				if (base.alloca->type && base.alloca->type->kind == ResolvedType::ARRAY) {
					const u32 index = emit(*mul.lhs);
					const u32 result = stack_top;
					emitOp(LS_OP_LOAD_INDEXED_LOCAL_I32);
					emit(result);
					emit(base.alloca->stack_sp);
					emit(index);
					emit(op.size);
					emit((i32)0);
					emit((u32)static_cast<ArrayResolvedType*>(base.alloca->type)->size);
					emit(op.size);
					stack_top += op.size;
					return result;
				}
			}
		}
		emit(*op.addr);
		u32 addr_sp = stack_top - 4; // TODO is it always 4 bytes?
		emitOp(LS_OP_LOAD_LOCAL_REF);
		u32 ret = stack_top;
		emit(stack_top);
		emit(addr_sp);
		emit(op.size);
		stack_top += op.size;
		return ret;
	}

	u32 emitPushGlobalAddr(const LsOpPushGlobalAddr& op) {
		ASSERT(op.symbol);
		emitOp(LS_OP_GLOBAL_REF);
		u32 ret = stack_top;
		emit(ret);
		emit(op.symbol->slot.offset);
		stack_top += 4;
		return ret;
	}
	
	u32 emitGlobalLoad(const LsOpGlobalLoad& op) {
		u32 addr_sp = emit(*op.addr);
		emitOp(LS_OP_LOAD_GLOBAL_REF);
		u32 ret = stack_top;
		emit(ret);
		emit(addr_sp);
		emit(op.size);
		stack_top += op.size;
		return ret;
	}

	u32 emitCallDirect(const LsOpCallDirect& call) {
		u32* args = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * call.arg_count, alignof(u32)));
		for (u32 i = 0; i < call.arg_count; ++i) args[i] = emit(*call.args[i]);

		const u32 arg_base = stack_top;
		u32 param_index = 0;
		for (u32 i = 0; i < call.arg_count; ++i) {
			while (call.function->params[param_index].is_comptime) ++param_index;
			const u32 size = typeByteSize(*call.function->params[param_index++].resolved_type);
			emitOp(LS_OP_COPY);
			emit(stack_top);
			emit(args[i]);
			emit(size);
			stack_top += size;
		}
		emitOp(call.function->is_extern ? LS_OP_CALL_NATIVE : LS_OP_CALL_DIRECT);
		emit(call.function->bytecode_index);
		emit(arg_base);
		stack_top = arg_base + call.return_size;
		return arg_base;
	}

	u32 emitCallIndirect(const LsOpCallIndirect& call) {
		const u32 callee = emit(*call.callee);
		const u32 arg_base = callee + 4u;
		u32* args = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * call.arg_count, alignof(u32)));
		u32 arg_size = 0;
		for (u32 i = 0; i < call.arg_count; ++i) {
			args[i] = emit(*call.args[i]);
			emitOp(LS_OP_COPY);
			emit(arg_base + arg_size);
			emit(args[i]);
			emit(call.arg_sizes[i]);
			arg_size += call.arg_sizes[i];
		}
		emitOp(LS_OP_CALL_INDIRECT);
		emit(callee);
		emit(arg_size);
		emit(call.return_size);
		stack_top = callee + call.return_size;
		return callee;
	}

	u32 emitNull(const LsOpNull& op) {
		emitOp(LS_OP_LOAD_CONST_1);
		emit(stack_top);
		u32 ret = stack_top;
		emit((u32)0);
		stack_top += op.size;
		return ret;
	}

	u32 emit(LsIrOp& op) {
		op.bytecode_offset = code.size();
		switch (op.kind) {
			case LS_IR_OP_NEG:
			case LS_IR_OP_NOT: return emitUnary(static_cast<LsOpUnary&>(op));
			case LS_IR_OP_AND:
			case LS_IR_OP_OR: return emitShortCircuit(static_cast<LsOpBinary&>(op));
			case LS_IR_OP_NULL: return emitNull(static_cast<LsOpNull&>(op));
			case LS_IR_OP_NOP: return 0xffFFffFF;
			case LS_IR_OP_JUMP: return emitJump(static_cast<LsOpJump&>(op));
			case LS_IR_OP_CONDITIONAL_JUMP: return emitConditionalJump(*static_cast<LsOpConditionalJump*>(&op));
			case LS_IR_OP_CALL_DIRECT: return emitCallDirect(*static_cast<LsOpCallDirect*>(&op));
			case LS_IR_OP_CALL_INDIRECT: return emitCallIndirect(*static_cast<LsOpCallIndirect*>(&op));
			case LS_IR_OP_GLOBAL_LOAD: return emitGlobalLoad(*static_cast<LsOpGlobalLoad*>(&op));
			case LS_IR_OP_EXTRACT_VALUE: return emitExtractValue(*static_cast<LsOpExtractValue*>(&op));
			case LS_IR_OP_LOCAL_LOAD: return emitLocalLoad(*static_cast<LsOpLocalLoad*>(&op));
			case LS_IR_OP_PUSH_LOCAL_ADDR: return emitPushLocalAddr(*static_cast<LsOpPushLocalAddr*>(&op));
			case LS_IR_OP_PUSH_GLOBAL_ADDR: return emitPushGlobalAddr(*static_cast<LsOpPushGlobalAddr*>(&op));
			case LS_IR_OP_EQ:
			case LS_IR_OP_NE:
			case LS_IR_OP_LT:
			case LS_IR_OP_LE:
			case LS_IR_OP_GT:
			case LS_IR_OP_GE: return emitCompare(static_cast<LsOpBinary&>(op));
			case LS_IR_OP_COPY: return emitCopy(*static_cast<LsOpCopy*>(&op));
			case LS_IR_OP_CAST: return emitCast(static_cast<LsOpCast&>(op));
			case LS_IR_OP_ALLOCA: return emitAlloca(*static_cast<LsOpAlloca*>(&op));
			case LS_IR_OP_MUL: return emitBinary(LS_OP_MUL_I8, static_cast<LsOpBinary&>(op));
			case LS_IR_OP_ADD: return emitBinary(LS_OP_ADD_I8, static_cast<LsOpBinary&>(op));
			case LS_IR_OP_SUB: return emitBinary(LS_OP_SUB_I8, static_cast<LsOpBinary&>(op));
			case LS_IR_OP_DIV: return emitBinary(LS_OP_DIV_I8, static_cast<LsOpBinary&>(op));
			case LS_IR_OP_MOD: return emitBinary(LS_OP_MOD_I8, static_cast<LsOpBinary&>(op));
			case LS_IR_OP_LOAD_CONST: return emitLoadConst(static_cast<LsOpLoadConst&>(op));
			case LS_IR_OP_AGGREGATE_INIT: return emitAggregateInit(static_cast<LsOpAggregateInit&>(op));
			case LS_IR_OP_RETURN: return emitReturn(static_cast<LsOpReturn&>(op));
			default: ASSERT(false); break;
		}
		return 0xffFFffFF;
	}

	u32 emitReturn(const LsOpReturn& ret) {
		if (!ret.expression) {
			emitOp(LS_OP_RETURN_BASE);
			return stack_top;
		}
		emit(*ret.expression);
		emitOp(LS_OP_RETURN); // TODO
		emit(stack_top - ret.size);
		emit(ret.size);
		return stack_top - ret.size;
	}

	void emit(LsIrBlockData& block) {
		for (LsIrOp* op : block.ops) {
			emit(*op);
		}
	}

	void beginFunction(ls_function_bc* fn, FunctionExpression& fn_expr) {
		ASSERT(fn);
		fn_bc = fn;
		stack_top = 0;

		ResolvedType* return_type = static_cast<FunctionResolvedType*>(fn_expr.resolved_type)->return_type;

		fn_bc->code = (u8*)(u64)code.size(); // store offset since code.data may be reallocated
		fn_bc->kind = fn_expr.is_extern ? LS_FUNCTION_NATIVE : LS_FUNCTION_SCRIPT;
		fn_bc->is_builtin_native = false; //is_builtin_native; // TODO
		fn_bc->param_size = 0;
		for (const FunctionParam& param : fn_expr.params) {
			if (!param.is_comptime) fn_bc->param_size += typeByteSize(*param.resolved_type);
		}
		stack_top = fn_bc->param_size;
		//fn_bc->return_kind = toTypeKind(*return_type); // TODO
		fn_bc->return_size = typeByteSize(*return_type);
		fn_bc->frame_size = 0;
		fn_bc->code_size = 0u;
		fn_bc->source_map = nullptr;
		fn_bc->source_map_count = 0u;
		fn_bc->locals = nullptr;
		fn_bc->local_count = 0u;			
	}

	void endFunction() {
		ASSERT(fn_bc);
		fn_bc->code_size = code.size() - (u64)fn_bc->code;
		fn_bc->frame_size = stack_top;
		fn_bc = nullptr;
	}

	void emit(const void* data, u32 size) {
		for (u32 i = 0; i < size; ++i) {
			code.push_back(((const u8*)data)[i]);
		}
	}

	ls_host& host;
	ls_function_bc* fn_bc = nullptr;
	ByteArray code;
	u32 stack_top = 0;
};

}

ls_bytecode* ls_bytecode_compile(ls_module* module, ls_host* host) {
	if (!module || !host) return nullptr;

	ls_bytecode* bc = (ls_bytecode*)host->arena.allocate(host->arena.user_data, sizeof(ls_bytecode), alignof(ls_bytecode));
	memset(bc, 0, sizeof(*bc));
	bc->host = host;
	bc->arena = &host->arena;

	IRBuilder builder(*host);

	bc->function_count = 0;
	bc->global_size = 0;
	bc->has_global_init = false;
	for (Unit& u : module->units) {
		for (Symbol& s : u.symbols) {
			if (!symbolHasGlobalStorage(s)) continue;
			s.slot.storage = StorageSlot::GLOBAL;
			s.slot.offset = bc->global_size;
			s.slot.byte_size = typeByteSize(*s.resolved_type);
			if (s.slot.byte_size == 0) s.slot.byte_size = 1;
			s.slot.type = s.resolved_type;
			s.slot.kind = BytecodeCompiler::toTypeKind(*s.resolved_type);
			bc->global_size += s.slot.byte_size;
		}
	}
	for (Unit& u : module->units) {
		for (Symbol& s : u.symbols) {
			if (!s.expression) continue; // alias
			if (s.expression->kind != Expression::FUNCTION) continue;
			
			auto& fn_expr = static_cast<FunctionExpression&>(*s.expression);
			fn_expr.bytecode_index = bc->function_count++;
		}
	}
	if (bc->global_size > 0) ++bc->function_count;

	if (bc->function_count > 0) {
		bc->function_capacity = bc->function_count; // TODO get rid of capacity?
		bc->functions = (ls_function_bc*)host->arena.allocate(host->arena.user_data, sizeof(ls_function_bc) * bc->function_capacity, alignof(ls_function_bc));
		memset(bc->functions, 0, sizeof(ls_function_bc) * bc->function_capacity);
	}

	BytecodeCompiler bc_compiler(*host);
	u32 fn_index = 0;
	for (Unit& u : module->units) {
		for (Symbol& s : u.symbols) {
			if (!s.expression) continue; // alias

			if (s.expression->kind == Expression::FUNCTION) {
				auto& fn_expr = static_cast<FunctionExpression&>(*s.expression);
				ls_function_bc& fn_bc = bc->functions[fn_index];
				fn_bc.name = s.name;

				bc_compiler.beginFunction(&fn_bc, fn_expr);
				if (fn_expr.body) {
					LsIrBlockData& body = builder.buildFunctionIR(fn_expr);
					bc_compiler.emit(body);
					bc_compiler.patchJumps(body);
				}
				bc_compiler.endFunction();
				++fn_index;
			}
		}
	}

	if (bc->global_size > 0) {
		ls_function_bc& fn = bc->functions[fn_index++];
		memset(&fn, 0, sizeof(fn));
		fn.kind = LS_FUNCTION_SCRIPT;
		fn.return_size = 0;
		fn.param_size = 0;
		fn.frame_size = 0;
		fn.code = (u8*)(u64)bc_compiler.code.size();
		fn.code_size = 0;
		fn.source_map = nullptr;
		fn.source_map_count = 0;
		fn.locals = nullptr;
		fn.local_count = 0;
		bc_compiler.fn_bc = &fn;
		bc_compiler.stack_top = 0;
		for (Unit& u : module->units) {
			for (Symbol& s : u.symbols) {
				if (!symbolHasGlobalStorage(s) || s.expression->kind == Expression::UNDEFINED) continue;
				LsIrOp& value = builder.buildExpressionIR(*s.expression, true);
				u32 src = bc_compiler.emit(value);
				bc_compiler.emitOp(LS_OP_GLOBAL_STORE);
				bc_compiler.emit(s.slot.offset);
				bc_compiler.emit(src);
				bc_compiler.emit(s.slot.byte_size);
			}
		}
		bc_compiler.emitOp(LS_OP_RETURN_BASE);
		bc_compiler.endFunction();
		bc->has_global_init = true;
	}

	for (u32 i = 0; i < bc->function_count; ++i) {
		bc->functions[i].code = (u8*)(u64)bc_compiler.code.data + (u64)bc->functions[i].code;
	}

	return bc;
}
