---
layout: post
title: Linux vlan and macvlan implementation
date: 2016-03-16 20:00:30
categories: Linux
tags: network vlan macvlan
excerpt: Linux vlan and macvlan implementation
---

# VLAN

## Protocol

802.1Q定义了一个新的以太网帧字段,这个字段添加在以太网帧的源MAC之后,长度/类型字段之前。封装具体内容如图所示:
 
![](/assets/vlan_macvlan/vlan_and_macvlan_00.png)

802.1Q封装共4个字节,包含2个部分:TPID(Etype),Tag Control Info。

* TPID

长度为2个字节,固定为0x8100,标识报文的封装类型为以太网的802.1Q封装;

* Tag Control Info

包含三个部分:802.1P优先级、CFI、VLAN-ID;

(1) 802.1P Priority:这3位指明帧的优先级。一共有8种优先级,取值范围为0~7,,主要用于当交换机出端口发生拥塞时,交换机通过识别该优先级,优先发送优先级高的数据包。 

(2) CFI:以太网交换机中,规范格式指示器总被设置为0。由于兼容特性,CFI常用于以太网类网络和令牌环类网络之间,如果在以太网端口接收的帧具有CFI,那么设置为1,表示该帧不进行转发,这是因为以太网端口是一个无标签端口。

(3) VID:VLAN ID是对VLAN的识别字段,在标准802.1Q中常被使用。该字段为12位。支持4096(2^12)VLAN的识别。在4096可能的VID中,VID=0用于识别帧优先级,4095(FFF)作为预留值, 所以VLAN配置的最大可能值为4094。


* VLAN header

```c
///linux/vlan_if.h
/*
 * 	struct vlan_hdr - vlan header
 * 	@h_vlan_TCI: priority and VLAN ID
 *	@h_vlan_encapsulated_proto: packet type ID or len
 */
struct vlan_hdr {
	__be16	h_vlan_TCI;
	__be16	h_vlan_encapsulated_proto; /// 封装的内部协议，比如ETH_P_IP
};
```

* VLAN ethernet header

包括vlan header和ethernet header

```c
/**
 *	struct vlan_ethhdr - vlan ethernet header (ethhdr + vlan_hdr)
 *	@h_dest: destination ethernet address
 *	@h_source: source ethernet address
 *	@h_vlan_proto: ethernet protocol
 *	@h_vlan_TCI: priority and VLAN ID
 *	@h_vlan_encapsulated_proto: packet type ID or len
 */
struct vlan_ethhdr {
	unsigned char	h_dest[ETH_ALEN];
	unsigned char	h_source[ETH_ALEN];
	__be16		h_vlan_proto; ///ex: ETH_P_8021Q
	__be16		h_vlan_TCI;
	__be16		h_vlan_encapsulated_proto; ///ex: ETH_P_IP
};
```

示例：

![](/assets/vlan_macvlan/vlan_and_macvlan_01.png)

## ingress process

内核收到带有VLAN ID的数据帧时，会根据VLAN ID找到对应的VLAN device，然后将sk_buff->dev设置为vlan device：

```c
///net/core/dev.c
static int __netif_receive_skb_core(struct sk_buff *skb, bool pfmemalloc)
{
…
another_round:
	skb->skb_iif = skb->dev->ifindex;

///vlan untag
	if (skb->protocol == cpu_to_be16(ETH_P_8021Q) ||
	    skb->protocol == cpu_to_be16(ETH_P_8021AD)) {
		skb = vlan_untag(skb);
		if (unlikely(!skb))
			goto unlock;
	}

...
	///vlan tag exsit
	if (vlan_tx_tag_present(skb)) {
		if (pt_prev) {
			ret = deliver_skb(skb, pt_prev, orig_dev);
			pt_prev = NULL;
		}
		if (vlan_do_receive(&skb))
			goto another_round;
		else if (unlikely(!skb))
			goto unlock;
	}
...

///net/8021q/vlan_core.c
bool vlan_do_receive(struct sk_buff **skbp)
{
	struct sk_buff *skb = *skbp;
	__be16 vlan_proto = skb->vlan_proto;
	u16 vlan_id = vlan_tx_tag_get_id(skb);
	struct net_device *vlan_dev;
	struct vlan_pcpu_stats *rx_stats;
	///根据VLAN id找到对应的vlan device
	vlan_dev = vlan_find_dev(skb->dev, vlan_proto, vlan_id);
	if (!vlan_dev)
		return false;

	skb = *skbp = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(!skb))
		return false;
	///vlan device
	skb->dev = vlan_dev;

	///vlan device可以设置不同的MAC address
	if (skb->pkt_type == PACKET_OTHERHOST) {
		/* Our lower layer thinks this is not local, let's make sure.
		 * This allows the VLAN to have a different MAC than the
		 * underlying device, and still route correctly. */
		if (ether_addr_equal(eth_hdr(skb)->h_dest, vlan_dev->dev_addr))
			skb->pkt_type = PACKET_HOST;
	}
...
```


# MACVLAN on VLAN

Macvlan设备本身不支持VLAN ID，不能对数据帧tag/untag vlan ID，需要通过VLAN设备来实现。

![](/assets/vlan_macvlan/vlan_and_macvlan_02.jpg)

输入数据处理：
![](/assets/vlan_macvlan/vlan_and_macvlan_03.jpg)

输出数据处理：
![](/assets/vlan_macvlan/vlan_and_macvlan_04.jpg)

## MacVLAN vs Bridge

The macvlan is a trivial bridge that doesn't need to do learning as it knows every mac address it can receive, so it doesn't need to implement learning or stp. Which makes it simple stupid and and fast.

