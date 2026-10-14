/*
 * cgroup.c — cgroup v2 management for CodeArena sandbox.
 *
 * All paths are constructed as <root>/<name>/<file>. We assume cgroups v2
 * is mounted at /sys/fs/cgroup (verified by scripts/setup.sh). If the
 * kernel doesn't support a specific controller (e.g., io on some systems),
 * writes to its files fail gracefully when the corresponding field is 0.
 */

#include "sandbox.h"
#include "cgroup.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Maximum path length we build (root + name + file). */
#define PATH_MAX_LOCAL 512

/* --------------------------------------------------------------------- */
/* Low-level helpers                                                     */
/* --------------------------------------------------------------------- */

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

static int read_file_u64(const char *path, uint64_t *out)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    /* Some cgroup files return "max" instead of a number. */
    if (buf[0] < '0' || buf[0] > '9') { *out = 0; return 0; }
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(buf, &end, 10);
    if (errno != 0) return -1;
    *out = (uint64_t)v;
    return 0;
}

static int mkdir_if_missing(const char *path)
{
    if (mkdir(path, 0755) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

static int build_path(char *dst, size_t dstsz,
                      const char *root, const char *name, const char *file)
{
    int n;
    if (name && *name)
        n = snprintf(dst, dstsz, "%s/%s/%s", root, name, file);
    else
        n = snprintf(dst, dstsz, "%s/%s", root, file);
    if (n < 0 || (size_t)n >= dstsz) { errno = ENAMETOOLONG; return -1; }
    return 0;
}

/* --------------------------------------------------------------------- */
/* Parent initialization                                                 */
/* --------------------------------------------------------------------- */

int cgroup_init_parent(const char *root)
{
    if (!root) { errno = EINVAL; return -1; }

    if (mkdir_if_missing(root) < 0) return -1;

    /* Enable controllers on the parent's subtree_control. The kernel
     * silently ignores controllers that are already enabled, so we can
     * just write them every time. We must write ONE controller at a time
     * in some kernels; doing them together works on modern kernels. */

    char path[PATH_MAX_LOCAL];
    if (build_path(path, sizeof(path), root, NULL, "cgroup.subtree_control") < 0)
        return -1;

    /* Enable cpu, memory, pids, io (io may fail on some systems —
     * that's fine, memory/cpu/pids are the critical ones). */
    const char *controllers = "+cpu +memory +pids +io";
    if (write_file(path, controllers) < 0) {
        /* Retry with only the essential controllers, in case "+io" is
         * unsupported on this kernel. */
        if (write_file(path, "+cpu +memory +pids") < 0)
            return -1;
    }

    return 0;
}

/* --------------------------------------------------------------------- */
/* Per-submission cgroup                                                 */
/* --------------------------------------------------------------------- */

static int write_cpu_max(const char *root, const char *name, unsigned int pct)
{
    if (pct == 0) return 0;
    char path[PATH_MAX_LOCAL];
    if (build_path(path, sizeof(path), root, name, "cpu.max") < 0) return -1;

    /* period = 100000 µs (100 ms). quota = pct% of one period. */
    unsigned int period = 100000;
    unsigned int quota  = (unsigned int)((uint64_t)period * pct / 100);
    if (quota == 0) quota = 1;

    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%u %u\n", quota, period);
    if (n <= 0) { errno = EINVAL; return -1; }
    return write_file(path, buf);
}

static int write_memory_max(const char *root, const char *name, unsigned int mb)
{
    if (mb == 0) return 0;
    char path[PATH_MAX_LOCAL];
    if (build_path(path, sizeof(path), root, name, "memory.max") < 0) return -1;

    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%llu\n",
                     (unsigned long long)mb * 1024ULL * 1024ULL);
    if (n <= 0) { errno = EINVAL; return -1; }
    return write_file(path, buf);
}

static int write_pids_max(const char *root, const char *name, unsigned int max)
{
    if (max == 0) return 0;
    char path[PATH_MAX_LOCAL];
    if (build_path(path, sizeof(path), root, name, "pids.max") < 0) return -1;

    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%u\n", max);
    if (n <= 0) { errno = EINVAL; return -1; }
    return write_file(path, buf);
}

static int write_io_max(const char *root, const char *name,
                        uint64_t rbps, uint64_t wbps)
{
    if (rbps == 0 && wbps == 0) return 0;

    /* Format: "MAJ:MIN rbps=N wbps=N" for the root device (8:0 on most
     * Linux systems — the first SCSI/SATA/virtio block device). We
     * could enumerate block devices, but for a course project using a
     * single root block device, hardcoding 8:0 is standard practice
     * and what most online judges do. */
    char path[PATH_MAX_LOCAL];
    if (build_path(path, sizeof(path), root, name, "io.max") < 0) return -1;

    char buf[128];
    int n;
    if (rbps && wbps) {
        n = snprintf(buf, sizeof(buf),
                     "8:0 rbps=%llu wbps=%llu\n",
                     (unsigned long long)rbps, (unsigned long long)wbps);
    } else if (rbps) {
        n = snprintf(buf, sizeof(buf),
                     "8:0 rbps=%llu\n", (unsigned long long)rbps);
    } else {
        n = snprintf(buf, sizeof(buf),
                     "8:0 wbps=%llu\n", (unsigned long long)wbps);
    }
    if (n <= 0) { errno = EINVAL; return -1; }

    /* io.max may not exist on all kernels/setups. Non-fatal. */
    if (write_file(path, buf) < 0) {
        /* Silently ignore — not all systems have io controller. */
        return 0;
    }
    return 0;
}

int cgroup_create(const char *root, const char *name, const cgroup_config_t *cfg)
{
    if (!root || !name || !cfg) { errno = EINVAL; return -1; }

    char path[PATH_MAX_LOCAL];
    if (build_path(path, sizeof(path), root, name, "") < 0) return -1;

    /* Remove trailing slash that build_path added. */
    size_t plen = strlen(path);
    if (plen > 0 && path[plen - 1] == '/') path[plen - 1] = '\0';

    if (mkdir_if_missing(path) < 0) return -1;

    if (write_cpu_max(root, name, cfg->cpu_quota_pct) < 0) {
        /* If cpu.max write fails, tear down and report. */
        rmdir(path);
        return -1;
    }
    if (write_memory_max(root, name, cfg->memory_limit_mb) < 0) {
        rmdir(path);
        return -1;
    }
    if (write_pids_max(root, name, cfg->pids_max) < 0) {
        rmdir(path);
        return -1;
    }
    if (write_io_max(root, name, cfg->io_read_bps, cfg->io_write_bps) < 0) {
        /* io is optional — ignore failure */
    }

    return 0;
}

int cgroup_attach_pid(const char *root, const char *name, pid_t pid)
{
    if (!root || !name || pid <= 0) { errno = EINVAL; return -1; }

    char path[PATH_MAX_LOCAL];
    if (build_path(path, sizeof(path), root, name, "cgroup.procs") < 0)
        return -1;

    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%d\n", (int)pid);
    if (n <= 0) { errno = EINVAL; return -1; }

    return write_file(path, buf);
}

int cgroup_read_stats(const char *root, const char *name, cgroup_stats_t *out)
{
    if (!root || !name || !out) { errno = EINVAL; return -1; }

    memset(out, 0, sizeof(*out));

    char path[PATH_MAX_LOCAL];

    /* memory.peak */
    if (build_path(path, sizeof(path), root, name, "memory.peak") == 0) {
        (void)read_file_u64(path, &out->memory_peak_bytes);
    }

    /* pids.peak (may not exist on older kernels) */
    if (build_path(path, sizeof(path), root, name, "pids.peak") == 0) {
        (void)read_file_u64(path, &out->pids_peak);
    }

    /* cpu.stat is multi-line: "usage_usec N\nuser_usec N\n..." */
    if (build_path(path, sizeof(path), root, name, "cpu.stat") == 0) {
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            char buf[512];
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) {
                buf[n] = '\0';
                const char *key = "usage_usec";
                const char *p = strstr(buf, key);
                if (p) {
                    p += strlen(key);
                    while (*p == ' ' || *p == '\t') p++;
                    out->cpu_usage_usec = strtoull(p, NULL, 10);
                }
            }
        }
    }

    return 0;
}

