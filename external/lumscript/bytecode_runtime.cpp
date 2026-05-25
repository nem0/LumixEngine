#include "bytecode.h"

#include <new>

#include "string_utils.h"

ls_bytecode_runtime::ls_bytecode_runtime(ls_bytecode* bytecode_)
	: bytecode(bytecode_)
{}

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

static i32 bytecodeFindFunction(const ls_bytecode& bytecode, ls_string_view name) {
	for (i32 i = 0; i < bytecode.functions.size(); ++i) {
		if (equalStrings(bytecode.functions[i].name, name)) return i;
	}
	return -1;
}

void pushBytecodeValue(ls_bytecode_runtime* runtime, BytecodeValue value) {
	if (!runtime) return;
	runtime->stack.push_back(value);
}

ls_bytecode_runtime* createBytecodeRuntime(ls_bytecode* bytecode) {
	if (!bytecode) return nullptr;
	void* mem = bytecodeAllocate(&bytecode->host, sizeof(ls_bytecode_runtime), alignof(ls_bytecode_runtime));
	return mem ? new (mem) ls_bytecode_runtime(bytecode) : nullptr;
}

void destroyBytecodeRuntime(ls_bytecode_runtime* runtime) {
	if (!runtime) return;
	ls_host host = runtime->bytecode ? runtime->bytecode->host : ls_host{};
	runtime->~ls_bytecode_runtime();
	bytecodeDeallocate(&host, runtime);
}

bool callBytecodeRuntime(ls_bytecode_runtime* runtime, ls_string_view function_name) {
	if (!runtime || !runtime->bytecode) return false;

	ls_bytecode& bytecode = *runtime->bytecode;
	const i32 fn_idx = bytecodeFindFunction(bytecode, function_name);
	if (fn_idx < 0) return false;

	BytecodeFunction& fn = bytecode.functions[fn_idx];
	if (runtime->stack.size() < (size_t)fn.param_count) return false;
	
	const i32 end = fn.first_instruction + fn.instruction_count;
	for (i32 ip = fn.first_instruction; ip < end; ++ip) {
		const BytecodeInstruction& instruction = bytecode.instructions[ip];
		BytecodeOp op = BytecodeOp(u32(instruction) & 0xff);
		u32 payload = u32(instruction) >> 8;
		switch (op) {
			case BytecodeOp::LOAD_CONST: {
				runtime->stack.push_back(BytecodeValue(payload));
				break;
			}
			case BytecodeOp::RETURN:
				// returned value should be on stack
				return true;
		}
	}
	// we reached end of function
	return true;
}
