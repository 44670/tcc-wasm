int inc1(int x)
{
    return x + 1;
}

int add2(int x)
{
    return x + 2;
}

int (*global_fp)(int) = inc1;
int side;

void set_side(int x)
{
    side = x;
}

void (*global_vfp)(int) = set_side;

int call_local_fp(int x)
{
    int (*fp)(int) = add2;
    return fp(x);
}

int call_global_fp(int x)
{
    return global_fp(x);
}

int call_selected_fp(int x, int which)
{
    int (*fp)(int) = which ? add2 : inc1;
    return fp(x);
}

int call_void_fp(int x)
{
    global_vfp(x);
    return side;
}
