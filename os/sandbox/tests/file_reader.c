#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    FILE *f = fopen("/etc/passwd", "r");
    if (!f) {
        fprintf(stderr, "file_reader: cannot open /etc/passwd: %s\n", strerror(errno));
        return 1;
    }
    char buf[256];
    if (fgets(buf, sizeof(buf), f)) {
        printf("file_reader: first line: %s", buf);
    }
    fclose(f);
    return 0;
}