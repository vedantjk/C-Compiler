struct MESSAGE {
    int val;
    struct MESSAGE *next;
};

int mod(int val, int t) {
    int temp;
    temp = val / t;
    return val - temp * t;
}

int power(int x, int n) {
    int temp;
    if (n == 0) {
        return 1;
    } else {
        if (mod(n, 2) == 1) {
            return x * power(x, n - 1);
        } else {
            temp = power(x, n / 2);
            return temp * temp;
        }
    }
}

void crypt(int m, int key, struct MESSAGE *msg) {
    msg->val = mod(power(msg->val, key), m);
    if (msg->next != NULL) {
        crypt(m, key, msg->next);
    }
}

int main() {
    int key;
    int m;
    int length;
    int readTemp;
    int printTemp;
    struct MESSAGE *start;
    struct MESSAGE *current;
    struct MESSAGE *temp;

    start = (struct MESSAGE *)malloc(sizeof(struct MESSAGE));
    current = start;

    scanf("%d", &key);
    scanf("%d", &m);
    scanf("%d", &length);

    length = length - 1;
    scanf("%d", &readTemp);
    current->val = readTemp;

    while (length > 0) {
        current->next = (struct MESSAGE *)malloc(sizeof(struct MESSAGE));
        current = current->next;
        scanf("%d", &readTemp);
        current->val = readTemp;
        length = length - 1;
    }
    current->next = NULL;

    crypt(m, key, start);
    current = start;
    while (current != NULL) {
        temp = current;
        printTemp = current->val;
        printf("%d\n", printTemp);
        current = current->next;
        free(temp);
    }
    return 0;
}
