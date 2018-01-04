---
layout: post
title: OVN gateway practice
date: 2018-01-04 23:00:30
categories: Network
tags: OVN SDN OVS
excerpt: OVN gateway practice
---

`OVN Gateway`用于连接`overlay network`和`physical network`。它支持L2/L3两种方式：

> layer-2 which bridge an OVN logical switch into a VLAN, and layer-3 which provide a routed connection between an OVN router and the physical network.
>
> Unlike a distributed logical router (DLR), an OVN gateway router is centralized on a single host (chassis) so that it may provide services which cannot yet be distributed (NAT, load balancing, etc…). As of this publication there is a restriction that gateway routers may only connect to other routers via a logical switch, whereas DLRs may connect to one other directly via a peer link. Work is in progress to remove this restriction.
>
> It should be noted that it is possible to have multiple gateway routers tied into an environment, which means that it is possible to perform ingress ECMP routing into logical space. However, it is worth mentioning that OVN currently does not support egress ECMP between gateway routers. Again, this is being looked at as a future enhancement.


## 环境

环境: 

* node1 172.17.42.160/16 – will serve as OVN Central and Gateway Node
* node2 172.17.42.161/16 – will serve as an OVN Host
* node3 172.17.42.162/16 – will serve as an OVN Host
* client 172.18.1.10/16 - physical network node

网络拓扑结构：

```
         _________ 
        |  client | 172.18.1.10/16 Physical Network
         ---------
         ____|____ 
        |  switch | outside
         ---------
             |
         ____|____ 
        |  router | gw1 port 'gw1-outside': 172.18.1.2/16
         ---------      port 'gw1-join':    192.168.255.1/24
         ____|____ 
        |  switch | join  192.168.255.0/24
         ---------  
         ____|____ 
        |  router | router1 port 'router1-join':  192.168.255.2/24
         ---------          port 'router1-ls1': 192.168.100.1/24
             |
         ____|____ 
        |  switch | ls1 192.168.100.0/24
         ---------  
         /       \
 _______/_       _\_______  
|  vm1    |     |   vm2   |
 ---------       ---------
192.168.100.10  192.168.100.11
```

连接`vm1/vm2`的switch:

```
[root@node1 ~]# ovn-nbctl show
switch 0fab3ddd-6325-4219-aa1c-6dc9853b7069 (ls1)
    port ls1-vm1
        addresses: ["02:ac:10:ff:00:11"]
    port ls1-vm2
        addresses: ["02:ac:10:ff:00:22"]

[root@node1 ~]# ovn-sbctl show
Chassis "dc82b489-22b3-42dd-a28e-f25439316356"
    hostname: "node1"
    Encap geneve
        ip: "172.17.42.160"
        options: {csum="true"}
Chassis "598fec44-5787-452f-b527-2ef8c4adb942"
    hostname: "node2"
    Encap geneve
        ip: "172.17.42.161"
        options: {csum="true"}
    Port_Binding "ls1-vm1"
Chassis "54292ae7-c91c-423b-a936-5b416d6bae9f"
    hostname: "node3"
    Encap geneve
        ip: "172.17.42.162"
        options: {csum="true"}
    Port_Binding "ls1-vm2"
```


## 创建router(用于连接所有VM的switch)

```
# add the router
ovn-nbctl lr-add router1

# create router port for the connection to 'ls1'
ovn-nbctl lrp-add router1 router1-ls1 02:ac:10:ff:00:01 192.168.100.1/24

# create the 'ls1' switch port for connection to 'router1'
ovn-nbctl lsp-add ls1 ls1-router1
ovn-nbctl lsp-set-type ls1-router1 router
ovn-nbctl lsp-set-addresses ls1-router1 02:ac:10:ff:00:01
ovn-nbctl lsp-set-options ls1-router1 router-port=router1-ls1
```

Logical network:

```
# ovn-nbctl show
switch 0fab3ddd-6325-4219-aa1c-6dc9853b7069 (ls1)
    port ls1-router1
        type: router
        addresses: ["02:ac:10:ff:00:01"]
        router-port: router1-ls1
    port ls1-vm1
        addresses: ["02:ac:10:ff:00:11"]
    port ls1-vm2
        addresses: ["02:ac:10:ff:00:22"]
router 6dec5e02-fa39-4f2c-8e1e-7a0182f110e6 (router1)
    port router1-ls1
        mac: "02:ac:10:ff:00:01"
        networks: ["192.168.100.1/24"]
```

`vm1`访问`192.168.100.1`:

```
# ip netns exec vm1 ping -c 1 192.168.100.1 
PING 192.168.100.1 (192.168.100.1) 56(84) bytes of data.
64 bytes from 192.168.100.1: icmp_seq=1 ttl=254 time=0.275 ms
```


## 创建gateway router

指定在`node1`上部署`Gateway router`，创建router时，通过`options:chassis={chassis_uuid}`实现：

