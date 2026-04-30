# Wasm32 Backend Roadmap

This document is the durable design backlog for the `TCC_TARGET_WASM32`
backend. It is not a one-off punch list. Each item should either preserve a
clear backend invariant, move the backend toward a complete C ABI, or add a
test that prevents regression.

The shared runtime ABI and memory map are specified in `WASM_DESIGN.md`; this
file also tracks the remaining implementation tasks for that ABI.

## Current Position

The backend emits standalone WebAssembly text format (`.wat`) from TinyCC's
normal code generator hooks. It does not emit ELF objects and does not yet use
an external wasm linker.

Currently working:

- `wasm32-tcc -nostdlib -o out.wat file.c`
- `make wasm-test`, which builds `wasm32-tcc`, compiles every wasm32 smoke
  test to WAT, validates with WABT, runs Node runtime assertions, and
  checks important WAT shape properties
- `make wasm-compiler`, which builds the hosted TCC wasm compiler used by the
  browser IDE without Emscripten's generated JS runtime, MEMFS, `Module`, or
  `ccall`
- `make wasm-compiler-test`, which instantiates the hosted compiler wasm
  directly in Node and compiles fib, Duff's device, and a negative diagnostic
  case through the exported C ABI
- `make wasm-ide`, which prepares `web/ide-shell.html` plus sibling
  `web/tcc.wasm` and `web/libc.wasm` artifacts for the browser IDE. The page
  emits WAT/app wasm, accepts stdin, runs the app, and shows libc-captured
  stdout
- `make wasm-libc`, which builds `libc.wasm` from `lib/wasm32-libc.c` using
  `wasm32-tcc` and WABT
- `make wasm-libc-test`, which instantiates `libc.wasm` directly in Node and
  tests the libc import/export contract, heap setup, allocator edge cases,
  memory/string primitives, 1MB buffered stdin/stdout/stderr hooks, and direct
  printf/scanf-family calls
- `make wasm-runtime-test`, which instantiates `libc.wasm` and a TCC-generated
  app wasm with one host-owned memory, initializes libc from app heap bounds,
  feeds stdin, calls app `main`, and checks libc-captured stdout
- `make wasm-algorithm-test`, which compiles program/stdin/stdout fixtures,
  runs them in Node, and checks captured stdout
- `make wasm-printf-test`, which compiles and runs printf-family fixtures
  against `libc.wasm`
- `make wasm-lua-test`, which builds upstream Lua 5.1.5 sources through the
  wasm32 backend, runs the Lua REPL in Node through the shared runtime, and runs
  Lua's shipped `test/*.lua` sample corpus
- i32 integer scalars, pointers, char/short loads and stores
- direct calls and i32 scalar function returns
- function pointers through a wasm table and `call_indirect`
- initial 32-bit integer/pointer varargs ABI using caller-packed stack slots
- `libc.wasm` printf family for integer, pointer, char, string, width,
  precision, and common integer flags
- `libc.wasm` scanf family for integer, pointer, char, string, width,
  assignment suppression, `%n`, and small integer stores
- minimal in-memory file support in `libc.wasm`, including host-seeded files
  and basic `fopen`/`fread`/`fwrite`/`fseek`/`ftell`/`fclose`
- global data, string literals, simple pointer relocations, BSS/common symbols
- local stack frames in linear memory using `__stack_pointer`
- local arrays, structs, field access, `&local`, scalar spills
- `memset`, `memmove`, and `memcpy` fallbacks for compiler-emitted aggregate operations
- wasm-native `setjmp`/`longjmp` for protected-call style control flow, using
  Wasm exception handling tags when assembling linked runtime modules
- structured WAT emission for straight-line functions and simple reducible
  `if`/`else` diamonds with a shared return
- simple structured result paths use wasm stack results where possible, for
  example `(if (result i32) ...)` followed by stack restoration and bare
  `(return)`
- dispatcher-based CFG lowering for arbitrary forward/backward branches, including Duff's device

Known unsupported areas are intentionally explicit in `wasm32-gen.c`:

