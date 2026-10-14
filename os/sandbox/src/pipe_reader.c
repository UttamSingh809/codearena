/*
 * pipe_reader.c — bounded, non-deadlocking capture of two pipes.
 *
 * Reads from stdout_fd and stderr_fd in parallel using poll() so that
 * neither pipe can deadlock the other. Once `limit` bytes have been
 * captured from a stream, further bytes on that stream are discarded
 * (but the pipe is still drained so the child never blocks on write).
 */

#include "sandbox.h"

#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define DRAIN_CHUNK 4096

int sandbox_capture_output(int stdout_fd, int stderr_fd, size_t limit,
                           char *out_buf, size_t *out_len, int *out_trunc,
                           char *err_buf, size_t *err_len, int *err_trunc)
{
    struct pollfd fds[2];
    fds[0].fd     = stdout_fd;
    fds[0].events = POLLIN;
    fds[1].fd     = stderr_fd;
    fds[1].events = POLLIN;

    int active = 2;

    *out_len   = 0;
    *err_len   = 0;
    *out_trunc = 0;
    *err_trunc = 0;

    char drain[DRAIN_CHUNK];

    while (active > 0) {
        int ready = poll(fds, 2, -1);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return SANDBOX_ERR_IO;
        }

        for (int i = 0; i < 2; i++) {
            if (fds[i].fd < 0) continue;
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;

            char  *dst;
            size_t *len;
            int    *trunc;

            if (i == 0) { dst = out_buf; len = out_len; trunc = out_trunc; }
            else        { dst = err_buf; len = err_len; trunc = err_trunc; }

            ssize_t n = read(fds[i].fd, drain, sizeof(drain));
            if (n < 0) {
                if (errno == EINTR) continue;
                return SANDBOX_ERR_IO;
            }
            if (n == 0) {
                /* EOF on this stream */
                fds[i].fd = -1;
                active--;
                continue;
            }

            for (ssize_t j = 0; j < n; j++) {
                if (*len < limit) {
                    dst[*len] = drain[j];
                    (*len)++;
                } else {
                    *trunc = 1;
                }
            }
        }
    }

    return SANDBOX_OK;
}