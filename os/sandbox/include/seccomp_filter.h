#ifndef CODEARENA_SECCOMP_FILTER_H
#define CODEARENA_SECCOMP_FILTER_H

/*
 * CodeArena — seccomp-BPF syscall filtering
 *
 * Loads a whitelist filter into the calling process. After this call,
 * any syscall not on the whitelist triggers SCMP_ACT_KILL_PROCESS:
 * the kernel sends SIGSYS and terminates the process before the
 * syscall executes.
 *
 * Must be called AFTER namespaces (Step 5) and BEFORE execve().
 *
 * Two profiles are provided:
 *   SECCOMP_PROFILE_C        — C/C++ workloads (compiled submissions)
 *   SECCOMP_PROFILE_PYTHON   — Python interpreter workloads
 *
 * The C profile is the default and the most restrictive. The Python
 * profile allows additional syscalls needed by CPython (mmap variants,
 * futex, clone3, ioctl on tty, etc.).
 */

typedef enum {
    SECCOMP_PROFILE_C = 0,
    SECCOMP_PROFILE_PYTHON = 1,
} seccomp_profile_t;

/*
 * Load the seccomp filter into the current process.
 *
 * On success, returns 0. All subsequent syscalls in this process and
 * its children are filtered against the whitelist.
 *
 * On failure, returns -1 and sets errno:
 *   EINVAL   — bad profile
 *   ENOMEM   — libseccomp could not allocate
 *   EACCES   — kernel refused (usually PR_SET_NO_NEW_PRIVS missing)
 *
 * Diagnostic output: on failure, libseccomp prints the offending rule
 * number to stderr.
 */
int seccomp_apply(seccomp_profile_t profile);

/*
 * Return a human-readable name for a profile.
 */
const char *seccomp_profile_name(seccomp_profile_t profile);

#endif /* CODEARENA_SECCOMP_FILTER_H */