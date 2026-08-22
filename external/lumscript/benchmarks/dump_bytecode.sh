#!/usr/bin/env bash
set -euo pipefail

# Dump LumScript bytecode for all benchmarks.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BENCH_DIR="$ROOT/benchmarks"
LUMC="$ROOT/build/lumc"
OUT_DIR="$BENCH_DIR/bytecode"

if [[ ! -x "$LUMC" ]]; then
    echo "Error: lumc not found: $LUMC" >&2
    echo "Build the project first with ./build.sh release." >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

failed=0
shopt -s nullglob
benchmarks=("$BENCH_DIR"/*.ls)
for bench in "${benchmarks[@]}"; do
    name=$(basename "$bench" .ls)
    echo "Dumping $(basename "$bench")..."
    if "$LUMC" --dump-bytecode "$bench" > "$OUT_DIR/$name.dump"; then
        echo "  [OK]   $OUT_DIR/$name.dump"
    else
        echo "  [FAIL] $(basename "$bench")"
        failed=1
    fi
done

exit "$failed"
