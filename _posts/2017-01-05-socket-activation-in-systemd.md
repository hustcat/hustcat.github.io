---
layout: post
title: Socket activation in systemd
date: 2017-01-05 11:00:30
categories: Linux
tags: systemd
excerpt: Socket activation in systemd
---

## Introduction

`systemd`为了加快系统的启动速度，使用[socket activation](http://0pointer.de/blog/projects/socket-activation.html)的方式让所有系统服务并发启动。`socket activation`的思想由来所久，`inetd`使用它来实现按需启动网络服务。

`socket activation`的核心在于将创建`listen socket`的过程从`service daemon`移到systemd，即使该服务本身没有启动，其它依赖的服务也可以连接`listen socket`，然后`systemd`创建服务进程，并将`listen socket`转给该daemon进程，由后者处理`listen socket`的各种请求。这样使得所有的服务守护进程都可以同时启动。

> Socket activation makes it possible to start all four services completely simultaneously, without 
> any kind of ordering. Since the creation of the listening sockets is moved outside of the daemons 
> themselves we can start them all at the same time, and they are able to connect to each other's 
> sockets right-away.

![](/assets/systemd/sd-daemon-1.png)

## Write socket activation daemon

使用`socket activation`的服务，必须从`systemd`接收socket，而不是自己创建socket.

> A service capable of socket activation must be able to receive its preinitialized sockets from 
> systemd, instead of creating them internally. 


* NON-SOCKET-ACTIVATABLE SERVICE

非`socket activation`的服务一般是自己创建socket:

```c
/* Source Code Example #1: ORIGINAL, NOT SOCKET-ACTIVATABLE SERVICE */
...
union {
        struct sockaddr sa;
        struct sockaddr_un un;
} sa;
int fd;

fd = socket(AF_UNIX, SOCK_STREAM, 0);
if (fd < 0) {
        fprintf(stderr, "socket(): %m\n");
        exit(1);
}

memset(&sa, 0, sizeof(sa));
sa.un.sun_family = AF_UNIX;
strncpy(sa.un.sun_path, "/run/foobar.sk", sizeof(sa.un.sun_path));

if (bind(fd, &sa.sa, sizeof(sa)) < 0) {
        fprintf(stderr, "bind(): %m\n");
        exit(1);
}

if (listen(fd, SOMAXCONN) < 0) {
        fprintf(stderr, "listen(): %m\n");
        exit(1);
}
...
```

这种方式下，其它依赖的服务必须在该daemon进程启动后之能访问该服务。

* SOCKET-ACTIVATABLE SERVICE

```c
/* Source Code Example #2: UPDATED, SOCKET-ACTIVATABLE SERVICE */
...
#include "sd-daemon.h"
...
int fd;

if (sd_listen_fds(0) != 1) { ///使用systemd创建的socket
        fprintf(stderr, "No or too many file descriptors received.\n");
        exit(1);
}

fd = SD_LISTEN_FDS_START + 0;
...
```

这种方式下，传统的启动服务服务方式将不再可用。为了兼容两种方式，可以使用下面的方法：

* SOCKET-ACTIVATABLE SERVICE WITH COMPATIBILITY

```c
/* Source Code Example #3: UPDATED, SOCKET-ACTIVATABLE SERVICE WITH COMPATIBILITY */
...
#include "sd-daemon.h"
...
int fd, n;

n = sd_listen_fds(0);
if (n > 1) {
        fprintf(stderr, "Too many file descriptors received.\n");
        exit(1);
} else if (n == 1)
        fd = SD_LISTEN_FDS_START + 0;
else {
        union {
                struct sockaddr sa;
                struct sockaddr_un un;
        } sa;

        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
                fprintf(stderr, "socket(): %m\n");
                exit(1);
        }

        memset(&sa, 0, sizeof(sa));
        sa.un.sun_family = AF_UNIX;
        strncpy(sa.un.sun_path, "/run/foobar.sk", sizeof(sa.un.sun_path));

        if (bind(fd, &sa.sa, sizeof(sa)) < 0) {
                fprintf(stderr, "bind(): %m\n");
                exit(1);
        }

        if (listen(fd, SOMAXCONN) < 0) {
                fprintf(stderr, "listen(): %m\n");
                exit(1);
        }
}
...
```

完整程序参考[这里](https://github.com/hustcat/golangexample/blob/master/sd-daemon/server.c)。另外，[这里](https://github.com/coreos/go-systemd/tree/master/examples/activation/httpserver)还有一个Go语言的示例。

* Enable service in systemd

创建socket unit file:

```sh
# cat /etc/systemd/system/foobar.socket 
[Socket]
ListenStream=/run/foobar.sk

[Install]
WantedBy=sockets.target
```

创建对应的service file:

```sh
# cat /etc/systemd/system/foobar.service 
[Service]
ExecStart=/usr/local/bin/foobard
```

启动socket:

```sh
# systemctl enable foobar.socket
# systemctl start foobar.socket
# systemctl status foobar.socket
● foobar.socket
   Loaded: loaded (/etc/systemd/system/foobar.socket; enabled; vendor preset: disabled)
   Active: active (listening) since 四 2017-01-05 18:59:41 CST; 31s ago
   Listen: /run/foobar.sk (Stream)

1月 05 18:59:41 centos7 systemd[1]: Listening on foobar.socket.
1月 05 18:59:41 centos7 systemd[1]: Starting foobar.socket.

# lsof /run/foobar.sk 
COMMAND PID USER   FD   TYPE             DEVICE SIZE/OFF  NODE NAME
systemd   1 root   28u  unix 0xffff88002db53400      0t0 29058 /run/foobar.sk
```

可以看到`systemd`创建了对应的socket。但此时，`foobard`进程并没有启动。

当我们连接`/run/foobar.sk`时，`foobard`进程就会被`systemd`拉起：

```sh
# socat - unix-connect:/run/foobar.sk 
hello world
hello world
again
again


# ps -ef|grep foob
root      3589  3338  0 19:01 pts/1    00:00:00 socat - unix-connect:/run/foobar.sk
root      3590     1  0 19:01 ?        00:00:00 /usr/local/bin/foobard
```

## Internal

`systemd`在启动服务进程前，会设置环境变量`LISTEN_PID`和`LISTEN_FDS`:

```c
static int build_environment(
                const ExecContext *c,
                unsigned n_fds,
                usec_t watchdog_usec,
                const char *home,
                const char *username,
                const char *shell,
                char ***ret) {

        _cleanup_strv_free_ char **our_env = NULL;
        unsigned n_env = 0;
        char *x;

        assert(c);
        assert(ret);

        our_env = new0(char*, 10);
        if (!our_env)
                return -ENOMEM;

        if (n_fds > 0) {
                if (asprintf(&x, "LISTEN_PID="PID_FMT, getpid()) < 0)
                        return -ENOMEM;
                our_env[n_env++] = x;

                if (asprintf(&x, "LISTEN_FDS=%u", n_fds) < 0)
                        return -ENOMEM;
                our_env[n_env++] = x;
        }
...
```

服务进程通过`sd_listen_fds`获取对应的环境变量：

```c
_public_ int sd_listen_fds(int unset_environment) {
        const char *e;
        int n, r, fd;
        pid_t pid;

        e = getenv("LISTEN_PID");
        if (!e) {
                r = 0;
                goto finish;
        }

        r = parse_pid(e, &pid);
        if (r < 0)
                goto finish;

        /* Is this for us? */
        if (getpid() != pid) {
                r = 0;
                goto finish;
        }

        e = getenv("LISTEN_FDS");
        if (!e) {
                r = 0;
                goto finish;
        }
...
```

## Reference

* [systemd for Developers I: Socket Activation](http://0pointer.de/blog/projects/socket-activation.html)
* [systemd for Developers II](http://0pointer.de/blog/projects/socket-activation2.html)