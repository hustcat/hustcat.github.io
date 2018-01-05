---
layout: post
title: OVN load balancer practice
date: 2018-01-05 16:28:30
categories: Network
tags: OVN SDN OVS
excerpt: OVN load balancer practice
---

## Web server

`node2`:

```
mkdir -p /tmp/www
echo "i am vm1" > /tmp/www/index.html
cd /tmp/www
ip netns exec vm1 python -m SimpleHTTPServer 8000
```

`node3`:

```
mkdir -p /tmp/www
echo "i am vm2" > /tmp/www/index.html
cd /tmp/www
ip netns exec vm2 python -m SimpleHTTPServer 8000
```

## Configuring the Load Balancer Rules

首先配置LB规则，VIP `172.18.1.254`为`Physical Network`中的IP.

在`node1`上执行：

```
uuid=`ovn-nbctl create load_balancer vips:172.18.1.254="192.168.100.10,192.168.100.11"`
echo $uuid
```

这会在`Northbound DB`的`load_balancer`表中创建对应的项：

```
[root@node1 ~]# ovn-nbctl list load_balancer 
_uuid               : 3760718f-294f-491a-bf63-3faf3573d44c
external_ids        : {}
name                : ""
protocol            : []
vips                : {"172.18.1.254"="192.168.100.10,192.168.100.11"}
```


## 使用`Gateway Router`作为`Load Balancer`

`node1`:

```
ovn-nbctl set logical_router gw1 load_balancer=$uuid
```

```
[root@node1 ~]# ovn-nbctl lb-list                             
UUID                                    LB                  PROTO      VIP                      IPs
3760718f-294f-491a-bf63-3faf3573d44c                        tcp/udp    172.18.1.254             192.168.100.10,192.168.100.11

[root@node1 ~]# ovn-nbctl lr-lb-list gw1
UUID                                    LB                  PROTO      VIP                      IPs
3760718f-294f-491a-bf63-3faf3573d44c                        tcp/udp    172.18.1.254             192.168.100.10,192.168.100.11
```


访问LB，`client` -> `vip`:

```
[root@client ~]# curl http://172.18.1.254:8000
i am vm2
[root@client ~]# curl http://172.18.1.254:8000
i am vm1
```

> load balancer is not performing any sort of health checking. At present, the assumption is that health checks would be performed by an orchestration solution such as Kubernetes but it would be resonable to assume that this feature would be added at some future point.

删除LB:

```
ovn-nbctl clear logical_router gw1 load_balancer
ovn-nbctl destroy load_balancer $uuid
```

## 在`Logical Switch`上配置`Load Balancer`

为了方便测试，再创建`logical switch 'ls2' 192.168.101.0/24`:

```
# create the logical switch
ovn-nbctl ls-add ls2

# create logical port
ovn-nbctl lsp-add ls2 ls2-vm3
ovn-nbctl lsp-set-addresses ls2-vm3 02:ac:10:ff:00:33
ovn-nbctl lsp-set-port-security ls2-vm3 02:ac:10:ff:00:33

# create logical port
ovn-nbctl lsp-add ls2 ls2-vm4
ovn-nbctl lsp-set-addresses ls2-vm4 02:ac:10:ff:00:44
ovn-nbctl lsp-set-port-security ls2-vm4 02:ac:10:ff:00:44


# create router port for the connection to 'ls2'
ovn-nbctl lrp-add router1 router1-ls2 02:ac:10:ff:01:01 192.168.101.1/24

# create the 'ls2' switch port for connection to 'router1'
ovn-nbctl lsp-add ls2 ls2-router1
ovn-nbctl lsp-set-type ls2-router1 router
ovn-nbctl lsp-set-addresses ls2-router1 02:ac:10:ff:01:01
ovn-nbctl lsp-set-options ls2-router1 router-port=router1-ls2
```


```
# ovn-nbctl show
switch b97eb754-ea59-41f6-b435-ea6ada4659d1 (ls2)
    port ls2-vm3
        addresses: ["02:ac:10:ff:00:33"]
    port ls2-router1
        type: router
        addresses: ["02:ac:10:ff:01:01"]
        router-port: router1-ls2
    port ls2-vm4
        addresses: ["02:ac:10:ff:00:44"]
```


在`node2`上创建`vm3`:

```
ip netns add vm3
ovs-vsctl add-port br-int vm3 -- set interface vm3 type=internal
ip link set vm3 netns vm3
ip netns exec vm3 ip link set vm3 address 02:ac:10:ff:00:33
ip netns exec vm3 ip addr add 192.168.101.10/24 dev vm3
ip netns exec vm3 ip link set vm3 up
ip netns exec vm3 ip route add default via 192.168.101.1 dev vm3
ovs-vsctl set Interface vm3 external_ids:iface-id=ls2-vm3
```


`node1`:

```
uuid=`ovn-nbctl create load_balancer vips:10.254.10.10="192.168.100.10,192.168.100.11"`
echo $uuid
```

set LB for `ls2`:

```
ovn-nbctl set logical_switch ls2 load_balancer=$uuid
ovn-nbctl get logical_switch ls2 load_balancer
```

