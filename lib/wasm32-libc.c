/*
 * Small freestanding wasm32 runtime provider.
 *
 * This file is intentionally compiled by wasm32-tcc itself.  It is not a
 * complete hosted libc; it is the stable first provider module for TCC-emitted
 * wasm programs: memory allocation, byte/string primitives, and buffered stdio
 * hooks with no JavaScript runtime dependency.
 */

typedef unsigned int size_t;
typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) (void)(ap)
#define va_copy(dst, src) ((dst) = (src))

#define RT_STDIN 0
#define RT_STDOUT 1
#define RT_STDERR 2
#define RT_FILE_MEM 3

#define RT_ENOENT  (-2)
#define RT_EBADF  (-9)
#define RT_EINVAL (-22)
#define RT_ENOSPC (-28)
#define RT_ENOSYS (-52)

#define RT_STDIO_CAP 1048576u
#define RT_NULL (~0u)
#define RT_FMT_TO_BUFFER 0
#define RT_FMT_TO_STDOUT 1
#define RT_FMT_TO_STDERR 2

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define RAND_MAX 2147483647
#define CLOCKS_PER_SEC 1000

struct __wasm_FILE {
    int fd;
    int eof;
    int err;
    int has_ungot;
    unsigned char ungot;
    unsigned char *buf;
    size_t len;
    size_t pos;
    size_t cap;
    int writable;
    char *path;
};

typedef struct __wasm_FILE FILE;

struct RtBlock {
    size_t size;
    size_t next;
    int free;
};

struct RtFileData {
    char *path;
    unsigned char *data;
    size_t len;
    struct RtFileData *next;
};

static unsigned char *rt_stdin_buf;
static unsigned char *rt_stdout_buf;
static unsigned char *rt_stderr_buf;
static size_t rt_stdin_len;
static size_t rt_stdin_pos;
static size_t rt_stdout_used;
static size_t rt_stderr_used;

static size_t rt_heap_start;
static size_t rt_heap_end;
static size_t rt_heap_first;
static int rt_heap_ready;
static int rt_errno;
static int rt_stdio_hosted;
static unsigned rt_rand_state = 1;
static FILE rt_file_stdin = { RT_STDIN, 0, 0, 0, 0 };
static FILE rt_file_stdout = { RT_STDOUT, 0, 0, 0, 0 };
static FILE rt_file_stderr = { RT_STDERR, 0, 0, 0, 0 };
static struct RtFileData *rt_files;

int rt_write(int fd, const void *src, size_t len);
int rt_host_read(int fd, void *dst, size_t len);
int rt_host_write(int fd, const void *src, size_t len);
int rt_host_isatty(int fd);
void rt_host_exit(int code);
void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
int vsnprintf(char *dst, size_t n, const char *fmt, va_list ap);
int vsscanf(const char *src, const char *fmt, va_list ap);
double strtod(const char *s, char **endptr);

int *__errno_location(void)
{
    return &rt_errno;
}

static size_t rt_align8(size_t n)
{
    return (n + 7u) & ~7u;
}

static size_t rt_align8_down(size_t n)
{
    return n & ~7u;
}

static size_t rt_block_header_size(void)
{
    return rt_align8(sizeof(struct RtBlock));
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    size_t i;
    for (i = 0; i < n; ++i)
        d[i] = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    size_t i;
    for (i = 0; i < n; ++i)
        d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    size_t i;

    if (d == s || n == 0)
        return dst;
    if (d < s || d >= s + n) {
        for (i = 0; i < n; ++i)
            d[i] = s[i];
    } else {
        for (i = n; i != 0; --i)
            d[i - 1] = s[i - 1];
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a;
    const unsigned char *q = b;
    size_t i;
    for (i = 0; i < n; ++i) {
        if (p[i] != q[i])
            return (int)p[i] - (int)q[i];
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        ++n;
    return n;
}

char *strcpy(char *dst, const char *src)
{
    char *ret = dst;
    while ((*dst++ = *src++))
        ;
    return ret;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb || ca == 0)
            return (int)ca - (int)cb;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i] == (unsigned char)c)
            return (void *)(p + i);
    return 0;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i]; ++i)
        dst[i] = src[i];
    for (; i < n; ++i)
        dst[i] = 0;
    return dst;
}

char *strcat(char *dst, const char *src)
{
    strcpy(dst + strlen(dst), src);
    return dst;
}

char *strncat(char *dst, const char *src, size_t n)
{
    char *d = dst + strlen(dst);
    size_t i;
    for (i = 0; i < n && src[i]; ++i)
        d[i] = src[i];
    d[i] = 0;
    return dst;
}

int strcoll(const char *a, const char *b)
{
    return strcmp(a, b);
}

char *strchr(const char *s, int c)
{
    unsigned char ch = (unsigned char)c;
    for (;; ++s) {
        if ((unsigned char)*s == ch)
            return (char *)s;
        if (!*s)
            return 0;
    }
}

char *strrchr(const char *s, int c)
{
    const char *last = 0;
    unsigned char ch = (unsigned char)c;
    for (;; ++s) {
        if ((unsigned char)*s == ch)
            last = s;
        if (!*s)
            return (char *)last;
    }
}

char *strstr(const char *haystack, const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0)
        return (char *)haystack;
    while (*haystack) {
        if (*haystack == *needle && !strncmp(haystack, needle, nlen))
            return (char *)haystack;
        ++haystack;
    }
    return 0;
}

char *strdup(const char *s)
{
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (!copy)
        return 0;
    memcpy(copy, s, len + 1);
    return copy;
}

size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n])
        ++n;
    return n;
}

char *strpbrk(const char *s, const char *accept)
{
    for (; *s; ++s)
        if (strchr(accept, *s))
            return (char *)s;
    return 0;
}

size_t strspn(const char *s, const char *accept)
{
    size_t n = 0;
    while (s[n] && strchr(accept, s[n]))
        ++n;
    return n;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t n = 0;
    while (s[n] && !strchr(reject, s[n]))
        ++n;
    return n;
}

char *strtok_r(char *s, const char *delim, char **saveptr)
{
    char *end;

    if (!s)
        s = *saveptr;
    s += strspn(s, delim);
    if (!*s) {
        *saveptr = s;
        return 0;
    }
    end = s + strcspn(s, delim);
    if (*end) {
        *end++ = 0;
        *saveptr = end;
    } else {
        *saveptr = end;
    }
    return s;
}

char *strtok(char *s, const char *delim)
{
    static char *saveptr;
    return strtok_r(s, delim, &saveptr);
}

char *strerror(int errnum)
{
    switch (errnum) {
    case 0: return "success";
    case 2: return "no such file";
    case 5: return "i/o error";
    case 9: return "bad file descriptor";
    case 12: return "out of memory";
    case 22: return "invalid argument";
    case 28: return "no space";
    case 52: return "not implemented";
    default: return "error";
    }
}

static int rt_ascii(int c)
{
    return c & 255;
}

