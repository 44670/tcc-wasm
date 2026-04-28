# Wasm32 Backend Roadmap

This document is the durable design backlog for the `TCC_TARGET_WASM32`
backend. It is not a one-off punch list. Each item should either preserve a
clear backend invariant, move the backend toward a complete C ABI, or add a
test that prevents regression.

## Current Position

The backend emits standalone WebAssembly text format (`.wat`) from TinyCC's
normal code generator hooks. It does not emit ELF objects and does not yet use
an external wasm linker.

Currently working:

- `wasm32-tcc -nostdlib -o out.wat file.c`
- `make wasm-test`, which builds `wasm32-tcc`, compiles every wasm32 smoke
  test to WAT, validates with `wasm-as`, runs Node runtime assertions, and
  checks important WAT shape properties
- `make wasm-demo`, which builds `web/tcc-wasm-demo.html`: a single-file
  browser UI where TCC itself runs as wasm and emits wasm32 WAT from C source
- i32 integer scalars, pointers, char/short loads and stores
- direct calls and i32 scalar function returns
- function pointers through a wasm table and `call_indirect`
- global data, string literals, simple pointer relocations, BSS/common symbols
- local stack frames in linear memory using `__stack_pointer`
- local arrays, structs, field access, `&local`, scalar spills
- `memset`, `memmove`, and `memcpy` fallbacks for compiler-emitted aggregate operations
- structured WAT emission for straight-line functions and simple reducible
  `if`/`else` diamonds with a shared return
- simple structured result paths use wasm stack results where possible, for
  example `(if (result i32) ...)` followed by stack restoration and bare
  `(return)`
- dispatcher-based CFG lowering for arbitrary forward/backward branches, including Duff's device

Known unsupported areas are intentionally explicit in `wasm32-gen.c`:

- floating point
- varargs
- VLA
- computed goto
- full aggregate ABI
- unresolved imports and real linking

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
- Add tests with every semantic expansion. A feature is not "supported" until a
  `.c -> .wat -> wasm-as -> node` check exercises it.

## Core Architecture Target

### 1. Typed Wasm Value Layer

Current code assumes almost all scalar values are `i32`. That is the main reason
floating point and full ABI support are blocked.

Introduce a small internal model:

```c
typedef enum WasmValType {
    WVT_VOID,
    WVT_I32,
    WVT_I64,
    WVT_F32,
    WVT_F64,
} WasmValType;
```

Required follow-up design:

- Add `wasm32_val_type(CType *)`.
- Replace hardcoded `$rN i32` with typed virtual locals or split banks:
  `$iN`, `$lN`, `$fN`, `$dN`.
- Make `WasmType` store exact parameter/result value types, not just count plus
  `has_result`.
- Make `wasm32_value_expr`, `load`, `store`, `gfunc_call`, and table type
  emission use this model.

Acceptance tests:

- `float addf(float, float)`
- `double addd(double, double)`
- mixed `int -> double`, `double -> int`
- function pointer with `double (*)(double)`
- global and local float arrays

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
- Support imported external functions with explicit `(import ...)` declarations.
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

### 7. Browser Demo and Runtime Hosting

The browser demo is a product surface for the backend, not a separate compiler
path:

- `web/tcc_browser.c` embeds the normal TCC CLI compiled by Emscripten.
- The page writes `/input.c` into MEMFS and calls
  `tcc_compile_file("/input.c", "/out.wat")`.
- The wrapper invokes `wasm32-tcc -nostdinc -nostdlib -I/include -o /out.wat
  /input.c`.
- `include/` is embedded into MEMFS so freestanding headers such as
  `stdint.h`, `stddef.h`, and `stdarg.h` are available.
- The wrapper resets wasm32 backend globals around each compile so repeated
  browser compiles do not retain old functions, indirect-call types, or WAT ops.
- The hosted compiler uses a fixed 256MB Emscripten memory with growth disabled.

Tasks:

- Keep `web/tcc-wasm-shell.html` as the editable source for the UI and
  `web/tcc-wasm-demo.html` as generated output.
- Add a small smoke test that opens the generated HTML in headless Chromium and
  checks that the default fib example reaches `statusText == "Compiled"`.
- Add a second browser smoke test for Duff's device to catch repeated-compile
  state leaks.
- Make the page surface backend diagnostics without losing the previous
  successful WAT.
- Add a compact limitations drawer once the unsupported feature list becomes
  more useful to users than the current raw compiler errors.

Runtime-hosting design for larger C programs:

- Use one fixed memory: 4096 pages (256MB), minimum equals maximum.
- Put TCC-emitted program data/stack in a low-memory region and enforce a hard
  ceiling below the Emscripten runtime provider's `GLOBAL_BASE`.
- Build the provider runtime with an explicit high `GLOBAL_BASE`, for example
  64MB, and make the TCC backend reject any layout whose `__heap_base` would
  reach that address.
- Import runtime services through stable wrappers such as `rt_malloc`,
  `rt_free`, `rt_puts`, and typed printf helpers; do not import raw C varargs
  until the wasm32 varargs ABI is implemented.
- Start with imported memory mode:
  `(import "env" "memory" (memory 4096 4096))`, then add exports/imports only
  through documented symbols.

### 8. Varargs and VLA

These need a stable stack and ABI design first.

Varargs tasks:

- Define wasm32 `va_list` layout.
- Spill incoming arguments to an addressable argument area when necessary.
- Implement `gen_va_start`.
- Add `va_arg` tests for int, pointer, double, and mixed arguments.

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

The harness currently:

1. Builds `wasm32-tcc`.
2. Compiles every `tests/wasm32/*.c` to `.wat`.
3. Assembles every `.wat` with Binaryen `wasm-as`.
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

Harness tasks:

- Add optional `wasm-opt --validate` when Binaryen is available.
- Add negative tests for unsupported features with exact diagnostics:
  float, varargs, VLA, computed goto, unresolved imports.
- Add a mode that compares checked-in `tests/wasm32/*.wat` against freshly
  generated output, so WAT shape changes are intentional.
- Add browser smoke tests for `web/tcc-wasm-demo.html`.

## Near-Term Priority Order

1. Introduce the typed wasm value layer without changing behavior.
2. Tighten direct and indirect function signatures to exact wasm value types.
3. Decide the `long long` ABI policy before extending the type layer too far.
4. Implement float/double on top of typed values.
5. Replace fragile string-peeling in structured returns with typed expression
   records in `WasmOp`.
6. Broaden structured CFG lowering beyond straight-line and simple if/else.
7. Add imports for unresolved functions and runtime services.
8. Add browser smoke tests for `web/tcc-wasm-demo.html`.
9. Implement varargs.
10. Implement VLA.
11. Implement computed goto.

## Definition of Done for New Backend Features

A feature is done only when:

- unsupported code paths are removed or narrowed to a more precise error
- generated WAT validates with `wasm-as`
- a Node runtime test covers the behavior
- `make wasm-test` passes, including WAT shape assertions when codegen shape is
  part of the feature
- the native `make tcc` build still succeeds
- the emitted module ABI is documented if it changes
