#pragma once

#include <vector>

#include "capi.h"

struct Module;

enum class BytecodeOp : u8 {
	LOAD_CONST, // Load constant 24 bits 
	RETURN
};

enum class BytecodeInstruction : u32 {};

inline BytecodeOp getOp(BytecodeInstruction instruction) {
	return BytecodeOp(u32(instruction) & 0xff);
}

struct BytecodeFunction {
	ls_string_view name;
	i32 first_instruction = 0;
	i32 instruction_count = 0;
	i32 param_count = 0;
};

enum class BytecodeValue : u64 {};

struct ls_bytecode {
	explicit ls_bytecode(const ls_host* host);

	ls_host host;
	std::vector<BytecodeFunction> functions;
	std::vector<BytecodeInstruction> instructions;
};

struct ls_bytecode_runtime {
	explicit ls_bytecode_runtime(ls_bytecode* bytecode);

	ls_bytecode* bytecode = nullptr;
	std::vector<BytecodeValue> stack;
};

ls_bytecode* compileBytecode(Module& module, const ls_host* host);
void destroyBytecode(ls_bytecode* bytecode);
ls_bytecode_runtime* createBytecodeRuntime(ls_bytecode* bytecode);
void destroyBytecodeRuntime(ls_bytecode_runtime* runtime);

bool callBytecodeRuntime(ls_bytecode_runtime* runtime, ls_string_view function_name);
