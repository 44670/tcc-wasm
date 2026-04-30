const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const Runtime = require('../../web/runtime.js');
const { assembleWat } = require('./assemble_wat.js');

const ROOT = path.resolve(__dirname, '../..');
const encoder = new TextEncoder();
const decoder = new TextDecoder();

const IO_PRELUDE = `
int getchar(void);
int putchar(int ch);
int puts(const char *s);

int is_space(int c)
{
    return c == ' ' || c == '\\n' || c == '\\r' || c == '\\t';
}

int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

int next_int(void)
{
    int c;
    int sign = 1;
    int value = 0;

    do {
        c = getchar();
    } while (is_space(c));

    if (c == '-') {
        sign = -1;
        c = getchar();
    }

    while (is_digit(c)) {
        value = value * 10 + c - '0';
        c = getchar();
    }
    return value * sign;
}

void print_int(int value)
{
    char buf[16];
    int n = 0;

    if (value == 0) {
        putchar('0');
        return;
    }
    if (value < 0) {
        putchar('-');
        value = -value;
    }
    while (value > 0) {
        buf[n++] = (char)('0' + value % 10);
        value = value / 10;
    }
    while (n > 0)
        putchar(buf[--n]);
}

void print_space(void) { putchar(' '); }
void print_nl(void) { putchar('\\n'); }
`;

const cases = [
  {
    name: 'gcd',
    stdin: '462 1071\n',
    stdout: '21\n',
    program: `
int gcd(int a, int b)
{
    while (b != 0) {
        int t = a % b;
        a = b;
        b = t;
    }
    if (a < 0)
        a = -a;
    return a;
}

int main(void)
{
    print_int(gcd(next_int(), next_int()));
    print_nl();
    return 0;
}
`
  },
  {
    name: 'insertion_sort',
    stdin: '8 5 -1 7 3 3 0 -4 2\n',
    stdout: '-4 -1 0 2 3 3 5 7\n',
    program: `
int a[64];

int main(void)
{
    int n = next_int();
    int i;
    for (i = 0; i < n; ++i)
        a[i] = next_int();
    for (i = 1; i < n; ++i) {
        int x = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > x) {
            a[j + 1] = a[j];
            --j;
        }
        a[j + 1] = x;
    }
    for (i = 0; i < n; ++i) {
        if (i)
            print_space();
        print_int(a[i]);
    }
    print_nl();
    return 0;
}
`
  },
  {
    name: 'sieve_count',
    stdin: '30\n',
    stdout: '10\n',
    program: `
int composite[128];

int main(void)
{
    int n = next_int();
    int count = 0;
    int i;
    for (i = 2; i <= n; ++i) {
        if (!composite[i]) {
            int j;
            ++count;
            for (j = i + i; j <= n; j += i)
                composite[j] = 1;
        }
    }
    print_int(count);
    print_nl();
    return 0;
}
`
  },
  {
    name: 'bfs_distances',
    stdin: '6 6 0 1 0 2 1 3 2 3 3 4 4 5 0\n',
    stdout: '0 1 1 2 3 4\n',
    program: `
int head[16];
int to[64];
int next_edge[64];
int dist[16];
int queue[16];
int edge_count;

void add_edge(int u, int v)
{
    to[edge_count] = v;
    next_edge[edge_count] = head[u];
    head[u] = edge_count++;
}

int main(void)
{
    int n = next_int();
    int m = next_int();
    int i;
    int s;
    int qh = 0;
    int qt = 0;

    for (i = 0; i < n; ++i) {
        head[i] = -1;
        dist[i] = -1;
    }
    for (i = 0; i < m; ++i) {
        int u = next_int();
        int v = next_int();
        add_edge(u, v);
        add_edge(v, u);
    }

    s = next_int();
    dist[s] = 0;
    queue[qt++] = s;
    while (qh < qt) {
        int u = queue[qh++];
        int e;
        for (e = head[u]; e != -1; e = next_edge[e]) {
            int v = to[e];
            if (dist[v] == -1) {
                dist[v] = dist[u] + 1;
                queue[qt++] = v;
            }
        }
    }

    for (i = 0; i < n; ++i) {
        if (i)
            print_space();
        print_int(dist[i]);
    }
    print_nl();
    return 0;
}
`
  },
  {
    name: 'coin_change',
    stdin: '3 10 1 2 5\n',
    stdout: '10\n',
    program: `
int coin[16];
int ways[128];

int main(void)
{
    int n = next_int();
    int target = next_int();
    int i;
    ways[0] = 1;
    for (i = 0; i < n; ++i)
        coin[i] = next_int();
    for (i = 0; i < n; ++i) {
        int j;
        for (j = coin[i]; j <= target; ++j)
            ways[j] += ways[j - coin[i]];
    }
    print_int(ways[target]);
    print_nl();
    return 0;
}
`
  }
];

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
  return result;
}

