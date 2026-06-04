#include "bytecode.h"

#include <math.h>
#include <string.h>
#include <vector>
#include "utils.h"

struct ls_runtime {
	explicit ls_runtime(ls_bytecode* bytecode);

	ls_bytecode* bytecode = nullptr;
	std::vector<u64> stack;
	std::vector<u8> stack_kinds;
	std::vector<ls_native_fn> native_functions;
	bool globals_initialized = false;
};

enum class StackValueKind : u8 {
	RAW = 0,
	STRING = 1
};

ls_runtime::ls_runtime(ls_bytecode* bytecode_)
	: bytecode(bytecode_)
{}

static ls_string* toStringObject(u64 value) {
	return (ls_string*)(uintptr)value;
}

static void retainString(ls_string* value) {
	if (value) ++value->ref_count;
}

static void* bytecodeAllocate(const ls_host* host, size_t size, size_t align) {
	return host && host->allocate ? host->allocate(host->allocator_userdata, size, align) : ::operator new(size, std::nothrow);
}

static void bytecodeDeallocate(const ls_host* host, void* ptr) {
	if (!ptr) return;
	if (host && host->deallocate) {
		host->deallocate(host->allocator_userdata, ptr);
	}
	else {
		::operator delete(ptr);
	}
}

static void stackPushRaw(ls_runtime* runtime, u64 value, u8 kind = (u8)StackValueKind::RAW) {
	runtime->stack.push_back(value);
	runtime->stack_kinds.push_back(kind);
}

static void releaseString(ls_string* value) {
	if (!value) return;
	if (--value->ref_count != 0) return;
	bytecodeDeallocate(&value->host, value);
}

static void releaseStackValue(ls_runtime* runtime, u64 value, u8 kind) {
	if (kind == (u8)StackValueKind::STRING) releaseString(toStringObject(value));
}

static void stackPop(ls_runtime* runtime) {
	if (runtime->stack.empty()) return;
	releaseStackValue(runtime, runtime->stack.back(), runtime->stack_kinds.back());
	runtime->stack.pop_back();
	runtime->stack_kinds.pop_back();
}

template <typename T>
void pushStack(ls_runtime& runtime, const T& value) {
	u64 v = 0;
	static_assert(sizeof(value) <= sizeof(v));
	memcpy(&v, &value, sizeof(value));
	if constexpr (isSigned<T>() && sizeof(T) < sizeof(v)) {
		// Keep narrow signed values sign-extended so later wider reads preserve
		// the original negative value.
		// Example: pushing i8(-1) stores 0xFFFFFFFFFFFFFFFF, so reading it back
		// as i64 still yields -1 instead of 255.
		if (value < 0) {
			const u64 sign_mask = ~((u64(1) << (sizeof(T) * 8)) - 1);
			v |= sign_mask;
		}
	}
	stackPushRaw(&runtime, v);
}

template <typename T>
T popStack(ls_runtime& runtime) {
	T value;
	static_assert(sizeof(value) <= sizeof(runtime.stack.back()));
	memcpy(&value, &runtime.stack.back(), sizeof(value));
	stackPop(&runtime);
	return value;
}

static void bytecodeDeallocate(const ls_host* host, void* ptr);

static bool isStringValue(ls_runtime* runtime, size_t index) {
	return runtime && index < runtime->stack_kinds.size() && runtime->stack_kinds[index] == (u8)StackValueKind::STRING;
}

static void retainStackValue(ls_runtime* runtime, u64 value, u8 kind) {
	if (kind == (u8)StackValueKind::STRING) retainString(toStringObject(value));
}

static void stackPushCopy(ls_runtime* runtime, u64 value, u8 kind) {
	retainStackValue(runtime, value, kind);
	stackPushRaw(runtime, value, kind);
}

static void stackResize(ls_runtime* runtime, size_t size) {
	if (!runtime) return;
	while (runtime->stack.size() > size) stackPop(runtime);
	runtime->stack.resize(size);
	runtime->stack_kinds.resize(size, (u8)StackValueKind::RAW);
}

static void stackOverwrite(ls_runtime* runtime, size_t idx, u64 value, u8 kind) {
	releaseStackValue(runtime, runtime->stack[idx], runtime->stack_kinds[idx]);
	retainStackValue(runtime, value, kind);
	runtime->stack[idx] = value;
	runtime->stack_kinds[idx] = kind;
}

static void stackErase(ls_runtime* runtime, size_t idx) {
	releaseStackValue(runtime, runtime->stack[idx], runtime->stack_kinds[idx]);
	runtime->stack.erase(runtime->stack.begin() + (std::ptrdiff_t)idx);
	runtime->stack_kinds.erase(runtime->stack_kinds.begin() + (std::ptrdiff_t)idx);
}

static i32 runtimeStackIndex(ls_runtime* runtime, i32 index) {
	return index < 0 ? i32(runtime->stack.size()) + index : index;
}

static i32 bytecodeFindFunction(const ls_bytecode* bytecode, ls_string_view name) {
	if (!bytecode) return -1;
	for (i32 i = 0; i < bytecode->functions.size; ++i) {
		if (equalStrings(bytecode->functions.data[i].name, name)) return i;
	}
	return -1;
}

static void mathSinF32(ls_runtime* runtime) {
	ls_push_f32(runtime, sinf(ls_to_f32(runtime, -1)));
}

