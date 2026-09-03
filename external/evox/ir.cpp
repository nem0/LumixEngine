#include "ir.h"
#include "bytecode.h"
#include "compiler.h"
#include <string.h>

namespace {

struct EmitDst {
	u32 dst;
	u32 size;
};

bool typesEqual(const ResolvedType* a, const ResolvedType* b) {
	if (a == b) return true;
	if (!a || !b || a->kind != b->kind) return false;
	if (a->kind < ResolvedTypeKind::META) return true;
	switch (a->kind) {
		case ResolvedTypeKind::FUNCTION: {
			const auto* fa = static_cast<const FunctionResolvedType*>(a);
			const auto* fb = static_cast<const FunctionResolvedType*>(b);
			if (fa->params.size() != fb->params.size()) return false;
			if (!typesEqual(fa->return_type, fb->return_type)) return false;
			for (i32 i = 0; i < fa->params.size(); ++i) {
				if (fa->params[i].is_comptime != fb->params[i].is_comptime) return false;
				if (!typesEqual(fa->params[i].type, fb->params[i].type)) return false;
			}
			return true;
		}
		case ResolvedTypeKind::ARRAY: {
			const auto* aa = static_cast<const ArrayResolvedType*>(a);
			const auto* ab = static_cast<const ArrayResolvedType*>(b);
			return aa->size == ab->size && typesEqual(aa->element_type, ab->element_type);
		}
		case ResolvedTypeKind::POINTER: {
			const auto* pa = static_cast<const PointerResolvedType*>(a);
			const auto* pb = static_cast<const PointerResolvedType*>(b);
			return pa->is_const == pb->is_const && typesEqual(pa->inner, pb->inner);
		}
		case ResolvedTypeKind::SLICE: {
			const auto* sa = static_cast<const SliceResolvedType*>(a);
			const auto* sb = static_cast<const SliceResolvedType*>(b);
			return sa->is_const == sb->is_const && typesEqual(sa->element_type, sb->element_type);
		}
		case ResolvedTypeKind::NULLABLE: {
			const auto* na = static_cast<const NullableResolvedType*>(a);
			const auto* nb = static_cast<const NullableResolvedType*>(b);
			return typesEqual(na->inner, nb->inner);
		}
		default: return false;
	}
}

i32 unionMemberIndex(const UnionResolvedType& un, const ResolvedType* type) {
	for (i32 i = 0; i < un.members.size(); ++i) {
		if (typesEqual(un.members[i], type)) return i;
	}
	return -1;
}

// Tagged wrappers are [tag][payload]. Flow typing can request the payload type
// while storage is still the wrapper.
u64 payloadOffset(const ResolvedType* storage, const ResolvedType* requested) {
	if (!storage || !requested) return 0;
	if (storage->kind == ResolvedTypeKind::UNION && requested->kind != ResolvedTypeKind::UNION) return sizeof(i32);
	if (storage->kind == ResolvedTypeKind::NULLABLE && requested->kind != ResolvedTypeKind::NULLABLE) return 1;
	return 0;
}

ExIrOpKind invertCompare(ExIrOpKind kind) {
	switch (kind) {
		case ExIrOpKind::EQ: return ExIrOpKind::NE;
		case ExIrOpKind::NE: return ExIrOpKind::EQ;
		case ExIrOpKind::LT: return ExIrOpKind::GE;
		case ExIrOpKind::LE: return ExIrOpKind::GT;
		case ExIrOpKind::GT: return ExIrOpKind::LE;
		case ExIrOpKind::GE: return ExIrOpKind::LT;
		default: return ExIrOpKind::INVALID;
	}
}

// Source locations for ExIrSourceLoc. IR ops store a u32 index into the
// module's SourceLocTable (token.h); the BytecodeCompiler records it as-is and
// ex_bytecode_compile copies the table verbatim into the bytecode.
// AST to IR
struct IRBuilder {
	IRBuilder(ex_host& host)
		: host(host)
		, strings(host.arena)
		, locals(host.arena)
		, defers(host.arena)
		, loops(host.arena) {}

	template <typename T, typename... Args> T& alloc(Args&&... args) {
		T* v = (T*)host.arena.allocate(host.arena.user_data, sizeof(T), alignof(T));
		new (NewPlaceholder(), v) T(static_cast<Args&&>(args)...);
		assignSrcLoc(*v);
		return *v;
	}

	ExOpAlloca& allocAlloca(ResolvedType* type, ex_string_view name, ExIrOp* value) {
		auto& alloca = alloc<ExOpAlloca>();
		alloca.type = type;
		alloca.name = name;
		alloca.value = value;
		alloca.stack_sp = stack_cursor;
		stack_cursor += typeByteSize(*type);
		return alloca;
	}

	void assignSrcLoc(ExIrOp& op) { op.src_loc = current_src_loc; }
	void assignSrcLoc(ExIrBlockData&) {}

