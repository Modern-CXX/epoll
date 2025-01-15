
# epoll_example

```

server: 192.168.1.6
$ g++ -Wall -Wextra server.cpp -std=c++2a -g -o server
$ ./server 8000

client: 192.168.1.7
$ g++ -Wall -Wextra client.cpp -g -o client
$ ./client 192.168.1.6 8000 "foo"
$ ./client 192.168.1.6 8000 "     bar"

```

$ man epoll

    When used as an edge-triggered interface, for performance reasons,  it
    is  possible  to  add  the  file descriptor inside the epoll interface
    (EPOLL_CTL_ADD) once by specifying  (EPOLLIN|EPOLLOUT).   This  allows
    you to avoid continuously switching between EPOLLIN and EPOLLOUT call‐
    ing epoll_ctl(2) with EPOLL_CTL_MOD.

---

https://lkml.iu.edu/hypermail/linux/kernel/0810.3/0913.html

Re: unexpected extra pollout events from epoll
From: Davide Libenzi
Date: Sun Oct 26 2008

> > The best way to do it ATM, is to wait for POLLOUT only when
> > really needed.
>
> I'm a little unclear how to do this. If I set the epoll_wait call to
> wait for just epollin events, that's fine. But when I send a large
> buffer of data and use epoll_ctl to look for epollin|epollout events,
> don't I have the same problem?

You do that by writing data until it's finished, or you get EAGAIN. If you
get EAGAIN, you listen for EPOLLOUT.
Reading is same, but you'd wait for EPOLLIN.

---
