const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const Runtime = require('../../docs/runtime.js');
const { assembleWat } = require('./assemble_wat.js');

const ROOT = path.resolve(__dirname, '../..');
const decoder = new TextDecoder();

const SOURCE = `
typedef unsigned int size_t;
typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) (void)(ap)

int printf(const char *fmt, ...);
int sprintf(char *dst, const char *fmt, ...);
int snprintf(char *dst, size_t n, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vsnprintf(char *dst, size_t n, const char *fmt, va_list ap);
int strcmp(const char *a, const char *b);
size_t strlen(const char *s);

int wrap_vprintf(const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

int wrap_vsnprintf(char *dst, size_t n, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = vsnprintf(dst, n, fmt, ap);
    va_end(ap);
    return ret;
}

int main(void)
{
    char a[128];
    char b[16];
    char c[5];
    char d[64];
    int r1;
    int r2;
    int r3;
    int r4;
    int r5;
    int r6;
    int fail = 0;

    r6 = printf("plain %s %c %% %d %i %u %x %X %o %p\\n",
                "str", 'A', -42, 17, 3000000000u, 0x2a, 0x2a,
                0755, (void *)0x1234);
    if (r6 != 49)
        fail = 1;

    r1 = sprintf(a, "[%05d][%-6s][%+d][% d][%.3s][%#x]",
                 7, "xy", 8, 9, "abcdef", 0x2a);
    if (r1 != 34 || strcmp(a, "[00007][xy    ][+8][ 9][abc][0x2a]"))
        fail = 2;
    printf("sprintf:%s:%d\\n", a, r1);

    r2 = snprintf(b, sizeof b, "n=%04d/%s", 23, "abcdef");
    if (r2 != 13 || strcmp(b, "n=0023/abcdef"))
        fail = 3;
    printf("snprintf:%s:%d\\n", b, r2);

    r3 = snprintf(c, sizeof c, "abcdef");
    if (r3 != 6 || strcmp(c, "abcd") || strlen(c) != 4)
        fail = 4;
    printf("trunc:%s:%d:%d\\n", c, r3, strlen(c));

    r4 = wrap_vprintf("v:%d %s %p\\n", 11, "ok", (void *)0xbeef);
    if (r4 != 15)
        fail = 5;

    r5 = wrap_vsnprintf(d, sizeof d, "vbuf:%04x/%-4s", 0xff, "z");
    if (r5 != 14 || strcmp(d, "vbuf:00ff/z   "))
        fail = 6;
    printf("vsnprintf:%s:%d\\n", d, r5);

    if (snprintf((char *)0, 0, "zero%d", 1) != 5)
        fail = 7;

    return fail;
}
`;

const EXPECTED_STDOUT =
  'plain str A % -42 17 3000000000 2a 2A 755 0x1234\n' +
  'sprintf:[00007][xy    ][+8][ 9][abc][0x2a]:34\n' +
  'snprintf:n=0023/abcdef:13\n' +
  'trunc:abcd:6:4\n' +
  'v:11 ok 0xbeef\n' +
  'vsnprintf:vbuf:00ff/z   :14\n';

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

function valueOf(exported) {
  return exported && typeof exported === 'object' && 'value' in exported
    ? exported.value
    : exported;
}

function readString(memory, ptr, len) {
  return decoder.decode(new Uint8Array(memory.buffer).subarray(ptr, ptr + len));
}

(async () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'tcc-wasm-printf-'));
  const cPath = path.join(tmpDir, 'printf_family.c');
  const watPath = path.join(tmpDir, 'printf_family.wat');
  const wasmPath = path.join(tmpDir, 'printf_family.wasm');

  fs.writeFileSync(cPath, SOURCE);
  run(path.join(ROOT, 'wasm32-tcc'), [
    '-nostdinc',
    '-nostdlib',
    '-Wl,--wasm-app',
    '-o',
    watPath,
    cPath
  ]);
  await assembleWat(watPath, wasmPath);

  const libcMod = await WebAssembly.compile(fs.readFileSync(path.join(ROOT, 'libc.wasm')));
  const appMod = await WebAssembly.compile(fs.readFileSync(wasmPath));
  const memory = new WebAssembly.Memory({ initial: 4096, maximum: 4096 });
  const libc = await WebAssembly.instantiate(libcMod,
    Runtime.createLibcImports(memory));
  const app = await WebAssembly.instantiate(appMod, {
    env: { memory },
    libc: libc.exports
  });

  const heapBase = valueOf(app.exports.__heap_base);
  const heapEnd = valueOf(app.exports.__heap_end);
  if (libc.exports.rt_init_heap(heapBase, heapEnd) !== 0)
    throw new Error('failed to initialize libc heap');

  const rc = app.exports.main();
  const stdout = readString(memory, libc.exports.rt_stdout_ptr(),
                            libc.exports.rt_stdout_len());
  if (rc !== 0)
    throw new Error(`main returned ${rc}\nstdout: ${JSON.stringify(stdout)}`);

  if (stdout !== EXPECTED_STDOUT)
    throw new Error(`stdout mismatch\nexpected: ${JSON.stringify(EXPECTED_STDOUT)}\nactual:   ${JSON.stringify(stdout)}`);

  console.log('wasm printf family tests ok');
})().catch(err => {
  console.error(err && err.stack || err);
  process.exit(1);
});