	ExIrOp& buildImplicitConversionIR(Expression& expression, ResolvedType& target_type) {
		if (expression.kind == Expression::UNDEFINED) {
			// TODO should undefined do nothing?
			auto& storage = allocAlloca(&target_type, {}, &alloc<ExOpNop>());
			return storage;
		}
		if (target_type.kind == ResolvedTypeKind::ANY && expression.resolved_type) {
			ExIrOp* source = &buildExpressionIR(expression, false);
			if (source->result_mode == ExIrOp::VALUE) {
				auto& address = alloc<ExOpMaterializeAddr>(); address.value = source; source = &address;
			}
			auto& aggregate = alloc<ExOpAggregateInit>(); aggregate.type = &target_type; aggregate.value_count = 2;
			aggregate.values = static_cast<ExIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(ExIrOp*) * 2, alignof(ExIrOp*)));
			aggregate.offsets = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * 2, alignof(u32)));
			aggregate.sizes = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * 2, alignof(u32)));
			aggregate.values[0] = source; aggregate.offsets[0] = 0; aggregate.sizes[0] = 8;
			auto& type_value = alloc<ExOpLoadConst>(); static ResolvedType i32_type(ResolvedTypeKind::I32); type_value.type = &i32_type; const u32 type_id = (u32)expression.resolved_type->kind; memcpy(type_value.value, &type_id, sizeof(type_id));
			aggregate.values[1] = &type_value; aggregate.offsets[1] = 8; aggregate.sizes[1] = 4;
			return aggregate;
		}
		if (target_type.kind == ResolvedTypeKind::UNION && expression.resolved_type) {
			return convertValue(buildExpressionIR(expression, true), *expression.resolved_type, target_type);
		}
		if (target_type.kind == ResolvedTypeKind::NULLABLE) {
			ResolvedType* inner = static_cast<NullableResolvedType&>(target_type).inner;
			if (expression.resolved_type->kind == ResolvedTypeKind::NULLABLE || expression.kind == Expression::NULL_LITERAL) {
				return buildExpressionIR(expression, true);
			}
			auto& aggregate = alloc<ExOpAggregateInit>();
			aggregate.type = &target_type;
			const u32 payload_size = typeByteSize(*inner);
			aggregate.value_count = 2;
			aggregate.values = static_cast<ExIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(ExIrOp*) * 2, alignof(ExIrOp*)));
			aggregate.offsets = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * 2, alignof(u32)));
			aggregate.sizes = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * 2, alignof(u32)));
			auto& flag = alloc<ExOpLoadConst>();
			static ResolvedType u8_type(ResolvedTypeKind::U8);
			flag.type = &u8_type;
			const u8 non_null = 1;
			memcpy(flag.value, &non_null, sizeof(non_null));
			aggregate.values[0] = &flag;
			aggregate.values[1] = &buildImplicitConversionIR(expression, *inner);
			aggregate.offsets[0] = 0;
			aggregate.offsets[1] = 1;
			aggregate.sizes[0] = 1;
			aggregate.sizes[1] = payload_size;
			return aggregate;
		}
		if (target_type.kind != ResolvedTypeKind::SLICE || expression.resolved_type->kind != ResolvedTypeKind::ARRAY) {
			return buildExpressionIR(expression, true);
		}
		auto& array = static_cast<ArrayResolvedType&>(*expression.resolved_type);
		auto& slice = alloc<ExOpSlice>();
		ExIrOp* source = &buildExpressionIR(expression, false);
		if (source->result_mode == ExIrOp::VALUE) {
			auto& address = alloc<ExOpMaterializeAddr>();
			address.value = source;
			source = &address;
		}
		slice.source = source;
		slice.source_is_array = true;
		slice.source_length = array.size;
		slice.element_size = typeByteSize(*array.element_type);
		return slice;
	}

	ExIrOp& wrapUnionMember(ExIrOp& payload, ResolvedType& member_type, UnionResolvedType& dest) {
		const i32 member_index = unionMemberIndex(dest, &member_type);
		if (member_index < 0) return payload;
		auto& aggregate = alloc<ExOpAggregateInit>();
		aggregate.type = &dest;
		aggregate.value_count = 2;
		aggregate.values = static_cast<ExIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(ExIrOp*) * 2, alignof(ExIrOp*)));
		aggregate.offsets = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * 2, alignof(u32)));
		aggregate.sizes = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * 2, alignof(u32)));
		auto& tag = alloc<ExOpLoadConst>();
		static ResolvedType i32_type(ResolvedTypeKind::I32);
		tag.type = &i32_type;
		memcpy(tag.value, &member_index, sizeof(member_index));
		aggregate.values[0] = &tag;
		aggregate.values[1] = &payload;
		aggregate.offsets[0] = 0;
		aggregate.offsets[1] = sizeof(i32);
		aggregate.sizes[0] = sizeof(i32);
		aggregate.sizes[1] = typeByteSize(member_type);
		return aggregate;
	}

	ExIrOp& convertValue(ExIrOp& value, ResolvedType& source, ResolvedType& dest) {
		if (typesEqual(&source, &dest)) return value;
		if (dest.kind == ResolvedTypeKind::UNION) {
			auto& dest_union = static_cast<UnionResolvedType&>(dest);
			if (source.kind == ResolvedTypeKind::UNION) {
				auto& convert = alloc<ExOpUnionConvert>();
				convert.source_type = &source;
				convert.target_type = &dest;
				convert.value = &value;
				return convert;
			}
			return wrapUnionMember(value, source, dest_union);
		}
		if (source.kind == ResolvedTypeKind::UNION) {
			auto& payload = alloc<ExOpExtractValue>();
			payload.value = &value;
			payload.offset = sizeof(i32);
			payload.size = typeByteSize(dest);
			return payload;
		}
		return value;
	}

	ExIrOp& widenToI64(ExIrOp& value, ResolvedType& source_type) {
		switch (source_type.kind) {
			case ResolvedTypeKind::I64:
			case ResolvedTypeKind::U64:
			case ResolvedTypeKind::ISIZE:
			case ResolvedTypeKind::UNTYPED_INT:
				return value;
			default: break;
		}
		auto& cast = alloc<ExOpCast>();
		static ResolvedType i64_type(ResolvedTypeKind::I64);
		cast.type = &i64_type;
		cast.target_type = &source_type;
		cast.value = &value;
		return cast;
	}

	ExIrOp& loadAllocaValue(ExOpAlloca& storage) {
		auto& addr = alloc<ExOpPushLocalAddr>();
		addr.alloca = &storage;
		auto& load = alloc<ExOpLoad>();
		load.addr = &addr;
		load.size = typeByteSize(*storage.type);
		return load;
	}

	ExIrOp& offsetAddress(ExIrOp& address, u64 bytes) {
		if (bytes == 0) return address;
		auto& add = alloc<ExOpAdd>();
		auto& offset = alloc<ExOpLoadConst>();
		static ResolvedType pointer_type(ResolvedTypeKind::U64);
		memcpy(offset.value, &bytes, sizeof(bytes));
		offset.type = &pointer_type;
		add.lhs = &address;
		add.rhs = &offset;
		add.operand_type = &pointer_type;
		add.result_mode = ExIrOp::ADDRESS;
		return add;
	}

	void buildReturnIR(ExIrBlockData& block, ExIrOp* value) {
		auto& ir_ret = alloc<ExOpReturn>();
		if (value && return_type && return_type->kind != ResolvedTypeKind::VOID) {
			ir_ret.size = typeByteSize(*return_type);
			if (defers.empty()) {
				ir_ret.expression = value;
			} else {
				auto& stored = allocAlloca(return_type, {}, value);
				block.ops.push(&stored);
				auto& addr = alloc<ExOpPushLocalAddr>();
				addr.alloca = &stored;
				auto& load = alloc<ExOpLoad>();
				load.addr = &addr;
				load.size = ir_ret.size;
				ir_ret.expression = &load;
			}
		}
		emitDefers(block, 0);
		block.ops.push(&ir_ret);
	}

	ResolvedType* identifierStorageType(const IdentifierExpression& ie) {
		for (i32 i = locals.size() - 1; i >= 0; --i) {
			if (equalStrings(locals[i].name, ie.name)) return locals[i].alloca->type;
		}
		StorageSlot* slot = ie.symbol ? &ie.symbol->slot : ie.slot;
		if (slot && slot->type) return slot->type;
		return ie.resolved_type;
	}

	u32 internString(ex_string_view value) {
		for (i32 i = 0, c = strings.size(); i < c; ++i) {
			if (equalStrings(value, strings[i])) return i;
		}
		i64 len = value.length;
		char* tmp = (char*)host.arena.allocate(host.arena.user_data, len + 1, 1);
		memcpy(tmp, value.begin, len);
		tmp[len] = 0;
		strings.push({tmp, len});
		return strings.size() - 1;
	}

	bool hasInliningBlocker(const Expression& expression, u32 depth = 0) {
		if (depth > 8) return true;
		switch (expression.kind) {
			case Expression::ADDRESSOF:
				return true;
			case Expression::PANIC:
				return true;
			case Expression::CALL: {
				const auto& call = static_cast<const CallExpression&>(expression);
				FunctionExpression* function = call.resolved_fn;
				if (!function && call.callee->kind == Expression::IDENTIFIER) {
					const auto& callee = static_cast<const IdentifierExpression&>(*call.callee);
					function = callee.resolved_fn;
					if (!function && callee.symbol && callee.symbol->expression && callee.symbol->expression->kind == Expression::FUNCTION) {
						function = static_cast<FunctionExpression*>(callee.symbol->expression);
					}
				}
				if (!function && call.callee->kind == Expression::MEMBER) {
					function = static_cast<const MemberExpression&>(*call.callee).resolved_fn;
				}
				if (!function) return true;
				if (!function->is_extern) {
					if (!function->body || function->body->kind != Statement::BLOCK) return true;
					const auto& body = static_cast<const BlockStatement&>(*function->body);
					if (body.statements.size() != 1 || body.statements[0]->kind != Statement::RETURN) return true;
					const auto& ret = static_cast<const ReturnStatement&>(*body.statements[0]);
					if (!ret.expression || hasInliningBlocker(*ret.expression, depth + 1)) return true;
				}
				for (Expression* arg : call.args) if (hasInliningBlocker(*arg, depth + 1)) return true;
				return false;
			}
			case Expression::UNARY: {
				const auto& unary = static_cast<const UnaryExpression&>(expression);
				return (unary.operator_fn && !unary.operator_fn->is_extern) || hasInliningBlocker(*unary.expression, depth + 1);
			}
			case Expression::BINARY: {
				const auto& binary = static_cast<const BinaryExpression&>(expression);
				return (binary.operator_fn && !binary.operator_fn->is_extern) || hasInliningBlocker(*binary.lhs, depth + 1) || hasInliningBlocker(*binary.rhs, depth + 1);
			}
			case Expression::CAST:
				return hasInliningBlocker(*static_cast<const CastExpression&>(expression).expression, depth + 1);
			case Expression::MEMBER:
				return hasInliningBlocker(*static_cast<const MemberExpression&>(expression).expression, depth + 1);
			case Expression::BRACKET: {
				const auto& bracket = static_cast<const BracketExpression&>(expression);
				if (hasInliningBlocker(*bracket.base, depth + 1)) return true;
				for (Expression* arg : bracket.args) if (hasInliningBlocker(*arg, depth + 1)) return true;
				return false;
			}
			case Expression::SLICE: {
				const auto& slice = static_cast<const SliceExpression&>(expression);
				return hasInliningBlocker(*slice.base, depth + 1) ||
					(slice.begin && hasInliningBlocker(*slice.begin, depth + 1)) ||
					(slice.end && hasInliningBlocker(*slice.end, depth + 1));
			}
			case Expression::STRUCT_LITERAL: {
				const auto& literal = static_cast<const StructLiteralExpression&>(expression);
				for (Expression* value : literal.values) if (hasInliningBlocker(*value, depth + 1)) return true;
				return false;
			}
			case Expression::ARRAY_LITERAL: {
				const auto& literal = static_cast<const ArrayLiteralExpression&>(expression);
				for (Expression* value : literal.values) if (hasInliningBlocker(*value, depth + 1)) return true;
				return false;
			}
			case Expression::TERNARY: {
				const auto& ternary = static_cast<const TernaryExpression&>(expression);
				return hasInliningBlocker(*ternary.condition, depth + 1) ||
					hasInliningBlocker(*ternary.true_expr, depth + 1) || hasInliningBlocker(*ternary.false_expr, depth + 1);
			}
			case Expression::DEREFERENCE:
				return hasInliningBlocker(*static_cast<const DereferenceExpression&>(expression).subject, depth + 1);
			default:
				return false;
		}
	}

	ExIrOp* buildInlineCall(FunctionExpression& function, CallExpression& call) {
		if (function.is_extern || function.is_template) return nullptr;
		const auto& body = static_cast<const BlockStatement&>(*function.body);
		if (body.statements.size() != 1 || body.statements[0]->kind != Statement::RETURN) return nullptr;
		const auto& ret = static_cast<const ReturnStatement&>(*body.statements[0]);
		if (!ret.expression || hasInliningBlocker(*ret.expression)) return nullptr;

		u32 runtime_param_count = 0;
		for (const FunctionParam& param : function.params) {
			if (!param.is_comptime) ++runtime_param_count;
		}
		if (runtime_param_count != call.args.size()) return nullptr;
		for (Expression* arg : call.args) {
			if (hasInliningBlocker(*arg)) return nullptr;
		}

		ExIrOp** args = static_cast<ExIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(ExIrOp*) * call.args.size(), alignof(ExIrOp*)));
		for (i32 i = 0, param_index = 0; i < call.args.size(); ++i) {
			while (function.params[param_index].is_comptime) ++param_index;
			args[i] = &buildImplicitConversionIR(*call.args[i], *function.params[param_index].resolved_type);
			++param_index;
		}

		const i32 local_watermark = locals.size();
		u32 arg_index = 0;
		for (const FunctionParam& param : function.params) {
			if (param.is_comptime) continue;
			locals.push({param.name.value, nullptr, args[arg_index++]});
		}
		ExIrOp& result = buildImplicitConversionIR(*ret.expression, *static_cast<FunctionResolvedType*>(function.resolved_type)->return_type);
		locals.resize(local_watermark);
		return &result;
	}

	ExIrOp& buildExpressionIR(Expression& expr, bool as_rvalue) {
		SourceScope scope(*this, expr.token);
		if (as_rvalue && expr.comptime_value.kind == ComptimeValue::VALUE && expr.resolved_type) {
			const u32 size = typeByteSize(*expr.resolved_type);
			auto& constant = alloc<ExOpLoadBytes>();
			constant.type = expr.resolved_type;
			constant.value = expr.comptime_value.value;
			constant.size = size;
			return constant;
		}

		// A runtime type is represented by the index of its descriptor in the
		// bytecode type table.  Keep the semantic type (META) on the expression,
		// but materialize the descriptor index as a u32 at the IR boundary.
		if (as_rvalue && expr.resolved_type && expr.resolved_type->kind == ResolvedTypeKind::META
			&& static_cast<MetaType*>(expr.resolved_type)->runtime) {
			ResolvedType* represented = static_cast<MetaType*>(expr.resolved_type)->inner;
			if (!represented && expr.comptime_value.kind == ComptimeValue::TYPE) represented = expr.comptime_value.type;
			if (!represented && expr.kind == Expression::IDENTIFIER) {
				IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
				if (id.symbol && id.symbol->comptime_value.kind == ComptimeValue::TYPE) represented = id.symbol->comptime_value.type;
			}
			if (represented) {
				auto& constant = alloc<ExOpLoadConst>();
				constant.type = expr.resolved_type;
				constant.represented_type = represented;
				return constant;
			}
		}

		switch (expr.kind) {
			case Expression::UNDEFINED: return alloc<ExOpNop>();
			case Expression::ADDRESSOF: {
				auto& address = static_cast<AddressOfExpression&>(expr);
				return buildExpressionIR(*address.subject, false);
			}
			case Expression::DEREFERENCE: {
				auto& dereference = static_cast<DereferenceExpression&>(expr);
				ExIrOp& pointer = buildExpressionIR(*dereference.subject, true);
				pointer.result_mode = ExIrOp::ADDRESS;
				ResolvedType* storage = dereference.subject->kind == Expression::IDENTIFIER
					? identifierStorageType(static_cast<IdentifierExpression&>(*dereference.subject))
					: dereference.subject->resolved_type;
				ResolvedType* pointee = storage && storage->kind == ResolvedTypeKind::POINTER
					? static_cast<PointerResolvedType*>(storage)->inner : nullptr;
				ExIrOp& address = offsetAddress(pointer, payloadOffset(pointee, expr.resolved_type));
				if (!as_rvalue) return address;
				auto& load = alloc<ExOpLoad>();
				load.addr = &address;
				load.size = typeByteSize(*expr.resolved_type);
				return load;
			}
			case Expression::SLICE: {
				auto& slice = static_cast<SliceExpression&>(expr);
				auto& op = alloc<ExOpSlice>();
				op.source_is_array = slice.base->resolved_type->kind == ResolvedTypeKind::ARRAY;
				op.source_is_scalar = slice.base->resolved_type->kind != ResolvedTypeKind::SLICE && !op.source_is_array;
				op.source = &buildExpressionIR(*slice.base, !op.source_is_array && !op.source_is_scalar);
				ResolvedType* element_type = nullptr;
				if (op.source_is_array) {
					auto& array = static_cast<ArrayResolvedType&>(*slice.base->resolved_type);
					element_type = array.element_type;
					op.source_length = array.size;
				} else if (op.source_is_scalar) {
					element_type = slice.base->resolved_type;
					op.source_length = 1;
				} else {
					element_type = static_cast<SliceResolvedType*>(slice.base->resolved_type)->element_type;
				}
				op.element_size = typeByteSize(*element_type);
				if (slice.begin) op.begin = &widenToI64(buildExpressionIR(*slice.begin, true), *slice.begin->resolved_type);
				if (slice.end) op.end = &widenToI64(buildExpressionIR(*slice.end, true), *slice.end->resolved_type);
				return op;
			}
			case Expression::BRACKET: {
				auto& be = static_cast<BracketExpression&>(expr);
				// struct["field"]
				if (be.struct_field_name.begin) {
					auto& struct_type = static_cast<StructResolvedType&>(*be.base->resolved_type);
					u32 offset = 0;
					for (i32 i = 0; i < struct_type.decl->fields.size(); ++i) {
						if (!equalStrings(struct_type.decl->fields[i].name.value, be.struct_field_name)) continue;
						offset = structFieldOffset(struct_type, i);
						if (as_rvalue) {
							auto& extract = alloc<ExOpExtractValue>();
							extract.value = &buildExpressionIR(*be.base, true);
							extract.offset = offset;
							extract.size = typeByteSize(*be.resolved_type);
							return extract;
						}
						auto& add = alloc<ExOpAdd>();
						auto& base = buildExpressionIR(*be.base, false);
						add.lhs = &base;
						auto& rhs = alloc<ExOpLoadConst>();
						static ResolvedType offset_type(ResolvedTypeKind::U64);
						rhs.type = &offset_type;
						const u64 field_offset = offset;
						memcpy(rhs.value, &field_offset, sizeof(field_offset));
						add.rhs = &rhs;
						add.operand_type = &offset_type;
						add.result_mode = ExIrOp::ADDRESS;
						return add;
					}
					ASSERT(false);
					return alloc<ExOpNop>();
				}
				
				// slice[index]
				if (be.base->resolved_type->kind == ResolvedTypeKind::SLICE) {
					ASSERT(be.args.size() == 1);
					ExIrOp& index = buildExpressionIR(*be.args[0], true);
					if (as_rvalue) {
						auto& load = alloc<ExOpSliceLoad>();
						load.slice = &buildExpressionIR(*be.base, true);
						load.index = &index;
						load.index_type = be.args[0]->resolved_type;
						load.element_size = typeByteSize(*be.resolved_type);
						return load;
					}
					auto& ref = alloc<ExOpSliceRef>();
					ref.slice = &buildExpressionIR(*be.base, true);
					ref.index = &index;
					ref.index_type = be.args[0]->resolved_type;
					ref.element_size = typeByteSize(*be.resolved_type);
					return ref;
				}

				// array[index]
				ASSERT(be.base->resolved_type->kind == ResolvedTypeKind::ARRAY);
				ASSERT(be.args.size() == 1);
				ExIrOp& base = buildExpressionIR(*be.base, false);
				// A temporary array (for example, a function call returning an array)
				// is a value, not an address. Materialize its frame address before
				// applying the element offset.
				ExIrOp* base_address = &base;
				if (base.result_mode == ExIrOp::VALUE) {
					auto& address = alloc<ExOpMaterializeAddr>();
					address.value = &base;
					base_address = &address;
				}
				ExIrOp& index = buildExpressionIR(*be.args[0], true);
				auto& bounds_check = alloc<ExOpBoundsCheck>();
				bounds_check.index_type = be.args[0]->resolved_type;
				bounds_check.index = &index;
				bounds_check.length = (u64) static_cast<ArrayResolvedType*>(be.base->resolved_type)->size;
				ExIrOp* checked_index = &bounds_check;
				static ResolvedType R(ResolvedTypeKind::U64);
				if (be.args[0]->resolved_type->kind != ResolvedTypeKind::U64) {
					auto& cast = alloc<ExOpCast>();
					cast.type = &R;
					cast.target_type = be.args[0]->resolved_type;
					cast.value = checked_index;
					checked_index = &cast;
				}
				auto& add = alloc<ExOpAdd>();
				auto& mul = alloc<ExOpMul>();
				auto& size = alloc<ExOpLoadConst>();
				size.type = &R;
				mul.operand_type = &R;
				add.operand_type = &R;

				u32 elem_size = typeByteSize(*be.resolved_type);
				memcpy(size.value, &elem_size, sizeof(elem_size));

				mul.lhs = checked_index;
				mul.rhs = &size;
				add.lhs = base_address;
				add.rhs = &mul;
				add.result_mode = ExIrOp::ADDRESS;
				if (!as_rvalue) return add;

				auto& load = alloc<ExOpLoad>();
				load.addr = &add;
				load.size = elem_size;
				return load;
			}
			case Expression::MEMBER: {
				auto& me = static_cast<MemberExpression&>(expr);
				if (me.resolved_fn) {
					ASSERT(as_rvalue);
					auto& value = alloc<ExOpLoadConst>();
					value.type = expr.resolved_type;
					memcpy(value.value, &me.resolved_fn->bytecode_index, sizeof(me.resolved_fn->bytecode_index));
					return value;
				}

				// namespace.comptime
				if (me.resolved_symbol && me.resolved_symbol->kind == Symbol::COMPTIME) {
					ASSERT(as_rvalue);
					auto& value = alloc<ExOpLoadBytes>();
					value.type = expr.resolved_type;
					value.value = me.resolved_symbol->comptime_value.value;
					value.size = typeByteSize(*expr.resolved_type);
					return value;
				}

				// namespace.global
				if (me.resolved_symbol && me.resolved_symbol->slot.storage == StorageSlot::GLOBAL) {
					auto& address = alloc<ExOpPushGlobalAddr>();
					address.offset = me.resolved_symbol->slot.offset;
					if (!as_rvalue) return address;
					auto& load = alloc<ExOpLoad>();
					load.addr = &address;
					load.size = typeByteSize(*expr.resolved_type);
					return load;
				}

				// slice.length
				if (me.expression && me.expression->resolved_type && me.expression->resolved_type->kind == ResolvedTypeKind::SLICE) {
					ASSERT(equalStrings(me.name.value, makeStringView("length")));
					auto& length = alloc<ExOpExtractValue>();
					length.value = &buildExpressionIR(*me.expression, true);
					length.offset = sizeof(void*);
					length.size = sizeof(i64);
					return length;
				}

				// *.enum_value
				EnumResolvedType* enum_type = nullptr;
				if (!me.expression) {
					enum_type = static_cast<EnumResolvedType*>(expr.resolved_type);
				}
				else if (me.expression && me.expression->resolved_type && me.expression->resolved_type->kind == ResolvedTypeKind::META) {
					ResolvedType* type = static_cast<MetaType*>(me.expression->resolved_type)->inner;
					if (type->kind == ResolvedTypeKind::ENUM) enum_type = static_cast<EnumResolvedType*>(type);
				}
				if (enum_type) {
					ASSERT(me.enum_member_index >= 0);
					auto& op = alloc<ExOpLoadConst>();
					op.type = expr.resolved_type;
					const u32 value = (u32)me.enum_member_value;
					memcpy(op.value, &value, sizeof(value));
					return op;
				}

				// struct.field
				ResolvedType* base_type = me.expression->resolved_type;
				const bool pointer_base = base_type->kind == ResolvedTypeKind::POINTER;
				if (pointer_base) base_type = static_cast<PointerResolvedType*>(base_type)->inner;
				ASSERT(base_type->kind == ResolvedTypeKind::STRUCT);
				auto* struct_type = static_cast<StructResolvedType*>(base_type);
				auto& fields = struct_type->decl->fields;
				u32 offset = structFieldOffset(*struct_type, me.struct_field_index);

				auto& base = buildExpressionIR(*me.expression, pointer_base);

				if (!pointer_base && as_rvalue && base.result_mode == ExIrOp::VALUE) {
					auto& extract = alloc<ExOpExtractValue>();
					extract.value = &base;
					extract.offset = offset;
					extract.size = typeByteSize(*me.resolved_type);
					return extract;
				}

				auto& add = alloc<ExOpAdd>();
				add.lhs = &base;
				auto& rhs = alloc<ExOpLoadConst>();
				static ResolvedType R(ResolvedTypeKind::U64);
				const u64 pointer_offset = offset;
				memcpy(&rhs.value, &pointer_offset, sizeof(pointer_offset));
				rhs.type = &R;
				add.rhs = &rhs;
				add.operand_type = &R;
				add.result_mode = ExIrOp::ADDRESS;
				if (!as_rvalue) return add;

				auto& load = alloc<ExOpLoad>();
				load.addr = &add;
				load.size = typeByteSize(*me.resolved_type);
				return load;
			}
			case Expression::IDENTIFIER: {
				auto& ie = static_cast<IdentifierExpression&>(expr);
				FunctionExpression* value_function = ie.resolved_fn;
				if (!value_function && ie.symbol && ie.symbol->expression && ie.symbol->expression->kind == Expression::FUNCTION) {
					value_function = static_cast<FunctionExpression*>(ie.symbol->expression);
				}
				if (as_rvalue && value_function) {
					auto& op = alloc<ExOpLoadConst>();
					op.type = ie.resolved_type;
					memcpy(op.value, &value_function->bytecode_index, sizeof(value_function->bytecode_index));
					return op;
				}
				
				for (i32 i = locals.size() - 1; i >= 0; --i) {
					if (!equalStrings(locals[i].name, ie.name)) continue;
					if (as_rvalue && locals[i].inline_value) return *locals[i].inline_value;
					if (!locals[i].alloca) continue;

					// A narrowed any binding reads through its erased payload pointer.
					if (locals[i].alloca->type->kind == ResolvedTypeKind::ANY && ie.resolved_type->kind != ResolvedTypeKind::ANY) {
						auto& any_addr = alloc<ExOpPushLocalAddr>(); any_addr.alloca = locals[i].alloca;
						auto& ptr = alloc<ExOpLoad>(); ptr.addr = &any_addr; ptr.size = 8;
						if (!as_rvalue) { ptr.result_mode = ExIrOp::ADDRESS; return ptr; }
						auto& payload = alloc<ExOpLoad>(); payload.addr = &ptr; payload.size = typeByteSize(*ie.resolved_type); return payload;
					}

					auto& addr = alloc<ExOpPushLocalAddr>();
					addr.alloca = locals[i].alloca;
					ExIrOp& value_addr = offsetAddress(addr, payloadOffset(locals[i].alloca->type, ie.resolved_type));
					if (as_rvalue) {
						auto& load = alloc<ExOpLoad>();
						load.addr = &value_addr;
						load.size = typeByteSize(*ie.resolved_type);
						return load;
					}
					return value_addr;
				}

				if (ie.symbol && ie.symbol->kind == Symbol::COMPTIME && ie.symbol->comptime_bytes) {
					auto& value = alloc<ExOpLoadBytes>();
					value.type = ie.symbol->resolved_type;
					value.value = ie.symbol->comptime_bytes;
					value.size = ie.symbol->comptime_byte_size;
					if (!as_rvalue) {
						auto& address = alloc<ExOpMaterializeAddr>();
						address.value = &value;
						return address;
					}
					
					if (value.type == ie.resolved_type) return value;
					
					auto& cast = alloc<ExOpCast>();
					cast.type = ie.resolved_type;
					cast.target_type = value.type;
					cast.value = &value;
					return cast;
				}

				StorageSlot* global_slot = ie.symbol ? &ie.symbol->slot : ie.slot;
				if (global_slot && global_slot->storage == StorageSlot::GLOBAL) {
					auto& op = alloc<ExOpPushGlobalAddr>();
					op.offset = global_slot->offset;
					ExIrOp& addr = offsetAddress(op, payloadOffset(global_slot->type, ie.resolved_type));
					if (!as_rvalue) return addr;
					auto& load = alloc<ExOpLoad>();
					load.addr = &addr;
					load.size = typeByteSize(*ie.resolved_type);
					return load;
				}

				break;
			}
			case Expression::PANIC: {
				auto& panic = static_cast<PanicExpression&>(expr);
				auto& op = alloc<ExOpPanic>();
				op.message = &buildImplicitConversionIR(*panic.message, *panic.message->resolved_type);
				return op;
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
				if (!function && call.callee->kind == Expression::MEMBER) {
					function = static_cast<MemberExpression&>(*call.callee).resolved_fn;
				}
				if (function && function->bytecode_index != ~0u) {
					if (do_inline && call.callee->kind != Expression::MEMBER) {
						if (ExIrOp* inlined = buildInlineCall(*function, call)) return *inlined;
					}
					MemberExpression* member_callee = call.callee->kind == Expression::MEMBER ? static_cast<MemberExpression*>(call.callee) : nullptr;
					ResolvedType* receiver_type = member_callee && member_callee->expression ? member_callee->expression->resolved_type : nullptr;
					const bool is_ufcs = receiver_type && (receiver_type->kind == ResolvedTypeKind::STRUCT || receiver_type->kind == ResolvedTypeKind::ENUM || receiver_type->kind == ResolvedTypeKind::POINTER);
					auto& op = alloc<ExOpCallDirect>();
					op.function = function;
					op.arg_count = (is_ufcs ? 1u : 0u);
					for (i32 i = 0, param_index = is_ufcs ? 1u : 0u; i < call.args.size(); ++i, ++param_index) {
						if (!function->params[param_index].is_comptime) ++op.arg_count;
					}
					op.return_size = typeByteSize(*call.resolved_type);
					op.args = static_cast<ExIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(ExIrOp*) * op.arg_count, alignof(ExIrOp*)));
					u32 param_index = 0;
					u32 arg_index = 0;
					if (is_ufcs) {
						while (function->params[param_index].is_comptime) ++param_index;
						ResolvedType& target_type = *function->params[param_index++].resolved_type;
						op.args[arg_index++] = &buildImplicitConversionIR(*member_callee->expression, target_type);
					}
					for (i32 i = 0; i < call.args.size(); ++i) {
						FunctionParam& param = function->params[param_index++];
						if (param.is_comptime) continue;
						ResolvedType& target_type = *param.resolved_type;
						op.args[arg_index++] = &buildImplicitConversionIR(*call.args[i], target_type);
					}
					return op;
				}

				ASSERT(call.callee->resolved_type && call.callee->resolved_type->kind == ResolvedTypeKind::FUNCTION);
				auto& fn_type = static_cast<FunctionResolvedType&>(*call.callee->resolved_type);
				auto& op = alloc<ExOpCallIndirect>();
				op.callee = &buildExpressionIR(*call.callee, true);
				op.arg_count = call.args.size();
				op.return_size = typeByteSize(*call.resolved_type);
				op.args = static_cast<ExIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(ExIrOp*) * op.arg_count, alignof(ExIrOp*)));
				op.arg_sizes = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * op.arg_count, alignof(u32)));
				for (u32 i = 0; i < op.arg_count; ++i) {
					ResolvedType& target_type = *fn_type.params[i].type;
					op.args[i] = &buildImplicitConversionIR(*call.args[i], target_type);
					op.arg_sizes[i] = typeByteSize(target_type);
				}
				return op;
			}
			case Expression::ARRAY_LITERAL: {
				auto& ale = static_cast<ArrayLiteralExpression&>(expr);
				auto& op = alloc<ExOpAggregateInit>();
				op.type = ale.resolved_type;
				op.value_count = ale.values.size();
				op.values = static_cast<ExIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(ExIrOp*) * op.value_count, alignof(ExIrOp*)));
				for (u32 i = 0; i < op.value_count; ++i) op.values[i] = &buildExpressionIR(*ale.values[i], true);
				return op;
			}
			case Expression::STRUCT_LITERAL: {
				auto& sle = static_cast<StructLiteralExpression&>(expr);
				auto& op = alloc<ExOpAggregateInit>();
				op.type = sle.resolved_type;
				op.value_count = sle.values.size();
				op.values = static_cast<ExIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(ExIrOp*) * op.value_count, alignof(ExIrOp*)));
				if (op.type->kind == ResolvedTypeKind::STRUCT) {
					StructResolvedType* st = static_cast<StructResolvedType*>(op.type);
					op.offsets = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * op.value_count, alignof(u32)));
					op.sizes = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * op.value_count, alignof(u32)));
					for (u32 i = 0; i < op.value_count; ++i) {
						ResolvedType* field_type = st->fields[i].type;
						op.offsets[i] = structFieldOffset(*st, (i32)i);
						op.sizes[i] = typeByteSize(*field_type);
					}
				}
				for (u32 i = 0; i < op.value_count; ++i) op.values[i] = &buildExpressionIR(*sle.values[i], true);
				return op;
			}
			case Expression::CAST: {
				auto& ce = static_cast<CastExpression&>(expr);
				ExIrOp& inner = buildExpressionIR(*ce.expression, true);
				auto& op = alloc<ExOpCast>();
				op.type = ce.resolved_type;
				op.target_type = ce.expression->resolved_type;
				op.value = &inner;
				return op;
			}
			case Expression::NULL_LITERAL: {
				auto& op = alloc<ExOpNull>();
				op.size = typeByteSize(*expr.resolved_type);
				return op;
			}
			case Expression::SIZEOF: {
				auto& sz = static_cast<SizeofExpression&>(expr);
				auto& op = alloc<ExOpLoadConst>();
				static ResolvedType default_type(ResolvedTypeKind::I32);
				op.type = expr.resolved_type->kind == ResolvedTypeKind::UNTYPED_INT ? &default_type : expr.resolved_type;
				memcpy(op.value, &sz.value, sizeof(sz.value));
				return op;
			}
			case Expression::BOOL_LITERAL: {
				auto& ble = static_cast<BoolLiteralExpression&>(expr);
				auto& op = alloc<ExOpLoadConst>();
				op.type = ble.resolved_type;
				u8 value = ble.value ? 1 : 0;
				memcpy(&op.value, &value, sizeof(value));
				return op;
			}
			case Expression::STRING_LITERAL: {
				auto& literal = static_cast<StringLiteralExpression&>(expr);
				auto& ir_literal = alloc<ExOpStringLiteral>();
				ir_literal.index = internString(literal.value);
				ir_literal.length = u32(literal.value.length);
				return ir_literal;
			}
			case Expression::TYPE_MEMBER: {
				auto& member = static_cast<TypeMemberExpression&>(expr);
				switch (member.kind) { case TypeMemberExpression::NAME: {
						auto& pointer = alloc<ExOpLoadConst>();
						static ResolvedType pointer_type(ResolvedTypeKind::CPTR);
						pointer.type = &pointer_type;
						memcpy(pointer.value, &member.comptime_string.begin, sizeof(member.comptime_string.begin));
						auto& length = alloc<ExOpLoadConst>();
						static ResolvedType length_type(ResolvedTypeKind::I64);
						length.type = &length_type;
						const i64 value_length = member.comptime_string.length;
						memcpy(length.value, &value_length, sizeof(value_length));
						auto& slice = alloc<ExOpAggregateInit>();
						slice.type = expr.resolved_type;
						slice.value_count = 2;
						slice.values = static_cast<ExIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(ExIrOp*) * slice.value_count, alignof(ExIrOp*)));
						slice.values[0] = &pointer;
						slice.values[1] = &length;
						return slice;
					}
					case TypeMemberExpression::MIN:
					case TypeMemberExpression::MAX: {
						ASSERT(member.reflected_type && expr.resolved_type);
						const bool is_min = member.kind == TypeMemberExpression::MIN;
						auto& value = alloc<ExOpLoadConst>();
						value.type = expr.resolved_type;
						switch (member.reflected_type->kind) {
							case ResolvedTypeKind::I8: {
								const i8 v = is_min ? -128 : 127;
								memcpy(value.value, &v, sizeof(v));
								break;
							}
							case ResolvedTypeKind::I16: {
								const i16 v = is_min ? -32768 : 32767;
								memcpy(value.value, &v, sizeof(v));
								break;
							}
							case ResolvedTypeKind::I32: {
								const i32 v = is_min ? (i32)-2147483647 - 1 : (i32)2147483647;
								memcpy(value.value, &v, sizeof(v));
								break;
							}
							case ResolvedTypeKind::I64:
							case ResolvedTypeKind::ISIZE: {
								const i64 v = is_min ? (i64)-9223372036854775807LL - 1 : (i64)9223372036854775807LL;
								memcpy(value.value, &v, sizeof(v));
								break;
							}
							case ResolvedTypeKind::U8:
							case ResolvedTypeKind::BYTE: {
								const u8 v = is_min ? 0 : 255;
								memcpy(value.value, &v, sizeof(v));
								break;
							}
							case ResolvedTypeKind::U16: {
								const u16 v = is_min ? 0 : 65535;
								memcpy(value.value, &v, sizeof(v));
								break;
							}
							case ResolvedTypeKind::U32: {
								const u32 v = is_min ? 0 : (u32)4294967295u;
								memcpy(value.value, &v, sizeof(v));
								break;
							}
							case ResolvedTypeKind::U64: {
								const u64 v = is_min ? 0 : (u64)18446744073709551615ULL;
								memcpy(value.value, &v, sizeof(v));
								break;
							}
							case ResolvedTypeKind::F32: {
								const float v = is_min ? -3.402823466e+38f : 3.402823466e+38f;
								memcpy(value.value, &v, sizeof(v));
								break;
							}
							case ResolvedTypeKind::F64: {
								const double v = is_min ? -1.7976931348623157e+308 : 1.7976931348623157e+308;
								memcpy(value.value, &v, sizeof(v));
								break;
							}
							default: ASSERT(false); break;
						}
						return value;
					}
					default:
						ASSERT(false);
						return alloc<ExOpNop>();
				}
			}
			case Expression::INT_LITERAL: {
				auto& ile = static_cast<IntLiteralExpression&>(expr);
				auto& op = alloc<ExOpLoadConst>();
				op.type = ile.resolved_type;
				if (ile.resolved_type->kind == ResolvedTypeKind::F32) {
					const float value = (float)ile.value;
					memcpy(&op.value, &value, sizeof(value));
				} else if (ile.resolved_type->kind == ResolvedTypeKind::F64) {
					const double value = (double)ile.value;
					memcpy(&op.value, &value, sizeof(value));
				} else {
					memcpy(&op.value, &ile.value, sizeof(ile.value));
				}
				return op;
			}
			case Expression::FLOAT_LITERAL: {
				auto& fle = static_cast<FloatLiteralExpression&>(expr);
				auto& op = alloc<ExOpLoadConst>();
				op.type = fle.resolved_type;
				if (fle.resolved_type->kind == ResolvedTypeKind::F32) {
					float value = (float)fle.value;
					memcpy(&op.value, &value, sizeof(value));
				} else {
					memcpy(&op.value, &fle.value, sizeof(fle.value));
				}
				return op;
			}
			case Expression::UNARY: {
				auto& unary = static_cast<UnaryExpression&>(expr);
				ExIrOp& operand = buildExpressionIR(*unary.expression, true);
				if (unary.operator_fn) {
					auto& call = alloc<ExOpCallDirect>();
					call.function = unary.operator_fn;
					call.arg_count = 1;
					call.return_size = typeByteSize(*unary.resolved_type);
					call.args = static_cast<ExIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(ExIrOp*), alignof(ExIrOp*)));
					call.args[0] = &operand;
					return call;
				}
				if (unary.op == Token::MINUS) {
					auto& op = alloc<ExOpNeg>();
					op.operand_type = unary.resolved_type;
					op.operand = &operand;
					return op;
				}
				if (unary.op == Token::NOT) {
					if (operand.kind >= ExIrOpKind::EQ && operand.kind <= ExIrOpKind::GE) {
						auto& cmp = static_cast<ExOpBinary&>(operand);
						auto& op = alloc<ExOpBinary>(invertCompare(operand.kind));
						op.operand_type = cmp.operand_type;
						op.lhs = cmp.lhs;
						op.rhs = cmp.rhs;
						return op;
					}
					auto& op = alloc<ExOpNot>();
					op.operand_type = unary.resolved_type;
					op.operand = &operand;
					return op;
				}
				break;
			}
			case Expression::BINARY: {
				auto& be = static_cast<BinaryExpression&>(expr);
				ExIrOp& lhs = buildExpressionIR(*be.lhs, true);
				if (be.op == Token::IS) {
					ASSERT(be.lhs->resolved_type->kind == ResolvedTypeKind::UNION);
					ASSERT(be.rhs->resolved_type->kind == ResolvedTypeKind::META);
					auto& tag = alloc<ExOpExtractValue>();
					tag.value = &lhs;
					tag.offset = 0;
					tag.size = sizeof(i32);
					i32 member_index = be.union_member_index;
					ASSERT(member_index >= 0);
					static ResolvedType tag_type(ResolvedTypeKind::I32);
					auto& expected = alloc<ExOpLoadConst>();
					expected.type = &tag_type;
					memcpy(expected.value, &member_index, sizeof(member_index));
					auto& condition = alloc<ExOpEq>();
					condition.operand_type = &tag_type;
					condition.lhs = &tag;
					condition.rhs = &expected;
					return condition;
				}
				ExIrOp& rhs = buildExpressionIR(*be.rhs, true);
				if (be.operator_fn) {
					auto& op = alloc<ExOpCallDirect>();
					op.function = be.operator_fn;
					op.arg_count = 2;
					op.return_size = typeByteSize(*be.resolved_type);
					op.args = static_cast<ExIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(ExIrOp*) * op.arg_count, alignof(ExIrOp*)));
					op.args[0] = &lhs;
					op.args[1] = &rhs;
					return op;
				}
				ExOpBinary* op = nullptr;
				switch (be.op) {
					case Token::PLUS: op = &alloc<ExOpAdd>(); break;
					case Token::MINUS: op = &alloc<ExOpSub>(); break;
					case Token::STAR: op = &alloc<ExOpMul>(); break;
					case Token::SLASH: op = &alloc<ExOpDiv>(); break;
					case Token::PERCENT: op = &alloc<ExOpMod>(); break;
					case Token::GT: op = &alloc<ExOpGt>(); break;
					case Token::LT: op = &alloc<ExOpLt>(); break;
					case Token::GT_EQUAL: op = &alloc<ExOpGe>(); break;
					case Token::LT_EQUAL: op = &alloc<ExOpLe>(); break;
					case Token::EQUAL_EQUAL: op = &alloc<ExOpEq>(); break;
					case Token::BANG_EQUAL: op = &alloc<ExOpNe>(); break;
					case Token::AND: op = &alloc<ExOpAnd>(); break;
					case Token::OR: op = &alloc<ExOpOr>(); break;
					default: ASSERT(false); break;
				}
				op->operand_type = be.lhs->resolved_type;
				op->lhs = &lhs;
				op->rhs = &rhs;
				return *op;
			}
			case Expression::TERNARY: {
				auto& ternary = static_cast<TernaryExpression&>(expr);
				auto& op = alloc<ExOpTernary>();
				op.type = expr.resolved_type;
				op.condition = &buildExpressionIR(*ternary.condition, true);
				op.true_value = &buildImplicitConversionIR(*ternary.true_expr, *expr.resolved_type);
				op.false_value = &buildImplicitConversionIR(*ternary.false_expr, *expr.resolved_type);
				return op;
			}
		}
		ASSERT(false);
		static ExIrOp dummy(ExIrOpKind::RETURN);
		return dummy;
	}

	void emitDefers(ExIrBlockData& parent, u32 mark) {
		for (i32 i = defers.size() - 1; i >= (i32)mark; --i) {
			buildStatementIR(*defers[i], parent);
		}
	}

	// Replaces constant leaves in a loop condition with allocas initialized
	// once before the loop, so per-iteration entry/back-edge checks read
	// dedicated frame slots instead of re-materializing constants into shared
	// scratch temps on every evaluation.
	void hoistConditionConstants(ExIrOp*& node, ExIrBlockData& parent) {
		if (!node) return;
		switch (node->kind) {
			case ExIrOpKind::LOAD_CONST: {
				auto& c = *static_cast<ExOpLoadConst*>(node);
				bool is_zero = true;
				for (u32 i = 0, num = typeByteSize(*c.type); i < num; ++i)
					if (c.value[i] != 0) is_zero = false;
				// Zero has dedicated conditional branches. Keep it as a literal
				// so emission can use those branches without a frame temporary.
				if (is_zero) break;

				auto& alloca = allocAlloca(c.type, {}, &c);
				parent.ops.push(&alloca);
				auto& ref = alloc<ExOpFramePtr>();
				ref.alloca = &alloca;
				node = &ref;
				break;
			}
			case ExIrOpKind::EQ:
			case ExIrOpKind::NE:
			case ExIrOpKind::LT:
			case ExIrOpKind::LE:
			case ExIrOpKind::GT:
			case ExIrOpKind::GE:
			case ExIrOpKind::COMPARE:
			case ExIrOpKind::ADD:
			case ExIrOpKind::SUB:
			case ExIrOpKind::MUL:
			case ExIrOpKind::DIV:
			case ExIrOpKind::MOD:
			case ExIrOpKind::AND:
			case ExIrOpKind::OR: {
				auto& bin = *static_cast<ExOpBinary*>(node);
				hoistConditionConstants(bin.lhs, parent);
				hoistConditionConstants(bin.rhs, parent);
				break;
			}
			case ExIrOpKind::NOT:
			case ExIrOpKind::NEG: {
				auto& unary = *static_cast<ExOpUnary*>(node);
				hoistConditionConstants(unary.operand, parent);
				break;
			}
			case ExIrOpKind::CAST: {
				auto& cast = *static_cast<ExOpCast*>(node);
				hoistConditionConstants(cast.value, parent);
				break;
			}
			default: break;
		}
	}

	void buildStatementIR(Statement& st, ExIrBlockData& parent) {
		SourceScope scope(*this, st.token);
		switch (st.kind) {
			case Statement::EXPRESSION: {
				auto& ex = static_cast<ExpressionStatement&>(st);
				parent.ops.push(&buildExpressionIR(*ex.expression, true));
				break;
			}
			case Statement::LABEL: {
				auto& label = static_cast<LabelStatement&>(st);
				const ex_string_view previous_label = pending_loop_label;
				pending_loop_label = label.name;
				buildStatementIR(*label.statement, parent);
				pending_loop_label = previous_label;
				break;
			}
			case Statement::WHILE: {
				auto& while_statement = static_cast<WhileStatement&>(st);
				auto& exit = alloc<ExOpNop>();
				ExIrOp* condition = &buildExpressionIR(*while_statement.condition, true);
				hoistConditionConstants(condition, parent);

				auto& loop = alloc<ExOpConditionalJump>();
				loop.condition = condition;
				loop.true_block = &alloc<ExIrBlockData>(host.arena);

				auto& body_start = alloc<ExOpNop>();
				loop.body_start = &body_start;
				loop.true_block->ops.push(&body_start);

				// Bottom check: the per-iteration back edge. Shares the condition
				// tree with the entry check above; only one of the two runs per
				// pass through its position, so the condition is still evaluated
				// exactly once per iteration.
				auto& bottom = alloc<ExOpConditionalJump>();
				bottom.condition = loop.condition;
				bottom.bottom_tested = true;
				bottom.body_start = &body_start;

				loops.push({pending_loop_label, &bottom, &exit, (u32)defers.size()});
				pending_loop_label = {};
				buildStatementIR(*while_statement.body, *loop.true_block);
				loops.pop_back();

				loop.true_block->ops.push(&bottom);
				parent.ops.push(&loop);
				parent.ops.push(&exit);
				break;
			}
			case Statement::FOR: {
				auto& for_statement = static_cast<ForStatement&>(st);
				const bool is_range = for_statement.end != nullptr;
				const u32 local_watermark = locals.size();

				// `unroll for` over a comptime slice is expanded into straight-line
				// copies by the checker, which also folds the loop variable bindings
				// into comptime constants. Emit each copy with its own loop context so
				// `break`/`continue` emit real runtime branches: `continue` jumps to
				// the next copy (or out after the last), `break` jumps past all copies.
				if (for_statement.is_expanded) {
					auto& expanded = static_cast<BlockStatement&>(*for_statement.body);
					ExOpNop& exit = alloc<ExOpNop>();
					for (i32 i = 0; i < expanded.statements.size(); ++i) {
						ExOpNop& next = alloc<ExOpNop>();
						const bool is_last = i + 1 == expanded.statements.size();
						loops.push({pending_loop_label, is_last ? static_cast<ExIrOp*>(&exit) : static_cast<ExIrOp*>(&next), &exit, (u32)defers.size()});
						buildStatementIR(*expanded.statements[i], parent);
						loops.pop_back();
						if (!is_last) parent.ops.push(&next);
					}
					pending_loop_label = {};
					parent.ops.push(&exit);
					break;
				}

				const auto finishLoopIteration = [&](ExOpConditionalJump& loop, ExOpNop& increment_target, ExOpConditionalJump& bottom, ExOpNop& exit, ExOpAlloca* counter, ResolvedType* counter_type) {
					auto& block = *loop.true_block;
					block.ops.push(&increment_target);
					auto& addr = alloc<ExOpPushLocalAddr>();
					addr.alloca = counter;
					auto& load = alloc<ExOpLoad>();
					load.addr = &addr;
					load.size = typeByteSize(*counter_type);
					auto& one = alloc<ExOpLoadConst>();
					one.type = counter_type;
					const u64 one_value = 1;
					memcpy(one.value, &one_value, typeByteSize(*counter_type));
					auto& increment = alloc<ExOpAdd>();
					increment.operand_type = counter_type;
					increment.lhs = &load;
					increment.rhs = &one;
					auto& dst = alloc<ExOpPushLocalAddr>();
					dst.alloca = counter;
					auto& store = alloc<ExOpCopy>();
					store.type = counter_type;
					store.src = &increment;
					store.dst = &dst;
					block.ops.push(&store);
					loops.pop_back();
					locals.resize(local_watermark);
					block.ops.push(&bottom);
					parent.ops.push(&loop);
					parent.ops.push(&exit);
				};

				if (is_range) {
					ResolvedType* value_type = for_statement.begin->resolved_type;
					auto& value = allocAlloca(value_type, for_statement.value_var, &buildExpressionIR(*for_statement.begin, true));
					parent.ops.push(&value);
					auto& end = allocAlloca(value_type, {}, &buildExpressionIR(*for_statement.end, true));
					parent.ops.push(&end);

					locals.push({for_statement.value_var, &value});
					auto& value_addr = alloc<ExOpPushLocalAddr>();
					value_addr.alloca = &value;
					auto& value_load = alloc<ExOpLoad>();
					value_load.addr = &value_addr;
					value_load.size = typeByteSize(*value_type);
					auto& end_addr = alloc<ExOpPushLocalAddr>();
					end_addr.alloca = &end;
					auto& end_load = alloc<ExOpLoad>();
					end_load.addr = &end_addr;
					end_load.size = typeByteSize(*value_type);
				auto& condition = alloc<ExOpLt>();
				condition.operand_type = value_type;
				condition.lhs = &value_load;
				condition.rhs = &end_load;
				ExIrOp* condition_op = &condition;
				hoistConditionConstants(condition_op, parent);

				auto& loop = alloc<ExOpConditionalJump>();
				auto& increment_target = alloc<ExOpNop>();
				auto& exit = alloc<ExOpNop>();
				loop.condition = condition_op;
					loop.true_block = &alloc<ExIrBlockData>(host.arena);

					auto& body_start = alloc<ExOpNop>();
					loop.body_start = &body_start;
					loop.true_block->ops.push(&body_start);

					auto& bottom = alloc<ExOpConditionalJump>();
					bottom.condition = &condition;
					bottom.bottom_tested = true;
					bottom.body_start = &body_start;

					loops.push({pending_loop_label, &increment_target, &exit, (u32)defers.size()});
					pending_loop_label = {};
					buildStatementIR(*for_statement.body, *loop.true_block);
					finishLoopIteration(loop, increment_target, bottom, exit, &value, value_type);
					return;
				}

				if (for_statement.is_custom_iterator) {
					ResolvedType* iterator_type = for_statement.begin->resolved_type;
					ResolvedType* element_type = for_statement.iterator_element_type;
					auto& iterator = allocAlloca(iterator_type, {}, &buildExpressionIR(*for_statement.begin, true));
					parent.ops.push(&iterator);
					auto& value = allocAlloca(element_type, for_statement.value_var, &alloc<ExOpNop>());
					parent.ops.push(&value);

					ExOpAlloca* counter = nullptr;
					if (for_statement.is_key_value) {
						static ResolvedType index_type(ResolvedTypeKind::ISIZE);
						auto& zero = alloc<ExOpLoadConst>();
						zero.type = &index_type;
						const u64 zero_value = 0;
						memcpy(zero.value, &zero_value, typeByteSize(*zero.type));
						counter = &allocAlloca(zero.type, for_statement.key_var, &zero);
						parent.ops.push(counter);
					}

					locals.push({for_statement.value_var, &value});
					if (counter) locals.push({for_statement.key_var, counter});

					auto& iterator_addr = alloc<ExOpPushLocalAddr>();
					iterator_addr.alloca = &iterator;
					auto& iterator_load = alloc<ExOpLoad>();
					iterator_load.addr = &iterator_addr;
					iterator_load.size = typeByteSize(*iterator_type);
					const StructResolvedType& struct_type = *static_cast<StructResolvedType*>(iterator_type);
					const ResolvedType* next_type = struct_type.fields[for_statement.iterator_next_field].type;
					auto& next = alloc<ExOpExtractValue>();
					next.value = &iterator_load;
					next.offset = struct_type.fields[for_statement.iterator_next_field].offset;
					next.size = typeByteSize(*next_type);

					auto& next_iterator_addr = alloc<ExOpPushLocalAddr>();
					next_iterator_addr.alloca = &iterator;
					auto& value_addr = alloc<ExOpPushLocalAddr>();
					value_addr.alloca = &value;
					auto& condition_call = alloc<ExOpCallIndirect>();
					condition_call.callee = &next;
					condition_call.arg_count = 2;
					condition_call.return_size = sizeof(bool);
					condition_call.args = static_cast<ExIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(ExIrOp*) * 2, alignof(ExIrOp*)));
					condition_call.arg_sizes = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * 2, alignof(u32)));
					condition_call.args[0] = &next_iterator_addr;
					condition_call.args[1] = &value_addr;
					condition_call.arg_sizes[0] = typeByteSize(*static_cast<PointerResolvedType*>(static_cast<const FunctionResolvedType*>(next_type)->params[0].type));
					condition_call.arg_sizes[1] = typeByteSize(*static_cast<PointerResolvedType*>(static_cast<const FunctionResolvedType*>(next_type)->params[1].type));

					auto& loop = alloc<ExOpConditionalJump>();
					auto& increment_target = alloc<ExOpNop>();
					auto& exit = alloc<ExOpNop>();
					loop.condition = &condition_call;
					loop.true_block = &alloc<ExIrBlockData>(host.arena);
					auto& body_start = alloc<ExOpNop>();
					loop.body_start = &body_start;
					loop.true_block->ops.push(&body_start);
					auto& bottom = alloc<ExOpConditionalJump>();
					bottom.condition = &condition_call;
					bottom.bottom_tested = true;
					bottom.body_start = &body_start;
					loops.push({pending_loop_label, &increment_target, &exit, (u32)defers.size()});
					pending_loop_label = {};
					buildStatementIR(*for_statement.body, *loop.true_block);
					loop.true_block->ops.push(&increment_target);
					if (counter) {
						auto& addr = alloc<ExOpPushLocalAddr>(); addr.alloca = counter;
						auto& load = alloc<ExOpLoad>(); load.addr = &addr; load.size = typeByteSize(*counter->type);
						auto& one = alloc<ExOpLoadConst>(); one.type = counter->type; const u64 one_value = 1; memcpy(one.value, &one_value, typeByteSize(*one.type));
						auto& add = alloc<ExOpAdd>(); add.operand_type = counter->type; add.lhs = &load; add.rhs = &one;
						auto& dst = alloc<ExOpPushLocalAddr>(); dst.alloca = counter;
						auto& store = alloc<ExOpCopy>(); store.type = counter->type; store.src = &add; store.dst = &dst;
						loop.true_block->ops.push(&store);
					}
					loops.pop_back();
					locals.resize(local_watermark);
					loop.true_block->ops.push(&bottom);
					parent.ops.push(&loop);
					parent.ops.push(&exit);
					return;
				}

				ResolvedType* container_type = for_statement.begin->resolved_type;
				const bool is_slice = container_type->kind == ResolvedTypeKind::SLICE;
				ASSERT(is_slice || container_type->kind == ResolvedTypeKind::ARRAY);
				ResolvedType* element_type = is_slice
					? static_cast<SliceResolvedType*>(container_type)->element_type
					: static_cast<ArrayResolvedType*>(container_type)->element_type;
				static ResolvedType slice_index_type(ResolvedTypeKind::ISIZE);
				static ResolvedType array_index_type(ResolvedTypeKind::I32);
				ResolvedType* index_type = is_slice ? &slice_index_type : &array_index_type;

				auto& container = allocAlloca(container_type, {}, &buildExpressionIR(*for_statement.begin, true));
				parent.ops.push(&container);

				auto& zero = alloc<ExOpLoadConst>();
				zero.type = index_type;
				const u64 zero_value = 0;
				memcpy(zero.value, &zero_value, typeByteSize(*index_type));

				auto& counter = allocAlloca(index_type, {}, &zero);
				parent.ops.push(&counter);

				auto& value = allocAlloca(element_type, for_statement.value_var, &alloc<ExOpNop>());
				parent.ops.push(&value);
				
				if (for_statement.is_key_value) {
					counter.name = for_statement.key_var;
					locals.push({for_statement.key_var, &counter});
				}
				locals.push({for_statement.value_var, &value});

				auto& counter_addr = alloc<ExOpPushLocalAddr>();
				counter_addr.alloca = &counter;
				auto& counter_load = alloc<ExOpLoad>();
				counter_load.addr = &counter_addr;
				counter_load.size = typeByteSize(*index_type);
				auto& container_addr = alloc<ExOpPushLocalAddr>();
				container_addr.alloca = &container;
				auto& container_load = alloc<ExOpLoad>();
				container_load.addr = &container_addr;
				container_load.size = typeByteSize(*container_type);
				ExIrOp* boundary = nullptr;
				if (is_slice) {
					auto& length = alloc<ExOpExtractValue>();
					length.value = &container_load;
					length.offset = sizeof(void*);
					length.size = sizeof(i64);
					boundary = &length;
				}
				else {
					auto& length = alloc<ExOpLoadConst>();
					length.type = index_type;
					const i32 array_length = (i32)static_cast<ArrayResolvedType*>(container_type)->size;
					memcpy(length.value, &array_length, sizeof(array_length));
					boundary = &length;
				}
				auto& condition = alloc<ExOpLt>();
				condition.operand_type = index_type;
				condition.lhs = &counter_load;
				condition.rhs = boundary;
				ExIrOp* condition_op = &condition;
				hoistConditionConstants(condition_op, parent);

				auto& loop = alloc<ExOpConditionalJump>();
				auto& increment_target = alloc<ExOpNop>();
				auto& exit = alloc<ExOpNop>();
				loop.condition = condition_op;
				loop.true_block = &alloc<ExIrBlockData>(host.arena);

				auto& body_start = alloc<ExOpNop>();
				loop.body_start = &body_start;
				loop.true_block->ops.push(&body_start);

				auto& bottom = alloc<ExOpConditionalJump>();
				bottom.condition = condition_op;
				bottom.bottom_tested = true;
				bottom.body_start = &body_start;

				loops.push({pending_loop_label, &increment_target, &exit, (u32)defers.size()});
				pending_loop_label = {};

				ExIrOp* element = nullptr;
				if (is_slice) {
					auto& load = alloc<ExOpSliceLoad>();
					load.slice = &container_load;
					load.index = &counter_load;
					load.index_type = index_type;
					load.element_size = typeByteSize(*element_type);
					element = &load;
				}
				else {
					auto& base = buildExpressionIR(*for_statement.begin, false);
					auto& byte_size = alloc<ExOpLoadConst>();
					static ResolvedType offset_type(ResolvedTypeKind::U64);
					byte_size.type = &offset_type;
					const u32 size = typeByteSize(*element_type);
					const u64 pointer_scale = size;
					memcpy(byte_size.value, &pointer_scale, sizeof(pointer_scale));
					auto& casted_index = alloc<ExOpCast>();
					casted_index.type = &offset_type;
					casted_index.target_type = index_type;
					casted_index.value = &counter_load;
					auto& offset = alloc<ExOpMul>();
					offset.operand_type = &offset_type;
					offset.lhs = &casted_index;
					offset.rhs = &byte_size;
					auto& address = alloc<ExOpAdd>();
					address.operand_type = &offset_type;
					address.lhs = &base;
					address.rhs = &offset;
					address.result_mode = ExIrOp::ADDRESS;
					auto& load = alloc<ExOpLoad>();
					load.addr = &address;
					load.size = size;
					element = &load;
				}
				auto& value_addr = alloc<ExOpPushLocalAddr>();
				value_addr.alloca = &value;
				auto& assign_value = alloc<ExOpCopy>();
				assign_value.type = element_type;
				assign_value.src = element;
				assign_value.dst = &value_addr;
				loop.true_block->ops.push(&assign_value);
				buildStatementIR(*for_statement.body, *loop.true_block);
				finishLoopIteration(loop, increment_target, bottom, exit, &counter, index_type);
				return;
			}
			case Statement::MATCH: {
				auto& match = static_cast<MatchStatement&>(st);
				if (match.comptime_known) {
					ASSERT(match.comptime_arm >= 0 && match.comptime_arm < match.arms.size());
					buildStatementIR(*match.arms[match.comptime_arm].body, parent);
					break;
				}
				auto& subject = allocAlloca(match.subject->resolved_type, {}, &buildExpressionIR(*match.subject, true));
				parent.ops.push(&subject);

				ExIrBlockData* current = &parent;
				MatchArm* fallback_arm = nullptr;
				for (MatchArm& arm : match.arms) {
					if (arm.is_fallback) {
						fallback_arm = &arm;
						continue;
					}
					ExIrBlockData* arm_body = &alloc<ExIrBlockData>(host.arena);
					for (i32 pattern_index = 0; pattern_index < arm.patterns.size(); ++pattern_index) {
						MatchPattern& pattern = arm.patterns[pattern_index];
						auto& subject_addr = alloc<ExOpPushLocalAddr>();
						subject_addr.alloca = &subject;
						auto& subject_value = alloc<ExOpLoad>();
						subject_value.addr = &subject_addr;
						subject_value.size = typeByteSize(*subject.type);

						ExIrOp* condition = nullptr;
						if (pattern.end) {
							auto& lower = alloc<ExOpGe>();
							lower.operand_type = subject.type;
							lower.lhs = &subject_value;
							lower.rhs = &buildExpressionIR(*pattern.begin, true);
							auto& upper_subject_addr = alloc<ExOpPushLocalAddr>();
							upper_subject_addr.alloca = &subject;
							auto& upper_subject = alloc<ExOpLoad>();
							upper_subject.addr = &upper_subject_addr;
							upper_subject.size = typeByteSize(*subject.type);
							auto& upper = alloc<ExOpLe>();
							upper.operand_type = subject.type;
							upper.lhs = &upper_subject;
							upper.rhs = &buildExpressionIR(*pattern.end, true);
							auto& range = alloc<ExOpAnd>();
							range.operand_type = subject.type;
							range.lhs = &lower;
							range.rhs = &upper;
							condition = &range;
						} else if (subject.type->kind == ResolvedTypeKind::ANY) {
							auto& type_value = alloc<ExOpExtractValue>();
							type_value.value = &subject_value;
							type_value.offset = 8;
							type_value.size = 4;
							auto& expected = alloc<ExOpLoadConst>();
							static ResolvedType i32_type(ResolvedTypeKind::I32);
							static ResolvedType meta_type(ResolvedTypeKind::META);
							expected.type = &i32_type;
							ResolvedType* matched_type = pattern.begin->resolved_type && pattern.begin->resolved_type->kind == ResolvedTypeKind::META
															 ? static_cast<MetaType*>(pattern.begin->resolved_type)->inner
															 : pattern.begin->resolved_type;
							const u32 type_id = (u32)matched_type->kind;
							memcpy(expected.value, &type_id, sizeof(type_id));
							auto& equality = alloc<ExOpEq>();
							equality.operand_type = &i32_type;
							equality.lhs = &type_value;
							equality.rhs = &expected;
							condition = &equality;
						} else if (subject.type->kind == ResolvedTypeKind::UNION) {
							i32 member_index = pattern.union_member_index;
							ASSERT(member_index >= 0);
							auto& tag = alloc<ExOpExtractValue>();
							tag.value = &subject_value;
							tag.offset = 0;
							tag.size = sizeof(i32);
							static ResolvedType tag_type(ResolvedTypeKind::I32);
							auto& expected = alloc<ExOpLoadConst>();
							expected.type = &tag_type;
							memcpy(expected.value, &member_index, sizeof(member_index));
							auto& equality = alloc<ExOpEq>();
							equality.operand_type = &tag_type;
							equality.lhs = &tag;
							equality.rhs = &expected;
							condition = &equality;
						} else {
							auto& equality = alloc<ExOpEq>();
							equality.operand_type = subject.type;
							equality.lhs = &subject_value;
							equality.rhs = &buildExpressionIR(*pattern.begin, true);
							condition = &equality;
						}

						auto& branch = alloc<ExOpConditionalJump>();
						branch.condition = condition;
						branch.true_block = arm_body;
						branch.false_block = &alloc<ExIrBlockData>(host.arena);
						current->ops.push(&branch);
						current = branch.false_block;
					}
					buildStatementIR(*arm.body, *arm_body);
				}
				if (fallback_arm) {
					buildStatementIR(*fallback_arm->body, *current);
				}
				break;
			}
			case Statement::BREAK:
			case Statement::CONTINUE: {
				const bool is_break = st.kind == Statement::BREAK;
				const ex_string_view label = is_break ? static_cast<BreakStatement&>(st).label : static_cast<ContinueStatement&>(st).label;
				Loop* target = nullptr;
				if (size(label) == 0) {
					target = &loops.back();
				} else {
					for (i32 i = loops.size() - 1; i >= 0; --i) {
						if (equalStrings(label, loops[i].label)) {
							target = &loops[i];
							break;
						}
					}
				}
				ASSERT(target);
				emitDefers(parent, target->defer_watermark);
				auto& jump = alloc<ExOpJump>();
				jump.target = is_break ? target->break_target : target->continue_target;
				parent.ops.push(&jump);
				break;
			}
			case Statement::IF: {
				auto& ifs = static_cast<IfStatement&>(st);
				if (ifs.comptime_known) {
					Statement* branch = ifs.comptime_value ? static_cast<Statement*>(ifs.body) : ifs.else_branch;
					if (branch) buildStatementIR(*branch, parent);
					break;
				}
				auto& if_ir = alloc<ExOpConditionalJump>();
				if_ir.condition = &buildExpressionIR(*ifs.condition, true);
				if_ir.true_block = &alloc<ExIrBlockData>(host.arena);
				buildStatementIR(*ifs.body, *if_ir.true_block);
				if (ifs.else_branch) {
					if_ir.false_block = &alloc<ExIrBlockData>(host.arena);
					buildStatementIR(*ifs.else_branch, *if_ir.false_block);
				}
				parent.ops.push(&if_ir);
				break;
			}
			case Statement::ASSIGN: {
				auto& as = static_cast<AssignStatement&>(st);
				auto& rhs = buildImplicitConversionIR(*as.rhs, *as.lhs->resolved_type);
				auto& lhs = buildExpressionIR(*as.lhs, false);
				if (as.op == Token::EQUAL) {
					auto& cpy = alloc<ExOpCopy>();
					cpy.src = &rhs;
					cpy.dst = &lhs;
					cpy.type = as.lhs->resolved_type;
					parent.ops.push(&cpy);
					return;
				}
				auto& load = alloc<ExOpLoad>();
				load.addr = &lhs;
				load.size = typeByteSize(*as.lhs->resolved_type);
				ExIrOp* lhs_value = &load;
				if (as.resolved_op_fn) {
					auto& call = alloc<ExOpCallDirect>();
					call.function = as.resolved_op_fn;
					call.arg_count = 2;
					call.return_size = typeByteSize(*as.lhs->resolved_type);
					call.args = static_cast<ExIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(ExIrOp*) * call.arg_count, alignof(ExIrOp*)));
					call.args[0] = lhs_value;
					call.args[1] = &rhs;
					auto& cpy = alloc<ExOpCopy>();
					cpy.src = &call;
					cpy.dst = &lhs;
					cpy.type = as.lhs->resolved_type;
					parent.ops.push(&cpy);
					break;
				}
				ExOpBinary* op = nullptr;
				switch (as.op) {
					case Token::STAR_EQUAL: op = &alloc<ExOpMul>(); break;
					case Token::PLUS_EQUAL: op = &alloc<ExOpAdd>(); break;
					case Token::MINUS_EQUAL: op = &alloc<ExOpSub>(); break;
					case Token::SLASH_EQUAL: op = &alloc<ExOpDiv>(); break;
					default: ASSERT(false); return;
				}
				op->operand_type = as.lhs->resolved_type;
				op->lhs = lhs_value;
				op->rhs = &rhs;
				auto& cpy = alloc<ExOpCopy>();
				cpy.src = op;
				cpy.dst = &lhs;
				cpy.type = as.lhs->resolved_type;
				parent.ops.push(&cpy);
				break;
			}
			case Statement::VAR_DECL: {
				auto& vd = static_cast<VarDeclStatement&>(st);
				if (vd.is_comptime) break;
				if (vd.else_return) {
					ASSERT(vd.expression->resolved_type);
					if (vd.expression->resolved_type->kind == ResolvedTypeKind::NULLABLE) {
						auto& nullable = static_cast<NullableResolvedType&>(*vd.expression->resolved_type);
						auto& tmp = allocAlloca(vd.expression->resolved_type, {}, &buildExpressionIR(*vd.expression, true));
						parent.ops.push(&tmp);
						static ResolvedType u8_type(ResolvedTypeKind::U8);
						auto& flag = alloc<ExOpExtractValue>();
						flag.value = &loadAllocaValue(tmp);
						flag.offset = 0;
						flag.size = 1;
						auto& zero = alloc<ExOpLoadConst>();
						zero.type = &u8_type;
						const u8 zero_value = 0;
						memcpy(zero.value, &zero_value, sizeof(zero_value));
						auto& is_null = alloc<ExOpEq>();
						is_null.operand_type = &u8_type;
						is_null.lhs = &flag;
						is_null.rhs = &zero;
						auto& branch = alloc<ExOpConditionalJump>();
						branch.condition = &is_null;
						branch.true_block = &alloc<ExIrBlockData>(host.arena);
						branch.false_block = &alloc<ExIrBlockData>(host.arena);
						auto& dest = allocAlloca(vd.resolved_type, vd.name, &alloc<ExOpExtractValue>());
						auto& payload = static_cast<ExOpExtractValue&>(*dest.value);
						payload.value = &loadAllocaValue(tmp);
						payload.offset = 1;
						payload.size = typeByteSize(*nullable.inner);
						branch.false_block->ops.push(&dest);
						locals.push({vd.name, &dest});
						buildReturnIR(*branch.true_block, nullptr);
						parent.ops.push(&branch);
						break;
					}
					// var v : T = expr else return;
					// Evaluate expr once. If its tag is in T, bind v (extract or remap).
					// Otherwise return the residual U-T, converted to the function return type.
					ASSERT(vd.expression->resolved_type);
					ASSERT(vd.expression->resolved_type->kind == ResolvedTypeKind::UNION);
					ASSERT(vd.else_return_type);
					ASSERT(vd.resolved_type);
					auto& source_union = static_cast<UnionResolvedType&>(*vd.expression->resolved_type);
					auto& tmp = allocAlloca(vd.expression->resolved_type, {}, &buildExpressionIR(*vd.expression, true));
					parent.ops.push(&tmp);

					static ResolvedType tag_type(ResolvedTypeKind::I32);
					static ResolvedType bool_type(ResolvedTypeKind::BOOL);
					auto& tag = alloc<ExOpExtractValue>();
					tag.value = &loadAllocaValue(tmp);
					tag.offset = 0;
					tag.size = sizeof(i32);
					ExIrOp* condition = nullptr;
					for (i32 i = 0; i < source_union.members.size(); ++i) {
						if ((vd.else_return_target_mask & (1ull << (u32)i)) == 0) continue;
						auto& expected = alloc<ExOpLoadConst>();
						expected.type = &tag_type;
						memcpy(expected.value, &i, sizeof(i));
						auto& eq = alloc<ExOpEq>();
						eq.operand_type = &tag_type;
						eq.lhs = &tag;
						eq.rhs = &expected;
						if (!condition) {
							condition = &eq;
							continue;
						}
						auto& or_op = alloc<ExOpOr>();
						or_op.operand_type = &bool_type;
						or_op.lhs = condition;
						or_op.rhs = &eq;
						condition = &or_op;
					}
					ASSERT(condition);

					auto& branch = alloc<ExOpConditionalJump>();
					branch.condition = condition;
					branch.true_block = &alloc<ExIrBlockData>(host.arena);
					branch.false_block = &alloc<ExIrBlockData>(host.arena);

					auto& dest = allocAlloca(vd.resolved_type, vd.name, &convertValue(loadAllocaValue(tmp), *tmp.type, *vd.resolved_type));
					branch.true_block->ops.push(&dest);
					locals.push({vd.name, &dest});

					ExIrOp* residual = &convertValue(loadAllocaValue(tmp), *tmp.type, *vd.else_return_type);
					if (return_type) residual = &convertValue(*residual, *vd.else_return_type, *return_type);
					buildReturnIR(*branch.false_block, residual);
					parent.ops.push(&branch);
					break;
				}
				auto& alloca = allocAlloca(vd.resolved_type, vd.name, &buildImplicitConversionIR(*vd.expression, *vd.resolved_type));
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
				ASSERT(!ret.expression || return_type);
				ExIrOp* value = ret.expression ? &buildImplicitConversionIR(*ret.expression, *return_type) : nullptr;
				buildReturnIR(parent, value);
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

	ExIrBlockData& buildFunctionIR(FunctionExpression& expr) {
		ASSERT(locals.empty());
		ASSERT(!return_type);
		return_type = static_cast<FunctionResolvedType*>(expr.resolved_type)->return_type;
		current_src_loc = expr.token.src_loc;
		stack_cursor = 0;
		alloca_region_size = 0;
		ExIrBlockData& root = alloc<ExIrBlockData>(host.arena);
		u32 param_offset = 0;
		for (FunctionParam& param : expr.params) {
			if (param.is_comptime) continue;
			auto& alloca = alloc<ExOpAlloca>();
			alloca.type = param.resolved_type;
			alloca.name = param.name.value;
			alloca.stack_sp = param_offset;
			locals.push({param.name.value, &alloca});
			param.slot.storage = StorageSlot::LOCAL;
			param.slot.offset = param_offset;
			param.slot.type = param.resolved_type;
			param_offset += typeByteSize(*param.resolved_type);
		}
		stack_cursor = param_offset;
		buildStatementIR(*expr.body, root);
		root.ops.push(&alloc<ExOpReturn>());
		alloca_region_size = stack_cursor;
		locals.clear();
		return_type = nullptr;
		current_src_loc = EX_IR_INVALID_SOURCE_LOC;
		return root;
	}

	struct Local {
		ex_string_view name;
		ExOpAlloca* alloca = nullptr;
		ExIrOp* inline_value = nullptr;
	};
	struct Loop {
		ex_string_view label = {};
		ExIrOp* continue_target = nullptr;
		ExIrOp* break_target = nullptr;
		u32 defer_watermark = 0;
	};

	ex_host& host;
	ExpArray<Local> locals;
	ExpArray<ex_string_view> strings;
	ExpArray<Statement*> defers;
	ExpArray<Loop> loops;
	ex_string_view pending_loop_label = {};
	ResolvedType* return_type = nullptr;
	ExIrSourceLoc current_src_loc = EX_IR_INVALID_SOURCE_LOC;
	u32 stack_cursor = 0;
	u32 alloca_region_size = 0;
	bool do_inline = false;

	struct SourceScope {
		SourceScope(IRBuilder& builder, const Token& token)
			: builder(builder)
			, previous(builder.current_src_loc) {
			builder.current_src_loc = token.src_loc;
		}
		~SourceScope() { builder.current_src_loc = previous; }

		IRBuilder& builder;
		ExIrSourceLoc previous;
	};
};

