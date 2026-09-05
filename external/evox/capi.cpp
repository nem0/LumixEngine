#include "capi.h"
#include "bytecode.h"
#include "compiler.h"
#include "ir.h"

#include <stdlib.h>
#include <string.h>

const ex_type* ex_type_from_any(const ex_runtime* runtime, const void* value) {
	if (!runtime || !value) return nullptr;
	u32 type_index;
	memcpy(&type_index, (const u8*)value + 8, sizeof(type_index));
	return ex_bytecode_type(runtime->bytecode, type_index);
}

ex_module* ex_module_create(ex_host* host) {
	if (!host || !host->arena.allocate) return nullptr;
	return new ex_module(host);
}

void ex_module_destroy(ex_module* module) {
	delete module;
}

int ex_module_get_unit_count(ex_module* module) {
	return module ? module->units.size() : 0;
}

ex_unit* ex_module_get_unit(ex_module* module, int index) {
	if (!module || index < 0 || index >= module->units.size()) return nullptr;
	return (ex_unit*)&module->units[index];
}

ex_string_view ex_unit_get_path(ex_unit* unit) {
	return unit ? ((Unit*)unit)->path : ex_string_view{};
}

int ex_unit_get_import_count(ex_unit* unit) {
	return unit ? ((Unit*)unit)->imports.size() : 0;
}

ex_string_view ex_unit_get_import_path(ex_unit* unit, int index) {
	Unit* impl = (Unit*)unit;
	if (!impl || index < 0 || index >= impl->imports.size()) return {};
	return impl->imports[index].path;
}

int ex_unit_get_native_function_count(ex_unit* unit) {
	return unit ? ((Unit*)unit)->native_symbols.size() : 0;
}

ex_string_view ex_unit_get_native_function_name(ex_unit* unit, int index) {
	Unit* impl = (Unit*)unit;
	if (!impl || index < 0 || index >= impl->native_symbols.size()) return {};
	return impl->native_symbols[index]->name;
}

namespace {

struct DefinitionQuery {
	ex_module& module;
	ex_string_view source_name;
	u32 line;
	u32 column;
	ex_definition_location result = {};
	bool found = false;

	bool same(ex_string_view a, ex_string_view b) const {
		return a.length == b.length && (a.length == 0 || memcmp(a.begin, b.begin, (size_t)a.length) == 0);
	}

	bool covers(const Token& token) const {
		if (token.src_loc == EX_INVALID_SOURCE_LOC || token.src_loc >= (u32)module.src_locs.entries.size()) return false;
		const SourceLocTable::Entry& l = module.src_locs.entries[token.src_loc];
		if (!same(l.source_name, source_name) || l.line != line + 1) return false;

		u32 start = l.column ? l.column - 1 : 0;
		u32 length = token.value.length > 0 ? (u32)token.value.length : 1;
		return column >= start && column <= start + length;
	}

	void set(const Token& use, const Token& declaration, u32 length) {
		if (!covers(use) || declaration.src_loc == EX_INVALID_SOURCE_LOC || declaration.src_loc >= (u32)module.src_locs.entries.size()) return;

		const SourceLocTable::Entry& l = module.src_locs.entries[declaration.src_loc];
		result = {l.source_name, l.line ? l.line - 1 : 0, l.column ? l.column - 1 : 0, length};
		found = true;
	}

	void declaration(Symbol* symbol, const Token& use) {
		if (symbol) set(use, symbol->token, (u32)symbol->name.length);
	}

	Symbol* owner(FunctionExpression* fn) {
		if (!fn) return nullptr;
		for (Unit& unit : module.units) {
			for (Symbol& s : unit.symbols) {
				if (s.expression == fn) return &s;
			}
		}
		return nullptr;
	}
	
