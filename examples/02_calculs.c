int main(void)
{
    unsigned long total = 0;
    int i;

    for (i = 1; i <= 100; i++)
        total += (unsigned long)i;
    return total == 5050UL ? 0 : 1;
}
