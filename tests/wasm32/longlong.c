int ll_add_low(void)
{
    long long a = 0x7fffffffLL;
    long long b = 5;
    return (int)(a + b);
}

int ll_sub_borrow_low(void)
{
    long long a = 0x100000000LL;
    long long b = 1;
    return (int)(a - b);
}

int ll_mul_low(void)
{
    long long a = 65537;
    long long b = 65539;
    return (int)(a * b);
}

int ll_cmp(void)
{
    long long a = 0x100000001LL;
    long long b = 0x100000000LL;
    return a > b;
}

long long ll_identity(long long x)
{
    return x;
}

long long ll_add_export(long long a, long long b)
{
    return a + b;
}

int ll_cmp_high_signed(void)
{
    long long a = -0x100000000LL;
    long long b = 0x7fffffffLL;
    return a < b;
}

unsigned long long ull_add_export(unsigned long long a, unsigned long long b)
{
    return a + b;
}

int ull_cmp_high(void)
{
    unsigned long long a = 0x7000000000000001ULL;
    unsigned long long b = 0x6000000000000001ULL;
    return a > b;
}
