/* Declarations for the chapter 20 test helpers defined in util.c (built with the
 * system compiler and linked in). */
#ifndef UTIL_H
#define UTIL_H

int check_one_int(int actual, int expected);
int check_5_ints(int a, int b, int c, int d, int e, int start);
int check_12_ints(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j, int k, int l,
                  int start);
int check_one_uchar(unsigned char actual, unsigned char expected);
int check_one_uint(unsigned int actual, unsigned int expected);
int check_one_long(long actual, long expected);
int check_one_ulong(unsigned long actual, unsigned long expected);
int check_one_double(double actual, double expected);
int check_12_longs(long a, long b, long c, long d, long e, long f, long g, long h, long i, long j,
                   long k, long l, long start);
int check_six_chars(char a, char b, char c, char d, char e, char f, int start);
int check_14_doubles(double a, double b, double c, double d, double e, double f, double g, double h,
                     double i, double j, double k, double l, double m, double n, double start);
int check_12_vals(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j, long *k,
                  double *l, int start);
int id(int x);
double dbl_id(double x);
long long_id(long l);
unsigned unsigned_id(unsigned u);
unsigned char uchar_id(unsigned char uc);

#endif
