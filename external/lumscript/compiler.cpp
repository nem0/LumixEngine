#include "compiler.h"
#include "utils.h"
#include <float.h>

// Get the resolved type of a struct field, preferring the template-instantiated
// type if available, otherwise the declared type.
static ResolvedType* structFieldType(const StructResolvedType& st, i32 index) {
	ASSERT(st.decl);
	if (index < st.field_types.size()) return st.field_types[(u32)index];
	ASSERT(false);
	return nullptr;
}

u32 typeByteSize(const ResolvedType& t) {
	switch (t.kind) {
		case ResolvedType::VOID: return 0;
		case ResolvedType::BOOL:
		case ResolvedType::I8:
		case ResolvedType::U8:
		case ResolvedType::BYTE:
			return 1;
		case ResolvedType::I16:
		case ResolvedType::U16:
			return 2;
		case ResolvedType::I32:
		case ResolvedType::U32:
		case ResolvedType::F32:
		case ResolvedType::ENUM:
		case ResolvedType::FUNCTION:
			return 4;
		case ResolvedType::I64:
		case ResolvedType::U64:
		case ResolvedType::ISIZE:
		case ResolvedType::F64:
		case ResolvedType::STRING:
		case ResolvedType::CSTR:
		case ResolvedType::CPTR:
			return 8;
		case ResolvedType::NULLABLE: return 1 + typeByteSize(*static_cast<const NullableResolvedType&>(t).inner);
		case ResolvedType::UNION: {
			const UnionResolvedType& un = static_cast<const UnionResolvedType&>(t);
			u32 max_size = 0;
			for (ResolvedType* member : un.members) max_size = max_size > typeByteSize(*member) ? max_size : typeByteSize(*member);
			return 4 + max_size; // i32 tag followed by the largest member payload
		}
		case ResolvedType::SLICE: return 16;
		case ResolvedType::ARRAY: {
			const ArrayResolvedType& arr = static_cast<const ArrayResolvedType&>(t);
			ASSERT(arr.size > 0);
			ASSERT(arr.size < 0xffFFffFF); // TODO
			return (u32)arr.size * typeByteSize(*arr.element_type);
		}
	case ResolvedType::STRUCT: {
			const StructResolvedType& st = static_cast<const StructResolvedType&>(t);
			u32 count = 0;
			if (st.decl) {
				for (i32 i = 0; i < st.decl->fields.size(); ++i) {
					ResolvedType* field_type = structFieldType(st, i);
					if (field_type) count += typeByteSize(*field_type);
				}
			}
			return count ? count : 1;
		}
		default: return 1;
	}
}

struct TemplateBinding {
	ls_string_view name = {};
	ComptimeValue arg;
};

struct TemplateBindings {
	TemplateBindings(ls_arena& arena) : values(arena) {}

	ExpArray<TemplateBinding> values; // TODO allocation
};

struct Checker {
	ls_module& module;
	OutputFormatter error_stream;
	i32 suppress_errors = 0;

	Checker(ls_module& module)
		: module(module)
	{
		error_stream.host = module.host;
	}

	// `isNumericType` and `isIntegerType` deliberately report concrete types only, so the
	// existing hint/conversion logic keeps treating UNTYPED_INT as "not pinned yet". The
	// operator code that wants to accept an untyped operand uses these *OrUntyped helpers.
	static bool isIntegerType(const ResolvedType& t) { return t.kind >= ResolvedType::I8 && t.kind <= ResolvedType::ISIZE; }
	static bool isFloatType(const ResolvedType& t) { return t.kind >= ResolvedType::F32 && t.kind <= ResolvedType::F64; }
	static bool isNumericType(const ResolvedType& type) { return type.kind >= ResolvedType::I8 && type.kind <= ResolvedType::F64; }
	static bool isUntypedNumeric(const ResolvedType& t) { return t.kind == ResolvedType::UNTYPED_INT || t.kind == ResolvedType::UNTYPED_FLOAT; }
	static bool isNumericOrUntyped(const ResolvedType& t) { return isNumericType(t) || isUntypedNumeric(t); }
	static bool isIntegerOrUntyped(const ResolvedType& t) { return isIntegerType(t) || t.kind == ResolvedType::UNTYPED_INT; }

	template <typename T, typename... Args> static T* makeType(Unit& unit, Args&&... args) {
		// Semantic nodes live as long as their owning unit. Allocating them from the
		// unit arena also keeps cached types and template instances pointer-stable.
		ls_arena& arena = unit.arena;
		void* mem = arena.allocate(arena.user_data, sizeof(T), alignof(T));
		return ::new (mem) T(static_cast<Args&&>(args)...);
	}

	ResolvedType* primitiveType(ResolvedType::Kind kind) const {
		ASSERT(kind >= ResolvedType::VOID && kind < ResolvedType::META);
		return &module.primitives[kind];
	}

