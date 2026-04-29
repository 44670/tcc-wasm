/*
 * Browser wrapper for the experimental wasm32 backend without Emscripten's
 * JS runtime, MEMFS, ccall, or generated Module object.
 *
 * Emscripten is still used as a C-to-wasm compiler for TCC itself.  The host
 * page loads the resulting wasm module directly and copies source/output bytes
 * through linear memory.
 */

#define main tcc_cli_main
#include "../tcc.c"
#undef main

static char *bare_output;
static size_t bare_output_len;
static char *bare_error;
static size_t bare_error_len;

static void bare_free_result(void)
{
    libc_free(bare_output);
    bare_output = NULL;
    bare_output_len = 0;
    tcc_free(bare_error);
    bare_error = NULL;
    bare_error_len = 0;
}

static void bare_append(char **buf, size_t *len, const char *msg)
{
    size_t msg_len = strlen(msg);
    size_t extra = msg_len + (*len ? 1 : 0);
    char *p = tcc_realloc(*buf, *len + extra + 1);
    if (!p)
        return;
    *buf = p;
    if (*len)
        (*buf)[(*len)++] = '\n';
    memcpy(*buf + *len, msg, msg_len);
    *len += msg_len;
    (*buf)[*len] = '\0';
}

static void bare_error_func(void *opaque, const char *msg)
{
    (void)opaque;
    bare_append(&bare_error, &bare_error_len, msg);
}

static void bare_reset_wasm32(void)
{
#ifdef TCC_TARGET_WASM32
    int i, j;

    for (i = 0; i < nb_wasm32_funcs; ++i) {
        WasmFunc *fn = wasm32_funcs[i];
        if (!fn)
            continue;
        tcc_free(fn->name);
        for (j = 0; j < fn->nb_ops; ++j) {
            tcc_free(fn->ops[j].cond);
            tcc_free(fn->ops[j].text);
            tcc_free(fn->ops[j].env);
        }
        tcc_free(fn->ops);
        tcc_free(fn);
    }
    tcc_free(wasm32_funcs);
    wasm32_funcs = NULL;
    nb_wasm32_funcs = 0;

    wasm32_forget_all_reg_exprs();
    dynarray_reset(&wasm32_types, &nb_wasm32_types);
    wasm32_free_imports();
    wasm32_memory_end = 0;
    wasm32_stack_top = 0;
    wasm32_cur_func = NULL;
    wasm32_needs_sjlj = 0;
#endif
}

__attribute__((used))
int tcc_bare_compile_with_options(const char *source, const char *options)
{
    TCCState *s;
    FILE *out;
    char *wat = NULL;
    size_t wat_len = 0;
    int ret = -1;

    bare_free_result();
    bare_reset_wasm32();

    s = tcc_new();
    if (!s) {
        bare_append(&bare_error, &bare_error_len, "could not allocate TCC state");
        return -1;
    }
    tcc_set_error_func(s, NULL, bare_error_func);
    tcc_set_options(s, options && *options ? options : "-nostdinc -nostdlib");
    if (tcc_set_output_type(s, TCC_OUTPUT_EXE) < 0)
        goto done;
    if (tcc_compile_string_file(s, source, "input.c") < 0)
        goto done;

    out = open_memstream(&wat, &wat_len);
    if (!out) {
        bare_append(&bare_error, &bare_error_len, "could not create output stream");
        goto done;
    }
    ret = tcc_output_wast_file(s, out);
    if (fclose(out)) {
        bare_append(&bare_error, &bare_error_len, "could not close output stream");
        ret = -1;
    }
    if (ret == 0) {
        bare_output = wat;
        bare_output_len = wat_len;
        wat = NULL;
    }

done:
    libc_free(wat);
    tcc_delete(s);
    bare_reset_wasm32();
    return ret;
}

__attribute__((used))
int tcc_bare_compile(const char *source)
{
    return tcc_bare_compile_with_options(source, "-nostdinc -nostdlib");
}

__attribute__((used))
int tcc_bare_compile_app(const char *source)
{
    return tcc_bare_compile_with_options(source,
        "-nostdinc -nostdlib -Wl,--wasm-app");
}

__attribute__((used))
const char *tcc_bare_output(void)
{
    return bare_output ? bare_output : "";
}

__attribute__((used))
int tcc_bare_output_len(void)
{
    return (int)bare_output_len;
}

__attribute__((used))
const char *tcc_bare_error(void)
{
    return bare_error ? bare_error : "";
}

__attribute__((used))
int tcc_bare_error_len(void)
{
    return (int)bare_error_len;
}
