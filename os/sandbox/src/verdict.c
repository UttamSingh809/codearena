/*
 * verdict.c — determine a submission's verdict from its sandbox result
 * and the expected output.
 *
 * Priority order (first match wins):
 *   1. ns_setup_failed  → FAILED (internal sandbox error)
 *   2. seccomp_violation → RE (syscall attempt)
 *   3. oom_killed        → MLE
 *   4. signal == SIGXCPU → TLE (CPU time exhausted)
 *   5. timed_out         → TLE (wall-clock exhausted)
 *   6. signal == SIGXFSZ → OLE (file size limit hit)
 *   7. stdout_truncated  → OLE (output stream cap hit)
 *   8. signal != 0       → RE
 *   9. exit_code != 0    → RE
 *  10. output mismatch   → WA
 *  11. otherwise         → AC
 */

#include "verdict.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Code to string                                                     */
/* ------------------------------------------------------------------ */

const char *verdict_code_str(verdict_code_t code)
{
    switch (code) {
        case VERDICT_OK:     return "AC";
        case VERDICT_WA:     return "WA";
        case VERDICT_TLE:    return "TLE";
        case VERDICT_MLE:    return "MLE";
        case VERDICT_OLE:    return "OLE";
        case VERDICT_RE:     return "RE";
        case VERDICT_CE:     return "CE";
        case VERDICT_PE:     return "PE";
        case VERDICT_FAILED: return "FAILED";
        default:             return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/* Output comparison                                                  */
/* ------------------------------------------------------------------ */

/*
 * Iterate over both buffers simultaneously, skipping bytes that are
 * whitespace-equivalent under the normalization rules.
 *
 * This is a streaming comparison — no allocation, no copy. The rules
 * applied are:
 *
 *   - '\r' immediately before '\n' is dropped from both sides.
 *   - Trailing spaces and tabs at end of line are dropped from both.
 *   - Trailing newlines at end of buffer are dropped from both.
 *
 * Returns 1 if the two streams are equivalent, 0 otherwise.
 */
static int compare_streams(const char *a, size_t alen,
                           const char *b, size_t blen)
{
    size_t i = 0, j = 0;

    while (i < alen || j < blen) {
        /* Skip trailing whitespace on the current line of A. */
        size_t ai = i;
        while (ai < alen && a[ai] != '\n') ai++;
        size_t a_line_end = ai;

        /* Skip trailing whitespace on the current line of B. */
        size_t bj = j;
        while (bj < blen && b[bj] != '\n') bj++;
        size_t b_line_end = bj;

        /* Compute trimmed line ends (drop trailing spaces/tabs). */
        while (a_line_end > i &&
               (a[a_line_end - 1] == ' ' || a[a_line_end - 1] == '\t')) {
            a_line_end--;
        }
        while (b_line_end > j &&
               (b[b_line_end - 1] == ' ' || b[b_line_end - 1] == '\t')) {
            b_line_end--;
        }

        /* Compare the trimmed line bodies byte-exact. */
        size_t a_line_len = a_line_end - i;
        size_t b_line_len = b_line_end - j;
        if (a_line_len != b_line_len) return 0;
        if (memcmp(a + i, b + j, a_line_len) != 0) return 0;

        /* Advance past the line and its '\n' (if present). */
        i = ai;   /* points at '\n' or end */
        j = bj;
        if (i < alen && a[i] == '\n') i++;
        if (j < blen && b[j] == '\n') j++;

        /* If one side has a '\n' and the other is at end, the shorter
         * side wins only if the longer side has nothing but trailing
         * newlines. The outer loop handles that naturally. */
    }

    return 1;
}

/*
 * Strict comparison: byte-exact, no normalization.
 */
static int compare_strict(const char *a, size_t alen,
                          const char *b, size_t blen)
{
    if (alen != blen) return 0;
    if (alen == 0) return 1;
    return memcmp(a, b, alen) == 0;
}

/*
 * Public comparison: dispatches to strict or normalized.
 */
int verdict_output_match(const char *actual, size_t actual_len,
                         const char *expected, size_t expected_len,
                         int strict_whitespace)
{
    if (strict_whitespace) {
        return compare_strict(actual, actual_len,
                              expected, expected_len);
    }
    return compare_streams(actual, actual_len,
                           expected, expected_len);
}

/* ------------------------------------------------------------------ */
/* Verdict determination                                              */
/* ------------------------------------------------------------------ */

verdict_code_t verdict_determine(const sandbox_result_t *r,
                                 const verdict_input_t *in,
                                 char *out_str, size_t out_sz)
{
    verdict_code_t v = VERDICT_OK;

    if (!r) {
        v = VERDICT_FAILED;
        goto done;
    }

    /* 1. Sandbox setup failure. */
    if (r->ns_setup_failed)
    {
        v = VERDICT_FAILED;
        goto done;
    }

    /* 2. Seccomp violation — the submission tried a blocked syscall. */
    if (r->seccomp_violation)
    {
        v = VERDICT_RE;
        goto done;
    }

    /* 3. cgroup OOM — kernel killed the process. */
    if (r->oom_killed)
    {
        v = VERDICT_MLE;
        goto done;
    }

    /* 4. Wall-clock watchdog fired — TLE. Must come before signal
     *    checks because the watchdog sends SIGTERM/SIGKILL, and we
     *    don't want those to be interpreted as generic runtime errors. */
    if (r->timed_out)
    {
        v = VERDICT_TLE;
        goto done;
    }

    /* 5. CPU time exhausted (RLIMIT_CPU) — SIGXCPU. */
    if (r->signal == SIGXCPU)
    {
        v = VERDICT_TLE;
        goto done;
    }

    /* 6. File size limit hit (RLIMIT_FSIZE) — SIGXFSZ. */
    if (r->signal == SIGXFSZ)
    {
        v = VERDICT_OLE;
        goto done;
    }

    /* 7. Output stream cap hit. */
    if (r->stdout_truncated || r->stderr_truncated)
    {
        v = VERDICT_OLE;
        goto done;
    }

    /* 8. Any other signal. */
    if (r->signal != 0)
    {
        v = VERDICT_RE;
        goto done;
    }

    /* 9. Non-zero exit code. */
    if (r->exit_code != 0)
    {
        v = VERDICT_RE;
        goto done;
    }

        /* 10. Compare output. If we weren't given expected output, treat
         *     as "no check" and default to AC after clean exit. */
        if (in && in->expected)
    {
        if (!verdict_output_match(r->stdout_buf, r->stdout_len,
                                  in->expected, in->expected_len,
                                  in->strict_whitespace)) {
            v = VERDICT_WA; goto done;
        }
    }

    /* 11. Passed everything. */
    v = VERDICT_OK;

done:
    if (out_str && out_sz > 0) {
        const char *s = verdict_code_str(v);
        size_t n = strlen(s);
        if (n >= out_sz) n = out_sz - 1;
        memcpy(out_str, s, n);
        out_str[n] = '\0';
    }
    return v;
}