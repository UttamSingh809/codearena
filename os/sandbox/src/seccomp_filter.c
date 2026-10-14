/*
 * seccomp_filter.c — seccomp-BPF syscall whitelist for CodeArena.
 *
 * Two whitelists are provided:
 *
 *   SECCOMP_PROFILE_C
 *     The minimal set needed by a statically-linked or dynamically-linked
 *     C/C++ program. Deliberately excludes dangerous syscalls like
 *     ptrace, mount, bpf, userfaultfd, kexec_load, init_module, reboot,
 *     swapon, perf_event_open, and open_by_handle_at.
 *
 *   SECCOMP_PROFILE_PYTHON
 *     Superset of the C profile. Adds futex, clone3, prlimit64,
 *     mmap with MAP_JIT semantics, ioctl, and additional file
 *     descriptors operations needed by CPython's runtime.
 *
 * Any syscall not on the list terminates the process with SIGSYS.
 */

#include "seccomp_filter.h"

#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <seccomp.h>
#include <sys/prctl.h>

/* ------------------------------------------------------------------ */
/* PR_SET_NO_NEW_PRIVS                                                */
/* ------------------------------------------------------------------ */

/*
 * Prevent exec from ever gaining privileges. Required by the kernel
 * before an unprivileged process can install a seccomp filter.
 * (We're usually root inside the sandbox, but this is defensive.)
 */
static int no_new_privs(void)
{
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Whitelist helpers                                                  */
/* ------------------------------------------------------------------ */

/*
 * Add SCMP_SYS(<name>) with SCMP_ACT_ALLOW.
 * Silently ignores syscalls not defined on this architecture — this
 * lets the same whitelist compile on x86_64, aarch64, etc. without
 * #ifdef everywhere.
 */
static void allow(scmp_filter_ctx ctx, int syscall_nr)
{
    if (syscall_nr < 0) return;
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall_nr, 0);
}

#define ALLOW(name) allow(ctx, SCMP_SYS(name))

/* ------------------------------------------------------------------ */
/* Common whitelist (used by both C and Python profiles)              */
/* ------------------------------------------------------------------ */

static void install_common(scmp_filter_ctx ctx)
{
    /* ----- process lifecycle ----- */
    ALLOW(exit);
    ALLOW(exit_group);
    ALLOW(getpid);
    ALLOW(getppid);
    ALLOW(gettid);
    ALLOW(getuid);
    ALLOW(geteuid);
    ALLOW(getgid);
    ALLOW(getegid);
    ALLOW(getresuid);
    ALLOW(getresgid);
    ALLOW(setresuid);
    ALLOW(setresgid);
    ALLOW(setuid);
    ALLOW(setgid);
    ALLOW(setpgid);
    ALLOW(getpgid);
    ALLOW(getpgrp);
    ALLOW(setsid);
    ALLOW(getsid);
    ALLOW(arch_prctl);

    /* ----- memory ----- */
    ALLOW(brk);
    ALLOW(mmap);
    ALLOW(munmap);
    ALLOW(mprotect);
    ALLOW(mremap);
    ALLOW(madvise);
    ALLOW(membarrier);
    ALLOW(mbind);
    ALLOW(set_mempolicy);
    ALLOW(get_mempolicy);

    /* ----- signals ----- */
    ALLOW(rt_sigaction);
    ALLOW(rt_sigprocmask);
    ALLOW(rt_sigreturn);
    ALLOW(rt_sigtimedwait);
    ALLOW(sigaltstack);
    ALLOW(kill);
    ALLOW(tgkill);
    ALLOW(tkill);
    ALLOW(pause);
    ALLOW(restart_syscall);

    /* ----- time ----- */
    ALLOW(nanosleep);
    ALLOW(clock_nanosleep);
    ALLOW(clock_gettime);
    ALLOW(clock_getres);
    ALLOW(gettimeofday);
    ALLOW(time);
    ALLOW(times);

    /* ----- file I/O (open/read/write on files the pivot allows) ----- */
    ALLOW(open);
    ALLOW(openat);
    ALLOW(creat);
    ALLOW(close);
    ALLOW(read);
    ALLOW(write);
    ALLOW(readv);
    ALLOW(writev);
    ALLOW(pread64);
    ALLOW(pwrite64);
    ALLOW(lseek);
    ALLOW(fsync);
    ALLOW(fdatasync);
    ALLOW(ftruncate);
    ALLOW(truncate);
    ALLOW(dup);
    ALLOW(dup2);
    ALLOW(dup3);
    ALLOW(pipe);
    ALLOW(pipe2);
    ALLOW(fcntl);
    ALLOW(flock);
    ALLOW(ioctl);
    ALLOW(fstat);
    ALLOW(newfstatat);
    ALLOW(stat);
    ALLOW(lstat);
    ALLOW(statx);
    ALLOW(access);
    ALLOW(faccessat);
    ALLOW(faccessat2);
    ALLOW(readlink);
    ALLOW(readlinkat);
    ALLOW(getdents);
    ALLOW(getdents64);
    ALLOW(getcwd);
    ALLOW(chdir);
    ALLOW(fchdir);
    ALLOW(umask);
    ALLOW(getrlimit);
    ALLOW(prlimit64);

    /* ----- poll / event ----- */
    ALLOW(poll);
    ALLOW(ppoll);
    ALLOW(select);
    ALLOW(pselect6);
    ALLOW(epoll_create);
    ALLOW(epoll_create1);
    ALLOW(epoll_ctl);
    ALLOW(epoll_wait);
    ALLOW(epoll_pwait);
    ALLOW(eventfd);
    ALLOW(eventfd2);

    /* ----- file system metadata (read-only) ----- */
    ALLOW(getxattr);
    ALLOW(lgetxattr);
    ALLOW(fgetxattr);
    ALLOW(listxattr);
    ALLOW(llistxattr);
    ALLOW(flistxattr);

    /* ----- sysinfo ----- */
    ALLOW(uname);
    ALLOW(sysinfo);
    ALLOW(getrusage);

    /* ----- fork/exec (needed for subprocesses inside sandbox) ----- */
    ALLOW(clone);
    ALLOW(fork);
    ALLOW(vfork);
    ALLOW(execve);
    ALLOW(execveat);
    ALLOW(wait4);
    ALLOW(waitid);

    /* ----- sched ----- */
    ALLOW(sched_yield);
    ALLOW(sched_getaffinity);
    ALLOW(sched_setaffinity);
    ALLOW(sched_getparam);
    ALLOW(sched_getscheduler);
    ALLOW(sched_get_priority_max);
    ALLOW(sched_get_priority_min);
    ALLOW(sched_rr_get_interval);

    /* ----- futex (locks, threads) ----- */
    ALLOW(futex);
    ALLOW(set_robust_list);
    ALLOW(get_robust_list);
    ALLOW(set_tid_address);

    /* ----- random ----- */
    ALLOW(getrandom);

    /* ----- misc syscalls that are harmless ----- */
    ALLOW(sysinfo);
    ALLOW(times);
    ALLOW(umask);
    ALLOW(getpid);
    ALLOW(getppid);
}

