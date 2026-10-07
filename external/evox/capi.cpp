#include "capi.h"
#include "bytecode.h"
#include "compiler.h"
#include "ir.h"

#include <stdlib.h>
#include <string.h>
#include <vector>

const ex_type* ex_type_from_any(const ex_runtime* runtime, const void* value) {
	if (!runtime || !value) return nullptr;
	u32 type_index;
	memcpy(&type_index, (const u8*)value + 8, sizeof(type_index));
	return ex_bytecode_type(runtime->bytecode, type_index);
}

const ex_type* ex_primitive_type_from_kind(const ex_runtime* runtime, ex_type_kind kind) {
	switch (kind) {
		case EX_TYPE_VOID:
		case EX_TYPE_BOOL:
		case EX_TYPE_I8:
		case EX_TYPE_U8:
		case EX_TYPE_I16:
		case EX_TYPE_U16:
		case EX_TYPE_I32:
		case EX_TYPE_U32:
		case EX_TYPE_I64:
		case EX_TYPE_U64:
		case EX_TYPE_F32:
		case EX_TYPE_F64:
		case EX_TYPE_CPTR:
			break;
		default:
			return nullptr;
	}
	if (!runtime || !runtime->bytecode) return nullptr;
	for (u32 i = 0; i < runtime->bytecode->type_info_count; ++i) {
		const ex_type* type = &runtime->bytecode->type_info[i];
		if (type->kind == kind) return type;
	}
	return nullptr;
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
	u32 unit_index;
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
		if (l.unit_index != unit_index || l.line != line + 1) return false;

		u32 start = l.column ? l.column - 1 : 0;
		u32 length = token.value.length > 0 ? (u32)token.value.length : 1;
		return column >= start && column <= start + length;
	}

	void set(const Token& use, const Token& declaration, u32 length) {
		if (!covers(use) || declaration.src_loc == EX_INVALID_SOURCE_LOC || declaration.src_loc >= (u32)module.src_locs.entries.size()) return;

		const SourceLocTable::Entry& l = module.src_locs.entries[declaration.src_loc];
		if (l.unit_index >= (u32)module.units.size()) return;
		result = {module.units[l.unit_index].path, l.line ? l.line - 1 : 0, l.column ? l.column - 1 : 0, length};
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
										if (s.kind == EX_SYM_KIND_IMPORT && same(s.name, base->name)) declaration(&s, base->token);
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
				for (StructLiteralEntry& entry : x->entries) {
					if (entry.resolved_field_index >= 0 && x->resolved_type
						&& x->resolved_type->kind == ResolvedTypeKind::STRUCT && entry.name.type == Token::IDENTIFIER) {
						auto* st = static_cast<StructResolvedType*>(x->resolved_type);
						set(entry.name, st->decl->fields[entry.resolved_field_index].name,
							(u32)st->decl->fields[entry.resolved_field_index].name.value.length);
					}
					visitExpression(entry.value);
				}
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
				visitExpression(x->backing_type_expr);
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
				visitStatement(v->else_guard);
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
	for (u32 unit_index = 0; unit_index < (u32)module->units.size(); ++unit_index) {
		Unit& unit = module->units[unit_index];
		if (!equalStrings(unit.path, source_name)) continue;
		DefinitionQuery query{*module, unit_index, line, column};
		for (Symbol& symbol : unit.symbols) {
			query.set(symbol.token, symbol.token, (u32)symbol.name.length);
			query.visitExpression(symbol.expression);
		}
		if (!query.found) return EX_RESULT_FAILURE;
		*out_location = query.result;
		return EX_RESULT_OK;
	}
	return EX_RESULT_FAILURE;
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

int ex_unit_get_symbols_count(ex_unit* unit) {
	if (!unit) return 0;
	return ((Unit*)unit)->symbols.size();
}

ex_symbol_desc ex_unit_get_symbol(ex_unit* unit, int index) {
	ex_symbol_desc desc = { EX_SYM_KIND_INVALID, {}, 0, 0 };
	if (!unit) return desc;
	const Symbol& sym = ((Unit*)unit)->symbols[index];
	desc.kind = sym.kind;
	desc.name = sym.name;
	if (sym.token.src_loc != EX_INVALID_SOURCE_LOC && sym.token.src_loc < (u32)((Unit*)unit)->module->src_locs.entries.size()) {
		const SourceLocTable::Entry& loc = ((Unit*)unit)->module->src_locs.entries[(i32)sym.token.src_loc];
		desc.line = loc.line;
		desc.column = loc.column;
	}
	return desc;
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

// Bytecode serialization

namespace {

constexpr u32 BYTECODE_IMAGE_MAGIC = 0x43425845; // "EXBC"
constexpr u32 BYTECODE_IMAGE_VERSION = 2;

struct BytecodeWriter {
	ex_write_fn write;
	void* userdata;

	void bytes(const void* data, u64 size) {
		if (size) write(userdata, data, size);
	}
	void u32v(u32 v) { bytes(&v, sizeof(v)); }
	void u64v(u64 v) { bytes(&v, sizeof(v)); }
	void u8v(u8 v) { bytes(&v, sizeof(v)); }
	void string(ex_string_view s) {
		const u32 length = s.begin ? (u32)s.length : 0;
		u32v(length);
		bytes(s.begin, length);
	}
};

struct BytecodeReader {
	ex_host* host;
	const u8* cursor;
	const u8* end;
	bool ok = true;

	u64 remaining() const { return (u64)(end - cursor); }

	void bytes(void* out, u64 size) {
		if (!ok || remaining() < size) {
			ok = false;
			return;
		}
		if (size) memcpy(out, cursor, size);
		cursor += size;
	}

	u32 u32v() {
		u32 v = 0;
		bytes(&v, sizeof(v));
		return v;
	}

	u64 u64v() {
		u64 v = 0;
		bytes(&v, sizeof(v));
		return v;
	}

	u8 u8v() {
		u8 v = 0;
		bytes(&v, sizeof(v));
		return v;
	}

	// Element count of an array whose entries each take at least one byte in
	// the image, so a corrupt count cannot trigger a huge allocation.
	u32 count() {
		const u32 n = u32v();
		// this catch at least some issues 
		if (n > remaining()) ok = false;
		return ok ? n : 0;
	}

	void* allocate(u64 size, u64 align) {
		if (!ok || size == 0) return nullptr;
		void* mem = host->arena.allocate(host->arena.user_data, (size_t)size, (size_t)align);
		if (!mem) ok = false;
		return mem;
	}

	template <typename T> T* array(u32 n) {
		T* mem = (T*)allocate(sizeof(T) * (u64)n, alignof(T));
		if (mem) memset(mem, 0, sizeof(T) * (size_t)n);
		return mem;
	}

	ex_string_view string() {
		const u32 length = u32v();
		if (!ok || remaining() < length) {
			ok = false;
			return {};
		}
		char* mem = (char*)allocate((u64)length + 1, 1);
		if (!mem) return {};
		memcpy(mem, cursor, length);
		mem[length] = '\0';
		cursor += length;
		return {mem, (i64)length};
	}
};

bool validIndexOrNone(u32 index, u32 count) {
	return index == EX_TYPE_INDEX_NONE || index < count;
}
bool validRange(u32 first, u32 n, u32 count) {
	return n == 0 || ((u64)first + n <= count);
}

} // namespace

ex_result ex_bytecode_save(const ex_bytecode* bc, ex_write_fn write, void* userdata) {
	if (!bc || !write) return EX_RESULT_INVALID_ARGUMENT;
	// Breakpoints patch opcodes in place; saving them would persist the traps.
	if (bc->breakpoint_count > 0) return EX_RESULT_FAILURE;

	BytecodeWriter w{write, userdata};
	w.u32v(BYTECODE_IMAGE_MAGIC);
	w.u32v(BYTECODE_IMAGE_VERSION);

	w.u32v(bc->unit_count);
	for (u32 i = 0; i < bc->unit_count; ++i) {
		w.string(bc->units[i].source_name);
		w.u32v(bc->units[i].first_import);
		w.u32v(bc->units[i].import_count);
	}
	w.u32v(bc->unit_import_count);
	for (u32 i = 0; i < bc->unit_import_count; ++i) w.u32v(bc->unit_imports[i]);

	w.u32v(bc->global_size);
	w.u8v(bc->has_global_init ? 1 : 0);

	w.u32v(bc->function_count);
	for (u32 i = 0; i < bc->function_count; ++i) {
		const ex_function_bc& fn = bc->functions[i];
		w.string(fn.name);
		w.string(fn.unit_path);
		w.u32v((u32)fn.kind);
		w.u8v(fn.is_builtin_native ? 1 : 0);
		w.u32v(fn.param_size);
		w.u32v(fn.return_size);
		w.u32v(fn.frame_size);
		w.u32v((u32)fn.return_kind);
		w.u32v(fn.code_size);
		w.bytes(fn.code, fn.code_size);
		w.u32v(fn.source_map_count);
		for (u32 j = 0; j < fn.source_map_count; ++j) {
			w.u32v(fn.source_map[j].code_offset);
			w.u32v(fn.source_map[j].location_index);
		}
		w.u32v(fn.local_count);
		for (u32 j = 0; j < fn.local_count; ++j) {
			const ex_bytecode_local_debug_entry& local = fn.locals[j];
			w.string(local.name);
			w.u32v(local.offset);
			w.u32v(local.byte_size);
			w.u32v(local.type_index);
			w.u32v(local.scope_begin_offset);
		}
	}

	w.u32v(bc->string_count);
	for (u32 i = 0; i < bc->string_count; ++i) w.string(bc->strings[i]);

	w.u32v(bc->location_count);
	for (u32 i = 0; i < bc->location_count; ++i) {
		w.u32v(bc->locations[i].unit_index);
		w.u32v(bc->locations[i].line);
		w.u32v(bc->locations[i].column);
	}

	w.u32v(bc->global_debug_count);
	for (u32 i = 0; i < bc->global_debug_count; ++i) {
		const ex_bytecode_global_debug_entry& g = bc->global_debug[i];
		w.string(g.name);
		w.u32v(g.unit_index);
		w.u32v(g.offset);
		w.u32v(g.byte_size);
		w.u32v(g.type_index);
	}

	w.u32v(bc->type_info_count);
	for (u32 i = 0; i < bc->type_info_count; ++i) {
		const ex_type& t = bc->type_info[i];
		w.u32v((u32)t.kind);
		w.u32v(t.byte_size);
		w.u32v(t.alignment);
		w.u32v(t.field_count);
		w.u32v(t.first_field_index);
		w.u32v(t.attribute_count);
		w.u32v(t.first_attribute_index);
		w.u32v(t.member_count);
		w.u32v(t.first_member_index);
		w.u32v(t.value_count);
		w.u32v(t.first_value_index);
		w.u32v((u32)t.enum_backing_kind);
		w.u32v(t.element_type_index);
		w.u32v(t.array_length);
		w.u8v(t.is_const ? 1 : 0);
		w.string(t.name);
	}

	w.u32v(bc->type_field_count);
	for (u32 i = 0; i < bc->type_field_count; ++i) {
		const ex_type_field_info& f = bc->type_fields[i];
		w.string(f.name);
		w.u32v(f.type_index);
		w.u32v(f.offset);
		w.u32v(f.first_attribute_index);
		w.u32v(f.attribute_count);
	}

	// The payload size of an attribute is the byte size of its own type.
	w.u32v(bc->type_attribute_count);
	for (u32 i = 0; i < bc->type_attribute_count; ++i) {
		const ex_type_attribute_info& a = bc->type_attributes[i];
		w.u32v(a.type_index);
		w.bytes(a.value, a.type_index < bc->type_info_count ? bc->type_info[a.type_index].byte_size : 0);
	}

	w.u32v(bc->type_member_count);
	for (u32 i = 0; i < bc->type_member_count; ++i) w.u32v(bc->type_member_indices[i]);

	w.u32v(bc->type_enum_value_count);
	for (u32 i = 0; i < bc->type_enum_value_count; ++i) {
		w.string(bc->type_enum_values[i].name);
		w.u64v(bc->type_enum_values[i].value_bits);
	}

	// Slices inside the constant data are saved in their offset form.
	w.u32v(bc->const_data_size);
	if (bc->const_data_size > 0) {
		std::vector<u8> const_data(bc->const_data, bc->const_data + bc->const_data_size);
		ex_bytecode_relocate_const_data(bc, const_data.data(), false);
		w.bytes(const_data.data(), const_data.size());
	}
	w.u32v(bc->const_reloc_count);
	for (u32 i = 0; i < bc->const_reloc_count; ++i) w.u32v(bc->const_relocs[i]);
	return EX_RESULT_OK;
}

ex_bytecode* ex_bytecode_load(ex_host* host, const void* data, u64 size) {
	if (!host || !host->arena.allocate || !data) return nullptr;

	BytecodeReader r{host, (const u8*)data, (const u8*)data + size};
	if (r.u32v() != BYTECODE_IMAGE_MAGIC || r.u32v() != BYTECODE_IMAGE_VERSION) return nullptr;

	ex_bytecode* bc = r.array<ex_bytecode>(1);
	if (!bc) return nullptr;
	bc->host = host;
	bc->arena = &host->arena;

	bc->unit_count = r.count();
	bc->units = r.array<ex_bytecode_unit>(bc->unit_count);
	for (u32 i = 0; i < bc->unit_count && r.ok; ++i) {
		bc->units[i].source_name = r.string();
		bc->units[i].first_import = r.u32v();
		bc->units[i].import_count = r.u32v();
	}
	bc->unit_import_count = r.count();
	bc->unit_imports = r.array<u32>(bc->unit_import_count);
	for (u32 i = 0; i < bc->unit_import_count && r.ok; ++i) {
		bc->unit_imports[i] = r.u32v();
		if (bc->unit_imports[i] >= bc->unit_count) return nullptr;
	}
	for (u32 i = 0; i < bc->unit_count && r.ok; ++i) {
		if (!validRange(bc->units[i].first_import, bc->units[i].import_count, bc->unit_import_count)) return nullptr;
	}

	bc->global_size = r.u32v();
	bc->has_global_init = r.u8v() != 0;

	bc->function_count = r.count();
	bc->function_capacity = bc->function_count;
	bc->functions = r.array<ex_function_bc>(bc->function_count);
	for (u32 i = 0; i < bc->function_count && r.ok; ++i) {
		ex_function_bc& fn = bc->functions[i];
		fn.name = r.string();
		fn.unit_path = r.string();
		fn.kind = (ex_function_kind)r.u32v();
		fn.is_builtin_native = r.u8v() != 0;
		fn.param_size = r.u32v();
		fn.return_size = r.u32v();
		fn.frame_size = r.u32v();
		fn.return_kind = (ex_type_kind)r.u32v();
		fn.code_size = r.count();
		fn.code = (u8*)r.allocate(fn.code_size, 8);
		r.bytes(fn.code, fn.code_size);
		fn.source_map_count = r.count();
		fn.source_map = r.array<ex_bytecode_source_map_entry>(fn.source_map_count);
		for (u32 j = 0; j < fn.source_map_count && r.ok; ++j) {
			fn.source_map[j].code_offset = r.u32v();
			fn.source_map[j].location_index = r.u32v();
		}
		fn.local_count = r.count();
		fn.locals = r.array<ex_bytecode_local_debug_entry>(fn.local_count);
		for (u32 j = 0; j < fn.local_count && r.ok; ++j) {
			ex_bytecode_local_debug_entry& local = fn.locals[j];
			local.name = r.string();
			local.offset = r.u32v();
			local.byte_size = r.u32v();
			local.type_index = r.u32v();
			local.scope_begin_offset = r.u32v();
		}
	}

	bc->string_count = r.count();
	bc->strings = r.array<ex_string_view>(bc->string_count);
	for (u32 i = 0; i < bc->string_count && r.ok; ++i) bc->strings[i] = r.string();

	bc->location_count = r.count();
	bc->locations = r.array<ex_bytecode_location>(bc->location_count);
	for (u32 i = 0; i < bc->location_count && r.ok; ++i) {
		bc->locations[i].unit_index = r.u32v();
		bc->locations[i].line = r.u32v();
		bc->locations[i].column = r.u32v();
		if (bc->locations[i].unit_index >= bc->unit_count) return nullptr;
	}

	bc->global_debug_count = r.count();
	bc->global_debug = r.array<ex_bytecode_global_debug_entry>(bc->global_debug_count);
	for (u32 i = 0; i < bc->global_debug_count && r.ok; ++i) {
		ex_bytecode_global_debug_entry& g = bc->global_debug[i];
		g.name = r.string();
		g.unit_index = r.u32v();
		g.offset = r.u32v();
		g.byte_size = r.u32v();
		g.type_index = r.u32v();
		if (g.unit_index >= bc->unit_count) return nullptr;
	}

	bc->type_info_count = r.count();
	bc->type_info_capacity = bc->type_info_count;
	bc->type_info = r.array<ex_type>(bc->type_info_count);
	for (u32 i = 0; i < bc->type_info_count && r.ok; ++i) {
		ex_type& t = bc->type_info[i];
		t.bytecode = bc;
		t.kind = (ex_type_kind)r.u32v();
		t.byte_size = r.u32v();
		t.alignment = r.u32v();
		t.field_count = r.u32v();
		t.first_field_index = r.u32v();
		t.attribute_count = r.u32v();
		t.first_attribute_index = r.u32v();
		t.member_count = r.u32v();
		t.first_member_index = r.u32v();
		t.value_count = r.u32v();
		t.first_value_index = r.u32v();
		t.enum_backing_kind = (ex_type_kind)r.u32v();
		t.element_type_index = r.u32v();
		t.array_length = r.u32v();
		t.is_const = r.u8v() != 0;
		t.name = r.string();
	}

	bc->type_field_count = r.count();
	bc->type_field_capacity = bc->type_field_count;
	bc->type_fields = r.array<ex_type_field_info>(bc->type_field_count);
	for (u32 i = 0; i < bc->type_field_count && r.ok; ++i) {
		ex_type_field_info& f = bc->type_fields[i];
		f.name = r.string();
		f.type_index = r.u32v();
		f.offset = r.u32v();
		f.first_attribute_index = r.u32v();
		f.attribute_count = r.u32v();
	}

	bc->type_attribute_count = r.count();
	bc->type_attribute_capacity = bc->type_attribute_count;
	bc->type_attributes = r.array<ex_type_attribute_info>(bc->type_attribute_count);
	for (u32 i = 0; i < bc->type_attribute_count && r.ok; ++i) {
		ex_type_attribute_info& a = bc->type_attributes[i];
		a.type_index = r.u32v();
		if (a.type_index >= bc->type_info_count) return nullptr;
		const u32 value_size = bc->type_info[a.type_index].byte_size;
		void* value = r.allocate(value_size, 16);
		r.bytes(value, value_size);
		a.value = value;
	}

	bc->type_member_count = r.count();
	bc->type_member_capacity = bc->type_member_count;
	bc->type_member_indices = r.array<u32>(bc->type_member_count);
	for (u32 i = 0; i < bc->type_member_count && r.ok; ++i) bc->type_member_indices[i] = r.u32v();

	bc->type_enum_value_count = r.count();
	bc->type_enum_value_capacity = bc->type_enum_value_count;
	bc->type_enum_values = r.array<ex_type_enum_value_info>(bc->type_enum_value_count);
	for (u32 i = 0; i < bc->type_enum_value_count && r.ok; ++i) {
		bc->type_enum_values[i].name = r.string();
		bc->type_enum_values[i].value_bits = r.u64v();
	}

	bc->const_data_size = r.count();
	bc->const_data = (u8*)r.allocate(bc->const_data_size, 16);
	r.bytes(bc->const_data, bc->const_data_size);
	bc->const_reloc_count = r.count();
	bc->const_relocs = r.array<u32>(bc->const_reloc_count);
	for (u32 i = 0; i < bc->const_reloc_count && r.ok; ++i) {
		bc->const_relocs[i] = r.u32v();
		if (!r.ok) return nullptr;
		// Each slot must fit in the data and refer to an offset inside it.
		if (!validRange(bc->const_relocs[i], sizeof(uintptr_t), bc->const_data_size)) return nullptr;
		uintptr_t target;
		memcpy(&target, bc->const_data + bc->const_relocs[i], sizeof(target));
		if (target > bc->const_data_size) return nullptr;
	}
	if (!r.ok) return nullptr;

	// Cross-reference validation: later code indexes these tables unchecked.
	for (u32 i = 0; i < bc->function_count; ++i) {
		const ex_function_bc& fn = bc->functions[i];
		for (u32 j = 0; j < fn.source_map_count; ++j) {
			if (fn.source_map[j].location_index >= bc->location_count) return nullptr;
		}
		for (u32 j = 0; j < fn.local_count; ++j) {
			if (!validIndexOrNone(fn.locals[j].type_index, bc->type_info_count)) return nullptr;
		}
	}
	for (u32 i = 0; i < bc->global_debug_count; ++i) {
		if (!validIndexOrNone(bc->global_debug[i].type_index, bc->type_info_count)) return nullptr;
	}
	for (u32 i = 0; i < bc->type_info_count; ++i) {
		const ex_type& t = bc->type_info[i];
		if (!validRange(t.first_field_index, t.field_count, bc->type_field_count)) return nullptr;
		if (!validRange(t.first_attribute_index, t.attribute_count, bc->type_attribute_count)) return nullptr;
		if (!validRange(t.first_member_index, t.member_count, bc->type_member_count)) return nullptr;
		if (!validRange(t.first_value_index, t.value_count, bc->type_enum_value_count)) return nullptr;
		if (!validIndexOrNone(t.element_type_index, bc->type_info_count)) return nullptr;
	}
	for (u32 i = 0; i < bc->type_field_count; ++i) {
		const ex_type_field_info& f = bc->type_fields[i];
		if (!validIndexOrNone(f.type_index, bc->type_info_count)) return nullptr;
		if (!validRange(f.first_attribute_index, f.attribute_count, bc->type_attribute_count)) return nullptr;
	}
	for (u32 i = 0; i < bc->type_member_count; ++i) {
		if (!validIndexOrNone(bc->type_member_indices[i], bc->type_info_count)) return nullptr;
	}
	ex_bytecode_relocate_const_data(bc, bc->const_data, true);
	return bc;
}
