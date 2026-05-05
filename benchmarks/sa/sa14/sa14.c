struct A {
    int b;
};

void foo() {
    struct A *myVar;
    if (1 > 2) {
        return myVar;
    }
    return;
}

int bar(int x) {
    struct A *myVar;
    if (x > 0) {
        return 1;
    } else {
        if (x < 0) {
            return myVar;
        } else {
            return;
        }
    }
    return x;
}

int main() {
    return 4;
}
