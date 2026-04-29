#ifndef _MATH_H
#define _MATH_H

#define HUGE_VAL 2147483647

double fabs(double x);
double sin(double x);
double sinh(double x);
double cos(double x);
double cosh(double x);
double tan(double x);
double tanh(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double ceil(double x);
double floor(double x);
double fmod(double x, double y);
double modf(double x, double *iptr);
double sqrt(double x);
double pow(double x, double y);
double log(double x);
double log10(double x);
double exp(double x);
double frexp(double x, int *exp);
double ldexp(double x, int exp);

#endif
