---
layout: post
title: openstack neutron DVR internal with floating IP
date: 2016-01-31 17:00:30
categories: Openstack
tags: neutron
excerpt: openstack neutron DVR internal with floating IP
---

# Add floating IP for VM 

```
[root@controller ~]# neutron l3-agent-list-hosting-router demo-router
+--------------------------------------+----------+----------------+-------+----------+
| id                                   | host     | admin_state_up | alive | ha_state |
+--------------------------------------+----------+----------------+-------+----------+
| 5dc96667-7f5f-4e3d-85aa-22759c7a4c5a | compute1 | True           | :-)   |          |
| 77c78491-d4e6-491f-8ac6-e6348fbc87c9 | network  | True           | :-)   |          |
+--------------------------------------+----------+----------------+-------+----------+
```

Create a floating IP address on the external network

```
[root@controller ~]# neutron floatingip-create ext-net
Created a new floatingip:
+---------------------+--------------------------------------+
| Field               | Value                                |
+---------------------+--------------------------------------+
| fixed_ip_address    |                                      |
| floating_ip_address | 203.0.113.11                         |
| floating_network_id | 2f00f5f1-ad6f-4113-b550-df9f33afae49 |
| id                  | 8767bb49-dad7-4118-b8cc-309225489242 |
| port_id             |                                      |
| router_id           |                                      |
| status              | DOWN                                 |
| tenant_id           | 0b48811a7e614b1ba752a6544d4eba02     |
+---------------------+--------------------------------------+
```

Associate the floating IP address with the instance

```
[root@controller ~]# nova floating-ip-associate demo-vm1 203.0.113.11

[root@controller ~]# nova list
+--------------------------------------+----------+--------+------------+-------------+-------------------------------------+
| ID                                   | Name     | Status | Task State | Power State | Networks                            |
+--------------------------------------+----------+--------+------------+-------------+-------------------------------------+
| 5dd45381-ee24-4d37-b8e0-2a985e744ae2 | demo-vm1 | ACTIVE | -          | Running     | demo-net=192.168.10.4, 203.0.113.11 |
+--------------------------------------+----------+--------+------------+-------------+-------------------------------------+
```

# compute node's network

## OVS ports

```
[root@compute1 ~]# ovs-vsctl show
0ebd643a-fbf9-482c-8488-9292ad146e4a
    Bridge br-ex
        Port "fg-50b88d31-14"
            Interface "fg-50b88d31-14"
                type: internal
        Port "eth2"
            Interface "eth2"
        Port phy-br-ex
            Interface phy-br-ex
                type: patch
                options: {peer=int-br-ex}
        Port br-ex
            Interface br-ex
                type: internal
    Bridge br-int
        fail_mode: secure
        Port int-br-ex
            Interface int-br-ex
                type: patch
                options: {peer=phy-br-ex}
        Port br-int
            Interface br-int
                type: internal
        Port "qvofa8befcd-cd"
            tag: 1
            Interface "qvofa8befcd-cd"
        Port patch-tun
            Interface patch-tun
                type: patch
                options: {peer=patch-int}
        Port "qr-e97ad7a4-64"
            tag: 1
            Interface "qr-e97ad7a4-64"
                type: internal
    Bridge br-tun
        fail_mode: secure
        Port br-tun
            Interface br-tun
                type: internal
        Port "vxlan-ac122a33"
            Interface "vxlan-ac122a33"
                type: vxlan
                options: {df_default="true", in_key=flow, local_ip="172.18.42.52", out_key=flow, remote_ip="172.18.42.51"}
        Port patch-int
            Interface patch-int
                type: patch
                options: {peer=patch-tun}
    ovs_version: "2.4.0"
```
## qr-XXX netnamespace

