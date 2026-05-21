#pragma once

#include <new>
#include <span>
#include <vector>

#include "capi.h"
#include "token.h"
#include "string_utils.h"

struct TypeRef {
	enum Kind { INVALID, VOID, BOOL, I8, U8, I16, U16, I32, U32, I64, U64, F32, F64, STRING, UNTYPED_INT, UNTYPED_FLOAT, STRUCT, ENUM, NATIVE, FUNCTION, ARRAY, NULL_VALUE };

	TypeRef() {}
	TypeRef(Kind kind, ls_string_view name = {}, i32 struct_index = -1, Token token = {}, bool nullable = false)
		: kind(kind)
		, name(name)
		, struct_index(struct_index)
		, token(token)
		, nullable(nullable)
	{}

	Kind kind = INVALID;
	ls_string_view name;
	i32 struct_index = -1;
	Kind element_kind = INVALID;
	ls_string_view element_name;
	i32 array_size = 0;
	Token token;
	bool nullable = false;
};

struct Value {
	friend struct Runtime;
	
	TypeRef type;
	bool b = false;
	i32 i = 0;
	u32 u = 0;
	i64 i64 = 0;
	u64 u64 = 0;
	float f = 0;
	double d = 0;
	ls_string_view string;
	float composite[4] = {};
	void* ptr = nullptr;
private:
	std::vector<Value>* fields = nullptr;
};

Value makeI32(i32 value);
Value makeU32(u32 value);
Value makeI64(i64 value);
Value makeU64(u64 value);
Value makeF32(float value);
Value makeF64(double value);
	Value makeString(ls_string_view value);
Value makeFunction(TypeRef type, i32 index, bool is_native);
Value makeNull();
Value makeBool(bool value);

struct LumScriptNewPlaceholder {};
inline void* operator new(size_t, LumScriptNewPlaceholder, void* where) { return where; }
inline void operator delete(void*, LumScriptNewPlaceholder, void*) {}

template <typename T, typename... Args>
T* allocateObject(const ls_host* host, Args&&... args) {
	void* mem = host && host->allocate ? host->allocate(host->allocator_userdata, sizeof(T), alignof(T)) : ::operator new(sizeof(T), std::nothrow);
	return mem ? new (LumScriptNewPlaceholder(), mem) T(static_cast<Args&&>(args)...) : nullptr;
}

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

template <typename T>
void deleteObject(const ls_host* host, T* ptr) {
	if (!ptr) return;
	ptr->~T();
	deallocateMemory(host, ptr);
}

struct Value;
struct Module;

inline bool sameBaseType(const TypeRef& a, const TypeRef& b) {
	if (a.kind != b.kind) return false;
	if (a.kind == TypeRef::FUNCTION) return a.struct_index == b.struct_index;
	if (a.kind == TypeRef::ARRAY) return a.array_size == b.array_size && a.element_kind == b.element_kind && (a.element_kind != TypeRef::STRUCT && a.element_kind != TypeRef::ENUM && a.element_kind != TypeRef::NATIVE ? true : (a.struct_index == b.struct_index || equalStrings(a.element_name, b.element_name)));
	return a.kind != TypeRef::STRUCT && a.kind != TypeRef::ENUM && a.kind != TypeRef::NATIVE && a.kind != TypeRef::FUNCTION
		? true
		: a.struct_index == b.struct_index || equalStrings(a.name, b.name);
}

inline bool sameType(const TypeRef& a, const TypeRef& b) {
	if (a.nullable != b.nullable) return false;
	return sameBaseType(a, b);
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
	std::vector<i32> args;
	TypeRef type;
	TypeRef cast_type;
};

struct Stmt {
	enum Kind { BLOCK, VAR_DECL, FN_DECL, EXPR, ASSIGN, WHILE, BREAK, CONTINUE, RETURN, IF, DEFER, MATCH };

	Kind kind = BLOCK;
	Token token;
	std::vector<i32> children;
	ls_string_view name;
	TypeRef type;
	bool is_const = false;
	bool is_undefined_init = false;
	i32 expr = -1;
	i32 left = -1;
	i32 right = -1;
	i32 local_index = -1;
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
	ls_string_view name;
	std::vector<EnumMember> members;
	Token token;
};

struct StructDecl {
	ls_string_view name;
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
};

struct GlobalDecl {
	ls_string_view name;
	TypeRef type;
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

using NativeCallback = bool (*)(std::span<const Value> args, Value* result, void* userdata);
using ImportResolver = bool (*)(Module& module, ls_string_view path, ls_string_view alias, ls_string_view* source, void* userdata);

struct NativeTypeDecl {
	ls_string_view name;
	ls_string_view id;
};

struct NativeFunctionDecl {
	ls_string_view name;
	std::vector<Param> params;
	TypeRef return_type;
	NativeCallback callback = nullptr;
	void* userdata = nullptr;
	Token token;
};

struct FunctionTypeDecl {
	std::vector<TypeRef> params;
	TypeRef return_type;
};

struct Module {
	explicit Module(const ls_host* host)
		: host(host)
	{}

	~Module() {
		for (char* name : allocated_names) deallocateMemory(host, name);
		for (void* data : allocated_native_data) deallocateMemory(host, data);
	}

	ls_string_view copyName(ls_string_view name) {
		char* buffer = (char*)allocateMemory(host, size(name) + 1, alignof(char));
		if (!buffer) return {};
		copyString(std::span<char>(buffer, size(name) + 1), name);
		allocated_names.push_back(buffer);
		return ls_string_view{buffer, buffer + size(name)};
	}

	ls_string_view makeQualifiedName(ls_string_view prefix, ls_string_view name) {
		if (empty(prefix)) return name;
		char* buffer = (char*)allocateMemory(host, size(prefix) + size(name) + 2, alignof(char));
		if (!buffer) return {};
		char* out = buffer;
		for (const char* c = data(prefix); c != data(prefix) + size(prefix); ++c) *out++ = *c;
		*out++ = '.';
		for (const char* c = data(name); c != data(name) + size(name); ++c) *out++ = *c;
		*out = '\0';
		allocated_names.push_back(buffer);
		return ls_string_view{buffer, buffer + size(prefix) + size(name) + 1};
	}

	const ls_host* host = nullptr;
	std::vector<ImportDecl> imports;
	std::vector<NativeFunctionDecl> native_functions;
	std::vector<FunctionDecl> functions;
	std::vector<GlobalDecl> globals;
	std::vector<StructDecl> structs;
	std::vector<NativeTypeDecl> native_types;
	std::vector<EnumDecl> enums;
	std::vector<FunctionTypeDecl> function_types;
	std::vector<Expr> expressions;
	std::vector<Stmt> statements;
	std::vector<MatchPattern> match_patterns;
	std::vector<MatchArm> match_arms;
	std::vector<char*> allocated_names;
	std::vector<void*> allocated_native_data;
};

// TODO get rid of userdata, it's only used in generated engine API, and it's used in a wrong way there
inline NativeFunctionDecl& addNativeFunction(Module& module, ls_string_view name, TypeRef return_type, std::span<const TypeRef> param_types, NativeCallback callback, void* userdata = nullptr) {
	NativeFunctionDecl& fn = module.native_functions.emplace_back();
	fn.name = name;
	fn.return_type = return_type;
	fn.callback = callback;
	fn.userdata = userdata;
	for (TypeRef type : param_types) {
		Param& param = fn.params.emplace_back();
		param.type = type;
	}
	return fn;
}
