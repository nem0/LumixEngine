#pragma once

#include "typeref.h"

namespace Lumix::LumScript {

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
	StringView string;
	float composite[4] = {};
	void* ptr = nullptr;
private:
	Array<Value>* fields = nullptr;
};

Value makeI32(i32 value);
Value makeU32(u32 value);
Value makeI64(i64 value);
Value makeU64(u64 value);
Value makeF32(float value);
Value makeF64(double value);
Value makeString(StringView value);
Value makeFunction(TypeRef type, i32 index, bool is_native);
Value makeNull();
Value makeBool(bool value);

}