int isdigit(int c) { c = rt_ascii(c); return c >= '0' && c <= '9'; }
int islower(int c) { c = rt_ascii(c); return c >= 'a' && c <= 'z'; }
int isupper(int c) { c = rt_ascii(c); return c >= 'A' && c <= 'Z'; }
int isalpha(int c) { return islower(c) || isupper(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int iscntrl(int c) { c = rt_ascii(c); return c < 32 || c == 127; }
int isspace(int c) { c = rt_ascii(c); return c == ' ' || (c >= '\t' && c <= '\r'); }
int isxdigit(int c) { c = rt_ascii(c); return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int ispunct(int c) { c = rt_ascii(c); return c > 32 && c < 127 && !isalnum(c); }
int isblank(int c) { c = rt_ascii(c); return c == ' ' || c == '\t'; }
int isgraph(int c) { c = rt_ascii(c); return c > 32 && c < 127; }
int isprint(int c) { c = rt_ascii(c); return c >= 32 && c < 127; }
int tolower(int c) { return isupper(c) ? c + ('a' - 'A') : c; }
int toupper(int c) { return islower(c) ? c - ('a' - 'A') : c; }

static struct RtBlock *rt_block_at(size_t addr)
{
    return (struct RtBlock *)addr;
}

static void rt_heap_reset(void)
{
    rt_stdin_buf = 0;
    rt_stdout_buf = 0;
    rt_stderr_buf = 0;
    rt_stdin_len = 0;
    rt_stdin_pos = 0;
    rt_stdout_used = 0;
    rt_stderr_used = 0;
    rt_heap_start = 0;
    rt_heap_end = 0;
    rt_heap_first = 0;
    rt_heap_ready = 0;
}

static void rt_heap_split(size_t addr, size_t size)
{
    struct RtBlock *b = rt_block_at(addr);
    struct RtBlock *n;
    size_t hdr = rt_block_header_size();
    size_t n_addr;

    if (b->size < size + hdr + 8u)
        return;
    n_addr = addr + hdr + size;
    n = rt_block_at(n_addr);
    n->size = b->size - size - hdr;
    n->next = b->next;
    n->free = 1;
    b->size = size;
    b->next = n_addr;
}

static void rt_heap_coalesce(void)
{
    size_t addr = rt_heap_first;

    while (addr != RT_NULL) {
        struct RtBlock *b = rt_block_at(addr);
        if (b->free && b->next != RT_NULL) {
            struct RtBlock *n = rt_block_at(b->next);
            if (n->free) {
                b->size += rt_block_header_size() + n->size;
                b->next = n->next;
                continue;
            }
        }
        addr = b->next;
    }
}

void *malloc(size_t size)
{
    size_t addr;
    size_t hdr = rt_block_header_size();

    if (!rt_heap_ready)
        return 0;
    if (size == 0)
        size = 1;
    size = rt_align8(size);
    for (addr = rt_heap_first; addr != RT_NULL; addr = rt_block_at(addr)->next) {
        struct RtBlock *b = rt_block_at(addr);
        if (b->free && b->size >= size) {
            rt_heap_split(addr, size);
            b->free = 0;
            return (void *)(addr + hdr);
        }
    }
    return 0;
}

void free(void *ptr)
{
    size_t addr;
    struct RtBlock *b;

    if (!ptr || !rt_heap_ready)
        return;
    addr = (size_t)ptr;
    if (addr < rt_heap_start + rt_block_header_size() || addr >= rt_heap_end)
        return;
    addr -= rt_block_header_size();
    b = rt_block_at(addr);
    b->free = 1;
    rt_heap_coalesce();
}

void *calloc(size_t nmemb, size_t size)
{
    void *ptr;
    size_t total;

    if (size != 0 && nmemb > (~0u) / size)
        return 0;
    total = nmemb * size;
    ptr = malloc(total);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    size_t old_size;
    size_t addr;
    void *new_ptr;
    struct RtBlock *b;

    if (!ptr)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return 0;
    }
    if (!rt_heap_ready)
        return 0;

    addr = (size_t)ptr - rt_block_header_size();
    b = rt_block_at(addr);
    old_size = b->size;
    size = rt_align8(size);
    if (size <= old_size) {
        rt_heap_split(addr, size);
        return ptr;
    }

    new_ptr = malloc(size);
    if (!new_ptr)
        return 0;
    memcpy(new_ptr, ptr, old_size);
    free(ptr);
    return new_ptr;
}

int rt_init_heap(size_t start, size_t end)
{
    struct RtBlock *b;
    size_t hdr = rt_block_header_size();

    if (rt_heap_ready)
        return RT_EINVAL;
    start = rt_align8(start);
    end = rt_align8_down(end);
    if (end <= start + hdr + 3u * RT_STDIO_CAP)
        return RT_EINVAL;

    rt_heap_start = start;
    rt_heap_end = end;
    rt_heap_first = start;
    b = rt_block_at(rt_heap_first);
    b->size = end - start - hdr;
    b->next = RT_NULL;
    b->free = 1;
    rt_heap_ready = 1;

    rt_stdin_buf = malloc(RT_STDIO_CAP);
    rt_stdout_buf = malloc(RT_STDIO_CAP);
    rt_stderr_buf = malloc(RT_STDIO_CAP);
    if (!rt_stdin_buf || !rt_stdout_buf || !rt_stderr_buf) {
        rt_heap_reset();
        return RT_ENOSPC;
    }
    return 0;
}

int rt_heap_initialized(void)
{
    return rt_heap_ready;
}

int rt_stdio_capacity(void)
{
    return RT_STDIO_CAP;
}

static int rt_stdio_ready(void)
{
    return rt_stdin_buf && rt_stdout_buf && rt_stderr_buf;
}

void rt_stdio_reset(void)
{
    rt_stdin_len = 0;
    rt_stdin_pos = 0;
    rt_stdout_used = 0;
    rt_stderr_used = 0;
    rt_file_stdin.eof = 0;
    rt_file_stdin.err = 0;
    rt_file_stdin.has_ungot = 0;
    rt_file_stdout.err = 0;
    rt_file_stderr.err = 0;
}

void rt_stdio_set_hosted(int hosted)
{
    rt_stdio_hosted = hosted != 0;
}

int rt_stdio_hosted_enabled(void)
{
    return rt_stdio_hosted;
}

static void rt_stdin_compact(void)
{
    size_t remaining;

    if (!rt_stdio_ready())
        return;
    if (rt_stdin_pos == 0)
        return;
    if (rt_stdin_pos >= rt_stdin_len) {
        rt_stdin_len = 0;
        rt_stdin_pos = 0;
        return;
    }
    remaining = rt_stdin_len - rt_stdin_pos;
    memmove(rt_stdin_buf, rt_stdin_buf + rt_stdin_pos, remaining);
    rt_stdin_len = remaining;
    rt_stdin_pos = 0;
}

int rt_stdin_set(const void *src, size_t len)
{
    if (!rt_stdio_ready())
        return RT_EINVAL;
    if (!src && len)
        return RT_EINVAL;
    if (len > RT_STDIO_CAP)
        return RT_ENOSPC;
    if (len)
        memcpy(rt_stdin_buf, src, len);
    rt_stdin_len = len;
    rt_stdin_pos = 0;
    return (int)len;
}

int rt_stdin_append(const void *src, size_t len)
{
    if (!rt_stdio_ready())
        return RT_EINVAL;
    if (!src && len)
        return RT_EINVAL;
    rt_stdin_compact();
    if (len > RT_STDIO_CAP - rt_stdin_len)
        return RT_ENOSPC;
    if (len)
        memcpy(rt_stdin_buf + rt_stdin_len, src, len);
    rt_stdin_len += len;
    return (int)len;
}

int rt_stdin_remaining(void)
{
    if (!rt_stdio_ready())
        return 0;
    return (int)(rt_stdin_len - rt_stdin_pos);
}

int rt_stdout_ptr(void)
{
    return (int)rt_stdout_buf;
}

int rt_stdout_len(void)
{
    return (int)rt_stdout_used;
}

void rt_stdout_clear(void)
{
    rt_stdout_used = 0;
}

int rt_stderr_ptr(void)
{
    return (int)rt_stderr_buf;
}

int rt_stderr_len(void)
{
    return (int)rt_stderr_used;
}

void rt_stderr_clear(void)
{
    rt_stderr_used = 0;
}

static char *rt_strdup_n(const char *s, size_t len)
{
    char *p = malloc(len + 1);
    if (!p)
        return 0;
    if (len)
        memcpy(p, s, len);
    p[len] = 0;
    return p;
}

static struct RtFileData *rt_file_find(const char *path)
{
    struct RtFileData *it;

    if (!path)
        return 0;
    for (it = rt_files; it; it = it->next)
        if (strcmp(it->path, path) == 0)
            return it;
    return 0;
}

static int rt_file_store(const char *path, const void *src, size_t len)
{
    struct RtFileData *file;
    unsigned char *data;

    if (!path)
        return RT_EINVAL;
    file = rt_file_find(path);
    if (!file) {
        file = malloc(sizeof *file);
        if (!file)
            return RT_ENOSPC;
        memset(file, 0, sizeof *file);
        file->path = rt_strdup_n(path, strlen(path));
        if (!file->path) {
            free(file);
            return RT_ENOSPC;
        }
        file->next = rt_files;
        rt_files = file;
    }
    data = malloc(len ? len : 1);
    if (!data)
        return RT_ENOSPC;
    if (len)
        memcpy(data, src, len);
    free(file->data);
    file->data = data;
    file->len = len;
    return (int)len;
}

int rt_file_add(const char *path, const void *src, size_t len)
{
    return rt_file_store(path, src, len);
}

static int rt_file_append(FILE *f, const void *src, size_t len)
{
    unsigned char *p;
    size_t cap;

    if (!f || !f->writable)
        return RT_EBADF;
    if (!src && len)
        return RT_EINVAL;
    if (len > ((size_t)-1) - f->len)
        return RT_ENOSPC;
    if (f->len + len > f->cap) {
        cap = f->cap ? f->cap : 64;
        while (cap < f->len + len) {
            if (cap > ((size_t)-1) / 2) {
                cap = f->len + len;
                break;
            }
            cap *= 2;
        }
        p = realloc(f->buf, cap);
        if (!p)
            return RT_ENOSPC;
        f->buf = p;
        f->cap = cap;
    }
    if (len)
        memcpy(f->buf + f->len, src, len);
    f->len += len;
    f->pos = f->len;
    return (int)len;
}

static int rt_buffer_write(unsigned char *buf, size_t *buf_len,
                           const void *src, size_t len)
{
    size_t room;
    size_t n;

    if (!buf)
        return RT_EINVAL;
    if (!src && len)
        return RT_EINVAL;
    room = RT_STDIO_CAP - *buf_len;
    if (room == 0 && len)
        return RT_ENOSPC;
    n = len;
    if (n > room)
        n = room;
    if (n)
        memcpy(buf + *buf_len, src, n);
    *buf_len += n;
    return (int)n;
}

struct RtFmtOut {
    int mode;
    char *buf;
    size_t cap;
    size_t len;
    int error;
};

static void rt_fmt_putc(struct RtFmtOut *out, int ch)
{
    unsigned char c = (unsigned char)ch;

    if (out->error)
        return;
    if (out->mode == RT_FMT_TO_STDOUT) {
        if (rt_write(RT_STDOUT, &c, 1) != 1)
            out->error = 1;
    } else if (out->cap == RT_NULL) {
        out->buf[out->len] = (char)c;
    } else if (out->len + 1u < out->cap) {
        out->buf[out->len] = (char)c;
    }
    ++out->len;
}

static void rt_fmt_write(struct RtFmtOut *out, const char *s, size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i)
        rt_fmt_putc(out, s[i]);
}

static size_t rt_strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n])
        ++n;
    return n;
}