	static const char* primitiveTypeName(ResolvedType::Kind kind) {
		static const char* names[] = {"void", "bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "isize", "f32", "f64", "string", "cstr", "cptr", "byte"};
		ASSERT(kind >= ResolvedType::VOID && kind <= ResolvedType::BYTE);
		return names[kind - ResolvedType::VOID];
	}

	static bool typesEqual(const ResolvedType* a, const ResolvedType* b) {
		if (a == b) return true;
		if (!a || !b) return false;
		if (a->kind != b->kind) return false;
		switch (a->kind) {
			case ResolvedType::FUNCTION: {
				const auto* fa = static_cast<const FunctionResolvedType*>(a);
				const auto* fb = static_cast<const FunctionResolvedType*>(b);
				if (fa->param_types.size() != fb->param_types.size()) return false;
				if (!typesEqual(fa->return_type, fb->return_type)) return false;
				for (i32 i = 0; i < fa->param_types.size(); ++i)
					if (!typesEqual(fa->param_types[i], fb->param_types[i])) return false;
				return true;
			}
			case ResolvedType::ARRAY: {
				const auto* aa = static_cast<const ArrayResolvedType*>(a);
				const auto* ab = static_cast<const ArrayResolvedType*>(b);
				return aa->size == ab->size && typesEqual(aa->element_type, ab->element_type);
			}
			case ResolvedType::SLICE: {
				const auto* sa = static_cast<const SliceResolvedType*>(a);
				const auto* sb = static_cast<const SliceResolvedType*>(b);
				return typesEqual(sa->element_type, sb->element_type);
			}
			case ResolvedType::NULLABLE: {
				const auto* na = static_cast<const NullableResolvedType*>(a);
				const auto* nb = static_cast<const NullableResolvedType*>(b);
				return typesEqual(na->inner, nb->inner);
			}
			case ResolvedType::UNION: {
				const auto* ua = static_cast<const UnionResolvedType*>(a);
				const auto* ub = static_cast<const UnionResolvedType*>(b);
				if (ua->members.size() != ub->members.size()) return false;
				for (ResolvedType* member : ua->members) {
					bool found = false;
					for (ResolvedType* other : ub->members) if (typesEqual(member, other)) { found = true; break; }
					if (!found) return false;
				}
				return true;
			}
			// STRUCT/ENUM: one instance per declaration; a==b above is definitive.
			default: return false;
		}
	}

	static bool canImplicitlyConvert(const ResolvedType* src, const ResolvedType* dst) {
		if (typesEqual(src, dst)) return true;
		if (!src || !dst) return false;
		if (dst->kind == ResolvedType::UNION) {
			const UnionResolvedType* un = static_cast<const UnionResolvedType*>(dst);
			if (src->kind == ResolvedType::UNION) {
				const UnionResolvedType* source = static_cast<const UnionResolvedType*>(src);
				for (ResolvedType* source_member : source->members) {
					bool found = false;
					for (ResolvedType* member : un->members) {
						if (typesEqual(source_member, member)) { found = true; break; }
					}
					if (!found) return false;
				}
				return true;
			}
			for (ResolvedType* member : un->members) {
				if (typesEqual(src, member)) return true;
			}
			return false;
		}
		if (src->kind == ResolvedType::STRING && dst->kind == ResolvedType::CSTR) return true;
		// An untyped literal converts to any concrete numeric type (its width is chosen at the
		// materialization point). This is only a safety net; callers materialize first.
		if (src->kind == ResolvedType::UNTYPED_INT) return isNumericType(*dst);
		if (src->kind == ResolvedType::UNTYPED_FLOAT) return isFloatType(*dst);
		if (src->kind == ResolvedType::ARRAY && dst->kind == ResolvedType::SLICE) {
			const auto* arr = static_cast<const ArrayResolvedType*>(src);
			const auto* slice = static_cast<const SliceResolvedType*>(dst);
			return typesEqual(arr->element_type, slice->element_type);
		}
		if (dst->kind == ResolvedType::NULLABLE) {
			const auto* nb = static_cast<const NullableResolvedType*>(dst);
			return typesEqual(src, nb->inner);
		}
		return false;
	}

	static TemplateBinding* findTemplateBinding(TemplateBindings* bindings, ls_string_view name) {
		if (!bindings) return nullptr;
		for (TemplateBinding& binding : bindings->values) {
			if (equalStrings(binding.name, name)) return &binding;
		}
		return nullptr;
	}

	static const TemplateBinding* findTemplateBinding(const TemplateBindings* bindings, ls_string_view name) {
		return findTemplateBinding(const_cast<TemplateBindings*>(bindings), name);
	}

	bool comptimeValuesEqual(const ComptimeValue& a, const ComptimeValue& b) {
		if (a.kind != b.kind) return false;
		switch (a.kind) {
			case ComptimeValue::INVALID: return false;
			case ComptimeValue::TYPE: return typesEqual(a.type, b.type);
			case ComptimeValue::INT: return a.int_value == b.int_value;
			case ComptimeValue::FLOAT: return a.float_value == b.float_value;
			case ComptimeValue::BOOL: return a.bool_value == b.bool_value;
			case ComptimeValue::STRING: return equalStrings(a.string_value, b.string_value);
		}
		return false;
	}

	bool bindTemplateArg(TemplateBindings& bindings, ls_string_view name, const ComptimeValue& arg) {
		if (TemplateBinding* existing = findTemplateBinding(&bindings, name)) {
			return comptimeValuesEqual(existing->arg, arg);
		}
		TemplateBinding& binding = bindings.values.emplace_back();
		binding.name = name;
		binding.arg = arg;
		return true;
	}

	// Unwrap MetaType to get the actual type for value use; pass through for value symbols.
	static ResolvedType* unwrapMeta(ResolvedType* t) { return t && t->kind == ResolvedType::META ? static_cast<MetaType*>(t)->inner : t; }

	// Literal type-checking resolves against the non-nullable destination: a hint of
	// `i32?` still constrains an integer literal as an i32.
	static ResolvedType* unwrapNullable(ResolvedType* t) { return (t && t->kind == ResolvedType::NULLABLE) ? static_cast<NullableResolvedType*>(t)->inner : t; }

	struct SemanticLocalBinding {
		ls_string_view name = {};
		ResolvedType* type = nullptr;
		bool is_immutable = false;
		// Slot of the underlying declaration (frame slot for locals, the symbol's
		// global slot when the binding narrows a global).
		StorageSlot* slot = nullptr;
	};

	struct FunctionCheckContext {
		explicit FunctionCheckContext(ls_arena& arena)
			: locals(arena)
			, scope_marks(arena)
			, loop_labels(arena)
			, label_names(arena)
			, declared_loop_labels(arena)
			, declared_loop_kinds(arena) {}

		ExpArray<SemanticLocalBinding> locals;
		ExpArray<i32> scope_marks;
		ExpArray<ls_string_view> loop_labels;
		ExpArray<ls_string_view> label_names;
		ExpArray<ls_string_view> declared_loop_labels;
		ExpArray<Statement::Kind> declared_loop_kinds;
		bool comptime_only = false;
		i32 in_defer = 0;
	};

	void error(i64 value) {
		char buffer[32];
		char* end = buffer + sizeof(buffer);
		char* cursor = end;
		u64 magnitude = value < 0 ? (u64)(-(value + 1)) + 1u : (u64)value;
		do {
			*--cursor = (char)('0' + magnitude % 10u);
			magnitude /= 10u;
		} while (magnitude);
		if (value < 0) *--cursor = '-';
		error(ls_string_view{cursor, end});
	}

	void error(ResolvedType* type) { error(static_cast<const ResolvedType*>(type)); }

	void error(const ComptimeValue::Kind kind) {
		switch (kind) {
			case ComptimeValue::INVALID: error("invalid"); return;
			case ComptimeValue::TYPE: error("type"); return;
			case ComptimeValue::INT: error("integer"); return;
			case ComptimeValue::FLOAT: error("float"); return; 
			case ComptimeValue::BOOL: error("bool"); return;
			case ComptimeValue::STRING: error("string"); return;
		}
	}

	void error(const ResolvedType* type) {
		if (!type) {
			error("<unresolved>");
			return;
		}
		switch (type->kind) {
			case ResolvedType::UNTYPED_INT: error("{integer}"); return;
			case ResolvedType::UNTYPED_FLOAT: error("{float}"); return;
			case ResolvedType::META: error("type"); return;
			case ResolvedType::ENUM: {
				const EnumResolvedType* en = static_cast<const EnumResolvedType*>(type);
				error(empty(en->decl->cached_name) ? makeStringView("<anonymous>") : en->decl->cached_name);
				return;
			}
			case ResolvedType::STRUCT: {
				const StructResolvedType* st = static_cast<const StructResolvedType*>(type);
				error(empty(st->decl->cached_name) ? makeStringView("<anonymous>") : st->decl->cached_name);
				return;
			}
			case ResolvedType::FUNCTION: {
				const FunctionResolvedType* fn = static_cast<const FunctionResolvedType*>(type);
				error("fn(");
				for (i32 i = 0; i < fn->param_types.size(); ++i) {
					if (i > 0) error(", ");
					error(fn->param_types[i]);
				}
				error(") : ");
				error(fn->return_type);
				return;
			}
			case ResolvedType::ARRAY: {
				const ArrayResolvedType* array = static_cast<const ArrayResolvedType*>(type);
				error(array->element_type);
				error("[");
				error(array->size);
				error("]");
				return;
			}
			case ResolvedType::SLICE:
				error(static_cast<const SliceResolvedType*>(type)->element_type);
				error("[]");
				return;
			case ResolvedType::NULLABLE:
				error("?");
				error(static_cast<const NullableResolvedType*>(type)->inner);
				return;
			case ResolvedType::UNION: {
				const UnionResolvedType* un = static_cast<const UnionResolvedType*>(type);
				for (i32 i = 0; i < un->members.size(); ++i) {
					if (i > 0) error(" | ");
					error(un->members[i]);
				}
				return;
			}
			default:
				if (type->kind >= ResolvedType::VOID && type->kind <= ResolvedType::BYTE) {
					error(primitiveTypeName(type->kind));
					return;
				}
				error("<invalid>");
				return;
		}
	}

	template <typename T> void error(T&& arg) {
		if (suppress_errors == 0) {
			error_stream.print(static_cast<T&&>(arg));
		}
	}

	template <typename... Args> void errorLine(Token token, Args&&... args) {
		if (suppress_errors != 0) return;
		if (!empty(token.source_name)) {
			error_stream.print(token.source_name);
			error_stream.print(": ");
		}
		if (token.line > 0) {
			error_stream.print("line ");
			error_stream.print(token.line);
			error_stream.print(": ");
		}
		int dummy[] = {(error(static_cast<Args&&>(args)), 0)...};
		(void)dummy;
		error("\n");
	}

	enum class LookupPolicy { NameOnly, Checked };

	// Result of a symbol lookup. `check_failed` is set only under LookupPolicy::Checked
	// when the symbol was found but its checkSymbol() failed — distinguishing a genuine
	// declaration error from an undeclared name (both used to collapse to nullptr).
	struct SymbolRef {
		Unit* owner = nullptr;
		Symbol* symbol = nullptr;
		bool ambiguous = false;
		bool check_failed = false;
		explicit operator bool() const { return symbol && !ambiguous && !check_failed; }
	};

	// `sizeof(T)` / `alignof(T)`. Rejects a value-denoting name (the operand must be a type).
	bool resolveSizeofValue(Unit& unit, SizeofExpression& sz, ComptimeValue& out, TemplateBindings* bindings = nullptr) {
		ResolvedType* measured = resolveTypeExpr(unit, *sz.type_expr, bindings);
		if (!measured) return false;
		// Preserve the concrete operand for backends that need to emit a native
		// sizeof/alignof expression instead of using the folded integer value.
		sz.type_expr->resolved_type = measured;

		const i64 size = typeByteSize(*measured);
		i64 align = size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;
		if (measured->kind == ResolvedType::UNION) {
			align = 4;
			for (ResolvedType* member : static_cast<UnionResolvedType*>(measured)->members) {
				const i64 member_size = typeByteSize(*member);
				const i64 member_align = member_size >= 8 ? 8 : member_size >= 4 ? 4 : member_size >= 2 ? 2 : 1;
				if (member_align > align) align = member_align;
			}
		}
		out = ComptimeValue(sz.is_align ? align : size);
		sz.value = (u64)out.int_value;
		return true;
	}

	// TODO full comptime eval 
	// TODO error msgs
	ComptimeValue resolveComptimeValue(Unit& unit, Expression& expr, TemplateBindings* bindings = nullptr) {
		switch (expr.kind) {
			case Expression::RESOLVED_TYPE:
				return ComptimeValue(static_cast<ResolvedTypeExpression&>(expr).type);
			case Expression::TYPE_LITERAL: {
				const ResolvedType::Kind kind = static_cast<TypeLiteralExpression&>(expr).type;
				if (kind == ResolvedType::META) return ComptimeValue(makeType<MetaType>(unit));
				if (kind >= ResolvedType::VOID && kind <= ResolvedType::BYTE) return ComptimeValue(primitiveType(kind));
				// TODO create a test to hit this
				return {};
			}
			case Expression::FUNCTION_TYPE: {
				FunctionTypeExpression& fn = static_cast<FunctionTypeExpression&>(expr);
				FunctionResolvedType* resolved = makeType<FunctionResolvedType>(unit, unit.arena);
				for (Expression* param : fn.params) {
					ResolvedType* pt = resolveTypeExpr(unit, *param, bindings);
					if (!pt) {
						// TODO create a test to hit this
						return {};
					}
					resolved->param_types.push(pt);
				}
				resolved->return_type = resolveTypeExpr(unit, *fn.return_type, bindings);
				if (!resolved->return_type) {
					// TODO create a test to hit this
					return {};
				}
				return ComptimeValue(resolved);
			}
			case Expression::ARRAY_TYPE: {
				ArrayTypeExpression& arr = static_cast<ArrayTypeExpression&>(expr);
				ArrayResolvedType* resolved = makeType<ArrayResolvedType>(unit);
				resolved->element_type = resolveTypeExpr(unit, *arr.element_type, bindings);
				i64 size = 0;
				if (!resolved->element_type) {
					// TODO create a test to hit this
					return {};
				}
				if (!resolveComptimeIntValue(unit, arr.size, size, bindings)) {
					// TODO create a test to hit this
					return {};
				}
				if (size <= 0) {
					errorLine(arr.size->token, "Array size must be a positive integer");
					return {};
				}
				resolved->size = size;
				return ComptimeValue(resolved);
			}
			case Expression::SLICE_TYPE: {
				SliceTypeExpression& sl = static_cast<SliceTypeExpression&>(expr);
				SliceResolvedType* resolved = makeType<SliceResolvedType>(unit);
				resolved->element_type = resolveTypeExpr(unit, *sl.element_type, bindings);
				if (!resolved->element_type) {
					// TODO create a test to hit this
					return {};
				}
				return ComptimeValue(resolved);
			}
			case Expression::NULLABLE_TYPE: {
				NullableTypeExpression& nullable = static_cast<NullableTypeExpression&>(expr);
				NullableResolvedType* resolved = makeType<NullableResolvedType>(unit);
				resolved->inner = resolveTypeExpr(unit, *nullable.inner, bindings);
				if (!resolved->inner) {
					// TODO create a test to hit this
					return {};
				}
				return ComptimeValue(resolved);
			}
			case Expression::UNION_TYPE: {
				UnionTypeExpression& un = static_cast<UnionTypeExpression&>(expr);
				UnionResolvedType* resolved = makeType<UnionResolvedType>(unit, unit.arena);
				for (Expression* member_expr : un.members) {
					ResolvedType* member = resolveTypeExpr(unit, *member_expr, bindings);
					if (!member) return {};
					const auto add_member = [&](auto&& self, ResolvedType* candidate, Token token, bool allow_duplicate) -> bool {
						if (candidate->kind == ResolvedType::UNION) {
							for (ResolvedType* nested : static_cast<UnionResolvedType*>(candidate)->members) if (!self(self, nested, token, true)) return false;
							return true;
						}
						for (ResolvedType* existing : resolved->members) {
							if (typesEqual(existing, candidate)) {
								if (allow_duplicate) return true;
								errorLine(token, "Duplicate union member");
								return false;
							}
						}
						if (candidate->kind == ResolvedType::NULLABLE || candidate->kind == ResolvedType::VOID) {
							errorLine(token, "Invalid union member");
							return false;
						}
						resolved->members.push(candidate);
						return true;
					};
					if (!add_member(add_member, member, member_expr->token, false)) return {};
				}
				return ComptimeValue(resolved);
			}
			case Expression::BRACKET: {
				BracketExpression& br = static_cast<BracketExpression&>(expr);
				if (!br.base) {
					// TODO create a test to hit this
					return {};
				}
				if (br.base->kind != Expression::IDENTIFIER && br.base->kind != Expression::MEMBER) {
					errorLine(br.base->token, "[] can only be applied to a type name or member");
					return {};
				}
				ResolvedType* resolved_base = resolveTypeExpr(unit, *br.base, bindings);
				if (!resolved_base) {
					// TODO create a test to hit this
					return {};
				}
				const bool from_binding = br.base->kind == Expression::IDENTIFIER
					&& findTemplateBinding(bindings, static_cast<IdentifierExpression*>(br.base)->name);
				if (!from_binding) {
					SymbolRef ref = resolveSymbol(unit, *br.base);
					if (!ref || !ref.symbol->resolved_type || ref.symbol->resolved_type->kind != ResolvedType::META) {
						// TODO create a test to hit this
						return {};
					}
				}
				if (resolved_base->kind != ResolvedType::STRUCT) {
					// TODO create a test to hit this
					return {};
				}
				StructResolvedType* st = static_cast<StructResolvedType*>(resolved_base);
				if (!st->decl || st->decl->comptime_params.empty()) {
					// TODO create a test to hit this
					return {};
				}
				Unit* owner = findTypeNamespaceUnit(*st);
				if (!owner)	{
					// TODO create a test to hit this
					return {};
				}
				ResolvedType* type = instantiateStructTemplate(*owner, *st->decl, br.args, &unit);
				if (type) expr.resolved_type = type;
				return type ? ComptimeValue(type) : ComptimeValue();
			}
			case Expression::GENERIC_IDENTIFIER: {
				GenericIdentifierExpression& generic = static_cast<GenericIdentifierExpression&>(expr);
				TemplateBinding* binding = findTemplateBinding(bindings, generic.name);
				if (!binding) {
					// TODO error msg
					return {};
				}
				if (binding->arg.kind != ComptimeValue::TYPE) {
					// TODO create a test to hit this
					return {};
				}
				expr.resolved_type = binding->arg.type;
				return binding->arg;
			}
			case Expression::SIZEOF: {
				ComptimeValue out;
				if (!resolveSizeofValue(unit, static_cast<SizeofExpression&>(expr), out, bindings)) {
					// TODO create a test to hit this
					return {};
				}
				return out;
			}
			case Expression::INT_LITERAL: {
				const u64 value = static_cast<IntLiteralExpression&>(expr).value;
				if (value > 9223372036854775807ull) {
					// TODO create a test to hit this
					return {};
				}
				return ComptimeValue((i64)value);
			}
			case Expression::FLOAT_LITERAL: {
				return ComptimeValue(static_cast<FloatLiteralExpression&>(expr).value);
			}
			case Expression::BOOL_LITERAL: {
				return ComptimeValue(static_cast<BoolLiteralExpression&>(expr).value);
			}
			case Expression::STRING_LITERAL: {
				return ComptimeValue(static_cast<StringLiteralExpression&>(expr).value);
			}
			case Expression::IDENTIFIER: {
				IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
				if (TemplateBinding* binding = findTemplateBinding(bindings, id.name)) {
					if (binding->arg.kind == ComptimeValue::TYPE) expr.resolved_type = binding->arg.type;
					return binding->arg;
				}
				SymbolRef ref = resolveSymbol(unit, {}, id.name, LookupPolicy::Checked);
				if (!ref) {
					// TODO create a test to hit this
					return {};
				}
				id.symbol = ref.symbol;
				if (ref.symbol->resolved_type && ref.symbol->resolved_type->kind == ResolvedType::META) {
					return ComptimeValue(unwrapMeta(ref.symbol->resolved_type));
				}
				if (ref.symbol->storage != Symbol::COMPTIME) {
					errorLine(id.token, "Symbol '", id.name, "' is not a compile-time value");
					return {};
				}
				if (!ref.symbol->expression) {
					// TODO create a test to hit this
					return {};
				}
				return resolveComptimeValue(*ref.owner, *ref.symbol->expression, bindings);
			}
			case Expression::MEMBER: {
				MemberExpression& member = static_cast<MemberExpression&>(expr);
				if (!member.expression || member.expression->kind != Expression::IDENTIFIER) {
					errorLine(expr.token, "Expected an import alias in type expression");
					return {};
				}
				const ls_string_view qualifier = static_cast<IdentifierExpression*>(member.expression)->name;
				if (!findImportedUnitByAlias(unit, qualifier)) {
					errorLine(expr.token, "Unknown import alias ", qualifier);
					return {};
				}
				SymbolRef ref = resolveSymbol(unit, qualifier, member.name, LookupPolicy::Checked);
				if (!ref) {
					errorLine(expr.token, "Unknown type ", qualifier, ".", member.name);
					return {};
				}
				if (ref.symbol->resolved_type && ref.symbol->resolved_type->kind == ResolvedType::META) {
					return ComptimeValue(unwrapMeta(ref.symbol->resolved_type));
				}
				if (ref.symbol->storage != Symbol::COMPTIME || !ref.symbol->expression) {
					// TODO create a test to hit this
					return {};
				}
				return resolveComptimeValue(*ref.owner, *ref.symbol->expression, bindings);
			}
			case Expression::BINARY: {
				BinaryExpression& bin = static_cast<BinaryExpression&>(expr);
				ComptimeValue lhs = resolveComptimeValue(unit, *bin.lhs, bindings);
				ComptimeValue rhs = resolveComptimeValue(unit, *bin.rhs, bindings);
				if (lhs.kind == ComptimeValue::INVALID || rhs.kind == ComptimeValue::INVALID) {
					// TODO create a test to hit this
					return {};
				}
				bool lhs_float = lhs.kind == ComptimeValue::FLOAT;
				bool rhs_float = rhs.kind == ComptimeValue::FLOAT;
				bool result_float = lhs_float || rhs_float;

				double lhs_f = lhs.asFloat();
				double rhs_f = rhs.asFloat();
				double result_f = 0;
				i64 result_i = 0;

				switch (bin.op) {
					case Token::PLUS:
						result_float ? (result_f = lhs_f + rhs_f) : (result_i = lhs.int_value + rhs.int_value);
						break;
					case Token::MINUS:
						result_float ? (result_f = lhs_f - rhs_f) : (result_i = lhs.int_value - rhs.int_value);
						break;
					case Token::STAR:
						result_float ? (result_f = lhs_f * rhs_f) : (result_i = lhs.int_value * rhs.int_value);
						break;
					case Token::SLASH: {
						if (result_float ? (rhs_f == 0) : (rhs.int_value == 0)) return {};
						result_float ? (result_f = lhs_f / rhs_f) : (result_i = lhs.int_value / rhs.int_value);
						break;
					}
					case Token::PERCENT:
						if (!result_float && rhs.int_value == 0) {
							// TODO create a test to hit this
							return {};
						}
						if (result_float) {
							// TODO create a test to hit this
							return {};
						}
						result_i = lhs.int_value % rhs.int_value;
						break;
					default:
						// TODO create a test to hit this
						return {};
				}
				return result_float ? ComptimeValue(result_f) : ComptimeValue(result_i);
			}
			case Expression::UNARY: {
				UnaryExpression& un = static_cast<UnaryExpression&>(expr);
				if (un.op != Token::MINUS) {
					// TODO create a test to hit this
					return {};
				}
				ComptimeValue out = resolveComptimeValue(unit, *un.expression, bindings);
				if (out.kind == ComptimeValue::INVALID) {
					// TODO create a test to hit this
					return {};
				}
				if (out.kind == ComptimeValue::FLOAT) {
					out.float_value = -out.float_value;
				} else {
					out.int_value = -out.int_value;
				}
				return out;
			}
			case Expression::TERNARY: {
				TernaryExpression& tern = static_cast<TernaryExpression&>(expr);
				ComptimeValue cond = resolveComptimeValue(unit, *tern.condition, bindings);
				if (cond.kind == ComptimeValue::INVALID)
					// TODO create a test to hit this
					return {};

				ComptimeValue result = cond.asInt() != 0
					? resolveComptimeValue(unit, *tern.true_expr, bindings)
					: resolveComptimeValue(unit, *tern.false_expr, bindings);
				return result;
			}
			case Expression::UNDEFINED:
				errorLine(expr.token, "Undefined expression cannot be used as a compile-time value");
				return {};
			default:
				errorLine(expr.token, "Expression cannot be used as a compile-time value");
				return {};
		}
	}

	// TODO Legacy wrappers for backwards compatibility, inline and remove
	bool resolveComptimeIntValue(Unit& unit, Expression* expr, i64& out, TemplateBindings* bindings = nullptr) {
		ComptimeValue val = resolveComptimeValue(unit, *expr, bindings);
		if (val.kind == ComptimeValue::INVALID) return false;
		out = val.asInt();
		return true;
	}

	ResolvedType* checkExprMaterialized(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* target) {
		ResolvedType* t = checkExpr(unit, ctx, expr, target);
		if (t && isUntypedNumeric(*t) && target && target->kind == ResolvedType::UNION) {
			UnionResolvedType& un = static_cast<UnionResolvedType&>(*target);
			ResolvedType* member = nullptr;
			for (ResolvedType* candidate : un.members) {
				const bool compatible = t->kind == ResolvedType::UNTYPED_INT ? isIntegerType(*candidate) : isFloatType(*candidate);
				if (!compatible) continue;
				if (member) {
					errorLine(expr.token, "Cannot infer union member type for numeric literal");
					return nullptr;
				}
				member = candidate;
			}
			if (member) t = materializeUntyped(expr, member);
		} else if (t && isUntypedNumeric(*t)) {
			t = materializeUntyped(expr, target);
		}
		return t;
	}

	// Unified symbol resolution. A bare name is ambiguous when it matches multiple
	// declarations across the current module and unaliased imports. A qualified
	// lookup is confined to the aliased unit.
	SymbolRef resolveSymbol(Unit& unit, ls_string_view qualifier, ls_string_view name, LookupPolicy policy, ResolvedType* first_arg_type = nullptr) {
		SymbolRef ref;
		if (!empty(qualifier)) {
			if (Unit* owner = findImportedUnitByAlias(unit, qualifier)) {
				if (Symbol* candidate = findSymbol(*owner, name)) ref = {owner, candidate};
			}
		} else {
			SymbolRef namespaced;
			if (first_arg_type) {
				if (Unit* namespace_unit = findTypeNamespaceUnit(*first_arg_type)) {
					if (Symbol* candidate = findSymbol(*namespace_unit, name)) {
						namespaced = {namespace_unit, candidate};
					}
				}
			}

			Symbol* local = findSymbol(unit, name);
			SymbolRef imported;
			for (const Import& imp : unit.imports) {
				if (!empty(imp.alias)) continue;
				ASSERT(imp.unit);
				Unit* imported_unit = imp.unit;
				if (Symbol* candidate = findSymbol(*imported_unit, name)) {
					if (imported.symbol) {
						imported = {nullptr, nullptr, true};
						break;
					}
					imported = {imported_unit, candidate};
				}
			}
			bool ambiguous = imported.ambiguous || (local && imported.symbol);
			// ADL: namespaced is only used when no local or unaliased-import match exists
			if (local) {
				ref.owner = &unit;
				ref.symbol = local;
				ref.ambiguous = ambiguous;
			} else if (imported.symbol) {
				ref = imported;
				ref.ambiguous = ambiguous;
			} else if (namespaced.symbol) {
				ref = namespaced;
			}
		}
		if (!ref.symbol) return ref;

		if (policy == LookupPolicy::Checked && checkSymbol(*ref.owner, *ref.symbol) == LS_RESULT_FAILURE) {
			ref.check_failed = true;
		}
		return ref;
	}

	Expression* makeComptimeValueExpression(Unit& unit, const ComptimeValue& arg) {
		switch (arg.kind) {
			case ComptimeValue::INVALID: break;
			case ComptimeValue::TYPE: {
				ResolvedTypeExpression* expr = makeType<ResolvedTypeExpression>(unit);
				expr->type = arg.type;
				return expr;
			}
			case ComptimeValue::INT: {
				if (arg.int_value < 0) {
					UnaryExpression* un = makeType<UnaryExpression>(unit);
					un->op = Token::MINUS;
					IntLiteralExpression* lit = makeType<IntLiteralExpression>(unit);
					lit->value = (u64)(-(arg.int_value + 1)) + 1u;
					un->expression = lit;
					return un;
				}
				IntLiteralExpression* lit = makeType<IntLiteralExpression>(unit);
				lit->value = (u64)arg.int_value;
				return lit;
			}
			case ComptimeValue::FLOAT: {
				FloatLiteralExpression* lit = makeType<FloatLiteralExpression>(unit);
				lit->value = arg.float_value;
				return lit;
			}
			case ComptimeValue::BOOL: return makeType<BoolLiteralExpression>(unit, arg.bool_value);
			case ComptimeValue::STRING: {
				StringLiteralExpression* lit = makeType<StringLiteralExpression>(unit);
				lit->value = arg.string_value;
				return lit;
			}
		}
		ASSERT(false);
		return nullptr;
	}

	Expression* cloneExpression(Unit& unit, Expression* src, const TemplateBindings* bindings) {
		if (!src) return nullptr;
		Expression* out = nullptr;
		switch (src->kind) {
			case Expression::IDENTIFIER: {
				IdentifierExpression* s = static_cast<IdentifierExpression*>(src);
				if (const TemplateBinding* binding = findTemplateBinding(bindings, s->name)) {
					out = makeComptimeValueExpression(unit, binding->arg);
					break;
				}
				IdentifierExpression* id = makeType<IdentifierExpression>(unit);
				id->name = s->name;
				out = id;
				break;
			}
			case Expression::UNION_TYPE: {
				UnionTypeExpression* s = static_cast<UnionTypeExpression*>(src);
				UnionTypeExpression* un = makeType<UnionTypeExpression>(unit, unit.arena);
				for (Expression* member : s->members) un->members.push(cloneExpression(unit, member, bindings));
				out = un;
				break;
			}
			case Expression::INT_LITERAL: {
				IntLiteralExpression* s = static_cast<IntLiteralExpression*>(src);
				IntLiteralExpression* lit = makeType<IntLiteralExpression>(unit);
				lit->value = s->value;
				out = lit;
				break;
			}
			case Expression::FLOAT_LITERAL: {
				FloatLiteralExpression* s = static_cast<FloatLiteralExpression*>(src);
				FloatLiteralExpression* lit = makeType<FloatLiteralExpression>(unit);
				lit->value = s->value;
				out = lit;
				break;
			}
			case Expression::BOOL_LITERAL: out = makeType<BoolLiteralExpression>(unit, static_cast<BoolLiteralExpression*>(src)->value); break;
			case Expression::STRING_LITERAL: {
				StringLiteralExpression* s = static_cast<StringLiteralExpression*>(src);
				StringLiteralExpression* lit = makeType<StringLiteralExpression>(unit);
				lit->value = s->value;
				out = lit;
				break;
			}
			case Expression::NULL_LITERAL: out = makeType<NullLiteralExpression>(unit); break;
			case Expression::UNDEFINED: out = makeType<UndefinedExpression>(unit); break;
			case Expression::TYPE_LITERAL: out = makeType<TypeLiteralExpression>(unit, static_cast<TypeLiteralExpression*>(src)->type); break;
			case Expression::GENERIC_IDENTIFIER: {
				GenericIdentifierExpression* s = static_cast<GenericIdentifierExpression*>(src);
				if (const TemplateBinding* binding = findTemplateBinding(bindings, s->name)) {
					out = makeComptimeValueExpression(unit, binding->arg);
					break;
				}
				GenericIdentifierExpression* generic = makeType<GenericIdentifierExpression>(unit);
				generic->name = s->name;
				out = generic;
				break;
			}
			case Expression::RESOLVED_TYPE: {
				ResolvedTypeExpression* type = makeType<ResolvedTypeExpression>(unit);
				type->type = static_cast<ResolvedTypeExpression*>(src)->type;
				out = type;
				break;
			}
			case Expression::ARRAY_TYPE: {
				ArrayTypeExpression* s = static_cast<ArrayTypeExpression*>(src);
				ArrayTypeExpression* arr = makeType<ArrayTypeExpression>(unit);
				arr->size = cloneExpression(unit, s->size, bindings);
				arr->element_type = cloneExpression(unit, s->element_type, bindings);
				out = arr;
				break;
			}
			case Expression::SLICE_TYPE: {
				SliceTypeExpression* sl = makeType<SliceTypeExpression>(unit);
				sl->element_type = cloneExpression(unit, static_cast<SliceTypeExpression*>(src)->element_type, bindings);
				out = sl;
				break;
			}
			case Expression::NULLABLE_TYPE: {
				NullableTypeExpression* nullable = makeType<NullableTypeExpression>(unit);
				nullable->inner = cloneExpression(unit, static_cast<NullableTypeExpression*>(src)->inner, bindings);
				out = nullable;
				break;
			}
			case Expression::FUNCTION_TYPE: {
				FunctionTypeExpression* s = static_cast<FunctionTypeExpression*>(src);
				FunctionTypeExpression* fn = makeType<FunctionTypeExpression>(unit, unit.arena);
				for (Expression* param : s->params) fn->params.push(cloneExpression(unit, param, bindings));
				fn->return_type = cloneExpression(unit, s->return_type, bindings);
				out = fn;
				break;
			}
			case Expression::SIZEOF: {
				SizeofExpression* s = static_cast<SizeofExpression*>(src);
				SizeofExpression* sz = makeType<SizeofExpression>(unit);
				sz->type_expr = cloneExpression(unit, s->type_expr, bindings);
				sz->is_align = s->is_align;
				out = sz;
				break;
			}
			case Expression::CALL: {
				CallExpression* s = static_cast<CallExpression*>(src);
				CallExpression* call = makeType<CallExpression>(unit, unit.arena);
				call->callee = cloneExpression(unit, s->callee, bindings);
				for (Expression* arg : s->args) call->args.push(cloneExpression(unit, arg, bindings));
				out = call;
				break;
			}
			case Expression::UNARY: {
				UnaryExpression* s = static_cast<UnaryExpression*>(src);
				UnaryExpression* un = makeType<UnaryExpression>(unit);
				un->op = s->op;
				un->expression = cloneExpression(unit, s->expression, bindings);
				out = un;
				break;
			}
			case Expression::BINARY: {
				BinaryExpression* s = static_cast<BinaryExpression*>(src);
				BinaryExpression* bin = makeType<BinaryExpression>(unit);
				bin->op = s->op;
				bin->lhs = cloneExpression(unit, s->lhs, bindings);
				bin->rhs = cloneExpression(unit, s->rhs, bindings);
				out = bin;
				break;
			}
			case Expression::TERNARY: {
				TernaryExpression* s = static_cast<TernaryExpression*>(src);
				TernaryExpression* tern = makeType<TernaryExpression>(unit);
				tern->condition = cloneExpression(unit, s->condition, bindings);
				tern->true_expr = cloneExpression(unit, s->true_expr, bindings);
				tern->false_expr = cloneExpression(unit, s->false_expr, bindings);
				out = tern;
				break;
			}
			case Expression::CAST: {
				CastExpression* s = static_cast<CastExpression*>(src);
				CastExpression* cast = makeType<CastExpression>(unit);
				cast->expression = cloneExpression(unit, s->expression, bindings);
				cast->type_expr = cloneExpression(unit, s->type_expr, bindings);
				out = cast;
				break;
			}
			case Expression::MEMBER: {
				MemberExpression* s = static_cast<MemberExpression*>(src);
				MemberExpression* mem = makeType<MemberExpression>(unit);
				// In a qualified type name (`lib.Foo`), the left-hand identifier is
				// an import alias, not a template binding. Keep it intact so a generic
				// parameter with the same spelling cannot rewrite the qualifier.
				if (s->expression && s->expression->kind == Expression::IDENTIFIER
					&& findImportedUnitByAlias(unit, static_cast<IdentifierExpression*>(s->expression)->name)) {
					IdentifierExpression* id = makeType<IdentifierExpression>(unit);
					id->name = static_cast<IdentifierExpression*>(s->expression)->name;
					id->token = s->expression->token;
					mem->expression = id;
				} else {
					mem->expression = cloneExpression(unit, s->expression, bindings);
				}
				mem->name = s->name;
				out = mem;
				break;
			}
			case Expression::BRACKET: {
				BracketExpression* s = static_cast<BracketExpression*>(src);
				BracketExpression* br = makeType<BracketExpression>(unit, unit.arena);
				br->base = cloneExpression(unit, s->base, bindings);
				for (Expression* arg : s->args) br->args.push(cloneExpression(unit, arg, bindings));
				out = br;
				break;
			}
			case Expression::SLICE: {
				SliceExpression* s = static_cast<SliceExpression*>(src);
				SliceExpression* sl = makeType<SliceExpression>(unit);
				sl->base = cloneExpression(unit, s->base, bindings);
				sl->begin = cloneExpression(unit, s->begin, bindings);
				sl->end = cloneExpression(unit, s->end, bindings);
				out = sl;
				break;
			}
			case Expression::STRUCT_LITERAL: {
				StructLiteralExpression* s = static_cast<StructLiteralExpression*>(src);
				StructLiteralExpression* lit = makeType<StructLiteralExpression>(unit, unit.arena);
				lit->type = cloneExpression(unit, s->type, bindings);
				for (Expression* value : s->values) lit->values.push(cloneExpression(unit, value, bindings));
				out = lit;
				break;
			}
			default: out = makeType<Expression>(unit, src->kind); break;
		}
		out->token = src->token;
		return out;
	}

	Statement* cloneStatement(Unit& unit, Statement* src, const TemplateBindings* bindings) {
		if (!src) return nullptr;
		Statement* out = nullptr;
		switch (src->kind) {
			case Statement::BLOCK: {
				BlockStatement* s = static_cast<BlockStatement*>(src);
				BlockStatement* block = makeType<BlockStatement>(unit, unit.arena);
				for (Statement* st : s->statements) block->statements.push(cloneStatement(unit, st, bindings));
				out = block;
				break;
			}
			case Statement::EXPRESSION: {
				ExpressionStatement* s = static_cast<ExpressionStatement*>(src);
				ExpressionStatement* st = makeType<ExpressionStatement>(unit);
				st->expression = cloneExpression(unit, s->expression, bindings);
				out = st;
				break;
			}
			case Statement::RETURN: {
				ReturnStatement* s = static_cast<ReturnStatement*>(src);
				ReturnStatement* st = makeType<ReturnStatement>(unit);
				st->expression = cloneExpression(unit, s->expression, bindings);
				out = st;
				break;
			}
			case Statement::VAR_DECL: {
				VarDeclStatement* s = static_cast<VarDeclStatement*>(src);
				VarDeclStatement* st = makeType<VarDeclStatement>(unit);
				st->name = s->name;
				st->type_expr = cloneExpression(unit, s->type_expr, bindings);
				st->expression = cloneExpression(unit, s->expression, bindings);
				st->is_immutable = s->is_immutable;
				out = st;
				break;
			}
			case Statement::ASSIGN: {
				AssignStatement* s = static_cast<AssignStatement*>(src);
				AssignStatement* st = makeType<AssignStatement>(unit);
				st->lhs = cloneExpression(unit, s->lhs, bindings);
				st->rhs = cloneExpression(unit, s->rhs, bindings);
				st->op = s->op;
				out = st;
				break;
			}
			case Statement::IF: {
				IfStatement* s = static_cast<IfStatement*>(src);
				IfStatement* st = makeType<IfStatement>(unit);
				st->condition = cloneExpression(unit, s->condition, bindings);
				st->body = static_cast<BlockStatement*>(cloneStatement(unit, s->body, bindings));
				st->else_branch = cloneStatement(unit, s->else_branch, bindings);
				out = st;
				break;
			}
			case Statement::MATCH: {
				MatchStatement* s = static_cast<MatchStatement*>(src);
				MatchStatement* st = makeType<MatchStatement>(unit, unit.arena);
				st->subject = cloneExpression(unit, s->subject, bindings);
				for (MatchArm& src_arm : s->arms) {
					MatchArm& dst_arm = st->arms.emplace_back(unit.arena);
					dst_arm.is_fallback = src_arm.is_fallback;
					for (MatchPattern& src_pattern : src_arm.patterns) {
						MatchPattern& dst_pattern = dst_arm.patterns.emplace_back();
						dst_pattern.begin = cloneExpression(unit, src_pattern.begin, bindings);
						dst_pattern.end = cloneExpression(unit, src_pattern.end, bindings);
					}
					dst_arm.body = static_cast<BlockStatement*>(cloneStatement(unit, src_arm.body, bindings));
				}
				out = st;
				break;
			}
			case Statement::WHILE: {
				WhileStatement* s = static_cast<WhileStatement*>(src);
				WhileStatement* st = makeType<WhileStatement>(unit);
				st->condition = cloneExpression(unit, s->condition, bindings);
				st->body = static_cast<BlockStatement*>(cloneStatement(unit, s->body, bindings));
				out = st;
				break;
			}
			case Statement::FOR: {
				ForStatement* s = static_cast<ForStatement*>(src);
				ForStatement* st = makeType<ForStatement>(unit);
				st->loop_var = s->loop_var;
				st->begin = cloneExpression(unit, s->begin, bindings);
				st->end = cloneExpression(unit, s->end, bindings);
				st->body = static_cast<BlockStatement*>(cloneStatement(unit, s->body, bindings));
				out = st;
				break;
			}
			case Statement::BREAK: {
				BreakStatement* s = static_cast<BreakStatement*>(src);
				BreakStatement* st = makeType<BreakStatement>(unit);
				st->label = s->label;
				out = st;
				break;
			}
			case Statement::CONTINUE: {
				ContinueStatement* s = static_cast<ContinueStatement*>(src);
				ContinueStatement* st = makeType<ContinueStatement>(unit);
				st->label = s->label;
				out = st;
				break;
			}
			case Statement::DEFER: {
				DeferStatement* s = static_cast<DeferStatement*>(src);
				DeferStatement* st = makeType<DeferStatement>(unit);
				st->statement = cloneStatement(unit, s->statement, bindings);
				out = st;
				break;
			}
			case Statement::LABEL: {
				LabelStatement* s = static_cast<LabelStatement*>(src);
				LabelStatement* st = makeType<LabelStatement>(unit);
				st->name = s->name;
				st->statement = cloneStatement(unit, s->statement, bindings);
				out = st;
				break;
			}
			default: return src;
		}
		out->token = src->token;
		return out;
	}

	// Resolve a type-denoting expression to a semantic type. Keeping this type-only
	// wrapper prevents general compile-time values from being accepted in annotations.
	ResolvedType* resolveTypeExpr(Unit& unit, Expression& expr, TemplateBindings* bindings = nullptr) {
		ComptimeValue value = resolveComptimeValue(unit, expr, bindings);
		if (value.kind == ComptimeValue::INVALID) return nullptr;
		if (value.kind != ComptimeValue::TYPE) {
			errorLine(expr.token, "Expected a type expression, but got ", value.kind);
			// TODO create a test to hit this
			return nullptr;
		}
		return value.type;
	}

	// The common numeric type two operands must share, or null if they are not numerically
	// compatible. UNTYPED_INT adopts any concrete numeric partner; two untyped ints stay
	// untyped (resolved to a default later). Callers materialize the operands afterwards.
	ResolvedType* unifyNumeric(ResolvedType& a, ResolvedType& b) const {
		const bool ui_a = a.kind == ResolvedType::UNTYPED_INT, ui_b = b.kind == ResolvedType::UNTYPED_INT;
		const bool uf_a = a.kind == ResolvedType::UNTYPED_FLOAT, uf_b = b.kind == ResolvedType::UNTYPED_FLOAT;
		// Two untyped operands: float wins over int (1 + 1.5 -> untyped float).
		if ((ui_a || uf_a) && (ui_b || uf_b)) return (uf_a || uf_b) ? primitiveType(ResolvedType::UNTYPED_FLOAT) : primitiveType(ResolvedType::UNTYPED_INT);
		// One untyped, one concrete: untyped adopts the concrete type if compatible.
		if (ui_a) return isNumericType(b) ? &b : nullptr;
		if (ui_b) return isNumericType(a) ? &a : nullptr;
		if (uf_a) return isFloatType(b) ? &b : nullptr;
		if (uf_b) return isFloatType(a) ? &a : nullptr;
		return (isNumericType(a) && typesEqual(&a, &b)) ? &a : nullptr;
	}

	static bool intLiteralFitsType(u64 value, ResolvedType::Kind kind) {
		switch (kind) {
			case ResolvedType::I8: return value <= 127u;
			case ResolvedType::U8: return value <= 255u;
			case ResolvedType::I16: return value <= 32767u;
			case ResolvedType::U16: return value <= 65535u;
			case ResolvedType::I32: return value <= 2147483647u;
			case ResolvedType::U32: return value <= 4294967295u;
			case ResolvedType::I64: return value <= 9223372036854775807ull;
			case ResolvedType::ISIZE: return value <= 9223372036854775807ull;
			case ResolvedType::U64: return true;
			case ResolvedType::F32: {
				// Value must be exactly representable as f32.
				if (value > (1ULL << 24)) return false;
				float as_f32 = (float)value;
				return (u64)as_f32 == value;
			}
			case ResolvedType::F64: {
				// Value must be exactly representable as f64.
				double as_f64 = (double)value;
				return (u64)as_f64 == value;
			}
			default: return false;
		}
	}

	static bool negatedIntLiteralFitsType(u64 magnitude, ResolvedType::Kind kind) {
		switch (kind) {
			case ResolvedType::I8: return magnitude <= 128u;
			case ResolvedType::U8: return false;
			case ResolvedType::I16: return magnitude <= 32768u;
			case ResolvedType::U16: return false;
			case ResolvedType::I32: return magnitude <= 2147483648u;
			case ResolvedType::U32: return false;
			case ResolvedType::I64: return magnitude <= 9223372036854775808ull;
			case ResolvedType::ISIZE: return magnitude <= 9223372036854775808ull;
			case ResolvedType::U64: return false;
			case ResolvedType::F32: return intLiteralFitsType(magnitude, ResolvedType::F32);
			case ResolvedType::F64: return intLiteralFitsType(magnitude, ResolvedType::F64);
			default: return false;
		}
	}

	// Check whether an expression that has already resolved as untyped numeric can
	// be pinned to `concrete`, without changing the AST. This is shared by normal
	// materialization and overload matching so a candidate never matches only to
	// fail during the commit pass.
	static bool canMaterializeUntyped(const Expression& expr, const ResolvedType& concrete) {
		switch (expr.kind) {
			case Expression::INT_LITERAL:
				return isNumericType(concrete) && intLiteralFitsType(static_cast<const IntLiteralExpression&>(expr).value, concrete.kind);
			case Expression::SIZEOF:
				return isNumericType(concrete) && intLiteralFitsType(static_cast<const SizeofExpression&>(expr).value, concrete.kind);
			case Expression::FLOAT_LITERAL: {
				if (!isFloatType(concrete)) return false;
				const double value = static_cast<const FloatLiteralExpression&>(expr).value;
				return concrete.kind != ResolvedType::F32 || (value <= (double)FLT_MAX && value >= -(double)FLT_MAX);
			}
			case Expression::UNARY: {
				const UnaryExpression& un = static_cast<const UnaryExpression&>(expr);
				if (!un.expression) return true;
				if (un.op == Token::MINUS && un.expression->kind == Expression::INT_LITERAL)
					return isNumericType(concrete) && negatedIntLiteralFitsType(static_cast<const IntLiteralExpression*>(un.expression)->value, concrete.kind);
				return canMaterializeUntyped(*un.expression, concrete);
			}
			case Expression::BINARY: {
				const BinaryExpression& bin = static_cast<const BinaryExpression&>(expr);
				return (!bin.lhs || canMaterializeUntyped(*bin.lhs, concrete))
					&& (!bin.rhs || canMaterializeUntyped(*bin.rhs, concrete));
			}
			case Expression::TERNARY: {
				const TernaryExpression& tern = static_cast<const TernaryExpression&>(expr);
				return canMaterializeUntyped(*tern.true_expr, concrete)
					&& canMaterializeUntyped(*tern.false_expr, concrete);
			}
			default: return true;
		}
	}

	FunctionResolvedType* buildFunctionType(Unit& unit, FunctionExpression& fn) {
		ASSERT(!fn.is_template);
		if (fn.resolved_type) return static_cast<FunctionResolvedType*>(fn.resolved_type);

		FunctionResolvedType* fn_type = makeType<FunctionResolvedType>(unit, unit.arena);
		fn_type->decl = &fn;
		for (FunctionParam& param : fn.params) {
			param.resolved_type = resolveTypeExpr(unit, *param.type_expr);
			if (!param.resolved_type) return nullptr;

			if (param.is_ref && param.resolved_type->kind == ResolvedType::NULLABLE) {
				// not supported by the language
				errorLine(fn.token, "Function parameter ", param.name, " cannot be a nullable reference");
				return nullptr;
			}
			fn_type->param_types.push(param.resolved_type);
		}
		fn_type->return_type = resolveTypeExpr(unit, *fn.return_type);
		if (!fn_type->return_type) return nullptr;

		fn.resolved_type = fn_type;
		return fn_type;
	}

	enum class OverloadResult { NOT_FOUND, FOUND, AMBIGUOUS, FAILED };

	static FunctionResolvedType* asFunctionType(ResolvedType* type) {
		type = unwrapMeta(type);
		return type && type->kind == ResolvedType::FUNCTION ? static_cast<FunctionResolvedType*>(type) : nullptr;
	}

	static FunctionExpression* asFunctionExpression(Symbol& symbol) {
		return symbol.expression && symbol.expression->kind == Expression::FUNCTION ? static_cast<FunctionExpression*>(symbol.expression) : nullptr;
	}

	static const char* symbolKind(const Symbol& symbol) {
		switch (symbol.storage) {
			case Symbol::VARIABLE: return "variable";
			case Symbol::CONST: return "constant";
			case Symbol::IMPORT: return "namespace";
			case Symbol::COMPTIME:
				if (symbol.resolved_type && symbol.resolved_type->kind == ResolvedType::META) return "type";
				return "compile-time value";
		}
		return "symbol";
	}

	ResolvedType* checkCallCandidate(Unit& unit,
		FunctionCheckContext* ctx,
		CallExpression& call,
		FunctionResolvedType& fn_type,
		FunctionExpression* resolved_fn = nullptr,
		u32 ufcs_param_offset = 0)
	{
		// num args mismatch
		if (fn_type.param_types.size() != call.args.size() + ufcs_param_offset) {
			errorLine(call.token, "Function call argument count mismatch: expected ", fn_type.param_types.size() - ufcs_param_offset, ", got ", call.args.size());
			return nullptr;
		}

		// ufcs type mismatch
		if (ufcs_param_offset) {
			MemberExpression& mem = static_cast<MemberExpression&>(*call.callee);
			ResolvedType* receiver_type = mem.expression->resolved_type;
			if (!receiver_type || !canImplicitlyConvert(receiver_type, fn_type.param_types[0])) {
				return nullptr;
			}
			if (fn_type.decl && fn_type.decl->params[0].is_ref) {
				bool writable = false;
				receiver_type = checkAssignableExpr(unit, ctx, *mem.expression, writable);
				if (!receiver_type || !writable) {
					errorLine(mem.expression->token, "Cannot pass non-writable UFCS receiver as ref argument");
					return nullptr;
				}
			}
		}

		for (i32 i = 0; i < call.args.size(); ++i) {
			const i32 param_index = ufcs_param_offset + i;
			ResolvedType* param_type = fn_type.param_types[param_index];
			Expression* arg = call.args[i];
			if (fn_type.decl && param_index < fn_type.decl->params.size()) {
				if (fn_type.decl->params[param_index].is_comptime) continue;
				if (fn_type.decl->params[param_index].is_ref) {
					if (arg->kind != Expression::UNARY) {
						errorLine(call.args[i]->token, "Cannot pass non-ref expression as ref argument ", i + 1, " of function call");
						return nullptr;
					}
					UnaryExpression* un = static_cast<UnaryExpression*>(arg);
					if (un->op != Token::REF) {
						errorLine(call.args[i]->token, "Cannot pass non-ref expression as ref argument ", i + 1, " of function call");
						return nullptr;
					}
					bool writable = false;
					ResolvedType* arg_type = checkAssignableExpr(unit, ctx, *un->expression, writable);
					if (!arg_type) return nullptr;
					if (!writable) {
						errorLine(call.args[i]->token, "Cannot pass non-writable expression as ref argument ", i + 1, " of function call");
						return nullptr;
					}
					if (!typesEqual(arg_type, param_type)) {
						errorLine(call.args[i]->token, "Cannot convert ", arg_type, " to ", param_type, " for ref argument ", i + 1, " of function call");
						return nullptr;
					}
					continue;
				}
			}

			ResolvedType* arg_type = checkExprMaterialized(unit, ctx, *arg, param_type);
			if (!arg_type) return nullptr;

			if (!canImplicitlyConvert(arg_type, param_type)) {
				errorLine(call.args[i]->token, "Cannot convert ", arg_type, " to ", param_type, " for argument ", i + 1, " of function call");
				return nullptr;
			}
		}

		if (resolved_fn) call.resolved_fn = resolved_fn;
		call.resolved_type = fn_type.return_type;
		return call.resolved_type;
	}

	static bool operandMatchesParam(Expression& operand, ResolvedType& type, ResolvedType& param) {
		if (typesEqual(&type, &param)) return true;
		if (isUntypedNumeric(type)) return canMaterializeUntyped(operand, param);
		return false;
	}

	OverloadResult resolveOperatorOverload(Unit& unit,
		FunctionCheckContext* ctx,
		Token::Type op,
		i32 arity,
		Expression** operands, // array of `arity` expression pointers
		ResolvedType** operand_types,
		StructExpression& host,
		ResolvedType*& result_type,
		FunctionExpression*& result_fn
	) {
		FunctionResolvedType* matched_type = nullptr;
		FunctionExpression* matched_fn = nullptr;
		for (StructOperator& cand : host.operators) {
			if (cand.op != op) continue;

			FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(cand.fn->resolved_type);
			if (fn_type->param_types.size() != arity) continue;

			if (!operandMatchesParam(*operands[0], *operand_types[0], *fn_type->param_types[0])) continue;;
			if (arity > 1 && !operandMatchesParam(*operands[1], *operand_types[1], *fn_type->param_types[1])) continue;

			if (matched_fn) return OverloadResult::AMBIGUOUS;

			matched_type = fn_type;
			matched_fn = cand.fn;
		}

		if (!matched_fn) return OverloadResult::NOT_FOUND;

		// Commit pass: pin untyped numeric operands to the winning signature.
		for (i32 j = 0; j < arity; ++j) {
			ResolvedType* param = matched_type->param_types[(u32)j];
			if (!materializeUntyped(*operands[j], param)) {
				errorLine(operands[j]->token, "Cannot convert operand ", j + 1, " of operator ", operatorSymbolName(op), " to ", param);
				return OverloadResult::FAILED;
			}
		}
		result_type = matched_type->return_type;
		result_fn = matched_fn;
		return OverloadResult::FOUND;
	}

	static Symbol* findSymbol(Unit& unit, ls_string_view name) {
		for (Symbol& sym : unit.symbols) {
			if (equalStrings(sym.name, name)) return &sym;
		}
		return nullptr;
	}

	static SemanticLocalBinding* findLocal(FunctionCheckContext& ctx, ls_string_view name) {
		for (i32 i = (i32)ctx.locals.size() - 1; i >= 0; --i) {
			SemanticLocalBinding& binding = ctx.locals[(u32)i];
			if (equalStrings(binding.name, name)) return &binding;
		}
		return nullptr;
	}

	static void pushScope(FunctionCheckContext& ctx) { ctx.scope_marks.push((u32)ctx.locals.size()); }

	static void popScope(FunctionCheckContext& ctx) {
		if (ctx.scope_marks.empty()) return;
		const i32 mark = ctx.scope_marks.back();
		ctx.scope_marks.pop_back();
		while (ctx.locals.size() > mark) ctx.locals.pop_back();
	}

	static bool comptimeValueMatchesExpected(const ComptimeValue& value, ResolvedType* expected) {
		if (!expected) return value.kind == ComptimeValue::TYPE;
		if (value.kind == ComptimeValue::INVALID) return false;
		if (expected->kind == ResolvedType::META) return value.kind == ComptimeValue::TYPE;
		if (value.kind == ComptimeValue::TYPE) return false;
		switch (expected->kind) {
			case ResolvedType::BOOL: return value.kind == ComptimeValue::BOOL;
			case ResolvedType::STRING: return value.kind == ComptimeValue::STRING;
			case ResolvedType::F32:
			case ResolvedType::F64:
				if (value.kind == ComptimeValue::FLOAT)
					return expected->kind == ResolvedType::F64 || (value.float_value <= (double)FLT_MAX && value.float_value >= -(double)FLT_MAX);
				if (value.kind != ComptimeValue::INT) return false;
				return intLiteralFitsType(value.int_value < 0 ? (u64)(-(value.int_value + 1)) + 1u : (u64)value.int_value, expected->kind);
			default:
				if (!isIntegerType(*expected) || value.kind != ComptimeValue::INT) return false;
				if (value.int_value < 0) return negatedIntLiteralFitsType((u64)(-(value.int_value + 1)) + 1u, expected->kind);
				return intLiteralFitsType((u64)value.int_value, expected->kind);
		}
	}

	bool resolveStructFields(Unit& unit, const Symbol* sym, StructExpression& st, StructResolvedType& st_type, TemplateBindings* bindings) {
		for (NamedDecl& field : st.fields) {
			ResolvedType* field_type = resolveTypeExpr(unit, *field.type_expr, bindings);
			if (!field_type) return false;

			st_type.field_types.push(field_type);
		}

		ExpArray<ResolvedType*> visited(unit.arena);
		for (ResolvedType* field_type : st_type.field_types) {
			if (containsStructByValue(*field_type, st_type, visited)) {
				if (sym) errorLine(sym->token, "Recursive by-value field in struct ", sym->name);
				else errorLine(st.token, "Recursive by-value field in struct");
				return false;
			}
		}
		return true;
	}

	StructResolvedType* instantiateStructTemplate(Unit& unit, StructExpression& st, ExpArray<Expression*>& arg_exprs, Unit* argument_unit = nullptr) {
		ASSERT(!st.comptime_params.empty());

		if (st.comptime_params.size() != arg_exprs.size()) {
			errorLine(st.token, "Template struct expects ", st.comptime_params.size(), " arguments, but got ", arg_exprs.size());
			return nullptr;
		}

		Unit& arg_unit = argument_unit ? *argument_unit : unit;
		ExpArray<ComptimeValue> args(unit.arena);
		args.resize(st.comptime_params.size());

		// eval args
		for (i32 i = 0; i < st.comptime_params.size(); ++i) {
			NamedDecl& param = st.comptime_params[i];
			ComptimeValue arg = resolveComptimeValue(arg_unit, *arg_exprs[i]);
			if (arg.kind == ComptimeValue::INVALID) {
				errorLine(st.token, "Could not resolve template struct argument ", i + 1);
				return nullptr;
			}
			if (!comptimeValueMatchesExpected(arg, param.resolved_type)) {
				errorLine(st.token, "Template struct argument ", i + 1, " type mismatch: expected ", param.resolved_type, ", got ", arg.kind);
				return nullptr;
			}
			args[i] = arg;
		}

		// look in cache
		for (TemplateStructInstance& instance : st.template_struct_instances) {
			ASSERT(instance.args.size() == args.size());
			bool equal = true;
			for (i32 i = 0; i < args.size(); ++i) {
				if (!comptimeValuesEqual(instance.args[i], args[i])) {
					equal = false;
					break;
				}
			}
			if (equal) return instance.check_failed ? nullptr : instance.type;
		}

		// new instance + cache it
		TemplateStructInstance& new_instance = st.template_struct_instances.emplace_back(unit.arena);
		for (const ComptimeValue& arg : args) new_instance.args.push(arg);
		StructResolvedType* st_type = makeType<StructResolvedType>(unit, unit.arena);
		st_type->decl = &st;
		new_instance.type = st_type;

		TemplateBindings bindings(unit.arena);
		for (i32 i = 0; i < st.comptime_params.size(); ++i) {
			TemplateBinding& binding = bindings.values.emplace_back();
			binding.name = st.comptime_params[i].name;
			binding.arg = args[i];
		}

		if (!resolveStructFields(unit, nullptr, st, *st_type, &bindings)) {
			new_instance.check_failed = true;
			return nullptr;
		}
		return st_type;
	}

	// TODO error msgs here instead of in the callers
	bool inferTemplateArg(Unit& unit, TemplateBindings& bindings, Expression& pattern, const ComptimeValue& actual) {
		ResolvedType* actual_type = actual.kind == ComptimeValue::TYPE ? actual.type : nullptr;
		switch (pattern.kind) {
			case Expression::GENERIC_IDENTIFIER: {
				GenericIdentifierExpression& generic = static_cast<GenericIdentifierExpression&>(pattern);
				ComptimeValue value = actual_type ? ComptimeValue(unwrapNullable(actual_type)) : actual;
				return bindTemplateArg(bindings, generic.name, value);
			}
			case Expression::IDENTIFIER: {
				IdentifierExpression& id = static_cast<IdentifierExpression&>(pattern);
				ComptimeValue value = actual_type ? ComptimeValue(unwrapNullable(actual_type)) : actual;
				if (findTemplateBinding(&bindings, id.name)) return bindTemplateArg(bindings, id.name, value);
				if (actual_type && !resolveSymbol(unit, {}, id.name, LookupPolicy::NameOnly)) {
					return bindTemplateArg(bindings, id.name, value);
				}
				break;
			}
			case Expression::ARRAY_TYPE: {
				if (!actual_type || actual_type->kind != ResolvedType::ARRAY) return false;
				ArrayTypeExpression& p = static_cast<ArrayTypeExpression&>(pattern);
				ArrayResolvedType& a = static_cast<ArrayResolvedType&>(*actual_type);
				return inferTemplateArg(unit, bindings, *p.element_type, ComptimeValue(a.element_type));
			}
			case Expression::SLICE_TYPE: {
				if (!actual_type) return false;
				ResolvedType* actual_element = nullptr;
				if (actual_type->kind == ResolvedType::SLICE) actual_element = static_cast<SliceResolvedType*>(actual_type)->element_type;
				else if (actual_type->kind == ResolvedType::ARRAY) actual_element = static_cast<ArrayResolvedType*>(actual_type)->element_type;
				else return false;
				return inferTemplateArg(unit, bindings, *static_cast<SliceTypeExpression&>(pattern).element_type, ComptimeValue(actual_element));
			}
			case Expression::NULLABLE_TYPE: {
				if (!actual_type || actual_type->kind != ResolvedType::NULLABLE) return false;
				return inferTemplateArg(unit, bindings, *static_cast<NullableTypeExpression&>(pattern).inner, ComptimeValue(static_cast<NullableResolvedType*>(actual_type)->inner));
			}
			case Expression::BRACKET: {
				if (!actual_type || actual_type->kind != ResolvedType::STRUCT) break;
				BracketExpression& br = static_cast<BracketExpression&>(pattern);
				StructResolvedType* actual_struct = static_cast<StructResolvedType*>(actual_type);
				ResolvedType* expected_base = resolveTypeExpr(unit, *br.base, &bindings);
				if (!expected_base) return false;
				if (expected_base->kind != ResolvedType::STRUCT) return false;
				if (static_cast<StructResolvedType*>(expected_base)->decl != actual_struct->decl) return false;
				if (br.args.size() != actual_struct->decl->comptime_params.size()) return false;
				for (i32 i = 0; i < br.args.size(); ++i) {
					ComptimeValue actual_arg;
					if (!actual_struct->decl) return false;
					bool found = false;
					for (TemplateStructInstance& instance : actual_struct->decl->template_struct_instances) {
						if (instance.type != actual_struct) continue;
						if (i < 0 || i >= instance.args.size()) return false;
						actual_arg = instance.args[i];
						found = true;
						break;
					}
					if (!found) return false;
					if (!inferTemplateArg(unit, bindings, *br.args[i], actual_arg)) return false;
				}
				return true;
			}
		}

		if (actual.kind != ComptimeValue::TYPE) return false;

		ResolvedType* pattern_type = resolveTypeExpr(unit, pattern, &bindings);
		if (!pattern_type) return false;

		return typesEqual(pattern_type, actual.type);
	}

	FunctionExpression* instantiateFunctionTemplate(Unit& unit, FunctionExpression& fn, const TemplateBindings& bindings) {
		// check cache first
		for (TemplateFunctionInstance& instance : fn.template_function_instances) {
			if (instance.args.size() == bindings.values.size()) {
				bool equal = true;
				for (i32 i = 0; i < instance.args.size(); ++i) {
					if (!comptimeValuesEqual(instance.args[i], bindings.values[i].arg)) {
						equal = false;
						break;
					}
				}
				if (equal) return instance.check_failed ? nullptr : instance.instance;
			}
		}

		// create new instance
		TemplateFunctionInstance& instance = fn.template_function_instances.emplace_back(unit.arena);
		for (const TemplateBinding& binding : bindings.values) instance.args.push(binding.arg);

		FunctionExpression* clone = makeType<FunctionExpression>(unit, unit.arena);
		clone->token = fn.token;
		clone->is_template = false;
		clone->is_extern = fn.is_extern;
		FunctionResolvedType* fn_type = nullptr;
		bool body_ok = false;
		for (FunctionParam& src_param : fn.params) {
			FunctionParam& dst_param = clone->params.emplace_back();
			dst_param.name = src_param.name;
			dst_param.is_ref = src_param.is_ref;
			dst_param.is_comptime = src_param.is_comptime;
			dst_param.is_generic = false;
			dst_param.type_expr = cloneExpression(unit, src_param.type_expr, &bindings);
		}
		clone->return_type = cloneExpression(unit, fn.return_type, &bindings);
		clone->body = cloneStatement(unit, fn.body, &bindings);
		instance.instance = clone;

		fn_type = buildFunctionType(unit, *clone);
		if (!fn_type) {
			// TODO create a test to hit this
			errorLine(fn.token, "Could not build template function signature");
		}
		body_ok = fn_type && (!clone->body || checkFunctionBody(unit, *clone));
		if (fn_type && !body_ok) {
			errorLine(fn.token, "Could not check template function body");
		}
		bool ok = fn_type && body_ok;
		if (!ok) {
			instance.check_failed = true;
			clone->resolved_type = nullptr;
			return nullptr;
		}
		instance.type = fn_type;
		return clone;
	}

	SymbolRef resolveSymbol(Unit& unit, const Expression& expression) {
		ls_string_view qualifier = {};
		ls_string_view name = {};
		if (expression.kind == Expression::IDENTIFIER) {
			name = static_cast<const IdentifierExpression&>(expression).name;
		} else if (expression.kind == Expression::MEMBER) {
			const MemberExpression& member = static_cast<const MemberExpression&>(expression);
			if (!member.expression || member.expression->kind != Expression::IDENTIFIER) return {};
			qualifier = static_cast<IdentifierExpression*>(member.expression)->name;
			name = member.name;
		} else {
			return {};
		}
		return resolveSymbol(unit, qualifier, name, LookupPolicy::NameOnly);
	}

	ls_result checkComptimeFunctionSymbol(Unit& unit, Symbol& sym) {
		FunctionExpression& fn = static_cast<FunctionExpression&>(*sym.expression);
		if (fn.is_template) {
			sym.resolved_type = nullptr;
			return LS_RESULT_OK;
		}

		// Signature was built and published by checkSymbol before dispatching here.
		FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(fn.resolved_type);
		if (const Token::Type op_token = tokenFromOperatorName(sym.name); op_token != Token::ERROR) {
			const i32 arity = (i32)fn_type->param_types.size();
			if ((op_token == Token::MINUS ? (arity != 1 && arity != 2) : arity != 2)) {
				errorLine(sym.token, "Invalid operator arity");
				return LS_RESULT_FAILURE;
			}

			bool struct_signature = false;
			for (ResolvedType* param : fn_type->param_types) {
				if (param->kind == ResolvedType::STRUCT) {
					struct_signature = true;
					break;
				}
			}
			if (!struct_signature) {
				errorLine(sym.token, "Operator overloads must have at least one struct parameter");
				return LS_RESULT_FAILURE;
			}

			for (ResolvedType* param : fn_type->param_types) {
				if (param->kind == ResolvedType::ENUM) {
					errorLine(sym.token, "Operator overloads with enum parameters are not allowed; use a wrapper struct instead");
					return LS_RESULT_FAILURE;
				}
			}
		}

		if (!checkFunctionBody(unit, fn)) return LS_RESULT_FAILURE;
		return LS_RESULT_OK;
	}

	bool containsStructByValue(ResolvedType& type, StructResolvedType& target, ExpArray<ResolvedType*>& visited) {
		if (&type == &target) return true;
		if (type.kind == ResolvedType::ARRAY) {
			return containsStructByValue(*static_cast<ArrayResolvedType&>(type).element_type, target, visited);
		}
		if (type.kind == ResolvedType::NULLABLE) {
			return containsStructByValue(*static_cast<NullableResolvedType&>(type).inner, target, visited);
		}
		if (type.kind == ResolvedType::UNION) {
			for (ResolvedType* member : static_cast<UnionResolvedType&>(type).members) {
				if (containsStructByValue(*member, target, visited)) return true;
			}
			return false;
		}
		if (type.kind != ResolvedType::STRUCT) return false;
		for (ResolvedType* seen : visited) {
			if (seen == &type) return false;
		}
		visited.push(&type);
		StructResolvedType& st = static_cast<StructResolvedType&>(type);
		for (ResolvedType* field_type : st.field_types) {
			if (containsStructByValue(*field_type, target, visited)) return true;
		}
		return false;
	}

	ls_result checkComptimeStructSymbol(Unit& unit, Symbol& sym) {
		StructExpression& st = static_cast<StructExpression&>(*sym.expression);
		st.cached_name = sym.name;
		st.cached_owner = &unit;
		StructResolvedType* st_type = makeType<StructResolvedType>(unit, unit.arena);
		st_type->decl = &st;
		MetaType* meta = makeType<MetaType>(unit);
		meta->inner = st_type;
		sym.resolved_type = meta;
		if (!st.comptime_params.empty()) {
			for (NamedDecl& field : st.fields) {
				if (typeExprIsDirectSelfReference(*field.type_expr, sym.name)) {
					errorLine(sym.token, "Recursive by-value field '", field.name, "' in struct: ", sym.name);
					return LS_RESULT_FAILURE;
				}
			}
			for (NamedDecl& param : st.comptime_params) {
				if (!param.type_expr) continue;
				ResolvedType* param_type = resolveTypeExpr(unit, *param.type_expr);
				if (!param_type) return LS_RESULT_FAILURE;

				if (!(param_type->kind >= ResolvedType::BOOL && param_type->kind <= ResolvedType::STRING)) {
					errorLine(sym.token, "Struct template value comptime parameters must be primitive values");
					return LS_RESULT_FAILURE;
				}
				param.resolved_type = param_type;
			}
			return LS_RESULT_OK;
		}

		if (!resolveStructFields(unit, &sym, st, *st_type, nullptr)) return LS_RESULT_FAILURE;
		return LS_RESULT_OK;
	}

	ls_result checkComptimeEnumSymbol(Unit& unit, Symbol& sym) {
		EnumExpression& en = static_cast<EnumExpression&>(*sym.expression);
		en.cached_name = sym.name;
		en.cached_owner = &unit;
		EnumResolvedType* en_type = makeType<EnumResolvedType>(unit);
		en_type->decl = &en;
		MetaType* meta = makeType<MetaType>(unit);
		meta->inner = en_type;
		sym.resolved_type = meta;
		return LS_RESULT_OK;
	}

	ls_result checkComptimeValueSymbol(Unit& unit, Symbol& sym) {
		// Plain comptime value: comptime N = expr;
		ResolvedType* annotation = nullptr;
		if (sym.type_expr) {
			annotation = resolveTypeExpr(unit, *sym.type_expr);
			if (!annotation) {
				// TODO can we even get here?
				return LS_RESULT_FAILURE;
			}
		}

		FunctionCheckContext comptime_ctx(unit.arena);
		comptime_ctx.comptime_only = true;
		// TODO shouldn't we use resolveComptimeValue here instead?
		ResolvedType* expr_type = checkExprMaterialized(unit, &comptime_ctx, *sym.expression, annotation);
		if (!expr_type) return LS_RESULT_FAILURE;

		if (annotation && !canImplicitlyConvert(expr_type, annotation)) {
			errorLine(sym.token, "Cannot convert comptime initializer type ", expr_type, " to annotated type ", annotation, " for: ", sym.name);
			return LS_RESULT_FAILURE;
		}

		if (sym.expression && (sym.expression->kind == Expression::TYPE_LITERAL || sym.expression->kind == Expression::UNION_TYPE)) {
			MetaType* meta = makeType<MetaType>(unit);
			meta->inner = expr_type;
			sym.resolved_type = meta;
		} else {
			sym.resolved_type = annotation ? annotation : expr_type;
		}
		return LS_RESULT_OK;
	}

	ls_result checkComptimeSymbol(Unit& unit, Symbol& sym) {
		switch (sym.expression->kind) {
			case Expression::FUNCTION: return checkComptimeFunctionSymbol(unit, sym);
			case Expression::STRUCT: return checkComptimeStructSymbol(unit, sym);
			case Expression::ENUM: return checkComptimeEnumSymbol(unit, sym);
			default: return checkComptimeValueSymbol(unit, sym);
		}
	}

	ls_result checkRuntimeSymbol(Unit& unit, Symbol& sym) {
		ResolvedType* annotation = nullptr;
		if (sym.type_expr) {
			annotation = resolveTypeExpr(unit, *sym.type_expr);
			if (!annotation) {
				// TODO can we even get here?
				return LS_RESULT_FAILURE;
			}
		}
		ASSERT(sym.expression);
		Expression* expr = sym.expression;

		if (expr->kind == Expression::UNDEFINED) {
			if (!annotation) {
				// var a = undefined; - no way to know the type
				errorLine(sym.token, "'undefined' initializer requires an explicit type annotation: ", sym.name);
				return LS_RESULT_FAILURE;
			}
			if (sym.storage == Symbol::CONST) {
				// const a : i32 = undefined; - not useful
				errorLine(sym.token, "const cannot be initialized with 'undefined': ", sym.name);
				return LS_RESULT_FAILURE;
			}
		}

		if (expr->kind == Expression::FUNCTION && !static_cast<FunctionExpression&>(*expr).is_template) {
			// checkSymbol already published the signature as the symbol's type; the body
			// must be checked here because checkExpr treats the pre-built signature as an
			// already-checked cache entry and skips the body.
			if (!checkFunctionBody(unit, static_cast<FunctionExpression&>(*expr))) return LS_RESULT_FAILURE;
		}

		ResolvedType* expr_type = checkExprMaterialized(unit, nullptr, *expr, annotation);
		if (!expr_type) return LS_RESULT_FAILURE;
		if (annotation && !canImplicitlyConvert(expr_type, annotation)) {
			errorLine(sym.token, "Cannot convert initializer type ", expr_type, " to annotated type ", annotation, " for: ", sym.name);
			return LS_RESULT_FAILURE;
		}

		sym.resolved_type = annotation ? annotation : expr_type;
		return LS_RESULT_OK;
	}

	FunctionExpression* instantiateAndCheckTemplate(Unit& unit, FunctionCheckContext* ctx, Expression& call_expr, CallExpression& call, Unit& template_unit, FunctionExpression& fn, u32 ufcs_param_offset = 0) {
		ASSERT(fn.is_template);
		TemplateBindings bindings(unit.arena); // TODO reuse?
		if (fn.params.size() != call.args.size() + ufcs_param_offset) {
			errorLine(call_expr.token, "Function call argument count mismatch: expected ", fn.params.size() - ufcs_param_offset, ", got ", call.args.size());
			return nullptr;
		}
		if (ufcs_param_offset) {
			MemberExpression& mem = static_cast<MemberExpression&>(*call.callee);
			if (!inferTemplateArg(template_unit, bindings, *fn.params[0].type_expr, ComptimeValue(mem.expression->resolved_type))) {
				errorLine(call_expr.token, "Cannot infer template arguments for receiver of ", fn.token.value);
				return nullptr;
			}
		}

		for (i32 i = 0; i < call.args.size(); ++i) {
			const u32 param_index = ufcs_param_offset + i;
			FunctionParam& param = fn.params[param_index];
			Expression* arg = call.args[i];

			ResolvedType* expected = nullptr;
			if (param.is_comptime
				&& param.type_expr->kind == Expression::TYPE_LITERAL
				&& static_cast<TypeLiteralExpression*>(param.type_expr)->type == ResolvedType::META)
			{
				expected = nullptr;
			} else {
				// TODO why suppress errors?
				++suppress_errors;
				expected = resolveTypeExpr(template_unit, *param.type_expr, &bindings);
				--suppress_errors;
			}

			if (param.is_comptime) {
				ComptimeValue template_arg;
				template_arg = resolveComptimeValue(unit, *arg, &bindings);
				if (!comptimeValueMatchesExpected(template_arg, expected)) {
					errorLine(arg->token, "Could not resolve comptime template argument");
					return nullptr;
				}
				if (!bindTemplateArg(bindings, param.name, template_arg)) {
					errorLine(arg->token, "Conflicting comptime template argument");
					return nullptr;
				}
				continue;
			}

			ResolvedType* arg_type = nullptr;
			if (param.is_ref) {
				if (arg->kind != Expression::UNARY) {
					errorLine(arg->token, "Cannot pass non-ref expression as ref argument ", i + 1, " of function call");
					return nullptr;
				}
				UnaryExpression* un = static_cast<UnaryExpression*>(arg);
				if (un->op != Token::REF) {
					errorLine(arg->token, "Cannot pass non-ref expression as ref argument ", i + 1, " of function call");
					return nullptr;
				}
				bool writable = false;
				arg_type = checkAssignableExpr(unit, ctx, *un->expression, writable);
				if (!arg_type) return nullptr;
				if (!writable) {
					errorLine(arg->token, "Cannot pass non-writable expression as ref argument ", i + 1, " of function call");
					return nullptr;
				}
			} else {
				arg_type = checkExprMaterialized(unit, ctx, *arg, expected);
				if (!arg_type) return nullptr;
				if (expected && !canImplicitlyConvert(arg_type, expected)) {
					errorLine(arg->token, "Cannot convert ", arg_type, " to ", expected, " for argument ", i + 1, " of function call");
					return nullptr;
				}
			}
			if (!inferTemplateArg(template_unit, bindings, *param.type_expr, ComptimeValue(arg_type))) {
				errorLine(arg->token, "Cannot infer template parameter type for argument ", i + 1, " of ", fn.token.value);
				return nullptr;
			}
		}

		FunctionExpression* instance = instantiateFunctionTemplate(template_unit, fn, bindings);
		if (!instance) return nullptr;
		return instance;
	}

	ResolvedType* checkCallExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr) {
		CallExpression& call = static_cast<CallExpression&>(expr);

		// length(slice | array)
		if (call.callee->kind == Expression::IDENTIFIER && call.args.size() == 1) {
			ls_string_view name = static_cast<IdentifierExpression&>(*call.callee).name;
			if (equalStrings(name, makeStringView("length"))) {
				ResolvedType* arg = checkExpr(unit, ctx, *call.args[0], nullptr);
				if (arg && (arg->kind == ResolvedType::ARRAY || arg->kind == ResolvedType::SLICE)) {
					expr.resolved_type = primitiveType(ResolvedType::ISIZE);
					return expr.resolved_type;
				}
			}
		}

		// Suppress errors while probing normal lookup because a failed member lookup can
		// fall back to UFCS, and the first argument can be checked again during the call.
		++suppress_errors;
		ResolvedType* first_arg_type = nullptr;
		if (!call.args.empty()) {
			first_arg_type = checkExpr(unit, ctx, *call.args[0], nullptr);
		}
		ResolvedType* callee_type = checkExpr(unit, ctx, *call.callee, nullptr, first_arg_type);
		--suppress_errors;

		if (callee_type) {
			switch (callee_type->kind) {
				case ResolvedType::FUNCTION:
					return checkCallCandidate(unit, ctx, call, static_cast<FunctionResolvedType&>(*callee_type));
				case ResolvedType::NULLABLE:
					errorLine(expr.token, "Cannot call nullable function type ", callee_type, "without a null check");
					return nullptr;
				default:
					errorLine(expr.token, "Cannot call non-function type ", callee_type);
					return nullptr;
			}
		}

		// template with inferred parameters
		if (SymbolRef sym = resolveSymbol(unit, *call.callee)) {
			FunctionExpression* fn = asFunctionExpression(*sym.symbol);
			if (!fn) {
				errorLine(expr.token, "Cannot call ", symbolKind(*sym.symbol), " '", sym.symbol->name, "' as a function");
				return nullptr;
			}
			if (fn->is_template) {
				FunctionExpression* instance = instantiateAndCheckTemplate(unit, ctx, expr, call, *sym.owner, *fn);
				if (!instance) return nullptr;
				if (call.callee->kind == Expression::IDENTIFIER) static_cast<IdentifierExpression*>(call.callee)->symbol = sym.symbol;
				FunctionResolvedType* fn_type = asFunctionType(instance->resolved_type);
				ASSERT(fn_type);
				return checkCallCandidate(unit, ctx, call, *fn_type, instance);
			}

			errorLine(expr.token, "Cannot call ", symbolKind(*sym.symbol), " '", sym.symbol->name, "' as a function");
			return nullptr;
		}

		// UFCS: x.foo(a, b) -> foo(x, a, b)
		if (call.callee->kind != Expression::MEMBER) {
			// check again without suppressed errors
			checkExpr(unit, ctx, *call.callee, nullptr, first_arg_type);
			return nullptr;
		}

		MemberExpression& mem = static_cast<MemberExpression&>(*call.callee);

		if (!mem.expression) {
			errorLine(expr.token, "Expected receiver expression for function call ", mem.name);
			return nullptr;
		}

		if (!mem.expression->resolved_type) {
			errorLine(expr.token, "Cannot call function ", mem.name, " with UFCS receiver with unknown type");
			return nullptr;
		}

		ResolvedType& receiver_type = *mem.expression->resolved_type;
		if (receiver_type.kind != ResolvedType::STRUCT && receiver_type.kind != ResolvedType::ENUM) {
			errorLine(expr.token, "Cannot call member function ", mem.name, " on type ", &receiver_type, ", expected struct or enum");
			return nullptr;
		}

		// Method syntax dispatches on the receiver: the type's own unit wins over
		// local and imported declarations, so e.g. a script's own `init` does not
		// shadow `array.init` in `a.init()`. Lexical lookup is only a fallback.
		SymbolRef ref;
		if (Unit* namespace_unit = findTypeNamespaceUnit(receiver_type)) {
			if (Symbol* candidate = findSymbol(*namespace_unit, mem.name)) {
				ref = {namespace_unit, candidate};
				if (checkSymbol(*namespace_unit, *candidate) == LS_RESULT_FAILURE) ref.check_failed = true;
			}
		}
		if (!ref.symbol) ref = resolveSymbol(unit, {}, mem.name, LookupPolicy::Checked);

		if (!ref) {
			errorLine(expr.token, "Could not resolve member function: ", mem.name);
			return nullptr;
		}

		FunctionExpression* fn = asFunctionExpression(*ref.symbol);
		if (fn && fn->is_template) {
			FunctionExpression* instance = instantiateAndCheckTemplate(unit, ctx, expr, call, *ref.owner, *fn, 1);
			if (!instance) return nullptr;
			fn = instance;
		}
		FunctionResolvedType* fn_type = asFunctionType(fn ? fn->resolved_type : ref.symbol->resolved_type);
		if (!fn_type) {
			errorLine(expr.token, "Cannot call ", symbolKind(*ref.symbol), " '", ref.symbol->name, "' as a function");
			return nullptr;
		}
		return checkCallCandidate(unit, ctx, call, *fn_type, fn, 1);
	}