/* ------------------------------------------------------------------ */
/* C profile                                                          */
/* ------------------------------------------------------------------ */

static void install_c(scmp_filter_ctx ctx)
{
    install_common(ctx);

    /* C runtime may use these on init/exit */
    ALLOW(prctl);
    ALLOW(arch_prctl);
    ALLOW(set_tid_address);
    ALLOW(rseq);
    ALLOW(membarrier);

    /* Threading via pthreads may use clone3 on modern glibc */
    ALLOW(clone3);

    /* Memory-mapped files (used by glibc for locale data, etc.) */
    ALLOW(mmap);
    ALLOW(munmap);
    ALLOW(mprotect);
    ALLOW(mremap);

    /* Not used by C but harmless; some libc versions probe them */
    ALLOW(sigaltstack);
    ALLOW(rt_sigpending);
    ALLOW(rt_sigsuspend);
    ALLOW(rt_sigtimedwait);

    /* File descriptor duplication and inheritance */
    ALLOW(close_range);
}

/* ------------------------------------------------------------------ */
/* Python profile                                                     */
/* ------------------------------------------------------------------ */

static void install_python(scmp_filter_ctx ctx)
{
    /* Python needs everything the C profile has, plus more. */
    install_c(ctx);

    /* Python's import system opens many files with stat/open/readlink */
    ALLOW(statfs);
    ALLOW(fstatfs);

    /* Python's memory allocator uses mmap with special flags */
    ALLOW(mmap);
    ALLOW(munmap);
    ALLOW(madvise);
    ALLOW(mprotect);

    /* CPython uses ioctl on stdio to detect tty */
    ALLOW(ioctl);

    /* Python subprocess module */
    ALLOW(clone3);
    ALLOW(pidfd_open);

    /* Signal handling */
    ALLOW(rt_sigtimedwait);
    ALLOW(signalfd);
    ALLOW(signalfd4);

    /* Async I/O */
    ALLOW(epoll_create1);
    ALLOW(timerfd_create);
    ALLOW(timerfd_settime);
    ALLOW(timerfd_gettime);

}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

const char *seccomp_profile_name(seccomp_profile_t profile)
{
    switch (profile) {
        case SECCOMP_PROFILE_C:      return "c";
        case SECCOMP_PROFILE_PYTHON: return "python";
        default:                     return "unknown";
    }
}

int seccomp_apply(seccomp_profile_t profile)
{
    if (no_new_privs() < 0) {
        return -1;
    }

    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL_PROCESS);
    if (!ctx) {
        errno = ENOMEM;
        return -1;
    }

    switch (profile) {
        case SECCOMP_PROFILE_C:
            install_c(ctx);
            break;
        case SECCOMP_PROFILE_PYTHON:
            install_python(ctx);
            break;
        default:
            seccomp_release(ctx);
            errno = EINVAL;
            return -1;
    }

    int rc = seccomp_load(ctx);
    seccomp_release(ctx);
    return (rc < 0) ? -1 : 0;
}