// client.cpp

// $ g++ -Wall -Wextra -std=c++2a client.cpp -g -o client
// $ ./client 192.168.1.16 8000 "foo"
// $ ./client 192.168.1.16 8000 "     bar"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
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
  int fd;
  int ret;
  struct sockaddr_in addr;
  const char *host;
  int port;
  int seq = 0;

  if (argc != 4) {
    PRINT_LOG("Usage: %s <host> <port> <client_tag>\n", argv[0]);
    return 1;
  }

  host = argv[1];
  port = atoi(argv[2]);

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    PERROR("socket");
    return -1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_port = htons(port);
  addr.sin_family = AF_INET;

  ret = inet_aton(host, (struct in_addr *)&addr.sin_addr.s_addr);
  if (ret == 0) {
    PRINT_LOG("inet_aton failed");
    return -1;
  }

  ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
  if (ret == -1) {
    PERROR("connect");
    close(fd);
    return -1;
  }

  PRINT_LOG("connect to server %s:%d", host, port);

  set_nonblocking(fd);
  // signal(SIGPIPE, SIG_IGN);

  for (;;) {
    // sleep(1); // test

    // read
    char buf[1024] = {'\0'};
    ret = read(fd, buf, sizeof(buf) - 1);
    if (ret > 0) {
      printf("%s", buf);
    }

    // write
    std::string client_tag = std::string("hello from client ") + argv[3] +
                             std::string("_") + std::to_string(seq++) +
                             std::string("\n");
    ret = write(fd, client_tag.c_str(), client_tag.size());
  }

  ret = close(fd);

  return 0;
}