	void visitExpression(Expression* e) {
		if (!e) return;

		switch (e->kind) {
			case Expression::IDENTIFIER: {
				auto* x = static_cast<IdentifierExpression*>(e);
				if (x->symbol)
					declaration(x->symbol, e->token);
				else if (x->declaration_token)
					set(e->token, *x->declaration_token, (u32)x->declaration_token->value.length);
				else if (x->resolved_fn)
					declaration(owner(x->resolved_fn), e->token);
				break;
			}
			case Expression::MEMBER: {
				auto* x = static_cast<MemberExpression*>(e);
				if (x->resolved_symbol) set(x->name, x->resolved_symbol->token, (u32)x->resolved_symbol->name.length);
				if (x->resolved_fn && !x->resolved_symbol) declaration(owner(x->resolved_fn), x->name);
				if (x->enum_member_index >= 0 && x->expression && x->expression->resolved_type) {
					ResolvedType* type = x->expression->resolved_type;
					if (type->kind == ResolvedTypeKind::META) type = static_cast<MetaType*>(type)->inner;
					if (type && type->kind == ResolvedTypeKind::ENUM) {
						auto* en = static_cast<EnumResolvedType*>(type);
						if (x->enum_member_index < en->decl->members.size()) {
							const EnumMember& member = en->decl->members[x->enum_member_index];
							set(x->name, member.name, (u32)member.name.value.length);
						}
					}
				}
				if (x->expression && x->expression->kind == Expression::IDENTIFIER) {
					auto* base = static_cast<IdentifierExpression*>(x->expression);
					for (Unit& u : module.units)
						for (Import& imp : u.imports)
							if (same(imp.alias, base->name)) {
								if (!base->symbol) {
									for (Symbol& s : u.symbols) {
										if (s.kind == Symbol::IMPORT && same(s.name, base->name)) declaration(&s, base->token);
									}
								}
								if (!x->resolved_symbol && imp.unit) {
									for (Symbol& s : imp.unit->symbols) {
										if (same(s.name, x->name.value)) {
											declaration(&s, x->name);
										}
									}
								}
							}
				}
				if (x->struct_field_index >= 0 && x->expression && x->expression->resolved_type) {
					ResolvedType* t = x->expression->resolved_type;
					while (t && (t->kind == ResolvedTypeKind::POINTER || t->kind == ResolvedTypeKind::NULLABLE)) {
						t = t->kind == ResolvedTypeKind::POINTER ? static_cast<PointerResolvedType*>(t)->inner : static_cast<NullableResolvedType*>(t)->inner;
					}
					if (t && t->kind == ResolvedTypeKind::STRUCT) {
						auto* st = static_cast<StructResolvedType*>(t);
						if (x->struct_field_index < st->decl->fields.size()) {
							set(x->name, st->decl->fields[x->struct_field_index].name, (u32)st->decl->fields[x->struct_field_index].name.value.length);
						}
					}
				}
				visitExpression(x->expression);
				break;
			}
			case Expression::CALL: {
				auto* x = static_cast<CallExpression*>(e);
				if (x->callee && x->callee->kind == Expression::MEMBER && x->resolved_fn) {
					auto* m = static_cast<MemberExpression*>(x->callee);
					declaration(owner(x->resolved_fn), m->name);
				}
				visitExpression(x->callee);
				for (Expression* a : x->args) visitExpression(a);
				break;
			}
			case Expression::PANIC: visitExpression(static_cast<PanicExpression*>(e)->message); break;
			case Expression::UNARY: visitExpression(static_cast<UnaryExpression*>(e)->expression); break;
			case Expression::BINARY: {
				auto* x = static_cast<BinaryExpression*>(e);
				visitExpression(x->lhs);
				visitExpression(x->rhs);
				break;
			}
			case Expression::CAST: {
				auto* x = static_cast<CastExpression*>(e);
				visitExpression(x->expression);
				visitExpression(x->type_expr);
				break;
			}
			case Expression::BRACKET: {
				auto* x = static_cast<BracketExpression*>(e);
				if (x->struct_field_name.length > 0 && x->base && x->base->resolved_type && !x->args.empty()) {
					ResolvedType* t = x->base->resolved_type;
					while (t && (t->kind == ResolvedTypeKind::POINTER || t->kind == ResolvedTypeKind::NULLABLE)) {
						t = t->kind == ResolvedTypeKind::POINTER ? static_cast<PointerResolvedType*>(t)->inner : static_cast<NullableResolvedType*>(t)->inner;
					}
					if (t && t->kind == ResolvedTypeKind::STRUCT) {
						auto* st = static_cast<StructResolvedType*>(t);
						for (i32 i = 0; i < st->decl->fields.size(); ++i) {
							if (!same(st->decl->fields[i].name.value, x->struct_field_name)) continue;
							set(x->args[0]->token, st->decl->fields[i].name, (u32)st->decl->fields[i].name.value.length);
							break;
						}
					}
				}
				visitExpression(x->base);
				for (Expression* a : x->args) visitExpression(a);
				break;
			}
			case Expression::SLICE: {
				auto* x = static_cast<SliceExpression*>(e);
				visitExpression(x->base);
				visitExpression(x->begin);
				visitExpression(x->end);
				break;
			}
			case Expression::STRUCT_LITERAL: {
				auto* x = static_cast<StructLiteralExpression*>(e);
				visitExpression(x->type);
				for (Expression* a : x->values) visitExpression(a);
				break;
			}
			case Expression::ARRAY_LITERAL:
				for (Expression* a : static_cast<ArrayLiteralExpression*>(e)->values) visitExpression(a);
				break;
			case Expression::STRUCT: {
				auto* x = static_cast<StructExpression*>(e);
				visitAttributes(x->attributes);
				for (StructFieldDecl& f : x->fields) {
					set(f.name, f.name, (u32)f.name.value.length);
					visitAttributes(f.attributes);
					visitExpression(f.type_expr);
				}
				break;
			}
			case Expression::ENUM: {
				auto* x = static_cast<EnumExpression*>(e);
				for (EnumMember& m : x->members) {
					set(m.name, m.name, (u32)m.name.value.length);
					visitExpression(m.value);
				}
				break;
			}
			case Expression::TYPE_MEMBER: visitExpression(static_cast<TypeMemberExpression*>(e)->expression); break;
			case Expression::FUNCTION_TYPE: {
				auto* x = static_cast<FunctionTypeExpression*>(e);
				for (FunctionTypeParam& p : x->params) visitExpression(p.type_expr);
				visitExpression(x->return_type);
				break;
			}
			case Expression::UNION_TYPE: {
				auto* x = static_cast<UnionTypeExpression*>(e);
				for (Expression* type : x->members) visitExpression(type);
				break;
			}
			case Expression::FUNCTION: visitFunction(static_cast<FunctionExpression*>(e)); break;
			case Expression::TYPEOF: visitExpression(static_cast<TypeofExpression*>(e)->operand); break;
			case Expression::SIZEOF: visitExpression(static_cast<SizeofExpression*>(e)->type_expr); break;
			case Expression::ARRAY_TYPE: {
				auto* x = static_cast<ArrayTypeExpression*>(e);
				visitExpression(x->size);
				visitExpression(x->element_type);
				break;
			}
			case Expression::SLICE_TYPE: visitExpression(static_cast<SliceTypeExpression*>(e)->element_type); break;
			case Expression::NULLABLE_TYPE: visitExpression(static_cast<NullableTypeExpression*>(e)->inner); break;
			case Expression::POINTER_TYPE: visitExpression(static_cast<PointerTypeExpression*>(e)->inner); break;
			case Expression::DEREFERENCE: visitExpression(static_cast<DereferenceExpression*>(e)->subject); break;
			case Expression::ADDRESSOF: visitExpression(static_cast<AddressOfExpression*>(e)->subject); break;
			case Expression::TERNARY: {
				auto* x = static_cast<TernaryExpression*>(e);
				visitExpression(x->condition);
				visitExpression(x->true_expr);
				visitExpression(x->false_expr);
				break;
			}
			default: break;
		}
	}
	void visitAttributes(ExpArray<Attribute>* attributes) {
		if (!attributes) return;

		for (Attribute& a : *attributes) {
			visitExpression(a.type);
			visitExpression(a.value);
		}
	}

