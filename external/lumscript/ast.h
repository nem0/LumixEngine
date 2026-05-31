#pragma once

#include <new>
#include <span>
#include <vector>

#include "capi.h"
#include "token.h"
#include "string_utils.h"

inline void* allocateMemory(const ls_host* host, size_t size, size_t align) {
	return host && host->allocate ? host->allocate(host->allocator_userdata, size, align) : ::operator new(size, std::nothrow);
}

inline void deallocateMemory(const ls_host* host, void* ptr) {
	if (!ptr) return;
	if (host && host->deallocate) {
		host->deallocate(host->allocator_userdata, ptr);
	}
	else {
		::operator delete(ptr);
	}
}

struct CanonicalName {
	ls_string_view path;
	ls_string_view name;
};

inline bool equal(const CanonicalName& a, const CanonicalName& b) {
	return equalStrings(a.name, b.name) && equalStrings(a.path, b.path);
}

inline bool splitMemberName(ls_string_view name, ls_string_view* owner, ls_string_view* member) {
	for (const char* c = data(name) + size(name); c != data(name); --c) {
		if (*(c - 1) != '.') continue;
		*owner = ls_string_view{data(name), c - 1};
		*member = ls_string_view{c, data(name) + size(name)};
		return true;
	}
	return false;
}

struct TypeRef {
	TypeRef() {}
	TypeRef(ls_type_kind kind, ls_string_view unresolved_name = {}, i32 struct_index = -1, Token token = {}, bool nullable = false)
		: kind(kind)
		, unresolved_name(unresolved_name)
		, struct_index(struct_index)
		, token(token)
		, nullable(nullable)
	{}

	ls_type_kind kind = LS_TYPE_INVALID;
	ls_string_view unresolved_name;
	CanonicalName canonical_name = {};
	i32 struct_index = -1;
	ls_type_kind element_kind = LS_TYPE_INVALID;
	ls_string_view element_name;
	i32 array_size = 0;
	Token token;
	bool nullable = false;
};

struct ls_module;

// TODO make sure canonical names are resolved
inline bool sameBaseType(const TypeRef& a, const TypeRef& b) {
	if (a.kind != b.kind) return false;
	if (a.kind == LS_TYPE_FUNCTION) return a.struct_index == b.struct_index;
	if (a.kind == LS_TYPE_ARRAY) return a.array_size == b.array_size && a.element_kind == b.element_kind && (a.element_kind != LS_TYPE_STRUCT && a.element_kind != LS_TYPE_ENUM ? true : (a.struct_index == b.struct_index || equalStrings(a.element_name, b.element_name)));
	return a.kind != LS_TYPE_STRUCT && a.kind != LS_TYPE_ENUM && a.kind != LS_TYPE_FUNCTION
		? true
		: a.struct_index == b.struct_index || equal(a.canonical_name, b.canonical_name);
}

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

	Kind kind = NUMBER;
	Token token;
	ls_string_view name;
	ls_string_view qualified_name;
	ls_string_view string;
	double number = 0;
	bool boolean = false;
	i32 left = -1;
	i32 right = -1;
	i32 method_receiver = -1;
	i32 resolved_function = -1;
	std::vector<i32> args;
	TypeRef type;
	TypeRef cast_type;
};

struct Stmt {
	enum Kind { BLOCK, VAR_DECL, FN_DECL, EXPR, ASSIGN, WHILE, FOR, BREAK, CONTINUE, RETURN, IF, DEFER, MATCH };

	Kind kind = BLOCK;
	Token token;
	std::vector<i32> children;
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
	Token token;
	std::vector<i32> patterns;
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
	CanonicalName name;
	std::vector<EnumMember> members;
	Token token;
};

struct StructDecl {
	CanonicalName name;
	std::vector<FieldDecl> fields;
	Token token;
};

struct Param {
	ls_string_view name;
	TypeRef type;
	Token token;
	bool is_ref = false;
};

struct FunctionDecl {
	ls_string_view name;
	ls_string_view local_name;
	std::vector<Param> params;
	TypeRef return_type;
	i32 body = -1;
	Token token;
	bool is_nested = false;
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
	ls_string_view canonical_name;
	std::vector<Param> params;
	TypeRef return_type;
	Token token;
};

struct FunctionTypeDecl {
	std::vector<TypeRef> params;
	TypeRef return_type;
};

struct Symbol {
	enum Kind {
		UNDEFINED,
		NOT_FOUND,
		COLLISION,

		ENUM,
		FN,
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
	ls_string_view source_name;
	std::vector<ImportDecl> imports;
	std::vector<NativeFunctionDecl> native_functions;
	std::vector<FunctionDecl> functions;
	std::vector<GlobalDecl> globals;
	std::vector<StructDecl> structs;
	std::vector<EnumDecl> enums;
	std::vector<Symbol> symbols;
};

struct ls_module {
	explicit ls_module(const ls_host* host)
		: host(host ? *host : ls_host{})
	{}

	~ls_module() {
		for (Unit* unit : units) delete unit;
		for (char* name : allocated_names) deallocateMemory(&host, name);
	}

	Unit& addUnit(ls_string_view source_name = {}) {
		Unit* unit = new Unit();
		units.push_back(unit);
		unit->source_name = source_name;
		return *unit;
	}

	i32 findGlobal(CanonicalName name) const {
		i32 index = 0;
		for (const Unit* unit_ptr : units) {
			const Unit& unit = *unit_ptr;
			for (const GlobalDecl& global : unit.globals) {
				if (equal(global.canonical_name, name)) return index;
				++index;
			}
		}
		return -1;
	}

