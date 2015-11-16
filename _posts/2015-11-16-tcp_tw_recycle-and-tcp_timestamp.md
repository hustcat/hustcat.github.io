---
layout: post
title: tcp_tw_recycle和tcp_timestamp的问题
date: 2015-11-16 18:16:30
categories: Linux
tags: network
excerpt: tcp_tw_recycle和tcp_timestamp的问题
---

# 问题描述

很多文章都讨论了对外提供服务的服务器，慎用tcp_tw_recycle。这里不再详细讨论，只是简单总结一下。

TCP规范中规定的处于TIME_WAIT的TCP连接必须等待2MSL时间。但在linux中，如果开启了tcp_tw_recycle，TIME_WAIT的TCP连接就不会等待2MSL时间（而是rto或者60s），从而达到快速重用（回收）处于TIME_WAIT状态的tcp连接的目的。这就可能导致连接收到之前连接的数据。为此，linux在打开tcp_tw_recycle的情况下，会记录下TIME_WAIT连接的对端（peer）信息，包括IP地址、时间戳等：

```c
struct inet_peer
{
	/* group together avl_left,avl_right,v4daddr to speedup lookups */
	struct inet_peer	*avl_left, *avl_right;
	__be32			v4daddr;	/* peer's address */
	__u16			avl_height;
	__u16			ip_id_count;	/* IP ID for the next packet */
	struct list_head	unused;
	__u32			dtime;		/* the time of last use of not
						 * referenced entries */
	atomic_t		refcnt;
	atomic_t		rid;		/* Frag reception counter */
	__u32			tcp_ts;
	unsigned long		tcp_ts_stamp;
};

/*
 * Move a socket to time-wait or dead fin-wait-2 state.
 */
void tcp_time_wait(struct sock *sk, int state, int timeo)
{
	struct inet_timewait_sock *tw = NULL;
	const struct inet_connection_sock *icsk = inet_csk(sk);
	const struct tcp_sock *tp = tcp_sk(sk);
	int recycle_ok = 0;
	///tcp_v4_remember_stamp
	if (tcp_death_row.sysctl_tw_recycle && tp->rx_opt.ts_recent_stamp)
		recycle_ok = icsk->icsk_af_ops->remember_stamp(sk);

	if (tcp_death_row.tw_count < tcp_death_row.sysctl_max_tw_buckets)
		tw = inet_twsk_alloc(sk, state);

	if (tw != NULL) {
		struct tcp_timewait_sock *tcptw = tcp_twsk((struct sock *)tw);
		const int rto = (icsk->icsk_rto << 2) - (icsk->icsk_rto >> 1);
...
		/* Linkage updates. */
		__inet_twsk_hashdance(tw, sk, &tcp_hashinfo);

		/* Get the TIME_WAIT timeout firing. */
		if (timeo < rto)
			timeo = rto;
    ///TIME_WAIT连接等待时间
		if (recycle_ok) {
			tw->tw_timeout = rto;
		} else {
			tw->tw_timeout = TCP_TIMEWAIT_LEN; //60s
			if (state == TCP_TIME_WAIT)
				timeo = TCP_TIMEWAIT_LEN;
		}

		inet_twsk_schedule(tw, &tcp_death_row, timeo,
				   TCP_TIMEWAIT_LEN);
		inet_twsk_put(tw); 
…

/* VJ's idea. Save last timestamp seen from this destination
 * and hold it at least for normal timewait interval to use for duplicate
 * segment detection in subsequent connections, before they enter synchronized
 * state.
 */

int tcp_v4_remember_stamp(struct sock *sk)
{
	struct inet_sock *inet = inet_sk(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	struct rtable *rt = (struct rtable *)__sk_dst_get(sk);
	struct inet_peer *peer = NULL;
	int release_it = 0;

	if (!rt || rt->rt_dst != inet->daddr) {
		peer = inet_getpeer(inet->daddr, 1);
		release_it = 1;
	} else {
		if (!rt->peer)
			rt_bind_peer(rt, 1);
		peer = rt->peer;
	}

	if (peer) {
		if ((s32)(peer->tcp_ts - tp->rx_opt.ts_recent) <= 0 ||
		    (peer->tcp_ts_stamp + TCP_PAWS_MSL < get_seconds() &&
		     peer->tcp_ts_stamp <= tp->rx_opt.ts_recent_stamp)) {
			peer->tcp_ts_stamp = tp->rx_opt.ts_recent_stamp;
			peer->tcp_ts = tp->rx_opt.ts_recent;
		}
		if (release_it)
			inet_putpeer(peer);
		return 1;
	}

	return 0;
}
```

