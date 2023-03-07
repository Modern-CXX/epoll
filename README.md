# epoll

```

server: 192.168.1.6
$ g++ -Wall -Wextra server.cpp -std=c++2a -g -o server && ./server

client: 192.168.1.7
$ g++ -Wall -Wextra client.cpp -g -o client && ./client 192.168.1.6 8000 "hello server 111 " 

```
