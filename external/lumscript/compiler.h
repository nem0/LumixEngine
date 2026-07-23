#pragma once

#include "capi.h"
#include "expressions.h"
#include "resolved_types.h"
#include "statements.h"
#include "utils.h"

struct Unit;

struct Symbol {
	enum Storage {
		// Runtime storage. The initializer expression is evaluated at runtime unless
		// later analysis proves it can be folded.
		VARIABLE,
		// Runtime storage initialized once and rejected as an assignment target.
		CONST,
		// Compile-time binding. Functions, structs and enums are represented
		// as values bound here instead of as separate declaration categories.
		COMPTIME,
		// Aliased import (`import "path" as name`). Reserves the alias name in the
		// symbol table so that collision detection covers it like any other name.
		// The actual unit linkage is still stored in Unit::imports.
		IMPORT,
	};

	enum CheckState {
		UNCHECKED,
		CHECKING, // re-entry here means a definition cycle
		FAILED,
		CHECKED,
	};

	Storage storage;
	CheckState check_state = UNCHECKED;
	ls_string_view name; // as written in unit
	Token token = {};
	// Syntax-level type annotation. This preserves aliases and unresolved names
	// exactly as written; semantic resolution converts to resolved_type later.
	Expression* type_expr = nullptr;
	// Canonical semantic type of the symbol.
	// For struct/enum/fn type declarations this is MetaType { inner = actual type }.
	// For value symbols this is the value's type directly.
	ResolvedType* resolved_type = nullptr;
	// Symbols bind names to expressions. Top-level `fn`, `struct`, and `enum`
	// syntax should eventually desugar to COMPTIME symbols with expression values.
	Expression* expression = nullptr; // expression used to initialize the symbol
	// Storage location in the global data segment. The checker points identifier
	// expressions at this slot (see symbolHasGlobalStorage); its values are filled
	// in by ls_bytecode_compile's global layout pass before function bodies compile.
	StorageSlot slot;
};

// True when the symbol occupies runtime storage in the global data segment.
// The checker uses this to decide whether an identifier gets a slot pointer;
// ls_bytecode_compile's global layout pass must lay out exactly these symbols.
inline bool symbolHasGlobalStorage(const Symbol& sym) {
	return sym.expression
		&& (!sym.resolved_type || sym.resolved_type->kind != ResolvedType::META)
		&& sym.expression->kind != Expression::FUNCTION
		&& sym.expression->kind != Expression::STRUCT
		&& sym.expression->kind != Expression::ENUM;
}

struct Import {
	ls_string_view path = {};
	ls_string_view alias = {}; // empty when imported without `as`
	Unit* unit = nullptr; // cached resolved unit pointer, set during import resolution
};

// Returns the symbol name used to store an operator overload, e.g. Token::PLUS -> "+".
// Operator names are non-identifier strings, so they cannot collide with user symbols.
// Returns nullptr for non-overloadable tokens.
inline const char* operatorSymbolName(Token::Type op) {
	switch (op) {
		case Token::PLUS:         return "+";
		case Token::MINUS:        return "-";
		case Token::STAR:         return "*";
		case Token::SLASH:        return "/";
		case Token::PERCENT:      return "%";
		case Token::EQUAL_EQUAL:  return "==";
		case Token::BANG_EQUAL:   return "!=";
		case Token::LT:           return "<";
		case Token::LT_EQUAL:     return "<=";
		case Token::GT:           return ">";
		case Token::GT_EQUAL:     return ">=";
		default:                  return nullptr;
	}
}

// Returns the operator token for a symbol named by operatorSymbolName, or
// Token::ERROR if the name does not match any operator overload.
inline Token::Type tokenFromOperatorName(ls_string_view name) {
	struct Entry { const char* name; Token::Type token; };
	static constexpr Entry table[] = {
		{ "+",  Token::PLUS },
		{ "-",  Token::MINUS },
		{ "*",  Token::STAR },
		{ "/",  Token::SLASH },
		{ "%",  Token::PERCENT },
		{ "==", Token::EQUAL_EQUAL },
		{ "!=", Token::BANG_EQUAL },
		{ "<",  Token::LT },
		{ "<=", Token::LT_EQUAL },
		{ ">",  Token::GT },
		{ ">=", Token::GT_EQUAL },
	};
	for (const Entry& e : table) {
		const char* p = name.begin;
		const char* q = e.name;
		while (p != name.end && *q && *p == *q) { ++p; ++q; }
		if (p == name.end && *q == '\0') return e.token;
	}
	return Token::ERROR;
}

inline bool isOperatorSymbol(ls_string_view name) {
	return tokenFromOperatorName(name) != Token::ERROR;
}

struct Unit {
	Unit(ls_string_view path, ls_arena& module_arena)
		: arena(module_arena)
		, symbols(module_arena)
		, types(module_arena)
		, imports(module_arena)
		, native_symbols(module_arena)
		, path(path) {}

	enum ImportState { IMPORT_PENDING, IMPORT_RESOLVING, IMPORT_DONE };
	ImportState import_state = IMPORT_PENDING;

	ls_string_view path;
	ls_arena& arena;

	ExpArray<Symbol> symbols;
	ExpArray<ResolvedType> types;
	ExpArray<Import> imports;
	ExpArray<Symbol*> native_symbols;
};

struct ls_module {
	ls_module(ls_host* host);

	ls_host* host;
	ls_arena& arena;
	ExpArray<Unit> units;
	// One canonical instance per primitive kind, indexed by ResolvedType::Kind.
	// Pointer equality suffices for primitives; use typesEqual() for compound types.
	ResolvedType primitives[ResolvedType::META];
	EnumExpression type_kind_decl;
	EnumResolvedType type_kind;
};