- VLA
- computed goto
- full aggregate ABI
- full hosted-library coverage for every C math/stdio edge case
- general wasm object files/linking beyond direct app imports from `libc.wasm`

## Design Principles

- Keep TinyCC's front end and generic lowering intact. The wasm backend should
  consume TCC's existing `SValue`, `load`, `store`, `gen_op*`, call, and branch
  hooks rather than inventing a parallel compiler pipeline.
- Make representation decisions explicit. Every C type that reaches wasm codegen
  should map through a small wasm value model, not ad hoc string formatting.
- Preserve arbitrary C control flow first. Structured wasm is an optimization
  tier over the recorded basic-block stream; the dispatcher remains the
  correctness fallback because it handles `goto`, switches, and Duff's device
  uniformly.
- Prefer standalone output while the backend is young. Built-in `mem*` helpers
  are acceptable until imports/runtime/linking are designed.
- Keep the browser demo honest: Emscripten may host the compiler, but the
  user's C input must still be compiled by this backend, not by Emscripten.
- Keep browser hosting layered. Emscripten may compile TCC itself to wasm, but
  the default durable host ABI should be plain wasm exports plus a small,
  documented import object; the generated Emscripten JS runtime is optional
  compatibility glue, not the compiler interface.
- Add tests with every semantic expansion. A feature is not "supported" until a
  `.c -> .wat -> WABT -> node` check exercises it.

## Core Architecture Target

### 1. Typed Wasm Value Layer

The backend now has a small internal wasm value model:

```c
typedef enum WasmValType {
    WVT_VOID,
    WVT_I32,
    WVT_I64,
    WVT_F32,
    WVT_F64,
} WasmValType;
```

Implemented:

- `wasm32_val_type(CType *)`.
- split local banks: `$rN` for `i32`, `$lN` for `i64`, `$fN` for `f32`, and
  `$dN` for `f64`.
- `WasmType` stores exact parameter/result value types, not just count plus
  `has_result`.
- `wasm32_value_expr`, `load`, `store`, `gfunc_call`, and table type emission
  use this model.
- `tests/wasm32/floats.c` covers `float`, `double`, mixed conversions, typed
  globals, typed function pointers, and exported `long long` interop.
- `tests/lua/lua_wasm_all.c` builds upstream Lua 5.1.5 with normal
  `lua_Number == double` and upstream `lmathlib.c`.

### 2. Memory and Stack Model

Current stack handling is static and simple:

- data begins at address `1024`
- stack starts after data plus `WASM32_STACK_SIZE`
- each function subtracts a static frame size from `__stack_pointer`
- locals and spills live at negative offsets from `$fp`

This is correct enough for non-VLA functions, but it needs hardening.

Tasks:

- Document exported memory layout in emitted module comments.
- Add stack overflow/underflow traps behind a debug flag.
- Separate `__data_end`, `__stack_base`, `__stack_pointer`, and `__heap_base`.
  Today `__heap_base` is after the reserved stack, which is pragmatic but should
  be made intentional.
- Make stack size configurable with a TCC option or a backend macro.
- Decide whether address `0` remains a null trap area and keep data away from it.

Acceptance tests:

- nested recursion with locals
- large local arrays
- address comparisons among globals, locals, null, and heap base

### 3. Control Flow Lowering

The backend now has two control-flow lowerings.

Structured lowering is attempted first for recognized reducible shapes:

- straight-line functions become direct WAT with no `$pc` dispatcher
- simple `if`/`else` diamonds with a shared return become wasm `if`/`else`
- branch conditions keep pure operand-stack expressions instead of forcing
  temporary `$rN` locals
- simple structured returns leave the result on the wasm operand stack, restore
  `__stack_pointer`, then use a bare `return`
- structured functions declare only the wasm locals actually referenced by the
  emitted body, not every TCC virtual register

The dispatcher model is still the correctness baseline:

- each basic block has an integer id
- branches assign `$pc` and `br $dispatch`
- returns restore stack and return directly

This is deliberately less pretty than structured wasm, but it is robust for C.
The rule is not "structured or bust"; it is "structured when proven, dispatcher
otherwise." Duff's device must continue taking the dispatcher path until a
general irreducible-control-flow strategy exists.

