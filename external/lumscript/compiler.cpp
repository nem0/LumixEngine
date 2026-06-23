#include "compiler.h"
#include "utils.h"
#include <float.h>

struct Checker {

	ls_module& module;
	OutputFormatter error_stream;
	i32 suppress_errors = 0;

	Checker(ls_module& module)
		: module(module)
		, templates(*this) {
		error_stream.host = module.host;
	}

	template <typename T, typename... Args> static T* makeType(Unit& unit, Args&&... args) {
		// Semantic nodes live as long as their owning unit. Allocating them from the
		// unit arena also keeps cached types and template instances pointer-stable.
		ls_arena& arena = *unit.arena.arena;
		void* mem = arena.allocate(arena.user_data, sizeof(T), alignof(T));
		return ::new (mem) T(static_cast<Args&&>(args)...);
	}

	ResolvedType* primitiveType(ResolvedType::Kind kind) {
		ASSERT(kind >= ResolvedType::VOID && kind < ResolvedType::META);
		return &module.primitives[kind];
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
			// STRUCT/ENUM: one instance per declaration; a==b above is definitive.
			// BRACKET_TYPE: resolved away before comparison.
			default: return false;
		}
	}

	static bool canImplicitlyConvert(const ResolvedType* src, const ResolvedType* dst) {
		if (typesEqual(src, dst)) return true;
		if (!src || !dst) return false;
		// An untyped literal converts to any concrete numeric type (its width is chosen at the
		// materialization point). This is only a safety net; callers materialize first.
		if (isUntypedInt(src))   return isNumericType(dst);
		if (isUntypedFloat(src)) return isFloatType(dst);
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

	static bool isIntegerType(const ResolvedType* t) {
		if (!t) return false;
		switch (t->kind) {
			case ResolvedType::I8:
			case ResolvedType::I16:
			case ResolvedType::I32:
			case ResolvedType::I64:
			case ResolvedType::U8:
			case ResolvedType::U16:
			case ResolvedType::U32:
			case ResolvedType::U64: return true;
			default: return false;
		}
	}

	static bool isFloatType(const ResolvedType* t) {
		if (!t) return false;
		return t->kind == ResolvedType::F32 || t->kind == ResolvedType::F64;
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
		ExpArray<u32> scope_marks;
		ExpArray<ls_string_view> loop_labels;
		ExpArray<ls_string_view> label_names;
		ExpArray<ls_string_view> declared_loop_labels;
		ExpArray<Statement::Kind> declared_loop_kinds;
		bool comptime_only = false;
		i32 in_defer = 0;
	};

	struct TemplateBinding {
		ls_string_view name = {};
		ResolvedType* type = nullptr;
		bool has_value = false;
		i64 value = 0;
	};

	struct TemplateArgs {
		TemplateArgs(ls_arena& arena)
			: bindings(arena)
			, type_args(arena)
			, value_args(arena) {}

		ExpArray<TemplateBinding> bindings;
		ExpArray<ResolvedType*> type_args;
		ExpArray<i64> value_args;
	};

	struct TemplateEnvironment {
		Checker& checker;
		const ExpArray<TemplateBinding>* active_bindings = nullptr;

		struct Scope {
			TemplateEnvironment& env;
			const ExpArray<TemplateBinding>* previous;

			Scope(TemplateEnvironment& e, const ExpArray<TemplateBinding>& bindings)
				: env(e), previous(e.active_bindings) {
				e.active_bindings = &bindings;
			}

			~Scope() { env.active_bindings = previous; }
		};

		TemplateEnvironment(Checker& checker) : checker(checker) {}

		const TemplateBinding* findBinding(ls_string_view name) const {
			if (!active_bindings) return nullptr;
			for (const TemplateBinding& binding : *active_bindings) {
				if (equalStrings(binding.name, name)) return &binding;
			}
			return nullptr;
		}

		ResolvedType* findType(ls_string_view name) const {
			const TemplateBinding* b = findBinding(name);
			return b ? b->type : nullptr;
		}

		bool findIntValue(ls_string_view name, i64& out) const {
			const TemplateBinding* b = findBinding(name);
			if (!b || !b->has_value) return false;
			out = b->value;
			return true;
		}

		static bool hasValueComptimeParams(const ExpArray<NamedDecl>& params) {
			for (const NamedDecl& param : params) {
				if (param.parsed_type) return true;
			}
			return false;
		}

		ResolvedType* resolveTemplateComptimeArgType(Unit& unit, const ComptimeArg& arg) {
			if (arg.kind == ComptimeArg::TYPE) {
				return checker.resolveParsedType(unit, arg.type);
			}
			return nullptr;
		}

		bool collectTemplateArgs(Unit& unit, const ExpArray<NamedDecl>& params, const ExpArray<ComptimeArg>& args, TemplateArgs& out, bool allow_value_args) {
			if (params.size() != args.size()) return false;
			for (i32 i = 0; i < params.size(); ++i) {
				const NamedDecl& param = params[i];
				const ComptimeArg& arg = args[i];
				TemplateBinding& binding = out.bindings.emplace_back();
				binding.name = param.name;
				if (!param.parsed_type) {
					ResolvedType* type = resolveTemplateComptimeArgType(unit, arg);
					if (!type) return false;
					binding.type = type;
					out.type_args.push(type);
				}
				else {
					if (!allow_value_args) return false;
					ResolvedType* param_type = checker.resolveParsedType(unit, param.parsed_type);
					if (!param_type || param_type->kind != ResolvedType::I32) return false;
					if (arg.kind != ComptimeArg::EXPRESSION) return false;
					i64 value = 0;
					if (!checker.resolveComptimeIntValue(unit, arg.expression, value)) return false;
					binding.has_value = true;
					binding.value = value;
					out.value_args.push(value);
				}
			}
			return true;
		}

		bool collectTemplateArgs(Unit& unit, const ExpArray<NamedDecl>& params, const ExpArray<Expression*>& args, TemplateArgs& out) {
			if (params.size() != args.size()) return false;
			for (i32 i = 0; i < params.size(); ++i) {
				const NamedDecl& param = params[i];
				if (param.parsed_type) return false;
				ResolvedType* type = checker.resolveExpressionAsType(unit, *args[i]);
				if (!type) return false;
				TemplateBinding& binding = out.bindings.emplace_back();
				binding.name = param.name;
				binding.type = type;
				out.type_args.push(type);
			}
			return true;
		}

		bool typeArgListsEqual(const ExpArray<ResolvedType*>& a, const ExpArray<ResolvedType*>& b) {
			if (a.size() != b.size()) return false;
			for (i32 i = 0; i < a.size(); ++i) {
				if (!checker.typesEqual(a[i], b[i])) return false;
			}
			return true;
		}

		bool valueArgListsEqual(const ExpArray<i64>& a, const ExpArray<i64>& b) {
			if (a.size() != b.size()) return false;
			for (i32 i = 0; i < a.size(); ++i) {
				if (a[i] != b[i]) return false;
			}
			return true;
		}

		StructResolvedType* instantiateStructTemplateWithArgs(Unit& unit, StructExpression& st, ExpArray<TemplateBinding>& bindings, ExpArray<ResolvedType*>& type_args, ExpArray<i64>& value_args) {
			for (StructResolvedType* instance : unit.template_struct_instances) {
				if (instance->decl == &st && typeArgListsEqual(instance->type_args, type_args) && valueArgListsEqual(instance->value_args, value_args)) return instance;
			}

			StructResolvedType* st_type = checker.makeType<StructResolvedType>(unit, *unit.arena.arena);
			st_type->decl = &st;
			for (ResolvedType* arg : type_args) st_type->type_args.push(arg);
			for (i64 arg : value_args) st_type->value_args.push(arg);
			// Publish the half-built instance before resolving fields so `Node[T]`
			// inside `Node[T]` fields resolves to this exact specialization. That lets
			// the direct by-value recursion check catch the cycle instead of allocating
			// an endless chain of equivalent instances.
			unit.template_struct_instances.push(st_type);

			bool ok = true;
			{
				Scope scope(*this, bindings);
				for (NamedDecl& field : st.fields) {
					ResolvedType* field_type = checker.resolveParsedType(unit, field.parsed_type);
					if (!field_type || field_type == st_type) { ok = false; break; }
					st_type->field_types.push(field_type);
				}
			}
			if (!ok) {
				unit.template_struct_instances.pop_back();
				return nullptr;
			}

			return st_type;
		}

		StructResolvedType* instantiateStructTemplate(Unit& unit, StructExpression& st, const ExpArray<ComptimeArg>& args) {
			TemplateArgs template_args(*unit.arena.arena);
			if (!collectTemplateArgs(unit, st.comptime_params, args, template_args, true)) return nullptr;
			return instantiateStructTemplateWithArgs(unit, st, template_args.bindings, template_args.type_args, template_args.value_args);
		}

		StructResolvedType* instantiateStructTemplate(Unit& unit, StructExpression& st, const ExpArray<Expression*>& args) {
			TemplateArgs template_args(*unit.arena.arena);
			if (!collectTemplateArgs(unit, st.comptime_params, args, template_args)) return nullptr;
			return instantiateStructTemplateWithArgs(unit, st, template_args.bindings, template_args.type_args, template_args.value_args);
		}

		FunctionExpression* cloneFunctionInstance(Unit& unit, FunctionExpression& source) {
			FunctionExpression* fn = checker.makeType<FunctionExpression>(unit, *unit.arena.arena);
			fn->token = source.token;
			fn->return_type = checker.cloneParsedType(unit, source.return_type);
			fn->body = checker.cloneStatement(unit, source.body);
			fn->is_extern = source.is_extern;
			for (FunctionParam& src_param : source.runtime_params) {
				FunctionParam& dst = fn->runtime_params.emplace_back();
				dst.name = src_param.name;
				dst.is_ref = src_param.is_ref;
				dst.parsed_type = checker.cloneParsedType(unit, src_param.parsed_type);
			}
			return fn;
		}

		FunctionResolvedType* buildFunctionTypeWithBindings(Unit& unit, FunctionExpression& fn, const ExpArray<TemplateBinding>& bindings, bool reject_nullable_refs) {
			Scope scope(*this, bindings);
			return checker.buildFunctionType(unit, fn, reject_nullable_refs);
		}

		FunctionExpression* instantiateFunctionTemplateWithArgs(Unit& unit, Symbol& sym, FunctionExpression& source, ExpArray<TemplateBinding>& bindings, ExpArray<ResolvedType*>& type_args) {
			for (TemplateFunctionInstance& instance : unit.template_function_instances) {
				if (instance.source == &source && typeArgListsEqual(instance.type->type_args, type_args)) return instance.instance;
			}

			FunctionExpression* fn = cloneFunctionInstance(unit, source);
			FunctionResolvedType* fn_type = buildFunctionTypeWithBindings(unit, *fn, bindings, true);
			if (!fn_type) return nullptr;
			for (ResolvedType* arg : type_args) fn_type->type_args.push(arg);
			fn->resolved_type = fn_type;
			TemplateFunctionInstance& instance = unit.template_function_instances.emplace_back();
			instance.name = sym.name;
			instance.source_symbol = &sym;
			instance.source = &source;
			instance.instance = fn;
			instance.type = fn_type;
			if (fn->body) {
				Scope scope(*this, bindings);
				if (!checker.checkFunctionBody(unit, *fn)) {
					unit.template_function_instances.pop_back();
					return nullptr;
				}
			}
			return fn;
		}

		FunctionExpression* instantiateFunctionTemplate(Unit& unit, Symbol& sym, FunctionExpression& source, const ExpArray<ComptimeArg>& args) {
			if (hasValueComptimeParams(source.comptime_params)) return nullptr;
			TemplateArgs template_args(*unit.arena.arena);
			if (!collectTemplateArgs(unit, source.comptime_params, args, template_args, false)) return nullptr;
			return instantiateFunctionTemplateWithArgs(unit, sym, source, template_args.bindings, template_args.type_args);
		}

		FunctionExpression* instantiateFunctionTemplate(Unit& unit, Symbol& sym, FunctionExpression& source, const ExpArray<Expression*>& args) {
			if (hasValueComptimeParams(source.comptime_params)) return nullptr;
			TemplateArgs template_args(*unit.arena.arena);
			if (!collectTemplateArgs(unit, source.comptime_params, args, template_args)) return nullptr;
			return instantiateFunctionTemplateWithArgs(unit, sym, source, template_args.bindings, template_args.type_args);
		}

		bool isTemplateParam(const ExpArray<NamedDecl>& params, ls_string_view name) {
			for (const NamedDecl& param : params) {
				if (equalStrings(param.name, name) && !param.parsed_type) return true;
			}
			return false;
		}

		bool bindInferredType(ExpArray<TemplateBinding>& bindings, ls_string_view name, ResolvedType* type) {
			if (!type) return false;
			for (TemplateBinding& binding : bindings) {
				if (!equalStrings(binding.name, name)) continue;
				return checker.typesEqual(binding.type, type);
			}
			TemplateBinding& binding = bindings.emplace_back();
			binding.name = name;
			binding.type = type;
			return true;
		}

		bool inferTemplateType(Unit& unit, const ExpArray<NamedDecl>& params, ParsedType* pattern, ResolvedType* actual, ExpArray<TemplateBinding>& bindings) {
			if (!pattern || !actual) return false;
			if (pattern->is_nullable) {
				if (actual->kind != ResolvedType::NULLABLE) return false;
				actual = static_cast<NullableResolvedType*>(actual)->inner;
			}
			switch (pattern->kind) {
				case ParsedType::QUALIFIED: {
					QualifiedParsedType* q = static_cast<QualifiedParsedType*>(pattern);
					if (empty(q->qualifier) && isTemplateParam(params, q->name)) return bindInferredType(bindings, q->name, actual);
					ResolvedType* resolved = checker.resolveParsedType(unit, pattern);
					return resolved && checker.typesEqual(resolved, actual);
				}
				case ParsedType::ARRAY: {
					if (actual->kind != ResolvedType::ARRAY) return false;
					ArrayParsedType* p = static_cast<ArrayParsedType*>(pattern);
					ArrayResolvedType* a = static_cast<ArrayResolvedType*>(actual);
					i64 size = 0;
					if (!checker.resolveComptimeIntValue(unit, p->size, size) || size != a->size) return false;
					return inferTemplateType(unit, params, p->element_type, a->element_type, bindings);
				}
				case ParsedType::SLICE: {
					if (actual->kind != ResolvedType::SLICE) return false;
					return inferTemplateType(unit, params, static_cast<SliceParsedType*>(pattern)->element_type, static_cast<SliceResolvedType*>(actual)->element_type, bindings);
				}
				case ParsedType::BRACKET_TYPE: {
					if (actual->kind != ResolvedType::STRUCT) return false;
					BracketTypeParsedType* br = static_cast<BracketTypeParsedType*>(pattern);
					ResolvedType* callee = checker.resolveParsedType(unit, br->callee);
					if (!callee || callee->kind != ResolvedType::STRUCT) return false;
					StructResolvedType* pattern_base = static_cast<StructResolvedType*>(callee);
					StructResolvedType* actual_struct = static_cast<StructResolvedType*>(actual);
					if (pattern_base->decl != actual_struct->decl) return false;
					if (br->args.size() != actual_struct->type_args.size()) return false;
					for (i32 i = 0; i < br->args.size(); ++i) {
						const ComptimeArg& arg = br->args[i];
						if (arg.kind != ComptimeArg::TYPE) return false;
						if (!inferTemplateType(unit, params, arg.type, actual_struct->type_args[i], bindings)) return false;
					}
					return true;
				}
				default: {
					ResolvedType* resolved = checker.resolveParsedType(unit, pattern);
					return resolved && checker.typesEqual(resolved, actual);
				}
			}
		}

		FunctionExpression* inferAndInstantiateFunctionTemplate(Unit& decl_unit, Unit& call_unit, FunctionCheckContext* ctx, Symbol& sym, FunctionExpression& source, CallExpression& call, ResolvedType* hint) {
			if (hasValueComptimeParams(source.comptime_params)) return nullptr;
			ExpArray<TemplateBinding> bindings(*decl_unit.arena.arena);
			ResolvedType* return_hint = hint && hint->kind == ResolvedType::NULLABLE ? static_cast<NullableResolvedType*>(hint)->inner : hint;
			if (!call.args.empty() && return_hint && !inferTemplateType(decl_unit, source.comptime_params, source.return_type, return_hint, bindings)) {
				return nullptr;
			}
			if (source.runtime_params.size() != call.args.size()) return nullptr;
			Scope scope(*this, bindings);
			for (i32 i = 0; i < source.runtime_params.size(); ++i) {
				FunctionParam& param = source.runtime_params[i];
				ResolvedType* param_hint = checker.resolveParsedType(decl_unit, param.parsed_type);
				ResolvedType* arg_type = nullptr;
				if (param.is_ref) {
					Expression* arg = call.args[i];
					if (!arg || arg->kind != Expression::UNARY || static_cast<UnaryExpression*>(arg)->op != Token::REF) return nullptr;
					bool writable = false;
					arg_type = checker.checkAssignableExpr(call_unit, ctx, static_cast<UnaryExpression*>(arg)->expression, writable);
					if (!writable) return nullptr;
				}
				else {
					// Imported templates are inferred against the declaration's signature,
					// but their arguments live in the caller's scope. Keeping those units
					// separate avoids resolving caller locals/imports as if they belonged
					// to the library that declared the template.
					arg_type = checker.checkExpr(call_unit, ctx, *call.args[i], param_hint);
				}
				if (!arg_type) return nullptr;
				if (checker.isUntypedNumeric(arg_type)) {
					arg_type = checker.materializeUntyped(*call.args[i], param_hint);
					if (!arg_type) return nullptr;
				}
				if (!inferTemplateType(decl_unit, source.comptime_params, param.parsed_type, arg_type, bindings)) return nullptr;
			}
			ExpArray<ResolvedType*> type_args(*decl_unit.arena.arena);
			for (const NamedDecl& param : source.comptime_params) {
				ResolvedType* type = nullptr;
				for (TemplateBinding& binding : bindings) {
					if (!equalStrings(binding.name, param.name)) continue;
					type = binding.type;
					break;
				}
				if (!type) return nullptr;
				type_args.push(type);
			}
			return instantiateFunctionTemplateWithArgs(decl_unit, sym, source, bindings, type_args);
		}
	};

	TemplateEnvironment templates;

	ResolvedType* findTemplateType(ls_string_view name) const { return templates.findType(name); }
	bool findTemplateIntValue(ls_string_view name, i64& out) const { return templates.findIntValue(name, out); }

	// TODO
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

	ls_string_view findTypeName(const ResolvedType& type) {
		for (const Unit& candidate_unit : module.units) {
			for (const Symbol& symbol : candidate_unit.symbols) {
				if (unwrapMeta(symbol.resolved_type) == &type) return symbol.name;
				if (!symbol.expression) continue;
				if (type.kind == ResolvedType::STRUCT && symbol.expression->kind == Expression::STRUCT && static_cast<const StructResolvedType&>(type).decl == symbol.expression) {
					return symbol.name;
				}
				if (type.kind == ResolvedType::ENUM && symbol.expression->kind == Expression::ENUM && static_cast<const EnumResolvedType&>(type).decl == symbol.expression) {
					return symbol.name;
				}
			}
		}
		return {};
	}

	void error(ResolvedType* type) { error(static_cast<const ResolvedType*>(type)); }

	void error(const ResolvedType* type) {
		if (!type) {
			error("<unresolved>");
			return;
		}
		switch (type->kind) {
			case ResolvedType::VOID: error("void"); return;
			case ResolvedType::BOOL: error("bool"); return;
			case ResolvedType::I8: error("i8"); return;
			case ResolvedType::I16: error("i16"); return;
			case ResolvedType::I32: error("i32"); return;
			case ResolvedType::I64: error("i64"); return;
			case ResolvedType::U8: error("u8"); return;
			case ResolvedType::U16: error("u16"); return;
			case ResolvedType::U32: error("u32"); return;
			case ResolvedType::U64: error("u64"); return;
			case ResolvedType::F32: error("f32"); return;
			case ResolvedType::F64: error("f64"); return;
			case ResolvedType::STRING: error("string"); return;
			case ResolvedType::CPTR: error("cptr"); return;
			case ResolvedType::UNTYPED_INT:   error("{integer}"); return;
			case ResolvedType::UNTYPED_FLOAT: error("{float}"); return;
			case ResolvedType::META: error("type"); return;
			case ResolvedType::ENUM:
			case ResolvedType::STRUCT: {
				ls_string_view name = findTypeName(*type);
				error(empty(name) ? makeStringView("<anonymous>") : name);
				if (type->kind != ResolvedType::STRUCT) return;
				const StructResolvedType* st = static_cast<const StructResolvedType*>(type);
				if (st->type_args.empty() && st->value_args.empty()) return;
				error("[");
				for (u32 i = 0; i < st->type_args.size(); ++i) {
					if (i > 0) error(", ");
					if (st->type_args[i])
						error(st->type_args[i]);
					else
						error(st->value_args[i]);
				}
				error("]");
				return;
			}
			case ResolvedType::FUNCTION: {
				const FunctionResolvedType* fn = static_cast<const FunctionResolvedType*>(type);
				error("fn(");
				for (u32 i = 0; i < fn->param_types.size(); ++i) {
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
			default: error("<invalid>"); return;
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

	bool resolveComptimeIntValue(Unit& unit, Expression* expr, i64& out) {
		if (!expr) return false;
		switch (expr->kind) {
			case Expression::INT_LITERAL: {
				const u64 value = static_cast<IntLiteralExpression*>(expr)->value;
				if (value > 9223372036854775807ull) return false;
				out = (i64)value;
				return true;
			}
			case Expression::IDENTIFIER: {
				IdentifierExpression* id = static_cast<IdentifierExpression*>(expr);
				if (findTemplateIntValue(id->name, out)) return true;
				SymbolRef ref = resolveSymbol(unit, {}, id->name, LookupPolicy::NameOnly);
				if (!ref.symbol || ref.symbol->storage != Symbol::COMPTIME) return false;
				if (checkSymbol(*ref.owner, *ref.symbol) == LS_RESULT_FAILURE) return false;
				return resolveComptimeIntValue(unit, ref.symbol->expression, out);
			}
			case Expression::UNARY: {
				UnaryExpression* un = static_cast<UnaryExpression*>(expr);
				if (un->op != Token::MINUS) return false;
				if (!resolveComptimeIntValue(unit, un->expression, out)) return false;
				out = -out;
				return true;
			}
			default: return false;
		}
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

	ParsedType* cloneParsedType(Unit& unit, ParsedType* src) {
		if (!src) return nullptr;
		ParsedType* out = nullptr;
		switch (src->kind) {
			case ParsedType::QUALIFIED: {
				QualifiedParsedType* s = static_cast<QualifiedParsedType*>(src);
				QualifiedParsedType* q = makeType<QualifiedParsedType>(unit);
				q->qualifier = s->qualifier;
				q->name = s->name;
				out = q;
				break;
			}
			case ParsedType::FUNCTION: {
				FunctionParsedType* s = static_cast<FunctionParsedType*>(src);
				FunctionParsedType* fn = makeType<FunctionParsedType>(unit, *unit.arena.arena);
				for (ParsedType* param : s->params) fn->params.push(cloneParsedType(unit, param));
				fn->return_type = cloneParsedType(unit, s->return_type);
				out = fn;
				break;
			}
			case ParsedType::ARRAY: {
				ArrayParsedType* s = static_cast<ArrayParsedType*>(src);
				ArrayParsedType* arr = makeType<ArrayParsedType>(unit);
				arr->element_type = cloneParsedType(unit, s->element_type);
				arr->size = s->size;
				out = arr;
				break;
			}
			case ParsedType::SLICE: {
				SliceParsedType* s = static_cast<SliceParsedType*>(src);
				SliceParsedType* sl = makeType<SliceParsedType>(unit);
				sl->element_type = cloneParsedType(unit, s->element_type);
				out = sl;
				break;
			}
			case ParsedType::BRACKET_TYPE: {
				BracketTypeParsedType* s = static_cast<BracketTypeParsedType*>(src);
				BracketTypeParsedType* br = makeType<BracketTypeParsedType>(unit, *unit.arena.arena);
				br->callee = cloneParsedType(unit, s->callee);
				for (const ComptimeArg& arg : s->args) {
					ComptimeArg& dst = br->args.emplace_back();
					dst.kind = arg.kind;
					dst.type = cloneParsedType(unit, arg.type);
					dst.expression = arg.expression;
				}
				out = br;
				break;
			}
			default:
				out = makeType<ParsedType>(unit, src->kind);
				break;
		}
		out->is_nullable = src->is_nullable;
		out->token = src->token;
		return out;
	}

	Expression* cloneExpression(Unit& unit, Expression* src) {
		if (!src) return nullptr;
		Expression* out = nullptr;
		switch (src->kind) {
			case Expression::IDENTIFIER: {
				IdentifierExpression* s = static_cast<IdentifierExpression*>(src);
				IdentifierExpression* id = makeType<IdentifierExpression>(unit);
				id->name = s->name;
				out = id;
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
			case Expression::BOOL_LITERAL:
				out = makeType<BoolLiteralExpression>(unit, static_cast<BoolLiteralExpression*>(src)->value);
				break;
			case Expression::STRING_LITERAL: {
				StringLiteralExpression* s = static_cast<StringLiteralExpression*>(src);
				StringLiteralExpression* lit = makeType<StringLiteralExpression>(unit);
				lit->value = s->value;
				out = lit;
				break;
			}
			case Expression::NULL_LITERAL: out = makeType<NullLiteralExpression>(unit); break;
			case Expression::UNDEFINED: out = makeType<UndefinedExpression>(unit); break;
			case Expression::TYPE_LITERAL:
				out = makeType<TypeLiteralExpression>(unit, static_cast<TypeLiteralExpression*>(src)->type);
				break;
			case Expression::CALL: {
				CallExpression* s = static_cast<CallExpression*>(src);
				CallExpression* call = makeType<CallExpression>(unit, *unit.arena.arena);
				call->callee = cloneExpression(unit, s->callee);
				for (Expression* arg : s->args) call->args.push(cloneExpression(unit, arg));
				out = call;
				break;
			}
			case Expression::UNARY: {
				UnaryExpression* s = static_cast<UnaryExpression*>(src);
				UnaryExpression* un = makeType<UnaryExpression>(unit);
				un->op = s->op;
				un->expression = cloneExpression(unit, s->expression);
				out = un;
				break;
			}
			case Expression::BINARY: {
				BinaryExpression* s = static_cast<BinaryExpression*>(src);
				BinaryExpression* bin = makeType<BinaryExpression>(unit);
				bin->op = s->op;
				bin->lhs = cloneExpression(unit, s->lhs);
				bin->rhs = cloneExpression(unit, s->rhs);
				out = bin;
				break;
			}
			case Expression::CAST: {
				CastExpression* s = static_cast<CastExpression*>(src);
				CastExpression* cast = makeType<CastExpression>(unit);
				cast->expression = cloneExpression(unit, s->expression);
				cast->parsed_type = cloneParsedType(unit, s->parsed_type);
				out = cast;
				break;
			}
			case Expression::MEMBER: {
				MemberExpression* s = static_cast<MemberExpression*>(src);
				MemberExpression* mem = makeType<MemberExpression>(unit);
				mem->expression = cloneExpression(unit, s->expression);
				mem->name = s->name;
				out = mem;
				break;
			}
			case Expression::BRACKET: {
				BracketExpression* s = static_cast<BracketExpression*>(src);
				BracketExpression* br = makeType<BracketExpression>(unit, *unit.arena.arena);
				br->base = cloneExpression(unit, s->base);
				for (Expression* arg : s->args) br->args.push(cloneExpression(unit, arg));
				br->has_colon = s->has_colon;
				br->end = cloneExpression(unit, s->end);
				out = br;
				break;
			}
			case Expression::STRUCT_LITERAL: {
				StructLiteralExpression* s = static_cast<StructLiteralExpression*>(src);
				StructLiteralExpression* lit = makeType<StructLiteralExpression>(unit, *unit.arena.arena);
				lit->type = cloneExpression(unit, s->type);
				for (Expression* value : s->values) lit->values.push(cloneExpression(unit, value));
				out = lit;
				break;
			}
			default:
				out = makeType<Expression>(unit, src->kind);
				break;
		}
		out->token = src->token;
		return out;
	}

	Statement* cloneStatement(Unit& unit, Statement* src) {
		if (!src) return nullptr;
		Statement* out = nullptr;
		switch (src->kind) {
			case Statement::BLOCK: {
				BlockStatement* s = static_cast<BlockStatement*>(src);
				BlockStatement* block = makeType<BlockStatement>(unit, *unit.arena.arena);
				for (Statement* st : s->statements) block->statements.push(cloneStatement(unit, st));
				out = block;
				break;
			}
			case Statement::EXPRESSION: {
				ExpressionStatement* s = static_cast<ExpressionStatement*>(src);
				ExpressionStatement* st = makeType<ExpressionStatement>(unit);
				st->expression = cloneExpression(unit, s->expression);
				out = st;
				break;
			}
			case Statement::RETURN: {
				ReturnStatement* s = static_cast<ReturnStatement*>(src);
				ReturnStatement* st = makeType<ReturnStatement>(unit);
				st->expression = cloneExpression(unit, s->expression);
				out = st;
				break;
			}
			case Statement::VAR_DECL: {
				VarDeclStatement* s = static_cast<VarDeclStatement*>(src);
				VarDeclStatement* st = makeType<VarDeclStatement>(unit);
				st->name = s->name;
				st->parsed_type = cloneParsedType(unit, s->parsed_type);
				st->expression = cloneExpression(unit, s->expression);
				st->is_immutable = s->is_immutable;
				out = st;
				break;
			}
			case Statement::ASSIGN: {
				AssignStatement* s = static_cast<AssignStatement*>(src);
				AssignStatement* st = makeType<AssignStatement>(unit);
				st->lhs = cloneExpression(unit, s->lhs);
				st->rhs = cloneExpression(unit, s->rhs);
				st->op = s->op;
				out = st;
				break;
			}
			case Statement::IF: {
				IfStatement* s = static_cast<IfStatement*>(src);
				IfStatement* st = makeType<IfStatement>(unit);
				st->condition = cloneExpression(unit, s->condition);
				st->body = static_cast<BlockStatement*>(cloneStatement(unit, s->body));
				st->else_branch = cloneStatement(unit, s->else_branch);
				out = st;
				break;
			}
			default:
				return src;
		}
		out->token = src->token;
		return out;
	}

	ResolvedType* resolveParsedType(Unit& unit, ParsedType* parsed) {
		if (!parsed) return nullptr;
		ResolvedType* result = nullptr;
		switch (parsed->kind) {
			case ParsedType::VOID: result = primitiveType(ResolvedType::VOID); break;
			case ParsedType::BOOL: result = primitiveType(ResolvedType::BOOL); break;
			case ParsedType::I8: result = primitiveType(ResolvedType::I8); break;
			case ParsedType::I16: result = primitiveType(ResolvedType::I16); break;
			case ParsedType::I32: result = primitiveType(ResolvedType::I32); break;
			case ParsedType::I64: result = primitiveType(ResolvedType::I64); break;
			case ParsedType::U8: result = primitiveType(ResolvedType::U8); break;
			case ParsedType::U16: result = primitiveType(ResolvedType::U16); break;
			case ParsedType::U32: result = primitiveType(ResolvedType::U32); break;
			case ParsedType::U64: result = primitiveType(ResolvedType::U64); break;
			case ParsedType::F32: result = primitiveType(ResolvedType::F32); break;
			case ParsedType::F64: result = primitiveType(ResolvedType::F64); break;
			case ParsedType::STRING: result = primitiveType(ResolvedType::STRING); break;
			case ParsedType::CPTR: result = primitiveType(ResolvedType::CPTR); break;
			case ParsedType::TYPE: {
				result = makeType<MetaType>(unit);
				break;
			}
			case ParsedType::FUNCTION: {
				FunctionParsedType* fn = static_cast<FunctionParsedType*>(parsed);
				FunctionResolvedType* resolved = makeType<FunctionResolvedType>(unit, *unit.arena.arena);
				for (ParsedType* param : fn->params) {
					ResolvedType* pt = resolveParsedType(unit, param);
					if (!pt) return nullptr;
					resolved->param_types.push(pt);
				}
				resolved->return_type = resolveParsedType(unit, fn->return_type);
				result = resolved;
				break;
			}
			case ParsedType::ARRAY: {
				ArrayParsedType* arr = static_cast<ArrayParsedType*>(parsed);
				ArrayResolvedType* resolved = makeType<ArrayResolvedType>(unit);
				resolved->element_type = resolveParsedType(unit, arr->element_type);
				i64 size = 0;
				if (!resolved->element_type || !resolveComptimeIntValue(unit, arr->size, size)) return nullptr;
				if (size <= 0) return nullptr;
				resolved->size = size;
				result = resolved;
				break;
			}
			case ParsedType::SLICE: {
				SliceParsedType* sl = static_cast<SliceParsedType*>(parsed);
				SliceResolvedType* resolved = makeType<SliceResolvedType>(unit);
				resolved->element_type = resolveParsedType(unit, sl->element_type);
				result = resolved;
				break;
			}
			case ParsedType::QUALIFIED: {
				QualifiedParsedType* q = static_cast<QualifiedParsedType*>(parsed);
				if (empty(q->qualifier)) {
					if (ResolvedType* template_type = findTemplateType(q->name)) {
						result = template_type;
						break;
					}
				}
				SymbolRef ref = resolveSymbol(unit, q->qualifier, q->name, LookupPolicy::Checked);
				result = ref ? unwrapMeta(ref.symbol->resolved_type) : nullptr;
				break;
			}
			case ParsedType::BRACKET_TYPE: {
				BracketTypeParsedType* call = static_cast<BracketTypeParsedType*>(parsed);
				ResolvedType* callee = resolveParsedType(unit, call->callee);
				if (!callee) return nullptr;
				if (callee->kind == ResolvedType::STRUCT) {
					StructResolvedType* st = static_cast<StructResolvedType*>(callee);
					if (!st->decl || st->decl->comptime_params.empty()) return nullptr;
					result = templates.instantiateStructTemplate(unit, *st->decl, call->args);
					break;
				}
				if (call->args.size() == 1 && call->args[0].kind == ComptimeArg::EXPRESSION) {
					ArrayResolvedType* resolved = makeType<ArrayResolvedType>(unit);
					resolved->element_type = callee;
					i64 size = 0;
					if (!resolveComptimeIntValue(unit, call->args[0].expression, size)) return nullptr;
					if (size <= 0) return nullptr;
					resolved->size = size;
					result = resolved;
					break;
				}
				result = nullptr;
				break;
			}
			default: return nullptr;
		}
		if (result && parsed->is_nullable) {
			NullableResolvedType* nullable = makeType<NullableResolvedType>(unit);
			nullable->inner = result;
			result = nullable;
		}
		return result;
	}

	static bool isNumericType(const ResolvedType* type) {
		if (!type) return false;
		switch (type->kind) {
			case ResolvedType::I8:
			case ResolvedType::I16:
			case ResolvedType::I32:
			case ResolvedType::I64:
			case ResolvedType::U8:
			case ResolvedType::U16:
			case ResolvedType::U32:
			case ResolvedType::U64:
			case ResolvedType::F32:
			case ResolvedType::F64: return true;
			default: return false;
		}
	}

	// `isNumericType` and `isIntegerType` deliberately report concrete types only, so the
	// existing hint/conversion logic keeps treating UNTYPED_INT as "not pinned yet". The
	// operator code that wants to accept an untyped operand uses these *OrUntyped helpers.
	static bool isUntypedInt(const ResolvedType* t)   { return t && t->kind == ResolvedType::UNTYPED_INT; }
	static bool isUntypedFloat(const ResolvedType* t) { return t && t->kind == ResolvedType::UNTYPED_FLOAT; }
	static bool isUntypedNumeric(const ResolvedType* t) { return isUntypedInt(t) || isUntypedFloat(t); }
	static bool isNumericOrUntyped(const ResolvedType* t) { return isNumericType(t) || isUntypedNumeric(t); }
	static bool isIntegerOrUntyped(const ResolvedType* t) { return isIntegerType(t) || isUntypedInt(t); }

	ResolvedType* untypedInt()   { return primitiveType(ResolvedType::UNTYPED_INT); }
	ResolvedType* untypedFloat() { return primitiveType(ResolvedType::UNTYPED_FLOAT); }

	// The common numeric type two operands must share, or null if they are not numerically
	// compatible. UNTYPED_INT adopts any concrete numeric partner; two untyped ints stay
	// untyped (resolved to a default later). Callers materialize the operands afterwards.
	ResolvedType* unifyNumeric(ResolvedType* a, ResolvedType* b) {
		if (!a || !b) return nullptr;
		const bool ui_a = isUntypedInt(a),   ui_b = isUntypedInt(b);
		const bool uf_a = isUntypedFloat(a), uf_b = isUntypedFloat(b);
		// Two untyped operands: float wins over int (1 + 1.5 → untyped float).
		if ((ui_a || uf_a) && (ui_b || uf_b)) return (uf_a || uf_b) ? untypedFloat() : untypedInt();
		// One untyped, one concrete: untyped adopts the concrete type if compatible.
		if (ui_a) return isNumericType(b) ? b : nullptr;
		if (ui_b) return isNumericType(a) ? a : nullptr;
		if (uf_a) return isFloatType(b)   ? b : nullptr;
		if (uf_b) return isFloatType(a)   ? a : nullptr;
		return (isNumericType(a) && typesEqual(a, b)) ? a : nullptr;
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
			case ResolvedType::U64: return false;
			case ResolvedType::F32: return intLiteralFitsType(magnitude, ResolvedType::F32);
			case ResolvedType::F64: return intLiteralFitsType(magnitude, ResolvedType::F64);
			default: return false;
		}
	}

	static bool isPrimitiveValueType(const ResolvedType* type) {
		if (!type) return false;
		switch (type->kind) {
			case ResolvedType::VOID:
			case ResolvedType::BOOL:
			case ResolvedType::I8:
			case ResolvedType::I16:
			case ResolvedType::I32:
			case ResolvedType::I64:
			case ResolvedType::U8:
			case ResolvedType::U16:
			case ResolvedType::U32:
			case ResolvedType::U64:
			case ResolvedType::F32:
			case ResolvedType::F64:
			case ResolvedType::STRING:
			case ResolvedType::CPTR: return true;
			default: return false;
		}
	}

	static bool isOverloadableBinaryOperator(Token::Type op) { return operatorSymbolName(op) != nullptr; }

	FunctionResolvedType* buildFunctionType(Unit& unit, FunctionExpression& fn, bool reject_nullable_refs) {
		// The AST owns the canonical signature. Besides avoiding duplicate arena
		// allocations, publishing it here gives recursive checking a stable identity.
		if (fn.resolved_type) return static_cast<FunctionResolvedType*>(fn.resolved_type);
		if (!fn.comptime_params.empty()) return nullptr;

		FunctionResolvedType* fn_type = makeType<FunctionResolvedType>(unit, *unit.arena.arena);
		fn_type->decl = &fn;
		for (FunctionParam& param : fn.runtime_params) {
			param.resolved_type = resolveParsedType(unit, param.parsed_type);
			if (!param.resolved_type) {
				// TODO error msg
				return nullptr;
			}
			if (reject_nullable_refs && param.is_ref && param.resolved_type->kind == ResolvedType::NULLABLE) {
				errorLine(fn.token, "Function parameter ", param.name, " cannot be a nullable reference");
				return nullptr;
			}
			fn_type->param_types.push(param.resolved_type);
		}
		fn_type->return_type = resolveParsedType(unit, fn.return_type);
		if (!fn_type->return_type) {
			// TODO error msg
			return nullptr;
		}
		fn.resolved_type = fn_type;
		return fn_type;
	}

	static bool operatorHasPrimitiveSignature(const FunctionResolvedType& fn_type) {
		if (fn_type.param_types.empty()) return false;
		for (ResolvedType* param : fn_type.param_types) {
			if (!isPrimitiveValueType(param)) return false;
		}
		return true;
	}

	// Probe-and-commit overload resolution for n-ary operators.
	// Probes each candidate by type-checking the operands in place under suppressed
	// errors: stale resolved_type/symbol annotations from a losing candidate are
	// overwritten by the commit pass once a unique match is confirmed, and a candidate
	// that fails must not emit diagnostics. A function-literal operand checked during a
	// probe is safe because checkExpr clears its cached resolved_type when its body
	// fails, so a later non-suppressed check re-runs the body.
	// Looks up only the operator list of the first struct operand (host). Operators are
	// indexed there when their symbol is checked, so this is O(overloads-on-type).
	enum class OverloadResult { NOT_FOUND, FOUND, AMBIGUOUS };

	struct CallResolutionCandidate {
		FunctionResolvedType* type = nullptr;
		FunctionExpression* resolved_fn = nullptr;
		u32 ufcs_param_offset = 0;
	};

	bool matchCallCandidate(Unit& unit, FunctionCheckContext* ctx, CallExpression& call, Expression& expr, const CallResolutionCandidate& candidate, ResolvedType*& result_type) {
		FunctionResolvedType* fn_type = candidate.type;
		if (!fn_type) return false;

		if (fn_type->param_types.size() != call.args.size() + candidate.ufcs_param_offset) {
			if (!suppress_errors) {
				errorLine(expr.token, "Function call argument count mismatch: expected ", fn_type->param_types.size() - candidate.ufcs_param_offset, ", got ", call.args.size());
			}
			return false;
		}

		if (candidate.ufcs_param_offset) {
			MemberExpression& mem = static_cast<MemberExpression&>(*call.callee);
			ResolvedType* receiver_type = mem.expression ? mem.expression->resolved_type : nullptr;
			if (!receiver_type || !canImplicitlyConvert(receiver_type, fn_type->param_types[0])) {
				return false;
			}
		}

		for (u32 i = 0; i < call.args.size(); ++i) {
			const u32 param_index = candidate.ufcs_param_offset + i;
			ResolvedType* param_type = fn_type->param_types[param_index];
			Expression* arg = call.args[i];

			if (fn_type->decl && fn_type->decl->runtime_params.size() > param_index && fn_type->decl->runtime_params[param_index].is_ref) {
				if (arg->kind != Expression::UNARY) {
					if (!suppress_errors) {
						errorLine(call.args[i]->token, "Cannot pass non-ref expression as ref argument ", i + 1, " of function call");
					}
					return false;
				}
				UnaryExpression* un = static_cast<UnaryExpression*>(arg);
				if (un->op != Token::REF) {
					return false;
				}
				bool writable = false;
				ResolvedType* arg_type = checkAssignableExpr(unit, ctx, un->expression, writable);
				if (!arg_type) return false;
				if (!writable) {
					if (!suppress_errors) {
						errorLine(call.args[i]->token, "Cannot pass non-writable expression as ref argument ", i + 1, " of function call");
					}
					return false;
				}
				if (!typesEqual(arg_type, param_type)) {
					if (!suppress_errors) {
						errorLine(call.args[i]->token, "Cannot convert ", arg_type, " to ", param_type, " for ref argument ", i + 1, " of function call");
					}
					return false;
				}
				continue;
			}

			ResolvedType* arg_type = checkExpr(unit, ctx, *arg, param_type);
			if (!arg_type) return false;
			if (isUntypedNumeric(arg_type)) {
				arg_type = materializeUntyped(*arg, param_type);
				if (!arg_type) return false;
			}
			if (!canImplicitlyConvert(arg_type, param_type)) {
				if (!suppress_errors) {
					errorLine(call.args[i]->token, "Cannot convert ", arg_type, " to ", param_type, " for argument ", i + 1, " of function call");
				}
				return false;
			}
		}

		result_type = fn_type->return_type;
		return true;
	}

	bool matchOperatorCandidate(Unit& unit, FunctionCheckContext* ctx, Expression** operands, i32 arity, FunctionResolvedType* fn_type, ResolvedType*& result_type) {
		if (!fn_type) return false;
		if ((i32)fn_type->param_types.size() != arity) return false;

		for (i32 i = 0; i < arity; ++i) {
			ResolvedType* t = checkExpr(unit, ctx, *operands[i], fn_type->param_types[(u32)i]);
			if (!t || !typesEqual(t, fn_type->param_types[(u32)i])) return false;
		}

		result_type = fn_type->return_type;
		return true;
	}

	OverloadResult resolveOperatorOverload(Unit& unit,
		FunctionCheckContext* ctx,
		Token::Type op,
		i32 arity,
		Expression** operands, // array of `arity` expression pointers
		ResolvedType*& result_type) {

		// Find the host: first operand whose resolved type is a struct.
		// Primitives and untyped literals are skipped; they cannot host operator lists.
		StructExpression* host = nullptr;
		for (i32 i = 0; i < arity && !host; ++i) {
			++suppress_errors;
			ResolvedType* t = checkExpr(unit, ctx, *operands[i], nullptr);
			--suppress_errors;
			if (t && t->kind == ResolvedType::STRUCT)
				host = static_cast<StructResolvedType*>(t)->decl;
		}
		if (!host) return OverloadResult::NOT_FOUND;


		ExpArray<FunctionResolvedType*> candidates(unit.arena);
		for (StructOperator& cand : host->operators) {
			if (cand.op != op) continue;
			FunctionResolvedType* fn_type = static_cast<FunctionResolvedType*>(cand.fn->resolved_type);
			if (!fn_type || (i32)fn_type->param_types.size() != arity) continue;
			candidates.push(fn_type);
		}

		i32 match_index = -1;
		ResolvedType* matched_type = nullptr;
		for (i32 i = 0; i < (i32)candidates.size(); ++i) {
			++suppress_errors;
			ResolvedType* candidate_type = nullptr;
			const bool match = matchOperatorCandidate(unit, ctx, operands, arity, candidates[i], candidate_type);
			--suppress_errors;
			if (!match) continue;
			if (match_index >= 0) return OverloadResult::AMBIGUOUS;
			match_index = i;
			matched_type = candidate_type;
		}

		if (match_index < 0) return OverloadResult::NOT_FOUND;
		result_type = matched_type;
		return OverloadResult::FOUND;
	}

	OverloadResult resolveUnaryOperator(Unit& unit, FunctionCheckContext* ctx, Token::Type op, Expression* expr, ResolvedType*& result_type) {
		if (op != Token::MINUS) return OverloadResult::NOT_FOUND;
		Expression* operands[1] = {expr};
		return resolveOperatorOverload(unit, ctx, op, 1, operands, result_type);
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

	static bool inCurrentScope(FunctionCheckContext& ctx, ls_string_view name) {
		const u32 mark = ctx.scope_marks.empty() ? 0u : ctx.scope_marks.back();
		for (u32 i = mark; i < ctx.locals.size(); ++i) {
			if (equalStrings(ctx.locals[i].name, name)) return true;
		}
		return false;
	}

	static void pushScope(FunctionCheckContext& ctx) { ctx.scope_marks.push((u32)ctx.locals.size()); }

	static void popScope(FunctionCheckContext& ctx) {
		if (ctx.scope_marks.empty()) return;
		const u32 mark = ctx.scope_marks.back();
		ctx.scope_marks.pop_back();
		while (ctx.locals.size() > mark) ctx.locals.pop_back();
	}

	static bool isProvisionalSymbolVisible(const Symbol& sym) {
		// Recursive functions publish a provisional type while their bodies are being checked.
		if (sym.expression && sym.expression->kind == Expression::FUNCTION && sym.resolved_type) return true;
		// Struct declarations publish a provisional meta-type before field resolution.
		if (sym.expression && sym.expression->kind == Expression::STRUCT && sym.resolved_type && sym.resolved_type->kind == ResolvedType::META) return true;
		return false;
	}

	bool findSymbolForNameExpression(Unit& unit, const Expression& expression, Unit*& owner, Symbol*& symbol) {
		ls_string_view qualifier = {};
		ls_string_view name = {};
		if (expression.kind == Expression::IDENTIFIER) {
			name = static_cast<const IdentifierExpression&>(expression).name;
		} else if (expression.kind == Expression::MEMBER) {
			const MemberExpression& member = static_cast<const MemberExpression&>(expression);
			if (!member.expression || member.expression->kind != Expression::IDENTIFIER) return false;
			qualifier = static_cast<IdentifierExpression*>(member.expression)->name;
			name = member.name;
		} else {
			return false;
		}
		SymbolRef ref = resolveSymbol(unit, qualifier, name, LookupPolicy::NameOnly);
		owner = ref.owner;
		symbol = ref.symbol;
		return ref.symbol != nullptr;
	}

	ResolvedType* resolveExpressionAsType(Unit& unit, Expression& expression) {
		if (expression.kind == Expression::TYPE_LITERAL) {
			ParsedType::Kind parsed_kind = static_cast<TypeLiteralExpression&>(expression).type;
			if (parsed_kind < ParsedType::VOID || parsed_kind > ParsedType::TYPE) return nullptr;
			if (parsed_kind == ParsedType::TYPE) {
				return makeType<MetaType>(unit);
			}
			return primitiveType(static_cast<ResolvedType::Kind>(parsed_kind));
		}
		if (expression.kind == Expression::IDENTIFIER || expression.kind == Expression::MEMBER) {
			Unit* owner = nullptr;
			Symbol* symbol = nullptr;
			if (!findSymbolForNameExpression(unit, expression, owner, symbol)) return nullptr;
			if (checkSymbol(*owner, *symbol) == LS_RESULT_FAILURE) return nullptr;
			if (expression.kind == Expression::IDENTIFIER) static_cast<IdentifierExpression&>(expression).symbol = symbol;
			return unwrapMeta(symbol->resolved_type);
		}
		if (expression.kind == Expression::BRACKET) {
			BracketExpression& br = static_cast<BracketExpression&>(expression);
			if (br.has_colon) return nullptr;
			Unit* owner = nullptr;
			Symbol* symbol = nullptr;
			if (!br.base || !findSymbolForNameExpression(unit, *br.base, owner, symbol)) return nullptr;
			if (checkSymbol(*owner, *symbol) == LS_RESULT_FAILURE) return nullptr;
			if (!symbol->expression) return nullptr;
			if (symbol->expression->kind == Expression::STRUCT) {
				StructExpression& st = static_cast<StructExpression&>(*symbol->expression);
				if (st.comptime_params.empty()) return nullptr;
				StructResolvedType* instance = templates.instantiateStructTemplate(unit, st, br.args);
				expression.resolved_type = instance;
				return instance;
			}
			if (symbol->expression->kind == Expression::FUNCTION) {
				FunctionExpression& fn = static_cast<FunctionExpression&>(*symbol->expression);
				if (fn.comptime_params.empty()) return nullptr;
				FunctionExpression* instance = templates.instantiateFunctionTemplate(*owner, *symbol, fn, br.args);
				if (!instance) return nullptr;
				expression.resolved_type = instance->resolved_type;
				return expression.resolved_type;
			}
		}
		return nullptr;
	}

	// `base.name` where `base` is an identifier naming an aliased import (or other
	// qualifier). Returns {} when `base` is not an identifier or names no such member,
	// in which case the caller falls back to treating `base` as a value.
	SymbolRef resolveQualifiedMember(Unit& unit, Expression& base, ls_string_view name) {
		if (base.kind != Expression::IDENTIFIER) return {};
		return resolveSymbol(unit, static_cast<IdentifierExpression&>(base).name, name, LookupPolicy::Checked);
	}

	ls_result checkComptimeFunctionSymbol(Unit& unit, Symbol& sym) {
		FunctionExpression& fn = static_cast<FunctionExpression&>(*sym.expression);
		if (!fn.comptime_params.empty()) {
			if (templates.hasValueComptimeParams(fn.comptime_params)) {
				errorLine(sym.token, "Function templates only support type comptime parameters");
				return LS_RESULT_FAILURE;
			}
			sym.resolved_type = nullptr;
			return LS_RESULT_OK;
		}
		FunctionResolvedType* fn_type = buildFunctionType(unit, fn, true);
		if (!fn_type) return LS_RESULT_FAILURE;
		sym.resolved_type = fn_type;
		if (const Token::Type op_token = tokenFromOperatorName(sym.name); op_token != Token::ERROR) {
			const i32 arity = (i32)fn_type->param_types.size();
			if ((op_token == Token::MINUS ? (arity != 1 && arity != 2) : arity != 2)) {
				errorLine(sym.token, "Invalid operator arity");
				return LS_RESULT_FAILURE;
			}
			if (operatorHasPrimitiveSignature(*fn_type)) {
				errorLine(sym.token, "Operator overloads for primitive signatures are not allowed");
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

	ls_result checkComptimeStructSymbol(Unit& unit, Symbol& sym) {
		StructExpression& st = static_cast<StructExpression&>(*sym.expression);
		StructResolvedType* st_type = makeType<StructResolvedType>(unit, *unit.arena.arena);
		st_type->decl = &st;
		MetaType* meta = makeType<MetaType>(unit);
		meta->inner = st_type;
		sym.resolved_type = meta;
		if (!st.comptime_params.empty()) {
			for (NamedDecl& field : st.fields) {
				if (parsedTypeIsDirectSelfReference(field.parsed_type, sym.name)) {
					errorLine(sym.token, "Recursive by-value field '", field.name, "' in struct: ", sym.name);
					return LS_RESULT_FAILURE;
				}
			}
			return LS_RESULT_OK;
		}
		for (NamedDecl& field : st.fields) {
			field.resolved_type = resolveParsedType(unit, field.parsed_type);
			if (!field.resolved_type) {
				errorLine(sym.token, "Could not resolve type of field '", field.name, "' in struct: ", sym.name);
				return LS_RESULT_FAILURE;
			}
			if (field.resolved_type == st_type) {
				errorLine(sym.token, "Recursive by-value field '", field.name, "' in struct: ", sym.name);
				return LS_RESULT_FAILURE;
			}
			st_type->field_types.push(field.resolved_type);
		}
		return LS_RESULT_OK;
	}

	ls_result checkComptimeEnumSymbol(Unit& unit, Symbol& sym) {
		EnumExpression& en = static_cast<EnumExpression&>(*sym.expression);
		EnumResolvedType* en_type = makeType<EnumResolvedType>(unit);
		en_type->decl = &en;
		MetaType* meta = makeType<MetaType>(unit);
		meta->inner = en_type;
		sym.resolved_type = meta;
		return LS_RESULT_OK;
	}

	ls_result checkComptimeValueSymbol(Unit& unit, Symbol& sym) {
		// Plain comptime value: comptime N = expr;
		ResolvedType* annotation = resolveParsedType(unit, sym.parsed_type);
		FunctionCheckContext comptime_ctx(unit.arena);
		comptime_ctx.comptime_only = true;
		ResolvedType* expr_type = checkExpr(unit, &comptime_ctx, *sym.expression, annotation);
		if (!expr_type) {
			errorLine(sym.token, "Unresolved initializer for: ", sym.name);
			return LS_RESULT_FAILURE;
		}
		if (isUntypedNumeric(expr_type)) {
			expr_type = materializeUntyped(*sym.expression, annotation);
			if (!expr_type) return LS_RESULT_FAILURE;
		}
		if (annotation && expr_type && !canImplicitlyConvert(expr_type, annotation)) {
			errorLine(sym.token, "Type mismatch in comptime declaration: ", sym.name);
			return LS_RESULT_FAILURE;
		}
		if (sym.expression && sym.expression->kind == Expression::TYPE_LITERAL) {
			MetaType* meta = makeType<MetaType>(unit);
			meta->inner = expr_type;
			sym.resolved_type = meta;
		} else {
			sym.resolved_type = annotation ? annotation : expr_type;
		}
		return LS_RESULT_OK;
	}

	ls_result checkRuntimeFunctionSymbol(Unit& unit, Symbol& sym, ResolvedType* annotation) {
		FunctionExpression& fn = static_cast<FunctionExpression&>(*sym.expression);
		FunctionResolvedType* fn_type = buildFunctionType(unit, fn, true);
		if (!fn_type) return LS_RESULT_FAILURE;
		sym.resolved_type = fn_type;
		if (annotation && !canImplicitlyConvert(fn_type, annotation)) {
			errorLine(sym.token, "Type mismatch in initializer for: ", sym.name);
			return LS_RESULT_FAILURE;
		}
		if (!checkFunctionBody(unit, fn)) return LS_RESULT_FAILURE;
		return LS_RESULT_OK;
	}

	ls_result checkRuntimeValueSymbol(Unit& unit, Symbol& sym, ResolvedType* annotation) {
		ResolvedType* expr_type = sym.expression ? checkExpr(unit, nullptr, *sym.expression, annotation) : nullptr;
		if (sym.expression && !expr_type) {
			errorLine(sym.token, "Unresolved initializer for: ", sym.name);
			return LS_RESULT_FAILURE;
		}
		// A global initializer is a storage point too: pin any untyped value before
		// codegen so nothing untyped reaches the backend.
		if (isUntypedNumeric(expr_type)) {
			expr_type = materializeUntyped(*sym.expression, annotation);
			if (!expr_type) return LS_RESULT_FAILURE;
		}
		if (annotation && expr_type && !canImplicitlyConvert(expr_type, annotation)) {
			errorLine(sym.token, "Type mismatch in initializer for: ", sym.name);
			return LS_RESULT_FAILURE;
		}
		sym.resolved_type = annotation ? annotation : expr_type;
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
		ResolvedType* annotation = resolveParsedType(unit, sym.parsed_type);

		if (sym.expression && sym.expression->kind == Expression::UNDEFINED) {
			if (!annotation) {
				errorLine(sym.token, "'undefined' initializer requires an explicit type annotation: ", sym.name);
				return LS_RESULT_FAILURE;
			}
			if (sym.storage == Symbol::CONST) {
				errorLine(sym.token, "const cannot be initialized with 'undefined': ", sym.name);
				return LS_RESULT_FAILURE;
			}
		}

		return sym.expression && sym.expression->kind == Expression::FUNCTION
			? checkRuntimeFunctionSymbol(unit, sym, annotation)
			: checkRuntimeValueSymbol(unit, sym, annotation);
	}

	ResolvedType* checkCallExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		CallExpression& call = static_cast<CallExpression&>(expr);

		// length(slice | array)
		if (call.callee->kind == Expression::IDENTIFIER && call.args.size() == 1) {
			ls_string_view name = static_cast<IdentifierExpression&>(*call.callee).name;
			if (equalStrings(name, makeStringView("length"))) {
				ResolvedType* arg = checkExpr(unit, ctx, *call.args[0], nullptr, nullptr);
				if (arg && (arg->kind == ResolvedType::ARRAY || arg->kind == ResolvedType::SLICE)) {
					expr.resolved_type = primitiveType(ResolvedType::I32);
					return expr.resolved_type;
				}
				return nullptr;
			}
		}

		// Collect explicit call candidates first, then validate them in order. This keeps
		// the resolution flow readable while preserving the current suppressed-error behavior
		// for losing candidates.
		// Slots: direct callee type, template instantiation, UFCS.
		CallResolutionCandidate candidates[3];
		i32 candidate_count = 0;

		// suppress errors because callee can be checked twice - one normal and one for ufcs
		// and because first arg can be checked twice - one normal and one for ADL
		++suppress_errors;
		ResolvedType* first_arg_type = nullptr;
		if (!call.args.empty()) {
			first_arg_type = checkExpr(unit, ctx, *call.args[0], nullptr, nullptr);
		}
		ResolvedType* callee_type = checkExpr(unit, ctx, *call.callee, nullptr, first_arg_type);
		--suppress_errors;

		if (callee_type && callee_type->kind == ResolvedType::FUNCTION) {
			candidates[candidate_count++].type = static_cast<FunctionResolvedType*>(callee_type);
		}

		if (!callee_type && (call.callee->kind == Expression::IDENTIFIER || call.callee->kind == Expression::MEMBER)) {
			Unit* owner = nullptr;
			Symbol* symbol = nullptr;
			if (findSymbolForNameExpression(unit, *call.callee, owner, symbol) && symbol->expression && symbol->expression->kind == Expression::FUNCTION) {
				FunctionExpression& fn = static_cast<FunctionExpression&>(*symbol->expression);
				if (!fn.comptime_params.empty()) {
					FunctionExpression* instance = templates.inferAndInstantiateFunctionTemplate(*owner, unit, ctx, *symbol, fn, call, hint);
					if (!instance) return nullptr;
					if (call.callee->kind == Expression::IDENTIFIER) static_cast<IdentifierExpression*>(call.callee)->symbol = symbol;
					CallResolutionCandidate& candidate = candidates[candidate_count++];
					candidate.type = static_cast<FunctionResolvedType*>(instance->resolved_type);
					candidate.resolved_fn = instance;
					candidate.ufcs_param_offset = 0;
				}
			}
		}

		// UFCS: x.foo(a, b) -> foo(x, a, b)
		// When the callee is a member expression that didn't resolve as a field or namespace
		// member, try looking up the member name as a free function in the receiver type's
		// namespace unit. Restricted to struct/enum receivers to avoid UFCS on primitives.
		// Codegen's existing findMemberFunction path handles emission (receiver + args).
		if (!callee_type && call.callee->kind == Expression::MEMBER) {
			MemberExpression& mem = static_cast<MemberExpression&>(*call.callee);
			if (mem.expression) {
				ResolvedType* receiver_type = mem.expression->resolved_type;
				if (receiver_type) {
					Unit* ns = findTypeNamespaceUnit(*receiver_type);
					Symbol* ns_sym = ns ? findSymbol(*ns, mem.name) : nullptr;
					if (ns_sym && checkSymbol(*ns, *ns_sym) == LS_RESULT_FAILURE) ns_sym = nullptr;
					const bool is_struct_or_enum = receiver_type->kind == ResolvedType::STRUCT || receiver_type->kind == ResolvedType::ENUM;
					Symbol* local_sym = is_struct_or_enum ? findSymbol(unit, mem.name) : nullptr;
					if (local_sym && checkSymbol(unit, *local_sym) == LS_RESULT_FAILURE) local_sym = nullptr;
					Symbol* resolved = local_sym ? local_sym : ns_sym;
					if (resolved) {
						CallResolutionCandidate& candidate = candidates[candidate_count++];
						candidate.type = static_cast<FunctionResolvedType*>(unwrapMeta(resolved->resolved_type));
						candidate.resolved_fn = resolved->expression && resolved->expression->kind == Expression::FUNCTION ? static_cast<FunctionExpression*>(resolved->expression) : nullptr;
						candidate.ufcs_param_offset = 1;
					}
				}
			}
		}

		if (!candidate_count) {
			if (callee_type) {
				errorLine(expr.token, "Cannot call non-function type ", callee_type);
			}
			return nullptr;
		}

		auto commit = [&](const CallResolutionCandidate& winner, ResolvedType* result_type) {
			if (winner.resolved_fn) call.resolved_fn = winner.resolved_fn;
			expr.resolved_type = result_type;
		};

		if (candidate_count == 1) {
			ResolvedType* result_type = nullptr;
			if (!matchCallCandidate(unit, ctx, call, expr, candidates[0], result_type)) return nullptr;
			commit(candidates[0], result_type);
			return result_type;
		}

		i32 match_index = -1;
		ResolvedType* matched_result = nullptr;
		for (i32 i = 0; i < candidate_count; ++i) {
			++suppress_errors;
			ResolvedType* candidate_type = nullptr;
			const bool match = matchCallCandidate(unit, ctx, call, expr, candidates[i], candidate_type);
			--suppress_errors;
			if (!match) continue;
			ASSERT(candidate_type);
			if (match_index >= 0) {
				errorLine(expr.token, "Ambiguous function call");
				return nullptr;
			}
			match_index = i;
			matched_result = candidate_type;
		}

		if (match_index < 0) {
			if (callee_type) {
				errorLine(expr.token, "Cannot call non-function type ", callee_type);
			}
			return nullptr;
		}

		commit(candidates[match_index], matched_result);
		return matched_result;
	}

	// Pin an untyped numeric expression to a concrete type.
	// A concrete `concrete` range-checks leaf literals; a null/non-numeric `concrete`
	// uses the literal's default inferred width.
	ResolvedType* materializeUntyped(Expression& expr, ResolvedType* concrete) {
		if (isUntypedInt(expr.resolved_type)) {
			if (isNumericType(concrete)) return materializeUntyped(expr, concrete, true);
			// No target: pin to the narrowest default integer width that holds the value.
			ResolvedType::Kind kind = ResolvedType::I32;
			if (expr.kind == Expression::INT_LITERAL) {
				const u64 value = static_cast<IntLiteralExpression&>(expr).value;
				if (value > 9223372036854775807ull) kind = ResolvedType::U64;
				else if (value > 2147483647u) kind = ResolvedType::I64;
			}
			else if (expr.kind == Expression::UNARY) {
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
			return materializeUntyped(expr, primitiveType(kind), false);
		}
		if (isUntypedFloat(expr.resolved_type)) {
			if (isFloatType(concrete)) return materializeUntyped(expr, concrete, true);
			return materializeUntyped(expr, primitiveType(ResolvedType::F64), false);
		}
		return expr.resolved_type;
	}

	ResolvedType* materializeUntyped(Expression& expr, ResolvedType* concrete, bool check_fit) {
		if (!isUntypedNumeric(expr.resolved_type)) return expr.resolved_type;
		switch (expr.kind) {
			case Expression::INT_LITERAL: {
				const u64 value = static_cast<IntLiteralExpression&>(expr).value;
				if (check_fit && !intLiteralFitsType(value, concrete->kind)) {
					errorLine(expr.token, "Integer literal does not fit in ", concrete);
					return nullptr;
				}
				expr.resolved_type = concrete;
				return concrete;
			}
			case Expression::FLOAT_LITERAL: {
				const double value = static_cast<FloatLiteralExpression&>(expr).value;
				if (check_fit && concrete->kind == ResolvedType::F32) {
					if (value > (double)FLT_MAX || value < -(double)FLT_MAX) {
						errorLine(expr.token, "Float literal does not fit in f32");
						return nullptr;
					}
				}
				expr.resolved_type = concrete;
				return concrete;
			}
			case Expression::UNARY: {
				UnaryExpression& un = static_cast<UnaryExpression&>(expr);
				// A negated integer literal range-checks against the target width.
				if (un.op == Token::MINUS && un.expression && un.expression->kind == Expression::INT_LITERAL) {
					const u64 magnitude = static_cast<IntLiteralExpression*>(un.expression)->value;
					if (check_fit && !negatedIntLiteralFitsType(magnitude, concrete->kind)) {
						errorLine(expr.token, "Integer literal does not fit in type ", concrete);
						return nullptr;
					}
					un.expression->resolved_type = concrete;
					expr.resolved_type = concrete;
					return concrete;
				}
				if (un.expression && !materializeUntyped(*un.expression, concrete, check_fit)) return nullptr;
				expr.resolved_type = concrete;
				return concrete;
			}
			case Expression::BINARY: {
				BinaryExpression& bin = static_cast<BinaryExpression&>(expr);
				if (bin.lhs && !materializeUntyped(*bin.lhs, concrete, check_fit)) return nullptr;
				if (bin.rhs && !materializeUntyped(*bin.rhs, concrete, check_fit)) return nullptr;
				expr.resolved_type = concrete;
				return concrete;
			}
			default: expr.resolved_type = concrete; return concrete;
		}
	}

	ResolvedType* checkUnaryExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		UnaryExpression& un = static_cast<UnaryExpression&>(expr);
		if (un.op == Token::MINUS && un.expression && un.expression->kind == Expression::INT_LITERAL) {
			// Range-check the negated integer literal against the expected type.
			IntLiteralExpression* lit = static_cast<IntLiteralExpression*>(un.expression);
			ResolvedType* int_hint = unwrapNullable(hint);
			if (int_hint && isNumericType(int_hint)) {
				if (!negatedIntLiteralFitsType(lit->value, int_hint->kind)) {
					errorLine(expr.token, "Integer literal does not fit in type ", int_hint);
					return nullptr;
				}
				lit->resolved_type = int_hint;
				expr.resolved_type = int_hint;
				return int_hint;
			}
			// No concrete target: stay untyped until materialization chooses a width.
			lit->resolved_type = untypedInt();
			expr.resolved_type = lit->resolved_type;
			return expr.resolved_type;
		}
		ResolvedType* overload_result = nullptr;
		switch (resolveUnaryOperator(unit, ctx, un.op, un.expression, overload_result)) {
			case OverloadResult::FOUND: expr.resolved_type = overload_result; return overload_result;
			case OverloadResult::AMBIGUOUS: errorLine(expr.token, "Ambiguous operator ", operatorSymbolName(un.op), " overload"); return nullptr;
			case OverloadResult::NOT_FOUND: break;
		}
		ResolvedType* inner = checkExpr(unit, ctx, *un.expression, hint);
		if (!inner) return nullptr;
		switch (un.op) {
			case Token::MINUS: {
				if (!isNumericOrUntyped(inner)) {
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
		ResolvedType* overload_result = nullptr;
		Expression* bin_operands[2] = {bin.lhs, bin.rhs};
		const OverloadResult bin_overload = isOverloadableBinaryOperator(bin.op) ? resolveOperatorOverload(unit, ctx, bin.op, 2, bin_operands, overload_result) : OverloadResult::NOT_FOUND;
		switch (bin_overload) {
			case OverloadResult::FOUND: expr.resolved_type = overload_result; return overload_result;
			case OverloadResult::AMBIGUOUS: errorLine(expr.token, "Ambiguous operator ", operatorSymbolName(bin.op), " overload"); return nullptr;
			case OverloadResult::NOT_FOUND: break;
		}
		// The hint describes the result of the whole expression, not the operands, so we do not
		// forward it blindly. lhs is checked first; rhs then takes lhs as its hint so a literal
		// operand can adopt the other side's type. Operands that stay untyped are unified
		// below (which makes operand inference symmetric, e.g. `3 == x` / `1.5 == x`).
		++suppress_errors;
		ResolvedType* lhs = checkExpr(unit, ctx, *bin.lhs, hint);
		--suppress_errors;
		ResolvedType* rhs = checkExpr(unit, ctx, *bin.rhs, lhs ? lhs : hint);
		// If lhs failed but rhs resolved (e.g. `.Idle == state`), retry lhs with rhs type as hint.
		if (!lhs && rhs) lhs = checkExpr(unit, ctx, *bin.lhs, rhs);
		if (!lhs || !rhs) return nullptr;

		auto invalidOperands = [&]() -> ResolvedType* {
			errorLine(expr.token, "Cannot apply operator ", operatorSymbolName(bin.op), " to ", lhs, " and ", rhs);
			return nullptr;
		};
		// Resolve a numeric operator: unify the operands and pin both to the result type.
		// int_default/float_default control what a fully-untyped pair materializes to:
		// for arithmetic they stay untyped so an outer context still picks the width;
		// for comparisons they pin to a concrete default (the result is bool, not numeric).
		auto resolveNumeric = [&](bool integer_only, ResolvedType* int_default, ResolvedType* float_default) -> ResolvedType* {
			ResolvedType* unified = unifyNumeric(lhs, rhs);
			if (!unified || (integer_only && !isIntegerOrUntyped(unified))) return invalidOperands();
			ResolvedType* concrete = isUntypedInt(unified) ? int_default
			                       : isUntypedFloat(unified) ? float_default
			                       : unified;
			if (!materializeUntyped(*bin.lhs, concrete) || !materializeUntyped(*bin.rhs, concrete)) return nullptr;
			return concrete;
		};
		switch (bin.op) {
			case Token::PLUS:
				if (typesEqual(lhs, primitiveType(ResolvedType::STRING)) && typesEqual(rhs, primitiveType(ResolvedType::STRING))) {
					expr.resolved_type = lhs;
					return lhs;
				}
				[[fallthrough]];
			case Token::MINUS:
			case Token::STAR:
			case Token::SLASH: {
				// Arithmetic keeps the result untyped when both operands are untyped.
				ResolvedType* result = resolveNumeric(false, untypedInt(), untypedFloat());
				if (!result) return nullptr;
				expr.resolved_type = result;
				return result;
			}
			case Token::PERCENT: {
				ResolvedType* result = resolveNumeric(true, untypedInt(), nullptr);
				if (!result) return nullptr;
				expr.resolved_type = result;
				return result;
			}
			case Token::EQUAL_EQUAL:
			case Token::BANG_EQUAL: {
				// Equality also works on non-numerics (enums, strings); only unify when numeric.
				if (isNumericOrUntyped(lhs) || isNumericOrUntyped(rhs)) {
					if (!resolveNumeric(false, primitiveType(ResolvedType::I32), primitiveType(ResolvedType::F64))) return nullptr;
				} else if (!typesEqual(lhs, rhs))
					return invalidOperands();
				expr.resolved_type = primitiveType(ResolvedType::BOOL);
				return expr.resolved_type;
			}
			case Token::LT:
			case Token::LT_EQUAL:
			case Token::GT:
			case Token::GT_EQUAL:
				if (!resolveNumeric(false, primitiveType(ResolvedType::I32), primitiveType(ResolvedType::F64))) return nullptr;
				expr.resolved_type = primitiveType(ResolvedType::BOOL);
				return expr.resolved_type;
			case Token::AND:
			case Token::OR:
				if (!typesEqual(lhs, primitiveType(ResolvedType::BOOL)) || !typesEqual(rhs, primitiveType(ResolvedType::BOOL))) return invalidOperands();
				expr.resolved_type = primitiveType(ResolvedType::BOOL);
				return expr.resolved_type;
			default:
				// TODO error msg, can we even get here?
				return nullptr;
		}
	}

	ResolvedType* checkCastExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr) {
		CastExpression& cast = static_cast<CastExpression&>(expr);
		ResolvedType* dst_type = resolveParsedType(unit, cast.parsed_type);
		if (!dst_type) {
			errorLine(expr.token, "Cannot resolve cast type");
			return nullptr;
		}
		// Don't pass dst_type as hint: explicit casts allow out-of-range values and
		// the operand resolves independently (e.g. `-1 as u8` should work).
		ResolvedType* src_type = checkExpr(unit, ctx, *cast.expression, nullptr);
		if (!src_type) return nullptr;
		// The source is consumed as a value here; pin an untyped literal to its i32 default
		// (an explicit cast then converts it, so no range check against the destination).
		if (isUntypedNumeric(src_type)) {
			src_type = materializeUntyped(*cast.expression, nullptr);
			if (!src_type) return nullptr;
		}
		const bool src_numeric = isNumericType(src_type);
		const bool dst_numeric = isNumericType(dst_type);
		const bool src_integer = isIntegerType(src_type);
		const bool dst_integer = isIntegerType(dst_type);
		const bool src_enum = src_type->kind == ResolvedType::ENUM;
		const bool dst_enum = dst_type->kind == ResolvedType::ENUM;
		// bool->bool (and any other same-type cast) is covered by the trailing typesEqual.
		const bool valid_cast = (src_numeric && dst_numeric) || (src_enum && dst_integer) || (src_integer && dst_enum) || typesEqual(src_type, dst_type);
		if (!valid_cast) {
			errorLine(expr.token, "Cannot cast ", src_type, " to ", dst_type);
			return nullptr;
		}
		expr.resolved_type = dst_type;
		return dst_type;
	}

	ResolvedType* checkMemberExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		MemberExpression& member = static_cast<MemberExpression&>(expr);
		// .enum_member

		if (!member.expression) {
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
			IdentifierExpression* id = static_cast<IdentifierExpression*>(member.expression); // parser makes sure member.expression is identifier
			Unit* imported_unit = findImportedUnitByAlias(unit, id->name);
			if (imported_unit) {
				SymbolRef sym = resolveSymbol(*imported_unit, {}, member.name, LookupPolicy::NameOnly);
				if (sym) {
					if (checkSymbol(*sym.owner, *sym.symbol) == LS_RESULT_FAILURE) return nullptr;

					expr.resolved_type = sym.symbol->resolved_type;
					return expr.resolved_type;
				}
				errorLine(expr.token, member.name, " not found in ", id->name);
				return nullptr;
			}
			errorLine(expr.token, "Unknown identifier ", id->name);
			return nullptr;
		}

		switch (base_type->kind) {
			case ResolvedType::STRUCT: {
				// struct.field
				StructResolvedType* st = static_cast<StructResolvedType*>(base_type);
				for (i32 i = 0; i < st->decl->fields.size(); ++i) {
					const NamedDecl& field = st->decl->fields[i];
					if (equalStrings(field.name, member.name)) {
						ResolvedType* field_type = (u32)i < st->field_types.size() ? st->field_types[(u32)i] : field.resolved_type;
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
				if (isPrimitiveValueType(base_type)) {
					errorLine(expr.token, "Cannot access member ", member.name, " of primitive type ", base_type);
					return nullptr;
				}
				return nullptr; // TODO can we get here? / TODO error msg
		}
	}

	ResolvedType* checkBracketExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		BracketExpression& br = static_cast<BracketExpression&>(expr);
		if (ResolvedType* template_type = resolveExpressionAsType(unit, expr)) {
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
		if (br.has_colon) {
			for (Expression* arg : br.args) {
				ResolvedType* arg_type = checkExpr(unit, ctx, *arg, primitiveType(ResolvedType::I32));
				if (!arg_type || !isIntegerType(arg_type)) {
					// TODO error msg
					return nullptr;
				}
			}
			if (br.end) {
				ResolvedType* end_type = checkExpr(unit, ctx, *br.end, primitiveType(ResolvedType::I32));
				if (!end_type || !isIntegerType(end_type)) {
					// TODO error msg
					return nullptr;
				}
			}
			const ArrayResolvedType* arr = base_type->kind == ResolvedType::ARRAY ? static_cast<const ArrayResolvedType*>(base_type) : nullptr;
			i64 begin = 0;
			i64 end = arr ? arr->size : 0;
			const bool has_begin = !br.args.empty() && resolveComptimeIntValue(unit, br.args[0], begin);
			const bool has_end = br.end ? resolveComptimeIntValue(unit, br.end, end) : !!arr;
			if (arr) {
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
		if (br.args.size() != 1) {
			// TODO error msg
			return nullptr;
		}
		ResolvedType* index_type = checkExpr(unit, ctx, *br.args[0], primitiveType(ResolvedType::I32));
		if (!index_type) {
			// TODO error msg
			return nullptr;
		}
		if (!isIntegerType(index_type)) {
			errorLine(expr.token, "Cannot index with type ", index_type, ", expected integer type");
			return nullptr;
		}
		if (base_type->kind == ResolvedType::ARRAY) {
			const ArrayResolvedType* arr = static_cast<const ArrayResolvedType*>(base_type);
			i64 index = 0;
			if (resolveComptimeIntValue(unit, br.args[0], index)) {
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

	ResolvedType* checkStructLiteralExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint) {
		StructLiteralExpression& lit = static_cast<StructLiteralExpression&>(expr);
		// In the type position of a literal, a top-level type name wins over a
		// same-named local value. This is also what lets `template[x](x { ... })`
		// use `x` as a type argument and as an ordinary local index nearby.
		ResolvedType* type = nullptr;
		if (lit.type) {
			type = resolveExpressionAsType(unit, *lit.type);
			if (!type) {
				// Happens when lit.type is a bracket expression (e.g. a template instantiation like Foo[T]).
				// resolveExpressionAsType only handles TYPE_LITERAL, IDENTIFIER, and MEMBER.
				type = checkExpr(unit, ctx, *lit.type, hint);
			}
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
			ResolvedType* field_type = (u32)i < st->field_types.size() ? st->field_types[(u32)i] : st->decl->fields[(u32)i].resolved_type;
			ResolvedType* value_type = checkExpr(unit, ctx, *lit.values[i], field_type);
			if (!value_type) return nullptr;
			if (isUntypedNumeric(value_type)) value_type = materializeUntyped(*lit.values[i], field_type);
			if (!value_type || !canImplicitlyConvert(value_type, field_type)) {
				errorLine(lit.values[i]->token, "Cannot convert ", value_type, " to ", field_type, " for field ", st->decl->fields[(u32)i].name, " of struct literal");
				return nullptr;
			}
		}
		expr.resolved_type = type;
		return type;
	}

	ResolvedType* checkExpr(Unit& unit, FunctionCheckContext* ctx, Expression& expr, ResolvedType* hint, ResolvedType* first_arg_type = nullptr) {
		switch (expr.kind) {
			case Expression::INT_LITERAL: {
				ResolvedType* int_hint = unwrapNullable(hint);
				if (int_hint && isNumericType(int_hint)) {
					const u64 value = static_cast<IntLiteralExpression&>(expr).value;
					if (!intLiteralFitsType(value, int_hint->kind)) {
						errorLine(expr.token, "Integer literal does not fit in ", int_hint);
						return nullptr;
					}
					expr.resolved_type = int_hint;
				} else {
					// No concrete numeric target yet: stay untyped and let unification with the
					// surrounding operand (or a later materialization point) pin the width.
					expr.resolved_type = untypedInt();
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
					expr.resolved_type = (float_hint && isFloatType(float_hint)) ? float_hint : untypedFloat();
				}
				return expr.resolved_type;
			}
			case Expression::BOOL_LITERAL: expr.resolved_type = primitiveType(ResolvedType::BOOL); return expr.resolved_type;
			case Expression::STRING_LITERAL: expr.resolved_type = primitiveType(ResolvedType::STRING); return expr.resolved_type;
			case Expression::NULL_LITERAL:
				if (!hint) {
					// TODO error msg
					return nullptr;
				}
				if (hint->kind != ResolvedType::NULLABLE && hint->kind != ResolvedType::SLICE) {
					errorLine(expr.token, "Cannot use null literal as ", hint);
					return nullptr;
				}
				expr.resolved_type = hint;
				return expr.resolved_type;
			case Expression::UNDEFINED: expr.resolved_type = hint; return expr.resolved_type;
			case Expression::TYPE_LITERAL: {
				if (!ctx || !ctx->comptime_only) return nullptr;
				ParsedType::Kind parsed_kind = static_cast<TypeLiteralExpression&>(expr).type;
				if (parsed_kind == ParsedType::TYPE) {
					expr.resolved_type = makeType<MetaType>(unit);
					return expr.resolved_type;
				}
				ResolvedType* t = primitiveType(static_cast<ResolvedType::Kind>(parsed_kind));
				expr.resolved_type = t;
				return t;
			}
			case Expression::IDENTIFIER: {
				IdentifierExpression& id = static_cast<IdentifierExpression&>(expr);
				if (ctx) {
					if (SemanticLocalBinding* local = findLocal(*ctx, id.name)) {
						id.symbol = nullptr;
						expr.resolved_type = local->type;
						return expr.resolved_type;
					}
				}
				SymbolRef ref = resolveSymbol(unit, {}, id.name, LookupPolicy::Checked, first_arg_type);
				if (!ref) {
					errorLine(expr.token, "Unknown identifier ", id.name);
					return nullptr;
				}
				if (ctx && ctx->comptime_only && ref.symbol->storage != Symbol::COMPTIME) {
					errorLine(expr.token, "Cannot use non-comptime symbol ", id.name, " in comptime context");
					return nullptr;
				}
				id.symbol = ref.symbol;
				expr.resolved_type = ref.symbol->resolved_type;
				return expr.resolved_type;
			}
			case Expression::FUNCTION: {
				FunctionExpression& fn = static_cast<FunctionExpression&>(expr);
				if (fn.resolved_type) return fn.resolved_type;
				FunctionResolvedType* fn_type = buildFunctionType(unit, fn, true);
				if (!fn_type) {
					// TODO error msg
					return nullptr;
				}
				// resolved_type must be set before checkFunctionBody (it reads the return
				// type from it), but a body checked under suppressed errors that fails must
				// not stay cached: clearing it lets a later non-suppressed check re-run the
				// body and surface the real diagnostic.
				expr.resolved_type = fn_type;
				if (fn.body) {
					if (!checkFunctionBody(unit, fn)) {
						expr.resolved_type = nullptr;
						return nullptr;
					}
				}
				return fn_type;
			}
			case Expression::CALL: return checkCallExpr(unit, ctx, expr, hint);
			case Expression::UNARY: return checkUnaryExpr(unit, ctx, expr, hint);
			case Expression::BINARY: return checkBinaryExpr(unit, ctx, expr, hint);
			case Expression::CAST: return checkCastExpr(unit, ctx, expr);
			case Expression::MEMBER: return checkMemberExpr(unit, ctx, expr, hint);
			case Expression::BRACKET: return checkBracketExpr(unit, ctx, expr, hint);
			case Expression::STRUCT_LITERAL: return checkStructLiteralExpr(unit, ctx, expr, hint);
			default: return nullptr;
		}
	}

	ResolvedType* checkAssignableExpr(Unit& unit, FunctionCheckContext* ctx, Expression* expr, bool& is_writable) {
		if (!expr) {
			is_writable = false;
			return nullptr;
		}
		switch (expr->kind) {
			case Expression::IDENTIFIER: {
				IdentifierExpression* id = static_cast<IdentifierExpression*>(expr);
				if (ctx) {
					if (SemanticLocalBinding* local = findLocal(*ctx, id->name)) {
						is_writable = !local->is_immutable;
						return local->type;
					}
				}
				SymbolRef ref = resolveSymbol(unit, {}, id->name, LookupPolicy::Checked);
				if (!ref) {
					is_writable = false;
					return nullptr;
				}
				id->symbol = ref.symbol;
				is_writable = ref.symbol->storage == Symbol::VARIABLE;
				return unwrapMeta(ref.symbol->resolved_type);
			}
			case Expression::MEMBER: {
				MemberExpression* member = static_cast<MemberExpression*>(expr);
				if (!member->expression) {
					is_writable = false;
					return nullptr;
				}
				if (SymbolRef ref = resolveQualifiedMember(unit, *member->expression, member->name)) {
					is_writable = ref.symbol->storage == Symbol::VARIABLE;
					return unwrapMeta(ref.symbol->resolved_type);
				}
				bool base_writable = false;
				ResolvedType* base_type = checkAssignableExpr(unit, ctx, member->expression, base_writable);
				if (!base_type || !base_writable) {
					is_writable = false;
					return nullptr;
				}
				ResolvedType* field_type = checkExpr(unit, ctx, *expr, nullptr);
				is_writable = field_type != nullptr;
				return field_type;
			}
			case Expression::BRACKET: {
				BracketExpression* br = static_cast<BracketExpression*>(expr);
				bool base_writable = false;
				ResolvedType* base_type = checkAssignableExpr(unit, ctx, br->base, base_writable);
				if (!base_type || !base_writable) {
					is_writable = false;
					return nullptr;
				}
				ResolvedType* value_type = checkExpr(unit, ctx, *expr, nullptr);
				is_writable = value_type != nullptr;
				return value_type;
			}
			default: is_writable = false; return nullptr;
		}
	}

	static bool isPrimitiveShadowName(ls_string_view name) {
		static const char* names[] = {"void", "bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64", "string", "cptr", "type"};
		for (const char* primitive : names) {
			if (equalStrings(name, makeStringView(primitive))) return true;
		}
		return false;
	}

	bool parsedTypeIsDirectSelfReference(ParsedType* type, ls_string_view name) {
		if (!type) return false;
		if (type->kind == ParsedType::QUALIFIED) {
			QualifiedParsedType* q = static_cast<QualifiedParsedType*>(type);
			return empty(q->qualifier) && equalStrings(q->name, name);
		}
		if (type->kind == ParsedType::BRACKET_TYPE) {
			BracketTypeParsedType* br = static_cast<BracketTypeParsedType*>(type);
			return parsedTypeIsDirectSelfReference(br->callee, name);
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
		const Expression* decl = nullptr;
		switch (type.kind) {
			case ResolvedType::STRUCT: decl = static_cast<const StructResolvedType&>(type).decl; break;
			case ResolvedType::ENUM: decl = static_cast<const EnumResolvedType&>(type).decl; break;
			default: return nullptr;
		}
		if (!decl) return nullptr;

		for (Unit& candidate_unit : module.units) {
			for (Symbol& symbol : candidate_unit.symbols) {
				if (unwrapMeta(symbol.resolved_type) == &type) return &candidate_unit;
				if (symbol.expression == decl) return &candidate_unit;
			}
		}
		return nullptr;
	}

	bool checkFunctionBody(Unit& unit, FunctionExpression& fn) {
		if (!fn.body) return true;
		if (fn.body->kind != Statement::BLOCK) {
			// TODO error msg
			return false;
		}

		ResolvedType* return_type = fn.resolved_type ? static_cast<FunctionResolvedType*>(fn.resolved_type)->return_type : nullptr;
		FunctionCheckContext ctx(*unit.arena.arena);
		pushScope(ctx);
		for (FunctionParam& param : fn.runtime_params) {
			if (findSymbol(unit, param.name)) {
				errorLine(fn.token, "Parameter ", param.name, " shadows a global symbol");
				return false;
			}
			SemanticLocalBinding& binding = ctx.locals.emplace_back();
			binding.name = param.name;
			binding.type = param.resolved_type;
			binding.is_immutable = !param.is_ref;
		}

		BlockStatement* body = static_cast<BlockStatement*>(fn.body);
		for (Statement* st : body->statements) {
			if (!checkStatement(unit, ctx, st, return_type, {})) return false;
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


	bool checkVarDeclStatement(Unit& unit, FunctionCheckContext& ctx, VarDeclStatement* var) {
		if (findLocal(ctx, var->name)) {
			errorLine(var->token, "Variable ", var->name, " shadows an existing local or parameter");
			return false;
		}

		if (findSymbol(unit, var->name)) {
			errorLine(var->token, "Variable ", var->name, " conflicts with a symbol of the same name in the same unit");
			return false;
		}

		ResolvedType* annotation = resolveParsedType(unit, var->parsed_type);
		// The parser always attaches an initializer (`var x = ...;`); there is no
		// uninitialized local form. Unlike global symbols, this path may dereference
		// it unconditionally.
		ASSERT(var->expression);
		if (var->expression->kind == Expression::UNDEFINED) {
			if (!annotation) {
				errorLine(var->token, "Variable ", var->name, " must have a type annotation if initialized with undefined");
				return false;
			}
			if (var->is_immutable) {
				errorLine(var->token, "Variable ", var->name, " cannot be immutable if initialized with undefined");
				return false;
			}
		}

		ResolvedType* expr_type = checkExpr(unit, &ctx, *var->expression, annotation);
		if (!expr_type) return false;
		// Storing into a variable pins the value: an untyped initializer adopts the annotation
		// (or i32 when inferred). `var x = 5` is i32, not an untyped constant that leaks onward.
		if (isUntypedNumeric(expr_type)) {
			expr_type = materializeUntyped(*var->expression, annotation);
			if (!expr_type) return false;
		}

		if (annotation && !canImplicitlyConvert(expr_type, annotation)) {
			errorLine(var->token, "Cannot convert ", expr_type, " to ", annotation, " for variable ", var->name);
			return false;
		}
		ResolvedType* final_type = annotation ? annotation : expr_type;
		var->resolved_type = final_type;

		SemanticLocalBinding& binding = ctx.locals.emplace_back();
		binding.name = var->name;
		binding.type = final_type;
		binding.is_immutable = var->is_immutable;
		return true;
	}

	bool checkAssignStatement(Unit& unit, FunctionCheckContext& ctx, AssignStatement* assign) {
		bool writable = false;
		ResolvedType* lhs_type = checkAssignableExpr(unit, &ctx, assign->lhs, writable);
		if (!lhs_type) return false;
		if (!writable) {
			errorLine(assign->token, "Epression is immutable and cannot be assigned to");
			return false;
		}
		ResolvedType* rhs_type = checkExpr(unit, &ctx, *assign->rhs, lhs_type);
		if (!rhs_type) return false;
		if (isUntypedNumeric(rhs_type)) {
			rhs_type = materializeUntyped(*assign->rhs, lhs_type);
			if (!rhs_type) return false;
		}
		ResolvedType* op_result = nullptr;
		switch (assign->op) {
			case Token::EQUAL:
				if (!canImplicitlyConvert(rhs_type, lhs_type)) {
					errorLine(assign->token, "Cannot convert ", rhs_type, " to ", lhs_type, " for assignment");
					return false;
				}
				return true;
			case Token::PLUS_EQUAL:
			case Token::MINUS_EQUAL:
			case Token::STAR_EQUAL:
			case Token::SLASH_EQUAL: {
				if (isNumericType(lhs_type)) return true;
				const Token::Type base_op = assign->op == Token::PLUS_EQUAL	   ? Token::PLUS
											: assign->op == Token::MINUS_EQUAL ? Token::MINUS
											: assign->op == Token::STAR_EQUAL  ? Token::STAR
																			   : Token::SLASH;
				Expression* operands[2] = {assign->lhs, assign->rhs};
				switch (resolveOperatorOverload(unit, &ctx, base_op, 2, operands, op_result)) {
					case OverloadResult::FOUND: break;
					case OverloadResult::AMBIGUOUS: errorLine(assign->token, "Ambiguous operator overload for compound assignment on type ", lhs_type); return false;
					case OverloadResult::NOT_FOUND: errorLine(assign->token, "No matching operator overload for compound assignment on type ", lhs_type); return false;
				}
				assign->lhs = operands[0];
				assign->rhs = operands[1];

				if (!canImplicitlyConvert(op_result, lhs_type)) {
					errorLine(assign->token, "Compound assignment operator returns ", op_result, " which cannot be implicitly converted to the target type ", lhs_type);
					return false;
				}
				return true;
			}
			default: return false; // TODO can we even get here?
		}
	}

	bool checkIfStatement(Unit& unit, FunctionCheckContext& ctx, IfStatement* ifst, ResolvedType* return_type) {
		ResolvedType* cond = checkExpr(unit, &ctx, *ifst->condition, primitiveType(ResolvedType::BOOL));
		if (!cond) return false;
		if (!typesEqual(cond, primitiveType(ResolvedType::BOOL))) {
			errorLine(ifst->token, "If condition must be of type bool, got ", cond);
			return false;
		}

		// Detect `x != null` / `x == null` to narrow x inside the respective branch.
		ls_string_view narrowed_name = {};
		ResolvedType* narrowed_type = nullptr;
		bool narrowed_is_immutable = false;
		bool narrow_in_true = false;
		if (ifst->condition && ifst->condition->kind == Expression::BINARY) {
			BinaryExpression* bin = static_cast<BinaryExpression*>(ifst->condition);
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
						} else if (id->symbol) {
							narrowed_is_immutable = id->symbol->storage != Symbol::VARIABLE;
						}
						narrow_in_true = (bin->op == Token::BANG_EQUAL);
					}
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
				bool ok = checkStatement(unit, ctx, branch, return_type, {});
				popScope(ctx);
				return ok;
			}
			return checkStatement(unit, ctx, branch, return_type, {});
		};

		if (!checkBranchWithNarrowing(ifst->body, narrow_in_true)) return false;
		if (!checkBranchWithNarrowing(ifst->else_branch, !narrow_in_true)) return false;
		return true;
	}

	bool checkForStatement(Unit& unit, FunctionCheckContext& ctx, ForStatement* fs, ResolvedType* return_type, ls_string_view pending_label) {
		ResolvedType* begin_type = checkExpr(unit, &ctx, *fs->begin, primitiveType(ResolvedType::I32));
		ResolvedType* end_type = checkExpr(unit, &ctx, *fs->end, begin_type ? begin_type : primitiveType(ResolvedType::I32));
		if (!begin_type || !end_type || !isIntegerType(begin_type) || !isIntegerType(end_type)) {
			errorLine(fs->token, "For loop bounds must be of integer type, got ", begin_type, " and ", end_type);
			return false;
		}
		if (!typesEqual(begin_type, end_type)) {
			// TODO error msg
			return false;
		}

		pushScope(ctx);
		SemanticLocalBinding& binding = ctx.locals.emplace_back();
		binding.name = fs->loop_var;
		binding.type = begin_type;
		binding.is_immutable = true;
		ctx.loop_labels.push(pending_label);
		bool ok = checkStatement(unit, ctx, fs->body, return_type, {});
		ctx.loop_labels.pop_back();
		popScope(ctx);
		return ok;
	}

	bool checkLabelStatement(Unit& unit, FunctionCheckContext& ctx, LabelStatement* label, ResolvedType* return_type) {
		for (i32 i = (i32)ctx.label_names.size() - 1; i >= 0; --i) {
			if (equalStrings(ctx.label_names[(u32)i], label->name)) {
				errorLine(label->token, "Label ", label->name, " already declared in this function"); // TODO isn't this already caught below?
				return false;
			}
		}
		const bool labeled_loop = label->statement && (label->statement->kind == Statement::WHILE || label->statement->kind == Statement::FOR);
		if (labeled_loop) {
			// Active labels catch lexical duplicates. Keep a second function-wide
			// registry because reusing a name for a different loop construct is
			// ambiguous to later control-flow lowering, while sequential loops of
			// the same construct intentionally reuse labels.
			bool known_label = false;
			for (u32 i = 0; i < ctx.declared_loop_labels.size(); ++i) {
				if (!equalStrings(ctx.declared_loop_labels[i], label->name)) continue;
				if (ctx.declared_loop_kinds[i] != label->statement->kind) {
					errorLine(label->token, "Label ", label->name, " already declared for a different loop construct");
					return false;
				}
				known_label = true;
				break;
			}
			if (!known_label) {
				ctx.declared_loop_labels.push(label->name);
				ctx.declared_loop_kinds.push(label->statement->kind);
			}
		}
		ctx.label_names.push(label->name);
		const bool ok = checkStatement(unit, ctx, label->statement, return_type, labeled_loop ? label->name : ls_string_view{});
		ctx.label_names.pop_back();
		return ok;
	}

	bool checkMatchStatement(Unit& unit, FunctionCheckContext& ctx, MatchStatement* ms, ResolvedType* return_type) {
		ResolvedType* subject = checkExpr(unit, &ctx, *ms->subject, nullptr);
		if (!subject) return false;
		// Pin an untyped subject (e.g. `match 5 { ... }`) so the arm patterns get a concrete
		// type to match against.
		if (isUntypedNumeric(subject)) {
			subject = materializeUntyped(*ms->subject, nullptr);
			if (!subject) return false;
		}

		// Subject must be a scalar numeric type, enum, or string.
		const bool subject_is_numeric = isNumericType(subject);
		const bool subject_is_enum = subject->kind == ResolvedType::ENUM;
		const bool subject_is_string = subject->kind == ResolvedType::STRING;
		if (!subject_is_numeric && !subject_is_enum && !subject_is_string) {
			errorLine(ms->token, "Match statement subject must be a numeric type, enum, or string, got ", subject);
			return false;
		}

		bool has_fallback = false;
		// Track covered enum members for exhaustiveness checking.
		const EnumResolvedType* subject_enum = subject_is_enum ? static_cast<const EnumResolvedType*>(subject) : nullptr;
		ExpArray<bool> covered_enum_members(unit.arena);
		if (subject_enum) covered_enum_members.resize(subject_enum->decl->members.size(), false);
		u32 covered_enum_count = 0;

		for (MatchArm& arm : ms->arms) {
			if (arm.is_fallback) {
				if (has_fallback) {
					// TODO error msg
					return false;
				}
				has_fallback = true;
			}
			for (MatchPattern& pattern : arm.patterns) {
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
					for (u32 i = 0; i < (u32)subject_enum->decl->members.size(); ++i) {
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
			if (!checkStatement(unit, ctx, arm.body, return_type, {})) return false;
		}

		// Enum match must cover all variants or have a fallback.
		if (subject_enum && !has_fallback) {
			if (covered_enum_count != (u32)subject_enum->decl->members.size()) {
				errorLine(ms->token, "Match statement on enum is not exhaustive");
				return false;
			}
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
					// TODO error msg
					return false;
				}
				if (return_type->kind == ResolvedType::VOID) {
					if (ret->expression) {
						// TODO error msg
						return false;
					}
					return true;
				}
				if (!ret->expression) {
					// TODO error msg
					return false;
				}
				ResolvedType* expr_type = checkExpr(unit, &ctx, *ret->expression, return_type);
				if (!expr_type) return false;
				if (isUntypedInt(expr_type)) {
					expr_type = materializeUntyped(*ret->expression, return_type);
					if (!expr_type) return false;
				}
				if (!canImplicitlyConvert(expr_type, return_type)) {
					errorLine(ret->token, "Return expression type ", expr_type, " does not match function return type ", return_type);
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
			case Statement::FOR: {
				return checkForStatement(unit, ctx, static_cast<ForStatement*>(st), return_type, pending_label);
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
			case Statement::VAR_DECL: return checkVarDeclStatement(unit, ctx, static_cast<VarDeclStatement*>(st));
			case Statement::ASSIGN: return checkAssignStatement(unit, ctx, static_cast<AssignStatement*>(st));
			case Statement::IF: return checkIfStatement(unit, ctx, static_cast<IfStatement*>(st), return_type);
			case Statement::LABEL: return checkLabelStatement(unit, ctx, static_cast<LabelStatement*>(st), return_type);
			case Statement::MATCH: return checkMatchStatement(unit, ctx, static_cast<MatchStatement*>(st), return_type);
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

	bool resolveImports(ls_import_resolver_fn import_resolver, void* import_resolver_userdata) {
		for (u32 unit_index = 0; unit_index < module.units.size(); ++unit_index) {
			Unit& unit = module.units[unit_index];
			if (!resolveImportsForUnit(unit, import_resolver, import_resolver_userdata)) return false;
		}
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
				if (!fn.comptime_params.empty()) continue;
				FunctionResolvedType* fn_type = buildFunctionType(unit, fn, false);
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
			for (TemplateFunctionInstance& instance : unit.template_function_instances) {
				Symbol& sym = unit.symbols.emplace_back();
				sym.name = instance.name;
				sym.storage = Symbol::COMPTIME;
				sym.check_state = Symbol::CHECKED;
				sym.expression = instance.instance;
				sym.resolved_type = instance.type;
			}
		}
		return LS_RESULT_OK;
	}

	ls_result checkSymbol(Unit& unit, Symbol& sym) {
		if (sym.check_state == Symbol::CHECKED) return LS_RESULT_OK;
		if (sym.check_state == Symbol::FAILED) return LS_RESULT_FAILURE;
		auto fail = [&]() -> ls_result {
			sym.check_state = Symbol::FAILED;
			return LS_RESULT_FAILURE;
		};

		if (sym.storage == Symbol::IMPORT) {
			sym.check_state = Symbol::CHECKED;
			return LS_RESULT_OK;
		}

		if (sym.storage == Symbol::COMPTIME && isPrimitiveShadowName(sym.name)) {
			errorLine(sym.token, "Can not shadow primitive type: ", sym.name);
			return fail();
		}

		if (sym.check_state == Symbol::CHECKING) {
			if (isProvisionalSymbolVisible(sym)) return LS_RESULT_OK;
			errorLine(sym.token, "Cyclic definition: ", sym.name);
			return fail();
		}

		sym.check_state = Symbol::CHECKING;

		if (sym.storage == Symbol::COMPTIME && sym.expression) {
			if (checkComptimeSymbol(unit, sym) == LS_RESULT_FAILURE) return fail();
		} else {
			if (checkRuntimeSymbol(unit, sym) == LS_RESULT_FAILURE) return fail();
		}

		sym.check_state = Symbol::CHECKED;
		return LS_RESULT_OK;
	}

}; // struct Checker

ls_result ls_module_typecheck(ls_module* module) {
	if (!module) return LS_RESULT_FAILURE;
	return Checker(*module).typecheck();
}

ls_result ls_module_compile(ls_module* module, ls_string_view source, ls_string_view source_name, ls_import_resolver_fn import_resolver, void* import_resolver_userdata) {
	if (!module) return LS_RESULT_FAILURE;
	Checker checker(*module);
	if (ls_module_parse(module, source, source_name) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	if (!checker.resolveImports(import_resolver, import_resolver_userdata)) return LS_RESULT_FAILURE;
	if (ls_module_typecheck(module) == LS_RESULT_FAILURE) return LS_RESULT_FAILURE;
	return LS_RESULT_OK;
}
