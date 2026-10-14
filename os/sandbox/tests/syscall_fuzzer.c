/*
 * syscall_fuzzer.c — invoke a selection of dangerous syscalls and
 * report the result of each. Under seccomp, the FIRST blocked syscall
 * terminates the process with SIGSYS.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static void try_syscall(const char *name, long nr)
{
    errno = 0;
    long rc = syscall(nr);
    printf("%-20s nr=%-4ld rc=%ld errno=%d (%s)\n",
           name, nr, rc, errno, strerror(errno));
    fflush(stdout);
}

int main(void)
{
    /* These are all syscalls we want BLOCKED. Under seccomp, the
     * process will die at the first one that fires. */
    try_syscall("ptrace",         SYS_ptrace);
    try_syscall("mount",          SYS_mount);
    try_syscall("umount2",        SYS_umount2);
    try_syscall("reboot",         SYS_reboot);
    try_syscall("kexec_load",     SYS_kexec_load);
    try_syscall("init_module",    SYS_init_module);
    try_syscall("bpf",            SYS_bpf);
    try_syscall("userfaultfd",    SYS_userfaultfd);
    try_syscall("perf_event_open",SYS_perf_event_open);
    try_syscall("open_by_handle_at", SYS_open_by_handle_at);

    printf("syscall_fuzzer: survived all calls (seccomp did NOT block)\n");
    return 0;
}