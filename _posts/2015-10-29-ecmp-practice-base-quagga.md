---
layout: post
title: Equal Cost Multipath(ECMP) practice base on Quagga
date: 2015-10-29 18:16:30
categories: Linux
tags: network ecmp quagga
excerpt: Equal Cost Multipath(ECMP) practice base on Quagga
---

# 网络环境

** client: **

* yy1(172.17.42.2/16, gateway 172.17.42.1/16)
* yy3(172.18.42.2/16, gateway 172.18.42.1/16)

** router: **

* router-17(172.17.42.1/16, 192.168.0.2/24, 192.168.1.2/24)
* router-18(172.18.42.1/16, 192.168.0.3/24, 192.168.1.3/24)

整个网络结构大致如下：

![](/assets/2015-10-29-ecmp-practice-base-quagga.jpg)

# zebra配置

```sh
# cat /etc/quagga/zebra.conf 
hostname router-17
password zebra
enable password zebra

#service quagga start

[root@router-17 ~]# vtysh 

Hello, this is Quagga (version 0.99.22.4).
Copyright 1996-2005 Kunihiro Ishiguro, et al.

router-18# configure terminal
router-18(config)# log file /var/log/quagga/quagga.log
router-18(config)# exit
router-18# write
Building Configuration...
Configuration saved to /etc/quagga/zebra.conf
[OK]
```

# ospfd配置

```sh
# cat /etc/quagga/ospfd.conf
! -*- ospf -*-
!
! OSPFd sample configuration file
!
!
hostname router-17
password zebra
enable password zebra
!
!router ospf
!  network 192.168.1.0/24 area 0
!
log stdout


#启动ospfd
# service ospfd start
```

** 配置网络 **

配置router-17的网络

```sh
router-17# configure terminal
router-17(config)# router ospf
router-17(config-router)# router-id 192.168.1.2
router-17(config-router)# network 192.168.0.0/24 area 0
router-17(config-router)# network 192.168.1.0/24 area 0
router-17(config-router)# network 172.17.0.0/16 area 0
router-17(config-router)# do write
```

配置router-18的网络

```sh
router-18# configure terminal
router-18(config)# router ospf
router-18(config-router)# router-id 192.168.1.3
router-18(config-router)# network 192.168.0.0/24 area 0
router-18(config-router)# network 192.168.1.0/24 area 0
router-18(config-router)# network 172.18.0.0/16 area 0
router-18(config-router)# do write
```

** 查看路由 **

```sh
router-17# show ip route
Codes: K - kernel route, C - connected, S - static, R - RIP,
       O - OSPF, I - IS-IS, B - BGP, A - Babel,
       > - selected route, * - FIB route

C>* 127.0.0.0/8 is directly connected, lo
O   172.17.0.0/16 [110/10] is directly connected, eth0, 00:04:55
C>* 172.17.0.0/16 is directly connected, eth0
O>* 172.18.0.0/16 [110/20] via 192.168.0.3, eth1, 00:00:48
  *                        via 192.168.1.3, eth2, 00:00:48
O   192.168.0.0/24 [110/10] is directly connected, eth1, 00:05:08
C>* 192.168.0.0/24 is directly connected, eth1
O   192.168.1.0/24 [110/10] is directly connected, eth2, 00:05:01
C>* 192.168.1.0/24 is directly connected, eth2


router-18# show ip route
Codes: K - kernel route, C - connected, S - static, R - RIP,
       O - OSPF, I - IS-IS, B - BGP, A - Babel,
       > - selected route, * - FIB route

C>* 127.0.0.0/8 is directly connected, lo
O>* 172.17.0.0/16 [110/20] via 192.168.0.2, eth1, 00:00:19
  *                        via 192.168.1.2, eth2, 00:00:19
O   172.18.0.0/16 [110/10] is directly connected, eth0, 00:00:17
C>* 172.18.0.0/16 is directly connected, eth0
O   192.168.0.0/24 [110/10] is directly connected, eth1, 00:00:27
C>* 192.168.0.0/24 is directly connected, eth1
O   192.168.1.0/24 [110/10] is directly connected, eth2, 00:00:22
C>* 192.168.1.0/24 is directly connected, eth2

[root@router-17 ~]# ip route show
172.17.0.0/16 dev eth0  proto kernel  scope link  src 172.17.42.1 
172.18.0.0/16  proto zebra  metric 20 
        nexthop via 192.168.0.3  dev eth1 weight 1
        nexthop via 192.168.1.3  dev eth2 weight 1
192.168.0.0/24 dev eth1  proto kernel  scope link  src 192.168.0.2 
192.168.1.0/24 dev eth2  proto kernel  scope link  src 192.168.1.2

[root@router-18 ~]# ip route show
172.17.0.0/16  proto zebra  metric 20 
        nexthop via 192.168.0.2  dev eth1 weight 1
        nexthop via 192.168.1.2  dev eth2 weight 1
172.18.0.0/16 dev eth0  proto kernel  scope link  src 172.18.42.1 
192.168.0.0/24 dev eth1  proto kernel  scope link  src 192.168.0.3 
192.168.1.0/24 dev eth2  proto kernel  scope link  src 192.168.1.3
```

