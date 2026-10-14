/*
 * namespace.c — namespace isolation for the CodeArena sandbox.
 *
 * Called in the child process after fork() and after dup2(). All
 * operations happen in the child; the parent is unaffected.
 *
 * The tricky part is CLONE_NEWPID: unshare(CLONE_NEWPID) puts the
 * *caller's future children* into the new PID namespace. To become
 * PID 1 ourselves, we must unshare(CLONE_NEWPID) and then fork() once.
 * The parent of that fork exits immediately; the grandchild runs
 * with PID 1 in the new namespace.
 */

#include "namespace.h"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MS_REC
#define MS_REC 16384
#endif

/* ------------------------------------------------------------------ */
/* Utilities                                                          */
/* ------------------------------------------------------------------ */

static int write_file(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t len = strlen(value);
    ssize_t n = write(fd, value, len);
    int saved = errno;
    close(fd);
    if (n < 0 || (size_t)n != len) { errno = saved; return -1; }
    return 0;
}

static void die(const char *what)
{
    int err = errno;
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
                     "sandbox: ns_setup: %s failed: %s\n",
                     what, strerror(err));
    if (n > 0) {
        ssize_t ignore = write(STDERR_FILENO, buf, (size_t)n);
        (void)ignore;
    }
    _exit(125);
}

static int mkdir_p(const char *path, mode_t mode)
{
    char tmp[512];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) { errno = ENAMETOOLONG; return -1; }
    memcpy(tmp, path, len + 1);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) < 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) < 0 && errno != EEXIST) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* User namespace: uid/gid mapping                                    */
/* ------------------------------------------------------------------ */

/*
 * After unshare(CLONE_NEWUSER), the calling process has a full set of
 * capabilities *inside* the new user namespace but is nobody outside.
 * We map our real uid/gid to uid 0 / gid 0 inside, so the child thinks
 * it's root but is unprivileged on the host.
 *
 * IMPORTANT: uid and gid MUST be read from the parent namespace BEFORE
 * unshare(CLONE_NEWUSER). After unshare, getuid() returns the overflow
 * uid (65534) because no mapping exists yet, and the kernel refuses to
 * map a uid we don't actually own in the parent namespace.
 */
static void setup_uid_gid_maps(uid_t real_uid, gid_t real_gid)
{
    /* Disable setgroups before writing gid_map (required since Linux 3.19). */
    if (write_file("/proc/self/setgroups", "deny") < 0) {
        /* Older kernels or restricted envs may not have this file.
         * Only fatal if gid_map write later fails. */
    }

    char buf[64];

    int n = snprintf(buf, sizeof(buf), "0 %u 1\n", (unsigned)real_uid);
    if (n <= 0 || write_file("/proc/self/uid_map", buf) < 0)
        die("write uid_map");

    n = snprintf(buf, sizeof(buf), "0 %u 1\n", (unsigned)real_gid);
    if (n <= 0 || write_file("/proc/self/gid_map", buf) < 0)
        die("write gid_map");
}

/* ------------------------------------------------------------------ */
/* Rootfs preparation                                                 */
/* ------------------------------------------------------------------ */

/*
 * Bind-mount each (host, guest) pair read-only into the rootfs.
 * The guest path is interpreted relative to rootfs.
 *
 * Example: host=/lib/x86_64-linux-gnu/libc.so.6
 *          guest=/lib/libc.so.6
 *
 * Result: rootfs/lib/libc.so.6 becomes a read-only bind of the host libc.
 */
static void bind_mounts(const ns_config_t *cfg)
{
    if (cfg->bind_count == 0) return;

    for (size_t i = 0; i < cfg->bind_count; i++) {
        const char *host  = cfg->bind_host_paths[i];
        const char *guest_rel = cfg->bind_guest_paths[i];

        if (!host || !guest_rel) continue;

        /* Build absolute guest path: <rootfs><guest_rel> */
        char guest_abs[512];
        int n = snprintf(guest_abs, sizeof(guest_abs), "%s%s",
                         cfg->rootfs, guest_rel);
        if (n <= 0 || (size_t)n >= sizeof(guest_abs)) {
            errno = ENAMETOOLONG;
            die("guest path too long");
        }

        /* Ensure the mount point exists (file or dir). */
        struct stat st;
        if (stat(host, &st) < 0) die("stat bind host");

        if (S_ISDIR(st.st_mode)) {
            if (mkdir_p(guest_abs, 0755) < 0) die("mkdir bind dir");
        } else {
            /* For files, ensure parent dir exists, then touch the file. */
            char parent[512];
            memcpy(parent, guest_abs, strlen(guest_abs) + 1);
            char *slash = strrchr(parent, '/');
            if (slash && slash != parent) {
                *slash = '\0';
                if (mkdir_p(parent, 0755) < 0) die("mkdir bind file parent");
            }
            int fd = open(guest_abs, O_CREAT | O_WRONLY | O_CLOEXEC, 0644);
            if (fd < 0) die("touch bind file");
            close(fd);
        }

        /* Bind mount read-only. */
        if (mount(host, guest_abs, NULL, MS_BIND | MS_REC, NULL) < 0)
            die("mount bind");
        if (mount(NULL, guest_abs, NULL,
                  MS_BIND | MS_REMOUNT | MS_RDONLY | MS_REC, NULL) < 0) {
            /* Some kernels don't allow remounting a bind read-only in one
             * shot. Failure here is a warning, not fatal. */
        }
    }
}

