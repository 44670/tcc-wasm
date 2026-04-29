#ifdef TARGET_DEFS_ONLY

#define NB_REGS 8

#define RC_INT    0x0001
#define RC_FLOAT  0x0002
#define RC_R0     0x0004
#define RC_R1     0x0008
#define RC_R2     0x0010
#define RC_R3     0x0020
#define RC_R4     0x0040
#define RC_R5     0x0080
#define RC_R6     0x0100
#define RC_R7     0x0200

#define RC_IRET   RC_R0
#define RC_IRE2   RC_R1
#define RC_FRET   RC_R0

#define REG_IRET  0
#define REG_IRE2  1
#define REG_FRET  0

#define PTR_SIZE 4

#define LDOUBLE_SIZE 16
#define LDOUBLE_ALIGN 16
#define MAX_ALIGN 16

#undef CONFIG_TCC_BCHECK

#else /* !TARGET_DEFS_ONLY */

#define USING_GLOBALS
#include "tcc.h"

ST_DATA const char * const target_machine_defs =
    "__wasm__\0"
    "__wasm32__\0"
    ;

ST_DATA const int reg_classes[NB_REGS] = {
    RC_INT | RC_R0,
    RC_INT | RC_R1,
    RC_INT | RC_R2,
    RC_INT | RC_R3,
    RC_INT | RC_R4,
    RC_INT | RC_R5,
    RC_INT | RC_R6,
    RC_INT | RC_R7,
};

enum {
    WOP_TEXT,
    WOP_JMP,
    WOP_CJMP,
    WOP_RET,
    WOP_SETJMP,
};

typedef struct WasmOp {
    int kind;
    int target;
    int next;
    int cmp_op;
    int cmp_a;
    int cmp_b;
    int is_return;
    char *cond;
    char *text;
    char *env;
} WasmOp;

typedef struct WasmFunc {
    char *name;
    int sym_index;
    int is_exported;
    int is_variadic;
    int va_param;
    int has_result;
    int base_pc;
    unsigned long frame_size;
    int nb_ops;
    WasmOp *ops;
    int nb_params;
    int uses_sjlj;
} WasmFunc;

typedef struct WasmType {
    int nb_params;
    int has_result;
} WasmType;

typedef struct WasmImport {
    char *name;
    int nb_params;
    int has_result;
} WasmImport;

typedef struct WasmBlockMap {
    unsigned char *start;
    int *starts;
    int *pc_to_block;
    int nb_starts;
} WasmBlockMap;

ST_DATA WasmFunc **wasm32_funcs;
ST_DATA int nb_wasm32_funcs;
static WasmType **wasm32_types;
static int nb_wasm32_types;
static WasmImport **wasm32_imports;
static int nb_wasm32_imports;
static unsigned long wasm32_memory_end;
static unsigned long wasm32_stack_top;
static WasmFunc *wasm32_cur_func;
static char *wasm32_reg_exprs[NB_REGS];
static int wasm32_next_jmp_is_return;
static int wasm32_needs_sjlj;

#define WASM32_STACK_SIZE 65536
#define WASM32_MODE_STANDALONE 0
#define WASM32_MODE_LIBC       1
#define WASM32_MODE_APP        2
#define WASM32_LIBC_BASE       0x1000UL
#define WASM32_APP_BASE        0x100000UL
#define WASM32_SHARED_PAGES    4096
#define WASM32_SHARED_LIMIT    (WASM32_SHARED_PAGES * 65536UL)

static unsigned long wasm32_align(unsigned long v, unsigned long a);
static int wasm32_supported_scalar(CType *type);
static int wasm32_func_table_index_by_sym_index(int sym_index);
static int wasm32_has_func(const char *name);
static void wasm32_emit_text(const char *fmt, ...);
static int wasm32_named_param_count(CType *type);
static int wasm32_is_variadic_func_type(CType *type);
static int wasm32_is_setjmp_name(const char *name);
static int wasm32_is_longjmp_name(const char *name);

static char *wasm32_printf_expr(const char *fmt, ...)
{
    CString cs;
    va_list ap;
    char *ret;

    cstr_new(&cs);
    va_start(ap, fmt);
    cstr_vprintf(&cs, fmt, ap);
    va_end(ap);
    ret = tcc_strdup(cs.data);
    cstr_free(&cs);
    return ret;
}

static void wasm32_forget_reg_expr(int r)
{
    if (r < 0 || r >= NB_REGS)
        return;
    tcc_free(wasm32_reg_exprs[r]);
    wasm32_reg_exprs[r] = NULL;
}

static void wasm32_forget_all_reg_exprs(void)
{
    int i;
    for (i = 0; i < NB_REGS; ++i)
        wasm32_forget_reg_expr(i);
}

static int wasm32_reg_expr_uses_local(int expr_reg, int local_reg)
{
    char needle[32];

    if (expr_reg < 0 || expr_reg >= NB_REGS || !wasm32_reg_exprs[expr_reg])
        return 0;
    snprintf(needle, sizeof needle, "(local.get $r%d)", local_reg);
    return strstr(wasm32_reg_exprs[expr_reg], needle) != NULL;
}

static void wasm32_materialize_reg_expr(int r);

static void wasm32_materialize_reg_exprs_using_local(int r)
{
    int i;

    if (r < 0 || r >= NB_REGS)
        return;
    for (i = 0; i < NB_REGS; ++i) {
        if (i != r && wasm32_reg_expr_uses_local(i, r))
            wasm32_materialize_reg_expr(i);
    }
}

static char *wasm32_get_reg_expr(int r)
{
    if (r >= 0 && r < NB_REGS && wasm32_reg_exprs[r])
        return tcc_strdup(wasm32_reg_exprs[r]);
    return wasm32_printf_expr("(local.get $r%d)", r);
}

static void wasm32_set_reg_expr_owned(int r, char *expr)
{
    if (r < 0 || r >= NB_REGS) {
        tcc_free(expr);
        return;
    }
    wasm32_materialize_reg_exprs_using_local(r);
    wasm32_forget_reg_expr(r);
    wasm32_reg_exprs[r] = expr;
}

static void wasm32_materialize_reg_expr(int r)
{
    if (r < 0 || r >= NB_REGS || !wasm32_reg_exprs[r])
        return;
    if (nocode_wanted)
        return;
    wasm32_materialize_reg_exprs_using_local(r);
    wasm32_emit_text("(local.set $r%d %s)", r, wasm32_reg_exprs[r]);
    wasm32_forget_reg_expr(r);
}

static void wasm32_materialize_all_reg_exprs(void)
{
    int i;
    for (i = 0; i < NB_REGS; ++i)
        wasm32_materialize_reg_expr(i);
}

static void wasm32_materialize_reg_exprs_except(int *keep)
{
    int i;
    for (i = 0; i < NB_REGS; ++i)
        if (!keep[i])
            wasm32_materialize_reg_expr(i);
}

static void wasm32_mark_svalue_regs(SValue *sv, int *keep)
{
    int v;

    if (sv->r == VT_CMP) {
        keep[sv->cmp_r & 255] = 1;
        keep[(sv->cmp_r >> 8) & 255] = 1;
        return;
    }
    v = sv->r & VT_VALMASK;
    if (v >= 0 && v < NB_REGS)
        keep[v] = 1;
    if (sv->r2 >= 0 && sv->r2 < NB_REGS)
        keep[sv->r2] = 1;
}

static void wasm32_retarget_jumps(WasmFunc *fn, int old_pc, int new_pc)
{
    int i;
    for (i = 0; i < fn->nb_ops; ++i) {
        WasmOp *op = &fn->ops[i];
        if ((op->kind == WOP_JMP || op->kind == WOP_CJMP)
            && op->target == old_pc
            && op->is_return)
            op->target = new_pc;
    }
}

ST_FUNC void wasm32_mark_return_jump(void)
{
    wasm32_next_jmp_is_return = 1;
}

static WasmFunc *wasm32_new_func(const char *name)
{
    WasmFunc *f = tcc_mallocz(sizeof *f);
    f->name = tcc_strdup(name);
    dynarray_add(&wasm32_funcs, &nb_wasm32_funcs, f);
    return f;
}

static void wasm32_vec_add_op(WasmFunc *f, WasmOp *op)
{
    f->ops = tcc_realloc(f->ops, (f->nb_ops + 1) * sizeof f->ops[0]);
    f->ops[f->nb_ops++] = *op;
}

static WasmOp *wasm32_op_at(int pc)
{
    WasmFunc *f = wasm32_cur_func;
    int i = pc - f->base_pc;
    if (i < 0 || i >= f->nb_ops)
        tcc_error("wasm32: bad branch chain pc %d", pc);
    return &f->ops[i];
}

static int wasm32_emit_op(WasmOp *op)
{
    int pc;
    if (nocode_wanted)
        return 0;
    pc = ind++;
    wasm32_vec_add_op(wasm32_cur_func, op);
    return pc;
}

static int wasm32_emit_setjmp_op(char *env)
{
    WasmOp op;

    if (nocode_wanted) {
        tcc_free(env);
        return 0;
    }
    memset(&op, 0, sizeof op);
    op.kind = WOP_SETJMP;
    op.env = env;
    if (wasm32_cur_func)
        wasm32_cur_func->uses_sjlj = 1;
    wasm32_needs_sjlj = 1;
    return wasm32_emit_op(&op);
}

static void wasm32_emit_text(const char *fmt, ...)
{
    CString cs;
    WasmOp op;
    va_list ap;

    if (nocode_wanted)
        return;
    cstr_new(&cs);
    va_start(ap, fmt);
    cstr_vprintf(&cs, fmt, ap);
    va_end(ap);
    memset(&op, 0, sizeof op);
    op.kind = WOP_TEXT;
    op.text = tcc_strdup(cs.data);
    cstr_free(&cs);
    wasm32_emit_op(&op);
}