bool isTypeFactory(const FunctionExpression& function) {
	if (!function.return_type) return false;
	if (function.return_type->kind == Expression::TYPE_LITERAL) {
		return static_cast<const TypeLiteralExpression*>(function.return_type)->type == ResolvedTypeKind::META;
	}
	return function.return_type->resolved_type && function.return_type->resolved_type->kind == ResolvedTypeKind::META;
}

static ex_string_view copyStringViewToArena(ex_arena& arena, ex_string_view src) {
	if (empty(src)) return src;
	const i64 len = (i64)size(src);
	char* mem = (char*)arena.allocate(arena.user_data, len + 1, 1);
	if (!mem) return src;
	memcpy(mem, src.begin, len);
	mem[len] = '\0';
	return { mem, len };
}

// Debug-facing type kind: mirrors BytecodeCompiler::toTypeKind but reports
// nullable as EX_TYPE_NULLABLE (toTypeKind maps it to EX_TYPE_NULL_VALUE for
// bytecode comparison opcodes).
static ex_type_kind debugTypeKind(const ResolvedType& type) {
	if (type.kind == ResolvedTypeKind::NULLABLE) return EX_TYPE_NULLABLE;
	switch (type.kind) {
		case ResolvedTypeKind::VOID: return EX_TYPE_VOID;
		case ResolvedTypeKind::BOOL: return EX_TYPE_BOOL;
		case ResolvedTypeKind::I8: return EX_TYPE_I8;
		case ResolvedTypeKind::I16: return EX_TYPE_I16;
		case ResolvedTypeKind::I32: return EX_TYPE_I32;
		case ResolvedTypeKind::I64: return EX_TYPE_I64;
		case ResolvedTypeKind::UNTYPED_INT: return EX_TYPE_I64;
		case ResolvedTypeKind::UNTYPED_FLOAT: return EX_TYPE_F64;
		case ResolvedTypeKind::U8: return EX_TYPE_U8;
		case ResolvedTypeKind::U16: return EX_TYPE_U16;
		case ResolvedTypeKind::U32: return EX_TYPE_U32;
		case ResolvedTypeKind::U64: return EX_TYPE_U64;
		case ResolvedTypeKind::ISIZE: return EX_TYPE_I64;
		case ResolvedTypeKind::F32: return EX_TYPE_F32;
		case ResolvedTypeKind::F64: return EX_TYPE_F64;
		case ResolvedTypeKind::CSTR: return EX_TYPE_CPTR;
		case ResolvedTypeKind::CPTR: return EX_TYPE_CPTR;
		case ResolvedTypeKind::POINTER: return EX_TYPE_CPTR;
		case ResolvedTypeKind::BYTE: return EX_TYPE_U8;
		case ResolvedTypeKind::FUNCTION: return EX_TYPE_FUNCTION;
		case ResolvedTypeKind::ARRAY: return EX_TYPE_ARRAY;
		case ResolvedTypeKind::SLICE: return EX_TYPE_SLICE;
		case ResolvedTypeKind::ENUM: return EX_TYPE_ENUM;
		case ResolvedTypeKind::STRUCT: return EX_TYPE_STRUCT;
		case ResolvedTypeKind::UNION: return EX_TYPE_TAGGED_UNION;
		default: return EX_TYPE_INVALID;
	}
}

