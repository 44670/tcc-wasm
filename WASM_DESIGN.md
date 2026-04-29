# Wasm Runtime Design

This document describes the intended ABI between TCC-generated wasm programs,
`libc.wasm`, and the JavaScript host used by both browser and Node.

The goal is a simple, deterministic runtime:

- fixed 256MB wasm memory, no growth
- one shared linear memory for all modules
- `libc.wasm` provides C runtime services
- `app.wasm` provides the application code and layout bounds
- the JS host owns instantiation order and startup wiring

## Modules

### `libc.wasm`

`libc.wasm` is built from `lib/wasm32-libc.c` by `wasm32-tcc`, not by
Emscripten. It is the runtime provider for generated apps.

In linked-runtime mode it should:

- import shared memory from the host
- reserve the first 1MB for libc static data and runtime control state
- export runtime functions such as `malloc`, `free`, `read`, `write`,
  `getchar`, `putchar`, and `puts`
- export host inspection helpers for buffered stdio
- initialize its heap allocator from app-provided heap bounds

The current build flag is:

```text
-Wl,--wasm-libc
```

### `app.wasm`

`app.wasm` is compiled from user C by the TCC wasm32 backend.

In linked-runtime mode it should:

- import the same shared memory from the host
- place static data at or above `0x00100000`
- use the normal TCC stack model above its static data
- export heap/layout bounds for the host
- import libc functions by name

The current build flag is:

```text
-Wl,--wasm-app
```

The app is the layout authority for its own data, stack, and heap start. The
host uses those exported bounds to initialize libc.

### JavaScript Host

The host is deliberately small and should run in both browser and Node. It owns
the `WebAssembly.Memory` object to avoid an instantiation cycle between app and
libc.

Startup order:

```js
const memory = new WebAssembly.Memory({
  initial: 4096,
  maximum: 4096
});

const libc = await instantiate(libcBytes, {
  env: { memory }
});

const app = await instantiate(appBytes, {
  env: { memory },
  libc: libc.exports
});

libc.exports.rt_init_heap(
  app.exports.__heap_base.value,
  app.exports.__heap_end.value
);

app.exports.main();
```

The host should reject missing imports/exports with explicit errors before
calling the app entry point.

## Memory Map

All addresses are offsets into the single shared memory.

```text
0x00000000 - 0x00000fff   reserved/null guard convention
0x00001000 - 0x000fffff   libc static data and runtime control state
0x00100000 - app data end app static data and BSS
app data end - heap base    app stack region
heap base - heap end        shared libc malloc heap
heap end - 0x0fffffff       reserved/future
```

Memory size is fixed:

```text
4096 pages * 65536 bytes = 256MB
```

No module should call `memory.grow`.

## Static Data Rules

`libc.wasm` may have active data segments, but they must fit below the 1MB
reserved boundary:

```text
libc.__data_end <= 0x00100000
```

`app.wasm` active data segments must start at `0x00100000` or above. In
linked-runtime mode the backend must not use the current standalone `1024` data
base for apps.

Required backend modes:

```text
libc.wasm: data_base = 0x00001000, static_limit = 0x00100000
app.wasm:  data_base = 0x00100000, imported_memory = true
```

If app layout crosses the configured heap end or reserved runtime range, the
backend should fail at compile/output time.

## Heap Contract

The app exports heap bounds:

```wat
(global $__heap_base (export "__heap_base") i32 (i32.const ...))
(global $__heap_end  (export "__heap_end")  i32 (i32.const ...))
```

The host passes those bounds to libc:

```c
int rt_init_heap(unsigned start, unsigned end);
```

Rules:

- `start` and `end` are byte offsets in shared memory
- `start` is aligned up to at least 8 bytes
- `end` is aligned down to at least 8 bytes
- `end > start + minimum allocator metadata`
- `malloc`, `calloc`, and `realloc` return null until `rt_init_heap` succeeds
- repeated initialization is rejected until a deliberate `rt_shutdown` design
  exists

`libc.wasm` should not keep a fixed static malloc heap in linked-runtime mode.
The allocator operates only over the app-provided heap range.

## Stdio Contract

`stdin`, `stdout`, and `stderr` are buffered by libc for deterministic tests and
browser use.

In linked-runtime mode their buffers are allocated from the heap during
`rt_init_heap`:

```text
stdin buffer    1MB
stdout buffer   1MB
stderr buffer   1MB
```

The buffer pointers remain stable for the lifetime of the runtime instance.

Public host-facing operations:

```c
int rt_stdin_set(const void *src, unsigned len);
int rt_stdin_append(const void *src, unsigned len);
int rt_stdin_remaining(void);

int rt_stdout_ptr(void);
int rt_stdout_len(void);
void rt_stdout_clear(void);

int rt_stderr_ptr(void);
int rt_stderr_len(void);
void rt_stderr_clear(void);
```