这样，当内核收到同一个IP的SYN包时，就会去比较时间戳，检查SYN包的时间戳是否滞后，如果滞后，就将其丢掉（认为是旧连接的数据）：

```c
int tcp_v4_conn_request(struct sock *sk, struct sk_buff *skb)
{
...
		struct inet_peer *peer = NULL;

		/* VJ's idea. We save last timestamp seen
		 * from the destination in peer table, when entering
		 * state TIME-WAIT, and check against it before
		 * accepting new connection request.
		 *
		 * If "isn" is not zero, this request hit alive
		 * timewait bucket, so that all the necessary checks
		 * are made in the function processing timewait state.
		 */
		if (tmp_opt.saw_tstamp &&
		    tcp_death_row.sysctl_tw_recycle &&
		    (dst = inet_csk_route_req(sk, req)) != NULL &&
		    (peer = rt_get_peer((struct rtable *)dst)) != NULL &&
		    peer->v4daddr == saddr) {
			if (get_seconds() < peer->tcp_ts_stamp + TCP_PAWS_MSL &&
			    (s32)(peer->tcp_ts - req->ts_recent) >
							TCP_PAWS_WINDOW) {
				NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_PAWSPASSIVEREJECTED);
				goto drop_and_release;
			}
		}
```

> tmp_opt.saw_tstamp：开启tcp_timestamp（/proc/sys/net/ipv4/tcp_timestamps）
>
> sysctl_tw_recycle:开启tcp_tw_recycle选项（/proc/sys/net/ipv4/tcp_tw_recycle）
>
> TCP_PAWS_MSL：60s，该条件判断表示该源ip的上次tcp连接通信发生在60s内
>
> TCP_PAWS_WINDOW：1，该条件判断表示该源ip的上次tcp连接通信的timestamp 大于本次tcp，即本次连接滞后

![](/assets/2015-11-16-tcp_tw_recycle3.jpg)

这在绝大部分情况下是没有问题的，但是对于我们实际的client-server的服务，访问我们服务的用户一般都位于NAT之后，如果NAT之后有多个用户访问同一个服务，就有可能因为时间戳滞后的连接被丢掉。如下：

```
		(1)send SYN, ts=100						
client1 --------------------->     ---------> (2) ts(NAT)=100
		(3)send SYN, ts=80	   NAT             SERVER
client2 --------------------->     ---------> (4) 发现ts(NAT) > ts(80)，直接丢掉
```

# 问题

那么问题来了，对于对外服务的server，开启了tcp_tw_recycle就一定会导致客客户端连接不上呢？答案是NO。
从上面的分析，可以看到，只有处于TIME_WAIT的连接，内核才会记录peer的信息。Server端如何产生TIME_WAIT的连接？答案是server端主动关闭了client端的连接。

# timestamp

接收端解析tcp_timestamps

```c
/* Look for tcp options. Normally only called on SYN and SYNACK packets.
 * But, this can also be called on packets in the established flow when
 * the fast version below fails.
 */
void tcp_parse_options(struct sk_buff *skb, struct tcp_options_received *opt_rx,
		       int estab)
...
			case TCPOPT_TIMESTAMP:
				if ((opsize == TCPOLEN_TIMESTAMP) &&
				    ((estab && opt_rx->tstamp_ok) ||
				     (!estab && sysctl_tcp_timestamps))) {
					opt_rx->saw_tstamp = 1;
					opt_rx->rcv_tsval = get_unaligned_be32(ptr);
					opt_rx->rcv_tsecr = get_unaligned_be32(ptr + 4);
				}
				break;
```