	void visitFunction(FunctionExpression* fn) {
		if (!fn) return;

		for (FunctionParam& p : fn->params) {
			set(p.name, p.name, (u32)p.name.value.length);
			visitExpression(p.type_expr);
		}
		visitExpression(fn->return_type);
		visitStatement(fn->body);
	}

	void visitStatement(Statement* s) {
		if (!s) return;
		switch (s->kind) {
			case Statement::BLOCK:
				for (Statement* x : static_cast<BlockStatement*>(s)->statements) visitStatement(x);
				break;
			case Statement::EXPRESSION: visitExpression(static_cast<ExpressionStatement*>(s)->expression); break;
			case Statement::RETURN: visitExpression(static_cast<ReturnStatement*>(s)->expression); break;
			case Statement::VAR_DECL: {
				auto* v = static_cast<VarDeclStatement*>(s);
				set(v->name_token, v->name_token, (u32)v->name_token.value.length);
				visitExpression(v->type_expr);
				visitExpression(v->expression);
				break;
			}
			case Statement::ASSIGN: {
				auto* a = static_cast<AssignStatement*>(s);
				visitExpression(a->lhs);
				visitExpression(a->rhs);
				break;
			}
			case Statement::IF: {
				auto* x = static_cast<IfStatement*>(s);
				visitExpression(x->condition);
				visitStatement(x->body);
				visitStatement(x->else_branch);
				break;
			}
			case Statement::MATCH: {
				auto* x = static_cast<MatchStatement*>(s);
				visitExpression(x->subject);
				for (MatchArm& arm : x->arms) {
					for (MatchPattern& p : arm.patterns) {
						visitExpression(p.begin);
						visitExpression(p.end);
					}
					visitStatement(arm.body);
				}
				break;
			}
			case Statement::WHILE: {
				auto* x = static_cast<WhileStatement*>(s);
				visitExpression(x->condition);
				visitStatement(x->body);
				break;
			}
			case Statement::FOR: {
				auto* x = static_cast<ForStatement*>(s);
				if (x->key_token.src_loc != EX_INVALID_SOURCE_LOC) set(x->key_token, x->key_token, (u32)x->key_var.length);
				set(x->value_token, x->value_token, (u32)x->value_var.length);
				visitExpression(x->begin);
				visitExpression(x->end);
				visitStatement(x->body);
				break;
			}
			case Statement::DEFER: visitStatement(static_cast<DeferStatement*>(s)->statement); break;
			case Statement::LABEL: visitStatement(static_cast<LabelStatement*>(s)->statement); break;
			default: break;
		}
	}
};

} // namespace

