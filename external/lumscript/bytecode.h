#pragma once

#include <vector>

#include "capi.h"

struct Module;

enum class BytecodeOp : u8 {
	LOAD_CONST8,
	LOAD_CONST16,
	LOAD_CONST32,
	LOAD_CONST64,
	LOAD_PARAM,
	LOAD_GLOBAL,
	LOAD_LOCAL,
	STORE_GLOBAL,
	STORE_LOCAL,
	LOAD_INDIRECT,
	STORE_INDIRECT,
	NOT_BOOL,
	JUMP,
	JUMP_IF_FALSE,
	POP,
	CALL,
	CALL_NATIVE,
	CALL_INDIRECT,
	ADD_I32,
	ADD_U32,
	ADD_I16,
	ADD_U16,
	ADD_I8,
	ADD_U8,
	ADD_I64,
	ADD_U64,
	ADD_F32,
	ADD_F64,
	SUB_I32,
	SUB_U32,
	SUB_I16,
	SUB_U16,
	SUB_I8,
	SUB_U8,
	SUB_I64,
	SUB_U64,
	SUB_F32,
	SUB_F64,
	MUL_I32,
	MUL_U32,
	MUL_I16,
	MUL_U16,
	MUL_I8,
	MUL_U8,
	MUL_I64,
	MUL_U64,
	MUL_F32,
	MUL_F64,
	DIV_I32,
	DIV_U32,
	DIV_I16,
	DIV_U16,
	DIV_I8,
	DIV_U8,
	DIV_I64,
	DIV_U64,
	DIV_F32,
	DIV_F64,
	MOD_I32,
	MOD_U32,
	MOD_I16,
	MOD_U16,
	MOD_I8,
	MOD_U8,
	MOD_I64,
	MOD_U64,
	CAST,
	CMP_EQ,
	CMP_NE,
	CMP_GT,
	CMP_GE,
	CMP_LT,
	CMP_LE,
	RETURN
};

struct BytecodeFunction {
	ls_string_view name;
	std::vector<ls_type> params;
	ls_type return_type = {};
	i32 code_offset = 0;
	i32 code_size = 0;
	i32 param_count = 0;
	i32 local_count = 0;
	i32 return_count = 0;
};

struct BytecodeNativeFunction {
	ls_string_view name;
	std::vector<ls_type> params;
	ls_type return_type = {};
	ls_native_fn callback = nullptr;
	void* userdata = nullptr;
};

enum class BytecodeValue : u64 {};

struct ls_bytecode {
	explicit ls_bytecode(const ls_host* host);

	ls_host host;
	std::vector<BytecodeFunction> functions;
	std::vector<BytecodeNativeFunction> native_functions;
	i32 global_count = 0;
	std::vector<u8> global_init_code;
	std::vector<u8> code;
};

struct ls_runtime {
	explicit ls_runtime(ls_bytecode* bytecode);

	ls_bytecode* bytecode = nullptr;
	std::vector<u64> stack;
	bool globals_initialized = false;
};

ls_bytecode* compileBytecode(Module& module, const ls_host* host);
void destroyBytecode(ls_bytecode* bytecode);
ls_runtime* createBytecodeRuntime(ls_bytecode* bytecode);
void destroyBytecodeRuntime(ls_runtime* runtime);

bool callBytecodeRuntime(ls_runtime* runtime, i32 function_index);
