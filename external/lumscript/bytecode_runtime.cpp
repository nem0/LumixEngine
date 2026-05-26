#include "bytecode.h"

#include <new>
#include <type_traits>
#include "string_utils.h"

ls_runtime::ls_runtime(ls_bytecode* bytecode_)
	: bytecode(bytecode_)
{}

template <typename T>
void pushStack(ls_runtime& runtime, const T& value);

template <typename T>
T popStack(ls_runtime& runtime);

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

static bool callBytecodeCode(
	ls_runtime* runtime,
	const u8* code,
	size_t code_size,
	i32 param_count,
	i32 local_count,
	i32 result_count
);

static bool decodeFunctionHandle(u64 raw, i32* fn_idx) {
	if (raw == 0) return false;
	*fn_idx = (i32)(raw - 1);
	return *fn_idx >= 0;
}

static bool callBytecodeFunctionValue(
	ls_runtime* runtime,
	u64 handle,
	size_t param_count
) {
	(void)param_count;
	i32 fn_idx = -1;
	if (!decodeFunctionHandle(handle, &fn_idx)) return false;
	if (!runtime || !runtime->bytecode) return false;
	if ((size_t)fn_idx >= runtime->bytecode->functions.size()) return false;
	BytecodeFunction& fn = runtime->bytecode->functions[fn_idx];
	return callBytecodeCode(runtime, &runtime->bytecode->code[fn.code_offset], (size_t)fn.code_size, fn.param_count, fn.local_count, fn.return_count);
}

ls_runtime* createBytecodeRuntime(ls_bytecode* bytecode) {
	if (!bytecode) return nullptr;
	void* mem = bytecodeAllocate(&bytecode->host, sizeof(ls_runtime), alignof(ls_runtime));
	if (!mem) return nullptr;
	ls_runtime* runtime = new (mem) ls_runtime(bytecode);
	runtime->stack.resize((size_t)bytecode->global_count);
	return runtime;
}

void destroyBytecodeRuntime(ls_runtime* runtime) {
	if (!runtime) return;
	ls_host host = runtime->bytecode ? runtime->bytecode->host : ls_host{};
	runtime->~ls_runtime();
	bytecodeDeallocate(&host, runtime);
}

template <typename T>
void pushStack(ls_runtime& runtime, const T& value) {
	u64 v = 0;
	static_assert(sizeof(value) <= sizeof(v));
	memcpy(&v, &value, sizeof(value));
	if constexpr (std::is_signed_v<T> && sizeof(T) < sizeof(v)) {
		if (value < 0) {
			const u64 sign_mask = ~((u64(1) << (sizeof(T) * 8)) - 1);
			v |= sign_mask;
		}
	}
	runtime.stack.push_back(v);
}

template <typename T>
T popStack(ls_runtime& runtime) {
	T value;
	static_assert(sizeof(value) <= sizeof(runtime.stack.back()));
	memcpy(&value, &runtime.stack.back(), sizeof(value));
	runtime.stack.pop_back();
	return value;
}