static void mathCosF32(ls_runtime* runtime) {
	ls_push_f32(runtime, cosf(ls_to_f32(runtime, -1)));
}

static void mathSinF64(ls_runtime* runtime) {
	ls_push_f64(runtime, sin(ls_to_f64(runtime, -1)));
}

static void mathCosF64(ls_runtime* runtime) {
	ls_push_f64(runtime, cos(ls_to_f64(runtime, -1)));
}

static void mathSqrtF32(ls_runtime* runtime) {
	ls_push_f32(runtime, sqrtf(ls_to_f32(runtime, -1)));
}

static void mathSqrtF64(ls_runtime* runtime) {
	ls_push_f64(runtime, sqrt(ls_to_f64(runtime, -1)));
}

static void bindBuiltinNativeFunctions(ls_runtime* runtime) {
	if (!runtime || !runtime->bytecode) return;
	for (size_t i = 0; i < (size_t)runtime->bytecode->native_functions.size; ++i) {
		const BytecodeNativeFunction& fn = runtime->bytecode->native_functions.data[i];
		ls_native_fn& binding = runtime->native_functions[i];
		if (equalStrings(fn.canonical_name, makeStringView("std:math.sin")) && fn.return_type.kind == LS_TYPE_F32 && fn.params.size == 1 && fn.params.data[0].kind == LS_TYPE_F32) {
			binding = &mathSinF32;
		}
		else if (equalStrings(fn.canonical_name, makeStringView("std:math.cos")) && fn.return_type.kind == LS_TYPE_F32 && fn.params.size == 1 && fn.params.data[0].kind == LS_TYPE_F32) {
			binding = &mathCosF32;
		}
		else if (equalStrings(fn.canonical_name, makeStringView("std:math.sqrt")) && fn.return_type.kind == LS_TYPE_F32 && fn.params.size == 1 && fn.params.data[0].kind == LS_TYPE_F32) {
			binding = &mathSqrtF32;
		}
		else if (equalStrings(fn.canonical_name, makeStringView("std:math.sin_f64")) && fn.return_type.kind == LS_TYPE_F64 && fn.params.size == 1 && fn.params.data[0].kind == LS_TYPE_F64) {
			binding = &mathSinF64;
		}
		else if (equalStrings(fn.canonical_name, makeStringView("std:math.cos_f64")) && fn.return_type.kind == LS_TYPE_F64 && fn.params.size == 1 && fn.params.data[0].kind == LS_TYPE_F64) {
			binding = &mathCosF64;
		}
		else if (equalStrings(fn.canonical_name, makeStringView("std:math.sqrt_f64")) && fn.return_type.kind == LS_TYPE_F64 && fn.params.size == 1 && fn.params.data[0].kind == LS_TYPE_F64) {
			binding = &mathSqrtF64;
		}
	}
}

ls_runtime* createBytecodeRuntime(ls_bytecode* bytecode);
void destroyBytecodeRuntime(ls_runtime* runtime);

ls_runtime* createBytecodeRuntime(ls_bytecode* bytecode) {
	if (!bytecode) return nullptr;
	if (!bytecode->host.create_arena || !bytecode->host.destroy_arena) return nullptr;
	void* mem = bytecodeAllocate(&bytecode->host, sizeof(ls_runtime), alignof(ls_runtime));
	if (!mem) return nullptr;
	ls_runtime* runtime = new (mem) ls_runtime(bytecode);
	runtime->stack.resize((size_t)bytecode->global_count);
	runtime->stack_kinds.resize((size_t)bytecode->global_count, (u8)StackValueKind::RAW);
	runtime->native_functions.resize((size_t)bytecode->native_functions.size);
	bindBuiltinNativeFunctions(runtime);
	return runtime;
}

void destroyBytecodeRuntime(ls_runtime* runtime) {
	if (!runtime) return;
	ls_host host = runtime->bytecode ? runtime->bytecode->host : ls_host{};
	stackResize(runtime, 0);
	runtime->~ls_runtime();
	bytecodeDeallocate(&host, runtime);
}


template <typename T>
static inline void bytecodeAdd(ls_runtime& runtime) {
	using U = std::make_unsigned_t<T>;
	const U rhs = popStack<U>(runtime);
	const U lhs = popStack<U>(runtime);
	const U result = lhs + rhs;
	T value;
	memcpy(&value, &result, sizeof(value));
	pushStack(runtime, value);
}

template <typename T>
static inline void bytecodeSub(ls_runtime& runtime) {
	using U = std::make_unsigned_t<T>;
	const U rhs = popStack<U>(runtime);
	const U lhs = popStack<U>(runtime);
	const U result = lhs - rhs;
	T value;
	memcpy(&value, &result, sizeof(value));
	pushStack(runtime, value);
}

template <typename T>
static inline void bytecodeAddFloat(ls_runtime& runtime) {
	const T rhs = popStack<T>(runtime);
	const T lhs = popStack<T>(runtime);
	pushStack(runtime, lhs + rhs);
}

