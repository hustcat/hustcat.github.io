---
layout: post
title: CentOS6.5下VLAN设备的性能问题
date: 2015-06-30 22:00:30
categories: Linux
tags: vlan
excerpt: CentOS6.5下VLAN设备的性能问题。
---

# 问题描述

之前做的一些网络性能的测试都是在三层网络测试的，最近在大二层网络重新测试TDocker的网络性能时，发现物理机的性能比容器还差，在容器内部可以跑60w+，物理机器却只能跑45w+。这与100w+的预期相差太远。

由于在大二层的网络下引入了VLAN设备（由于linux bridge不支持VLAN而引入），所以初步怀疑问题出在VLAN network device。

使用perf看一下，发现dev_queue_xmit中的一个spin lock占用了大量的CPU，达到70%+。
![](/assets/2015-06-30-vlan-performance-problem-1.png)

但是，在3.10.x的内核下却没有这个问题：
![](/assets/2015-06-30-vlan-performance-problem-2.png)

从上面可以看到，在3.10.x内核下，内核spin lock的开销很小。另外，从后者的调用的路径可以看到，spin lock主要出现在sk_buff从VLAN设备下发物理网卡，而不是从协议栈下发VLAN设备。看来，对于CentOS6.5（2.6.32-431），问题主要出现在VLAN设备。

# 原因分析

先看看dev_queue_xmit函数，它是协议栈到底层网络设备的入口。

## dev_queue_xmit

```c
//net/core/dev.c
int dev_queue_xmit(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct netdev_queue *txq;
	struct Qdisc *q;
...
	txq = netdev_pick_tx(dev, skb);
	q = rcu_dereference(txq->qdisc);

	trace_net_dev_queue(skb);
	if (q->enqueue) { ///对于VLAN设备，没有qdisc队列，参考noqueue_qdisc
		rc = __dev_xmit_skb(skb, q, dev, txq);
		goto out;
	}

	/* The device has no queue. Common case for software devices:
	   loopback, all the sorts of tunnels...

	   Really, it is unlikely that netif_tx_lock protection is necessary
	   here.  (f.e. loopback and IP tunnels are clean ignoring statistics
	   counters.)
	   However, it is possible, that they rely on protection
	   made by us here.

	   Check this and shot the lock. It is not prone from deadlocks.
	   Either shot noqueue qdisc, it is even simpler 8)
	 */
	if (dev->flags & IFF_UP) {
		int cpu = smp_processor_id(); /* ok because BHs are off */

		if (txq->xmit_lock_owner != cpu) {

			HARD_TX_LOCK(dev, txq, cpu);

			if (!netif_tx_queue_stopped(txq)) {
				rc = NET_XMIT_SUCCESS;
				if (!dev_hard_start_xmit(skb, dev, txq)) {
					HARD_TX_UNLOCK(dev, txq);
					goto out;
				}
			}
			HARD_TX_UNLOCK(dev, txq);
		} 
	}

	rc = -ENETDOWN;
	rcu_read_unlock_bh();
```

可以看到，内核在把sk_buff下发给网络设备驱动之前，会尝试请求队列的xmit_lock，这是为了防止SMP多个CPU同时给driver下发数据。实际上，大部分driver自身内部已经实现了lock，所以，这里的xmit_lock显得有点多余。所以，内核引入了NETIF_F_LLTX，如果驱动已经实现了lock，就会设置NETIF_F_LLTX标志位，这样，内核在调用dev_queue_xmit时，就不会对xmit_lock加锁了。

## TX_LOCK

```c
#define HARD_TX_LOCK(dev, txq, cpu) {			\
	if ((dev->features & NETIF_F_LLTX) == 0) {	\
		__netif_tx_lock(txq, cpu);		\
	}						\
}

static inline void __netif_tx_lock(struct netdev_queue *txq, int cpu)
{
	spin_lock(&txq->_xmit_lock);
	txq->xmit_lock_owner = cpu;
}
```
从上面的代码可以看到，如果网络设备设置了NETIF_F_LLTX，内核就不会对xmit_lock加锁。

但是CentOS6.5（2.6.32-431）的内核，对于VLAN设备，却没有设置NETIF_F_LLTX，由于VLAN设备只有一个队列，必然导致xmit_lock竞争，使得sys CPU高达70%多。

* 2.6.32-431

```c
static int vlan_dev_init(struct net_device *dev)
{
	struct net_device *real_dev = vlan_dev_info(dev)->real_dev;
...
	/* IFF_BROADCAST|IFF_MULTICAST; ??? */
	dev->flags  = real_dev->flags & ~(IFF_UP | IFF_PROMISC | IFF_ALLMULTI);
	dev->iflink = real_dev->ifindex;
	dev->state  = (real_dev->state & ((1<<__LINK_STATE_NOCARRIER) |
					  (1<<__LINK_STATE_DORMANT))) |
		      (1<<__LINK_STATE_PRESENT);

	dev->features |= real_dev->features & real_dev->vlan_features;
...
```

* 3.10.x

而在3.10.x的内核，对于VLAN设备，也只有一个队列，为什么却没有性能问题呢？

实际上，3.10.x的内核，对于VLAN设备，设置了NETIF_F_LLTX，仅管只有一个队列，也不会有xmit_lock的开销。

