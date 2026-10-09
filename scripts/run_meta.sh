#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/tmp/gmake"
META_BIN="$BUILD_DIR/bin/Debug/meta"

cd "$SCRIPT_DIR"

if [[ ! -x ./premake5 ]]; then
  echo "error: Linux premake5 is missing or not executable: $SCRIPT_DIR/premake5" >&2
  echo "       run: chmod +x \"$SCRIPT_DIR/premake5\"" >&2
  exit 1
fi

./premake5 --no-studio --no-physics --no-renderer --no-audio --no-navigation --no-animation --no-evox gmake
make -C "$BUILD_DIR" -j config=debug64 meta

if [[ ! -x "$META_BIN" ]]; then
  echo "error: meta executable was not produced: $META_BIN" >&2
  exit 2
fi

cd "$SCRIPT_DIR/.."
exec "$META_BIN" "$@"
