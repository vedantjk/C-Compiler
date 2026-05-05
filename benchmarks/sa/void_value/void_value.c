void doThing() {
    return;
}

int main() {
    int x;
    x = doThing();
    if (doThing()) {
        x = 0;
    }
    x = doThing() + 1;
    return 0;
}
