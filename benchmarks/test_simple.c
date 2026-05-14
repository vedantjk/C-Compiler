struct Point {
    int x;
    int y;
};

struct Node {
    int val;
    struct Node *child;
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

int sizeofTest() {
    int a;
    int *p;
    struct Point s;

    /* --- valid: type form --- */
    sizeof(int);
    sizeof(int*);
    sizeof(struct Point);

    /* --- valid: expr form (not evaluated, but type-checked) --- */
    sizeof(a);
    sizeof(a + 1);
    sizeof(*p);
    sizeof(s);

    /* --- valid: result is int — assign into an int --- */
    a = sizeof(int);

    /* --- invalid: bad operand inside expr form propagates --- */
    sizeof(nope);     /* undeclared identifier */
    sizeof(*a);       /* deref non-pointer */

    return 0;
}

int castTest() {
    int a;
    int *p;
    char c;
    struct Point s;

    /* --- valid: scalar -> scalar --- */
    (int)c;
    (char)a;
    (int*)a;
    (int)p;

    /* --- valid: (void)x to discard --- */
    (void)a;
    (void)p;

    /* --- valid: null-pointer-constant via (void*)0 still works --- */
    p = (void*)0;

    /* --- valid: cast result is the target type (assign back) --- */
    a = (int)p;
    p = (int*)a;

    /* --- invalid: cast FROM struct --- */
    (int)s;

    /* --- invalid: cast TO struct --- */
    (struct Point)a;

    return 0;
}

int subscriptTest() {
    int a;
    int *p;
    int arr[5];
    int matrix[3][4];
    char *cp;
    struct Point s;

    /* --- valid: pointer subscript --- */
    p[0];
    p[a];

    /* --- valid: array subscript --- */
    arr[0];
    arr[a];

    /* --- valid: multi-dim subscript peels one layer at a time --- */
    matrix[1];        /* result is int[4], lvalue */
    matrix[1][2];     /* result is int, lvalue */

    /* --- valid: result is lvalue, can assign through it --- */
    arr[0] = 7;
    p[a] = 9;
    matrix[1][2] = 11;

    /* --- valid: char* indexing --- */
    cp[0];

    /* --- invalid: non-integer index --- */
    arr[p];
    arr[s];

    /* --- invalid: operand not array/pointer --- */
    a[0];
    s[0];
    5[arr];           /* symmetric form intentionally rejected in v1 */

    return 0;
}

int ternaryTest() {
    int a;
    int b;
    int *p;
    int *q;
    char *cp;
    struct Point s;

    /* --- valid: int condition, matching int branches --- */
    a ? 1 : 2;

    /* --- valid: pointer condition --- */
    p ? 1 : 2;

    /* --- valid: assign result, both branches int --- */
    a = b ? 1 : 2;

    /* --- valid: matching pointer branches --- */
    a = p ? 0 : 0;
    p ? p : q;

    /* --- valid: pointer + null-pointer-constant on either side --- */
    p = a ? p : 0;
    p = a ? 0 : p;

    /* --- valid: nested ternary --- */
    a ? (b ? 1 : 2) : 3;

    /* --- invalid: condition is struct (not scalar) --- */
    s ? 1 : 2;

    /* --- invalid: branches are incompatible --- */
    a ? p : 5;        /* pointer vs non-null int */
    a ? p : cp;       /* int* vs char* */
    a ? a : s;        /* int vs struct */

    /* --- invalid: ternary result is not lvalue --- */
    &(a ? b : 1);
    (a ? b : 1) = 5;

    return 0;
}

int memberTest() {
    int i;
    int *ip;
    struct Point s;
    struct Point *ps;
    struct Node n;
    struct Node *pn;

    /* --- valid: simple . and -> --- */
    s.x;
    ps->x;
    n.val;
    pn->val;

    /* --- valid: pointer-typed fields --- */
    n.child;
    pn->child;

    /* --- valid: chained access --- */
    n.child->val;
    pn->child->val;
    pn->child->child->val;
    n.child->child->val;

    /* --- valid: member is lvalue --- */
    s.x = 7;
    ps->x = 8;
    &s.y;
    &pn->val;

    /* --- valid: chained member also lvalue --- */
    pn->child->val = 9;
    &n.child->val;

    /* --- invalid: no such field --- */
    s.nope;
    ps->nope;
    pn->child->nope;

    /* --- invalid: '.' used on pointer (need '->') --- */
    ps.x;
    pn.val;

    /* --- invalid: '->' used on non-pointer (need '.') --- */
    s->x;
    n->val;

    /* --- invalid: LHS is not struct / pointer-to-struct --- */
    i.x;
    ip->x;          /* int* peeled gives int, not a struct */

    return 0;
}

int main() {
    return 0;
}
