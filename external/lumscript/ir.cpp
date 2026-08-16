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

	template <typename T, typename... Args> T& alloc(Args&&... args) {
		T* v = (T*)host.arena.allocate(host.arena.user_data, sizeof(T), alignof(T));
		new (NewPlaceholder(), v) T(static_cast<Args&&>(args)...);
		return *v;
	}

	LsIrOp& buildImplicitConversionIR(Expression& expression, ResolvedType& target_type) {
		if (expression.kind == Expression::UNDEFINED) {
			// TODO should undefined do nothing?
			auto& storage = alloc<LsOpAlloca>();
			storage.type = &target_type;
			storage.value = &alloc<LsOpNop>();
			return storage;
		}
		if (target_type.kind != ResolvedType::SLICE || expression.resolved_type->kind != ResolvedType::ARRAY) {
			return buildExpressionIR(expression, true);
		}
		auto& array = static_cast<ArrayResolvedType&>(*expression.resolved_type);
		auto& slice = alloc<LsOpSlice>();
		LsIrOp* source = &buildExpressionIR(expression, false);
		if (source->result_mode == LsIrOp::VALUE) {
			auto& address = alloc<LsOpMaterializeAddr>();
			address.value = source;
			source = &address;
		}
		slice.source = source;
		slice.source_is_array = true;
		slice.source_length = array.size;
		slice.element_size = typeByteSize(*array.element_type);
		return slice;
	}

	LsIrOp& buildExpressionIR(Expression& expr, bool as_rvalue) {
		if (as_rvalue && expr.comptime_value.kind == ComptimeValue::VALUE && expr.comptime_value.value && expr.resolved_type && typeByteSize(*expr.resolved_type) <= sizeof(LsOpLoadConst::value)) {
			auto& constant = alloc<LsOpLoadConst>();
			constant.type = expr.resolved_type;
			memcpy(constant.value, expr.comptime_value.value, typeByteSize(*expr.resolved_type));
			return constant;
		}
		if (as_rvalue && expr.comptime_value.kind == ComptimeValue::VALUE && expr.comptime_value.value && expr.resolved_type) {
			auto& constant = alloc<LsOpLoadBytes>();
			constant.type = expr.resolved_type;
			constant.value = expr.comptime_value.value;
			constant.size = typeByteSize(*expr.resolved_type);
			return constant;
		}
		switch (expr.kind) {
			case Expression::UNDEFINED: return alloc<LsOpNop>();
			case Expression::ADDRESSOF: {
				auto& address = static_cast<AddressOfExpression&>(expr);
				return buildExpressionIR(*address.subject, false);
			}
			case Expression::DEREFERENCE: {
				auto& dereference = static_cast<DereferenceExpression&>(expr);
				LsIrOp& address = buildExpressionIR(*dereference.subject, true);
				address.result_mode = LsIrOp::ADDRESS;
				if (!as_rvalue) return address;
				auto& load = alloc<LsOpLoad>();
				load.addr = &address;
				load.size = typeByteSize(*expr.resolved_type);
				return load;
			}
			case Expression::SLICE: {
				auto& slice = static_cast<SliceExpression&>(expr);
				auto& op = alloc<LsOpSlice>();
				op.source_is_array = slice.base->resolved_type->kind == ResolvedType::ARRAY;
				op.source_is_scalar = slice.base->resolved_type->kind != ResolvedType::SLICE && !op.source_is_array;
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
				if (slice.begin) op.begin = &buildExpressionIR(*slice.begin, true);
				if (slice.end) op.end = &buildExpressionIR(*slice.end, true);
				return op;
			}
			case Expression::BRACKET: {
				auto& be = static_cast<BracketExpression&>(expr);
				if (be.struct_field_name.begin) {
					auto& struct_type = static_cast<StructResolvedType&>(*be.base->resolved_type);
					u32 offset = 0;
					for (u32 i = 0; i < struct_type.decl->fields.size(); ++i) {
						if (!equalStrings(struct_type.decl->fields[i].name, be.struct_field_name)) {
							offset += typeByteSize(*struct_type.field_types[i]);
							continue;
						}
						if (as_rvalue) {
							auto& extract = alloc<LsOpExtractValue>();
							extract.value = &buildExpressionIR(*be.base, true);
							extract.offset = offset;
							extract.size = typeByteSize(*be.resolved_type);
							return extract;
						}
						auto& add = alloc<LsOpAdd>();
						auto& base = buildExpressionIR(*be.base, false);
						add.lhs = &base;
						auto& rhs = alloc<LsOpLoadConst>();
						static ResolvedType offset_type(ResolvedType::U64);
						rhs.type = &offset_type;
						const u64 field_offset = offset;
						memcpy(rhs.value, &field_offset, sizeof(field_offset));
						add.rhs = &rhs;
						add.operand_type = &offset_type;
						add.result_mode = LsIrOp::ADDRESS;
						return add;
					}
					ASSERT(false);
					return alloc<LsOpNop>();
				}
				if (be.base->resolved_type->kind == ResolvedType::SLICE) {
					ASSERT(be.args.size() == 1);
					LsIrOp* index = &buildExpressionIR(*be.args[0], true);
					if (typeByteSize(*be.args[0]->resolved_type) != sizeof(i64)) {
						static ResolvedType index_type(ResolvedType::I64);
						auto& cast = alloc<LsOpCast>();
						cast.type = &index_type;
						cast.target_type = be.args[0]->resolved_type;
						cast.value = index;
						index = &cast;
					}
					if (!as_rvalue) {
						auto& ref = alloc<LsOpSliceRef>();
						ref.slice = &buildExpressionIR(*be.base, true);
						ref.index = index;
						ref.element_size = typeByteSize(*be.resolved_type);
						return ref;
					}
					auto& load = alloc<LsOpSliceLoad>();
					load.slice = &buildExpressionIR(*be.base, true);
					load.index = index;
					load.element_size = typeByteSize(*be.resolved_type);
					return load;
				}
				ASSERT(be.base->resolved_type->kind == ResolvedType::ARRAY);
				LsIrOp& base = buildExpressionIR(*be.base, false);
				ASSERT(be.args.size() == 1);
				LsIrOp& index = buildExpressionIR(*be.args[0], true);
				auto& bounds_check = alloc<LsOpBoundsCheck>();
				bounds_check.index_type = be.args[0]->resolved_type;
				bounds_check.index = &index;
				bounds_check.length = (u64) static_cast<ArrayResolvedType*>(be.base->resolved_type)->size;
				LsIrOp* checked_index = &bounds_check;
				static ResolvedType R(ResolvedType::U64);
				if (be.args[0]->resolved_type->kind != ResolvedType::U64) {
					auto& cast = alloc<LsOpCast>();
					cast.type = &R;
					cast.target_type = be.args[0]->resolved_type;
					cast.value = checked_index;
					checked_index = &cast;
				}
				auto& add = alloc<LsOpAdd>();
				auto& mul = alloc<LsOpMul>();
				auto& size = alloc<LsOpLoadConst>();
				size.type = &R;
				mul.operand_type = &R;
				add.operand_type = &R;

				u32 elem_size = typeByteSize(*be.resolved_type);
				memcpy(size.value, &elem_size, sizeof(elem_size));

				mul.lhs = checked_index;
				mul.rhs = &size;
				add.lhs = &base;
				add.rhs = &mul;
				add.result_mode = LsIrOp::ADDRESS;
				if (!as_rvalue) return add;

				auto& load = alloc<LsOpLoad>();
				load.addr = &add;
				load.size = elem_size;
				return load;
			}
			case Expression::MEMBER: {
				auto& me = static_cast<MemberExpression&>(expr);
				if (me.resolved_fn && as_rvalue) {
					auto& value = alloc<LsOpLoadConst>();
					value.type = expr.resolved_type;
					memcpy(value.value, &me.resolved_fn->bytecode_index, sizeof(me.resolved_fn->bytecode_index));
					return value;
				}
				if (as_rvalue && me.resolved_symbol && me.resolved_symbol->storage == Symbol::COMPTIME && me.resolved_symbol->comptime_value.kind == ComptimeValue::VALUE &&
					me.resolved_symbol->comptime_value.value && expr.resolved_type && typeByteSize(*expr.resolved_type) <= sizeof(LsOpLoadConst::value)) {
					auto& value = alloc<LsOpLoadConst>();
					value.type = expr.resolved_type;
					memcpy(value.value, me.resolved_symbol->comptime_value.value, typeByteSize(*expr.resolved_type));
					return value;
				}
				// A qualified imported value is a namespace member, not a struct field.
				// Use its global slot directly so indexed access can take its address.
				if (me.resolved_symbol && symbolHasGlobalStorage(*me.resolved_symbol) && me.resolved_symbol->slot.storage == StorageSlot::GLOBAL) {
					auto& address = alloc<LsOpPushGlobalAddr>();
					address.offset = me.resolved_symbol->slot.offset;
					if (!as_rvalue) return address;
					auto& load = alloc<LsOpLoad>();
					load.addr = &address;
					load.size = typeByteSize(*expr.resolved_type);
					return load;
				}
				if (me.expression && me.expression->resolved_type && me.expression->resolved_type->kind == ResolvedType::SLICE && equalStrings(me.name, makeStringView("length"))) {
					auto& length = alloc<LsOpExtractValue>();
					length.value = &buildExpressionIR(*me.expression, true);
					length.offset = sizeof(void*);
					length.size = sizeof(i64);
					return length;
				}
				EnumResolvedType* enum_type = nullptr;
				if (!me.expression)
					enum_type = static_cast<EnumResolvedType*>(expr.resolved_type);
				else if (me.expression && me.expression->resolved_type && me.expression->resolved_type->kind == ResolvedType::META) {
					ResolvedType* type = static_cast<MetaType*>(me.expression->resolved_type)->inner;
					if (type->kind == ResolvedType::ENUM) enum_type = static_cast<EnumResolvedType*>(type);
				}
				if (enum_type) {
					ASSERT(me.enum_member_index >= 0);
					auto& op = alloc<LsOpLoadConst>();
					op.type = expr.resolved_type;
					const u32 value = (u32)me.enum_member_value;
					memcpy(op.value, &value, sizeof(value));
					return op;
				}
				ResolvedType* base_type = me.expression->resolved_type;
				const bool pointer_base = base_type->kind == ResolvedType::POINTER;
				if (pointer_base) base_type = static_cast<PointerResolvedType*>(base_type)->inner;
				auto* struct_type = static_cast<StructResolvedType*>(base_type);
				auto& fields = struct_type->decl->fields;
				u32 offset = 0;
				for (u32 i = 0; i < fields.size(); ++i) {
					if (!equalStrings(fields[i].name, me.name)) {
						offset += typeByteSize(*struct_type->field_types[i]);
						continue;
					}

					if (as_rvalue) {
						auto& base = buildExpressionIR(*me.expression, pointer_base);
						if (pointer_base) base.result_mode = LsIrOp::ADDRESS;
						switch (base.result_mode) {
							case LsIrOp::ADDRESS: {
								auto& add = alloc<LsOpAdd>();
								add.lhs = &base;
								auto& rhs = alloc<LsOpLoadConst>();
								const u64 pointer_offset = offset;
								memcpy(rhs.value, &pointer_offset, sizeof(pointer_offset));
								static ResolvedType R(ResolvedType::U64);
								rhs.type = &R;
								add.rhs = &rhs;
								add.operand_type = &R;
								add.result_mode = LsIrOp::ADDRESS;

								auto& load = alloc<LsOpLoad>();
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
					} else {
						auto& add = alloc<LsOpAdd>();
						auto& base = buildExpressionIR(*me.expression, pointer_base);
						if (pointer_base) base.result_mode = LsIrOp::ADDRESS;
						add.lhs = &base;
						auto& rhs = alloc<LsOpLoadConst>();
						static ResolvedType U64(ResolvedType::U64);
						const u64 pointer_offset = offset;
						memcpy(&rhs.value, &pointer_offset, sizeof(pointer_offset));
						rhs.type = &U64;
						add.rhs = &rhs;
						add.operand_type = &U64;
						add.result_mode = LsIrOp::ADDRESS;

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
				if (ie.symbol && ie.symbol->storage == Symbol::COMPTIME && ie.symbol->comptime_bytes) {
					auto& value = alloc<LsOpLoadBytes>();
					value.type = ie.symbol->resolved_type;
					value.value = ie.symbol->comptime_bytes;
					value.size = ie.symbol->comptime_byte_size;
					if (!as_rvalue) {
						auto& address = alloc<LsOpMaterializeAddr>();
						address.value = &value;
						return address;
					}
					if (value.type == ie.resolved_type) return value;
					auto& cast = alloc<LsOpCast>();
					cast.type = ie.resolved_type;
					cast.target_type = value.type;
					cast.value = &value;
					return cast;
				}
				StorageSlot* global_slot = ie.symbol ? &ie.symbol->slot : ie.slot;
				if (global_slot && global_slot->storage == StorageSlot::GLOBAL) {
					u32 offset = global_slot->offset;
					if (global_slot->type && global_slot->type->kind == ResolvedType::NULLABLE && ie.resolved_type->kind != ResolvedType::NULLABLE) ++offset;
					if (as_rvalue) {
						auto& addr = alloc<LsOpPushGlobalAddr>();
						addr.offset = offset;
						auto& load = alloc<LsOpLoad>();
						load.addr = &addr;
						load.size = typeByteSize(*ie.resolved_type);
						return load;
					} else {
						auto& op = alloc<LsOpPushGlobalAddr>();
						op.offset = offset;
						return op;
					}
				}

				for (i32 i = locals.size() - 1; i >= 0; --i) {
					if (!equalStrings(locals[i].name, ie.name)) continue;
					auto& addr = alloc<LsOpPushLocalAddr>();
					addr.alloca = locals[i].alloca;
					LsIrOp* value_addr = &addr;
					if (locals[i].alloca->type->kind == ResolvedType::NULLABLE && ie.resolved_type->kind != ResolvedType::NULLABLE) {
						auto& add = alloc<LsOpAdd>();
						auto& offset = alloc<LsOpLoadConst>();
						static ResolvedType pointer_type(ResolvedType::U64);
						const u64 payload_offset = 1;
						memcpy(offset.value, &payload_offset, sizeof(payload_offset));
						offset.type = &pointer_type;
						add.lhs = &addr;
						add.rhs = &offset;
						add.operand_type = &pointer_type;
						add.result_mode = LsIrOp::ADDRESS;
						value_addr = &add;
					} else if (locals[i].alloca->type->kind == ResolvedType::UNION && ie.resolved_type->kind != ResolvedType::UNION) {
						if (as_rvalue) {
							auto& union_value = alloc<LsOpLoad>();
							union_value.addr = &addr;
							union_value.size = typeByteSize(*locals[i].alloca->type);
							auto& payload = alloc<LsOpExtractValue>();
							payload.value = &union_value;
							payload.offset = sizeof(i32);
							payload.size = typeByteSize(*ie.resolved_type);
							return payload;
						}
						auto& add = alloc<LsOpAdd>();
						auto& offset = alloc<LsOpLoadConst>();
						static ResolvedType pointer_type(ResolvedType::U64);
						const u64 payload_offset = sizeof(i32);
						memcpy(offset.value, &payload_offset, sizeof(payload_offset));
						offset.type = &pointer_type;
						add.lhs = &addr;
						add.rhs = &offset;
						add.operand_type = &pointer_type;
						add.result_mode = LsIrOp::ADDRESS;
						value_addr = &add;
					}

					if (as_rvalue) {
						auto& load = alloc<LsOpLoad>();
						load.addr = value_addr;
						load.size = typeByteSize(*ie.resolved_type);
						return load;
					}
					return *value_addr;
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
				if (!function && call.callee->kind == Expression::MEMBER) {
					function = static_cast<MemberExpression&>(*call.callee).resolved_fn;
				}
				if (function && function->bytecode_index != ~0u) {
					MemberExpression* member_callee = call.callee->kind == Expression::MEMBER ? static_cast<MemberExpression*>(call.callee) : nullptr;
					ResolvedType* receiver_type = member_callee && member_callee->expression ? member_callee->expression->resolved_type : nullptr;
					const bool is_ufcs = receiver_type && (receiver_type->kind == ResolvedType::STRUCT || receiver_type->kind == ResolvedType::ENUM || receiver_type->kind == ResolvedType::POINTER);
					auto& op = alloc<LsOpCallDirect>();
					op.function = function;
					op.arg_count = (is_ufcs ? 1u : 0u);
					for (u32 i = 0, param_index = is_ufcs ? 1u : 0u; i < call.args.size(); ++i, ++param_index) {
						if (!function->params[param_index].is_comptime) ++op.arg_count;
					}
					op.return_size = typeByteSize(*call.resolved_type);
					op.args = static_cast<LsIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(LsIrOp*) * op.arg_count, alignof(LsIrOp*)));
					u32 param_index = 0;
					u32 arg_index = 0;
					if (is_ufcs) {
						while (function->params[param_index].is_comptime) ++param_index;
						ResolvedType& target_type = *function->params[param_index++].resolved_type;
						op.args[arg_index++] = &buildImplicitConversionIR(*member_callee->expression, target_type);
					}
					for (u32 i = 0; i < call.args.size(); ++i) {
						FunctionParam& param = function->params[param_index++];
						if (param.is_comptime) continue;
						ResolvedType& target_type = *param.resolved_type;
						op.args[arg_index++] = &buildImplicitConversionIR(*call.args[i], target_type);
					}
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
					ResolvedType& target_type = *fn_type.params[i].type;
					op.args[i] = &buildImplicitConversionIR(*call.args[i], target_type);
					op.arg_sizes[i] = typeByteSize(target_type);
				}
				return op;
			}
			case Expression::ARRAY_LITERAL: {
				auto& ale = static_cast<ArrayLiteralExpression&>(expr);
				auto& op = alloc<LsOpAggregateInit>();
				op.type = ale.resolved_type;
				op.value_count = ale.values.size();
				op.values = static_cast<LsIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(LsIrOp*) * op.value_count, alignof(LsIrOp*)));
				for (u32 i = 0; i < op.value_count; ++i) op.values[i] = &buildExpressionIR(*ale.values[i], true);
				return op;
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
			case Expression::SIZEOF: {
				auto& sz = static_cast<SizeofExpression&>(expr);
				auto& op = alloc<LsOpLoadConst>();
				static ResolvedType default_type(ResolvedType::I32);
				op.type = expr.resolved_type->kind == ResolvedType::UNTYPED_INT ? &default_type : expr.resolved_type;
				memcpy(op.value, &sz.value, sizeof(sz.value));
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
			case Expression::STRING_LITERAL: {
				auto& literal = static_cast<StringLiteralExpression&>(expr);
				auto& pointer = alloc<LsOpLoadConst>();
				static ResolvedType pointer_type(ResolvedType::CPTR);
				pointer.type = &pointer_type;
				memcpy(pointer.value, &literal.value.begin, sizeof(literal.value.begin));
				auto& length = alloc<LsOpLoadConst>();
				static ResolvedType length_type(ResolvedType::I64);
				length.type = &length_type;
				const i64 value_length = literal.value.end - literal.value.begin;
				memcpy(length.value, &value_length, sizeof(value_length));
				auto& slice = alloc<LsOpAggregateInit>();
				slice.type = literal.resolved_type;
				slice.value_count = 2;
				slice.values = static_cast<LsIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(LsIrOp*) * slice.value_count, alignof(LsIrOp*)));
				slice.values[0] = &pointer;
				slice.values[1] = &length;
				return slice;
			}
			case Expression::TYPE_MEMBER: {
				auto& member = static_cast<TypeMemberExpression&>(expr);
				if (member.kind == TypeMemberExpression::NAME) {
					auto& pointer = alloc<LsOpLoadConst>();
					static ResolvedType pointer_type(ResolvedType::CPTR);
					pointer.type = &pointer_type;
					memcpy(pointer.value, &member.comptime_string.begin, sizeof(member.comptime_string.begin));
					auto& length = alloc<LsOpLoadConst>();
					static ResolvedType length_type(ResolvedType::I64);
					length.type = &length_type;
					const i64 value_length = member.comptime_string.end - member.comptime_string.begin;
					memcpy(length.value, &value_length, sizeof(value_length));
					auto& slice = alloc<LsOpAggregateInit>();
					slice.type = expr.resolved_type;
					slice.value_count = 2;
					slice.values = static_cast<LsIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(LsIrOp*) * slice.value_count, alignof(LsIrOp*)));
					slice.values[0] = &pointer;
					slice.values[1] = &length;
					return slice;
				}
				if (member.kind == TypeMemberExpression::MIN || member.kind == TypeMemberExpression::MAX) {
					ASSERT(member.reflected_type && expr.resolved_type);
					const bool is_min = member.kind == TypeMemberExpression::MIN;
					auto& value = alloc<LsOpLoadConst>();
					value.type = expr.resolved_type;
					switch (member.reflected_type->kind) {
						case ResolvedType::I8: {
							const i8 v = is_min ? -128 : 127;
							memcpy(value.value, &v, sizeof(v));
							break;
						}
						case ResolvedType::I16: {
							const i16 v = is_min ? -32768 : 32767;
							memcpy(value.value, &v, sizeof(v));
							break;
						}
						case ResolvedType::I32: {
							const i32 v = is_min ? (i32)-2147483647 - 1 : (i32)2147483647;
							memcpy(value.value, &v, sizeof(v));
							break;
						}
						case ResolvedType::I64:
						case ResolvedType::ISIZE: {
							const i64 v = is_min ? (i64)-9223372036854775807LL - 1 : (i64)9223372036854775807LL;
							memcpy(value.value, &v, sizeof(v));
							break;
						}
						case ResolvedType::U8:
						case ResolvedType::BYTE: {
							const u8 v = is_min ? 0 : 255;
							memcpy(value.value, &v, sizeof(v));
							break;
						}
						case ResolvedType::U16: {
							const u16 v = is_min ? 0 : 65535;
							memcpy(value.value, &v, sizeof(v));
							break;
						}
						case ResolvedType::U32: {
							const u32 v = is_min ? 0 : (u32)4294967295u;
							memcpy(value.value, &v, sizeof(v));
							break;
						}
						case ResolvedType::U64: {
							const u64 v = is_min ? 0 : (u64)18446744073709551615ULL;
							memcpy(value.value, &v, sizeof(v));
							break;
						}
						case ResolvedType::F32: {
							const float v = is_min ? -3.402823466e+38f : 3.402823466e+38f;
							memcpy(value.value, &v, sizeof(v));
							break;
						}
						case ResolvedType::F64: {
							const double v = is_min ? -1.7976931348623157e+308 : 1.7976931348623157e+308;
							memcpy(value.value, &v, sizeof(v));
							break;
						}
						default: ASSERT(false); break;
					}
					return value;
				}
				ASSERT(false);
				return alloc<LsOpNop>();
			}
			case Expression::INT_LITERAL: {
				auto& ile = static_cast<IntLiteralExpression&>(expr);
				auto& op = alloc<LsOpLoadConst>();
				op.type = ile.resolved_type;
				if (ile.resolved_type->kind == ResolvedType::F32) {
					const float value = (float)ile.value;
					memcpy(&op.value, &value, sizeof(value));
				} else if (ile.resolved_type->kind == ResolvedType::F64) {
					const double value = (double)ile.value;
					memcpy(&op.value, &value, sizeof(value));
				} else {
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
				} else {
					memcpy(&op.value, &fle.value, sizeof(fle.value));
				}
				return op;
			}
			case Expression::UNARY: {
				auto& unary = static_cast<UnaryExpression&>(expr);
				LsIrOp& operand = buildExpressionIR(*unary.expression, true);
				if (unary.operator_fn) {
					auto& call = alloc<LsOpCallDirect>();
					call.function = unary.operator_fn;
					call.arg_count = 1;
					call.return_size = typeByteSize(*unary.resolved_type);
					call.args = static_cast<LsIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(LsIrOp*), alignof(LsIrOp*)));
					call.args[0] = &operand;
					return call;
				}
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
				if (be.op == Token::IS) {
					ASSERT(be.lhs->resolved_type->kind == ResolvedType::UNION);
					ASSERT(be.rhs->resolved_type->kind == ResolvedType::META);
					auto& tag = alloc<LsOpExtractValue>();
					tag.value = &lhs;
					tag.offset = 0;
					tag.size = sizeof(i32);
					i32 member_index = be.union_member_index;
					ASSERT(member_index >= 0);
					static ResolvedType tag_type(ResolvedType::I32);
					auto& expected = alloc<LsOpLoadConst>();
					expected.type = &tag_type;
					memcpy(expected.value, &member_index, sizeof(member_index));
					auto& condition = alloc<LsOpEq>();
					condition.operand_type = &tag_type;
					condition.lhs = &tag;
					condition.rhs = &expected;
					return condition;
				}
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
			case Expression::TERNARY: {
				auto& ternary = static_cast<TernaryExpression&>(expr);
				auto& op = alloc<LsOpTernary>();
				op.type = expr.resolved_type;
				op.condition = &buildExpressionIR(*ternary.condition, true);
				op.true_value = &buildImplicitConversionIR(*ternary.true_expr, *expr.resolved_type);
				op.false_value = &buildImplicitConversionIR(*ternary.false_expr, *expr.resolved_type);
				return op;
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
			case Statement::EXPRESSION: {
				auto& ex = static_cast<ExpressionStatement&>(st);
				parent.ops.push(&buildExpressionIR(*ex.expression, true));
				break;
			}
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
			case Statement::FOR: {
				auto& for_statement = static_cast<ForStatement&>(st);
				if (for_statement.end) {
					ResolvedType* value_type = for_statement.begin->resolved_type;
					auto& value = alloc<LsOpAlloca>();
					value.type = value_type;
					value.value = &buildExpressionIR(*for_statement.begin, true);
					parent.ops.push(&value);
					auto& end = alloc<LsOpAlloca>();
					end.type = value_type;
					end.value = &buildExpressionIR(*for_statement.end, true);
					parent.ops.push(&end);

					const u32 local_watermark = locals.size();
					locals.push({for_statement.value_var, &value});
					auto& value_addr = alloc<LsOpPushLocalAddr>();
					value_addr.alloca = &value;
					auto& value_load = alloc<LsOpLoad>();
					value_load.addr = &value_addr;
					value_load.size = typeByteSize(*value_type);
					auto& end_addr = alloc<LsOpPushLocalAddr>();
					end_addr.alloca = &end;
					auto& end_load = alloc<LsOpLoad>();
					end_load.addr = &end_addr;
					end_load.size = typeByteSize(*value_type);
					auto& condition = alloc<LsOpLt>();
					condition.operand_type = value_type;
					condition.lhs = &value_load;
					condition.rhs = &end_load;

					auto& loop = alloc<LsOpConditionalJump>();
					auto& increment_target = alloc<LsOpNop>();
					auto& exit = alloc<LsOpNop>();
					loop.condition = &condition;
					loop.true_block = &alloc<LsIrBlockData>(host.arena);
					loops.push({pending_loop_label, &increment_target, &exit, (u32)defers.size()});
					pending_loop_label = {};
					buildStatementIR(*for_statement.body, *loop.true_block);
					loop.true_block->ops.push(&increment_target);

					auto& increment_addr = alloc<LsOpPushLocalAddr>();
					increment_addr.alloca = &value;
					auto& increment_value = alloc<LsOpLoad>();
					increment_value.addr = &increment_addr;
					increment_value.size = typeByteSize(*value_type);
					auto& one = alloc<LsOpLoadConst>();
					one.type = value_type;
					const u64 one_value = 1;
					memcpy(one.value, &one_value, typeByteSize(*value_type));
					auto& increment = alloc<LsOpAdd>();
					increment.operand_type = value_type;
					increment.lhs = &increment_value;
					increment.rhs = &one;
					auto& store_addr = alloc<LsOpPushLocalAddr>();
					store_addr.alloca = &value;
					auto& store = alloc<LsOpCopy>();
					store.type = value_type;
					store.src = &increment;
					store.dst = &store_addr;
					loop.true_block->ops.push(&store);
					loops.pop_back();
					locals.resize(local_watermark);
					auto& back_edge = alloc<LsOpJump>();
					back_edge.target = &loop;
					loop.true_block->ops.push(&back_edge);
					parent.ops.push(&loop);
					parent.ops.push(&exit);
					break;
				}
				if (for_statement.begin->resolved_type->kind == ResolvedType::SLICE) {
					auto& slice_type = static_cast<SliceResolvedType&>(*for_statement.begin->resolved_type);
					auto& slice = alloc<LsOpAlloca>();
					slice.type = &slice_type;
					slice.value = &buildExpressionIR(*for_statement.begin, true);
					parent.ops.push(&slice);

					static ResolvedType index_type(ResolvedType::ISIZE);
					auto& index_value = alloc<LsOpLoadConst>();
					index_value.type = &index_type;
					auto& index = alloc<LsOpAlloca>();
					index.type = &index_type;
					index.value = &index_value;
					parent.ops.push(&index);

					auto& value = alloc<LsOpAlloca>();
					value.type = slice_type.element_type;
					value.value = &alloc<LsOpNop>();
					parent.ops.push(&value);
					const u32 local_watermark = locals.size();
					if (for_statement.is_key_value) locals.push({for_statement.key_var, &index});
					locals.push({for_statement.value_var, &value});

					auto& index_addr = alloc<LsOpPushLocalAddr>();
					index_addr.alloca = &index;
					auto& index_load = alloc<LsOpLoad>();
					index_load.addr = &index_addr;
					index_load.size = typeByteSize(index_type);
					auto& slice_addr = alloc<LsOpPushLocalAddr>();
					slice_addr.alloca = &slice;
					auto& slice_load = alloc<LsOpLoad>();
					slice_load.addr = &slice_addr;
					slice_load.size = typeByteSize(slice_type);
					auto& length = alloc<LsOpExtractValue>();
					length.value = &slice_load;
					length.offset = sizeof(void*);
					length.size = sizeof(i64);
					auto& condition = alloc<LsOpLt>();
					condition.operand_type = &index_type;
					condition.lhs = &index_load;
					condition.rhs = &length;

					auto& loop = alloc<LsOpConditionalJump>();
					auto& exit = alloc<LsOpNop>();
					loop.condition = &condition;
					loop.true_block = &alloc<LsIrBlockData>(host.arena);
					loops.push({pending_loop_label, &loop, &exit, (u32)defers.size()});
					pending_loop_label = {};

					auto& element = alloc<LsOpSliceLoad>();
					element.slice = &slice_load;
					element.index = &index_load;
					element.element_size = typeByteSize(*slice_type.element_type);
					auto& value_addr = alloc<LsOpPushLocalAddr>();
					value_addr.alloca = &value;
					auto& assign_value = alloc<LsOpCopy>();
					assign_value.type = slice_type.element_type;
					assign_value.src = &element;
					assign_value.dst = &value_addr;
					loop.true_block->ops.push(&assign_value);
					buildStatementIR(*for_statement.body, *loop.true_block);

					auto& increment_addr = alloc<LsOpPushLocalAddr>();
					increment_addr.alloca = &index;
					auto& increment_value = alloc<LsOpLoad>();
					increment_value.addr = &increment_addr;
					increment_value.size = typeByteSize(index_type);
					auto& one = alloc<LsOpLoadConst>();
					one.type = &index_type;
					const i64 one_value = 1;
					memcpy(one.value, &one_value, sizeof(one_value));
					auto& increment = alloc<LsOpAdd>();
					increment.operand_type = &index_type;
					increment.lhs = &increment_value;
					increment.rhs = &one;
					auto& store_addr = alloc<LsOpPushLocalAddr>();
					store_addr.alloca = &index;
					auto& store = alloc<LsOpCopy>();
					store.type = &index_type;
					store.src = &increment;
					store.dst = &store_addr;
					loop.true_block->ops.push(&store);
					loops.pop_back();
					locals.resize(local_watermark);
					auto& back_edge = alloc<LsOpJump>();
					back_edge.target = &loop;
					loop.true_block->ops.push(&back_edge);
					parent.ops.push(&loop);
					parent.ops.push(&exit);
					break;
				}
				ASSERT(!for_statement.end);
				ASSERT(for_statement.begin->resolved_type->kind == ResolvedType::ARRAY);
				auto& array = static_cast<ArrayResolvedType&>(*for_statement.begin->resolved_type);

				static ResolvedType index_type(ResolvedType::I32);
				auto& index_value = alloc<LsOpLoadConst>();
				index_value.type = &index_type;
				auto& index = alloc<LsOpAlloca>();
				index.type = &index_type;
				index.value = &index_value;
				parent.ops.push(&index);

				auto& value = alloc<LsOpAlloca>();
				value.type = array.element_type;
				value.value = &alloc<LsOpNop>();
				parent.ops.push(&value);
				const u32 local_watermark = locals.size();
				if (for_statement.is_key_value) locals.push({for_statement.key_var, &index});
				locals.push({for_statement.value_var, &value});

				auto& condition_index_addr = alloc<LsOpPushLocalAddr>();
				condition_index_addr.alloca = &index;
				auto& condition_index = alloc<LsOpLoad>();
				condition_index.addr = &condition_index_addr;
				condition_index.size = typeByteSize(index_type);
				auto& length = alloc<LsOpLoadConst>();
				length.type = &index_type;
				const i32 array_length = (i32)array.size;
				memcpy(length.value, &array_length, sizeof(array_length));
				auto& condition = alloc<LsOpLt>();
				condition.operand_type = &index_type;
				condition.lhs = &condition_index;
				condition.rhs = &length;

				auto& loop = alloc<LsOpConditionalJump>();
				auto& exit = alloc<LsOpNop>();
				loop.condition = &condition;
				loop.true_block = &alloc<LsIrBlockData>(host.arena);
				loops.push({pending_loop_label, &loop, &exit, (u32)defers.size()});
				pending_loop_label = {};

				auto& base = buildExpressionIR(*for_statement.begin, false);
				auto& element_index_addr = alloc<LsOpPushLocalAddr>();
				element_index_addr.alloca = &index;
				auto& element_index = alloc<LsOpLoad>();
				element_index.addr = &element_index_addr;
				element_index.size = typeByteSize(index_type);
				auto& element_size = alloc<LsOpLoadConst>();
				static ResolvedType offset_type(ResolvedType::U64);
				element_size.type = &offset_type;
				const u32 byte_size = typeByteSize(*array.element_type);
				const u64 pointer_scale = byte_size;
				memcpy(element_size.value, &pointer_scale, sizeof(pointer_scale));
				auto& pointer_index = alloc<LsOpCast>();
				pointer_index.type = &offset_type;
				pointer_index.target_type = &index_type;
				pointer_index.value = &element_index;
				auto& offset = alloc<LsOpMul>();
				offset.operand_type = &offset_type;
				offset.lhs = &pointer_index;
				offset.rhs = &element_size;
				auto& address = alloc<LsOpAdd>();
				address.operand_type = &offset_type;
				address.lhs = &base;
				address.rhs = &offset;
				address.result_mode = LsIrOp::ADDRESS;
				auto& element = alloc<LsOpLoad>();
				element.addr = &address;
				element.size = byte_size;
				auto& value_addr = alloc<LsOpPushLocalAddr>();
				value_addr.alloca = &value;
				auto& assign_value = alloc<LsOpCopy>();
				assign_value.type = array.element_type;
				assign_value.src = &element;
				assign_value.dst = &value_addr;
				loop.true_block->ops.push(&assign_value);
				buildStatementIR(*for_statement.body, *loop.true_block);

				auto& increment_addr = alloc<LsOpPushLocalAddr>();
				increment_addr.alloca = &index;
				auto& increment_value = alloc<LsOpLoad>();
				increment_value.addr = &increment_addr;
				increment_value.size = typeByteSize(index_type);
				auto& one = alloc<LsOpLoadConst>();
				one.type = &index_type;
				const i32 one_value = 1;
				memcpy(one.value, &one_value, sizeof(one_value));
				auto& increment = alloc<LsOpAdd>();
				increment.operand_type = &index_type;
				increment.lhs = &increment_value;
				increment.rhs = &one;
				auto& store_index_addr = alloc<LsOpPushLocalAddr>();
				store_index_addr.alloca = &index;
				auto& store_index = alloc<LsOpCopy>();
				store_index.type = &index_type;
				store_index.src = &increment;
				store_index.dst = &store_index_addr;
				loop.true_block->ops.push(&store_index);
				loops.pop_back();
				locals.resize(local_watermark);
				auto& back_edge = alloc<LsOpJump>();
				back_edge.target = &loop;
				loop.true_block->ops.push(&back_edge);
				parent.ops.push(&loop);
				parent.ops.push(&exit);
				break;
			}
			case Statement::MATCH: {
				auto& match = static_cast<MatchStatement&>(st);
				if (match.comptime_known) {
					ASSERT(match.comptime_arm >= 0 && match.comptime_arm < match.arms.size());
					buildStatementIR(*match.arms[match.comptime_arm].body, parent);
					break;
				}
				auto& subject = alloc<LsOpAlloca>();
				subject.type = match.subject->resolved_type;
				subject.value = &buildExpressionIR(*match.subject, true);
				parent.ops.push(&subject);

				LsIrBlockData* current = &parent;
				for (MatchArm& arm : match.arms) {
					if (arm.is_fallback) {
						buildStatementIR(*arm.body, *current);
						break;
					}
					LsIrBlockData* arm_body = &alloc<LsIrBlockData>(host.arena);
					for (u32 pattern_index = 0; pattern_index < arm.patterns.size(); ++pattern_index) {
						MatchPattern& pattern = arm.patterns[pattern_index];
						auto& subject_addr = alloc<LsOpPushLocalAddr>();
						subject_addr.alloca = &subject;
						auto& subject_value = alloc<LsOpLoad>();
						subject_value.addr = &subject_addr;
						subject_value.size = typeByteSize(*subject.type);

						LsIrOp* condition = nullptr;
						if (pattern.end) {
							auto& lower = alloc<LsOpGe>();
							lower.operand_type = subject.type;
							lower.lhs = &subject_value;
							lower.rhs = &buildExpressionIR(*pattern.begin, true);
							auto& upper_subject_addr = alloc<LsOpPushLocalAddr>();
							upper_subject_addr.alloca = &subject;
							auto& upper_subject = alloc<LsOpLoad>();
							upper_subject.addr = &upper_subject_addr;
							upper_subject.size = typeByteSize(*subject.type);
							auto& upper = alloc<LsOpLe>();
							upper.operand_type = subject.type;
							upper.lhs = &upper_subject;
							upper.rhs = &buildExpressionIR(*pattern.end, true);
							auto& range = alloc<LsOpAnd>();
							range.operand_type = subject.type;
							range.lhs = &lower;
							range.rhs = &upper;
							condition = &range;
						} else if (subject.type->kind == ResolvedType::UNION) {
							i32 member_index = pattern.union_member_index;
							ASSERT(member_index >= 0);
							auto& tag = alloc<LsOpExtractValue>();
							tag.value = &subject_value;
							tag.offset = 0;
							tag.size = sizeof(i32);
							static ResolvedType tag_type(ResolvedType::I32);
							auto& expected = alloc<LsOpLoadConst>();
							expected.type = &tag_type;
							memcpy(expected.value, &member_index, sizeof(member_index));
							auto& equality = alloc<LsOpEq>();
							equality.operand_type = &tag_type;
							equality.lhs = &tag;
							equality.rhs = &expected;
							condition = &equality;
						} else {
							auto& equality = alloc<LsOpEq>();
							equality.operand_type = subject.type;
							equality.lhs = &subject_value;
							equality.rhs = &buildExpressionIR(*pattern.begin, true);
							condition = &equality;
						}

						auto& branch = alloc<LsOpConditionalJump>();
						branch.condition = condition;
						branch.true_block = arm_body;
						branch.false_block = &alloc<LsIrBlockData>(host.arena);
						current->ops.push(&branch);
						current = branch.false_block;
					}
					buildStatementIR(*arm.body, *arm_body);
				}
				break;
			}
			case Statement::BREAK:
			case Statement::CONTINUE: {
				const bool is_break = st.kind == Statement::BREAK;
				const ls_string_view label = is_break ? static_cast<BreakStatement&>(st).label : static_cast<ContinueStatement&>(st).label;
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
				auto& jump = alloc<LsOpJump>();
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
				auto& rhs = buildImplicitConversionIR(*as.rhs, *as.lhs->resolved_type);
				auto& lhs = buildExpressionIR(*as.lhs, false);
				if (as.op == Token::EQUAL) {
					auto& cpy = alloc<LsOpCopy>();
					cpy.src = &rhs;
					cpy.dst = &lhs;
					cpy.type = as.lhs->resolved_type;
					parent.ops.push(&cpy);
					return;
				}
				auto& load = alloc<LsOpLoad>();
				load.addr = &lhs;
				load.size = typeByteSize(*as.lhs->resolved_type);
				LsIrOp* lhs_value = &load;
				if (as.resolved_op_fn) {
					auto& call = alloc<LsOpCallDirect>();
					call.function = as.resolved_op_fn;
					call.arg_count = 2;
					call.return_size = typeByteSize(*as.lhs->resolved_type);
					call.args = static_cast<LsIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(LsIrOp*) * call.arg_count, alignof(LsIrOp*)));
					call.args[0] = lhs_value;
					call.args[1] = &rhs;
					auto& cpy = alloc<LsOpCopy>();
					cpy.src = &call;
					cpy.dst = &lhs;
					cpy.type = as.lhs->resolved_type;
					parent.ops.push(&cpy);
					break;
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
				if (vd.is_comptime) break;
				auto& alloca = alloc<LsOpAlloca>();
				alloca.type = vd.resolved_type;
				alloca.value = &buildImplicitConversionIR(*vd.expression, *vd.resolved_type);
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
					ASSERT(return_type);
					ir_ret.size = typeByteSize(*return_type);
					LsIrOp& expression = buildImplicitConversionIR(*ret.expression, *return_type);
					if (defers.empty()) {
						ir_ret.expression = &expression;
					} else {
						auto& value = alloc<LsOpAlloca>();
						value.type = return_type;
						value.value = &expression;
						parent.ops.push(&value);
						auto& addr = alloc<LsOpPushLocalAddr>();
						addr.alloca = &value;
						auto& load = alloc<LsOpLoad>();
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
		ASSERT(!return_type);
		return_type = static_cast<FunctionResolvedType*>(expr.resolved_type)->return_type;
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
		root.ops.push(&alloc<LsOpReturn>());
		locals.clear();
		return_type = nullptr;
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
	ResolvedType* return_type = nullptr;
};

bool isTypeFactory(const FunctionExpression& function) {
	if (!function.return_type) return false;
	if (function.return_type->kind == Expression::TYPE_LITERAL) {
		return static_cast<const TypeLiteralExpression*>(function.return_type)->type == ResolvedType::META;
	}
	return function.return_type->resolved_type && function.return_type->resolved_type->kind == ResolvedType::META;
}

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
		, code(host.arena) {}

	template <typename T, typename... Args> T& alloc(Args&&... args) {
		T* v = (T*)host.arena.allocate(host.arena.user_data, sizeof(T), alignof(T));
		new (NewPlaceholder(), v) T(static_cast<Args&&>(args)...);
		return *v;
	}

	void emitOp(ls_op op) {
		u8 tmp = (u8)op;
		emit(&tmp, sizeof(tmp));
	}

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
			case ResolvedType::ISIZE: return 6;
			case ResolvedType::U64: return 7;
			case ResolvedType::F32: return 8;
			case ResolvedType::F64: return 9;
			default: ASSERT(false); return -1;
		}
	}

	u32 emitCompare(const LsOpBinary& cmp) {
		if (cmp.operand_type->kind == ResolvedType::SLICE) {
			const u32 lhs = emit(*cmp.lhs);
			const u32 rhs = emit(*cmp.rhs);
			const u32 result = stack_top++;
			const auto& slice = static_cast<const SliceResolvedType&>(*cmp.operand_type);
			emitOp(LS_OP_SLICE_EQ);
			emit(result);
			emit(lhs);
			emit(rhs);
			emit(typeByteSize(*slice.element_type));
			emit((u8)toTypeKind(*slice.element_type));
			if (cmp.kind == LS_IR_OP_NE) {
				emitOp(LS_OP_NOT);
				emit(result);
			}
			return result;
		}
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
		emit((u8)(cmp.operand_type->kind == ResolvedType::NULLABLE ? LS_TYPE_U8 : toTypeKind(*cmp.operand_type)));
		return result;
	}

	u32 emitUnary(const LsOpUnary& op) {
		const u32 operand = emit(*op.operand);
		if (op.kind == LS_IR_OP_NEG)
			emitOp(ls_op(LS_OP_NEG_I8 + numericKindIndex(*op.operand_type)));
		else
			emitOp(LS_OP_NOT);
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

	u32 emitTernary(const LsOpTernary& ternary) {
		const u32 condition = emit(*ternary.condition);
		const u32 size = typeByteSize(*ternary.type);
		const u32 result = stack_top;
		stack_top += size;

		emitOp(LS_OP_JZ_U8);
		emit(condition);
		const u32 false_jump = code.size();
		emit((i16)0);

		const u32 true_value = emit(*ternary.true_value);
		emitOp(LS_OP_COPY);
		emit(result);
		emit(true_value);
		emit(size);
		u32 high_water = stack_top;
		emitOp(LS_OP_JUMP);
		const u32 end_jump = code.size();
		emit((i16)0);

		patchI16(false_jump, code.size());
		stack_top = result + size;
		const u32 false_value = emit(*ternary.false_value);
		emitOp(LS_OP_COPY);
		emit(result);
		emit(false_value);
		emit(size);
		if (stack_top > high_water) high_water = stack_top;
		patchI16(end_jump, code.size());
		stack_top = high_water;
		return result;
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
			} else if (op->kind == LS_IR_OP_CONDITIONAL_JUMP) {
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

	u32 emitLoadBytes(const LsOpLoadBytes& load) {
		const u32 result = stack_top;
		u32 offset = 0;
		while (offset < load.size) {
			const u32 remaining = load.size - offset;
			const u32 size = remaining >= 8 ? 8u : remaining >= 4 ? 4u : remaining >= 2 ? 2u : 1u;
			emitOp(size == 8 ? LS_OP_LOAD_CONST_8 : size == 4 ? LS_OP_LOAD_CONST_4 : size == 2 ? LS_OP_LOAD_CONST_2 : LS_OP_LOAD_CONST_1);
			emit(stack_top);
			emit(load.value + offset, size);
			stack_top += size;
			offset += size;
		}
		return result;
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
		switch (copy.dst->result_mode) {
			case LsIrOp::ADDRESS: {
				emitOp(LS_OP_STORE_PTR);
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
		stack_top += typeByteSize(*aggregate.type);
		u32 offset = 0;
		for (u32 i = 0; i < aggregate.value_count; ++i) {
			const u32 value = emit(*aggregate.values[i]);
			const u32 size = aggregate.sizes							   ? aggregate.sizes[i]
							 : aggregate.type->kind == ResolvedType::ARRAY ? typeByteSize(*static_cast<ArrayResolvedType*>(aggregate.type)->element_type)
																		   : (u32)sizeof(u64);
			emitOp(LS_OP_COPY);
			emit(result + (aggregate.offsets ? aggregate.offsets[i] : offset));
			emit(value);
			emit(size);
			offset += size;
		}
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
			case ResolvedType::UNTYPED_INT: return LS_TYPE_I64;
			case ResolvedType::UNTYPED_FLOAT: return LS_TYPE_F64;
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

	u32 emitBoundsCheck(const LsOpBoundsCheck& op) {
		const u32 index = emit(*op.index);
		emitOp(LS_OP_BOUNDS_CHECK);
		emit(index);
		emit((u8)toTypeKind(*op.index_type));
		emit(op.length);
		return index;
	}

	u32 emitCast(const LsOpCast& cast) {
		u32 value_sp = emit(*cast.value);
		if (cast.target_type->kind == ResolvedType::SLICE && cast.type->kind == ResolvedType::SLICE) {
			auto& source = static_cast<SliceResolvedType&>(*cast.target_type);
			auto& target = static_cast<SliceResolvedType&>(*cast.type);
			static ResolvedType i64_type(ResolvedType::I64);
			u32 length_sp = stack_top;
			emitOp(LS_OP_SLICE_LENGTH);
			emit(length_sp);
			emit(value_sp);
			stack_top += sizeof(i64);
			const u32 source_size = typeByteSize(*source.element_type);
			const u32 target_size = typeByteSize(*target.element_type);
			if (source_size != target_size) {
				auto& ratio = alloc<LsOpLoadConst>();
				ratio.type = &i64_type;
				const bool shrink = target_size >= source_size;
				const i64 factor = shrink ? (source_size ? (i64)target_size / source_size : 1) : (target_size ? (i64)source_size / target_size : 1);
				memcpy(ratio.value, &factor, sizeof(factor));
				const u32 ratio_sp = emit(static_cast<LsIrOp&>(ratio));
				const u32 scaled_sp = stack_top;
				emitOp(shrink ? LS_OP_DIV_I64 : LS_OP_MUL_I64);
				emit(scaled_sp);
				emit(length_sp);
				emit(ratio_sp);
				stack_top += sizeof(i64);
				length_sp = scaled_sp;
			}
			const u32 result = stack_top;
			stack_top += typeByteSize(*cast.type);
			emitOp(LS_OP_COPY);
			emit(result);
			emit(value_sp);
			emit(sizeof(void*));
			emitOp(LS_OP_COPY);
			emit(result + sizeof(void*));
			emit(length_sp);
			emit(sizeof(i64));
			return result;
		}
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
		ASSERT(op.alloca->stack_sp != 0xffFFffFF);
		emitOp(LS_OP_FRAME_PTR);
		emit(stack_top);
		emit(op.alloca->stack_sp);
		stack_top += sizeof(void*);
		return stack_top - sizeof(void*);
	}

	u32 emitMaterializeAddr(const LsOpMaterializeAddr& op) {
		const u32 value_sp = emit(*op.value);
		emitOp(LS_OP_FRAME_PTR);
		emit(stack_top);
		emit(value_sp);
		stack_top += sizeof(void*);
		return stack_top - sizeof(void*);
	}

	u32 emitLoad(const LsOpLoad& op) {
		u32 addr_sp = emit(*op.addr);
		emitOp(LS_OP_LOAD_PTR);
		u32 ret = stack_top;
		emit(stack_top);
		emit(addr_sp);
		emit(op.size);
		stack_top += op.size;
		return ret;
	}

	u32 emitPushGlobalAddr(const LsOpPushGlobalAddr& op) {
		emitOp(LS_OP_GLOBAL_PTR);
		u32 ret = stack_top;
		emit(ret);
		emit(op.offset);
		stack_top += sizeof(void*);
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
		if (stack_top > stack_high_water) stack_high_water = stack_top;
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
		if (stack_top > stack_high_water) stack_high_water = stack_top;
		emitOp(LS_OP_CALL_INDIRECT);
		emit(callee);
		emit(arg_size);
		emit(call.return_size);
		stack_top = callee + call.return_size;
		return callee;
	}

	u32 emitNull(const LsOpNull& op) {
		u32 ret = stack_top;
		u32 remaining = op.size;
		while (remaining >= 8) {
			emitOp(LS_OP_LOAD_CONST_8);
			emit(stack_top);
			emit((u64)0);
			stack_top += 8;
			remaining -= 8;
		}
		while (remaining > 0) {
			emitOp(LS_OP_LOAD_CONST_1);
			emit(stack_top);
			emit((u8)0);
			++stack_top;
			--remaining;
		}
		return ret;
	}

	u32 emitSlice(const LsOpSlice& op) {
		u32 result;
		if (op.source_is_scalar) {
			const u32 source = emit(*op.source);
			result = stack_top;
			emitOp(LS_OP_COPY);
			emit(result);
			emit(source);
			emit((u32)sizeof(void*));
			stack_top += sizeof(void*);
			emitOp(LS_OP_LOAD_CONST_8);
			emit(stack_top);
			emit(op.source_length);
			stack_top += sizeof(i64);
		} else if (op.source_is_array) {
			ASSERT(op.source->result_mode == LsIrOp::ADDRESS);
			result = emit(*op.source);
			emitOp(LS_OP_LOAD_CONST_8);
			emit(stack_top);
			emit(op.source_length);
			stack_top += sizeof(i64);
		} else {
			result = emit(*op.source);
		}

		if (!op.begin && !op.end) return result;

		static ResolvedType index_type(ResolvedType::I64);
		auto emitBound = [&](LsIrOp* bound, i64 fallback) {
			if (bound) return emit(*bound);
			auto& constant = alloc<LsOpLoadConst>();
			constant.type = &index_type;
			memcpy(constant.value, &fallback, sizeof(fallback));
			return emit(static_cast<LsIrOp&>(constant));
		};
		const u32 begin = emitBound(op.begin, 0);
		u32 end;
		if (op.end) {
			end = emit(*op.end);
		} else if (op.source_is_array) {
			end = emitBound(nullptr, op.source_length);
		} else {
			end = stack_top;
			emitOp(LS_OP_SLICE_LENGTH);
			emit(stack_top);
			emit(result);
			stack_top += sizeof(i64);
		}
		emitOp(LS_OP_SLICE);
		emit(result);
		emit(begin);
		emit(end);
		emit(op.element_size);
		return result;
	}

	u32 emitSliceLoad(const LsOpSliceLoad& op) {
		const u32 slice = emit(*op.slice);
		const u32 index = emit(*op.index);
		const u32 result = stack_top;
		emitOp(LS_OP_SLICE_LOAD);
		emit(result);
		emit(slice);
		emit(index);
		emit(op.element_size);
		stack_top += op.element_size;
		return result;
	}

	u32 emitSliceRef(const LsOpSliceRef& op) {
		const u32 slice = emit(*op.slice);
		const u32 index = emit(*op.index);
		emitOp(LS_OP_SLICE_REF);
		emit(slice);
		emit(index);
		emit(op.element_size);
		return slice;
	}

	u32 emit(LsIrOp& op) {
		op.bytecode_offset = code.size();
		switch (op.kind) {
			case LS_IR_OP_SLICE_REF: return emitSliceRef(static_cast<LsOpSliceRef&>(op));
			case LS_IR_OP_SLICE_LOAD: return emitSliceLoad(static_cast<LsOpSliceLoad&>(op));
			case LS_IR_OP_SLICE: return emitSlice(static_cast<LsOpSlice&>(op));
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
			case LS_IR_OP_EXTRACT_VALUE: return emitExtractValue(*static_cast<LsOpExtractValue*>(&op));
			case LS_IR_OP_LOAD: return emitLoad(*static_cast<LsOpLoad*>(&op));
			case LS_IR_OP_PUSH_LOCAL_ADDR: return emitPushLocalAddr(*static_cast<LsOpPushLocalAddr*>(&op));
			case LS_IR_OP_MATERIALIZE_ADDR: return emitMaterializeAddr(*static_cast<LsOpMaterializeAddr*>(&op));
			case LS_IR_OP_PUSH_GLOBAL_ADDR: return emitPushGlobalAddr(*static_cast<LsOpPushGlobalAddr*>(&op));
			case LS_IR_OP_EQ:
			case LS_IR_OP_NE:
			case LS_IR_OP_LT:
			case LS_IR_OP_LE:
			case LS_IR_OP_GT:
			case LS_IR_OP_GE: return emitCompare(static_cast<LsOpBinary&>(op));
			case LS_IR_OP_COPY: return emitCopy(*static_cast<LsOpCopy*>(&op));
			case LS_IR_OP_CAST: return emitCast(static_cast<LsOpCast&>(op));
			case LS_IR_OP_TERNARY: return emitTernary(static_cast<LsOpTernary&>(op));
			case LS_IR_OP_BOUNDS_CHECK: return emitBoundsCheck(static_cast<LsOpBoundsCheck&>(op));
			case LS_IR_OP_ALLOCA: return emitAlloca(*static_cast<LsOpAlloca*>(&op));
			case LS_IR_OP_MUL: return emitBinary(LS_OP_MUL_I8, static_cast<LsOpBinary&>(op));
			case LS_IR_OP_ADD: return emitBinary(LS_OP_ADD_I8, static_cast<LsOpBinary&>(op));
			case LS_IR_OP_SUB: return emitBinary(LS_OP_SUB_I8, static_cast<LsOpBinary&>(op));
			case LS_IR_OP_DIV: return emitBinary(LS_OP_DIV_I8, static_cast<LsOpBinary&>(op));
			case LS_IR_OP_MOD: return emitBinary(LS_OP_MOD_I8, static_cast<LsOpBinary&>(op));
			case LS_IR_OP_LOAD_CONST: return emitLoadConst(static_cast<LsOpLoadConst&>(op));
			case LS_IR_OP_LOAD_BYTES: return emitLoadBytes(static_cast<LsOpLoadBytes&>(op));
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
		const u32 result = emit(*ret.expression);
		emitOp(LS_OP_RETURN);
		emit(result);
		emit(ret.size);
		return result;
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
		stack_high_water = 0;

		ResolvedType* return_type = static_cast<FunctionResolvedType*>(fn_expr.resolved_type)->return_type;

		fn_bc->code = (u8*)(u64)code.size(); // store offset since code.data may be reallocated
		fn_bc->kind = fn_expr.is_extern ? LS_FUNCTION_NATIVE : LS_FUNCTION_SCRIPT;
		fn_bc->is_builtin_native = false; // is_builtin_native; // TODO
		fn_bc->param_size = 0;
		for (const FunctionParam& param : fn_expr.params) {
			if (!param.is_comptime) fn_bc->param_size += typeByteSize(*param.resolved_type);
		}
		stack_top = fn_bc->param_size;
		stack_high_water = stack_top;
		// fn_bc->return_kind = toTypeKind(*return_type); // TODO
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
		fn_bc->frame_size = stack_top > stack_high_water ? stack_top : stack_high_water;
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
	u32 stack_high_water = 0;
};

} // namespace

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
			if (isTypeFactory(fn_expr)) continue;
			if (fn_expr.is_template) continue;
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
				if (isTypeFactory(fn_expr)) continue;
				if (fn_expr.is_template) continue;
				ls_function_bc& fn_bc = bc->functions[fn_expr.bytecode_index];
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
				const u32 dst = bc_compiler.stack_top;
				bc_compiler.emitOp(LS_OP_GLOBAL_PTR);
				bc_compiler.emit(dst);
				bc_compiler.emit(s.slot.offset);
				bc_compiler.stack_top += sizeof(void*);
				bc_compiler.emitOp(LS_OP_STORE_PTR);
				bc_compiler.emit(dst);
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
