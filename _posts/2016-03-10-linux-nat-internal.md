---
layout: post
title: Linux NAT internal
date: 2016-03-10 23:00:30
categories: Linux
tags: network nat iptables
excerpt: Linux NAT internal
---

Linux的NAT功能是在iptables中实现的，先看一下iptables整体框架：

![](/assets/netfilter/2016-03-10-linux-nat-internal-01.gif)

图片来自[这里](http://vbird.dic.ksu.edu.tw/linux_server/0250simple_firewall.php)。

# SNAT实现

整体流程：

![](/assets/netfilter/2016-03-10-linux-nat-internal-02.jpg)

对于SNAT，在xt_snat_target_v0中根据action修改nf_conn->tuplehash[IP_CT_DIR_REPLY].tuple。然后在nf_nat_packet中发送数据。

在nf_nat_packet中，会将packet的源地址修改为inverse tuple的源地址：

```c
//net/netfilter/nf_nat_core.c
/* Do packet manipulations according to nf_nat_setup_info. */
unsigned int nf_nat_packet(struct nf_conn *ct,
			   enum ip_conntrack_info ctinfo,
			   unsigned int hooknum,
			   struct sk_buff *skb)
{
...
	/* Non-atomic: these bits don't change. */
	if (ct->status & statusbit) {
		struct nf_conntrack_tuple target;

		/* We are aiming to look like inverse of other direction. */
		nf_ct_invert_tuplepr(&target, &ct->tuplehash[!dir].tuple);  ///使用inverse tuple，并且对于SNAT使用其src address，对于DNAT使用其dst addr

		l3proto = __nf_nat_l3proto_find(target.src.l3num);
		l4proto = __nf_nat_l4proto_find(target.src.l3num,
						target.dst.protonum);
		if (!l3proto->manip_pkt(skb, 0, l4proto, &target, mtype)) ///nf_nat_ipv4_manip_pkt
			return NF_DROP;
	}
	return NF_ACCEPT;


///net/ipv4/netfilter/nf_nat_l3proto_ipv4.c
static bool nf_nat_ipv4_manip_pkt(struct sk_buff *skb,
				  unsigned int iphdroff,
				  const struct nf_nat_l4proto *l4proto,
				  const struct nf_conntrack_tuple *target,
				  enum nf_nat_manip_type maniptype)
{
...
	iph = (void *)skb->data + iphdroff;
	hdroff = iphdroff + iph->ihl * 4;

	if (!l4proto->manip_pkt(skb, &nf_nat_l3proto_ipv4, iphdroff, hdroff,
				target, maniptype))
		return false;
	iph = (void *)skb->data + iphdroff;

	if (maniptype == NF_NAT_MANIP_SRC) {  ///修改源地址
		csum_replace4(&iph->check, iph->saddr, target->src.u3.ip);
		iph->saddr = target->src.u3.ip;
	} else {
		csum_replace4(&iph->check, iph->daddr, target->dst.u3.ip);
		iph->daddr = target->dst.u3.ip;  ///修改目的地址
	}
```

考虑如下情况：

A <-> G <-> S

经过SNAT修改后，nf_conn->tuplehash的两个tuple为{(A->S)，(S->G)}，当G收到A发过的packet(A->S)时，会修改packet的源地址为(G->S)（根据tuple[REPLY]=(S->G) ），然后发送给S；当G收到S返回的packet(S->G)时，会修改packet的目的地址为(S->A) （根据tuple[ORIGINAL]=(A->S) ），然后返回给A。

值得注意的是：

对于SNAT,第一次修改original packet的源地址，发生在NF_INET_POST_ROUTING；第二次修改reply packet的目的地址，发生在NF_INET_PRE_ROUTING。

同理，对于DNAT，第一次修改original packet的目的地址，发生在NF_INET_PRE_ROUTING，第二次修改reply packet的源地址，发生在NF_INET_POST_ROUTING。

# DNAT实现
 
![](/assets/netfilter/2016-03-10-linux-nat-internal-03.jpg)

理解DNAT/SNAT的一个关键点在于，内核在conntrack的基础上，根据iptables的DNAT/SNAT规则修改REPLY tuple。当内核收到reply packet时，就可以从conntrack得到REPLY tuple，然后就可以得到对应的ORIGINAL tuple，再对ORIGINAL tuple取反得到inverse tuple，最后，再根据inverse tuple，修改reply packet的源地址（DNAT），或者目标址（SNAT）［注：这是对reply packet!］。

第二点，就是要理解DNAT/SNAT修改地址的HOOK点。

# Full NAT

```sh
[root@real-server]# iptables -t nat -A PREROUTING -d 205.254.211.17 -j DNAT --to-destination 192.168.100.17
[root@real-server]# iptables -t nat -A POSTROUTING -s 192.168.100.17 -j SNAT --to-destination 205.254.211.17
```

参考[这里](http://linux-ip.net/html/nat-dnat.html)。


# 其它

* SNAT 和 MASQUERADE 的区别

SNAT 是明确指定修改的源地址，而 MASQUERADE 会自动获取出口接口（根据路由表）的 IP 地址。性能方面比 SNAT 几乎忽略不计，而无需明确指定 IP 地址，所以在 DHCP 和 PPPOE 的动态 IP 环境下（也就是IP不固定的时候）使用特别方面。

```
iptables -t nat -A POSTROUTING -o $WAN -j MASQUERADE
iptables -t nat -A POSTROUTING -o $WAN -j SNAT --to-source $IP
```

* DNAT 和 REDIRECT 的区别

这组动作和上面的刚好相反，是用来修改目的地址的。DNAT 是明确指定修改的目的地址，而 REDIRECT 会把要转发的包的目的地址改写为入口接口的 IP 地址。

```
iptables -t nat -A PREROUTING -i $WAN-p tcp --dport 80 -j REDIRECT --to-ports 3128
iptables -t nat -A PREROUTING -i $WAN -p tcp --dport 80 -j DNAT --to-destination $IP --to-ports 3128
```

# related posts

* [Address Spoofing with iptables in Linux](https://sandilands.info/sgordon/address-spoofing-with-iptables-in-linux)
* [Linux NAT(Network Address Translation) Router Explained](http://www.slashroot.in/linux-nat-network-address-translation-router-explained)
* [第九章、防火墙与 NAT 服务器](http://vbird.dic.ksu.edu.tw/linux_server/0250simple_firewall.php)
* [Linux Netfilter实现机制和扩展技术](https://www.ibm.com/developerworks/cn/linux/l-ntflt/)
* [netfilter 链接跟踪机制与NAT原理](http://www.cnblogs.com/liushaodong/archive/2013/02/26/2933593.html)
* [iptables深入解析-nat篇](http://blog.chinaunix.net/uid-20786208-id-5145525.html)