static ResolvedType* structFieldTypeAt(const StructResolvedType& st, i32 index) {
	if (index < st.fields.size()) return st.fields[index].type;
	return nullptr;
}

// Builds the bytecode's type metadata (`ex_bytecode::type_info` and its flat
// field/member/enum-value tables) from the resolved types referenced by
// globals, parameters, and locals. Types are interned (deduplicated by
// structural equality); compound types recursively intern their children
// (struct fields, union members, array/slice/nullable/pointer element). The
// `first_*_index`/`*_count` fields on each `ex_type` entry are captured before
// recursing, so the indices into the flat arrays stay stable until `commit`
// copies everything into the bytecode arena.
struct TypeInfoBuilder {
	TypeInfoBuilder(ex_host& host, ex_bytecode& bytecode)
		: host(host)
		, bytecode(bytecode)
		, sources(host.arena)
		, types(host.arena)
		, fields(host.arena)
		, attributes(host.arena)
		, member_indices(host.arena)
		, enum_values(host.arena) {}

	u32 internType(const ResolvedType& type) {
		for (i32 i = 0; i < sources.size(); ++i) {
			if (typesEqual(sources[i], &type)) return (u32)i;
		}

		const u32 first_field = (u32)fields.size();
		const u32 first_attribute = (u32)attributes.size();
		const u32 first_member = (u32)member_indices.size();
		const u32 first_value = (u32)enum_values.size();
		const u32 self = (u32)types.size();
		sources.push(&type);
		ex_type& entry = types.emplace_back();
		entry.bytecode = &bytecode;
		entry.kind = debugTypeKind(type);
		entry.byte_size = typeByteSize(type);
		entry.alignment = typeAlignment(type);
		entry.first_field_index = first_field;
		entry.first_attribute_index = first_attribute;
		entry.attribute_count = 0;
		entry.member_count = 0;
		entry.first_member_index = first_member;
		entry.first_value_index = first_value;
		entry.element_type_index = EX_TYPE_INDEX_NONE;
		entry.array_length = EX_TYPE_INDEX_NONE;

		switch (type.kind) {
			case ResolvedTypeKind::STRUCT: {
				const StructResolvedType& st = static_cast<const StructResolvedType&>(type);
				if (st.decl) {
					entry.name = copyStringViewToArena(host.arena, st.decl->cached_name);
					if (st.decl->attributes) {
						for (const Attribute& attr : *st.decl->attributes) {
							if (!attr.resolved_type || !attr.comptime_bytes) continue;
							u32 type_index = internType(*attr.resolved_type);
							ex_type_attribute_info& info = attributes.emplace_back();
							info.type_index = type_index;
							u32 size = typeByteSize(*attr.resolved_type);
							void* value = host.arena.allocate(host.arena.user_data, size, 1);
							copyMemory(value, attr.comptime_bytes, size);
							info.value = value;
						}
						entry.attribute_count = (u32)attributes.size() - first_attribute;
					}
					// Append this type's own field entries first (recursion
					// below must not interleave child fields into this range).
					for (i32 i = 0; i < st.decl->fields.size(); ++i) {
						ResolvedType* field_type = structFieldTypeAt(st, i);
						if (!field_type) continue;
						u32 field_index = (u32)fields.size();
						ex_type_field_info& field = fields.emplace_back();
						field.name = copyStringViewToArena(host.arena, st.decl->fields[i].name.value);
						field.type_index = EX_TYPE_INDEX_NONE;
						field.offset = structFieldOffset(st, i);
						field.first_attribute_index = (u32)attributes.size();
						field.attribute_count = 0;
						if (st.decl->fields[i].attributes) {
							for (const Attribute& attr : *st.decl->fields[i].attributes) {
								if (!attr.resolved_type || !attr.comptime_bytes) continue;
								u32 type_index = internType(*attr.resolved_type);
								ex_type_attribute_info& info = attributes.emplace_back();
								info.type_index = type_index;
								u32 size = typeByteSize(*attr.resolved_type);
								void* value = host.arena.allocate(host.arena.user_data, size, 1);
								copyMemory(value, attr.comptime_bytes, size);
								info.value = value;
							}
							fields[field_index].attribute_count = (u32)attributes.size() - fields[field_index].first_attribute_index;
						}
					}
					entry.field_count = (u32)fields.size() - first_field;
					// Then intern child types (which may append their own
					// fields/members to the flat arrays).
					i32 dst = (i32)first_field;
					for (i32 i = 0; i < st.decl->fields.size(); ++i) {
						ResolvedType* field_type = structFieldTypeAt(st, i);
						if (!field_type) continue;
						fields[dst].type_index = internType(*field_type);
						++dst;
					}
				}
				break;
			}
			case ResolvedTypeKind::UNION: {
				const UnionResolvedType& un = static_cast<const UnionResolvedType&>(type);
				for (i32 i = 0; i < un.members.size(); ++i) member_indices.push(EX_TYPE_INDEX_NONE);
				entry.member_count = (u32)member_indices.size() - first_member;
				for (i32 i = 0; i < un.members.size(); ++i) {
					member_indices[(i32)first_member + i] = internType(*un.members[i]);
				}
				break;
			}
			case ResolvedTypeKind::ENUM: {
				const EnumResolvedType& en = static_cast<const EnumResolvedType&>(type);
				if (en.decl) {
					entry.name = copyStringViewToArena(host.arena, en.decl->cached_name);
					for (i32 i = 0; i < en.decl->members.size(); ++i) {
						ex_type_enum_value_info& value = enum_values.emplace_back();
						value.name = copyStringViewToArena(host.arena, en.decl->members[i].name.value);
						value.value = (i32)enumMemberValue(en, i);
					}
					entry.value_count = (u32)enum_values.size() - first_value;
				}
				break;
			}
			case ResolvedTypeKind::ARRAY: {
				const ArrayResolvedType& arr = static_cast<const ArrayResolvedType&>(type);
				entry.element_type_index = internType(*arr.element_type);
				entry.array_length = (arr.size >= 0 && arr.size < (i64)EX_TYPE_INDEX_NONE) ? (u32)arr.size : 0u;
				break;
			}
			case ResolvedTypeKind::SLICE: {
				const SliceResolvedType& slice = static_cast<const SliceResolvedType&>(type);
				entry.element_type_index = internType(*slice.element_type);
				entry.is_const = slice.is_const;
				break;
			}
			case ResolvedTypeKind::NULLABLE: {
				const NullableResolvedType& nullable = static_cast<const NullableResolvedType&>(type);
				entry.element_type_index = internType(*nullable.inner);
				break;
			}
			case ResolvedTypeKind::POINTER: {
				const PointerResolvedType& pointer = static_cast<const PointerResolvedType&>(type);
				entry.element_type_index = internType(*pointer.inner);
				entry.is_const = pointer.is_const;
				break;
			}
			default: break;
		}

		return self;
	}

