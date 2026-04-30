#!/usr/bin/env bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT_DIR=${OUT_DIR:-$(mktemp -d /tmp/tcc-wasm32-tests.XXXXXX)}

cd "$ROOT"
mkdir -p "$OUT_DIR"
export WASM_ASSEMBLER=${WASM_ASSEMBLER:-wabt}

make -B wasm32-tcc

for src in tests/wasm32/*.c; do
    name=$(basename "$src" .c)
    ./wasm32-tcc -nostdlib -o "$OUT_DIR/$name.wat" "$src"
    node tests/wasm32/assemble_wat.js --exceptions "$OUT_DIR/$name.wat" "$OUT_DIR/$name.wasm"
done

node tests/wasm32/assert.js "$OUT_DIR"
