---
layout: post
title: sendmsg return EINVAL caused by ARP table full
date: 2016-08-26 16:00:30
categories: Linux
tags: ARP
excerpt: sendmsg return EINVAL caused by ARP table full
---

## 1 问题描述

业务进程在调用sendmsg时，偶尔出EINVAL的错误，概率大概在万分之一：

```
sendmsg(46, {msg_name(16)={sa_family=AF_INET, sin_port=htons(46214), sin_addr=inet_addr("218.x.x.x")}, msg_iov(1)=[{"+\235\205\3
25\0\0\0\0\201#\26\370PP\377\377\375\256\363\2673f\0\t\0\t@\23\1\0\0\26"..., 208}], msg_controllen=0, msg_flags=0}, 0) = 208

sendmsg(46, {msg_name(16)={sa_family=AF_INET, sin_port=htons(37843), sin_addr=inet_addr("125.x.x.x")}, msg_iov(1)=[{"+\235!\240\
0\0\0\0\204\250\266\314PP\377\377\227\263\363\2673f\0\t\0\t@\23\1\0\0\26"..., 208}], msg_controllen=0, msg_flags=0}, 0) = -1 EINVAL
(Invalid argument)
```

相同的参数，第二个返回EINVAL，从strace看不出原因。

## 2 原因分析

Trace sendmsg系统调用的返回值：

```
<...>-43897 [020] d... 20604933.720675: SyS_sendmsg: (tracesys+0xdd/0xe2 <- SyS_sendmsg) arg1=e0
<...>-43895 [017] d... 20604933.720676: SyS_sendmsg: (tracesys+0xdd/0xe2 <- SyS_sendmsg) arg1=100
<...>-43894 [023] d... 20604933.720685: SyS_sendmsg: (tracesys+0xdd/0xe2 <- SyS_sendmsg) arg1=ffffffffffffffea
<...>-43895 [017] d... 20604933.720687: SyS_sendmsg: (tracesys+0xdd/0xe2 <- SyS_sendmsg) arg1=100
<...>-43897 [020] d... 20604933.720689: SyS_sendmsg: (tracesys+0xdd/0xe2 <- SyS_sendmsg) arg1=e0
```

0xffffffffffffffea是-22的十六进制。从这里来看，sendmsg的确偶尔会出-22错误。

再看udp_sendmsg:

```
<...>-43911 [022] d... 20605553.887574: udp_sendmsg: (inet_sendmsg+0x45/0xb0 <- udp_sendmsg) arg1=130
<...>-43897 [019] d... 20605553.887578: udp_sendmsg: (inet_sendmsg+0x45/0xb0 <- udp_sendmsg) arg1=ffffffea
<...>-43913 [015] d... 20605553.887580: udp_sendmsg: (inet_sendmsg+0x45/0xb0 <- udp_sendmsg) arg1=e0
```

同样出现-22。udp_sendmsg是udp socket发送时的入口，是一个比较复杂的函数，调用了很多函数。而且本身也有好几个地方返回-EINVAL。从代码无法确认原因，只能继续往协议栈下层trace。

![](/assets/wangzhe/wangzhe1.png)

从这里来看，每个udp_sendmsg都调用了udp_send_skb。

```c
int udp_sendmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
		size_t len) ///len: msg total length
{
…
	/* Lockless fast path for the non-corking case. */
	if (!corkreq) { ///no cork
		skb = ip_make_skb(sk, fl4, getfrag, msg->msg_iov, ulen,
				  sizeof(struct udphdr), &ipc, &rt,
				  msg->msg_flags);
		err = PTR_ERR(skb);
		if (!IS_ERR_OR_NULL(skb))
			err = udp_send_skb(skb, fl4);
		goto out;
	}
```

udp_send_skb的返回值:

```
<...>-43913 [015] d... 20607931.015006: udp_send_skb: (udp_sendmsg+0x510/0x930 <- udp_send_skb) arg1=0
<...>-43901 [010] d... 20607931.017637: udp_send_skb: (udp_sendmsg+0x510/0x930 <- udp_send_skb) arg1=ffffffea
<...>-43913 [014] d... 20607931.017648: udp_send_skb: (udp_sendmsg+0x510/0x930 <- udp_send_skb) arg1=0
```

