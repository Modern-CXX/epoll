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

void set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        PERROR("fcntl");
        exit(EXIT_FAILURE);
    }

    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) {
        PERROR("fcntl");
        exit(EXIT_FAILURE);
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
        exit(EXIT_FAILURE);
    }

    ret = bind(listenfd, (struct sockaddr*)&my_addr, sizeof(my_addr));
    if (ret == -1) {
        PERROR("bind");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    ret = listen(listenfd, SOMAXCONN);
    if (ret == -1) {
        PERROR("listen");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    signal(SIGPIPE, SIG_IGN);

    /* set up epoll */
    efd = epoll_create1(0);
    if (efd == -1) {
        PERROR("epoll_create1");
        exit(EXIT_FAILURE);
    }

    ev.events = EPOLLIN;
    ev.data.fd = listenfd;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &ev) == -1) {
        PERROR("epoll_ctl");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        nfds = epoll_wait(efd, events, sizeof(events) / sizeof(events[0]), 1);
        if (nfds == -1) {
            PERROR("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == listenfd) {
                connfd = accept(listenfd,
                                (struct sockaddr *) &peer_addr, &addrlen);
                if (connfd == -1) {
                    PERROR("accept");
                    exit(EXIT_FAILURE);
                }
                set_nonblock(connfd);

                conn_count++;
                PRINT_LOG("accept connection from client %s:%u, conn_count: %d",
                    inet_ntoa(peer_addr.sin_addr), peer_addr.sin_port,
                    conn_count);

                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = connfd;
                if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &ev) == -1) {
                    PERROR("epoll_ctl");
                    exit(EXIT_FAILURE);
                }

            } else if (events[i].events & EPOLLIN) {
                int fd = events[i].data.fd;
                int done = 0;

                for (;;){
                    char buf[1024] = {0};
                    ret = read(fd, buf, sizeof(buf) - 1);

                    if (ret == -1) {
                        // PERROR("read");
                        break;
                    }
                    if (ret == 0){
                        close(fd);
                        done = 1;
                        break;
                    }
                    PRINT_LOG("%s", buf);
                }
                if (done){
                    break;
                }

                ev.events = EPOLLOUT | EPOLLET;
                ev.data.fd = fd;
                if (epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev) == -1) {
                    PERROR("epoll_ctl");
                    exit(EXIT_FAILURE);
                }

            } else if (events[i].events & EPOLLOUT){
                int fd = events[i].data.fd;
                int done = 0;

                for (;;){
                    const char *msg = "hello client ";
                    sleep(1); //test

                    ret = write(fd, msg, strlen(msg));
                    if (ret == -1 || errno == EAGAIN || errno == EPIPE) {
                        // PERROR("write");
                        break;
                    }
                    if (ret == 0) {
                        close(fd);
                        done = 1;
                        break;
                    }
                }
                if (done){
                    break;
                }

                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = fd;
                if (epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev) == -1) {
                    PERROR("epoll_ctl");
                    exit(EXIT_FAILURE);
                }

            } else {
                PRINT_LOG("unknown event");
                int fd = events[i].data.fd;
                close(fd);
                exit(EXIT_FAILURE);

            }
        }
    }
}
