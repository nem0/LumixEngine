#!/usr/bin/env bash
set -euo pipefail

# Dump Evox bytecode for all benchmarks.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BENCH_DIR="$ROOT/benchmarks"
EVOXC="$ROOT/build/evoxc"
OUT_DIR="$BENCH_DIR/bytecode"

if [[ ! -x "$EVOXC" ]]; then
    echo "Error: evoxc not found: $EVOXC" >&2
    echo "Build the project first with ./build.sh release." >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

failed=0
shopt -s nullglob
benchmarks=("$BENCH_DIR"/*.evox)
for bench in "${benchmarks[@]}"; do
    name=$(basename "$bench" .evox)
    echo "Dumping $(basename "$bench")..."
    if "$EVOXC" --dump-bytecode "$bench" > "$OUT_DIR/$name.dump"; then
        echo "  [OK]   $OUT_DIR/$name.dump"
    else
        echo "  [FAIL] $(basename "$bench")"
        failed=1
    fi
done

exit "$failed"