static int rt_is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static int rt_is_space(int c)
{
    return c == ' ' || c == '\n' || c == '\r' || c == '\t'
        || c == '\f' || c == '\v';
}

static int rt_digit_value(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    return -1;
}

struct RtScanIn {
    const char *buf;
    size_t pos;
    size_t start;
    size_t len;
    int bounded;
};

#define RT_SCAN_LEN_DEFAULT 0
#define RT_SCAN_LEN_HH 1
#define RT_SCAN_LEN_H 2
#define RT_SCAN_LEN_L 3
#define RT_SCAN_LEN_LL 4
#define RT_SCAN_LEN_INTMAX 5
#define RT_SCAN_LEN_SIZE 6
#define RT_SCAN_LEN_PTRDIFF 7

static int rt_scan_peek_at(struct RtScanIn *in, size_t off)
{
    size_t p = in->pos + off;
    if (in->bounded) {
        if (p >= in->len)
            return -1;
    } else if (in->buf[p] == 0) {
        return -1;
    }
    return (unsigned char)in->buf[p];
}

static int rt_scan_peek(struct RtScanIn *in)
{
    return rt_scan_peek_at(in, 0);
}

static int rt_scan_get(struct RtScanIn *in)
{
    int c = rt_scan_peek(in);
    if (c >= 0)
        ++in->pos;
    return c;
}

static void rt_scan_skip_space(struct RtScanIn *in)
{
    while (rt_is_space(rt_scan_peek(in)))
        ++in->pos;
}

static int rt_scan_finish(struct RtScanIn *in, int assigned)
{
    if (assigned == 0 && rt_scan_peek(in) < 0)
        return -1;
    return assigned;
}

static int rt_scan_parse_number(struct RtScanIn *in, int spec, int width,
                                unsigned long long *out, int *negative)
{
    size_t token_start = in->pos;
    int limit = width > 0 ? width : 0x7fffffff;
    int base;
    int digits = 0;
    unsigned long long value = 0;
    int c;
    int d;

    *negative = 0;
    if (limit <= 0)
        return 0;

    c = rt_scan_peek(in);
    if ((c == '-' || c == '+') && limit > 0) {
        *negative = c == '-';
        ++in->pos;
        --limit;
    }

    if (spec == 'o') {
        base = 8;
    } else if (spec == 'x' || spec == 'X' || spec == 'p') {
        base = 16;
        if (limit >= 3 && rt_scan_peek_at(in, 0) == '0'
            && (rt_scan_peek_at(in, 1) == 'x'
                || rt_scan_peek_at(in, 1) == 'X')
            && rt_digit_value(rt_scan_peek_at(in, 2)) >= 0
            && rt_digit_value(rt_scan_peek_at(in, 2)) < 16) {
            in->pos += 2;
            limit -= 2;
        }
    } else if (spec == 'i') {
        base = 10;
        if (limit >= 1 && rt_scan_peek_at(in, 0) == '0') {
            base = 8;
            if (limit >= 3 && (rt_scan_peek_at(in, 1) == 'x'
                               || rt_scan_peek_at(in, 1) == 'X')
                && rt_digit_value(rt_scan_peek_at(in, 2)) >= 0
                && rt_digit_value(rt_scan_peek_at(in, 2)) < 16) {
                base = 16;
                in->pos += 2;
                limit -= 2;
            }
        }
    } else {
        base = 10;
    }

    while (limit > 0) {
        c = rt_scan_peek(in);
        d = rt_digit_value(c);
        if (d < 0 || d >= base)
            break;
        value = value * (unsigned int)base + (unsigned int)d;
        ++in->pos;
        --limit;
        ++digits;
    }

    if (digits == 0) {
        in->pos = token_start;
        return 0;
    }
    *out = *negative ? 0ull - value : value;
    return 1;
}

static void rt_scan_store_int(void *dst, int length, unsigned long long value)
{
    if (length == RT_SCAN_LEN_HH) {
        *(unsigned char *)dst = (unsigned char)value;
    } else if (length == RT_SCAN_LEN_H) {
        *(unsigned short *)dst = (unsigned short)value;
    } else if (length == RT_SCAN_LEN_LL || length == RT_SCAN_LEN_INTMAX) {
        *(unsigned long long *)dst = value;
    } else if (length == RT_SCAN_LEN_SIZE) {
        *(size_t *)dst = (size_t)value;
    } else if (length == RT_SCAN_LEN_PTRDIFF) {
        *(int *)dst = (int)value;
    } else {
        *(unsigned int *)dst = value;
    }
}

