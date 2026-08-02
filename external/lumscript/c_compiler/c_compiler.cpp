// Small C backend for the checked LumScript AST.
//
// This deliberately is not a second implementation of the bytecode compiler.
// It covers the scalar, structured-control-flow subset that is useful for
// embedding tests and reports failure for AST nodes that need a runtime
// representation (strings, aggregates, closures, etc.).

#include "c_compiler.h"

#include <stdio.h>

namespace {

struct CCompiler {
	ls_c_output_fn output = nullptr;
	void* output_user_data = nullptr;
	bool ok = true;
	bool emit_print = false;
	i32 indent = 0;
	const NullableResolvedType* emitted_nullable_types[128] = {};
	i32 emitted_nullable_type_count = 0;
	struct DeferredScope { const Statement* statements[64] = {}; i32 count = 0; } deferred_scopes[64];
	i32 deferred_scope_count = 0;
	i32 loop_defer_depths[64] = {};
	ls_string_view loop_labels[64] = {};
	u32 loop_ids[64] = {};
	i32 loop_count = 0;
	u32 next_loop_id = 0;
	ls_string_view pending_loop_label = {};
	u32 match_count = 0;
	const FunctionExpression* local_functions[128] = {};
	i32 local_function_count = 0;
	const FunctionExpression* operator_functions[128] = {};
	i32 operator_function_count = 0;
	struct DirectFunction { const FunctionExpression* function; ls_string_view name; i32 template_index; } direct_functions[256] = {};
	i32 direct_function_count = 0;
	const StructResolvedType* template_struct_types[256] = {};
	i32 template_struct_type_count = 0;
	struct NullableBinding { ls_string_view name; const NullableResolvedType* type; };
	struct NullableScope { NullableBinding bindings[64] = {}; i32 count = 0; } nullable_scopes[64];
	i32 nullable_scope_count = 0;
	struct RefScope { ls_string_view names[64] = {}; i32 count = 0; } ref_scopes[64];
	i32 ref_scope_count = 0;

	void text(ls_string_view value) { if (output) output(output_user_data, value); }
	void text(const char* value) { text(makeStringView(value)); }
	void integer(u64 value) { char buffer[32]; const int count = snprintf(buffer, sizeof(buffer), "%llu", value); text({buffer, buffer + count}); }
	void floating(double value) { char buffer[64]; const int count = snprintf(buffer, sizeof(buffer), "%.17g", value); text({buffer, buffer + count}); }
	void line() { text("\n"); }
	void pad() { for (i32 i = 0; i < indent; ++i) text("    "); }
	void unsupported() { ok = false; }
	void preamble() {
		text("#include <stdbool.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <stdio.h>\n#include <string.h>\n\n#ifdef _MSC_VER\n\t#define LS_ALIGNOF(T) __alignof(T)\n#else\n\t#define LS_ALIGNOF(T) __alignof__(T)\n#endif\n\ntypedef struct { void* data; int64_t length; } ls_slice;\n");
		// Floats compare element-wise: NaN is not equal to itself and +0.0 equals
		// -0.0 despite differing bits. Integral elements have no padding, so those
		// go through memcmp.
		text("static bool ls_slice_eq(ls_slice a, ls_slice b, int64_t element_size) {\n"
			 "\tif (a.length != b.length) return false;\n"
			 "\tif (a.length == 0) return true;\n"
			 "\tif (!a.data || !b.data) return false;\n"
			 "\tif (a.data == b.data) return true;\n"
			 "\treturn memcmp(a.data, b.data, (size_t)(a.length * element_size)) == 0;\n"
			 "}\n"
			 "#define LS_SLICE_EQ_FLOAT(NAME, T) \\\n"
			 "\tstatic bool NAME(ls_slice a, ls_slice b) { \\\n"
			 "\t\tif (a.length != b.length) return false; \\\n"
			 "\t\tif (a.length == 0) return true; \\\n"
			 "\t\tif (!a.data || !b.data) return false; \\\n"
			 "\t\tfor (int64_t i = 0; i < a.length; ++i) { \\\n"
			 "\t\t\tif (!(((const T*)a.data)[i] == ((const T*)b.data)[i])) return false; \\\n"
			 "\t\t} \\\n"
			 "\t\treturn true; \\\n"
			 "\t}\n"
			 "LS_SLICE_EQ_FLOAT(ls_slice_eq_f32, float)\n"
			 "LS_SLICE_EQ_FLOAT(ls_slice_eq_f64, double)\n"
			 "#undef LS_SLICE_EQ_FLOAT\n");
		if (emit_print) text("void print(ls_slice msg) { fwrite(msg.data, 1, (size_t)msg.length, stdout); }\n");
		line();
	}
	void enumMemberName(const EnumExpression& en, ls_string_view member) {
		text(en.cached_name); text("_"); text(member);
	}
	void structName(const StructResolvedType* value) {
		if (value->decl->comptime_params.empty()) { text(value->decl->cached_name); return; }
		for (i32 i = 0; i < template_struct_type_count; ++i) {
			if (template_struct_types[i] == value) { text(value->decl->cached_name); text("__lum_template_"); integer(i); return; }
		}
		unsupported();
	}
	void registerTemplateStruct(const StructResolvedType* value) {
		for (i32 i = 0; i < template_struct_type_count; ++i) if (template_struct_types[i] == value) return;
		if (template_struct_type_count == 256) { unsupported(); return; }
		template_struct_types[template_struct_type_count++] = value;
	}
	bool typeLabel(const ResolvedType* value) {
		if (!value) return false;
		switch (value->kind) {
			case ResolvedType::BOOL: text("bool"); return true;
			case ResolvedType::I8: text("i8"); return true; case ResolvedType::U8: case ResolvedType::BYTE: text("u8"); return true;
			case ResolvedType::I16: text("i16"); return true; case ResolvedType::U16: text("u16"); return true;
			case ResolvedType::I32: text("i32"); return true; case ResolvedType::U32: text("u32"); return true;
			case ResolvedType::I64: case ResolvedType::ISIZE: text("i64"); return true; case ResolvedType::U64: text("u64"); return true;
			case ResolvedType::F32: text("f32"); return true; case ResolvedType::F64: text("f64"); return true;
			case ResolvedType::CSTR: text("cstr"); return true;
			case ResolvedType::CPTR: text("ptr"); return true; case ResolvedType::SLICE: text("slice"); return true;
			case ResolvedType::ENUM: text(static_cast<const EnumResolvedType*>(value)->decl->cached_name); return true;
			case ResolvedType::STRUCT: structName(static_cast<const StructResolvedType*>(value)); return ok;
			default: return false;
		}
	}

	bool nullableName(const NullableResolvedType* value) {
		text("ls_nullable_");
		if (!typeLabel(value->inner)) { unsupported(); return false; }
		return true;
	}

