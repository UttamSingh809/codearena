#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static void show(const char *label, const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        printf("%-8s inode=%llu\n", label, (unsigned long long)st.st_ino);
    } else {
        printf("%-8s <missing>\n", label);
    }
}

int main(void)
{
    printf("pid  = %d\n", getpid());
    show("pid",  "/proc/self/ns/pid");
    show("mnt",  "/proc/self/ns/mnt");
    show("net",  "/proc/self/ns/net");
    show("user", "/proc/self/ns/user");
    show("uts",  "/proc/self/ns/uts");
    show("ipc",  "/proc/self/ns/ipc");
    return 0;
}