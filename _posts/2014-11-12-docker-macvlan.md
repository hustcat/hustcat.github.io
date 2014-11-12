---
layout: post
title:  Docker自定义网络——MacVLAN
date: 2014-11-12 19:23:30
categories: Linux
tags: docker
excerpt: 一般来说，我们在自定义Docker与外部网络通信的网络，除了NAT，还有Linux Bridge、Open vSwitch、MacVLAN几种选择。MacVLAN相对于前两者，拥有更好的性能。
---

一般来说，我们在自定义Docker与外部网络通信的网络，除了NAT，还有Linux Bridge、Open vSwitch、MacVLAN几种选择。MacVLAN相对于前两者，拥有更好的性能。

MacVLAN有4种模式，参考[这里](http://backreference.org/2014/03/20/some-notes-on-macvlanmacvtap/)。
VEPA需要接入交换机支持hairpin mode。相对而言，Bridge mode更加常用。

环境

yy1: 172.16.213.128
yy2: 172.16.213.129

我们在yy2上启动容器

```sh
#docker run -d --net="none" --name=test1 dbyin/centos
# docker inspect --format="{{ .State.Pid }}" test1
2084
```

创建MACVLAN设备

```sh
# ip link add eth0.1 link eth0 type macvlan mode bridge
# ip link list
8: eth0.1@eth0: <BROADCAST,MULTICAST> mtu 1500 qdisc noop state DOWN 
link/ether 6e:e2:9c:e3:15:c6 brd ff:ff:ff:ff:ff:ff
```

将MACVLAN设备加入到容器的network space：

```sh
# ip link set netns 2084 eth0.1
# nsenter --target=2084 --net --mount --uts --pid
-bash-4.2# ip link list
8: eth0.1@if2: <BROADCAST,MULTICAST> mtu 1500 qdisc noop state DOWN mode DEFAULT 
    link/ether 6e:e2:9c:e3:15:c6 brd ff:ff:ff:ff:ff:ff
-bash-4.2# ip link set eth0.1 up
-bash-4.2# ifconfig
eth0.1: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500
        inet6 fe80::6ce2:9cff:fee3:15c6  prefixlen 64  scopeid 0x20<link>
        ether 6e:e2:9c:e3:15:c6  txqueuelen 0  (Ethernet)
        RX packets 0  bytes 0 (0.0 B)
        RX errors 0  dropped 0  overruns 0  frame 0
        TX packets 6  bytes 468 (468.0 B)
        TX errors 0  dropped 0 overruns 0  carrier 0  collisions 0
```

设置ip和网关:

```sh
-bash-4.2# ip addr add 172.16.213.180/16 dev eth0.1
-bash-4.2# ip route add default via 172.16.213.2 dev eth0.1
```

对于MACVLAN，Host是无法访问的，

```sh
[root@yy2 ~]# ping 172.16.213.180
PING 172.16.213.180 (172.16.213.180) 56(84) bytes of data.
From 172.16.213.129 icmp_seq=2 Destination Host Unreachable
```

可以在另外的Host上访问：

```sh
[root@yy1 ~]# ssh root@172.16.213.180
root@172.16.213.180's password: 
Last login: Tue Nov 11 07:49:27 2014 from 172.16.213.128
-bash-4.2#
```

> 注意：如果你是在虚拟机VMWare上测试，需要把Host的网卡设置为promisc模式：
>
> [root@yy2 ~]# ip link set eth0 promisc on
>
> 否则，其它Host也无法访问容器的网络。原因参考
> [WMware 82545EM不支持unicast filtering](http://sourceforge.net/p/e1000/mailman/message/32952083)

主要参考

* [FOUR WAYS TO CONNECT A DOCKER CONTAINER TO A LOCAL NETWORK][ref1]

[ref1]: http://blog.oddbit.com/2014/08/11/four-ways-to-connect-a-docker/
