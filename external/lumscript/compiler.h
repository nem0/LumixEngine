#pragma once

#include "capi.h"
#include "exparray.h"
#include "token.h"

struct CanonicalName {
	ls_string_view path;
	ls_string_view name;
};

bool equal(const CanonicalName& a, const CanonicalName& b);
bool empty(CanonicalName name);
bool splitMemberName(ls_string_view name, ls_string_view* owner, ls_string_view* member);
bool sameBaseType(const struct TypeRef& a, const TypeRef& b);

struct TypeRef {
	TypeRef();
	TypeRef(ls_type_kind kind, ls_string_view unresolved_name = {}, i32 struct_index = -1, Token token = {}, bool nullable = false);

	ls_type_kind kind = LS_TYPE_INVALID;
	ls_string_view unresolved_name;
	CanonicalName canonical_name = {};
	union {
		i32 function_index;
		i32 struct_index;
		i32 import_index;
		i32 enum_index;
	};
	ls_type_kind element_kind = LS_TYPE_INVALID;
	ls_string_view element_name;
	i32 array_size = 0;
	Token token;
	bool nullable = false;
	bool is_ref = false;
};

// TODO make sure canonical names are resolved
struct Expr {
	enum Kind {
		NUMBER,
		STRING_LITERAL,
		BOOL_LITERAL,
		NULL_LITERAL,
		VAR,
		FIELD,
		REF,
		UNARY,
		BINARY,
		CALL,
		CAST,
		STRUCT_LITERAL,
		CONSTRUCTOR,
		ENUM_LITERAL,
		FUNCTION_REF,
		INDEX
	};

	Expr(ls_arena& arena) : args(arena) {};
	Kind kind = NUMBER;
	Token token;
	ls_string_view name;
	CanonicalName qualified_name = {};
	ls_string_view string;
	double number = 0;
	union {
		bool is_native_fn = false;
		bool is_true_literal;
	};
	i32 left = -1;
	i32 right = -1;
	i32 method_receiver = -1;
	i32 native_function = -1;
	i32 resolved_function = -1;
	ExpArray<i32> args;
	TypeRef type;
	TypeRef cast_type;
};

struct Stmt {
	enum Kind { BLOCK, VAR_DECL, EXPR, ASSIGN, WHILE, FOR, BREAK, CONTINUE, RETURN, IF, DEFER, MATCH };

	Stmt(ls_arena& arena) : children(arena) {};
	Kind kind = BLOCK;
	Token token;
	ExpArray<i32> children;
	ls_string_view name;
	ls_string_view label_name;
	TypeRef type;
	bool is_const = false;
	bool is_undefined_init = false;
	i32 expr = -1;
	i32 left = -1;
	i32 right = -1;
	i32 local_index = -1;
	i32 resolved_function = -1;
	Token::Type assign_op = Token::EQUAL;
};

struct MatchPattern {
	enum Kind { VALUE, RANGE, DEFAULT } kind = VALUE;

	Token token;
	i32 start_expr = -1;
	i32 end_expr = -1;
};

struct MatchArm {
	MatchArm(ls_arena& arena) : patterns(arena) {};
	Token token;
	ExpArray<MatchPattern> patterns;
	i32 stmt = -1;
};

struct FieldDecl {
	ls_string_view name;
	TypeRef type;
	Token token;
};

struct EnumMember {
	ls_string_view name;
	i32 value = -1;  // -1 means auto-assign
	Token token;
};

struct EnumDecl {
	EnumDecl(ls_arena& arena) : members(arena) {};
	CanonicalName name;
	ExpArray<EnumMember> members;
	Token token;
};

struct StructDecl {
	StructDecl(ls_arena& arena) : fields(arena) {};
	CanonicalName name;
	ExpArray<FieldDecl> fields;
	Token token;
};

struct Param {
	ls_string_view name;
	TypeRef type;
	Token token;
	bool is_ref = false;
};

struct FunctionDecl {
	FunctionDecl(ls_arena& arena) : params(arena) {};
	CanonicalName canonical_name;
	ExpArray<Param> params;
	TypeRef return_type;
	i32 body = -1;
	Token token;
	bool is_operator = false;
	Token::Type operator_token = Token::END_OF_FILE;
};

struct GlobalDecl {
	CanonicalName canonical_name;
	TypeRef type;
	i32 slot = -1;
	i32 slot_count = 1;
	Token token;
	bool is_const = false;
	bool is_undefined_init = false;
	i32 expr = -1;
};

struct ImportDecl {
	ls_string_view path;
	ls_string_view alias;
	Token token;
	bool processed = false;
};

struct NativeFunctionDecl {
	NativeFunctionDecl(ls_arena& arena) : params(arena) {};
	CanonicalName canonical_name;
	ExpArray<Param> params;
	TypeRef return_type;
	Token token;
};

struct FunctionTypeDecl {
	FunctionTypeDecl(ls_arena& arena) : params(arena) {};
	ExpArray<TypeRef> params;
	TypeRef return_type;
};

struct Symbol {
	enum Kind {
		UNDEFINED,
		NOT_FOUND,
		COLLISION,

		ENUM,
		EXTERN_FN,
		GLOBAL_VAR,
		STRUCT
	};
	Kind kind = UNDEFINED;
	CanonicalName name = {};
	struct Unit* unit = nullptr;
	i32 index = -1;
};

struct Unit {
	struct ArenaOwner {
		const ls_host& host;
		ls_arena* arena;

		ArenaOwner(const ls_host& host);
		ArenaOwner(const ArenaOwner&) = delete;
		ArenaOwner& operator=(const ArenaOwner&) = delete;
		~ArenaOwner();
	};

	explicit Unit(const ls_host& host);

	ls_string_view source_name;
	ArenaOwner arena_owner;
	ExpArray<ImportDecl> imports;
	ExpArray<NativeFunctionDecl> native_functions;
	ExpArray<FunctionDecl> functions;
	ExpArray<GlobalDecl> globals;
	ExpArray<StructDecl> structs;
	ExpArray<EnumDecl> enums;
	ExpArray<Symbol> symbols;
	ExpArray<Expr> expressions;
	ExpArray<Stmt> statements;
	ExpArray<MatchArm> match_arms;
};

struct ls_module {
	explicit ls_module(const ls_host* host);
	~ls_module();

	Unit& addUnit(ls_string_view source_name = {});
	i32 findFunction(CanonicalName name) const;
	i32 findNativeFunction(ls_string_view name) const;
	i32 findNativeFunction(CanonicalName name) const;
	i32 findEnum(CanonicalName name) const;
	i32 findEnumMember(const EnumDecl& e, ls_string_view name) const;
	ls_string_view makeQualifiedName(ls_string_view prefix, ls_string_view name);

	ls_host host;
	struct ArenaOwner {
		const ls_host& host;
		ls_arena* arena;

		ArenaOwner(const ls_host& host);
		ArenaOwner(const ArenaOwner&) = delete;
		ArenaOwner& operator=(const ArenaOwner&) = delete;
		~ArenaOwner();
	};

	ArenaOwner arena_owner;
	ExpArray<Unit> units;
	ExpArray<FunctionTypeDecl> function_types;
};

