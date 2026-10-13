#include <stdio.h>

int main(void)
{
    volatile unsigned long x = 0;
    while (1) {
        x++;
        if ((x & 0xFFFFFFFUL) == 0) {
            fprintf(stderr, "cpu_hog: iteration %lu\n", x);
        }
    }
    return 0;
}