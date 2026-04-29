float gf = 1.25f;
double gd = 2.5;
float gf_arr[2] = { 0.5f, 4.25f };
double gd_arr[2] = { 1.5, 8.75 };

float addf(float a, float b)
{
    return a + b;
}

double addd(double a, double b)
{
    return a + b;
}

float negf(float a)
{
    return -a;
}

double mixd(double a, double b)
{
    return (a * b) / 2.0;
}

int f_less(float a, float b)
{
    return a < b;
}

int d_ge(double a, double b)
{
    return a >= b;
}

double int_to_double(int x)
{
    return (double)x + 0.5;
}

int double_to_int(double x)
{
    return (int)x;
}

double float_to_double(float x)
{
    return (double)x + 0.25;
}

float double_to_float(double x)
{
    return (float)x;
}

float global_float_sum(void)
{
    return gf + gf_arr[1];
}

double global_double_sum(void)
{
    return gd + gd_arr[1];
}

double call_double_ptr(double (*fn)(double, double), double a, double b)
{
    return fn(a, b);
}

double call_addd_ptr(double a, double b)
{
    return call_double_ptr(addd, a, b);
}

long long double_to_ll(double x)
{
    return (long long)x;
}

double ll_to_double(long long x)
{
    return (double)x + 0.25;
}