Tasks:

- Keep dispatcher as the fallback for any unrecognized CFG.
- Expand structured lowering incrementally:
  - direct `if` without `else`
  - single natural loops to wasm `loop`
  - nested reducible conditionals
  - reducible switches to `br_table`
- Split the structured emitter from text-string rewrites. Today it can peel
  `(local.set $rN EXPR)` into a stack result, but a durable version should carry
  a small typed expression record in `WasmOp`.
- Add comments in emitted WAT for block ids and original pseudo-pc ranges.
- Track whether each function used structured lowering or dispatcher fallback in
  a debug comment or optional diagnostic.
- Add explicit tests for irreducible `goto`, nested switch, and Duff variants.

Expression-stack invariant:

- Deferred `$rN` expressions may be folded into WAT only inside a straight-line
  region.
- A conditional branch may capture its compare expression, then must materialize
  or forget pending expressions before the CFG edge.
- A memory store is a side-effect boundary: all pending expressions are
  materialized before the store so post-increment and alias-sensitive code see
  the pre-store value.
- Function calls are side-effect boundaries: pending expressions unrelated to
  the callee/arguments are materialized before the call, argument expressions
  are consumed by the call itself, call results are emitted immediately, and the
  remaining expression state must not cross CFG joins unmaterialized.

Acceptance tests:

- fib-style recursion should emit structured WAT, not the dispatcher
- straight-line arithmetic helpers should emit direct WAT with only used locals
- structured result paths should use wasm stack values, not only
  `(return (local.get $r0))`
- direct call chains should keep call arguments on the wasm operand stack when
  doing so does not cross a side-effect boundary
- Duff's device with memory effects
- forward and backward gotos
- switch with default and sparse cases
- nested loops with break and continue

### 4. C ABI and Calls

The current ABI is "all scalar params/results are i32." This must become a real
wasm32 C ABI.

Tasks:

- Define scalar ABI:
  - `i32` for integer/pointer <= 32-bit
  - `i64` policy for `long long`: either native `i64` or preserve TCC two-word
    lowering. Pick one and make it consistent.
  - `f32` and `f64` for float/double
- Define aggregate ABI:
  - by-value struct arguments
  - struct returns using hidden sret pointer where needed
  - packed/small aggregate policy
- Expand indirect-call signatures to exact wasm types.
- Broaden imported external functions beyond the current linked-runtime libc
  namespace.
- Decide how libc/runtime symbols are resolved:
  - standalone built-ins for tiny smoke tests
  - imports for host-provided functions
  - later wasm object/linker integration

Acceptance tests:

- direct and indirect calls for each scalar wasm type
- void, scalar, and struct returns
- nested calls with register pressure
- calls to imported `puts` or a test host function

### 5. Floating Point

Floating point should be implemented through the typed value layer, not patched
directly into the current i32-only code.

Tasks:

- Add `f32.load`, `f64.load`, `f32.store`, `f64.store`.
- Implement `gen_opf`:
  - arithmetic: `add`, `sub`, `mul`, `div`
  - comparisons: `eq`, `ne`, `lt`, `le`, `gt`, `ge`
  - unary negation
- Implement conversions:
  - `i32.trunc_f32_s/u`, `i32.trunc_f64_s/u`
  - `f32.convert_i32_s/u`, `f64.convert_i32_s/u`
  - `f64.promote_f32`, `f32.demote_f64`
- Decide what to do with `long double`.
  - simplest initial policy: reject explicitly
  - later policy: map to `f64` only if target ABI says so

Acceptance tests:

- local/global float and double variables
- float arithmetic and comparisons
- casts between int, float, and double
- float function params, returns, and function pointers

### 6. Data, Relocations, and Linking

Current data relocation is intentionally narrow: 32-bit data/pointer relocation
inside allocated memory sections.

Tasks:

- Audit all relocation kinds that TCC can emit for wasm32.
- Error on unresolved non-import symbols with precise diagnostics.
- Add import declarations for unresolved functions/data when requested.
- Add support for function address relocations in data, already needed by global
  function pointers.