Program-facing operations:

```c
int read(int fd, void *dst, unsigned len);
int write(int fd, const void *src, unsigned len);
int getchar(void);
int putchar(int ch);
int puts(const char *s);
```

The initial wasm32 varargs ABI is available for 32-bit integer and pointer
arguments. That is enough for the current integer/string `printf` and `scanf`
families. Full hosted-libc behavior still depends on 64-bit vararg slots,
floating point lowering, and complete format handling.

The current libc provider implements the first printf family:

```c
int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int sprintf(char *dst, const char *fmt, ...);
int vsprintf(char *dst, const char *fmt, va_list ap);
int snprintf(char *dst, unsigned n, const char *fmt, ...);
int vsnprintf(char *dst, unsigned n, const char *fmt, va_list ap);
```

This path supports 32-bit integer and pointer slots only. Length modifiers are
accepted syntactically but cannot become complete until the backend has 64-bit
and floating-point vararg slots.

The current libc provider also implements the first scanf family:

```c
int scanf(const char *fmt, ...);
int vscanf(const char *fmt, va_list ap);
int sscanf(const char *src, const char *fmt, ...);
int vsscanf(const char *src, const char *fmt, va_list ap);
```

This path supports `%d`, `%i`, `%u`, `%x`, `%X`, `%o`, `%p`, `%c`, `%s`, `%n`,
`%%`, field width, assignment suppression, and `hh`/`h` integer stores.
Scansets, 64-bit stores, floating-point conversions, and fully specified
overflow behavior remain TODO.

## Varargs ABI

Wasm has fixed-arity function types, so C variadic functions use one hidden
parameter. A C function declared as:

```c
int f(int n, ...);
```

is emitted with this wasm-level shape:

```wat
(func $f (param $p0 i32) (param $__va_area i32) (result i32))
```

Caller rules:

- evaluate and promote the named arguments normally
- reserve a temporary stack array below the caller's current stack pointer
- store each variadic tail argument in ABI order as a 4-byte slot
- pass the base of that array as the hidden final argument
- restore the caller stack pointer after the call returns

Callee rules:

- spill only named parameters into its normal local frame
- treat the hidden `__va_area` parameter as the initial `va_list`
- `va_arg(ap, T)` rounds `sizeof(T)` up to 4, advances the `char *`, and loads
  from the old slot address

The core `va_list` contract deliberately does not include a count. C varargs
APIs supply their own bound through a named parameter, a format string, or a
sentinel. If host-side diagnostics need a count later, the caller can place a
debug-only header before the passed pointer without changing the public
`va_list` value.

## Imports

App modules import runtime functions from the `libc` module namespace:

```wat
(import "libc" "putchar" (func $putchar (param i32) (result i32)))
(import "libc" "write"   (func $write   (param i32 i32 i32) (result i32)))
(import "libc" "malloc"  (func $malloc  (param i32) (result i32)))
```

Both app and libc import memory from the `env` module namespace:

```wat
(import "env" "memory" (memory 4096 4096))
```

This avoids cyclic instantiation while still giving app and libc one address
space.

## Browser IDE

`web/ide-shell.html` is the browser product surface for the linked-runtime
design. It is a checked-in plain HTML shell that loads sibling artifacts with
relative fetches:

- `web/runtime.js`, the shared browser/Node runtime wrapper
- `web/tcc.wasm`, copied from the hosted TCC compiler wasm built by Emscripten
  as standalone wasm without the generated Emscripten JS runtime
- `web/libc.wasm`, copied from the runtime provider built by `wasm32-tcc`

The page compiles user C through `tcc_bare_compile_app`, displays the emitted
WAT, assembles WAT to app wasm in the browser, instantiates app and libc with
one fixed memory, seeds stdin through `rt_stdin_set`, calls `main`, and reads
stdout/stderr from libc's buffered stdio exports.

Emscripten is allowed in this layer only as the compiler used to host TCC
itself in the browser. User C code is still compiled by the wasm32 backend and
runs against `libc.wasm`.

The current IDE uses WABT from a CDN for WAT-to-wasm assembly. A fully offline
IDE should ship a local assembler payload or replace this with a binary wasm
writer.

## Test Strategy

Minimum linked-runtime tests:

- instantiate shared memory, libc, and app in Node
- verify app data starts at `0x00100000`
- verify libc static data fits below `0x00100000`
- call `rt_init_heap(app.__heap_base, app.__heap_end)`
- seed stdin through libc
- call app `main`
- compare captured stdout/stderr
- assert `malloc` fails before heap initialization
- assert invalid heap bounds are rejected
- assert stdio buffers are allocated from the heap range

The existing `wasm-algorithm-test` runs through the shared `libc.wasm` host so
program/stdin/expected-stdout fixtures exercise the same app/runtime ABI as the
IDE.