static const char *wasm32_iop(int op, int uns)
{
    switch (op) {
    case '+': return "i32.add";
    case '-': return "i32.sub";
    case '*': return "i32.mul";
    case '&': return "i32.and";
    case '^': return "i32.xor";
    case '|': return "i32.or";
    case TOK_SHL: return "i32.shl";
    case TOK_SHR: return "i32.shr_u";
    case TOK_SAR: return "i32.shr_s";
    case '/':
    case TOK_PDIV: return "i32.div_s";
    case TOK_UDIV: return "i32.div_u";
    case '%': return "i32.rem_s";
    case TOK_UMOD: return "i32.rem_u";
    case TOK_EQ: return "i32.eq";
    case TOK_NE: return "i32.ne";
    case TOK_LT: return uns ? "i32.lt_u" : "i32.lt_s";
    case TOK_LE: return uns ? "i32.le_u" : "i32.le_s";
    case TOK_GT: return uns ? "i32.gt_u" : "i32.gt_s";
    case TOK_GE: return uns ? "i32.ge_u" : "i32.ge_s";
    case TOK_ULT: return "i32.lt_u";
    case TOK_ULE: return "i32.le_u";
    case TOK_UGT: return "i32.gt_u";
    case TOK_UGE: return "i32.ge_u";
    default:
        tcc_error("wasm32: unsupported integer op '%s'", get_tok_str(op, NULL));
        return "i32.add";
    }
}

static int wasm32_supported_func_type(CType *type, int *nb_params,
                                      int *has_result)
{
    Sym *sym;
    int n = 0;

    if ((type->t & VT_BTYPE) != VT_FUNC)
        return 0;
    *has_result = (type->ref->type.t & VT_BTYPE) != VT_VOID;
    if (!wasm32_supported_scalar(&type->ref->type))
        return 0;
    sym = type->ref;
    while ((sym = sym->next) != NULL) {
        if (!wasm32_supported_scalar(&sym->type))
            return 0;
        ++n;
    }
    if (type->ref->f.func_type == FUNC_ELLIPSIS)
        ++n;
    *nb_params = n;
    return 1;
}

static int wasm32_named_param_count(CType *type)
{
    Sym *sym;
    int n = 0;

    if ((type->t & VT_BTYPE) != VT_FUNC)
        return 0;
    sym = type->ref;
    while ((sym = sym->next) != NULL)
        ++n;
    return n;
}

static int wasm32_is_variadic_func_type(CType *type)
{
    return (type->t & VT_BTYPE) == VT_FUNC
        && type->ref->f.func_type == FUNC_ELLIPSIS;
}

static int wasm32_is_setjmp_name(const char *name)
{
    return !strcmp(name, "setjmp") || !strcmp(name, "_setjmp");
}

static int wasm32_is_longjmp_name(const char *name)
{
    return !strcmp(name, "longjmp") || !strcmp(name, "_longjmp");
}

static int wasm32_func_type_id(CType *type)
{
    int nb_params, has_result, i;
    WasmType *wt;

    if (!wasm32_supported_func_type(type, &nb_params, &has_result))
        tcc_error("wasm32: unsupported function pointer type");
    for (i = 0; i < nb_wasm32_types; ++i)
        if (wasm32_types[i]->nb_params == nb_params
            && wasm32_types[i]->has_result == has_result)
            return i;
    wt = tcc_mallocz(sizeof *wt);
    wt->nb_params = nb_params;
    wt->has_result = has_result;
    dynarray_add(&wasm32_types, &nb_wasm32_types, wt);
    return nb_wasm32_types - 1;
}

static void wasm32_note_direct_call(const char *name, CType *type)
{
    int nb_params, has_result, i;
    WasmImport *wi;

    if (!strcmp(name, "memset") || !strcmp(name, "memmove")
        || !strcmp(name, "memcpy"))
        return;
    if (!wasm32_supported_func_type(type, &nb_params, &has_result))
        tcc_error("wasm32: unsupported imported function pointer type");
    for (i = 0; i < nb_wasm32_imports; ++i) {
        wi = wasm32_imports[i];
        if (strcmp(wi->name, name))
            continue;
        if (wi->nb_params != nb_params || wi->has_result != has_result)
            tcc_error("wasm32: imported function '%s' used with incompatible type",
                      name);
        return;
    }
    wi = tcc_mallocz(sizeof *wi);
    wi->name = tcc_strdup(name);
    wi->nb_params = nb_params;
    wi->has_result = has_result;
    dynarray_add(&wasm32_imports, &nb_wasm32_imports, wi);
}

static void wasm32_free_imports(void)
{
    int i;
    for (i = 0; i < nb_wasm32_imports; ++i) {
        tcc_free(wasm32_imports[i]->name);
        tcc_free(wasm32_imports[i]);
    }
    tcc_free(wasm32_imports);
    wasm32_imports = NULL;
    nb_wasm32_imports = 0;
}

static void wasm32_cmp_expr(CString *cs, int op, int a, int b)
{
    char *ae = wasm32_get_reg_expr(a);
    char *be = wasm32_get_reg_expr(b);
    cstr_printf(cs, "(%s %s %s)", wasm32_iop(op, 0), ae, be);
    tcc_free(ae);
    tcc_free(be);
}

static const char *wasm32_load_op(CType *type)
{
    int t = type->t;
    switch (t & VT_BTYPE) {
    case VT_BOOL:
        return "i32.load8_u";
    case VT_BYTE:
        return (t & VT_UNSIGNED) ? "i32.load8_u" : "i32.load8_s";
    case VT_SHORT:
        return (t & VT_UNSIGNED) ? "i32.load16_u" : "i32.load16_s";
    case VT_INT:
    case VT_ENUM:
    case VT_PTR:
    case VT_FUNC:
        return "i32.load";
    default:
        tcc_error("wasm32: unsupported memory load type");
        return "i32.load";
    }
}

static const char *wasm32_store_op(CType *type)
{
    switch (type->t & VT_BTYPE) {
    case VT_BOOL:
    case VT_BYTE:
        return "i32.store8";
    case VT_SHORT:
        return "i32.store16";
    case VT_INT:
    case VT_ENUM:
    case VT_PTR:
    case VT_FUNC:
        return "i32.store";
    default:
        tcc_error("wasm32: unsupported memory store type");
        return "i32.store";
    }
}

static void wasm32_addr_expr(CString *cs, int r, int offset)
{
    char *base = wasm32_get_reg_expr(r);
    if (offset)
        cstr_printf(cs, "(i32.add %s (i32.const %d))", base, offset);
    else
        cstr_printf(cs, "%s", base);
    tcc_free(base);
}

static void wasm32_local_addr_expr(CString *cs, int offset)
{
    if (offset)
        cstr_printf(cs, "(i32.add (local.get $fp) (i32.const %d))", offset);
    else
        cstr_printf(cs, "(local.get $fp)");
}

static void wasm32_sym_addr_expr(CString *cs, Sym *sym, int offset)
{
    int sym_index;
    if (!sym->c)
        put_extern_sym(sym, NULL, 0, 0);
    sym_index = sym->c;
    if ((sym->type.t & VT_BTYPE) == VT_FUNC) {
        if (offset)
            tcc_error("wasm32: function address with addend is not supported");
        cstr_printf(cs, "(global.get $__funcidx_%d)", sym_index);
        return;
    }
    if (offset)
        cstr_printf(cs, "(i32.add (global.get $__addr_%d) (i32.const %d))",
                    sym_index, offset);
    else
        cstr_printf(cs, "(global.get $__addr_%d)", sym_index);
}

static void wasm32_value_expr(CString *cs, SValue *sv)
{
    int v = sv->r & VT_VALMASK;

    if (sv->r == VT_CMP) {
        wasm32_cmp_expr(cs, sv->cmp_op, sv->cmp_r & 255,
                        (sv->cmp_r >> 8) & 255);
    } else if (sv->r & VT_LVAL) {
        if ((sv->r & VT_SYM) && v == VT_CONST) {
            CString addr;
            cstr_new(&addr);
            wasm32_sym_addr_expr(&addr, sv->sym, sv->c.i);
            cstr_printf(cs, "(%s %s)", wasm32_load_op(&sv->type), addr.data);
            cstr_free(&addr);
        } else if (v == VT_LOCAL) {
            CString addr;
            cstr_new(&addr);
            wasm32_local_addr_expr(&addr, sv->c.i);
            cstr_printf(cs, "(%s %s)", wasm32_load_op(&sv->type), addr.data);
            cstr_free(&addr);
        } else if (v == VT_LLOCAL) {
            CString addr;
            cstr_new(&addr);
            wasm32_local_addr_expr(&addr, sv->c.i);
            cstr_printf(cs, "(%s (i32.load %s))",
                        wasm32_load_op(&sv->type), addr.data);
            cstr_free(&addr);
        } else if (v < VT_CONST) {
            CString addr;
            cstr_new(&addr);
            wasm32_addr_expr(&addr, v, 0);
            cstr_printf(cs, "(%s %s)", wasm32_load_op(&sv->type), addr.data);
            cstr_free(&addr);
        } else {
            tcc_error("wasm32: unsupported lvalue load");
        }
    } else if (v == VT_CONST) {
        if (sv->r & VT_SYM)
            wasm32_sym_addr_expr(cs, sv->sym, sv->c.i);
        else
            cstr_printf(cs, "(i32.const %d)", (int)sv->c.i);
    } else if (v == VT_LOCAL) {
        wasm32_local_addr_expr(cs, sv->c.i);
    } else if (v == VT_LLOCAL) {
        CString addr;
        cstr_new(&addr);
        wasm32_local_addr_expr(&addr, sv->c.i);
        cstr_printf(cs, "(i32.load %s)", addr.data);
        cstr_free(&addr);
    } else if (v < VT_CONST) {
        char *expr = wasm32_get_reg_expr(v);
        cstr_printf(cs, "%s", expr);
        tcc_free(expr);
    } else {
        tcc_error("wasm32: unsupported value kind 0x%x", sv->r);
    }
}

