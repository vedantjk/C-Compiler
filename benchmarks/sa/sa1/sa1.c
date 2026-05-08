int a;
int d;

void foo(int a, int b, int d) {
    int a;
    int d;
    //printf("%d\n", a);
}

int main() {
    foo(4, 3, 4);
    return 0;
}