static int rt_vscan(struct RtScanIn *in, const char *fmt, va_list ap)
{
    int assigned = 0;

    while (*fmt) {
        int suppress;
        int width;
        int length;
        int spec;

        if (rt_is_space(*fmt)) {
            while (rt_is_space(*fmt))
                ++fmt;
            rt_scan_skip_space(in);
            continue;
        }

        if (*fmt != '%') {
            if (rt_scan_peek(in) != (unsigned char)*fmt)
                return rt_scan_finish(in, assigned);
            ++in->pos;
            ++fmt;
            continue;
        }

        ++fmt;
        if (*fmt == '%') {
            if (rt_scan_peek(in) != '%')
                return rt_scan_finish(in, assigned);
            ++in->pos;
            ++fmt;
            continue;
        }

        suppress = 0;
        if (*fmt == '*') {
            suppress = 1;
            ++fmt;
        }

        width = 0;
        while (rt_is_digit(*fmt)) {
            width = width * 10 + *fmt - '0';
            ++fmt;
        }

        length = RT_SCAN_LEN_DEFAULT;
        if (*fmt == 'h') {
            ++fmt;
            if (*fmt == 'h') {
                length = RT_SCAN_LEN_HH;
                ++fmt;
            } else {
                length = RT_SCAN_LEN_H;
            }
        } else if (*fmt == 'l') {
            ++fmt;
            if (*fmt == 'l') {
                length = RT_SCAN_LEN_LL;
                ++fmt;
            } else {
                length = RT_SCAN_LEN_L;
            }
        } else if (*fmt == 'j') {
            length = RT_SCAN_LEN_INTMAX;
            ++fmt;
        } else if (*fmt == 'z') {
            length = RT_SCAN_LEN_SIZE;
            ++fmt;
        } else if (*fmt == 't') {
            length = RT_SCAN_LEN_PTRDIFF;
            ++fmt;
        } else {
            while (*fmt == 'L')
                ++fmt;
        }

        spec = *fmt ? *fmt++ : 0;
        if (spec != 'c' && spec != '[' && spec != 'n')
            rt_scan_skip_space(in);

        if (spec == 'd' || spec == 'i' || spec == 'u' || spec == 'x'
            || spec == 'X' || spec == 'o' || spec == 'p') {
            unsigned long long value;
            int negative;
            if (!rt_scan_parse_number(in, spec, width, &value, &negative))
                return rt_scan_finish(in, assigned);
            if (!suppress) {
                void *dst = va_arg(ap, void *);
                if (spec == 'p')
                    *(void **)dst = (void *)value;
                else
                    rt_scan_store_int(dst, length, value);
                ++assigned;
            }
            continue;
        }

        if (spec == 'f' || spec == 'F' || spec == 'e' || spec == 'E'
            || spec == 'g' || spec == 'G') {
            char token[128];
            char *end;
            double value;
            size_t token_start = in->pos;
            int limit = width > 0 ? width : (int)sizeof(token) - 1;
            int count = 0;
            int c;

            if (limit > (int)sizeof(token) - 1)
                limit = (int)sizeof(token) - 1;
            while (count < limit) {
                c = rt_scan_peek_at(in, (size_t)count);
                if (!(rt_is_digit(c) || c == '+' || c == '-'
                      || c == '.' || c == 'e' || c == 'E'))
                    break;
                token[count++] = (char)c;
            }
            token[count] = 0;
            value = strtod(token, &end);
            if (end == token) {
                in->pos = token_start;
                return rt_scan_finish(in, assigned);
            }
            in->pos = token_start + (size_t)(end - token);
            if (!suppress) {
                if (length == RT_SCAN_LEN_L) {
                    double *dst = va_arg(ap, double *);
                    *dst = value;
                } else {
                    float *dst = va_arg(ap, float *);
                    *dst = (float)value;
                }
                ++assigned;
            }
            continue;
        }

        if (spec == 's') {
            char *dst = suppress ? 0 : va_arg(ap, char *);
            int count = 0;
            int limit = width > 0 ? width : 0x7fffffff;
            while (limit > 0 && rt_scan_peek(in) >= 0
                   && !rt_is_space(rt_scan_peek(in))) {
                int c = rt_scan_get(in);
                if (!suppress)
                    dst[count] = (char)c;
                ++count;
                --limit;
            }
            if (count == 0)
                return rt_scan_finish(in, assigned);
            if (!suppress) {
                dst[count] = 0;
                ++assigned;
            }
            continue;
        }

        if (spec == 'c') {
            char *dst = suppress ? 0 : va_arg(ap, char *);
            int count = 0;
            int limit = width > 0 ? width : 1;
            while (count < limit) {
                int c = rt_scan_get(in);
                if (c < 0)
                    return rt_scan_finish(in, assigned);
                if (!suppress)
                    dst[count] = (char)c;
                ++count;
            }
            if (!suppress)
                ++assigned;
            continue;
        }

        if (spec == 'n') {
            if (!suppress) {
                void *dst = va_arg(ap, void *);
                rt_scan_store_int(dst, length,
                                  (unsigned int)(in->pos - in->start));
            }
            continue;
        }

        return rt_scan_finish(in, assigned);
    }
    return assigned;
}

static void rt_fmt_pad(struct RtFmtOut *out, int ch, int count)
{
    while (count-- > 0)
        rt_fmt_putc(out, ch);
}

static void rt_ull_split(unsigned long long value, unsigned int *lo,
                         unsigned int *hi)
{
    union {
        unsigned long long ull;
        unsigned int u32[2];
    } parts;

    parts.ull = value;
    *lo = parts.u32[0];
    *hi = parts.u32[1];
}

static int rt_u64_bit(unsigned int lo, unsigned int hi, int bit)
{
    if (bit < 32)
        return (int)((lo >> bit) & 1u);
    return (int)((hi >> (bit - 32)) & 1u);
}

static int rt_ull_to_decimal_digits(char *buf, unsigned long long value)
{
    unsigned int lo;
    unsigned int hi;
    int count = 1;
    int bit;
    int i;

    rt_ull_split(value, &lo, &hi);
    buf[0] = '0';
    for (bit = 63; bit >= 0; --bit) {
        int carry = rt_u64_bit(lo, hi, bit);
        for (i = 0; i < count; ++i) {
            int digit = (buf[i] - '0') * 2 + carry;
            if (digit >= 10) {
                buf[i] = (char)('0' + digit - 10);
                carry = 1;
            } else {
                buf[i] = (char)('0' + digit);
                carry = 0;
            }
        }
        if (carry)
            buf[count++] = '1';
    }
    while (count > 1 && buf[count - 1] == '0')
        --count;
    return count;
}

static int rt_ull_to_power2_digits(char *buf, unsigned long long value,
                                   int shift, const char *digits)
{
    unsigned int lo;
    unsigned int hi;
    int n = 0;
    int bit;

    rt_ull_split(value, &lo, &hi);
    for (bit = 0; bit < 64; bit += shift) {
        unsigned int digit = 0;
        int i;
        for (i = 0; i < shift && bit + i < 64; ++i)
            digit |= (unsigned int)rt_u64_bit(lo, hi, bit + i) << i;
        buf[n++] = digits[digit];
    }
    while (n > 1 && buf[n - 1] == '0')
        --n;
    return n;
}

static int rt_ull_to_digits(char *buf, unsigned long long value,
                            unsigned int base, int upper)
{
    static const char lower_digits[] = "0123456789abcdef";
    static const char upper_digits[] = "0123456789ABCDEF";
    const char *digits = upper ? upper_digits : lower_digits;

    if (base == 10)
        return rt_ull_to_decimal_digits(buf, value);
    if (base == 16)
        return rt_ull_to_power2_digits(buf, value, 4, digits);
    return rt_ull_to_power2_digits(buf, value, 3, digits);
}

static void rt_fmt_number(struct RtFmtOut *out, unsigned long long value,
                          unsigned int base, int negative, int upper,
                          int width, int precision, int left, int zero,
                          int plus, int space, int alt, int pointer)
{
    char digits[65];
    char prefix[3];
    int digit_count;
    int prefix_len = 0;
    int zero_count;
    int total;
    int i;

    prefix[0] = 0;
    if (negative) {
        prefix[prefix_len++] = '-';
    } else if (plus) {
        prefix[prefix_len++] = '+';
    } else if (space) {
        prefix[prefix_len++] = ' ';
    }

    if (pointer || (alt && value != 0 && (base == 16 || base == 8))) {
        if (base == 16) {
            prefix[prefix_len++] = '0';
            prefix[prefix_len++] = upper ? 'X' : 'x';
        } else if (base == 8) {
            prefix[prefix_len++] = '0';
        }
    }

    digit_count = 0;
    if (!(precision == 0 && value == 0))
        digit_count = rt_ull_to_digits(digits, value, base, upper);

    zero_count = 0;
    if (precision > digit_count)
        zero_count = precision - digit_count;
    total = prefix_len + zero_count + digit_count;

    if (!left && !(zero && precision < 0))
        rt_fmt_pad(out, ' ', width - total);
    rt_fmt_write(out, prefix, (size_t)prefix_len);
    if (!left && zero && precision < 0)
        rt_fmt_pad(out, '0', width - total);
    rt_fmt_pad(out, '0', zero_count);
    for (i = digit_count - 1; i >= 0; --i)
        rt_fmt_putc(out, digits[i]);
    if (left)
        rt_fmt_pad(out, ' ', width - total);
}

