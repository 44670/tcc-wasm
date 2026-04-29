const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const Runtime = require('../../web/runtime.js');

const ROOT = path.resolve(__dirname, '../..');
const libcPath = process.argv[2] || path.join(ROOT, 'libc.wasm');

function findWasmAs() {
  const candidates = [];
  if (process.env.WASM_AS)
    candidates.push(process.env.WASM_AS);
  candidates.push('wasm-as');
  if (process.env.HOME)
    candidates.push(path.join(process.env.HOME, 'emsdk/upstream/bin/wasm-as'));
  for (const candidate of candidates) {
    const result = spawnSync(candidate, ['--version'], { encoding: 'utf8' });
    if (!result.error && result.status === 0)
      return candidate;
  }
  throw new Error('wasm-as not found; set WASM_AS=/path/to/wasm-as');
}

function run(command, args, options = {}) {
  const result = spawnSync(command, args, {
    cwd: ROOT,
    encoding: 'utf8',
    ...options
  });
  if (result.status !== 0) {
    process.stderr.write(result.stdout || '');
    process.stderr.write(result.stderr || '');
    throw new Error(`${command} ${args.join(' ')} failed`);
  }
}

async function assertRejects(name, fn, pattern) {
  try {
    await fn();
  } catch (err) {
    const message = err && err.message || String(err);
    if (!pattern.test(message))
      throw new Error(`${name}: got ${JSON.stringify(message)}`);
    return;
  }
  throw new Error(`${name}: expected rejection`);
}

function assemble(tmpDir, name, wat) {
  const watPath = path.join(tmpDir, `${name}.wat`);
  const wasmPath = path.join(tmpDir, `${name}.wasm`);
  fs.writeFileSync(watPath, wat);
  run(findWasmAs(), [watPath, '-o', wasmPath], { cwd: tmpDir });
  return fs.readFileSync(wasmPath);
}

function appModule(body) {
  return `(module
  (import "env" "memory" (memory 4096 4096))
  ${body}
)`;
}

(async () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'tcc-wasm-host-errors-'));
  const runtime = await Runtime.AppRuntime.create({ libc: libcPath });

  const missingHeap = assemble(tmpDir, 'missing_heap', appModule(`
  (global $__heap_end (export "__heap_end") i32 (i32.const 268435456))
  (func $main (export "main") (result i32)
    (i32.const 0))
`));

  const missingEntry = assemble(tmpDir, 'missing_entry', appModule(`
  (global $__heap_base (export "__heap_base") i32 (i32.const 1048576))
  (global $__heap_end (export "__heap_end") i32 (i32.const 268435456))
`));

  const missingLibc = assemble(tmpDir, 'missing_libc', appModule(`
  (import "libc" "missing_func" (func $missing_func))
  (global $__heap_base (export "__heap_base") i32 (i32.const 1048576))
  (global $__heap_end (export "__heap_end") i32 (i32.const 268435456))
  (func $main (export "main") (result i32)
    (call $missing_func)
    (i32.const 0))
`));

  const missingMemory = assemble(tmpDir, 'missing_memory', `(module
  (global $__heap_base (export "__heap_base") i32 (i32.const 1048576))
  (global $__heap_end (export "__heap_end") i32 (i32.const 268435456))
  (func $main (export "main") (result i32)
    (i32.const 0))
)`);

  await assertRejects('missing heap bounds',
                      () => runtime.run(missingHeap),
                      /did not export heap bounds/);
  await assertRejects('missing app entry',
                      () => runtime.run(missingEntry),
                      /did not export entry: main/);
  await assertRejects('missing libc import',
                      () => runtime.run(missingLibc),
                      /unavailable libc symbol: missing_func/);
  await assertRejects('missing shared memory import',
                      () => runtime.run(missingMemory),
                      /did not import shared memory/);

  console.log('wasm runtime host error tests ok');
})().catch(err => {
  console.error(err && err.stack || err);
  process.exit(1);
});
