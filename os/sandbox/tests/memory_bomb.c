#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    /* Target in MB; default 1024 MB. */
    size_t target_mb = (argc > 1) ? (size_t)strtoul(argv[1], NULL, 10) : 1024;

    const size_t chunk = 1 * 1024 * 1024;
    size_t total = 0;

    fprintf(stderr, "memory_bomb: target %zu MB\n", target_mb);

    while (total < target_mb * 1024 * 1024) {
        char *p = malloc(chunk);
        if (!p) {
            fprintf(stderr, "memory_bomb: malloc failed at %zu MB\n", total / (1024 * 1024));
            return 1;
        }
        /* Touch the memory so it becomes resident (not just virtual). */
        memset(p, 0xAB, chunk);
        total += chunk;

        if ((total / chunk) % 64 == 0) {
            fprintf(stderr, "memory_bomb: allocated %zu MB\n", total / (1024 * 1024));
        }
    }

    fprintf(stderr, "memory_bomb: finished allocating %zu MB\n", total / (1024 * 1024));
    return 0;
}