static int rt_append_char(char *buf, int pos, int cap, int ch)
{
    if (pos + 1 < cap)
        buf[pos] = (char)ch;
    return pos + 1;
}

static int rt_append_str(char *buf, int pos, int cap, const char *s)
{
    while (*s)
        pos = rt_append_char(buf, pos, cap, *s++);
    return pos;
}

static int rt_append_uint_dec(char *buf, int pos, int cap, unsigned int v)
{
    char tmp[16];
    int n = 0;
    int i;

    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    for (i = n - 1; i >= 0; --i)
        pos = rt_append_char(buf, pos, cap, tmp[i]);
    return pos;
}

static double rt_pow10_int(int n)
{
    double r = 1.0;
    int i;

    if (n >= 0) {
        for (i = 0; i < n; ++i)
            r *= 10.0;
    } else {
        for (i = 0; i < -n; ++i)
            r /= 10.0;
    }
    return r;
}

static int rt_decimal_exp(double x)
{
    int e = 0;

    if (x < 0)
        x = -x;
    if (x == 0.0)
        return 0;
    while (x >= 10.0) {
        x /= 10.0;
        ++e;
    }
    while (x < 1.0) {
        x *= 10.0;
        --e;
    }
    return e;
}

static int rt_append_fixed_abs(char *buf, int pos, int cap, double x,
                               int precision, int trim)
{
    unsigned int whole;
    double frac;
    double round = 0.5;
    int dot_pos;
    int end_pos;
    int i;

    if (precision < 0)
        precision = 6;
    if (precision > 60)
        precision = 60;
    for (i = 0; i < precision; ++i)
        round /= 10.0;
    x += round;
    whole = (unsigned int)x;
    frac = x - (double)whole;
    pos = rt_append_uint_dec(buf, pos, cap, whole);
    dot_pos = pos;
    if (precision > 0)
        pos = rt_append_char(buf, pos, cap, '.');
    for (i = 0; i < precision; ++i) {
        int digit;
        frac *= 10.0;
        digit = (int)frac;
        pos = rt_append_char(buf, pos, cap, '0' + digit);
        frac -= (double)digit;
    }
    end_pos = pos;
    if (trim) {
        while (end_pos > dot_pos && buf[end_pos - 1] == '0')
            --end_pos;
        if (end_pos > dot_pos && buf[end_pos - 1] == '.')
            --end_pos;
        pos = end_pos;
    }
    return pos;
}

static int rt_append_exp_abs(char *buf, int pos, int cap, double x,
                             int precision, int upper, int trim)
{
    int exp = rt_decimal_exp(x);
    int i;

    if (precision < 0)
        precision = 6;
    if (precision > 60)
        precision = 60;
    x /= rt_pow10_int(exp);
    x += 0.5 * rt_pow10_int(-precision);
    if (x >= 10.0) {
        x /= 10.0;
        ++exp;
    }
    pos = rt_append_fixed_abs(buf, pos, cap, x, precision, trim);
    pos = rt_append_char(buf, pos, cap, upper ? 'E' : 'e');
    pos = rt_append_char(buf, pos, cap, exp < 0 ? '-' : '+');
    if (exp < 0)
        exp = -exp;
    if (exp < 10)
        pos = rt_append_char(buf, pos, cap, '0');
    if (exp < 100) {
        pos = rt_append_uint_dec(buf, pos, cap, (unsigned int)exp);
    } else {
        for (i = 1000; i > 1 && exp < i; i /= 10)
            ;
        pos = rt_append_uint_dec(buf, pos, cap, (unsigned int)exp);
    }
    return pos;
}

static void rt_fmt_double(struct RtFmtOut *out, double value, int spec,
                          int width, int precision, int left, int zero,
                          int plus, int space, int alt)
{
    char buf[160];
    int pos = 0;
    int negative = value < 0.0;
    int trim = !alt;
    int upper = spec == 'E' || spec == 'G';
    int i;

    if (value != value) {
        pos = rt_append_str(buf, pos, sizeof buf, upper ? "NAN" : "nan");
    } else {
        if (negative)
            value = -value;
        if (negative)
            pos = rt_append_char(buf, pos, sizeof buf, '-');
        else if (plus)
            pos = rt_append_char(buf, pos, sizeof buf, '+');
        else if (space)
            pos = rt_append_char(buf, pos, sizeof buf, ' ');

        if (value > 1.0e308) {
            pos = rt_append_str(buf, pos, sizeof buf, upper ? "INF" : "inf");
        } else if (spec == 'f' || spec == 'F') {
            pos = rt_append_fixed_abs(buf, pos, sizeof buf, value,
                                      precision < 0 ? 6 : precision, 0);
        } else if (spec == 'e' || spec == 'E') {
            pos = rt_append_exp_abs(buf, pos, sizeof buf, value,
                                    precision < 0 ? 6 : precision, upper, 0);
        } else {
            int prec = precision < 0 ? 6 : precision;
            int exp;
            if (prec == 0)
                prec = 1;
            exp = rt_decimal_exp(value);
            if (exp < -4 || exp >= prec) {
                pos = rt_append_exp_abs(buf, pos, sizeof buf, value,
                                        prec - 1, upper, trim);
            } else {
                int frac = prec - (exp + 1);
                if (frac < 0)
                    frac = 0;
                pos = rt_append_fixed_abs(buf, pos, sizeof buf, value,
                                          frac, trim);
            }
        }
    }

    if (pos >= (int)sizeof buf)
        pos = (int)sizeof buf - 1;
    buf[pos] = 0;
    if (!left)
        rt_fmt_pad(out, zero ? '0' : ' ', width - pos);
    for (i = 0; i < pos; ++i)
        rt_fmt_putc(out, buf[i]);
    if (left)
        rt_fmt_pad(out, ' ', width - pos);
}

