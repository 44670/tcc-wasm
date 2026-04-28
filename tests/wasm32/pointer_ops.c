int get0(char *p)
{
    return *p;
}

int get1(char *p)
{
    return p[1];
}

int get_post(char *p)
{
    return *p++;
}

void set_post(char *p, char v)
{
    *p++ = v;
}

void copy_post(char *to, char *from)
{
    *to++ = *from++;
}
