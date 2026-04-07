int print(){
    return 0;
}

int main()
{
    int x = 5;
    int y = 10;
    int z = (x + y) * 2 - x / (y - 3);
    print();
    int a = print();
    if (x > 0 && y <= 10)
    {
        int w = z + 1;
        if (w > 20)
        {
            printf("%d\n", w);
        }
        return w;
    }
    return z;
}