static int rt_vformat(struct RtFmtOut *out, const char *fmt, va_list ap)
{
    while (*fmt) {
        int left;
        int plus;
        int space;
        int alt;
        int zero;
        int width;
        int precision;
        int length;
        int done;
        int spec;

        if (*fmt != '%') {
            rt_fmt_putc(out, *fmt++);
            continue;
        }
        ++fmt;
        if (*fmt == '%') {
            rt_fmt_putc(out, *fmt++);
            continue;
        }

        left = plus = space = alt = zero = 0;
        done = 0;
        while (!done) {
            if (*fmt == '-') {
                left = 1;
                ++fmt;
            } else if (*fmt == '+') {
                plus = 1;
                ++fmt;
            } else if (*fmt == ' ') {
                space = 1;
                ++fmt;
            } else if (*fmt == '#') {
                alt = 1;
                ++fmt;
            } else if (*fmt == '0') {
                zero = 1;
                ++fmt;
            } else {
                done = 1;
            }
        }
        if (left)
            zero = 0;

        width = 0;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left = 1;
                zero = 0;
                width = -width;
            }
            ++fmt;
        } else {
            while (rt_is_digit(*fmt)) {
                width = width * 10 + *fmt - '0';
                ++fmt;
            }
        }

        precision = -1;
        if (*fmt == '.') {
            ++fmt;
            precision = 0;
            if (*fmt == '*') {
                precision = va_arg(ap, int);
                if (precision < 0)
                    precision = -1;
                ++fmt;
            } else {
                while (rt_is_digit(*fmt)) {
                    precision = precision * 10 + *fmt - '0';
                    ++fmt;
                }
            }
        }

        length = RT_SCAN_LEN_DEFAULT;
        if (*fmt == 'h') {
            ++fmt;
            if (*fmt == 'h') {
                length = RT_SCAN_LEN_HH;
                ++fmt;
            } else {
                length = RT_SCAN_LEN_H;
            }
        } else if (*fmt == 'l') {
            ++fmt;
            if (*fmt == 'l') {
                length = RT_SCAN_LEN_LL;
                ++fmt;
            } else {
                length = RT_SCAN_LEN_L;
            }
        } else if (*fmt == 'j') {
            length = RT_SCAN_LEN_INTMAX;
            ++fmt;
        } else if (*fmt == 'z') {
            length = RT_SCAN_LEN_SIZE;
            ++fmt;
        } else if (*fmt == 't') {
            length = RT_SCAN_LEN_PTRDIFF;
            ++fmt;
        } else if (*fmt == 'L') {
            ++fmt;
        }

        spec = *fmt ? *fmt++ : 0;
        switch (spec) {
        case 'd':
        case 'i': {
            if (length == RT_SCAN_LEN_LL || length == RT_SCAN_LEN_INTMAX) {
                long long v = va_arg(ap, long long);
                unsigned long long mag =
                    v < 0 ? 0ull - (unsigned long long)v
                          : (unsigned long long)v;
                rt_fmt_number(out, mag, 10, v < 0, 0, width, precision, left,
                              zero, plus, space, 0, 0);
            } else if (length == RT_SCAN_LEN_L || length == RT_SCAN_LEN_PTRDIFF) {
                long v = va_arg(ap, long);
                unsigned long mag =
                    v < 0 ? 0ul - (unsigned long)v : (unsigned long)v;
                rt_fmt_number(out, mag, 10, v < 0, 0, width, precision, left,
                              zero, plus, space, 0, 0);
            } else {
                int v = va_arg(ap, int);
                unsigned int mag =
                    v < 0 ? 0u - (unsigned int)v : (unsigned int)v;
                rt_fmt_number(out, mag, 10, v < 0, 0, width, precision, left,
                              zero, plus, space, 0, 0);
            }
            break;
        }
        case 'u': {
            if (length == RT_SCAN_LEN_LL || length == RT_SCAN_LEN_INTMAX) {
                unsigned long long v = va_arg(ap, unsigned long long);
                rt_fmt_number(out, v, 10, 0, 0, width, precision, left, zero,
                              0, 0, 0, 0);
            } else if (length == RT_SCAN_LEN_L || length == RT_SCAN_LEN_SIZE
                       || length == RT_SCAN_LEN_PTRDIFF) {
                unsigned long v = va_arg(ap, unsigned long);
                rt_fmt_number(out, v, 10, 0, 0, width, precision, left, zero,
                              0, 0, 0, 0);
            } else {
                unsigned int v = va_arg(ap, unsigned int);
                rt_fmt_number(out, v, 10, 0, 0, width, precision, left, zero,
                              0, 0, 0, 0);
            }
            break;
        }
        case 'x': {
            if (length == RT_SCAN_LEN_LL || length == RT_SCAN_LEN_INTMAX) {
                unsigned long long v = va_arg(ap, unsigned long long);
                rt_fmt_number(out, v, 16, 0, 0, width, precision, left, zero,
                              0, 0, alt, 0);
            } else if (length == RT_SCAN_LEN_L || length == RT_SCAN_LEN_SIZE
                       || length == RT_SCAN_LEN_PTRDIFF) {
                unsigned long v = va_arg(ap, unsigned long);
                rt_fmt_number(out, v, 16, 0, 0, width, precision, left, zero,
                              0, 0, alt, 0);
            } else {
                unsigned int v = va_arg(ap, unsigned int);
                rt_fmt_number(out, v, 16, 0, 0, width, precision, left, zero,
                              0, 0, alt, 0);
            }
            break;
        }
        case 'X': {
            if (length == RT_SCAN_LEN_LL || length == RT_SCAN_LEN_INTMAX) {
                unsigned long long v = va_arg(ap, unsigned long long);
                rt_fmt_number(out, v, 16, 0, 1, width, precision, left, zero,
                              0, 0, alt, 0);
            } else if (length == RT_SCAN_LEN_L || length == RT_SCAN_LEN_SIZE
                       || length == RT_SCAN_LEN_PTRDIFF) {
                unsigned long v = va_arg(ap, unsigned long);
                rt_fmt_number(out, v, 16, 0, 1, width, precision, left, zero,
                              0, 0, alt, 0);
            } else {
                unsigned int v = va_arg(ap, unsigned int);
                rt_fmt_number(out, v, 16, 0, 1, width, precision, left, zero,
                              0, 0, alt, 0);
            }
            break;
        }
        case 'o': {
            if (length == RT_SCAN_LEN_LL || length == RT_SCAN_LEN_INTMAX) {
                unsigned long long v = va_arg(ap, unsigned long long);
                rt_fmt_number(out, v, 8, 0, 0, width, precision, left, zero,
                              0, 0, alt, 0);
            } else if (length == RT_SCAN_LEN_L || length == RT_SCAN_LEN_SIZE
                       || length == RT_SCAN_LEN_PTRDIFF) {
                unsigned long v = va_arg(ap, unsigned long);
                rt_fmt_number(out, v, 8, 0, 0, width, precision, left, zero,
                              0, 0, alt, 0);
            } else {
                unsigned int v = va_arg(ap, unsigned int);
                rt_fmt_number(out, v, 8, 0, 0, width, precision, left, zero,
                              0, 0, alt, 0);
            }
            break;
        }
        case 'p': {
            unsigned int v = va_arg(ap, unsigned int);
            int pointer_precision = precision < 0 ? 1 : precision;
            rt_fmt_number(out, v, 16, 0, 0,
                          width, pointer_precision, left, zero,
                          0, 0, v != 0, v == 0);
            break;
        }
        case 'c': {
            int ch = va_arg(ap, int);
            if (!left)
                rt_fmt_pad(out, ' ', width - 1);
            rt_fmt_putc(out, ch);
            if (left)
                rt_fmt_pad(out, ' ', width - 1);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            size_t len;
            if (!s)
                s = "(null)";
            len = precision >= 0 ? rt_strnlen(s, (size_t)precision) : strlen(s);
            if (!left)
                rt_fmt_pad(out, ' ', width - (int)len);
            rt_fmt_write(out, s, len);
            if (left)
                rt_fmt_pad(out, ' ', width - (int)len);
            break;
        }
        case 'f':
        case 'F': {
            double v = va_arg(ap, double);
            rt_fmt_double(out, v, spec, width, precision, left, zero,
                          plus, space, alt);
            break;
        }
        case 'e':
        case 'E':
        case 'g':
        case 'G': {
            double v = va_arg(ap, double);
            rt_fmt_double(out, v, spec, width, precision, left, zero,
                          plus, space, alt);
            break;
        }
        case 0:
            --fmt;
            break;
        default:
            rt_fmt_putc(out, '%');
            rt_fmt_putc(out, spec);
            break;
        }
    }
    return out->error ? -1 : (int)out->len;
}

int rt_read(int fd, void *dst, size_t len)
{
    size_t avail;
    size_t n;

    if (rt_stdio_hosted)
        return rt_host_read(fd, dst, len);
    if (!rt_stdio_ready())
        return RT_EINVAL;
    if (fd != RT_STDIN)
        return RT_EBADF;
    if (!dst && len)
        return RT_EINVAL;
    avail = rt_stdin_len - rt_stdin_pos;
    n = len;
    if (n > avail)
        n = avail;
    if (n)
        memcpy(dst, rt_stdin_buf + rt_stdin_pos, n);
    rt_stdin_pos += n;
    return (int)n;
}

int rt_write(int fd, const void *src, size_t len)
{
    if (rt_stdio_hosted)
        return rt_host_write(fd, src, len);
    if (fd == RT_STDOUT)
        return rt_buffer_write(rt_stdout_buf, &rt_stdout_used, src, len);
    if (fd == RT_STDERR)
        return rt_buffer_write(rt_stderr_buf, &rt_stderr_used, src, len);
    return RT_EBADF;
}

int read(int fd, void *dst, size_t len)
{
    return rt_read(fd, dst, len);
}

int write(int fd, const void *src, size_t len)
{
    return rt_write(fd, src, len);
}

int rt_getchar(void)
{
    unsigned char ch;
    if (rt_read(RT_STDIN, &ch, 1) != 1)
        return -1;
    return (int)ch;
}

int getchar(void)
{
    return rt_getchar();
}

int rt_putchar(int ch)
{
    unsigned char c = (unsigned char)ch;
    if (rt_write(RT_STDOUT, &c, 1) != 1)
        return -1;
    return (int)c;
}

int putchar(int ch)
{
    return rt_putchar(ch);
}

