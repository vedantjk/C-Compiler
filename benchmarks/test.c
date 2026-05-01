int add(int a, int b){
    return a + b;
}

/* New function: pointer and int params (exercises parseParam with pointer declarator) */
int sum_array(int *a, int n){
    int total = 0;
    return total;
}

/* New function: multi-dim array param (exercises parseParam with [N][M]) */
int process_matrix(int m[3][4], int rows){
    int x = 0;
    return x;
}

/* New function: pointer-to-pointer param */
int double_ptr(int **pp){
    int z = 0;
    return z;
}

int main()
{
    int x = 5;
    int y = 10;
    int a[10][11];
    a[1] = 2;
    a[x] = 12;
    a[x][y] = 23;
    int s1 = sizeof(int);
    int s2 = sizeof(int *);
    int s3 = sizeof(char[10]);
    int s4 = sizeof x;            /* expr form, no parens */
    int s5 = sizeof(x);           /* expr form, parenthesized — disambiguates to expr because x isn't a type */
    int s6 = sizeof !x;           /* sizeof of a unary expr */
    int s7 = sizeof a[0];         /* sizeof of a subscript */
    return sizeof(int) + 1;       /* in return */
    if (sizeof x > 0) { }         /* in condition */
    /* mixing unary with binary precedence */
    int a = -x + y;
    int b = -x * y;
    int c = -(x + y) * 2;
    int d = -x * -y;
    int e = !x && y;
    int g = -x + -y * -2;

    /* postfix mixed in */
    int h = x++ + y;
    int i = x + y++;
    int j = -x++;
    int k = -(x++);

    /* unary on a function call result */
    int l = -add(x, y);
    int m = !add(x, y);

    /* chained / nested unary */
    int n = !!x;
    int o = - - -x;
    int p = ~~x;

    /* unary inside conditions */
    if (-x < 0) {
        int q = 1;
    }
    if (!x && !y) {
        int r = 2;
    }

    /* unary inside for loop */
    for (int t = 0; t < 10; t = t + 1) {
        int u = -t;
    }

    /* unary inside while loop */
    while (!x) {
        x = x + 1;
    }

    /* === NEW: pointer declarations === */
    int *ptr;
    int **dptr;
    int ***tptr;

    /* pointer with initialization (uses unary &) */
    int *addrOfX = &x;

    /* === NEW: array declarations === */
    int arr1d[10];
    int matrix[3][4];
    int cube[2][3][4];

    /* === NEW: mixed comma decls — each var has its own declarator === */
    int *aa, bb, *cc;
    int dd[5], ee, ff[2][3];

    /* === NEW: char with pointers and arrays === */
    char *str;
    char buf[32];
    char names[10][32];

    /* === NEW: member access (.) and (->) — names need not be declared, parser only === */
    p.x = 1;
    q->y = 2;
    p.x.y = 3;            /* chained dot, left-associative */
    q->next->val = 4;     /* chained arrow */
    a[i].field = 5;       /* subscript then member */
    obj.arr[i] = 6;       /* member then subscript */
    obj.inner.arr[i] = 7; /* chained member then subscript */
    int memberRead = p.x + q->y;  /* members in expression position */


    char c = (char)x;            /* int → char, truncates */
    int  n = (int)c;             /* char → int, sign-extends */
    int *ip = (int *)p;          /* pointer reinterpretation */
    char *cp = (char *)&n;       /* peek at int's bytes */
    return -x;
}
