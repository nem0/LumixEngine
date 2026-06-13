#pragma once

#include "capi.h"
#include "expressions.h"
#include "parsed_types.h"
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
		COMPTIME
	};

	enum CheckState {
		UNCHECKED,
		CHECKING, // re-entry here means a definition cycle
		CHECKED,
	};

	Storage storage;
	CheckState check_state = UNCHECKED;
	ls_string_view name; // as written in unit
	// Syntax-level type annotation. This preserves aliases and unresolved names
	// exactly as written; semantic resolution converts to resolved_type later.
	ParsedType* parsed_type = nullptr;
	// Canonical semantic type — the type of the symbol as an expression value.
	// For struct/enum declarations this is TYPE (a comptime type value); for
	// functions it is the FunctionResolvedType.
	ResolvedType* resolved_type = nullptr;
	// For type-producing declarations (struct, enum): the type of a variable of
	// that type, distinct from resolved_type which describes the declaration value.
	// Null for non-type symbols.
	ResolvedType* instance_type = nullptr;
	// Symbols bind names to expressions. Top-level `fn`, `struct`, and `enum`
	// syntax should eventually desugar to COMPTIME symbols with expression values.
	Expression* expression = nullptr; // expression used to initialize the symbol
};

// so we can pass arenas to ExpArrays defined in the same struct as the arena
struct ArenaOwner {
	ArenaOwner(const ls_host* host)
		: host(host) {
		arena = host->create_arena();
	}

	~ArenaOwner() { host->destroy_arena(arena); }

	void operator=(const ArenaOwner&) = delete;
	void operator=(ArenaOwner&&) = delete;
	ArenaOwner(const ArenaOwner&) = delete;
	ArenaOwner(ArenaOwner&&) = delete;

	operator ls_arena&() { return *arena; }

	const ls_host* host;
	ls_arena* arena;
};

struct Import {
	ls_string_view path = {};
	ls_string_view alias = {}; // empty when imported without `as`
};

// Operators are a separate declaration form: their names are fixed tokens, not
// identifiers, so they live outside the symbol table and feed overload resolution.
struct OperatorDecl {
	Token::Type op = Token::ERROR;
	FunctionExpression* function = nullptr;
};

struct FunctionInstance {
	enum CheckState {
		CREATING,
		CHECKING,
		READY,
		FAILED,
	};

	explicit FunctionInstance(ls_arena& arena)
		: type_args(arena) {}

	Unit* unit = nullptr;
	Symbol* symbol = nullptr;
	FunctionExpression* declaration = nullptr;
	// Template bodies are cloned before checking because every expression and
	// local declaration stores semantic results directly on its AST node.
	FunctionExpression* function = nullptr;
	FunctionResolvedType* type = nullptr;
	ExpArray<ResolvedType*> type_args;
	CheckState check_state = CREATING;
};

struct Unit {
	Unit(ls_string_view path, const ls_host* host)
		: arena(host)
		, symbols(arena)
		, types(arena)
		, struct_instances(arena)
		, function_instances(arena)
		, imports(arena)
		, operators(arena)
		, path(path) {}

	enum ImportState { IMPORT_PENDING, IMPORT_RESOLVING, IMPORT_DONE };
	ImportState import_state = IMPORT_PENDING;

	ls_string_view path;
	ArenaOwner arena;

	// Parsed top-level bindings. Function/type declarations are represented as
	// symbols bound to AST expressions instead of separate declaration objects.
	ExpArray<Symbol> symbols; // top level symbols
	// Canonical semantic types allocated during resolution/instantiation.
	ExpArray<ResolvedType> types;
	ExpArray<StructResolvedType*> struct_instances;
	ExpArray<FunctionInstance*> function_instances;
	ExpArray<Import> imports;
	ExpArray<OperatorDecl> operators;
};

struct ls_module {
	ls_module(const ls_host* host)
		: host(host)
		, arena(host)
		, units(arena) {
		for (i32 i = 0; i <= ResolvedType::TYPE; ++i)
			primitives[i].kind = static_cast<ResolvedType::Kind>(i);
	}

	const ls_host* host;
	ArenaOwner arena;
	ExpArray<Unit> units;
	// One canonical instance per primitive kind, indexed by ResolvedType::Kind.
	// Pointer equality suffices for primitives; use typesEqual() for type constructors.
	ResolvedType primitives[ResolvedType::TYPE + 1];
};