int rt_puts(const char *s)
{
    unsigned char nl = '\n';
    size_t len;
    int ret;

    if (!s)
        return RT_EINVAL;
    len = strlen(s);
    ret = rt_write(RT_STDOUT, s, len);
    if (ret < 0)
        return ret;
    if ((size_t)ret != len)
        return RT_ENOSPC;
    ret = rt_write(RT_STDOUT, &nl, 1);
    if (ret != 1)
        return ret < 0 ? ret : RT_ENOSPC;
    return (int)(len + 1);
}

int puts(const char *s)
{
    return rt_puts(s);
}

FILE *__wasm_stdin(void)
{
    return &rt_file_stdin;
}

FILE *__wasm_stdout(void)
{
    return &rt_file_stdout;
}

FILE *__wasm_stderr(void)
{
    return &rt_file_stderr;
}

int fgetc(FILE *f)
{
    unsigned char ch;
    int r;

    if (!f) {
        rt_errno = RT_EBADF;
        return EOF;
    }
    if (f->has_ungot) {
        f->has_ungot = 0;
        return f->ungot;
    }
    if (f->fd == RT_FILE_MEM) {
        if (f->pos >= f->len) {
            f->eof = 1;
            return EOF;
        }
        f->eof = 0;
        return f->buf[f->pos++];
    }
    r = rt_read(f->fd, &ch, 1);
    if (r == 1) {
        f->eof = 0;
        return ch;
    }
    if (r == 0)
        f->eof = 1;
    else {
        f->err = 1;
        rt_errno = -r;
    }
    return EOF;
}

int fputc(int ch, FILE *f)
{
    unsigned char c = (unsigned char)ch;
    int r;

    if (!f) {
        rt_errno = RT_EBADF;
        return EOF;
    }
    if (f->fd == RT_FILE_MEM) {
        r = rt_file_append(f, &c, 1);
        if (r == 1)
            return c;
        f->err = 1;
        if (r < 0)
            rt_errno = -r;
        return EOF;
    }
    r = rt_write(f->fd, &c, 1);
    if (r == 1)
        return c;
    f->err = 1;
    if (r < 0)
        rt_errno = -r;
    return EOF;
}

int ungetc(int ch, FILE *f)
{
    if (!f || ch == EOF)
        return EOF;
    f->ungot = (unsigned char)ch;
    f->has_ungot = 1;
    f->eof = 0;
    return (unsigned char)ch;
}

char *fgets(char *s, int n, FILE *f)
{
    int i;
    int c;

    if (!s || n <= 0 || !f)
        return 0;
    for (i = 0; i < n - 1; ++i) {
        c = fgetc(f);
        if (c == EOF)
            break;
        s[i] = (char)c;
        if (c == '\n') {
            ++i;
            break;
        }
    }
    if (i == 0)
        return 0;
    s[i] = 0;
    return s;
}

int fputs(const char *s, FILE *f)
{
    size_t len;
    int r;

    if (!s || !f)
        return EOF;
    len = strlen(s);
    r = rt_write(f->fd, s, len);
    if (r < 0 || (size_t)r != len) {
        f->err = 1;
        if (r < 0)
            rt_errno = -r;
        return EOF;
    }
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f)
{
    unsigned char *p = ptr;
    size_t total = size * nmemb;
    size_t got = 0;
    int c;

    if (size == 0 || nmemb == 0)
        return 0;
    while (got < total) {
        c = fgetc(f);
        if (c == EOF)
            break;
        p[got++] = (unsigned char)c;
    }
    return got / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f)
{
    size_t total = size * nmemb;
    int r;

    if (size == 0 || nmemb == 0)
        return 0;
    if (!f)
        return 0;
    if (f->fd == RT_FILE_MEM) {
        r = rt_file_append(f, ptr, total);
        if (r < 0) {
            f->err = 1;
            rt_errno = -r;
            return 0;
        }
        return ((size_t)r) / size;
    }
    r = rt_write(f->fd, ptr, total);
    if (r < 0) {
        f->err = 1;
        rt_errno = -r;
        return 0;
    }
    return ((size_t)r) / size;
}

int fflush(FILE *f)
{
    (void)f;
    return 0;
}

int fclose(FILE *f)
{
    int r = 0;

    if (!f)
        return EOF;
    if (f == &rt_file_stdin || f == &rt_file_stdout || f == &rt_file_stderr)
        return 0;
    if (f->fd == RT_FILE_MEM && f->writable && f->path) {
        r = rt_file_store(f->path, f->buf, f->len);
        if (r < 0) {
            f->err = 1;
            rt_errno = -r;
        }
    }
    if (f->writable)
        free(f->buf);
    free(f->path);
    free(f);
    return r < 0 ? EOF : 0;
}

FILE *fopen(const char *path, const char *mode)
{
    FILE *f;
    struct RtFileData *data;
    int writable = 0;

    if (!path || !mode) {
        rt_errno = -RT_EINVAL;
        return 0;
    }
    if (mode[0] == 'w' || mode[0] == 'a')
        writable = 1;
    f = malloc(sizeof *f);
    if (!f) {
        rt_errno = -RT_ENOSPC;
        return 0;
    }
    memset(f, 0, sizeof *f);
    f->fd = RT_FILE_MEM;
    f->writable = writable;
    if (writable) {
        f->path = rt_strdup_n(path, strlen(path));
        if (!f->path) {
            free(f);
            rt_errno = -RT_ENOSPC;
            return 0;
        }
        return f;
    }
    data = rt_file_find(path);
    if (!data) {
        free(f);
        rt_errno = -RT_ENOENT;
        return 0;
    }
    f->buf = data->data;
    f->len = data->len;
    f->cap = data->len;
    return f;
}

FILE *freopen(const char *path, const char *mode, FILE *f)
{
    (void)path;
    (void)mode;
    return f;
}

FILE *tmpfile(void)
{
    rt_errno = -RT_ENOSYS;
    return 0;
}

char *tmpnam(char *s)
{
    static char name[] = "tmp";
    if (s)
        return strcpy(s, name);
    return name;
}

int remove(const char *path)
{
    (void)path;
    rt_errno = -RT_ENOSYS;
    return -1;
}

int rename(const char *oldpath, const char *newpath)
{
    (void)oldpath;
    (void)newpath;
    rt_errno = -RT_ENOSYS;
    return -1;
}

int feof(FILE *f)
{
    return f ? f->eof : 1;
}

int ferror(FILE *f)
{
    return f ? f->err : 1;
}

void clearerr(FILE *f)
{
    if (f) {
        f->eof = 0;
        f->err = 0;
    }
}

int setvbuf(FILE *f, char *buf, int mode, size_t size)
{
    (void)f;
    (void)buf;
    (void)mode;
    (void)size;
    return 0;
}

int fseek(FILE *f, long offset, int whence)
{
    size_t base;
    size_t pos;

    if (!f || f->fd != RT_FILE_MEM) {
        rt_errno = -RT_EBADF;
        return -1;
    }
    if (whence == SEEK_SET)
        base = 0;
    else if (whence == SEEK_CUR)
        base = f->pos;
    else
        base = f->len;
    if (offset < 0 && (size_t)(-offset) > base) {
        rt_errno = -RT_EINVAL;
        return -1;
    }
    pos = offset < 0 ? base - (size_t)(-offset) : base + (size_t)offset;
    if (pos > f->len)
        pos = f->len;
    f->pos = pos;
    f->eof = 0;
    return 0;
}

