const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const Runtime = require('../../web/runtime.js');

const ROOT = path.resolve(__dirname, '../..');
const compilerPath = process.argv[2] || path.join(ROOT, 'web/tcc.wasm');
const libcPath = process.argv[3] || path.join(ROOT, 'web/libc.wasm');

const SOURCE = `
int getchar(void);
int printf(const char *fmt, ...);

int next_int(void)
{
    int c;
    int sign = 1;
    int value = 0;

    do {
        c = getchar();
    } while (c == ' ' || c == '\\n' || c == '\\r' || c == '\\t');

    if (c == '-') {
        sign = -1;
        c = getchar();
    }

    while (c >= '0' && c <= '9') {
        value = value * 10 + c - '0';
        c = getchar();
    }
    return value * sign;
}

int main(void)
{
    int a = next_int();
    int b = next_int();
    printf("%d\\n", a + b);
    return 0;
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
  const compiler = await Runtime.CompilerHost.create({ wasm: compilerPath });
  const appRuntime = await Runtime.AppRuntime.create({ libc: libcPath });
  const wat = compiler.compileApp(SOURCE);

  assertTrue('app mode should import shared memory',
             wat.includes('(import "env" "memory" (memory') &&
             wat.includes('4096 4096'));
  assertTrue('app mode should import printf from libc',
             wat.includes('(import "libc" "printf"'));
  assertTrue('app mode should export heap bounds',
             wat.includes('(export "__heap_base"'));

  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'tcc-wasm-ide-'));
  const watPath = path.join(tmpDir, 'app.wat');
  const wasmPath = path.join(tmpDir, 'app.wasm');
  fs.writeFileSync(watPath, wat);
  run(findWasmAs(), [watPath, '-o', wasmPath], { cwd: tmpDir });

  const result = await appRuntime.run(fs.readFileSync(wasmPath), {
    stdin: '2 40\n'
  });
  assertEq('main return', result.rc, 0);
  assertEq('stdout', result.stdout, '42\n');
  assertEq('stderr', result.stderr, '');

  console.log('wasm ide smoke ok');
})().catch(err => {
  console.error(err && err.stack || err);
  process.exit(1);
});
