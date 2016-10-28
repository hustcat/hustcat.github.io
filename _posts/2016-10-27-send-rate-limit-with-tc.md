---
layout: post
title: Network rate limiting with TC
date: 2016-10-27 12:00:30
categories: Linux
tags: TC
excerpt: Network rate limiting with TC
---

Linux上，我们可以对网络设备设置一定的队列规则(Queueing Disciplines)，从而影响数据的发送行为，比如reschedule, delay或者drop。从而间接达到限速的目的。

## Classless Queueing Disciplines

无分类队列规则是最简单的一类队列规则，这类规则，用户态工具不能对数据流进行分类对待。相反，分类队列规则比较复杂，但可以对数据进行更灵活的控制，比如对来自某个IP的流量限速。这里只是简单的讨论前者。

内核使用`struct Qdisc`来表示队列规则:

```c
struct Qdisc {
	int 			(*enqueue)(struct sk_buff *skb, struct Qdisc *dev); ///skb -> queue
	struct sk_buff *	(*dequeue)(struct Qdisc *dev);
	unsigned int		flags;
	const struct Qdisc_ops	*ops;
///...
}


struct netdev_queue {
/*
 * read mostly part
 */
	struct net_device	*dev;
	struct Qdisc		*qdisc;
///...
}
```

## pfifo_fast

`pfifo_fast`是大多数据网络设备默认的队列规则:

```sh
# tc qdisc show dev eth1
qdisc pfifo_fast 0: root refcnt 2 bands 3 priomap  1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1
```

`pfifo_fast`是使用简单的FIFO策略来管理skb，同时，具有简单的优先级(不受用户态工具控制)。有3个链表用于管理skb，`band 0`，`band 1`和`band 2`。内核根据`skb->priority`计算skb的band值，然后将skb放入对应的链表。在发送时，只要`band 0`有packet，就不会处理`band 1`，依然次类推。

```c
struct Qdisc_ops pfifo_fast_ops __read_mostly = {
	.id		=	"pfifo_fast",
	.priv_size	=	sizeof(struct pfifo_fast_priv),
	.enqueue	=	pfifo_fast_enqueue,
	.dequeue	=	pfifo_fast_dequeue,
	.peek		=	pfifo_fast_peek,
	.init		=	pfifo_fast_init,
	.reset		=	pfifo_fast_reset,
	.dump		=	pfifo_fast_dump,
	.owner		=	THIS_MODULE,
};

#define PFIFO_FAST_BANDS 3

/*
 * Private data for a pfifo_fast scheduler containing:
 * 	- queues for the three band
 * 	- bitmap indicating which of the bands contain skbs
 */
struct pfifo_fast_priv {
	u32 bitmap;
	struct sk_buff_head q[PFIFO_FAST_BANDS]; ///3个skb list
};
```

## Token Bucket Filter(tbf)

`tbf`是一种简单的限速的队列规则，它能够精确的控制网卡的速率。

> TBF is very precise, network- and processor friendly. It should be your first choice if you simply want to slow an interface down!

`tbf`的基本实现如下：

> The TBF implementation consists of a buffer (bucket), constantly filled by some virtual pieces of
> 
> information called tokens, at a specific rate (token rate). The most important parameter of the 
> 
> bucket is its size, that is the number of tokens it can store.
>
> Each arriving token collects one incoming data packet from the data queue and is then deleted from the bucket.


示例：

```sh
## 将eth1的出口带宽限制为1024kbit/s
# tc qdisc add dev eth1 root tbf rate 1024kbit latency 50ms burst 800000

# iperf -c 10.x.x.x -t 20
------------------------------------------------------------
Client connecting to 10.x.x.x, TCP port 5001
TCP window size: 21.9 KByte (default)
------------------------------------------------------------
[  3] local 10.y.y.y port 39434 connected with 10.x.x.x port 5001
[ ID] Interval       Transfer     Bandwidth
[  3]  0.0-21.0 sec  3.00 MBytes  1.20 Mbits/sec

# tc -s -d qdisc ls dev eth1 
qdisc tbf 8006: root refcnt 2 rate 1024Kbit burst 800000b/1 mpu 0b lat 50.0ms 
 Sent 10607570 bytes 12342 pkt (dropped 847, overlimits 0 requeues 0) 
 backlog 0b 0p requeues 0 
```

* 几个参数：

(1) limit or latency

> Limit is the number of bytes that can be queued waiting for tokens to become available. You can 
>
> also specify this the other way around by setting the latency parameter, which specifies the 
>
> maximum amount of time a packet can sit in the TBF. The latter calculation takes into account the 
>
> size of the bucket, the rate and possibly the peakrate (if set).

`limit`为等待可用`token`的数据字节数量，如果等待的数据总量超过这个值，后续的skb会被丢掉，不会进入Qdisc的队列；所以，该值越大，允许等待的数据就会越多，数据被丢掉的概率就相对较小。`latency`表示等待的最大时长，可以根据`latency`计算`limit`的值：

```c
///iproute2
static int tbf_parse_opt(struct qdisc_util *qu, int argc, char **argv, struct nlmsghdr *n)
{
///...
	if (opt.limit == 0) {
		///latency时间内能够传输的大小 + buffer的大小，opt.rate.rate是转化后的以byte单位
		double lim = opt.rate.rate*(double)latency/TIME_UNITS_PER_SEC + buffer;
		if (opt.peakrate.rate) {
			double lim2 = opt.peakrate.rate*(double)latency/TIME_UNITS_PER_SEC + mtu;
			if (lim2 < lim)
				lim = lim2;
		}
		opt.limit = lim;
	}
///...
}
```

(2) burst/buffer/maxburst

> Size of the bucket, in bytes. This is the maximum amount of bytes that tokens can be available for
>
> instantaneously. In general, larger shaping rates require a larger buffer. For 10mbit/s on Intel,
>
>  you need at least 10kbyte buffer if you want to reach your configured rate!
>
> If your buffer is too small, packets may be dropped because more tokens arrive per timer tick than fit in your bucket.

`buffer`的大小，`token`的最大瞬时值。如果一个packet的大小超过这个值，就会被丢掉。必须 >= MTU/B。

(3) rate

速率。以每秒为单位。


```c
///net/sched/sch_tbf.c
static struct Qdisc_ops tbf_qdisc_ops __read_mostly = {
	.next		=	NULL,
	.cl_ops		=	&tbf_class_ops,
	.id		=	"tbf",
	.priv_size	=	sizeof(struct tbf_sched_data),
	.enqueue	=	tbf_enqueue,
	.dequeue	=	tbf_dequeue,
	.peek		=	qdisc_peek_dequeued,
	.drop		=	tbf_drop,
	.init		=	tbf_init,
	.reset		=	tbf_reset,
	.destroy	=	tbf_destroy,
	.change		=	tbf_change,
	.dump		=	tbf_dump,
	.owner		=	THIS_MODULE,
};
```

## Stochastic Fairness Queueing(SFQ)

`SFQ`实现了一个简单的公平队列算法，保证不同的会话(比如TCP会话、UDP流)能够公平的使用有限的带宽，防止某些会话占用过多的带宽。但是，这种方式没有`tbf`精确。

```sh
# tc qdisc add dev ppp0 root sfq perturb 10
```

## 主要参考

* [Network Bandwidth Limiting on Linux with TC](https://chandanduttachowdhury.wordpress.com/2015/08/14/bandwidth-limiting-with-tc-on-linux/)
* [Queueing Disciplines for Bandwidth Management](http://www.tldp.org/HOWTO/Adv-Routing-HOWTO/lartc.qdisc.html)