	// Pin an untyped numeric expression to a concrete type.
	// A concrete `concrete` range-checks leaf literals; a null/non-numeric `concrete`
	// uses the literal's default inferred width.
	// Pin an untyped numeric expression to a concrete type. With no compatible
	// target, choose the expression's default type; recursive calls pass false
	// after the target has already been selected.
	ResolvedType* materializeUntyped(Expression& expr, ResolvedType* concrete, bool check_fit = true) {
		if (!isUntypedNumeric(*expr.resolved_type)) return expr.resolved_type;
		if (check_fit) {
			const bool compatible_target = concrete && ((expr.resolved_type->kind == ResolvedType::UNTYPED_INT && isNumericType(*concrete)) ||
														   (expr.resolved_type->kind == ResolvedType::UNTYPED_FLOAT && isFloatType(*concrete)));
			if (!compatible_target) {
				if (expr.resolved_type->kind == ResolvedType::UNTYPED_INT) {
					// No target: pin to the narrowest default integer width that holds the value.
					ResolvedType::Kind kind = ResolvedType::I32;
					if (expr.kind == Expression::INT_LITERAL) {
						const u64 value = static_cast<IntLiteralExpression&>(expr).value;
						if (value > 9223372036854775807ull)
							kind = ResolvedType::U64;
						else if (value > 2147483647u)
							kind = ResolvedType::I64;
					} else if (expr.kind == Expression::UNARY) {
						UnaryExpression& un = static_cast<UnaryExpression&>(expr);
						if (un.op == Token::MINUS && un.expression && un.expression->kind == Expression::INT_LITERAL) {
							const u64 magnitude = static_cast<IntLiteralExpression*>(un.expression)->value;
							if (magnitude > 9223372036854775808ull) {
								errorLine(expr.token, "Integer literal does not fit in any integer type");
								return nullptr;
							}
							if (magnitude > 2147483648u) kind = ResolvedType::I64;
						}
					}
					concrete = primitiveType(kind);
				} else {
					concrete = primitiveType(ResolvedType::F64);
				}
				check_fit = false;
			}
		}
		if (check_fit && !canMaterializeUntyped(expr, *concrete)) {
			errorLine(expr.token, "Untyped expression does not fit in ", concrete);
			return nullptr;
		}
		switch (expr.kind) {
			case Expression::INT_LITERAL: {
				expr.resolved_type = concrete;
				return concrete;
			}
			case Expression::SIZEOF: {
				expr.resolved_type = concrete;
				return concrete;
			}
			case Expression::FLOAT_LITERAL: {
				expr.resolved_type = concrete;
				return concrete;
			}
			case Expression::UNARY: {
				UnaryExpression& un = static_cast<UnaryExpression&>(expr);
				// A negated integer literal range-checks against the target width.
				if (un.op == Token::MINUS && un.expression && un.expression->kind == Expression::INT_LITERAL) {
					un.expression->resolved_type = concrete;
					expr.resolved_type = concrete;
					return concrete;
				}
				if (un.expression && !materializeUntyped(*un.expression, concrete, false)) return nullptr;
				expr.resolved_type = concrete;
				return concrete;
			}
			case Expression::BINARY: {
				BinaryExpression& bin = static_cast<BinaryExpression&>(expr);
				if (bin.lhs && !materializeUntyped(*bin.lhs, concrete, false)) return nullptr;
				if (bin.rhs && !materializeUntyped(*bin.rhs, concrete, false)) return nullptr;
				expr.resolved_type = concrete;
				return concrete;
			}
			case Expression::TERNARY: {
				TernaryExpression& tern = static_cast<TernaryExpression&>(expr);
				if (!materializeUntyped(*tern.true_expr, concrete, false)) return nullptr;
				if (!materializeUntyped(*tern.false_expr, concrete, false)) return nullptr;
				expr.resolved_type = concrete;
				return concrete;
			}
			default: expr.resolved_type = concrete; return concrete;
		}
	}

