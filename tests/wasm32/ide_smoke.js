const fs = require('fs');
const os = require('os');
const path = require('path');
const Runtime = require('../../docs/runtime.js');
const IdeResources = require('../../docs/ide-resources.js');
const { assembleWat } = require('./assemble_wat.js');

const ROOT = path.resolve(__dirname, '../..');
const compilerPath = process.argv[2] || path.join(ROOT, 'docs/tcc.wasm');
const libcPath = process.argv[3] || path.join(ROOT, 'docs/libc.wasm');

const SOURCE = `
int scanf(const char *fmt, ...);
int printf(const char *fmt, ...);

int main(void)
{
    int a = 0;
    int b = 0;
    if (scanf("%d %d", &a, &b) != 2)
        return 1;
    printf("%d\\n", a + b);
    return 0;
}
`;

const IMPLICIT_STDIO_SOURCE = `
#include <stdio.h>

int main(void)
{
    int a = 0;
    int b = 0;
    if (scanf("%d %d", &a, &b) != 2)
        return 1;
    printf("%d\\n", a + b);
    return 0;
}
`;

const WARNING_SOURCE = `
int main(void)
{
    int value = 0;
    return scanf("%d", &value);
}
`;

const MEM_SOURCE = `
void *memset(void *dst, int c, unsigned int n);
void *memcpy(void *dst, const void *src, unsigned int n);
void *memmove(void *dst, const void *src, unsigned int n);
int printf(const char *fmt, ...);

int main(void)
{
    char src[8];
    char dst[8];
    int i;

    for (i = 0; i < 8; ++i)
        src[i] = '0' + i;
    memset(dst, '?', sizeof dst);
    memcpy(dst, src, sizeof dst);
    memmove(dst + 1, dst, 7);
    printf("%c%c%c%c\\n", dst[0], dst[1], dst[2], dst[7]);
    return !(dst[0] == '0' && dst[1] == '0'
             && dst[2] == '1' && dst[7] == '6');
}
`;

const IMPLICIT_MEM_SOURCE = `
int printf(const char *fmt, ...);

struct pair {
    int a;
    int b;
    int c;
    int d;
};

int main(void)
{
    struct pair x;
    struct pair y;
    char src[4];
    char dst[32] = {0};

    x.a = 1;
    x.b = 2;
    x.c = 3;
    x.d = 4;
    y = x;
    src[0] = 'a';
    src[1] = 'b';
    src[2] = 'c';
    src[3] = 'd';
    memcpy(dst, src, 4);
    printf("%d%c%c%d\\n", y.d, dst[0], dst[3], dst[31]);
    return !(y.d == 4 && dst[0] == 'a' && dst[3] == 'd' && dst[31] == 0);
}
`;

function assertTrue(name, value) {
  if (!value)
    throw new Error(name);
}

function assertEq(name, got, expected) {
  if (got !== expected)
    throw new Error(`${name}: got ${JSON.stringify(got)}, expected ${JSON.stringify(expected)}`);
}

