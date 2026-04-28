const fs = require('fs');

const wasmPath = process.argv[2] || 'web/tcc-browser-bare.wasm';
const encoder = new TextEncoder();
const decoder = new TextDecoder();

let instance;
let memory;

function assertTrue(name, value) {
  if (!value)
    throw new Error(name);
}

function exp(name) {
  return instance.exports[name] || instance.exports[`_${name}`];
}

function memoryBytes() {
  return new Uint8Array(memory.buffer);
}

function readString(ptr, len) {
  if (!ptr || len <= 0)
    return '';
  return decoder.decode(memoryBytes().subarray(ptr, ptr + len));
}

function writeCString(text) {
  const bytes = encoder.encode(text);
  const malloc = exp('malloc');
  const ptr = malloc(bytes.length + 1);
  const mem = memoryBytes();
  mem.set(bytes, ptr);
  mem[ptr + bytes.length] = 0;
  return ptr;
}

function storeU32(ptr, value) {
  if (ptr)
    new DataView(memory.buffer).setUint32(ptr, value, true);
}

function storeU64(ptr, value) {
  if (ptr)
    new DataView(memory.buffer).setBigUint64(ptr, BigInt(value), true);
}

function invoke(fn, ...args) {
  const table = exp('__indirect_function_table');
  const target = table && table.get(fn);
  if (!target)
    throw new Error(`bad indirect function ${fn}`);
  return target(...args);
}

function makeImports() {
  const enosys = -52;
  return {
    env: {
      emscripten_notify_memory_growth() {},
      __syscall_getcwd() { return enosys; },
      __syscall_readlinkat() { return enosys; },
      _emscripten_throw_longjmp() { throw new Error('longjmp'); },
      invoke_ii: (fn, a) => invoke(fn, a),
      invoke_iii: (fn, a, b) => invoke(fn, a, b),
      invoke_iiii: (fn, a, b, c) => invoke(fn, a, b, c),
      invoke_iiiiii: (fn, a, b, c, d, e) => invoke(fn, a, b, c, d, e),
      invoke_iiiiiiii: (fn, a, b, c, d, e, f, g) => invoke(fn, a, b, c, d, e, f, g),
      invoke_v: fn => { invoke(fn); },
      invoke_vi: (fn, a) => { invoke(fn, a); },
      invoke_vii: (fn, a, b) => { invoke(fn, a, b); },
      invoke_viii: (fn, a, b, c) => { invoke(fn, a, b, c); },
      invoke_viiii: (fn, a, b, c, d) => { invoke(fn, a, b, c, d); },
      invoke_viiiii: (fn, a, b, c, d, e) => { invoke(fn, a, b, c, d, e); }
    },
    wasi_snapshot_preview1: {
      fd_write(fd, iovs, iovsLen, nwritten) {
        let written = 0;
        const view = new DataView(memory.buffer);
        for (let i = 0; i < iovsLen; ++i) {
          written += view.getUint32(iovs + i * 8 + 4, true);
        }
        storeU32(nwritten, written);
        return 0;
      },
      fd_close() { return 0; },
      fd_seek(fd, offset, whence, newOffset) {
        storeU64(newOffset, 0);
        return 0;
      },
      fd_read(fd, iovs, iovsLen, nread) {
        storeU32(nread, 0);
        return 0;
      },
      path_open() { return 44; },
      proc_exit(code) { throw new Error(`proc_exit(${code})`); },
      environ_sizes_get(count, size) {
        storeU32(count, 0);
        storeU32(size, 0);
        return 0;
      },
      environ_get() { return 0; },
      clock_time_get(clockId, precision, timePtr) {
        storeU64(timePtr, BigInt(Date.now()) * 1000000n);
        return 0;
      },
      random_get(ptr, len) {
        require('crypto').randomFillSync(memoryBytes().subarray(ptr, ptr + len));
        return 0;
      }
    }
  };
}

function compile(source) {
  const free = exp('free');
  const sourcePtr = writeCString(source);
  try {
    const rc = exp('tcc_bare_compile')(sourcePtr);
    return {
      rc,
      out: readString(exp('tcc_bare_output')(), exp('tcc_bare_output_len')()),
      err: readString(exp('tcc_bare_error')(), exp('tcc_bare_error_len')())
    };
  } finally {
    free(sourcePtr);
  }
}

async function main() {
  const bytes = fs.readFileSync(wasmPath);
  const mod = await WebAssembly.compile(bytes);
  const imports = WebAssembly.Module.imports(mod);
  const disallowed = imports.filter(i =>
    i.module !== 'env' && i.module !== 'wasi_snapshot_preview1');
  assertTrue(`unexpected imports: ${JSON.stringify(disallowed)}`, disallowed.length === 0);
  assertTrue('JS longjmp invoke imports should not be required',
             !imports.some(i => i.module === 'env' && i.name.startsWith('invoke_')));
  assertTrue('JS longjmp throw import should not be required',
             !imports.some(i => i.module === 'env' && i.name === '_emscripten_throw_longjmp'));

  instance = await WebAssembly.instantiate(mod, makeImports());
  memory = instance.exports.memory;
  assertTrue('fixed 256MB memory', memory.buffer.byteLength === 256 * 1024 * 1024);
  if (instance.exports._initialize)
    instance.exports._initialize();
  else if (instance.exports.__wasm_call_ctors)
    instance.exports.__wasm_call_ctors();

  const fib = compile(`int add(int a, int b) { return a + b; }
int fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }
int answer(void) { return add(fib(6), 4); }
`);
  assertTrue(`fib compile failed: ${fib.err}`, fib.rc === 0);
  assertTrue('fib should emit a module', fib.out.startsWith('(module'));
  assertTrue('fib output should contain fib', fib.out.includes('(func $fib'));
  assertTrue('fib should stay structured', !fib.out.includes('$dispatch'));

  const duff = compile(`void duff_copy(char *to, char *from, int count)
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
  assertTrue(`duff compile failed: ${duff.err}`, duff.rc === 0);
  assertTrue('duff output should contain duff_copy', duff.out.includes('(func $duff_copy'));
  assertTrue('duff should use dispatcher fallback', duff.out.includes('$dispatch'));

  const bad = compile('int broken(void) { return ;');
  assertTrue('bad source should fail', bad.rc !== 0);
  assertTrue('bad source should report diagnostics', bad.err.length > 0);

  console.log('wasm bare browser smoke ok');
}

main().catch(err => {
  console.error(err && err.stack || err);
  process.exit(1);
});
