int add(int a, int b)
{
    return a + b;
}

int fib(int n)
{
    if (n < 2)
        return n;
    return fib(n - 1) + fib(n - 2);
}

int answer(void)
{
    return add(fib(6), 4);
}
