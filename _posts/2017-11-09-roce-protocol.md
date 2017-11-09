---
layout: post
title: Understanding the RoCE network protocol
date: 2017-11-09 15:20:30
categories: Network
tags: RDMA RoCE 
excerpt: Understanding the RoCE network protocol
---

[`RoCE`](https://en.wikipedia.org/wiki/RDMA_over_Converged_Ethernet)是`RDMA over Converged Ethernet`的简称，基于它可以在以太网上实现`RDMA`.另外一种方式是`RDMA over an InfiniBand`.所以`RoCE`（严格来说是`RoCEv1`）是一个与`InfiniBand`相对应的链路层协议。

> There are two RoCE versions, RoCE v1 and RoCE v2. RoCE v1 is an Ethernet link layer protocol and hence allows communication between any two hosts in the same Ethernet broadcast domain. RoCE v2 is an internet layer protocol which means that RoCE v2 packets can be routed.

## RoCEv1

对于RoCE互联网络，硬件方面需要支持`IEEE DCB`的L2以太网交换机，计算节点需要支持RoCE的网卡：

> On the hardware side, basically you need an L2 Ethernet switch with IEEE DCB (Data Center Bridging, aka Converged Enhanced Ethernet) with support for priority flow control.
> 
>　On the compute or storage server end, you need an RoCE-capable network adapter.

对应的数据帧格式如下：


![](/assets/rdma/roce_00.png)


对应的协议规范参考[InfiniBand™ Architecture Specification Release 1.2.1 Annex A16: RoCE](https://cw.infinibandta.org/document/dl/7148)。

示例：

![](/assets/rdma/roce_01.jpg)

## RoCEv2

由于`RoCEv1`的数据帧不带IP头部，所以只能在L2子网内通信。所以`RoCEv2`扩展了`RoCEv1`，将`GRH(Global Routing Header)`换成`UDP header +　IP header`:

> RoCEv2 is a straightforward extension of the RoCE protocol that involves a simple modification of the RoCE packet format. 
> 
> Instead of the GRH, RoCEv2 packets carry an IP header which allows traversal of IP L3 Routers and a UDP header that serves as a stateless encapsulation layer for the RDMA Transport Protocol Packets over IP.

数据帧的格式如下：

![](/assets/rdma/roce_02.png)

示例：

![](/assets/rdma/roce_03.png)

值得一提的是内核在4.9通过软件的方式的实现了[RoCEv2](http://hustcat.github.io/linux-soft-roce-implementation/)，即`Soft-RoCE`.

## Refs

* [RoCE: An Ethernet-InfiniBand Love Story](https://www.hpcwire.com/2010/04/22/roce_an_ethernet-infiniband_love_story/)
* [InfiniBand™ Architecture Specification Release 1.2.1 Annex A16: RoCE](https://cw.infinibandta.org/document/dl/7148)
* [InfiniBand™ Architecture Specification Release 1.2.1 Annex A17: RoCEv2](https://cw.infinibandta.org/document/dl/7781)
* [RoCEv2 CNP Packet Format Example](https://community.mellanox.com/docs/DOC-2351)
