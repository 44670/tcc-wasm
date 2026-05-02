const fs = require('fs');
const Runtime = require('../../docs/runtime.js');

const wasmPath = process.argv[2] || 'libc.wasm';
const encoder = new TextEncoder();
const decoder = new TextDecoder();

const RT_EBADF = -9;
const RT_EINVAL = -22;
const RT_ENOSPC = -28;
const RT_STDIO_CAP = 1024 * 1024;

const EXPECTED_EXPORTS = [
  '__data_end',
  '__heap_base',
  'memory',
  'memset',
  'memcpy',
  'memmove',
  'memcmp',
  'strlen',
  'strnlen',
  'strcpy',
  'strdup',
  'strcmp',
  'strncmp',
  'strtok',
  'strtok_r',
  'malloc',
  'free',
  'calloc',
  'realloc',
  'rt_init_heap',
  'rt_heap_initialized',
  'rt_stdio_capacity',
  'rt_stdio_reset',
  'rt_stdin_set',
  'rt_stdin_append',
  'rt_stdin_remaining',
  'rt_stdout_ptr',
  'rt_stdout_len',
  'rt_stdout_clear',
  'rt_stderr_ptr',
  'rt_stderr_len',
  'rt_stderr_clear',
  'rt_read',
  'rt_write',
  'read',
  'write',
  'rt_getchar',
  'getchar',
  'rt_putchar',
  'putchar',
  'rt_puts',
  'puts',
  'vsnprintf',
  'snprintf',
  'vsprintf',
  'sprintf',
  'vprintf',
  'printf',
  'vsscanf',
  'sscanf',
  'vscanf',
  'scanf',
  'llabs',
  'atol',
  'atoll',
  'strtoll',
  'strtoull'
];