static void wasm32_set_reg_expr_from_svalue(int r, SValue *sv)
{
    CString cs;
    cstr_new(&cs);
    wasm32_value_expr(&cs, sv);
    wasm32_set_reg_expr_owned(r, tcc_strdup(cs.data));
    cstr_free(&cs);
}

static int wasm32_supported_scalar(CType *type)
{
    int bt = type->t & VT_BTYPE;
    return bt == VT_VOID
        || bt == VT_BOOL
        || bt == VT_BYTE
        || bt == VT_SHORT
        || bt == VT_INT
        || bt == VT_ENUM
        || bt == VT_PTR
        || bt == VT_FUNC;
}

ST_FUNC void o(unsigned int c)
{
    wasm32_emit_text("(; raw 0x%x ;)", c);
}

ST_FUNC void gsym_addr(int t, int a)
{
    if (t && a == ind) {
        wasm32_materialize_all_reg_exprs();
        a = ind;
    }
    while (t) {
        WasmOp *op = wasm32_op_at(t);
        int next = op->next;
        op->target = a;
        t = next;
    }
}

ST_FUNC void load(int r, SValue *sv)
{
    int bt = sv->type.t & VT_BTYPE;
    int v = sv->r & VT_VALMASK;
    if (!(sv->r & VT_LVAL) && (v == VT_JMP || v == VT_JMPI)) {
        int t = v & 1;
        int j;
        wasm32_forget_reg_expr(r);
        wasm32_emit_text("(local.set $r%d (i32.const %d))", r, t);
        j = gjmp(0);
        gsym(sv->c.i);
        wasm32_emit_text("(local.set $r%d (i32.const %d))", r, t ^ 1);
        gsym(j);
        return;
    }
    if (!wasm32_supported_scalar(&sv->type)
        && !(bt == VT_LLONG && !(sv->r & VT_LVAL)))
        tcc_error("wasm32: load of unsupported scalar type");
    wasm32_set_reg_expr_from_svalue(r, sv);
    if ((sv->r & VT_LVAL) || (!(sv->r & VT_LVAL) && v < VT_CONST && v != r))
        wasm32_materialize_reg_expr(r);
}

ST_FUNC void store(int r, SValue *sv)
{
    int v = sv->r & VT_VALMASK;
    char *expr;
    wasm32_materialize_all_reg_exprs();
    expr = wasm32_get_reg_expr(r);
    if ((sv->r & VT_LVAL) && (sv->r & VT_SYM) && v == VT_CONST) {
        CString addr;
        cstr_new(&addr);
        wasm32_sym_addr_expr(&addr, sv->sym, sv->c.i);
        wasm32_emit_text("(%s %s %s)",
                         wasm32_store_op(&sv->type), addr.data, expr);
        cstr_free(&addr);
    } else if ((sv->r & VT_LVAL) && (v == VT_LOCAL || v == VT_LLOCAL)) {
        CString addr;
        cstr_new(&addr);
        wasm32_local_addr_expr(&addr, sv->c.i);
        if (v == VT_LLOCAL)
            wasm32_emit_text("(%s (i32.load %s) %s)",
                             wasm32_store_op(&sv->type), addr.data, expr);
        else
            wasm32_emit_text("(%s %s %s)",
                             wasm32_store_op(&sv->type), addr.data, expr);
        cstr_free(&addr);
    } else if ((sv->r & VT_LVAL) && v < VT_CONST) {
        CString addr;
        cstr_new(&addr);
        wasm32_addr_expr(&addr, v, 0);
        wasm32_emit_text("(%s %s %s)",
                         wasm32_store_op(&sv->type), addr.data, expr);
        cstr_free(&addr);
    } else {
        tcc_free(expr);
        tcc_error("wasm32: only scalar local stores are implemented");
    }
    tcc_free(expr);
}

ST_FUNC void gfunc_call(int nb_args)
{
    SValue *func = vtop - nb_args;
    CString cs;
    const char *name;
    int has_result;
    int direct;
    int variadic;
    int named_args;
    int call_args;
    unsigned long va_size = 0;
    int keep[NB_REGS];
    int i;

    direct = (func->r & (VT_VALMASK | VT_SYM | VT_LVAL))
             == (VT_CONST | VT_SYM) && func->sym;
    if (direct) {
        name = get_tok_str(func->sym->v, NULL);
        if (wasm32_is_setjmp_name(name)) {
            CString env;

            if (nb_args != 1)
                tcc_error("wasm32: setjmp expects one argument");
            save_regs(nb_args + 1);
            wasm32_materialize_all_reg_exprs();
            cstr_new(&env);
            wasm32_value_expr(&env, func + 1);
            wasm32_emit_setjmp_op(tcc_strdup(env.data));
            cstr_free(&env);
            wasm32_forget_reg_expr(REG_IRET);
            vtop -= nb_args + 1;
            return;
        }
    }
    variadic = wasm32_is_variadic_func_type(&func->type);
    named_args = wasm32_named_param_count(&func->type);
    if (variadic) {
        unsigned long off = 0;
        for (i = named_args; i < nb_args; ++i) {
            int size, align;
            CType *type = &func[1 + i].type;
            if (!wasm32_supported_scalar(type))
                tcc_error("wasm32: unsupported vararg type");
            size = type_size(type, &align);
            if (size <= 0 || size > 4)
                tcc_error("wasm32: unsupported vararg size %d", size);
            off += wasm32_align(size, 4);
        }
        va_size = wasm32_align(off, 16);
    }
    call_args = variadic ? named_args : nb_args;

    save_regs(nb_args + 1);
    for (i = 0; i < NB_REGS; ++i)
        keep[i] = 0;
    for (i = 0; i <= nb_args; ++i)
        wasm32_mark_svalue_regs(func + i, keep);
    wasm32_materialize_reg_exprs_except(keep);

    if (variadic && va_size) {
        unsigned long off = 0;
        wasm32_emit_text("(global.set $__stack_pointer (i32.sub (global.get $__stack_pointer) (i32.const %lu)))",
                         va_size);
        for (i = named_args; i < nb_args; ++i) {
            CString arg, addr;
            int size, align;
            cstr_new(&arg);
            cstr_new(&addr);
            wasm32_value_expr(&arg, func + 1 + i);
            if (off)
                cstr_printf(&addr,
                            "(i32.add (global.get $__stack_pointer) (i32.const %lu))",
                            off);
            else
                cstr_printf(&addr, "(global.get $__stack_pointer)");
            wasm32_emit_text("(i32.store %s %s)", addr.data, arg.data);
            size = type_size(&func[1 + i].type, &align);
            off += wasm32_align(size, 4);
            cstr_free(&addr);
            cstr_free(&arg);
        }
    }

    has_result = (func->type.ref->type.t & VT_BTYPE) != VT_VOID;
    cstr_new(&cs);
    if (direct) {
        wasm32_note_direct_call(name, &func->type);
        if (has_result)
            cstr_printf(&cs, "(local.set $r%d (call $%s", REG_IRET, name);
        else
            cstr_printf(&cs, "(call $%s", name);
        for (i = 0; i < call_args; ++i) {
            CString arg;
            cstr_new(&arg);
            wasm32_value_expr(&arg, func + 1 + i);
            cstr_printf(&cs, " %s", arg.data);
            cstr_free(&arg);
        }
        if (variadic)
            cstr_printf(&cs, " (global.get $__stack_pointer)");
        cstr_printf(&cs, has_result ? "))" : ")");
    } else {
        int type_id = wasm32_func_type_id(&func->type);
        CString callee;
        if (has_result)
            cstr_printf(&cs, "(local.set $r%d (call_indirect (type $ft%d)",
                        REG_IRET, type_id);
        else
            cstr_printf(&cs, "(call_indirect (type $ft%d)", type_id);
        for (i = 0; i < call_args; ++i) {
            CString arg;
            cstr_new(&arg);
            wasm32_value_expr(&arg, func + 1 + i);
            cstr_printf(&cs, " %s", arg.data);
            cstr_free(&arg);
        }
        if (variadic)
            cstr_printf(&cs, " (global.get $__stack_pointer)");
        cstr_new(&callee);
        wasm32_value_expr(&callee, func);
        cstr_printf(&cs, " %s", callee.data);
        cstr_free(&callee);
        cstr_printf(&cs, has_result ? "))" : ")");
    }
    if (has_result)
        wasm32_forget_reg_expr(REG_IRET);
    wasm32_emit_text("%s", cs.data);
    if (variadic && va_size)
        wasm32_emit_text("(global.set $__stack_pointer (i32.add (global.get $__stack_pointer) (i32.const %lu)))",
                         va_size);
    cstr_free(&cs);
    vtop -= nb_args + 1;
}