long ftell(FILE *f)
{
    if (!f || f->fd != RT_FILE_MEM) {
        rt_errno = -RT_EBADF;
        return -1;
    }
    return (long)f->pos;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    char buf[4096];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    size_t len;

    if (n < 0)
        return n;
    len = (size_t)n;
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    if (fwrite(buf, 1, len, f) != len)
        return EOF;
    return n;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int fscanf(FILE *f, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    int r;

    if (!fgets(buf, sizeof(buf), f))
        return EOF;
    va_start(ap, fmt);
    r = vsscanf(buf, fmt, ap);
    va_end(ap);
    return r;
}

int vsnprintf(char *dst, size_t n, const char *fmt, va_list ap)
{
    struct RtFmtOut out;
    size_t term;

    out.mode = RT_FMT_TO_BUFFER;
    out.buf = dst;
    out.cap = n;
    out.len = 0;
    out.error = 0;
    rt_vformat(&out, fmt, ap);
    if (n != 0) {
        term = out.len;
        if (term >= n)
            term = n - 1u;
        dst[term] = 0;
    }
    return out.error ? -1 : (int)out.len;
}

int snprintf(char *dst, size_t n, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vsnprintf(dst, n, fmt, ap);
    va_end(ap);
    return ret;
}

int vsprintf(char *dst, const char *fmt, va_list ap)
{
    struct RtFmtOut out;
    out.mode = RT_FMT_TO_BUFFER;
    out.buf = dst;
    out.cap = RT_NULL;
    out.len = 0;
    out.error = 0;
    rt_vformat(&out, fmt, ap);
    dst[out.len] = 0;
    return out.error ? -1 : (int)out.len;
}

int sprintf(char *dst, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vsprintf(dst, fmt, ap);
    va_end(ap);
    return ret;
}

int vprintf(const char *fmt, va_list ap)
{
    struct RtFmtOut out;
    out.mode = RT_FMT_TO_STDOUT;
    out.buf = 0;
    out.cap = 0;
    out.len = 0;
    out.error = 0;
    return rt_vformat(&out, fmt, ap);
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

int vsscanf(const char *src, const char *fmt, va_list ap)
{
    struct RtScanIn in;

    if (!src || !fmt)
        return RT_EINVAL;
    in.buf = src;
    in.pos = 0;
    in.start = 0;
    in.len = 0;
    in.bounded = 0;
    return rt_vscan(&in, fmt, ap);
}

int sscanf(const char *src, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vsscanf(src, fmt, ap);
    va_end(ap);
    return ret;
}

int vscanf(const char *fmt, va_list ap)
{
    struct RtScanIn in;
    int ret;

    if (!rt_stdio_ready())
        return RT_EINVAL;
    if (!fmt)
        return RT_EINVAL;
    in.buf = (const char *)rt_stdin_buf;
    in.pos = rt_stdin_pos;
    in.start = rt_stdin_pos;
    in.len = rt_stdin_len;
    in.bounded = 1;
    ret = rt_vscan(&in, fmt, ap);
    rt_stdin_pos = in.pos;
    return ret;
}

int scanf(const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vscanf(fmt, ap);
    va_end(ap);
    return ret;
}

int abs(int value)
{
    return value < 0 ? -value : value;
}

long labs(long value)
{
    return value < 0 ? -value : value;
}

long long llabs(long long value)
{
    return value < 0 ? -value : value;
}

static unsigned long long rt_strtoull_parse(const char *s, char **endptr,
                                            int base)
{
    const char *start = s;
    const char *digits;
    unsigned long long value = 0;
    int digit;
    int neg = 0;

    while (isspace((unsigned char)*s))
        ++s;
    if (*s == '-' || *s == '+') {
        neg = *s == '-';
        ++s;
    }
    digits = s;
    if (base != 0 && (base < 2 || base > 36)) {
        if (endptr)
            *endptr = (char *)start;
        return 0;
    }
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
        digits = s;
    } else if (base == 0 && *s == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }
    while ((digit = rt_digit_value((unsigned char)*s)) >= 0 && digit < base) {
        value = value * (unsigned)base + (unsigned)digit;
        ++s;
    }
    if (endptr)
        *endptr = (char *)(s != digits ? s : start);
    return neg ? 0ull - value : value;
}

unsigned long long strtoull(const char *s, char **endptr, int base)
{
    return rt_strtoull_parse(s, endptr, base);
}

long long strtoll(const char *s, char **endptr, int base)
{
    return (long long)rt_strtoull_parse(s, endptr, base);
}

unsigned long strtoul(const char *s, char **endptr, int base)
{
    return (unsigned long)strtoull(s, endptr, base);
}

long strtol(const char *s, char **endptr, int base)
{
    return (long)strtoll(s, endptr, base);
}

int atoi(const char *s)
{
    return (int)strtol(s, 0, 10);
}

long atol(const char *s)
{
    return strtol(s, 0, 10);
}

long long atoll(const char *s)
{
    return strtoll(s, 0, 10);
}

double strtod(const char *s, char **endptr)
{
    const char *p = s;
    const char *digits_start;
    int sign = 1;
    double value = 0.0;
    double scale = 1.0;
    int saw_digit = 0;
    int exp_sign = 1;
    int exp_value = 0;
    int i;

    while (isspace((unsigned char)*p))
        ++p;
    if (*p == '-' || *p == '+') {
        if (*p == '-')
            sign = -1;
        ++p;
    }

    digits_start = p;
    while (*p >= '0' && *p <= '9') {
        value = value * 10.0 + (double)(*p - '0');
        ++p;
        saw_digit = 1;
    }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            value = value * 10.0 + (double)(*p - '0');
            scale *= 10.0;
            ++p;
            saw_digit = 1;
        }
    }

    if (saw_digit && (*p == 'e' || *p == 'E')) {
        const char *exp_start = p;
        const char *q = p + 1;
        int saw_exp = 0;
        if (*q == '-' || *q == '+') {
            if (*q == '-')
                exp_sign = -1;
            ++q;
        }
        while (*q >= '0' && *q <= '9') {
            exp_value = exp_value * 10 + *q - '0';
            ++q;
            saw_exp = 1;
        }
        if (saw_exp)
            p = q;
        else
            p = exp_start;
    }

    if (!saw_digit) {
        if (endptr)
            *endptr = (char *)s;
        return 0.0;
    }

    value /= scale;
    for (i = 0; i < exp_value; ++i) {
        if (exp_sign > 0)
            value *= 10.0;
        else
            value /= 10.0;
    }
    if (endptr)
        *endptr = (char *)(saw_digit ? p : digits_start);
    return sign < 0 ? -value : value;
}

char *getenv(const char *name)
{
    (void)name;
    return 0;
}

int system(const char *command)
{
    (void)command;
    rt_errno = -RT_ENOSYS;
    return -1;
}

int rand(void)
{
    rt_rand_state = rt_rand_state * 1103515245u + 12345u;
    return (int)((rt_rand_state >> 1) & RAND_MAX);
}

void srand(unsigned seed)
{
    rt_rand_state = seed ? seed : 1;
}

void abort(void)
{
    rt_host_exit(134);
    for (;;)
        ;
}

void exit(int code)
{
    rt_host_exit(code);
    for (;;)
        ;
}

typedef int time_t;
typedef int clock_t;

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

clock_t clock(void)
{
    return 0;
}

time_t time(time_t *t)
{
    time_t now = 0;
    if (t)
        *t = now;
    return now;
}

int difftime(time_t end, time_t beginning)
{
    return end - beginning;
}

time_t mktime(struct tm *tm)
{
    (void)tm;
    return 0;
}

static struct tm rt_tm;

struct tm *localtime(const time_t *t)
{
    (void)t;
    memset(&rt_tm, 0, sizeof(rt_tm));
    rt_tm.tm_mday = 1;
    rt_tm.tm_year = 70;
    return &rt_tm;
}

struct tm *gmtime(const time_t *t)
{
    return localtime(t);
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
    (void)tm;
    if (!s || max == 0)
        return 0;
    if (!fmt)
        fmt = "";
    strncpy(s, fmt, max - 1);
    s[max - 1] = 0;
    return strlen(s);
}

struct lconv {
    char *decimal_point;
};

static struct lconv rt_lconv = { "." };

struct lconv *localeconv(void)
{
    return &rt_lconv;
}

char *setlocale(int category, const char *locale)
{
    (void)category;
    (void)locale;
    return "C";
}

typedef void (*sighandler_t)(int);

sighandler_t signal(int sig, sighandler_t handler)
{
    (void)sig;
    return handler;
}

int setjmp(int *env)
{
    (void)env;
    return 0;
}

void longjmp(int *env, int val)
{
    (void)env;
    (void)val;
    abort();
}

int rt_isatty(int fd)
{
    if (rt_stdio_hosted)
        return rt_host_isatty(fd);
    return fd == RT_STDIN;
}
