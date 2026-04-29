struct Bits {
    unsigned a : 3;
    signed b : 5;
    unsigned c : 10;
};

int cast_signed_char(int x)
{
    return (signed char)x;
}

int cast_unsigned_char(int x)
{
    return (unsigned char)x;
}

int cast_signed_short(int x)
{
    return (short)x;
}

int cast_unsigned_short(int x)
{
    return (unsigned short)x;
}

int int32_cmp(void)
{
    int a = -1;
    int b = 1;
    return a < b;
}

int uint32_cmp(void)
{
    unsigned int a = 0xffffffffu;
    unsigned int b = 1u;
    return a > b;
}

int bitfield_local(void)
{
    struct Bits b;
    b.a = 5;
    b.b = -3;
    b.c = 513;
    return b.a + b.b * 10 + b.c;
}