	bool type(const ResolvedType* value) {
		if (!value) return false;
		switch (value->kind) {
			case ResolvedType::VOID: text("void"); return true;
			case ResolvedType::BOOL: text("bool"); return true;
			case ResolvedType::I8: text("int8_t"); return true;
			case ResolvedType::U8: case ResolvedType::BYTE: text("uint8_t"); return true;
			case ResolvedType::I16: text("int16_t"); return true;
			case ResolvedType::U16: text("uint16_t"); return true;
			case ResolvedType::I32: text("int32_t"); return true;
			case ResolvedType::U32: text("uint32_t"); return true;
			case ResolvedType::I64: case ResolvedType::ISIZE: text("int64_t"); return true;
			case ResolvedType::U64: text("uint64_t"); return true;
			case ResolvedType::F32: text("float"); return true;
			case ResolvedType::F64: text("double"); return true;
			case ResolvedType::CSTR: text("const char*"); return true;
			case ResolvedType::CPTR: text("void*"); return true;
			case ResolvedType::SLICE: text("ls_slice"); return true;
			case ResolvedType::NULLABLE: return nullableName(static_cast<const NullableResolvedType*>(value));
			case ResolvedType::ENUM: text(static_cast<const EnumResolvedType*>(value)->decl->cached_name); return true;
			case ResolvedType::STRUCT: structName(static_cast<const StructResolvedType*>(value)); return ok;
			default: unsupported(); return false;
		}
	}

	void nullableValue(const NullableResolvedType* type, const Expression* value) {
		text("("); if (!nullableName(type)) return; text("){ ");
		if (!value || value->kind == Expression::NULL_LITERAL || value->kind == Expression::UNDEFINED) text("false, { 0 }");
		else { text("true, "); expression(value); }
		text(" }");
	}

	void stringLiteral(ls_string_view value) {
		text("(ls_slice){ (void*)\"");
		for (const char* c = value.begin; c != value.end; ++c) {
			char escaped[5];
			const int count = snprintf(escaped, sizeof(escaped), "\\x%02X", (unsigned char)*c);
			text({escaped, escaped + count});
		}
		text("\", "); integer((u64)(value.end - value.begin)); text(" }");
	}

	void nullableDeclaration(const NullableResolvedType* value) {
		for (i32 i = 0; i < emitted_nullable_type_count; ++i) if (emitted_nullable_types[i] == value) return;
		if (emitted_nullable_type_count == 128) { unsupported(); return; }
		emitted_nullable_types[emitted_nullable_type_count++] = value;
		text("typedef struct { bool has_value; ");
		if (!type(value->inner)) return;
		text(" value; } ");
		nullableName(value);
		text(";"); line();
	}

	void pushNullableScope() {
		if (nullable_scope_count == 64) { unsupported(); return; }
		nullable_scopes[nullable_scope_count++].count = 0;
	}

	void bindNullable(ls_string_view name, const NullableResolvedType* type) {
		if (!nullable_scope_count || nullable_scopes[nullable_scope_count - 1].count == 64) { unsupported(); return; }
		NullableScope& scope = nullable_scopes[nullable_scope_count - 1];
		scope.bindings[scope.count++] = {name, type};
	}

	const NullableResolvedType* nullableBinding(ls_string_view name) const {
		for (i32 scope = nullable_scope_count - 1; scope >= 0; --scope) {
			for (i32 i = nullable_scopes[scope].count - 1; i >= 0; --i) if (equalStrings(nullable_scopes[scope].bindings[i].name, name)) return nullable_scopes[scope].bindings[i].type;
		}
		return nullptr;
	}

	const NullableResolvedType* identifierNullableType(const IdentifierExpression* identifier) const {
		if (identifier->symbol && identifier->symbol->resolved_type && identifier->symbol->resolved_type->kind == ResolvedType::NULLABLE) return static_cast<const NullableResolvedType*>(identifier->symbol->resolved_type);
		return nullableBinding(identifier->name);
	}

	void pushRefScope() {
		if (ref_scope_count == 64) { unsupported(); return; }
		ref_scopes[ref_scope_count++].count = 0;
	}

	void bindRef(ls_string_view name) {
		if (!ref_scope_count || ref_scopes[ref_scope_count - 1].count == 64) { unsupported(); return; }
		ref_scopes[ref_scope_count - 1].names[ref_scopes[ref_scope_count - 1].count++] = name;
	}

	bool isRefBinding(ls_string_view name) const {
		for (i32 scope = ref_scope_count - 1; scope >= 0; --scope) for (i32 i = ref_scopes[scope].count - 1; i >= 0; --i) if (equalStrings(ref_scopes[scope].names[i], name)) return true;
		return false;
	}

	void pushDeferredScope() {
		if (deferred_scope_count == 64) { unsupported(); return; }
		deferred_scopes[deferred_scope_count++].count = 0;
	}

	void deferStatement(const Statement* value) {
		if (!deferred_scope_count || deferred_scopes[deferred_scope_count - 1].count == 64) { unsupported(); return; }
		deferred_scopes[deferred_scope_count - 1].statements[deferred_scopes[deferred_scope_count - 1].count++] = value;
	}

	void emitDeferredScope(i32 scope) {
		for (i32 i = deferred_scopes[scope].count - 1; i >= 0; --i) {
			pad(); statement(deferred_scopes[scope].statements[i]); line();
		}
	}

	void emitDeferredScopes(i32 first_scope) {
		for (i32 scope = deferred_scope_count - 1; scope >= first_scope; --scope) emitDeferredScope(scope);
	}

	// C places array extents after a declarator name, while LumScript keeps
	// them in the type. Use this for fields, locals, and parameters.
	bool declaration(const ResolvedType* value, ls_string_view name) {
		if (value && value->kind == ResolvedType::FUNCTION) {
			const FunctionResolvedType* function = static_cast<const FunctionResolvedType*>(value);
			if (!type(function->return_type)) return false;
			text(" (*"); text(name); text(")(");
			for (i32 i = 0; i < function->params.size(); ++i) {
				if (i) text(", ");
				const bool is_ref = function->decl && i < function->decl->params.size() && function->decl->params[i].is_ref;
				if (!type(function->params[i].type)) return false;
				if (is_ref) text("*");
			}
			if (function->params.empty()) text("void");
			text(")"); return true;
		}
		const ResolvedType* element = value;
		while (element && element->kind == ResolvedType::ARRAY) element = static_cast<const ArrayResolvedType*>(element)->element_type;
		if (!type(element)) return false;
		text(" "); text(name);
		for (const ResolvedType* array = value; array && array->kind == ResolvedType::ARRAY; array = static_cast<const ArrayResolvedType*>(array)->element_type) {
			text("["); integer(static_cast<const ArrayResolvedType*>(array)->size); text("]");
		}
		return true;
	}

	bool refDeclaration(const ResolvedType* value, ls_string_view name) {
		if (!value || value->kind == ResolvedType::ARRAY || value->kind == ResolvedType::FUNCTION) { unsupported(); return false; }
		if (!type(value)) return false;
		text(" *"); text(name);
		return true;
	}

