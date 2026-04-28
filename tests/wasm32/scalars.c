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

int bitfield_local(void)
{
    struct Bits b;
    b.a = 5;
    b.b = -3;
    b.c = 513;
    return b.a + b.b * 10 + b.c;
}
