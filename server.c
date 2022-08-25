//server.cpp

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

void set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        perror("fcntl");
        exit(-1);
    }

    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) {
        perror("fcntl");
        exit(-1);
    }
}

int main()
{
    struct epoll_event ev, events[10];
    int listenfd, connfd, nfds, efd;

    /* set up listening socket */
    int port = 8000;
    int ret = 0, count = 0;
    struct sockaddr_in my_addr, peer_addr;
    socklen_t addrlen = sizeof(peer_addr);

    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_port = htons(port);
    my_addr.sin_addr.s_addr = htons(INADDR_ANY);
    my_addr.sin_family = AF_INET;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    ret = bind(listenfd, (struct sockaddr*)&my_addr, sizeof(my_addr));
    if (ret == -1) {
        perror("bind");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    ret = listen(listenfd, SOMAXCONN);
    if (ret == -1) {
        perror("listen");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    signal(SIGPIPE, SIG_IGN);

    /* set up epoll */
    efd = epoll_create1(0);
    if (efd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }
    ev.events = EPOLLIN;
    ev.data.fd = listenfd;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &ev) == -1) {
        perror("epoll_ctl listenfd");
        exit(EXIT_FAILURE);
    }
    for (;;) {
        nfds = epoll_wait(efd, events, sizeof(events) / sizeof(events[0]), 1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.fd == listenfd) {
                connfd = accept(listenfd,
                                (struct sockaddr *) &peer_addr, &addrlen);
                if (connfd == -1) {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }
                set_nonblock(connfd);
                count++; //
                printf("accept connection from client %s:%d, count: %d\n",
                    inet_ntoa(peer_addr.sin_addr), peer_addr.sin_port, count);

                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = connfd;
                if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &ev) == -1) {
                    perror("epoll_ctl connfd");
                    exit(EXIT_FAILURE);
                }

            } else if (events[n].events & EPOLLIN) {
                //errno == EAGAIN || errno == EWOULDBLOCK
                char buf[1024] = {0};
                ret = read(events[n].data.fd, buf, sizeof(buf) - 1);

                if (strlen(buf) > 0)
                    printf("%s\n", buf);

                ev.events = EPOLLOUT | EPOLLET;
                ev.data.fd = events[n].data.fd;
                epoll_ctl(efd, EPOLL_CTL_MOD, events[n].data.fd, &ev);

            } else if (events[n].events & EPOLLOUT){
                //errno == EAGAIN || errno == EWOULDBLOCK
                sleep(1); //test
                const char *msg = "hello client ";
                ret = write(events[n].data.fd, msg, strlen(msg));

                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = events[n].data.fd;
                epoll_ctl(efd, EPOLL_CTL_MOD, events[n].data.fd, &ev);

            } else {
                printf("unknown event");
                exit(EXIT_FAILURE);

            }
        }
    }
}
