#pragma once

#include "core/allocator.h"
#include "core/array.h"
#include "core/span.h"
#include "core/string.h"
#include "lumscript/lumscript_tokenizer.h"

namespace Lumix::LumScript {

struct Value;
struct Module;

struct TypeRef {
	enum Kind { INVALID, VOID, BOOL, I8, U8, I16, U16, I32, U32, I64, U64, F32, F64, STRING, UNTYPED_INT, UNTYPED_FLOAT, STRUCT, ENUM, NATIVE, NULL_VALUE } kind = INVALID;

	TypeRef() {}
	TypeRef(Kind kind, StringView name = {}, i32 struct_index = -1, Token token = {}, bool nullable = false)
		: kind(kind)
		, name(name)
		, struct_index(struct_index)
		, token(token)
		, nullable(nullable)
	{}

	StringView name;
	i32 struct_index = -1;
	Token token;
	bool nullable = false;
};

inline bool sameBaseType(const TypeRef& a, const TypeRef& b) {
	if (a.kind != b.kind) return false;
	return a.kind != TypeRef::STRUCT && a.kind != TypeRef::ENUM && a.kind != TypeRef::NATIVE
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
		ENUM_LITERAL
	};

	explicit Expr(IAllocator& allocator)
		: args(allocator)
	{}

	Kind kind = NUMBER;
	Token token;
	StringView name;
	StringView qualified_name;
	StringView string;
	double number = 0;
	bool boolean = false;
	i32 left = -1;
	i32 right = -1;
	i32 method_receiver = -1;
	Array<i32> args;
	TypeRef type;
	TypeRef cast_type;
};

struct Stmt {
	enum Kind { BLOCK, VAR_DECL, EXPR, ASSIGN, WHILE, RETURN, IF, DEFER, MATCH };

	explicit Stmt(IAllocator& allocator)
		: children(allocator)
	{}

	Kind kind = BLOCK;
	Token token;
	Array<i32> children;
	StringView name;
	TypeRef type;
	bool is_const = false;
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
	explicit MatchArm(IAllocator& allocator)
		: patterns(allocator)
	{}

	Token token;
	Array<i32> patterns;
	i32 stmt = -1;
};

struct FieldDecl {
	StringView name;
	TypeRef type;
	Token token;
};

struct EnumMember {
	StringView name;
	i32 value = -1;  // -1 means auto-assign
	Token token;
};

struct EnumDecl {
	explicit EnumDecl(IAllocator& allocator)
		: members(allocator)
	{}

	StringView name;
	Array<EnumMember> members;
	Token token;
};

struct StructDecl {
	explicit StructDecl(IAllocator& allocator)
		: fields(allocator)
	{}

	StringView name;
	Array<FieldDecl> fields;
	Token token;
};

struct Param {
	StringView name;
	TypeRef type;
	Token token;
	bool is_ref = false;
};

struct FunctionDecl {
	explicit FunctionDecl(IAllocator& allocator)
		: params(allocator)
	{}

	StringView name;
	Array<Param> params;
	TypeRef return_type;
	i32 body = -1;
	Token token;
};

struct GlobalDecl {
	StringView name;
	TypeRef type;
	Token token;
	bool is_const = false;
	i32 expr = -1;
};

struct ImportDecl {
	StringView path;
	StringView alias;
	Token token;
	bool processed = false;
};

using NativeCallback = bool (*)(Span<const Value> args, Value* result, void* userdata);
using ImportResolver = bool (*)(Module& module, StringView path, StringView alias, StringView* source, void* userdata);

struct NativeTypeDecl {
	StringView name;
	StringView id;
};

struct NativeFunctionDecl {
	explicit NativeFunctionDecl(IAllocator& allocator)
		: params(allocator)
	{}

	StringView name;
	Array<Param> params;
	TypeRef return_type;
	NativeCallback callback = nullptr;
	void* userdata = nullptr;
	Token token;
};

struct Module {
	explicit Module(IAllocator& allocator)
		: allocator(allocator)
		, imports(allocator)
		, native_types(allocator)
		, structs(allocator)
		, enums(allocator)
		, globals(allocator)
		, functions(allocator)
		, native_functions(allocator)
		, expressions(allocator)
		, statements(allocator)
		, match_patterns(allocator)
		, match_arms(allocator)
		, allocated_names(allocator)
		, allocated_native_data(allocator)
	{}

	~Module() {
		for (char* name : allocated_names) allocator.deallocate(name);
		for (void* data : allocated_native_data) allocator.deallocate(data);
	}

	StringView copyName(StringView name) {
		char* data = (char*)allocator.allocate(name.size() + 1, alignof(char));
		copyString(Span(data, name.size() + 1), name);
		allocated_names.push(data);
		return StringView(data, data + name.size());
	}

	StringView makeQualifiedName(StringView prefix, StringView name) {
		if (prefix.empty()) return name;
		char* data = (char*)allocator.allocate(prefix.size() + name.size() + 2, alignof(char));
		char* out = data;
		for (const char* c = prefix.begin; c != prefix.end; ++c) *out++ = *c;
		*out++ = '.';
		for (const char* c = name.begin; c != name.end; ++c) *out++ = *c;
		*out = '\0';
		allocated_names.push(data);
		return StringView(data, data + prefix.size() + name.size() + 1);
	}

	IAllocator& allocator;
	Array<ImportDecl> imports;
	Array<NativeTypeDecl> native_types;
	Array<StructDecl> structs;
	Array<EnumDecl> enums;
	Array<GlobalDecl> globals;
	Array<FunctionDecl> functions;
	Array<NativeFunctionDecl> native_functions;
	Array<Expr> expressions;
	Array<Stmt> statements;
	Array<MatchPattern> match_patterns;
	Array<MatchArm> match_arms;
	Array<char*> allocated_names;
	Array<void*> allocated_native_data;
};

inline NativeFunctionDecl& addNativeFunction(Module& module, StringView name, TypeRef return_type, Span<const TypeRef> param_types, NativeCallback callback, void* userdata = nullptr) {
	NativeFunctionDecl& fn = module.native_functions.emplace(module.allocator);
	fn.name = name;
	fn.return_type = return_type;
	fn.callback = callback;
	fn.userdata = userdata;
	for (TypeRef type : param_types) {
		Param& param = fn.params.emplace();
		param.type = type;
	}
	return fn;
}

} // namespace Lumix::LumScript
