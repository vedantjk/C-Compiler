int add(int a, int b){
    return a + b;
}

int main()
{
    int x = 5;
    int y = 10;

    // mixing unary with binary precedence
    int a = -x + y;
    int b = -x * y;
    int c = -(x + y) * 2;
    int d = -x * -y;
    int e = !x && y;
    int g = -x + -y * -2;

    // postfix mixed in
    int h = x++ + y;
    int i = x + y++;
    int j = -x++;
    int k = -(x++);

    // unary on a function call result
    int l = -add(x, y);
    int m = !add(x, y);

    // chained / nested unary
    int n = !!x;
    int o = - - -x;
    int p = ~~x;

    // unary inside conditions
    if (-x < 0) {
        int q = 1;
    }
    if (!x && !y) {
        int r = 2;
    }

    // unary inside for loop
    for (int t = 0; t < 10; t = t + 1) {
        int u = -t;
    }

    // unary inside while loop
    while (!x) {
        x = x + 1;
    }

    return -x;
}
