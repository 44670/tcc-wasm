const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const ROOT = path.resolve(__dirname, '../..');

function commandOk(command, args) {
  const result = spawnSync(command, args, { encoding: 'utf8' });
  return !result.error && result.status === 0;
}

function run(command, args, options = {}) {
  const result = spawnSync(command, args, {
    cwd: ROOT,
    encoding: 'utf8',
    ...options
  });
  if (result.status !== 0) {
    const detail = `${result.stdout || ''}${result.stderr || ''}`.trim();
    throw new Error(`${command} ${args.join(' ')} failed${detail ? `\n${detail}` : ''}`);
  }
}

function findWat2Wasm() {
  const candidates = [];
  if (process.env.WAT2WASM)
    candidates.push(process.env.WAT2WASM);
  candidates.push(path.join(ROOT, 'node_modules/.bin/wat2wasm'));
  candidates.push('wat2wasm');

  for (const candidate of candidates) {
    if (commandOk(candidate, ['--version']))
      return candidate;
  }
  return null;
}

function findWasmAs() {
  const candidates = [];
  if (process.env.WASM_AS)
    candidates.push(process.env.WASM_AS);
  candidates.push('wasm-as');
  if (process.env.HOME)
    candidates.push(path.join(process.env.HOME, 'emsdk/upstream/bin/wasm-as'));

  for (const candidate of candidates) {
    if (commandOk(candidate, ['--version']))
      return candidate;
  }
  return null;
}

function wabtFeatures(options = {}) {
  return {
    exceptions: options.exceptions !== false,
    mutable_globals: true,
    sat_float_to_int: true,
    sign_extension: true,
    simd: true,
    threads: true,
    function_references: true,
    multi_value: true,
    tail_call: true,
    bulk_memory: true,
    reference_types: true
  };
}

async function assembleWithWabtModule(watPath, wasmPath, options) {
  let wabtFactory;
  try {
    wabtFactory = require('wabt');
  } catch (err) {
    return false;
  }

  const wabt = await wabtFactory();
  const mod = wabt.parseWat(path.basename(watPath),
                            fs.readFileSync(watPath, 'utf8'),
                            wabtFeatures(options));
  try {
    mod.resolveNames();
    mod.validate();
    const binary = mod.toBinary({ write_debug_names: true });
    fs.writeFileSync(wasmPath, Buffer.from(binary.buffer));
  } finally {
    mod.destroy();
  }
  return true;
}

function assembleWithWat2Wasm(watPath, wasmPath, options) {
  const wat2wasm = findWat2Wasm();
  if (!wat2wasm)
    return false;
  const args = [];
  if (options.exceptions !== false)
    args.push('--enable-exceptions');
  args.push(watPath, '-o', wasmPath);
  run(wat2wasm, args, { cwd: path.dirname(watPath) });
  return true;
}

function assembleWithBinaryen(watPath, wasmPath, options) {
  const wasmAs = findWasmAs();
  if (!wasmAs)
    return false;
  const args = [];
  if (options.exceptions !== false)
    args.push('--enable-exception-handling');
  args.push(watPath, '-o', wasmPath);
  run(wasmAs, args, { cwd: path.dirname(watPath) });
  return true;
}

async function assembleWat(watPath, wasmPath, options = {}) {
  watPath = path.resolve(watPath);
  wasmPath = path.resolve(wasmPath);
  const mode = (options.assembler || process.env.WASM_ASSEMBLER || 'auto').toLowerCase();
  const errors = [];

  if (mode === 'wabt' || mode === 'auto') {
    try {
      if (assembleWithWat2Wasm(watPath, wasmPath, options))
        return { assembler: 'wabt-cli' };
    } catch (err) {
      errors.push(err.message);
      if (mode === 'wabt')
        throw err;
    }
    try {
      if (await assembleWithWabtModule(watPath, wasmPath, options))
        return { assembler: 'wabt-js' };
    } catch (err) {
      errors.push(err.message);
      if (mode === 'wabt')
        throw err;
    }
    if (mode === 'wabt')
      throw new Error('WABT assembler requested but unavailable; run npm install or set WAT2WASM=/path/to/wat2wasm');
  }

  if (mode === 'binaryen' || mode === 'wasm-as' || mode === 'auto') {
    try {
      if (assembleWithBinaryen(watPath, wasmPath, options))
        return { assembler: 'wasm-as' };
    } catch (err) {
      errors.push(err.message);
      throw err;
    }
  }

  throw new Error(`no wasm assembler available${errors.length ? `\n${errors.join('\n')}` : ''}`);
}

async function main() {
  const args = process.argv.slice(2);
  const options = { exceptions: true };
  while (args[0] && args[0].startsWith('--')) {
    const arg = args.shift();
    if (arg === '--exceptions')
      options.exceptions = true;
    else if (arg === '--no-exceptions')
      options.exceptions = false;
    else if (arg.startsWith('--assembler='))
      options.assembler = arg.slice('--assembler='.length);
    else
      throw new Error(`unknown option: ${arg}`);
  }
  if (args.length !== 2)
    throw new Error('usage: node tests/wasm32/assemble_wat.js [--assembler=wabt|binaryen|auto] WAT WASM');
  await assembleWat(args[0], args[1], options);
}

module.exports = {
  assembleWat,
  findWasmAs,
  findWat2Wasm
};

if (require.main === module) {
  main().catch(err => {
    console.error(err && err.stack || err);
    process.exit(1);
  });
}