function assertEq(name, got, exp) {
  if (got !== exp)
    throw new Error(`${name}: got ${JSON.stringify(got)}, expected ${JSON.stringify(exp)}`);
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

function mem(memory) {
  return new Uint8Array(memory.buffer);
}

function view(memory) {
  return new DataView(memory.buffer);
}

function readString(memory, ptr, len) {
  return decoder.decode(mem(memory).subarray(ptr, ptr + len));
}

function readCString(memory, ptr) {
  const bytes = mem(memory);
  let end = ptr;
  while (bytes[end] !== 0)
    ++end;
  return readString(memory, ptr, end - ptr);
}

function writeString(memory, ptr, text, nul = true) {
  const encoded = encoder.encode(text);
  mem(memory).set(encoded, ptr);
  if (nul)
    mem(memory)[ptr + encoded.length] = 0;
  return encoded.length;
}

function alloc(exports, size, label) {
  const ptr = exports.malloc(size || 1);
  assertTrue(`${label || 'malloc'} returned pointer`, ptr !== 0);
  assertEq(`${label || 'malloc'} alignment`, ptr & 7, 0);
  return ptr;
}

function putString(memory, exports, text, label = 'string') {
  const ptr = alloc(exports, encoder.encode(text).length + 1, label);
  const len = writeString(memory, ptr, text);
  return { ptr, len };
}

function putBytes(memory, exports, values, label = 'bytes') {
  const ptr = alloc(exports, values.length || 1, label);
  mem(memory).set(values, ptr);
  return { ptr, len: values.length };
}

function putU32(memory, ptr, value) {
  view(memory).setUint32(ptr, value >>> 0, true);
}

function getU32(memory, ptr) {
  return view(memory).getUint32(ptr, true);
}

function getI32(memory, ptr) {
  return view(memory).getInt32(ptr, true);
}

function putU16(memory, ptr, value) {
  view(memory).setUint16(ptr, value & 0xffff, true);
}

function getI16(memory, ptr) {
  return view(memory).getInt16(ptr, true);
}

function packVa(memory, exports, values, label = 'va_area') {
  const ptr = alloc(exports, values.length * 4 || 1, label);
  for (let i = 0; i < values.length; ++i)
    putU32(memory, ptr + i * 4, values[i]);
  return ptr;
}

async function instantiate(mod) {
  const memory = new WebAssembly.Memory({ initial: 4096, maximum: 4096 });
  const instance = await WebAssembly.instantiate(mod,
    Runtime.createLibcImports(memory));
  return { memory, exports: instance.exports };
}

function initHeap(memory, exports, start = 0x100000, end = 0x1000000) {
  assertEq('heap init', exports.rt_init_heap(start, end), 0);
  assertEq('heap initialized', exports.rt_heap_initialized(), 1);
  assertTrue('stdin buffer from heap', exports.rt_stdin_remaining() === 0);
  assertTrue('stdout from heap', exports.rt_stdout_ptr() >= start);
  assertTrue('stdout below heap end', exports.rt_stdout_ptr() < end);
  assertTrue('stderr from heap', exports.rt_stderr_ptr() >= start);
  assertTrue('stderr below heap end', exports.rt_stderr_ptr() < end);
}

async function testImportsAndExports(mod) {
  const imports = WebAssembly.Module.imports(mod);
  assertTrue('memory import',
             imports.some(i => i.module === 'env' && i.name === 'memory' && i.kind === 'memory'));
  for (const name of ['rt_host_read', 'rt_host_write', 'rt_host_exit', 'rt_host_isatty'])
    assertTrue(`host import ${name}`,
               imports.some(i => i.module === 'env' && i.name === name && i.kind === 'function'));

  const exports = WebAssembly.Module.exports(mod).map(e => e.name);
  for (const name of EXPECTED_EXPORTS)
    assertTrue(`export ${name}`, exports.includes(name));

  const { exports: e } = await instantiate(mod);
  assertTrue('__data_end below libc reserve', valueOf(e.__data_end) <= 0x100000);
  assertTrue('__heap_base exported', valueOf(e.__heap_base) > valueOf(e.__data_end));
  assertEq('stdio cap', e.rt_stdio_capacity(), RT_STDIO_CAP);
  assertEq('heap initially not ready', e.rt_heap_initialized(), 0);
  assertEq('malloc before init', e.malloc(16), 0);
  assertEq('read before init', e.read(0, 0, 0), RT_EINVAL);
  assertEq('write before init', e.write(1, 0, 0), RT_EINVAL);
}

async function testHeapBounds(mod) {
  let ctx = await instantiate(mod);
  assertEq('tiny heap rejected', ctx.exports.rt_init_heap(0x100000, 0x100100), RT_EINVAL);
  assertEq('allocation after failed init', ctx.exports.malloc(8), 0);

  ctx = await instantiate(mod);
  initHeap(ctx.memory, ctx.exports, 0x100003, 0x1000005);
  assertEq('heap init twice rejected', ctx.exports.rt_init_heap(0x100000, 0x1000000), RT_EINVAL);
}

async function testAllocator(mod) {
  const { memory, exports: e } = await instantiate(mod);
  initHeap(memory, e);

  const zero = alloc(e, 0, 'malloc zero');
  e.free(zero);
  e.free(0);
  e.free(1234);

  const a = alloc(e, 24, 'first reusable block');
  const b = alloc(e, 24, 'middle reusable block');
  const d = alloc(e, 24, 'last reusable block');
  e.free(b);
  const reused = alloc(e, 8, 'reuse freed block');
  assertEq('malloc reuses first fit block', reused, b);
  e.free(a);
  e.free(reused);
  e.free(d);

  const p = alloc(e, 16, 'malloc 16');
  for (let i = 0; i < 16; ++i)
    mem(memory)[p + i] = 65 + i;
  const q = e.realloc(p, 32);
  assertTrue('realloc grow returned pointer', q !== 0);
  assertEq('realloc grow alignment', q & 7, 0);
  for (let i = 0; i < 16; ++i)
    assertEq(`realloc preserved ${i}`, mem(memory)[q + i], 65 + i);

  const r = e.realloc(q, 8);
  assertTrue('realloc shrink returned pointer', r !== 0);
  for (let i = 0; i < 8; ++i)
    assertEq(`realloc shrink preserved ${i}`, mem(memory)[r + i], 65 + i);
  e.free(r);

  const c = e.calloc(4, 4);
  assertTrue('calloc returned pointer', c !== 0);
  for (let i = 0; i < 16; ++i)
    assertEq(`calloc zero ${i}`, mem(memory)[c + i], 0);
  e.free(c);

  assertEq('calloc overflow rejected', e.calloc(0x80000000, 4), 0);
  assertEq('huge malloc rejected', e.malloc(0x7fffffff), 0);
  const fromNull = e.realloc(0, 24);
  assertTrue('realloc null acts like malloc', fromNull !== 0);
  const shrinkToZero = e.realloc(fromNull, 0);
  assertEq('realloc size zero frees', shrinkToZero, 0);
}

async function testMemoryPrimitives(mod) {
  const { memory, exports: e } = await instantiate(mod);
  initHeap(memory, e);

  const a = alloc(e, 48, 'mem a');
  const b = alloc(e, 48, 'mem b');
  assertEq('memset return', e.memset(a, 0x5a, 16), a);
  for (let i = 0; i < 16; ++i)
    assertEq(`memset byte ${i}`, mem(memory)[a + i], 0x5a);

  for (let i = 0; i < 16; ++i)
    mem(memory)[b + i] = i;
  assertEq('memcpy return', e.memcpy(a, b, 16), a);
  assertEq('memcmp equal', e.memcmp(a, b, 16), 0);
  mem(memory)[a + 7] = 99;
  assertTrue('memcmp greater', e.memcmp(a, b, 16) > 0);
  mem(memory)[a + 7] = 1;
  assertTrue('memcmp less', e.memcmp(a, b, 16) < 0);

  for (let i = 0; i < 16; ++i)
    mem(memory)[a + i] = i;
  assertEq('memmove overlap forward return', e.memmove(a + 4, a, 12), a + 4);
  assertEq('memmove overlap forward data', Array.from(mem(memory).subarray(a + 4, a + 16)).join(','), '0,1,2,3,4,5,6,7,8,9,10,11');
  assertEq('memmove overlap backward return', e.memmove(a, a + 4, 12), a);
  assertEq('memmove overlap backward data', Array.from(mem(memory).subarray(a, a + 12)).join(','), '0,1,2,3,4,5,6,7,8,9,10,11');
  assertEq('memmove zero self', e.memmove(a, a, 0), a);
  assertEq('memcpy zero return', e.memcpy(a, b, 0), a);
  assertEq('memset zero return', e.memset(a, 0, 0), a);
  assertEq('memcmp zero equal', e.memcmp(a, b, 0), 0);
}

async function testStringPrimitives(mod) {
  const { memory, exports: e } = await instantiate(mod);
  initHeap(memory, e);

  const src = putString(memory, e, 'abcdef', 'src string');
  const dst = alloc(e, 16, 'dst string');
  assertEq('strlen', e.strlen(src.ptr), 6);
  assertEq('strcpy return', e.strcpy(dst, src.ptr), dst);
  assertEq('strcpy data', readCString(memory, dst), 'abcdef');
  assertEq('strcmp equal', e.strcmp(dst, src.ptr), 0);

  const abc = putString(memory, e, 'abc', 'abc string');
  const abd = putString(memory, e, 'abd', 'abd string');
  const empty = putString(memory, e, '', 'empty string');
  assertTrue('strcmp less', e.strcmp(abc.ptr, abd.ptr) < 0);
  assertTrue('strcmp greater', e.strcmp(abd.ptr, abc.ptr) > 0);
  assertEq('strlen empty', e.strlen(empty.ptr), 0);
  assertEq('strcpy empty return', e.strcpy(dst, empty.ptr), dst);
  assertEq('strcpy empty data', readCString(memory, dst), '');
  assertEq('strncmp prefix equal', e.strncmp(abc.ptr, abd.ptr, 2), 0);
  assertTrue('strncmp detects diff', e.strncmp(abc.ptr, abd.ptr, 3) < 0);
  assertEq('strncmp zero length', e.strncmp(abc.ptr, abd.ptr, 0), 0);
}

async function testStdio(mod) {
  const { memory, exports: e } = await instantiate(mod);
  initHeap(memory, e);

  const input = putString(memory, e, 'abcdef', 'stdin input');
  assertEq('stdin set', e.rt_stdin_set(input.ptr, input.len), 6);
  assertEq('stdin remaining after set', e.rt_stdin_remaining(), 6);

  const readBuf = alloc(e, 16, 'read buffer');
  assertEq('read first', e.read(0, readBuf, 2), 2);
  assertEq('read first data', readString(memory, readBuf, 2), 'ab');
  assertEq('getchar', e.getchar(), 'c'.charCodeAt(0));
  assertEq('rt_getchar', e.rt_getchar(), 'd'.charCodeAt(0));

  const append = putString(memory, e, 'gh', 'stdin append');
  assertEq('stdin append compacts', e.rt_stdin_append(append.ptr, append.len), 2);
  assertEq('stdin remaining after append', e.rt_stdin_remaining(), 4);
  assertEq('read rest', e.rt_read(0, readBuf, 16), 4);
  assertEq('read rest data', readString(memory, readBuf, 4), 'efgh');
  assertEq('read eof', e.read(0, readBuf, 1), 0);
  assertEq('bad read fd', e.read(9, readBuf, 1), RT_EBADF);

  const tooLarge = e.malloc(RT_STDIO_CAP + 1);
  assertTrue('large stdin fixture allocation', tooLarge !== 0);
  assertEq('stdin set too large', e.rt_stdin_set(tooLarge, RT_STDIO_CAP + 1), RT_ENOSPC);
  e.free(tooLarge);

  e.rt_stdio_reset();
  const out = putString(memory, e, 'hello', 'stdout input');
  assertEq('write stdout', e.write(1, out.ptr, out.len), 5);
  assertEq('rt_putchar', e.rt_putchar('!'.charCodeAt(0)), '!'.charCodeAt(0));
  assertEq('putchar', e.putchar('?'.charCodeAt(0)), '?'.charCodeAt(0));
  assertEq('puts', e.puts(out.ptr), 6);
  assertEq('rt_puts', e.rt_puts(out.ptr), 6);
  assertEq('stdout text',
           readString(memory, e.rt_stdout_ptr(), e.rt_stdout_len()),
           'hello!?hello\nhello\n');
  e.rt_stdout_clear();
  assertEq('stdout clear', e.rt_stdout_len(), 0);

  const err = putString(memory, e, 'err', 'stderr input');
  assertEq('write stderr', e.rt_write(2, err.ptr, err.len), 3);
  assertEq('stderr text',
           readString(memory, e.rt_stderr_ptr(), e.rt_stderr_len()),
           'err');
  e.rt_stderr_clear();
  assertEq('stderr clear', e.rt_stderr_len(), 0);
  assertEq('bad write fd', e.write(9, out.ptr, out.len), RT_EBADF);
}

async function testPrintfFamily(mod) {
  const { memory, exports: e } = await instantiate(mod);
  initHeap(memory, e);

  const str = putString(memory, e, 'xy', 'printf string');
  const fmt = putString(memory, e, 'A:%s:%c:%d:%u:%#x:%p:%%', 'printf fmt');
  const va = packVa(memory, e, [
    str.ptr,
    'Z'.charCodeAt(0),
    -7,
    3000000000,
    0x2a,
    0x1234
  ], 'printf va');
  const printfExpected = 'A:xy:Z:-7:3000000000:0x2a:0x1234:%';
  assertEq('printf return', e.printf(fmt.ptr, va), printfExpected.length);
  assertEq('printf stdout',
           readString(memory, e.rt_stdout_ptr(), e.rt_stdout_len()),
           printfExpected);
  e.rt_stdout_clear();

  const dst = alloc(e, 128, 'sprintf dst');
  const fmt2 = putString(memory, e, '[%05d][%-4s][%.2s]', 'sprintf fmt');
  const va2 = packVa(memory, e, [9, str.ptr, str.ptr], 'sprintf va');
  const sprintfExpected = '[00009][xy  ][xy]';
  assertEq('sprintf return', e.sprintf(dst, fmt2.ptr, va2), sprintfExpected.length);
  assertEq('sprintf data', readCString(memory, dst), sprintfExpected);

  const small = alloc(e, 8, 'snprintf dst');
  const fmt3 = putString(memory, e, 'abcdef%d', 'snprintf fmt');
  const va3 = packVa(memory, e, [12], 'snprintf va');
  assertEq('snprintf return', e.snprintf(small, 5, fmt3.ptr, va3), 8);
  assertEq('snprintf truncates', readCString(memory, small), 'abcd');
  assertEq('snprintf null zero', e.snprintf(0, 0, fmt3.ptr, va3), 8);

  const vdst = alloc(e, 64, 'vsnprintf dst');
  const fmt4 = putString(memory, e, 'v:%04x/%s', 'vsnprintf fmt');
  const va4 = packVa(memory, e, [0xbeef, str.ptr], 'vsnprintf va');
  assertEq('vsnprintf return', e.vsnprintf(vdst, 64, fmt4.ptr, va4), 9);
  assertEq('vsnprintf data', readCString(memory, vdst), 'v:beef/xy');
  assertEq('vsprintf return', e.vsprintf(vdst, fmt4.ptr, va4), 9);
  assertEq('vsprintf data', readCString(memory, vdst), 'v:beef/xy');
  assertEq('vprintf return', e.vprintf(fmt4.ptr, va4), 9);
  assertEq('vprintf stdout',
           readString(memory, e.rt_stdout_ptr(), e.rt_stdout_len()),
           'v:beef/xy');
}

async function testScanfFamily(mod) {
  const { memory, exports: e } = await instantiate(mod);
  initHeap(memory, e);

  const src = putString(memory, e, ' -42 0x2a 0755 word xy 0x1234', 'scan src');
  const fmt = putString(memory, e, '%d %i %o %5s %2c %p%n', 'scan fmt');
  const outI = alloc(e, 4, 'scan int');
  const outAuto = alloc(e, 4, 'scan auto');
  const outOct = alloc(e, 4, 'scan oct');
  const outWord = alloc(e, 16, 'scan word');
  const outChars = alloc(e, 4, 'scan chars');
  const outPtr = alloc(e, 4, 'scan ptr');
  const outN = alloc(e, 4, 'scan n');
  putU32(memory, outN, 0xffffffff);
  const va = packVa(memory, e, [
    outI,
    outAuto,
    outOct,
    outWord,
    outChars,
    outPtr,
    outN
  ], 'sscanf va');

  assertEq('sscanf return', e.sscanf(src.ptr, fmt.ptr, va), 6);
  mem(memory)[outChars + 2] = 0;
  assertEq('sscanf d', getI32(memory, outI), -42);
  assertEq('sscanf i', getI32(memory, outAuto), 42);
  assertEq('sscanf o', getU32(memory, outOct), 493);
  assertEq('sscanf s', readCString(memory, outWord), 'word');
  assertEq('sscanf c', readCString(memory, outChars), 'xy');
  assertEq('sscanf p', getU32(memory, outPtr), 0x1234);
  assertEq('sscanf n', getI32(memory, outN), 29);

  const smallSrc = putString(memory, e, '255 -12', 'small scan src');
  const smallFmt = putString(memory, e, '%hhu %hd', 'small scan fmt');
  const outU8 = alloc(e, 1, 'scan u8');
  const outI16 = alloc(e, 2, 'scan i16');
  putU16(memory, outI16, 0);
  const smallVa = packVa(memory, e, [outU8, outI16], 'small scan va');
  assertEq('vsscanf return', e.vsscanf(smallSrc.ptr, smallFmt.ptr, smallVa), 2);
  assertEq('vsscanf hhu', mem(memory)[outU8], 255);
  assertEq('vsscanf hd', getI16(memory, outI16), -12);

  const skipSrc = putString(memory, e, '123 abc 456', 'skip scan src');
  const skipFmt = putString(memory, e, '%*d %3s %d', 'skip scan fmt');
  const outAfter = alloc(e, 4, 'skip int');
  const skipVa = packVa(memory, e, [outWord, outAfter], 'skip scan va');
  assertEq('sscanf suppression return', e.sscanf(skipSrc.ptr, skipFmt.ptr, skipVa), 2);
  assertEq('sscanf suppression word', readCString(memory, outWord), 'abc');
  assertEq('sscanf suppression int', getI32(memory, outAfter), 456);

  const stdin = putString(memory, e, '17 beef stdin-tail\n', 'scanf stdin');
  assertEq('scanf stdin set', e.rt_stdin_set(stdin.ptr, stdin.len), stdin.len);
  const scanfFmt = putString(memory, e, '%u %x %15s%n', 'scanf fmt');
  const outU = alloc(e, 4, 'scanf u');
  const outHex = alloc(e, 4, 'scanf hex');
  const scanfVa = packVa(memory, e, [outU, outHex, outWord, outN], 'scanf va');
  assertEq('scanf return', e.scanf(scanfFmt.ptr, scanfVa), 3);
  assertEq('scanf u', getU32(memory, outU), 17);
  assertEq('scanf x', getU32(memory, outHex), 0xbeef);
  assertEq('scanf s', readCString(memory, outWord), 'stdin-tail');
  assertEq('scanf n', getI32(memory, outN), 18);
  assertEq('vscanf eof/mismatch', e.vscanf(scanfFmt.ptr, scanfVa), -1);
}

(async () => {
  const mod = await WebAssembly.compile(fs.readFileSync(wasmPath));

  await testImportsAndExports(mod);
  await testHeapBounds(mod);
  await testAllocator(mod);
  await testMemoryPrimitives(mod);
  await testStringPrimitives(mod);
  await testStdio(mod);
  await testPrintfFamily(mod);
  await testScanfFamily(mod);

  console.log('wasm libc detailed tests ok');
})().catch(err => {
  console.error(err && err.stack || err);
  process.exit(1);
});
