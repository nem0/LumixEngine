#pragma once

#include "capi.h"
#include "token.h"

struct TypeRef {
	TypeRef() {}
	TypeRef(ls_type_kind kind, ls_string_view name = {}, i32 struct_index = -1, Token token = {}, bool nullable = false)
		: kind(kind)
		, name(name)
		, struct_index(struct_index)
		, token(token)
		, nullable(nullable)
	{}

	ls_type_kind kind = LS_TYPE_INVALID;
	ls_string_view name;
	i32 struct_index = -1;
	ls_type_kind element_kind = LS_TYPE_INVALID;
	ls_string_view element_name;
	i32 array_size = 0;
	Token token;
	bool nullable = false;
};
