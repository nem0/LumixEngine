#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/tmp/gmake"
META_BIN="$BUILD_DIR/bin/Debug/meta"

cd "$SCRIPT_DIR"

if [[ ! -x ./genie ]]; then
  echo "error: Linux genie is missing or not executable: $SCRIPT_DIR/genie" >&2
  echo "       run: chmod +x \"$SCRIPT_DIR/genie\"" >&2
  exit 1
fi

./genie --no-studio --no-physics --no-renderer --no-audio --no-navigation --no-animation --no-lua --no-lumscript gmake
make -C "$BUILD_DIR" -j config=debug64 meta

if [[ ! -x "$META_BIN" ]]; then
  echo "error: meta executable was not produced: $META_BIN" >&2
  exit 2
fi

cd "$SCRIPT_DIR/.."
exec "$META_BIN" "$@"
