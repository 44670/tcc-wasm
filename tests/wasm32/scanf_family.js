const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const Runtime = require('../../web/runtime.js');

const ROOT = path.resolve(__dirname, '../..');

const SOURCE = `
typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) (void)(ap)

int printf(const char *fmt, ...);
int scanf(const char *fmt, ...);
int sscanf(const char *src, const char *fmt, ...);
int vsscanf(const char *src, const char *fmt, va_list ap);
int strcmp(const char *a, const char *b);

int wrap_vsscanf(const char *src, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = vsscanf(src, fmt, ap);
    va_end(ap);
    return ret;
}

int main(void)
{
    int a = 0;
    int b = 0;
    unsigned int oct = 0;
    unsigned int u = 0;
    unsigned int hx = 0;
    unsigned char uc = 0;
    short sh = 0;
    char word[32];
    char cc[4];
    void *ptr = 0;
    int n = -1;
    int after = 0;
    int r;
    int fail = 0;

    r = sscanf(" -42 0x2a 0755 word xy 0x1234",
               "%d %i %o %5s %2c %p%n",
               &a, &b, &oct, word, cc, &ptr, &n);
    cc[2] = 0;
    if (r != 6 || a != -42 || b != 42 || oct != 493
        || strcmp(word, "word") || strcmp(cc, "xy")
        || ptr != (void *)0x1234 || n != 29)
        fail = 1;
    printf("sscanf:%d:%d:%d:%u:%s:%s:%p:%d\\n",
           r, a, b, oct, word, cc, ptr, n);

    r = sscanf("255 -12", "%hhu %hd", &uc, &sh);
    if (r != 2 || uc != 255 || sh != -12)
        fail = 2;
    printf("small:%d:%d:%d\\n", r, uc, sh);

    r = sscanf("123 abc 456", "%*d %3s %d", word, &after);
    if (r != 2 || strcmp(word, "abc") || after != 456)
        fail = 3;
    printf("skip:%d:%s:%d\\n", r, word, after);

    r = wrap_vsscanf("77 zz", "%d %2c", &a, cc);
    cc[2] = 0;
    if (r != 2 || a != 77 || strcmp(cc, "zz"))
        fail = 4;
    printf("vsscanf:%d:%d:%s\\n", r, a, cc);

    n = -1;
    r = scanf("%u %x %15s%n", &u, &hx, word, &n);
    if (r != 3 || u != 17 || hx != 0xbeef || strcmp(word, "stdin-tail")
        || n != 18)
        fail = 5;
    printf("scanf:%d:%u:%u:%s:%d\\n", r, u, hx, word, n);

    return fail;
}
`;

const EXPECTED_STDOUT =
  'sscanf:6:-42:42:493:word:xy:0x1234:29\n' +
  'small:2:255:-12\n' +
  'skip:2:abc:456\n' +
  'vsscanf:2:77:zz\n' +
  'scanf:3:17:48879:stdin-tail:18\n';

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

(async () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'tcc-wasm-scanf-'));
  const cPath = path.join(tmpDir, 'scanf_family.c');
  const watPath = path.join(tmpDir, 'scanf_family.wat');
  const wasmPath = path.join(tmpDir, 'scanf_family.wasm');

  fs.writeFileSync(cPath, SOURCE);
  run(path.join(ROOT, 'wasm32-tcc'), [
    '-nostdinc',
    '-nostdlib',
    '-Wl,--wasm-app',
    '-o',
    watPath,
    cPath
  ]);
  run(findWasmAs(), [watPath, '-o', wasmPath], { cwd: tmpDir });

  const runtime = await Runtime.AppRuntime.create({
    libc: path.join(ROOT, 'libc.wasm')
  });
  const result = await runtime.run(fs.readFileSync(wasmPath), {
    stdin: '17 beef stdin-tail\n'
  });

  if (result.rc !== 0)
    throw new Error(`main returned ${result.rc}\nstdout: ${JSON.stringify(result.stdout)}`);
  if (result.stdout !== EXPECTED_STDOUT)
    throw new Error(`stdout mismatch\nexpected: ${JSON.stringify(EXPECTED_STDOUT)}\nactual:   ${JSON.stringify(result.stdout)}`);

  console.log('wasm scanf family tests ok');
})().catch(err => {
  console.error(err && err.stack || err);
  process.exit(1);
});
