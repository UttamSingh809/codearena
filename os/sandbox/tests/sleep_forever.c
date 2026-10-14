/*
 * sleep_forever.c — consumes no CPU but never exits.
 * Under the watchdog with timeout_ms=1000, this must be killed
 * after ~1 second (SIGTERM, then SIGKILL after grace).
 */
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    fprintf(stderr, "sleep_forever: entering pause() loop\n");
    for (;;) {
        pause();   /* sleeps until a signal; nothing catches it */
    }
    return 0;
}