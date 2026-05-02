const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const Runtime = require('../../docs/runtime.js');
const { assembleWat } = require('./assemble_wat.js');

const ROOT = path.resolve(__dirname, '../..');

const SOURCE = `
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_int(const void *ap, const void *bp)
{
    int a = *(const int *)ap;
    int b = *(const int *)bp;
    return (a > b) - (a < b);
}

int main(void)
{
    int values[] = {9, -3, 4, 4, 0, 17, -8};
    int expected[] = {-8, -3, 0, 4, 4, 9, 17};
    int n = sizeof(values) / sizeof(values[0]);
    int key = 4;
    int miss = 5;
    int *hit;
    int i;
    int fail = 0;
    char *end;
    long long signed_value;
    unsigned long long unsigned_value;
    char *copy;
    char *tok1;
    char *tok2;
    char *tok3;
    char *tok4;

    qsort(values, n, sizeof(values[0]), cmp_int);
    for (i = 0; i < n; ++i)
        if (values[i] != expected[i])
            fail = 1;

    hit = bsearch(&key, values, n, sizeof(values[0]), cmp_int);
    if (!hit || *hit != key)
        fail = 2;
    if (bsearch(&miss, values, n, sizeof(values[0]), cmp_int))
        fail = 3;

    signed_value = strtoll(" -1234567890123xyz", &end, 10);
    if (signed_value != -1234567890123LL || *end != 'x')
        fail = 4;

    unsigned_value = strtoull("0xffffffffffffffff!", &end, 0);
    if (unsigned_value != ~0ULL || *end != '!')
        fail = 5;

    if (abs(-7) != 7 || labs(-123456L) != 123456L
        || llabs(-9000000000LL) != 9000000000LL)
        fail = 6;
    if (atoi("42x") != 42 || atol("-321x") != -321L
        || atoll("9000000000q") != 9000000000LL)
        fail = 7;

    copy = strdup("red,green,,blue");
    if (!copy)
        fail = 8;
    tok1 = strtok(copy, ",");
    tok2 = strtok(0, ",");
    tok3 = strtok(0, ",");
    tok4 = strtok(0, ",");
    if (!tok1 || !tok2 || !tok3 || tok4
        || strcmp(tok1, "red") || strcmp(tok2, "green")
        || strcmp(tok3, "blue"))
        fail = 9;
    free(copy);

    if (strnlen("abcdef", 3) != 3 || strnlen("abc", 10) != 3)
        fail = 10;
    if (!isblank('\\t') || !isgraph('A') || !isprint(' ')
        || isgraph(' '))
        fail = 11;

    printf("stdlib:%d:%d:%lld:%llu:%ld:%lld:%s:%s:%s\\n",
           values[0], values[n - 1], signed_value, unsigned_value,
           atol("-321x"), atoll("9000000000q"), tok1, tok2, tok3);
    return fail;
}
`;

const EXPECTED_STDOUT =
  'stdlib:-8:17:-1234567890123:18446744073709551615:-321:9000000000:red:green:blue\n';

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
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'tcc-wasm-stdlib-'));
  const cPath = path.join(tmpDir, 'stdlib_family.c');
  const watPath = path.join(tmpDir, 'stdlib_family.wat');
  const wasmPath = path.join(tmpDir, 'stdlib_family.wasm');

  fs.writeFileSync(cPath, SOURCE);
  run(path.join(ROOT, 'wasm32-tcc'), [
    '-nostdinc',
    '-nostdlib',
    '-Iinclude',
    '-Wl,--wasm-app',
    '-o',
    watPath,
    cPath
  ]);
  await assembleWat(watPath, wasmPath);

  const runtime = await Runtime.AppRuntime.create({
    libc: path.join(ROOT, 'libc.wasm')
  });
  const result = await runtime.run(fs.readFileSync(wasmPath));

  if (result.rc !== 0)
    throw new Error(`main returned ${result.rc}\\nstdout: ${JSON.stringify(result.stdout)}`);
  if (result.stdout !== EXPECTED_STDOUT)
    throw new Error(`stdout mismatch\\nexpected: ${JSON.stringify(EXPECTED_STDOUT)}\\nactual:   ${JSON.stringify(result.stdout)}`);

  console.log('wasm stdlib family tests ok');
})().catch(err => {
  console.error(err && err.stack || err);
  process.exit(1);
});