(async () => {
  const compiler = await Runtime.CompilerHost.create({
    wasm: compilerPath,
    resources: IdeResources
  });
  const appRuntime = await Runtime.AppRuntime.create({ libc: libcPath });
  const wat = compiler.compileApp(SOURCE);

  assertTrue('app mode should import shared memory',
             wat.includes('(import "env" "memory" (memory') &&
             wat.includes('4096 4096'));
  assertTrue('app mode should import printf from libc',
             wat.includes('(import "libc" "printf"'));
  assertTrue('app mode should import scanf from libc',
             wat.includes('(import "libc" "scanf"'));
  assertTrue('app mode should export heap bounds',
             wat.includes('(export "__heap_base"'));

  const warningResult = compiler.compileAppResult(WARNING_SOURCE);
  assertTrue('compile result should keep app WAT',
             warningResult.wat.includes('(module'));
  assertTrue('compile result should keep warnings',
             /warning: implicit declaration of function 'scanf'/.test(
               warningResult.diagnostics));

  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'tcc-wasm-ide-'));
  const watPath = path.join(tmpDir, 'app.wat');
  const wasmPath = path.join(tmpDir, 'app.wasm');
  fs.writeFileSync(watPath, wat);
  await assembleWat(watPath, wasmPath);

  const result = await appRuntime.run(fs.readFileSync(wasmPath), {
    stdin: '2 40\n'
  });
  assertEq('main return', result.rc, 0);
  assertEq('stdout', result.stdout, '42\n');
  assertEq('stderr', result.stderr, '');

  const implicitStdioWat = compiler.compileApp(IMPLICIT_STDIO_SOURCE);
  assertTrue('implicit scanf should use libc varargs ABI',
             implicitStdioWat.includes(
               '(import "libc" "scanf" (func $scanf (param i32) (param i32) (result i32))'));
  assertTrue('implicit printf should use libc varargs ABI',
             implicitStdioWat.includes(
               '(import "libc" "printf" (func $printf (param i32) (param i32) (result i32))'));

  const implicitStdioWatPath = path.join(tmpDir, 'implicit_stdio_app.wat');
  const implicitStdioWasmPath = path.join(tmpDir, 'implicit_stdio_app.wasm');
  fs.writeFileSync(implicitStdioWatPath, implicitStdioWat);
  await assembleWat(implicitStdioWatPath, implicitStdioWasmPath);

  const implicitStdioResult = await appRuntime.run(fs.readFileSync(implicitStdioWasmPath), {
    stdin: '19 23\n'
  });
  assertEq('implicit stdio main return', implicitStdioResult.rc, 0);
  assertEq('implicit stdio stdout', implicitStdioResult.stdout, '42\n');
  assertEq('implicit stdio stderr', implicitStdioResult.stderr, '');

  const memWat = compiler.compileApp(MEM_SOURCE);
  assertTrue('app mode should import memset from libc',
             memWat.includes('(import "libc" "memset"'));
  assertTrue('app mode should import memcpy from libc',
             memWat.includes('(import "libc" "memcpy"'));
  assertTrue('app mode should import memmove from libc',
             memWat.includes('(import "libc" "memmove"'));
  assertTrue('app mode should not define fallback memset',
             !memWat.includes('(func $memset (param $dst i32)'));
  assertTrue('app mode should not define fallback memcpy',
             !memWat.includes('(func $memcpy (param $dst i32)'));
  assertTrue('app mode should not define fallback memmove',
             !memWat.includes('(func $memmove (param $dst i32)'));

  const memWatPath = path.join(tmpDir, 'mem_app.wat');
  const memWasmPath = path.join(tmpDir, 'mem_app.wasm');
  fs.writeFileSync(memWatPath, memWat);
  await assembleWat(memWatPath, memWasmPath);

  const memResult = await appRuntime.run(fs.readFileSync(memWasmPath));
  assertEq('mem main return', memResult.rc, 0);
  assertEq('mem stdout', memResult.stdout, '0016\n');
  assertEq('mem stderr', memResult.stderr, '');

  const implicitMemWat = compiler.compileApp(IMPLICIT_MEM_SOURCE);
  for (const name of ['memset', 'memcpy', 'memmove']) {
    assertTrue(`implicit app mode should import ${name} with libc ABI`,
               implicitMemWat.includes(
                 `(import "libc" "${name}" (func $${name} (param i32) (param i32) (param i32) (result i32))`));
  }

  const implicitMemWatPath = path.join(tmpDir, 'implicit_mem_app.wat');
  const implicitMemWasmPath = path.join(tmpDir, 'implicit_mem_app.wasm');
  fs.writeFileSync(implicitMemWatPath, implicitMemWat);
  await assembleWat(implicitMemWatPath, implicitMemWasmPath);

  const implicitMemResult = await appRuntime.run(fs.readFileSync(implicitMemWasmPath));
  assertEq('implicit mem main return', implicitMemResult.rc, 0);
  assertEq('implicit mem stdout', implicitMemResult.stdout, '4ad0\n');
  assertEq('implicit mem stderr', implicitMemResult.stderr, '');

  console.log('wasm ide smoke ok');
})().catch(err => {
  console.error(err && err.stack || err);
  process.exit(1);
});
