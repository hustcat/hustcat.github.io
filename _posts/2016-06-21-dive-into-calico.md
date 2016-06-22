---
layout: post
title: Dive into Calico - IP Interconnect Fabrics
date: 2016-06-21 16:00:30
categories: Network
tags: calico
excerpt: Dive into Calico - IP Interconnect Fabrics
---

Calico的底层网络可以是[L2网络](http://docs.projectcalico.org/en/latest/l2-interconnectFabric.html)，也可以是[L3网络](https://github.com/projectcalico/calico/blob/master/docs/source/l3-interconnectFabric.rst#where-large-scale-ip-networks-and-hardware-collide)。

## Leaf-Spine Network Topology

现代数据中心都由传统的core/aggregation/access的网络架构，转向新的[leaf-spine](https://blog.westmonroepartners.com/a-beginners-guide-to-understanding-the-leaf-spine-network-topology/)架构。

具体来说，比如用于虚拟化的[大二层网络](http://www.h3c.com.cn/About_H3C/Company_Publication/IP_Lh/2012/06/Home/Catalog/201212/769069_30008_0.htm)，ToR与spine之间为L2互联:
![](/assets/calico/leaf-spine-l2.jpg)
以[FaricPath](http://www.cisco.com/c/en/us/products/collateral/switches/nexus-7000-series-switches/white_paper_c11-687554.html)/[TRILL](https://en.wikipedia.org/wiki/TRILL_%28computing%29)/[IRF](http://www.h3c.com.cn/Products___Technology/Technology/IRF/)等技术为代表，大二层网络不再使用STP消除环路，支持等价多路径转发(ECMP)等。

或者是普通的L3网络，ToR与spine之间L3(现在比较常见)、或者L2互联:
![](/assets/calico/leaf-spine-l3.jpg)

值得一提的是，[facebook](https://code.facebook.com/posts/360346274145943/introducing-data-center-fabric-the-next-generation-facebook-data-center-network/)使用的这种架构:`The network is all layer3 – from TOR uplinks to the edge. `

## [IP Interconnect Fabrics](http://docs.projectcalico.org/en/1.3.0/l3-interconnectFabric.html)

一般来说，有2种方式构建IP互联网络：

(1)基于IGP(The routing infrastructure is based on some form of IGP)，比如OSPF协议。因为IGP有很多[限制](https://www.projectcalico.org/why-bgp/)，Calico没有使用IGP，但可以将IGP与BGP结合起来，中间路由的下一跳使用IGP，端点的下一跳使用BGP。

(2)完全基于BGP(routing infrastructure is based entirely on BGP)。
这2种方式都假设ToR为边界router。


### BGP-only interconnect fabrics

有2种方式构建BGP-only互联网络：

* (1)AS per rack model
ToR交换机及其下面的所有计算节点构成一个AS.
Each of the TOR switches (and their subsidiary compute servers) are a unique Autonomous System (AS).

* (2)AS per server model

每个计算节点为一个单独的AS,ToR交换机构成中间AS.
Each of the compute servers is a unique AS, and the TOR switches make up a transit AS.


在上面2种模型中，ToR与spine之间可以是2层互联，或者3层互联。如果是3层互联，每个spine switch为一个单独的AS，ToR与spine为BGP peer。

* [IP Interconnect Fabrics in Calico](https://github.com/projectcalico/calico/blob/master/docs/source/l3-interconnectFabric.rst)


### The AS Per Rack model(ASPR)

* (1)using Ethernet as the spine interconnect

ToR与spine之间为2层互联，ToR与ToR之间为eBGP peer，受ToR的数量限制，peer的数量可能会比较多（几百个）。

![](/assets/calico/l3-aspr-01.svg)

* (2)using Routers as the spine interconnect

ToR与spine之间为3层互联，ToR与spine之间互为eBGP peer，不同的是，ToR不需要peer ToR，但spine必须peer所有ToR。
![](/assets/calico/l3-aspr-02.svg)

这2种方式中，同一个Rack下面的所有计算节点与ToR在相同的AS，它们之间可以使用BGP全互联(full mesh)，也可以在Rack内部署route reflector:可以是ToR本身，或者rack下面的某个计算节点(either hosted on the ToR itself, or as a virtual function hosted on one or more compute servers within the rack)。

ToR作为eBGP router，会将其它ToR的路由分发给相同AS下的所有计算节点，同时将计算节点的路由分发给其它ToR(This means that each compute server will see the ToR as the next hop for all external routes, and the individual compute servers are the next hop for all routes external to the rack)

### The AS per Compute Server model(ASPS)

每个计算点为一个单独的AS，也分2种情况：

* (1)using Ethernet as the spine interconnect
![](/assets/calico/l3-asps-01.svg)

* (2)using Routers as the spine interconnect
![](/assets/calico/l3-asps-02.svg)

ASPS的逻辑实现与ASPR基本一样。但有2个值得注意的地方：

> (1)AS数量会比较多，所有应该使用4字节的 AS numbers。 从这一点来看，ASPR的扩展性会好一点。<br/>
> (2)没有route reflector，所有的BGP peering都是eBGP。

### The Downward Default model

这种方式比较特殊，所有的计算节点使用相同的AS number，所有的ToR也使用相同的AS number。

![](/assets/calico/l3-fabric-downward-default.svg)

In this diagram, we are showing that all Calico nodes share the same AS number, as do all ToR switches. However, those ASs are different (A1 is not the same network as A2, even though the both share the same AS number A ).

## 小结

当Calico工作在3层网络的时候，需要ToR与计算节点，或者ToR与ToR之间开启动BGP peer，对物理网络的侵入性比较大。相比较而言，Overlay network不会依赖于物理网络，这也是Overlay的优势。

## Reference

* [Why BGP?](https://www.projectcalico.org/why-bgp/)
* [IP Interconnect Fabrics](http://docs.projectcalico.org/en/1.3.0/l3-interconnectFabric.html)
* [Calico over an Ethernet interconnect fabric](http://docs.projectcalico.org/en/latest/l2-interconnectFabric.html)
* [Video: Basic introduction to the Leaf/Spine data center networking fabric design](http://bradhedlund.com/2012/10/24/video-a-basic-introduction-to-the-leafspine-data-center-networking-fabric-design/)
* [A Beginner’s Guide to Understanding the Leaf-Spine Network Topology](https://blog.westmonroepartners.com/a-beginners-guide-to-understanding-the-leaf-spine-network-topology/)
* [External Connectivity - Hosts on their own Layer 2 segment](https://github.com/projectcalico/calico-containers/blob/master/docs/ExternalConnectivity.md)

