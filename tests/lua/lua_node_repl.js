const fs = require('fs');
const path = require('path');
const Runtime = require('../../web/runtime.js');

function assertEq(name, got, expected) {
  if (got !== expected)
    throw new Error(`${name}: got ${JSON.stringify(got)}, expected ${JSON.stringify(expected)}`);
}

(async () => {
  const luaWasm = process.argv[2] || path.join(__dirname, 'lua.wasm');
  const libcWasm = process.argv[3] || path.resolve(__dirname, '../../libc.wasm');
  const rt = await Runtime.AppRuntime.create({ libc: libcWasm });
  const wasm = fs.readFileSync(luaWasm);

  let result = await rt.run(wasm, {
    args: ['lua', '-v'],
    stdin: ''
  });
  assertEq('lua -v rc', result.rc, 0);
  assertEq('lua -v stdout', result.stdout, '');
  assertEq('lua -v stderr',
           result.stderr,
           'Lua 5.1.5  Copyright (C) 1994-2012 Lua.org, PUC-Rio\n');

  result = await rt.run(wasm, {
    args: ['lua'],
    stdin: [
      'print(1+2)',
      'print(math.max(4,9))',
      'io.write("ok", string.char(10))',
      'print(1+)',
      'print(pcall(function() error("x") end))',
      'print(6*7)',
      ''
    ].join('\n')
  });
  assertEq('lua repl rc', result.rc, 0);
  assertEq('lua repl stdout',
           result.stdout,
           '> 3\n> 9\n> ok\n> > false\tstdin:1: x\n> 42\n> \n');
  assertEq('lua repl stderr',
           result.stderr,
           'Lua 5.1.5  Copyright (C) 1994-2012 Lua.org, PUC-Rio\n' +
           "stdin:1: unexpected symbol near ')'\n");

  console.log('lua node repl smoke ok');
})().catch(err => {
  console.error(err && err.stack || err);
  process.exit(1);
});