static inline bool bytecodeAddString(ls_runtime& runtime) {
	if (runtime.stack.size() < 2) return false;
	ls_string_view rhs = ls_to_string(&runtime, -1);
	ls_string_view lhs = ls_to_string(&runtime, -2);
	const size_t lhs_len = (size_t)(lhs.end - lhs.begin);
	const size_t rhs_len = (size_t)(rhs.end - rhs.begin);
	std::vector<char> buffer(lhs_len + rhs_len);
	if (lhs_len > 0) memcpy(buffer.data(), lhs.begin, lhs_len);
	if (rhs_len > 0) memcpy(buffer.data() + lhs_len, rhs.begin, rhs_len);
	stackPop(&runtime);
	stackPop(&runtime);
	const char* data = buffer.empty() ? "" : buffer.data();
	ls_push_string(&runtime, {data, data + buffer.size()});
	return true;
}

template <typename T>
static inline void bytecodeSubFloat(ls_runtime& runtime) {
	const T rhs = popStack<T>(runtime);
	const T lhs = popStack<T>(runtime);
	pushStack(runtime, lhs - rhs);
}

template <typename T>
static inline void bytecodeMulFloat(ls_runtime& runtime) {
	const T rhs = popStack<T>(runtime);
	const T lhs = popStack<T>(runtime);
	pushStack(runtime, lhs * rhs);
}

template <typename T>
static inline bool bytecodeDivFloat(ls_runtime& runtime) {
	const T rhs = popStack<T>(runtime);
	const T lhs = popStack<T>(runtime);
	if (rhs == T{}) return false;
	pushStack(runtime, lhs / rhs);
	return true;
}

template <typename T>
static inline void bytecodeMul(ls_runtime& runtime) {
	// Integer multiplication stays in the target width by computing through the
	// unsigned representation and then reinterpreting the resulting bit pattern.
	using U = std::make_unsigned_t<T>;
	const U rhs = popStack<U>(runtime);
	const U lhs = popStack<U>(runtime);
	const U result = lhs * rhs;
	T value;
	memcpy(&value, &result, sizeof(value));
	pushStack(runtime, value);
}

template <typename T>
static inline bool bytecodeDiv(ls_runtime& runtime) {
	// Division and modulo are the only integer arithmetic ops that can fail at
	// runtime here; we reject zero divisors rather than letting the host UB leak
	// into the VM.
	const T rhs = popStack<T>(runtime);
	const T lhs = popStack<T>(runtime);
	if (rhs == T{}) return false;
	pushStack(runtime, lhs / rhs);
	return true;
}

template <typename T>
static inline bool bytecodeMod(ls_runtime& runtime) {
	// Modulo mirrors division's zero check and keeps the remainder in the same
	// signedness as the source expression.
	const T rhs = popStack<T>(runtime);
	const T lhs = popStack<T>(runtime);
	if (rhs == T{}) return false;
	pushStack(runtime, lhs % rhs);
	return true;
}

template <typename Src>
static inline bool bytecodeCastFrom(ls_runtime& runtime, ls_type_kind dst_kind) {
	const Src src = popStack<Src>(runtime);
	switch (dst_kind) {
		case LS_TYPE_BOOL: pushStack(runtime, (u8)(src != Src{})); return true;
		case LS_TYPE_I8: pushStack(runtime, (i8)src); return true;
		case LS_TYPE_U8: pushStack(runtime, (u8)src); return true;
		case LS_TYPE_I16: pushStack(runtime, (i16)src); return true;
		case LS_TYPE_U16: pushStack(runtime, (u16)src); return true;
		case LS_TYPE_I32: case LS_TYPE_ENUM: pushStack(runtime, (i32)src); return true;
		case LS_TYPE_U32: pushStack(runtime, (u32)src); return true;
		case LS_TYPE_I64: pushStack(runtime, (i64)src); return true;
		case LS_TYPE_U64: pushStack(runtime, (u64)src); return true;
		case LS_TYPE_F32: pushStack(runtime, (float)src); return true;
		case LS_TYPE_F64: pushStack(runtime, (double)src); return true;
		default: return false;
	}
}

static inline ls_type_kind bytecodeCastKind(ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_ENUM:
		case LS_TYPE_UNTYPED_INT: return LS_TYPE_I32;
		case LS_TYPE_UNTYPED_FLOAT: return LS_TYPE_F32;
		default: return kind;
	}
}

static bool bytecodeCastByType(ls_runtime& runtime, ls_type_kind src_kind, ls_type_kind dst_kind) {
	switch (bytecodeCastKind(src_kind)) {
		case LS_TYPE_BOOL: return bytecodeCastFrom<u8>(runtime, bytecodeCastKind(dst_kind));
		case LS_TYPE_I8: return bytecodeCastFrom<i8>(runtime, bytecodeCastKind(dst_kind));
		case LS_TYPE_U8: return bytecodeCastFrom<u8>(runtime, bytecodeCastKind(dst_kind));
		case LS_TYPE_I16: return bytecodeCastFrom<i16>(runtime, bytecodeCastKind(dst_kind));
		case LS_TYPE_U16: return bytecodeCastFrom<u16>(runtime, bytecodeCastKind(dst_kind));
		case LS_TYPE_I32: return bytecodeCastFrom<i32>(runtime, bytecodeCastKind(dst_kind));
		case LS_TYPE_U32: return bytecodeCastFrom<u32>(runtime, bytecodeCastKind(dst_kind));
		case LS_TYPE_I64: return bytecodeCastFrom<i64>(runtime, bytecodeCastKind(dst_kind));
		case LS_TYPE_U64: return bytecodeCastFrom<u64>(runtime, bytecodeCastKind(dst_kind));
		case LS_TYPE_F32: return bytecodeCastFrom<float>(runtime, bytecodeCastKind(dst_kind));
		case LS_TYPE_F64: return bytecodeCastFrom<double>(runtime, bytecodeCastKind(dst_kind));
		default: return false;
	}
}

