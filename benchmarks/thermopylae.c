int fact(int x) {
    if (x <= 1) {
        return 1;
    } else {
        return x * fact(x - 1);
    }
}

int main() {
    int stop;
    int factor, toStop, temp;

    stop = 0;
    factor = 0;

    while (!stop) {
        scanf("%d", &factor);
        temp = fact(factor);
        printf("%d\n", temp);
        scanf("%d", &toStop);
        if (toStop == 0) {
            stop = 1;
        }
    }
    return 0;
}
