#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/tmp/gmake"
TEST_BIN="$BUILD_DIR/bin/Debug/tests"

cd "$SCRIPT_DIR"

"$SCRIPT_DIR/run_meta.sh"

if [[ ! -x ./premake5 ]]; then
  echo "error: Linux premake5 is missing or not executable: $SCRIPT_DIR/premake5" >&2
  echo "       run: chmod +x \"$SCRIPT_DIR/premake5\"" >&2
  exit 1
fi

./premake5 --with-tests \
  --no-physics \
  --no-navigation \
  gmake

make -C "$BUILD_DIR" -j config=debug_x64 tests

if [[ ! -x "$TEST_BIN" ]]; then
  echo "error: test executable was not produced: $TEST_BIN" >&2
  exit 2
fi

cd "$SCRIPT_DIR/../data"
exec "$TEST_BIN" "$@"