template <typename T, typename Compare>
static inline void bytecodeCompare(ls_runtime& runtime, Compare cmp) {
	const T rhs = popStack<T>(runtime);
	const T lhs = popStack<T>(runtime);
	pushStack(runtime, (u8)(cmp(lhs, rhs) ? 1 : 0));
}

template <typename Compare>
static bool bytecodeCompareByType(ls_runtime& runtime, ls_type_kind kind, Compare cmp) {
	switch (kind) {
		case LS_TYPE_BOOL:
		case LS_TYPE_U8: bytecodeCompare<u8>(runtime, cmp); return true;
		case LS_TYPE_I8: bytecodeCompare<i8>(runtime, cmp); return true;
		case LS_TYPE_U16: bytecodeCompare<u16>(runtime, cmp); return true;
		case LS_TYPE_I16: bytecodeCompare<i16>(runtime, cmp); return true;
		case LS_TYPE_U32: bytecodeCompare<u32>(runtime, cmp); return true;
		case LS_TYPE_I32:
		case LS_TYPE_ENUM:
		case LS_TYPE_UNTYPED_INT: bytecodeCompare<i32>(runtime, cmp); return true;
		case LS_TYPE_U64: bytecodeCompare<u64>(runtime, cmp); return true;
		case LS_TYPE_I64: bytecodeCompare<i64>(runtime, cmp); return true;
		case LS_TYPE_F32: bytecodeCompare<float>(runtime, cmp); return true;
		case LS_TYPE_F64: bytecodeCompare<double>(runtime, cmp); return true;
		default: return false;
	}
}

static bool decodeFunctionHandle(u64 raw, i32* fn_idx) {
	if (raw == 0) return false;
	*fn_idx = (i32)(raw - 1);
	return *fn_idx >= 0;
}

bool callBytecodeCode(
	ls_runtime* runtime,
	const u8* code,
	size_t code_size,
	i32 param_count,
	i32 local_count,
	i32 result_count
);

static bool callBytecodeFunctionValue(
	ls_runtime* runtime,
	u64 handle,
	size_t param_slot_count
) {
	i32 fn_idx = -1;
	if (!decodeFunctionHandle(handle, &fn_idx)) return false;
	if (!runtime || !runtime->bytecode) return false;
	if ((size_t)fn_idx >= (size_t)runtime->bytecode->functions.size) return false;
	BytecodeFunction& fn = runtime->bytecode->functions.data[fn_idx];
	// Indirect calls carry flattened VM slots, not source parameter count.
	// A single struct argument may occupy multiple stack entries.
	if ((size_t)fn.param_slot_count != param_slot_count) return false;
	return callBytecodeCode(runtime, &runtime->bytecode->code.data[fn.code_offset], (size_t)fn.code_size, fn.param_slot_count, fn.local_count, fn.return_count);
}

static bool finishCall(ls_runtime& runtime, size_t frame_base, size_t result_stack_base, size_t result_count) {
	if (runtime.stack.size() < result_stack_base) return false;
	if (runtime.stack.size() < result_stack_base + result_count) return false;
	struct ResultValue {
		u64 value;
		u8 kind;
	};
	std::vector<ResultValue> result;
	for (size_t i = 0; i < result_count; ++i) {
		const size_t idx = runtime.stack.size() - result_count + i;
		result.push_back({runtime.stack[idx], runtime.stack_kinds[idx]});
		retainStackValue(&runtime, runtime.stack[idx], runtime.stack_kinds[idx]);
	}
	stackResize(&runtime, frame_base);
	for (const ResultValue& value : result) stackPushRaw(&runtime, value.value, value.kind);
	return true;
}

