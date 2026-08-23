#!/usr/bin/env bash
set -euo pipefail

# Linux build for LumScript. Usage: ./build.sh [release|debug|tests]
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT="$ROOT/build"
MODE=${1:-debug}
CC=${CC:-cc}
CXX=${CXX:-c++}

CFLAGS=(-std=c11 -D_GNU_SOURCE -I"$ROOT" -I"$ROOT/../../src")
CXXFLAGS=(-std=c++20 -I"$ROOT" -I"$ROOT/../../src")
if [[ "$MODE" == release ]]; then
    CFLAGS+=(-O2 -DNDEBUG)
    CXXFLAGS+=(-O2 -DNDEBUG)
else
    CFLAGS+=(-O0 -g)
    CXXFLAGS+=(-O0 -g)
fi
if [[ "$MODE" == tests ]]; then
    CFLAGS+=(-DLS_TESTS)
    CXXFLAGS+=(-DLS_TESTS)
fi

mkdir -p "$OUT"
rm -f "$OUT"/*.o

compile_c()   { "$CC" "${CFLAGS[@]}" -c "$1" -o "$2"; }
compile_cpp() { "$CXX" "${CXXFLAGS[@]}" -c "$1" -o "$2"; }

compile_c "$ROOT/runtime.c" "$OUT/runtime.o"
compile_c "$ROOT/debugger.c" "$OUT/debugger.o"
compile_cpp "$ROOT/parser.cpp" "$OUT/parser.o"
compile_cpp "$ROOT/compiler.cpp" "$OUT/compiler.o"
compile_cpp "$ROOT/ir.cpp" "$OUT/ir.o"
compile_cpp "$ROOT/capi.cpp" "$OUT/capi.o"

if [[ "$MODE" == tests ]]; then
    compile_cpp "$ROOT/tests/main.cpp" "$OUT/tests_main.o"
    "$CXX" "$OUT/tests_main.o" "$OUT/parser.o" "$OUT/compiler.o" "$OUT/ir.o" \
        "$OUT/capi.o" "$OUT/runtime.o" "$OUT/debugger.o" -lm -o "$OUT/tests"
else
    compile_c "$ROOT/lumc.c" "$OUT/lumc.o"
    "$CXX" "$OUT/lumc.o" "$OUT/parser.o" "$OUT/compiler.o" "$OUT/ir.o" \
        "$OUT/capi.o" "$OUT/runtime.o" "$OUT/debugger.o" -lm -o "$OUT/lumc"
fi

if [[ "$MODE" == tests ]]; then
    echo "Built: $OUT/tests"
else
    echo "Built: $OUT/lumc"
fi
