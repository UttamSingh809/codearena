/*
 * main.c — CLI front-end for the CodeArena sandbox.
 *
 * This file is the user-facing entry point for the sandbox binary.
 * It parses command-line options, builds a sandbox_config_t, calls
 * sandbox_run(), runs the verdict engine, and prints a formatted
 * report of the results.
 *
 * Scope as of Step 7:
 *   - Execution:  binary path, stdin, output limit, wall-clock timeout
 *   - rlimits:    CPU seconds, file size, process count, stack, NOFILE
 *   - cgroups:    cpu.max, memory.max, pids.max via /sys/fs/cgroup
 *   - Namespaces: PID, mount, network, user, UTS, IPC
 *   - Rootfs:     pivot_root into a minimal filesystem with bind mounts
 *   - Seccomp:    whitelist-based syscall filter (C or Python profile)
 *   - Watchdog:   wall-clock timeout via SIGTERM→SIGKILL escalation
 *   - Verdict:    verdict engine combining all signals into AC/WA/TLE/...
 *
 * Examples:
 *   ./sandbox --binary ./tests/hello --input "codearena"
 *   ./sandbox --binary ./tests/cpu_hog --cpu-seconds 2
 *   ./sandbox --binary ./tests/memory_bomb --cgroup-memory 64
 *   ./sandbox --binary ./tests/show_ns --no-cgroup --all-ns
 *   ./sandbox --binary ./tests/hello --no-cgroup --all-ns --seccomp
 *   ./sandbox --binary ./tests/sleep_forever --time 1000
 *   ./sandbox --binary ./tests/hello --input "codearena" \
 *       --expected-file /tmp/expected.txt
 *   ./sandbox --binary /bin/hello \
 *       --no-cgroup --user-ns --mnt-ns --pid-ns --seccomp \
 *       --rootfs "$PWD/rootfs" \
 *       --bind "$PWD/tests/hello:/bin/hello" \
 *       --bind /usr/lib/x86_64-linux-gnu/libc.so.6:/lib/libc.so.6 \
 *       --bind /lib64/ld-linux-x86-64.so.2:/lib64/ld-linux-x86-64.so.2
 */

#include "sandbox.h"
#include "cgroup.h"
#include "watchdog.h"
#include "verdict.h"

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ===================================================================== */
/* Helpers                                                               */
/* ===================================================================== */

/*
 * Build a unique cgroup name from the current PID and wall time.
 *
 * Using both PID and time ensures:
 *   - No collision with concurrent sandbox invocations (different PIDs).
 *   - No collision with rapid sequential invocations (different times).
 *   - Names remain valid as cgroup directory names (alnum + underscore).
 */
static void default_cgroup_name(char *dst, size_t dstsz)
{
    snprintf(dst, dstsz, "sandbox_%d_%ld",
             (int)getpid(), (long)time(NULL));
}

/*
 * Print the usage message.
 *
 * Organized by feature category so users can find the flag they need:
 *   Execution  — basic run parameters
 *   rlimits    — resource limits enforced by setrlimit()
 *   cgroups    — resource limits enforced by cgroup v2 filesystem
 *   Namespaces — isolation boundaries created by unshare()
 *   Seccomp    — syscall filtering via BPF whitelist
 *   Watchdog   — wall-clock timeout and verdict determination
 */
