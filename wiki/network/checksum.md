
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

  * 设备不支持硬件校验和计算；
  
  * 设备计算了硬件校验和，但发现该数据帧已经损坏。此时，设备驱动程序可以直接丢弃该数据帧。但有些设备驱动程序（比如e10000/igb/ixbge）却没有丢弃数据帧，而是将`ip_summed`设置为`CHECKSUM_NONE`，然后交给上层协议栈重新计算并处理这种错误。


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

## GSO and CSUM offload

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
				     skb_checksum_help(skb))
					goto out_kfree_skb;
			}
		}
///...
}
```

`ip_summed==CHECKSUM_PARTIAL`表示协议栈并没有计算完校验和，只计算了[伪头](http://www.tcpipguide.com/free/t_TCPChecksumCalculationandtheTCPPseudoHeader-2.htm)，将传输层的数据部分留给了硬件进行计算。如果底层硬件不支持`CSUM`，则`skb_checksum_help`完成计算校验和。