	void commit() {
		// ExpArray is bin-based (not contiguous), so copy element-wise rather
		// than memcpy'ing data().
		if (types.size() > 0) {
			bytecode.type_info = (ex_type*)host.arena.allocate(host.arena.user_data, sizeof(ex_type) * (u32)types.size(), alignof(ex_type));
			for (i32 i = 0; i < types.size(); ++i) bytecode.type_info[i] = types[i];
			bytecode.type_info_count = (u32)types.size();
			bytecode.type_info_capacity = (u32)types.size();
		}
		if (attributes.size() > 0) {
			bytecode.type_attributes = (ex_type_attribute_info*)host.arena.allocate(host.arena.user_data, sizeof(ex_type_attribute_info) * (u32)attributes.size(), alignof(ex_type_attribute_info));
			for (i32 i = 0; i < attributes.size(); ++i) bytecode.type_attributes[i] = attributes[i];
			bytecode.type_attribute_count = (u32)attributes.size();
			bytecode.type_attribute_capacity = (u32)attributes.size();
		}
		if (fields.size() > 0) {
			bytecode.type_fields = (ex_type_field_info*)host.arena.allocate(host.arena.user_data, sizeof(ex_type_field_info) * (u32)fields.size(), alignof(ex_type_field_info));
			for (i32 i = 0; i < fields.size(); ++i) bytecode.type_fields[i] = fields[i];
			bytecode.type_field_count = (u32)fields.size();
			bytecode.type_field_capacity = (u32)fields.size();
		}
		if (member_indices.size() > 0) {
			bytecode.type_member_indices = (u32*)host.arena.allocate(host.arena.user_data, sizeof(u32) * (u32)member_indices.size(), alignof(u32));
			for (i32 i = 0; i < member_indices.size(); ++i) bytecode.type_member_indices[i] = member_indices[i];
			bytecode.type_member_count = (u32)member_indices.size();
			bytecode.type_member_capacity = (u32)member_indices.size();
		}
		if (enum_values.size() > 0) {
			bytecode.type_enum_values = (ex_type_enum_value_info*)host.arena.allocate(host.arena.user_data, sizeof(ex_type_enum_value_info) * (u32)enum_values.size(), alignof(ex_type_enum_value_info));
			for (i32 i = 0; i < enum_values.size(); ++i) bytecode.type_enum_values[i] = enum_values[i];
			bytecode.type_enum_value_count = (u32)enum_values.size();
			bytecode.type_enum_value_capacity = (u32)enum_values.size();
		}
	}

	ex_host& host;
	ex_bytecode& bytecode;
	ExpArray<const ResolvedType*> sources;
	ExpArray<ex_type> types;
	ExpArray<ex_type_field_info> fields;
	ExpArray<ex_type_attribute_info> attributes;
	ExpArray<u32> member_indices;
	ExpArray<ex_type_enum_value_info> enum_values;
};

struct ByteArray {
	explicit ByteArray(ex_arena& arena)
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

	// Record a source-map entry at the current code offset for `loc`, unless
	// the last entry already sits at this offset (then update it in place).
	// `loc` is a token index into the module SourceLocTable, which
	// ex_bytecode_compile copies verbatim into the bytecode location table.
	void recordSourceMap(ExIrSourceLoc loc) {
		if (loc == EX_IR_INVALID_SOURCE_LOC) return;
		if (!source_map.empty() && source_map.back().code_offset == count) {
			source_map.back().location_index = loc;
			return;
		}
		ex_bytecode_source_map_entry entry;
		entry.code_offset = count;
		entry.location_index = loc;
		source_map.push_back(entry);
	}

	ex_arena& arena;
	u8* data = nullptr;
	u32 count = 0;
	u32 capacity = 0;
	ExpArray<ex_bytecode_source_map_entry> source_map;
};


// IR to bytecode
struct BytecodeCompiler {
	BytecodeCompiler(ex_host& host, ex_bytecode& bytecode, TypeInfoBuilder& type_info)
		: host(host)
		, bytecode(bytecode)
		, type_info(type_info)
		, code(host.arena)
		, local_debug(host.arena) {}

	template <typename T, typename... Args> T& alloc(Args&&... args) {
		T* v = (T*)host.arena.allocate(host.arena.user_data, sizeof(T), alignof(T));
		new (NewPlaceholder(), v) T(static_cast<Args&&>(args)...);
		assignSrcLoc(*v);
		return *v;
	}

	void assignSrcLoc(ExIrOp& op) { op.src_loc = current_src_loc; }
	void assignSrcLoc(ExIrBlockData&) {}

	void emitOp(ex_op op) {
		code.recordSourceMap(current_src_loc);
		u8 tmp = (u8)op;
		emit(&tmp, sizeof(tmp));
	}

	void emit(u8 v) { emit(&v, sizeof(v)); }
	void emit(i16 v) { emit(&v, sizeof(v)); }
	void emit(i32 v) { emit(&v, sizeof(v)); }
	void emit(u32 v) { emit(&v, sizeof(v)); }
	void emit(u64 v) { emit(&v, sizeof(v)); }
	void emit(i64 v) { emit(&v, sizeof(v)); }

	static u32 numericKindIndex(const ResolvedType& type) {
		switch (type.kind) {
			case ResolvedTypeKind::I8: return 0;
			case ResolvedTypeKind::U8: return 1;
			case ResolvedTypeKind::I16: return 2;
			case ResolvedTypeKind::U16: return 3;
			case ResolvedTypeKind::I32: return 4;
			case ResolvedTypeKind::U32: return 5;
			case ResolvedTypeKind::I64: return 6;
			case ResolvedTypeKind::ISIZE: return 6;
			case ResolvedTypeKind::U64: return 7;
			case ResolvedTypeKind::F32: return 8;
			case ResolvedTypeKind::F64: return 9;
			default: ASSERT(false); return -1;
		}
	}

	static ex_op immediateArithmeticOp(ExIrOpKind kind, const ResolvedType& type) {
		const u32 index = kind == ExIrOpKind::ADD || kind == ExIrOpKind::SUB || kind == ExIrOpKind::MUL ? arithmeticKindIndex(type) : numericKindIndex(type);
		switch (kind) {
			case ExIrOpKind::ADD: return ex_op(EX_OP_ADD_8_IMM + index);
			case ExIrOpKind::SUB: return ex_op(EX_OP_SUB_8_IMM + index);
			case ExIrOpKind::MUL: return ex_op(EX_OP_MUL_8_IMM + index);
			case ExIrOpKind::DIV: return ex_op(EX_OP_DIV_I8_IMM + index);
			case ExIrOpKind::MOD:
				ASSERT(index < 8);
				return ex_op(EX_OP_MOD_I8_IMM + index);
			default: ASSERT(false); return EX_OP_ADD_8_IMM;
		}
	}

	static ex_op arithmeticOp(ExIrOpKind kind, const ResolvedType& type) {
		const u32 index = arithmeticKindIndex(type);
		switch (kind) {
			case ExIrOpKind::ADD: return ex_op(EX_OP_ADD_8 + index);
			case ExIrOpKind::SUB: return ex_op(EX_OP_SUB_8 + index);
			case ExIrOpKind::MUL: return ex_op(EX_OP_MUL_8 + index);
			default: ASSERT(false); return EX_OP_ADD_8;
		}
	}

	u32 emitCompare(const ExOpBinary& cmp) {
		if (cmp.operand_type->kind == ResolvedTypeKind::SLICE) {
			const u32 lhs = emit(*cmp.lhs, nullptr);
			const u32 rhs = emit(*cmp.rhs, nullptr);
			const u32 result = stack_top++;
			const auto& slice = static_cast<const SliceResolvedType&>(*cmp.operand_type);
			emitOp(EX_OP_SLICE_EQ);
			emit(result);
			emit(lhs);
			emit(rhs);
			emit(typeByteSize(*slice.element_type));
			emit((u8)toTypeKind(*slice.element_type));
			if (cmp.kind == ExIrOpKind::NE) {
				const u32 neq = stack_top++;
				emitOp(EX_OP_NOT);
				emit(neq);
				emit(result);
				return neq;
			}
			return result;
		}
		u32 lhs_offset = emitOperand(*cmp.lhs);
		u32 rhs_offset = emitOperand(*cmp.rhs);
		switch (cmp.kind) {
			case ExIrOpKind::EQ: emitOp(EX_OP_EQ); break;
			case ExIrOpKind::NE: emitOp(EX_OP_NE); break;
			case ExIrOpKind::LT: emitOp(EX_OP_LT); break;
			case ExIrOpKind::LE: emitOp(EX_OP_LE); break;
			case ExIrOpKind::GT: emitOp(EX_OP_GT); break;
			case ExIrOpKind::GE: emitOp(EX_OP_GE); break;
			default: ASSERT(false); break;
		}
		emit(stack_top);
		u32 result = stack_top++;
		emit(lhs_offset);
		emit(rhs_offset);
		emit((u8)(cmp.operand_type->kind == ResolvedTypeKind::NULLABLE ? EX_TYPE_U8 : toTypeKind(*cmp.operand_type)));
		return result;
	}

	u32 emitUnary(const ExOpUnary& op, EmitDst* dst) {
		const u32 operand = emitOperand(*op.operand);
		if (op.kind == ExIrOpKind::NEG)
			emitOp(ex_op(EX_OP_NEG_I8 + numericKindIndex(*op.operand_type)));
		else
			emitOp(EX_OP_NOT);
		const u32 result = dst ? dst->dst : stack_top;
		emit(result);
		emit(operand);
		if (!dst) stack_top += typeByteSize(*op.operand_type);
		return result;
	}

	void patchI16(u32 position, u32 target) {
		const i64 relative = (i64)target - (i64)(position + sizeof(i16));
		ASSERT(relative >= -32768 && relative <= 32767);
		const i16 offset = (i16)relative;
		memcpy(code.data + position, &offset, sizeof(offset));
	}

	// Return a zero-test branch for a comparison whose right-hand side is
	// known to be zero. The branch condition is the comparison result itself,
	// rather than the inverted condition used by fusedCompareJumpOp.
	bool zeroCompareJumpOp(ExIrOpKind condition_kind, const ResolvedType& operand_type, ex_op& out_op) {
		const bool is_i32 = operand_type.kind == ResolvedTypeKind::I32;
		const bool is_i64 = operand_type.kind == ResolvedTypeKind::I64 || operand_type.kind == ResolvedTypeKind::ISIZE;
		if (!is_i32 && !is_i64) return false;
		switch (condition_kind) {
			case ExIrOpKind::EQ: out_op = is_i32 ? EX_OP_JZ_I32 : EX_OP_JZ_I64; break;
			case ExIrOpKind::NE: out_op = is_i32 ? EX_OP_JNZ_I32 : EX_OP_JNZ_I64; break;
			case ExIrOpKind::GT: out_op = is_i32 ? EX_OP_JGZ_I32 : EX_OP_JGZ_I64; break;
			case ExIrOpKind::GE: out_op = is_i32 ? EX_OP_JGEZ_I32 : EX_OP_JGEZ_I64; break;
			case ExIrOpKind::LT: out_op = is_i32 ? EX_OP_JLTZ_I32 : EX_OP_JLTZ_I64; break;
			case ExIrOpKind::LE: out_op = is_i32 ? EX_OP_JLEZ_I32 : EX_OP_JLEZ_I64; break;
			default: return false;
		}
		return true;
	}

	// Fuse a numeric compare and the branch that follows it into a single
	// compare-and-branch opcode (e.g. `n <= 1` + `JZ_U8` -> `JGT_I32`, which
	// jumps past the body when `n > 1`). Returns false when the condition is
	// not a fusable comparison.
	bool fusedCompareJumpOp(ExIrOpKind condition_kind, const ResolvedType& operand_type, bool negate, ex_op& out_op) {
		const ExIrOpKind skip = negate ? condition_kind : invertCompare(condition_kind);
		switch (operand_type.kind) {
			case ResolvedTypeKind::I8:
			case ResolvedTypeKind::U8:
			case ResolvedTypeKind::I16:
			case ResolvedTypeKind::U16:
			case ResolvedTypeKind::I32:
			case ResolvedTypeKind::U32:
			case ResolvedTypeKind::I64:
			case ResolvedTypeKind::U64:
			case ResolvedTypeKind::ISIZE:
			case ResolvedTypeKind::F32:
			case ResolvedTypeKind::F64:
				break;
			default: return false;
		}
		u32 variant;
		switch (skip) {
			case ExIrOpKind::EQ: variant = 0; break;
			case ExIrOpKind::GE: variant = 1; break;
			case ExIrOpKind::GT: variant = 2; break;
			case ExIrOpKind::LT: variant = 3; break;
			case ExIrOpKind::LE: variant = 4; break;
			case ExIrOpKind::NE: variant = 5; break;
			default: return false;
		}
		out_op = ex_op(EX_OP_JE_I8 + numericKindIndex(operand_type) * 6u + variant);
		return true;
	}

	u32 emitConditionalJump(const ExOpConditionalJump& conditional) {
		const u32 entry_stack_top = stack_top;

		const ExIrOp* condition = conditional.condition;

		// Fold compile-time-constant conditions: either a literal or a hoisted
		// alloca initialized from one.
		const ExOpLoadConst* constant = nullptr;
		if (condition->kind == ExIrOpKind::LOAD_CONST) {
			constant = static_cast<const ExOpLoadConst*>(condition);
		} else if (condition->kind == ExIrOpKind::FRAME_PTR) {
			auto& ref = static_cast<const ExOpFramePtr&>(*condition);
			if (ref.alloca->value->kind == ExIrOpKind::LOAD_CONST)
				constant = static_cast<const ExOpLoadConst*>(ref.alloca->value);
		}
		bool is_constant = constant != nullptr;
		bool constant_value = false;
		if (is_constant) {
			bool truthy = false;
			for (u32 i = 0; i < typeByteSize(*constant->type); ++i) truthy |= constant->value[i] != 0;
			constant_value = truthy;
		}

		if (conditional.bottom_tested) {
			ASSERT(conditional.body_start);
			if (is_constant) {
				if (constant_value) {
					// Always taken: the back edge is an unconditional jump.
					emitOp(EX_OP_JUMP);
					const u32 patch_pos = code.size();
					emit((i16)0);
					patchI16(patch_pos, conditional.body_start->bytecode_offset);
				}
				// Never taken: fall through to the loop exit.
				return entry_stack_top;
			}
			// Jump back into the body while the condition holds; fall through to
			// whatever follows (the loop exit) once it fails.
			const ExOpBinary* compare = nullptr;
			if (condition->kind >= ExIrOpKind::EQ && condition->kind <= ExIrOpKind::GE) {
				compare = static_cast<const ExOpBinary*>(condition);
			}
			ex_op fused_opcode;
			const ExOpLoadConst* rhs_constant = compare && compare->rhs->kind == ExIrOpKind::LOAD_CONST
				? static_cast<const ExOpLoadConst*>(compare->rhs) : nullptr;
			bool rhs_is_zero = rhs_constant != nullptr;
			if (rhs_is_zero) {
				for (u32 i = 0; i < typeByteSize(*rhs_constant->type); ++i)
					if (rhs_constant->value[i] != 0) rhs_is_zero = false;
			}
			if (compare && rhs_is_zero && zeroCompareJumpOp(compare->kind, *compare->operand_type, fused_opcode)) {
				const u32 lhs_slot = emitOperand(*compare->lhs);
				emitOp(fused_opcode);
				emit(lhs_slot);
				const u32 patch_pos = code.size();
				emit((i16)0);
				patchI16(patch_pos, conditional.body_start->bytecode_offset);
			} else if (compare && fusedCompareJumpOp(compare->kind, *compare->operand_type, true, fused_opcode)) {
				const u32 lhs_slot = emitOperand(*compare->lhs);
				const u32 rhs_slot = emitOperand(*compare->rhs);
				emitOp(fused_opcode);
				emit(lhs_slot);
				emit(rhs_slot);
				const u32 patch_pos = code.size();
				emit((i16)0);
				patchI16(patch_pos, conditional.body_start->bytecode_offset);
			} else {
				const u32 condition_slot = emit(*conditional.condition, nullptr);
				emitOp(EX_OP_JNZ_U8);
				emit(condition_slot);
				const u32 patch_pos = code.size();
				emit((i16)0);
				patchI16(patch_pos, conditional.body_start->bytecode_offset);
			}
			// Leave stack_top at its post-condition value so emitBlock records
			// the temporary slots used by the condition evaluation.
			return entry_stack_top;
		}

		u32 false_jump = 0;
		bool have_jump = false;
		if (is_constant) {
			if (!constant_value) {
				// Always false: jump straight past the block.
				emitOp(EX_OP_JUMP);
				false_jump = code.size();
				emit((i16)0);
				have_jump = true;
			}
			// Always true: no branch - fall into the block.
		} else if (condition->kind >= ExIrOpKind::EQ && condition->kind <= ExIrOpKind::GE) {
			const ExOpBinary& compare = static_cast<const ExOpBinary&>(*condition);
			ex_op fused_opcode;
			const ExOpLoadConst* rhs_constant = compare.rhs->kind == ExIrOpKind::LOAD_CONST
				? static_cast<const ExOpLoadConst*>(compare.rhs) : nullptr;
			bool rhs_is_zero = rhs_constant != nullptr;
			if (rhs_is_zero) {
				for (u32 i = 0; i < typeByteSize(*rhs_constant->type); ++i)
					if (rhs_constant->value[i] != 0) rhs_is_zero = false;
			}
			if (rhs_is_zero && zeroCompareJumpOp(invertCompare(compare.kind), *compare.operand_type, fused_opcode)) {
				const u32 lhs_slot = emitOperand(*compare.lhs);
				emitOp(fused_opcode);
				emit(lhs_slot);
				false_jump = code.size();
				emit((i16)0);
				have_jump = true;
			} else if (fusedCompareJumpOp(compare.kind, *compare.operand_type, false, fused_opcode)) {
				const u32 lhs_slot = emitOperand(*compare.lhs);
				const u32 rhs_slot = emitOperand(*compare.rhs);
				emitOp(fused_opcode);
				emit(lhs_slot);
				emit(rhs_slot);
				false_jump = code.size();
				emit((i16)0);
				have_jump = true;
			}
		}
		if (!have_jump && !is_constant) {
			const u32 condition_slot = emit(*conditional.condition, nullptr);
			emitOp(EX_OP_JZ_U8);
			emit(condition_slot);
			false_jump = code.size();
			emit((i16)0);
			have_jump = true;
		}
		const u32 condition_peak = stack_top;

		emitBlock(*conditional.true_block);
		u32 peak = stack_top;
		if (peak < condition_peak) peak = condition_peak;
		if (!conditional.false_block) {
			if (have_jump) patchI16(false_jump, code.size());
			stack_top = peak;
			return entry_stack_top;
		}

		emitOp(EX_OP_JUMP);
		const u32 end_jump = code.size();
		emit((i16)0);
		if (have_jump) patchI16(false_jump, code.size());
		stack_top = entry_stack_top;
		emitBlock(*conditional.false_block);
		if (stack_top > peak) peak = stack_top;
		patchI16(end_jump, code.size());
		stack_top = peak;
		return entry_stack_top;
	}

	u32 emitTernary(const ExOpTernary& ternary) {
		const u32 condition = emit(*ternary.condition, nullptr);
		const u32 size = typeByteSize(*ternary.type);
		const u32 result = stack_top;
		stack_top += size;

		emitOp(EX_OP_JZ_U8);
		emit(condition);
		const u32 false_jump = code.size();
		emit((i16)0);

		const u32 true_value = emit(*ternary.true_value, nullptr);
		emitOp(EX_OP_COPY);
		emit(result);
		emit(true_value);
		emit(size);
		u32 high_water = stack_top;
		emitOp(EX_OP_JUMP);
		const u32 end_jump = code.size();
		emit((i16)0);

		patchI16(false_jump, code.size());
		stack_top = result + size;
		const u32 false_value = emit(*ternary.false_value, nullptr);
		emitOp(EX_OP_COPY);
		emit(result);
		emit(false_value);
		emit(size);
		if (stack_top > high_water) high_water = stack_top;
		patchI16(end_jump, code.size());
		stack_top = high_water;
		return result;
	}

	u32 emitJump(ExOpJump& jump) {
		ASSERT(jump.target);
		emitOp(EX_OP_JUMP);
		jump.bytecode_patch_offset = code.size();
		emit((i16)0);
		return stack_top;
	}

	u32 emitShortCircuit(const ExOpBinary& op) {
		const u32 lhs = emit(*op.lhs, nullptr);
		emitOp(op.kind == ExIrOpKind::AND ? EX_OP_JZ_U8 : EX_OP_JNZ_U8);
		emit(lhs);
		const u32 short_jump = code.size();
		emit((i16)0);

		const u32 rhs = emit(*op.rhs, nullptr);
		const u32 result = stack_top;
		stack_top++;
		emitOp(EX_OP_COPY);
		emit(result);
		emit(rhs);
		emit((u32)1);
		emitOp(EX_OP_JUMP);
		const u32 end_jump = code.size();
		emit((i16)0);

		patchI16(short_jump, code.size());
		emitOp(EX_OP_COPY);
		emit(result);
		emit(lhs);
		emit((u32)1);
		patchI16(end_jump, code.size());
		return result;
	}

	void patchJumps(ExIrBlockData& block) {
		for (ExIrOp* op : block.ops) {
			if (op->kind == ExIrOpKind::JUMP) {
				auto& jump = static_cast<ExOpJump&>(*op);
				ASSERT(jump.target);
				ASSERT(jump.target->bytecode_offset != 0xffffffffu);
				ASSERT(jump.bytecode_patch_offset != 0xffffffffu);
				patchI16(jump.bytecode_patch_offset, jump.target->bytecode_offset);
			} else if (op->kind == ExIrOpKind::CONDITIONAL_JUMP) {
				auto& conditional = static_cast<ExOpConditionalJump&>(*op);
				if (conditional.true_block) patchJumps(*conditional.true_block);
				if (conditional.false_block) patchJumps(*conditional.false_block);
			}
		}
	}

	struct FusedMultiplyMatch {
		ExIrOp* multiply_op = nullptr;
		ExIrOp* addend_op = nullptr;
		bool mul_is_lhs = false;
		ex_op op_f32 = EX_OP_MADD_F32;
	};

	static bool unwrapMultiply(ExIrOp& op, ExIrOp*& multiply, bool& negated) {
		negated = false;
		if (op.kind == ExIrOpKind::MUL) {
			multiply = &op;
			return true;
		}
		if (op.kind == ExIrOpKind::NEG && static_cast<ExOpNeg&>(op).operand->kind == ExIrOpKind::MUL) {
			multiply = static_cast<ExOpNeg&>(op).operand;
			negated = true;
			return true;
		}
		return false;
	}

