---
layout: post
title: flannel host-gw network
date: 2016-03-18 17:00:30
categories: Linux
tags: network flannel
excerpt: flannel host-gw network
---

### Envirionment

```
node2 172.17.42.41
node3 172.17.42.43
```

### network information

```sh
# etcdctl set /cluster.hostgw/network/config < flannel.json                                             
{
"Network": "192.168.0.0/16",
"SubnetLen": 26,
"SubnetMin": "192.168.0.64",
"SubnetMax": "192.168.250.192",
"Backend": 
  {
    "Type": "host-gw"
  }
}
```

### start flannel

```sh
# flanneld -etcd-endpoints=http://etcd-server:2379 -etcd-prefix=/cluster.hostgw/network -logtostderr=true -v=3 &>> /var/log/flanneld &
```

### start docker

```sh
# source /run/flannel/subnet.env
# docker daemon --debug=true --bip=${FLANNEL_SUBNET} --mtu=${FLANNEL_MTU} --iptables=false --ip-masq=false --insecure-registry 10.193.1.158:5000 &>> /var/log/docker &
```

### node2's network

```sh
[root@node2 ~]# ip a
53: docker0: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 qdisc noqueue state DOWN 
    link/ether 02:42:d6:0f:f8:28 brd ff:ff:ff:ff:ff:ff
    inet 192.168.1.193/26 scope global docker0
       valid_lft forever preferred_lft forever

[root@node2 ~]# docker run --net=bridge -itd  --name='vm2' sshd:1.0                          
a8eb12382b33561161da74177395394b5ba5310cee8aa159adf681f6c63e9f53

[root@node2 ~]# docker exec vm2 ip a
54: eth0@if55: <BROADCAST,MULTICAST,UP,LOWER_UP,M-DOWN> mtu 1500 qdisc noqueue state UP 
    link/ether 02:42:c0:a8:01:c2 brd ff:ff:ff:ff:ff:ff
    inet 192.168.1.194/26 scope global eth0
       valid_lft forever preferred_lft forever
    inet6 fe80::42:c0ff:fea8:1c2/64 scope link 
       valid_lft forever preferred_lft forever

[root@node2 ~]# ip route show
default via 172.17.42.1 dev eth0 
172.17.0.0/16 dev eth0  proto kernel  scope link  src 172.17.42.41 
192.168.1.192/26 dev docker0  proto kernel  scope link  src 192.168.1.193 
192.168.6.192/26 via 172.17.42.43 dev eth0
```

### node3's network

```sh
[root@node3 ~]# ip a
27: docker0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UP 
    link/ether 02:42:a1:97:bc:10 brd ff:ff:ff:ff:ff:ff
    inet 192.168.6.193/26 scope global docker0
       valid_lft forever preferred_lft forever
    inet6 fe80::42:a1ff:fe97:bc10/64 scope link 
       valid_lft forever preferred_lft forever

[root@node3 ~]# docker run --net=bridge  -itd  --name='vm1' sshd:1.0                 
7667404482df215e0c8c33e8b9ebe42725be8a45e41f793c9350f08c789c18b3
[root@node3 ~]# docker exec vm1 ip a
28: eth0@if29: <BROADCAST,MULTICAST,UP,LOWER_UP,M-DOWN> mtu 1500 qdisc noqueue state UP 
    link/ether 02:42:c0:a8:06:c2 brd ff:ff:ff:ff:ff:ff
    inet 192.168.6.194/26 scope global eth0
       valid_lft forever preferred_lft forever
    inet6 fe80::42:c0ff:fea8:6c2/64 scope link 
       valid_lft forever preferred_lft forever

[root@node3 ~]# ip route show
default via 172.17.42.1 dev eth0 
172.17.0.0/16 dev eth0  proto kernel  scope link  src 172.17.42.43 
192.168.1.192/26 via 172.17.42.41 dev eth0 
192.168.6.192/26 dev docker0  proto kernel  scope link  src 192.168.6.193
```

### connective test

```sh
[root@node3 ~]# docker exec vm1 ping -c 3 192.168.1.194
PING 192.168.1.194 (192.168.1.194) 56(84) bytes of data.
64 bytes from 192.168.1.194: icmp_seq=1 ttl=62 time=0.313 ms
64 bytes from 192.168.1.194: icmp_seq=2 ttl=62 time=0.297 ms
64 bytes from 192.168.1.194: icmp_seq=3 ttl=62 time=0.309 ms
```

### routing path

```sh
[root@node3 ~]# docker exec vm1 traceroute 192.168.1.194     
traceroute to 192.168.1.194 (192.168.1.194), 30 hops max, 60 byte packets
 1  192.168.6.193 (192.168.6.193)  0.076 ms  0.015 ms  0.013 ms
 2  172.17.42.41 (172.17.42.41)  0.383 ms  0.356 ms  0.344 ms
 3  192.168.1.194 (192.168.1.194)  0.491 ms  0.463 ms  0.437 ms
```

vm1(192.168.6.194) <--> node3.docker0(192.168.6.193) <--> node3.eth0(172.17.42.43) <--> node2.eth0(172.17.42.41) <--> node2.docker0(192.168.1.193) <--> vm2(192.168.1.194)

### summary

host-gw is pure Layer 3 approach like Calico, which avoids the packet encapsulation associated with the Layer 2 solution, and will reduces transport overhead and improves performance.

However, note host-gw requires direct layer2 connectivity between hosts running flannel.


### related posts
* [flannel project](https://github.com/coreos/flannel)
* [Comparison of Networking Solutions for Kubernetes](http://machinezone.github.io/research/networking-solutions-for-kubernetes/)
* [Battlefield: Calico, Flannel, Weave and Docker Overlay ](https://xelatex.github.io/2015/11/15/Battlefield-Calico-Flannel-Weave-and-Docker-Overlay-Network/)

