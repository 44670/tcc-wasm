int sink;

void set_sink(int v)
{
    sink = v;
}

int void_call_roundtrip(void)
{
    set_sink(37);
    return sink;
}

int nested_call_arg(int x)
{
    set_sink(x + 5);
    return sink + 1;
}
