// server2.cpp

// $ g++ -Wall -Wextra server2.cpp -std=c++2a -g -o server2 && ./server2

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <mutex>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

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

int efd;
std::vector<int> vfd;
std::mutex lock;
int time_to_write = 0;

int set_nonblock(int fd) {
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
  return 0;
}

void update_events() {
  struct epoll_event ev;
  int ret = 0;
  int err = 0;
  for (;;) {
    // sleep(1); //test
    time_to_write = !time_to_write;

    std::lock_guard<std::mutex> guard(lock);
    // PRINT_LOG("vfd: %lu, time_to_write: %d", vfd.size(), time_to_write);

    for (auto fd : vfd) {
      // PRINT_LOG("fd: %d, time_to_write: %d", fd, time_to_write);
      ev.events = EPOLLIN | EPOLLET;
      ev.data.fd = fd;

      if (time_to_write) {
        ev.events = EPOLLOUT | EPOLLET;
      }

      ret = epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev);
      err = errno;
      if (err == EBADF) {
        continue;
      }
      if (ret == -1) {
        PERROR("epoll_ctl");
      }
    }
  }
}

int main() {
  struct epoll_event ev, events[10];
  int listenfd, connfd, nfds;

  /* set up listening socket */
  int port = 8000;
  int ret = 0;
  struct sockaddr_in my_addr, peer_addr;
  socklen_t addrlen = sizeof(peer_addr);
  int sockopt = 1;
  int seq = 0;

  memset(&my_addr, 0, sizeof(my_addr));
  my_addr.sin_port = htons(port);
  my_addr.sin_addr.s_addr = htons(INADDR_ANY);
  my_addr.sin_family = AF_INET;

  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd == -1) {
    PERROR("socket");
    return -1;
  }

  ret =
      setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt));
  if (ret == -1) {
    PERROR("setsockopt");
    close(listenfd);
    return -1;
  }

  ret = bind(listenfd, (struct sockaddr *)&my_addr, sizeof(my_addr));
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
  PRINT_LOG("waiting for data input from clients");

  std::thread(update_events).detach();

  for (;;) {
    nfds = epoll_wait(efd, events, sizeof(events) / sizeof(events[0]),
                      1000); // 1 sec
    // PRINT_LOG("epoll_wait");
    if (nfds == -1) {
      PERROR("epoll_wait");
      for (int i = 0; i != sizeof(events) / sizeof(events[0]); i++) {
        PRINT_LOG();
        close(events[i].data.fd);
      }
      return -1;
    }

    for (int i = 0; i < nfds; i++) {
      if (events[i].data.fd == listenfd) {
        connfd = accept(listenfd, (struct sockaddr *)&peer_addr, &addrlen);
        if (connfd == -1) {
          PERROR("accept");
          continue;
        }
        if (set_nonblock(connfd)) {
          PRINT_LOG("set_nonblock failed");
          continue;
        }

        PRINT_LOG("accept client %s:%u", inet_ntoa(peer_addr.sin_addr),
                  peer_addr.sin_port);

        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = connfd;
        if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &ev) == -1) {
          PERROR("epoll_ctl");
        }

        {
          std::lock_guard<std::mutex> guard(lock);
          vfd.push_back(connfd);
        }

      } else if (events[i].events & (EPOLLHUP | EPOLLERR)) {
        int fd = events[i].data.fd;
        close(fd);

        {
          std::lock_guard<std::mutex> guard(lock);
          std::erase(vfd, fd);
        }

        PRINT_LOG("EPOLLHUP | EPOLLERR");

      } else {
        if (events[i].events & EPOLLIN) {
          int fd = events[i].data.fd;
          enum { count = 1024 };
          char buf[count + 1] = {'\0'}, *p = buf;
          size_t len = 0;

          while (len < count) {
            ret = read(fd, p, count - len);
            int err = errno;
            if (ret > 0) {
              len += ret;
              p += ret;
            }

            if (ret == 0) {
              close(fd);

              {
                std::lock_guard<std::mutex> guard(lock);
                std::erase(vfd, fd);
              }

              break;
            }
            if (err == EAGAIN || err == EWOULDBLOCK || err == EPIPE ||
                err == ECONNRESET) {
              break;
            }
            if (err == EINTR) {
              continue;
            }
            if (err != 0) {
              PRINT_LOG("ret: %d, err: %d, %s", ret, err, strerror(err));
            }
          }

          if (len > 0) {
            PRINT_LOG("%s", buf);
          }
        }

        if (events[i].events & EPOLLOUT) {
          int fd = events[i].data.fd;
          enum { count = 1024 };
          char buf[count + 1] = {'\0'}, *p = buf;
          std::string msg = "hello client " + std::to_string(seq++);
          strncpy(buf, msg.c_str(), count);
          size_t len = 0;

          while (len < count) {
            ret = write(fd, p, count - len);
            int err = errno;
            if (ret > 0) {
              len += ret;
              p += ret;
            }

            if (ret == 0) {
              close(fd);

              {
                std::lock_guard<std::mutex> guard(lock);
                std::erase(vfd, fd);
              }

              break;
            }
            if (err == EAGAIN || err == EWOULDBLOCK || err == EPIPE ||
                err == ECONNRESET) {
              break;
            }
            if (err == EINTR) {
              continue;
            }
            if (err != 0) {
              PRINT_LOG("ret: %d, err: %d, %s", ret, err, strerror(err));
            }
          }
        }
      }
    }
  }

  return 0;
}