结果:
```
# # ovn-nbctl ls-lb-list ls2
UUID                                    LB                  PROTO      VIP                      IPs
a19bece1-52bf-4555-89f4-257534c0b9d9                        tcp/udp    10.254.10.10             192.168.100.10,192.168.100.11
```

测试（`vm3` -> `VIP`）:

```
[root@node2 ~]# ip netns exec vm3 curl 10.254.10.10:8000
i am vm2
[root@node2 ~]# ip netns exec vm3 curl 10.254.10.10:8000
i am vm1
```

值得注意的是，如果在`ls1`上设置LB，`vm3`不能访问VIP，这说明，LB是对client的switch，而不是server端的switch.

> This highlights the requirement that load balancing be applied on the client’s logical switch rather than the server’s logical switch.


删除LB:

```
ovn-nbctl clear logical_switch ls2 load_balancer
ovn-nbctl destroy load_balancer $uuid
```

## Tracing

* `vm3` -> `192.168.101.1`:

```
ovs-appctl ofproto/trace br-int in_port=14,icmp,icmp_type=0x8,dl_src=02:ac:10:ff:00:33,dl_dst=02:ac:10:ff:01:01,nw_src=192.168.101.10,nw_dst=192.168.101.1 
```

* `vm3` -> `vm1`:

```
ovs-appctl ofproto/trace br-int in_port=14,ip,dl_src=02:ac:10:ff:00:33,dl_dst=02:ac:10:ff:01:01,nw_src=192.168.101.10,nw_dst=192.168.100.10,nw_ttl=32

ovs-appctl ofproto/trace br-int in_port=14,icmp,icmp_type=0x8,dl_src=02:ac:10:ff:00:33,dl_dst=02:ac:10:ff:01:01,nw_src=192.168.101.10,nw_dst=192.168.100.10,nw_ttl=32
```

## 原理

`OVN`的实现利用了`OVS`的NAT规则：

```
17. ct_state=+new+trk,ip,metadata=0xa,nw_dst=10.254.10.10, priority 110, cookie 0x30d9e9b5
    group:1
    ct(commit,table=18,zone=NXM_NX_REG13[0..15],nat(dst=192.168.100.10))
    nat(dst=192.168.100.10)
     -> A clone of the packet is forked to recirculate. The forked pipeline will be resumed at table 18.
```

如下：

```
table=17, n_packets=10, n_bytes=740, idle_age=516, priority=110,ct_state=+new+trk,ip,metadata=0xa,nw_dst=10.254.10.10 actions=group:1
```

## 问题定位

执行`ovn-nbctl lsp-set-addresses ls2-vm3 02:ac:10:ff:00:33`修改`ls2-vm3`的MAC后，`vm3`无法ping `192.168.101.1`，使用`ovs-appctl ofproto/trace`:

```
# ovs-appctl ofproto/trace br-int in_port=14,icmp,icmp_type=0x8,dl_src=02:ac:10:ff:00:33,dl_dst=02:ac:10:ff:01:01,nw_src=192.168.101.10,nw_dst=192.168.101.1 
...
14. ip,metadata=0x6, priority 0, cookie 0x986c8ad3
    push:NXM_NX_REG0[]
    push:NXM_NX_XXREG0[96..127]
    pop:NXM_NX_REG0[]
     -> NXM_NX_REG0[] is now 0xc0a8650a
    set_field:00:00:00:00:00:00->eth_dst
    resubmit(,66)
    66. reg0=0xc0a8650a,reg15=0x3,metadata=0x6, priority 100
            set_field:02:ac:10:ff:00:13->eth_dst
    pop:NXM_NX_REG0[]
     -> NXM_NX_REG0[] is now 0xc0a8650a
```

可以看到`66`规则将目标MAC设置为`02:ac:10:ff:00:13`:

```
[root@node2 ovs]# ovs-ofctl dump-flows br-int | grep table=66
table=66, n_packets=0, n_bytes=0, idle_age=8580, priority=100,reg0=0xc0a8640b,reg15=0x1,metadata=0x6 actions=mod_dl_dst:02:ac:10:ff:00:22
table=66, n_packets=18, n_bytes=1764, idle_age=1085, priority=100,reg0=0xc0a8640a,reg15=0x1,metadata=0x6 actions=mod_dl_dst:02:ac:10:ff:00:11
table=66, n_packets=33, n_bytes=3234, idle_age=1283, priority=100,reg0=0xc0a8650a,reg15=0x3,metadata=0x6 actions=mod_dl_dst:02:ac:10:ff:00:13
table=66, n_packets=0, n_bytes=0, idle_age=8580, priority=100,reg0=0,reg1=0,reg2=0,reg3=0,reg15=0x3,metadata=0x6 actions=mod_dl_dst:00:00:00:00:00:00
```

修改上面的第3条规则：

```
[root@node2 ovs]# ovs-ofctl del-flows br-int "table=66,reg0=0xc0a8650a,reg15=0x3,metadata=0x6"
[root@node2 ovs]# ovs-ofctl add-flow br-int "table=66,reg0=0xc0a8650a,reg15=0x3,metadata=0x6,actions=mod_dl_dst:02:ac:10:ff:00:33"
```

问题解决。

## Refs

* [The OVN Load Balancer](http://blog.spinhirne.com/2016/09/the-ovn-load-balancer.html)