	ResolvedType* checkUnaryExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		UnaryExpression& un = static_cast<UnaryExpression&>(expr);
		if (un.op == Token::MINUS && un.expression->kind == Expression::INT_LITERAL) {
			// Range-check the negated integer literal against the expected type.
			IntLiteralExpression* lit = static_cast<IntLiteralExpression*>(un.expression);
			ResolvedType* int_hint = unwrapNullable(hint);
			if (int_hint && isNumericType(*int_hint)) {
				if (!negatedIntLiteralFitsType(lit->value, int_hint->kind)) {
					errorLine(expr.token, "Integer literal does not fit in type ", int_hint);
					return nullptr;
				}
				lit->resolved_type = int_hint;
				expr.resolved_type = int_hint;
				return int_hint;
			}
			// No concrete target: stay untyped until materialization chooses a width.
			lit->resolved_type = primitiveType(ResolvedType::UNTYPED_INT);
			expr.resolved_type = lit->resolved_type;
			return expr.resolved_type;
		}
		ResolvedType* inner = checkExpr(unit, ctx, *un.expression, hint);
		if (!inner) return nullptr;

		if (un.op == Token::MINUS && inner->kind == ResolvedType::STRUCT) {
			ResolvedType* overload_result = nullptr;
			FunctionExpression* overload_fn = nullptr;
			StructExpression* host = static_cast<StructResolvedType*>(inner)->decl;
			OverloadResult unary_result = resolveOperatorOverload(unit, ctx, un.op, 1, &un.expression, &inner, *host, overload_result, overload_fn);
			switch (unary_result) {
				case OverloadResult::FOUND: un.resolved_fn = overload_fn; expr.resolved_type = overload_result; return overload_result;
				case OverloadResult::AMBIGUOUS: errorLine(expr.token, "Ambiguous operator ", operatorSymbolName(un.op), " overload"); return nullptr;
				case OverloadResult::FAILED: return nullptr;
				case OverloadResult::NOT_FOUND: errorLine(expr.token, "No matching operator ", operatorSymbolName(un.op), " overload"); return nullptr;
			}
		}

