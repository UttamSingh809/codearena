#include <stdio.h>

int main(void)
{
    printf("line on stdout\n");
    fflush(stdout);
    fprintf(stderr, "line on stderr\n");
    fflush(stderr);
    return 0;
}