#ifdef TARGET_DEFS_ONLY

#define EM_TCC_TARGET EM_NONE

#define R_DATA_32   1
#define R_DATA_PTR  R_DATA_32
#define R_JMP_SLOT  2
#define R_GLOB_DAT  3
#define R_COPY      4
#define R_RELATIVE  5
#define R_NUM       6

#define ELF_START_ADDR 0
#define ELF_PAGE_SIZE  0x10000

#define PCRELATIVE_DLLPLT 0
#define RELOCATE_DLLPLT 0

#else /* !TARGET_DEFS_ONLY */

#include "tcc.h"

ST_FUNC int code_reloc(int reloc_type)
{
    return 0;
}

ST_FUNC int gotplt_entry_type(int reloc_type)
{
    return NO_GOTPLT_ENTRY;
}

ST_FUNC unsigned create_plt_entry(TCCState *s1, unsigned got_offset,
                                  struct sym_attr *attr)
{
    tcc_error_noabort("wasm32: PLT is not implemented");
    return 0;
}

ST_FUNC void relocate_plt(TCCState *s1)
{
}

ST_FUNC void tccelf_add_crtend(TCCState *s1)
{
}

ST_FUNC void relocate(TCCState *s1, ElfW_Rel *rel, int type,
                      unsigned char *ptr, addr_t addr, addr_t val)
{
    if (type == R_DATA_32)
        add32le(ptr, val);
}

ST_FUNC int tcc_output_wast_file(TCCState *s1, FILE *f)
{
    int ret;
    tcc_enter_state(s1);
    ret = wasm32_output_module(f);
    tcc_exit_state(s1);
    return ret;
}

ST_FUNC int tcc_output_wast(TCCState *s1, const char *filename)
{
    FILE *f = fopen(filename, "w");
    int ret;
    if (!f)
        return tcc_error_noabort("could not write '%s': %s",
                                 filename, strerror(errno));
    ret = tcc_output_wast_file(s1, f);
    if (fclose(f))
        ret = tcc_error_noabort("could not close '%s': %s",
                                filename, strerror(errno));
    return ret;
}

#endif /* !TARGET_DEFS_ONLY */
