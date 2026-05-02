const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');
const Runtime = require('../../docs/runtime.js');

const ROOT = path.resolve(__dirname, '../..');
const TEST_DIR = path.join(__dirname, 'src/lua-5.1.5/test');

const CASES = [
  { name: 'hello.lua' },
  { name: 'factorial.lua' },
  { name: 'fib.lua', args: ['8'] },
  { name: 'fibfor.lua' },
  { name: 'sieve.lua', prefix: 'N=40\n' },
  { name: 'sort.lua' },
  { name: 'cf.lua' },
  { name: 'bisect.lua' },
  { name: 'printf.lua' },
  { name: 'env.lua' },
  { name: 'echo.lua', args: ['alpha', 'beta'] },
  { name: 'globals.lua', stdin: '[1] GETGLOBAL 0 -1 ; print\n[2] SETGLOBAL 0 -2 ; x\n' },
  { name: 'table.lua', stdin: 'a 1\na 2\nb 3\n' },
  { name: 'xd.lua', stdin: 'ABC\n' },
  { name: 'trace-globals.lua' },
  { name: 'readonly.lua', expectRc: 1, expectStderr: 'cannot redefine global variable' },
  {
    name: 'life.lua',
    replacements: [['if gen>2000 then break end', 'if gen>20 then break end']],
    timeout: 30000
  },
  {
    name: 'luac.lua',
    args: ['hello.lua'],
    files: {
      'hello.lua': fs.readFileSync(path.join(TEST_DIR, 'hello.lua'), 'utf8')
    }
  },
];

function assertIncludes(name, got, needle) {
  if (!got.includes(needle))
    throw new Error(`${name}: expected ${JSON.stringify(got)} to include ${JSON.stringify(needle)}`);
}

function buildArgPrelude(scriptName, args) {
  const entries = [`[0]=${JSON.stringify(scriptName)}`];
  for (let i = 0; i < args.length; ++i)
    entries.push(`[${i + 1}]=${JSON.stringify(args[i])}`);
  return `arg={${entries.join(',')}}\n`;
}

async function runCase(luaWasm, libcWasm, testCase) {
  const rt = await Runtime.AppRuntime.create({ libc: libcWasm });
  const wasm = fs.readFileSync(luaWasm);
  const scriptPath = path.join(TEST_DIR, testCase.name);
  let script = fs.readFileSync(scriptPath, 'utf8');
  const args = testCase.args || [];
  for (const replacement of testCase.replacements || [])
    script = script.split(replacement[0]).join(replacement[1]);
  const source = buildArgPrelude(testCase.name, args) + (testCase.prefix || '') + script;
  const result = await rt.run(wasm, {
    args: ['lua', '-e', source],
    stdin: testCase.stdin || '',
    files: testCase.files
  });
  const expectRc = testCase.expectRc || 0;
  if (result.rc !== expectRc)
    throw new Error(`${testCase.name}: rc ${result.rc}, expected ${expectRc}\nstdout:\n${result.stdout}\nstderr:\n${result.stderr}`);
  if (testCase.expectStdout)
    assertIncludes(`${testCase.name} stdout`, result.stdout, testCase.expectStdout);
  if (testCase.expectStderr)
    assertIncludes(`${testCase.name} stderr`, result.stderr, testCase.expectStderr);
  return result;
}

async function childMain() {
  const luaWasm = process.argv[3];
  const libcWasm = process.argv[4];
  const name = process.argv[5];
  const testCase = CASES.find(c => c.name === name);
  if (!testCase)
    throw new Error(`unknown Lua test: ${name}`);
  const result = await runCase(luaWasm, libcWasm, testCase);
  process.stdout.write(JSON.stringify({
    name,
    rc: result.rc,
    stdoutLen: result.stdout.length,
    stderrLen: result.stderr.length
  }) + '\n');
}

function parentMain() {
  const luaWasm = process.argv[2] || path.join(__dirname, 'lua.wasm');
  const libcWasm = process.argv[3] || path.join(ROOT, 'libc.wasm');
  const failures = [];

  for (const testCase of CASES) {
    const child = spawnSync(process.execPath, [
      __filename,
      '--child',
      luaWasm,
      libcWasm,
      testCase.name
    ], {
      cwd: ROOT,
      encoding: 'utf8',
      timeout: testCase.timeout || 10000
    });
    if (child.error || child.status !== 0) {
      failures.push({
        name: testCase.name,
        status: child.status,
        error: child.error && child.error.message,
        stdout: child.stdout,
        stderr: child.stderr
      });
      process.stderr.write(`lua internal ${testCase.name} failed\n`);
      if (child.stdout)
        process.stderr.write(child.stdout);
      if (child.stderr)
        process.stderr.write(child.stderr);
    } else {
      const summary = JSON.parse(child.stdout);
      console.log(`lua internal ${summary.name} ok (${summary.stdoutLen} stdout bytes)`);
    }
  }

  if (failures.length) {
    const names = failures.map(f => f.name).join(', ');
    throw new Error(`Lua internal tests failed: ${names}`);
  }
  console.log(`lua internal tests ok (${CASES.length} cases)`);
}

if (process.argv[2] === '--child') {
  childMain().catch(err => {
    console.error(err && err.stack || err);
    process.exit(1);
  });
} else {
  try {
    parentMain();
  } catch (err) {
    console.error(err && err.stack || err);
    process.exit(1);
  }
}
