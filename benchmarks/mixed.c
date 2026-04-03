struct simple {
    int one;
};

struct foo {
    int bar;
    int cool;
    struct simple *simp;
};

struct foo *globalfoo;
struct foo *unusedGlobal;

void tailrecursive(int num) {
    struct foo *unused;

    if (num <= 0) {
        return;
    }
    unused = (struct foo *)malloc(sizeof(struct foo));
    unusedGlobal = unused;
    tailrecursive(num - 1);
}

int add(int x, int y) {
    return x + y;
}

void domath(int num) {
    struct foo *math1;
    struct foo *math2;
    int tmp;

    math1 = (struct foo *)malloc(sizeof(struct foo));
    math1->simp = (struct simple *)malloc(sizeof(struct simple));
    math2 = (struct foo *)malloc(sizeof(struct foo));
    math2->simp = (struct simple *)malloc(sizeof(struct simple));

    math1->bar = num;
    math2->bar = 3;
    math1->simp->one = math1->bar;
    math2->simp->one = math2->bar;

    while (num > 0) {
        tmp = math1->bar * math2->bar;
        tmp = (tmp * math1->simp->one) / math2->bar;
        tmp = add(math2->simp->one, math1->bar);
        tmp = math2->bar - math1->bar;
        num = num - 1;
    }

    free(math1);
    free(math2);
}

void objinstantiation(int num) {
    struct foo *tmp;

    while (num > 0) {
        tmp = (struct foo *)malloc(sizeof(struct foo));
        free(tmp);
        num = num - 1;
    }
}

int ackermann(int m, int n) {
    if (m == 0) {
        return n + 1;
    }

    if (n == 0) {
        return ackermann(m - 1, 1);
    } else {
        return ackermann(m - 1, ackermann(m, n - 1));
    }
}

int main() {
    int a;
    int b;
    int c;
    int d;
    int e;
    int temp;

    scanf("%d", &a);
    scanf("%d", &b);
    scanf("%d", &c);
    scanf("%d", &d);
    scanf("%d", &e);

    tailrecursive(a);
    domath(b);
    objinstantiation(c);
    temp = ackermann(d, e);
    printf("a=%d\nb=%d\nc=%d\ntemp=%d\n", a, b, c, temp);
    return 0;
}
