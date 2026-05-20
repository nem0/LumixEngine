#pragma once

#include "token.h"
#include "core/string.h"

namespace Lumix::LumScript {

struct TypeRef {
	enum Kind { INVALID, VOID, BOOL, I8, U8, I16, U16, I32, U32, I64, U64, F32, F64, STRING, UNTYPED_INT, UNTYPED_FLOAT, STRUCT, ENUM, NATIVE, FUNCTION, ARRAY, NULL_VALUE } kind = INVALID;

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
	Kind element_kind = INVALID;
	StringView element_name;
	i32 array_size = 0;
	Token token;
	bool nullable = false;
};

}