udp_send_skb也返回-22，一直往下，到ip_finish_output

```c
static int ip_finish_output(struct sk_buff *skb)
{
#if defined(CONFIG_NETFILTER) && defined(CONFIG_XFRM)
	/* Policy lookup after SNAT yielded a new policy */
	if (skb_dst(skb)->xfrm != NULL) {
		IPCB(skb)->flags |= IPSKB_REROUTED;
		return dst_output(skb);
	}
#endif
	if (skb->len > ip_skb_dst_mtu(skb) && !skb_is_gso(skb))///for UDP, if no UDP, skb_shinfo(skb)->gso_size will be zero
		return ip_fragment(skb, ip_finish_output2);
	else
		return ip_finish_output2(skb);
}
```

```
<...>-43894 [018] d... 20608163.686619: ip_finish_output: (ip_output+0x58/0x90 <- ip_finish_output) arg1=0
<...>-43903 [015] d... 20608163.686622: ip_finish_output: (ip_output+0x58/0x90 <- ip_finish_output) arg1=ffffffea
<...>-43913 [004] d.s. 20608163.686624: ip_finish_output: (ip_output+0x58/0x90 <- ip_finish_output) arg1=0
```

ip_finish_output也返回-22。ip_finish_output会走第二个分支，直接调用ip_finish_output2： 

```c
static inline int ip_finish_output2(struct sk_buff *skb)
{

…
	rcu_read_lock_bh();
	nexthop = (__force u32) rt_nexthop(rt, ip_hdr(skb)->daddr);//ex: gateway L3 address or dest IP
	neigh = __ipv4_neigh_lookup_noref(dev, nexthop);
	if (unlikely(!neigh)) ///create L3->L2 entry
		neigh = __neigh_create(&arp_tbl, &nexthop, dev, false);
	if (!IS_ERR(neigh)) {
		int res = dst_neigh_output(dst, neigh, skb);

		rcu_read_unlock_bh();
		return res;
	}
	rcu_read_unlock_bh();

	net_dbg_ratelimited("%s: No header cache and no neighbour!\n",
			    __func__);
	kfree_skb(skb);
	return -EINVAL;
}
```

从上面的代码来看，只有当__neigh_create返回ERR，ip_finish_output2才会返回-EINVAL。

```c
struct neighbour *__neigh_create(struct neigh_table *tbl, const void *pkey,
				 struct net_device *dev, bool want_ref)
{
	u32 hash_val;
	int key_len = tbl->key_len;
	int error;
	struct neighbour *n1, *rc, *n = neigh_alloc(tbl, dev);
	struct neigh_hash_table *nht;

	if (!n) {
		rc = ERR_PTR(-ENOBUFS);
		goto out;
	}
…
```

从代码来看，当neigh_alloc返回NULL时，会返回-ENOBUFS检查一下neigh_alloc的返回值：

```
<...>-43903 [017] d.s. 20610205.724684: neigh_alloc: (__neigh_create+0x35/0x340 <- neigh_alloc) arg1=ffff880f94656e00
<...>-43894 [020] d.s. 20610205.724697: neigh_alloc: (__neigh_create+0x35/0x340 <- neigh_alloc) arg1=0
<...>-43901 [010] d.s. 20610205.724734: neigh_alloc: (__neigh_create+0x35/0x340 <- neigh_alloc) arg1=ffff8814086aae00
<...>-43897 [021] d.s. 20610205.724746: neigh_alloc: (__neigh_create+0x35/0x340 <- neigh_alloc) arg1=ffff8818782a6c00
```

```
static struct neighbour *neigh_alloc(struct neigh_table *tbl, struct net_device *dev)
{
	struct neighbour *n = NULL;
	unsigned long now = jiffies;
	int entries;

	entries = atomic_inc_return(&tbl->entries) - 1;
	if (entries >= tbl->gc_thresh3 ||
	    (entries >= tbl->gc_thresh2 &&
	     time_after(now, tbl->last_flush + 5 * HZ))) {
		if (!neigh_forced_gc(tbl) &&
		    entries >= tbl->gc_thresh3)
			goto out_entries;
	}
…
out:
	return n;

out_entries:
	atomic_dec(&tbl->entries);
	goto out;
}
```