ST_FUNC void gfunc_prolog(Sym *func_sym)
{
    Sym *sym;
    int param = 0;

    wasm32_forget_all_reg_exprs();
    if (!func_sym->c)
        put_extern_sym(func_sym, NULL, 0, 0);
    wasm32_cur_func = wasm32_new_func(get_tok_str(func_sym->v, NULL));
    wasm32_cur_func->sym_index = func_sym->c;
    wasm32_cur_func->is_exported = 0 == (func_sym->type.t & VT_STATIC);
    wasm32_cur_func->is_variadic = wasm32_is_variadic_func_type(&func_sym->type);
    wasm32_cur_func->va_param = -1;
    wasm32_cur_func->has_result = (func_vt.t & VT_BTYPE) != VT_VOID;
    if (!wasm32_supported_scalar(&func_vt))
        tcc_error("wasm32: unsupported function result type");

    wasm32_cur_func->base_pc = ind + 1;
    ind = wasm32_cur_func->base_pc;
    loc = 0;

    sym = func_sym->type.ref;
    while ((sym = sym->next) != NULL) {
        int size, align, c;
        if (!wasm32_supported_scalar(&sym->type))
            tcc_error("wasm32: unsupported parameter type");
        size = type_size(&sym->type, &align);
        loc = (loc - size) & -align;
        c = loc;
        wasm32_cur_func->nb_params++;
        gfunc_set_param(sym, c, 0);
        {
            CString addr;
            cstr_new(&addr);
            wasm32_local_addr_expr(&addr, c);
            wasm32_emit_text("(%s %s (local.get $p%d))",
                             wasm32_store_op(&sym->type), addr.data, param);
            cstr_free(&addr);
        }
        ++param;
    }
    if (wasm32_cur_func->is_variadic) {
        wasm32_cur_func->va_param = param;
        wasm32_cur_func->nb_params++;
        ++param;
    }
}

ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret,
                       int *ret_align, int *regsize)
{
    *ret_align = 1;
    *regsize = 4;
    return 0;
}

ST_FUNC void gfunc_epilog(void)
{
    WasmOp op;
    int old_epilog = ind;
    int has_result_expr = wasm32_cur_func->has_result
        && REG_IRET >= 0 && REG_IRET < NB_REGS
        && wasm32_reg_exprs[REG_IRET] != NULL;

    if (has_result_expr)
        wasm32_materialize_reg_expr(REG_IRET);
    wasm32_forget_all_reg_exprs();
    wasm32_cur_func->frame_size = wasm32_align(-loc, 16);
    memset(&op, 0, sizeof op);
    op.kind = WOP_RET;
    wasm32_emit_op(&op);
    if (has_result_expr)
        wasm32_retarget_jumps(wasm32_cur_func, old_epilog, ind - 1);
}

ST_FUNC void gen_fill_nops(int bytes)
{
    while (bytes-- > 0)
        wasm32_emit_text("(nop)");
}

ST_FUNC int gjmp(int t)
{
    WasmOp op;
    if (nocode_wanted)
        return t;
    wasm32_materialize_all_reg_exprs();
    memset(&op, 0, sizeof op);
    op.kind = WOP_JMP;
    op.target = -1;
    op.next = t;
    op.is_return = wasm32_next_jmp_is_return;
    wasm32_next_jmp_is_return = 0;
    return wasm32_emit_op(&op);
}

ST_FUNC void gjmp_addr(int a)
{
    WasmOp op;
    if (nocode_wanted)
        return;
    wasm32_materialize_all_reg_exprs();
    memset(&op, 0, sizeof op);
    op.kind = WOP_JMP;
    op.target = a;
    op.next = 0;
    wasm32_emit_op(&op);
}

ST_FUNC int gjmp_cond(int op, int t)
{
    WasmOp wop;
    CString cond;
    if (nocode_wanted)
        return t;
    memset(&wop, 0, sizeof wop);
    wop.kind = WOP_CJMP;
    wop.target = -1;
    wop.next = t;
    wop.cmp_op = op;
    wop.cmp_a = vtop->cmp_r & 255;
    wop.cmp_b = (vtop->cmp_r >> 8) & 255;
    wasm32_materialize_reg_expr(wop.cmp_a);
    wasm32_materialize_reg_expr(wop.cmp_b);
    cstr_new(&cond);
    wasm32_cmp_expr(&cond, op, wop.cmp_a, wop.cmp_b);
    wop.cond = tcc_strdup(cond.data);
    cstr_free(&cond);
    wasm32_materialize_all_reg_exprs();
    return wasm32_emit_op(&wop);
}

ST_FUNC int gjmp_append(int n, int t)
{
    if (n) {
        int n1 = n, n2;
        WasmOp *op;
        do {
            op = wasm32_op_at(n1);
            n2 = op->next;
            if (!n2)
                break;
            n1 = n2;
        } while (1);
        op->next = t;
        t = n;
    }
    return t;
}

ST_FUNC void gen_opi(int op)
{
    int r, a, b, uns;

    if (op == TOK_ADDC1 || op == TOK_ADDC2
        || op == TOK_SUBC1 || op == TOK_SUBC2) {
        gv2(RC_INT, RC_INT);
        a = vtop[-1].r & VT_VALMASK;
        b = vtop[0].r & VT_VALMASK;
        wasm32_materialize_reg_expr(a);
        wasm32_materialize_reg_expr(b);
        vtop--;
        r = a;
        if (op == TOK_ADDC1) {
            wasm32_emit_text("(local.set $carry (i32.lt_u (i32.add (local.get $r%d) (local.get $r%d)) (local.get $r%d)))",
                             a, b, a);
            wasm32_emit_text("(local.set $r%d (i32.add (local.get $r%d) (local.get $r%d)))",
                             r, a, b);
        } else if (op == TOK_ADDC2) {
            wasm32_emit_text("(local.set $r%d (i32.add (i32.add (local.get $r%d) (local.get $r%d)) (local.get $carry)))",
                             r, a, b);
        } else if (op == TOK_SUBC1) {
            wasm32_emit_text("(local.set $carry (i32.lt_u (local.get $r%d) (local.get $r%d)))",
                             a, b);
            wasm32_emit_text("(local.set $r%d (i32.sub (local.get $r%d) (local.get $r%d)))",
                             r, a, b);
        } else {
            wasm32_emit_text("(local.set $r%d (i32.sub (i32.sub (local.get $r%d) (local.get $r%d)) (local.get $carry)))",
                             r, a, b);
        }
        vtop->r = r;
        return;
    }

    if (op == TOK_UMULL) {
        int hi;
        gv2(RC_INT, RC_INT);
        a = vtop[-1].r & VT_VALMASK;
        b = vtop[0].r & VT_VALMASK;
        wasm32_materialize_reg_expr(a);
        wasm32_materialize_reg_expr(b);
        hi = get_reg(RC_INT);
        wasm32_forget_reg_expr(hi);
        vtop--;
        wasm32_emit_text("(local.set $r%d (i32.wrap_i64 (i64.shr_u (i64.mul (i64.extend_i32_u (local.get $r%d)) (i64.extend_i32_u (local.get $r%d))) (i64.const 32))))",
                         hi, a, b);
        wasm32_emit_text("(local.set $r%d (i32.wrap_i64 (i64.mul (i64.extend_i32_u (local.get $r%d)) (i64.extend_i32_u (local.get $r%d)))))",
                         a, a, b);
        vtop->r = a;
        vtop->r2 = hi;
        return;
    }

    gv2(RC_INT, RC_INT);
    a = vtop[-1].r & VT_VALMASK;
    b = vtop[0].r & VT_VALMASK;
    uns = vtop[-1].type.t & VT_UNSIGNED;
    vtop--;
    if (op >= TOK_ULT && op <= TOK_GT) {
        vset_VT_CMP(op);
        vtop->cmp_r = a | (b << 8);
        return;
    }
    r = a;
    {
        char *ae = wasm32_get_reg_expr(a);
        char *be = wasm32_get_reg_expr(b);
        wasm32_forget_reg_expr(a);
        wasm32_forget_reg_expr(b);
        wasm32_set_reg_expr_owned(r, wasm32_printf_expr("(%s %s %s)",
                                wasm32_iop(op, uns), ae, be));
        tcc_free(ae);
        tcc_free(be);
    }
    vtop->r = r;
}

ST_FUNC void gen_opf(int op)
{
    tcc_error("wasm32: floating point is not implemented yet");
}

ST_FUNC void gen_cvt_itof(int t)
{
    tcc_error("wasm32: floating point conversion is not implemented yet");
}

ST_FUNC void gen_cvt_ftoi(int t)
{
    tcc_error("wasm32: floating point conversion is not implemented yet");
}

ST_FUNC void gen_cvt_ftof(int t)
{
    tcc_error("wasm32: floating point conversion is not implemented yet");
}

ST_FUNC void gen_va_start(void)
{
    int r;

    if (!wasm32_cur_func || !wasm32_cur_func->is_variadic)
        tcc_error("wasm32: va_start in non-variadic function");
    vtop--;
    r = get_reg(RC_INT);
    wasm32_set_reg_expr_owned(r, wasm32_printf_expr("(local.get $p%d)",
                                                    wasm32_cur_func->va_param));
    vset(&char_pointer_type, r, 0);
}

ST_FUNC void ggoto(void)
{
    tcc_error("wasm32: computed goto is not implemented yet");
}

ST_FUNC void gen_vla_sp_save(int addr)
{
    tcc_error("wasm32: VLA is not implemented yet");
}

ST_FUNC void gen_vla_sp_restore(int addr)
{
    tcc_error("wasm32: VLA is not implemented yet");
}

ST_FUNC void gen_vla_alloc(CType *type, int align)
{
    tcc_error("wasm32: VLA is not implemented yet");
}

static int wasm32_block_id(int *pc_to_block, int local_pc)
{
    int id = pc_to_block[local_pc];
    if (id < 0)
        tcc_error("wasm32: branch target is not a basic block");
    return id;
}

