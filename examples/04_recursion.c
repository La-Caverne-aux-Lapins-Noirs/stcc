long factorial(int n)
{
    if (n <= 1)
        return 1L;
    return (long)n * factorial(n - 1);
}

int main(void)
{
    return factorial(10) == 3628800L ? 0 : 1;
}