	u32 emitFusedMultiply(const ExOpBinary& outer, const FusedMultiplyMatch& match, EmitDst* dst) {
		const auto& multiply = static_cast<const ExOpBinary&>(*match.multiply_op);
		// Keep source evaluation order even when the multiply is on the RHS.
		const u32 addend = match.mul_is_lhs ? 0 : emitOperand(*match.addend_op);
		const u32 lhs = emitOperand(*multiply.lhs);
		const u32 rhs = emitOperand(*multiply.rhs);
		const u32 ordered_addend = match.mul_is_lhs ? emitOperand(*match.addend_op) : addend;
		const u32 size = typeByteSize(*outer.operand_type);
		const u32 result = dst ? dst->dst : stack_top;
		emitOp((ex_op)((u32)match.op_f32 + (outer.operand_type->kind == ResolvedTypeKind::F64 ? 1u : 0u)));
		emit(result);
		emit(lhs);
		emit(rhs);
		emit(ordered_addend);
		if (!dst) stack_top += size;
		return result;
	}

	u32 emitBinary(ex_op base_op, const ExOpBinary& ir_op, EmitDst* dst) {
		FusedMultiplyMatch fused;
		if (do_optimize && (ir_op.kind == ExIrOpKind::ADD || ir_op.kind == ExIrOpKind::SUB) 
			&& (ir_op.operand_type->kind == ResolvedTypeKind::F32 || ir_op.operand_type->kind == ResolvedTypeKind::F64))
		{
			ExIrOp* multiply_lhs = nullptr;
			bool negated_lhs = false;
			ExIrOp* multiply_rhs = nullptr;
			bool negated_rhs = false;
			const bool has_lhs_mul = unwrapMultiply(*ir_op.lhs, multiply_lhs, negated_lhs);
			const bool has_rhs_mul = unwrapMultiply(*ir_op.rhs, multiply_rhs, negated_rhs);
			if (has_lhs_mul || has_rhs_mul) {
				fused.mul_is_lhs = has_lhs_mul;
				fused.multiply_op = has_lhs_mul ? multiply_lhs : multiply_rhs;
				const bool sub = ir_op.kind == ExIrOpKind::SUB;
				const bool product_negated = has_lhs_mul ? negated_lhs : negated_rhs;
				const bool product_positive = (fused.mul_is_lhs || !sub) != product_negated;
				const bool addend_negated = has_lhs_mul ? negated_rhs : negated_lhs;
				const bool addend_positive = (!fused.mul_is_lhs || !sub) != addend_negated;
				fused.addend_op = has_lhs_mul
					? (has_rhs_mul ? multiply_rhs : ir_op.rhs)
					: ir_op.lhs;
				fused.op_f32 = product_positive
					? (addend_positive ? EX_OP_MADD_F32 : EX_OP_MSUB_F32)
					: (addend_positive ? EX_OP_NMADD_F32 : EX_OP_NMSUB_F32);
				return emitFusedMultiply(ir_op, fused, dst);
			}
		}
		u32 lhs = emitOperand(*ir_op.lhs);
		const u8* immediate = nullptr;
		if (ir_op.rhs->kind == ExIrOpKind::LOAD_CONST) immediate = static_cast<const ExOpLoadConst&>(*ir_op.rhs).value;
		if (ir_op.rhs->kind == ExIrOpKind::LOAD_BYTES) immediate = static_cast<const ExOpLoadBytes&>(*ir_op.rhs).value;
		if (immediate) {
			const ResolvedType* rhs_type = nullptr;
			if (ir_op.rhs->kind == ExIrOpKind::LOAD_CONST) rhs_type = static_cast<const ExOpLoadConst&>(*ir_op.rhs).type;
			else rhs_type = static_cast<const ExOpLoadBytes&>(*ir_op.rhs).type;

			const u32 size = typeByteSize(*ir_op.operand_type);
			if (rhs_type && rhs_type->kind == ir_op.operand_type->kind && typeByteSize(*rhs_type) == size) {
				u64 immediate_value = 0;
				memcpy(&immediate_value, immediate, size);
				// A zero-offset address addition is a no-op: address registers
				// are read-only, so the base operand already holds the address.
				if (ir_op.result_mode == ExIrOp::ADDRESS && ir_op.kind == ExIrOpKind::ADD && immediate_value == 0u) {
					return lhs;
				}
				const bool increment = ir_op.kind == ExIrOpKind::ADD && immediate_value == 1u;
				const bool decrement = ir_op.kind == ExIrOpKind::SUB && immediate_value == 1u;
				if (dst && dst->dst == lhs && (increment || decrement) &&
					((size == 4 && (ir_op.operand_type->kind == ResolvedTypeKind::I32 || ir_op.operand_type->kind == ResolvedTypeKind::U32)) ||
					 (size == 8 && (ir_op.operand_type->kind == ResolvedTypeKind::I64 || ir_op.operand_type->kind == ResolvedTypeKind::U64 || ir_op.operand_type->kind == ResolvedTypeKind::ISIZE)))) {
					emitOp(size == 4 ? (increment ? EX_OP_INC_I32 : EX_OP_DEC_I32) : (increment ? EX_OP_INC_I64 : EX_OP_DEC_I64));
					emit(lhs);
					return lhs;
				}
				emitOp(immediateArithmeticOp(ir_op.kind, *ir_op.operand_type));
				const u32 ret = dst ? dst->dst : stack_top;
				emit(ret);
				emit(lhs);
				emit(immediate, size);
				if (!dst) stack_top += size;
				return ret;
			}
		}
		u32 rhs = emitOperand(*ir_op.rhs);
		emitOp(base_op == EX_OP_ADD_8 || base_op == EX_OP_SUB_8 || base_op == EX_OP_MUL_8
			? arithmeticOp(ir_op.kind, *ir_op.operand_type)
			: ex_op(base_op + numericKindIndex(*ir_op.operand_type)));
		u32 ret = dst ? dst->dst : stack_top;
		emit(ret);
		emit(lhs);
		emit(rhs);
		if (!dst) stack_top += typeByteSize(*ir_op.operand_type);
		return ret;
	}

	u32 emitLoadConst(const ExOpLoadConst& load, EmitDst* dst) {
		u32 size = typeByteSize(*load.type);
		u8 value[8];
		memcpy(value, load.value, sizeof(value));
		if (load.represented_type) {
			const u32 type_index = type_info.internType(*load.represented_type);
			memcpy(value, &type_index, sizeof(type_index));
		}
		const u8* load_value = load.represented_type ? value : load.value;
		u32 res = dst ? dst->dst : stack_top;
		switch (size) {
			case 1: {
				emitOp(EX_OP_LOAD_CONST_1);
				emit(res);
				if (!dst) stack_top += 1;
				emit(load_value, 1);
				return res;
			}
			case 2: {
				emitOp(EX_OP_LOAD_CONST_2);
				emit(res);
				if (!dst) stack_top += 2;
				emit(load_value, 2);
				return res;
			}
			case 4: {
				emitOp(EX_OP_LOAD_CONST_4);
				emit(res);
				if (!dst) stack_top += 4;
				emit(load_value, 4);
				return res;
			}
			case 8: {
				emitOp(EX_OP_LOAD_CONST_8);
				emit(res);
				if (!dst) stack_top += 8;
				emit(load_value, 8);
				return res;
			}
		}
		ASSERT(false);
		return 0xffFFffFF;
	}

	u32 emitLoadBytes(const ExOpLoadBytes& load, EmitDst* dst) {
		const u32 result = dst ? dst->dst : stack_top;
		u32 iter = result;
		u32 offset = 0;
		while (offset < load.size) {
			const u32 remaining = load.size - offset;
			const u32 size = remaining >= 8 ? 8u : remaining >= 4 ? 4u : remaining >= 2 ? 2u : 1u;
			emitOp(size == 8 ? EX_OP_LOAD_CONST_8 : size == 4 ? EX_OP_LOAD_CONST_4 : size == 2 ? EX_OP_LOAD_CONST_2 : EX_OP_LOAD_CONST_1);
			emit(iter);
			emit(load.value + offset, size);
			iter += size;
			offset += size;
		}
		if (!dst) stack_top = iter;
		return result;
	}

	u32 emitAlloca(ExOpAlloca& alloca) {
		ASSERT(alloca.stack_sp != 0xffFFffFF);
		if (alloca.value->kind != ExIrOpKind::NOP) {
			if (do_optimize && alloca.value->kind != ExIrOpKind::AGGREGATE_INIT) {
				EmitDst dst = { .dst = alloca.stack_sp, .size = typeByteSize(*alloca.type) };
				const u32 value_slot = emit(*alloca.value, &dst);
				if (!empty(alloca.name)) recordLocal(alloca.name, alloca.stack_sp, *alloca.type, (u32)code.size());
			}
			else {
				const u32 value_slot = emit(*alloca.value, nullptr);
				emitOp(EX_OP_COPY);
				emit(alloca.stack_sp);
				emit(value_slot);
				emit(typeByteSize(*alloca.type));
				if (!empty(alloca.name)) recordLocal(alloca.name, alloca.stack_sp, *alloca.type, (u32)code.size());
			}
		} else {
			if (!empty(alloca.name)) recordLocal(alloca.name, alloca.stack_sp, *alloca.type, alloca.bytecode_offset);
		}
		const u32 slot_end = alloca.stack_sp + typeByteSize(*alloca.type);
		if (stack_top < slot_end) stack_top = slot_end;
		return alloca.stack_sp;
	}

	void recordLocal(ex_string_view name, u32 offset, const ResolvedType& type, u32 scope_begin_offset) {
		ex_bytecode_local_debug_entry& entry = local_debug.emplace_back();
		entry.name = copyStringViewToArena(host.arena, name);
		entry.offset = offset;
		entry.byte_size = typeByteSize(type);
		entry.kind = debugTypeKind(type);
		entry.type_index = type_info.internType(type);
		entry.scope_begin_offset = scope_begin_offset;
	}

	static ex_op indexedLoadOp(ex_type_kind kind) {
		switch (kind) {
			case EX_TYPE_I8:
			case EX_TYPE_U8:
			case EX_TYPE_BOOL: return EX_OP_LOAD_INDEXED_8;
			case EX_TYPE_I16:
			case EX_TYPE_U16: return EX_OP_LOAD_INDEXED_16;
			case EX_TYPE_I32:
			case EX_TYPE_U32:
			case EX_TYPE_ENUM: return EX_OP_LOAD_INDEXED_32;
			case EX_TYPE_I64:
			case EX_TYPE_U64: return EX_OP_LOAD_INDEXED_64;
			default: ASSERT(false); return EX_OP_LOAD_INDEXED_8;
		}
	}

	static ex_op indexedStoreOp(ex_type_kind kind) {
		return (ex_op)((u32)EX_OP_STORE_INDEXED_8 + ((u32)indexedLoadOp(kind) - (u32)EX_OP_LOAD_INDEXED_8));
	}

	struct IndexedAddress {
		ExIrOp* base = nullptr;
		ExIrOp* index = nullptr;
		ex_type_kind index_kind = EX_TYPE_INVALID;
		u64 length = 0;
		u32 element_size = 0;
		u32 base_slot = 0;
		u32 index_slot = 0;
		bool index_immediate = false;
		u64 immediate_index = 0;
	};

	bool tryGetIndexedImmediate(const ExIrOp& index, ex_type_kind kind, u64& result) {
		if (index.kind != ExIrOpKind::LOAD_CONST) return false;
		const auto& constant = static_cast<const ExOpLoadConst&>(index);
		switch (kind) {
			case EX_TYPE_I8: { i8 value; memcpy(&value, constant.value, sizeof(value)); if (value < 0) return false; result = (u64)value; return true; }
			case EX_TYPE_I16: { i16 value; memcpy(&value, constant.value, sizeof(value)); if (value < 0) return false; result = (u64)value; return true; }
			case EX_TYPE_I32: { i32 value; memcpy(&value, constant.value, sizeof(value)); if (value < 0) return false; result = (u64)value; return true; }
			case EX_TYPE_I64: { i64 value; memcpy(&value, constant.value, sizeof(value)); if (value < 0) return false; result = (u64)value; return true; }
			case EX_TYPE_ENUM:
			case EX_TYPE_U8:
			case EX_TYPE_U16:
			case EX_TYPE_U32:
			case EX_TYPE_U64:
			case EX_TYPE_BOOL:
				memcpy(&result, constant.value, sizeof(result));
				return true;
			default: return false;
		}
	}

	static u32 arithmeticKindIndex(const ResolvedType& type) {
		switch (type.kind) {
			case ResolvedTypeKind::I8:
			case ResolvedTypeKind::U8: return 0;
			case ResolvedTypeKind::I16:
			case ResolvedTypeKind::U16: return 1;
			case ResolvedTypeKind::I32:
			case ResolvedTypeKind::U32: return 2;
			case ResolvedTypeKind::I64:
			case ResolvedTypeKind::ISIZE:
			case ResolvedTypeKind::U64: return 3;
			case ResolvedTypeKind::F32: return 4;
			case ResolvedTypeKind::F64: return 5;
			default: ASSERT(false); return -1;
		}
	}

	// Matches an address formed as base + constant byte offset (the shape
	// produced by struct field access through pointers). The constant folds
	// into LOAD_PTR/STORE_PTR's immediate offset operand.
	bool tryGetOffsetAddress(const ExIrOp& address, ExIrOp*& base, u32& offset) {
		if (address.kind != ExIrOpKind::ADD) return false;
		const auto& add = static_cast<const ExOpBinary&>(address);
		if (!add.rhs || add.rhs->kind != ExIrOpKind::LOAD_CONST) return false;
		const auto& constant = static_cast<const ExOpLoadConst&>(*add.rhs);
		if (!constant.type || constant.type->kind != ResolvedTypeKind::U64) return false;
		u64 value = 0;
		memcpy(&value, constant.value, sizeof(value));
		if (value > 0xffffffffu) return false;
		base = add.lhs;
		offset = (u32)value;
		return true;
	}

	bool tryGetDirectFrameAddress(const ExIrOp& address, u32& offset) {
		if (address.kind == ExIrOpKind::PUSH_LOCAL_ADDR) {
			offset = static_cast<const ExOpPushLocalAddr&>(address).alloca->stack_sp;
			return true;
		}
		ExIrOp* base = nullptr;
		u32 field_offset = 0;
		if (tryGetOffsetAddress(address, base, field_offset) && base->kind == ExIrOpKind::PUSH_LOCAL_ADDR) {
			offset = static_cast<ExOpPushLocalAddr*>(base)->alloca->stack_sp + field_offset;
			return true;
		}
		return false;
	}

	bool tryGetIndexedAddress(ExIrOp& address, IndexedAddress& result) {
		if (address.kind != ExIrOpKind::ADD) return false;
		auto& add = static_cast<ExOpBinary&>(address);
		if (!add.rhs || add.rhs->kind != ExIrOpKind::MUL) return false;
		auto& mul = static_cast<ExOpBinary&>(*add.rhs);
		if (!mul.rhs || mul.rhs->kind != ExIrOpKind::LOAD_CONST) return false;
		auto& size = static_cast<ExOpLoadConst&>(*mul.rhs);
		if (!size.type || size.type->kind != ResolvedTypeKind::U64) return false;
		u64 element_size = 0;
		memcpy(&element_size, size.value, sizeof(element_size));
		if (element_size > 0xffffffffu) return false;

		ExIrOp* checked = mul.lhs;
		if (!checked) return false;
		if (checked->kind == ExIrOpKind::CAST) checked = static_cast<ExOpCast&>(*checked).value;
		if (!checked || checked->kind != ExIrOpKind::BOUNDS_CHECK) return false;
		auto& bounds = static_cast<ExOpBoundsCheck&>(*checked);
		result.base = add.lhs;
		if (result.base->kind != ExIrOpKind::PUSH_LOCAL_ADDR && result.base->kind != ExIrOpKind::FRAME_PTR) return false;
		result.index = bounds.index;
		result.index_kind = toTypeKind(*bounds.index_type);
		result.index_immediate = tryGetIndexedImmediate(*result.index, result.index_kind, result.immediate_index);
		result.length = bounds.length;
		result.element_size = (u32)element_size;
		return true;
	}

	u32 emitIndexedAddress(IndexedAddress& address) {
		if (address.base->kind == ExIrOpKind::PUSH_LOCAL_ADDR) {
			address.base_slot = static_cast<ExOpPushLocalAddr&>(*address.base).alloca->stack_sp;
		} else {
			address.base_slot = static_cast<ExOpFramePtr&>(*address.base).alloca->stack_sp;
		}
		if (!address.index_immediate) address.index_slot = emit(*address.index, nullptr);
		return address.index_slot;
	}

	u32 indexedImmediateOffset(const IndexedAddress& address) {
		// The checker validates constant indices against the static array
		// length (compiler.cpp checkBracketExpr), so base, index and element
		// size fold into a single frame offset with no bounds checking.
		const u64 offset = address.base_slot + address.immediate_index * address.element_size;
		ASSERT(offset <= 0xffFFffFFu);
		return (u32)offset;
	}

	u32 emitSliceFieldCopy(const ExOpCopy& copy, ExIrOp& slice_op, ExIrOp& index_op, const ResolvedType& index_type,
		u32 element_size, u32 field_offset, u32 field_size)
	{
		const u32 src = emit(*copy.src, nullptr);
		const u32 slice = emitOperand(slice_op);
		const u32 index = emitOperand(index_op);
		emitOp(sliceFieldStoreOp(toTypeKind(index_type)));
		emit(slice);
		emit(index);
		emit(element_size);
		emit(field_offset);
		emit(field_size);
		emit(src);
		return slice;
	}

