int main()
{
    int x = 5;
    int y = 10;
    int z = (x + y) * 2 - x / (y - 3);
    if (x > 0 && y <= 10)
    {
        int w = z + 1;
        return w;
    }else {
        return x;
    }
    return z;
}