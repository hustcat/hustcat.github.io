---
layout: post
title: Difference of VXLAN L3MISS between flannel and docker overlay implementation
date: 2016-03-23 20:00:30
categories: Linux
tags: network vxlan flannel
excerpt: Difference of VXLAN L3MISS between flannel and docker overlay implementation
---

## VXLAN L3MISS

当VXLAN设备发生数据时，如果不能在邻居表中找到目标L3对应的L2地址时，就意味着发生了L3MISS：

```c
/* Transmit local packets over Vxlan
 *
 * Outer IP header inherits ECN and DF from inner header.
 * Outer UDP destination is the VXLAN assigned port.
 *           source port is based on hash of flow
 */
static netdev_tx_t vxlan_xmit(struct sk_buff *skb, struct net_device *dev)
{
...
	if ((vxlan->flags & VXLAN_F_PROXY) && ntohs(eth->h_proto) == ETH_P_ARP)
		return arp_reduce(dev, skb);
...
}


static int arp_reduce(struct net_device *dev, struct sk_buff *skb)
{
...
	n = neigh_lookup(&arp_tbl, &tip, dev);

	if (n) {
		struct vxlan_fdb *f;
		struct sk_buff	*reply;

		if (!(n->nud_state & NUD_CONNECTED)) {
			neigh_release(n);
			goto out;
		}

		//...

	} else if (vxlan->flags & VXLAN_F_L3MISS) ///notify userspace
		vxlan_ip_miss(dev, tip);
out:
	consume_skb(skb);
	return NETDEV_TX_OK;
}
```

## Difference between flannel and docker overlay

在docker overlay中，vxlan设备设置了L3MISS:

```sh
# ip -d link show dev vx-000101-f6a72
24: vx-000101-f6a72: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue master ov-000101-f6a72 state UNKNOWN 
    link/ether 8e:6b:a1:11:cd:74 brd ff:ff:ff:ff:ff:ff
    vxlan id 257 port 32768 61000 proxy l2miss l3miss ageing 300
```

而flannel中却并没有设置L3MISS:

```sh
# ip -d link show dev flannel.1
36: flannel.1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1450 qdisc noqueue state UNKNOWN 
    link/ether ee:99:86:c3:3f:ba brd ff:ff:ff:ff:ff:ff
    vxlan id 1 local 10.125.225.35 dev eth1 port 32768 61000 nolearning ageing 300 
```

实际上，对于flannel，即使设置了L3MISS，也没有什么用，并不会触发vxlan_ip_miss的逻辑。因为在flannel中，ARP请求发生在L3向L2下发数据的时候。这个时候，当调用arp_reduce的时候，对应的邻居项struct neighbour已经完成创建（在__neigh_create中完成）:

![](/assets/vxlan_l3miss/vxlan-l3miss-01.jpg)

所以，flannel中的l3miss必须基于neigh_app_ns实现。而对于docker overlay的实现，vxlan设备作为bridge的端口，ARP请求是由bridge转发过来的，可以走到vxlan_ip_miss的逻辑，所以它的l3miss必须基于arp_reduce实现。

当从容器ping其它容器时，内核的发起ARP请求的时候函数调用流程(flannel)：

```
# ./kprobe -s 'r:rtnl_notify'
rtnl_notify
Tracing kprobe rtnl_notify. Ctrl-C to end.
            ping-26142 [000] d.s1 2986893.114240: rtnl_notify: (__neigh_notify+0xbe/0xe0 <- rtnl_notify)
            ping-26142 [000] d.s1 2986893.114259: <stack trace>
 => neigh_app_ns
 => arp_solicit
 => neigh_probe
 => __neigh_event_send
 => neigh_resolve_output
 => ip_finish_output2
 => ip_finish_output
 => ip_output
 => ip_forward_finish
 => ip_forward
 => ip_rcv_finish
 => ip_rcv
 => __netif_receive_skb_core
 => __netif_receive_skb
 => netif_receive_skb_internal
 => netif_receive_skb_sk
 => NF_HOOK.clone.0
 => br_handle_frame_finish
 => NF_HOOK_THRESH
 => br_nf_pre_routing_finish
 => NF_HOOK_THRESH
 => br_nf_pre_routing
 => nf_iterate
 => nf_hook_slow
 => br_handle_frame
 => __netif_receive_skb_core
 => __netif_receive_skb
 => process_backlog
 => napi_poll
 => net_rx_action
 => __do_softirq
 => do_softirq_own_stack
 => do_softirq
 => __local_bh_enable_ip
 => ip_finish_output2
 => ip_finish_output
 => ip_output
 => ip_local_out_sk
 => ip_send_skb
 => ip_push_pending_frames
 => raw_sendmsg
 => inet_sendmsg
 => sock_sendmsg
 => ___sys_sendmsg
 => __sys_sendmsg
 => SyS_sendmsg
 => entry_SYSCALL_64_fastpath
```

