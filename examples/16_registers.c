int sum(register int count)
{
    int values[4] = {3, 5, 7, 11};
    register(a3) int *cursor = values;
    register(d3) int result = 0;

    while (count--) {
        result += *cursor;
        cursor++;
    }
    __asm__ {
        addi.w  #16,result
    }
    return result;
}

int main(void)
{
    return sum(4); /* 42; normal C calls still pass arguments on the stack. */
}
