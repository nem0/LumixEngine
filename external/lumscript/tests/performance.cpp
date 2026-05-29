#include <chrono>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "../ast.h"
#include "../capi.h"
#include "../string_utils.h"

int main() {
    const char* env_lines = std::getenv("PERFT_LINES");
    const int LINES = env_lines ? std::atoi(env_lines) : 100000; // ~100k LOC target by default
    std::string src;
    src.reserve((size_t)LINES * 32);

    // Add imports and a small main so the module has an entry point
    src += "import \"core:math\" as math;\n";
    src += "fn main() : void { var _ = math.sin(0.5); }\n";

    for (int i = 0; i < LINES; ++i) {
        src += "fn f_";
        src += std::to_string(i);
        src += "() : void { var x = ";
        src += std::to_string(i);
        src += "; var v = math.sin(1.0); if (x % 2 == 0) { var y = math.cos(2.0); } else { var y = math.sqrt(4.0); } }\n";
    }

    ls_host host = {};
    auto perfPrint = [](void* /*userdata*/, ls_string_view msg) {
        for (const char* c = msg.begin; c != msg.end; ++c) putchar(*c);
    };
    host.print = (ls_print_fn)perfPrint;
    ls_module* module = ls_module_create(&host);
    // Pre-reserve container capacities to avoid repeated allocations during compile
    if (!module) {
        std::fprintf(stderr, "Failed to create module\n");
        return 2;
    }

    // Measure front-end (parse+imports+typecheck) and bytecode separately
    auto fe0 = std::chrono::steady_clock::now();
    ls_result rfe = ls_module_compile(module, makeStringView(src.c_str()), {}, nullptr, nullptr);
    auto fe1 = std::chrono::steady_clock::now();
    long long ms_frontend = std::chrono::duration_cast<std::chrono::milliseconds>(fe1 - fe0).count();

    long long ms_byte = 0;
    ls_result rbyte = LS_RESULT_FAILURE;
    if (rfe == LS_RESULT_OK) {
        auto b0 = std::chrono::steady_clock::now();
        ls_bytecode* bytecode = ls_bytecode_compile(module, &host);
        auto b1 = std::chrono::steady_clock::now();
        rbyte = bytecode ? LS_RESULT_OK : LS_RESULT_FAILURE;
        ms_byte = std::chrono::duration_cast<std::chrono::milliseconds>(b1 - b0).count();
        if (bytecode) ls_bytecode_destroy(bytecode);
    }

    std::printf("frontend=%lld ms, bytecode=%lld ms (fe=%d byte=%d)\n", ms_frontend, ms_byte, (int)rfe, (int)rbyte);

    ls_module_destroy(module);
    return (rfe == LS_RESULT_OK && rbyte == LS_RESULT_OK) ? 0 : 1;
}