	bool typeExpression(const ResolvedType* value) {
		if (value && value->kind == ResolvedType::ARRAY) {
			const ArrayResolvedType* array = static_cast<const ArrayResolvedType*>(value);
			if (!typeExpression(array->element_type)) return false;
			text("["); integer(array->size); text("]");
			return true;
		}
		return type(value);
	}

	const ResolvedType* measuredType(const Expression* expression) const {
		if (!expression) return nullptr;
		const ResolvedType* value = expression->resolved_type;
		if (!value && expression->kind == Expression::IDENTIFIER) value = static_cast<const IdentifierExpression*>(expression)->symbol ? static_cast<const IdentifierExpression*>(expression)->symbol->resolved_type : nullptr;
		if (!value && expression->kind == Expression::RESOLVED_TYPE) value = expression->resolved_type;
		if (value && value->kind == ResolvedType::META) return static_cast<const MetaType*>(value)->inner;
		return value;
	}

	const char* op(Token::Type value) {
		switch (value) {
			case Token::PLUS: return "+"; case Token::MINUS: return "-";
			case Token::STAR: return "*"; case Token::SLASH: return "/";
			case Token::PERCENT: return "%"; case Token::EQUAL_EQUAL: return "==";
			case Token::BANG_EQUAL: return "!="; case Token::LT: return "<";
			case Token::LT_EQUAL: return "<="; case Token::GT: return ">";
			case Token::GT_EQUAL: return ">="; case Token::AND: return "&&";
			case Token::OR: return "||"; case Token::EQUAL: return "=";
			case Token::PLUS_EQUAL: return "+="; case Token::MINUS_EQUAL: return "-=";
			case Token::STAR_EQUAL: return "*="; case Token::SLASH_EQUAL: return "/=";
			default: unsupported(); return "";
		}
	}