		switch (un.op) {
			case Token::MINUS: {
				if (!isNumericOrUntyped(*inner)) {
					errorLine(expr.token, "Cannot apply negation operator to ", inner);
					return nullptr;
				}
				const ResolvedType::Kind k = inner->kind;
				if (k == ResolvedType::U8 || k == ResolvedType::U16 || k == ResolvedType::U32 || k == ResolvedType::U64) {
					// TODO error msg
					return nullptr;
				}
				expr.resolved_type = inner;
				return inner;
			}
			case Token::NOT:
				if (!typesEqual(inner, primitiveType(ResolvedType::BOOL))) {
					errorLine(expr.token, "Cannot apply not operator to ", inner);
					return nullptr;
				}
				expr.resolved_type = primitiveType(ResolvedType::BOOL);
				return expr.resolved_type;
			case Token::REF:
				// TODO error msg?
				return nullptr;
			default:
				// TODO error msg
				return nullptr;
		}
	}

	ResolvedType* checkBinaryExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		BinaryExpression& bin = static_cast<BinaryExpression&>(expr);
		// lhs is probed first because context-dependent syntax may need rhs as its hint.
		// e.g. .Idle == e
		++suppress_errors;
		ResolvedType* lhs = checkExpr(unit, ctx, *bin.lhs, nullptr);
		--suppress_errors;
		// typeless struct literals are not allowed in operators
		// so we can't use structs as hints
		const bool lhs_is_struct = lhs && lhs->kind == ResolvedType::STRUCT;
		ResolvedType* rhs = checkExpr(unit, ctx, *bin.rhs, lhs_is_struct ? nullptr : (lhs ? lhs : hint));
		if (!rhs) return nullptr;
		// Retry the lhs once the rhs gives it a non-struct contextual type.
		// try to use rhs's type as hint for lhs, e.g. 2 * v, or .Idle == e
		if (!lhs) lhs = checkExpr(unit, ctx, *bin.lhs, rhs->kind == ResolvedType::STRUCT ? nullptr : rhs);
		if (!lhs) return nullptr;
		if (bin.op == Token::IS) {
			if (lhs->kind != ResolvedType::UNION || rhs->kind != ResolvedType::META) {
				errorLine(expr.token, "Union membership test requires a union value and member type");
				return nullptr;
			}
			ResolvedType* member = unwrapMeta(rhs);
			for (ResolvedType* candidate : static_cast<UnionResolvedType*>(lhs)->members) {
				if (typesEqual(candidate, member)) {
					expr.resolved_type = primitiveType(ResolvedType::BOOL);
					return expr.resolved_type;
				}
			}
			errorLine(expr.token, "Type ", member, " is not a member of union ", lhs);
			return nullptr;
		}

		// operator overload, at least one of operands must be struct
		if (lhs_is_struct || rhs->kind == ResolvedType::STRUCT) {
			if (!operatorSymbolName(bin.op)) {
				// TODO can we even get here?
				return nullptr;
			}
			ResolvedType* overload_result = nullptr;
			FunctionExpression* overload_fn = nullptr;
			Expression* bin_operands[2] = {bin.lhs, bin.rhs};

			ResolvedType* operand_types[2] = {lhs, rhs};
			StructExpression* host =
				lhs->kind == ResolvedType::STRUCT
					? static_cast<StructResolvedType*>(lhs)->decl
					: static_cast<StructResolvedType*>(rhs)->decl;
			OverloadResult bin_overload = resolveOperatorOverload(unit, ctx, bin.op, 2, bin_operands, operand_types, *host, overload_result, overload_fn);
			switch (bin_overload) {
				case OverloadResult::FOUND: bin.resolved_fn = overload_fn; expr.resolved_type = overload_result; return overload_result;
				case OverloadResult::AMBIGUOUS: errorLine(expr.token, "Ambiguous operator ", operatorSymbolName(bin.op), " overload"); return nullptr;
				case OverloadResult::FAILED: return nullptr;
				case OverloadResult::NOT_FOUND: errorLine(expr.token, "No matching operator ", operatorSymbolName(bin.op), " overload"); return nullptr;
			}
		}

		// Resolve a numeric operator: unify the operands and pin both to the result type.
		enum class NumericMode { ARITHMETIC, INTEGER, COMPARISON };
		auto resolveNumeric = [&](NumericMode mode) -> ResolvedType* {
			if (!isNumericOrUntyped(*lhs) || !isNumericOrUntyped(*rhs)) {
				errorLine(expr.token, "Operator ", operatorSymbolName(bin.op), " expects numeric operands, got ", lhs, " and ", rhs);
				return nullptr;
			}
			ResolvedType* unified = unifyNumeric(*lhs, *rhs);
			if (mode == NumericMode::INTEGER && (!unified || !isIntegerOrUntyped(*unified))) {
				errorLine(expr.token, "Operator ", operatorSymbolName(bin.op), " expects integer operands, got ", lhs, " and ", rhs);
				return nullptr;
			}
			if (!unified) {
				if ((isFloatType(*lhs) && isIntegerType(*rhs)) || (isIntegerType(*lhs) && isFloatType(*rhs))) {
					errorLine(expr.token, "Cannot mix integer and float operands with operator ", operatorSymbolName(bin.op), ", got ", lhs, " and ", rhs);
					return nullptr;
				}
				errorLine(expr.token, "Cannot apply operator ", operatorSymbolName(bin.op), " to ", lhs, " and ", rhs);
				return nullptr;
			}
			if (mode != NumericMode::COMPARISON && isUntypedNumeric(*unified)) {
				return unified;
			}
			ResolvedType* concrete = unified;
			if (mode == NumericMode::COMPARISON) {
				if (unified->kind == ResolvedType::UNTYPED_INT)
					concrete = primitiveType(ResolvedType::I32);
				else if (unified->kind == ResolvedType::UNTYPED_FLOAT)
					concrete = primitiveType(ResolvedType::F64);
			}
			if (!materializeUntyped(*bin.lhs, concrete) || !materializeUntyped(*bin.rhs, concrete)) return nullptr;
			return concrete;
		};

		ResolvedType* result = nullptr;
		switch (bin.op) {
			case Token::PLUS:
				if (typesEqual(lhs, primitiveType(ResolvedType::STRING)) && typesEqual(rhs, primitiveType(ResolvedType::STRING))) {
					result = lhs;
					break;
				}
				if (typesEqual(lhs, primitiveType(ResolvedType::STRING)) || typesEqual(rhs, primitiveType(ResolvedType::STRING))) {
					errorLine(expr.token, "String concatenation requires both operands to be string, got ", lhs, " and ", rhs);
					return nullptr;
				}
				[[fallthrough]];
			case Token::MINUS:
			case Token::STAR:
			case Token::SLASH:
				// Arithmetic keeps the result untyped when both operands are untyped.
				result = resolveNumeric(NumericMode::ARITHMETIC);
				break;
			case Token::PERCENT: result = resolveNumeric(NumericMode::INTEGER); break;
			case Token::EQUAL_EQUAL:
			case Token::BANG_EQUAL: {
				if (lhs->kind == ResolvedType::UNION || rhs->kind == ResolvedType::UNION) {
					errorLine(expr.token, "Cannot compare union values");
					return nullptr;
				}
				// Equality also works on non-numerics (enums, strings); only unify when numeric.
				if (isNumericOrUntyped(*lhs) || isNumericOrUntyped(*rhs)) {
					if (!resolveNumeric(NumericMode::COMPARISON)) return nullptr;
				} else if (!typesEqual(lhs, rhs)) {
					errorLine(expr.token, "Cannot compare ", lhs, " and ", rhs);
					return nullptr;
				} else {
					// Equality is defined only for kinds with a well-defined comparison:
					// value kinds compare bitwise, strings by content, cstr/cptr by
					// address. Nullable values compare only against the null literal.
					// Aggregates (arrays, slices, nullables) have no equality.
					bool comparable = false;
					switch (lhs->kind) {
						case ResolvedType::BOOL:
						case ResolvedType::ENUM:
						case ResolvedType::STRING:
						case ResolvedType::CSTR:
						case ResolvedType::CPTR:
						case ResolvedType::BYTE:
						case ResolvedType::FUNCTION:
							comparable = true;
							break;
						case ResolvedType::NULLABLE:
							comparable = (bin.rhs && bin.rhs->kind == Expression::NULL_LITERAL)
								|| (bin.lhs && bin.lhs->kind == Expression::NULL_LITERAL);
							break;
						default:
							break;
					}
					if (!comparable) {
						errorLine(expr.token, "Cannot compare ", lhs, " and ", rhs);
						return nullptr;
					}
				}
				result = primitiveType(ResolvedType::BOOL);
				break;
			}
			case Token::LT:
			case Token::LT_EQUAL:
			case Token::GT:
			case Token::GT_EQUAL:
				if (!resolveNumeric(NumericMode::COMPARISON)) return nullptr;
				result = primitiveType(ResolvedType::BOOL);
				break;
			case Token::AND:
			case Token::OR:
				if (!typesEqual(lhs, primitiveType(ResolvedType::BOOL)) || !typesEqual(rhs, primitiveType(ResolvedType::BOOL))) {
					errorLine(expr.token, "Logical operator requires bool operands, got ", lhs, " and ", rhs);
					return nullptr;
				}
				result = primitiveType(ResolvedType::BOOL);
				break;
			default:
				// TODO error msg, can we even get here?
				return nullptr;
		}
		if (!result) return nullptr;
		expr.resolved_type = result;
		return result;
	}

	ResolvedType* checkTernaryExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		TernaryExpression& tern = static_cast<TernaryExpression&>(expr);

		ResolvedType* cond = checkExpr(unit, ctx, *tern.condition, nullptr);
		if (!cond) return nullptr;

		if (!typesEqual(cond, primitiveType(ResolvedType::BOOL))) {
			errorLine(expr.token, "Ternary condition must be bool, got ", cond);
			return nullptr;
		}

		ResolvedType* true_type = checkExpr(unit, ctx, *tern.true_expr, hint);
		if (!true_type) return nullptr;

		ResolvedType* false_type = checkExpr(unit, ctx, *tern.false_expr, hint);
		if (!false_type) return nullptr;

		if (!typesEqual(true_type, false_type)) {
			errorLine(expr.token, "Ternary branches have different types: ", true_type, " and ", false_type);
			return nullptr;
		}

		expr.resolved_type = true_type;
		return true_type;
	}

	ResolvedType* checkCastExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr) {
		CastExpression& cast = static_cast<CastExpression&>(expr);
		ResolvedType* dst_type = resolveTypeExpr(unit, *cast.type_expr);
		if (!dst_type) return nullptr;

		// Don't pass dst_type as hint: explicit casts allow out-of-range values and
		// the operand resolves independently (e.g. `-1 as u8` should work).
		ResolvedType* src_type = checkExpr(unit, ctx, *cast.expression, nullptr);
		if (!src_type) return nullptr;
		// The source is consumed as a value here; pin an untyped literal to its i32 default
		// (an explicit cast then converts it, so no range check against the destination).
		if (isUntypedNumeric(*src_type)) {
			src_type = materializeUntyped(*cast.expression, nullptr);
			if (!src_type) return nullptr;
		}
		const bool src_numeric = isNumericType(*src_type);
		const bool dst_numeric = isNumericType(*dst_type);
		const bool src_integer = isIntegerType(*src_type);
		const bool dst_integer = isIntegerType(*dst_type);
		const bool src_enum = src_type->kind == ResolvedType::ENUM;
		const bool dst_enum = dst_type->kind == ResolvedType::ENUM;
		// Slice reinterpret cast: `[]byte as []T` / `[]T as []byte`. Exactly one side must
		// have `byte` elements; reinterpreting between two unrelated typed slices is rejected.
		bool slice_reinterpret = false;
		if (src_type->kind == ResolvedType::SLICE && dst_type->kind == ResolvedType::SLICE) {
			ResolvedType* src_elem = static_cast<SliceResolvedType*>(src_type)->element_type;
			ResolvedType* dst_elem = static_cast<SliceResolvedType*>(dst_type)->element_type;
			const bool src_byte = src_elem && src_elem->kind == ResolvedType::BYTE;
			const bool dst_byte = dst_elem && dst_elem->kind == ResolvedType::BYTE;
			slice_reinterpret = src_byte != dst_byte;
		}
		// bool->bool (and any other same-type cast) is covered by the trailing typesEqual.
		const bool string_cstr_cast = (src_type->kind == ResolvedType::STRING && dst_type->kind == ResolvedType::CSTR)
			|| (src_type->kind == ResolvedType::CSTR && dst_type->kind == ResolvedType::STRING);
		if (src_type->kind == ResolvedType::UNION) {
			const UnionResolvedType* un = static_cast<const UnionResolvedType*>(src_type);
			for (ResolvedType* member : un->members) {
				if (!typesEqual(member, dst_type)) continue;
				NullableResolvedType* nullable = makeType<NullableResolvedType>(unit);
				nullable->inner = dst_type;
				expr.resolved_type = nullable;
				return nullable;
			}
		}
		const bool valid_cast = (src_numeric && dst_numeric) || (src_enum && dst_integer) || (src_integer && dst_enum) || slice_reinterpret || string_cstr_cast || typesEqual(src_type, dst_type);
		if (!valid_cast) {
			errorLine(expr.token, "Cannot cast ", src_type, " to ", dst_type);
			return nullptr;
		}
		expr.resolved_type = dst_type;
		return dst_type;
	}

	ResolvedType* checkMemberExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		MemberExpression& member = static_cast<MemberExpression&>(expr);

		if (!member.expression) {
			// .enum_member
			if (!hint) {
				errorLine(expr.token, "Cannot resolve .", member.name, ", use EnumName.value syntax or provide a hint");
				return nullptr;
			}

			hint = unwrapNullable(hint);
			if (hint->kind == ResolvedType::ENUM) {
				EnumResolvedType* en = static_cast<EnumResolvedType*>(hint);
				for (const EnumMember& m : en->decl->members) {
					if (equalStrings(m.name, member.name)) {
						expr.resolved_type = en;
						return en;
					}
				}
				errorLine(expr.token, ".", member.name, " not found in ", hint);
				return nullptr;
			}
			errorLine(expr.token, "Cannot convert .", member.name, " to ", hint);
			return nullptr;
		}

		ResolvedType* base_type = checkExpr(unit, ctx, *member.expression, nullptr);
		if (!base_type) {
			// alias.*
			if (!member.expression || member.expression->kind != Expression::IDENTIFIER) {
				return nullptr;
			}
			IdentifierExpression* id = static_cast<IdentifierExpression*>(member.expression); // parser makes sure member.expression is identifier
			Unit* imported_unit = findImportedUnitByAlias(unit, id->name);
			if (!imported_unit) {
				errorLine(expr.token, "Unknown identifier ", id->name);
				return nullptr;
			}
			
			SymbolRef sym = resolveSymbol(*imported_unit, {}, member.name, LookupPolicy::Checked);
			if (sym.symbol) {
				if (sym.check_failed) return nullptr;

				expr.resolved_type = sym.symbol->resolved_type;
				member.resolved_symbol = sym.symbol;
				if (sym.symbol->expression && sym.symbol->expression->kind == Expression::FUNCTION) {
					member.resolved_fn = static_cast<FunctionExpression*>(sym.symbol->expression);
				}
				return expr.resolved_type;
			}
			errorLine(expr.token, member.name, " not found in ", id->name);
			return nullptr;
		}

		switch (base_type->kind) {
			case ResolvedType::STRUCT: {
				// struct.field
				StructResolvedType* st = static_cast<StructResolvedType*>(base_type);
				for (i32 i = 0; i < st->decl->fields.size(); ++i) {
					const NamedDecl& field = st->decl->fields[i];
					if (equalStrings(field.name, member.name)) {
						ResolvedType* field_type = structFieldType(*st, i);
						expr.resolved_type = field_type;
						return field_type;
					}
				}
				errorLine(expr.token, member.name, " not found in ", base_type);
				return nullptr;
			}
			case ResolvedType::META: {
				// TypeName.member — only enums support member access through a type name
				ResolvedType* inner = static_cast<MetaType*>(base_type)->inner;
				if (inner->kind == ResolvedType::ENUM) {
					EnumResolvedType* en = static_cast<EnumResolvedType*>(inner);
					for (const EnumMember& m : en->decl->members) {
						if (equalStrings(m.name, member.name)) {
							expr.resolved_type = inner;
							return inner;
						}
					}
					errorLine(expr.token, member.name, " not found in ", inner);
					return nullptr;
				}
				errorLine(expr.token, "Cannot access member '", member.name, "' on type");
				return nullptr;
			}
			case ResolvedType::ENUM: {
				// If the name matches a variant, the user wrote instance.Variant — give a clear error.
				// Otherwise return nullptr silently so the call checker can try UFCS.
				EnumResolvedType* en = static_cast<EnumResolvedType*>(base_type);
				for (const EnumMember& m : en->decl->members) {
					if (equalStrings(m.name, member.name)) {
						errorLine(expr.token, "Cannot access enum member '", member.name, "' through an instance; use the enum type name instead");
						return nullptr;
					}
				}
				return nullptr;
			}
			case ResolvedType::NULLABLE: errorLine(expr.token, "Cannot access member ", member.name, " of nullable type without a null check"); return nullptr;

			default:
				if (isUntypedNumeric(*base_type)) {
					errorLine(expr.token, "Cannot access member ", member.name, " of untyped numeric type ", base_type);
					return nullptr;
				}
				if (base_type->kind >= ResolvedType::VOID && base_type->kind <= ResolvedType::CPTR) {
					errorLine(expr.token, "Cannot access member ", member.name, " of primitive type ", base_type);
					return nullptr;
				}

				errorLine(expr.token, "Cannot access member ", member.name, " on type ", base_type);
				return nullptr;
		}
	}

	ResolvedType* checkBracketExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		BracketExpression& br = static_cast<BracketExpression&>(expr);
		++suppress_errors;
		ResolvedType* template_type = resolveTypeExpr(unit, expr);
		--suppress_errors;
		if (template_type) {
			// templates
			expr.resolved_type = template_type;
			return template_type;
		}

		ResolvedType* base_type = checkExpr(unit, ctx, *br.base, nullptr);
		if (!base_type) return nullptr;

		if (base_type->kind == ResolvedType::NULLABLE) {
			errorLine(expr.token, "Cannot index nullable type without a null check");
			return nullptr;
		}

		if (base_type->kind != ResolvedType::ARRAY && base_type->kind != ResolvedType::SLICE) {
			errorLine(expr.token, "Cannot index type ", base_type);
			return nullptr;
		}

		if (br.args.size() != 1) {
			errorLine(expr.token, "Indexing expects exactly one argument");
			return nullptr;
		}

		ResolvedType* index_type = checkExpr(unit, ctx, *br.args[0], primitiveType(ResolvedType::I32));
		if (!index_type) return nullptr;

		if (!isIntegerType(*index_type)) {
			errorLine(expr.token, "Cannot index with type ", index_type, ", expected integer type");
			return nullptr;
		}

		if (base_type->kind == ResolvedType::ARRAY) {
			const ArrayResolvedType* arr = static_cast<const ArrayResolvedType*>(base_type);
			i64 index = 0;
			++suppress_errors;
			const bool is_comptime = resolveComptimeIntValue(unit, br.args[0], index);
			--suppress_errors;
			if (is_comptime) {
				if (index < 0 || index >= arr->size) {
					errorLine(expr.token, "Array index out of bounds: ", index, " (array size: ", arr->size, ")");
					return nullptr;
				}
			}
			expr.resolved_type = arr->element_type;
		} else {
			expr.resolved_type = static_cast<SliceResolvedType*>(base_type)->element_type;
		}
		return expr.resolved_type;
	}

	ResolvedType* checkSliceExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr) {
		SliceExpression& sl = static_cast<SliceExpression&>(expr);
		ResolvedType* base_type = checkExpr(unit, ctx, *sl.base, nullptr);
		if (!base_type) return nullptr;

		if (base_type->kind == ResolvedType::NULLABLE) {
			errorLine(expr.token, "Cannot index nullable type without a null check");
			return nullptr;
		}

		if (base_type->kind != ResolvedType::ARRAY && base_type->kind != ResolvedType::SLICE) {
			errorLine(expr.token, "Cannot index type ", base_type);
			return nullptr;
		}

		Expression* bounds[] = {sl.begin, sl.end};
		for (Expression* bound : bounds) {
			if (!bound) continue;
			ResolvedType* bound_type = checkExpr(unit, ctx, *bound, primitiveType(ResolvedType::I32));
			if (!bound_type) return nullptr;

			if (!isIntegerType(*bound_type)) {
				errorLine(expr.token, "Cannot slice with type ", bound_type, ", expected integer type");
				return nullptr;
			}
		}

		const ArrayResolvedType* arr = base_type->kind == ResolvedType::ARRAY ? static_cast<const ArrayResolvedType*>(base_type) : nullptr;
		if (arr) {
			i64 begin = 0;
			i64 end = arr->size;
			++suppress_errors;
			const bool has_begin = sl.begin && resolveComptimeIntValue(unit, sl.begin, begin);
			const bool has_end = sl.end ? resolveComptimeIntValue(unit, sl.end, end) : true;
			--suppress_errors;
			if (has_begin && (begin < 0 || begin > arr->size)) {
				errorLine(expr.token, "Array slice begin index out of bounds: ", begin, " (array size: ", arr->size, ")");
				return nullptr;
			}
			if (has_end && (end < 0 || end > arr->size)) {
				errorLine(expr.token, "Array slice end index out of bounds: ", end, " (array size: ", arr->size, ")");
				return nullptr;
			}
			if (has_begin && has_end && begin > end) {
				errorLine(expr.token, "Array slice begin index ", begin, " is greater than end index ", end);
				return nullptr;
			}
		}

		SliceResolvedType* slice = makeType<SliceResolvedType>(unit);
		slice->element_type = arr ? arr->element_type : static_cast<SliceResolvedType*>(base_type)->element_type;
		expr.resolved_type = slice;
		return slice;
	}

	ResolvedType* checkStructLiteralExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		StructLiteralExpression& lit = static_cast<StructLiteralExpression&>(expr);
		// In the type position of a literal, a top-level type name wins over a
		// same-named local value. This is also what lets `template[x](x { ... })`
		// use `x` as a type argument and as an ordinary local index nearby.
		ResolvedType* type = nullptr;
		if (lit.type) {
			type = resolveTypeExpr(unit, *lit.type);
			if (!type) return nullptr;
		}
		if (!type) type = hint;
		if (!type) {
			errorLine(expr.token, "Cannot resolve struct literal type");
			return nullptr;
		}
		if (type->kind != ResolvedType::STRUCT) {
			errorLine(expr.token, "Expected struct type, got ", type);
			return nullptr;
		}
		if (lit.type) lit.type->resolved_type = type;
		StructResolvedType* st = static_cast<StructResolvedType*>(type);
		if (st->decl->fields.size() != lit.values.size()) {
			errorLine(expr.token, "Struct literal has ", lit.values.size(), " values, but struct type has ", st->decl->fields.size(), " fields");
			return nullptr;
		}
		for (i32 i = 0; i < lit.values.size(); ++i) {
			ResolvedType* field_type = structFieldType(*st, i);
			ResolvedType* value_type = checkExprMaterialized(unit, ctx, *lit.values[i], field_type);
			ASSERT(field_type);
			if (!value_type) return nullptr;
			if (!canImplicitlyConvert(value_type, field_type)) {
				errorLine(expr.token, "Cannot convert struct literal value ", i, " from ", value_type, " to ", field_type);
				return nullptr;
			}
		}
		expr.resolved_type = type;
		return type;
	}

	ResolvedType* checkIdentifierExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint, ResolvedType* first_arg_type = nullptr) {
		IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
		if (ctx) {
			if (SemanticLocalBinding* local = findLocal(*ctx, id.name)) {
				id.symbol = nullptr;
				id.slot = local->slot;
				expr.resolved_type = local->type;
				return expr.resolved_type;
			}
		}
		SymbolRef ref = resolveSymbol(unit, {}, id.name, LookupPolicy::Checked, first_arg_type);
		if (ref.ambiguous) {
			// TODO list collisions 
			errorLine(expr.token, "Ambiguous identifier ", id.name);
			return nullptr;
		}
		if (!ref) {
			errorLine(expr.token, "Unknown identifier ", id.name);
			return nullptr;
		}
		if (ctx && ctx->comptime_only && ref.symbol->storage != Symbol::COMPTIME) {
			errorLine(expr.token, "Cannot use non-comptime symbol ", id.name, " in comptime context");
			return nullptr;
		}
		id.symbol = ref.symbol;

		/* handle something like
			fn identity(a : $T) : T { return a; }
			fn get_identity() : fn(i32) : i32 { return identity; }
		*/
		if (hint && hint->kind == ResolvedType::FUNCTION && ref.symbol->expression && ref.symbol->expression->kind == Expression::FUNCTION) {
			FunctionExpression* fn = static_cast<FunctionExpression*>(ref.symbol->expression);
			if (fn->is_template) {
				FunctionResolvedType& target = *static_cast<FunctionResolvedType*>(hint);
				TemplateBindings bindings(ref.owner->arena);
				if (fn->params.size() != target.param_types.size()) {
					errorLine(expr.token, "Mismatched number of parameters for function template : expected ", target.param_types.size(), ", got ", fn->params.size());
					return nullptr;
				}
				for (FunctionParam& param : fn->params) {
					u32 target_param_index = u32(&param - fn->params.data());
					if (param.is_comptime) {
						errorLine(expr.token, "Cannot infer template argument for comptime parameter ", param.name);
						return nullptr;
					}
					if (!inferTemplateArg(*ref.owner, bindings, *param.type_expr, ComptimeValue(target.param_types[target_param_index]))) {
						errorLine(expr.token, "Cannot infer template argument for parameter ", param.name);
						return nullptr;
					}
				}
				if (!inferTemplateArg(*ref.owner, bindings, *fn->return_type, ComptimeValue(target.return_type))) {
					errorLine(expr.token, "Cannot infer return type for function template");
					return nullptr;
				}
				FunctionExpression* instance = instantiateFunctionTemplate(*ref.owner, *fn, bindings);
				if (!instance) return nullptr;

				FunctionResolvedType* instance_type = asFunctionType(instance->resolved_type);
				if (!(instance_type && typesEqual(instance_type, &target))) {
					errorLine(expr.token, "Function template instantiation does not match the expected function type");
					return nullptr;
				}
				id.resolved_fn = instance;
				expr.resolved_type = instance->resolved_type;
				return expr.resolved_type;
			}
		}

		if (symbolHasGlobalStorage(*ref.symbol)) id.slot = &ref.symbol->slot;
		expr.resolved_type = ref.symbol->resolved_type;
		return expr.resolved_type;
	}

	ResolvedType* checkExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint, ResolvedType* first_arg_type = nullptr) {
		switch (expr.kind) {
			case Expression::INT_LITERAL: {
				ResolvedType* int_hint = unwrapNullable(hint);
				if (int_hint && isNumericType(*int_hint)) {
					const u64 value = static_cast<IntLiteralExpression&>(expr).value;
					if (!intLiteralFitsType(value, int_hint->kind)) {
						errorLine(expr.token, "Integer literal does not fit in ", int_hint);
						return nullptr;
					}
					expr.resolved_type = int_hint;
				} else {
					// No concrete numeric target yet: stay untyped and let unification with the
					// surrounding operand (or a later materialization point) pin the width.
					expr.resolved_type = primitiveType(ResolvedType::UNTYPED_INT);
				}
				return expr.resolved_type;
			}
			case Expression::FLOAT_LITERAL: {
				ResolvedType* float_hint = unwrapNullable(hint);
				if (float_hint && float_hint->kind == ResolvedType::F32) {
					const double value = static_cast<FloatLiteralExpression&>(expr).value;
					if (value > (double)FLT_MAX || value < -(double)FLT_MAX) {
						// TODO error msg
						errorLine(expr.token, "Float literal does not fit in f32");
						return nullptr;
					}
					expr.resolved_type = float_hint;
				} else {
					expr.resolved_type = (float_hint && isFloatType(*float_hint)) ? float_hint : primitiveType(ResolvedType::UNTYPED_FLOAT);
				}
				return expr.resolved_type;
			}
			case Expression::SIZEOF: {
				SizeofExpression& sz = static_cast<SizeofExpression&>(expr);
				ComptimeValue v;
				if (!resolveSizeofValue(unit, sz, v)) return nullptr;
				
				ResolvedType* int_hint = unwrapNullable(hint);
				if (int_hint && isNumericType(*int_hint)) {
					if (!intLiteralFitsType(sz.value, int_hint->kind)) {
						errorLine(expr.token, "Constant does not fit in ", int_hint);
						return nullptr;
					}
					expr.resolved_type = int_hint;
				} else {
					expr.resolved_type = primitiveType(ResolvedType::UNTYPED_INT);
				}
				return expr.resolved_type;
			}
			case Expression::UNDEFINED: expr.resolved_type = hint; return expr.resolved_type;
			case Expression::BOOL_LITERAL: expr.resolved_type = primitiveType(ResolvedType::BOOL); return expr.resolved_type;
			case Expression::STRING_LITERAL: expr.resolved_type = primitiveType(ResolvedType::STRING); return expr.resolved_type;
			case Expression::NULL_LITERAL:
				if (!hint) {
					errorLine(expr.token, "Cannot use null literal without a type hint");
					return nullptr;
				}
				if (hint->kind != ResolvedType::NULLABLE && hint->kind != ResolvedType::SLICE && hint->kind != ResolvedType::CPTR) {
					errorLine(expr.token, "Cannot use null literal as ", hint);
					return nullptr;
				}
				expr.resolved_type = hint;
				return expr.resolved_type;
			case Expression::TYPE_LITERAL: {
				if (!ctx) {
					errorLine(expr.token, "Cannot use type literal outside of a comptime context");
					return nullptr;
				}
				if (!ctx->comptime_only) {
					errorLine(expr.token, "Cannot use type literal in a non-comptime context");
					return nullptr;
				}
				const ResolvedType::Kind kind = static_cast<TypeLiteralExpression&>(expr).type;
				if (kind == ResolvedType::META) {
					expr.resolved_type = makeType<MetaType>(unit);
					return expr.resolved_type;
				}
				ResolvedType* t = primitiveType(kind);
				expr.resolved_type = t;
				return t;
			}
			case Expression::UNION_TYPE: {
				if (!ctx) {
					errorLine(expr.token, "Cannot use union type outside of a comptime context");
					return nullptr;
				}
				if (!ctx->comptime_only) {
					errorLine(expr.token, "Cannot use union type in a non-comptime context");
					return nullptr;
				}
				ResolvedType* type = resolveTypeExpr(unit, expr);
				expr.resolved_type = type;
				return type;
			}
			case Expression::FUNCTION: {
				// stuff like const foo = fn() : i32 { return 1; };
				FunctionExpression& fn = static_cast<FunctionExpression&>(expr);
				if (fn.resolved_type) return fn.resolved_type;
				FunctionResolvedType* fn_type = buildFunctionType(unit, fn);
				if (!fn_type) return nullptr;
				
				// resolved_type must be set before checkFunctionBody (it reads the return
				// type from it), but a body checked under suppressed errors that fails must
				// not stay cached: clearing it lets a later non-suppressed check re-run the
				// body and surface the real diagnostic.
				expr.resolved_type = fn_type;
				if (!checkFunctionBody(unit, fn)) {
					expr.resolved_type = nullptr;
					return nullptr;
				}
				return fn_type;
			}
			case Expression::IDENTIFIER: return checkIdentifierExpr(unit, ctx, expr, hint, first_arg_type);
			case Expression::CALL: return checkCallExpr(unit, ctx, expr);
			case Expression::UNARY: return checkUnaryExpr(unit, ctx, expr, hint);
			case Expression::BINARY: return checkBinaryExpr(unit, ctx, expr, hint);
			case Expression::TERNARY: return checkTernaryExpr(unit, ctx, expr, hint);
			case Expression::CAST: return checkCastExpr(unit, ctx, expr);
			case Expression::MEMBER: return checkMemberExpr(unit, ctx, expr, hint);
			case Expression::BRACKET: return checkBracketExpr(unit, ctx, expr, hint);
			case Expression::SLICE: return checkSliceExpr(unit, ctx, expr);
			case Expression::STRUCT_LITERAL: return checkStructLiteralExpr(unit, ctx, expr, hint);
			default: return nullptr;
		}
	}

	ResolvedType* checkAssignableExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, bool& is_writable) {
		switch (expr.kind) {
			case Expression::IDENTIFIER: {
				IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
				if (ctx) {
					if (SemanticLocalBinding* local = findLocal(*ctx, id.name)) {
						is_writable = !local->is_immutable;
						id.slot = local->slot;
						expr.resolved_type = local->type;
						return local->type;
					}
				}
				SymbolRef ref = resolveSymbol(unit, {}, id.name, LookupPolicy::Checked);
				if (!ref) {
					errorLine(expr.token, "Unknown identifier ", id.name);
					is_writable = false;
					return nullptr;
				}
				id.symbol = ref.symbol;
				// TODO why is this here?
				if (symbolHasGlobalStorage(*ref.symbol)) id.slot = &ref.symbol->slot;
				is_writable = ref.symbol->storage == Symbol::VARIABLE;
				expr.resolved_type = unwrapMeta(ref.symbol->resolved_type);
				return expr.resolved_type;
			}
			case Expression::MEMBER: {
				MemberExpression& member = static_cast<MemberExpression&>(expr);
				if (!member.expression) {
					// TODO better error msg
					errorLine(expr.token, "Cannot assign to .", member.name, " without a base expression");
					is_writable = false;
					return nullptr;
				}
				SymbolRef ref = {};
				if (member.expression->kind == Expression::IDENTIFIER) {
					ref = resolveSymbol(unit, static_cast<IdentifierExpression&>(*member.expression).name, member.name, LookupPolicy::Checked);
				}
				if (ref) {
					is_writable = ref.symbol->storage == Symbol::VARIABLE;
					expr.resolved_type = unwrapMeta(ref.symbol->resolved_type);
					return expr.resolved_type;
				}
				bool base_writable = false;
				ResolvedType* base_type = checkAssignableExpr(unit, ctx, *member.expression, base_writable);
				if (!base_type) {
					is_writable = false;
					return nullptr;
				}
				if (!base_writable) {
					is_writable = false;
					errorLine(expr.token, "Cannot assign to member ", member.name, " of non-writable base expression");
					return nullptr;
				}
				ResolvedType* field_type = checkExpr(unit, ctx, expr, nullptr);
				is_writable = field_type != nullptr;
				expr.resolved_type = field_type;
				return field_type;
			}
			case Expression::BRACKET: {
				BracketExpression& br = static_cast<BracketExpression&>(expr);
				bool base_writable = false;
				ResolvedType* base_type = checkAssignableExpr(unit, ctx, *br.base, base_writable);
				if (!base_type) {
					is_writable = false;
					return nullptr;
				}
				// A slice is a view: writing an element mutates the viewed storage, not
				// the slice binding itself, so the binding's immutability does not apply.
				if (!base_writable && base_type->kind != ResolvedType::SLICE) {
					is_writable = false;
					errorLine(expr.token, "Cannot assign to element of non-writable base expression");
					return nullptr;
				}
				ResolvedType* value_type = checkExpr(unit, ctx, expr, nullptr);
				is_writable = value_type != nullptr;
				expr.resolved_type = value_type;
				return value_type;
			}
			default:
				errorLine(expr.token, "Expression is not assignable");
				is_writable = false;
				return nullptr;
		}
	}

	static bool isPrimitiveShadowName(ls_string_view name) {
		for (i32 kind = ResolvedType::VOID; kind <= ResolvedType::CPTR; ++kind) {
			if (equalStrings(name, makeStringView(primitiveTypeName(static_cast<ResolvedType::Kind>(kind))))) return true;
		}
		return equalStrings(name, makeStringView("type"));
	}

	bool typeExprIsDirectSelfReference(Expression& type, ls_string_view name) {
		if (type.kind == Expression::NULLABLE_TYPE) {
			return typeExprIsDirectSelfReference(*static_cast<NullableTypeExpression&>(type).inner, name);
		}
		if (type.kind == Expression::IDENTIFIER) {
			return equalStrings(static_cast<IdentifierExpression&>(type).name, name);
		}
		if (type.kind == Expression::BRACKET) {
			return typeExprIsDirectSelfReference(*static_cast<BracketExpression&>(type).base, name);
		}
		return false;
	}

	Unit* findImportedUnitByAlias(Unit& unit, ls_string_view alias) {
		for (const Import& import : unit.imports) {
			if (!equalStrings(import.alias, alias)) continue;
			ASSERT(import.unit);
			return import.unit;
		}
		return nullptr;
	}

	Unit* findTypeNamespaceUnit(const ResolvedType& type) {
		switch (type.kind) {
			case ResolvedType::STRUCT: return static_cast<const StructResolvedType&>(type).decl->cached_owner;
			case ResolvedType::ENUM: return static_cast<const EnumResolvedType&>(type).decl->cached_owner;
			default: return nullptr;
		}
	}

	// Conservative reachability check: true only if every path through `st`
	// is guaranteed to hit a `return`. Loops are never credited (the body may
	// run zero times) and `match` is only credited when it has a fallback arm
	// (exhaustiveness for enum-only matches is intentionally not special-cased
	// here to keep this analysis simple).
	static bool statementAlwaysReturns(Statement& st) {
		switch (st.kind) {
			case Statement::RETURN: return true;
			case Statement::BLOCK: return blockAlwaysReturns(static_cast<BlockStatement&>(st));
			case Statement::LABEL: return statementAlwaysReturns(*static_cast<LabelStatement&>(st).statement);
			case Statement::IF: {
				IfStatement& ifst = static_cast<IfStatement&>(st);
				if (!ifst.else_branch) return false;
				return blockAlwaysReturns(*ifst.body) && statementAlwaysReturns(*ifst.else_branch);
			}
			case Statement::MATCH: {
				MatchStatement& ms = static_cast<MatchStatement&>(st);
				bool has_fallback = false;
				for (MatchArm& arm : ms.arms) {
					if (arm.is_fallback) has_fallback = true;
					if (!blockAlwaysReturns(*arm.body)) return false;
				}
				return has_fallback;
			}
			default: return false;
		}
	}

	static bool blockAlwaysReturns(BlockStatement& block) {
		for (Statement* st : block.statements) {
			if (statementAlwaysReturns(*st)) return true;
		}
		return false;
	}

	bool checkFunctionBody(Unit& unit, FunctionExpression& fn) {
		if (!fn.body) return true;
		ASSERT(fn.body->kind == Statement::BLOCK);

		ResolvedType* return_type = static_cast<FunctionResolvedType*>(fn.resolved_type)->return_type;
		ASSERT(return_type);
		FunctionCheckContext ctx(unit.arena); // TODO reuse?
		pushScope(ctx);
		for (FunctionParam& param : fn.params) {
			if (findSymbol(unit, param.name)) {
				errorLine(fn.token, "Parameter ", param.name, " shadows a global symbol");
				return false;
			}
			SemanticLocalBinding& binding = ctx.locals.emplace_back();
			binding.name = param.name;
			binding.type = param.resolved_type;
			binding.is_immutable = !param.is_ref;
			binding.slot = &param.slot;
		}

		BlockStatement* body = static_cast<BlockStatement*>(fn.body);
		for (Statement* st : body->statements) {
			if (!checkStatement(unit, ctx, st, return_type, {})) return false;
		}

		if (return_type->kind != ResolvedType::VOID && !blockAlwaysReturns(*body)) {
			errorLine(fn.token, "Function must return a value on all code paths");
			return false;
		}
		return true;
	}

	static bool checkLabelTarget(FunctionCheckContext& ctx, ls_string_view label) {
		if (empty(label)) return !ctx.loop_labels.empty();
		for (i32 i = (i32)ctx.loop_labels.size() - 1; i >= 0; --i) {
			if (equalStrings(ctx.loop_labels[(u32)i], label)) return true;
		}
		return false;
	}

	bool checkVarDeclStatement(Unit& unit, FunctionCheckContext& ctx, VarDeclStatement& var) {
		// TODO collision with templates
		if (findLocal(ctx, var.name)) {
			errorLine(var.token, "Variable ", var.name, " shadows an existing local or parameter");
			return false;
		}

		if (findSymbol(unit, var.name)) {
			errorLine(var.token, "Variable ", var.name, " conflicts with a symbol of the same name in the same unit");
			return false;
		}

		ResolvedType* annotation = nullptr;
		if (var.type_expr) {
			annotation = resolveTypeExpr(unit, *var.type_expr);
			if (!annotation) {
				// TODO can we even get here?
				return false;
			}
		}
		// The parser always attaches an initializer (`var x = ...;`); there is no
		// uninitialized local form. Unlike global symbols, this path may dereference
		// it unconditionally.
		ASSERT(var.expression);
		if (var.expression->kind == Expression::UNDEFINED) {
			if (!annotation) {
				errorLine(var.token, "Variable ", var.name, " must have a type annotation if initialized with undefined");
				return false;
			}
			if (var.is_immutable) {
				errorLine(var.token, "Variable ", var.name, " cannot be immutable if initialized with undefined");
				return false;
			}
		}

		ResolvedType* expr_type = checkExprMaterialized(unit, &ctx, *var.expression, annotation);
		if (!expr_type) return false;
		if (annotation && !canImplicitlyConvert(expr_type, annotation)) {
			errorLine(var.token, "Cannot convert initializer expression of type ", expr_type, " to annotated type ", annotation);
			return false;
		}
		ResolvedType* final_type = annotation ? annotation : expr_type;
		var.resolved_type = final_type;

		SemanticLocalBinding& binding = ctx.locals.emplace_back();
		binding.name = var.name;
		binding.type = final_type;
		binding.is_immutable = var.is_immutable;
		binding.slot = &var.slot;
		return true;
	}

	bool checkAssignStatement(Unit& unit, FunctionCheckContext& ctx, AssignStatement& assign) {
		bool writable = false;
		ResolvedType* lhs_type = checkAssignableExpr(unit, &ctx, *assign.lhs, writable);
		if (!lhs_type) return false;
		if (!writable) {
			errorLine(assign.token, "Epression is immutable and cannot be assigned to");
			return false;
		}

		assign.lhs->resolved_type = lhs_type;

		ResolvedType* rhs_type = checkExprMaterialized(unit, &ctx, *assign.rhs, lhs_type);
		if (!rhs_type) return false;
		ResolvedType* assignment_target = lhs_type;
		if (assign.lhs->kind == Expression::IDENTIFIER) {
			IdentifierExpression* id = static_cast<IdentifierExpression*>(assign.lhs);
			if (id->slot && id->slot->type && id->slot->type->kind == ResolvedType::UNION) assignment_target = id->slot->type;
			if (id->slot) {
				for (i32 i = (i32)ctx.locals.size() - 2; i >= 0; --i) {
					SemanticLocalBinding& binding = ctx.locals[(u32)i];
					if (equalStrings(binding.name, id->name) && binding.slot == id->slot && binding.type->kind == ResolvedType::UNION) {
						assignment_target = binding.type;
						break;
					}
				}
			}
		}

		if (isNumericType(*lhs_type)) {
			if (!canImplicitlyConvert(rhs_type, lhs_type)) {
				errorLine(assign.token, "Cannot convert ", rhs_type, " to ", lhs_type, " for compound assignment");
				return false;
			}
			return true;
		}

		ResolvedType* op_result = nullptr;
		switch (assign.op) {
			case Token::EQUAL:
				if (!canImplicitlyConvert(rhs_type, assignment_target)) {
					errorLine(assign.token, "Cannot convert ", rhs_type, " to ", assignment_target, " for assignment");
					return false;
				}
				return true;
			case Token::PLUS_EQUAL:
			case Token::MINUS_EQUAL:
			case Token::STAR_EQUAL:
			case Token::SLASH_EQUAL: {
				const Token::Type base_op = assign.op == Token::PLUS_EQUAL	   ? Token::PLUS
											: assign.op == Token::MINUS_EQUAL ? Token::MINUS
											: assign.op == Token::STAR_EQUAL  ? Token::STAR
																			   : Token::SLASH;
				Expression* operands[2] = {assign.lhs, assign.rhs};
				FunctionExpression* op_fn = nullptr;
				ResolvedType* operand_types[2] = {lhs_type, rhs_type};
				StructExpression* host = lhs_type->kind == ResolvedType::STRUCT ? static_cast<StructResolvedType*>(lhs_type)->decl
					: rhs_type->kind == ResolvedType::STRUCT ? static_cast<StructResolvedType*>(rhs_type)->decl : nullptr;
				if (!host) {
					errorLine(assign.token, "No matching operator overload for compound assignment on type ", lhs_type);
					return false;
				}
				switch (resolveOperatorOverload(unit, &ctx, base_op, 2, operands, operand_types, *host, op_result, op_fn)) {
					case OverloadResult::FOUND: assign.resolved_op_fn = op_fn; break;
					case OverloadResult::AMBIGUOUS: errorLine(assign.token, "Ambiguous operator overload for compound assignment on type ", lhs_type); return false;
					case OverloadResult::FAILED: return false;
					case OverloadResult::NOT_FOUND: errorLine(assign.token, "No matching operator overload for compound assignment on type ", lhs_type); return false;
				}
				assign.lhs = operands[0];
				assign.rhs = operands[1];

				if (!canImplicitlyConvert(op_result, lhs_type)) {
					// operator +(a : Vec2, b : Vec2) : i32 { return a.x + b.x; }
					// value += Vec2 { 2 };
					errorLine(assign.token, "Compound assignment operator returns ", op_result, " which cannot be implicitly converted to the target type ", lhs_type);
					return false;
				}
				return true;
			}
			default:
			 	// parser rejects all other operators
				ASSERT(false);
				return false;
		}
	}

	bool checkIfStatement(Unit& unit, FunctionCheckContext& ctx, IfStatement& ifst, ResolvedType* return_type) {
		ResolvedType* cond = checkExpr(unit, &ctx, *ifst.condition, primitiveType(ResolvedType::BOOL));
		if (!cond) return false;
		if (!typesEqual(cond, primitiveType(ResolvedType::BOOL))) {
			errorLine(ifst.token, "If condition must be of type bool, got ", cond);
			return false;
		}

		// Detect `x != null` / `x == null` to narrow x inside the respective branch.
		ls_string_view narrowed_name = {};
		ResolvedType* narrowed_type = nullptr;
		bool narrowed_is_immutable = false;
		bool narrow_in_true = false;
		StorageSlot* narrowed_slot = nullptr;
		if (ifst.condition && ifst.condition->kind == Expression::BINARY) {
			BinaryExpression* bin = static_cast<BinaryExpression*>(ifst.condition);
			if (bin->op == Token::BANG_EQUAL || bin->op == Token::EQUAL_EQUAL) {
				Expression* id_side = nullptr;
				if (bin->rhs && bin->rhs->kind == Expression::NULL_LITERAL)
					id_side = bin->lhs;
				else if (bin->lhs && bin->lhs->kind == Expression::NULL_LITERAL)
					id_side = bin->rhs;
				if (id_side && id_side->kind == Expression::IDENTIFIER) {
					ResolvedType* id_type = id_side->resolved_type;
					if (id_type && id_type->kind == ResolvedType::NULLABLE) {
						IdentifierExpression* id = static_cast<IdentifierExpression*>(id_side);
						narrowed_name = id->name;
						narrowed_type = static_cast<NullableResolvedType*>(id_type)->inner;
						if (SemanticLocalBinding* local = findLocal(ctx, id->name)) {
							narrowed_is_immutable = local->is_immutable;
							narrowed_slot = local->slot;
						} else if (id->symbol) {
							narrowed_is_immutable = id->symbol->storage != Symbol::VARIABLE;
							narrowed_slot = id->slot;
						}
						narrow_in_true = (bin->op == Token::BANG_EQUAL);
					}
				}
			}
			else if (bin->op == Token::IS && bin->lhs && bin->lhs->kind == Expression::IDENTIFIER) {
				ResolvedType* member = bin->rhs ? unwrapMeta(bin->rhs->resolved_type) : nullptr;
				if (member) {
					IdentifierExpression* id = static_cast<IdentifierExpression*>(bin->lhs);
					narrowed_name = id->name;
					narrowed_type = member;
					if (SemanticLocalBinding* local = findLocal(ctx, id->name)) {
						narrowed_is_immutable = local->is_immutable;
						narrowed_slot = local->slot;
					} else if (id->symbol) {
						narrowed_is_immutable = id->symbol->storage != Symbol::VARIABLE;
						narrowed_slot = id->slot;
					}
					narrow_in_true = true;
				}
			}
		}

		auto checkBranchWithNarrowing = [&](Statement* branch, bool apply_narrowing) -> bool {
			if (apply_narrowing && narrowed_type) {
				pushScope(ctx);
				SemanticLocalBinding& nb = ctx.locals.emplace_back();
				nb.name = narrowed_name;
				nb.type = narrowed_type;
				nb.is_immutable = narrowed_is_immutable;
				nb.slot = narrowed_slot;
				bool ok = checkStatement(unit, ctx, branch, return_type, {});
				popScope(ctx);
				return ok;
			}
			return checkStatement(unit, ctx, branch, return_type, {});
		};

		if (!checkBranchWithNarrowing(ifst.body, narrow_in_true)) return false;
		if (!checkBranchWithNarrowing(ifst.else_branch, !narrow_in_true)) return false;

		// A terminating branch leaves only the branch where the nullable value was non-null.
		const bool continues_with_narrowed_value = narrowed_type
			&& ((!narrow_in_true && statementAlwaysReturns(*ifst.body))
				|| (narrow_in_true && ifst.else_branch && statementAlwaysReturns(*ifst.else_branch)));
		if (continues_with_narrowed_value) {
			SemanticLocalBinding& nb = ctx.locals.emplace_back();
			nb.name = narrowed_name;
			nb.type = narrowed_type;
			nb.is_immutable = narrowed_is_immutable;
			nb.slot = narrowed_slot;
		}
		return true;
	}

	bool checkForStatement(Unit& unit, FunctionCheckContext& ctx, ForStatement& fs, ResolvedType* return_type, ls_string_view pending_label) {
		// TODO collision with templates
		// Bounds are checked without a forced hint first so an untyped bound can adopt
		// the other bound's concrete type (`for i = 0..length(s)` iterates as isize).
		// Two untyped bounds default to i32.
		ResolvedType* begin_type = checkExpr(unit, &ctx, *fs.begin, nullptr);
		ResolvedType* end_type = nullptr;
		if (begin_type) {
			end_type = checkExpr(unit, &ctx, *fs.end, isUntypedNumeric(*begin_type) ? nullptr : begin_type);
		}
		if (begin_type && end_type) {
			if (begin_type->kind == ResolvedType::UNTYPED_INT) {
				begin_type = materializeUntyped(*fs.begin, isIntegerType(*end_type) ? end_type : nullptr);
			}
			if (begin_type && end_type->kind == ResolvedType::UNTYPED_INT) {
				end_type = materializeUntyped(*fs.end, isIntegerType(*begin_type) ? begin_type : nullptr);
			}
		}
		if (!begin_type || !end_type || !isIntegerType(*begin_type) || !isIntegerType(*end_type)) {
			errorLine(fs.token, "For loop bounds must be of integer type, got ", begin_type, " and ", end_type);
			return false;
		}
		if (!typesEqual(begin_type, end_type)) {
			errorLine(fs.token, "For loop bounds must have the same type, got ", begin_type, " and ", end_type);
			return false;
		}

		pushScope(ctx);
		SemanticLocalBinding& binding = ctx.locals.emplace_back();
		binding.name = fs.loop_var;
		binding.type = begin_type;
		binding.is_immutable = true;
		binding.slot = &fs.slot;
		ctx.loop_labels.push(pending_label);
		bool ok = checkStatement(unit, ctx, fs.body, return_type, {});
		ctx.loop_labels.pop_back();
		popScope(ctx);
		return ok;
	}

	bool checkLabelStatement(Unit& unit, FunctionCheckContext& ctx, LabelStatement& label, ResolvedType* return_type) {
		for (i32 i = (i32)ctx.label_names.size() - 1; i >= 0; --i) {
			if (equalStrings(ctx.label_names[(u32)i], label.name)) {
				errorLine(label.token, "Label ", label.name, " already declared in this function"); // TODO isn't this already caught below?
				return false;
			}
		}
		const bool labeled_loop = label.statement && (label.statement->kind == Statement::WHILE || label.statement->kind == Statement::FOR);
		if (labeled_loop) {
			// Active labels catch lexical duplicates. Keep a second function-wide
			// registry because reusing a name for a different loop construct is
			// ambiguous to later control-flow lowering, while sequential loops of
			// the same construct intentionally reuse labels.
			bool known_label = false;
			for (i32 i = 0; i < ctx.declared_loop_labels.size(); ++i) {
				if (!equalStrings(ctx.declared_loop_labels[i], label.name)) continue;
				if (ctx.declared_loop_kinds[i] != label.statement->kind) {
					errorLine(label.token, "Label ", label.name, " already declared for a different loop construct");
					return false;
				}
				known_label = true;
				break;
			}
			if (!known_label) {
				ctx.declared_loop_labels.push(label.name);
				ctx.declared_loop_kinds.push(label.statement->kind);
			}
		}
		ctx.label_names.push(label.name);
		const bool ok = checkStatement(unit, ctx, label.statement, return_type, labeled_loop ? label.name : ls_string_view{});
		ctx.label_names.pop_back();
		return ok;
	}

	bool checkMatchStatement(Unit& unit, FunctionCheckContext& ctx, MatchStatement& ms, ResolvedType* return_type) {
		ResolvedType* subject = checkExprMaterialized(unit, &ctx, *ms.subject, nullptr);
		if (!subject) return false;

		// Subject must be a scalar numeric type, enum, or string.
		const bool subject_is_numeric = isNumericType(*subject);
		const bool subject_is_enum = subject->kind == ResolvedType::ENUM;
		const bool subject_is_string = subject->kind == ResolvedType::STRING;
		const bool subject_is_union = subject->kind == ResolvedType::UNION;
		if (!subject_is_numeric && !subject_is_enum && !subject_is_string && !subject_is_union) {
			errorLine(ms.token, "Match statement subject must be a numeric type, enum, or string, got ", subject);
			return false;
		}

		bool has_fallback = false;
		// Track covered enum members for exhaustiveness checking.
		const EnumResolvedType* subject_enum = subject_is_enum ? static_cast<const EnumResolvedType*>(subject) : nullptr;
		ExpArray<bool> covered_enum_members(unit.arena);
		if (subject_enum) covered_enum_members.resize(subject_enum->decl->members.size(), false);
		u32 covered_enum_count = 0;
		const UnionResolvedType* subject_union = subject_is_union ? static_cast<const UnionResolvedType*>(subject) : nullptr;
		ExpArray<bool> covered_union_members(unit.arena);
		if (subject_union) covered_union_members.resize(subject_union->members.size(), false);
		u32 covered_union_count = 0;

		for (MatchArm& arm : ms.arms) {
			if (arm.is_fallback) {
				if (has_fallback) {
					// TODO error msg
					return false;
				}
				has_fallback = true;
			}
			for (MatchPattern& pattern : arm.patterns) {
				if (subject_union) {
					if (pattern.end) {
						errorLine(pattern.begin->token, "Range patterns are not valid for union matches");
						return false;
					}
					ResolvedType* member = nullptr;
					if (pattern.begin->kind == Expression::IDENTIFIER) {
						const ls_string_view name = static_cast<IdentifierExpression*>(pattern.begin)->name;
						for (ResolvedType* candidate : subject_union->members) {
							ls_string_view candidate_name = {};
							if (candidate->kind == ResolvedType::STRUCT) candidate_name = static_cast<StructResolvedType*>(candidate)->decl->cached_name;
							else if (candidate->kind == ResolvedType::ENUM) candidate_name = static_cast<EnumResolvedType*>(candidate)->decl->cached_name;
							if (!equalStrings(name, candidate_name)) continue;
							if (member && !typesEqual(member, candidate)) {
								errorLine(pattern.begin->token, "Ambiguous union member type ", name);
								return false;
							}
							member = candidate;
						}
					}
					if (!member) member = resolveTypeExpr(unit, *pattern.begin);
					if (!member) return false;
					i32 member_index = -1;
					for (i32 i = 0; i < subject_union->members.size(); ++i) {
						if (typesEqual(subject_union->members[i], member)) { member_index = i; break; }
					}
					if (member_index < 0) {
						errorLine(pattern.begin->token, "Type ", member, " is not a member of union ", subject);
						return false;
					}
					if (covered_union_members[member_index]) {
						errorLine(pattern.begin->token, "Duplicate match arm for union member ", member);
						return false;
					}
					covered_union_members[member_index] = true;
					++covered_union_count;
					MetaType* meta = makeType<MetaType>(unit);
					meta->inner = member;
					pattern.begin->resolved_type = meta;
					continue;
				}
				ResolvedType* begin = checkExpr(unit, &ctx, *pattern.begin, subject);
				if (!begin || !typesEqual(begin, subject)) return false;
				if (pattern.end) {
					// Range patterns are only valid for numeric types.
					if (!subject_is_numeric) {
						errorLine(pattern.begin->token, "Range patterns are only valid for numeric types, got ", subject);
						return false;
					}
					ResolvedType* end = checkExpr(unit, &ctx, *pattern.end, subject);
					if (!end || !typesEqual(end, subject)) return false;
				}
				// Track enum coverage and detect duplicates.
				if (subject_enum && pattern.begin && pattern.begin->kind == Expression::MEMBER) {
					MemberExpression* mem = static_cast<MemberExpression*>(pattern.begin);
					for (i32 i = 0; i < subject_enum->decl->members.size(); ++i) {
						if (!equalStrings(subject_enum->decl->members[i].name, mem->name)) continue;
						if (covered_enum_members[i]) {
							errorLine(pattern.begin->token, "Duplicate match arm for enum member ", mem->name);
							return false;
						}
						covered_enum_members[i] = true;
						++covered_enum_count;
						break;
					}
				}
			}
			ResolvedType* narrowed_union_member = nullptr;
			if (subject_union && arm.patterns.size() == 1) {
				narrowed_union_member = unwrapMeta(arm.patterns[0].begin->resolved_type);
			}
			else if (subject_union && arm.is_fallback && covered_union_count == (u32)subject_union->members.size() - 1u) {
				for (i32 i = 0; i < subject_union->members.size(); ++i) {
					if (!covered_union_members[i]) { narrowed_union_member = subject_union->members[i]; break; }
				}
			}
			if (narrowed_union_member && ms.subject->kind == Expression::IDENTIFIER) {
				IdentifierExpression* id = static_cast<IdentifierExpression*>(ms.subject);
				SemanticLocalBinding* local = findLocal(ctx, id->name);
				pushScope(ctx);
				SemanticLocalBinding& narrowed = ctx.locals.emplace_back();
				narrowed.name = id->name;
				narrowed.type = narrowed_union_member;
				if (local) {
					narrowed.is_immutable = local->is_immutable;
					narrowed.slot = local->slot;
				} else {
					narrowed.is_immutable = id->symbol && id->symbol->storage != Symbol::VARIABLE;
					narrowed.slot = id->slot;
				}
				const bool ok = checkStatement(unit, ctx, arm.body, return_type, {});
				popScope(ctx);
				if (!ok) return false;
			}
			else if (!checkStatement(unit, ctx, arm.body, return_type, {})) return false;
		}

		// Enum match must cover all variants or have a fallback.
		if (subject_enum && !has_fallback) {
			if (covered_enum_count != (u32)subject_enum->decl->members.size()) {
				errorLine(ms.token, "Match statement on enum is not exhaustive");
				return false;
			}
		}
		if (subject_union && !has_fallback && covered_union_count != (u32)subject_union->members.size()) {
			errorLine(ms.token, "Match statement on union is not exhaustive");
			return false;
		}

		return true;
	}

	bool checkStatement(Unit& unit, FunctionCheckContext& ctx, Statement* st, ResolvedType* return_type, ls_string_view pending_label) {
		if (!st) return true;

		switch (st->kind) {
			case Statement::BLOCK: {
				BlockStatement* block = static_cast<BlockStatement*>(st);
				pushScope(ctx);
				for (Statement* child : block->statements) {
					if (!checkStatement(unit, ctx, child, return_type, {})) {
						popScope(ctx);
						return false;
					}
				}
				popScope(ctx);
				return true;
			}
			case Statement::EXPRESSION: {
				ExpressionStatement* expr = static_cast<ExpressionStatement*>(st);
				if (!checkExpr(unit, &ctx, *expr->expression, nullptr)) return false;
				// A discarded expression has no context to pin its width; default it to i32.
				materializeUntyped(*expr->expression, nullptr);
				return true;
			}
			case Statement::RETURN: {
				ReturnStatement* ret = static_cast<ReturnStatement*>(st);
				if (ctx.in_defer) {
					errorLine(ret->token, "Defer statement cannot contain a return statement");
					return false;
				}
				if (!return_type) {
					// TODO can we even get here?
					return false;
				}
				if (return_type->kind == ResolvedType::VOID) {
					if (ret->expression) {
						// TODO can we even get here?
						return false;
					}
					return true;
				}
				if (!ret->expression) {
					// TODO can we even get here?
					return false;
				}
				ResolvedType* expr_type = checkExprMaterialized(unit, &ctx, *ret->expression, return_type);
				if (!expr_type) return false;
				if (!canImplicitlyConvert(expr_type, return_type)) {
					errorLine(ret->token, "Cannot convert return expression of type ", expr_type, " to function return type ", return_type);
					return false;
				}
				return true;
			}
			case Statement::WHILE: {
				WhileStatement* ws = static_cast<WhileStatement*>(st);
				ResolvedType* cond = checkExpr(unit, &ctx, *ws->condition, primitiveType(ResolvedType::BOOL));
				if (!cond || !typesEqual(cond, primitiveType(ResolvedType::BOOL))) {
					errorLine(ws->token, "While condition must be of type bool, got ", cond);
					return false;
				}
				ctx.loop_labels.push(pending_label);
				bool ok = checkStatement(unit, ctx, ws->body, return_type, {});
				ctx.loop_labels.pop_back();
				return ok;
			}
			case Statement::BREAK:
			case Statement::CONTINUE: {
				BreakStatement* br = static_cast<BreakStatement*>(st);
				ls_string_view label = st->kind == Statement::BREAK ? br->label : static_cast<ContinueStatement*>(st)->label;
				if (!checkLabelTarget(ctx, label)) {
					errorLine(br->token, "No matching loop to label ", label);
					return false;
				}
				return true;
			}
			case Statement::DEFER: {
				DeferStatement* df = static_cast<DeferStatement*>(st);
				++ctx.in_defer;
				bool ok = checkStatement(unit, ctx, df->statement, return_type, {});
				--ctx.in_defer;
				return ok;
			}
			case Statement::FOR: return checkForStatement(unit, ctx, static_cast<ForStatement&>(*st), return_type, pending_label);
			case Statement::VAR_DECL: return checkVarDeclStatement(unit, ctx, static_cast<VarDeclStatement&>(*st));
			case Statement::ASSIGN: return checkAssignStatement(unit, ctx, static_cast<AssignStatement&>(*st));
			case Statement::IF: return checkIfStatement(unit, ctx, static_cast<IfStatement&>(*st), return_type);
			case Statement::LABEL: return checkLabelStatement(unit, ctx, static_cast<LabelStatement&>(*st), return_type);
			case Statement::MATCH: return checkMatchStatement(unit, ctx, static_cast<MatchStatement&>(*st), return_type);
			default: return false;
		}
	}

	static inline const char builtin_math_source[] = R"(
		extern fn sin(v : f32) : f32;
		extern fn cos(v : f32) : f32;
		extern fn sqrt(v : f32) : f32;
		extern fn sin_f64(v : f64) : f64;
		extern fn cos_f64(v : f64) : f64;
		extern fn sqrt_f64(v : f64) : f64;
		extern fn pow(v : f32, exponent : f32) : f32;
		extern fn pow_f64(v : f64, exponent : f64) : f64;
	)";

	static inline const char builtin_mem_source[] = R"(
		extern fn alloc(size : isize, align : isize) : []byte;
		extern fn free(memory : []byte) : void;
	)";

	bool resolveImportsForUnit(Unit& unit, ls_import_resolver_fn import_resolver, void* import_resolver_userdata) {
		if (unit.import_state == Unit::IMPORT_DONE) return true;
		if (unit.import_state == Unit::IMPORT_RESOLVING) {
			errorLine({}, "Import cycle detected: ", unit.path);
			return false;
		}
		// This is the gray state of a depth-first traversal; reaching it again through
		// an import edge identifies a cycle, while IMPORT_DONE permits shared imports.
		unit.import_state = Unit::IMPORT_RESOLVING;

		// Check for duplicate aliases within this unit.
		for (i32 i = 0; i < unit.imports.size(); ++i) {
			const Import& a = unit.imports[i];
			for (i32 j = i + 1; j < unit.imports.size(); ++j) {
				const Import& b = unit.imports[j];
				if (!empty(a.alias) && equalStrings(a.alias, b.alias)) {
					errorLine({}, "Duplicate import alias: ", a.alias);
					return false;
				}
				if (equalStrings(a.path, b.path)) {
					errorLine({}, "Duplicate import: ", a.path);
					return false;
				}
			}
		}

		for (i32 i = 0; i < unit.imports.size(); ++i) {
			Import& import = unit.imports[i];
			Unit* imported = import.unit;
			if (!imported) {
				for (Unit& u : module.units) {
					if (equalStrings(u.path, import.path)) {
						imported = &u;
						break;
					}
				}
			}
			if (!imported) {
				ls_string_view source = {};
				if (equalStrings(import.path, makeStringView("std:math"))) {
					source = makeStringView(builtin_math_source);
				} else if (equalStrings(import.path, makeStringView("std:mem"))) {
					source = makeStringView(builtin_mem_source);
				} else {
					if (!import_resolver) {
						errorLine({}, "No import resolver for: ", import.path);
						return false;
					}
					if (!import_resolver(import_resolver_userdata, import.path, import.alias, &source)) {
						errorLine({}, "Import not found: ", import.path);
						return false;
					}
				}
				if (ls_module_parse(&module, source, import.path) == LS_RESULT_FAILURE) return false;
				imported = &module.units.back();
			}
			import.unit = imported;
			if (!resolveImportsForUnit(*imported, import_resolver, import_resolver_userdata)) return false;
		}

		unit.import_state = Unit::IMPORT_DONE;
		return true;
	}

	ls_result typecheck() {
		// Phase 1: resolve operator signatures and attach to host structs without checking
		// bodies. Operator bodies may call other operators, so all attachments must be
		// complete before any body is checked.
		for (Unit& unit : module.units) {
			for (Symbol& sym : unit.symbols) {
				const Token::Type op = tokenFromOperatorName(sym.name);
				if (op == Token::ERROR) continue;
				if (!sym.expression || sym.expression->kind != Expression::FUNCTION) continue;
				FunctionExpression& fn = static_cast<FunctionExpression&>(*sym.expression);
				if (fn.is_template) continue;
				FunctionResolvedType* fn_type = buildFunctionType(unit, fn);
				if (!fn_type) return LS_RESULT_FAILURE;
				for (ResolvedType* param : fn_type->param_types) {
					if (param->kind != ResolvedType::STRUCT) continue;
					static_cast<StructResolvedType*>(param)->decl->operators.push({op, &fn});
					break;
				}
			}
		}
		// Phase 2: check all symbols including operator bodies.
		for (Unit& unit : module.units) {
			for (Symbol& sym : unit.symbols) {
				if (checkSymbol(unit, sym) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
			}
		}
		// Phase 3: promote template function instances into unit.symbols so the
		// bytecode compiler can iterate a single list. ExpArray bins are stable, so
		// pointers into the array remain valid after appending new entries.
		for (Unit& unit : module.units) {
			const i32 n = unit.symbols.size();
			for (i32 i = 0; i < n; ++i) {
				Symbol& sym = unit.symbols[i];
				if (!sym.expression || sym.expression->kind != Expression::FUNCTION) continue;
				FunctionExpression& fn = static_cast<FunctionExpression&>(*sym.expression);
				if (!fn.is_template) continue;
				for (TemplateFunctionInstance& instance : fn.template_function_instances) {
					Symbol& new_sym = unit.symbols.emplace_back();
					new_sym.storage = Symbol::COMPTIME;
					new_sym.check_state = Symbol::CHECKED;
					new_sym.expression = instance.instance;
					new_sym.resolved_type = instance.type;
				}
			}
		}
		return LS_RESULT_OK;
	}

	ls_result checkSymbol(Unit& unit, Symbol& sym) {
		if (sym.check_state == Symbol::CHECKED) return LS_RESULT_OK;
		if (sym.check_state == Symbol::FAILED) return LS_RESULT_FAILURE;

		if (sym.storage == Symbol::IMPORT) {
			sym.check_state = Symbol::CHECKED;
			return LS_RESULT_OK;
		}

		if (sym.storage == Symbol::COMPTIME && isPrimitiveShadowName(sym.name)) {
			errorLine(sym.token, "Can not shadow primitive type: ", sym.name);
			sym.check_state = Symbol::FAILED;
			return LS_RESULT_FAILURE;
		}

		if (sym.check_state == Symbol::CHECKING) {
			// Recursive functions publish a provisional type while their bodies are being checked.
			// Struct declarations publish a provisional meta-type before field resolution.
			if (sym.expression && sym.resolved_type) {
				if (sym.expression->kind == Expression::FUNCTION) return LS_RESULT_OK;
				if (sym.expression->kind == Expression::STRUCT && sym.resolved_type->kind == ResolvedType::META) return LS_RESULT_OK;
			}

			errorLine(sym.token, "Cyclic definition: ", sym.name);
			sym.check_state = Symbol::FAILED;
			return LS_RESULT_FAILURE;
		}

		sym.check_state = Symbol::CHECKING;

		// Publish a function symbol's signature before any expression or body is
		// checked: identifier resolution reads sym.resolved_type directly, so
		// recursive and mutual references only resolve if the type is available up
		// front. An explicit annotation wins so e.g. nullable function globals stay
		// nullable.
		if (sym.expression && sym.expression->kind == Expression::FUNCTION) {
			FunctionExpression& fn = static_cast<FunctionExpression&>(*sym.expression);
			if (!fn.is_template) {
				// TODO what's going on on the next line?
				ResolvedType* annotation = sym.storage == Symbol::COMPTIME ? nullptr : (sym.type_expr ? resolveTypeExpr(unit, *sym.type_expr) : nullptr);
				FunctionResolvedType* fn_type = buildFunctionType(unit, fn);
				if (!fn_type) {
					sym.check_state = Symbol::FAILED;
					return LS_RESULT_FAILURE;
				}
				sym.resolved_type = annotation ? annotation : fn_type;
			}
		}

		if (sym.storage == Symbol::COMPTIME && sym.expression) { // TODO why if sym.expression
			if (checkComptimeSymbol(unit, sym) == LS_RESULT_FAILURE) {
				sym.check_state = Symbol::FAILED;
				return LS_RESULT_FAILURE;
			}
		} else {
			if (checkRuntimeSymbol(unit, sym) == LS_RESULT_FAILURE) {
				sym.check_state = Symbol::FAILED;
				return LS_RESULT_FAILURE;
			}
		}

		sym.check_state = Symbol::CHECKED;
		return LS_RESULT_OK;
	}

}; // struct Checker

ls_result ls_module_typecheck(ls_module* module) {
	if (!module) return LS_RESULT_FAILURE;
	if (Checker(*module).typecheck() == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	for (Unit& unit : module->units) {
		unit.native_symbols.clear();
		for (Symbol& sym : unit.symbols) {
			if (!sym.expression || sym.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* fn = static_cast<FunctionExpression*>(sym.expression);
			if (fn->is_extern && !fn->is_template) unit.native_symbols.push(&sym);
		}
	}
	return LS_RESULT_OK;
}

ls_result ls_module_compile(ls_module* module, ls_string_view source, ls_string_view source_name, ls_import_resolver_fn import_resolver, void* import_resolver_userdata) {
	if (!module) return LS_RESULT_FAILURE;
	Checker checker(*module);
	if (ls_module_parse(module, source, source_name) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	for (i32 unit_index = 0; unit_index < checker.module.units.size(); ++unit_index) {
		Unit& unit = checker.module.units[unit_index];
		if (!checker.resolveImportsForUnit(unit, import_resolver, import_resolver_userdata)) return LS_RESULT_FAILURE;
	}
	if (ls_module_typecheck(module) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	return LS_RESULT_OK;
}
