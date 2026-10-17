#include "math.h"
int main(void)
{
    int a = square(7);
    int b = square(9);

    return a == 49 && b == 81 && calls == 2 ? 0 : 1;
}
