#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>

#include "../arena.h"
#include "../utils.h"
#include "../bytecode.h"
#include "../capi.h"

static void testPrint(void* userdata, ls_string_view msg) {
	for (const char* c = msg.begin; c != msg.end; ++c) {
		putchar(*c);
	}
}

int main(int argc, char** argv) {
	ls_host host = {};
	host.print = &testPrint;
	ls_default_arena_create(&host.arena);

	const char* source = R"(
		fn fib(n : i32) : i32 {
			if n <= 1 {
				return n;
			}
			return fib(n - 1) + fib(n - 2);
		}

		fn main() : i32 {
			return fib(30);
		}
	)";

	ls_module* module = ls_module_create(&host);
	if (!module) {
		printf("Failed to create module\n");
		return -1;
	}

	if (!ls_module_compile(module, makeStringView(source), makeStringView("fib_perf"), nullptr, nullptr)) {
		printf("Failed to compile module\n");
		return -1;
	}

	ls_bytecode* bytecode = ls_bytecode_compile(module, &host);
	if (!bytecode) {
		printf("Failed to compile bytecode\n");
		return -1;
	}

	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	if (!runtime) {
		printf("Failed to create runtime\n");
		return -1;
	}

	printf("Running fib(30) performance test...\n");
	const auto start_time = std::chrono::steady_clock::now();

	ls_result result = ls_call(runtime, makeStringView("main"));

	const auto end_time = std::chrono::steady_clock::now();
	const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
	const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

	if (result == LS_RESULT_OK) {
		i32 ret_value = ls_to_i32(runtime, -1);
		printf("Result: %d\n", ret_value);
		printf("Time: %lld ms (%lld us)\n", (long long)elapsed_ms, (long long)elapsed_us);
	} else {
		printf("Runtime error: %d\n", result);
	}

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	ls_default_arena_destroy(&host.arena);

	return result == LS_RESULT_OK ? 0 : -1;
}
