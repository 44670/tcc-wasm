#!/usr/bin/env bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT_DIR=${OUT_DIR:-$(mktemp -d /tmp/tcc-wasm32-tests.XXXXXX)}
WASM_AS=${WASM_AS:-wasm-as}

if ! command -v "$WASM_AS" >/dev/null 2>&1; then
    if [ -x "$HOME/emsdk/upstream/bin/wasm-as" ]; then
        WASM_AS="$HOME/emsdk/upstream/bin/wasm-as"
    else
        echo "wasm32 tests: wasm-as not found; set WASM_AS=/path/to/wasm-as" >&2
        exit 1
    fi
fi

cd "$ROOT"
mkdir -p "$OUT_DIR"

make -B wasm32-tcc

for src in tests/wasm32/*.c; do
    name=$(basename "$src" .c)
    ./wasm32-tcc -nostdlib -o "$OUT_DIR/$name.wat" "$src"
    "$WASM_AS" "$OUT_DIR/$name.wat" -o "$OUT_DIR/$name.wasm"
done

node tests/wasm32/assert.js "$OUT_DIR"
