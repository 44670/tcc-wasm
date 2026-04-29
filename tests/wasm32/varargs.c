typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) (void)(ap)

int global_a = 17;
int global_b = 29;

int sum_ints(int n, ...)
{
    va_list ap;
    int i;
    int total = 0;

    va_start(ap, n);
    for (i = 0; i < n; ++i)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

int pick_ptr(int which, ...)
{
    va_list ap;
    int *p;

    va_start(ap, which);
    p = va_arg(ap, int *);
    if (which)
        p = va_arg(ap, int *);
    va_end(ap);
    return *p;
}

int no_extra_args(void)
{
    return sum_ints(0);
}

int sum_five(void)
{
    return sum_ints(5, 1, 2, 3, 4, 5);
}

int pointer_second(void)
{
    return pick_ptr(1, &global_a, &global_b);
}

int nested_varargs(void)
{
    return sum_ints(2, sum_ints(2, 3, 4), 5);
}

int indirect_varargs(void)
{
    int (*fn)(int, ...) = sum_ints;
    return fn(3, 7, 8, 9);
}
