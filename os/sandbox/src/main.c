/*
 * main.c — CLI front-end for the sandbox.
 *
 * Examples:
 *   ./sandbox --binary ./tests/hello --input "codearena"
 *   ./sandbox --binary ./tests/cpu_hog --cpu-seconds 2
 *   ./sandbox --binary ./tests/file_filler --max-file-size 1048576
 */

#define _GNU_SOURCE
#include "sandbox.h"

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s --binary PATH [options]\n"
        "\n"
        "Execution:\n"
        "  -b, --binary PATH           path to the program to run (required)\n"
        "  -i, --input STRING          stdin supplied to the program\n"
        "  -f, --input-file PATH       read stdin from a file instead\n"
        "  -o, --output-limit BYTES    cap on bytes captured per stream (default 65536)\n"
        "  -t, --time MS               wall-clock timeout (step 7; default 2000)\n"
        "\n"
        "Resource limits (step 3):\n"
        "  -c, --cpu-seconds N         RLIMIT_CPU hard limit (default 2, 0=unlimited)\n"
        "  -s, --max-file-size BYTES   RLIMIT_FSIZE (default 1048576, 0=unlimited)\n"
        "  -p, --max-pids N            RLIMIT_NPROC (default 64, 0=unlimited)\n"
        "  -S, --stack-size BYTES      RLIMIT_STACK (default 8388608, 0=default)\n"
        "  -n, --max-open-files N      RLIMIT_NOFILE (default 64, 0=default)\n"
        "\n"
        "  -h, --help                  show this help\n",
        prog);
}

static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open %s: %s\n", path, strerror(errno));
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

static const char *describe_result(const sandbox_result_t *r)
{
    if (r->signal == SIGXCPU) return "TIME LIMIT (CPU)";
    if (r->signal == SIGXFSZ) return "FILE SIZE LIMIT";
    if (r->signal == SIGKILL) return "KILLED (SIGKILL)";
    if (r->signal)            return "TERMINATED BY SIGNAL";
    if (r->exit_code == 0)    return "OK";
    if (r->exit_code == 126)  return "SANDBOX SETUP FAILED (rlimit)";
    if (r->exit_code == 127)  return "EXEC FAILED";
    return "NON-ZERO EXIT";
}

int main(int argc, char **argv)
{
    const char  *binary       = NULL;
    const char  *input_str    = NULL;
    const char  *input_file   = NULL;
    size_t       output_limit = 64 * 1024;
    unsigned int timeout_ms   = 2000;

    /* Step 3 rlimit defaults */
    unsigned int cpu_seconds    = 2;
    size_t       max_file_size  = 1024 * 1024;             /* 1 MB */
    unsigned int max_pids       = 64;
    size_t       stack_size     = 8 * 1024 * 1024;         /* 8 MB */
    unsigned int max_open_files = 64;

    static struct option long_opts[] = {
        { "binary",         required_argument, NULL, 'b' },
        { "input",          required_argument, NULL, 'i' },
        { "input-file",     required_argument, NULL, 'f' },
        { "output-limit",   required_argument, NULL, 'o' },
        { "time",           required_argument, NULL, 't' },
        { "cpu-seconds",    required_argument, NULL, 'c' },
        { "max-file-size",  required_argument, NULL, 's' },
        { "max-pids",       required_argument, NULL, 'p' },
        { "stack-size",     required_argument, NULL, 'S' },
        { "max-open-files", required_argument, NULL, 'n' },
        { "help",           no_argument,       NULL, 'h' },
        { 0, 0, 0, 0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "b:i:f:o:t:c:s:p:S:n:h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'b': binary       = optarg;                                break;
            case 'i': input_str    = optarg;                                break;
            case 'f': input_file   = optarg;                                break;
            case 'o': output_limit = strtoul(optarg, NULL, 10);             break;
            case 't': timeout_ms   = (unsigned)strtoul(optarg, NULL, 10);   break;
            case 'c': cpu_seconds  = (unsigned)strtoul(optarg, NULL, 10);   break;
            case 's': max_file_size = (size_t)strtoull(optarg, NULL, 10);   break;
            case 'p': max_pids     = (unsigned)strtoul(optarg, NULL, 10);   break;
            case 'S': stack_size   = (size_t)strtoull(optarg, NULL, 10);    break;
            case 'n': max_open_files = (unsigned)strtoul(optarg, NULL, 10); break;
            case 'h': usage(argv[0]); return 0;
            default:  usage(argv[0]); return 1;
        }
    }

    if (!binary) {
        fprintf(stderr, "error: --binary is required\n");
        usage(argv[0]);
        return 1;
    }

    char  *input_buf = NULL;
    size_t input_len = 0;
    if (input_str) {
        input_len = strlen(input_str);
        input_buf = malloc(input_len ? input_len : 1);
        if (input_buf && input_len) memcpy(input_buf, input_str, input_len);
    } else if (input_file) {
        input_buf = read_file(input_file, &input_len);
        if (!input_buf) return 1;
    }

    char *child_argv[] = { (char *)binary, NULL };

    sandbox_config_t cfg = {
        .binary         = binary,
        .argv           = child_argv,
        .stdin_data     = input_buf,
        .stdin_len      = input_len,
        .output_limit   = output_limit,
        .timeout_ms     = timeout_ms,
        .cpu_seconds    = cpu_seconds,
        .max_file_size  = max_file_size,
        .max_pids       = max_pids,
        .stack_size     = stack_size,
        .max_open_files = max_open_files,
    };

    sandbox_result_t res;
    int rc = sandbox_run(&cfg, &res);
    if (rc != SANDBOX_OK) {
        fprintf(stderr, "sandbox_run failed: rc=%d\n", rc);
        free(input_buf);
        return 1;
    }

    printf("=== SANDBOX RESULT ===\n");
    printf("verdict:          %s\n", describe_result(&res));
    printf("exit_code:        %d\n", res.exit_code);
    printf("signal:           %d\n", res.signal);
    printf("wall_time_ms:     %ld\n", res.wall_time_ms);
    printf("cpu_time_ms:      %ld (user=%ld sys=%ld)\n",
           res.cpu_time_ms, res.user_time_ms, res.sys_time_ms);
    printf("max_rss_kb:       %ld\n", res.max_rss_kb);
    printf("stdout_len:       %zu%s\n",
           res.stdout_len, res.stdout_truncated ? " (truncated)" : "");
    printf("stderr_len:       %zu%s\n",
           res.stderr_len, res.stderr_truncated ? " (truncated)" : "");
    printf("--- STDOUT ---\n");
    if (res.stdout_len) fwrite(res.stdout_buf, 1, res.stdout_len, stdout);
    printf("\n--- STDERR ---\n");
    if (res.stderr_len) fwrite(res.stderr_buf, 1, res.stderr_len, stdout);
    printf("\n=== END ===\n");

    int ok = (res.exit_code == 0 && res.signal == 0);
    sandbox_result_free(&res);
    free(input_buf);
    return ok ? 0 : 1;
}