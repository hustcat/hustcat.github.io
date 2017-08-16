## calculate IP/TCP/UDP checsum

内核计算IP/TCPU/UDP的校验和的方法，参考[How to Calculate IP/TCP/UDP Checksum–Part 1 Theory](http://www.roman10.net/2011/11/27/how-to-calculate-iptcpudp-checksumpart-1-theory/).

简单来说，就是对要计算的数据，以16bit为单元进行累加，然后取反。


TCP收包时，检查校验和：

```
static __sum16 tcp_v4_checksum_init(struct sk_buff *skb)
{
	const struct iphdr *iph = ip_hdr(skb);

	if (skb->ip_summed == CHECKSUM_COMPLETE) {
		if (!tcp_v4_check(skb->len, iph->saddr, ///check TCP/UDP pseudo-header checksum
				  iph->daddr, skb->csum)) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			return 0;
		}
	}

	skb->csum = csum_tcpudp_nofold(iph->saddr, iph->daddr,
				       skb->len, IPPROTO_TCP, 0); ///calc pseudo header checksum

	if (skb->len <= 76) {
		return __skb_checksum_complete(skb); /// 基于伪头累加和，计算整个数据包的checksum
	}
	return 0;
}
```

`csum_tcpudp_nofold`用于计算伪头的checksum，`__skb_checksum_complete`基于伪头累加和(`skb->csum`)计算整个skb的校验和。

* IP CSUM (send)

对于TCP，L4->L3时，调用`ip_queue_xmit`添加IP header， `ip_queue_xmit` -> `ip_local_out` -> `__ip_local_out` -> `ip_send_check`，`ip_send_check`完成`IP header`的`checksum`的计算：

```
/* Generate a checksum for an outgoing IP datagram. */
void ip_send_check(struct iphdr *iph)
{
	iph->check = 0;
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
}
```

* UDP CSUM (send)

`udp_sendmsg` ->  `ip_append_data` -> `__ip_append_data`, `__ip_append_data`在分片拷贝用户数据时，会对每个skb的数据计算checksum:

```
///copy user data -> to
int
ip_generic_getfrag(void *from, char *to, int offset, int len, int odd, struct sk_buff *skb)
{
	struct iovec *iov = from;

	if (skb->ip_summed == CHECKSUM_PARTIAL) { ///HW calc data csum
		if (memcpy_fromiovecend(to, iov, offset, len) < 0)///don't calc csum
			return -EFAULT;
	} else { ///software calc data csum
		__wsum csum = 0;
		if (csum_partial_copy_fromiovecend(to, iov, offset, len, &csum) < 0)
			return -EFAULT;
		skb->csum = csum_block_add(skb->csum, csum, odd);
	}
	return 0;
}
```

`ip_generic_getfrag`作为一个通用函数，本身考虑了`HW CSUM`，即`CHECKSUM_PARTIAL`，则不计算数据的checksum。但是如果用户数据超过PMTU，即发生分片时，`HW CSUM`不再有效，必须进行软件计算checksum.

`udp_sendmsg` -> `udp_push_pending_frames` -> `udp_send_skb`,内核在发送这些分片(skb)之前，会累加所有分片的checksum(skb->csum)，并考虑第一个skb的L4 header和伪头：

```c
///add UDP header
static int udp_send_skb(struct sk_buff *skb, struct flowi4 *fl4)
{
	struct sock *sk = skb->sk;
	struct inet_sock *inet = inet_sk(sk);
	struct udphdr *uh;
	int err = 0;
	int is_udplite = IS_UDPLITE(sk);
	int offset = skb_transport_offset(skb);
	int len = skb->len - offset;
	__wsum csum = 0;

	/*
	 * Create a UDP header
	 */
	uh = udp_hdr(skb);
	uh->source = inet->inet_sport;
	uh->dest = fl4->fl4_dport;
	uh->len = htons(len);
	uh->check = 0;

	if (is_udplite)  				 /*     UDP-Lite      */
		csum = udplite_csum(skb);

	else if (sk->sk_no_check == UDP_CSUM_NOXMIT) {   /* UDP csum disabled */

		skb->ip_summed = CHECKSUM_NONE;
		goto send;

	} else if (skb->ip_summed == CHECKSUM_PARTIAL) { /* UDP hardware csum */

		udp4_hwcsum(skb, fl4->saddr, fl4->daddr);
		goto send;

	} else
		csum = udp_csum(skb);

	/* add protocol-dependent pseudo-header */
	uh->check = csum_tcpudp_magic(fl4->saddr, fl4->daddr, len, ///加上伪头csum
				      sk->sk_protocol, csum);
	if (uh->check == 0)
		uh->check = CSUM_MANGLED_0;

send:
	err = ip_send_skb(sock_net(sk), skb); ///send skb
///..
}

static inline __wsum udp_csum(struct sk_buff *skb)
{
	__wsum csum = csum_partial(skb_transport_header(skb),
				   sizeof(struct udphdr), skb->csum); ///L4 header's csum
        ///frag_list中的skb是没有L4 header的
	for (skb = skb_shinfo(skb)->frag_list; skb; skb = skb->next) {
		csum = csum_add(csum, skb->csum); ///累加所有分片的skb->csum
	}
	return csum;
}
```

* ip_fragment & ip_defrag

一般来说，L4会根据PMTU完成用户数据的分片，但是L4只会对第一个skb添加L3 header，`ip_fragment`需要对`skb->frag_list`中的skb添加L3 header；当然，`ip_fragment`也会做数据切分操作，如果数据超过MTU。

`ip_local_deliver` -> `ip_defrag`，当接收端收到这些分片后，需要进行重组。

TODO: `ip_defrag`对checksum的处理。

## net_device->features

`net_device->features`字段表示设备的各种特性。其中一些位用于表示硬件校验和的计算能力：

```
#define NETIF_F_IP_CSUM		__NETIF_F(HW_CSUM)
#define NETIF_F_IP_CSUM		__NETIF_F(IP_CSUM) ///ipv4 + TCP/UDP
#define NETIF_F_IPV6_CSUM	__NETIF_F(IPV6_CSUM)
```

`NETIF_F_IP_CSUM`表示硬件可以计算L4 checksum，但是只针对IPV4的TCP和UDP。但是一些设备扩展支持VXLAN和NVGRE。
`NETIF_F_IP_CSUM`是一种协议感知的计算checksum的方法。具体来说，上层提供两个CSUM的参数(`csum_start`和`csum_offset`)。

> NETIF_F_HW_CSUM is a protocol agnostic method to offload the transmit checksum. In this method the host 
> provides checksum related parameters in a transmit descriptor for a packet. These parameters include the 
> starting offset of data to checksum and the offset in the packet where the computed checksum is to be written. The 
> length of data to checksum is implicitly the length of the packet minus the starting offset. 

值得一提的是，`igb/ixgbe`使用的`NETIF_F_IP_CSUM`.

## sk_buff

取决于skb是接收封包，还是发送封包，`skb->csum`和`skb->ip_summed`的意义会不同。

```
/*
 *	@csum: Checksum (must include start/offset pair)
 *	@csum_start: Offset from skb->head where checksumming should start
 *	@csum_offset: Offset from csum_start where checksum should be stored
 *	@ip_summed: Driver fed us an IP checksum
 */
struct sk_buff {
	union {
		__wsum		csum;
		struct {
			__u16	csum_start;
			__u16	csum_offset;
		};
	};

	__u8			local_df:1,
				cloned:1,
				ip_summed:2,
				nohdr:1,
				nfctinfo:3;
```

`skb->ip_summed`一般的取值：

```
/* Don't change this without changing skb_csum_unnecessary! */
#define CHECKSUM_NONE 0
#define CHECKSUM_UNNECESSARY 1 ///hardware verified the checksums
#define CHECKSUM_COMPLETE 2
#define CHECKSUM_PARTIAL 3 ///only compute IP header, not include data
```

## 接收时的CSUM

对于接收包,`skb->csum`可能包含L4校验和。`skb->ip_summed`表述L4校验和的状态：

* (1) CHECKSUM_UNNECESSARY

`CHECKSUM_UNNECESSARY`表示底层硬件已经计算了CSUM，以`igb`驱动为例：

`igb_poll` -> `igb_clean_rx_irq` -> `igb_process_skb_fields` -> `igb_rx_checksum`:

```
static inline void igb_rx_checksum(struct igb_ring *ring,
				   union e1000_adv_rx_desc *rx_desc,
				   struct sk_buff *skb)
{
///...
	/* Rx checksum disabled via ethtool */
	if (!(ring->netdev->features & NETIF_F_RXCSUM)) ///关闭RXCSUM
		return;

	/* TCP/UDP checksum error bit is set */
	if (igb_test_staterr(rx_desc,
			     E1000_RXDEXT_STATERR_TCPE |
			     E1000_RXDEXT_STATERR_IPE)) {
		/* work around errata with sctp packets where the TCPE aka
		 * L4E bit is set incorrectly on 64 byte (60 byte w/o crc)
		 * packets, (aka let the stack check the crc32c)
		 */
		if (!((skb->len == 60) &&
		      test_bit(IGB_RING_FLAG_RX_SCTP_CSUM, &ring->flags))) {
			u64_stats_update_begin(&ring->rx_syncp);
			ring->rx_stats.csum_err++;
			u64_stats_update_end(&ring->rx_syncp);
		}
		/* let the stack verify checksum errors，交给协议栈进一步验证csum */
		return;
	}
	/* It must be a TCP or UDP packet with a valid checksum */
	if (igb_test_staterr(rx_desc, E1000_RXD_STAT_TCPCS |
				      E1000_RXD_STAT_UDPCS))
		skb->ip_summed = CHECKSUM_UNNECESSARY; ///stack don't needed verify
}
```

`TCP`层在收到包后，发现`skb->ip_summed`为`CHECKSUM_UNNECESSARY`就不会再检查checksum了:

```
int tcp_v4_rcv(struct sk_buff *skb)
{
///...
	/* An explanation is required here, I think.
	 * Packet length and doff are validated by header prediction,
	 * provided case of th->doff==0 is eliminated.
	 * So, we defer the checks. */
	if (!skb_csum_unnecessary(skb) && tcp_v4_checksum_init(skb))
		goto csum_error;
///...
}

static inline int skb_csum_unnecessary(const struct sk_buff *skb)
{
	return skb->ip_summed & CHECKSUM_UNNECESSARY;
}
```

* (2) CHECKSUM_NONE

`csum`中的校验和无效，可能有以下几种原因： 

> * 设备不支持硬件校验和计算；
>
> * 设备计算了硬件校验和，但发现该数据帧已经损坏。此时，设备驱动程序可以直接丢弃该数据帧。但有些设备驱动程序（比如e10000/igb/ixbge）却没有丢弃数据帧，而是将`ip_summed`设置为`CHECKSUM_NONE`，然后交给上层协议栈重新计算并处理这种错误。

* (3) CHECKSUM_COMPLETE

表明网卡已经计算了L4层报头和payload的校验和，并且`skb->csum`已经被赋值，此时L4层的接收者只需要加伪头并验证校验结果。以TCP为例：

```
static __sum16 tcp_v4_checksum_init(struct sk_buff *skb)
{
	const struct iphdr *iph = ip_hdr(skb);

	if (skb->ip_summed == CHECKSUM_COMPLETE) {
		if (!tcp_v4_check(skb->len, iph->saddr, ///check TCP/UDP pseudo-header checksum
				  iph->daddr, skb->csum)) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			return 0;
		}
	}
///...
}
```

值得一提的，`igb/ixgbe`没有使用`CHECKSUM_COMPLETE`，而是使用的`CHECKSUM_UNNECESSARY`.

注意`CHECKSUM_COMPLETE`和`CHECKSUM_UNNECESSARY`的区别，对于前者，上层还需要计算伪头校验和，再进行验证，见`tcp_v4_check`。实际上，早前的内核版本为`CHECKSUM_HW`，参考[The 2.6.19 process begins](https://lwn.net/Articles/200304/)。

> The CHECKSUM_HW value has long been used in the networking subsystem to support hardware checksumming. That value has been replaced with CHECKSUM_PARTIAL (intended for outgoing packets where the job must be completed by the hardware) and CHECKSUM_COMPLETE (for incoming packets which have been completely checksummed by the hardware).

* Veth的BUG

这里讨论一个有意思的问题：[Linux kernel bug delivers corrupt TCP/IP data to Mesos, Kubernetes, Docker containers](https://tech.vijayp.ca/linux-kernel-bug-delivers-corrupt-tcp-ip-data-to-mesos-kubernetes-docker-containers-4986f88f7a19).

Veth设备会将`CHECKSUM_NONE`改为`CHECKSUM_UNNECESSARY`。这样，就会导致硬件收到损坏的数据帧后，转给veth后，却变成了`CHECKSUM_UNNECESSARY`，上层协议栈（TCP）就不会再计算检查数据包的校验和了。

```
static netdev_tx_t veth_xmit(struct sk_buff *skb, struct net_device *dev)
{
///...

	/* don't change ip_summed == CHECKSUM_PARTIAL, as that
	 * will cause bad checksum on forwarded packets
	 */
	if (skb->ip_summed == CHECKSUM_NONE &&
	    rcv->features & NETIF_F_RXCSUM)
		skb->ip_summed = CHECKSUM_UNNECESSARY;
}
```

veth最初是用于本地通信的设备，一般来说，本地的数据帧不太可能发生损坏。在发送数据时，如果协议栈已经计算校验和，会将`skb->ip_summed`设置为`CHECKSUM_NONE`。所以，对于veth本机通信，接收端没有必要再计算校验和。但是，对于容器虚拟化场景，veth的数据包可能来自网络，如果还这样设置，就会导致损坏的数据帧传给应用层。

## 发送时CSUM

同样，对于发送包,`skb->ip_summed`用于L4校验和的状态，以通知底层网卡是否还需要处理校验和：

* (1) CHECKSUM_NONE

此时，`CHECKSUM_NONE`表示协议栈已经计算了校验和，设备不需要做任何事情。

* (2) CHECKSUM_PARTIAL

`CHECKSUM_PARTIAL`表示使用硬件checksum ，协议栈已经计算L4层的伪头的校验和，并且已经加入uh->check字段中，此时只需要设备计算整个头4层头的校验值。


```
int tcp_sendmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
		size_t size)
{
///...

				/*
				 * Check whether we can use HW checksum.
				 */
				if (sk->sk_route_caps & NETIF_F_ALL_CSUM)
					skb->ip_summed = CHECKSUM_PARTIAL;
}


static int tcp_transmit_skb(struct sock *sk, struct sk_buff *skb, int clone_it,
			    gfp_t gfp_mask)
{
///...
	icsk->icsk_af_ops->send_check(sk, skb); ///tcp_v4_send_check
}


static void __tcp_v4_send_check(struct sk_buff *skb,
				__be32 saddr, __be32 daddr)
{
	struct tcphdr *th = tcp_hdr(skb);

	if (skb->ip_summed == CHECKSUM_PARTIAL) { ///HW CSUM
		th->check = ~tcp_v4_check(skb->len, saddr, daddr, 0); ///add IPv4 pseudo header checksum
		skb->csum_start = skb_transport_header(skb) - skb->head;
		skb->csum_offset = offsetof(struct tcphdr, check);
	} else {
		th->check = tcp_v4_check(skb->len, saddr, daddr,
					 csum_partial(th,
						      th->doff << 2,
						      skb->csum)); ///ip_summed == CHECKSUM_NONE
	}
}

/* This routine computes an IPv4 TCP checksum. */
void tcp_v4_send_check(struct sock *sk, struct sk_buff *skb)
{
	const struct inet_sock *inet = inet_sk(sk);

	__tcp_v4_send_check(skb, inet->inet_saddr, inet->inet_daddr);
}
```

* dev_queue_xmit

最后在`dev_queue_xmit`发送的时候发现设备不支持硬件checksum还会进行软件计算（是否会走这里？）:

```
int dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev,
			struct netdev_queue *txq)
{
///...
		if (netif_needs_gso(skb, features)) {
			if (unlikely(dev_gso_segment(skb, features))) ///GSO(software offload)
				goto out_kfree_skb;
			if (skb->next)
				goto gso;
		} else { ///hardware offload
			if (skb_needs_linearize(skb, features) &&
			    __skb_linearize(skb))
				goto out_kfree_skb;

			/* If packet is not checksummed and device does not
			 * support checksumming for this protocol, complete
			 * checksumming here.
			 */
			if (skb->ip_summed == CHECKSUM_PARTIAL) { ///only header csum is computed
				if (skb->encapsulation)
					skb_set_inner_transport_header(skb,
						skb_checksum_start_offset(skb));
				else
					skb_set_transport_header(skb,
						skb_checksum_start_offset(skb));
				if (!(features & NETIF_F_ALL_CSUM) && ///check hardware if support offload
				     skb_checksum_help(skb)) ///HW not support CSUM
					goto out_kfree_skb;
			}
		}
}
```

`ip_summed==CHECKSUM_PARTIAL`表示协议栈并没有计算完校验和，只计算了[伪头](http://www.tcpipguide.com/free/t_TCPChecksumCalculationandtheTCPPseudoHeader-2.htm)，将传输层的数据部分留给了硬件进行计算。如果底层硬件不支持`CSUM`，则`skb_checksum_help`完成计算校验和。

## Remote checksum

TODO:

## 相关资料

* [Checksum Offloads in the Linux Networking Stack](https://www.kernel.org/doc/Documentation/networking/checksum-offloads.txt)
* [How to Calculate IP/TCP/UDP Checksum–Part 1 Theory](http://www.roman10.net/2011/11/27/how-to-calculate-iptcpudp-checksumpart-1-theory/)
* [Linux kernel bug delivers corrupt TCP/IP data to Mesos, Kubernetes, Docker containers](https://tech.vijayp.ca/linux-kernel-bug-delivers-corrupt-tcp-ip-data-to-mesos-kubernetes-docker-containers-4986f88f7a19)
* [tc netem](https://wiki.linuxfoundation.org/networking/netem)
