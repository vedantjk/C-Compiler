int x;
int a = 0;
int m, n, o;
char c;
int *p;
int **pp;
int a[10];
int m[3][4];
int *a[10];
int x = 1, y = 2;
int *foo() { return 0; }
int **bar() { return 0; }

struct Point {
    int x;
    int y;
};

struct Node {
    int data;
    struct Node *next;
};

struct Mixed {
    int a, b;
    char *name;
    int grid[3][4];
    struct Point origin;
    struct Point *cursor;
};

struct Point origin;
struct Point *cursor;
struct Point path[100];

struct Point getOrigin() { return 0; }
struct Point *findPoint() { return 0; }

void translate(struct Point p, int dx) { }
void scale(struct Point *p, int k) { }

void main() {
    struct Point p;
    struct Point *q;
    p.x = 1;
    p.y = 2;
    q->x = 3;
}