function valueOf(exported) {
  return exported && typeof exported === 'object' && 'value' in exported
    ? exported.value
    : exported;
}

function readString(memory, ptr, len) {
  return decoder.decode(new Uint8Array(memory.buffer).subarray(ptr, ptr + len));
}

async function runCase(tmpDir, libcMod, test) {
  const cPath = path.join(tmpDir, `${test.name}.c`);
  const watPath = path.join(tmpDir, `${test.name}.wat`);
  const wasmPath = path.join(tmpDir, `${test.name}.wasm`);
  const source = `${IO_PRELUDE}\n${test.program}`;

  fs.writeFileSync(cPath, source);
  run(path.join(ROOT, 'wasm32-tcc'), [
    '-nostdinc',
    '-nostdlib',
    '-Wl,--wasm-app',
    '-o',
    watPath,
    cPath
  ]);
  await assembleWat(watPath, wasmPath);

  const appMod = await WebAssembly.compile(fs.readFileSync(wasmPath));
  const memory = new WebAssembly.Memory({ initial: 4096, maximum: 4096 });
  const libc = await WebAssembly.instantiate(libcMod,
    Runtime.createLibcImports(memory));
  const app = await WebAssembly.instantiate(appMod, {
    env: { memory },
    libc: libc.exports
  });

  const heapBase = valueOf(app.exports.__heap_base);
  const heapEnd = valueOf(app.exports.__heap_end);
  if (libc.exports.rt_init_heap(heapBase, heapEnd) !== 0)
    throw new Error(`${test.name}: failed to initialize libc heap`);

  const input = encoder.encode(test.stdin);
  const inputPtr = libc.exports.malloc(input.length || 1);

  if (!inputPtr)
    throw new Error(`${test.name}: failed to allocate stdin fixture`);
  new Uint8Array(memory.buffer).set(input, inputPtr);
  if (libc.exports.rt_stdin_set(inputPtr, input.length) !== input.length)
    throw new Error(`${test.name}: failed to seed stdin`);
  libc.exports.free(inputPtr);

  const rc = app.exports.main();
  if (rc !== 0)
    throw new Error(`${test.name}: main returned ${rc}`);

  const actual = readString(memory, libc.exports.rt_stdout_ptr(),
                            libc.exports.rt_stdout_len());
  if (actual !== test.stdout) {
    throw new Error(`${test.name}: stdout mismatch\nexpected: ${JSON.stringify(test.stdout)}\nactual:   ${JSON.stringify(actual)}`);
  }
}

(async () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'tcc-wasm-algorithms-'));
  const libcMod = await WebAssembly.compile(fs.readFileSync(path.join(ROOT, 'libc.wasm')));

  for (const test of cases)
    await runCase(tmpDir, libcMod, test);

  console.log(`wasm algorithm tests ok (${cases.length} cases)`);
})().catch(err => {
  console.error(err && err.stack || err);
  process.exit(1);
});
