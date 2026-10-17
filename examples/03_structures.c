struct Point {
    int x;
    int y;
};

int main(void)
{
    struct Point points[3] = {{1, 2}, {3, 4}, {5, 6}};
    struct Point *p = points;
    int total = 0;
    int i;

    for (i = 0; i < 3; i++, p++)
        total += p->x + p->y;
    return total == 21 ? 0 : 1;
}