```
# create router 'gw1'
ovn-nbctl create Logical_Router name=gw1 options:chassis=dc82b489-22b3-42dd-a28e-f25439316356

# create a new logical switch for connecting the 'gw1' and 'router1' routers
ovn-nbctl ls-add join

# connect 'gw1' to the 'join' switch
ovn-nbctl lrp-add gw1 gw1-join 02:ac:10:ff:ff:01 192.168.255.1/24
ovn-nbctl lsp-add join join-gw1
ovn-nbctl lsp-set-type join-gw1 router
ovn-nbctl lsp-set-addresses join-gw1 02:ac:10:ff:ff:01
ovn-nbctl lsp-set-options join-gw1 router-port=gw1-join


# 'router1' to the 'join' switch
ovn-nbctl lrp-add router1 router1-join 02:ac:10:ff:ff:02 192.168.255.2/24
ovn-nbctl lsp-add join join-router1
ovn-nbctl lsp-set-type join-router1 router
ovn-nbctl lsp-set-addresses join-router1 02:ac:10:ff:ff:02
ovn-nbctl lsp-set-options join-router1 router-port=router1-join


# add static routes
ovn-nbctl lr-route-add gw1 "192.168.100.0/24" 192.168.255.2
ovn-nbctl lr-route-add router1 "0.0.0.0/0" 192.168.255.1
```


* logical network

```
# ovn-nbctl show
switch 0fab3ddd-6325-4219-aa1c-6dc9853b7069 (ls1)
    port ls1-router1
        type: router
        addresses: ["02:ac:10:ff:00:01"]
        router-port: router1-ls1
    port ls1-vm1
        addresses: ["02:ac:10:ff:00:11"]
    port ls1-vm2
        addresses: ["02:ac:10:ff:00:22"]
switch d4b119e9-0298-42ab-8cc7-480292231953 (join)
    port join-gw1
        type: router
        addresses: ["02:ac:10:ff:ff:01"]
        router-port: gw1-join
    port join-router1
        type: router
        addresses: ["02:ac:10:ff:ff:02"]
        router-port: router1-join
router 6dec5e02-fa39-4f2c-8e1e-7a0182f110e6 (router1)
    port router1-ls1
        mac: "02:ac:10:ff:00:01"
        networks: ["192.168.100.1/24"]
    port router1-join
        mac: "02:ac:10:ff:ff:02"
        networks: ["192.168.255.2/24"]
router f29af2c3-e9d1-46f9-bff7-b9b8f0fd56df (gw1)
    port gw1-join
        mac: "02:ac:10:ff:ff:01"
        networks: ["192.168.255.1/24"]
```

`node2`上的`vm1`访问`gw1`:

```
[root@node2 ~]# ip netns exec vm1 ip route add default via 192.168.100.1

[root@node2 ~]# ip netns exec vm1 ping -c 1 192.168.255.1
PING 192.168.255.1 (192.168.255.1) 56(84) bytes of data.
64 bytes from 192.168.255.1: icmp_seq=1 ttl=253 time=2.18 ms
```


## 连接overlay network与physical network

这里假设物理网络的IP为`172.18.0.0/16`:

```
# create new port on router 'gw1'
ovn-nbctl lrp-add gw1 gw1-outside 02:0a:7f:18:01:02 172.18.1.2/16

# create new logical switch and connect it to 'gw1'
ovn-nbctl ls-add outside
ovn-nbctl lsp-add outside outside-gw1
ovn-nbctl lsp-set-type outside-gw1 router
ovn-nbctl lsp-set-addresses outside-gw1 02:0a:7f:18:01:02
ovn-nbctl lsp-set-options outside-gw1 router-port=gw1-outside

# create a bridge for eth1 (run on 'node1')
ovs-vsctl add-br br-eth1

# create bridge mapping for eth1. map network name "phyNet" to br-eth1 (run on 'node1')
ovs-vsctl set Open_vSwitch . external-ids:ovn-bridge-mappings=phyNet:br-eth1

# create localnet port on 'outside'. set the network name to "phyNet"
ovn-nbctl lsp-add outside outside-localnet
ovn-nbctl lsp-set-addresses outside-localnet unknown
ovn-nbctl lsp-set-type outside-localnet localnet
ovn-nbctl lsp-set-options outside-localnet network_name=phyNet

# connect eth1 to br-eth1 (run on 'node1')
ovs-vsctl add-port br-eth1 eth1
```

完整的`logical network`:

```
# ovn-nbctl show
switch d4b119e9-0298-42ab-8cc7-480292231953 (join)
    port join-gw1
        type: router
        addresses: ["02:ac:10:ff:ff:01"]
        router-port: gw1-join
    port join-router1
        type: router
        addresses: ["02:ac:10:ff:ff:02"]
        router-port: router1-join
switch 0fab3ddd-6325-4219-aa1c-6dc9853b7069 (ls1)
    port ls1-router1
        type: router
        addresses: ["02:ac:10:ff:00:01"]
        router-port: router1-ls1
    port ls1-vm1
        addresses: ["02:ac:10:ff:00:11"]
    port ls1-vm2
        addresses: ["02:ac:10:ff:00:22"]
switch 64dc14b1-3e0f-4f68-b388-d76826a5c972 (outside)
    port outside-gw1
        type: router
        addresses: ["02:0a:7f:18:01:02"]
        router-port: gw1-outside
    port outside-localnet
        type: localnet
        addresses: ["unknown"]
router 6dec5e02-fa39-4f2c-8e1e-7a0182f110e6 (router1)
    port router1-ls1
        mac: "02:ac:10:ff:00:01"
        networks: ["192.168.100.1/24"]
    port router1-join
        mac: "02:ac:10:ff:ff:02"
        networks: ["192.168.255.2/24"]
router f29af2c3-e9d1-46f9-bff7-b9b8f0fd56df (gw1)
    port gw1-outside
        mac: "02:0a:7f:18:01:02"
        networks: ["172.18.1.2/16"]
    port gw1-join
        mac: "02:ac:10:ff:ff:01"
        networks: ["192.168.255.1/24"]
```

