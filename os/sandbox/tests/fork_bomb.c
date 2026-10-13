#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_CHILDREN 256

int main(void)
{
    if (geteuid() == 0) {
        fprintf(stderr, "fork_bomb: refusing to run as root "
                        "(RLIMIT_NPROC has no effect on root)\n");
        return 0;
    }

    pid_t pids[MAX_CHILDREN];
    int   n = 0;

    while (n < MAX_CHILDREN) {
        pid_t p = fork();
        if (p < 0) {
            fprintf(stderr, "fork_bomb: fork() failed after %d children: %s\n",
                    n, strerror(errno));
            break;
        }
        if (p == 0) {
            /* Child: idle until killed by parent or by sandbox teardown. */
            sleep(60);
            _exit(0);
        }
        pids[n++] = p;
    }

    fprintf(stderr, "fork_bomb: spawned %d children before stopping\n", n);

    for (int i = 0; i < n; i++) kill(pids[i], SIGKILL);
    for (int i = 0; i < n; i++) waitpid(pids[i], NULL, 0);

    return (n >= MAX_CHILDREN) ? 1 : 0;
}