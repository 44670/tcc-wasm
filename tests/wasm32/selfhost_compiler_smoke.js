const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const Runtime = require('../../docs/runtime.js');
const { assembleWat } = require('./assemble_wat.js');

const ROOT = path.resolve(__dirname, '../..');
const encoder = new TextEncoder();

function assertEq(name, got, exp) {
  if (got !== exp)
    throw new Error(`${name}: got ${got}, expected ${exp}`);
}

function assertTrue(name, value) {
  if (!value)
    throw new Error(name);
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

function writeCString(memory, malloc, text) {
  const raw = encoder.encode(text);
  const ptr = malloc(raw.length + 1);
  if (!ptr)
    throw new Error('malloc failed');
  const mem = new Uint8Array(memory.buffer);
  mem.set(raw, ptr);
  mem[ptr + raw.length] = 0;
  return ptr;
}

function readString(memory, ptr, len) {
  return Runtime.readString(memory, ptr, len);
}

async function buildSelfhostCompiler(tmpDir) {
  const watPath = path.join(tmpDir, 'tcc-selfhost.wat');
  const wasmPath = path.join(tmpDir, 'tcc-selfhost.wasm');

  run(path.join(ROOT, 'wasm32-tcc'), [
    '-nostdinc',
    '-Iinclude',
    '-I.',
    '-DONE_SOURCE=1',
    '-DTCC_TARGET_WASM32',
    '-DCONFIG_TCC_STATIC',
    '-DCONFIG_TCC_SEMLOCK=0',
    '-Dinline=',
    '-DCONFIG_TCC_CROSSPREFIX="wasm32-"',
    '-DCONFIG_TCCDIR="/"',
    '-DCONFIG_TCC_SYSINCLUDEPATHS="/include"',
    '-DCONFIG_TCC_LIBPATHS="/"',
    '-DTCC_GITHASH="browser-tcc"',
    '-Wl,--wasm-app',
    '-o',
    watPath,
    path.join(ROOT, 'docs/tcc_browser_bare.c')
  ]);
  await assembleWat(watPath, wasmPath, { exceptions: true });
  return wasmPath;
}

async function instantiateSelfhostCompiler(wasmPath) {
  const memory = new WebAssembly.Memory({ initial: 4096, maximum: 4096 });
  const libcModule = await WebAssembly.compile(fs.readFileSync(path.join(ROOT, 'libc.wasm')));
  const appModule = await WebAssembly.compile(fs.readFileSync(wasmPath));
  const libc = await WebAssembly.instantiate(libcModule,
    Runtime.createLibcImports(memory));
  const libcImports = Runtime.createAppLibcImports(memory, libc.exports);

  for (const imp of WebAssembly.Module.imports(appModule)) {
    if (imp.module === 'libc')
      assertTrue(`missing selfhost libc import: ${imp.name}`, imp.name in libcImports);
  }

  const app = await WebAssembly.instantiate(appModule, {
    env: { memory },
    libc: libcImports
  });

  const heapBase = Runtime.valueOf(app.exports.__heap_base);
  const heapEnd = Runtime.valueOf(app.exports.__heap_end);
  assertTrue('selfhost heap base exported', heapBase !== undefined);
  assertTrue('selfhost heap end exported', heapEnd !== undefined);
  assertEq('selfhost heap init', libc.exports.rt_init_heap(heapBase, heapEnd), 0);
  if (app.exports.__wasm_call_ctors)
    app.exports.__wasm_call_ctors();
  return { memory, libc, app };
}

function compileWithSelfhost(host, source) {
  const ptr = writeCString(host.memory, host.libc.exports.malloc, source);
  try {
    const rc = host.app.exports.tcc_bare_compile(ptr);
    return {
      rc,
      wat: readString(host.memory,
                      host.app.exports.tcc_bare_output(),
                      host.app.exports.tcc_bare_output_len()),
      diagnostics: readString(host.memory,
                              host.app.exports.tcc_bare_error(),
                              host.app.exports.tcc_bare_error_len())
    };
  } finally {
    host.libc.exports.free(ptr);
  }
}

async function assertSelfhostProgram(host, tmpDir, name, source, args, expected) {
  const result = compileWithSelfhost(host, source);
  assertEq(`${name} selfhost compile failed: ${result.diagnostics}`, result.rc, 0);
  assertEq(`${name} selfhost diagnostics`, result.diagnostics, '');
  assertTrue(`${name} selfhost should emit a module`, result.wat.startsWith('(module'));
  assertTrue(`${name} selfhost output should contain answer`, result.wat.includes('(func $answer'));

  const watPath = path.join(tmpDir, `${name}.wat`);
  const wasmPath = path.join(tmpDir, `${name}.wasm`);
  fs.writeFileSync(watPath, result.wat);
  await assembleWat(watPath, wasmPath, { exceptions: true });
  const answer = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {});
  assertEq(`${name} selfhost answer result`,
           answer.instance.exports.answer(...args), expected);
}

(async () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'tcc-wasm-selfhost-'));
  const selfhostWasm = await buildSelfhostCompiler(tmpDir);
  const host = await instantiateSelfhostCompiler(selfhostWasm);
  await assertSelfhostProgram(host, tmpDir, 'const-return',
                              'int answer(void){return 42;}\n', [], 42);
  await assertSelfhostProgram(host, tmpDir, 'param-store',
                              'int answer(int x){return x+1;}\n', [41], 42);
  await assertSelfhostProgram(host, tmpDir, 'direct-call',
                              'int add(int a,int b){return a+b;} int answer(void){return add(19,23);}\n',
                              [], 42);
  await assertSelfhostProgram(host, tmpDir, 'switch-sort',
                              'int answer(int x){switch(x){case 7:return 70;case 6:return 60;case 5:return 50;case 4:return 40;case 3:return 42;case 2:return 20;case 1:return 10;case 0:return 0;default:return -1;}}\n',
                              [3], 42);
  await assertSelfhostProgram(host, tmpDir, 'global-store',
                              'int x; int answer(void){x=42;return x;}\n',
                              [], 42);

  console.log('wasm selfhost compiler smoke ok');
})().catch(err => {
  console.error(err && err.stack || err);
  process.exit(1);
});
