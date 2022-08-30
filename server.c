//server.c

#include <sys/epoll.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

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
    struct epoll_event ev, events[10];
    int listenfd, connfd, nfds, efd;

    /* set up listening socket */
    int port = 8000;
    int ret = 0, conn_count = 0;
    struct sockaddr_in my_addr, peer_addr;
    socklen_t addrlen = sizeof(peer_addr);
    int sockopt = 1;

    if (argc == 2){
        int n = atoi(argv[1]);
        if (n != 0) {
            port = n;
        }
    }

    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_port = htons(port);
    my_addr.sin_addr.s_addr = htons(INADDR_ANY);
    my_addr.sin_family = AF_INET;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        PERROR("socket");
        return -1;
    }

    ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt));
    if (ret == -1){
        PERROR("setsockopt");
        return -1;
    }

    ret = bind(listenfd, (struct sockaddr*)&my_addr, sizeof(my_addr));
    if (ret == -1) {
        PERROR("bind");
        close(listenfd);
        return -1;
    }

    ret = listen(listenfd, SOMAXCONN);
    if (ret == -1) {
        PERROR("listen");
        close(listenfd);
        return -1;
    }
    PRINT_LOG("listen on port: %d", port);
    signal(SIGPIPE, SIG_IGN);

    /* set up epoll */
    efd = epoll_create1(0);
    if (efd == -1) {
        PERROR("epoll_create1");
        return -1;
    }

    ev.events = EPOLLIN;
    ev.data.fd = listenfd;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &ev) == -1) {
        PERROR("epoll_ctl");
        return -1;
    }

    for (;;) {
        nfds = epoll_wait(efd, events, sizeof(events) / sizeof(events[0]),
                                                                1000); //1 sec
        if (nfds == -1) {
            PERROR("epoll_wait");
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == listenfd) {
                connfd = accept(listenfd,
                                (struct sockaddr *) &peer_addr, &addrlen);
                if (connfd == -1) {
                    PERROR("accept");
                }
                if (set_nonblock(connfd)){
                    return -1;
                }

                conn_count++;
                PRINT_LOG("accept connection from client %s:%u, conn_count: %d",
                    inet_ntoa(peer_addr.sin_addr), peer_addr.sin_port,
                    conn_count);

                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = connfd;
                if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &ev) == -1) {
                    PERROR("epoll_ctl");
                }

            } else if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                int fd = events[i].data.fd;
                close(fd);
                PRINT_LOG("EPOLLHUP | EPOLLERR");

            } else if (events[i].events & EPOLLIN) {
                int fd = events[i].data.fd;
                int done = 0;

                for (;;){
                    char buf[1024] = {0};
                    ret = read(fd, buf, sizeof(buf) - 1);
                    if (ret > 0){
                        PRINT_LOG("%s", buf);
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK ||
                                        errno == EPIPE || errno == ECONNRESET ||
                                        errno == EINTR){
                        break;
                    }
                    if (errno != 0){
                        PERROR("read, ret: %d, errno: %d", ret, errno);
                    }
                    if (ret == -1) {
                        break;
                    }
                    if (ret == 0){
                        close(fd);
                        done = 1;
                        break;
                    }
                }
                if (done){
                    continue;
                }

                ev.events = EPOLLOUT | EPOLLET;
                ev.data.fd = fd;
                if (epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev) == -1) {
                    PERROR("epoll_ctl");
                }

            } else if (events[i].events & EPOLLOUT){
                int fd = events[i].data.fd;
                int done = 0;

                for (;;){
                    sleep(1); //test
                    const char *msg = "hello client ";
                    ret = write(fd, msg, strlen(msg));
                    if (errno == EAGAIN || errno == EWOULDBLOCK ||
                                        errno == EPIPE || errno == ECONNRESET ||
                                        errno == EINTR){
                        break;
                    }
                    PERROR("write, ret: %d, errno: %d", ret, errno);
                    if (ret == -1) {
                        break;
                    }
                    if (ret == 0) {
                        close(fd);
                        done = 1;
                        break;
                    }
                }
                if (done){
                    continue;
                }

                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = fd;
                if (epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev) == -1) {
                    PERROR("epoll_ctl");
                }

            } else {
                PRINT_LOG("unknown event");
                int fd = events[i].data.fd;
                close(fd);

            }
        }
    }

    return 0;
}