```
ping -> socket -> L3 -> veth -> bridge -> L3 forward -> neighbor
```

到这里，就明白了为什么flannel不需要设置L3MISS，而docker overlay必须设置L3MISS。

另外，每个运行flannel的host的所有容器，在VXLAN设备中生成邻居表项的L2地址都一样。所以，相对于docker overlay的实现，flannel中VXLAN设备的(MAC->VTEP)转发表会小很多（等于Host数量）。

## ARPD

在一些大型网络上，比如虚拟化（容器）场景，邻居的数量比较大。所以，neighbour数据结构消耗的内存可能非常大，这会影响系统性能。

Arpd是一个用户态守护进程，可以将ARP负载从内核转到它自己的缓存中。要使用arpd，内核编译时必须打开CONFIG_ARPD。实际上，这个宏在新的版本已经[去掉](https://lkml.org/lkml/2013/8/29/98)。

ARP与arpd的交互：
 
![](/assets/vxlan_l3miss/vxlan-l3miss-00.jpg)

```c
//kernel-3.10.x
static void arp_solicit(struct neighbour *neigh, struct sk_buff *skb)
{
...
	///select source L3 address
	if (!saddr)
		saddr = inet_select_addr(dev, target, RT_SCOPE_LINK);

	probes -= neigh->parms->ucast_probes; ///3, /proc/sys/net/ipv4/neigh/$NIC/ucast_solicit
	if (probes < 0) {
		if (!(neigh->nud_state & NUD_VALID))
			pr_debug("trying to ucast probe in NUD_INVALID\n");
		neigh_ha_snapshot(dst_ha, neigh, dev);
		dst_hw = dst_ha;
	} else {///超过内核的次数，转到用户态
		probes -= neigh->parms->app_probes; ///proc/sys/net/ipv4/neigh/$NIC/app_solicit
		if (probes < 0) {
#ifdef CONFIG_ARPD
			neigh_app_ns(neigh); ///notify userspace
#endif
			return;
		}
	}
	///send ARP request
	arp_send(ARPOP_REQUEST, ETH_P_ARP, target, dev, saddr,
		 dst_hw, dev->dev_addr, NULL);
}

#ifdef CONFIG_ARPD
void neigh_app_ns(struct neighbour *n)
{
	__neigh_notify(n, RTM_GETNEIGH, NLM_F_REQUEST);
}
#endif
```

我们必须将内核参数app_solicit设置为大于0的值:

```sh
#echo 3 > /proc/sys/net/ipv4/neigh/flannel.1/app_solicit
```

flannel将vxlan设备的app_solicit值设置为3:

```go	
func newVXLANDevice(devAttrs *vxlanDeviceAttrs) (*vxlanDevice, error)
...
	// this enables ARP requests being sent to userspace via netlink
	sysctlPath := fmt.Sprintf("/proc/sys/net/ipv4/neigh/%s/app_solicit", devAttrs.name)
	sysctlSet(sysctlPath, "3")
...
```

# Related posts

* [net: neighbour: Remove CONFIG_ARPD](https://lkml.org/lkml/2013/8/29/98)
* [Linux Kernel Networking­ advanced topics:Neighboring and IPsec](http://www.haifux.org/lectures/180/netLec2.pdf)
