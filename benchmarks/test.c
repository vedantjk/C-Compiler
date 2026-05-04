int x;
int a = 0;
int m, n, o;
char c;
int *p;
int **pp;
int a[10];
int m[3][4];
int *a[10];
int x = 1, y = 2;
int *foo() { return 0; }
int **bar() { return 0; }

struct Point {
    int x;
    int y;
};

struct Node {
    int data;
    struct Node *next;
};

struct Mixed {
    int a, b;
    char *name;
    int grid[3][4];
    struct Point origin;
    struct Point *cursor;
};

struct Point origin;
struct Point *cursor;
struct Point path[100];

struct Point getOrigin() { return 0; }
struct Point *findPoint() { return 0; }

void translate(struct Point p, int dx) { }
void scale(struct Point *p, int k) { }

void main() {
    struct Point p;
    struct Point *q;
    struct Point { int x; int y; };
    p.x = 1;
    p.y = 2;
    q->x = 3;
}

int main() { ; return 0; }      // leading empty
int main() { return 0; ; }      // trailing empty
int main() { ; }                // only empty
int main() { ;;; return 0; }    // multiple

int main() {
    int i = 0;
    do { i = i + 1; } while (i < 10);
    do { ; } while (0);            // exercises empty-stmt inside do-while body
    do { break; } while (1);
    do { continue; } while (i < 5);
    return 0;
}

// ---- bundled change: AssignExpr + ExprStmt ----

// right-associativity of '='
int main() {
    int a; int b; int c;
    a = b = c = 5;                 // parses as a = (b = (c = 5))
    return a;
}

// assignment in initializer (initializer is just any expression now)
int main() {
    int x = 1;
    int y = (x = 5);               // y == 5, x mutated to 5
    int z = x = y = 7;             // chain inside an init
    return z;
}

// assignment in condition slots — no parens-around-statement weirdness
int main() {
    int x = 0;
    if (x = 5) { return x; }
    while (x = x - 1) { ; }
    return 0;
}

// assignment in function-call argument (was the case you just thought might break)
int foo(int a, int b) { return a + b; }
int main() {
    int x; int y;
    foo(x = 1, y = 2);             // commas here are arg separators, NOT comma operator
    foo(x = y = 3, 4);
    return 0;
}

// assignment in array index and as return value
int main() {
    int a[10]; int i = 0;
    a[i = 3] = 99;
    return a[i] = 0;               // legal: returns the assigned value
}

// bare expression statements that used to require a wrapper
int side_effect() { return 0; }
int main() {
    int x = 0; int y = 0;
    side_effect();                 // ExprStmt(FunctionCallExpr) — used to be FunctionCallStmt
    x++;                           // ExprStmt(UnaryExpr) — bare postfix, didn't parse before
    ++x;                           // bare prefix
    --y;
    y--;
    1 + 2;                         // legal C: useless ExprStmt
    x + y;                         // also legal, also useless
    (x);                           // parenthesized bare expression
    return 0;
}

// assignment to non-trivial LHS forms
struct Pt { int x; int y; };
int main() {
    int *p; int **pp;
    int a[10];
    struct Pt s; struct Pt *sp;
    *p = 5;
    **pp = 7;
    a[0] = 1;
    s.x = 2;
    sp->y = 3;
    return 0;
}

// for-loop with bare expressions in init/update slots (the parseExpressionStmt / parseExpression switch)
int main() {
    int i = 0; int n = 10;
    for (i = 0; i < n; i = i + 1) { side_effect(); }
    for (i = 0; i < n; i++) { ; }                // bare postfix as update
    for (side_effect(); i < n; side_effect()) { ; } // bare call in both slots
    return 0;
}

// parser-permissive: these should PARSE but semantic analysis must reject later
int main() {
    int a; int b;
    (a + b) = 5;                   // not an lvalue — parser accepts, semantic rejects
    side_effect() = 5;             // same
    1 = 2;                         // same
    return 0;
}
