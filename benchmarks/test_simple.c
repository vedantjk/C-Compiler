int main()
{
    int x = 0;

    // break in a while loop
    while (1) {
        x = x + 1;
        if (x > 10) {
            break;
        }
    }

    // continue in a while loop
    int y = 0;
    while (y < 10) {
        y = y + 1;
        if (y == 5) {
            continue;
        }
        x = x + y;
    }

    // break + continue together in a for loop
    for (int i = 0; i < 20; i = i + 1) {
        if (i == 3) {
            continue;
        }
        if (i == 15) {
            break;
        }
        x = x + i;
    }

    // nested loops — inner break shouldn't affect outer
    for (int a = 0; a < 5; a = a + 1) {
        for (int b = 0; b < 5; b = b + 1) {
            if (b == 2) {
                break;
            }
            x = x + 1;
        }
    }

    // break/continue at the very top of a loop body
    while (x > 0) {
        break;
    }
    while (x < 100) {
        x = x + 1;
        continue;
    }

    return x;
}
