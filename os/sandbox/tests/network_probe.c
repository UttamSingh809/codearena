#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "network_probe: socket() failed: %s\n", strerror(errno));
        return 1;
    }

    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(53);
    inet_pton(AF_INET, "1.1.1.1", &sa.sin_addr);

    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        fprintf(stderr, "network_probe: connect() failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    printf("network_probe: connected (unexpected under net ns)\n");
    close(fd);
    return 0;
}