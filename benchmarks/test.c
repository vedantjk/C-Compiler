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

    return -x;
}