static bool callBytecodeCode(
	ls_runtime* runtime,
	const u8* code,
	size_t code_size,
	i32 param_count,
	i32 local_count,
	i32 result_count
) {
	if (!runtime || !runtime->bytecode) return false;
	if (runtime->stack.size() < (size_t)param_count) return false;
	ls_bytecode& bytecode = *runtime->bytecode;
	const size_t stack_base = runtime->stack.size() - (size_t)param_count;
	const size_t frame_base = stack_base;
	const size_t local_base = frame_base + (size_t)param_count;
	const size_t result_stack_base = local_base + (size_t)local_count;
	stackResize(runtime, result_stack_base);
	
	const u8* ip = code;
	const u8* ip_end = ip + code_size;
	while (ip != ip_end) {
		BytecodeOp op = BytecodeOp(*ip);
		++ip;
		switch (op) {
			case BytecodeOp::LOAD_CONST8: {
				u64 c = 0;
				memcpy(&c, ip, 1);
				ip += 1;
				pushStack(*runtime, c);
				break;
			}
			case BytecodeOp::LOAD_CONST16: { 
				u64 c = 0;
				memcpy(&c, ip, 2);
				ip += 2;
				pushStack(*runtime, c);
				break;
			}
			case BytecodeOp::LOAD_CONST32: {
				u64 c = 0;
				memcpy(&c, ip, 4);
				ip += 4;
				pushStack(*runtime, c);
				break;
			}
			case BytecodeOp::LOAD_CONST64: {
				u64 c = 0;
				memcpy(&c, ip, 8);
				ip += 8;
				pushStack(*runtime, c);
				break;
			}
			case BytecodeOp::LOAD_STRING: {
				u32 idx = 0;
				memcpy(&idx, ip, sizeof(idx));
				ip += sizeof(idx);
				if (idx >= (u32)bytecode.string_literals.size) return false;
				ls_string* str = bytecode.string_literals.data[idx];
				retainString(str);
				stackPushRaw(runtime, (u64)(uintptr)str, (u8)StackValueKind::STRING);
				break;
			}
			case BytecodeOp::LOAD_PARAM: {
				const u8 param_idx = *ip;
				++ip;
				if (param_idx >= param_count) return false;
				stackPushCopy(runtime, runtime->stack[stack_base + param_idx], runtime->stack_kinds[stack_base + param_idx]);
				break;
			}
			case BytecodeOp::LOAD_GLOBAL: {
				u32 global_idx = 0;
				memcpy(&global_idx, ip, sizeof(global_idx));
				ip += sizeof(global_idx);
				if (!runtime->bytecode || global_idx >= (u32)runtime->bytecode->global_count) return false;
				stackPushCopy(runtime, runtime->stack[global_idx], runtime->stack_kinds[global_idx]);
				break;
			}
			case BytecodeOp::LOAD_LOCAL: {
				const u8 local_idx = *ip;
				++ip;
				if (local_idx >= local_count) return false;
				stackPushCopy(runtime, runtime->stack[local_base + local_idx], runtime->stack_kinds[local_base + local_idx]);
				break;
			}
			case BytecodeOp::STORE_GLOBAL: {
				u32 global_idx = 0;
				memcpy(&global_idx, ip, sizeof(global_idx));
				ip += sizeof(global_idx);
				if (!runtime->bytecode || global_idx >= (u32)runtime->bytecode->global_count) return false;
				if (runtime->stack.empty()) return false;
				stackOverwrite(runtime, global_idx, runtime->stack.back(), runtime->stack_kinds.back());
				stackPop(runtime);
				break;
			}
			case BytecodeOp::STORE_LOCAL: {
				const u8 local_idx = *ip;
				++ip;
				if (local_idx >= local_count) return false;
				if (runtime->stack.empty()) return false;
				stackOverwrite(runtime, local_base + local_idx, runtime->stack.back(), runtime->stack_kinds.back());
				stackPop(runtime);
				break;
			}
			case BytecodeOp::LOAD_INDIRECT: {
				// The compiler pushes an absolute slot index first. We pop that
				// address, then copy the pointed-to slots back onto the evaluation
				// stack. This is the runtime side of `ref` reads.
				u8 slot_count = *ip;
				++ip;
				if (runtime->stack.empty()) return false;
				const size_t address = (size_t)runtime->stack.back();
				stackPop(runtime);
				if (address + slot_count > runtime->stack.size()) return false;
				for (u8 i = 0; i < slot_count; ++i) stackPushCopy(runtime, runtime->stack[address + i], runtime->stack_kinds[address + i]);
				break;
			}
			case BytecodeOp::STORE_INDIRECT: {
				// The stack layout here is:
				// - value payload slots
				// - address slot on top
				//
				// We pop the address, then write the payload back-to-front so the
				// first logical slot ends up at the lowest address.
				u8 slot_count = *ip;
				++ip;
				if (runtime->stack.size() < (size_t)slot_count + 1) return false;
				const size_t address = (size_t)runtime->stack.back();
				stackPop(runtime);
				if (address + slot_count > runtime->stack.size()) return false;
				for (i32 i = (i32)slot_count - 1; i >= 0; --i) {
					const u64 value = runtime->stack.back();
					const u8 kind = runtime->stack_kinds.back();
					stackOverwrite(runtime, address + (size_t)i, value, kind);
					stackPop(runtime);
				}
				break;
			}
			case BytecodeOp::NOT_BOOL: {
				const u8 value = (u8)popStack<u64>(*runtime);
				pushStack(*runtime, (u8)(!value));
				break;
			}
			case BytecodeOp::JUMP: {
				i32 offset = 0;
				memcpy(&offset, ip, sizeof(offset));
				ip += sizeof(offset);
				ip += offset;
				if (ip < code || ip > ip_end) return false;
				break;
			}
			case BytecodeOp::JUMP_IF_FALSE: {
				i32 offset = 0;
				memcpy(&offset, ip, sizeof(offset));
				ip += sizeof(offset);
				const u64 cond = popStack<u64>(*runtime);
				if (!cond) {
					ip += offset;
					if (ip < code || ip > ip_end) return false;
				}
				break;
			}
			case BytecodeOp::POP: {
				if (runtime->stack.empty()) return false;
				stackPop(runtime);
				break;
			}
			case BytecodeOp::CALL: {
				u32 callee_idx = 0;
				memcpy(&callee_idx, ip, sizeof(callee_idx));
				ip += sizeof(callee_idx);
				if (callee_idx >= (u32)bytecode.functions.size) return false;
				BytecodeFunction& fn = bytecode.functions.data[callee_idx];
				if (!callBytecodeCode(runtime, &bytecode.code.data[fn.code_offset], (size_t)fn.code_size, fn.param_slot_count, fn.local_count, fn.return_count)) return false;
				break;
			}
			case BytecodeOp::CALL_NATIVE: {
				u32 callee_idx = 0;
				memcpy(&callee_idx, ip, sizeof(callee_idx));
				ip += sizeof(callee_idx);
				if (callee_idx >= (u32)bytecode.native_functions.size) return false;
				BytecodeNativeFunction& fn = bytecode.native_functions.data[callee_idx];
				if (callee_idx >= runtime->native_functions.size()) return false;
				ls_native_fn& binding = runtime->native_functions[callee_idx];
				const size_t arg_count = (size_t)fn.params.size;
				if (runtime->stack.size() < arg_count) return false;
				const size_t arg_base = runtime->stack.size() - arg_count;
				const size_t result_count = fn.return_count;
				const size_t stack_size_before = runtime->stack.size();
				if (!binding) return false;
				binding(runtime);
				if (runtime->stack.size() != stack_size_before + result_count) return false;
				if (result_count > 0) {
					const size_t result_base = stack_size_before;
					for (size_t i = 0; i < result_count; ++i) {
						const u64 value = runtime->stack[result_base + i];
						const u8 kind = runtime->stack_kinds[result_base + i];
						stackOverwrite(runtime, arg_base + i, value, kind);
					}
				}
				stackResize(runtime, arg_base + result_count);
				break;
			}
			case BytecodeOp::CALL_INDIRECT: {
				u8 param_slot_count = *ip;
				++ip;
				if (runtime->stack.size() < (size_t)param_slot_count + 1) return false;
				// Layout is: function handle, then the flattened argument slots.
				const size_t handle_pos = runtime->stack.size() - (size_t)param_slot_count - 1;
				const u64 handle = runtime->stack[handle_pos];
				stackErase(runtime, handle_pos);
				if (!callBytecodeFunctionValue(runtime, handle, param_slot_count)) return false;
				break;
			}
			case BytecodeOp::ADD_I8: bytecodeAdd<i8>(*runtime); break;
			case BytecodeOp::ADD_U8: bytecodeAdd<u8>(*runtime); break;
			case BytecodeOp::ADD_I16: bytecodeAdd<i16>(*runtime); break;
			case BytecodeOp::ADD_U16: bytecodeAdd<u16>(*runtime); break;
			case BytecodeOp::ADD_I32: bytecodeAdd<i32>(*runtime); break;
			case BytecodeOp::ADD_U32: bytecodeAdd<u32>(*runtime); break;
			case BytecodeOp::ADD_I64: bytecodeAdd<i64>(*runtime); break;
			case BytecodeOp::ADD_U64: bytecodeAdd<u64>(*runtime); break;
			case BytecodeOp::ADD_F32: bytecodeAddFloat<float>(*runtime); break;
			case BytecodeOp::ADD_F64: bytecodeAddFloat<double>(*runtime); break;
			case BytecodeOp::ADD_STRING: if (!bytecodeAddString(*runtime)) return false; break;
			case BytecodeOp::SUB_I8: bytecodeSub<i8>(*runtime); break;
			case BytecodeOp::SUB_U8: bytecodeSub<u8>(*runtime); break;
			case BytecodeOp::SUB_I16: bytecodeSub<i16>(*runtime); break;
			case BytecodeOp::SUB_U16: bytecodeSub<u16>(*runtime); break;
			case BytecodeOp::SUB_I32: bytecodeSub<i32>(*runtime); break;
			case BytecodeOp::SUB_U32: bytecodeSub<u32>(*runtime); break;
			case BytecodeOp::SUB_I64: bytecodeSub<i64>(*runtime); break;
			case BytecodeOp::SUB_U64: bytecodeSub<u64>(*runtime); break;
			case BytecodeOp::SUB_F32: bytecodeSubFloat<float>(*runtime); break;
			case BytecodeOp::SUB_F64: bytecodeSubFloat<double>(*runtime); break;
			// The VM dispatch is intentionally explicit so each opcode documents the
			// concrete storage width it operates on.
			case BytecodeOp::MUL_I8: bytecodeMul<i8>(*runtime); break;
			case BytecodeOp::MUL_U8: bytecodeMul<u8>(*runtime); break;
			case BytecodeOp::MUL_I16: bytecodeMul<i16>(*runtime); break;
			case BytecodeOp::MUL_U16: bytecodeMul<u16>(*runtime); break;
			case BytecodeOp::MUL_I32: bytecodeMul<i32>(*runtime); break;
			case BytecodeOp::MUL_U32: bytecodeMul<u32>(*runtime); break;
			case BytecodeOp::MUL_I64: bytecodeMul<i64>(*runtime); break;
			case BytecodeOp::MUL_U64: bytecodeMul<u64>(*runtime); break;
			case BytecodeOp::MUL_F32: bytecodeMulFloat<float>(*runtime); break;
			case BytecodeOp::MUL_F64: bytecodeMulFloat<double>(*runtime); break;
			case BytecodeOp::DIV_I8: if (!bytecodeDiv<i8>(*runtime)) return false; break;
			case BytecodeOp::DIV_U8: if (!bytecodeDiv<u8>(*runtime)) return false; break;
			case BytecodeOp::DIV_I16: if (!bytecodeDiv<i16>(*runtime)) return false; break;
			case BytecodeOp::DIV_U16: if (!bytecodeDiv<u16>(*runtime)) return false; break;
			case BytecodeOp::DIV_I32: if (!bytecodeDiv<i32>(*runtime)) return false; break;
			case BytecodeOp::DIV_U32: if (!bytecodeDiv<u32>(*runtime)) return false; break;
			case BytecodeOp::DIV_I64: if (!bytecodeDiv<i64>(*runtime)) return false; break;
			case BytecodeOp::DIV_U64: if (!bytecodeDiv<u64>(*runtime)) return false; break;
			case BytecodeOp::DIV_F32: if (!bytecodeDivFloat<float>(*runtime)) return false; break;
			case BytecodeOp::DIV_F64: if (!bytecodeDivFloat<double>(*runtime)) return false; break;
			case BytecodeOp::MOD_I8: if (!bytecodeMod<i8>(*runtime)) return false; break;
			case BytecodeOp::MOD_U8: if (!bytecodeMod<u8>(*runtime)) return false; break;
			case BytecodeOp::MOD_I16: if (!bytecodeMod<i16>(*runtime)) return false; break;
			case BytecodeOp::MOD_U16: if (!bytecodeMod<u16>(*runtime)) return false; break;
			case BytecodeOp::MOD_I32: if (!bytecodeMod<i32>(*runtime)) return false; break;
			case BytecodeOp::MOD_U32: if (!bytecodeMod<u32>(*runtime)) return false; break;
			case BytecodeOp::MOD_I64: if (!bytecodeMod<i64>(*runtime)) return false; break;
			case BytecodeOp::MOD_U64: if (!bytecodeMod<u64>(*runtime)) return false; break;
			case BytecodeOp::CAST: {
				const u8 src_kind = *ip;
				++ip;
				const u8 dst_kind = *ip;
				++ip;
				if (!bytecodeCastByType(*runtime, (ls_type_kind)src_kind, (ls_type_kind)dst_kind)) return false;
				break;
			}
			case BytecodeOp::CMP_EQ: {
				const u8 kind = *ip;
				++ip;
				if (!bytecodeCompareByType(*runtime, (ls_type_kind)kind, [](auto lhs, auto rhs) { return lhs == rhs; })) return false;
				break;
			}
			case BytecodeOp::CMP_NE: {
				const u8 kind = *ip;
				++ip;
				if (!bytecodeCompareByType(*runtime, (ls_type_kind)kind, [](auto lhs, auto rhs) { return lhs != rhs; })) return false;
				break;
			}
			case BytecodeOp::CMP_GT: {
				const u8 kind = *ip;
				++ip;
				if (!bytecodeCompareByType(*runtime, (ls_type_kind)kind, [](auto lhs, auto rhs) { return lhs > rhs; })) return false;
				break;
			}
			case BytecodeOp::CMP_GE: {
				const u8 kind = *ip;
				++ip;
				if (!bytecodeCompareByType(*runtime, (ls_type_kind)kind, [](auto lhs, auto rhs) { return lhs >= rhs; })) return false;
				break;
			}
			case BytecodeOp::CMP_LT: {
				const u8 kind = *ip;
				++ip;
				if (!bytecodeCompareByType(*runtime, (ls_type_kind)kind, [](auto lhs, auto rhs) { return lhs < rhs; })) return false;
				break;
			}
			case BytecodeOp::CMP_LE: {
				const u8 kind = *ip;
				++ip;
				if (!bytecodeCompareByType(*runtime, (ls_type_kind)kind, [](auto lhs, auto rhs) { return lhs <= rhs; })) return false;
				break;
			}
			case BytecodeOp::ABORT:
				return false;
			case BytecodeOp::RETURN:
				return finishCall(*runtime, frame_base, result_stack_base, (size_t)result_count);
		}
	}
	// we reached end of function
	return finishCall(*runtime, frame_base, result_stack_base, (size_t)result_count);
}

