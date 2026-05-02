const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const Runtime = require('../../docs/runtime.js');
const { assembleWat } = require('./assemble_wat.js');

const ROOT = path.resolve(__dirname, '../..');
const encoder = new TextEncoder();
const decoder = new TextDecoder();

const APP_SOURCE = `
int getchar(void);
int putchar(int ch);
int puts(const char *s);

int is_space(int c)
{
    return c == ' ' || c == '\\n' || c == '\\r' || c == '\\t';
}

int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

int next_int(void)
{
    int c;
    int value = 0;
    do {
        c = getchar();
    } while (is_space(c));
    while (is_digit(c)) {
        value = value * 10 + c - '0';
        c = getchar();
    }
    return value;
}

void print_int(int value)
{
    char buf[16];
    int n = 0;
    if (value == 0) {
        putchar('0');
        return;
    }
    while (value > 0) {
        buf[n++] = (char)('0' + value % 10);
        value = value / 10;
    }
    while (n > 0)
        putchar(buf[--n]);
}

int loop_limit_count(int *limit)
{
    int i;
    int twotoi;
    for (i = 0, twotoi = 1; twotoi / 2 < *limit; i++, twotoi *= 2)
        ;
    return i;
}

int large_unsigned_guard(unsigned start, unsigned hdr, unsigned end)
{
    if (end <= start + hdr + 3u * 1048576u)
        return -1;
    return 7;
}

int main(void)
{
    int a = next_int();
    int b = next_int();
    int limit = 1;
    print_int(a * b + a + b);
    putchar('\\n');
    puts("shared");
    print_int(loop_limit_count(&limit));
    putchar(' ');
    print_int(large_unsigned_guard(1124448u, 16u, 268435456u));
    putchar('\\n');
    return 0;
}
`;

const IMPORT_VARARGS_SOURCE = `
int imported_sum(int n, ...);

int call_imported_sum(void)
{
    return imported_sum(3, 4, 5, 6);
}
`;

const SJLJ_SOURCE = `
#include <setjmp.h>
#include <stdio.h>

static jmp_buf jb;

void jump0(void)
{
    longjmp(jb, 0);
}

void jump7(void)
{
    jump0();
}

int main(void)
{
    int v = setjmp(jb);
    if (v == 0) {
        puts("before");
        jump7();
        puts("bad");
    }
    printf("after %d\\n", v);
    return 0;
}
`;

function assertEq(name, got, exp) {
  if (got !== exp)
    throw new Error(`${name}: got ${got}, expected ${exp}`);
}

function assertTrue(name, value) {
  if (!value)
    throw new Error(name);
}

