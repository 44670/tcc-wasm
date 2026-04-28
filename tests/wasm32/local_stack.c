struct Pair {
    int a;
    int b;
};

int local_array_sum(void)
{
    int a[3] = { 4, 5, 6 };
    return a[0] + a[1] + a[2];
}

int local_array_partial_zero(void)
{
    int a[3] = { 4 };
    return a[0] + a[1] + a[2];
}

int local_struct_field(void)
{
    struct Pair p;
    p.a = 12;
    p.b = 30;
    return p.b - p.a;
}

int ptr_to_local(void)
{
    int x = 41;
    int *p = &x;
    *p = *p + 1;
    return x;
}

int struct_ptr_field(void)
{
    struct Pair p;
    struct Pair *q = &p;
    q->a = 3;
    q->b = 9;
    return q->a + q->b;
}

int struct_assign_copy(void)
{
    struct Pair a;
    struct Pair b;
    a.a = 6;
    a.b = 8;
    b = a;
    return b.a * 10 + b.b;
}