extern "C" {

ls_runtime* ls_runtime_create(ls_bytecode* bytecode) {
	return createBytecodeRuntime(bytecode);
}

void ls_runtime_destroy(ls_runtime* runtime) {
	destroyBytecodeRuntime(runtime);
}

ls_result ls_runtime_set_native_function_callback(
	ls_runtime* runtime,
	int function_index,
	ls_native_fn callback
) {
	if (!runtime || !runtime->bytecode) return LS_RESULT_FAILURE;
	if (function_index < 0 || function_index >= (i32)runtime->native_functions.size()) return LS_RESULT_FAILURE;
	runtime->native_functions[(size_t)function_index] = callback;
	return LS_RESULT_OK;
}

i32 ls_to_i32(ls_runtime* runtime, i32 index) {
	if (!runtime) return 0;
	return i32(runtime->stack[runtimeStackIndex(runtime, index)]);
}

i32 ls_to_bool(ls_runtime* runtime, i32 index) {
	return ls_to_i32(runtime, index) != 0;
}

u32 ls_to_u32(ls_runtime* runtime, i32 index) {
	return u32(runtime->stack[runtimeStackIndex(runtime, index)]);
}

i64 ls_to_i64(ls_runtime* runtime, i32 index) {
	return (i64)runtime->stack[runtimeStackIndex(runtime, index)];
}

void* ls_to_ptr(ls_runtime* runtime, i32 index) {
	return (void*)(uintptr)runtime->stack[runtimeStackIndex(runtime, index)];
}

u64 ls_to_u64(ls_runtime* runtime, i32 index) {
	return runtime->stack[runtimeStackIndex(runtime, index)];
}

float ls_to_f32(ls_runtime* runtime, i32 index) {
	float value = 0.0f;
	memcpy(&value, &runtime->stack[runtimeStackIndex(runtime, index)], sizeof(value));
	return value;
}

double ls_to_f64(ls_runtime* runtime, i32 index) {
	double value = 0.0;
	memcpy(&value, &runtime->stack[runtimeStackIndex(runtime, index)], sizeof(value));
	return value;
}

ls_string_view ls_to_string(ls_runtime* runtime, i32 index) {
	ls_string_view result = {};
	if (!runtime) return result;
	const i32 stack_index = runtimeStackIndex(runtime, index);
	if (stack_index < 0 || stack_index >= (i32)runtime->stack.size()) return result;
	const u64 value = runtime->stack[(size_t)stack_index];
	if (isStringValue(runtime, (size_t)stack_index)) {
		const ls_string* str = toStringObject(value);
		result.begin = str->chars;
		result.end = str->chars + str->length;
		return result;
	}
	const char* begin = (const char*)(uintptr)value;
	if (!begin) return result;
	result.begin = begin;
	result.end = begin + strlen(begin);
	return result;
}

void ls_push_bool(ls_runtime* runtime, int value) {
	stackPushRaw(runtime, value ? 1u : 0u);
}

void ls_push_i32(ls_runtime* runtime, i32 value) {
	stackPushRaw(runtime, (u64)value);
}

void ls_push_u32(ls_runtime* runtime, u32 value) {
	stackPushRaw(runtime, (u64)value);
}

void ls_push_i64(ls_runtime* runtime, i64 value) {
	stackPushRaw(runtime, (u64)value);
}

void ls_push_ptr(ls_runtime* runtime, void* value) {
	stackPushRaw(runtime, (u64)(uintptr)value);
}

void ls_push_u64(ls_runtime* runtime, u64 value) {
	stackPushRaw(runtime, value);
}

void ls_push_f32(ls_runtime* runtime, float value) {
	u64 raw = 0;
	memcpy(&raw, &value, sizeof(value));
	stackPushRaw(runtime, raw);
}

void ls_push_f64(ls_runtime* runtime, double value) {
	u64 raw = 0;
	memcpy(&raw, &value, sizeof(value));
	stackPushRaw(runtime, raw);
}

void ls_push_string(ls_runtime* runtime, ls_string_view value) {
	if (!runtime) return;
	if (!runtime->bytecode) {
		stackPushRaw(runtime, 0);
		return;
	}
	const size_t len = (size_t)(value.end - value.begin);
	ls_string* storage = (ls_string*)bytecodeAllocate(&runtime->bytecode->host, sizeof(ls_string) + len, alignof(ls_string));
	if (!storage) {
		stackPushRaw(runtime, 0);
		return;
	}
	storage->host = runtime->bytecode->host;
	storage->ref_count = 1;
	storage->length = (u32)len;
	if (len > 0) memcpy(storage->chars, value.begin, len);
	storage->chars[len] = '\0';
	stackPushRaw(runtime, (u64)(uintptr)storage, (u8)StackValueKind::STRING);
}

void ls_push_null(ls_runtime* runtime) {
	stackPushRaw(runtime, 0);
}

ls_result ls_call_index(ls_runtime* runtime, i32 function_index) {
	BytecodeFunction& fn = runtime->bytecode->functions.data[function_index];
	// The VM frames are sized in stack slots, not source-level parameters.
	// This keeps multi-slot values and `ref` parameters aligned with runtime layout.
	if (fn.param_slot_count <= 0 && fn.params.size > 0) return LS_RESULT_FAILURE;

	const size_t global_count = (size_t)runtime->bytecode->global_count;
	if (runtime->stack.size() < global_count + (size_t)fn.param_slot_count) return LS_RESULT_FAILURE;

	if (!runtime->globals_initialized) {
		runtime->globals_initialized = true;
		if (runtime->bytecode->global_init_code.size > 0) {
			if (!callBytecodeCode(runtime, runtime->bytecode->global_init_code.data, (size_t)runtime->bytecode->global_init_code.size, 0, 0, 0)) {
				return LS_RESULT_FAILURE;
			}
		}
	}

	return callBytecodeCode(runtime, &runtime->bytecode->code.data[fn.code_offset], (size_t)fn.code_size, fn.param_slot_count, fn.local_count, fn.return_count) ? LS_RESULT_OK : LS_RESULT_FAILURE;
}

ls_result ls_call(ls_runtime* runtime, ls_string_view function_name) {
	const i32 function_index = runtime && runtime->bytecode ? bytecodeFindFunction(runtime->bytecode, function_name) : -1;
	if (function_index < 0) return LS_RESULT_FAILURE;
	return ls_call_index(runtime, function_index);
}

ls_type_kind ls_bytecode_runtime_result_kind(ls_runtime* runtime, ls_string_view function_name) {
	if (!runtime || !runtime->bytecode) return LS_TYPE_VOID;
	const i32 function_index = bytecodeFindFunction(runtime->bytecode, function_name);
	if (function_index < 0) return LS_TYPE_VOID;
	return runtime->bytecode->functions.data[(size_t)function_index].return_type.kind;
}

}
