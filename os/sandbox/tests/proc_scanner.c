#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    printf("proc_scanner: my pid = %d\n", getpid());

    FILE *f = fopen("/proc/1/cmdline", "r");
    if (!f) {
        fprintf(stderr, "proc_scanner: cannot open /proc/1/cmdline: %s\n", strerror(errno));
        return 1;
    }
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    /* cmdline uses \0 as separator; replace for readability. */
    for (size_t i = 0; i < n; i++) if (buf[i] == '\0') buf[i] = ' ';
    printf("proc_scanner: /proc/1/cmdline = %s\n", buf);
    return 0;
}