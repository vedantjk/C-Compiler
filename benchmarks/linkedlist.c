struct Node {
    int num;
    struct Node *next;
};

struct Node *head;
struct Node *tail;

void Add(int num) {
    struct Node *newList;
    newList = (struct Node *)malloc(sizeof(struct Node));
    newList->num = num;
    newList->next = NULL;

    if (head == NULL) {
        head = newList;
        tail = newList;
    } else {
        tail->next = newList;
        tail = newList;
    }
}

void PrintList(struct Node *curr) {
    int printValue;

    if (curr == tail) {
        printValue = curr->num;
        printf("%d\n", printValue);
    } else {
        printValue = curr->num;
        printf("%d\n", printValue);
        PrintList(curr->next);
    }
}

void Del(struct Node *curr, int num) {
    struct Node *temp;
    if (curr == NULL) {
        /* do nothing */
    } else {
        if (head->num == num) {
            temp = head;
            head = head->next;
            free(temp);
        } else {
            if (curr->next == tail) {
                temp = tail;
                tail = curr;
                tail->next = NULL;
                free(temp);
            } else {
                if (curr->next->num == num) {
                    temp = curr->next;
                    curr->next = curr->next->next;
                    free(temp);
                } else {
                    Del(curr->next, num);
                }
            }
        }
    }
}

int main() {
    int x;
    int y;
    int i;

    scanf("%d", &x);
    scanf("%d", &y);

    Add(1);
    Add(10);
    Add(3);
    Add(4);
    Add(x);

    PrintList(head);
    i = 0;
    while (i < 50000000) {
        Add(i);
        i = i + 1;
    }
    i = 0;
    while (i < 50000000) {
        Del(head, i);
        i = i + 1;
    }
    Del(head, y);
    PrintList(head);
    return 0;
}
