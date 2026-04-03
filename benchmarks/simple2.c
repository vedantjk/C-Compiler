struct Point2D {
    int x;
    int y;
};

int globalInit;

struct Point2D *Init(int initVal) {
    struct Point2D *newPt;
    newPt = NULL;
    if (initVal > 0) {
        newPt = (struct Point2D *)malloc(sizeof(struct Point2D));
        newPt->x = initVal;
        newPt->y = initVal;
        return newPt;
    }
    return newPt;
}

int main() {
    int a;
    int b;
    struct Point2D *pt1;
    struct Point2D *pt2;
    a = 5;
    b = (a + 7) * 3;
    pt1 = (struct Point2D *)malloc(sizeof(struct Point2D));
    pt1->x = a;
    pt1->y = b;
    scanf("%d", &globalInit);
    pt2 = Init(globalInit);
    printf("offset=%d\npt2.x=%d\npt2.y=%d\n", globalInit, pt2->x, pt2->y);
    free(pt1);
    free(pt2);
    return 0;
}
