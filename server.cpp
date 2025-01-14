// server.cpp

// $ g++ -Wall -Wextra server.cpp -std=c++2a -g -o server && ./server

/*

1/4. register the listen_sock using EPOLLIN with epoll,
(do not use EPOLLET so early in here)

2/4. register the new socket using EPOLLIN | EPOLLET,
for reading when accepting new connection when epoll_wait gets listen sock,

3/4. change to EPOLLOUT | EPOLLET, when epoll_wait gets EPOLLIN,

4/4. change to EPOLLIN | EPOLLET, when epoll_wait gets EPOLLOUT,

*/

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PRINT_LOG(fmt, ...)                                                    \
  do {                                                                         \
    printf("%s:%d:%s: " fmt "\n", __FILE__, __LINE__, __func__,                \
           ##__VA_ARGS__);                                                     \
  } while (0) /* ; no trailing semicolon here */

#define PERROR(fmt, ...)                                                       \
  do {                                                                         \
    printf("%s:%d:%s: %s. " fmt "\n", __FILE__, __LINE__, __func__,            \
           strerror(errno), ##__VA_ARGS__);                                    \
  } while (0) /* ; no trailing semicolon here */

#define PORT 8000
#define MAX_EVENTS 10
#define BUFFER_SIZE 1024
int ret;

int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
  int listen_sock, epoll_fd, conn_sock;
  struct epoll_event event, events[MAX_EVENTS];
  struct sockaddr_in my_addr, peer_addr;
  socklen_t addrlen = sizeof(peer_addr);
  int sockopt = 1;

  memset(&my_addr, 0, sizeof(my_addr));
  my_addr.sin_family = AF_INET;
  my_addr.sin_addr.s_addr = INADDR_ANY;
  my_addr.sin_port = htons(PORT);

  listen_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_sock < 0) {
    PERROR("socket");
    exit(EXIT_FAILURE);
  }

  if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &sockopt,
                 sizeof(sockopt)) == -1) {
    PERROR("setsockopt");
    close(listen_sock);
    exit(EXIT_FAILURE);
  }

  if (bind(listen_sock, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
    PERROR("bind");
    close(listen_sock);
    exit(EXIT_FAILURE);
  }

  if (listen(listen_sock, SOMAXCONN) < 0) {
    PERROR("listen");
    close(listen_sock);
    exit(EXIT_FAILURE);
  }

  PRINT_LOG("listen on port: %d", PORT);
  signal(SIGPIPE, SIG_IGN);

  epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    PERROR("epoll_create1");
    close(listen_sock);
    exit(EXIT_FAILURE);
  }

  // 1/4. register the listen_sock using EPOLLIN with epoll ,

  event.events = EPOLLIN;
  event.data.fd = listen_sock;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event) < 0) {
    PERROR("epoll_ctl: listen_sock");
    close(listen_sock);
    exit(EXIT_FAILURE);
  }

  PRINT_LOG("waiting for data input from clients");

  for (;;) {
    int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000); // 1 sec

    if (nfds == -1) {
      PERROR("epoll_wait");
      for (int i = 0; i != sizeof(events) / sizeof(events[0]); i++) {
        close(events[i].data.fd);
      }
      exit(EXIT_FAILURE);
    }

    for (int i = 0; i < nfds; i++) {
      if (events[i].data.fd == listen_sock) {
        conn_sock =
            accept(listen_sock, (struct sockaddr *)&peer_addr, &addrlen);

        if (conn_sock == -1) {
          PERROR("accept");
          continue;
        }

        if (set_nonblocking(conn_sock) == -1) {
          PERROR("set_nonblocking");
          continue;
        }

        PRINT_LOG("accept client %s:%u", inet_ntoa(peer_addr.sin_addr),
                  peer_addr.sin_port);

        // 2/4. register the new socket using EPOLLIN | EPOLLET for reading
        // when accepting new connection ,

        event.events = EPOLLIN | EPOLLET;
        event.data.fd = conn_sock;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &event) == -1) {
          PERROR("epoll_ctl");
        }

      } else {
        char buffer[BUFFER_SIZE];
        int client_sock = events[i].data.fd;

        // partial read or write not handled right now.

        if (events[i].events & EPOLLIN) {
          ret = read(client_sock, buffer, sizeof(buffer));
          PRINT_LOG("%.*s", (int)ret, buffer);

          // 3/4. change to EPOLLOUT | EPOLLET when handling EPOLLIN ,

          event.events = EPOLLOUT | EPOLLET;
          event.data.fd = client_sock;
          epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_sock, &event);

          if (ret == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
              PERROR("read");
              close(client_sock);
            }
          }

          if (ret == 0) {
            PRINT_LOG("Client disconnected: fd=%d", client_sock);
            close(client_sock);
          }
        }

        // do not use `else` to combine the above and the below if statements.
        // we may use both EPOLLIN | EPOLLOUT | EPOLLET when accept new
        // connection. so it may contains both EPOLLIN | EPOLLOUT.

        if (events[i].events & EPOLLOUT) {
          const char *msg = "hello client!";
          ret = write(client_sock, msg, strlen(msg));

          // 4/4. change to EPOLLIN | EPOLLET when handling EPOLLOUT ,

          event.events = EPOLLIN | EPOLLET;
          event.data.fd = client_sock;
          epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_sock, &event);

          if (ret == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
              PERROR("read");
              close(client_sock);
            }
          }

          if (ret == 0) {
            PRINT_LOG("Client disconnected: fd=%d", client_sock);
            close(client_sock);
          }
        }
      }
    }
  }

  close(epoll_fd);
  close(listen_sock);
  return 0;
}