static void wasm32_build_block_map(WasmFunc *fn, WasmBlockMap *map)
{
    int n = fn->nb_ops;
    int i;

    memset(map, 0, sizeof *map);
    map->start = tcc_mallocz(n + 1);
    map->pc_to_block = tcc_malloc((n + 1) * sizeof map->pc_to_block[0]);
    for (i = 0; i <= n; ++i)
        map->pc_to_block[i] = -1;
    map->start[0] = 1;
    for (i = 0; i < n; ++i) {
        WasmOp *op = &fn->ops[i];
        int target = -1;
        if (op->kind == WOP_JMP || op->kind == WOP_CJMP) {
            if (op->target < 0)
                tcc_error("wasm32: unresolved branch in '%s'", fn->name);
            target = op->target - fn->base_pc;
            if (target < 0 || target > n)
                tcc_error("wasm32: branch target out of range in '%s'", fn->name);
            map->start[target] = 1;
            if (i + 1 <= n)
                map->start[i + 1] = 1;
        } else if (op->kind == WOP_RET && i + 1 <= n) {
            map->start[i + 1] = 1;
        } else if (op->kind == WOP_SETJMP && i + 1 <= n) {
            map->start[i + 1] = 1;
        }
    }
    for (i = 0; i <= n; ++i) {
        if (map->start[i]) {
            map->starts = tcc_realloc(map->starts,
                                      (map->nb_starts + 1) * sizeof map->starts[0]);
            map->starts[map->nb_starts] = i;
            map->pc_to_block[i] = map->nb_starts++;
        }
    }
}

static void wasm32_free_block_map(WasmBlockMap *map)
{
    tcc_free(map->start);
    tcc_free(map->starts);
    tcc_free(map->pc_to_block);
}

static int wasm32_block_end(WasmFunc *fn, WasmBlockMap *map, int block)
{
    if (block + 1 < map->nb_starts)
        return map->starts[block + 1];
    return fn->nb_ops;
}

static void wasm32_emit_branch(FILE *f, int block)
{
    fprintf(f, "          (local.set $pc (i32.const %d))\n", block);
    fprintf(f, "          (br $dispatch)\n");
}

static void wasm32_emit_return_at(FILE *f, WasmFunc *fn, const char *indent)
{
    if (fn->frame_size)
        fprintf(f, "%s(global.set $__stack_pointer (local.get $fp))\n", indent);
    if (fn->has_result)
        fprintf(f, "%s(return (local.get $r%d))\n", indent, REG_IRET);
    else
        fprintf(f, "%s(return)\n", indent);
}

static void wasm32_emit_return_from_stack_at(FILE *f, WasmFunc *fn,
                                             const char *indent)
{
    if (fn->frame_size)
        fprintf(f, "%s(global.set $__stack_pointer (local.get $fp))\n", indent);
    fprintf(f, "%s(return)\n", indent);
}

static void wasm32_emit_return(FILE *f, WasmFunc *fn)
{
    wasm32_emit_return_at(f, fn, "          ");
}

static void wasm32_emit_func_signature(FILE *out, WasmFunc *fn)
{
    int i;

    fprintf(out, "  (func $%s", fn->name);
    if (fn->is_exported)
        fprintf(out, " (export \"%s\")", fn->name);
    for (i = 0; i < fn->nb_params; ++i)
        fprintf(out, " (param $p%d i32)", i);
    if (fn->has_result)
        fprintf(out, " (result i32)");
    fprintf(out, "\n");
}

static int wasm32_try_emit_longjmp_func(FILE *out, WasmFunc *fn)
{
    if (tcc_state->wasm_link_mode != WASM32_MODE_LIBC
        || !wasm32_is_longjmp_name(fn->name))
        return 0;
    wasm32_emit_func_signature(out, fn);
    fprintf(out, "    (if (i32.eqz (local.get $p1))\n");
    fprintf(out, "      (then\n");
    fprintf(out, "        (local.set $p1 (i32.const 1))\n");
    fprintf(out, "      )\n");
    fprintf(out, "    )\n");
    fprintf(out, "    (i32.store (i32.add (local.get $p0) (i32.const 8)) (local.get $p1))\n");
    fprintf(out, "    (throw $__wasm_longjmp_tag (local.get $p0))\n");
    fprintf(out, "    (unreachable)\n");
    fprintf(out, "  )\n");
    return 1;
}

static void wasm32_emit_sjlj_tag(TCCState *s1, FILE *f)
{
    if (s1->wasm_link_mode == WASM32_MODE_LIBC) {
        fprintf(f, "  (tag $__wasm_longjmp_tag (export \"__wasm_longjmp_tag\") (param i32))\n");
    } else if (s1->wasm_link_mode == WASM32_MODE_APP && wasm32_needs_sjlj) {
        fprintf(f, "  (import \"libc\" \"__wasm_longjmp_tag\" (tag $__wasm_longjmp_tag (param i32)))\n");
    } else if (wasm32_needs_sjlj) {
        fprintf(f, "  (tag $__wasm_longjmp_tag (param i32))\n");
    }
}

static void wasm32_scan_text_locals(const char *s, int *used_regs,
                                    int *use_carry)
{
    int r;

    if (!s)
        return;
    if (strstr(s, "$carry"))
        *use_carry = 1;
    for (r = 0; r < NB_REGS; ++r) {
        char name[8];
        snprintf(name, sizeof name, "$r%d", r);
        if (strstr(s, name))
            used_regs[r] = 1;
    }
}

static void wasm32_clear_used_locals(int *used_regs, int *use_carry)
{
    int r;

    for (r = 0; r < NB_REGS; ++r)
        used_regs[r] = 0;
    *use_carry = 0;
}

static void wasm32_scan_ops_locals(WasmFunc *fn, int from, int to,
                                   int *used_regs, int *use_carry)
{
    int i;

    for (i = from; i < to; ++i) {
        wasm32_scan_text_locals(fn->ops[i].text, used_regs, use_carry);
        wasm32_scan_text_locals(fn->ops[i].cond, used_regs, use_carry);
        wasm32_scan_text_locals(fn->ops[i].env, used_regs, use_carry);
    }
}

static void wasm32_scan_structured_locals(WasmFunc *fn, int *used_regs,
                                          int *use_carry)
{
    wasm32_clear_used_locals(used_regs, use_carry);
    if (fn->has_result)
        used_regs[REG_IRET] = 1;
    wasm32_scan_ops_locals(fn, 0, fn->nb_ops, used_regs, use_carry);
}

static void wasm32_emit_structured_header_with_locals(FILE *out, WasmFunc *fn,
                                                      int *used_regs,
                                                      int use_carry)
{
    int i;

    wasm32_emit_func_signature(out, fn);
    fprintf(out, "    (local $fp i32)\n");
    if (use_carry)
        fprintf(out, "    (local $carry i32)\n");
    for (i = 0; i < NB_REGS; ++i)
        if (used_regs[i])
            fprintf(out, "    (local $r%d i32)\n", i);
    fprintf(out, "    (local.set $fp (global.get $__stack_pointer))\n");
    if (fn->frame_size)
        fprintf(out, "    (global.set $__stack_pointer (i32.sub (local.get $fp) (i32.const %lu)))\n",
                fn->frame_size);
}

static void wasm32_emit_structured_header(FILE *out, WasmFunc *fn)
{
    int used_regs[NB_REGS];
    int use_carry;

    wasm32_scan_structured_locals(fn, used_regs, &use_carry);
    wasm32_emit_structured_header_with_locals(out, fn, used_regs, use_carry);
}

static int wasm32_are_text_ops(WasmFunc *fn, int from, int to)
{
    int i;

    for (i = from; i < to; ++i) {
        WasmOp *op = &fn->ops[i];
        if (op->kind != WOP_TEXT)
            return 0;
    }
    return 1;
}

static void wasm32_emit_text_ops(FILE *out, WasmFunc *fn, int from, int to,
                                 const char *indent)
{
    int i;

    for (i = from; i < to; ++i)
        fprintf(out, "%s%s\n", indent, fn->ops[i].text);
}

static char *wasm32_local_set_expr(WasmOp *op, int r)
{
    char prefix[32];
    const char *text;
    int prefix_len, text_len, expr_len;
    char *expr;

    if (op->kind != WOP_TEXT || !op->text)
        return NULL;
    snprintf(prefix, sizeof prefix, "(local.set $r%d ", r);
    text = op->text;
    prefix_len = strlen(prefix);
    text_len = strlen(text);
    if (strncmp(text, prefix, prefix_len) || text_len <= prefix_len + 1
        || text[text_len - 1] != ')')
        return NULL;
    expr_len = text_len - prefix_len - 1;
    expr = tcc_malloc(expr_len + 1);
    memcpy(expr, text + prefix_len, expr_len);
    expr[expr_len] = '\0';
    return expr;
}

static int wasm32_branch_body_info(WasmFunc *fn, WasmBlockMap *map, int block,
                                   int *body_from, int *body_to,
                                   int *target, int *ends_with_return)
{
    int from = map->starts[block];
    int to = wasm32_block_end(fn, map, block);
    WasmOp *last;
    int i;

    if (from >= to)
        return 0;
    last = &fn->ops[to - 1];
    *body_from = from;
    *body_to = to - 1;
    *target = -1;
    *ends_with_return = 0;
    for (i = from; i < to; ++i)
        if (fn->ops[i].kind != WOP_TEXT)
            break;
    if (i == to) {
        if (block + 1 >= map->nb_starts)
            return 0;
        *body_to = to;
        *target = block + 1;
        return 1;
    }
    for (i = from; i < to - 1; ++i)
        if (fn->ops[i].kind != WOP_TEXT)
            return 0;
    if (last->kind == WOP_JMP) {
        *target = wasm32_block_id(map->pc_to_block, last->target - fn->base_pc);
        return 1;
    }
    if (last->kind == WOP_RET) {
        *ends_with_return = 1;
        return 1;
    }
    return 0;
}