	void expression(const Expression* value) {
		if (!value) { unsupported(); return; }
		switch (value->kind) {
			case Expression::IDENTIFIER: {
				const IdentifierExpression* identifier = static_cast<const IdentifierExpression*>(value);
				const NullableResolvedType* nullable = identifierNullableType(identifier);
				const bool is_ref = isRefBinding(identifier->name);
				if (is_ref) text("(*");
				text(identifier->name);
				if (is_ref) text(")");
				if (nullable && value->resolved_type == nullable->inner) text(".value");
				return;
			}
			case Expression::INT_LITERAL: integer(static_cast<const IntLiteralExpression*>(value)->value); return;
			case Expression::FLOAT_LITERAL: floating(static_cast<const FloatLiteralExpression*>(value)->value); return;
			case Expression::BOOL_LITERAL: text(static_cast<const BoolLiteralExpression*>(value)->value ? "true" : "false"); return;
			case Expression::STRING_LITERAL: stringLiteral(static_cast<const StringLiteralExpression*>(value)->value); return;
			case Expression::SIZEOF: {
				const SizeofExpression* size = static_cast<const SizeofExpression*>(value);
				const ResolvedType* measured = measuredType(size->type_expr);
				if (!measured) { unsupported(); return; }
				text(size->is_align ? "LS_ALIGNOF(" : "sizeof(");
				if (!typeExpression(measured)) return;
				text(")"); return;
			}
			case Expression::NULL_LITERAL: {
				if (value->resolved_type && value->resolved_type->kind == ResolvedType::SLICE) text("(ls_slice){ NULL, 0 }");
				else text("NULL");
				return;
			}
			case Expression::CAST: {
				const CastExpression* cast = static_cast<const CastExpression*>(value);
				text("("); if (!type(cast->resolved_type)) return; text(")"); expression(cast->expression); return;
			}
			case Expression::UNARY: {
				const UnaryExpression* unary = static_cast<const UnaryExpression*>(value);
				if (unary->op == Token::REF) { text("&("); expression(unary->expression); text(")"); return; }
				if (unary->resolved_fn) {
					const i32 index = operatorFunctionIndex(unary->resolved_fn);
					if (index < 0) { unsupported(); return; }
					operatorFunctionName(index); text("("); expression(unary->expression); text(")"); return;
				}
				if (unary->op == Token::NOT) text("!"); else text(op(unary->op));
				text("("); expression(unary->expression); text(")"); return;
			}
			case Expression::BINARY: {
				const BinaryExpression* binary = static_cast<const BinaryExpression*>(value);
				const Expression* nullable = nullptr;
				if (binary->lhs && binary->lhs->resolved_type && binary->lhs->resolved_type->kind == ResolvedType::NULLABLE && binary->rhs && binary->rhs->kind == Expression::NULL_LITERAL) nullable = binary->lhs;
				if (binary->rhs && binary->rhs->resolved_type && binary->rhs->resolved_type->kind == ResolvedType::NULLABLE && binary->lhs && binary->lhs->kind == Expression::NULL_LITERAL) nullable = binary->rhs;
				if (nullable && (binary->op == Token::EQUAL_EQUAL || binary->op == Token::BANG_EQUAL)) {
					text("("); expression(nullable); text(".has_value "); text(binary->op == Token::EQUAL_EQUAL ? "== false" : "!= false"); text(")"); return;
				}
				if ((binary->op == Token::EQUAL_EQUAL || binary->op == Token::BANG_EQUAL)
					&& binary->lhs && binary->lhs->resolved_type && binary->lhs->resolved_type->kind == ResolvedType::SLICE
					&& binary->rhs && binary->rhs->resolved_type && binary->rhs->resolved_type->kind == ResolvedType::SLICE)
				{
					// C cannot compare the ls_slice struct directly.
					const SliceResolvedType* slice_type = static_cast<const SliceResolvedType*>(binary->lhs->resolved_type);
					const bool is_float = slice_type->element_type
						&& (slice_type->element_type->kind == ResolvedType::F32 || slice_type->element_type->kind == ResolvedType::F64);
					text("(");
					if (binary->op == Token::BANG_EQUAL) text("!");
					text(is_float
						? (slice_type->element_type->kind == ResolvedType::F32 ? "ls_slice_eq_f32(" : "ls_slice_eq_f64(")
						: "ls_slice_eq(");
					expression(binary->lhs); text(", "); expression(binary->rhs);
					if (!is_float) { text(", "); integer(typeByteSize(*slice_type->element_type)); }
					text("))"); return;
				}
				if (binary->resolved_fn) {
					const i32 index = operatorFunctionIndex(binary->resolved_fn);
					if (index < 0) { unsupported(); return; }
					operatorFunctionName(index); text("("); expression(binary->lhs); text(", "); expression(binary->rhs); text(")"); return;
				}
				text("("); expression(binary->lhs); text(" "); text(op(binary->op)); text(" "); expression(binary->rhs); text(")"); return;
			}
			case Expression::CALL: {
				const CallExpression* call = static_cast<const CallExpression*>(value);
				if (call->callee && call->callee->kind == Expression::IDENTIFIER && call->args.size() == 1
					&& equalStrings(static_cast<const IdentifierExpression*>(call->callee)->name, makeStringView("length")))
				{
					const ResolvedType* argument_type = call->args[0]->resolved_type;
					if (argument_type && argument_type->kind == ResolvedType::ARRAY) {
						integer(static_cast<const ArrayResolvedType*>(argument_type)->size); return;
					}
					if (argument_type && argument_type->kind == ResolvedType::SLICE) {
						text("("); expression(call->args[0]); text(").length"); return;
					}
					unsupported(); return;
				}
				// The checker leaves a UFCS call as `receiver.method(args)` and
				// records the selected free function. C has no UFCS, so emit the
				// resolved target with the receiver prepended to its arguments.
				if (call->resolved_fn && call->callee && call->callee->kind == Expression::MEMBER) {
					const MemberExpression* member = static_cast<const MemberExpression*>(call->callee);
					const i32 function = directFunctionIndex(call->resolved_fn);
					if (!member->expression || function < 0) { unsupported(); return; }
					directFunctionName(function); text("(");
					if (member->expression->kind == Expression::STRING_LITERAL && call->resolved_fn->params[0].resolved_type->kind == ResolvedType::CSTR) { text("("); expression(member->expression); text(").data"); } else expression(member->expression);
					for (i32 i = 0; i < call->args.size(); ++i) { text(", "); if (call->args[i]->kind == Expression::STRING_LITERAL && call->resolved_fn->params[i + 1].resolved_type->kind == ResolvedType::CSTR) { text("("); expression(call->args[i]); text(").data"); } else expression(call->args[i]); }
					text(")"); return;
				}
				if (call->resolved_fn) {
					const i32 function = directFunctionIndex(call->resolved_fn);
					if (function < 0) { unsupported(); return; }
					directFunctionName(function); text("(");
					for (i32 i = 0; i < call->args.size(); ++i) { if (i) text(", "); if (call->args[i]->kind == Expression::STRING_LITERAL && call->resolved_fn->params[i].resolved_type->kind == ResolvedType::CSTR) { text("("); expression(call->args[i]); text(").data"); } else expression(call->args[i]); }
					text(")"); return;
				}
				expression(call->callee); text("(");
				const ResolvedType* callee_type = call->callee ? call->callee->resolved_type : nullptr;
				const FunctionExpression* callee_fn = nullptr;
				if (!callee_type && call->callee && call->callee->kind == Expression::IDENTIFIER) callee_type = static_cast<const IdentifierExpression*>(call->callee)->symbol->resolved_type;
				if ((!callee_type || callee_type->kind != ResolvedType::FUNCTION) && call->callee && call->callee->kind == Expression::IDENTIFIER) {
					const Symbol* symbol = static_cast<const IdentifierExpression*>(call->callee)->symbol;
					if (symbol && symbol->expression && symbol->expression->kind == Expression::FUNCTION) { callee_fn = static_cast<const FunctionExpression*>(symbol->expression); callee_type = callee_fn->resolved_type; }
				}
				const FunctionResolvedType* fn_type = callee_type && callee_type->kind == ResolvedType::FUNCTION ? static_cast<const FunctionResolvedType*>(callee_type) : nullptr;
				for (i32 i = 0; i < call->args.size(); ++i) { if (i) text(", "); if (((callee_fn && callee_fn->params[i].resolved_type && callee_fn->params[i].resolved_type->kind == ResolvedType::CSTR) || (fn_type && fn_type->params[i].type->kind == ResolvedType::CSTR)) && call->args[i]->kind == Expression::STRING_LITERAL) { text("("); expression(call->args[i]); text(").data"); } else expression(call->args[i]); }
				text(")"); return;
			}
			case Expression::FUNCTION: {
				const i32 index = localFunctionIndex(static_cast<const FunctionExpression*>(value));
				if (index < 0) { unsupported(); return; }
				localFunctionName(index); return;
			}
			case Expression::MEMBER: {
				const MemberExpression* member = static_cast<const MemberExpression*>(value);
				if (!member->expression) {
					if (!member->resolved_type || member->resolved_type->kind != ResolvedType::ENUM) { unsupported(); return; }
					enumMemberName(*static_cast<const EnumResolvedType*>(member->resolved_type)->decl, member->name);
					return;
				}
				const ResolvedType* base_type = member->expression->resolved_type;
				if (base_type && base_type->kind == ResolvedType::META) {
					const ResolvedType* inner = static_cast<const MetaType*>(base_type)->inner;
					if (inner && inner->kind == ResolvedType::ENUM) {
						enumMemberName(*static_cast<const EnumResolvedType*>(inner)->decl, member->name);
						return;
					}
				}
				expression(member->expression); text("."); text(member->name); return;
			}
			case Expression::STRUCT_LITERAL: {
				const StructLiteralExpression* literal = static_cast<const StructLiteralExpression*>(value);
				const ResolvedType* literal_type = literal->type ? literal->type->resolved_type : value->resolved_type;
				if (literal_type && literal_type->kind == ResolvedType::META) literal_type = static_cast<const MetaType*>(literal_type)->inner;
				if (!literal_type || literal_type->kind != ResolvedType::STRUCT) { unsupported(); return; }
				text("("); type(literal_type); text("){ ");
				for (i32 i = 0; i < literal->values.size(); ++i) { if (i) text(", "); expression(literal->values[i]); }
				text(" }"); return;
			}
			case Expression::BRACKET: {
				const BracketExpression* bracket = static_cast<const BracketExpression*>(value);
				if (!bracket->base || !bracket->base->resolved_type || bracket->args.size() != 1) { unsupported(); return; }
				if (!empty(bracket->struct_field_name)) {
					expression(bracket->base); text("."); text(bracket->struct_field_name); return;
				}
				if (bracket->base->resolved_type->kind == ResolvedType::ARRAY) {
					expression(bracket->base); text("["); expression(bracket->args[0]); text("]"); return;
				}
				if (bracket->base->resolved_type->kind != ResolvedType::SLICE) { unsupported(); return; }
				text("(("); if (!type(bracket->resolved_type)) return; text("*)("); expression(bracket->base); text(").data)["); expression(bracket->args[0]); text("]"); return;
			}
			case Expression::SLICE: {
				const SliceExpression* slice = static_cast<const SliceExpression*>(value);
				const ResolvedType* base_type = slice->base ? slice->base->resolved_type : nullptr;
				if (!base_type || (base_type->kind != ResolvedType::ARRAY && base_type->kind != ResolvedType::SLICE)) { unsupported(); return; }
				const ResolvedType* element = base_type->kind == ResolvedType::ARRAY
					? static_cast<const ArrayResolvedType*>(base_type)->element_type
					: static_cast<const SliceResolvedType*>(base_type)->element_type;
				text("(ls_slice){ ");
				if (base_type->kind == ResolvedType::ARRAY) {
					text("(void*)&("); expression(slice->base); text(")[");
					if (slice->begin) expression(slice->begin); else text("0");
					text("]");
				}
				else {
					text("(void*)(("); if (!type(element)) return; text("*)("); expression(slice->base); text(").data + ");
					if (slice->begin) expression(slice->begin); else text("0");
					text(")");
				}
				text(", ");
				if (slice->end) expression(slice->end);
				else if (base_type->kind == ResolvedType::ARRAY) integer(static_cast<const ArrayResolvedType*>(base_type)->size);
				else { text("("); expression(slice->base); text(").length"); }
				text(" - "); if (slice->begin) expression(slice->begin); else text("0");
				text(" }"); return;
			}
			default: unsupported(); return;
		}
	}

