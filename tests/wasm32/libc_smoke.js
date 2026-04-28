const fs = require('fs');

const wasmPath = process.argv[2] || 'libc.wasm';
const encoder = new TextEncoder();
const decoder = new TextDecoder();

function assertEq(name, got, exp) {
  if (got !== exp)
    throw new Error(`${name}: got ${got}, expected ${exp}`);
}

function assertTrue(name, value) {
  if (!value)
    throw new Error(name);
}

function bytes(memory) {
  return new Uint8Array(memory.buffer);
}

function putString(memory, exports, text) {
  const encoded = encoder.encode(text);
  const ptr = exports.malloc(encoded.length + 1);
  assertTrue('malloc string', ptr !== 0);
  bytes(memory).set(encoded, ptr);
  bytes(memory)[ptr + encoded.length] = 0;
  return { ptr, len: encoded.length };
}

function readString(memory, ptr, len) {
  return decoder.decode(bytes(memory).subarray(ptr, ptr + len));
}

(async () => {
  const mod = await WebAssembly.compile(fs.readFileSync(wasmPath));
  const imports = WebAssembly.Module.imports(mod);
  assertEq('import count', imports.length, 0);

  const instance = await WebAssembly.instantiate(mod, {});
  const e = instance.exports;
  const memory = e.memory;

  assertTrue('memory export', memory instanceof WebAssembly.Memory);
  assertEq('stdio cap', e.rt_stdio_capacity(), 1024 * 1024);

  const p = e.malloc(16);
  assertTrue('malloc returned pointer', p !== 0);
  for (let i = 0; i < 16; ++i)
    bytes(memory)[p + i] = 65 + i;
  const q = e.realloc(p, 32);
  assertTrue('realloc returned pointer', q !== 0);
  for (let i = 0; i < 16; ++i)
    assertEq(`realloc preserved ${i}`, bytes(memory)[q + i], 65 + i);
  e.free(q);

  const z = e.calloc(4, 4);
  assertTrue('calloc returned pointer', z !== 0);
  for (let i = 0; i < 16; ++i)
    assertEq(`calloc zero ${i}`, bytes(memory)[z + i], 0);
  e.free(z);

  const input = putString(memory, e, 'abcdef');
  assertEq('stdin set', e.rt_stdin_set(input.ptr, input.len), 6);
  assertEq('stdin remaining', e.rt_stdin_remaining(), 6);

  const readBuf = e.malloc(8);
  assertTrue('read buffer', readBuf !== 0);
  assertEq('read first', e.read(0, readBuf, 2), 2);
  assertEq('read first data', readString(memory, readBuf, 2), 'ab');
  assertEq('getchar', e.getchar(), 'c'.charCodeAt(0));
  assertEq('read rest', e.rt_read(0, readBuf, 8), 3);
  assertEq('read rest data', readString(memory, readBuf, 3), 'def');
  assertEq('read eof', e.read(0, readBuf, 1), 0);

  e.rt_stdio_reset();
  const out = putString(memory, e, 'hello');
  assertEq('write stdout', e.write(1, out.ptr, out.len), 5);
  assertEq('putchar', e.putchar('!'.charCodeAt(0)), '!'.charCodeAt(0));
  assertEq('puts', e.puts(out.ptr), 6);
  assertEq('stdout text',
           readString(memory, e.rt_stdout_ptr(), e.rt_stdout_len()),
           'hello!hello\n');

  const err = putString(memory, e, 'err');
  assertEq('write stderr', e.rt_write(2, err.ptr, err.len), 3);
  assertEq('stderr text',
           readString(memory, e.rt_stderr_ptr(), e.rt_stderr_len()),
           'err');

  e.free(input.ptr);
  e.free(readBuf);
  e.free(out.ptr);
  e.free(err.ptr);

  console.log('wasm libc smoke ok');
})().catch(err => {
  console.error(err && err.stack || err);
  process.exit(1);
});