function valueOf(exported) {
  return exported && typeof exported === 'object' && 'value' in exported
    ? exported.value
    : exported;
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

function readString(memory, ptr, len) {
  return decoder.decode(new Uint8Array(memory.buffer).subarray(ptr, ptr + len));
}

function assertImportedVarargs(tmpDir) {
  const src = path.join(tmpDir, 'import_varargs.c');
  const wat = path.join(tmpDir, 'import_varargs.wat');

  fs.writeFileSync(src, IMPORT_VARARGS_SOURCE);
  run(path.join(ROOT, 'wasm32-tcc'), [
    '-nostdinc',
    '-nostdlib',
    '-Wl,--wasm-app',
    '-o',
    wat,
    src
  ]);

  const text = fs.readFileSync(wat, 'utf8');
  assertTrue('imported varargs should use named arg plus hidden va_area',
             text.includes('(import "libc" "imported_sum" (func $imported_sum (param i32) (param i32) (result i32)))'));
  assertTrue('imported varargs should pack caller stack array',
             text.includes('(i32.store (global.get $__stack_pointer) (i32.const 4))'));
  assertTrue('imported varargs should pass hidden stack-array pointer',
             text.includes('(call $imported_sum (i32.const 3) (global.get $__stack_pointer))'));
}

async function assertSetjmpLongjmp(tmpDir) {
  const src = path.join(tmpDir, 'sjlj.c');
  const wat = path.join(tmpDir, 'sjlj.wat');
  const wasm = path.join(tmpDir, 'sjlj.wasm');

  fs.writeFileSync(src, SJLJ_SOURCE);
  run(path.join(ROOT, 'wasm32-tcc'), [
    '-nostdinc',
    '-nostdlib',
    '-Wl,--wasm-app',
    '-Iinclude',
    '-o',
    wat,
    src
  ]);

  const text = fs.readFileSync(wat, 'utf8');
  assertTrue('setjmp app imports longjmp tag',
             text.includes('(import "libc" "__wasm_longjmp_tag" (tag $__wasm_longjmp_tag (param i32)))'));
  assertTrue('setjmp app catches longjmp tag',
             text.includes('(catch $__wasm_longjmp_tag'));
  await assembleWat(wat, wasm);

  const rt = await Runtime.AppRuntime.create({ libc: path.join(ROOT, 'libc.wasm') });
  const result = await rt.run(fs.readFileSync(wasm), { args: ['sjlj'] });
  assertEq('setjmp rc', result.rc, 0);
  assertEq('setjmp stdout', result.stdout, 'before\nafter 1\n');
  assertEq('setjmp stderr', result.stderr, '');
}

(async () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'tcc-wasm-runtime-'));
  const appC = path.join(tmpDir, 'app.c');
  const appWat = path.join(tmpDir, 'app.wat');
  const appWasm = path.join(tmpDir, 'app.wasm');

  assertImportedVarargs(tmpDir);
  await assertSetjmpLongjmp(tmpDir);

  fs.writeFileSync(appC, APP_SOURCE);
  run(path.join(ROOT, 'wasm32-tcc'), [
    '-nostdinc',
    '-nostdlib',
    '-Wl,--wasm-app',
    '-o',
    appWat,
    appC
  ]);
  await assembleWat(appWat, appWasm);

  const libcMod = await WebAssembly.compile(fs.readFileSync(path.join(ROOT, 'libc.wasm')));
  const appMod = await WebAssembly.compile(fs.readFileSync(appWasm));
  const memory = new WebAssembly.Memory({ initial: 4096, maximum: 4096 });

  const libcImports = WebAssembly.Module.imports(libcMod);
  assertTrue('libc imports shared memory',
             libcImports.some(i => i.module === 'env' && i.name === 'memory'));

  const appImports = WebAssembly.Module.imports(appMod);
  assertTrue('app imports shared memory',
             appImports.some(i => i.module === 'env' && i.name === 'memory'));
  assertTrue('app imports getchar',
             appImports.some(i => i.module === 'libc' && i.name === 'getchar'));
  assertTrue('app imports putchar',
             appImports.some(i => i.module === 'libc' && i.name === 'putchar'));
  assertTrue('app imports puts',
             appImports.some(i => i.module === 'libc' && i.name === 'puts'));

  const libc = await WebAssembly.instantiate(libcMod,
    Runtime.createLibcImports(memory));
  assertTrue('libc static data below 1MB',
             valueOf(libc.exports.__data_end) <= 0x100000);
  assertEq('malloc before init', libc.exports.malloc(4), 0);

  const app = await WebAssembly.instantiate(appMod, {
    env: { memory },
    libc: libc.exports
  });
  assertTrue('app data starts above 1MB',
             valueOf(app.exports.__data_end) >= 0x100000);

  const heapBase = valueOf(app.exports.__heap_base);
  const heapEnd = valueOf(app.exports.__heap_end);
  assertTrue('app heap base above app data', heapBase >= valueOf(app.exports.__data_end));
  assertTrue('app heap end above heap base', heapEnd > heapBase);
  assertEq('heap init', libc.exports.rt_init_heap(heapBase, heapEnd), 0);

  const input = encoder.encode('6 7\n');
  const inputPtr = libc.exports.malloc(input.length);
  assertTrue('input allocation', inputPtr !== 0);
  new Uint8Array(memory.buffer).set(input, inputPtr);
  assertEq('stdin set', libc.exports.rt_stdin_set(inputPtr, input.length), input.length);
  libc.exports.free(inputPtr);

  assertEq('main return', app.exports.main(), 0);
  assertEq('stdout',
           readString(memory, libc.exports.rt_stdout_ptr(), libc.exports.rt_stdout_len()),
           '55\nshared\n1 7\n');

  console.log('wasm shared runtime smoke ok');
})().catch(err => {
  console.error(err && err.stack || err);
  process.exit(1);
});
