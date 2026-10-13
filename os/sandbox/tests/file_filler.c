#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/tmp/codearena_file_filler_test.dat";
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("file_filler: fopen");
        return 1;
    }

    char buf[4096];
    memset(buf, 'A', sizeof(buf));

    size_t total  = 0;
    const size_t target = (size_t)1024 * 1024 * 1024;   /* 1 GB */

    while (total < target) {
        size_t n = fwrite(buf, 1, sizeof(buf), f);
        if (n != sizeof(buf)) {
            fprintf(stderr,
                    "file_filler: fwrite returned %zu after %zu bytes (errno=%d %s)\n",
                    n, total, errno, strerror(errno));
            fflush(f);
            fclose(f);
            return 1;
        }
        total += n;
    }

    fflush(f);
    fclose(f);
    fprintf(stderr, "file_filler: wrote %zu bytes\n", total);
    return 0;
}