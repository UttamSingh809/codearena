#include <stdio.h>
#include <string.h>

int main(void)
{
    char buf[256];
    if (fgets(buf, sizeof(buf), stdin)) {
        size_t n = strlen(buf);
        if (n > 0 && buf[n - 1] == '\n') buf[n - 1] = '\0';
        printf("Hello, %s!\n", buf);
    } else {
        printf("Hello, world!\n");
    }
    return 0;
}