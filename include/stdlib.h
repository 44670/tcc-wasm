#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0
#define RAND_MAX 2147483647

typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

void abort(void);
void exit(int code);
int system(const char *command);
char *getenv(const char *name);
int abs(int value);
long labs(long value);
long long llabs(long long value);
int atoi(const char *s);
long atol(const char *s);
long long atoll(const char *s);
long strtol(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
long long strtoll(const char *s, char **endptr, int base);
unsigned long long strtoull(const char *s, char **endptr, int base);
double strtod(const char *s, char **endptr);
int rand(void);
void srand(unsigned seed);

static void *__tcc_stdlib_bsearch(const void *key, const void *base,
                                  size_t nmemb, size_t width,
                                  int (*compar)(const void *, const void *))
{
    const char *items = (const char *)base;

    while (nmemb) {
        size_t mid = nmemb / 2;
        const char *item = items + mid * width;
        int cmp = compar(key, item);
        if (cmp == 0)
            return (void *)item;
        if (cmp > 0) {
            items = item + width;
            nmemb -= mid + 1;
        } else {
            nmemb = mid;
        }
    }
    return 0;
}

static void __tcc_stdlib_swap(char *a, char *b, size_t width)
{
    while (width--) {
        char tmp = *a;
        *a++ = *b;
        *b++ = tmp;
    }
}

static void __tcc_stdlib_sift_down(char *base, size_t start, size_t end,
                                   size_t width,
                                   int (*compar)(const void *, const void *))
{
    size_t root = start;

    while (root * 2 + 1 <= end) {
        size_t child = root * 2 + 1;
        size_t swap_at = root;

        if (compar(base + swap_at * width, base + child * width) < 0)
            swap_at = child;
        if (child + 1 <= end
            && compar(base + swap_at * width,
                      base + (child + 1) * width) < 0)
            swap_at = child + 1;
        if (swap_at == root)
            return;
        __tcc_stdlib_swap(base + root * width, base + swap_at * width,
                          width);
        root = swap_at;
    }
}

static void __tcc_stdlib_qsort(char *base, size_t nmemb, size_t width,
                               int (*compar)(const void *, const void *))
{
    size_t start;
    size_t end;

    start = (nmemb - 2) / 2 + 1;
    while (start) {
        --start;
        __tcc_stdlib_sift_down(base, start, nmemb - 1, width, compar);
    }

    end = nmemb - 1;
    while (end) {
        __tcc_stdlib_swap(base, base + end * width, width);
        --end;
        __tcc_stdlib_sift_down(base, 0, end, width, compar);
    }
}

static void qsort(void *base, size_t nmemb, size_t width,
                  int (*compar)(const void *, const void *))
{
    if (!base || !compar || width == 0 || nmemb < 2)
        return;
    __tcc_stdlib_qsort((char *)base, nmemb, width, compar);
}

static void *bsearch(const void *key, const void *base, size_t nmemb,
                     size_t width,
                     int (*compar)(const void *, const void *))
{
    if (!key || !base || !compar || width == 0)
        return 0;
    return __tcc_stdlib_bsearch(key, base, nmemb, width, compar);
}

#endif
