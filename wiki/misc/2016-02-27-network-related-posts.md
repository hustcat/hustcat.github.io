---
layout: post
title: Network related posts
date: 2016-02-27 15:00:30
categories: Network
tags: network
excerpt: Network related posts
---

# Bridge related

## Overview

* [Virtual switching technologies and Linux bridge](http://events.linuxfoundation.org/sites/events/files/slides/LinuxConJapan2014_makita_0.pdf)

## bridge and iptables

* [ebtables/iptables interaction on a Linux-based bridge](http://ebtables.netfilter.org/br_fw_ia/br_fw_ia.html)
* [Bridge-nf Frequently Asked Questions](http://ebtables.netfilter.org/misc/brnf-faq.html)
* [Iptables - Bridge and Forward chain](http://serverfault.com/questions/162366/iptables-bridge-and-forward-chain)

## bridge and containers

* [Host bridge & NAT bridge](https://www.flockport.com/lxc-networking-guide/)

> Host bridge将Host的物理NIC eth0作为bridge的端口，容器（虚拟机）看上去就像一台物理机（相同的IP网络），可以直接
> 与外部通信。NAT bridge不会将Host的物理NIC eth0作为bridge的端口，只用连接内部私有的容器（虚拟机）网络，
> 容器（虚拟机）不能直接与外部通信，需要通过Host的eth0进行DNAT/SNAT。


# VLAN/MacVLAN/IPVLAN

## VLAN

### Overview

* [VLANs on Linux](http://www.linuxjournal.com/article/7268)

> VLAN的主要优点:
> (1)广播域被限制在一个VLAN内,节省了带宽,提高了网络处理能力。
> (2)增强局域网的安全性:VLAN间不能直接通信,即一个VLAN内的用户不能和其它VLAN内的用户直接通信,而需要通过路由器或三层交换机等三层设备。
> (3)灵活构建虚拟工作组:用VLAN可以划分不同的用户到不同的工作组,同一工作组的用户也不必局限于某一固定的物理范围,网络构建和维护更方便灵活。

* [802.1Q VLAN implementation for Linux](http://www.candelatech.com/~greear/vlan.html)

* Create VLAN device

```
# ip link add link eth0 name eth0.11 type vlan id 11  ### vconfig add eth0 11
# ip a
2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP qlen 1000
    link/ether 00:50:56:2b:94:78 brd ff:ff:ff:ff:ff:ff
    inet 172.16.213.128/24 brd 172.16.213.255 scope global eth0
       valid_lft forever preferred_lft forever
    inet6 fe80::250:56ff:fe2b:9478/64 scope link 
       valid_lft forever preferred_lft forever
4: eth0.11@eth0: <BROADCAST,MULTICAST> mtu 1500 qdisc noop state DOWN 
    link/ether 00:50:56:2b:94:78 brd ff:ff:ff:ff:ff:ff
# ip -d link show eth0.11
4: eth0.11@eth0: <BROADCAST,MULTICAST> mtu 1500 qdisc noop state DOWN 
    link/ether 00:50:56:2b:94:78 brd ff:ff:ff:ff:ff:ff
    vlan id 11 <REORDER_HDR> 
```

注意：VLAN设备的MAC地址与eth0相同。

## MacVLAN

### Overview

* [Linux Networking: MAC VLANs and Virtual Ethernets](http://www.pocketnix.org/posts/Linux%20Networking:%20MAC%20VLANs%20and%20Virtual%20Ethernets)
* [Some notes on macvlan/macvtap](http://backreference.org/2014/03/20/some-notes-on-macvlanmacvtap/)
* [Edge Virtual Bridging](http://wikibon.org/wiki/v/Edge_Virtual_Bridging)

> MacVLAN的实现来自EVB标准。

* Create MacVLAN

```
# ip link add eth0.1 link eth0 type macvlan mode bridge
# ip a
2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP qlen 1000
    link/ether 00:50:56:2b:94:78 brd ff:ff:ff:ff:ff:ff
    inet 172.16.213.128/24 brd 172.16.213.255 scope global eth0
       valid_lft forever preferred_lft forever
    inet6 fe80::250:56ff:fe2b:9478/64 scope link 
       valid_lft forever preferred_lft forever
3: eth0.1@eth0: <BROADCAST,MULTICAST> mtu 1500 qdisc noop state DOWN 
    link/ether 92:57:e0:02:3e:3d brd ff:ff:ff:ff:ff:ff

# ip -d link show eth0.1
3: eth0.1@eth0: <BROADCAST,MULTICAST> mtu 1500 qdisc noop state DOWN 
    link/ether 92:57:e0:02:3e:3d brd ff:ff:ff:ff:ff:ff
    macvlan  mode bridge
```

### MacVLAN and Containers

* [LXC Macvlan networking](https://www.flockport.com/lxc-macvlan-networking/)
* [Macvlan, Ipvlan and 802.1q Trunk Driver Notes](https://gist.github.com/nerdalert/c0363c15d20986633fda)


## IPVLAN

### Overview

* [IPVLAN Driver HOWTO](https://github.com/torvalds/linux/blob/master/Documentation/networking/ipvlan.txt)
* [IPVLAN – The beginning](http://people.netfilter.org/pablo/netdev0.1/papers/IPVLAN-The-beginning.pdf)

### MacVLAN vs IPVLAN

What to choose (macvlan vs. ipvlan)?

These two devices are very similar in many regards and the specific use case could very well define which device to choose. if one of the following situations defines your use case then you can choose to use ipvlan

* (a) The Linux host that is connected to the external switch / router has policy configured that allows only one mac per port.
* (b) No of virtual devices created on a master exceed the mac capacity and puts the NIC in promiscous mode and degraded performance is a concern.
* (c) If the slave device is to be put into the hostile / untrusted network namespace where L2 on the slave could be changed / misused.



# SDN

## VXLAN

### Overview

* [VXLAN Series – Different Components – Part 1](http://blogs.vmware.com/vsphere/2013/04/vxlan-series-different-components-part-1.html)

### VXLAN and containers

* [Flockport labs - LXC and VXLAN](https://www.flockport.com/flockport-labs-lxc-and-vxlan/)


## OVN

* [ovn-architecture - Open Virtual Network architecture](http://openvswitch.org/support/dist-docs/ovn-architecture.7.html)
* [Introduction to OVN](http://galsagie.github.io/2015/04/20/ovn-1/)

### OVN & Container

* [Containers Support In OVN](http://galsagie.github.io/2015/04/26/ovn-containers/)


## OVS

* [OVS:Documentation](http://openvswitch.org/support/)

### OVSDB
* [The Open vSwitch Database Management Protocol](https://tools.ietf.org/html/rfc7047)
* [What is Open vSwitch Database or OVSDB?](https://www.sdxcentral.com/open-source/definitions/what-is-ovsdb/)

## NFV

* [Accelerating the NFV Data Plane: SR-IOV and DPDK… in my own words](http://www.metaswitch.com/the-switch/accelerating-the-nfv-data-plane)
* [Boosting the NFV datapath with RHEL OpenStack Platform](http://redhatstackblog.redhat.com/2016/02/10/boosting-the-nfv-datapath-with-rhel-openstack-platform/)

## Neutron

* [Neutron/L2-GW](https://wiki.openstack.org/wiki/Neutron/L2-GW)
* [Neutron L2 Gateway + HP 5930 switch OVSDB integration, for VXLAN bridging and routing](http://kimizhang.com/neutron-l2-gateway-hp-5930-switch-ovsdb-integration/)
* [L2 Gateway正式发布实现二层互联](https://www.ustack.com/blog/l2-gateway/)


# DPDK

* [在虚拟机间 NFV 应用上使用采用 DPDK 的 Open vSwitch](https://software.intel.com/zh-cn/articles/using-open-vswitch-with-dpdk-for-inter-vm-nfv-applications)
* [Intel DPDK Step by Step instructions](http://www.slideshare.net/hisaki/intel-dpdk-step-by-step-instructions)

* [DPDK Design Tips (Part 1 - RSS)](http://galsagie.github.io/2015/02/26/dpdk-tips-1/)

* [Understanding DPDK](http://www.slideshare.net/garyachy/dpdk-44585840)
* [Accelerating Neutron with Intel DPDK](http://www.slideshare.net/AlexanderShalimov/ashalimov-neutron-dpdkv1)
* [DPDK Introduction](https://feiskyer.github.io/2016/04/24/DPDK-Introduction/)

## User space driver

* [Device drivers in user space](http://www.embedded.com/design/operating-systems/4401769/Device-drivers-in-user-space)


## DPDK & SR-IOV

* [10. I40E/IXGBE/IGB Virtual Function Driver](http://dpdk.org/doc/guides-16.04/nics/intel_vf.html)


# Netfilter

## overview

* [Netfilter High Availability](http://people.netfilter.org/hidden/nfws-2005-ctsync_slides.pdf)

## iptables

### overview

* [A Deep Dive into Iptables and Netfilter Architecture](https://www.digitalocean.com/community/tutorials/a-deep-dive-into-iptables-and-netfilter-architecture)
* [An IPTABLES Primer](https://danielmiessler.com/study/iptables/)
* [Building a Professional Firewall with Linux and Iptables](https://danielmiessler.com/blog/professional-firewall-iptables/)
* [Linux Configuration for NAT and Firewall](http://www.eecs.wsu.edu/~hauser/teaching/CS455-F01/LectureNotes/4-22-4up.pdf)

### NAT

* [Address Spoofing with iptables in Linux](https://sandilands.info/sgordon/address-spoofing-with-iptables-in-linux)
* [Linux NAT(Network Address Translation) Router Explained](http://www.slashroot.in/linux-nat-network-address-translation-router-explained)
* [第九章、防火墙与 NAT 服务器](http://vbird.dic.ksu.edu.tw/linux_server/0250simple_firewall.php)


### Stateless NAT

* [Stateless NAT with iproute2](http://linux-ip.net/html/nat-stateless.html)
* [Stateless Floating IPs](https://feiskyer.github.io/2015/06/24/stateless-floating-ips/)
* [Linux Stateless无状态NAT-使用TC来配置](http://blog.csdn.net/dog250/article/details/7256411)
* [: Add stateless NAT](https://lwn.net/Articles/252540/)


## nftables

* [Nftables: a new packet filtering engine](https://lwn.net/Articles/324989/)
* [What comes after 'iptables'? Its successor, of course: `nftables`](https://developers.redhat.com/blog/2016/10/28/what-comes-after-iptables-its-successor-of-course-nftables/)

## SYN proxy

* [DDoS protection Using Netfilter/iptables](https://people.netfilter.org/hawk/presentations/devconf2014/iptables-ddos-mitigation_JesperBrouer.pdf)
* [Working with SYNPROXY](https://github.com/firehol/firehol/wiki/Working-with-SYNPROXY)
* [SynProxy 工作原理](https://blog.gobyoung.com/cn/2015-how-synproxy-works.html)

# BPF

* [Dive into BPF: a list of reading material](https://qmonnet.github.io/whirl-offload/2016/09/01/dive-into-bpf/)

## XDP

* [[PATCH RFC 0/3] xdp: Generalize XDP](https://www.mail-archive.com/netdev@vger.kernel.org/msg129432.html)


# Kubernetes network

* [Networking in Containers and Container Clusters](http://www.netdevconf.org/0.1/docs/Networking%20in%20Containers%20and%20Container%20Clusters.pdf)
* [Comparison of Networking Solutions for Kubernetes](http://machinezone.github.io/research/networking-solutions-for-kubernetes/)

* [SIG-Networking: Kubernetes Network Policy APIs Coming in 1.3](http://blog.kubernetes.io/2016/04/Kubernetes-Network-Policy-APIs.html)

## CNM & CNI

* [Container networking models: navigating differences and similarities](http://www.plumgrid.com/plumgrid-blog/2016/06/blog-series-3-4-container-networking-models-navigating-differences-and-similarities/)
* [Why Kubernetes doesn’t use libnetwork](http://blog.kubernetes.io/2016/01/why-Kubernetes-doesnt-use-libnetwork.html)


# Loadbalance

## haproxy

* [Haproxy](http://loadbalancer.org/blog/category/haproxy)



# Routing

* [Software-based routers](https://l3net.wordpress.com/routers/)

## policy routing

* [Chapter 2 - Policy Routing Theory](http://www.policyrouting.org/PolicyRoutingBook/ONLINE/CH02.web.html)
* [Overcoming Asymmetric Routing on Multi-Homed Servers](http://www.linuxjournal.com/article/7291)
* [Stateful NAT with Asymmetric Routing](http://brbccie.blogspot.com/2013/03/stateful-nat-with-asymmetric-routing.html)

## BGP

### GoBGP

* [GoBGP – A Control Plane Evolving Software Networking](http://networkstatic.net/gobgp-control-plane-evolving-software-networking/)

## Bird


# Network stack

* [Navigating the Linux kernel network stack: receive path](http://epickrram.blogspot.com/2016/05/navigating-linux-kernel-network-stack.html)
* [napi](https://wiki.linuxfoundation.org/networking/napi)

## GRO

* [JLS2009: Generic receive offload](https://lwn.net/Articles/358910/)


# Tools

## iproute2

* [iproute2](http://www.linuxfoundation.org/collaborate/workgroups/networking/iproute2)
* [IPROUTE2 Utility Suite Howto](http://www.policyrouting.org/iproute2.doc.html)

* [ip - show / manipulate routing, devices, policy routing and tunnels](http://man7.org/linux/man-pages/man8/ip.8.html)
* [ip-link - network device configuration](http://stuff.onse.fi/man?program=ip-link)

* [bridge - show / manipulate bridge addresses and devices](http://man7.org/linux/man-pages/man8/bridge.8.html)

## tcpdump

* [A tcpdump Primer with Examples](https://danielmiessler.com/study/tcpdump/)



# Blogs

* [Scott Lowe](http://blog.scottlowe.org/)
* [Gal Sagie](http://galsagie.github.io)
