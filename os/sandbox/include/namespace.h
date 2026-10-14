#ifndef CODEARENA_NAMESPACE_H
#define CODEARENA_NAMESPACE_H

/*
 * CodeArena — namespace isolation
 *
 * Sets up PID, mount, network, user, UTS, and IPC namespaces for
 * the sandboxed child. Must be called from the child process between
 * fork() and execve(), AFTER dup2() and BEFORE seccomp (Step 6).
 *
 * Order of operations inside ns_setup():
 *   1. unshare(CLONE_NEWUSER)      — gain capabilities in new user ns
 *   2. write uid_map / gid_map     — map container root to caller's uid
 *   3. setgroups("deny")           — required before gid_map on most kernels
 *   4. unshare(CLONE_NEWPID)       — new PID namespace (affects children)
 *   5. unshare(CLONE_NEWNS)        — new mount namespace
 *   6. unshare(CLONE_NEWNET)       — new network namespace (optional)
 *   7. unshare(CLONE_NEWUTS)       — new UTS namespace
 *   8. unshare(CLONE_NEWIPC)       — new IPC namespace
 *   9. mount("", "/", MS_PRIVATE | MS_REC)
 *  10. bind-mount rootfs and runtime files read-only
 *  11. pivot_root into rootfs
 *  12. chdir("/")
 *
 * Note: unshare(CLONE_NEWPID) only affects children created AFTER the
 * call. The calling process stays in the old PID namespace. This means
 * the child needs an extra fork() to actually be PID 1 in the new ns.
 * ns_setup() handles this internally and returns only in the grandchild
 * that is now PID 1 of the new namespace.
 */

#include <stddef.h>
#include <sys/types.h>

typedef struct {
    int         enable_pid_ns;      /* 1 = isolate PIDs                          */
    int         enable_mount_ns;    /* 1 = isolate filesystem                    */
    int         enable_net_ns;      /* 1 = isolate network (0 = keep host net)   */
    int         enable_user_ns;     /* 1 = map root to caller                    */
    int         enable_uts_ns;      /* 1 = isolate hostname                      */
    int         enable_ipc_ns;      /* 1 = isolate SysV IPC                      */

    const char *rootfs;             /* path to minimal rootfs (may be NULL)      */
    const char *hostname;           /* inside UTS ns (default "codearena")       */

    /* Bind mounts to make available inside rootfs (host_path -> guest_path).
     * Both must be absolute. Mounted read-only. */
    const char *const *bind_host_paths;
    const char *const *bind_guest_paths;
    size_t             bind_count;
} ns_config_t;

/*
 * Apply namespaces in the calling process (called in the child after
 * fork). On success this function RETURNS ONLY IN THE CHILD PROCESS
 * THAT IS NOW PID 1 IN THE NEW NAMESPACE. The intermediate process
 * exits with status 0 after reaping.
 *
 * Returns 0 on success (in the grandchild). On failure, prints a
 * diagnostic to stderr and _exit(125) — never returns.
 */
int ns_setup(const ns_config_t *cfg);

/*
 * Read-only helper: returns 1 if the current process is PID 1 in its
 * namespace, 0 otherwise. Useful for debugging.
 */
int ns_is_pid1(void);

#endif /* CODEARENA_NAMESPACE_H */