```c
static int vlan_dev_init(struct net_device *dev)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
...
	/* IFF_BROADCAST|IFF_MULTICAST; ??? */
	dev->flags  = real_dev->flags & ~(IFF_UP | IFF_PROMISC | IFF_ALLMULTI |
					  IFF_MASTER | IFF_SLAVE);
	dev->iflink = real_dev->ifindex;
	dev->state  = (real_dev->state & ((1<<__LINK_STATE_NOCARRIER) |
					  (1<<__LINK_STATE_DORMANT))) |
		      (1<<__LINK_STATE_PRESENT);

	dev->hw_features = NETIF_F_ALL_CSUM | NETIF_F_SG |
			   NETIF_F_FRAGLIST | NETIF_F_ALL_TSO |
			   NETIF_F_HIGHDMA | NETIF_F_SCTP_CSUM |
			   NETIF_F_ALL_FCOE;

	dev->features |= real_dev->vlan_features | NETIF_F_LLTX;
```

# 查看网络设备features

一般来说，我们可以通过ethtool -k 查看网络设备的feature:

* 2.6.32-431

```sh
# ethtool  -k eth1.11
Features for eth1.11:
rx-checksumming: on
tx-checksumming: on
scatter-gather: on
tcp-segmentation-offload: on
udp-fragmentation-offload: off
generic-segmentation-offload: on
generic-receive-offload: on
large-receive-offload: off
rx-vlan-offload: off
tx-vlan-offload: off
ntuple-filters: off
receive-hashing: off
```

对于CentOS6.5（2.6.32-431），是从/sys/class/net/${ethX}/features读取features：

```sh
#cat /sys/class/net/eth1.11/features
0x114833
--------------------------
1 0001 0100 1000 0011 0011  0x114833
          1 0000 0000 0000  NETIF_F_LLTX    4096
            1000 0000 0000  NETIF_F_GSO     2048
     1 0000 0000 0000 0000  NETIF_F_TSO     1<<16
        100 0000 0000 0000  NETIF_F_GRO     16384
                        01  NETIF_F_SG      1
                        10  NETIF_F_IP_CSUM 2
                    1 0000  NETIF_F_IPV6_CSUM  16
                   10 0000  NETIF_F_HIGHDMA  32
1 0000 0000 0000 0000 0000  NETIF_F_TSO6  (1<<20)
```

可以看到，CentOS6.5的内核对于VLAN设备，没有设置NETIF_F_LLTX标志。

* 3.10.x

对于3.10.x内核，已经没有/sys/class/net/${ethX}/features，但是内核支持ETHTOOL_GFEATURES命令（2.6.32-431不支持该命令），ethtool通过ETHTOOL_GFEATURES获取网络设备的features：

```c
//net/core/ethtool.c
int dev_ethtool(struct net *net, struct ifreq *ifr)
{	
	case ETHTOOL_GFEATURES:
		rc = ethtool_get_features(dev, useraddr);
		break;
```

```sh
# ./ethtool -k eth1.11 | grep tx-lockless
tx-lockless: on [fixed]
# ./ethtool -k eth1 | grep tx-lockless   
tx-lockless: off [fixed]
```

从上面可以确认，3.10.x的内核对VLAN设备的确设置了NETIF_F_LLTX标志。

* ethtool的实现

```c
//ethtool-3.5
static struct feature_state *
get_features(struct cmd_context *ctx, const struct feature_defs *defs)
{
...
	if (defs->n_features) { ///内核支持ETHTOOL_GFEATURES
		state->features.cmd = ETHTOOL_GFEATURES;
		state->features.size = FEATURE_BITS_TO_BLOCKS(defs->n_features);
		err = send_ioctl(ctx, &state->features);
		if (err)
			perror("Cannot get device generic features");
		else
			allfail = 0;
	} else {
		/* We should have got VLAN tag offload flags through
		 * ETHTOOL_GFLAGS.  However, prior to Linux 2.6.37
		 * they were not exposed in this way - and since VLAN
		 * tag offload was defined and implemented by many
		 * drivers, we shouldn't assume they are off.
		 * Instead, since these feature flag values were
		 * stable, read them from sysfs.
		 */
		char buf[20]; ///从/sys/class/net/%s/features读取features
		if (get_netdev_attr(ctx, "features", buf, sizeof(buf)) > 0)
			state->off_flags |=
				strtoul(buf, NULL, 0) &
				(ETH_FLAG_RXVLAN | ETH_FLAG_TXVLAN);
	}


static int get_netdev_attr(struct cmd_context *ctx, const char *name,
		    char *buf, size_t buf_len)
{
#ifdef TEST_ETHTOOL
	errno = ENOENT;
	return -1;
#else
	char path[40 + IFNAMSIZ];
	ssize_t len;
	int fd;

	len = snprintf(path, sizeof(path), "/sys/class/net/%s/%s",
		       ctx->devname, name);
	assert(len < sizeof(path));
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return fd;
	len = read(fd, buf, buf_len - 1);
	if (len >= 0)
		buf[len] = 0;
	close(fd);
	return len;
#endif
}
```

# 参考资料

* https://www.kernel.org/doc/Documentation/networking/netdevices.txt
* [NETIF_F_LLTX and race conditions](https://lwn.net/Articles/121566/)
* http://man7.org/linux/man-pages/man8/ethtool.8.html
* [ethtool 在 Linux 中的实现框架和应用](http://www.ibm.com/developerworks/cn/linux/1304_wangjy_ethtools/)