	i32 findGlobal(ls_string_view name) const {
		i32 index = 0;
		for (const Unit* unit_ptr : units) {
			const Unit& unit = *unit_ptr;
			for (const GlobalDecl& global : unit.globals) {
				if (equalQualifiedName(global.canonical_name, name)) return index;
				++index;
			}
		}
		return -1;
	}

	i32 findStruct(CanonicalName name) const {
		i32 index = 0;
		for (const Unit* unit_ptr : units) {
			const Unit& unit = *unit_ptr;
			for (const StructDecl& s : unit.structs) {
				if (equalStrings(s.name.path, name.path) && equalStrings(s.name.name, name.name)) return index;
				++index;
			}
		}
		return -1;
	}

	i32 findStruct(ls_string_view name) const {
		i32 index = 0;
		ls_string_view a, b;
		if (splitMemberName(name, &a, &b)) {
			for (const Unit* unit_ptr : units) {
				const Unit& unit = *unit_ptr;
				for (const StructDecl& s : unit.structs) {
					if (equalStrings(s.name.path, a) && equalStrings(s.name.name, b )) return index;
					++index;
				}
			}
		}
		else {
			for (const Unit* unit_ptr : units) {
				const Unit& unit = *unit_ptr;
				for (const StructDecl& s : unit.structs) {
					if (equalStrings(s.name.name, name) && s.name.path.begin == s.name.path.end) return index;
					++index;
				}
			}
		}
		return -1;
	}

	i32 findFunction(ls_string_view name) const {
		i32 index = 0;
		for (const Unit* unit_ptr : units) {
			const Unit& unit = *unit_ptr;
			for (const FunctionDecl& fn : unit.functions) {
				if (!fn.is_nested && equalStrings(fn.name, name)) return index;
				++index;
			}
		}
		return -1;
	}

	i32 findNativeFunction(ls_string_view name) const {
		i32 index = 0;
		for (const Unit* unit_ptr : units) {
			const Unit& unit = *unit_ptr;
			for (const NativeFunctionDecl& fn : unit.native_functions) {
				if (equalStrings(fn.canonical_name, name)) return index;
				++index;
			}
		}
		return -1;
	}

	i32 findEnum(CanonicalName name) const {
		i32 index = 0;
		for (const Unit* unit_ptr : units) {
			const Unit& unit = *unit_ptr;
			for (const EnumDecl& e : unit.enums) {
				if (equalStrings(e.name.path, name.path) && equalStrings(e.name.name, name.name)) return index;
				++index;
			}
		}
		return -1;
	}

	bool equalQualifiedName(CanonicalName canonical_name, ls_string_view name) const {
		ls_string_view owner;
		ls_string_view member;
		if (splitMemberName(name, &owner, &member)) {
			return equalStrings(canonical_name.path, owner) && equalStrings(canonical_name.name, member);
		}
		return empty(canonical_name.path) && equalStrings(canonical_name.name, name);
	}

	// TODO
	i32 findEnum(ls_string_view name) const {
		i32 index = 0;
		ls_string_view a, b;
		if (splitMemberName(name, &a, &b)) {
			for (const Unit* unit_ptr : units) {
				const Unit& unit = *unit_ptr;
				for (const EnumDecl& e : unit.enums) {
					if (equalStrings(e.name.path, a) && equalStrings(e.name.name, b )) return index;
					++index;
				}
			}
		}
		else {
			for (const Unit* unit_ptr : units) {
				const Unit& unit = *unit_ptr;
				for (const EnumDecl& e : unit.enums) {
					if (equalStrings(e.name.name, name) && e.name.path.begin == e.name.path.end) return index;
					++index;
				}
			}
		}
		return -1;
	}

	i32 findEnumMember(const EnumDecl& e, ls_string_view name) const {
		for (i32 i = 0; i < (i32)e.members.size(); ++i) {
			if (equalStrings(e.members[(size_t)i].name, name)) return i;
		}
		return -1;
	}

	ls_string_view copyName(ls_string_view name) {
		char* buffer = (char*)allocateMemory(&host, size(name) + 1, alignof(char));
		if (!buffer) return {};
		copyString(std::span<char>(buffer, size(name) + 1), name);
		allocated_names.push_back(buffer);
		return ls_string_view{buffer, buffer + size(name)};
	}

	ls_string_view makeQualifiedName(ls_string_view prefix, ls_string_view name) {
		if (empty(prefix)) return name;
		char* buffer = (char*)allocateMemory(&host, size(prefix) + size(name) + 2, alignof(char));
		if (!buffer) return {};
		char* out = buffer;
		for (const char* c = data(prefix); c != data(prefix) + size(prefix); ++c) *out++ = *c;
		*out++ = '.';
		for (const char* c = data(name); c != data(name) + size(name); ++c) *out++ = *c;
		*out = '\0';
		allocated_names.push_back(buffer);
		return ls_string_view{buffer, buffer + size(prefix) + size(name) + 1};
	}

	ls_host host;
	std::vector<Unit*> units;
	std::vector<FunctionTypeDecl> function_types;
	std::vector<Expr> expressions;
	std::vector<Stmt> statements;
	std::vector<MatchPattern> match_patterns;
	std::vector<MatchArm> match_arms;
	std::vector<char*> allocated_names;
};

