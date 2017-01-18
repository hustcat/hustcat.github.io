---
layout: post
title: IPIP tunnel, GRE tunnel and LVS IP tunnel
date: 2016-02-12 18:00:30
categories: Linux
tags: LVS 
excerpt: IPIP tunnel, GRE tunnel and LVS IP tunnel
---

# IPIP tunnel

* Server A

```
# ip tunnel add c2d mode ipip remote 172.17.42.112 local 172.17.42.110 dev eth0
# ip a
4: tunl0: <NOARP> mtu 0 qdisc noop state DOWN 
    link/ipip 172.17.42.110 brd 172.17.42.112
7: c2d@eth0: <POINTOPOINT,NOARP> mtu 1480 qdisc noop state DOWN 
    link/ipip 172.17.42.110 peer 172.17.42.112

# ip link set tunl0 up
# ifconfig c2d 192.168.2.1 netmask 255.255.255.0
# ip a
7: c2d@eth0: <POINTOPOINT,NOARP,UP,LOWER_UP> mtu 1480 qdisc noqueue state UNKNOWN 
    link/ipip 172.17.42.110 peer 172.17.42.112
    inet 192.168.2.1/24 scope global c2d
       valid_lft forever preferred_lft forever
```

* Server B

```
# ip tunnel add d2c mode ipip remote 172.17.42.110 local 172.17.42.112 dev eth0
# ip a
4: tunl0: <NOARP> mtu 0 qdisc noop state DOWN 
    link/ipip 172.17.42.112 brd 172.17.42.110
5: d2c@eth0: <POINTOPOINT,NOARP> mtu 1480 qdisc noop state DOWN 
    link/ipip 172.17.42.112 peer 172.17.42.110


# ifconfig d2c 192.168.2.2 netmask 255.255.255.0
# ip a
5: d2c@eth0: <POINTOPOINT,NOARP,UP,LOWER_UP> mtu 1480 qdisc noqueue state UNKNOWN 
    link/ipip 172.17.42.112 peer 172.17.42.110
    inet 192.168.2.2/24 scope global d2c
       valid_lft forever preferred_lft forever
```

* A ping B

```
# ping -c 3 192.168.2.2
PING 192.168.2.2 (192.168.2.2) 56(84) bytes of data.
64 bytes from 192.168.2.2: icmp_seq=1 ttl=64 time=0.143 ms
64 bytes from 192.168.2.2: icmp_seq=2 ttl=64 time=0.249 ms
64 bytes from 192.168.2.2: icmp_seq=3 ttl=64 time=0.243 ms

--- 192.168.2.2 ping statistics ---
3 packets transmitted, 3 received, 0% packet loss, time 1999ms
rtt min/avg/max/mdev = 0.143/0.211/0.249/0.051 ms
```

* IPIP tunnel internal

ipip_tunnel_xmit


参考[Linux Netfilter实现机制和扩展技术](https://www.ibm.com/developerworks/cn/linux/l-ntflt/)

# IPIP vs GRE tunnel 

(1)GRE tunnel相对于IPIP tunnel，多一个GRE header(4~16 bytes)，它可以封装任何三层协议，而IPIP只能封装IP协议；

(2)IPIP的tunnel是在host上创建的，而GRE是在中间router上创建的。

# 相关资料

* [GRE vs IPIP Tunneling](http://packetlife.net/blog/2012/feb/27/gre-vs-ipip-tunneling)
* [使用ip tunnel打通私有网络](http://www.opstool.com/article/183)
* [5.3. GRE tunneling](http://lartc.org/howto/lartc.tunnel.gre.html)
* [LVS集群系统网络核心原理分析](http://zh.linuxvirtualserver.org/node/98)