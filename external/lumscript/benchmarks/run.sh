#!/usr/bin/env bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BENCH_DIR="$ROOT/benchmarks"
LUMC="$ROOT/build/lumc"

printf '\n=== LumScript Benchmark Suite ===\n\n'
printf 'Building optimized lumc...\n'
"$ROOT/build.sh" release
printf '\n'

passed=0
total=0
shopt -s nullglob
benchmarks=("$BENCH_DIR"/*.ls)
for bench in "${benchmarks[@]}"; do
    name=$(basename "$bench")
    printf 'Running: %s [bytecode]\n' "$name"
    if "$LUMC" "$bench"; then
        printf '  [OK] %s bytecode completed successfully\n' "$name"
        ((passed += 1))
    else
        printf '  [FAIL] %s bytecode failed\n' "$name"
    fi
    ((total += 1))
    printf '\n'
done

printf '=== Summary ===\n'
printf 'Passed: %d/%d\n\n' "$passed" "$total"
(( passed == total ))