# 测试

## 连通性测试

```sh
[root@yy3 ~]# ping 172.17.42.2
PING 172.17.42.2 (172.17.42.2) 56(84) bytes of data.
64 bytes from 172.17.42.2: icmp_seq=1 ttl=62 time=0.354 ms
64 bytes from 172.17.42.2: icmp_seq=2 ttl=62 time=0.323 ms

[root@yy3 ~]# traceroute 172.17.42.2 
traceroute to 172.17.42.2 (172.17.42.2), 30 hops max, 60 byte packets
 1  172.18.42.1 (172.18.42.1)  0.452 ms  0.382 ms  0.332 ms
 2  192.168.0.2 (192.168.0.2)  0.285 ms  0.339 ms  0.284 ms
 3  172.17.42.2 (172.17.42.2)  0.430 ms  0.374 ms  0.324 ms

[root@yy1 ~]# traceroute 172.18.42.2
traceroute to 172.18.42.2 (172.18.42.2), 30 hops max, 60 byte packets
 1  172.17.42.1 (172.17.42.1)  0.137 ms  0.081 ms  0.070 ms
 2  192.168.1.3 (192.168.1.3)  0.190 ms  0.153 ms  0.173 ms
 3  172.18.42.2 (172.18.42.2)  0.371 ms  0.309 ms  0.274 ms
```

可以看到，yy3->yy1与yy1->yy3两边走的路径并不一样。

## 动态路由测试

关掉router-18的eth1接口

```sh
router-18# configure terminal
router-18(config)# interface eth1
router-18(config-if)# shutdown
router-18(config-if)# exit
router-18(config)# exit
router-18# show ip route
Codes: K - kernel route, C - connected, S - static, R - RIP,
       O - OSPF, I - IS-IS, B - BGP, A - Babel,
       > - selected route, * - FIB route

C>* 127.0.0.0/8 is directly connected, lo
O>* 172.17.0.0/16 [110/20] via 192.168.1.2, eth2, 00:00:19
O   172.18.0.0/16 [110/10] is directly connected, eth0, 01:18:13
C>* 172.18.0.0/16 is directly connected, eth0
O>* 192.168.0.0/24 [110/20] via 192.168.1.2, eth2, 00:00:19
O   192.168.1.0/24 [110/10] is directly connected, eth2, 01:18:18
C>* 192.168.1.0/24 is directly connected, eth2
```

查看路由

```sh
[root@router-18 ~]# ip route list
172.17.0.0/16 via 192.168.1.2 dev eth2  proto zebra  metric 20 
172.18.0.0/16 dev eth0  proto kernel  scope link  src 172.18.42.1 
192.168.0.0/24 via 192.168.1.2 dev eth2  proto zebra  metric 20 
192.168.1.0/24 dev eth2  proto kernel  scope link  src 192.168.1.3

[root@yy3 ~]# traceroute 172.17.42.2 
traceroute to 172.17.42.2 (172.17.42.2), 30 hops max, 60 byte packets
 1  172.18.42.1 (172.18.42.1)  0.439 ms  0.373 ms  0.323 ms
 2  192.168.1.2 (192.168.1.2)  0.246 ms  0.196 ms  0.149 ms
 3  172.17.42.2 (172.17.42.2)  0.369 ms  0.326 ms  0.281 ms
```

可以看到yy3->yy1的路径发生了变化。


# 配置注意事项

Router必须开启ip转发：

```sh
# cat /proc/sys/net/ipv4/ip_forward 
1
```

Router作为网关的接口必须开启arp proxy：

```sh
# cat /proc/sys/net/ipv4/conf/eth0/proxy_arp      
1
```

清除所有接口arp缓存

```sh
# ip neigh flush dev eth0
```

# 参考资料

* [Equal-cost multi-path routing](https://en.wikipedia.org/wiki/Equal-cost_multi-path_routing)
* [quagga doc](http://www.nongnu.org/quagga/docs/quagga.html)
* [How to turn your CentOS box into an OSPF router using Quagga](http://xmodulo.com/turn-centos-box-into-ospf-router-quagga.html)
* [Equal Cost Multipath](https://l3net.wordpress.com/2012/11/08/equal-cost-multipath/)
* [路由观念与路由器设定](http://vbird.dic.ksu.edu.tw/linux_server/0230router.php)