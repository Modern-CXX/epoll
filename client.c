//client.c

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <thread>
#include <fcntl.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    struct addrinfo hints, *result;
    const char *host = argv[1];
    const char *port = argv[2];
    int sfd;
    int sockopt = 1;
    int ret;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    if (argc != 4)
    {
        printf("Usage: %s <host> <port> <msg>\n", argv[0]);
        return 1;
    }

    ret = getaddrinfo(host, port, &hints, &result);
    if (ret != 0)
    {
        printf("getaddrinfo: %s\n", gai_strerror(ret));
        return 1;
    }

    sfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sfd == -1)
    {
        printf("socket: %s\n", strerror(errno));
        return 1;
    }

    ret = connect(sfd, result->ai_addr, result->ai_addrlen);
    if (ret == -1)
    {
        printf("connect: %s\n", strerror(errno));
        return 1;
    }

    freeaddrinfo(result);

    //set socket non-block
    int flag = fcntl(sfd, F_GETFL);
    if (sfd == -1)
    {
        printf("fcntl: %s\n", strerror(errno));
        return 1;
    }
    flag |= O_NONBLOCK;
    flag = fcntl(sfd, F_SETFL, flag);
    if (sfd == -1)
    {
        printf("fcntl: %s\n", strerror(errno));
        return 1;
    }


    //send / recv

    while (1)
    {
        sleep(1); //test
        const char *msg = argv[3];
        ret = write(sfd, msg, strlen(msg));

        char buf[1024] = {'\0'};
        ret = read(sfd, buf, sizeof(buf) - 1);
        if (strlen(buf) > 0)
            printf("%s\n", buf);
    }

    //send / recv


    ret = close(sfd);
    if (ret == -1)
    {
        printf("close: %s\n", strerror(errno));
        return 1;
    }

    return 0;
}
