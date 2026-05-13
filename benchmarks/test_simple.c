struct Point {
    int x;
    int y;
};

int printf(char *fmt, ...);
int add(int a, int b);

int g;
char ch;
int *ptr;
char *cptr;

int add(int a, int b) {
    int sum;
    return a + b;
}

int redeclare(int x) {
    int x;
    return x;
}

int dupParam(int p, int p) {
    return p;
}

int useUndeclared() {
    return q;
}

int arith() {
    int i;
    char c;
    int *p;
    i + c;
    i * 5;
    i % 3;
    p + i;
    return 0;
}

int compare() {
    int i;
    int *p;
    int *q;
    char *cp;
    i < 5;
    i == 5;
    p == q;
    p == 0;
    0 != p;
    p == cp;
    p < i;
    return 0;
}

int bits() {
    int *p;
    int i;
    i & 1;
    i | 2;
    i ^ 3;
    i << 1;
    i >> 1;
    p & 1;
    return 0;
}

int logic() {
    int *p;
    int i;
    i && p;
    i || 0;
    return 0;
}

int commaExpr() {
    int i;
    char c;
    (i, c);
    return 0;
}

int calls() {
    add(1, 2);
    add(1);
    add(1, 2, 3);
    printf("hi\n");
    printf("%d %s\n", 1, "x");
    return 0;
}

int retMismatch() {
    return "hi";
}

void voidWithValue() {
    return 5;
}

int main() {
    return 0;
}