	void enumDeclaration(const Symbol& symbol, const EnumExpression& en) {
		text("typedef enum "); text(symbol.name); text(" { ");
		for (i32 i = 0; i < en.members.size(); ++i) {
			if (i) text(", ");
			enumMemberName(en, en.members[i].name);
			if (en.members[i].value) { text(" = "); expression(en.members[i].value); }
		}
		text(" } "); text(symbol.name); text(";"); line();
	}

	void structForwardDeclaration(const Symbol& symbol) {
		text("typedef struct "); text(symbol.name); text(" "); text(symbol.name); text(";"); line();
	}

	void structDeclaration(const Symbol& symbol, const StructExpression& st) {
		const StructResolvedType* struct_type = symbol.resolved_type && symbol.resolved_type->kind == ResolvedType::META
			? static_cast<const StructResolvedType*>(static_cast<const MetaType*>(symbol.resolved_type)->inner)
			: nullptr;
		if (!struct_type) { unsupported(); return; }
		text("struct "); text(symbol.name); text(" {"); line(); ++indent;
		for (i32 i = 0; i < st.fields.size(); ++i) {
			pad();
			ResolvedType* field_type = i < struct_type->field_types.size() ? struct_type->field_types[i] : st.fields[i].resolved_type;
			if (!declaration(field_type, st.fields[i].name)) return;
			text(";"); line();
		}
		--indent; text("};"); line();
	}

	void templateStructForwardDeclaration(const StructResolvedType& type) {
		text("typedef struct "); structName(&type); text(" "); structName(&type); text(";"); line();
	}

	void templateStructDeclaration(const StructExpression& st, const StructResolvedType& type) {
		text("struct "); structName(&type); text(" {"); line(); ++indent;
		for (i32 i = 0; i < st.fields.size(); ++i) {
			pad();
			if (i >= type.field_types.size() || !declaration(type.field_types[i], st.fields[i].name)) return;
			text(";"); line();
		}
		--indent; text("};"); line();
	}

	void globalDeclaration(const Symbol& symbol) {
		if (!symbolHasGlobalStorage(symbol) || !symbol.resolved_type) return;
		if (symbol.storage == Symbol::CONST) text("const ");
		if (!declaration(symbol.resolved_type, symbol.name)) return;
		if (symbol.expression && symbol.expression->kind != Expression::UNDEFINED) {
			text(" = ");
			if (symbol.resolved_type->kind == ResolvedType::NULLABLE) nullableValue(static_cast<const NullableResolvedType*>(symbol.resolved_type), symbol.expression);
			else if (symbol.resolved_type->kind == ResolvedType::CSTR && symbol.expression->kind == Expression::STRING_LITERAL) { text("("); expression(symbol.expression); text(").data"); }
			else expression(symbol.expression);
		}
		text(";"); line();
	}

	void matchTemporary(u32 index) { text("__lum_match_"); integer(index); }
	void localFunctionName(i32 index) { text("__lum_local_fn_"); integer(index); }
	void operatorFunctionName(i32 index) { text("__lum_operator_"); integer(index); }
	i32 localFunctionIndex(const FunctionExpression* function) const {
		for (i32 i = 0; i < local_function_count; ++i) if (local_functions[i] == function) return i;
		return -1;
	}
	i32 operatorFunctionIndex(const FunctionExpression* function) const {
		for (i32 i = 0; i < operator_function_count; ++i) if (operator_functions[i] == function) return i;
		return -1;
	}
	void registerOperatorFunction(const FunctionExpression* function) {
		if (operatorFunctionIndex(function) >= 0) return;
		if (operator_function_count == 128) { unsupported(); return; }
		operator_functions[operator_function_count++] = function;
	}
	void registerDirectFunction(const FunctionExpression* function, ls_string_view name, i32 template_index = -1) {
		for (i32 i = 0; i < direct_function_count; ++i) if (direct_functions[i].function == function) return;
		if (direct_function_count == 256) { unsupported(); return; }
		direct_functions[direct_function_count++] = {function, name, template_index};
	}
	i32 directFunctionIndex(const FunctionExpression* function) const {
		for (i32 i = 0; i < direct_function_count; ++i) if (direct_functions[i].function == function) return i;
		return -1;
	}
	void directFunctionName(i32 index) {
		ASSERT(index >= 0 && index < direct_function_count);
		const DirectFunction& function = direct_functions[index];
		text(function.name);
		if (function.template_index >= 0) { text("__lum_template_"); integer(function.template_index); }
	}
	void loopLabel(const char* kind, u32 id) { text("__lum_"); text(kind); text("_"); integer(id); }

	i32 findLoop(ls_string_view label) const {
		if (empty(label)) return loop_count - 1;
		for (i32 i = loop_count - 1; i >= 0; --i) if (equalStrings(loop_labels[i], label)) return i;
		return -1;
	}

	void matchStatement(const MatchStatement* match) {
		const u32 index = match_count++;
		text("{"); line(); ++indent;
		pad(); if (!type(match->subject->resolved_type)) return; text(" "); matchTemporary(index); text(" = "); expression(match->subject); text(";"); line();
		bool first_arm = true;
		for (const MatchArm& arm : match->arms) {
			pad();
			if (arm.is_fallback) text(first_arm ? "if (true) " : "else ");
			else {
				text(first_arm ? "if (" : "else if (");
				for (i32 i = 0; i < arm.patterns.size(); ++i) {
					if (i) text(" || ");
					const MatchPattern& pattern = arm.patterns[i];
					if (pattern.end) {
						text("("); matchTemporary(index); text(" >= "); expression(pattern.begin); text(" && "); matchTemporary(index); text(" <= "); expression(pattern.end); text(")");
					}
					else { text("("); matchTemporary(index); text(" == "); expression(pattern.begin); text(")"); }
				}
				text(") ");
			}
			statement(arm.body); line();
			first_arm = false;
		}
		--indent; pad(); text("}");
	}