	u32 emitCopy(const ExOpCopy& copy) {
		if (copy.dst->kind == ExIrOpKind::SLICE_FIELD_STORE) {
			auto& field = static_cast<ExOpSliceFieldStore&>(*copy.dst);
			return emitSliceFieldCopy(copy, *field.slice, *field.index, *field.index_type,
				field.element_size, field.field_offset, field.field_size);
		}

		u32 frame_offset = 0;
		const bool direct_frame = copy.dst->result_mode == ExIrOp::ADDRESS && tryGetDirectFrameAddress(*copy.dst, frame_offset);
		if (do_optimize && direct_frame) {
			EmitDst dst = { frame_offset, typeByteSize(*copy.type) };
			emit(*copy.src, &dst);
			return dst.dst;
		}
		IndexedAddress indexed;
		if (copy.dst->result_mode == ExIrOp::ADDRESS && tryGetIndexedAddress(*copy.dst, indexed)) {
			u32 src = emit(*copy.src, nullptr);
			emitIndexedAddress(indexed);
			if (indexed.index_immediate) {
				emitOp(EX_OP_COPY);
				emit(indexedImmediateOffset(indexed));
				emit(src);
				emit(indexed.element_size);
			} else {
				emitOp(indexedStoreOp(indexed.index_kind));
				ASSERT(!indexed.index_immediate);
				// The runtime scales the bounds-checked index by element_size
				// without an overflow check, so the whole array's scaled extent
				// must stay within the u32 frame-offset space.
				ASSERT((u64)indexed.length * indexed.element_size + indexed.base_slot <= 0xffFFffFFu);
				emit(indexed.base_slot);
				emit(indexed.index_slot);
				emit(indexed.length);
				emit(indexed.element_size);
				emit(src);
			}
			return indexed.base_slot;
		}
		u32 src = emit(*copy.src, nullptr);
		if (direct_frame) {
			emitOp(EX_OP_COPY);
			emit(frame_offset);
			emit(src);
			emit(typeByteSize(*copy.type));
			return frame_offset;
		}
		u32 dst = emit(*copy.dst, nullptr);
		switch (copy.dst->result_mode) {
			case ExIrOp::ADDRESS: {
				emitOp(EX_OP_STORE_PTR);
				emit(dst);
				emit(u32(0));
				emit(src);
				emit(typeByteSize(*copy.type));
				return dst;
			}
			case ExIrOp::VALUE: {
				emitOp(EX_OP_COPY);
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

	u32 emitExtractValue(const ExOpExtractValue& op, EmitDst* dst) {
		const u32 value = emitOperand(*op.value);
		const u32 ret = dst ? dst->dst : stack_top;
		emitOp(EX_OP_COPY);
		emit(ret);
		emit(value + op.offset);
		emit(op.size);
		if (!dst) stack_top += op.size;
		return ret;
	}

	u32 emitUnionConvert(const ExOpUnionConvert& op) {
		const UnionResolvedType& source = static_cast<const UnionResolvedType&>(*op.source_type);
		const UnionResolvedType& dest = static_cast<const UnionResolvedType&>(*op.target_type);
		// Evaluate the source once: tag remap and payload copy both need it.
		const u32 src = emitOperand(*op.value);
		const u32 dest_size = typeByteSize(*op.target_type);
		const u32 src_size = typeByteSize(*op.source_type);
		const u32 result = stack_top;
		stack_top += dest_size;

		const u32 src_payload = src_size > sizeof(i32) ? src_size - (u32)sizeof(i32) : 0;
		const u32 dest_payload = dest_size > sizeof(i32) ? dest_size - (u32)sizeof(i32) : 0;
		const u32 payload_size = src_payload < dest_payload ? src_payload : dest_payload;
		if (payload_size) {
			emitOp(EX_OP_COPY);
			emit(result + (u32)sizeof(i32));
			emit(src + (u32)sizeof(i32));
			emit(payload_size);
		}

		// Copy the source tag, then overwrite it when dest uses a different index.
		emitOp(EX_OP_COPY);
		emit(result);
		emit(src);
		emit((u32)sizeof(i32));

		bool needs_remap = false;
		for (i32 i = 0; i < source.members.size(); ++i) {
			const i32 dest_tag = unionMemberIndex(dest, source.members[i]);
			if (dest_tag >= 0 && dest_tag != i) { needs_remap = true; break; }
		}
		if (!needs_remap) return result;

		const u32 expected_sp = stack_top;
		stack_top += sizeof(i32);
		const u32 cond_sp = stack_top++;
		for (i32 i = 0; i < source.members.size(); ++i) {
			const i32 dest_tag = unionMemberIndex(dest, source.members[i]);
			if (dest_tag < 0 || dest_tag == i) continue;

			emitOp(EX_OP_LOAD_CONST_4);
			emit(expected_sp);
			emit((i32)i);
			emitOp(EX_OP_EQ);
			emit(cond_sp);
			emit(src);
			emit(expected_sp);
			emit((u8)EX_TYPE_I32);
			emitOp(EX_OP_JZ_U8);
			emit(cond_sp);
			const u32 skip = code.size();
			emit((i16)0);
			emitOp(EX_OP_LOAD_CONST_4);
			emit(result);
			emit(dest_tag);
			patchI16(skip, code.size());
		}
		return result;
	}

	u32 emitAggregateInit(const ExOpAggregateInit& aggregate) {
		const u32 result = stack_top;
		stack_top += typeByteSize(*aggregate.type);
		u32 offset = 0;
		for (u32 i = 0; i < aggregate.value_count; ++i) {
			const u32 value = emit(*aggregate.values[i], nullptr);
			const u32 size = aggregate.sizes								   ? aggregate.sizes[i]
							 : aggregate.type->kind == ResolvedTypeKind::ARRAY ? typeByteSize(*static_cast<ArrayResolvedType*>(aggregate.type)->element_type)
																			   : (u32)sizeof(u64);
			emitOp(EX_OP_COPY);
			emit(result + (aggregate.offsets ? aggregate.offsets[i] : offset));
			emit(value);
			emit(size);
			offset += size;
		}
		return result;
	}

	static ex_type_kind toTypeKind(const ResolvedType& type) {
		switch (type.kind) {
			case ResolvedTypeKind::VOID: return EX_TYPE_VOID;
			case ResolvedTypeKind::BOOL: return EX_TYPE_BOOL;
			case ResolvedTypeKind::I8: return EX_TYPE_I8;
			case ResolvedTypeKind::I16: return EX_TYPE_I16;
			case ResolvedTypeKind::I32: return EX_TYPE_I32;
			case ResolvedTypeKind::I64: return EX_TYPE_I64;
			case ResolvedTypeKind::UNTYPED_INT: return EX_TYPE_I64;
			case ResolvedTypeKind::UNTYPED_FLOAT: return EX_TYPE_F64;
			case ResolvedTypeKind::U8: return EX_TYPE_U8;
			case ResolvedTypeKind::U16: return EX_TYPE_U16;
			case ResolvedTypeKind::U32: return EX_TYPE_U32;
			case ResolvedTypeKind::U64: return EX_TYPE_U64;
			case ResolvedTypeKind::ISIZE: return EX_TYPE_I64;
			case ResolvedTypeKind::F32: return EX_TYPE_F32;
			case ResolvedTypeKind::F64: return EX_TYPE_F64;
			case ResolvedTypeKind::CSTR: return EX_TYPE_CPTR;
			case ResolvedTypeKind::CPTR: return EX_TYPE_CPTR;
			case ResolvedTypeKind::POINTER: return EX_TYPE_CPTR;
			case ResolvedTypeKind::BYTE: return EX_TYPE_U8;
			case ResolvedTypeKind::ANY: return EX_TYPE_ANY;
			case ResolvedTypeKind::FUNCTION: return EX_TYPE_FUNCTION;
			case ResolvedTypeKind::ARRAY: return EX_TYPE_ARRAY;
			case ResolvedTypeKind::SLICE: return EX_TYPE_SLICE;
			case ResolvedTypeKind::NULLABLE: return EX_TYPE_NULL_VALUE;
			case ResolvedTypeKind::ENUM: return EX_TYPE_ENUM;
			case ResolvedTypeKind::STRUCT: return EX_TYPE_STRUCT;
			case ResolvedTypeKind::UNION: return EX_TYPE_TAGGED_UNION;
			default: return EX_TYPE_INVALID;
		}
	}

	static ex_op sliceFieldLoadOp(ex_type_kind kind) {
		switch (kind) {
			case EX_TYPE_I8: case EX_TYPE_U8: case EX_TYPE_BOOL: return EX_OP_SLICE_LOAD_8;
			case EX_TYPE_I16: case EX_TYPE_U16: return EX_OP_SLICE_LOAD_16;
			case EX_TYPE_I32: case EX_TYPE_U32: case EX_TYPE_F32: case EX_TYPE_ENUM: return EX_OP_SLICE_LOAD_32;
			case EX_TYPE_I64: case EX_TYPE_U64: case EX_TYPE_F64: return EX_OP_SLICE_LOAD_64;
			default: ASSERT(false); return EX_OP_SLICE_LOAD_32;
		}
	}

	static ex_op sliceFieldStoreOp(ex_type_kind kind) {
		switch (kind) {
			case EX_TYPE_I8: case EX_TYPE_U8: case EX_TYPE_BOOL: return EX_OP_SLICE_STORE_8;
			case EX_TYPE_I16: case EX_TYPE_U16: return EX_OP_SLICE_STORE_16;
			case EX_TYPE_I32: case EX_TYPE_U32: case EX_TYPE_F32: case EX_TYPE_ENUM: return EX_OP_SLICE_STORE_32;
			case EX_TYPE_I64: case EX_TYPE_U64: case EX_TYPE_F64: return EX_OP_SLICE_STORE_64;
			default: ASSERT(false); return EX_OP_SLICE_STORE_32;
		}
	}

	u32 emitBoundsCheck(const ExOpBoundsCheck& op) {
		const u32 index = emitOperand(*op.index);
		emitOp(EX_OP_BOUNDS_CHECK);
		emit(index);
		emit((u8)toTypeKind(*op.index_type));
		emit(op.length);
		return index;
	}

	bool tryGetDirectFrameValue(const ExIrOp& value, u32& offset) {
		// A local/parameter value whose bytes live at a static frame offset:
		// a plain load, its constant-offset struct field, or the folded
		// frame-slot reference optimize() rewrites plain loads into.
		if (value.kind == ExIrOpKind::FRAME_PTR) {
			offset = static_cast<const ExOpFramePtr&>(value).alloca->stack_sp;
			return true;
		}
		if (value.kind != ExIrOpKind::LOAD) return false;
		const auto& load = static_cast<const ExOpLoad&>(value);
		return tryGetDirectFrameAddress(*load.addr, offset);
	}

	// Slot an operand's value already lives in when nothing needs to be
	// emitted for it; reads straight from locals'/parameters' frame bytes,
	// including their struct fields. Falls back to a full emission.
	u32 emitOperand(ExIrOp& op) {
		u32 slot = 0;
		if (tryGetDirectFrameValue(op, slot)) return slot;
		return emit(op, nullptr);
	}

	u32 emitCast(const ExOpCast& cast, EmitDst* dst) {
		u32 value_sp = 0;
		if (!tryGetDirectFrameValue(*cast.value, value_sp)) value_sp = emit(*cast.value, nullptr);
		if (cast.target_type->kind == ResolvedTypeKind::SLICE && cast.type->kind == ResolvedTypeKind::SLICE) {
			auto& source = static_cast<SliceResolvedType&>(*cast.target_type);
			auto& target = static_cast<SliceResolvedType&>(*cast.type);
			static ResolvedType i64_type(ResolvedTypeKind::I64);
			// Materialize the destination before calculating the rescaled length. This
			// keeps the reinterpretation independent of temporary lifetime/overlap.
			const u32 result = stack_top;
			stack_top += typeByteSize(*cast.type);
			emitOp(EX_OP_COPY);
			emit(result);
			emit(value_sp);
			emit((u32)sizeof(void*));
			u32 length_sp = stack_top;
			emitOp(EX_OP_COPY);
			emit(length_sp);
			emit((u32)(value_sp + sizeof(void*)));
			emit((u32)sizeof(i64));
			stack_top += sizeof(i64);
			const u32 source_size = typeByteSize(*source.element_type);
			const u32 target_size = typeByteSize(*target.element_type);
			if (source_size != target_size) {
				auto& ratio = alloc<ExOpLoadConst>();
				ratio.type = &i64_type;
				const bool shrink = target_size >= source_size;
				const i64 factor = shrink ? (source_size ? (i64)target_size / source_size : 1) : (target_size ? (i64)source_size / target_size : 1);
				memcpy(ratio.value, &factor, sizeof(factor));
				const u32 ratio_sp = emit(static_cast<ExIrOp&>(ratio), nullptr);
				const u32 scaled_sp = stack_top;
				emitOp(shrink ? EX_OP_DIV_I64 : EX_OP_MUL_64);
				emit(scaled_sp);
				emit(length_sp);
				emit(ratio_sp);
				stack_top += sizeof(i64);
				length_sp = scaled_sp;
			}
			emitOp(EX_OP_COPY);
			emit((u32)(result + sizeof(void*)));
			emit(length_sp);
			emit((u32)sizeof(i64));
			return result;
		}
		const u32 result = dst ? dst->dst : stack_top;
		emitOp(EX_OP_CAST);
		emit(result);
		emit(value_sp);
		emit((u8)toTypeKind(*cast.target_type));
		emit((u8)toTypeKind(*cast.type));

		if (!dst) stack_top += typeByteSize(*cast.type);
		return result;
	}

	u32 emitPushLocalAddr(const ExOpPushLocalAddr& op) {
		ASSERT(op.alloca->stack_sp != 0xffFFffFF);
		emitOp(EX_OP_FRAME_PTR);
		emit(stack_top);
		emit(op.alloca->stack_sp);
		stack_top += sizeof(void*);
		return stack_top - sizeof(void*);
	}

	u32 emitMaterializeAddr(const ExOpMaterializeAddr& op) {
		const u32 value_sp = emit(*op.value, nullptr);
		emitOp(EX_OP_FRAME_PTR);
		emit(stack_top);
		emit(value_sp);
		stack_top += sizeof(void*);
		return stack_top - sizeof(void*);
	}

	u32 emitLoad(const ExOpLoad& op, EmitDst* dst) {
		IndexedAddress indexed;
		if (tryGetIndexedAddress(*op.addr, indexed)) {
			const u32 ret = dst ? dst->dst : stack_top;
			emitIndexedAddress(indexed);
			if (indexed.index_immediate) {
				emitOp(EX_OP_COPY);
				emit(ret);
				emit(indexedImmediateOffset(indexed));
				emit(indexed.element_size);
			} else {
				emitOp(indexedLoadOp(indexed.index_kind));
				emit(ret);
				ASSERT(!indexed.index_immediate);
				// Same invariant as the STORE_INDEXED site in emitCopy.
				ASSERT((u64)indexed.length * indexed.element_size + indexed.base_slot <= 0xffFFffFFu);
				emit(indexed.base_slot);
				emit(indexed.index_slot);
				emit(indexed.length);
				emit(indexed.element_size);
			}
			if (!dst) stack_top += op.size;
			return ret;
		}
		ExIrOp* address_base = op.addr;
		u32 address_offset = 0;
		tryGetOffsetAddress(*op.addr, address_base, address_offset);
		u32 frame_offset = 0;
		if (tryGetDirectFrameAddress(*op.addr, frame_offset)) {
			const u32 ret = dst ? dst->dst : stack_top;
			emitOp(EX_OP_COPY);
			emit(ret);
			emit(frame_offset);
			emit(op.size);
			if (!dst) stack_top += op.size;
			return ret;
		}
		u32 addr_sp = 0;
		if (!tryGetDirectFrameValue(*address_base, addr_sp)) addr_sp = emit(*address_base, nullptr);
		emitOp(EX_OP_LOAD_PTR);
		u32 ret = dst ? dst->dst : stack_top;
		emit(ret);
		emit(addr_sp);
		emit(address_offset);
		emit(op.size);
		if (!dst) stack_top += op.size;
		return ret;
	}

	u32 emitPushGlobalAddr(const ExOpPushGlobalAddr& op) {
		emitOp(EX_OP_GLOBAL_PTR);
		u32 ret = stack_top;
		emit(ret);
		emit(op.offset);
		stack_top += sizeof(void*);
		return ret;
	}

	static bool canForward(const ExIrOp& op) {
		switch (op.kind) {
			case ExIrOpKind::LOAD_CONST:
			case ExIrOpKind::LOAD_BYTES:
			case ExIrOpKind::FRAME_PTR:
				return true;
			case ExIrOpKind::LOAD: {
				const auto& load = static_cast<const ExOpLoad&>(op);
				return load.addr->kind == ExIrOpKind::FRAME_PTR || load.addr->kind == ExIrOpKind::PUSH_LOCAL_ADDR || load.addr->kind == ExIrOpKind::PUSH_GLOBAL_ADDR;
			}
			case ExIrOpKind::NEG:
			case ExIrOpKind::NOT:
				return canForward(*static_cast<const ExOpUnary&>(op).operand);
			case ExIrOpKind::CAST:
				return canForward(*static_cast<const ExOpCast&>(op).value);
			case ExIrOpKind::BOUNDS_CHECK:
				return canForward(*static_cast<const ExOpBoundsCheck&>(op).index);
			case ExIrOpKind::ADD:
			case ExIrOpKind::SUB:
			case ExIrOpKind::MUL:
			case ExIrOpKind::DIV:
			case ExIrOpKind::MOD:
			case ExIrOpKind::EQ:
			case ExIrOpKind::NE:
			case ExIrOpKind::LT:
			case ExIrOpKind::LE:
			case ExIrOpKind::GT:
			case ExIrOpKind::GE: {
				const auto& binary = static_cast<const ExOpBinary&>(op);
				return canForward(*binary.lhs) && canForward(*binary.rhs);
			}
			default:
				return false;
		}
	}

	static bool canForward(const ExOpCallDirect& call) {
		for (u32 i = 0; i < call.arg_count; ++i) {
			if (!canForward(*call.args[i])) {
				return false;
			}
		}
		return true;
	}

	u32 emitCallDirect(const ExOpCallDirect& call) {
		u32* args = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * call.arg_count, alignof(u32)));
		u32 args_base = stack_top;
		if (do_optimize && canForward(call)) {
			u32 total_arg_size = 0;
			for (u32 i = 0; i < call.arg_count; ++i) {
				FunctionParam& param = call.function->params[i];
				if (param.is_comptime) continue;
				
				total_arg_size += typeByteSize(*param.resolved_type);
			}
			stack_top += total_arg_size;

			u32 arg_sp = args_base;
			u32 param_index = 0;
			for (u32 i = 0; i < call.arg_count; ++i) {
				while (call.function->params[param_index].is_comptime) ++param_index;
				EmitDst dst = { arg_sp, typeByteSize(*call.function->params[param_index].resolved_type) };
				arg_sp += dst.size;
				++param_index;
				args[i] = emit(*call.args[i], &dst);
			}
		}
		else {
			for (u32 i = 0; i < call.arg_count; ++i) args[i] = emit(*call.args[i], nullptr);
			args_base = stack_top;
			u32 param_index = 0;
			for (u32 i = 0; i < call.arg_count; ++i) {
				while (call.function->params[param_index].is_comptime) ++param_index;
				const u32 size = typeByteSize(*call.function->params[param_index].resolved_type);
				++param_index;
				emitOp(EX_OP_COPY);
				emit(stack_top);
				emit(args[i]);
				emit(size);
				stack_top += size;
			}
		}

		if (stack_top > stack_high_water) stack_high_water = stack_top;
		emitOp(call.function->is_extern ? EX_OP_CALL_NATIVE : EX_OP_CALL_DIRECT);
		emit(call.function->bytecode_index);
		emit(args_base);
		stack_top = args_base + call.return_size;
		return args_base;
	}

	u32 emitCallIndirect(const ExOpCallIndirect& call) {
		const u32 callee = emit(*call.callee, nullptr);
		const u32 arg_base = callee + 4u;
		u32* args = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * call.arg_count, alignof(u32)));
		u32 arg_size = 0;
		for (u32 i = 0; i < call.arg_count; ++i) {
			args[i] = emit(*call.args[i], nullptr);
			emitOp(EX_OP_COPY);
			emit(arg_base + arg_size);
			emit(args[i]);
			emit(call.arg_sizes[i]);
			arg_size += call.arg_sizes[i];
		}
		if (stack_top > stack_high_water) stack_high_water = stack_top;
		emitOp(EX_OP_CALL_INDIRECT);
		emit(callee);
		emit(arg_size);
		emit(call.return_size);
		stack_top = callee + call.return_size;
		return callee;
	}

	u32 emitNull(const ExOpNull& op) {
		u32 ret = stack_top;
		u32 remaining = op.size;
		while (remaining >= 8) {
			emitOp(EX_OP_LOAD_CONST_8);
			emit(stack_top);
			emit((u64)0);
			stack_top += 8;
			remaining -= 8;
		}
		while (remaining > 0) {
			emitOp(EX_OP_LOAD_CONST_1);
			emit(stack_top);
			emit((u8)0);
			++stack_top;
			--remaining;
		}
		return ret;
	}

	u32 emitSlice(const ExOpSlice& op) {
		u32 result;
		if (op.source_is_scalar) {
			const u32 source = emitOperand(*op.source);
			result = stack_top;
			emitOp(EX_OP_COPY);
			emit(result);
			emit(source);
			emit((u32)sizeof(void*));
			stack_top += sizeof(void*);
			emitOp(EX_OP_LOAD_CONST_8);
			emit(stack_top);
			emit(op.source_length);
			stack_top += sizeof(i64);
		} else if (op.source_is_array) {
			ASSERT(op.source->result_mode == ExIrOp::ADDRESS);
			result = emit(*op.source, nullptr);
			emitOp(EX_OP_LOAD_CONST_8);
			emit(stack_top);
			emit(op.source_length);
			stack_top += sizeof(i64);
		} else {
			result = emitOperand(*op.source);
		}

		if (!op.begin && !op.end) return result;

		static ResolvedType index_type(ResolvedTypeKind::I64);
		auto emitBound = [&](ExIrOp* bound, i64 fallback) {
			if (bound) return emitOperand(*bound);
			auto& constant = alloc<ExOpLoadConst>();
			constant.type = &index_type;
			memcpy(constant.value, &fallback, sizeof(fallback));
			return emit(static_cast<ExIrOp&>(constant), nullptr);
		};
		const u32 begin = emitBound(op.begin, 0);
		u32 end;
		if (op.end) {
			end = emitOperand(*op.end);
		} else if (op.source_is_array) {
			end = emitBound(nullptr, op.source_length);
		} else {
			end = stack_top;
			emitOp(EX_OP_SLICE_LENGTH);
			emit(stack_top);
			emit(result);
			stack_top += sizeof(i64);
		}
		const u32 destination = stack_top;
		emitOp(EX_OP_SLICE);
		emit(destination);
		emit(result);
		emit(begin);
		emit(end);
		emit(op.element_size);
		stack_top += sizeof(void*) + sizeof(i64);
		return destination;
	}

	u32 emitSliceLoad(const ExOpSliceLoad& op) {
		const u32 slice = emitOperand(*op.slice);
		const u32 index = emitOperand(*op.index);
		const u32 result = stack_top;
		const ex_op load_op = sliceFieldLoadOp(toTypeKind(*op.index_type));
		emitOp(load_op);
		emit(result);
		emit(slice);
		emit(index);
		emit(op.element_size);
		emit((u32)0);
		emit(op.element_size);
		stack_top += op.element_size;
		return result;
	}

	u32 emitStringLiteral(const ExOpStringLiteral& op) {
		u32 res = stack_top;
		const u32 result = stack_top;
		stack_top += sizeof(void*) + sizeof(u64);
		emitOp(EX_OP_STRING_SLICE);
		emit(result);
		emit(op.index);
		return result;
	}

	u32 emitSliceRef(const ExOpSliceRef& op, EmitDst* dst) {
		const u32 slice = emitOperand(*op.slice);
		const u32 index = emitOperand(*op.index);
		const u32 ret = dst ? dst->dst : stack_top;
		if (!dst) stack_top += sizeof(void*);
		emitOp(EX_OP_SLICE_REF);
		emit(ret);
		emit(slice);
		emit(index);
		emit((u8)toTypeKind(*op.index_type));
		emit(op.element_size);
		return ret;
	}

	u32 emitSliceFieldStoreAddress(const ExOpSliceFieldStore& op) {
		const u32 slice = emitOperand(*op.slice);
		const u32 index = emitOperand(*op.index);
		const u32 address = stack_top;
		stack_top += sizeof(void*);
		emitOp(EX_OP_SLICE_REF);
		emit(address);
		emit(slice);
		emit(index);
		emit((u8)toTypeKind(*op.index_type));
		emit(op.element_size);
		return address;
	}

	u32 emitSliceFieldLoad(const ExOpSliceFieldLoad& op, EmitDst* dst) {
		const u32 slice = emitOperand(*op.slice);
		const u32 index = emitOperand(*op.index);
		const u32 result = dst ? dst->dst : stack_top;
		const ex_type_kind kind = toTypeKind(*op.index_type);
		const ex_op op_code = sliceFieldLoadOp(kind);
		emitOp(op_code);
		emit(result);
		emit(slice);
		emit(index);
		emit(op.element_size);
		emit(op.field_offset);
		emit(op.field_size);
		if (!dst) stack_top += op.field_size;
		return result;
	}

	u32 emitFramePtr(ExOpFramePtr& frame) {
		ASSERT(frame.alloca);
		return frame.alloca->stack_sp;
	}

	u32 emit(ExIrOp& op, EmitDst* dst) {
		op.bytecode_offset = code.size();
		SourceScope scope(*this, op.src_loc);
		u32 result = -1;
		switch (op.kind) {
			case ExIrOpKind::FRAME_PTR: result = emitFramePtr(static_cast<ExOpFramePtr&>(op)); break;
			case ExIrOpKind::STRING_LITERAL: result = emitStringLiteral(static_cast<ExOpStringLiteral&>(op)); break;
			case ExIrOpKind::SLICE_REF: result = emitSliceRef(static_cast<ExOpSliceRef&>(op), dst); break;
			case ExIrOpKind::SLICE_FIELD_LOAD: result = emitSliceFieldLoad(static_cast<ExOpSliceFieldLoad&>(op), dst); break;
			case ExIrOpKind::SLICE_FIELD_STORE: result = emitSliceFieldStoreAddress(static_cast<ExOpSliceFieldStore&>(op)); break;
			case ExIrOpKind::SLICE_LOAD: result = emitSliceLoad(static_cast<ExOpSliceLoad&>(op)); break;
			case ExIrOpKind::SLICE: result = emitSlice(static_cast<ExOpSlice&>(op)); break;
			case ExIrOpKind::NEG:
			case ExIrOpKind::NOT: result = emitUnary(static_cast<ExOpUnary&>(op), dst); break;
			case ExIrOpKind::AND:
			case ExIrOpKind::OR: result = emitShortCircuit(static_cast<ExOpBinary&>(op)); break;
			case ExIrOpKind::NULL_VALUE: result = emitNull(static_cast<ExOpNull&>(op)); break;
			case ExIrOpKind::NOP: result = 0xffFFffFF; break;
			case ExIrOpKind::JUMP: result = emitJump(static_cast<ExOpJump&>(op)); break;
			case ExIrOpKind::CONDITIONAL_JUMP: result = emitConditionalJump(*static_cast<ExOpConditionalJump*>(&op)); break;
			case ExIrOpKind::CALL_DIRECT: result = emitCallDirect(*static_cast<ExOpCallDirect*>(&op)); break;
			case ExIrOpKind::PANIC: {
				ExOpPanic& panic = *static_cast<ExOpPanic*>(&op);
				const u32 message = emit(*panic.message, nullptr);
				if (stack_top > stack_high_water) stack_high_water = stack_top;
				emitOp(EX_OP_PANIC);
				emit(message);
				result = 0xffffffffu;
				break;
			}
			case ExIrOpKind::CALL_INDIRECT: result = emitCallIndirect(*static_cast<ExOpCallIndirect*>(&op)); break;
			case ExIrOpKind::EXTRACT_VALUE: result = emitExtractValue(*static_cast<ExOpExtractValue*>(&op), dst); break;
			case ExIrOpKind::LOAD: result = emitLoad(*static_cast<ExOpLoad*>(&op), dst); break;
			case ExIrOpKind::PUSH_LOCAL_ADDR: result = emitPushLocalAddr(*static_cast<ExOpPushLocalAddr*>(&op)); break;
			case ExIrOpKind::MATERIALIZE_ADDR: result = emitMaterializeAddr(*static_cast<ExOpMaterializeAddr*>(&op)); break;
			case ExIrOpKind::PUSH_GLOBAL_ADDR: result = emitPushGlobalAddr(*static_cast<ExOpPushGlobalAddr*>(&op)); break;
			case ExIrOpKind::EQ:
			case ExIrOpKind::NE:
			case ExIrOpKind::LT:
			case ExIrOpKind::LE:
			case ExIrOpKind::GT:
			case ExIrOpKind::GE: result = emitCompare(static_cast<ExOpBinary&>(op)); break;
			case ExIrOpKind::COPY: result = emitCopy(*static_cast<ExOpCopy*>(&op)); break;
			case ExIrOpKind::CAST: result = emitCast(static_cast<ExOpCast&>(op), dst); break;
			case ExIrOpKind::TERNARY: result = emitTernary(static_cast<ExOpTernary&>(op)); break;
			case ExIrOpKind::BOUNDS_CHECK: result = emitBoundsCheck(static_cast<ExOpBoundsCheck&>(op)); break;
			case ExIrOpKind::ALLOCA: result = emitAlloca(*static_cast<ExOpAlloca*>(&op)); break;
			case ExIrOpKind::MUL: result = emitBinary(EX_OP_MUL_8, static_cast<ExOpBinary&>(op), dst); break;
			case ExIrOpKind::ADD: result = emitBinary(EX_OP_ADD_8, static_cast<ExOpBinary&>(op), dst); break;
			case ExIrOpKind::SUB: result = emitBinary(EX_OP_SUB_8, static_cast<ExOpBinary&>(op), dst); break;
			case ExIrOpKind::DIV: result = emitBinary(EX_OP_DIV_I8, static_cast<ExOpBinary&>(op), dst); break;
			case ExIrOpKind::MOD: result = emitBinary(EX_OP_MOD_I8, static_cast<ExOpBinary&>(op), dst); break;
			case ExIrOpKind::LOAD_CONST: result = emitLoadConst(static_cast<ExOpLoadConst&>(op), dst); break;
			case ExIrOpKind::LOAD_BYTES: result = emitLoadBytes(static_cast<ExOpLoadBytes&>(op), dst); break;
			case ExIrOpKind::UNION_CONVERT: result = emitUnionConvert(static_cast<ExOpUnionConvert&>(op)); break;
			case ExIrOpKind::AGGREGATE_INIT: result = emitAggregateInit(static_cast<ExOpAggregateInit&>(op)); break;
			case ExIrOpKind::RETURN: result = emitReturn(static_cast<ExOpReturn&>(op)); break;
			default: ASSERT(false); break;
		}
		if (dst && dst->dst != result) {
			emitOp(EX_OP_COPY);
			emit(dst->dst);
			emit(result);
			emit(dst->size);
		}
		return dst ? dst->dst : result;
	}

	u32 emitReturn(const ExOpReturn& ret) {
		if (!ret.expression) {
			emitOp(EX_OP_RETURN_BASE);
			return stack_top;
		}
		if (do_optimize) {
			EmitDst dst = {0, ret.size};
			const u32 result = emit(*ret.expression, &dst);
			emitOp(EX_OP_RETURN_BASE);
			return result;
		}
		const u32 result = emit(*ret.expression, nullptr);
		emitOp(EX_OP_RETURN);
		emit(result);
		emit(ret.size);
		return result;
	}

	void optimize(ExIrOp*& op) {
		switch (op->kind) {
			case ExIrOpKind::ALLOCA: {
				auto& alloca = *static_cast<ExOpAlloca*>(op);
				if (alloca.value) optimize(alloca.value);
				break;
			}
			case ExIrOpKind::CALL_DIRECT: {
				auto& call = *static_cast<ExOpCallDirect*>(op);
				for (u32 i = 0; i < call.arg_count; ++i) {
					optimize(call.args[i]);
				}
				break;
			}
			case ExIrOpKind::COPY: {
				auto& copy = *static_cast<ExOpCopy*>(op);
				optimize(copy.src);
				optimize(copy.dst);

				// Fold a slice-element address plus a constant field offset into
				// the dedicated slice-field store operation. The emitter keeps the
				// old address-shaped fallback for builds without optimization.
				ExIrOp* address_base = copy.dst;
				u32 field_offset = 0;
				if (copy.dst->result_mode == ExIrOp::ADDRESS
					&& tryGetOffsetAddress(*copy.dst, address_base, field_offset)
					&& address_base->kind == ExIrOpKind::SLICE_REF) {
					auto& ref = *static_cast<ExOpSliceRef*>(address_base);
					auto& field = alloc<ExOpSliceFieldStore>();
					field.slice = ref.slice;
					field.index = ref.index;
					field.element_size = ref.element_size;
					field.index_type = ref.index_type;
					field.field_offset = field_offset;
					field.field_size = typeByteSize(*copy.type);
					field.src_loc = copy.dst->src_loc;
					copy.dst = &field;
				}
				break;
			}
			case ExIrOpKind::NOT:
			case ExIrOpKind::NEG: {
				auto& v = *static_cast<ExOpUnary*>(op);
				optimize(v.operand);
				break;
			}
			case ExIrOpKind::MUL: {
				auto& mul = *static_cast<ExOpBinary*>(op);
				optimize(mul.lhs);
				optimize(mul.rhs);
				ExIrOp* lhs_negated = nullptr;
				if (mul.lhs->kind == ExIrOpKind::NEG) lhs_negated = static_cast<ExOpNeg*>(mul.lhs)->operand;
				ExIrOp* rhs_negated = nullptr;
				if (mul.rhs->kind == ExIrOpKind::NEG) rhs_negated = static_cast<ExOpNeg*>(mul.rhs)->operand;
				if (!lhs_negated && !rhs_negated) break;

				auto& inner = alloc<ExOpMul>();
				inner.lhs = lhs_negated ? lhs_negated : mul.lhs;
				inner.rhs = rhs_negated ? rhs_negated : mul.rhs;
				inner.operand_type = mul.operand_type;
				inner.src_loc = mul.src_loc;
				if (lhs_negated && rhs_negated) {
					op = &inner;
					break;
				}

				auto& neg = alloc<ExOpNeg>();
				neg.operand = &inner;
				neg.operand_type = mul.operand_type;
				neg.src_loc = mul.src_loc;
				op = &neg;
				break;
			}
			case ExIrOpKind::ADD:
			case ExIrOpKind::SUB: {
				auto& binary = *static_cast<ExOpBinary*>(op);
				optimize(binary.lhs);
				optimize(binary.rhs);
				const bool lhs_negated = binary.lhs->kind == ExIrOpKind::NEG;
				const bool rhs_negated = binary.rhs->kind == ExIrOpKind::NEG;
				ExOpBinary* rewritten = nullptr;
				if (rhs_negated) {
					rewritten = binary.kind == ExIrOpKind::ADD
						? static_cast<ExOpBinary*>(&alloc<ExOpSub>())
						: static_cast<ExOpBinary*>(&alloc<ExOpAdd>());
					rewritten->lhs = binary.lhs;
					rewritten->rhs = static_cast<ExOpNeg*>(binary.rhs)->operand;
				}
				else if (lhs_negated && binary.kind == ExIrOpKind::ADD) {
					auto& sub = alloc<ExOpSub>();
					sub.lhs = binary.rhs;
					sub.rhs = static_cast<ExOpNeg*>(binary.lhs)->operand;
					rewritten = &sub;
				}
				if (!rewritten) break;
				
				rewritten->operand_type = binary.operand_type;
				rewritten->src_loc = binary.src_loc;
				op = rewritten;
				break;
			}
			case ExIrOpKind::DIV:
			case ExIrOpKind::MOD:
			case ExIrOpKind::COMPARE:
			case ExIrOpKind::EQ:
			case ExIrOpKind::NE:
			case ExIrOpKind::AND:
			case ExIrOpKind::OR:
			case ExIrOpKind::LE:
			case ExIrOpKind::LT:
			case ExIrOpKind::GE:
			case ExIrOpKind::GT: {
				auto& add = *static_cast<ExOpBinary*>(op);
				optimize(add.lhs);
				optimize(add.rhs);
				break;
			}
			case ExIrOpKind::CONDITIONAL_JUMP: {
				auto& jmp = *static_cast<ExOpConditionalJump*>(op);
				optimize(jmp.condition);
				if (jmp.true_block) optimize(*jmp.true_block);
				if (jmp.false_block) optimize(*jmp.false_block);
				break;
			}
			case ExIrOpKind::RETURN: {
				auto& ret = *static_cast<ExOpReturn*>(op);
				if (ret.expression) optimize(ret.expression);
				break;
			}
			case ExIrOpKind::LOAD: {
				auto* load = static_cast<ExOpLoad*>(op);
				optimize(load->addr);
				ExIrOp* address_base = load->addr;
				u32 field_offset = 0;
				if (tryGetOffsetAddress(*load->addr, address_base, field_offset) && address_base->kind == ExIrOpKind::SLICE_REF) {
					auto& ref = *static_cast<ExOpSliceRef*>(address_base);
					auto& field = alloc<ExOpSliceFieldLoad>();
					field.slice = ref.slice;
					field.index = ref.index;
					field.element_size = ref.element_size;
					field.index_type = ref.index_type;
					field.field_offset = field_offset;
					field.field_size = load->size;
					field.src_loc = load->src_loc;
					op = &field;
				}
				else if (load->addr->kind == ExIrOpKind::PUSH_LOCAL_ADDR) {
					auto& addr = *static_cast<ExOpPushLocalAddr*>(load->addr);
					auto& ref = alloc<ExOpFramePtr>();
					ref.alloca = addr.alloca;
					ref.src_loc = load->src_loc;
					op = &ref;
				}
				break;
			}
			case ExIrOpKind::SLICE_REF: {
				auto& ref = *static_cast<ExOpSliceRef*>(op);
				optimize(ref.slice);
				optimize(ref.index);
				break;
			}
			case ExIrOpKind::SLICE_LOAD: {
				auto& load = *static_cast<ExOpSliceLoad*>(op);
				optimize(load.slice);
				optimize(load.index);
				break;
			}
			case ExIrOpKind::SLICE_FIELD_LOAD:
			case ExIrOpKind::SLICE_FIELD_STORE: {
				auto& field = *static_cast<ExOpSliceField*>(op);
				optimize(field.slice);
				optimize(field.index);
				break;
			}
			case ExIrOpKind::BOUNDS_CHECK: {
				auto& bounds = *static_cast<ExOpBoundsCheck*>(op);
				optimize(bounds.index);
				break;
			}
			case ExIrOpKind::CAST: {
				auto& cast = *static_cast<ExOpCast*>(op);
				optimize(cast.value);
				break;
			}
			case ExIrOpKind::MATERIALIZE_ADDR: {
				auto& address = *static_cast<ExOpMaterializeAddr*>(op);
				optimize(address.value);
				break;
			}
		}
	}

	void optimize(ExIrBlockData& body) {
		if (!do_optimize) return;
		for (ExIrOp*& op : body.ops) {
			optimize(op);
		}
	}

	void emitBlock(ExIrBlockData& block) {
		for (ExIrOp* op : block.ops) {
			stack_top = temp_base;
			emit(*op, nullptr);
			if (stack_top > stack_high_water) stack_high_water = stack_top;
		}
	}

	void beginFunction(ex_function_bc* fn, FunctionExpression& fn_expr, u32 alloca_region_size) {
		ASSERT(fn);
		fn_bc = fn;
		temp_base = alloca_region_size;
		stack_top = temp_base;
		stack_high_water = temp_base;

		ResolvedType* return_type = static_cast<FunctionResolvedType*>(fn_expr.resolved_type)->return_type;

		fn_bc->code = (u8*)(u64)code.size(); // store offset since code.data may be reallocated
		fn_bc->kind = fn_expr.is_extern ? EX_FUNCTION_NATIVE : EX_FUNCTION_SCRIPT;
		fn_bc->is_builtin_native = false;
		fn_bc->param_size = 0;
		local_debug.clear();
		u32 param_offset = 0;
		for (const FunctionParam& param : fn_expr.params) {
			if (param.is_comptime) continue;
			const u32 size = typeByteSize(*param.resolved_type);
			fn_bc->param_size += size;
			recordLocal(param.name.value, param_offset, *param.resolved_type, 0u);
			param_offset += size;
		}
		fn_bc->return_kind = toTypeKind(*return_type);
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
		fn_bc->code_size = u32(code.size() - (u64)fn_bc->code);
		fn_bc->frame_size = stack_top > stack_high_water ? stack_top : stack_high_water;

		const u32 function_start = (u32)(u64)fn_bc->code;
		const u32 function_end = function_start + fn_bc->code_size;
		for (i32 i = 0, c = code.source_map.size(); i < c; ++i) {
			const ex_bytecode_source_map_entry& entry = code.source_map[(i32)i];
			if (entry.code_offset < function_start || entry.code_offset >= function_end) continue;
			++fn_bc->source_map_count;
		}
		if (fn_bc->source_map_count > 0u) {
			fn_bc->source_map = (ex_bytecode_source_map_entry*)host.arena.allocate(
				host.arena.user_data,
				sizeof(ex_bytecode_source_map_entry) * fn_bc->source_map_count,
				alignof(ex_bytecode_source_map_entry));
			u32 out = 0;
			for (i32 i = 0, c = code.source_map.size(); i < c; ++i) {
				const ex_bytecode_source_map_entry& entry = code.source_map[(i32)i];
				if (entry.code_offset < function_start || entry.code_offset >= function_end) continue;
				ex_bytecode_source_map_entry& dst = fn_bc->source_map[out++];
				dst.code_offset = entry.code_offset - function_start;
				dst.location_index = entry.location_index;
			}
		}

		fn_bc->local_count = (u32)local_debug.size();
		if (fn_bc->local_count > 0u) {
			fn_bc->locals = (ex_bytecode_local_debug_entry*)host.arena.allocate(
				host.arena.user_data,
				sizeof(ex_bytecode_local_debug_entry) * fn_bc->local_count,
				alignof(ex_bytecode_local_debug_entry));
			for (u32 i = 0; i < fn_bc->local_count; ++i) {
				fn_bc->locals[i] = local_debug[(i32)i];
			}
		}
		local_debug.clear();

		fn_bc = nullptr;
	}

	void emit(const void* data, u32 size) {
		for (u32 i = 0; i < size; ++i) {
			code.push_back(((const u8*)data)[i]);
		}
	}

	ex_host& host;
	ex_bytecode& bytecode;
	TypeInfoBuilder& type_info;
	ex_function_bc* fn_bc = nullptr;
	ByteArray code;
	bool do_optimize = false;
	bool do_inline = false;
	// Debug entries for the current function's named params and locals, in
	// declaration order (params first). Copied into the arena in endFunction
	// and cleared per function.
	ExpArray<ex_bytecode_local_debug_entry> local_debug;
	u32 stack_top = 0;
	u32 stack_high_water = 0;
	u32 temp_base = 0;
	ExIrSourceLoc current_src_loc = EX_IR_INVALID_SOURCE_LOC;

	struct SourceScope {
		SourceScope(BytecodeCompiler& compiler, ExIrSourceLoc loc)
			: compiler(compiler)
			, previous(compiler.current_src_loc) {
			compiler.current_src_loc = loc;
		}
		~SourceScope() { compiler.current_src_loc = previous; }

		BytecodeCompiler& compiler;
		ExIrSourceLoc previous;
	};
};

} // namespace