int cgroup_destroy(const char *root, const char *name)
{
    if (!root || !name) { errno = EINVAL; return -1; }

    char dir[PATH_MAX_LOCAL];
    if (build_path(dir, sizeof(dir), root, name, "") < 0) return -1;

    size_t plen = strlen(dir);
    if (plen > 0 && dir[plen - 1] == '/') dir[plen - 1] = '\0';

    /* First attempt: rmdir directly. */
    if (rmdir(dir) == 0) return 0;
    if (errno != EBUSY && errno != ENOTEMPTY) return -1;

    /* Second attempt: cgroup still has processes. Kill them all. */
    char procs_path[PATH_MAX_LOCAL];
    if (build_path(procs_path, sizeof(procs_path), root, name, "cgroup.procs") < 0)
        return -1;

    int fd = open(procs_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;

    char buf[512];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        /* No processes listed; retry rmdir once more. */
        return rmdir(dir);
    }
    buf[n] = '\0';

    /* Parse PIDs line by line and SIGKILL each. */
    char *saveptr = NULL;
    for (char *line = strtok_r(buf, "\n", &saveptr);
         line;
         line = strtok_r(NULL, "\n", &saveptr))
    {
        pid_t p = (pid_t)atoi(line);
        if (p > 0) kill(p, SIGKILL);
    }

    /* Brief pause to let the kernel reap them. Then retry. */
    usleep(20000);  /* 20 ms */

    if (rmdir(dir) == 0) return 0;
    return -1;
}