static int wasm32_join_is_return(WasmFunc *fn, WasmBlockMap *map, int block)
{
    int from = map->starts[block];
    int to = wasm32_block_end(fn, map, block);
    int i;

    if (from >= to)
        return 0;
    for (i = from; i < to - 1; ++i)
        if (fn->ops[i].kind != WOP_TEXT)
            return 0;
    return fn->ops[to - 1].kind == WOP_RET;
}

static int wasm32_other_blocks_are_empty(WasmFunc *fn, WasmBlockMap *map,
                                         int b0, int b1, int b2, int b3)
{
    int b;

    for (b = 0; b < map->nb_starts; ++b) {
        int from, to;
        if (b == b0 || b == b1 || b == b2 || b == b3)
            continue;
        from = map->starts[b];
        to = wasm32_block_end(fn, map, b);
        if (from != to)
            return 0;
    }
    return 1;
}

static int wasm32_try_emit_straight_func(FILE *out, WasmFunc *fn)
{
    int i;

    for (i = 0; i < fn->nb_ops; ++i) {
        WasmOp *op = &fn->ops[i];
        char *ret_expr = NULL;
        if (op->kind == WOP_TEXT)
            continue;
        if (op->kind != WOP_RET)
            return 0;
        if (i + 1 != fn->nb_ops)
            return 0;
        if (!wasm32_are_text_ops(fn, 0, i))
            return 0;
        if (fn->has_result && i > 0)
            ret_expr = wasm32_local_set_expr(&fn->ops[i - 1], REG_IRET);
        if (ret_expr) {
            int used_regs[NB_REGS];
            int use_carry;

            wasm32_clear_used_locals(used_regs, &use_carry);
            wasm32_scan_ops_locals(fn, 0, i - 1, used_regs, &use_carry);
            wasm32_scan_text_locals(ret_expr, used_regs, &use_carry);
            wasm32_emit_structured_header_with_locals(out, fn, used_regs,
                                                      use_carry);
            wasm32_emit_text_ops(out, fn, 0, i - 1, "    ");
            fprintf(out, "    %s\n", ret_expr);
            wasm32_emit_return_from_stack_at(out, fn, "    ");
            tcc_free(ret_expr);
        } else {
            wasm32_emit_structured_header(out, fn);
            wasm32_emit_text_ops(out, fn, 0, i, "    ");
            wasm32_emit_return_at(out, fn, "    ");
        }
        fprintf(out, "  )\n");
        return 1;
    }
    return 0;
}

static int wasm32_try_emit_simple_if_func(FILE *out, WasmFunc *fn)
{
    WasmBlockMap map;
    WasmOp *cjmp;
    int b0_from, b0_to, cidx;
    int true_block, false_block;
    int t_from, t_to, f_from, f_to;
    int t_target, f_target;
    int t_return, f_return;
    int join_block;
    int join_from, join_to;
    int ok = 0;

    wasm32_build_block_map(fn, &map);
    if (map.nb_starts < 4)
        goto done;
    b0_from = map.starts[0];
    b0_to = wasm32_block_end(fn, &map, 0);
    if (b0_from >= b0_to)
        goto done;
    cidx = b0_to - 1;
    cjmp = &fn->ops[cidx];
    if (cjmp->kind != WOP_CJMP || !cjmp->cond)
        goto done;
    true_block = wasm32_block_id(map.pc_to_block, cjmp->target - fn->base_pc);
    false_block = wasm32_block_id(map.pc_to_block, cidx + 1);
    if (true_block == false_block)
        goto done;
    if (!wasm32_branch_body_info(fn, &map, true_block, &t_from, &t_to,
                                 &t_target, &t_return))
        goto done;
    if (!wasm32_branch_body_info(fn, &map, false_block, &f_from, &f_to,
                                 &f_target, &f_return))
        goto done;
    if (t_return || f_return)
        goto done;
    if (t_target != f_target)
        goto done;
    join_block = t_target;
    if (!wasm32_join_is_return(fn, &map, join_block))
        goto done;
    join_from = map.starts[join_block];
    join_to = wasm32_block_end(fn, &map, join_block) - 1;
    if (!wasm32_other_blocks_are_empty(fn, &map, 0, true_block, false_block,
                                       join_block))
        goto done;
    if (!wasm32_are_text_ops(fn, b0_from, cidx)
        || !wasm32_are_text_ops(fn, t_from, t_to)
        || !wasm32_are_text_ops(fn, f_from, f_to)
        || !wasm32_are_text_ops(fn, map.starts[join_block],
                                wasm32_block_end(fn, &map, join_block) - 1))
        goto done;

    if (fn->has_result && join_from == join_to && t_to > t_from
        && f_to > f_from) {
        char *t_expr = wasm32_local_set_expr(&fn->ops[t_to - 1], REG_IRET);
        char *f_expr = wasm32_local_set_expr(&fn->ops[f_to - 1], REG_IRET);
        if (t_expr && f_expr) {
            int used_regs[NB_REGS];
            int use_carry;

            wasm32_clear_used_locals(used_regs, &use_carry);
            wasm32_scan_ops_locals(fn, b0_from, cidx, used_regs, &use_carry);
            wasm32_scan_text_locals(cjmp->cond, used_regs, &use_carry);
            wasm32_scan_ops_locals(fn, t_from, t_to - 1, used_regs, &use_carry);
            wasm32_scan_text_locals(t_expr, used_regs, &use_carry);
            wasm32_scan_ops_locals(fn, f_from, f_to - 1, used_regs, &use_carry);
            wasm32_scan_text_locals(f_expr, used_regs, &use_carry);
            wasm32_emit_structured_header_with_locals(out, fn, used_regs,
                                                      use_carry);
            wasm32_emit_text_ops(out, fn, b0_from, cidx, "    ");
            fprintf(out, "    (if (result i32) %s\n", cjmp->cond);
            fprintf(out, "      (then\n");
            wasm32_emit_text_ops(out, fn, t_from, t_to - 1, "        ");
            fprintf(out, "        %s\n", t_expr);
            fprintf(out, "      )\n");
            fprintf(out, "      (else\n");
            wasm32_emit_text_ops(out, fn, f_from, f_to - 1, "        ");
            fprintf(out, "        %s\n", f_expr);
            fprintf(out, "      )\n");
            fprintf(out, "    )\n");
            wasm32_emit_return_from_stack_at(out, fn, "    ");
            tcc_free(t_expr);
            tcc_free(f_expr);
            goto emitted;
        }
        tcc_free(t_expr);
        tcc_free(f_expr);
    }
    wasm32_emit_structured_header(out, fn);
    wasm32_emit_text_ops(out, fn, b0_from, cidx, "    ");
    fprintf(out, "    (if %s\n", cjmp->cond);
    fprintf(out, "      (then\n");
    wasm32_emit_text_ops(out, fn, t_from, t_to, "        ");
    fprintf(out, "      )\n");
    fprintf(out, "      (else\n");
    wasm32_emit_text_ops(out, fn, f_from, f_to, "        ");
    fprintf(out, "      )\n");
    fprintf(out, "    )\n");
    wasm32_emit_text_ops(out, fn, join_from, join_to, "    ");
    wasm32_emit_return_at(out, fn, "    ");
emitted:
    fprintf(out, "  )\n");
    ok = 1;
    goto done;

done:
    wasm32_free_block_map(&map);
    return ok;
}

static int wasm32_try_emit_structured_func(FILE *out, WasmFunc *fn)
{
    if (wasm32_try_emit_straight_func(out, fn))
        return 1;
    if (wasm32_try_emit_simple_if_func(out, fn))
        return 1;
    return 0;
}

