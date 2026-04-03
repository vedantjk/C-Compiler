int isqrt(int a) {
    int square;
    int delta;
    square = 1;
    delta = 3;
    while (square <= a) {
        square = square + delta;
        delta = delta + 2;
    }
    return (delta / 2 - 1);
}

int prime(int a) {
    int max;
    int divisor;
    int remainder;
    if (a < 2) {
        return 0;
    } else {
        max = isqrt(a);
        divisor = 2;
        while (divisor <= max) {
            remainder = a - ((a / divisor) * divisor);
            if (remainder == 0) {
                return 0;
            }
            divisor = divisor + 1;
        }
        return 1;
    }
}

int main() {
    int limit;
    int a;
    scanf("%d", &limit);
    a = 0;
    while (a <= limit) {
        if (prime(a)) {
            printf("%d\n", a);
        }
        a = a + 1;
    }
    return 0;
}
