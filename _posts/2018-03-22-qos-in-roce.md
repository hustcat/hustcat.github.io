---
layout: post
title: Qos in RoCE
date: 2018-03-22 18:28:30
categories: Network
tags: RDMA RoCE ECN PFC
excerpt: Qos in RoCE
---

## Overview

TCP/IP协议栈满足不了现代IDC工作负载(workloads)的需求，主要有2个原因：(1)内核处理收发包需要消耗大量的CPU；(2)TCP不能满足应用对低延迟的需求：一方面，内核协议栈会带来数十ms的延迟；另一方面，TCP的拥塞控制算法、超时重传机制都会增加延迟。

RDMA在NIC内部实现传输协议，所以没有第一个问题；同时，通过`zero-copy`、`kernel bypass`避免了内核层面的延迟。

与TCP不同的是，RDMA需要一个无损(lossless)的网络。例如，交换机不能因为缓冲区溢出而丢包。为此，RoCE使用`PFC(Priority-based Flow Control)`带进行流控。一旦交换机的port的接收队列超过一定阀值(shreshold)时，就会向对端发送`PFC pause frame`，通知发送端停止继续发包。一旦接收队列低于另一个阀值时，就会发送一个`pause with zero duration`，通知发送端恢复发包。

PFC对数据流进行分类(class)，不同种类的数据流设置不同的优先级。比如将RoCE的数据流和TCP/IP等其它数据流设置不同的优先级。详细参考[Considerations for Global Pause, PFC and QoS with Mellanox Switches and Adapters](https://community.mellanox.com/docs/DOC-2022)

### Network Flow Classification

对于IP/Ethernet，有2种方式对网络流量分类：

* By using PCP bits on the VLAN header
* By using DSCP bits on the IP header

详细介绍参考[Understanding QoS Configuration for RoCE](https://community.mellanox.com/docs/DOC-2894)。

### Traffic Control Mechanisms

对于RoCE，有2个机制用于流控：`Flow Control (PFC)`和`Congestion Control (DCQCN)`，这两个机制可以同时，也可以分开工作。

* Flow Control (PFC)

PFC是一个链路层协议，只能针对port进行流控，粒度较粗。一旦发生拥塞，会导致整个端口停止pause。这是不合理的，参考[Understanding RoCEv2 Congestion Management](https://community.mellanox.com/docs/DOC-2321)。为此，RoCE引入`Congestion Control`。

* Congestion Control (DCQCN)

`DC-QCN`是RoCE使用的拥塞控制协议，它基于`Explicit Congestion Notification (ECN)`。后面会详细介绍。

## PFC

前面介绍有2种方式对网络流量进行分类，所以，PFC也有2种实现。

### VLAN-based PFC

* VLAN tag

基于[VLAN tag](https://en.wikipedia.org/wiki/IEEE_802.1Q)的Priority code point (PCP，3-bits)定义了8个[Priority](https://en.wikipedia.org/wiki/IEEE_P802.1p).

* VLAN-based PFC

In case of L2 network, PFC uses the priority bits within the VLAN tag (IEEE 802.1p) to differentiate up to eight types of flows that can be subject to flow control (each one independently).

* RoCE with VLAN-based PFC

[HowTo Run RoCE and TCP over L2 Enabled with PFC](https://community.mellanox.com/docs/DOC-1415).

```
## 将skb prio 0~7 映射到vlan prio 3
for i in {0..7}; do ip link set dev eth1.100 type vlan egress-qos-map $i:3 ; done

## enable PFC on TC3
mlnx_qos -i eth1 -f 0,0,0,1,0,0,0,0
```

例如：

```
[root@node1 ~]# cat /proc/net/vlan/eth1.100 
eth1.100  VID: 100       REORDER_HDR: 1  dev->priv_flags: 1001
         total frames received            0
          total bytes received            0
      Broadcast/Multicast Rcvd            0

      total frames transmitted            0
       total bytes transmitted            0
Device: eth1
INGRESS priority mappings: 0:0  1:0  2:0  3:0  4:0  5:0  6:0 7:0
 EGRESS priority mappings: 
[root@node1 ~]# for i in {0..7}; do ip link set dev eth1.100 type vlan egress-qos-map $i:3 ; done
[root@node1 ~]# cat /proc/net/vlan/eth1.100                                                      
eth1.100  VID: 100       REORDER_HDR: 1  dev->priv_flags: 1001
         total frames received            0
          total bytes received            0
      Broadcast/Multicast Rcvd            0

      total frames transmitted            0
       total bytes transmitted            0
Device: eth1
INGRESS priority mappings: 0:0  1:0  2:0  3:0  4:0  5:0  6:0 7:0
 EGRESS priority mappings: 0:3 1:3 2:3 3:3 4:3 5:3 6:3 7:3 
```

参考[HowTo Set Egress Priority VLAN on Linux](https://community.mellanox.com/docs/DOC-2311).

* 问题

基于VLAN的PFC机制有2个主要问题：(1)交换机需要工作在trunk模式；(2)没有标准的方式实现`VLAN PCP`跨L3网络传输(VLAN是一个L2协议)。

`DSCP-based PFC`通过使用IP头部的`DSCP`字段解决了上面2个问题。

### DSCP-based PFC 

DSCP-based PFC requires both NICs and switches to classify and queue packets based on the DSCP value instead of the VLAN tag.

* DSCP vs TOS

The type of service (ToS) field in the IPv4 header has had various purposes over the years, and has been defined in different ways by five RFCs.[1] The modern redefinition of the ToS field is a six-bit Differentiated Services Code Point (DSCP) field[2] and a two-bit Explicit Congestion Notification (ECN) field.[3] While Differentiated Services is somewhat backwards compatible with ToS, ECN is not.

详细介绍参考：

* [Type of service](https://en.wikipedia.org/wiki/Type_of_service)
* [Differentiated services](https://en.wikipedia.org/wiki/Differentiated_services)

### PFC机制的一些问题

RDMA的PFC机制可能会导致一些问题：

* RDMA transport livelock

尽管PFC可以避免`buffer overflow`导致的丢包，但是，其它一些原因，比如FCS错误，也可能导致网络丢包。RDMA的`go-back-0`算法，每次出现丢包，都会导致整个message的所有packet都会重传，从而导致`livelock`。TCP有SACK算法，由于RDMA传输层在NIC实现，受限于硬件资源，NIC很难实现SACK算法。可以使用`go-back-N`算法来避免这个问题。

* PFC Deadlock

当PFC机制与Ethernet的广播机制工作时，可能导致出现`PFC Deadlock`。简单来说，就是PFC机制会导致相应的port停止发包，而Ethernet的广播包可能引起新的`PFC pause`依赖（比如port对端的server down掉)，从而引起循环依赖。广播和多播对于`loseless`是非常危险的，建议不要将其归于`loseless classes`。

* NIC PFC pause frame storm

由于`PFC pause`是传递的，所以很容器引起`pause frame storm`。比如，NIC因为bug导致接收缓冲区填满，NIC会一直对外发送`pause frame`。需要在NIC端和交换机端使用`watchdog`机制来防止`pause storm`。

* The Slow-receiver symptom

由于NIC的资源有限，它将大部分数据结构，比如`QPC(Queue Pair Context)` 和`WQE (Work Queue Element)`都放在host memory。而NIC只会缓存部分数据对象，一旦出现`cache miss`，NIC的处理速度就会下降。

## ECN

### ECN with TCP/IP

ECN是一个端到端的拥塞通知机制，而不需要丢包。ECN是可选的特性，它需要端点开启ECN支持，同时底层的网络也需要支持。

传统的TCP/IP网络，通过丢包来表明网络拥塞，`router/switch/server`都会这么做。而对于支持ECN的路由器，当发生网络拥塞时，会设置IP头部的ECN(2bits)标志位，而接收端会给发送端返回拥塞的通知(`echo of the congestion indication`)，然后发送端降低发送速率。

由于发送速率由传输层(TCP)控制，所以，ECN需要TCP和IP层同时配合。

[rfc3168](https://tools.ietf.org/html/rfc3168)定义了`ECN for TCP/IP`。

#### ECN with IP

[IP头部](https://en.wikipedia.org/wiki/IPv4#Header)有2个bit的ECN标志位：

* 00 – Non ECN-Capable Transport, Non-ECT
* 10 – ECN Capable Transport, ECT(0)
* 01 – ECN Capable Transport, ECT(1)
* 11 – Congestion Encountered, CE.

如果端点支持ECN，就数据包中的标志位设置为`ECT(0)`或者`ECT(1)`。

#### ECN with TCP

为了支持ECN，TCP使用了[TCP头部](https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_segment_structure)的3个标志位：`Nonce Sum (NS)`，`ECN-Echo (ECE)`和`Congestion Window Reduced (CWR)`。

### ECN in RoCEv2

[RoCEv2](https://cw.infinibandta.org/document/dl/7781)引入了ECN机制来实现拥塞控制，即`RoCEv2 Congestion Management (RCM)`。通过RCM，一旦网络发生拥塞，就会通知发送端降低发送速率。与TCP类似，RoCEv2使用传输层头部`Base Transport Header (BTH)`的`FECN`标志位来标识拥塞。

实现RCM的RoCEv2 HCAs必须遵循下面的规则：

(1) 如果收到IP.ECN为`11`的包，HCA生成一个[RoCEv2 CNP(Congestion Notification Packet)](https://community.mellanox.com/docs/DOC-2351)包，返回给发送端；
(2) 如果收到`RoCEv2 CNP`包，则降低对应QP的发送速率；
(3) 从上一次收到`RoCEv2 CNP`后，经过配置的时间或者字节数，HCA可以增加对应QP的发送速率。


* RCM的一些术语

Term | Description
--- | ---
RP (Injector) | Reaction Point - the end node that performs rate limitation to prevent congestion
NP  | Notification Point - the end node that receives the packets from the injector and sends back notifications to the injector for indications regarding the congestion situation
CP  | Congestion Point - the switch queue in which congestion happens
CNP | The RoCEv2 Congestion Notification Packet - The notification message an NP sends to the RP when it receives CE marked packets.

* RoCEv2的ECN示例

参考[Congestion Control Loop](https://community.mellanox.com/docs/DOC-2321#jive_content_id_Congestion_Control_Loop)。

* ECN的配置

参考[How To Configure RoCE over a Lossless Fabric (PFC + ECN) End-to-End Using ConnectX-4 and Spectrum (Trust L2)](https://community.mellanox.com/docs/DOC-2733)。

## Refs

### 一些关于PFC的文献

* [RDMA over Commodity Ethernet at Scale](http://www.cs.ubc.ca/~andy/538w-2016/papers/rdma_sigcomm2016.pdf)
* [Network Considerations for Global Pause, PFC and QoS with Mellanox Switches and Adapters](https://community.mellanox.com/docs/DOC-2022)
* [Understanding QoS Configuration for RoCE](https://community.mellanox.com/docs/DOC-2894)
* [HowTo Run RoCE and TCP over L2 Enabled with PFC](https://community.mellanox.com/docs/DOC-1415)
* [Revisiting Network Support for RDMA](http://netseminar.stanford.edu/seminars/03_16_17.pdf)
* [RoCE v2 Considerations](https://community.mellanox.com/docs/DOC-1451)

### 一些关于ECN的文献

* [Explicit Congestion Notification](https://en.wikipedia.org/wiki/Explicit_Congestion_Notification)
* [Understanding RoCEv2 Congestion Management](https://community.mellanox.com/docs/DOC-2321)
* [Understanding DC-QCN Algorithm for RoCE Congestion Control](https://community.mellanox.com/docs/DOC-2783)
* [Congestion Control for Large-Scale RDMA Deployments](https://conferences.sigcomm.org/sigcomm/2015/pdf/papers/p523.pdf)
* [Annex A17: RoCEv2 ](https://cw.infinibandta.org/document/dl/7781)
* [RoCEv2 CNP Packet Format Example](https://community.mellanox.com/docs/DOC-2351)