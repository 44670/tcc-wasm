const fs = require('fs');
const path = require('path');
const Runtime = require('../../docs/runtime.js');

const ROOT = path.resolve(__dirname, '../..');
const wasmPath = process.argv[2] || path.join(ROOT, 'docs/tcc.wasm');
const libcPath = process.argv[3] || path.join(ROOT, 'docs/libc.wasm');

function assertTrue(name, value) {
  if (!value)
    throw new Error(name);
}

async function main() {
  const module = await WebAssembly.compile(fs.readFileSync(wasmPath));
  const imports = WebAssembly.Module.imports(module);
  assertTrue('compiler should import shared memory',
             imports.some(i => i.module === 'env' && i.name === 'memory'));
  assertTrue('compiler should import libc',
             imports.some(i => i.module === 'libc'));
  assertTrue('compiler should not require Emscripten invoke imports',
             !imports.some(i => i.module === 'env' && i.name.startsWith('invoke_')));
  assertTrue('compiler should not require Emscripten longjmp import',
             !imports.some(i => i.module === 'env' && i.name === '_emscripten_throw_longjmp'));

  const compiler = await Runtime.CompilerHost.create({
    wasm: module,
    libc: libcPath
  });
  assertTrue('fixed 256MB memory',
             compiler.memory.buffer.byteLength === 256 * 1024 * 1024);

  const fib = compiler.compileResult(`int add(int a, int b) { return a + b; }
int fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }
int answer(void) { return add(fib(6), 4); }
`);
  assertTrue(`fib compile failed: ${fib.diagnostics}`, fib.rc === 0);
  assertTrue('fib should emit a module', fib.wat.startsWith('(module'));
  assertTrue('fib output should contain fib', fib.wat.includes('(func $fib'));
  assertTrue('fib should stay structured', !fib.wat.includes('$dispatch'));

  await compiler.reset();
  const afterReset = compiler.compileResult('int answer(void) { return 42; }\n');
  assertTrue(`post-reset compile failed: ${afterReset.diagnostics}`,
             afterReset.rc === 0);
  assertTrue('post-reset output should contain answer',
             afterReset.wat.includes('(func $answer'));

  const duff = compiler.compileResult(`void duff_copy(char *to, char *from, int count)
{
    int n = (count + 7) / 8;
    switch (count % 8) {
    case 0:
        do {
            *to++ = *from++;
    case 7:
            *to++ = *from++;
    case 6:
            *to++ = *from++;
    case 5:
            *to++ = *from++;
    case 4:
            *to++ = *from++;
    case 3:
            *to++ = *from++;
    case 2:
            *to++ = *from++;
    case 1:
            *to++ = *from++;
        } while (--n > 0);
    }
}
`);
  assertTrue(`duff compile failed: ${duff.diagnostics}`, duff.rc === 0);
  assertTrue('duff output should contain duff_copy', duff.wat.includes('(func $duff_copy'));
  assertTrue('duff should use dispatcher fallback', duff.wat.includes('$dispatch'));

  let badFailed = false;
  try {
    compiler.compileResult('int broken(void) { return ;');
  } catch (err) {
    badFailed = err.rc !== 0 && err.diagnostics.length > 0;
  }
  assertTrue('bad source should fail with diagnostics', badFailed);

  console.log('wasm compiler smoke ok');
}

main().catch(err => {
  console.error(err && err.stack || err);
  process.exit(1);
});
