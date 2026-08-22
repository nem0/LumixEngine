#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/tmp/gmake"
TEST_BIN="$BUILD_DIR/bin/Debug/tests"

cd "$SCRIPT_DIR"

"$SCRIPT_DIR/run_meta.sh"

if [[ ! -x ./genie ]]; then
  echo "error: Linux genie is missing or not executable: $SCRIPT_DIR/genie" >&2
  echo "       run: chmod +x \"$SCRIPT_DIR/genie\"" >&2
  exit 1
fi

# The headless tests do not need the platform renderer or runtime plugins.
./genie --with-tests \
  --no-physics \
  --no-navigation \
  --no-animation \
  --no-audio \
  --no-lua \
  --no-lumscript \
  gmake

make -C "$BUILD_DIR" -j config=debug64 tests

if [[ ! -x "$TEST_BIN" ]]; then
  echo "error: test executable was not produced: $TEST_BIN" >&2
  exit 2
fi

cd "$SCRIPT_DIR/.."
exec "$TEST_BIN" "$@"
