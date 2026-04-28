int g = 7;
int arr[3] = { 11, 22, 33 };
char msg[] = "abc";
char *msgp = msg;

int get_g(void)
{
    return g;
}

void set_g(int v)
{
    g = v;
}

int get_arr(int i)
{
    return arr[i];
}

int get_msg1(void)
{
    return msg[1];
}

int get_msgp2(void)
{
    return msgp[2];
}

int get_lit1(void)
{
    return "xy"[1];
}
