//client.c

#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>

#define PRINT_LOG(fmt, ...) do \
{ \
    printf("%s:%d:%s: " fmt "\n", \
        __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
} while (0) /* ; no trailing semicolon here */

#define PERROR(fmt, ...) do \
{ \
    printf("%s:%d:%s: %s. " fmt "\n", \
        __FILE__, __LINE__, __func__, strerror(errno), ##__VA_ARGS__); \
} while (0) /* ; no trailing semicolon here */

int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        PERROR("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) {
        PERROR("fcntl");
        return -1;
    }
}

int main(int argc, char *argv[])
{
    int fd;
    int ret;
    struct sockaddr_in addr;
    const char *host;
    int port;
    const char *msg;

    if (argc != 4) {
        PRINT_LOG("Usage: %s <host> <port> <msg>\n", argv[0]);
        return 1;
    }

    host = argv[1];
    port = atoi(argv[2]);
    msg = argv[3];

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1){
        PERROR("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;

    ret = inet_aton(host, (struct in_addr*)&addr.sin_addr.s_addr);
    if (ret == 0){
        PRINT_LOG("inet_aton failed");
        return -1;
    }

    ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret == -1){
        PERROR("connect");
        close(fd);
        return -1;
    }

    PRINT_LOG("connect to %s:%d", host, port);

    set_nonblock(fd);

    for(;;){
        sleep(1); //test
        ret = write(fd, msg, strlen(msg));

        char buf[1024] = {'\0'};
        ret = read(fd, buf, sizeof(buf) - 1);
        if (ret > 0){
            PRINT_LOG("%s", buf);
        }
    }
    ret = close(fd);

    return 0;
}
