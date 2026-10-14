#ifndef CODEARENA_VERDICT_H
#define CODEARENA_VERDICT_H

/*
 * CodeArena — verdict determination.
 *
 * Given the result of a sandboxed run and the expected output for one
 * test case, produce a verdict string.
 *
 * Verdicts follow the standard competitive-programming vocabulary:
 *   AC   Accepted               — passed
 *   WA   Wrong Answer           — ran cleanly, output mismatch
 *   TLE  Time Limit Exceeded    — killed by wall-clock watchdog
 *   MLE  Memory Limit Exceeded  — killed by cgroup OOM
 *   OLE  Output Limit Exceeded  — output exceeded the stream cap
 *   RE   Runtime Error          — non-zero exit, signal, or seccomp
 *   CE   Compilation Error      — reserved for the compile step
 *   FAILED Internal Error       — sandbox setup failed
 *
 * The verdict is derived in priority order (see verdict_determine()).
 */

#include "sandbox.h"
#include <stddef.h>

typedef struct {
    /* Expected output for this test case (bytes, not NUL-terminated). */
    const char *expected;
    size_t      expected_len;

    /* What to do with trailing whitespace and line endings.
     * VERDICT_STRICT:   byte-exact match.
     * VERDICT_TRIMMED:  strip trailing whitespace per line, strip
     *                   trailing newlines overall, ignore CR.
     * Default: VERDICT_TRIMMED (what 99% of judges use). */
    int         strict_whitespace;
} verdict_input_t;

typedef enum {
    VERDICT_OK = 0,
    VERDICT_WA = 1,
    VERDICT_TLE = 2,
    VERDICT_MLE = 3,
    VERDICT_OLE = 4,
    VERDICT_RE = 5,
    VERDICT_CE = 6,
    VERDICT_FAILED = 7,
    VERDICT_PE = 8,
} verdict_code_t;

/*
 * Determine the verdict for one test case.
 *
 * Returns one of the VERDICT_* codes. Also writes a short human-readable
 * string into `out_str` (at most `out_sz` bytes, always NUL-terminated).
 */
verdict_code_t verdict_determine(const sandbox_result_t *r,
                                 const verdict_input_t *in,
                                 char *out_str, size_t out_sz);

/*
 * Convert a VERDICT_* code to a stable string. Never returns NULL.
 * Returned string is static; do not free.
 */
const char *verdict_code_str(verdict_code_t code);

/*
 * Compare two buffers after applying normalization rules.
 * Returns 1 if they match, 0 otherwise.
 *
 * Normalization (when strict_whitespace == 0):
 *   - Drop trailing '\r' before each '\n' (CRLF → LF)
 *   - Strip trailing spaces/tabs from each line
 *   - Strip trailing newlines at the very end
 */
int verdict_output_match(const char *actual, size_t actual_len,
                         const char *expected, size_t expected_len,
                         int strict_whitespace);

#endif /* CODEARENA_VERDICT_H */