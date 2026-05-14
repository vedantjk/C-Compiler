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

int unary() {
    int a;
    int b;
    int *p;
    struct Point s;

    /* --- valid --- */
    -a;
    +a;
    ~a;
    !a;
    !p;
    *p;
    &a;
    &p;
    ++a;
    a++;
    --a;
    a--;
    ++p;
    p--;
    *&a;       /* &a is int*, *(int*) is int lvalue */
    &*p;       /* *p is int lvalue, &(int lvalue) is int* */
    !!a;

    /* --- invalid: arithmetic on pointer --- */
    -p;
    ~p;

    /* --- invalid: ! on struct (not scalar) --- */
    !s;

    /* --- invalid: deref non-pointer --- */
    *a;
    *5;

    /* --- invalid: addr-of non-lvalue --- */
    &5;
    &(a + b);

    /* --- invalid: ++/-- on non-lvalue --- */
    ++5;
    (a + b)++;

    /* --- invalid: result of ++ is not lvalue --- */
    &(a++);

    /* --- invalid: ++ on struct (lvalue but not scalar) --- */
    ++s;

    return 0;
}

int assign() {
    int a;
    int b;
    char c;
    int *p;
    int *q;
    char *cp;
    struct Point s;

    /* --- valid: plain '=' same type --- */
    a = b;
    c = c;
    p = q;

    /* --- valid: null-pointer-constant to pointer --- */
    p = 0;

    /* --- valid: chained / right-associative --- */
    a = b = 5;

    /* --- valid: compound arithmetic on ints --- */
    a += b;
    a -= 1;
    a *= 2;
    a /= 3;
    a %= 4;

    /* --- valid: pointer += int / -= int --- */
    p += 1;
    p -= a;

    /* --- valid: compound bitwise on ints --- */
    a &= 1;
    a |= 2;
    a ^= 3;
    a <<= 1;
    a >>= 1;

    /* --- invalid: type mismatch on '=' --- */
    a = p;
    p = cp;
    a = "hi";

    /* --- invalid: LHS not lvalue --- */
    5 = a;
    (a + b) = 3;
    (a = 1) = 2;     /* result of '=' is not lvalue */

    /* --- invalid: += / -= bad operands --- */
    a += p;          /* RHS pointer */
    s += 1;          /* LHS struct */
    p += q;          /* RHS pointer */

    /* --- invalid: *= / /= / %= bad operands --- */
    p *= 1;          /* LHS pointer */
    a *= p;          /* RHS pointer */
    s /= 1;          /* LHS struct */

    /* --- invalid: bitwise= bad operands --- */
    p &= 1;          /* LHS pointer */
    a |= p;          /* RHS pointer */
    a <<= cp;        /* RHS pointer */

    /* --- invalid: result of assignment is not lvalue --- */
    &(a = 1);
    &(a += 1);

    return 0;
}

int main() {
    return 0;
}
