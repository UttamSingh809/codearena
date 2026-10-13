#include <stdio.h>

int main(void)
{
    /* Write ~4 MB to stdout — far more than our typical 64 KB limit. */
    for (int i = 0; i < 65536; i++) {
        fputs("0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF\n", stdout);
    }
    return 0;
}