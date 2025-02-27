// server.cpp
//
// $ ./server 8000
//
// epoll(7): waiting for an event only after read(2) or write(2) return EAGAIN.
//
// EAGAIN:
// 1. On the server, when there is no data to read or when there is a large
// amount of data to write, EAGAIN will be encountered. This indicates that the
// server cannot read or write more data at the moment. The server should return
// to epoll_wait to wait for the next EPOLLIN or EPOLLOUT event before
// attempting to read or write more data again.
//
// When server returns to epoll_wait():
// - server will receive EPOLLIN if socket file descriptor changes from
// non-readable to readable, which occurs when client writes data to server;
// - or server will receive EPOLLOUT if socket file descriptor changes from
// non-writable to writable, which occurs when client reads data from server.
//
// 2. On the server, if there is only a small amount of data to write and EAGAIN
// is not encountered, the server can definitively return to epoll_wait after
// writing the data. However, in edge-triggered mode, the server should register
// for EPOLLOUT only when there is data to write.
//
// 3. (Starvation) On the server, if there is a large amount of data to read
// from one client, it may indicate that the client is continuously writing, and
// the server is continuously reading. In this case, both the server and the
// client are actively reading and writing, and EAGAIN may not be encountered.
// However, this may starve socket file descriptors of other connections.
//
// - Maintain a new ready list (not the ready list of epoll(7) itself).
// - When epoll_wait receives an EPOLLIN or EPOLLOUT event on a file descriptor,
// add it to the new ready list if it is not already there.
// - Use a round-robin approach based on fairness of time slice or data length
// among the file descriptors in the new ready list. Read or write until EAGAIN
// is encountered.
// - When EAGAIN is encountered, remove the file descriptor from the new ready
// list and return to epoll_wait.
// - In edge-triggered mode, epoll only notifies when a socket becomes readable
// or writable. Receiving EPOLLIN or EPOLLOUT from epoll_wait is not a sign to
// read or write immediately, but a sign to add the file descriptor to the new
// ready list. Read or write on the file descriptors in the new ready list until
// EAGAIN is encountered.
//
// Thundering Herd:
// epoll with edge-triggered mode provides a useful optimization for avoiding
// thundering herd.
//

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
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

int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    PRINT_LOG("Usage: %s <port>", argv[0]);
    return -1;
  }

  int listen_sock, epoll_fd, conn_sock;
  struct epoll_event event, events[256];
  struct sockaddr_in my_addr, peer_addr;
  socklen_t addrlen = sizeof(peer_addr);
  int sockopt = 1;
  int port = atoi(argv[1]);

  memset(&my_addr, 0, sizeof(my_addr));
  my_addr.sin_family = AF_INET;
  my_addr.sin_addr.s_addr = INADDR_ANY;
  my_addr.sin_port = htons(port);

  listen_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_sock < 0) {
    PERROR("socket");
    return -1;
  }

  if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &sockopt,
                 sizeof(sockopt)) == -1) {
    PERROR("setsockopt");
    close(listen_sock);
    return -1;
  }

  if (bind(listen_sock, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
    PERROR("bind");
    close(listen_sock);
    return -1;
  }

  if (listen(listen_sock, SOMAXCONN) < 0) {
    PERROR("listen");
    close(listen_sock);
    return -1;
  }

  PRINT_LOG("listen on port: %d", port);
  signal(SIGPIPE, SIG_IGN);

  epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    PERROR("epoll_create1");
    close(listen_sock);
    return -1;
  }

  event.events = EPOLLIN;
  event.data.fd = listen_sock;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event) < 0) {
    PERROR("epoll_ctl: listen_sock");
    close(listen_sock);
    return -1;
  }

  for (;;) {
    // sleep(1); // test

    int nfds =
        epoll_wait(epoll_fd, events, sizeof(events) / sizeof(events[0]), -1);

    if (nfds == -1) {
      PERROR("epoll_wait");
      for (int i = 0; i != sizeof(events) / sizeof(events[0]); i++) {
        close(events[i].data.fd);
      }
      return -1;
    }

    for (int i = 0; i < nfds; i++) {
      if (events[i].data.fd == listen_sock) {
        conn_sock =
            accept(listen_sock, (struct sockaddr *)&peer_addr, &addrlen);

        if (conn_sock == -1) {
          PERROR("accept");
          continue;
        }

        PRINT_LOG("accept client %s:%u, fd:%d", inet_ntoa(peer_addr.sin_addr),
                  peer_addr.sin_port, conn_sock);

        if (set_nonblocking(conn_sock) == -1) {
          PERROR("set_nonblocking");
          continue;
        }

        event.events = EPOLLIN | EPOLLOUT | EPOLLET;
        event.data.fd = conn_sock;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &event) == -1) {
          PERROR("epoll_ctl: conn_sock");
          close(conn_sock);
          continue;
        }

      } else {
        int client_sock = events[i].data.fd;
        int ret = 0;

        if (events[i].events & EPOLLIN) {
          std::string msg;

          for (;;) {
            char buf[1024] = {'\0'};
            int count = sizeof(buf) - 1;
            ret = read(client_sock, buf, count);

            if (ret == -1) {
              if (errno == EAGAIN) {
                break;
              } else if (errno == EINTR) {
                continue;
              } else {
                PERROR("read");
                close(client_sock);
                break;
              }
            } else if (ret == 0) {
              PRINT_LOG("client disconnected: fd:%d", client_sock);
              close(client_sock);
              break;
            }

            msg += buf;
          }

          if (!empty(msg)) {
            printf("fd:%d, %s", client_sock, msg.c_str());
          }
        }

        if (events[i].events & EPOLLOUT) {
          const char *msg = "hello client\n";

          for (;;) {
            const char *buf = msg;
            int count = strlen(msg);
            ret = write(client_sock, buf, count);

            if (ret == -1) {
              if (errno == EAGAIN) {
                break;
              } else if (errno == EINTR) {
                continue;
              } else {
                PERROR("write");
                close(client_sock);
                break;
              }
            } else if (ret == 0) {
              PRINT_LOG("client disconnected: fd:%d", client_sock);
              close(client_sock);
              break;
            }

            if (ret < count) {
              buf += ret;
              count -= ret;
            }
          }
        }
      }
    }
  }

  close(epoll_fd);
  close(listen_sock);
  return 0;
}