static void wasm32_emit_func(FILE *out, WasmFunc *fn)
{
    WasmBlockMap map;
    int i, b;

    if (wasm32_try_emit_longjmp_func(out, fn))
        return;
    if (wasm32_try_emit_structured_func(out, fn))
        return;

    wasm32_build_block_map(fn, &map);
    wasm32_emit_func_signature(out, fn);
    fprintf(out, "    (local $pc i32)\n");
    fprintf(out, "    (local $fp i32)\n");
    fprintf(out, "    (local $carry i32)\n");
    if (fn->uses_sjlj)
        fprintf(out, "    (local $lj_env i32)\n");
    for (i = 0; i < NB_REGS; ++i)
        fprintf(out, "    (local $r%d i32)\n", i);
    fprintf(out, "    (local.set $fp (global.get $__stack_pointer))\n");
    if (fn->frame_size)
        fprintf(out, "    (global.set $__stack_pointer (i32.sub (local.get $fp) (i32.const %lu)))\n",
                fn->frame_size);
    fprintf(out, "    (local.set $pc (i32.const 0))\n");
    fprintf(out, "    (loop $dispatch\n");
    if (fn->uses_sjlj) {
        fprintf(out, "      (try\n");
        fprintf(out, "        (do\n");
    }

    for (b = 0; b < map.nb_starts; ++b) {
        int from = map.starts[b];
        int to = wasm32_block_end(fn, &map, b);
        int terminated = 0;
        fprintf(out, "      (if (i32.eq (local.get $pc) (i32.const %d))\n", b);
        fprintf(out, "        (then\n");
        for (i = from; i < to; ++i) {
            WasmOp *op = &fn->ops[i];
            if (op->kind == WOP_TEXT) {
                fprintf(out, "          %s\n", op->text);
            } else if (op->kind == WOP_JMP) {
                wasm32_emit_branch(out,
                    wasm32_block_id(map.pc_to_block, op->target - fn->base_pc));
                terminated = 1;
                break;
            } else if (op->kind == WOP_CJMP) {
                int true_block = wasm32_block_id(map.pc_to_block,
                    op->target - fn->base_pc);
                int false_block = wasm32_block_id(map.pc_to_block, i + 1);
                if (op->cond)
                    fprintf(out, "          (if %s\n", op->cond);
                else
                    fprintf(out, "          (if (%s (local.get $r%d) (local.get $r%d))\n",
                            wasm32_iop(op->cmp_op, 0), op->cmp_a, op->cmp_b);
                fprintf(out, "            (then\n");
                wasm32_emit_branch(out, true_block);
                fprintf(out, "            )\n");
                fprintf(out, "          )\n");
                wasm32_emit_branch(out, false_block);
                terminated = 1;
                break;
            } else if (op->kind == WOP_RET) {
                wasm32_emit_return(out, fn);
                terminated = 1;
                break;
            } else if (op->kind == WOP_SETJMP) {
                int resume_block = wasm32_block_id(map.pc_to_block, i + 1);
                fprintf(out, "          (local.set $r%d %s)\n",
                        REG_IRET, op->env);
                fprintf(out, "          (i32.store (local.get $r%d) (local.get $fp))\n",
                        REG_IRET);
                fprintf(out, "          (i32.store (i32.add (local.get $r%d) (i32.const 4)) (i32.const %d))\n",
                        REG_IRET, resume_block);
                fprintf(out, "          (i32.store (i32.add (local.get $r%d) (i32.const 8)) (i32.const 0))\n",
                        REG_IRET);
                fprintf(out, "          (local.set $r%d (i32.const 0))\n",
                        REG_IRET);
            }
        }
        if (!terminated) {
            if (b + 1 < map.nb_starts)
                wasm32_emit_branch(out, b + 1);
            else
                wasm32_emit_return(out, fn);
        }
        fprintf(out, "        )\n");
        fprintf(out, "      )\n");
    }

    fprintf(out, "      (unreachable)\n");
    if (fn->uses_sjlj) {
        fprintf(out, "        )\n");
        fprintf(out, "        (catch $__wasm_longjmp_tag\n");
        fprintf(out, "          (local.set $lj_env)\n");
        fprintf(out, "          (if (i32.eq (i32.load (local.get $lj_env)) (local.get $fp))\n");
        fprintf(out, "            (then\n");
        fprintf(out, "              (local.set $r%d (i32.load (i32.add (local.get $lj_env) (i32.const 8))))\n",
                REG_IRET);
        fprintf(out, "              (local.set $pc (i32.load (i32.add (local.get $lj_env) (i32.const 4))))\n");
        if (fn->frame_size)
            fprintf(out, "              (global.set $__stack_pointer (i32.sub (local.get $fp) (i32.const %lu)))\n",
                    fn->frame_size);
        else
            fprintf(out, "              (global.set $__stack_pointer (local.get $fp))\n");
        fprintf(out, "              (br $dispatch)\n");
        fprintf(out, "            )\n");
        fprintf(out, "          )\n");
        fprintf(out, "          (throw $__wasm_longjmp_tag (local.get $lj_env))\n");
        fprintf(out, "        )\n");
        fprintf(out, "      )\n");
    }
    fprintf(out, "    )\n");
    fprintf(out, "  )\n");

    wasm32_free_block_map(&map);
}

static unsigned long wasm32_align(unsigned long v, unsigned long a)
{
    if (a <= 1)
        return v;
    return (v + a - 1) & ~(a - 1);
}

static unsigned long wasm32_sec_size(Section *s)
{
    unsigned long size = s->sh_size;
    if (size < s->data_offset)
        size = s->data_offset;
    return size;
}

static int wasm32_is_memory_section(Section *s)
{
    if (!s)
        return 0;
    if (!(s->sh_flags & SHF_ALLOC))
        return 0;
    if (s->sh_flags & SHF_EXECINSTR)
        return 0;
    return s->sh_type == SHT_PROGBITS || s->sh_type == SHT_NOBITS;
}

static void wasm32_resolve_common_syms(TCCState *s1)
{
    ElfSym *sym;

    for_each_elem(symtab_section, 1, sym, ElfSym) {
        if (sym->st_shndx == SHN_COMMON) {
            sym->st_value = section_add(bss_section, sym->st_size,
                                        sym->st_value);
            sym->st_shndx = bss_section->sh_num;
        }
    }
}

static int wasm32_layout_memory(TCCState *s1)
{
    unsigned long addr;
    int i;

    if (s1->wasm_link_mode == WASM32_MODE_LIBC)
        addr = WASM32_LIBC_BASE;
    else if (s1->wasm_link_mode == WASM32_MODE_APP)
        addr = WASM32_APP_BASE;
    else
        addr = 1024;

    wasm32_resolve_common_syms(s1);

    for (i = 1; i < s1->nb_sections; ++i) {
        Section *s = s1->sections[i];
        unsigned long size;
        if (!wasm32_is_memory_section(s))
            continue;
        addr = wasm32_align(addr, s->sh_addralign);
        s->sh_addr = addr;
        size = wasm32_sec_size(s);
        s->sh_size = size;
        addr += size;
    }
    wasm32_memory_end = wasm32_align(addr, 16);
    if (s1->wasm_link_mode == WASM32_MODE_LIBC
        && wasm32_memory_end > WASM32_APP_BASE)
        tcc_error("wasm32: libc static data exceeds reserved 1MB region");
    return 0;
}

static addr_t wasm32_sym_addr(TCCState *s1, int sym_index)
{
    ElfSym *sym;
    ElfSym *other;
    const char *name;
    int func_index;
    int sh;

    if (!sym_index)
        return 0;
    func_index = wasm32_func_table_index_by_sym_index(sym_index);
    if (func_index)
        return func_index;
    sym = &((ElfSym *)symtab_section->data)[sym_index];
    if (sym->st_shndx == SHN_ABS)
        return sym->st_value;
    if (sym->st_shndx > 0 && sym->st_shndx < s1->nb_sections)
        return s1->sections[sym->st_shndx]->sh_addr + sym->st_value;
    name = (const char *)symtab_section->link->data + sym->st_name;
    if (name && name[0]) {
        for_each_elem(symtab_section, 1, other, ElfSym) {
            if (other == sym)
                continue;
            if (strcmp(name, (const char *)symtab_section->link->data
                       + other->st_name))
                continue;
            sh = other->st_shndx;
            if (sh > 0 && sh < s1->nb_sections
                && wasm32_is_memory_section(s1->sections[sh]))
                return s1->sections[sh]->sh_addr + other->st_value;
        }
    }
    return 0;
}

static void wasm32_relocate_data(TCCState *s1)
{
    int i;
    for (i = 1; i < s1->nb_sections; ++i) {
        Section *s = s1->sections[i];
        Section *reloc = s->reloc;
        ElfW_Rel *rel, *rel_end;
        if (!wasm32_is_memory_section(s) || !reloc)
            continue;
        rel = (ElfW_Rel *)reloc->data;
        rel_end = (ElfW_Rel *)(reloc->data + reloc->data_offset);
        for (; rel < rel_end; ++rel) {
            int type = ELFW(R_TYPE)(rel->r_info);
            int sym_index = ELFW(R_SYM)(rel->r_info);
            unsigned char *ptr = s->data + rel->r_offset;
            addr_t val = wasm32_sym_addr(s1, sym_index);
            if (type != R_DATA_32 && type != R_DATA_PTR)
                tcc_error("wasm32: unsupported data relocation type %d", type);
            add32le(ptr, val);
        }
    }
}

static void wasm32_emit_data_string(FILE *f, const unsigned char *data,
                                    unsigned long size)
{
    unsigned long i;
    fputc('"', f);
    for (i = 0; i < size; ++i) {
        unsigned char c = data[i];
        if (c == '"' || c == '\\')
            fprintf(f, "\\%c", c);
        else if (c >= 0x20 && c < 0x7f)
            fputc(c, f);
        else
            fprintf(f, "\\%02x", c);
    }
    fputc('"', f);
}

static void wasm32_emit_data_sections(TCCState *s1, FILE *f)
{
    int i;
    for (i = 1; i < s1->nb_sections; ++i) {
        Section *s = s1->sections[i];
        if (!wasm32_is_memory_section(s) || s->sh_type == SHT_NOBITS)
            continue;
        if (!s->data_offset)
            continue;
        fprintf(f, "  (data (i32.const %lu) ", (unsigned long)s->sh_addr);
        wasm32_emit_data_string(f, s->data, s->data_offset);
        fprintf(f, ")\n");
    }
}

static void wasm32_emit_addr_globals(TCCState *s1, FILE *f)
{
    ElfSym *sym;
    int i;

    for_each_elem(symtab_section, 1, sym, ElfSym) {
        addr_t addr;
        i = sym - (ElfSym *)symtab_section->data;
        if (sym->st_shndx <= 0 || sym->st_shndx >= s1->nb_sections)
            continue;
        if (!wasm32_is_memory_section(s1->sections[sym->st_shndx]))
            continue;
        addr = s1->sections[sym->st_shndx]->sh_addr + sym->st_value;
        fprintf(f, "  (global $__addr_%d i32 (i32.const %lu))\n",
                i, (unsigned long)addr);
    }
    for_each_elem(symtab_section, 1, sym, ElfSym) {
        ElfSym *other;
        const char *name;
        addr_t addr = 0;
        int found = 0;

        i = sym - (ElfSym *)symtab_section->data;
        if (sym->st_shndx > 0 && sym->st_shndx < s1->nb_sections
            && wasm32_is_memory_section(s1->sections[sym->st_shndx]))
            continue;
        name = (const char *)symtab_section->link->data + sym->st_name;
        if (!name || !name[0])
            continue;
        for_each_elem(symtab_section, 1, other, ElfSym) {
            int sh = other->st_shndx;
            if (other == sym)
                continue;
            if (strcmp(name, (const char *)symtab_section->link->data
                       + other->st_name))
                continue;
            if (sh <= 0 || sh >= s1->nb_sections)
                continue;
            if (!wasm32_is_memory_section(s1->sections[sh]))
                continue;
            addr = s1->sections[sh]->sh_addr + other->st_value;
            found = 1;
            break;
        }
        if (found)
            fprintf(f, "  (global $__addr_%d i32 (i32.const %lu))\n",
                    i, (unsigned long)addr);
    }
}