- Filter or disable all non-runtime metadata sections for WAT output.
  `.eh_frame` is disabled; keep debug/test coverage sections out unless a
  dedicated debug format is designed.
- Consider a future binary wasm writer or object format path. Keep WAT writer as
  the debug/reference backend.

Acceptance tests:

- global pointer to data
- global pointer to function
- arrays of function pointers
- common symbols across multiple inputs, once multi-input linking is supported

### 7. Browser IDE and Runtime Hosting

The browser IDE is the product surface for the backend, not a separate compiler
path.

Hosted compiler wasm:

- `web/tcc_browser_bare.c` embeds TCC through `tcc.c`, but exports a direct C
  ABI: `tcc_bare_compile`, `tcc_bare_compile_app`,
  `tcc_bare_compile_with_options`, `tcc_bare_output`,
  `tcc_bare_output_len`, `tcc_bare_error`, and `tcc_bare_error_len`.
- The compiler host is built with `--no-entry`, `STANDALONE_WASM=1`,
  `FILESYSTEM=0`, fixed 256MB memory, and wasm-native longjmp
  (`-fwasm-exceptions -sSUPPORT_LONGJMP=wasm`). This removes the generated
  Emscripten JS runtime and avoids JS `invoke_*` longjmp imports.
- The remaining imports are small WASI/env shims for libc edges that TCC still
  reaches, such as stdio writes, time, empty environment, and filesystem
  syscalls that currently return `-ENOSYS`.
- Output uses `open_memstream` plus `tcc_output_wast_file`, so no MEMFS output
  file is needed.

IDE host:

- `web/ide-shell.html` is the editable source. `make wasm-ide` prepares
  sibling `web/tcc.wasm` and `web/libc.wasm` artifacts; the IDE loads those and
  `web/runtime.js` directly instead of embedding wasm into generated HTML.
- The IDE compiles with `tcc_bare_compile_app`, displays WAT, assembles it to
  app wasm in the browser, then runs app wasm with one host-owned fixed memory
  shared with `libc.wasm`.
- Program input flows through `libc.rt_stdin_set`; output is read from
  `rt_stdout_ptr`/`rt_stdout_len` and `rt_stderr_ptr`/`rt_stderr_len`.
- The browser WAT assembler is WABT loaded from a CDN. Node tests use the
  pinned `wabt` npm package by default, with Binaryen `wasm-as` available as an
  explicit fallback via `WASM_ASSEMBLER=binaryen`.

Tasks:

- Keep `web/ide-shell.html` as the editable source, with `web/tcc.wasm`,
  `web/libc.wasm`, and `web/runtime.js` as sibling runtime assets.
- Decide whether browser WAT-to-wasm assembly stays on CDN WABT for now or
  becomes a local `web/wabt.wasm`/JS dependency.
- Add a small smoke test that opens `web/ide-shell.html` in headless Chromium
  and checks that the default fib example reaches `statusText == "Compiled"`.
- Add a second browser smoke test for Duff's device to catch repeated-compile
  state leaks.
- Make the page surface backend diagnostics without losing the previous
  successful WAT.
- Add a compact limitations drawer once the unsupported feature list becomes
  more useful to users than the current raw compiler errors.
- If older browser compatibility becomes necessary, add a second non-EH build
  target. Do not silently switch the primary target back to Emscripten JS
  longjmp glue.

Runtime-hosting design for larger C programs:

- Use one fixed memory: 4096 pages (256MB), minimum equals maximum.
- The host owns the `WebAssembly.Memory` object. Both `libc.wasm` and app wasm
  import it as `(import "env" "memory" (memory 4096 4096))`.
- `libc.wasm` keeps its static data below the first 1MB. User modules place
  static data at or above `0x00100000`, then export layout globals such as
  `__data_end`, `__heap_base`, and `__heap_end`.
- The host startup sequence should instantiate `libc.wasm`, instantiate the app
  with `libc` imports, call `rt_init_heap(app.__heap_base, app.__heap_end)`,
  then call the app entry point. This makes the app provide the dynamic heap
  range instead of baking one into `libc.wasm`.
- `rt_init_heap(start, end)` must align bounds, reject too-small or overlapping
  ranges, and leave `malloc` returning null until initialization succeeds.