ex_bytecode* ex_bytecode_compile(ex_module* module, ex_host* host, ex_bytecode_compile_options* options) {
	if (!module || !host) return nullptr;

	ex_bytecode* bc = (ex_bytecode*)host->arena.allocate(host->arena.user_data, sizeof(ex_bytecode), alignof(ex_bytecode));
	memset(bc, 0, sizeof(*bc));
	bc->host = host;
	bc->arena = &host->arena;

	SourceLocTable& src_locs = module->src_locs;
	IRBuilder builder(*host);
	TypeInfoBuilder type_info(*host, *bc);

	bc->function_count = 0;
	bc->global_size = 0;
	bc->has_global_init = false;

	// One entry per named global in declaration order (the synthetic
	// global-initializer function and compiler temporaries are not globals).
	// The table is filled in the same pass that lays out global storage so the
	// reported offsets match the runtime slots exactly. Names are copied into
	// the bytecode arena so the bytecode does not reference the module after
	// compilation.
	u32 global_debug_count = 0;
	for (Unit& u : module->units) {
		for (Symbol& s : u.symbols) {
			if (!symbolHasGlobalStorage(s)) continue;
			++global_debug_count;
		}
	}
	bc->global_debug_count = global_debug_count;
	if (global_debug_count > 0u) {
		bc->global_debug = (ex_bytecode_global_debug_entry*)host->arena.allocate(
			host->arena.user_data,
			sizeof(ex_bytecode_global_debug_entry) * global_debug_count,
			alignof(ex_bytecode_global_debug_entry));
	}

	u32 global_debug_index = 0;
	for (Unit& u : module->units) {
		for (Symbol& s : u.symbols) {
			if (!symbolHasGlobalStorage(s)) continue;
			s.slot.storage = StorageSlot::GLOBAL;
			s.slot.offset = bc->global_size;
			s.slot.type = s.resolved_type;
			const u32 byte_size = typeByteSize(*s.resolved_type);
			bc->global_size += byte_size;

			ex_bytecode_global_debug_entry& entry = bc->global_debug[global_debug_index++];
			entry.name = copyStringViewToArena(host->arena, s.name);
			entry.offset = s.slot.offset;
			entry.byte_size = byte_size;
			entry.kind = debugTypeKind(*s.resolved_type);
			entry.type_index = type_info.internType(*s.resolved_type);
		}
	}
	// Keep named struct declarations available to hosts even when they are not
	// referenced by runtime code (for example, editor-only component data).
	for (Unit& u : module->units) {
		for (Symbol& s : u.symbols) {
			if (!s.expression || s.expression->kind != Expression::STRUCT) continue;
			if (!s.resolved_type || s.resolved_type->kind != ResolvedTypeKind::META) continue;
			ResolvedType* type = static_cast<MetaType*>(s.resolved_type)->inner;
			if (type) type_info.internType(*type);
		}
	}

	for (Unit& u : module->units) {
		for (Symbol& s : u.symbols) {
			if (!s.expression) continue; // alias
			if (s.expression->kind != Expression::FUNCTION) continue;

			auto& fn_expr = static_cast<FunctionExpression&>(*s.expression);
			if (isTypeFactory(fn_expr)) continue;
			if (fn_expr.is_template) continue;
			fn_expr.bytecode_index = bc->function_count++;
		}
	}
	if (bc->global_size > 0) ++bc->function_count;

	if (bc->function_count > 0) {
		bc->function_capacity = bc->function_count; // TODO get rid of capacity?
		bc->functions = (ex_function_bc*)host->arena.allocate(host->arena.user_data, sizeof(ex_function_bc) * bc->function_capacity, alignof(ex_function_bc));
		memset(bc->functions, 0, sizeof(ex_function_bc) * bc->function_capacity);
	}

	BytecodeCompiler bc_compiler(*host, *bc, type_info);
	if (options) bc_compiler.do_optimize = options->optimize;
	bc_compiler.do_inline = bc_compiler.do_optimize;
	builder.do_inline = bc_compiler.do_optimize;
	u32 fn_index = 0;
	for (Unit& u : module->units) {
		for (Symbol& s : u.symbols) {
			if (!s.expression) continue; // alias

			if (s.expression->kind == Expression::FUNCTION) {
				auto& fn_expr = static_cast<FunctionExpression&>(*s.expression);
				if (isTypeFactory(fn_expr)) continue;
				if (fn_expr.is_template) continue;
				ex_function_bc& fn_bc = bc->functions[fn_expr.bytecode_index];
				fn_bc.name = copyStringViewToArena(host->arena, s.name);

				if (fn_expr.body) {
					ExIrBlockData& body = builder.buildFunctionIR(fn_expr);
					bc_compiler.beginFunction(&fn_bc, fn_expr, builder.alloca_region_size);
					bc_compiler.optimize(body);
					bc_compiler.emitBlock(body);
					bc_compiler.patchJumps(body);
				} else {
					bc_compiler.beginFunction(&fn_bc, fn_expr, 0);
				}
				fn_bc.is_builtin_native = fn_expr.is_extern && (equalStrings(u.path, makeStringView("std:math")) || equalStrings(u.path, makeStringView("std:mem")));
				bc_compiler.endFunction();
				++fn_index;
			}
		}
	}

	if (builder.strings.size() > 0) {
		bc->string_count = builder.strings.size();
		bc->strings = (ex_string_view*)host->arena.allocate(host->arena.user_data, sizeof(ex_string_view) * bc->string_count, alignof(ex_string_view));
		for (i32 i = 0, c = builder.strings.size(); i < c; ++i) {
			bc->strings[i] = builder.strings[i];
		}
	}

	if (bc->global_size > 0) {
		ex_function_bc& fn = bc->functions[fn_index++];
		memset(&fn, 0, sizeof(fn));
		fn.kind = EX_FUNCTION_SCRIPT;
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
		builder.stack_cursor = 0;
		for (Unit& u : module->units) {
			for (Symbol& s : u.symbols) {
				if (!symbolHasGlobalStorage(s) || s.expression->kind == Expression::UNDEFINED) continue;
				ExIrOp& value = builder.buildExpressionIR(*s.expression, true);
				u32 src = bc_compiler.emit(value, nullptr);
				const u32 dst = bc_compiler.stack_top;
				bc_compiler.emitOp(EX_OP_GLOBAL_PTR);
				bc_compiler.emit(dst);
				bc_compiler.emit(s.slot.offset);
				bc_compiler.stack_top += sizeof(void*);
				bc_compiler.emitOp(EX_OP_STORE_PTR);
				bc_compiler.emit(dst);
				bc_compiler.emit(0u);
				bc_compiler.emit(src);
				bc_compiler.emit(typeByteSize(*s.resolved_type));
			}
		}
		bc_compiler.emitOp(EX_OP_RETURN_BASE);
		bc_compiler.endFunction();
		bc->has_global_init = true;
	}

	// Copy the module's append-only SourceLocTable into the bytecode location
	// table verbatim (one entry per token; token indices are reused as-is by
	// the source maps). source_name strings are copied into the bytecode arena
	// so the bytecode does not reference the module after compilation.
	const u32 location_count = (u32)src_locs.entries.size();
	bc->location_count = location_count;
	if (location_count > 0u) {
		bc->locations = (ex_bytecode_location*)host->arena.allocate(
			host->arena.user_data,
			sizeof(ex_bytecode_location) * location_count,
			alignof(ex_bytecode_location));
		for (i32 i = 0; i < (i32)location_count; ++i) {
			const SourceLocTable::Entry& src = src_locs.entries[i];
			bc->locations[i].source_name = copyStringViewToArena(host->arena, src.source_name);
			bc->locations[i].line = src.line;
			bc->locations[i].column = src.column;
		}
	}

	for (u32 i = 0; i < bc->function_count; ++i) {
		bc->functions[i].code = (u8*)(u64)bc_compiler.code.data + (u64)bc->functions[i].code;
	}

	type_info.commit();

	return bc;
}