	void statement(const Statement* value) {
		if (!value) { unsupported(); return; }
		switch (value->kind) {
			case Statement::BLOCK: {
				const BlockStatement* block = static_cast<const BlockStatement*>(value); text("{"); line(); ++indent; pushDeferredScope(); pushNullableScope();
				bool terminated = false;
				for (Statement* child : block->statements) {
					if (child->kind == Statement::DEFER) { deferStatement(static_cast<const DeferStatement*>(child)->statement); continue; }
					pad(); statement(child); line();
					if (child->kind == Statement::RETURN || child->kind == Statement::BREAK || child->kind == Statement::CONTINUE) { terminated = true; break; }
				}
				if (!terminated) emitDeferredScope(deferred_scope_count - 1); --deferred_scope_count; --nullable_scope_count;
				--indent; pad(); text("}"); return;
			}
			case Statement::EXPRESSION: expression(static_cast<const ExpressionStatement*>(value)->expression); text(";"); return;
			case Statement::RETURN: { const auto* ret = static_cast<const ReturnStatement*>(value); emitDeferredScopes(0); pad(); text("return"); if (ret->expression) { text(" "); expression(ret->expression); } text(";"); return; }
			case Statement::VAR_DECL: { const auto* var = static_cast<const VarDeclStatement*>(value); if (var->resolved_type->kind == ResolvedType::NULLABLE) { nullableDeclaration(static_cast<const NullableResolvedType*>(var->resolved_type)); pad(); } if (!declaration(var->resolved_type, var->name)) return; if (var->expression && var->expression->kind != Expression::UNDEFINED) { text(" = "); if (var->resolved_type->kind == ResolvedType::NULLABLE) nullableValue(static_cast<const NullableResolvedType*>(var->resolved_type), var->expression); else if (var->resolved_type->kind == ResolvedType::CSTR && var->expression->kind == Expression::STRING_LITERAL) { text("("); expression(var->expression); text(").data"); } else expression(var->expression); } text(";"); if (var->resolved_type->kind == ResolvedType::NULLABLE) bindNullable(var->name, static_cast<const NullableResolvedType*>(var->resolved_type)); return; }
			case Statement::ASSIGN: {
				const auto* assign = static_cast<const AssignStatement*>(value);
				if (assign->resolved_op_fn) {
					const i32 index = operatorFunctionIndex(assign->resolved_op_fn);
					if (index < 0) { unsupported(); return; }
					expression(assign->lhs); text(" = "); operatorFunctionName(index); text("("); expression(assign->lhs); text(", "); expression(assign->rhs); text(");"); return;
				}
				expression(assign->lhs); text(" "); text(op(assign->op)); text(" "); expression(assign->rhs); text(";"); return;
			}
			case Statement::IF: { const auto* ifs = static_cast<const IfStatement*>(value); text("if ("); expression(ifs->condition); text(") "); statement(ifs->body); if (ifs->else_branch) { text(" else "); statement(ifs->else_branch); } return; }
			case Statement::MATCH: matchStatement(static_cast<const MatchStatement*>(value)); return;
			case Statement::WHILE: {
				const auto* loop = static_cast<const WhileStatement*>(value); if (loop_count == 64) { unsupported(); return; }
				const i32 loop_index = loop_count++; const u32 id = next_loop_id++;
				loop_defer_depths[loop_index] = deferred_scope_count; loop_labels[loop_index] = pending_loop_label; loop_ids[loop_index] = id; pending_loop_label = {};
				text("while ("); expression(loop->condition); text(") {"); line(); ++indent; pad(); statement(loop->body); line(); pad(); loopLabel("continue", id); text(": ;"); line(); --indent; pad(); text("}"); line(); pad(); loopLabel("break", id); text(": ;"); --loop_count; return;
			}
			case Statement::FOR: {
				const auto* loop = static_cast<const ForStatement*>(value); if (loop_count == 64) { unsupported(); return; }
				const i32 loop_index = loop_count++; const u32 id = next_loop_id++;
				loop_defer_depths[loop_index] = deferred_scope_count; loop_labels[loop_index] = pending_loop_label; loop_ids[loop_index] = id; pending_loop_label = {};
				text("for ("); if (!type(loop->begin->resolved_type)) { --loop_count; return; } text(" "); text(loop->key_var); text(" = "); expression(loop->begin); text("; "); text(loop->key_var); text(" < "); expression(loop->end); text("; ++"); text(loop->key_var); text(") {"); line(); ++indent;
				pad(); statement(loop->body); line(); pad(); loopLabel("continue", id); text(": ;"); line(); --indent; pad(); text("}"); line(); pad(); loopLabel("break", id); text(": ;"); --loop_count; return;
			}
			case Statement::BREAK: { const auto* br = static_cast<const BreakStatement*>(value); const i32 loop = findLoop(br->label); if (loop < 0) { unsupported(); return; } emitDeferredScopes(loop_defer_depths[loop]); pad(); if (empty(br->label)) text("break;"); else { text("goto "); loopLabel("break", loop_ids[loop]); text(";"); } return; }
			case Statement::CONTINUE: { const auto* cont = static_cast<const ContinueStatement*>(value); const i32 loop = findLoop(cont->label); if (loop < 0) { unsupported(); return; } emitDeferredScopes(loop_defer_depths[loop]); pad(); if (empty(cont->label)) text("continue;"); else { text("goto "); loopLabel("continue", loop_ids[loop]); text(";"); } return; }
			case Statement::DEFER: deferStatement(static_cast<const DeferStatement*>(value)->statement); return;
			case Statement::LABEL: { const auto* label = static_cast<const LabelStatement*>(value); const ls_string_view previous = pending_loop_label; if (label->statement && (label->statement->kind == Statement::WHILE || label->statement->kind == Statement::FOR)) pending_loop_label = label->name; statement(label->statement); pending_loop_label = previous; return; }
			default: unsupported(); return;
		}
	}

	void function(const Symbol& symbol, const FunctionExpression& fn, bool is_declaration, i32 template_index = -1) {
		const auto* fn_type = static_cast<const FunctionResolvedType*>(fn.resolved_type);
		if (!fn_type) return;
		if (fn.is_extern) text("extern ");
		if (!type(fn_type->return_type)) return;
		text(" ");
		if (isOperatorSymbol(symbol.name)) {
			const i32 index = operatorFunctionIndex(&fn);
			if (index < 0) { unsupported(); return; }
			operatorFunctionName(index);
		}
		else {
			text(symbol.name);
			if (template_index >= 0) { text("__lum_template_"); integer(template_index); }
		}
		text("(");
		bool first = true;
		for (const FunctionParam& param : fn.params) {
			if (param.is_comptime) continue;
			if (!first) text(", ");
			if (param.is_ref ? !refDeclaration(param.resolved_type, param.name) : !declaration(param.resolved_type, param.name)) return;
			first = false;
		}
		if (first) text("void"); text(")");
		if (is_declaration || fn.is_extern) { text(";"); return; }
		pushNullableScope(); pushRefScope();
		for (const FunctionParam& param : fn.params) if (!param.is_comptime && param.resolved_type && param.resolved_type->kind == ResolvedType::NULLABLE) bindNullable(param.name, static_cast<const NullableResolvedType*>(param.resolved_type));
		for (const FunctionParam& param : fn.params) if (!param.is_comptime && param.is_ref) bindRef(param.name);
		text(" "); statement(fn.body); --nullable_scope_count; --ref_scope_count; line();
	}

