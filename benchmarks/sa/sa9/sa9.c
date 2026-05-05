struct C {
    int z;
};
struct B {
    int y;
    struct C *c;
};
struct A {
    int x;
    struct B *b;
};

int main() {
    struct A *a;
    int i;
    a->b->c->t = 3;
    a->b->c->z->d = 1;
    a->b->c->d->e->f = 1;
    i = a->b->c->t;
    i = a->b->c->d->e->f;
    return 0;
}