static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s --binary PATH [options]\n"
        "\n"
        "Execution:\n"
        "  -b, --binary PATH           path to the program to run (required)\n"
        "  -i, --input STRING          stdin supplied to the program\n"
        "  -f, --input-file PATH       read stdin from a file instead\n"
        "  -o, --output-limit BYTES    cap per stream (default 65536)\n"
        "  -t, --time MS               wall-clock timeout in ms (default 2000)\n"
        "\n"
        "rlimits (step 3):\n"
        "  -c, --cpu-seconds N         RLIMIT_CPU hard limit (default 2)\n"
        "  -s, --max-file-size BYTES   RLIMIT_FSIZE (default 1048576)\n"
        "  -p, --max-pids N            RLIMIT_NPROC (default 64)\n"
        "  -S, --stack-size BYTES      RLIMIT_STACK (default 8388608)\n"
        "  -n, --max-open-files N      RLIMIT_NOFILE (default 64)\n"
        "\n"
        "cgroups v2 (step 4):\n"
        "      --cgroup-root PATH      cgroup root (default /sys/fs/cgroup/codearena)\n"
        "      --cgroup-name NAME      unique cgroup name (auto if omitted)\n"
        "      --cgroup-cpu PCT        cpu.max as %% of one core (0 = none)\n"
        "      --cgroup-memory MB      memory.max in MB (0 = none)\n"
        "      --cgroup-pids N         pids.max (0 = none)\n"
        "      --no-cgroup             disable cgroup usage entirely\n"
        "\n"
        "Namespaces (step 5):\n"
        "      --pid-ns                new PID namespace\n"
        "      --mnt-ns                new mount namespace\n"
        "      --net-ns                new network namespace\n"
        "      --user-ns               new user namespace (uid/gid mapped)\n"
        "      --uts-ns                new UTS namespace (hostname)\n"
        "      --ipc-ns                new IPC namespace\n"
        "      --all-ns                enable all of the above\n"
        "      --rootfs PATH           pivot_root into PATH inside mnt ns\n"
        "      --hostname NAME         hostname inside UTS ns (default codearena)\n"
        "      --bind HOST:GUEST       bind-mount HOST read-only at GUEST inside rootfs\n"
        "                              (repeatable, up to 32 times)\n"
        "\n"
        "Seccomp (step 6):\n"
        "      --seccomp               enable seccomp-BPF syscall whitelist\n"
        "      --seccomp-profile NAME  c (default) or python\n"
        "\n"
        "Watchdog & verdict (step 7):\n"
        "      --grace-ms MS           SIGTERM→SIGKILL grace period (default 500)\n"
        "      --expected-file PATH    expected output file for verdict determination\n"
        "      --strict-ws             require byte-exact output (no whitespace trim)\n"
        "\n"
        "  -h, --help                  show this help\n",
        prog);
}

/*
 * Read an entire file into a heap-allocated NUL-terminated buffer.
 * Returns NULL on failure. The caller owns the buffer.
 */
static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    *out_len = n;
    return buf;
}

/*
 * Print a one-line summary of which namespaces were enabled.
 * Used in the result report so the caller knows what was actually
 * enforced (as opposed to what was requested but silently dropped).
 */
static void print_ns_summary(const sandbox_config_t *cfg)
{
    printf("namespaces:         ");
    if (cfg->enable_pid_ns)   printf("pid ");
    if (cfg->enable_mount_ns) printf("mnt ");
    if (cfg->enable_net_ns)   printf("net ");
    if (cfg->enable_user_ns)  printf("user ");
    if (cfg->enable_uts_ns)   printf("uts ");
    if (cfg->enable_ipc_ns)   printf("ipc ");
    if (!cfg->enable_pid_ns && !cfg->enable_mount_ns && !cfg->enable_net_ns &&
        !cfg->enable_user_ns && !cfg->enable_uts_ns && !cfg->enable_ipc_ns)
        printf("(none)");
    printf("\n");
}

/*
 * Print a one-line summary of which resource limits were configured.
 * Mirrors print_ns_summary() — shows effective configuration, not
 * requested configuration.
 */
static void print_limits_summary(const sandbox_config_t *cfg)
{
    printf("limits:             ");
    if (cfg->cpu_seconds > 0)          printf("cpu=%us ",        cfg->cpu_seconds);
    if (cfg->max_file_size > 0)        printf("fsize=%zuB ",     cfg->max_file_size);
    if (cfg->max_pids > 0)             printf("pids=%u ",        cfg->max_pids);
    if (cfg->cgroup_memory_mb > 0)     printf("mem=%uMB ",       cfg->cgroup_memory_mb);
    if (cfg->cgroup_cpu_pct > 0)       printf("cpu_rate=%u%% ",  cfg->cgroup_cpu_pct);
    if (cfg->cgroup_pids_max > 0)      printf("cg_pids=%u ",     cfg->cgroup_pids_max);
    printf("\n");
}

