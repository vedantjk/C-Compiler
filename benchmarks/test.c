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
    a = 1+2 ? 1 : 0;
    return 0;
}

// ---- ternary `?:` ----

// basic ternary, condition uses binary so we know precedence is right
int abs_val(int x) { return x > 0 ? x : -x; }

// right-associativity: a ? b : c ? d : e  parses as  a ? b : (c ? d : e)
int main() {
    int a = 1; int b = 2; int c = 3; int d = 4; int e = 5;
    int r = a ? b : c ? d : e;     // = a ? b : (c ? d : e)
    return r;
}

// chained "switch" idiom — exercises right-assoc nesting depth
int classify(int x) {
    return x == 0 ? 1
         : x == 1 ? 2
         : x == 2 ? 3
         : 0;
}

// ternary in expression contexts (init, return, condition, arg, index, member)
int foo(int a, int b) { return a + b; }
int main() {
    int x = 1;
    int y = x ? 10 : 20;                  // init slot
    int a[10];
    if (x ? 1 : 0) { return 1; }          // if-condition
    while (x ? x - 1 : 0) { x = x - 1; }  // while-condition
    foo(x ? 1 : 2, x ? 3 : 4);            // both call args
    a[x ? 0 : 1] = 99;                    // subscript index
    return x ? a[0] : a[1];               // return
}

// assignment inside ternary slots
int main() {
    int x = 0; int y = 0; int z = 0;
    x = y ? (z = 5) : 0;           // explicit parens
    x = y ? z = 5 : 0;             // middle slot — `expression` allows assignment, no parens needed
    x = y ? 1 : z = 5;             // third slot — gcc-style: parses as y ? 1 : (z = 5)
    return x;
}

// ternary nested as condition
int main() {
    int a = 1; int b = 2;
    int r = (a > b ? a : b) > 0 ? 1 : -1;  // ternary feeding a binary feeding another ternary
    return r;
}

// parens force a different parse tree than right-assoc default
int main() {
    int a = 1; int b = 2; int c = 3; int d = 4; int e = 5;
    int r = (a ? b : c) ? d : e;   // forces left-leaning shape
    return r;
}

// ternary in pointer/struct contexts — no parser-level lvalue check
struct Node { int data; struct Node *next; };
int main() {
    struct Node *p; struct Node *q; struct Node *r;
    int v = (p ? p : q)->data;     // ternary expression as base of `->`
    return v;
}

// ---- compound assignment ----

// every compound operator, basic form
int main() {
    int x = 0;
    x += 1;
    x -= 1;
    x *= 2;
    x /= 2;
    x %= 3;
    x <<= 1;
    x >>= 1;
    x &= 7;
    x |= 8;
    x ^= 4;
    return x;
}

// right-associativity: a += b += c  parses as  a += (b += c)
int main() {
    int a = 1; int b = 2; int c = 3;
    a += b += c;                   // a += (b += c)
    return a;
}

// mixing plain `=` and compound — both sit at the same precedence level
int main() {
    int x = 0; int y = 0;
    x = y += 5;                    // x = (y += 5)
    x += y = 7;                    // x += (y = 7)
    return x;
}

// compound on non-trivial LHS forms
struct Box { int w; int h; };
int main() {
    int *p; int a[10]; struct Box b; struct Box *bp;
    *p += 1;
    a[0] *= 2;
    b.w -= 3;
    bp->h <<= 2;
    return 0;
}

// compound with non-trivial RHS — exercises full expression on the right
int main() {
    int x = 10; int y = 3; int z = 2;
    x += y * z;                    // RHS is a binary expression
    x *= y + z;
    x &= y > z ? 0xF : 0x0;        // RHS is a ternary (note: hex may not parse; use decimal if it doesn't)
    return x;
}

// compound in expression contexts (init, return, condition, arg)
int sink(int v) { return v; }
int main() {
    int x = 1; int y = 2;
    int z = (x += 5);              // init slot — assignment-expression
    if (x -= 1) { return x; }      // if-condition
    while (y *= 2) { y = y - 100; }
    sink(x ^= y);                  // function arg
    return x;
}

// compound in for-loop update slot — bare expression, no `;` consumed by slot
int main() {
    int i = 0; int sum = 0;
    for (i = 0; i < 10; i += 2) { sum += i; }
    for (i = 0; i < 100; i *= 2) { ; }   // i never grows from 0 — infinite-loop semantically, but parses
    return sum;
}

// parser-permissive: compound on non-lvalue, parser accepts, semantic rejects
int main() {
    int a; int b;
    (a + b) += 5;
    1 *= 2;
    return 0;
}

// ---- bitwise + % as binary ops ----

// each line targets one precedence boundary; comment shows expected parse
int main() {
    int a = 1; int b = 2; int c = 3;

    int r1 = a | b & c;            // = a | (b & c)         — & tighter than |
    int r2 = a & b | c;            // = (a & b) | c
    int r3 = a ^ b | c;            // = (a ^ b) | c         — ^ tighter than |, looser than &
    int r4 = a & b ^ c;            // = (a & b) ^ c

    int r5 = a == b & c;           // = (a == b) & c        — equality tighter than &
    int r6 = a & b == c;           // = a & (b == c)        — famous C wart
    int r7 = a & b == 0;

    int r8 = a + b << c;           // = (a + b) << c        — shift looser than +
    int r9 = a << b + c;           // = a << (b + c)
    int r10 = a < b << c;          // = a < (b << c)        — shift tighter than <

    int r11 = a + b % c;           // = a + (b % c)         — % at multiplicative level
    int r12 = a * b % c;           // = (a * b) % c         — left-assoc same level

    int r13 = ~a & b;              // = (~a) & b
    int r14 = a & ~b;              // = a & (~b)

    return r1 + r2 + r3 + r4 + r5 + r6 + r7 + r8 + r9 + r10 + r11 + r12 + r13 + r14;
}