```
[root@compute1 ~]# ip netns exec qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 ip a   
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN 
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
2: rfp-dd6481d3-c: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP qlen 1000
    link/ether 82:de:1b:13:37:d8 brd ff:ff:ff:ff:ff:ff
    inet 169.254.31.28/31 scope global rfp-dd6481d3-c
       valid_lft forever preferred_lft forever
    inet 203.0.113.11/32 brd 203.0.113.11 scope global rfp-dd6481d3-c
       valid_lft forever preferred_lft forever
    inet6 fe80::80de:1bff:fe13:37d8/64 scope link 
       valid_lft forever preferred_lft forever
14: qr-e97ad7a4-64: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN 
    link/ether fa:16:3e:ec:a7:5e brd ff:ff:ff:ff:ff:ff
    inet 192.168.10.1/24 brd 192.168.10.255 scope global qr-e97ad7a4-64
       valid_lft forever preferred_lft forever
    inet6 fe80::f816:3eff:feec:a75e/64 scope link 
       valid_lft forever preferred_lft forever

[root@compute1 ~]# ip netns exec qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 ip rule
0:      from all lookup local 
32766:  from all lookup main 
32767:  from all lookup default 
57481:  from 192.168.10.4 lookup 16 
3232238081:     from 192.168.10.1/24 lookup 3232238081 

[root@compute1 ~]# ip netns exec qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 ip route list table 16
default via 169.254.31.29 dev rfp-dd6481d3-c 

[root@compute1 ~]# ip netns exec qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 iptables -t nat -S    
-P PREROUTING ACCEPT
-P INPUT ACCEPT
-P OUTPUT ACCEPT
-P POSTROUTING ACCEPT
-N neutron-l3-agent-OUTPUT
-N neutron-l3-agent-POSTROUTING
-N neutron-l3-agent-PREROUTING
-N neutron-l3-agent-float-snat
-N neutron-l3-agent-snat
-N neutron-postrouting-bottom
-A PREROUTING -j neutron-l3-agent-PREROUTING
-A OUTPUT -j neutron-l3-agent-OUTPUT
-A POSTROUTING -j neutron-l3-agent-POSTROUTING
-A POSTROUTING -j neutron-postrouting-bottom
-A neutron-l3-agent-OUTPUT -d 203.0.113.11/32 -j DNAT --to-destination 192.168.10.4
-A neutron-l3-agent-POSTROUTING ! -i rfp-dd6481d3-c ! -o rfp-dd6481d3-c -m conntrack ! --ctstate DNAT -j ACCEPT
-A neutron-l3-agent-PREROUTING -d 169.254.169.254/32 -i qr-+ -p tcp -m tcp --dport 80 -j REDIRECT --to-ports 9697
-A neutron-l3-agent-PREROUTING -d 203.0.113.11/32 -j DNAT --to-destination 192.168.10.4
-A neutron-l3-agent-float-snat -s 192.168.10.4/32 -j SNAT --to-source 203.0.113.11
-A neutron-l3-agent-snat -j neutron-l3-agent-float-snat
-A neutron-postrouting-bottom -m comment --comment "Perform source NAT on outgoing traffic." -j neutron-l3-agent-snat
```

可以看到compute node的qroute增加了一条rule和route table。即所有来自demo-vm1(192.168.10.4)都会从rfp-dd6481d3-c(169.254.31.28)发往下一跳169.254.31.29，并且在发送之前，完成SNAT转换:

```
-A neutron-l3-agent-float-snat -s 192.168.10.4/32 -j SNAT --to-source 203.0.113.11
```

实际上，rfp-dd6481d3-c是一个veth设备，与fip netnamespace的fpr-dd6481d3-c(169.254.31.29)相连。

## fip netnamespace

```
[root@compute1 ~]# ip netns
fip-2f00f5f1-ad6f-4113-b550-df9f33afae49
qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3

[root@compute1 ~]# ip netns exec fip-2f00f5f1-ad6f-4113-b550-df9f33afae49 ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN 
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
2: fpr-dd6481d3-c: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP qlen 1000
    link/ether e6:b2:b3:96:c9:6c brd ff:ff:ff:ff:ff:ff
    inet 169.254.31.29/31 scope global fpr-dd6481d3-c
       valid_lft forever preferred_lft forever
    inet6 fe80::e4b2:b3ff:fe96:c96c/64 scope link 
       valid_lft forever preferred_lft forever
15: fg-50b88d31-14: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN 
    link/ether fa:16:3e:a5:23:8f brd ff:ff:ff:ff:ff:ff
    inet 203.0.113.12/24 brd 203.0.113.255 scope global fg-50b88d31-14
       valid_lft forever preferred_lft forever
    inet6 fe80::f816:3eff:fea5:238f/64 scope link 
       valid_lft forever preferred_lft forever

[root@compute1 ~]# ip netns exec fip-2f00f5f1-ad6f-4113-b550-df9f33afae49 ip route show
default via 203.0.113.1 dev fg-50b88d31-14 
169.254.31.28/31 dev fpr-dd6481d3-c  proto kernel  scope link  src 169.254.31.29 
203.0.113.0/24 dev fg-50b88d31-14  proto kernel  scope link  src 203.0.113.12 
203.0.113.11 via 169.254.31.28 dev fpr-dd6481d3-c 
```

