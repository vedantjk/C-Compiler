int foo(int a, int b) {
    int c;
    int d;
    scanf("%d", &c);
    scanf("%d", &d);
    if (c > d) {
        d = c + 1;
    } else {
        c = d + 1;
    }
    return a + c + d;
}

int main() {
    int a;
    int b;
    int c;
    scanf("%d", &a);
    scanf("%d", &b);
    c = foo(a, b);
    printf("%d\n", c);
    return 0;
}
