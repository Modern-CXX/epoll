// server.cpp

// $ g++ -Wall -Wextra server.cpp -std=c++2a -g -o server && ./server

/*

1/4. register the listen_sock using EPOLLIN with epoll,
(do not use EPOLLET so early in here)

2/4. register the new socket using EPOLLIN | EPOLLET,
for reading when accepting new connection when epoll_wait gets listen sock,

3/4. change to EPOLLOUT | EPOLLET, when epoll_wait gets EPOLLIN,

4/4. change to EPOLLIN | EPOLLET, when epoll_wait gets EPOLLOUT,

note:
do not use `else` to combine two if statements into one.
because it may contains both EPOLLIN | EPOLLOUT,
and we need to handle both at the same time:
  if (events[i].events & EPOLLIN) {...}
  `else` if (events[i].events & EPOLLOUT) {...}


*/

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <mutex>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#define PRINT_LOG(fmt, ...)                                                    \
  do {                                                                         \
    printf("%s:%d:%s: " fmt "\n", __FILE__, __LINE__, __func__,                \
           ##__VA_ARGS__);                                                     \
  } while (0) /* ; no trailing semicolon here */

#define PORT 8000
#define MAX_EVENTS 10
#define BUFFER_SIZE 1024

int set_non_blocking(int sockfd) {
  int flags = fcntl(sockfd, F_GETFL, 0);
  return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
  int listen_sock, epoll_fd, conn_sock;
  struct epoll_event event, events[MAX_EVENTS];
  struct sockaddr_in my_addr, peer_addr;
  socklen_t addrlen = sizeof(peer_addr);

  listen_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_sock < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  memset(&my_addr, 0, sizeof(my_addr));
  my_addr.sin_family = AF_INET;
  my_addr.sin_addr.s_addr = htons(INADDR_ANY);
  my_addr.sin_port = htons(PORT);

  if (bind(listen_sock, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
    perror("bind");
    close(listen_sock);
    exit(EXIT_FAILURE);
  }

  if (listen(listen_sock, SOMAXCONN) < 0) {
    perror("listen");
    close(listen_sock);
    exit(EXIT_FAILURE);
  }

  PRINT_LOG("listen on port: %d", PORT);
  signal(SIGPIPE, SIG_IGN);

  epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    perror("epoll_create1");
    close(listen_sock);
    exit(EXIT_FAILURE);
  }

  // 1/4. register the listen_sock using EPOLLIN with epoll ,

  event.events = EPOLLIN;
  event.data.fd = listen_sock;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event) < 0) {
    perror("epoll_ctl: listen_sock");
    close(listen_sock);
    exit(EXIT_FAILURE);
  }

  PRINT_LOG("waiting for data input from clients");

  for (;;) {
    int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000); // 1 sec
    for (int i = 0; i < n; i++) {
      if (events[i].data.fd == listen_sock) {
        conn_sock =
            accept(listen_sock, (struct sockaddr *)&peer_addr, &addrlen);
        set_non_blocking(conn_sock);

        PRINT_LOG("accept client %s:%u", inet_ntoa(peer_addr.sin_addr),
                  peer_addr.sin_port);

        // 2/4. register the new socket using EPOLLIN | EPOLLET for reading
        // when accepting new connection ,

        event.events = EPOLLIN | EPOLLET; // EPOLLIN | EPOLLOUT | EPOLLET
        event.data.fd = conn_sock;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &event);

        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          perror("accept");
        }
      } else {
        char buffer[BUFFER_SIZE];
        ssize_t count;
        int client_sock = events[i].data.fd;

        if (events[i].events & EPOLLIN) {
          while ((count = read(client_sock, buffer, sizeof(buffer))) > 0) {
            printf("Received: %.*s\n", (int)count, buffer);

            // 3/4. change to EPOLLOUT | EPOLLET when handling EPOLLIN ,

            event.events = EPOLLOUT | EPOLLET;
            event.data.fd = client_sock;
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_sock, &event);
          }

          if (count == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
              perror("read");
              close(client_sock);
            }
          } else if (count == 0) {
            printf("Client disconnected: fd=%d\n", client_sock);
            close(client_sock);
          }
        }

        // do not use `else` to combine two separate if statements into one.
        // we may use both EPOLLIN | EPOLLOUT when accept new connection.
        // so it may contains both EPOLLIN | EPOLLOUT.
        //   event.events = EPOLLIN | EPOLLOUT | EPOLLET

        if (events[i].events & EPOLLOUT) {
          const char *msg = "hello client!\n";
          ssize_t written = write(client_sock, msg, strlen(msg));
          if (written == -1) {
            perror("write");
          }

          // 4/4. change to EPOLLIN | EPOLLET when handling EPOLLOUT ,

          event.events = EPOLLIN | EPOLLET;
          event.data.fd = client_sock;
          epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_sock, &event);
        }
      }
    }
  }

  close(epoll_fd);
  close(listen_sock);
  return 0;
}