`vm1`访问`gw1-outside`:

```
[root@node2 ~]# ip netns exec vm1 ping -c 1 172.18.1.2   
PING 172.18.1.2 (172.18.1.2) 56(84) bytes of data.
64 bytes from 172.18.1.2: icmp_seq=1 ttl=253 time=1.00 ms
```


### 物理网络访问overlay( by direct)

对于与`node1`在同一个L2网络的节点，可以通过配置路由访问overlay network：

`client (172.18.1.10)` -> `gw1`:

```
# ip route show
172.18.0.0/16 dev eth1  proto kernel  scope link  src 172.18.1.10 

# ping -c 1 172.18.1.2
PING 172.18.1.2 (172.18.1.2) 56(84) bytes of data.
64 bytes from 172.18.1.2: icmp_seq=1 ttl=254 time=0.438 ms
```

`client`增加访问`192.168.0.0/16`的路由：

```
ip route add 192.168.0.0/16 via 172.18.1.2 dev eth1
```

`client (172.18.1.10)` -> `vm1 (192.168.100.10)`
```
# ping -c 1 192.168.100.10
PING 192.168.100.10 (192.168.100.10) 56(84) bytes of data.
64 bytes from 192.168.100.10: icmp_seq=1 ttl=62 time=1.35 ms
```

`vm1`:
```
[root@node2 ~]# ip netns exec vm1 tcpdump -nnvv -i vm1
tcpdump: listening on vm1, link-type EN10MB (Ethernet), capture size 262144 bytes
07:41:28.561299 IP (tos 0x0, ttl 62, id 0, offset 0, flags [DF], proto ICMP (1), length 84)
    172.18.1.10 > 192.168.100.10: ICMP echo request, id 8160, seq 1, length 64
07:41:28.561357 IP (tos 0x0, ttl 64, id 53879, offset 0, flags [none], proto ICMP (1), length 84)
    192.168.100.10 > 172.18.1.10: ICMP echo reply, id 8160, seq 1, length 64
```

### overlay访问物理网络(by NAT)

对于overlay访问物理网络，可以通过NAT来实现。

创建NAT规则：

```
# ovn-nbctl -- --id=@nat create nat type="snat" logical_ip=192.168.100.0/24 external_ip=172.18.1.2 -- add logical_router gw1 nat @nat
3243ffd3-8d77-4bd3-9f7e-49e74e87b4a7

# ovn-nbctl lr-nat-list gw1
TYPE             EXTERNAL_IP        LOGICAL_IP            EXTERNAL_MAC         LOGICAL_PORT
snat             172.18.1.2         192.168.100.0/24

# ovn-nbctl list NAT       
_uuid               : 3243ffd3-8d77-4bd3-9f7e-49e74e87b4a7
external_ip         : "172.18.1.2"
external_mac        : []
logical_ip          : "192.168.100.0/24"
logical_port        : []
type                : snat
```

可以通过命令`ovn-nbctl lr-nat-del gw1 snat 192.168.100.0/24`删除NAT规则.


`vm1 192.168.100.10` -> `client 172.18.1.10`:

```
[root@node2 ~]# ip netns exec vm1 ping -c 1 172.18.1.10
PING 172.18.1.10 (172.18.1.10) 56(84) bytes of data.
64 bytes from 172.18.1.10: icmp_seq=1 ttl=62 time=1.63 ms


[root@client ~]# tcpdump icmp -nnvv -i eth1
[10894068.821880] device eth1 entered promiscuous mode
tcpdump: listening on eth1, link-type EN10MB (Ethernet), capture size 65535 bytes
08:24:50.316495 IP (tos 0x0, ttl 61, id 31580, offset 0, flags [DF], proto ICMP (1), length 84)
    172.18.1.2 > 172.18.1.10: ICMP echo request, id 5587, seq 1, length 64
08:24:50.316536 IP (tos 0x0, ttl 64, id 6129, offset 0, flags [none], proto ICMP (1), length 84)
    172.18.1.10 > 172.18.1.2: ICMP echo reply, id 5587, seq 1, length 64
```

可以看到，`client`端看到的IP为`172.18.1.2`.

## Refs

* [The OVN Gateway Router](http://blog.spinhirne.com/2016/09/the-ovn-gateway-router.html)
* [OVN Sandbox](https://github.com/openvswitch/ovs/blob/v2.8.1/Documentation/tutorials/ovn-sandbox.rst)