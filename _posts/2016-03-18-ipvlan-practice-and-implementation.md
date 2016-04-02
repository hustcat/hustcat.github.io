---
layout: post
title: ipvlan practice and implementation
date: 2016-03-18 20:00:30
categories: Linux
tags: network ipvlan
excerpt: ipvlan practice and implementation
---

## Introduce

IPVLAN支持两种模式: L2 mode and L3 mode。L2 mode的功能与实现原理与MACVLAN差不多，IPVLAN设备本身处理(ARP)广播/多播。只不过，MACVLAN是通过MAC查找MACVLAN设备，而IPVLAN是通过IP查找IPVLAN设备。

L3的功能和实现比较有意思。在L3的情况下，IPVLAN设备本身不会处理二层协议和路由，而是由IPVLAN对应的下层设备负责处理。注意，这里的负责并不是说由下层设备替IPVLAN处理，IPVLAN设备本身不会发送、也不会接收（ARP）广播，这与IP tunnel比较类似。


## IPVLAN L3 mode

### environment

```
node2 172.17.42.41
node3 172.17.42.43
```

### node2 network

```sh
[root@node2 ~]# ip netns add ns0
[root@node2 ~]# ip link add link eth0 ipvl0 type ipvlan
[root@node2 ~]# ip link set dev ipvl0 netns ns0

[root@node2 ~]# ip netns exec ns0 ip link set dev ipvl0 up
[root@node2 ~]# ip netns exec ns0 ip -d link show ipvl0      
56: ipvl0@if2: <BROADCAST,MULTICAST,NOARP,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN mode DEFAULT qlen 1000
    link/ether 52:54:60:11:02:02 brd ff:ff:ff:ff:ff:ff promiscuity 0 
ipvlan
[root@node2 ~]# ip netns exec ns0 ip addr add 10.1.1.2/24 dev ipvl0
[root@node2 ~]# ip netns exec ns0 ip route add default dev ipvl0 scope link
[root@node2 ~]# ip netns exec ns0 ip route show                            
default dev ipvl0  scope link 
10.1.1.0/24 dev ipvl0  proto kernel  scope link  src 10.1.1.2
```

从上面的NOARP可以看到，IPVLAN不会处理ARP请求。

### node3 <--> node2.ns0

```sh
[root@node3 ~]# ip route add 10.1.1.2/32 via 172.17.42.41  
[root@node3 ~]# ping -c 3 10.1.1.2                       
PING 10.1.1.2 (10.1.1.2) 56(84) bytes of data.
64 bytes from 10.1.1.2: icmp_seq=1 ttl=64 time=0.282 ms
64 bytes from 10.1.1.2: icmp_seq=2 ttl=64 time=0.238 ms
64 bytes from 10.1.1.2: icmp_seq=3 ttl=64 time=0.192 ms
```

### node2.ns0 <--> node3.ns1

```sh
[root@node3 ~]# ip netns add ns1
[root@node3 ~]# ip link add link eth0 ipvl1 type ipvlan
[root@node3 ~]# ip link set dev ipvl1 netns ns1


[root@node3 ~]# ip netns exec ns1 ip link set ipvl1 up
[root@node3 ~]# ip netns exec ns1 ip addr add 10.1.1.3/24 dev ipvl1

[root@node2 ~]# ip route add 10.1.1.3/32 via 172.17.42.43
[root@node2 ~]# ip netns exec ns0 ping -c 3 10.1.1.3    
PING 10.1.1.3 (10.1.1.3) 56(84) bytes of data.
64 bytes from 10.1.1.3: icmp_seq=1 ttl=64 time=0.224 ms
64 bytes from 10.1.1.3: icmp_seq=2 ttl=64 time=0.194 ms
64 bytes from 10.1.1.3: icmp_seq=3 ttl=64 time=0.221 ms
```

### node2 <--> node3.ns1

