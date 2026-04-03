struct Node {
    int value;
    struct Node *left;
    struct Node *right;
};

struct Node *root;

int compare(int cur, int neuw) {
    if (cur < neuw) {
        return 1;
    } else {
        if (cur > neuw) {
            return -1;
        } else {
            return 0;
        }
    }
}

void addNode(int numAdd, struct Node *curr) {
    int compVal;
    struct Node *newNode;

    if (curr == NULL) {
        newNode = (struct Node *)malloc(sizeof(struct Node));
        newNode->value = numAdd;
        newNode->left = NULL;
        newNode->right = NULL;
        root = newNode;
    } else {
        compVal = compare(curr->value, numAdd);

        if (compVal == -1) {
            if (curr->left == NULL) {
                newNode = (struct Node *)malloc(sizeof(struct Node));
                newNode->value = numAdd;
                newNode->left = NULL;
                newNode->right = NULL;
                curr->left = newNode;
            } else {
                addNode(numAdd, curr->left);
            }
        } else {
            if (compVal == 1) {
                if (curr->right == NULL) {
                    newNode = (struct Node *)malloc(sizeof(struct Node));
                    newNode->value = numAdd;
                    newNode->left = NULL;
                    newNode->right = NULL;
                    curr->right = newNode;
                } else {
                    addNode(numAdd, curr->right);
                }
            }
        }
    }
}

void printDepthTree(struct Node *curr) {
    int temp;
    if (curr != NULL) {
        if (curr->left != NULL) {
            printDepthTree(curr->left);
        }
        temp = curr->value;
        printf("%d\n", temp);

        if (curr->right != NULL) {
            printDepthTree(curr->right);
        }
    }
}

void deleteLeavesTree(struct Node *curr) {
    if (curr != NULL) {
        if (curr->left != NULL) {
            deleteLeavesTree(curr->left);
        }
        if (curr->right != NULL) {
            deleteLeavesTree(curr->right);
        }
        free(curr);
    }
}

int main() {
    int input;
    int temp;

    root = NULL;
    input = 0;

    scanf("%d", &input);

    while (input != 0) {
        addNode(input, root);
        scanf("%d", &input);
    }

    printDepthTree(root);
    deleteLeavesTree(root);
    return 0;
}
