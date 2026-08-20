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

// Source locations for LsIrSourceLoc. IR ops store a u32 index into the
// module's SourceLocTable (token.h); the BytecodeCompiler records it as-is and
// ls_bytecode_compile copies the table verbatim into the bytecode.
// AST to IR
struct IRBuilder {
	IRBuilder(ls_host& host)
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

	LsOpAlloca& allocAlloca(ResolvedType* type, ls_string_view name, LsIrOp* value) {
		auto& alloca = alloc<LsOpAlloca>();
		alloca.type = type;
		alloca.name = name;
		alloca.value = value;
		alloca.stack_sp = stack_cursor;
		stack_cursor += typeByteSize(*type);
		return alloca;
	}

	void assignSrcLoc(LsIrOp& op) { op.src_loc = current_src_loc; }
	void assignSrcLoc(LsIrBlockData&) {}

	LsIrOp& buildImplicitConversionIR(Expression& expression, ResolvedType& target_type) {
		if (expression.kind == Expression::UNDEFINED) {
			// TODO should undefined do nothing?
			auto& storage = allocAlloca(&target_type, {}, &alloc<LsOpNop>());
			return storage;
		}
		if (target_type.kind == ResolvedTypeKind::UNION && expression.resolved_type) {
			return convertValue(buildExpressionIR(expression, true), *expression.resolved_type, target_type);
		}
		if (target_type.kind == ResolvedTypeKind::NULLABLE) {
			ResolvedType* inner = static_cast<NullableResolvedType&>(target_type).inner;
			if (expression.resolved_type->kind == ResolvedTypeKind::NULLABLE || expression.kind == Expression::NULL_LITERAL) {
				return buildExpressionIR(expression, true);
			}
			auto& aggregate = alloc<LsOpAggregateInit>();
			aggregate.type = &target_type;
			const u32 payload_size = typeByteSize(*inner);
			aggregate.value_count = 2;
			aggregate.values = static_cast<LsIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(LsIrOp*) * 2, alignof(LsIrOp*)));
			aggregate.offsets = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * 2, alignof(u32)));
			aggregate.sizes = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * 2, alignof(u32)));
			auto& flag = alloc<LsOpLoadConst>();
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

	LsIrOp& wrapUnionMember(LsIrOp& payload, ResolvedType& member_type, UnionResolvedType& dest) {
		const i32 member_index = unionMemberIndex(dest, &member_type);
		if (member_index < 0) return payload;
		auto& aggregate = alloc<LsOpAggregateInit>();
		aggregate.type = &dest;
		aggregate.value_count = 2;
		aggregate.values = static_cast<LsIrOp**>(host.arena.allocate(host.arena.user_data, sizeof(LsIrOp*) * 2, alignof(LsIrOp*)));
		aggregate.offsets = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * 2, alignof(u32)));
		aggregate.sizes = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * 2, alignof(u32)));
		auto& tag = alloc<LsOpLoadConst>();
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

	LsIrOp& convertValue(LsIrOp& value, ResolvedType& source, ResolvedType& dest) {
		if (typesEqual(&source, &dest)) return value;
		if (dest.kind == ResolvedTypeKind::UNION) {
			auto& dest_union = static_cast<UnionResolvedType&>(dest);
			if (source.kind == ResolvedTypeKind::UNION) {
				auto& convert = alloc<LsOpUnionConvert>();
				convert.source_type = &source;
				convert.target_type = &dest;
				convert.value = &value;
				return convert;
			}
			return wrapUnionMember(value, source, dest_union);
		}
		if (source.kind == ResolvedTypeKind::UNION) {
			auto& payload = alloc<LsOpExtractValue>();
			payload.value = &value;
			payload.offset = sizeof(i32);
			payload.size = typeByteSize(dest);
			return payload;
		}
		return value;
	}

	// Runtime slice indexing and slice bounds are always read back as a fixed
	// 8-byte i64 (SLICE_LOAD / SLICE_REF / SLICE in runtime.c). Widen narrower
	// integer indices so the upper four bytes are not stale stack data.
	LsIrOp& widenToI64(LsIrOp& value, ResolvedType& source_type) {
		switch (source_type.kind) {
			case ResolvedTypeKind::I64:
			case ResolvedTypeKind::U64:
			case ResolvedTypeKind::ISIZE:
			case ResolvedTypeKind::UNTYPED_INT:
				return value;
			default: break;
		}
		auto& cast = alloc<LsOpCast>();
		static ResolvedType i64_type(ResolvedTypeKind::I64);
		cast.type = &i64_type;
		cast.target_type = &source_type;
		cast.value = &value;
		return cast;
	}

	LsIrOp& loadAllocaValue(LsOpAlloca& storage) {
		auto& addr = alloc<LsOpPushLocalAddr>();
		addr.alloca = &storage;
		auto& load = alloc<LsOpLoad>();
		load.addr = &addr;
		load.size = typeByteSize(*storage.type);
		return load;
	}

	LsIrOp& offsetAddress(LsIrOp& address, u64 bytes) {
		if (bytes == 0) return address;
		auto& add = alloc<LsOpAdd>();
		auto& offset = alloc<LsOpLoadConst>();
		static ResolvedType pointer_type(ResolvedTypeKind::U64);
		memcpy(offset.value, &bytes, sizeof(bytes));
		offset.type = &pointer_type;
		add.lhs = &address;
		add.rhs = &offset;
		add.operand_type = &pointer_type;
		add.result_mode = LsIrOp::ADDRESS;
		return add;
	}

	void buildReturnIR(LsIrBlockData& block, LsIrOp* value) {
		auto& ir_ret = alloc<LsOpReturn>();
		if (value && return_type && return_type->kind != ResolvedTypeKind::VOID) {
			ir_ret.size = typeByteSize(*return_type);
			if (defers.empty()) {
				ir_ret.expression = value;
			} else {
				auto& stored = allocAlloca(return_type, {}, value);
				block.ops.push(&stored);
				auto& addr = alloc<LsOpPushLocalAddr>();
				addr.alloca = &stored;
				auto& load = alloc<LsOpLoad>();
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

	u32 internString(ls_string_view value) {
		for (i32 i = 0, c = strings.size(); i < c; ++i) {
			if (equalStrings(value, strings[i])) return i;
		}
		u32 len = u32(value.end - value.begin);
		char* tmp = (char*)host.arena.allocate(host.arena.user_data, len + 1, 1);
		memcpy(tmp, value.begin, len);
		tmp[len] = 0;
		strings.push({tmp, tmp + len});
		return strings.size() - 1;
	}

	LsIrOp& buildExpressionIR(Expression& expr, bool as_rvalue) {
		SourceScope scope(*this, expr.token);
		if (as_rvalue && expr.comptime_value.kind == ComptimeValue::VALUE && expr.resolved_type) {
			const u32 size = typeByteSize(*expr.resolved_type);
			auto& constant = alloc<LsOpLoadBytes>();
			constant.type = expr.resolved_type;
			constant.value = expr.comptime_value.value;
			constant.size = size;
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
				LsIrOp& pointer = buildExpressionIR(*dereference.subject, true);
				pointer.result_mode = LsIrOp::ADDRESS;
				ResolvedType* storage = dereference.subject->kind == Expression::IDENTIFIER
					? identifierStorageType(static_cast<IdentifierExpression&>(*dereference.subject))
					: dereference.subject->resolved_type;
				ResolvedType* pointee = storage && storage->kind == ResolvedTypeKind::POINTER
					? static_cast<PointerResolvedType*>(storage)->inner : nullptr;
				LsIrOp& address = offsetAddress(pointer, payloadOffset(pointee, expr.resolved_type));
				if (!as_rvalue) return address;
				auto& load = alloc<LsOpLoad>();
				load.addr = &address;
				load.size = typeByteSize(*expr.resolved_type);
				return load;
			}
			case Expression::SLICE: {
				auto& slice = static_cast<SliceExpression&>(expr);
				auto& op = alloc<LsOpSlice>();
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
						static ResolvedType offset_type(ResolvedTypeKind::U64);
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
				
				// slice[index]
				if (be.base->resolved_type->kind == ResolvedTypeKind::SLICE) {
					ASSERT(be.args.size() == 1);
					LsIrOp& index = widenToI64(buildExpressionIR(*be.args[0], true), *be.args[0]->resolved_type);
					if (as_rvalue) {
					auto& load = alloc<LsOpSliceLoad>();
						load.slice = &buildExpressionIR(*be.base, true);
						load.index = &index;
						load.element_size = typeByteSize(*be.resolved_type);
						return load;
					}
					auto& ref = alloc<LsOpSliceRef>();
					ref.slice = &buildExpressionIR(*be.base, true);
					ref.index = &index;
					ref.element_size = typeByteSize(*be.resolved_type);
					return ref;
				}

				// array[index]
				ASSERT(be.base->resolved_type->kind == ResolvedTypeKind::ARRAY);
				ASSERT(be.args.size() == 1);
				LsIrOp& base = buildExpressionIR(*be.base, false);
				// A temporary array (for example, a function call returning an array)
				// is a value, not an address. Materialize its frame address before
				// applying the element offset.
				LsIrOp* base_address = &base;
				if (base.result_mode == LsIrOp::VALUE) {
					auto& address = alloc<LsOpMaterializeAddr>();
					address.value = &base;
					base_address = &address;
				}
				LsIrOp& index = buildExpressionIR(*be.args[0], true);
				auto& bounds_check = alloc<LsOpBoundsCheck>();
				bounds_check.index_type = be.args[0]->resolved_type;
				bounds_check.index = &index;
				bounds_check.length = (u64) static_cast<ArrayResolvedType*>(be.base->resolved_type)->size;
				LsIrOp* checked_index = &bounds_check;
				static ResolvedType R(ResolvedTypeKind::U64);
				if (be.args[0]->resolved_type->kind != ResolvedTypeKind::U64) {
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
				add.lhs = base_address;
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
				if (me.resolved_fn) {
					ASSERT(as_rvalue);
					auto& value = alloc<LsOpLoadConst>();
					value.type = expr.resolved_type;
					memcpy(value.value, &me.resolved_fn->bytecode_index, sizeof(me.resolved_fn->bytecode_index));
					return value;
				}

				// namespace.comptime
				if (me.resolved_symbol && me.resolved_symbol->storage == Symbol::COMPTIME) {
					ASSERT(as_rvalue);
					auto& value = alloc<LsOpLoadBytes>();
					value.type = expr.resolved_type;
					value.value = me.resolved_symbol->comptime_value.value;
					value.size = typeByteSize(*expr.resolved_type);
					return value;
				}

				// namespace.global
				if (me.resolved_symbol && me.resolved_symbol->slot.storage == StorageSlot::GLOBAL) {
					auto& address = alloc<LsOpPushGlobalAddr>();
					address.offset = me.resolved_symbol->slot.offset;
					if (!as_rvalue) return address;
					auto& load = alloc<LsOpLoad>();
					load.addr = &address;
					load.size = typeByteSize(*expr.resolved_type);
					return load;
				}

				// slice.length
				if (me.expression && me.expression->resolved_type && me.expression->resolved_type->kind == ResolvedTypeKind::SLICE) {
					ASSERT(equalStrings(me.name, makeStringView("length")));
					auto& length = alloc<LsOpExtractValue>();
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
					auto& op = alloc<LsOpLoadConst>();
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
				u32 offset = 0;
				for (u32 i = 0; i < me.struct_field_index; ++i) {
					offset += typeByteSize(*struct_type->field_types[i]);
				}

				auto& base = buildExpressionIR(*me.expression, pointer_base);
				
				if (as_rvalue && base.result_mode == LsIrOp::VALUE) {
					auto& extract = alloc<LsOpExtractValue>();
					extract.value = &base;
					extract.offset = offset;
					extract.size = typeByteSize(*me.resolved_type);
					return extract;
				}

				auto& add = alloc<LsOpAdd>();
				add.lhs = &base;
				auto& rhs = alloc<LsOpLoadConst>();
				static ResolvedType R(ResolvedTypeKind::U64);
				const u64 pointer_offset = offset;
				memcpy(&rhs.value, &pointer_offset, sizeof(pointer_offset));
				rhs.type = &R;
				add.rhs = &rhs;
				add.operand_type = &R;
				add.result_mode = LsIrOp::ADDRESS;
				if (!as_rvalue) return add;

				auto& load = alloc<LsOpLoad>();
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
					auto& op = alloc<LsOpPushGlobalAddr>();
					op.offset = global_slot->offset;
					LsIrOp& addr = offsetAddress(op, payloadOffset(global_slot->type, ie.resolved_type));
					if (!as_rvalue) return addr;
					auto& load = alloc<LsOpLoad>();
					load.addr = &addr;
					load.size = typeByteSize(*ie.resolved_type);
					return load;
				}

				for (i32 i = locals.size() - 1; i >= 0; --i) {
					if (!equalStrings(locals[i].name, ie.name)) continue;

					auto& addr = alloc<LsOpPushLocalAddr>();
					addr.alloca = locals[i].alloca;
					LsIrOp& value_addr = offsetAddress(addr, payloadOffset(locals[i].alloca->type, ie.resolved_type));
					if (as_rvalue) {
						auto& load = alloc<LsOpLoad>();
						load.addr = &value_addr;
						load.size = typeByteSize(*ie.resolved_type);
						return load;
					}
					return value_addr;
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
					const bool is_ufcs = receiver_type && (receiver_type->kind == ResolvedTypeKind::STRUCT || receiver_type->kind == ResolvedTypeKind::ENUM || receiver_type->kind == ResolvedTypeKind::POINTER);
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

				ASSERT(call.callee->resolved_type && call.callee->resolved_type->kind == ResolvedTypeKind::FUNCTION);
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
				if (op.type->kind == ResolvedTypeKind::STRUCT) {
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
				static ResolvedType default_type(ResolvedTypeKind::I32);
				op.type = expr.resolved_type->kind == ResolvedTypeKind::UNTYPED_INT ? &default_type : expr.resolved_type;
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
				auto& ir_literal = alloc<LsOpStringLiteral>();
				ir_literal.index = internString(literal.value);
				ir_literal.length = u32(literal.value.end - literal.value.begin);
				return ir_literal;
			}
			case Expression::TYPE_MEMBER: {
				auto& member = static_cast<TypeMemberExpression&>(expr);
				switch (member.kind) { case TypeMemberExpression::NAME: {
						auto& pointer = alloc<LsOpLoadConst>();
						static ResolvedType pointer_type(ResolvedTypeKind::CPTR);
						pointer.type = &pointer_type;
						memcpy(pointer.value, &member.comptime_string.begin, sizeof(member.comptime_string.begin));
						auto& length = alloc<LsOpLoadConst>();
						static ResolvedType length_type(ResolvedTypeKind::I64);
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
					case TypeMemberExpression::MIN:
					case TypeMemberExpression::MAX: {
						ASSERT(member.reflected_type && expr.resolved_type);
						const bool is_min = member.kind == TypeMemberExpression::MIN;
						auto& value = alloc<LsOpLoadConst>();
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
						return alloc<LsOpNop>();
				}
			}
			case Expression::INT_LITERAL: {
				auto& ile = static_cast<IntLiteralExpression&>(expr);
				auto& op = alloc<LsOpLoadConst>();
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
				auto& op = alloc<LsOpLoadConst>();
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
					ASSERT(be.lhs->resolved_type->kind == ResolvedTypeKind::UNION);
					ASSERT(be.rhs->resolved_type->kind == ResolvedTypeKind::META);
					auto& tag = alloc<LsOpExtractValue>();
					tag.value = &lhs;
					tag.offset = 0;
					tag.size = sizeof(i32);
					i32 member_index = be.union_member_index;
					ASSERT(member_index >= 0);
					static ResolvedType tag_type(ResolvedTypeKind::I32);
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
		SourceScope scope(*this, st.token);
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
				const bool is_range = for_statement.end != nullptr;
				const u32 local_watermark = locals.size();

				// `unroll for` over a comptime slice is expanded into straight-line
				// copies by the checker, which also folds the loop variable bindings
				// into comptime constants. Emit each copy with its own loop context so
				// `break`/`continue` emit real runtime branches: `continue` jumps to
				// the next copy (or out after the last), `break` jumps past all copies.
				if (for_statement.is_expanded) {
					auto& expanded = static_cast<BlockStatement&>(*for_statement.body);
					LsOpNop& exit = alloc<LsOpNop>();
					for (u32 i = 0; i < expanded.statements.size(); ++i) {
						LsOpNop& next = alloc<LsOpNop>();
						const bool is_last = i + 1 == expanded.statements.size();
						loops.push({pending_loop_label, is_last ? static_cast<LsIrOp*>(&exit) : static_cast<LsIrOp*>(&next), &exit, (u32)defers.size()});
						buildStatementIR(*expanded.statements[i], parent);
						loops.pop_back();
						if (!is_last) parent.ops.push(&next);
					}
					pending_loop_label = {};
					parent.ops.push(&exit);
					break;
				}

				const auto finishLoopIteration = [&](LsIrBlockData* block, LsOpConditionalJump& loop, LsOpNop& increment_target, LsOpNop& exit, LsOpAlloca* counter, ResolvedType* counter_type) {
					block->ops.push(&increment_target);
					auto& addr = alloc<LsOpPushLocalAddr>();
					addr.alloca = counter;
					auto& load = alloc<LsOpLoad>();
					load.addr = &addr;
					load.size = typeByteSize(*counter_type);
					auto& one = alloc<LsOpLoadConst>();
					one.type = counter_type;
					const u64 one_value = 1;
					memcpy(one.value, &one_value, typeByteSize(*counter_type));
					auto& increment = alloc<LsOpAdd>();
					increment.operand_type = counter_type;
					increment.lhs = &load;
					increment.rhs = &one;
					auto& dst = alloc<LsOpPushLocalAddr>();
					dst.alloca = counter;
					auto& store = alloc<LsOpCopy>();
					store.type = counter_type;
					store.src = &increment;
					store.dst = &dst;
					block->ops.push(&store);
					loops.pop_back();
					locals.resize(local_watermark);
					auto& back_edge = alloc<LsOpJump>();
					back_edge.target = &loop;
					block->ops.push(&back_edge);
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
					finishLoopIteration(loop.true_block, loop, increment_target, exit, &value, value_type);
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

				auto& zero = alloc<LsOpLoadConst>();
				zero.type = index_type;
				const u64 zero_value = 0;
				memcpy(zero.value, &zero_value, typeByteSize(*index_type));

				auto& counter = allocAlloca(index_type, {}, &zero);
				parent.ops.push(&counter);

				auto& value = allocAlloca(element_type, for_statement.value_var, &alloc<LsOpNop>());
				parent.ops.push(&value);
				
				if (for_statement.is_key_value) {
					counter.name = for_statement.key_var;
					locals.push({for_statement.key_var, &counter});
				}
				locals.push({for_statement.value_var, &value});

				auto& counter_addr = alloc<LsOpPushLocalAddr>();
				counter_addr.alloca = &counter;
				auto& counter_load = alloc<LsOpLoad>();
				counter_load.addr = &counter_addr;
				counter_load.size = typeByteSize(*index_type);
				auto& container_addr = alloc<LsOpPushLocalAddr>();
				container_addr.alloca = &container;
				auto& container_load = alloc<LsOpLoad>();
				container_load.addr = &container_addr;
				container_load.size = typeByteSize(*container_type);
				LsIrOp* boundary = nullptr;
				if (is_slice) {
					auto& length = alloc<LsOpExtractValue>();
					length.value = &container_load;
					length.offset = sizeof(void*);
					length.size = sizeof(i64);
					boundary = &length;
				}
				else {
					auto& length = alloc<LsOpLoadConst>();
					length.type = index_type;
					const i32 array_length = (i32)static_cast<ArrayResolvedType*>(container_type)->size;
					memcpy(length.value, &array_length, sizeof(array_length));
					boundary = &length;
				}
				auto& condition = alloc<LsOpLt>();
				condition.operand_type = index_type;
				condition.lhs = &counter_load;
				condition.rhs = boundary;

				auto& loop = alloc<LsOpConditionalJump>();
				auto& increment_target = alloc<LsOpNop>();
				auto& exit = alloc<LsOpNop>();
				loop.condition = &condition;
				loop.true_block = &alloc<LsIrBlockData>(host.arena);
				loops.push({pending_loop_label, &increment_target, &exit, (u32)defers.size()});
				pending_loop_label = {};

				LsIrOp* element = nullptr;
				if (is_slice) {
					auto& load = alloc<LsOpSliceLoad>();
					load.slice = &container_load;
					load.index = &counter_load;
					load.element_size = typeByteSize(*element_type);
					element = &load;
				}
				else {
					auto& base = buildExpressionIR(*for_statement.begin, false);
					auto& byte_size = alloc<LsOpLoadConst>();
					static ResolvedType offset_type(ResolvedTypeKind::U64);
					byte_size.type = &offset_type;
					const u32 size = typeByteSize(*element_type);
					const u64 pointer_scale = size;
					memcpy(byte_size.value, &pointer_scale, sizeof(pointer_scale));
					auto& casted_index = alloc<LsOpCast>();
					casted_index.type = &offset_type;
					casted_index.target_type = index_type;
					casted_index.value = &counter_load;
					auto& offset = alloc<LsOpMul>();
					offset.operand_type = &offset_type;
					offset.lhs = &casted_index;
					offset.rhs = &byte_size;
					auto& address = alloc<LsOpAdd>();
					address.operand_type = &offset_type;
					address.lhs = &base;
					address.rhs = &offset;
					address.result_mode = LsIrOp::ADDRESS;
					auto& load = alloc<LsOpLoad>();
					load.addr = &address;
					load.size = size;
					element = &load;
				}
				auto& value_addr = alloc<LsOpPushLocalAddr>();
				value_addr.alloca = &value;
				auto& assign_value = alloc<LsOpCopy>();
				assign_value.type = element_type;
				assign_value.src = element;
				assign_value.dst = &value_addr;
				loop.true_block->ops.push(&assign_value);
				buildStatementIR(*for_statement.body, *loop.true_block);
				finishLoopIteration(loop.true_block, loop, increment_target, exit, &counter, index_type);
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

				LsIrBlockData* current = &parent;
				MatchArm* fallback_arm = nullptr;
				for (MatchArm& arm : match.arms) {
					if (arm.is_fallback) {
						fallback_arm = &arm;
						continue;
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
						} else if (subject.type->kind == ResolvedTypeKind::UNION) {
							i32 member_index = pattern.union_member_index;
							ASSERT(member_index >= 0);
							auto& tag = alloc<LsOpExtractValue>();
							tag.value = &subject_value;
							tag.offset = 0;
							tag.size = sizeof(i32);
							static ResolvedType tag_type(ResolvedTypeKind::I32);
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
				if (fallback_arm) {
					buildStatementIR(*fallback_arm->body, *current);
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
				if (vd.else_return) {
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
					auto& tag = alloc<LsOpExtractValue>();
					tag.value = &loadAllocaValue(tmp);
					tag.offset = 0;
					tag.size = sizeof(i32);
					LsIrOp* condition = nullptr;
					for (i32 i = 0; i < source_union.members.size(); ++i) {
						if ((vd.else_return_target_mask & (1ull << (u32)i)) == 0) continue;
						auto& expected = alloc<LsOpLoadConst>();
						expected.type = &tag_type;
						memcpy(expected.value, &i, sizeof(i));
						auto& eq = alloc<LsOpEq>();
						eq.operand_type = &tag_type;
						eq.lhs = &tag;
						eq.rhs = &expected;
						if (!condition) {
							condition = &eq;
							continue;
						}
						auto& or_op = alloc<LsOpOr>();
						or_op.operand_type = &bool_type;
						or_op.lhs = condition;
						or_op.rhs = &eq;
						condition = &or_op;
					}
					ASSERT(condition);

					auto& branch = alloc<LsOpConditionalJump>();
					branch.condition = condition;
					branch.true_block = &alloc<LsIrBlockData>(host.arena);
					branch.false_block = &alloc<LsIrBlockData>(host.arena);

					auto& dest = allocAlloca(vd.resolved_type, vd.name, &convertValue(loadAllocaValue(tmp), *tmp.type, *vd.resolved_type));
					branch.true_block->ops.push(&dest);
					locals.push({vd.name, &dest});

					LsIrOp* residual = &convertValue(loadAllocaValue(tmp), *tmp.type, *vd.else_return_type);
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
				LsIrOp* value = ret.expression ? &buildImplicitConversionIR(*ret.expression, *return_type) : nullptr;
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

	LsIrBlockData& buildFunctionIR(FunctionExpression& expr) {
		ASSERT(locals.empty());
		ASSERT(!return_type);
		return_type = static_cast<FunctionResolvedType*>(expr.resolved_type)->return_type;
		current_src_loc = expr.token.src_loc;
		stack_cursor = 0;
		alloca_region_size = 0;
		LsIrBlockData& root = alloc<LsIrBlockData>(host.arena);
		u32 param_offset = 0;
		for (FunctionParam& param : expr.params) {
			if (param.is_comptime) continue;
			auto& alloca = alloc<LsOpAlloca>();
			alloca.type = param.resolved_type;
			alloca.name = param.name;
			alloca.stack_sp = param_offset;
			locals.push({param.name, &alloca});
			param.slot.storage = StorageSlot::LOCAL;
			param.slot.offset = param_offset;
			param.slot.byte_size = typeByteSize(*param.resolved_type);
			param.slot.type = param.resolved_type;
			param_offset += param.slot.byte_size;
		}
		stack_cursor = param_offset;
		buildStatementIR(*expr.body, root);
		root.ops.push(&alloc<LsOpReturn>());
		alloca_region_size = stack_cursor;
		locals.clear();
		return_type = nullptr;
		current_src_loc = LS_IR_INVALID_SOURCE_LOC;
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
	ExpArray<ls_string_view> strings;
	ExpArray<Statement*> defers;
	ExpArray<Loop> loops;
	ls_string_view pending_loop_label = {};
	ResolvedType* return_type = nullptr;
	LsIrSourceLoc current_src_loc = LS_IR_INVALID_SOURCE_LOC;
	u32 stack_cursor = 0;
	u32 alloca_region_size = 0;

	struct SourceScope {
		SourceScope(IRBuilder& builder, const Token& token)
			: builder(builder)
			, previous(builder.current_src_loc) {
			builder.current_src_loc = token.src_loc;
		}
		~SourceScope() { builder.current_src_loc = previous; }

		IRBuilder& builder;
		LsIrSourceLoc previous;
	};
};

bool isTypeFactory(const FunctionExpression& function) {
	if (!function.return_type) return false;
	if (function.return_type->kind == Expression::TYPE_LITERAL) {
		return static_cast<const TypeLiteralExpression*>(function.return_type)->type == ResolvedTypeKind::META;
	}
	return function.return_type->resolved_type && function.return_type->resolved_type->kind == ResolvedTypeKind::META;
}

static ls_string_view copyStringViewToArena(ls_arena& arena, ls_string_view src) {
	if (empty(src)) return src;
	const usize len = size(src);
	char* mem = (char*)arena.allocate(arena.user_data, len, 1);
	if (!mem) return src;
	memcpy(mem, src.begin, len);
	return { mem, mem + len };
}

// Debug-facing type kind: mirrors BytecodeCompiler::toTypeKind but reports
// nullable as LS_TYPE_NULLABLE (toTypeKind maps it to LS_TYPE_NULL_VALUE for
// bytecode comparison opcodes).
static ls_type_kind debugTypeKind(const ResolvedType& type) {
	if (type.kind == ResolvedTypeKind::NULLABLE) return LS_TYPE_NULLABLE;
	switch (type.kind) {
		case ResolvedTypeKind::VOID: return LS_TYPE_VOID;
		case ResolvedTypeKind::BOOL: return LS_TYPE_BOOL;
		case ResolvedTypeKind::I8: return LS_TYPE_I8;
		case ResolvedTypeKind::I16: return LS_TYPE_I16;
		case ResolvedTypeKind::I32: return LS_TYPE_I32;
		case ResolvedTypeKind::I64: return LS_TYPE_I64;
		case ResolvedTypeKind::UNTYPED_INT: return LS_TYPE_I64;
		case ResolvedTypeKind::UNTYPED_FLOAT: return LS_TYPE_F64;
		case ResolvedTypeKind::U8: return LS_TYPE_U8;
		case ResolvedTypeKind::U16: return LS_TYPE_U16;
		case ResolvedTypeKind::U32: return LS_TYPE_U32;
		case ResolvedTypeKind::U64: return LS_TYPE_U64;
		case ResolvedTypeKind::ISIZE: return LS_TYPE_I64;
		case ResolvedTypeKind::F32: return LS_TYPE_F32;
		case ResolvedTypeKind::F64: return LS_TYPE_F64;
		case ResolvedTypeKind::CSTR: return LS_TYPE_CPTR;
		case ResolvedTypeKind::CPTR: return LS_TYPE_CPTR;
		case ResolvedTypeKind::POINTER: return LS_TYPE_CPTR;
		case ResolvedTypeKind::BYTE: return LS_TYPE_U8;
		case ResolvedTypeKind::FUNCTION: return LS_TYPE_FUNCTION;
		case ResolvedTypeKind::ARRAY: return LS_TYPE_ARRAY;
		case ResolvedTypeKind::SLICE: return LS_TYPE_SLICE;
		case ResolvedTypeKind::ENUM: return LS_TYPE_ENUM;
		case ResolvedTypeKind::STRUCT: return LS_TYPE_STRUCT;
		case ResolvedTypeKind::UNION: return LS_TYPE_TAGGED_UNION;
		default: return LS_TYPE_INVALID;
	}
}

static ResolvedType* structFieldTypeAt(const StructResolvedType& st, i32 index) {
	if (index < st.field_types.size()) return st.field_types[index];
	return nullptr;
}

// Builds the bytecode's type metadata (`ls_bytecode::type_info` and its flat
// field/member/enum-value tables) from the resolved types referenced by
// globals, parameters, and locals. Types are interned (deduplicated by
// structural equality); compound types recursively intern their children
// (struct fields, union members, array/slice/nullable/pointer element). The
// `first_*_index`/`*_count` fields on each `ls_type` entry are captured before
// recursing, so the indices into the flat arrays stay stable until `commit`
// copies everything into the bytecode arena.
struct TypeInfoBuilder {
	TypeInfoBuilder(ls_host& host, ls_bytecode& bytecode)
		: host(host)
		, bytecode(bytecode)
		, sources(host.arena)
		, types(host.arena)
		, fields(host.arena)
		, member_indices(host.arena)
		, enum_values(host.arena) {}

	u32 internType(const ResolvedType& type) {
		for (i32 i = 0; i < sources.size(); ++i) {
			if (typesEqual(sources[i], &type)) return (u32)i;
		}

		const u32 first_field = (u32)fields.size();
		const u32 first_member = (u32)member_indices.size();
		const u32 first_value = (u32)enum_values.size();
		const u32 self = (u32)types.size();
		sources.push(&type);
		ls_type& entry = types.emplace_back();
		entry.bytecode = &bytecode;
		entry.kind = debugTypeKind(type);
		entry.byte_size = typeByteSize(type);
		entry.first_field_index = first_field;
		entry.first_member_index = first_member;
		entry.first_value_index = first_value;
		entry.element_type_index = LS_TYPE_INDEX_NONE;
		entry.array_length = LS_TYPE_INDEX_NONE;

		switch (type.kind) {
			case ResolvedTypeKind::STRUCT: {
				const StructResolvedType& st = static_cast<const StructResolvedType&>(type);
				if (st.decl) {
					entry.name = copyStringViewToArena(host.arena, st.decl->cached_name);
					u32 offset = 0;
					// Append this type's own field entries first (recursion
					// below must not interleave child fields into this range).
					for (i32 i = 0; i < st.decl->fields.size(); ++i) {
						ResolvedType* field_type = structFieldTypeAt(st, i);
						if (!field_type) continue;
						ls_type_field_info& field = fields.emplace_back();
						field.name = copyStringViewToArena(host.arena, st.decl->fields[i].name);
						field.type_index = LS_TYPE_INDEX_NONE;
						field.offset = offset;
						offset += typeByteSize(*field_type);
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
				for (i32 i = 0; i < un.members.size(); ++i) member_indices.push(LS_TYPE_INDEX_NONE);
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
						ls_type_enum_value_info& value = enum_values.emplace_back();
						value.name = copyStringViewToArena(host.arena, en.decl->members[i].name);
						value.value = (i32)enumMemberValue(en, i);
					}
					entry.value_count = (u32)enum_values.size() - first_value;
				}
				break;
			}
			case ResolvedTypeKind::ARRAY: {
				const ArrayResolvedType& arr = static_cast<const ArrayResolvedType&>(type);
				entry.element_type_index = internType(*arr.element_type);
				entry.array_length = (arr.size >= 0 && arr.size < (i64)LS_TYPE_INDEX_NONE) ? (u32)arr.size : 0u;
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
			bytecode.type_info = (ls_type*)host.arena.allocate(host.arena.user_data, sizeof(ls_type) * (u32)types.size(), alignof(ls_type));
			for (i32 i = 0; i < types.size(); ++i) bytecode.type_info[i] = types[i];
			bytecode.type_info_count = (u32)types.size();
			bytecode.type_info_capacity = (u32)types.size();
		}
		if (fields.size() > 0) {
			bytecode.type_fields = (ls_type_field_info*)host.arena.allocate(host.arena.user_data, sizeof(ls_type_field_info) * (u32)fields.size(), alignof(ls_type_field_info));
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
			bytecode.type_enum_values = (ls_type_enum_value_info*)host.arena.allocate(host.arena.user_data, sizeof(ls_type_enum_value_info) * (u32)enum_values.size(), alignof(ls_type_enum_value_info));
			for (i32 i = 0; i < enum_values.size(); ++i) bytecode.type_enum_values[i] = enum_values[i];
			bytecode.type_enum_value_count = (u32)enum_values.size();
			bytecode.type_enum_value_capacity = (u32)enum_values.size();
		}
	}

	ls_host& host;
	ls_bytecode& bytecode;
	ExpArray<const ResolvedType*> sources;
	ExpArray<ls_type> types;
	ExpArray<ls_type_field_info> fields;
	ExpArray<u32> member_indices;
	ExpArray<ls_type_enum_value_info> enum_values;
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

	// Record a source-map entry at the current code offset for `loc`, unless
	// the last entry already sits at this offset (then update it in place).
	// `loc` is a token index into the module SourceLocTable, which
	// ls_bytecode_compile copies verbatim into the bytecode location table.
	void recordSourceMap(LsIrSourceLoc loc) {
		if (loc == LS_IR_INVALID_SOURCE_LOC) return;
		if (!source_map.empty() && source_map.back().code_offset == count) {
			source_map.back().location_index = loc;
			return;
		}
		ls_bytecode_source_map_entry entry;
		entry.code_offset = count;
		entry.location_index = loc;
		source_map.push_back(entry);
	}

	ls_arena& arena;
	u8* data = nullptr;
	u32 count = 0;
	u32 capacity = 0;
	ExpArray<ls_bytecode_source_map_entry> source_map;
};


// IR to bytecode
struct BytecodeCompiler {
	BytecodeCompiler(ls_host& host, ls_bytecode& bytecode, TypeInfoBuilder& type_info)
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

	void assignSrcLoc(LsIrOp& op) { op.src_loc = current_src_loc; }
	void assignSrcLoc(LsIrBlockData&) {}

	void emitOp(ls_op op) {
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

	u32 emitCompare(const LsOpBinary& cmp) {
		if (cmp.operand_type->kind == ResolvedTypeKind::SLICE) {
			const u32 lhs = emit(*cmp.lhs, nullptr);
			const u32 rhs = emit(*cmp.rhs, nullptr);
			const u32 result = stack_top++;
			const auto& slice = static_cast<const SliceResolvedType&>(*cmp.operand_type);
			emitOp(LS_OP_SLICE_EQ);
			emit(result);
			emit(lhs);
			emit(rhs);
			emit(typeByteSize(*slice.element_type));
			emit((u8)toTypeKind(*slice.element_type));
			if (cmp.kind == LS_IR_OP_NE) {
				const u32 neq = stack_top++;
				emitOp(LS_OP_NOT);
				emit(neq);
				emit(result);
				return neq;
			}
			return result;
		}
		u32 lhs_offset = emit(*cmp.lhs, nullptr);
		u32 rhs_offset = emit(*cmp.rhs, nullptr);
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
		emit((u8)(cmp.operand_type->kind == ResolvedTypeKind::NULLABLE ? LS_TYPE_U8 : toTypeKind(*cmp.operand_type)));
		return result;
	}

	u32 emitUnary(const LsOpUnary& op, EmitDst* dst) {
		const u32 operand = emit(*op.operand, nullptr);
		if (op.kind == LS_IR_OP_NEG)
			emitOp(ls_op(LS_OP_NEG_I8 + numericKindIndex(*op.operand_type)));
		else
			emitOp(LS_OP_NOT);
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

	// Fuse a numeric compare and the branch that follows it into a single
	// compare-and-branch opcode (e.g. `n <= 1` + `JZ_U8` -> `JGT_I32`, which
	// jumps past the body when `n > 1`). Returns false when the condition is
	// not a fusable comparison.
	bool fusedCompareJumpOp(LsIrOpKind condition_kind, const ResolvedType& operand_type, bool negate, ls_op& out_op) {
		LsIrOpKind skip;
		switch (condition_kind) {
			case LS_IR_OP_EQ: if (!negate) return false; skip = LS_IR_OP_EQ; break;
			case LS_IR_OP_NE: if (negate) return false; skip = LS_IR_OP_EQ; break;
			case LS_IR_OP_LT: skip = negate ? LS_IR_OP_LT : LS_IR_OP_GE; break;
			case LS_IR_OP_LE: skip = negate ? LS_IR_OP_LE : LS_IR_OP_GT; break;
			case LS_IR_OP_GT: skip = negate ? LS_IR_OP_GT : LS_IR_OP_LE; break;
			case LS_IR_OP_GE: skip = negate ? LS_IR_OP_GE : LS_IR_OP_LT; break;
			default: return false;
		}
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
			case LS_IR_OP_EQ: variant = 0; break; // JE
			case LS_IR_OP_GE: variant = 1; break; // JGE
			case LS_IR_OP_GT: variant = 2; break; // JGT
			case LS_IR_OP_LT: variant = 3; break; // JLT
			case LS_IR_OP_LE: variant = 4; break; // JLE
			default: return false;
		}
		out_op = ls_op(LS_OP_JE_I8 + numericKindIndex(operand_type) * 5u + variant);
		return true;
	}

	u32 emitConditionalJump(const LsOpConditionalJump& conditional) {
		const u32 entry_stack_top = stack_top;

		// Peek through a leading NOT so `if (!(a < b))` can fuse too.
		const LsIrOp* condition = conditional.condition;
		bool negate = false;
		if (condition->kind == LS_IR_OP_NOT) {
			condition = static_cast<const LsOpUnary&>(*condition).operand;
			negate = true;
		}

		u32 false_jump = 0;
		bool have_jump = false;
		if (condition->kind >= LS_IR_OP_EQ && condition->kind <= LS_IR_OP_GE) {
			const LsOpBinary& compare = static_cast<const LsOpBinary&>(*condition);
			ls_op fused_opcode;
			if (fusedCompareJumpOp(compare.kind, *compare.operand_type, negate, fused_opcode)) {
				const u32 lhs_slot = emit(*compare.lhs, nullptr);
				const u32 rhs_slot = emit(*compare.rhs, nullptr);
				emitOp(fused_opcode);
				emit(lhs_slot);
				emit(rhs_slot);
				false_jump = code.size();
				emit((i16)0);
				have_jump = true;
			}
		}
		if (!have_jump) {
			const u32 condition_slot = emit(*conditional.condition, nullptr);
			emitOp(LS_OP_JZ_U8);
			emit(condition_slot);
			false_jump = code.size();
			emit((i16)0);
		}
		const u32 condition_peak = stack_top;

		emitBlock(*conditional.true_block);
		u32 peak = stack_top;
		if (peak < condition_peak) peak = condition_peak;
		if (!conditional.false_block) {
			patchI16(false_jump, code.size());
			stack_top = peak;
			return entry_stack_top;
		}

		emitOp(LS_OP_JUMP);
		const u32 end_jump = code.size();
		emit((i16)0);
		patchI16(false_jump, code.size());
		stack_top = entry_stack_top;
		emitBlock(*conditional.false_block);
		if (stack_top > peak) peak = stack_top;
		patchI16(end_jump, code.size());
		stack_top = peak;
		return entry_stack_top;
	}

	u32 emitTernary(const LsOpTernary& ternary) {
		const u32 condition = emit(*ternary.condition, nullptr);
		const u32 size = typeByteSize(*ternary.type);
		const u32 result = stack_top;
		stack_top += size;

		emitOp(LS_OP_JZ_U8);
		emit(condition);
		const u32 false_jump = code.size();
		emit((i16)0);

		const u32 true_value = emit(*ternary.true_value, nullptr);
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
		const u32 false_value = emit(*ternary.false_value, nullptr);
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
		const u32 lhs = emit(*op.lhs, nullptr);
		emitOp(op.kind == LS_IR_OP_AND ? LS_OP_JZ_U8 : LS_OP_JNZ_U8);
		emit(lhs);
		const u32 short_jump = code.size();
		emit((i16)0);

		const u32 rhs = emit(*op.rhs, nullptr);
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

	u32 emitBinary(ls_op base_op, const LsOpBinary& ir_op, EmitDst* dst) {
		u32 lhs = emit(*ir_op.lhs, nullptr);
		u32 rhs = emit(*ir_op.rhs, nullptr);
		emitOp(ls_op(base_op + numericKindIndex(*ir_op.operand_type)));
		u32 ret = dst ? dst->dst : stack_top;
		emit(ret);
		emit(lhs);
		emit(rhs);
		if (!dst) stack_top += typeByteSize(*ir_op.operand_type);
		return ret;
	}

	u32 emitLoadConst(const LsOpLoadConst& load, EmitDst* dst) {
		u32 size = typeByteSize(*load.type);
		switch (size) {
			case 1: {
				emitOp(LS_OP_LOAD_CONST_1);
				u32 res = dst ? dst->dst : stack_top;
				emit(res);
				if (!dst) stack_top += 1;
				emit(&load.value, 1);
				return res;
			}
			case 2: {
				emitOp(LS_OP_LOAD_CONST_2);
				u32 res = dst ? dst->dst : stack_top;
				emit(res);
				if (!dst) stack_top += 2;
				emit(&load.value, 2);
				return res;
			}
			case 4: {
				emitOp(LS_OP_LOAD_CONST_4);
				u32 res = dst ? dst->dst : stack_top;
				emit(res);
				if (!dst) stack_top += 4;
				emit(&load.value, 4);
				return res;
			}
			case 8: {
				emitOp(LS_OP_LOAD_CONST_8);
				u32 res = dst ? dst->dst : stack_top;
				emit(res);
				if (!dst) stack_top += 8;
				emit(&load.value, 8);
				return res;
			}
		}
		ASSERT(false);
		return 0xffFFffFF;
	}

	u32 emitLoadBytes(const LsOpLoadBytes& load, EmitDst* dst) {
		const u32 result = dst ? dst->dst : stack_top;
		u32 iter = result;
		u32 offset = 0;
		while (offset < load.size) {
			const u32 remaining = load.size - offset;
			const u32 size = remaining >= 8 ? 8u : remaining >= 4 ? 4u : remaining >= 2 ? 2u : 1u;
			emitOp(size == 8 ? LS_OP_LOAD_CONST_8 : size == 4 ? LS_OP_LOAD_CONST_4 : size == 2 ? LS_OP_LOAD_CONST_2 : LS_OP_LOAD_CONST_1);
			emit(iter);
			emit(load.value + offset, size);
			iter += size;
			offset += size;
		}
		if (!dst) stack_top = iter;
		return result;
	}

	u32 emitAlloca(LsOpAlloca& alloca) {
		ASSERT(alloca.stack_sp != 0xffFFffFF);
		if (alloca.value->kind != LS_IR_OP_NOP) {
			if (do_optimize) {
				EmitDst dst = { .dst = alloca.stack_sp, .size = typeByteSize(*alloca.type) };
				const u32 value_slot = emit(*alloca.value, &dst);
				if (!empty(alloca.name)) recordLocal(alloca.name, alloca.stack_sp, *alloca.type, (u32)code.size());
			}
			else {
				const u32 value_slot = emit(*alloca.value, nullptr);
				emitOp(LS_OP_COPY);
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

	void recordLocal(ls_string_view name, u32 offset, const ResolvedType& type, u32 scope_begin_offset) {
		ls_bytecode_local_debug_entry& entry = local_debug.emplace_back();
		entry.name = copyStringViewToArena(host.arena, name);
		entry.offset = offset;
		entry.byte_size = typeByteSize(type);
		entry.kind = debugTypeKind(type);
		entry.type_index = type_info.internType(type);
		entry.scope_begin_offset = scope_begin_offset;
	}

	u32 emitCopy(const LsOpCopy& copy) {
		u32 src = emit(*copy.src, nullptr);
		u32 dst = emit(*copy.dst, nullptr);
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
		const u32 value = emit(*op.value, nullptr);
		const u32 ret = stack_top;
		emitOp(LS_OP_COPY);
		emit(ret);
		emit(value + op.offset);
		emit(op.size);
		stack_top += op.size;
		return ret;
	}

	u32 emitUnionConvert(const LsOpUnionConvert& op) {
		const UnionResolvedType& source = static_cast<const UnionResolvedType&>(*op.source_type);
		const UnionResolvedType& dest = static_cast<const UnionResolvedType&>(*op.target_type);
		// Evaluate the source once: tag remap and payload copy both need it.
		const u32 src = emit(*op.value, nullptr);
		const u32 dest_size = typeByteSize(*op.target_type);
		const u32 src_size = typeByteSize(*op.source_type);
		const u32 result = stack_top;
		stack_top += dest_size;

		const u32 src_payload = src_size > sizeof(i32) ? src_size - (u32)sizeof(i32) : 0;
		const u32 dest_payload = dest_size > sizeof(i32) ? dest_size - (u32)sizeof(i32) : 0;
		const u32 payload_size = src_payload < dest_payload ? src_payload : dest_payload;
		if (payload_size) {
			emitOp(LS_OP_COPY);
			emit(result + (u32)sizeof(i32));
			emit(src + (u32)sizeof(i32));
			emit(payload_size);
		}

		// Copy the source tag, then overwrite it when dest uses a different index.
		emitOp(LS_OP_COPY);
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

			emitOp(LS_OP_LOAD_CONST_4);
			emit(expected_sp);
			emit((i32)i);
			emitOp(LS_OP_EQ);
			emit(cond_sp);
			emit(src);
			emit(expected_sp);
			emit((u8)LS_TYPE_I32);
			emitOp(LS_OP_JZ_U8);
			emit(cond_sp);
			const u32 skip = code.size();
			emit((i16)0);
			emitOp(LS_OP_LOAD_CONST_4);
			emit(result);
			emit(dest_tag);
			patchI16(skip, code.size());
		}
		return result;
	}

	u32 emitAggregateInit(const LsOpAggregateInit& aggregate) {
		const u32 result = stack_top;
		stack_top += typeByteSize(*aggregate.type);
		u32 offset = 0;
		for (u32 i = 0; i < aggregate.value_count; ++i) {
			const u32 value = emit(*aggregate.values[i], nullptr);
			const u32 size = aggregate.sizes								   ? aggregate.sizes[i]
							 : aggregate.type->kind == ResolvedTypeKind::ARRAY ? typeByteSize(*static_cast<ArrayResolvedType*>(aggregate.type)->element_type)
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
			case ResolvedTypeKind::VOID: return LS_TYPE_VOID;
			case ResolvedTypeKind::BOOL: return LS_TYPE_BOOL;
			case ResolvedTypeKind::I8: return LS_TYPE_I8;
			case ResolvedTypeKind::I16: return LS_TYPE_I16;
			case ResolvedTypeKind::I32: return LS_TYPE_I32;
			case ResolvedTypeKind::I64: return LS_TYPE_I64;
			case ResolvedTypeKind::UNTYPED_INT: return LS_TYPE_I64;
			case ResolvedTypeKind::UNTYPED_FLOAT: return LS_TYPE_F64;
			case ResolvedTypeKind::U8: return LS_TYPE_U8;
			case ResolvedTypeKind::U16: return LS_TYPE_U16;
			case ResolvedTypeKind::U32: return LS_TYPE_U32;
			case ResolvedTypeKind::U64: return LS_TYPE_U64;
			case ResolvedTypeKind::ISIZE: return LS_TYPE_I64;
			case ResolvedTypeKind::F32: return LS_TYPE_F32;
			case ResolvedTypeKind::F64: return LS_TYPE_F64;
			case ResolvedTypeKind::CSTR: return LS_TYPE_CPTR;
			case ResolvedTypeKind::CPTR: return LS_TYPE_CPTR;
			case ResolvedTypeKind::POINTER: return LS_TYPE_CPTR;
			case ResolvedTypeKind::BYTE: return LS_TYPE_U8;
			case ResolvedTypeKind::FUNCTION: return LS_TYPE_FUNCTION;
			case ResolvedTypeKind::ARRAY: return LS_TYPE_ARRAY;
			case ResolvedTypeKind::SLICE: return LS_TYPE_SLICE;
			case ResolvedTypeKind::NULLABLE: return LS_TYPE_NULL_VALUE;
			case ResolvedTypeKind::ENUM: return LS_TYPE_ENUM;
			case ResolvedTypeKind::STRUCT: return LS_TYPE_STRUCT;
			case ResolvedTypeKind::UNION: return LS_TYPE_TAGGED_UNION;
			default: return LS_TYPE_INVALID;
		}
	}

	u32 emitBoundsCheck(const LsOpBoundsCheck& op) {
		const u32 index = emit(*op.index, nullptr);
		emitOp(LS_OP_BOUNDS_CHECK);
		emit(index);
		emit((u8)toTypeKind(*op.index_type));
		emit(op.length);
		return index;
	}

	u32 emitCast(const LsOpCast& cast) {
		u32 value_sp = emit(*cast.value, nullptr);
		if (cast.target_type->kind == ResolvedTypeKind::SLICE && cast.type->kind == ResolvedTypeKind::SLICE) {
			auto& source = static_cast<SliceResolvedType&>(*cast.target_type);
			auto& target = static_cast<SliceResolvedType&>(*cast.type);
			static ResolvedType i64_type(ResolvedTypeKind::I64);
			// Materialize the destination before calculating the rescaled length. This
			// keeps the reinterpretation independent of temporary lifetime/overlap.
			const u32 result = stack_top;
			stack_top += typeByteSize(*cast.type);
			emitOp(LS_OP_COPY);
			emit(result);
			emit(value_sp);
			emit((u32)sizeof(void*));
			u32 length_sp = stack_top;
			emitOp(LS_OP_COPY);
			emit(length_sp);
			emit((u32)(value_sp + sizeof(void*)));
			emit((u32)sizeof(i64));
			stack_top += sizeof(i64);
			const u32 source_size = typeByteSize(*source.element_type);
			const u32 target_size = typeByteSize(*target.element_type);
			if (source_size != target_size) {
				auto& ratio = alloc<LsOpLoadConst>();
				ratio.type = &i64_type;
				const bool shrink = target_size >= source_size;
				const i64 factor = shrink ? (source_size ? (i64)target_size / source_size : 1) : (target_size ? (i64)source_size / target_size : 1);
				memcpy(ratio.value, &factor, sizeof(factor));
				const u32 ratio_sp = emit(static_cast<LsIrOp&>(ratio), nullptr);
				const u32 scaled_sp = stack_top;
				emitOp(shrink ? LS_OP_DIV_I64 : LS_OP_MUL_I64);
				emit(scaled_sp);
				emit(length_sp);
				emit(ratio_sp);
				stack_top += sizeof(i64);
				length_sp = scaled_sp;
			}
			emitOp(LS_OP_COPY);
			emit((u32)(result + sizeof(void*)));
			emit(length_sp);
			emit((u32)sizeof(i64));
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
		const u32 value_sp = emit(*op.value, nullptr);
		emitOp(LS_OP_FRAME_PTR);
		emit(stack_top);
		emit(value_sp);
		stack_top += sizeof(void*);
		return stack_top - sizeof(void*);
	}

	u32 emitLoad(const LsOpLoad& op) {
		/*if (op.addr->kind == LS_IR_OP_PUSH_LOCAL_ADDR) {
			u32 ret = stack_top;
			LsOpAlloca* alloca = static_cast<LsOpPushLocalAddr&>(*op.addr).alloca;
			u32 size = typeByteSize(*alloca->type);
			stack_top += size;
			emitOp(LS_OP_COPY);
			emit(ret);
			emit(alloca->stack_sp);
			emit(size);
			return ret;
		}*/
		u32 addr_sp = emit(*op.addr, nullptr);
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
		for (u32 i = 0; i < call.arg_count; ++i) args[i] = emit(*call.args[i], nullptr);

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
		const u32 callee = emit(*call.callee, nullptr);
		const u32 arg_base = callee + 4u;
		u32* args = static_cast<u32*>(host.arena.allocate(host.arena.user_data, sizeof(u32) * call.arg_count, alignof(u32)));
		u32 arg_size = 0;
		for (u32 i = 0; i < call.arg_count; ++i) {
			args[i] = emit(*call.args[i], nullptr);
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
			const u32 source = emit(*op.source, nullptr);
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
			result = emit(*op.source, nullptr);
			emitOp(LS_OP_LOAD_CONST_8);
			emit(stack_top);
			emit(op.source_length);
			stack_top += sizeof(i64);
		} else {
			result = emit(*op.source, nullptr);
		}

		if (!op.begin && !op.end) return result;

		static ResolvedType index_type(ResolvedTypeKind::I64);
		auto emitBound = [&](LsIrOp* bound, i64 fallback) {
			if (bound) return emit(*bound, nullptr);
			auto& constant = alloc<LsOpLoadConst>();
			constant.type = &index_type;
			memcpy(constant.value, &fallback, sizeof(fallback));
			return emit(static_cast<LsIrOp&>(constant), nullptr);
		};
		const u32 begin = emitBound(op.begin, 0);
		u32 end;
		if (op.end) {
			end = emit(*op.end, nullptr);
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
		const u32 slice = emit(*op.slice, nullptr);
		const u32 index = emit(*op.index, nullptr);
		const u32 result = stack_top;
		emitOp(LS_OP_SLICE_LOAD);
		emit(result);
		emit(slice);
		emit(index);
		emit(op.element_size);
		stack_top += op.element_size;
		return result;
	}

	u32 emitStringLiteral(const LsOpStringLiteral& op) {
		u32 res = stack_top;
		const u32 result = stack_top;
		stack_top += sizeof(void*) + sizeof(u64);
		emitOp(LS_OP_STRING_SLICE);
		emit(result);
		emit(op.index);
		return result;
	}

	u32 emitSliceRef(const LsOpSliceRef& op) {
		const u32 slice = emit(*op.slice, nullptr);
		const u32 index = emit(*op.index, nullptr);
		emitOp(LS_OP_SLICE_REF);
		emit(slice);
		emit(index);
		emit(op.element_size);
		return slice;
	}

	u32 emitFramePtr(LsOpFramePtr& frame) {
		ASSERT(frame.alloca);
		return frame.alloca->stack_sp;
	}

	u32 emit(LsIrOp& op, EmitDst* dst) {
		op.bytecode_offset = code.size();
		SourceScope scope(*this, op.src_loc);
		u32 result = -1;
		switch (op.kind) {
			case LS_IR_OP_FRAME_PTR: result = emitFramePtr(static_cast<LsOpFramePtr&>(op)); break;
			case LS_IR_OP_STRING_LITERAL: result = emitStringLiteral(static_cast<LsOpStringLiteral&>(op)); break;
			case LS_IR_OP_SLICE_REF: result = emitSliceRef(static_cast<LsOpSliceRef&>(op)); break;
			case LS_IR_OP_SLICE_LOAD: result = emitSliceLoad(static_cast<LsOpSliceLoad&>(op)); break;
			case LS_IR_OP_SLICE: result = emitSlice(static_cast<LsOpSlice&>(op)); break;
			case LS_IR_OP_NEG:
			case LS_IR_OP_NOT: result = emitUnary(static_cast<LsOpUnary&>(op), dst); break;
			case LS_IR_OP_AND:
			case LS_IR_OP_OR: result = emitShortCircuit(static_cast<LsOpBinary&>(op)); break;
			case LS_IR_OP_NULL: result = emitNull(static_cast<LsOpNull&>(op)); break;
			case LS_IR_OP_NOP: result = 0xffFFffFF; break;
			case LS_IR_OP_JUMP: result = emitJump(static_cast<LsOpJump&>(op)); break;
			case LS_IR_OP_CONDITIONAL_JUMP: result = emitConditionalJump(*static_cast<LsOpConditionalJump*>(&op)); break;
			case LS_IR_OP_CALL_DIRECT: result = emitCallDirect(*static_cast<LsOpCallDirect*>(&op)); break;
			case LS_IR_OP_CALL_INDIRECT: result = emitCallIndirect(*static_cast<LsOpCallIndirect*>(&op)); break;
			case LS_IR_OP_EXTRACT_VALUE: result = emitExtractValue(*static_cast<LsOpExtractValue*>(&op)); break;
			case LS_IR_OP_LOAD: result = emitLoad(*static_cast<LsOpLoad*>(&op)); break;
			case LS_IR_OP_PUSH_LOCAL_ADDR: result = emitPushLocalAddr(*static_cast<LsOpPushLocalAddr*>(&op)); break;
			case LS_IR_OP_MATERIALIZE_ADDR: result = emitMaterializeAddr(*static_cast<LsOpMaterializeAddr*>(&op)); break;
			case LS_IR_OP_PUSH_GLOBAL_ADDR: result = emitPushGlobalAddr(*static_cast<LsOpPushGlobalAddr*>(&op)); break;
			case LS_IR_OP_EQ:
			case LS_IR_OP_NE:
			case LS_IR_OP_LT:
			case LS_IR_OP_LE:
			case LS_IR_OP_GT:
			case LS_IR_OP_GE: result = emitCompare(static_cast<LsOpBinary&>(op)); break;
			case LS_IR_OP_COPY: result = emitCopy(*static_cast<LsOpCopy*>(&op)); break;
			case LS_IR_OP_CAST: result = emitCast(static_cast<LsOpCast&>(op)); break;
			case LS_IR_OP_TERNARY: result = emitTernary(static_cast<LsOpTernary&>(op)); break;
			case LS_IR_OP_BOUNDS_CHECK: result = emitBoundsCheck(static_cast<LsOpBoundsCheck&>(op)); break;
			case LS_IR_OP_ALLOCA: result = emitAlloca(*static_cast<LsOpAlloca*>(&op)); break;
			case LS_IR_OP_MUL: result = emitBinary(LS_OP_MUL_I8, static_cast<LsOpBinary&>(op), dst); break;
			case LS_IR_OP_ADD: result = emitBinary(LS_OP_ADD_I8, static_cast<LsOpBinary&>(op), dst); break;
			case LS_IR_OP_SUB: result = emitBinary(LS_OP_SUB_I8, static_cast<LsOpBinary&>(op), dst); break;
			case LS_IR_OP_DIV: result = emitBinary(LS_OP_DIV_I8, static_cast<LsOpBinary&>(op), dst); break;
			case LS_IR_OP_MOD: result = emitBinary(LS_OP_MOD_I8, static_cast<LsOpBinary&>(op), dst); break;
			case LS_IR_OP_LOAD_CONST: result = emitLoadConst(static_cast<LsOpLoadConst&>(op), dst); break;
			case LS_IR_OP_LOAD_BYTES: result = emitLoadBytes(static_cast<LsOpLoadBytes&>(op), dst); break;
			case LS_IR_OP_UNION_CONVERT: result = emitUnionConvert(static_cast<LsOpUnionConvert&>(op)); break;
			case LS_IR_OP_AGGREGATE_INIT: result = emitAggregateInit(static_cast<LsOpAggregateInit&>(op)); break;
			case LS_IR_OP_RETURN: result = emitReturn(static_cast<LsOpReturn&>(op)); break;
			default: ASSERT(false); break;
		}
		if (dst && dst->dst != result) {
			emitOp(LS_OP_COPY);
			emit(dst->dst);
			emit(result);
			emit(dst->size);
		}
		return dst ? dst->dst : result;
	}

	u32 emitReturn(const LsOpReturn& ret) {
		if (!ret.expression) {
			emitOp(LS_OP_RETURN_BASE);
			return stack_top;
		}
		if (do_optimize) {
			EmitDst dst = {0, ret.size};
			const u32 result = emit(*ret.expression, &dst);
			emitOp(LS_OP_RETURN_BASE);
			return result;
		}
		const u32 result = emit(*ret.expression, nullptr);
		emitOp(LS_OP_RETURN);
		emit(result);
		emit(ret.size);
		return result;
	}

	void optimize(LsIrOp*& op) {
		switch (op->kind) {
			case LS_IR_OP_ALLOCA: {
				auto& alloca = *static_cast<LsOpAlloca*>(op);
				if (alloca.value) optimize(alloca.value);
				break;
			}
			case LS_IR_OP_CALL_DIRECT: {
				auto& call = *static_cast<LsOpCallDirect*>(op);
				for (u32 i = 0; i < call.arg_count; ++i) {
					optimize(call.args[i]);
				}
				break;
			}
			case LS_IR_OP_COPY: {
				auto& copy = *static_cast<LsOpCopy*>(op);
				optimize(copy.src);
				optimize(copy.dst);
				break;
			}
			case LS_IR_OP_NOT:
			case LS_IR_OP_NEG: {
				auto& v = *static_cast<LsOpUnary*>(op);
				optimize(v.operand);
				break;
			}
			case LS_IR_OP_SUB:
			case LS_IR_OP_MUL:
			case LS_IR_OP_DIV:
			case LS_IR_OP_LE:
			case LS_IR_OP_LT:
			case LS_IR_OP_GE:
			case LS_IR_OP_GT:
			case LS_IR_OP_ADD: {
				auto& add = *static_cast<LsOpBinary*>(op);
				optimize(add.lhs);
				optimize(add.rhs);
				break;
			}
			case LS_IR_OP_CONDITIONAL_JUMP: {
				auto& jmp = *static_cast<LsOpConditionalJump*>(op);
				optimize(jmp.condition);
				optimize(*jmp.true_block);
				if (jmp.false_block) optimize(*jmp.false_block);
				break;
			}
			case LS_IR_OP_RETURN: {
				auto& ret = *static_cast<LsOpReturn*>(op);
				if (ret.expression) optimize(ret.expression);
				break;
			}
			case LS_IR_OP_LOAD: {
				auto* load = static_cast<LsOpLoad*>(op);
				if (load->addr->kind == LS_IR_OP_PUSH_LOCAL_ADDR) {
					auto& addr = *static_cast<LsOpPushLocalAddr*>(load->addr);
					auto& ref = alloc<LsOpFramePtr>();
					ref.alloca = addr.alloca;
					op = &ref;
				}
				break;
			}
		}
	}

	void optimize(LsIrBlockData& body) {
		if (!do_optimize) return;
		for (LsIrOp*& op : body.ops) {
			optimize(op);
		}
	}

	void emitBlock(LsIrBlockData& block) {
		for (LsIrOp* op : block.ops) {
			stack_top = temp_base;
			emit(*op, nullptr);
			if (stack_top > stack_high_water) stack_high_water = stack_top;
		}
	}

	void beginFunction(ls_function_bc* fn, FunctionExpression& fn_expr, u32 alloca_region_size) {
		ASSERT(fn);
		fn_bc = fn;
		temp_base = alloca_region_size;
		stack_top = temp_base;
		stack_high_water = temp_base;

		ResolvedType* return_type = static_cast<FunctionResolvedType*>(fn_expr.resolved_type)->return_type;

		fn_bc->code = (u8*)(u64)code.size(); // store offset since code.data may be reallocated
		fn_bc->kind = fn_expr.is_extern ? LS_FUNCTION_NATIVE : LS_FUNCTION_SCRIPT;
		fn_bc->is_builtin_native = false;
		fn_bc->param_size = 0;
		local_debug.clear();
		u32 param_offset = 0;
		for (const FunctionParam& param : fn_expr.params) {
			if (param.is_comptime) continue;
			const u32 size = typeByteSize(*param.resolved_type);
			fn_bc->param_size += size;
			recordLocal(param.name, param_offset, *param.resolved_type, 0u);
			param_offset += size;
		}
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

		const u32 function_start = (u32)(u64)fn_bc->code;
		const u32 function_end = function_start + fn_bc->code_size;
		for (i32 i = 0, c = code.source_map.size(); i < c; ++i) {
			const ls_bytecode_source_map_entry& entry = code.source_map[(i32)i];
			if (entry.code_offset < function_start || entry.code_offset >= function_end) continue;
			++fn_bc->source_map_count;
		}
		if (fn_bc->source_map_count > 0u) {
			fn_bc->source_map = (ls_bytecode_source_map_entry*)host.arena.allocate(
				host.arena.user_data,
				sizeof(ls_bytecode_source_map_entry) * fn_bc->source_map_count,
				alignof(ls_bytecode_source_map_entry));
			u32 out = 0;
			for (i32 i = 0, c = code.source_map.size(); i < c; ++i) {
				const ls_bytecode_source_map_entry& entry = code.source_map[(i32)i];
				if (entry.code_offset < function_start || entry.code_offset >= function_end) continue;
				ls_bytecode_source_map_entry& dst = fn_bc->source_map[out++];
				dst.code_offset = entry.code_offset - function_start;
				dst.location_index = entry.location_index;
			}
		}

		fn_bc->local_count = (u32)local_debug.size();
		if (fn_bc->local_count > 0u) {
			fn_bc->locals = (ls_bytecode_local_debug_entry*)host.arena.allocate(
				host.arena.user_data,
				sizeof(ls_bytecode_local_debug_entry) * fn_bc->local_count,
				alignof(ls_bytecode_local_debug_entry));
			memcpy(fn_bc->locals, local_debug.data(), sizeof(ls_bytecode_local_debug_entry) * fn_bc->local_count);
		}
		local_debug.clear();

		fn_bc = nullptr;
	}

	void emit(const void* data, u32 size) {
		for (u32 i = 0; i < size; ++i) {
			code.push_back(((const u8*)data)[i]);
		}
	}

	ls_host& host;
	ls_bytecode& bytecode;
	TypeInfoBuilder& type_info;
	ls_function_bc* fn_bc = nullptr;
	ByteArray code;
	bool do_optimize = false;
	// Debug entries for the current function's named params and locals, in
	// declaration order (params first). Copied into the arena in endFunction
	// and cleared per function.
	ExpArray<ls_bytecode_local_debug_entry> local_debug;
	u32 stack_top = 0;
	u32 stack_high_water = 0;
	u32 temp_base = 0;
	LsIrSourceLoc current_src_loc = LS_IR_INVALID_SOURCE_LOC;

	struct SourceScope {
		SourceScope(BytecodeCompiler& compiler, LsIrSourceLoc loc)
			: compiler(compiler)
			, previous(compiler.current_src_loc) {
			compiler.current_src_loc = loc;
		}
		~SourceScope() { compiler.current_src_loc = previous; }

		BytecodeCompiler& compiler;
		LsIrSourceLoc previous;
	};
};

} // namespace

ls_bytecode* ls_bytecode_compile(ls_module* module, ls_host* host, ls_bytecode_compile_options* options) {
	if (!module || !host) return nullptr;

	ls_bytecode* bc = (ls_bytecode*)host->arena.allocate(host->arena.user_data, sizeof(ls_bytecode), alignof(ls_bytecode));
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
		bc->global_debug = (ls_bytecode_global_debug_entry*)host->arena.allocate(
			host->arena.user_data,
			sizeof(ls_bytecode_global_debug_entry) * global_debug_count,
			alignof(ls_bytecode_global_debug_entry));
	}

	u32 global_debug_index = 0;
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

			ls_bytecode_global_debug_entry& entry = bc->global_debug[global_debug_index++];
			entry.name = copyStringViewToArena(host->arena, s.name);
			entry.offset = s.slot.offset;
			entry.byte_size = s.slot.byte_size;
			entry.kind = debugTypeKind(*s.resolved_type);
			entry.type_index = type_info.internType(*s.resolved_type);
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

	BytecodeCompiler bc_compiler(*host, *bc, type_info);
	if (options) bc_compiler.do_optimize = options->optimize;
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

				if (fn_expr.body) {
					LsIrBlockData& body = builder.buildFunctionIR(fn_expr);
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
		bc->strings = (ls_string_view*)host->arena.allocate(host->arena.user_data, sizeof(ls_string_view) * bc->string_count, alignof(ls_string_view));
		for (i32 i = 0, c = builder.strings.size(); i < c; ++i) {
			bc->strings[i] = builder.strings[i];
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
		builder.stack_cursor = 0;
		for (Unit& u : module->units) {
			for (Symbol& s : u.symbols) {
				if (!symbolHasGlobalStorage(s) || s.expression->kind == Expression::UNDEFINED) continue;
				LsIrOp& value = builder.buildExpressionIR(*s.expression, true);
				u32 src = bc_compiler.emit(value, nullptr);
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

	// Copy the module's append-only SourceLocTable into the bytecode location
	// table verbatim (one entry per token; token indices are reused as-is by
	// the source maps). source_name strings are copied into the bytecode arena
	// so the bytecode does not reference the module after compilation.
	const u32 location_count = (u32)src_locs.entries.size();
	bc->location_count = location_count;
	if (location_count > 0u) {
		bc->locations = (ls_bytecode_location*)host->arena.allocate(
			host->arena.user_data,
			sizeof(ls_bytecode_location) * location_count,
			alignof(ls_bytecode_location));
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