static bool finishCall(ls_runtime& runtime, size_t frame_base, size_t result_stack_base, size_t result_count) {
	if (runtime.stack.size() < result_stack_base) return false;
	if (runtime.stack.size() < result_stack_base + result_count) return false;
	std::vector<u64> result;
	for (size_t i = 0; i < result_count; ++i) result.push_back(runtime.stack[runtime.stack.size() - result_count + i]);
	runtime.stack.resize(frame_base);
	for (u64 value : result) runtime.stack.push_back(value);
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
	runtime->stack.resize(result_stack_base);
	
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
			case BytecodeOp::LOAD_PARAM: {
				const u8 param_idx = *ip;
				++ip;
				if (param_idx >= param_count) return false;
				pushStack(*runtime, runtime->stack[stack_base + param_idx]);
				break;
			}
			case BytecodeOp::LOAD_GLOBAL: {
				u32 global_idx = 0;
				memcpy(&global_idx, ip, sizeof(global_idx));
				ip += sizeof(global_idx);
				if (!runtime->bytecode || global_idx >= (u32)runtime->bytecode->global_count) return false;
				pushStack(*runtime, runtime->stack[global_idx]);
				break;
			}
			case BytecodeOp::LOAD_LOCAL: {
				const u8 local_idx = *ip;
				++ip;
				if (local_idx >= local_count) return false;
				pushStack(*runtime, runtime->stack[local_base + local_idx]);
				break;
			}
			case BytecodeOp::STORE_GLOBAL: {
				u32 global_idx = 0;
				memcpy(&global_idx, ip, sizeof(global_idx));
				ip += sizeof(global_idx);
				if (runtime->stack.empty()) return false;
				if (!runtime->bytecode || global_idx >= (u32)runtime->bytecode->global_count) return false;
				runtime->stack[global_idx] = runtime->stack.back();
				runtime->stack.pop_back();
				break;
			}
			case BytecodeOp::STORE_LOCAL: {
				const u8 local_idx = *ip;
				++ip;
				if (runtime->stack.empty()) return false;
				if (local_idx >= local_count) return false;
				runtime->stack[local_base + local_idx] = runtime->stack.back();
				runtime->stack.pop_back();
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
				runtime->stack.pop_back();
				if (address + slot_count > runtime->stack.size()) return false;
				for (u8 i = 0; i < slot_count; ++i) pushStack(*runtime, runtime->stack[address + i]);
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
				runtime->stack.pop_back();
				if (address + slot_count > runtime->stack.size()) return false;
				for (i32 i = (i32)slot_count - 1; i >= 0; --i) {
					runtime->stack[address + i] = runtime->stack.back();
					runtime->stack.pop_back();
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
				runtime->stack.pop_back();
				break;
			}
			case BytecodeOp::CALL: {
				u32 callee_idx = 0;
				memcpy(&callee_idx, ip, sizeof(callee_idx));
				ip += sizeof(callee_idx);
				if (callee_idx >= bytecode.functions.size()) return false;
				BytecodeFunction& fn = bytecode.functions[callee_idx];
				if (!callBytecodeCode(runtime, &bytecode.code[fn.code_offset], (size_t)fn.code_size, fn.param_count, fn.local_count, fn.return_count)) return false;
				break;
			}
			case BytecodeOp::CALL_NATIVE: {
				u32 callee_idx = 0;
				memcpy(&callee_idx, ip, sizeof(callee_idx));
				ip += sizeof(callee_idx);
				if (callee_idx >= bytecode.native_functions.size()) return false;
				BytecodeNativeFunction& fn = bytecode.native_functions[callee_idx];
				const size_t arg_count = (size_t)fn.params.size();
				if (runtime->stack.size() < arg_count) return false;
				const size_t arg_base = runtime->stack.size() - arg_count;
				const size_t result_count = fn.return_type.kind != LS_TYPE_VOID ? 1u : 0u;
				const size_t stack_size_before = runtime->stack.size();
				if (!fn.callback || !fn.callback(runtime, arg_count, result_count, fn.userdata)) return false;
				if (runtime->stack.size() != stack_size_before + result_count) return false;
				if (result_count > 0) {
					const size_t result_base = stack_size_before;
					for (size_t i = 0; i < result_count; ++i) {
						runtime->stack[arg_base + i] = runtime->stack[result_base + i];
					}
				}
				runtime->stack.resize(arg_base + result_count);
				break;
			}
			case BytecodeOp::CALL_INDIRECT: {
				u8 param_count = *ip;
				++ip;
				if (runtime->stack.size() < (size_t)param_count + 1) return false;
				const size_t handle_pos = runtime->stack.size() - (size_t)param_count - 1;
				const u64 handle = runtime->stack[handle_pos];
				runtime->stack.erase(runtime->stack.begin() + (std::ptrdiff_t)handle_pos);
				if (!callBytecodeFunctionValue(runtime, handle, param_count)) return false;
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
			case BytecodeOp::RETURN:
				return finishCall(*runtime, frame_base, result_stack_base, (size_t)result_count);
		}
	}
	// we reached end of function
	return finishCall(*runtime, frame_base, result_stack_base, (size_t)result_count);
}

static bool initializeGlobals(ls_runtime* runtime) {
	if (!runtime || !runtime->bytecode) return false;
	if (runtime->globals_initialized) return true;
	runtime->globals_initialized = true;
	if (runtime->bytecode->global_init_code.empty()) return true;
	return callBytecodeCode(runtime, runtime->bytecode->global_init_code.data(), runtime->bytecode->global_init_code.size(), 0, 0, 0);
}

bool callBytecodeRuntime(ls_runtime* runtime, i32 function_index) {
	if (!runtime || !runtime->bytecode) return false;
	if (!initializeGlobals(runtime)) return false;
	if (function_index < 0 || function_index >= runtime->bytecode->functions.size()) return false;
	BytecodeFunction& fn = runtime->bytecode->functions[function_index];
	return callBytecodeCode(runtime, &runtime->bytecode->code[fn.code_offset], (size_t)fn.code_size, fn.param_count, fn.local_count, fn.return_count);
}
