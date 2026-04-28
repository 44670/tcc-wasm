/*
 * Browser wrapper for the experimental wasm32 TCC backend.
 *
 * This file is compiled with Emscripten.  It embeds the normal TCC CLI in the
 * wasm module, but exposes one small C ABI that the HTML page can call after
 * writing source files into MEMFS.
 */

#include <emscripten/emscripten.h>

#define main tcc_cli_main
#include "../tcc.c"
#undef main

static void tcc_browser_reset_wasm32(void)
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
        }
        tcc_free(fn->ops);
        tcc_free(fn);
    }
    tcc_free(wasm32_funcs);
    wasm32_funcs = NULL;
    nb_wasm32_funcs = 0;

    wasm32_forget_all_reg_exprs();
    dynarray_reset(&wasm32_types, &nb_wasm32_types);
    wasm32_memory_end = 0;
    wasm32_stack_top = 0;
    wasm32_cur_func = NULL;
#endif
}

EMSCRIPTEN_KEEPALIVE
int tcc_compile_file(const char *input_path, const char *output_path)
{
    char *argv[8];
    int ret;

    argv[0] = "wasm32-tcc";
    argv[1] = "-nostdinc";
    argv[2] = "-nostdlib";
    argv[3] = "-I/include";
    argv[4] = "-o";
    argv[5] = (char *)output_path;
    argv[6] = (char *)input_path;
    argv[7] = NULL;

    tcc_browser_reset_wasm32();
    ret = tcc_cli_main(7, argv);
    tcc_browser_reset_wasm32();
    return ret;
}