当数据包(发往203.0.113.x)从VM传到fpr-dd6481d3-c(169.254.31.29)后，匹配第3条路由规则，从fg-50b88d31-14送出，fg-50b88d31-14是br-ex的port。

# North/south for instances with a floating IP address

## VM -> external

整体流程如下：

![](/assets/2016-02-03-openstack-neutron-dvr-internal-fip-1.png)

```
$ traceroute 203.0.113.250
traceroute to 203.0.113.250 (203.0.113.250), 30 hops max, 46 byte packets
 1  192.168.10.1 (192.168.10.1)  0.164 ms  0.207 ms  0.209 ms
 2  169.254.31.29 (169.254.31.29)  0.533 ms  0.445 ms  0.356 ms
 3  203.0.113.250 (203.0.113.250)  0.415 ms  0.374 ms  0.381 ms
```

## external -> VM

```
[root@controller ~]# nova secgroup-add-rule default icmp -1 -1 0.0.0.0/0
+-------------+-----------+---------+-----------+--------------+
| IP Protocol | From Port | To Port | IP Range  | Source Group |
+-------------+-----------+---------+-----------+--------------+
| icmp        | -1        | -1      | 0.0.0.0/0 |              |
+-------------+-----------+---------+-----------+--------------+
[root@controller ~]# nova secgroup-add-rule default tcp 22 22 0.0.0.0/0
+-------------+-----------+---------+-----------+--------------+
| IP Protocol | From Port | To Port | IP Range  | Source Group |
+-------------+-----------+---------+-----------+--------------+
| tcp         | 22        | 22      | 0.0.0.0/0 |              |
+-------------+-----------+---------+-----------+--------------+

[root@external ~]# ssh cirros@203.0.113.11
cirros@203.0.113.11's password: 
$ ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 16436 qdisc noqueue 
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1450 qdisc pfifo_fast qlen 1000
    link/ether fa:16:3e:94:5d:0c brd ff:ff:ff:ff:ff:ff
    inet 192.168.10.4/24 brd 192.168.10.255 scope global eth0
    inet6 fe80::f816:3eff:fe94:5d0c/64 scope link 
       valid_lft forever preferred_lft forever
```

当数据包到达fg-50b88d31-14(203.0.113.12/24)时，匹配最后一条路由规则:

```
203.0.113.11 via 169.254.31.28 dev fpr-dd6481d3-c 
```

传送到fip netnamespace的rfp-dd6481d3-c，然后匹配DNAT规则:

```
-A neutron-l3-agent-PREROUTING -d 203.0.113.11/32 -j DNAT --to-destination 192.168.10.4
```

然后匹配qr netnamespace的下面的路由规则:

```
[root@compute1 ~]# ip netns exec qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 ip route show     
169.254.31.28/31 dev rfp-dd6481d3-c  proto kernel  scope link  src 169.254.31.28 
192.168.10.0/24 dev qr-e97ad7a4-64  proto kernel  scope link  src 192.168.10.1 
```
然后从qr-e97ad7a4-64送出。最后经过br-int送给VM。

# DVR future

* VPNaaS support for DVR
* Full migration support for DVR routers.
* HA for Service Node
* IPv6 Support
* VLAN Support
* L3 Agent Refactor
* Distributed DHCP
* *** Performance tuning ***
* *** Distributed SNAT ***

# 总结

DVR实现的网络比较复杂，VM与external之间的链路也很长，大量的veth设备带来的网络性能和延迟都会是一个问题。

# 相关资料

* [Scenario: High Availability using Distributed Virtual Routing (DVR)](http://docs.openstack.org/liberty/networking-guide/scenario-dvr-ovs.html)
* [install guide: Add the Networking service](http://docs.openstack.org/liberty/install-guide-rdo/neutron.html)
* [Networking service command-line client](http://docs.openstack.org/cli-reference/neutron.html)
* [Distributed Virtual Routing – Floating IPs](http://assafmuller.com/2015/04/15/distributed-virtual-routing-floating-ips/)


