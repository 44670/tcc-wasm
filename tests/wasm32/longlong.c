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