	void localFunction(const FunctionExpression& fn, i32 index, bool is_declaration) {
		const auto* fn_type = static_cast<const FunctionResolvedType*>(fn.resolved_type);
		if (!fn_type || !type(fn_type->return_type)) return;
		text(" "); localFunctionName(index); text("(");
		bool first = true;
		for (const FunctionParam& param : fn.params) {
			if (param.is_comptime) continue;
			if (!first) text(", ");
			if (param.is_ref ? !refDeclaration(param.resolved_type, param.name) : !declaration(param.resolved_type, param.name)) return;
			first = false;
		}
		if (first) text("void"); text(")");
		if (is_declaration) { text(";"); return; }
		pushNullableScope(); pushRefScope();
		for (const FunctionParam& param : fn.params) if (!param.is_comptime && param.resolved_type && param.resolved_type->kind == ResolvedType::NULLABLE) bindNullable(param.name, static_cast<const NullableResolvedType*>(param.resolved_type));
		for (const FunctionParam& param : fn.params) if (!param.is_comptime && param.is_ref) bindRef(param.name);
		text(" "); statement(fn.body); --nullable_scope_count; --ref_scope_count; line();
	}

	void collectLocalFunction(const FunctionExpression* function) {
		if (localFunctionIndex(function) >= 0) return;
		if (local_function_count == 128) { unsupported(); return; }
		local_functions[local_function_count++] = function;
		collectLocalFunctions(function->body);
	}

	void collectLocalFunctions(const Expression* value) {
		if (!value) return;
		switch (value->kind) {
			case Expression::FUNCTION: collectLocalFunction(static_cast<const FunctionExpression*>(value)); return;
			case Expression::CALL: { const auto* call = static_cast<const CallExpression*>(value); collectLocalFunctions(call->callee); for (Expression* arg : call->args) collectLocalFunctions(arg); return; }
			case Expression::UNARY: collectLocalFunctions(static_cast<const UnaryExpression*>(value)->expression); return;
			case Expression::BINARY: { const auto* binary = static_cast<const BinaryExpression*>(value); collectLocalFunctions(binary->lhs); collectLocalFunctions(binary->rhs); return; }
			case Expression::CAST: collectLocalFunctions(static_cast<const CastExpression*>(value)->expression); return;
			case Expression::MEMBER: collectLocalFunctions(static_cast<const MemberExpression*>(value)->expression); return;
			case Expression::BRACKET: { const auto* bracket = static_cast<const BracketExpression*>(value); collectLocalFunctions(bracket->base); for (Expression* arg : bracket->args) collectLocalFunctions(arg); return; }
			case Expression::SLICE: { const auto* slice = static_cast<const SliceExpression*>(value); collectLocalFunctions(slice->base); collectLocalFunctions(slice->begin); collectLocalFunctions(slice->end); return; }
			case Expression::STRUCT_LITERAL: { const auto* literal = static_cast<const StructLiteralExpression*>(value); for (Expression* item : literal->values) collectLocalFunctions(item); return; }
			default: return;
		}
	}

