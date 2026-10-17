#include "math.h"
int calls;

int square(int x)
{
    calls++;
    return x * x;
}
