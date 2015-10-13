---
layout: post
title: Overlay network base on GRE/VXLAN with Openvswitch
date: 2015-10-13 20:00:30
categories: Linux
tags: network
excerpt: Overlay network base on GRE/VXLAN with Openvswitch
---

相对于Linux bridge，使用Openvswitch构建overlay network更加简单直接。

机器环境

```
yy2 10.193.6.36
yy3 10.193.6.37
```

内核

```
4.1.10
```

# 创建容器

** 在yy2上 **

```sh
[root@yy2 ~]# docker run -itd --net=none --name=vmX busybox    
d89ec0a35489ca2330efc0f309a815308c7104098f28cf221c3f41920ed84f6d

[root@yy2 ~]# ip link add vethX type veth peer name veth1 
[root@yy2 ~]# ip link set veth1 netns 49f114a96775 
[root@yy2 ~]# ip netns exec 49f114a96775 ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN 
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
13: veth1@if14: <BROADCAST,MULTICAST> mtu 1500 qdisc noop state DOWN qlen 1000
    link/ether 3e:36:f1:9e:0e:76 brd ff:ff:ff:ff:ff:ff

[root@yy2 ~]# ip netns exec 49f114a96775 ip addr add 172.20.1.1/24 dev veth1
[root@yy2 ~]# ip netns exec 49f114a96775 ip link set veth1 up
[root@yy2 ~]# ip netns exec 49f114a96775 ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN 
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
13: veth1@if14: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 qdisc pfifo_fast state LOWERLAYERDOWN qlen 1000
    link/ether 3e:36:f1:9e:0e:76 brd ff:ff:ff:ff:ff:ff
    inet 172.18.1.1/24 scope global veth1
       valid_lft forever preferred_lft forever

[root@yy2 ~]# ovs-vsctl add-br br-int
[root@yy2 ~]# ovs-vsctl add-port br-int vethX
[root@yy2 ~]# ovs-vsctl set port vethX tag=20
[root@yy2 ~]# ovs-vsctl show
85e56285-2d5b-43ac-b697-1ffbf7dcf6aa
    Bridge br-int
        Port br-int
            Interface br-int
                type: internal
        Port vethX
            tag: 20
            Interface vethX
    ovs_version: "2.3.1"
```

** 在yy3上 **

```sh
[root@yy3 ~]# docker run -itd --net=none --name=vmY busybox
ac77cfbc9e84a7378b87f8c91801cd471e239153cd8f6e4fae197f68349bf71d
[root@yy3 ~]# ip link add vethY type veth peer name veth1 
[root@yy3 ~]# ip link set veth1 netns f23b42e0f955
[root@yy3 ~]# ip netns exec f23b42e0f955 ip addr add 172.20.1.2/24 dev veth1
[root@yy3 ~]# ip netns exec f23b42e0f955 ip link set veth1 up
[root@yy3 ~]# ip netns exec f23b42e0f955 ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN 
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
10: veth1@if11: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 qdisc pfifo_fast state LOWERLAYERDOWN qlen 1000
    link/ether 0e:b3:e0:43:ae:bc brd ff:ff:ff:ff:ff:ff
    inet 172.18.1.2/24 scope global veth1
       valid_lft forever preferred_lft forever

[root@yy3 ~]# ovs-vsctl show
b6acce5a-9955-4163-8102-2a66188c8f42
    Bridge br-int
        Port vethY
            tag: 20
            Interface vethY
        Port br-int
            Interface br-int
                type: internal
ovs_version: "2.3.1"
```

# 创建GRE/VXLAN tunnel

** GRE tunnel **

```sh
[root@yy2 ~]# ovs-vsctl add-port br-int gre0 -- set interface gre0 type=gre options:remote_ip=10.193.6.37
[root@yy2 ~]# ovs-vsctl show     
85e56285-2d5b-43ac-b697-1ffbf7dcf6aa
    Bridge br-int
        Port br-int
            Interface br-int
                type: internal
        Port vethX
            tag: 20
            Interface vethX
        Port "gre0"
            Interface "gre0"
                type: gre
                options: {remote_ip="10.193.6.37"}
ovs_version: "2.3.1"

[root@yy3 ~]#  ovs-vsctl add-port br-int gre0 -- set interface gre0 type=gre options:remote_ip=10.193.6.36
[root@yy3 ~]# ovs-vsctl show     
b6acce5a-9955-4163-8102-2a66188c8f42
    Bridge br-int
        Port vethY
            tag: 20
            Interface vethY
        Port br-int
            Interface br-int
                type: internal
        Port "gre0"
            Interface "gre0"
                type: gre
                options: {remote_ip="10.193.6.36"}
ovs_version: "2.3.1"
```

** VXLAN tunnel **

```sh
[root@yy2 ~]# ovs-vsctl add-port br-int vxlan1 -- set interface vxlan1 type=vxlan options:remote_ip=10.193.6.37
[root@yy2 ~]# ovs-vsctl show
85e56285-2d5b-43ac-b697-1ffbf7dcf6aa
    Bridge br-int
        Port "vxlan1"
            Interface "vxlan1"
                type: vxlan
                options: {remote_ip="10.193.6.37"}
        Port br-int
            Interface br-int
                type: internal
        Port vethX
            Interface vethX
ovs_version: "2.3.1"

[root@yy3 ~]# ovs-vsctl add-port br-int vxlan1 -- set interface vxlan1 type=vxlan options:remote_ip=10.193.6.36
[root@yy3 ~]# ovs-vsctl show
b6acce5a-9955-4163-8102-2a66188c8f42
    Bridge br-int
        Port vethY
            Interface vethY
        Port br-int
            Interface br-int
                type: internal
        Port "vxlan1"
            Interface "vxlan1"
                type: vxlan
                options: {remote_ip="10.193.6.36"}
ovs_version: "2.3.1"
```

测试网络

```sh
[root@yy2 ~]# ip netns exec 49f114a96775 ping 172.20.1.2
PING 172.20.1.2 (172.20.1.2) 56(84) bytes of data.
64 bytes from 172.20.1.2: icmp_seq=1 ttl=64 time=1.41 ms
64 bytes from 172.20.1.2: icmp_seq=2 ttl=64 time=0.533 ms
64 bytes from 172.20.1.2: icmp_seq=3 ttl=64 time=0.539 ms
64 bytes from 172.20.1.2: icmp_seq=4 ttl=64 time=0.454 ms
64 bytes from 172.20.1.2: icmp_seq=5 ttl=64 time=0.631 ms
```

# 网络结构

![](/assets/2015-10-13-overlay-network-base-gre-network.jpg)

# GRE协议

![](/assets/2015-10-13-overlay-network-base-gre-protocal.png)

相比VXLAN，GRE直接基于IP实现tunnel，而VXLAN是基于UDP实现tunnel。

# 实现

![](/assets/2015-10-13-overlay-network-base-gre-implement.jpg)


# 相关资料
* [Namespaces, VLANs, Open vSwitch, and GRE Tunnels](http://blog.scottlowe.org/2013/09/09/namespaces-vlans-open-vswitch-and-gre-tunnels/)
* [Connecting Docker containers on multiple hosts](https://goldmann.pl/blog/2014/01/21/connecting-docker-containers-on-multiple-hosts/)
* [Generic Routing Encapsulation](https://en.wikipedia.org/wiki/Generic_Routing_Encapsulation)
* [深入理解 GRE tunnel](http://wangcong.org/2012/11/08/-e6-b7-b1-e5-85-a5-e7-90-86-e8-a7-a3-gre-tunnel/)
* [GRE tunneling](http://lartc.org/howto/lartc.tunnel.gre.html)