MacVLAN的实现来自[EVB标准](http://wikibon.org/wiki/v/Edge_Virtual_Bridging).

## MacVLAN and SR-IOV

VF不支持macvlan ，参考[这里](http://www.mail-archive.com/e1000-devel@lists.sourceforge.net/msg02733.html)


# hardware feature

## vlan offload

```sh
# ethtool -k eth1
rx-vlan-offload: on
tx-vlan-offload: on
rx-vlan-filter: on [fixed]
```

* igb

```c
static int igb_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
...
	netdev->features |= NETIF_F_SG |
			    NETIF_F_IP_CSUM |
			    NETIF_F_IPV6_CSUM |
			    NETIF_F_TSO |
			    NETIF_F_TSO6 |
			    NETIF_F_RXHASH |
			    NETIF_F_RXCSUM |
			    NETIF_F_HW_VLAN_CTAG_RX |
			    NETIF_F_HW_VLAN_CTAG_TX;

	/* copy netdev features into list of user selectable features */
	netdev->hw_features |= netdev->features;
	netdev->hw_features |= NETIF_F_RXALL;

	/* set this bit last since it cannot be part of hw_features */
	netdev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;
...
```

如果NIC支持VLAN offload，在发送时，由硬件添加vlan header：

```c
int dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev,
			struct netdev_queue *txq)
{
...
		if (vlan_tx_tag_present(skb) &&
		    !vlan_hw_offload_capable(features, skb->vlan_proto)) {
		    ///hw don't support vlan offload
			skb = __vlan_put_tag(skb, skb->vlan_proto,
					     vlan_tx_tag_get(skb));
			if (unlikely(!skb))
				goto out;

			skb->vlan_tci = 0;
		}
```

## unicast filter

Using unicast filtering if supported, instead of promiscuous mode(except for passthru). Unicast filtering allows NIC to receive multiple mac addresses
 
* igb

```c
static int igb_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
...
	netdev->priv_flags |= IFF_UNICAST_FLT;
```

* vlan

```c
/*  Attach a VLAN device to a mac address (ie Ethernet Card).
 *  Returns 0 if the device was created or a negative error code otherwise.
 */
static int register_vlan_device(struct net_device *real_dev, u16 vlan_id)
{
...
	new_dev->priv_flags |= (real_dev->priv_flags & IFF_UNICAST_FLT);
```

* macvlan

当MACVLAN设备up时，会将macvlan设备的MAC加到lower device的uc字段中：

```c
struct net_device {
...
	struct netdev_hw_addr_list	uc;	/* Unicast mac addresses */
```

```sh
# ip -d link show eth0.1
3: eth0.1@eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN 
    link/ether da:c9:53:ba:b6:91 brd ff:ff:ff:ff:ff:ff
macvlan  mode bridge

# bridge fdb show eth0
da:c9:53:ba:b6:91 dev eth0 self permanent
…
```

当MACVLAN网络设备up时，内核会将macvlan设备的MAC加到lower device的net_device->uc链表中，然后由驱动写到unicast address寄存器。

![](/assets/vlan_macvlan/vlan_and_macvlan_05.jpg)

在__dev_set_rx_mode中，如果下层设备不支持unicast filtering，则将下层设备设置为混杂模式。
对于igb驱动，会将unicast address写到RAR寄存器，如果寄存器空间不够，会将设置设置为混杂模式。详细参考igb_set_rx_mode。

如果lower device是VLAN device，则VLAN设备会将mac address下沉到真实设备：

```c
static void vlan_dev_set_rx_mode(struct net_device *vlan_dev)
{
	vlan_dev_mc_sync(vlan_dev_priv(vlan_dev)->real_dev, vlan_dev);
	vlan_dev_uc_sync(vlan_dev_priv(vlan_dev)->real_dev, vlan_dev);
}
```

* NIC driver

Intel 82576对unicast filtering的支持：

> 24 exact-matched packets for unicast and multicast frames
>
> 4096-bit hash filter for unicast frames
>
> Lower processor utilization
>
> Promiscuous (unicast and multicast) transfer mode support 
>
> Optional filtering of invalid frames

详细参考[这里](http://www.intel.com/content/www/us/en/embedded/products/networking/82576-gbe-controller-brief.html)


82576支持24个unicast address，I350支持32个：

```c
#define E1000_RAR_ENTRIES_82576        24
#define E1000_RAR_ENTRIES_I350         32


static s32 igb_get_invariants_82575(struct e1000_hw *hw)
{
...
	/* Set rar entry count */
	switch (mac->type) {
	case e1000_82576:
		mac->rar_entry_count = E1000_RAR_ENTRIES_82576;
		break;
	case e1000_82580:
		mac->rar_entry_count = E1000_RAR_ENTRIES_82580;
		break;
	case e1000_i350:
	case e1000_i354:
		mac->rar_entry_count = E1000_RAR_ENTRIES_I350;
```

igb驱动写unicast address到RAR table：

```c
///igb_main.c
static int igb_write_uc_addr_list(struct net_device *netdev)
{
	unsigned int vfn = adapter->vfs_allocated_count;
	///减掉VF的数量
	unsigned int rar_entries = hw->mac.rar_entry_count - (vfn + 1);

	/* return ENOMEM indicating insufficient memory for addresses */
	if (netdev_uc_count(netdev) > rar_entries)  ///空间不够
		return -ENOMEM;
```

X540支持128个unicast address：

```c
///ixgbe_x540.c
#define IXGBE_X540_RAR_ENTRIES		128
```