从代码来看，问题可能出在gc_thresh1，gc_thresh2，gc_thresh3几个参数，检查了一下内核的配置，相对于server的连接数来说，的确偏小。

调大了这几个内核参数，sendmsg再没出现-EINVAL的错误。

## 3 几个问题

### 3.1 内核错误日志

```c
static inline int ip_finish_output2(struct sk_buff *skb)
{
	net_dbg_ratelimited("%s: No header cache and no neighbour!\n",
			    __func__);
	kfree_skb(skb);
	return -EINVAL;
}
```

从代码来看，内核应该会输出上面的日志，而实际上，内核没有输出上面的日志。但输出了下面的日志：

```
[20614262.357823] net_ratelimit: 19 callbacks suppressed
[20614267.918729] net_ratelimit: 11 callbacks suppressed
[20614273.194747] net_ratelimit: 3 callbacks suppressed
[20614278.898950] net_ratelimit: 10 callbacks suppressed
[20614284.709900] net_ratelimit: 5 callbacks suppressed
```

```c
#define net_dbg_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_debug, fmt, ##__VA_ARGS__)
```

从代码来看，只有打开了DEBUG编译选项，net_dbg_ratelimited才会输出日志。

### 3.2 为什么会产生大量的ARP表项？

后来看了一下内核的ARP统计数据，发生产生了大量的ARP表项：

```
# cat /proc/net/stat/arp_cache | awk '{print $1,$2,$3,$4,$5,$6,$7}'
entries allocs destroys hash_grows lookups hits res_failed
00001e37 2963664f2 2936e1d84 00000000 36aa8589 1a117907 00000002
00001e37 14a0a39169 149fe48e05 00000001 00000000 00000000 00000000
…
```

第一列表示ARP表项数量，每个CPU一行。

关键地方在这里：

```c
static inline int ip_finish_output2(struct sk_buff *skb)
{
	nexthop = (__force u32) rt_nexthop(rt, ip_hdr(skb)->daddr);//ex: gateway L3 address or dest IP
	neigh = __ipv4_neigh_lookup_noref(dev, nexthop);
	if (unlikely(!neigh)) ///create L3->L2 entry
		neigh = __neigh_create(&arp_tbl, &nexthop, dev, false);
…

static inline __be32 rt_nexthop(const struct rtable *rt, __be32 daddr)
{
	if (rt->rt_gateway) ///return gateway IP
		return rt->rt_gateway;
	return daddr; /// return dest IP
}
```

从代码，只有当下一跳为网关，才会返回网关地址。而对于接了TGW的服务，会走另外的策略路由，转到tunnel设备。所以，rt_nexthop直接返回client IP。这样，对每个与服务端通信的client，都会建立一条ARP表项。

### 3.3 /proc/net/arp

ARP表项，每个net ns只会得到自己的表项，但实际上，arp_tbl是一个全局变量，如果实现net ns的区分显示呢？
实际上，内核是读取的时候会比较网络设备的net ns：

```c
static int __net_init arp_net_init(struct net *net)
{
	if (!proc_create("arp", S_IRUGO, net->proc_net, &arp_seq_fops)) ///proc/net/arp
		return -ENOMEM;
	return 0;
}

static struct neighbour *neigh_get_next(struct seq_file *seq,
					struct neighbour *n,
					loff_t *pos)
{
	struct neigh_seq_state *state = seq->private;
	struct net *net = seq_file_net(seq);
	struct neigh_hash_table *nht = state->nht;
…
	while (1) {
		while (n) {
			if (!net_eq(dev_net(n->dev), net)) ///check net ns
				goto next;
			if (state->neigh_sub_iter) {
				void *v = state->neigh_sub_iter(state, n, pos);
				if (v)
					return n;
				goto next;
			}
			if (!(state->flags & NEIGH_SEQ_SKIP_NOARP)) ///skip NOARP
				break;

			if (n->nud_state & ~NUD_NOARP) ///dev is set NOARP
				break;
next:
			n = rcu_dereference_bh(n->next);
		}
```