ex_result ex_module_definition_at(ex_module* module, ex_string_view source_name, u32 line, u32 column, ex_definition_location* out_location) {
	if (!module || !out_location) return EX_RESULT_FAILURE;
	*out_location = {};
	DefinitionQuery query{*module, source_name, line, column};
	for (Unit& unit : module->units) {
		if (!query.same(unit.path, source_name)) continue;
		for (Symbol& symbol : unit.symbols) {
			query.set(symbol.token, symbol.token, (u32)symbol.name.length);
			query.visitExpression(symbol.expression);
		}
	}
	if (!query.found) return EX_RESULT_FAILURE;
	*out_location = query.result;
	return EX_RESULT_OK;
}

ex_result ex_runtime_set_native_function_callback(ex_runtime* runtime, ex_unit* unit, int function_index, ex_native_fn callback) {
	Unit* impl = (Unit*)unit;
	if (!impl || function_index < 0 || function_index >= impl->native_symbols.size()) return EX_RESULT_FAILURE;
	FunctionExpression* fn = static_cast<FunctionExpression*>(impl->native_symbols[function_index]->expression);
	return ex_runtime_set_native_function_callback_by_bytecode_index(runtime, (int)fn->bytecode_index, callback);
}

int ex_module_get_function_count(ex_module* module) {
	if (!module) return 0;
	i32 count = 0;
	for (const Unit& unit : module->units) {
		for (const Symbol& sym : unit.symbols) {
			if (sym.expression && sym.expression->kind == Expression::FUNCTION) ++count;
		}
	}
	return count;
}

int ex_module_get_global_count(ex_module* module) {
	if (!module) return 0;
	i32 count = 0;
	for (const Unit& unit : module->units) {
		for (const Symbol& sym : unit.symbols) {
			if (sym.expression) ++count;
		}
	}
	return count;
}

void ex_bytecode_destroy(ex_bytecode* bytecode) {
	// TODO
}

u32 ex_bytecode_type_count(const ex_bytecode* bytecode) {
	return bytecode ? bytecode->type_info_count : 0;
}

const ex_type* ex_bytecode_type(const ex_bytecode* bytecode, u32 index) {
	if (!bytecode || index >= bytecode->type_info_count) return nullptr;
	return &bytecode->type_info[index];
}

u32 ex_type_attribute_count(const ex_type* type) {
	return type ? type->attribute_count : 0u;
}

ex_attribute ex_type_attribute_value(const ex_type* type, u32 attribute_index) {
	if (!type || !type->bytecode || attribute_index >= type->attribute_count) return {nullptr, nullptr};

	const ex_type_attribute_info& info = type->bytecode->type_attributes[type->first_attribute_index + attribute_index];
	if (info.type_index >= type->bytecode->type_info_count) return {nullptr, nullptr};

	return {(const ex_type*)&type->bytecode->type_info[info.type_index], info.value};
}

u32 ex_type_struct_field_attribute_count(const ex_type* type, u32 field_index) {
	if (!type || type->kind != EX_TYPE_STRUCT || !type->bytecode || field_index >= type->field_count) return 0u;

	return type->bytecode->type_fields[type->first_field_index + field_index].attribute_count;
}

ex_attribute ex_type_struct_field_attribute_value(const ex_type* type, u32 field_index, u32 attribute_index) {
	if (!type || type->kind != EX_TYPE_STRUCT || !type->bytecode || field_index >= type->field_count) return {nullptr, nullptr};

	const ex_type_field_info& field = type->bytecode->type_fields[type->first_field_index + field_index];
	if (attribute_index >= field.attribute_count) return {nullptr, nullptr};

	const ex_type_attribute_info& info = type->bytecode->type_attributes[field.first_attribute_index + attribute_index];
	if (info.type_index >= type->bytecode->type_info_count) return {nullptr, nullptr};

	return {(const ex_type*)&type->bytecode->type_info[info.type_index], info.value};
}