```sh
[root@node3 ~]# ip netns exec ns1 ip route add default dev ipvl1 scope link
[root@node3 ~]# ip netns exec ns1 ip route show                            
default dev ipvl1  scope link 
10.1.1.0/24 dev ipvl1  proto kernel  scope link  src 10.1.1.3
[root@node2 ~]# ping -c 3 10.1.1.3                       
PING 10.1.1.3 (10.1.1.3) 56(84) bytes of data.
64 bytes from 10.1.1.3: icmp_seq=1 ttl=64 time=0.252 ms
64 bytes from 10.1.1.3: icmp_seq=2 ttl=64 time=0.193 ms
64 bytes from 10.1.1.3: icmp_seq=3 ttl=64 time=0.199 ms
```

## send/receive implementation

### send

IPVLAN发送的数据的流程如下：

![](/assets/ipvlan/ipvlan-00.jpg)

我们来看看，IPVLAN是如何做到不需要ARP广播时。我们考虑 node2.ns0(10.1.1.2) -> node3(172.17.42.43)。

在ipvlan_xmit_mode_l3中，会将skb->dev改为下层的物理设备：

```c
static int ipvlan_xmit_mode_l3(struct sk_buff *skb, struct net_device *dev)
{
...
	skb->dev = ipvlan->phy_dev; ///phy device
	return ipvlan_process_outbound(skb, ipvlan);
}
```

然后在ipvlan_process_v4_outbound重新计算路由，然后再走二层的发送过程（ip_local_out）:

```c
static int ipvlan_process_v4_outbound(struct sk_buff *skb)
{
	const struct iphdr *ip4h = ip_hdr(skb);
	struct net_device *dev = skb->dev; ///phy device
	struct rtable *rt;
	int err, ret = NET_XMIT_DROP;
	struct flowi4 fl4 = {
		.flowi4_oif = dev_get_iflink(dev),
		.flowi4_tos = RT_TOS(ip4h->tos),
		.flowi4_flags = FLOWI_FLAG_ANYSRC,
		.daddr = ip4h->daddr,
		.saddr = ip4h->saddr,
	};
	///routing
	rt = ip_route_output_flow(dev_net(dev), &fl4, NULL);
	if (IS_ERR(rt))
		goto err;

	if (rt->rt_type != RTN_UNICAST && rt->rt_type != RTN_LOCAL) {
		ip_rt_put(rt);
		goto err;
	}
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->dst);
	err = ip_local_out(skb); ///send
```

从这里可以看到，路由和二层广播都是在下层物理设备进行的。所以，当IPVLAN设备访问外部时，它并不需要ARP广播。

### receive

当node3 -> node2.ns0时，会根据路由将数据先转给node2:
```
10.1.1.2 via 172.17.42.41 dev eth0
```
当node2.eth0收到数据后，再转给node2.ns0。


## summary

IPVLAN与flannel的host-gw类似，提供一个L3的跨节点netnamespace通信方案。但是很明显，IPVLAN避免了host-gw的bridge，也不需要veth pair，所以的效率肯定要比host-gw要高。

IPVLAN相对于MACVLAN会更安全，因为二层广播和路由都在host netnamespace中处理。另外，也避免了broadcast/multicast带来的性能损失。

但是，个人感觉IPVLAN与flannel的host-gw一样，需要Host是二层互连的吧？这必然会限制它的应用场景。

## related posts

* [Kernel doc](https://www.kernel.org/doc/Documentation/networking/ipvlan.txt)
* [IPVLAN – The beginning](http://people.netfilter.org/pablo/netdev0.1/papers/IPVLAN-The-beginning.pdf)
* [commit](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=2ad7bf3638411cb547f2823df08166c13ab04269)
* [ipvlan example](https://gist.github.com/nerdalert/f493d475d9ad36e194d6)
* [IPVlan L3 Mode Example](https://gist.github.com/nerdalert/c0363c15d20986633fda#ipvlan-l3-mode-example)