当我们读取/proc/net/arp时，内核会忽略NOARP的邻居表项：

```
static void *arp_seq_start(struct seq_file *seq, loff_t *pos)
{
	/* Don't want to confuse "arp -a" w/ magic entries,
	 * so we tell the generic iterator to skip NUD_NOARP.
	 */
	return neigh_seq_start(seq, pos, &arp_tbl, NEIGH_SEQ_SKIP_NOARP); ///忽略NUD_NOARP
}
```

而tunnel设备，会设置NOARP标志：

```
static void ipip_tunnel_setup(struct net_device *dev)
{
	dev->netdev_ops		= &ipip_netdev_ops;

	dev->type		= ARPHRD_TUNNEL;
	dev->flags		= IFF_NOARP;/// NO ARP
	dev->iflink		= 0;
	dev->addr_len		= 4;
	dev->features		|= NETIF_F_NETNS_LOCAL;
	dev->features		|= NETIF_F_LLTX;
	dev->priv_flags		&= ~IFF_XMIT_DST_RELEASE;

	dev->features		|= IPIP_FEATURES;
	dev->hw_features	|= IPIP_FEATURES;
	ip_tunnel_setup(dev, ipip_net_id);
}


// __neigh_create -> arp_constructor
static int arp_constructor(struct neighbour *neigh)
{
…
	///no ARP, see tunnel device: ipip_tunnel_setup
	if (!dev->header_ops) {///don't need neighbor protocal
		neigh->nud_state = NUD_NOARP;
		neigh->ops = &arp_direct_ops;
		neigh->output = neigh_direct_output;
	}
…
```

所以，通过/proc/net/arp无法得到tunnel设备产生的邻居表项。

### 3.4 /proc/net/stat/arp_cache

ARP表统计数据，只有init netns有该项。

```c
static void neigh_table_init_no_netlink(struct neigh_table *tbl)
{
	tbl->stats = alloc_percpu(struct neigh_statistics);
	if (!tbl->stats)
		panic("cannot create neighbour cache statistics");

#ifdef CONFIG_PROC_FS
	if (!proc_create_data(tbl->id, 0, init_net.proc_net_stat, ///proc/net/stat/arp_cache, only exsit in init.net
			      &neigh_stat_seq_fops, tbl))
		panic("cannot create neighbour proc dir entry");
#endif



static int neigh_stat_seq_show(struct seq_file *seq, void *v)
{
	struct neigh_table *tbl = seq->private;
	struct neigh_statistics *st = v;

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "entries  allocs destroys hash_grows  lookups hits  res_failed  rcv_probes_mcast rcv_probes_ucast  periodic_gc_runs forced_gc_runs unresolved_discards\n");
		return 0;
	}

	seq_printf(seq, "%08x  %08lx %08lx %08lx  %08lx %08lx  %08lx  "
			"%08lx %08lx  %08lx %08lx %08lx\n",
		   atomic_read(&tbl->entries), ///ARP表项数量

		   st->allocs,
		   st->destroys,
		   st->hash_grows,

		   st->lookups,
		   st->hits,

		   st->res_failed,

		   st->rcv_probes_mcast,
		   st->rcv_probes_ucast,

		   st->periodic_gc_runs,
		   st->forced_gc_runs,
		   st->unres_discards
		   );

	return 0;
}
```

### 3.5 /proc/sys/net/ipv4/neigh/xxx

ARP内核配置参数，包括gc_thresh1，gc_thresh2，gc_thresh3等。更多参考[1](http://man7.org/linux/man-pages/man7/arp.7.html)，[2](http://www.serveradminblog.com/2011/02/neighbour-table-overflow-sysctl-conf-tunning/)。

## 4 小结

ARP缓存满，竟然导致上层sendmsg返回EINVAL，第一次遇到。EINVAL错误被内核用坏了，很多时候与-1差不多，让人根本看不出原因。

内核的ARP表项是全局共享的，没有实现net namespace的隔离，使用docker时，应该适当调大其值。

最后，运气比较好，幸好是UDP，逻辑比较简单，如果是TCP，真不知道还能不能找出原因。