/*
 * Print which isolation layers are active in one line. Used for the
 * final report so the operator can see the defense-in-depth state at
 * a glance.
 */
static void print_isolation_summary(const sandbox_config_t *cfg)
{
    printf("isolation:          ");
    if (cfg->enable_seccomp) printf("seccomp(%s) ",
                                    cfg->seccomp_profile == 1 ? "python" : "c");
    if (cfg->cgroup_root && cfg->cgroup_name) printf("cgroup ");
    if (cfg->enable_user_ns)  printf("userns ");
    if (cfg->enable_mount_ns) printf("mntns ");
    if (cfg->enable_pid_ns)   printf("pidns ");
    if (cfg->enable_net_ns)   printf("netns ");
    if (cfg->enable_uts_ns)   printf("utsns ");
    if (cfg->enable_ipc_ns)   printf("ipcns ");
    if (!cfg->enable_seccomp && !cfg->cgroup_root && !cfg->enable_user_ns &&
        !cfg->enable_mount_ns && !cfg->enable_pid_ns && !cfg->enable_net_ns)
        printf("(minimal)");
    printf("\n");
}

/* ===================================================================== */
/* main()                                                                */
/* ===================================================================== */

int main(int argc, char **argv)
{
    /* ---------------- Execution parameters ---------------- */
    const char  *binary       = NULL;
    const char  *input_str    = NULL;
    const char  *input_file   = NULL;
    size_t       output_limit = 64 * 1024;         /* 64 KB */
    unsigned int timeout_ms   = 2000;

    /* ---------------- rlimits (step 3) -------------------- */
    unsigned int cpu_seconds    = 2;
    size_t       max_file_size  = 1024 * 1024;     /* 1 MB */
    unsigned int max_pids       = 64;
    size_t       stack_size     = 8 * 1024 * 1024; /* 8 MB */
    unsigned int max_open_files = 64;

    /* ---------------- cgroups (step 4) -------------------- */
    const char  *cgroup_root = "/sys/fs/cgroup/codearena";
    char         cgroup_name_buf[128] = {0};
    const char  *cgroup_name = NULL;
    unsigned int cgroup_cpu    = 0;
    unsigned int cgroup_memory = 0;
    unsigned int cgroup_pids   = 0;
    int          use_cgroup    = 1;

    /* ---------------- Namespaces (step 5) ----------------- */
    int          enable_pid_ns   = 0;
    int          enable_mount_ns = 0;
    int          enable_net_ns   = 0;
    int          enable_user_ns  = 0;
    int          enable_uts_ns   = 0;
    int          enable_ipc_ns   = 0;
    const char  *rootfs          = NULL;
    const char  *hostname        = "codearena";

    /*
     * Bind-mount list: --bind HOST:GUEST is repeatable up to 32 times.
     * We use static arrays because the strings must remain valid for the
     * lifetime of sandbox_run(), and the arrays need to be passed to
     * sandbox_config_t as const char* const*.
     *
     * The host string is strdup'ed (so we own it and must free it).
     * The guest string points into the same strdup'ed buffer, so it
     * doesn't need separate freeing.
     */
    static const char *bind_hosts[32];
    static const char *bind_guests[32];
    size_t             bind_count = 0;

    /* ---------------- Seccomp (step 6) -------------------- */
    int          enable_seccomp     = 0;
    int          seccomp_profile_id = 0;   /* 0 = C, 1 = Python */

    /* ---------------- Watchdog & verdict (step 7) --------- */
    unsigned int grace_ms      = 500;
    const char  *expected_file = NULL;
    char        *expected_buf  = NULL;
    size_t       expected_len  = 0;
    int          strict_ws     = 0;

    /* ---------------- Option parsing ---------------------- */

    /*
     * Option codes at or above 1000 are used for long-only options.
     * This avoids clashes with single-character codes returned by getopt
     * for short options.
     */
    enum {
        OPT_CGROUP_ROOT = 1000,
        OPT_CGROUP_NAME,
        OPT_CGROUP_CPU,
        OPT_CGROUP_MEM,
        OPT_CGROUP_PIDS,
        OPT_NO_CGROUP,
        OPT_PID_NS,
        OPT_MNT_NS,
        OPT_NET_NS,
        OPT_USER_NS,
        OPT_UTS_NS,
        OPT_IPC_NS,
        OPT_ALL_NS,
        OPT_ROOTFS,
        OPT_HOSTNAME,
        OPT_BIND,
        OPT_SECCOMP,
        OPT_SECCOMP_PROFILE,
        OPT_GRACE_MS,
        OPT_EXPECTED_FILE,
        OPT_STRICT_WS
    };

    static struct option long_opts[] = {
        /* Execution */
        { "binary",          required_argument, NULL, 'b' },
        { "input",           required_argument, NULL, 'i' },
        { "input-file",      required_argument, NULL, 'f' },
        { "output-limit",    required_argument, NULL, 'o' },
        { "time",            required_argument, NULL, 't' },
        /* rlimits */
        { "cpu-seconds",     required_argument, NULL, 'c' },
        { "max-file-size",   required_argument, NULL, 's' },
        { "max-pids",        required_argument, NULL, 'p' },
        { "stack-size",      required_argument, NULL, 'S' },
        { "max-open-files",  required_argument, NULL, 'n' },
        /* cgroups */
        { "cgroup-root",     required_argument, NULL, OPT_CGROUP_ROOT },
        { "cgroup-name",     required_argument, NULL, OPT_CGROUP_NAME },
        { "cgroup-cpu",      required_argument, NULL, OPT_CGROUP_CPU },
        { "cgroup-memory",   required_argument, NULL, OPT_CGROUP_MEM },
        { "cgroup-pids",     required_argument, NULL, OPT_CGROUP_PIDS },
        { "no-cgroup",       no_argument,       NULL, OPT_NO_CGROUP },
        /* namespaces */
        { "pid-ns",          no_argument,       NULL, OPT_PID_NS },
        { "mnt-ns",          no_argument,       NULL, OPT_MNT_NS },
        { "net-ns",          no_argument,       NULL, OPT_NET_NS },
        { "user-ns",         no_argument,       NULL, OPT_USER_NS },
        { "uts-ns",          no_argument,       NULL, OPT_UTS_NS },
        { "ipc-ns",          no_argument,       NULL, OPT_IPC_NS },
        { "all-ns",          no_argument,       NULL, OPT_ALL_NS },
        { "rootfs",          required_argument, NULL, OPT_ROOTFS },
        { "hostname",        required_argument, NULL, OPT_HOSTNAME },
        { "bind",            required_argument, NULL, OPT_BIND },
        /* seccomp */
        { "seccomp",         no_argument,       NULL, OPT_SECCOMP },
        { "seccomp-profile", required_argument, NULL, OPT_SECCOMP_PROFILE },
        /* watchdog & verdict */
        { "grace-ms",        required_argument, NULL, OPT_GRACE_MS },
        { "expected-file",   required_argument, NULL, OPT_EXPECTED_FILE },
        { "strict-ws",       no_argument,       NULL, OPT_STRICT_WS },
        /* help */
        { "help",            no_argument,       NULL, 'h' },
        { 0, 0, 0, 0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "b:i:f:o:t:c:s:p:S:n:h",
                              long_opts, NULL)) != -1) {
        switch (opt) {

        /* -------- Execution -------- */
        case 'b': binary         = optarg; break;
        case 'i': input_str      = optarg; break;
        case 'f': input_file     = optarg; break;
        case 'o': output_limit   = strtoul(optarg, NULL, 10); break;
        case 't': timeout_ms     = (unsigned)strtoul(optarg, NULL, 10); break;

        /* -------- rlimits -------- */
        case 'c': cpu_seconds    = (unsigned)strtoul(optarg, NULL, 10); break;
        case 's': max_file_size  = (size_t)strtoull(optarg, NULL, 10); break;
        case 'p': max_pids       = (unsigned)strtoul(optarg, NULL, 10); break;
        case 'S': stack_size     = (size_t)strtoull(optarg, NULL, 10); break;
        case 'n': max_open_files = (unsigned)strtoul(optarg, NULL, 10); break;

        /* -------- cgroups -------- */
        case OPT_CGROUP_ROOT: cgroup_root   = optarg; break;
        case OPT_CGROUP_NAME: cgroup_name   = optarg; break;
        case OPT_CGROUP_CPU:  cgroup_cpu    = (unsigned)strtoul(optarg, NULL, 10); break;
        case OPT_CGROUP_MEM:  cgroup_memory = (unsigned)strtoul(optarg, NULL, 10); break;
        case OPT_CGROUP_PIDS: cgroup_pids   = (unsigned)strtoul(optarg, NULL, 10); break;
        case OPT_NO_CGROUP:   use_cgroup    = 0; break;

        /* -------- namespaces -------- */
        case OPT_PID_NS:  enable_pid_ns   = 1; break;
        case OPT_MNT_NS:  enable_mount_ns = 1; break;
        case OPT_NET_NS:  enable_net_ns   = 1; break;
        case OPT_USER_NS: enable_user_ns  = 1; break;
        case OPT_UTS_NS:  enable_uts_ns   = 1; break;
        case OPT_IPC_NS:  enable_ipc_ns   = 1; break;
        case OPT_ALL_NS:
            enable_pid_ns = enable_mount_ns = enable_net_ns =
            enable_user_ns = enable_uts_ns = enable_ipc_ns = 1;
            break;

        case OPT_ROOTFS:
            rootfs = optarg;
            break;

        case OPT_HOSTNAME:
            hostname = optarg;
            break;

        case OPT_BIND: {
            /*
             * Parse "host:guest" and store both halves.
             * We strdup the whole string so that when we later free
             * bind_hosts[i], we also implicitly free bind_guests[i]
             * (which points into the same buffer).
             */
            char *dup = strdup(optarg);
            if (!dup) { fprintf(stderr, "strdup failed\n"); return 1; }

            char *colon = strchr(dup, ':');
            if (!colon) {
                fprintf(stderr, "--bind requires HOST:GUEST format\n");
                free(dup);
                return 1;
            }
            *colon = '\0';

            if (bind_count >= 32) {
                fprintf(stderr, "too many --bind options (max 32)\n");
                free(dup);
                return 1;
            }

            bind_hosts[bind_count]  = dup;
            bind_guests[bind_count] = colon + 1;
            bind_count++;
            break;
        }

        /* -------- seccomp -------- */
        case OPT_SECCOMP:
            enable_seccomp = 1;
            break;

        case OPT_SECCOMP_PROFILE:
            if (strcmp(optarg, "c") == 0) {
                seccomp_profile_id = 0;
            } else if (strcmp(optarg, "python") == 0) {
                seccomp_profile_id = 1;
            } else {
                fprintf(stderr, "unknown seccomp profile: %s "
                                "(expected 'c' or 'python')\n", optarg);
                for (size_t i = 0; i < bind_count; i++) free((void *)bind_hosts[i]);
                return 1;
            }
            enable_seccomp = 1;   /* --seccomp-profile implies --seccomp */
            break;

        /* -------- watchdog & verdict -------- */
        case OPT_GRACE_MS:
            grace_ms = (unsigned)strtoul(optarg, NULL, 10);
            break;

        case OPT_EXPECTED_FILE:
            expected_file = optarg;
            break;

        case OPT_STRICT_WS:
            strict_ws = 1;
            break;

        case 'h':
            usage(argv[0]);
            for (size_t i = 0; i < bind_count; i++) free((void *)bind_hosts[i]);
            return 0;

        default:
            usage(argv[0]);
            for (size_t i = 0; i < bind_count; i++) free((void *)bind_hosts[i]);
            return 1;
        }
    }

    /* ---------------- Validate and post-process ------------- */

    if (!binary) {
        fprintf(stderr, "error: --binary is required\n");
        usage(argv[0]);
        for (size_t i = 0; i < bind_count; i++) free((void *)bind_hosts[i]);
        return 1;
    }

    /*
     * pivot_root requires a mount namespace. If the user asked for a
     * rootfs but forgot --mnt-ns, enable it automatically.
     */
    if (rootfs && !enable_mount_ns) enable_mount_ns = 1;

    /*
     * On non-root systems, mount namespaces are only creatable inside
     * a user namespace. If we need mnt ns and we're not root, silently
     * enable user ns (which maps our uid to 0 inside).
     */
    if (enable_mount_ns && !enable_user_ns && geteuid() != 0) {
        enable_user_ns = 1;
    }

    /*
     * Same reasoning for PID namespaces: on non-root systems they
     * require a user namespace in which we have CAP_SYS_ADMIN.
     */
    if (enable_pid_ns && !enable_user_ns && geteuid() != 0) {
        enable_user_ns = 1;
    }

    /*
     * If we're already root, disable user namespace:
     *   1. We already have all capabilities needed for every other ns.
     *   2. Writing uid_map from a root process fails with EPERM because
     *      the kernel refuses to map uids the process doesn't own in
     *      the parent namespace.
     * Nothing is lost by skipping it; the child gets the same
     * isolation from the remaining namespaces.
     */
    if (geteuid() == 0 && enable_user_ns) {
        fprintf(stderr,
                "note: --user-ns ignored (running as root; root already "
                "has all capabilities)\n");
        enable_user_ns = 0;
    }

    /* ---------------- cgroup setup ------------------------ */

    if (use_cgroup) {
        if (!cgroup_name) {
            default_cgroup_name(cgroup_name_buf, sizeof(cgroup_name_buf));
            cgroup_name = cgroup_name_buf;
        }
        /*
         * cgroup_init_parent is idempotent — safe to call every run.
         * If it fails (e.g., no permission), we disable cgroups entirely
         * and continue with rlimits as the backstop.
         */
        if (cgroup_init_parent(cgroup_root) < 0) {
            fprintf(stderr,
                    "warning: cgroup_init_parent(%s) failed: %s\n"
                    "         continuing without cgroups\n",
                    cgroup_root, strerror(errno));
            use_cgroup = 0;
        }
    }

    /* ---------------- stdin input ------------------------- */

    char  *input_buf = NULL;
    size_t input_len = 0;

    if (input_str) {
        input_len = strlen(input_str);
        input_buf = malloc(input_len ? input_len : 1);
        if (input_buf && input_len) memcpy(input_buf, input_str, input_len);
    } else if (input_file) {
        input_buf = read_file(input_file, &input_len);
        if (!input_buf) {
            for (size_t i = 0; i < bind_count; i++) free((void *)bind_hosts[i]);
            return 1;
        }
    }

    /* ---------------- expected output (for verdict) ------- */

    if (expected_file) {
        expected_buf = read_file(expected_file, &expected_len);
        if (!expected_buf) {
            free(input_buf);
            for (size_t i = 0; i < bind_count; i++) free((void *)bind_hosts[i]);
            return 1;
        }
    }

    /* ---------------- Build sandbox_config_t --------------- */

    /*
     * We pass argv[0] as the binary path. Passing additional arguments
     * to the child is a future enhancement (Step 8+ will add `--`).
     */
    char *child_argv[] = { (char *)binary, NULL };

    sandbox_config_t cfg = {
        /* Execution */
        .binary         = binary,
        .argv           = child_argv,
        .stdin_data     = input_buf,
        .stdin_len      = input_len,
        .output_limit   = output_limit,
        .timeout_ms     = timeout_ms,
        .grace_ms       = grace_ms,

        /* rlimits */
        .cpu_seconds    = cpu_seconds,
        .max_file_size  = max_file_size,
        .max_pids       = max_pids,
        .stack_size     = stack_size,
        .max_open_files = max_open_files,

        /* cgroups */
        .cgroup_root         = use_cgroup ? cgroup_root : NULL,
        .cgroup_name         = use_cgroup ? cgroup_name : NULL,
        .cgroup_cpu_pct      = cgroup_cpu,
        .cgroup_memory_mb    = cgroup_memory,
        .cgroup_pids_max     = cgroup_pids,
        .cgroup_io_read_bps  = 0,
        .cgroup_io_write_bps = 0,

        /* Namespaces */
        .enable_pid_ns    = enable_pid_ns,
        .enable_mount_ns  = enable_mount_ns,
        .enable_net_ns    = enable_net_ns,
        .enable_user_ns   = enable_user_ns,
        .enable_uts_ns    = enable_uts_ns,
        .enable_ipc_ns    = enable_ipc_ns,
        .rootfs           = rootfs,
        .hostname         = hostname,
        .bind_host_paths  = bind_hosts,
        .bind_guest_paths = bind_guests,
        .bind_count       = bind_count,

        /* Seccomp */
        .enable_seccomp  = enable_seccomp,
        .seccomp_profile = seccomp_profile_id,
    };

    /* ---------------- Run --------------------------------- */

    sandbox_result_t res;
    int rc = sandbox_run(&cfg, &res);
    if (rc != SANDBOX_OK) {
        fprintf(stderr, "sandbox_run failed: rc=%d (%s)\n",
                rc, strerror(errno));
        free(input_buf);
        free(expected_buf);
        for (size_t i = 0; i < bind_count; i++) free((void *)bind_hosts[i]);
        return 1;
    }

    /* ---------------- Verdict determination --------------- */

    char verdict_str[16];
    verdict_input_t vin = {
        .expected          = expected_buf,
        .expected_len      = expected_len,
        .strict_whitespace = strict_ws,
    };
    verdict_code_t vcode = verdict_determine(&res, &vin,
                                             verdict_str, sizeof(verdict_str));
    (void)vcode;

    /* ---------------- Report ------------------------------ */

    printf("=== SANDBOX RESULT ===\n");
    printf("verdict:            %s\n", verdict_str);
    printf("exit_code:          %d\n", res.exit_code);
    printf("signal:             %d\n", res.signal);
    printf("timed_out:          %d\n", res.timed_out);
    printf("term_sent:          %d\n", res.term_sent);
    printf("kill_sent:          %d\n", res.kill_sent);
    printf("ns_setup_failed:    %d\n", res.ns_setup_failed);
    printf("seccomp_violation:  %d\n", res.seccomp_violation);
    printf("oom_killed:         %d\n", res.oom_killed);
    printf("wall_time_ms:       %ld\n", res.wall_time_ms);
    printf("cpu_time_ms:        %ld (user=%ld sys=%ld)\n",
           res.cpu_time_ms, res.user_time_ms, res.sys_time_ms);
    printf("max_rss_kb:         %ld\n", res.max_rss_kb);
    printf("cgroup_mem_peak_b:  %llu\n",
           (unsigned long long)res.cgroup_memory_peak_bytes);
    printf("cgroup_cpu_usec:    %llu\n",
           (unsigned long long)res.cgroup_cpu_usage_usec);
    printf("cgroup_pids_peak:   %llu\n",
           (unsigned long long)res.cgroup_pids_peak);
    printf("stdout_len:         %zu%s\n",
           res.stdout_len, res.stdout_truncated ? " (truncated)" : "");
    printf("stderr_len:         %zu%s\n",
           res.stderr_len, res.stderr_truncated ? " (truncated)" : "");

    print_ns_summary(&cfg);
    print_limits_summary(&cfg);
    print_isolation_summary(&cfg);

    printf("--- STDOUT ---\n");
    if (res.stdout_len) fwrite(res.stdout_buf, 1, res.stdout_len, stdout);
    printf("\n--- STDERR ---\n");
    if (res.stderr_len) fwrite(res.stderr_buf, 1, res.stderr_len, stdout);
    printf("\n=== END ===\n");

    /* ---------------- Cleanup ----------------------------- */

    int ok = (vcode == VERDICT_OK);

    sandbox_result_free(&res);
    free(input_buf);
    free(expected_buf);
    for (size_t i = 0; i < bind_count; i++) free((void *)bind_hosts[i]);

    return ok ? 0 : 1;
}