static int wasm32_func_table_index_by_sym_index(int sym_index)
{
    int i;
    int found = 0;
    for (i = 0; i < nb_wasm32_funcs; ++i)
        if (wasm32_funcs[i]->sym_index == sym_index)
            found = i + 1;
    return found;
}

static void wasm32_emit_func_index_globals(FILE *f)
{
    int i, j, duplicated_later;
    for (i = 0; i < nb_wasm32_funcs; ++i) {
        if (!wasm32_funcs[i]->sym_index)
            continue;
        duplicated_later = 0;
        for (j = i + 1; j < nb_wasm32_funcs; ++j) {
            if (wasm32_funcs[j]->sym_index == wasm32_funcs[i]->sym_index) {
                duplicated_later = 1;
                break;
            }
        }
        if (duplicated_later)
            continue;
        fprintf(f, "  (global $__funcidx_%d i32 (i32.const %d))\n",
                wasm32_funcs[i]->sym_index, i + 1);
    }
}

static void wasm32_emit_types(FILE *f)
{
    int i, j;
    for (i = 0; i < nb_wasm32_types; ++i) {
        fprintf(f, "  (type $ft%d (func", i);
        for (j = 0; j < wasm32_types[i]->nb_params; ++j)
            fprintf(f, " (param i32)");
        if (wasm32_types[i]->has_result)
            fprintf(f, " (result i32)");
        fprintf(f, "))\n");
    }
}

static void wasm32_emit_func_imports(TCCState *s1, FILE *f)
{
    int i, j;

    if (s1->wasm_link_mode != WASM32_MODE_APP
        && s1->wasm_link_mode != WASM32_MODE_LIBC)
        return;
    for (i = 0; i < nb_wasm32_imports; ++i) {
        WasmImport *wi = wasm32_imports[i];
        if (wasm32_has_func(wi->name))
            continue;
        fprintf(f, "  (import \"%s\" \"%s\" (func $%s",
                s1->wasm_link_mode == WASM32_MODE_LIBC ? "env" : "libc",
                wi->name, wi->name);
        for (j = 0; j < wi->nb_params; ++j)
            fprintf(f, " (param i32)");
        if (wi->has_result)
            fprintf(f, " (result i32)");
        fprintf(f, "))\n");
    }
}

static void wasm32_emit_table(FILE *f)
{
    int i;
    fprintf(f, "  (table $__indirect_function_table %d funcref)\n",
            nb_wasm32_funcs + 1);
    if (nb_wasm32_funcs) {
        fprintf(f, "  (elem (i32.const 1)");
        for (i = 0; i < nb_wasm32_funcs; ++i)
            fprintf(f, " $%s", wasm32_funcs[i]->name);
        fprintf(f, ")\n");
    }
}

static int wasm32_has_func(const char *name)
{
    int i;
    for (i = 0; i < nb_wasm32_funcs; ++i)
        if (!strcmp(wasm32_funcs[i]->name, name))
            return 1;
    return 0;
}

static void wasm32_emit_memory_builtins(FILE *f)
{
    if (!wasm32_has_func("memset")) {
        fprintf(f,
            "  (func $memset (param $dst i32) (param $val i32) (param $n i32) (result i32)\n"
            "    (local $p i32)\n"
            "    (local $end i32)\n"
            "    (local.set $p (local.get $dst))\n"
            "    (local.set $end (i32.add (local.get $dst) (local.get $n)))\n"
            "    (loop $loop\n"
            "      (if (i32.lt_u (local.get $p) (local.get $end))\n"
            "        (then\n"
            "          (i32.store8 (local.get $p) (local.get $val))\n"
            "          (local.set $p (i32.add (local.get $p) (i32.const 1)))\n"
            "          (br $loop)\n"
            "        )\n"
            "      )\n"
            "    )\n"
            "    (return (local.get $dst))\n"
            "  )\n");
    }
    if (!wasm32_has_func("memmove")) {
        fprintf(f,
            "  (func $memmove (param $dst i32) (param $src i32) (param $n i32) (result i32)\n"
            "    (local $d i32)\n"
            "    (local $s i32)\n"
            "    (local $end i32)\n"
            "    (if (i32.or (i32.le_u (local.get $dst) (local.get $src)) (i32.ge_u (local.get $dst) (i32.add (local.get $src) (local.get $n))))\n"
            "      (then\n"
            "        (local.set $d (local.get $dst))\n"
            "        (local.set $s (local.get $src))\n"
            "        (local.set $end (i32.add (local.get $dst) (local.get $n)))\n"
            "        (loop $forward\n"
            "          (if (i32.lt_u (local.get $d) (local.get $end))\n"
            "            (then\n"
            "              (i32.store8 (local.get $d) (i32.load8_u (local.get $s)))\n"
            "              (local.set $d (i32.add (local.get $d) (i32.const 1)))\n"
            "              (local.set $s (i32.add (local.get $s) (i32.const 1)))\n"
            "              (br $forward)\n"
            "            )\n"
            "          )\n"
            "        )\n"
            "      )\n"
            "      (else\n"
            "        (local.set $d (i32.add (local.get $dst) (local.get $n)))\n"
            "        (local.set $s (i32.add (local.get $src) (local.get $n)))\n"
            "        (loop $backward\n"
            "          (if (local.get $n)\n"
            "            (then\n"
            "              (local.set $d (i32.sub (local.get $d) (i32.const 1)))\n"
            "              (local.set $s (i32.sub (local.get $s) (i32.const 1)))\n"
            "              (i32.store8 (local.get $d) (i32.load8_u (local.get $s)))\n"
            "              (local.set $n (i32.sub (local.get $n) (i32.const 1)))\n"
            "              (br $backward)\n"
            "            )\n"
            "          )\n"
            "        )\n"
            "      )\n"
            "    )\n"
            "    (return (local.get $dst))\n"
            "  )\n");
    }
    if (!wasm32_has_func("memcpy")) {
        fprintf(f,
            "  (func $memcpy (param $dst i32) (param $src i32) (param $n i32) (result i32)\n"
            "    (return (call $memmove (local.get $dst) (local.get $src) (local.get $n)))\n"
            "  )\n");
    }
}

ST_FUNC int wasm32_output_module(FILE *f)
{
    int i;
    int pages;
    unsigned long heap_end;
    wasm32_layout_memory(tcc_state);
    wasm32_relocate_data(tcc_state);
    wasm32_stack_top = wasm32_align(wasm32_memory_end + WASM32_STACK_SIZE, 16);
    heap_end = tcc_state->wasm_heap_end ? tcc_state->wasm_heap_end
                                        : WASM32_SHARED_LIMIT;
    if (tcc_state->wasm_link_mode == WASM32_MODE_LIBC
        && wasm32_stack_top > WASM32_APP_BASE)
        tcc_error("wasm32: libc stack exceeds reserved 1MB region");
    if (tcc_state->wasm_link_mode == WASM32_MODE_APP
        && wasm32_stack_top >= heap_end)
        tcc_error("wasm32: app layout exceeds wasm heap end");
    if (tcc_state->wasm_link_mode) {
        pages = WASM32_SHARED_PAGES;
    } else {
        pages = (wasm32_stack_top + 0xffff) >> 16;
        if (pages < 1)
            pages = 1;
    }
    fprintf(f, "(module\n");
    if (tcc_state->wasm_link_mode)
        fprintf(f, "  (import \"env\" \"memory\" (memory $memory %d %d))\n",
                pages, pages);
    else
        fprintf(f, "  (memory $memory %d)\n", pages);
    wasm32_emit_func_imports(tcc_state, f);
    wasm32_emit_sjlj_tag(tcc_state, f);
    fprintf(f, "  (export \"memory\" (memory $memory))\n");
    fprintf(f, "  (global $__data_end (export \"__data_end\") i32 (i32.const %lu))\n",
            wasm32_memory_end);
    fprintf(f, "  (global $__heap_base (export \"__heap_base\") i32 (i32.const %lu))\n",
            wasm32_stack_top);
    if (tcc_state->wasm_link_mode == WASM32_MODE_APP)
        fprintf(f, "  (global $__heap_end (export \"__heap_end\") i32 (i32.const %lu))\n",
                heap_end);
    fprintf(f, "  (global $__stack_pointer (mut i32) (i32.const %lu))\n",
            wasm32_stack_top);
    wasm32_emit_types(f);
    wasm32_emit_table(f);
    wasm32_emit_func_index_globals(f);
    wasm32_emit_addr_globals(tcc_state, f);
    wasm32_emit_data_sections(tcc_state, f);
    wasm32_emit_memory_builtins(f);
    for (i = 0; i < nb_wasm32_funcs; ++i)
        wasm32_emit_func(f, wasm32_funcs[i]);
    fprintf(f, ")\n");
    return 0;
}

#endif /* !TARGET_DEFS_ONLY */
