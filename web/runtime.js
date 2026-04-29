(function (root, factory) {
  if (typeof module === "object" && module.exports)
    module.exports = factory();
  else
    root.TccWasmRuntime = factory();
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  const DEFAULT_PAGES = 4096;
  const encoder = new TextEncoder();
  const decoder = new TextDecoder();

  function isNode() {
    return typeof process === "object" && process.versions && process.versions.node;
  }

  function valueOf(exported) {
    return exported && typeof exported === "object" && "value" in exported
      ? exported.value
      : exported;
  }

  function bytes(memory) {
    return new Uint8Array(memory.buffer);
  }

  function readString(memory, ptr, len) {
    if (!ptr || len <= 0)
      return "";
    return decoder.decode(bytes(memory).subarray(ptr, ptr + len));
  }

  async function readBytes(source) {
    if (source instanceof Uint8Array)
      return source;
    if (source instanceof ArrayBuffer)
      return new Uint8Array(source);
    if (ArrayBuffer.isView(source))
      return new Uint8Array(source.buffer, source.byteOffset, source.byteLength);
    if (typeof source === "string") {
      if (isNode()) {
        const fs = require("fs");
        return new Uint8Array(fs.readFileSync(source));
      }
      const response = await fetch(source);
      if (!response.ok)
        throw new Error(`could not fetch ${source}`);
      return new Uint8Array(await response.arrayBuffer());
    }
    throw new Error("unsupported wasm source");
  }

  async function compileModule(source) {
    if (source instanceof WebAssembly.Module)
      return source;
    return WebAssembly.compile(await readBytes(source));
  }

  function fillRandom(dst) {
    if (typeof crypto === "object" && crypto.getRandomValues) {
      crypto.getRandomValues(dst);
      return;
    }
    if (typeof require === "function") {
      try {
        require("crypto").randomFillSync(dst);
        return;
      } catch (err) {
        /* fall through to deterministic-enough fallback for non-crypto hosts */
      }
    }
    for (let i = 0; i < dst.length; ++i)
      dst[i] = (Math.random() * 256) | 0;
  }

  function createCompilerImports(getInstance) {
    const enosys = -52;

    function exp(name) {
      const instance = getInstance();
      return instance.exports[name] || instance.exports[`_${name}`];
    }

    function memory() {
      return exp("memory");
    }

    function storeU32(ptr, value) {
      if (ptr)
        new DataView(memory().buffer).setUint32(ptr, value, true);
    }

    function storeU64(ptr, value) {
      if (ptr)
        new DataView(memory().buffer).setBigUint64(ptr, BigInt(value), true);
    }

    function invoke(fn, ...args) {
      const table = exp("__indirect_function_table");
      const target = table && table.get(fn);
      if (!target)
        throw new Error(`bad indirect function ${fn}`);
      return target(...args);
    }

    return {
      env: {
        emscripten_notify_memory_growth() {},
        __syscall_getcwd() { return enosys; },
        __syscall_readlinkat() { return enosys; },
        _emscripten_throw_longjmp() { throw new Error("longjmp"); },
        invoke_ii: (fn, a) => invoke(fn, a),
        invoke_iii: (fn, a, b) => invoke(fn, a, b),
        invoke_iiii: (fn, a, b, c) => invoke(fn, a, b, c),
        invoke_iiiiii: (fn, a, b, c, d, e) => invoke(fn, a, b, c, d, e),
        invoke_iiiiiiii: (fn, a, b, c, d, e, f, g) => invoke(fn, a, b, c, d, e, f, g),
        invoke_v: fn => { invoke(fn); },
        invoke_vi: (fn, a) => { invoke(fn, a); },
        invoke_vii: (fn, a, b) => { invoke(fn, a, b); },
        invoke_viii: (fn, a, b, c) => { invoke(fn, a, b, c); },
        invoke_viiii: (fn, a, b, c, d) => { invoke(fn, a, b, c, d); },
        invoke_viiiii: (fn, a, b, c, d, e) => { invoke(fn, a, b, c, d, e); }
      },
      wasi_snapshot_preview1: {
        fd_write(fd, iovs, iovsLen, nwritten) {
          let written = 0;
          const view = new DataView(memory().buffer);
          for (let i = 0; i < iovsLen; ++i)
            written += view.getUint32(iovs + i * 8 + 4, true);
          storeU32(nwritten, written);
          return 0;
        },
        fd_close() { return 0; },
        fd_seek(fd, offset, whence, newOffset) {
          storeU64(newOffset, 0);
          return 0;
        },
        fd_read(fd, iovs, iovsLen, nread) {
          storeU32(nread, 0);
          return 0;
        },
        path_open() { return 44; },
        proc_exit(code) { throw new Error(`proc_exit(${code})`); },
        environ_sizes_get(count, size) {
          storeU32(count, 0);
          storeU32(size, 0);
          return 0;
        },
        environ_get() { return 0; },
        clock_time_get(clockId, precision, timePtr) {
          storeU64(timePtr, BigInt(Date.now()) * 1000000n);
          return 0;
        },
        random_get(ptr, len) {
          fillRandom(bytes(memory()).subarray(ptr, ptr + len));
          return 0;
        }
      }
    };
  }

  function createLibcImports(memory, options = {}) {
    const useNodeStdio = options.stdio === "inherit" && isNode();
    const fs = useNodeStdio ? require("fs") : null;

    function copyWrite(fd, ptr, len) {
      if (!useNodeStdio)
        return len;
      const outFd = fd === 2 ? 2 : 1;
      const chunk = Buffer.from(bytes(memory).subarray(ptr, ptr + len));
      return fs.writeSync(outFd, chunk, 0, chunk.length);
    }

    return {
      env: {
        memory,
        rt_host_read(fd, ptr, len) {
          if (!useNodeStdio)
            return 0;
          if (fd !== 0)
            return -9;
          const cap = Math.min(len, 65536);
          const buf = Buffer.alloc(cap || 1);
          const n = fs.readSync(0, buf, 0, cap, null);
          if (n > 0)
            bytes(memory).set(buf.subarray(0, n), ptr);
          return n;
        },
        rt_host_write(fd, ptr, len) {
          if (fd !== 1 && fd !== 2)
            return -9;
          return copyWrite(fd, ptr, len);
        },
        rt_host_exit(code) {
          const err = new Error(`app exited with code ${code}`);
          err.name = "WasmExit";
          err.code = code;
          throw err;
        },
        rt_host_isatty(fd) {
          if (!useNodeStdio)
            return fd === 0 ? 1 : 0;
          if (fd === 0)
            return process.stdin.isTTY ? 1 : 0;
          if (fd === 1)
            return process.stdout.isTTY ? 1 : 0;
          if (fd === 2)
            return process.stderr.isTTY ? 1 : 0;
          return 0;
        }
      }
    };
  }

  class CompilerHost {
    constructor(instance) {
      this.instance = instance;
      this.exports = instance.exports;
      this.memory = instance.exports.memory;
    }

    static async create(options = {}) {
      const wasm = options.wasm || "./tcc.wasm";
      const module = await compileModule(wasm);
      let instance = null;
      const imports = options.imports || createCompilerImports(() => instance);
      instance = await WebAssembly.instantiate(module, imports);
      const host = new CompilerHost(instance);
      host.initialize();
      return host;
    }

    initialize() {
      if (this.exports._initialize)
        this.exports._initialize();
      else if (this.exports.__wasm_call_ctors)
        this.exports.__wasm_call_ctors();
    }

    exp(name) {
      return this.exports[name] || this.exports[`_${name}`];
    }

    memoryBytes() {
      return bytes(this.memory);
    }

    readString(ptr, len) {
      return readString(this.memory, ptr, len);
    }

    writeCString(text) {
      const raw = encoder.encode(text);
      const ptr = this.exp("malloc")(raw.length + 1);
      if (!ptr)
        throw new Error("compiler malloc failed");
      const mem = this.memoryBytes();
      mem.set(raw, ptr);
      mem[ptr + raw.length] = 0;
      return ptr;
    }

    compileWithOptions(source, options) {
      const compile = this.exp("tcc_bare_compile_with_options");
      const free = this.exp("free");
      const sourcePtr = this.writeCString(source);
      const optionsPtr = this.writeCString(options || "");
      try {
        const rc = compile(sourcePtr, optionsPtr);
        const wat = this.readString(this.exp("tcc_bare_output")(),
                                    this.exp("tcc_bare_output_len")());
        const error = this.readString(this.exp("tcc_bare_error")(),
                                      this.exp("tcc_bare_error_len")());
        if (rc !== 0)
          throw new Error(error || "compile failed");
        return wat;
      } finally {
        free(optionsPtr);
        free(sourcePtr);
      }
    }

    compile(source) {
      const compile = this.exp("tcc_bare_compile");
      const free = this.exp("free");
      const sourcePtr = this.writeCString(source);
      try {
        const rc = compile(sourcePtr);
        const wat = this.readString(this.exp("tcc_bare_output")(),
                                    this.exp("tcc_bare_output_len")());
        const error = this.readString(this.exp("tcc_bare_error")(),
                                      this.exp("tcc_bare_error_len")());
        if (rc !== 0)
          throw new Error(error || "compile failed");
        return wat;
      } finally {
        free(sourcePtr);
      }
    }

    compileApp(source) {
      const compile = this.exp("tcc_bare_compile_app");
      const free = this.exp("free");
      const sourcePtr = this.writeCString(source);
      try {
        const rc = compile(sourcePtr);
        const wat = this.readString(this.exp("tcc_bare_output")(),
                                    this.exp("tcc_bare_output_len")());
        const error = this.readString(this.exp("tcc_bare_error")(),
                                      this.exp("tcc_bare_error_len")());
        if (rc !== 0)
          throw new Error(error || "compile failed");
        return wat;
      } finally {
        free(sourcePtr);
      }
    }
  }

  class AppRuntime {
    constructor(libcModule, options = {}) {
      this.libcModule = libcModule;
      this.pages = options.pages || DEFAULT_PAGES;
    }

    static async create(options = {}) {
      const libc = options.libc || "./libc.wasm";
      return new AppRuntime(await compileModule(libc), options);
    }

    validateAppImports(appModule, libcExports) {
      const imports = WebAssembly.Module.imports(appModule);
      const hasSharedMemory = imports.some(imp =>
        imp.module === "env" && imp.name === "memory" && imp.kind === "memory");
      if (!hasSharedMemory)
        throw new Error("app module did not import shared memory");
      for (const imp of imports) {
        if (imp.module === "libc" && !(imp.name in libcExports))
          throw new Error(`app imports unavailable libc symbol: ${imp.name}`);
      }
    }

    async run(appSource, options = {}) {
      const pages = options.pages || this.pages;
      const memory = options.memory || new WebAssembly.Memory({
        initial: pages,
        maximum: pages
      });
      const appModule = await compileModule(appSource);
      const libc = await WebAssembly.instantiate(this.libcModule,
        createLibcImports(memory, options));
      if (options.stdio === "inherit" && libc.exports.rt_stdio_set_hosted)
        libc.exports.rt_stdio_set_hosted(1);
      this.validateAppImports(appModule, libc.exports);
      const app = await WebAssembly.instantiate(appModule, {
        env: { memory },
        libc: libc.exports
      });
      const heapBase = valueOf(app.exports.__heap_base);
      const heapEnd = valueOf(app.exports.__heap_end);
      if (heapBase === undefined || heapEnd === undefined)
        throw new Error("app module did not export heap bounds");
      const entryName = options.entry || "main";
      const entry = app.exports[entryName] || app.exports[`_${entryName}`];
      if (typeof entry !== "function")
        throw new Error(`app module did not export entry: ${entryName}`);
      if (libc.exports.rt_init_heap(heapBase, heapEnd) !== 0)
        throw new Error("could not initialize libc heap");
      if (options.files)
        this.seedFiles(memory, libc.exports, options.files);
      let argv = null;
      if (entry.length >= 2 || options.args)
        argv = this.writeArgv(memory, libc.exports, options.args || [entryName]);
      if (options.stdio !== "inherit")
        this.seedStdin(memory, libc.exports, options.stdin || "");
      if (app.exports.__wasm_call_ctors)
        app.exports.__wasm_call_ctors();
      let rc;
      try {
        rc = argv ? entry(argv.argc, argv.argv) : entry();
      } catch (err) {
        if (!err || err.name !== "WasmExit")
          throw err;
        rc = err.code;
      }
      return {
        rc,
        stdout: this.readRuntimeBuffer(memory, libc.exports, "rt_stdout_ptr", "rt_stdout_len"),
        stderr: this.readRuntimeBuffer(memory, libc.exports, "rt_stderr_ptr", "rt_stderr_len"),
        memory,
        app,
        libc
      };
    }

    writeArgv(memory, exports, args) {
      const mem = bytes(memory);
      const argv = exports.malloc((args.length + 1) * 4);
      if (!argv)
        throw new Error("could not allocate argv");
      const view = new DataView(memory.buffer);
      for (let i = 0; i < args.length; ++i) {
        const raw = encoder.encode(args[i]);
        const ptr = exports.malloc(raw.length + 1);
        if (!ptr)
          throw new Error("could not allocate argv string");
        mem.set(raw, ptr);
        mem[ptr + raw.length] = 0;
        view.setUint32(argv + i * 4, ptr, true);
      }
      view.setUint32(argv + args.length * 4, 0, true);
      return { argc: args.length, argv };
    }

    seedStdin(memory, exports, input) {
      const raw = typeof input === "string" ? encoder.encode(input) : input;
      const inputBytes = raw instanceof Uint8Array ? raw : new Uint8Array(raw || 0);
      const ptr = exports.malloc(inputBytes.length || 1);
      if (!ptr)
        throw new Error("could not allocate stdin buffer");
      bytes(memory).set(inputBytes, ptr);
      const written = exports.rt_stdin_set(ptr, inputBytes.length);
      exports.free(ptr);
      if (written !== inputBytes.length)
        throw new Error("could not seed stdin");
    }

    seedFiles(memory, exports, files) {
      if (!exports.rt_file_add)
        throw new Error("runtime does not support seeded files");
      for (const name of Object.keys(files)) {
        const pathBytes = encoder.encode(name);
        const value = files[name];
        const raw = typeof value === "string" ? encoder.encode(value) : value;
        const fileBytes = raw instanceof Uint8Array ? raw : new Uint8Array(raw || 0);
        const pathPtr = exports.malloc(pathBytes.length + 1);
        const dataPtr = exports.malloc(fileBytes.length || 1);
        if (!pathPtr || !dataPtr)
          throw new Error("could not allocate seeded file");
        const mem = bytes(memory);
        mem.set(pathBytes, pathPtr);
        mem[pathPtr + pathBytes.length] = 0;
        mem.set(fileBytes, dataPtr);
        const written = exports.rt_file_add(pathPtr, dataPtr, fileBytes.length);
        exports.free(dataPtr);
        exports.free(pathPtr);
        if (written !== fileBytes.length)
          throw new Error(`could not seed file: ${name}`);
      }
    }

    readRuntimeBuffer(memory, exports, ptrName, lenName) {
      const ptr = exports[ptrName]();
      const len = exports[lenName]();
      return decoder.decode(bytes(memory).subarray(ptr, ptr + len));
    }
  }

  async function runApp(appSource, options = {}) {
    const runtime = await AppRuntime.create(options);
    return runtime.run(appSource, options);
  }

  return {
    DEFAULT_PAGES,
    CompilerHost,
    AppRuntime,
    compileModule,
    createLibcImports,
    readBytes,
    readString,
    runApp,
    valueOf
  };
});
