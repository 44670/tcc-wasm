const fs = require('fs');
const path = require('path');

const outDir = process.argv[2];
if (!outDir) {
  console.error('usage: node tests/wasm32/assert.js OUT_DIR');
  process.exit(2);
}

function read(name, ext) {
  return fs.readFileSync(path.join(outDir, `${name}.${ext}`));
}

async function load(name) {
  const { instance } = await WebAssembly.instantiate(read(name, 'wasm'), {});
  return instance.exports;
}

function assertEq(name, got, exp) {
  if (got !== exp)
    throw new Error(`${name}: got ${got}, expected ${exp}`);
}

function assertTrue(name, value) {
  if (!value)
    throw new Error(name);
}

function funcBody(wat, name) {
  const start = wat.indexOf(`  (func $${name}`);
  if (start < 0)
    throw new Error(`missing function ${name}`);
  let depth = 0;
  for (let i = start; i < wat.length; ++i) {
    const ch = wat[i];
    if (ch === '(') {
      ++depth;
    } else if (ch === ')') {
      --depth;
      if (depth === 0)
        return wat.slice(start, i + 1);
    }
  }
  throw new Error(`unterminated function ${name}`);
}

function assertWatShape() {
  const fibWat = read('fib', 'wat').toString();
  const fib = funcBody(fibWat, 'fib');
  const add = funcBody(fibWat, 'add');
  const answer = funcBody(fibWat, 'answer');
  const duff = funcBody(read('duff', 'wat').toString(), 'duff_copy');

  assertTrue('fib should use structured if', fib.includes('\n    (if '));
  assertTrue('fib should not use dispatcher', !fib.includes('$dispatch'));
  assertTrue('fib should not declare $pc', !fib.includes('(local $pc'));
  assertTrue('fib should use a value-producing if',
             fib.includes('(if (result i32)'));
  assertTrue('fib condition should stay as an operand expression',
             fib.includes('(i32.ge_s (i32.load'));
  assertTrue('fib should return the wasm stack value',
             fib.includes('\n    (return)\n'));
  assertTrue('straight add should not use dispatcher', !add.includes('$dispatch'));
  assertTrue('straight add should not need a temporary result local',
             !add.includes('(local $r0'));
  assertTrue('answer should not use dispatcher', !answer.includes('$dispatch'));
  assertTrue('Duff fallback should still use dispatcher', duff.includes('$dispatch'));
}

async function assertRuntime() {
  let e;

  e = await load('fib');
  assertEq('fib.answer', e.answer(), 12);
  assertEq('fib.fib6', e.fib(6), 8);
  assertEq('fib.add', e.add(4, 5), 9);

  e = await load('calls');
  assertEq('calls.void_call_roundtrip', e.void_call_roundtrip(), 37);
  assertEq('calls.nested_call_arg', e.nested_call_arg(11), 17);

  e = await load('duff_control');
  assertEq('duff_control.1', e.duff_count(1), 1);
  assertEq('duff_control.8', e.duff_count(8), 36);
  assertEq('duff_control.11', e.duff_count(11), 42);

  e = await load('global_data');
  assertEq('global_data.get_g', e.get_g(), 7);
  e.set_g(19);
  assertEq('global_data.set_g', e.get_g(), 19);
  assertEq('global_data.arr', e.get_arr(2), 33);
  assertEq('global_data.msg1', e.get_msg1(), 98);
  assertEq('global_data.msgp2', e.get_msgp2(), 99);
  assertEq('global_data.lit1', e.get_lit1(), 121);

  e = await load('local_stack');
  assertEq('local_stack.local_array_sum', e.local_array_sum(), 15);
  assertEq('local_stack.local_array_partial_zero', e.local_array_partial_zero(), 4);
  assertEq('local_stack.local_struct_field', e.local_struct_field(), 18);
  assertEq('local_stack.ptr_to_local', e.ptr_to_local(), 42);
  assertEq('local_stack.struct_ptr_field', e.struct_ptr_field(), 12);
  assertEq('local_stack.struct_assign_copy', e.struct_assign_copy(), 68);

  e = await load('longlong');
  assertEq('longlong.ll_add_low', e.ll_add_low(), -2147483644);
  assertEq('longlong.ll_sub_borrow_low', e.ll_sub_borrow_low(), -1);
  assertEq('longlong.ll_mul_low', e.ll_mul_low(), 262147);
  assertEq('longlong.ll_cmp', e.ll_cmp(), 1);

  e = await load('function_ptr');
  assertEq('function_ptr.local', e.call_local_fp(4), 6);
  assertEq('function_ptr.global', e.call_global_fp(4), 5);
  assertEq('function_ptr.selected0', e.call_selected_fp(4, 0), 5);
  assertEq('function_ptr.selected1', e.call_selected_fp(4, 1), 6);
  assertEq('function_ptr.void', e.call_void_fp(44), 44);

  e = await load('scalars');
  assertEq('scalars.schar', e.cast_signed_char(255), -1);
  assertEq('scalars.uchar', e.cast_unsigned_char(255), 255);
  assertEq('scalars.sshort', e.cast_signed_short(65535), -1);
  assertEq('scalars.bitfield', e.bitfield_local(), 488);

  e = await load('pointer_ops');
  const mem = new Uint8Array(e.memory.buffer);
  mem[1024] = 65;
  mem[1025] = 66;
  mem[1030] = 0;
  assertEq('pointer_ops.get0', e.get0(1024), 65);
  assertEq('pointer_ops.get1', e.get1(1024), 66);
  assertEq('pointer_ops.get_post', e.get_post(1024), 65);
  e.set_post(1030, 90);
  assertEq('pointer_ops.set_post', mem[1030], 90);
  e.copy_post(1031, 1025);
  assertEq('pointer_ops.copy_post', mem[1031], 66);

  e = await load('duff');
  const dm = new Uint8Array(e.memory.buffer);
  const src = 1200;
  const dst = 1300;
  for (let i = 0; i < 13; ++i)
    dm[src + i] = 97 + i;
  e.duff_copy(dst, src, 13);
  for (let i = 0; i < 13; ++i)
    assertEq(`duff.${i}`, dm[dst + i], 97 + i);
}

(async () => {
  assertWatShape();
  await assertRuntime();
  console.log('wasm32 tests ok');
})().catch(err => {
  console.error(err && err.stack || err);
  process.exit(1);
});