对于发送端，如果开启了tcp_timestamp，在发送SYN时，就会填充timestamp option：

![](/assets/2015-11-16-tcp_tw_recycle4.jpg)

```c 
/* TCP timestamps are only 32-bits, this causes a slight
 * complication on 64-bit systems since we store a snapshot
 * of jiffies in the buffer control blocks below.  We decided
 * to use only the low 32-bits of jiffies and hide the ugly
 * casts with the following macro.
 */
#define tcp_time_stamp		((__u32)(jiffies))


/* Build a SYN and send it off. */
int tcp_connect(struct sock *sk)
{
...
	/* Send it off. */
	TCP_SKB_CB(buff)->when = tcp_time_stamp;

}


/* Compute TCP options for SYN packets. This is not the final
 * network wire format yet.
 */
static unsigned tcp_syn_options(struct sock *sk, struct sk_buff *skb,
				struct tcp_out_options *opts,
				struct tcp_md5sig_key **md5) {
...
	if (likely(sysctl_tcp_timestamps && *md5 == NULL)) {
		opts->options |= OPTION_TS;
		opts->tsval = TCP_SKB_CB(skb)->when;
		opts->tsecr = tp->rx_opt.ts_recent;
		size += TCPOLEN_TSTAMP_ALIGNED;
	}
```

发送ACK时，如果对方填充timestamp了，也要填充timestamp：

```c
/* The code following below sending ACKs in SYN-RECV and TIME-WAIT states
   outside socket context is ugly, certainly. What can I do?
 */

static void tcp_v4_send_ack(struct sk_buff *skb, u32 seq, u32 ack,
			    u32 win, u32 ts, int oif,
			    struct tcp_md5sig_key *key,
			    int reply_flags)
{
...
	if (ts) {
		rep.opt[0] = htonl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16) |
				   (TCPOPT_TIMESTAMP << 8) |
				   TCPOLEN_TIMESTAMP);
		rep.opt[1] = htonl(tcp_time_stamp);
		rep.opt[2] = htonl(ts);
		arg.iov[0].iov_len += TCPOLEN_TSTAMP_ALIGNED;
	}
```

参考[TCP Timestamp - Demystified](http://ithitman.blogspot.com/2013/02/tcp-timestamp-demystified.html)。

# 测试

我们假设client2、client1位于NAT（10.193.x.x之后），server运行在10.239.x.x，并假设client2的内核先启动，这样就会导致client2的时间戳>client1的时间戳。
然后依次在client2、client1运行测试客户端程序。在server端抓包，结果如下：

![](/assets/2015-11-16-tcp_tw_recycle1.png)

可以看到client1重传了7个SYN包。如果server端不主动close连接，是不会出现这个问题的：
![](/assets/2015-11-16-tcp_tw_recycle2.png)

# 总结

tcp_tw_recycle对于大量的主动外连（关闭）的短连接有价值，这可以防止端口耗尽，对于大多数程序，没有太大用处。
另外，tcp_timestamp并不是导致问题的原因，只不过，tcp_tw_recycle依赖于tcp_timestamp才能生效。如果遇到这个问题，我们只需要关闭tcp_tw_recycle即可，tcp_timestamp对于计算RTT是有用的。

# 测试程序

* [client.c](/assets/tcp_tw_recycle/client.c)
* [server.c](/assets/tcp_tw_recycle/server.c)

# 参考资料
* [tcp_tw_recycle和tcp_timestamps导致connect失败问题](http://blog.sina.com.cn/s/blog_781b0c850100znjd.html)
* [TCP的TIME_WAIT快速回收与重用](http://blog.csdn.net/dog250/article/details/13760985)

