/*
 * main.c — worker CLI entry point.
 *
 * Reads configuration from command-line flags and environment
 * variables, constructs a worker_config_t, creates the worker,
 * and runs the main loop.
 *
 * Example:
 *   ./codearena-worker \
 *       --id worker-1 \
 *       --queue /codearena_queue \
 *       --db "$DATABASE_URL" \
 *       --cgroup-root /sys/fs/cgroup/codearena \
 *       --tmp /tmp/codearena \
 *       --all-ns --seccomp
 */

#include "worker.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s --id NAME --queue NAME --db URL [options]\n"
        "\n"
        "Required:\n"
        "  --id NAME              unique worker identifier\n"
        "  --queue NAME           IPC queue name (e.g. /codearena_queue)\n"
        "  --db URL               libpq connection string or DATABASE_URL\n"
        "\n"
        "Sandbox:\n"
        "  --cgroup-root PATH     cgroup root (default /sys/fs/cgroup/codearena)\n"
        "  --tmp PATH             sandbox temp dir (default /tmp/codearena)\n"
        "  --rootfs PATH          minimal rootfs, optional\n"
        "  --seccomp              enable seccomp-BPF\n"
        "  --seccomp-profile NAME c or python (default c)\n"
        "  --all-ns               enable all namespaces\n"
        "  --heartbeat SECONDS    heartbeat interval (default 30)\n"
        "  --output-limit KB      max output per stream (default 64)\n"
        "\n"
        "  --help                 show this help\n",
        prog);
}

int main(int argc, char **argv)
{
    worker_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cgroup_root       = "/sys/fs/cgroup/codearena";
    cfg.sandbox_tmp       = "/tmp/codearena";
    cfg.heartbeat_seconds = 30;
    cfg.output_limit_kb   = 64;
    cfg.seccomp_profile   = 0;
    cfg.c_compiler        = "gcc";
    cfg.cpp_compiler      = "g++";

    /* Default: pick up DB URL from env if not passed explicitly. */
    cfg.db_url = getenv("DATABASE_URL");

    enum {
        OPT_ID = 1000,
        OPT_QUEUE,
        OPT_DB,
        OPT_CGROUP_ROOT,
        OPT_TMP,
        OPT_ROOTFS,
        OPT_SECCOMP,
        OPT_SECCOMP_PROFILE,
        OPT_ALL_NS,
        OPT_HEARTBEAT,
        OPT_OUTPUT_LIMIT,
    };

    static struct option opts[] = {
        { "id",              required_argument, NULL, OPT_ID },
        { "queue",           required_argument, NULL, OPT_QUEUE },
        { "db",              required_argument, NULL, OPT_DB },
        { "cgroup-root",     required_argument, NULL, OPT_CGROUP_ROOT },
        { "tmp",             required_argument, NULL, OPT_TMP },
        { "rootfs",          required_argument, NULL, OPT_ROOTFS },
        { "seccomp",         no_argument,       NULL, OPT_SECCOMP },
        { "seccomp-profile", required_argument, NULL, OPT_SECCOMP_PROFILE },
        { "all-ns",          no_argument,       NULL, OPT_ALL_NS },
        { "heartbeat",       required_argument, NULL, OPT_HEARTBEAT },
        { "output-limit",    required_argument, NULL, OPT_OUTPUT_LIMIT },
        { "help",            no_argument,       NULL, 'h' },
        { 0, 0, 0, 0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "h", opts, NULL)) != -1) {
        switch (opt) {
            case OPT_ID:       cfg.worker_id = optarg; break;
            case OPT_QUEUE:    cfg.queue_name = optarg; break;
            case OPT_DB:       cfg.db_url = optarg; break;
            case OPT_CGROUP_ROOT: cfg.cgroup_root = optarg; break;
            case OPT_TMP:      cfg.sandbox_tmp = optarg; break;
            case OPT_ROOTFS:   cfg.rootfs = optarg; break;
            case OPT_SECCOMP:  cfg.enable_seccomp = 1; break;
            case OPT_SECCOMP_PROFILE:
                if (strcmp(optarg, "c") == 0) cfg.seccomp_profile = 0;
                else if (strcmp(optarg, "python") == 0) cfg.seccomp_profile = 1;
                else { fprintf(stderr, "unknown profile: %s\n", optarg); return 1; }
                cfg.enable_seccomp = 1;
                break;
            case OPT_ALL_NS:
                cfg.enable_pid_ns = cfg.enable_mount_ns = cfg.enable_net_ns =
                cfg.enable_user_ns = cfg.enable_uts_ns = cfg.enable_ipc_ns = 1;
                break;
            case OPT_HEARTBEAT:  cfg.heartbeat_seconds = (unsigned)strtoul(optarg, NULL, 10); break;
            case OPT_OUTPUT_LIMIT: cfg.output_limit_kb = (unsigned)strtoul(optarg, NULL, 10); break;
            case 'h':          usage(argv[0]); return 0;
            default:           usage(argv[0]); return 1;
        }
    }

    if (!cfg.worker_id || !cfg.queue_name || !cfg.db_url) {
        fprintf(stderr, "error: --id, --queue, and --db are required\n");
        usage(argv[0]);
        return 1;
    }

    /* Root means user namespace is unnecessary and would fail. */
    if (geteuid() == 0 && cfg.enable_user_ns) {
        cfg.enable_user_ns = 0;
    }
    /* Non-root with mnt/pid ns needs user ns. */
    if (geteuid() != 0) {
        if ((cfg.enable_mount_ns || cfg.enable_pid_ns) && !cfg.enable_user_ns) {
            cfg.enable_user_ns = 1;
        }
    }

    worker_t *w = worker_create(&cfg);
    if (!w) {
        fprintf(stderr, "worker_create failed\n");
        return 1;
    }

    int rc = worker_run(w);
    worker_destroy(w);
    return rc == 0 ? 0 : 1;
}