	void collectLocalFunctions(const Statement* value) {
		if (!value) return;
		switch (value->kind) {
			case Statement::BLOCK: for (Statement* child : static_cast<const BlockStatement*>(value)->statements) collectLocalFunctions(child); return;
			case Statement::EXPRESSION: collectLocalFunctions(static_cast<const ExpressionStatement*>(value)->expression); return;
			case Statement::RETURN: collectLocalFunctions(static_cast<const ReturnStatement*>(value)->expression); return;
			case Statement::VAR_DECL: collectLocalFunctions(static_cast<const VarDeclStatement*>(value)->expression); return;
			case Statement::ASSIGN: { const auto* assign = static_cast<const AssignStatement*>(value); collectLocalFunctions(assign->lhs); collectLocalFunctions(assign->rhs); return; }
			case Statement::IF: { const auto* ifs = static_cast<const IfStatement*>(value); collectLocalFunctions(ifs->condition); collectLocalFunctions(ifs->body); collectLocalFunctions(ifs->else_branch); return; }
			case Statement::MATCH: { const auto* match = static_cast<const MatchStatement*>(value); collectLocalFunctions(match->subject); for (const MatchArm& arm : match->arms) { for (const MatchPattern& pattern : arm.patterns) { collectLocalFunctions(pattern.begin); collectLocalFunctions(pattern.end); } collectLocalFunctions(arm.body); } return; }
			case Statement::WHILE: { const auto* loop = static_cast<const WhileStatement*>(value); collectLocalFunctions(loop->condition); collectLocalFunctions(loop->body); return; }
			case Statement::FOR: { const auto* loop = static_cast<const ForStatement*>(value); collectLocalFunctions(loop->begin); collectLocalFunctions(loop->end); collectLocalFunctions(loop->body); return; }
			case Statement::DEFER: collectLocalFunctions(static_cast<const DeferStatement*>(value)->statement); return;
			case Statement::LABEL: collectLocalFunctions(static_cast<const LabelStatement*>(value)->statement); return;
			default: return;
		}
	}
};

static bool compileTypes(CCompiler& compiler, const Unit& unit) {
	for (const Symbol& symbol : unit.symbols) {
		if (symbol.expression && symbol.expression->kind == Expression::ENUM) compiler.enumDeclaration(symbol, *static_cast<const EnumExpression*>(symbol.expression));
	}
	for (const Symbol& symbol : unit.symbols) {
		if (!symbol.expression || symbol.expression->kind != Expression::STRUCT) continue;
		const StructExpression& st = *static_cast<const StructExpression*>(symbol.expression);
		if (st.comptime_params.empty()) compiler.structForwardDeclaration(symbol);
		else for (const TemplateStructInstance& instance : st.template_struct_instances) if (!instance.check_failed && instance.type) compiler.templateStructForwardDeclaration(*instance.type);
	}
	for (const Symbol& symbol : unit.symbols) {
		if (!symbol.expression || symbol.expression->kind != Expression::STRUCT) continue;
		const StructExpression& st = *static_cast<const StructExpression*>(symbol.expression);
		if (st.comptime_params.empty()) compiler.structDeclaration(symbol, st);
		else for (const TemplateStructInstance& instance : st.template_struct_instances) if (!instance.check_failed && instance.type) compiler.templateStructDeclaration(st, *instance.type);
	}
	for (const Symbol& symbol : unit.symbols) {
		if (symbol.resolved_type && symbol.resolved_type->kind == ResolvedType::NULLABLE) compiler.nullableDeclaration(static_cast<const NullableResolvedType*>(symbol.resolved_type));
		if (symbol.expression && symbol.expression->kind == Expression::FUNCTION) {
			const FunctionExpression* fn = static_cast<const FunctionExpression*>(symbol.expression);
			const FunctionResolvedType* fn_type = static_cast<const FunctionResolvedType*>(fn->resolved_type);
			if (fn_type && fn_type->return_type && fn_type->return_type->kind == ResolvedType::NULLABLE) compiler.nullableDeclaration(static_cast<const NullableResolvedType*>(fn_type->return_type));
			if (fn_type) for (const FunctionResolvedParam& param : fn_type->params) if (param.type && param.type->kind == ResolvedType::NULLABLE) compiler.nullableDeclaration(static_cast<const NullableResolvedType*>(param.type));
		}
	}
	return compiler.ok;
}

static void registerTemplateStructs(CCompiler& compiler, const Unit& unit) {
	for (const Symbol& symbol : unit.symbols) {
		if (!symbol.expression || symbol.expression->kind != Expression::STRUCT) continue;
		const StructExpression& st = *static_cast<const StructExpression*>(symbol.expression);
		for (const TemplateStructInstance& instance : st.template_struct_instances) if (!instance.check_failed && instance.type) compiler.registerTemplateStruct(instance.type);
	}
}

static void configureRuntime(CCompiler& compiler, const Unit& unit) {
	for (const Symbol& symbol : unit.symbols) {
		if (!symbol.expression || symbol.expression->kind != Expression::FUNCTION || !equalStrings(symbol.name, makeStringView("print"))) continue;
		const FunctionExpression& fn = *static_cast<const FunctionExpression*>(symbol.expression);
		const FunctionResolvedType* type = static_cast<const FunctionResolvedType*>(fn.resolved_type);
		if (fn.is_extern && type && type->return_type && type->return_type->kind == ResolvedType::VOID
			&& fn.params.size() == 1 && fn.params[0].resolved_type && fn.params[0].resolved_type->kind == ResolvedType::SLICE
			&& static_cast<SliceResolvedType*>(fn.params[0].resolved_type)->element_type->kind == ResolvedType::U8)
		{
			compiler.emit_print = true;
		}
	}
}

static bool compileFunctions(CCompiler& compiler, const Unit& unit) {
	const i32 first_local_function = compiler.local_function_count;
	for (const Symbol& symbol : unit.symbols) {
		if (!symbol.expression || symbol.expression->kind != Expression::FUNCTION || empty(symbol.name)) continue;
		const FunctionExpression* fn = static_cast<const FunctionExpression*>(symbol.expression);
		if (isOperatorSymbol(symbol.name)) compiler.registerOperatorFunction(fn);
		else {
			if (!fn->is_template) compiler.registerDirectFunction(fn, symbol.name);
			for (i32 i = 0; i < fn->template_function_instances.size(); ++i) {
				const TemplateFunctionInstance& instance = fn->template_function_instances[i];
				if (!instance.check_failed && instance.instance) compiler.registerDirectFunction(instance.instance, symbol.name, i);
			}
		}
	}
	for (const Symbol& symbol : unit.symbols) {
		if (!symbol.expression || symbol.expression->kind != Expression::FUNCTION || empty(symbol.name)) continue;
		const FunctionExpression* fn = static_cast<const FunctionExpression*>(symbol.expression);
		if (!fn->is_template) compiler.collectLocalFunctions(fn->body);
		for (const TemplateFunctionInstance& instance : fn->template_function_instances) if (!instance.check_failed && instance.instance) compiler.collectLocalFunctions(instance.instance->body);
	}
	for (i32 i = first_local_function; i < compiler.local_function_count; ++i) { compiler.localFunction(*compiler.local_functions[i], i, true); compiler.line(); }
	for (const Symbol& symbol : unit.symbols) {
		if (!symbol.expression || symbol.expression->kind != Expression::FUNCTION || empty(symbol.name)) continue;
		const FunctionExpression& fn = *static_cast<const FunctionExpression*>(symbol.expression);
		if (!fn.is_template) { compiler.function(symbol, fn, true); compiler.line(); }
		for (i32 i = 0; i < fn.template_function_instances.size(); ++i) {
			const TemplateFunctionInstance& instance = fn.template_function_instances[i];
			if (!instance.check_failed && instance.instance) { compiler.function(symbol, *instance.instance, true, i); compiler.line(); }
		}
	}
	for (i32 i = first_local_function; i < compiler.local_function_count; ++i) compiler.localFunction(*compiler.local_functions[i], i, false);
	for (const Symbol& symbol : unit.symbols) {
		if (!symbol.expression || symbol.expression->kind != Expression::FUNCTION || empty(symbol.name)) continue;
		const FunctionExpression& fn = *static_cast<const FunctionExpression*>(symbol.expression);
		if (!fn.is_template && !fn.is_extern) compiler.function(symbol, fn, false);
		for (i32 i = 0; i < fn.template_function_instances.size(); ++i) {
			const TemplateFunctionInstance& instance = fn.template_function_instances[i];
			if (!instance.check_failed && instance.instance && !instance.instance->is_extern) compiler.function(symbol, *instance.instance, false, i);
		}
	}
	return compiler.ok;
}

static bool compileGlobals(CCompiler& compiler, const Unit& unit) {
	for (const Symbol& symbol : unit.symbols) compiler.globalDeclaration(symbol);
	return compiler.ok;
}

} // namespace

// The emitted C is standalone for scalar code and starts with the headers it
// needs. Unsupported AST nodes make the function return false.
bool c_compile(const Unit& unit, ls_c_output_fn output, void* output_user_data) {
	CCompiler compiler;
	compiler.output = output;
	compiler.output_user_data = output_user_data;
	configureRuntime(compiler, unit);
	compiler.preamble();
	registerTemplateStructs(compiler, unit);
	if (!compileTypes(compiler, unit)) return false;
	if (!compileGlobals(compiler, unit)) return false;
	if (!compileFunctions(compiler, unit)) return false;
	return true;
}

bool c_compile(const ls_module& module, ls_c_output_fn output, void* output_user_data) {
	CCompiler compiler;
	compiler.output = output;
	compiler.output_user_data = output_user_data;
	for (const Unit& unit : module.units) configureRuntime(compiler, unit);
	compiler.preamble();
	for (const Unit& unit : module.units) registerTemplateStructs(compiler, unit);
	for (const Unit& unit : module.units) if (!compileTypes(compiler, unit)) return false;
	for (const Unit& unit : module.units) if (!compileGlobals(compiler, unit)) return false;
	// Register every direct target up front so UFCS can call functions declared
	// in another module unit as well.
	for (const Unit& unit : module.units) {
		for (const Symbol& symbol : unit.symbols) {
			if (!symbol.expression || symbol.expression->kind != Expression::FUNCTION || empty(symbol.name)) continue;
			const FunctionExpression* fn = static_cast<const FunctionExpression*>(symbol.expression);
			if (isOperatorSymbol(symbol.name)) compiler.registerOperatorFunction(fn);
			else {
				if (!fn->is_template) compiler.registerDirectFunction(fn, symbol.name);
				for (i32 i = 0; i < fn->template_function_instances.size(); ++i) {
					const TemplateFunctionInstance& instance = fn->template_function_instances[i];
					if (!instance.check_failed && instance.instance) compiler.registerDirectFunction(instance.instance, symbol.name, i);
				}
			}
		}
	}
	for (const Unit& unit : module.units) if (!compileFunctions(compiler, unit)) return false;
	return true;
}