- `rt_init_heap` should allocate libc-owned runtime state from that heap,
  including the 1MB stdin, stdout, and stderr buffers. Stdio pointers remain
  stable until a later `rt_shutdown`/reinitialization API exists.
- The backend should enforce a hard app ceiling so app static data/stack/heap
  cannot overlap libc fixed state or future reserved regions.
- Import runtime services through stable wrappers such as `malloc`, `free`,
  `read`, `write`, `putchar`, `puts`, and `printf`. Raw C varargs are now
  valid only for the initial 32-bit slot ABI; 64-bit and floating-point varargs
  remain unsupported.
- Add exports/imports only through documented symbols.

Standalone runtime provider:

- `lib/wasm32-libc.c` is compiled by this backend, not Emscripten. It is the
  bootstrap provider for `libc.wasm`.
- The first provider exports `malloc`, `free`, `realloc`, `calloc`, `memset`,
  `memcpy`, `memmove`, `memcmp`, string basics, simple stdio entry points,
  `strtod`, and integer/string plus basic floating-point `printf`/`scanf`
  families.
- `stdin`, `stdout`, and `stderr` use buffers with a 1MB capacity each,
  allocated from the app-provided `rt_init_heap` range. Host code can seed stdin
  through `rt_stdin_set`/`rt_stdin_append`, read captured output through
  `rt_stdout_ptr` plus `rt_stdout_len`, and reset state through
  `rt_stdio_reset`.
- `read`, `write`, `getchar`, `putchar`, and `puts` are non-vararg wrappers over
  those buffers. The wasm32 backend now has an initial 32-bit slot varargs ABI,
  so integer/string `printf` work can use the real C calling convention instead
  of inventing a separate one.
- The current provider imports host-owned memory and rejects allocation until
  `rt_init_heap(start, end)` succeeds.

Runtime/libc tasks:

- Add 64-bit integer formatting once 64-bit vararg slots are defined.
- Keep improving floating-point formatting precision and edge cases.
- Add scansets (`%[...]`) and stricter overflow/error behavior for scanf.
- Replace the small libc declarations in tests/demos with usable headers.
- Keep the in-memory file layer intentionally small unless a real POSIX-like
  filesystem contract is designed.
- Keep host-only math (`sin`, `pow`, `floor`, etc.) in `web/runtime.js` imports
  unless a C implementation needs direct app memory access.

### 8. Varargs and VLA

The initial varargs ABI uses caller-packed 4-byte stack slots and a hidden
`i32 __va_area` parameter after the named arguments. `va_list` is a `char *`
pointing at the first variadic slot; no hidden count is part of the public ABI.

Varargs tasks:

- [x] Define wasm32 `va_list` layout for 32-bit integer/pointer slots.
- [x] Pack the caller's variadic tail into an addressable stack array.
- [x] Add the hidden wasm parameter to direct, imported, and indirect variadic
  calls.
- [x] Implement `gen_va_start`.
- [x] Add `va_arg` runtime and WAT-shape tests for int, pointer, nested calls,
  zero extra args, and indirect variadic calls.
- [ ] Decide whether a debug-only count/header before `__va_area` is useful for
  host diagnostics; keep `va_list` itself as a pointer to the first argument.
- [ ] Add 64-bit integer vararg slots.
- [ ] Add double and mixed vararg tests after floating point is implemented.

VLA tasks:

- Allow dynamic stack adjustment inside a function.
- Preserve restore points for scope exit and gotos.
- Make `__stack_pointer` restoration interact correctly with dispatcher returns.

Acceptance tests:

- `sum(int n, ...)`
- mixed integer/double varargs
- local VLA indexing
- goto out of VLA scope

### 9. Computed Goto

Normal `goto` already works through the dispatcher. Computed goto needs label
addresses to be represented as dispatcher block ids or table entries.

Possible design:

- represent `&&label` as an integer dispatcher block id
- implement `ggoto()` as `$pc = label_id; br $dispatch`
- keep computed-goto values local to a function; reject cross-function use

Acceptance tests:

- threaded interpreter style dispatch
- label address stored in a local array
- reject label address escaping globally if unsupported

## Test Harness

The backend has a repeatable test command:

- `make wasm-test`
- implementation: `tests/wasm32/run.sh`
- assertions: `tests/wasm32/assert.js`
- hosted compiler smoke: `make wasm-compiler-test`
- runtime provider smoke: `make wasm-libc-test`
- shared runtime smoke: `make wasm-runtime-test`
- algorithm fixture smoke: `make wasm-algorithm-test`
- printf-family smoke: `make wasm-printf-test`
- scanf-family smoke: `make wasm-scanf-test`
- Lua 5.1.5 REPL and shipped sample tests: `make wasm-lua-test`
- IDE pipeline smoke: `make wasm-ide-test`

The harness currently:

1. Builds `wasm32-tcc`.
2. Compiles every `tests/wasm32/*.c` to `.wat`.
3. Assembles every `.wat` with WABT (`npm install` provides `wat2wasm`).
4. Runs Node assertions for exported functions.
5. Checks WAT shape for important codegen contracts:
   - fib uses structured WAT, not the dispatcher
   - straight-line arithmetic does not declare unnecessary result locals
   - Duff's device still uses the dispatcher fallback

Minimum always-on corpus:

- `fib.c`: recursion and direct calls
- `duff.c`: irreducible switch/goto-like control flow plus pointer post-increment
- `duff_control.c`: branch-only Duff validation
- `pointer_ops.c`: pointer loads/stores and post-increment
- `global_data.c`: data segments, rodata, string literals, pointer relocations
- `local_stack.c`: local arrays, structs, `&local`, struct copy
- `calls.c`: void calls and nested scalar calls
- `function_ptr.c`: direct and indirect calls, global function-pointer init
- `longlong.c`: current two-word arithmetic coverage
- `scalars.c`: char/short casts and bitfields
- `varargs.c`: initial 32-bit integer/pointer varargs ABI

Harness tasks:

- Add optional `wasm-opt --validate` when Binaryen is available.
- Add negative tests for unsupported features with exact diagnostics:
  float, 64-bit/floating varargs, VLA, computed goto, unsupported imports.
- Add a mode that compares checked-in `tests/wasm32/*.wat` against freshly
  generated output, so WAT shape changes are intentional.
- Add a real browser smoke test for `web/ide-shell.html`.
- Extend `wasm-compiler-test` to run in a real browser after the direct Node
  instantiation path is stable.
- Add broader user-module import tests now that the backend can emit imported
  memory and unresolved function imports against `libc.wasm`.
- Add more `rt_init_heap(start, end)` tests for invalid/unaligned bounds and
  exhaustion.
- Keep `wasm-algorithm-test` running through the shared `libc.wasm` host as the
  linked-runtime ABI evolves.
- Keep `make wasm-test`, `make wasm-libc-test`, `make wasm-runtime-test`,
  `make wasm-printf-test`, `make wasm-scanf-test`, `make wasm-lua-test`,
  `make wasm-ide-test`, and `make wasm-algorithm-test` passing after each ABI
  step.

## Near-Term Priority Order

1. Introduce the typed wasm value layer without changing behavior.
2. Tighten direct and indirect function signatures to exact wasm value types.
3. Decide the `long long` ABI policy before extending the type layer too far.
4. Implement float/double on top of typed values.
5. Replace fragile string-peeling in structured returns with typed expression
   records in `WasmOp`.
6. Broaden structured CFG lowering beyond straight-line and simple if/else.
7. Add broader imports for runtime services and document the supported libc
   surface.
8. Add a browser smoke test for `web/ide-shell.html`.
9. Extend varargs beyond 32-bit integer/pointer slots.
10. Implement VLA.
11. Implement computed goto.

## Definition of Done for New Backend Features

A feature is done only when:

- unsupported code paths are removed or narrowed to a more precise error
- generated WAT validates with WABT
- a Node runtime test covers the behavior
- `make wasm-test` passes, including WAT shape assertions when codegen shape is
  part of the feature
- the native `make tcc` build still succeeds
- the emitted module ABI is documented if it changes
