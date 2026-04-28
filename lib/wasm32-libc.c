/*
 * Small freestanding wasm32 runtime provider.
 *
 * This file is intentionally compiled by wasm32-tcc itself.  It is not a
 * complete hosted libc; it is the stable first provider module for TCC-emitted
 * wasm programs: memory allocation, byte/string primitives, and buffered stdio
 * hooks with no JavaScript runtime dependency.
 */

typedef unsigned int size_t;

#define RT_STDIN 0
#define RT_STDOUT 1
#define RT_STDERR 2

#define RT_EBADF  (-9)
#define RT_EINVAL (-22)
#define RT_ENOSPC (-28)

#define RT_STDIO_CAP 1048576u
#define RT_HEAP_CAP 16777216u
#define RT_NULL (~0u)

struct RtBlock {
    size_t size;
    size_t next;
    int free;
};

static unsigned char rt_stdin_buf[RT_STDIO_CAP];
static unsigned char rt_stdout_buf[RT_STDIO_CAP];
static unsigned char rt_stderr_buf[RT_STDIO_CAP];
static size_t rt_stdin_len;
static size_t rt_stdin_pos;
static size_t rt_stdout_used;
static size_t rt_stderr_used;

static unsigned char rt_heap[RT_HEAP_CAP];
static int rt_heap_ready;

static size_t rt_align8(size_t n)
{
    return (n + 7u) & ~7u;
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

static struct RtBlock *rt_block_at(size_t off)
{
    return (struct RtBlock *)(rt_heap + off);
}

static void rt_heap_init(void)
{
    struct RtBlock *b;
    if (rt_heap_ready)
        return;
    b = rt_block_at(0);
    b->size = RT_HEAP_CAP - sizeof(struct RtBlock);
    b->next = RT_NULL;
    b->free = 1;
    rt_heap_ready = 1;
}

static void rt_heap_split(size_t off, size_t size)
{
    struct RtBlock *b = rt_block_at(off);
    struct RtBlock *n;
    size_t n_off;

    if (b->size < size + sizeof(struct RtBlock) + 8u)
        return;
    n_off = off + sizeof(struct RtBlock) + size;
    n = rt_block_at(n_off);
    n->size = b->size - size - sizeof(struct RtBlock);
    n->next = b->next;
    n->free = 1;
    b->size = size;
    b->next = n_off;
}

static void rt_heap_coalesce(void)
{
    size_t off = 0;

    while (off != RT_NULL) {
        struct RtBlock *b = rt_block_at(off);
        if (b->free && b->next != RT_NULL) {
            struct RtBlock *n = rt_block_at(b->next);
            if (n->free) {
                b->size += sizeof(struct RtBlock) + n->size;
                b->next = n->next;
                continue;
            }
        }
        off = b->next;
    }
}

void *malloc(size_t size)
{
    size_t off;

    rt_heap_init();
    if (size == 0)
        size = 1;
    size = rt_align8(size);
    for (off = 0; off != RT_NULL; off = rt_block_at(off)->next) {
        struct RtBlock *b = rt_block_at(off);
        if (b->free && b->size >= size) {
            rt_heap_split(off, size);
            b->free = 0;
            return rt_heap + off + sizeof(struct RtBlock);
        }
    }
    return 0;
}

void free(void *ptr)
{
    size_t off;
    struct RtBlock *b;

    if (!ptr)
        return;
    off = (size_t)((unsigned char *)ptr - rt_heap);
    if (off < sizeof(struct RtBlock) || off >= RT_HEAP_CAP)
        return;
    off -= sizeof(struct RtBlock);
    b = rt_block_at(off);
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
    void *new_ptr;
    struct RtBlock *b;

    if (!ptr)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return 0;
    }

    b = rt_block_at((size_t)((unsigned char *)ptr - rt_heap)
                    - sizeof(struct RtBlock));
    old_size = b->size;
    size = rt_align8(size);
    if (size <= old_size) {
        rt_heap_split((size_t)((unsigned char *)ptr - rt_heap)
                      - sizeof(struct RtBlock), size);
        return ptr;
    }

    new_ptr = malloc(size);
    if (!new_ptr)
        return 0;
    memcpy(new_ptr, ptr, old_size);
    free(ptr);
    return new_ptr;
}

int rt_stdio_capacity(void)
{
    return RT_STDIO_CAP;
}

void rt_stdio_reset(void)
{
    rt_stdin_len = 0;
    rt_stdin_pos = 0;
    rt_stdout_used = 0;
    rt_stderr_used = 0;
}

static void rt_stdin_compact(void)
{
    size_t remaining;

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

static int rt_buffer_write(unsigned char *buf, size_t *buf_len,
                           const void *src, size_t len)
{
    size_t room;
    size_t n;

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

int rt_read(int fd, void *dst, size_t len)
{
    size_t avail;
    size_t n;

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