/*
 * Pivot into the given rootfs.
 *
 * Standard sequence:
 *   1. bind mount rootfs onto itself (required by pivot_root)
 *   2. mkdir <rootfs>/.oldroot
 *   3. pivot_root(rootfs, rootfs/.oldroot)
 *   4. chdir("/")
 *   5. umount2("/.oldroot", MNT_DETACH)
 *   6. rmdir("/.oldroot")
 */
static void pivot_into_rootfs(const char *rootfs)
{
    if (mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL) < 0)
        die("mount rootfs bind");

    char oldroot[512];
    int n = snprintf(oldroot, sizeof(oldroot), "%s/.oldroot", rootfs);
    if (n <= 0 || (size_t)n >= sizeof(oldroot)) {
        errno = ENAMETOOLONG;
        die("oldroot path too long");
    }

    if (mkdir(oldroot, 0755) < 0 && errno != EEXIST)
        die("mkdir .oldroot");

    if (syscall(SYS_pivot_root, rootfs, oldroot) < 0)
        die("pivot_root");

    if (chdir("/") < 0) die("chdir /");

    if (umount2("/.oldroot", MNT_DETACH) < 0) {
        /* Non-fatal in some environments. */
    }
    rmdir("/.oldroot");   /* may fail; non-fatal */
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

int ns_is_pid1(void) { return getpid() == 1; }

int ns_setup(const ns_config_t *cfg)
{
    if (!cfg) { errno = EINVAL; die("null config"); }

    /* ---------------- User namespace first ---------------- *
     * Only unshare into a user namespace if we are NOT already root.
     * Root already has all capabilities and writing uid_map from a
     * root process fails because the kernel refuses to let a process
     * that already has capabilities map uids it doesn't own.
     *
     * CRITICAL: read the real uid/gid BEFORE the unshare. After
     * unshare(CLONE_NEWUSER) with no mapping yet, getuid() returns
     * 65534 (overflow uid), which the kernel will refuse to map.
     */
    if (cfg->enable_user_ns && geteuid() != 0) {
        uid_t real_uid = getuid();
        gid_t real_gid = getgid();

        if (unshare(CLONE_NEWUSER) < 0) die("unshare(CLONE_NEWUSER)");
        setup_uid_gid_maps(real_uid, real_gid);
    }

    /* ---------------- PID namespace ----------------------- *
     * unshare(CLONE_NEWPID) puts future children in a new PID
     * namespace. To become PID 1 ourselves, we fork once and the
     * parent exits. The grandchild runs in the new namespace.   */
    if (cfg->enable_pid_ns) {
        if (unshare(CLONE_NEWPID) < 0) die("unshare(CLONE_NEWPID)");

        pid_t p = fork();
        if (p < 0) die("fork for PID ns");
        if (p > 0) {
            /* Intermediate parent: wait for the grandchild to exit
             * and propagate its status, then _exit. */
            int status = 0;
            while (waitpid(p, &status, 0) < 0 && errno == EINTR) { /* retry */ }
            if (WIFEXITED(status))   _exit(WEXITSTATUS(status));
            if (WIFSIGNALED(status)) _exit(128 + WTERMSIG(status));
            _exit(0);
        }
        /* We are the grandchild — PID 1 in the new namespace. */
    }

    /* ---------------- Mount namespace ---------------------- */
    if (cfg->enable_mount_ns) {
        if (unshare(CLONE_NEWNS) < 0) die("unshare(CLONE_NEWNS)");
    }

    /* ---------------- Network namespace -------------------- */
    if (cfg->enable_net_ns) {
        if (unshare(CLONE_NEWNET) < 0) die("unshare(CLONE_NEWNET)");
    }

    /* ---------------- UTS namespace ------------------------ */
    if (cfg->enable_uts_ns) {
        if (unshare(CLONE_NEWUTS) < 0) die("unshare(CLONE_NEWUTS)");
        const char *hn = cfg->hostname ? cfg->hostname : "codearena";
        if (sethostname(hn, strlen(hn)) < 0) {
            /* Non-fatal: hostname is cosmetic. */
        }
    }

    /* ---------------- IPC namespace ------------------------ */
    if (cfg->enable_ipc_ns) {
        if (unshare(CLONE_NEWIPC) < 0) die("unshare(CLONE_NEWIPC)");
    }

    /* ---------------- Mount propagation + rootfs ----------- */
    if (cfg->enable_mount_ns) {
        if (mount("", "/", NULL, MS_PRIVATE | MS_REC, NULL) < 0)
            die("mount private");

        if (cfg->rootfs && *cfg->rootfs) {
            bind_mounts(cfg);
            pivot_into_rootfs(cfg->rootfs);
        }
    }

    return 0;
}