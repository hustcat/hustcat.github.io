---
layout: post
title: Unix domain socket的实现及分片
date: 2015-06-15 19:50:30
categories: Linux
tags: network
excerpt: Unix domain socket的实现及分片。
---

[UNIX本地套接字](http://man7.org/linux/man-pages/man7/unix.7.html)常用来实现本地进程间通信。最近遇到一个业务的问题，稍微深入的研究了一下。

# Unix domain socket的实现

UNIX domain socket在内核的不会走TCP/IP协议栈，类似于双向通信的管道。
sock_sendmsg -> unix_stream_sendmsg

```c
static int unix_stream_sendmsg(struct kiocb *kiocb, struct socket *sock,
			       struct msghdr *msg, size_t len)
{
...
	while (sent < len) {
		/*
		 *	Optimisation for the fact that under 0.01% of X
		 *	messages typically need breaking up.
		 */

		size = len-sent;

		/* Keep two messages in the pipe so it schedules better */
		if (size > ((sk->sk_sndbuf >> 1) - 64))
			size = (sk->sk_sndbuf >> 1) - 64;

		if (size > SKB_MAX_ALLOC)
			size = SKB_MAX_ALLOC; ///16000个字节

		/*
		 *	Grab a buffer
		 */
		///创建sk_buff
		skb = sock_alloc_send_skb(sk, size, msg->msg_flags&MSG_DONTWAIT,
					  &err);
…
		/*
		 *	If you pass two values to the sock_alloc_send_skb
		 *	it tries to grab the large buffer with GFP_NOFS
		 *	(which can fail easily), and if it fails grab the
		 *	fallback size buffer which is under a page and will
		 *	succeed. [Alan]
		 */
		size = min_t(int, size, skb_tailroom(skb));
…
		///拷贝数据
		err = memcpy_fromiovec(skb_put(skb, size), msg->msg_iov, size);
…
		skb_queue_tail(&other->sk_receive_queue, skb); ///加到peer socket的接收队列
		if (max_level > unix_sk(other)->recursion_level)
			unix_sk(other)->recursion_level = max_level;
		unix_state_unlock(other);
		other->sk_data_ready(other, size); ///通知对端
		sent += size;
	} ///end while
```

# 数据分片

## SKB_MAX_ALLOC
对于UNIX domain socket，内核允许每个packet的最大size为SKB_MAX_ALLOC:

```c
		/* Keep two messages in the pipe so it schedules better */
		if (size > ((sk->sk_sndbuf >> 1) - 64))
			size = (sk->sk_sndbuf >> 1) - 64;

		if (size > SKB_MAX_ALLOC)
			size = SKB_MAX_ALLOC; ///16000个字节
```

SKB_MAX_ALLOC的计算很复杂：

```c
#define SKB_DATA_ALIGN(X)	(((X) + (SMP_CACHE_BYTES - 1)) & \
				 ~(SMP_CACHE_BYTES - 1))
#define SKB_WITH_OVERHEAD(X)	\
	((X) - SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))
#define SKB_MAX_ORDER(X, ORDER) \
	SKB_WITH_OVERHEAD((PAGE_SIZE << (ORDER)) - (X)) 
#define SKB_MAX_HEAD(X)		(SKB_MAX_ORDER((X), 0))
#define SKB_MAX_ALLOC		(SKB_MAX_ORDER(0, 2))///16000
```

* SMP_CACHE_BYTES为L1 cache line，一般为64 bytes
* sizeof(struct skb_shared_info) = 344 bytes
* SKB_DATA_ALIGN(sizeof(struct skb_shared_info)) = 384 bytes
* SKB_MAX_ALLOC = 4096*4 – 384 = 16000 bytes

可以看到，SKB_MAX_ALLOC刚好为16000。也就是说，UNIX domain socket，每个packet的max size为16000字节。

## 测试
写了个简单的测试程序，分别尝试发送32000个字节和32002个字节:
![](/assets/2015-06-15-unix-domain-socket.png)  
可以看到，内核会自动进行分片，每个packet最大16000字节。

## kernel trace

简单trace了一个kernel，可以看到，对于每个sk_buff，内核每次请求16344（16000 + sizeof(struct skb_shared_info)）个字节，实际分配16384字节，刚好16K。

```sh
#./tpoint –s kmem:kmalloc_node
             cli-1934  [001]  2796.789510: kmalloc_node: call_site=ffffffff8116ffbd ptr=ffff88007d6ec000 bytes_req=16344 bytes_alloc=16384 gfp_flags=GFP_KERNEL|GFP_REPEAT node=-1
             cli-1934  [001]  2796.789510: <stack trace>
 => kmem_cache_alloc_node_trace
 => __kmalloc_node
 => __alloc_skb
 => sock_alloc_send_pskb
 => sock_alloc_send_skb
 => unix_stream_sendmsg
 => sock_aio_write
 => do_sync_write
             cli-1934  [001]  2796.789545: kmalloc_node: call_site=ffffffff8116ffbd ptr=ffff88007d6ec000 bytes_req=16344 bytes_alloc=16384 gfp_flags=GFP_KERNEL|GFP_REPEAT node=-1
             cli-1934  [001]  2796.789546: <stack trace>
 => kmem_cache_alloc_node_trace
 => __kmalloc_node
 => __alloc_skb
 => sock_alloc_send_pskb
 => sock_alloc_send_skb
=> unix_stream_sendmsg
 => sock_aio_write
 => do_sync_write

```

# 附测试程序

参考[这里](http://troydhanson.github.io/network/Unix_domain_sockets.html)。

```c

/*gcc -o srv srv.c*/
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>

#define BUF_SIZE 32768
char *socket_path = "./socket";
char buf[BUF_SIZE];

int main(int argc, char *argv[]) {
  struct sockaddr_un addr;
  //char buf[100];
  int fd,cl,rc;

  if (argc > 1) socket_path=argv[1];

  if ( (fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
    perror("socket error");
    exit(-1);
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);

  unlink(socket_path);

  if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    perror("bind error");
    exit(-1);
  }

  if (listen(fd, 5) == -1) {
    perror("listen error");
    exit(-1);
  }

  printf("waiting client connect...\n");

  while (1) {
    if ( (cl = accept(fd, NULL, NULL)) == -1) {
      perror("accept error");
      continue;
    }

    while ( (rc=read(cl,buf,sizeof(buf))) > 0) {
      printf("read %u bytes: %.*s\n", rc, rc, buf);
    }
    if (rc == -1) {
      perror("read");
      exit(-1);
    }
    else if (rc == 0) {
      printf("EOF\n");
      close(cl);
    }
  }


  return 0;
}

/**gcc -o cli cli.c*/
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BUF_SIZE 32002
char *socket_path = "./socket";

char buf[BUF_SIZE];

int main(int argc, char *argv[]) {
  struct sockaddr_un addr;
  //char buf[16];
  int fd,rc;

  if (argc > 1) socket_path=argv[1];

  if ( (fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
    perror("socket error");
    exit(-1);
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);

  if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    perror("connect error");
    exit(-1);
  }

  rc = write(fd, buf, BUF_SIZE);
  fprintf(stdout, "send rc = %d\n", rc);

  sleep(2);
  close(fd);

  return 0;
}
```
