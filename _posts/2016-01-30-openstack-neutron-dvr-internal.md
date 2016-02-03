---
layout: post
title: openstack neutron DVR internal
date: 2016-01-30 08:00:30
categories: Openstack
tags: neutron
excerpt: openstack neutron DVR internal
---

# create network

```
[root@controller ~]# neutron net-create ext-net --router:external \
>   --provider:physical_network external --provider:network_type flat
Created a new network:
+---------------------------+--------------------------------------+
| Field                     | Value                                |
+---------------------------+--------------------------------------+
| admin_state_up            | True                                 |
| id                        | 2f00f5f1-ad6f-4113-b550-df9f33afae49 |
| mtu                       | 0                                    |
| name                      | ext-net                              |
| provider:network_type     | flat                                 |
| provider:physical_network | external                             |
| provider:segmentation_id  |                                      |
| router:external           | True                                 |
| shared                    | False                                |
| status                    | ACTIVE                               |
| subnets                   |                                      |
| tenant_id                 | 0b48811a7e614b1ba752a6544d4eba02     |
+---------------------------+--------------------------------------+


[root@controller ~]# neutron subnet-create ext-net 203.0.113.0/24 --allocation-pool \
>   start=203.0.113.10,end=203.0.113.20 --disable-dhcp \
>   --gateway 203.0.113.1
Created a new subnet:
+-------------------+--------------------------------------------------+
| Field             | Value                                            |
+-------------------+--------------------------------------------------+
| allocation_pools  | {"start": "203.0.113.10", "end": "203.0.113.20"} |
| cidr              | 203.0.113.0/24                                   |
| dns_nameservers   |                                                  |
| enable_dhcp       | False                                            |
| gateway_ip        | 203.0.113.1                                      |
| host_routes       |                                                  |
| id                | 6a9c7495-e163-4dcc-ab8a-05ad81975c6b             |
| ip_version        | 4                                                |
| ipv6_address_mode |                                                  |
| ipv6_ra_mode      |                                                  |
| name              |                                                  |
| network_id        | 2f00f5f1-ad6f-4113-b550-df9f33afae49             |
| subnetpool_id     |                                                  |
| tenant_id         | 0b48811a7e614b1ba752a6544d4eba02                 |
+-------------------+--------------------------------------------------+


[root@controller ~]# openstack project show demo
+-------------+----------------------------------+
| Field       | Value                            |
+-------------+----------------------------------+
| description | Demo Project                     |
| domain_id   | default                          |
| enabled     | True                             |
| id          | 54dda1538bf64ba986f8672884326761 |
| is_domain   | False                            |
| name        | demo                             |
| parent_id   | None                             |
+-------------+----------------------------------+

[root@controller ~]# neutron net-create demo-net --tenant-id 54dda1538bf64ba986f8672884326761 \
>   --provider:network_type vxlan
Created a new network:
+---------------------------+--------------------------------------+
| Field                     | Value                                |
+---------------------------+--------------------------------------+
| admin_state_up            | True                                 |
| id                        | 728d4816-d2ea-4f7d-b005-13a1437eb6d6 |
| mtu                       | 0                                    |
| name                      | demo-net                             |
| provider:network_type     | vxlan                                |
| provider:physical_network |                                      |
| provider:segmentation_id  | 92                                   |
| router:external           | False                                |
| shared                    | False                                |
| status                    | ACTIVE                               |
| subnets                   |                                      |
| tenant_id                 | 54dda1538bf64ba986f8672884326761     |
+---------------------------+--------------------------------------+

[root@controller ~]# neutron subnet-create demo-net --name demo-subnet --gateway 192.168.10.1 \
>   192.168.10.0/24
Created a new subnet:
+-------------------+----------------------------------------------------+
| Field             | Value                                              |
+-------------------+----------------------------------------------------+
| allocation_pools  | {"start": "192.168.10.2", "end": "192.168.10.254"} |
| cidr              | 192.168.10.0/24                                    |
| dns_nameservers   |                                                    |
| enable_dhcp       | True                                               |
| gateway_ip        | 192.168.10.1                                       |
| host_routes       |                                                    |
| id                | 4d758887-c119-4355-bdb9-d76140dc2c0c               |
| ip_version        | 4                                                  |
| ipv6_address_mode |                                                    |
| ipv6_ra_mode      |                                                    |
| name              | demo-subnet                                        |
| network_id        | 728d4816-d2ea-4f7d-b005-13a1437eb6d6               |
| subnetpool_id     |                                                    |
| tenant_id         | 0b48811a7e614b1ba752a6544d4eba02                   |
+-------------------+----------------------------------------------------+


[root@controller ~]# neutron router-create demo-router
Created a new router:
+-----------------------+--------------------------------------+
| Field                 | Value                                |
+-----------------------+--------------------------------------+
| admin_state_up        | True                                 |
| distributed           | True                                 |
| external_gateway_info |                                      |
| ha                    | False                                |
| id                    | dd6481d3-c3c6-4cf8-877c-877c94b2edd3 |
| name                  | demo-router                          |
| routes                |                                      |
| status                | ACTIVE                               |
| tenant_id             | 0b48811a7e614b1ba752a6544d4eba02     |
+-----------------------+--------------------------------------+

[root@controller ~]# neutron router-interface-add demo-router demo-subnet
Added interface e97ad7a4-643e-4460-b620-924831c65614 to router demo-router.

[root@network ~]# ovs-vsctl show
899935c8-0797-47c0-9290-8b718396519b
    Bridge br-int
        fail_mode: secure
        Port br-int
            Interface br-int
                type: internal
        Port int-br-ex
            Interface int-br-ex
                type: patch
                options: {peer=phy-br-ex}
        Port "qr-e97ad7a4-64"
            tag: 1
            Interface "qr-e97ad7a4-64"
                type: internal
        Port patch-tun
            Interface patch-tun
                type: patch
                options: {peer=patch-int}
        Port "tap9b7dc0f3-f1"
            tag: 1
            Interface "tap9b7dc0f3-f1"
                type: internal
    Bridge br-tun
        fail_mode: secure
        Port br-tun
            Interface br-tun
                type: internal
        Port patch-int
            Interface patch-int
                type: patch
                options: {peer=patch-tun}
    Bridge br-ex
        Port phy-br-ex
            Interface phy-br-ex
                type: patch
                options: {peer=int-br-ex}
        Port br-ex
            Interface br-ex
                type: internal
        Port "eth2"
            Interface "eth2"
    ovs_version: "2.4.0"

[root@network ~]# ip netns
qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3
qdhcp-728d4816-d2ea-4f7d-b005-13a1437eb6d6
[root@network ~]# ip netns exec qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN 
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
22: qr-e97ad7a4-64: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN 
    link/ether fa:16:3e:ec:a7:5e brd ff:ff:ff:ff:ff:ff
    inet 192.168.10.1/24 brd 192.168.10.255 scope global qr-e97ad7a4-64
       valid_lft forever preferred_lft forever
    inet6 fe80::f816:3eff:feec:a75e/64 scope link 
       valid_lft forever preferred_lft forever

[root@controller ~]# neutron router-gateway-set demo-router ext-net
Set gateway for router demo-router

[root@network ~]# ip netns
snat-dd6481d3-c3c6-4cf8-877c-877c94b2edd3
qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3
qdhcp-728d4816-d2ea-4f7d-b005-13a1437eb6d6
[root@network ~]# ip netns exec snat-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN 
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
23: sg-f69e692f-93: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN 
    link/ether fa:16:3e:78:a9:ba brd ff:ff:ff:ff:ff:ff
    inet 192.168.10.3/24 brd 192.168.10.255 scope global sg-f69e692f-93
       valid_lft forever preferred_lft forever
    inet6 fe80::f816:3eff:fe78:a9ba/64 scope link 
       valid_lft forever preferred_lft forever
24: qg-2d0bd3e5-d1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN 
    link/ether fa:16:3e:85:92:62 brd ff:ff:ff:ff:ff:ff
    inet 203.0.113.10/24 brd 203.0.113.255 scope global qg-2d0bd3e5-d1
       valid_lft forever preferred_lft forever
    inet6 fe80::f816:3eff:fe85:9262/64 scope link 
       valid_lft forever preferred_lft forever

[root@network ~]# ovs-vsctl show
899935c8-0797-47c0-9290-8b718396519b
    Bridge br-int
        fail_mode: secure
        Port br-int
            Interface br-int
                type: internal
        Port int-br-ex
            Interface int-br-ex
                type: patch
                options: {peer=phy-br-ex}
        Port "sg-f69e692f-93"
            tag: 1
            Interface "sg-f69e692f-93"
                type: internal
        Port "qr-e97ad7a4-64"
            tag: 1
            Interface "qr-e97ad7a4-64"
                type: internal
        Port patch-tun
            Interface patch-tun
                type: patch
                options: {peer=patch-int}
        Port "tap9b7dc0f3-f1"
            tag: 1
            Interface "tap9b7dc0f3-f1"
                type: internal
    Bridge br-tun
        fail_mode: secure
        Port br-tun
            Interface br-tun
                type: internal
        Port patch-int
            Interface patch-int
                type: patch
                options: {peer=patch-tun}
    Bridge br-ex
        Port "qg-2d0bd3e5-d1"
            Interface "qg-2d0bd3e5-d1"
                type: internal
        Port phy-br-ex
            Interface phy-br-ex
                type: patch
                options: {peer=int-br-ex}
        Port br-ex
            Interface br-ex
                type: internal
        Port "eth2"
            Interface "eth2"
    ovs_version: "2.4.0"
```

# launch an instance

```
[root@controller ~]# nova image-list
+--------------------------------------+--------+--------+--------+
| ID                                   | Name   | Status | Server |
+--------------------------------------+--------+--------+--------+
| e8289eeb-f1f7-48b9-8ca0-835e7d6eed8a | cirros | ACTIVE |        |
+--------------------------------------+--------+--------+--------+

[root@controller ~]# nova network-list
+--------------------------------------+----------+------+
| ID                                   | Label    | Cidr |
+--------------------------------------+----------+------+
| 728d4816-d2ea-4f7d-b005-13a1437eb6d6 | demo-net | -    |
| 2f00f5f1-ad6f-4113-b550-df9f33afae49 | ext-net  | -    |
+--------------------------------------+----------+------+

[root@controller ~]# nova boot --flavor  1 --image e8289eeb-f1f7-48b9-8ca0-835e7d6eed8a --nic net-id=728d4816-d2ea-4f7d-b005-13a1437eb6d6 demo-vm1
+--------------------------------------+-----------------------------------------------+
| Property                             | Value                                         |
+--------------------------------------+-----------------------------------------------+
| OS-DCF:diskConfig                    | MANUAL                                        |
| OS-EXT-AZ:availability_zone          |                                               |
| OS-EXT-SRV-ATTR:host                 | -                                             |
| OS-EXT-SRV-ATTR:hypervisor_hostname  | -                                             |
| OS-EXT-SRV-ATTR:instance_name        | instance-00000002                             |
| OS-EXT-STS:power_state               | 0                                             |
| OS-EXT-STS:task_state                | scheduling                                    |
| OS-EXT-STS:vm_state                  | building                                      |
| OS-SRV-USG:launched_at               | -                                             |
| OS-SRV-USG:terminated_at             | -                                             |
| accessIPv4                           |                                               |
| accessIPv6                           |                                               |
| adminPass                            | t3jc8cWjuMT5                                  |
| config_drive                         |                                               |
| created                              | 2016-02-03T03:12:24Z                          |
| flavor                               | m1.tiny (1)                                   |
| hostId                               |                                               |
| id                                   | 5dd45381-ee24-4d37-b8e0-2a985e744ae2          |
| image                                | cirros (e8289eeb-f1f7-48b9-8ca0-835e7d6eed8a) |
| key_name                             | -                                             |
| metadata                             | {}                                            |
| name                                 | demo-vm1                                      |
| os-extended-volumes:volumes_attached | []                                            |
| progress                             | 0                                             |
| security_groups                      | default                                       |
| status                               | BUILD                                         |
| tenant_id                            | 0b48811a7e614b1ba752a6544d4eba02              |
| updated                              | 2016-02-03T03:12:24Z                          |
| user_id                              | f0e070d07e9e4e3eb44204eda13f3de5              |
+--------------------------------------+-----------------------------------------------+

[root@controller ~]# nova list
+--------------------------------------+----------+--------+------------+-------------+-----------------------+
| ID                                   | Name     | Status | Task State | Power State | Networks              |
+--------------------------------------+----------+--------+------------+-------------+-----------------------+
| 5dd45381-ee24-4d37-b8e0-2a985e744ae2 | demo-vm1 | ACTIVE | -          | Running     | demo-net=192.168.10.4 |
+--------------------------------------+----------+--------+------------+-------------+-----------------------+
```


# compute node's network

## OVS ports

```
[root@compute1 ~]# ovs-vsctl show
0ebd643a-fbf9-482c-8488-9292ad146e4a
    Bridge br-ex
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
        Port "qvofa8befcd-cd"         #### VM tap device port
            tag: 1
            Interface "qvofa8befcd-cd"
        Port patch-tun
            Interface patch-tun
                type: patch
                options: {peer=patch-int}
        Port "qr-e97ad7a4-64"        #### qr-XXX device port
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
[root@compute1 ~]# ip netns
qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3
[root@compute1 ~]# ip netns exec qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN 
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
14: qr-e97ad7a4-64: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN 
    link/ether fa:16:3e:ec:a7:5e brd ff:ff:ff:ff:ff:ff
    inet 192.168.10.1/24 brd 192.168.10.255 scope global qr-e97ad7a4-64
       valid_lft forever preferred_lft forever
    inet6 fe80::f816:3eff:feec:a75e/64 scope link 
       valid_lft forever preferred_lft forever

[root@compute1 ~]# ip netns exec qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 ip route show
192.168.10.0/24 dev qr-e97ad7a4-64  proto kernel  scope link  src 192.168.10.1

[root@compute1 ~]# ip netns exec qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 ip rule show
0:      from all lookup local 
32766:  from all lookup main 
32767:  from all lookup default 
3232238081:     from 192.168.10.1/24 lookup 3232238081

[root@compute1 ~]# ip netns exec qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 ip route show table 3232238081
default via 192.168.10.3 dev qr-e97ad7a4-64
```

# network node's network

## OVS ports

```
[root@network ~]# ovs-vsctl show
899935c8-0797-47c0-9290-8b718396519b
    Bridge br-int
        fail_mode: secure
        Port br-int
            Interface br-int
                type: internal
        Port int-br-ex
            Interface int-br-ex
                type: patch
                options: {peer=phy-br-ex}
        Port "sg-f69e692f-93"     #### sg-YYY device port
            tag: 1
            Interface "sg-f69e692f-93"
                type: internal
        Port "qr-e97ad7a4-64"     #### qr-XXX device port
            tag: 1
            Interface "qr-e97ad7a4-64"
                type: internal
        Port patch-tun
            Interface patch-tun
                type: patch
                options: {peer=patch-int}
        Port "tap9b7dc0f3-f1"	   #### qdhcp tap device port
            tag: 1
            Interface "tap9b7dc0f3-f1"
                type: internal
    Bridge br-tun
        fail_mode: secure
        Port br-tun
            Interface br-tun
                type: internal
        Port patch-int
            Interface patch-int
                type: patch
                options: {peer=patch-tun}
        Port "vxlan-ac122a34"
            Interface "vxlan-ac122a34"
                type: vxlan
                options: {df_default="true", in_key=flow, local_ip="172.18.42.51", out_key=flow, remote_ip="172.18.42.52"}
    Bridge br-ex
        Port "qg-2d0bd3e5-d1"
            Interface "qg-2d0bd3e5-d1"
                type: internal
        Port phy-br-ex
            Interface phy-br-ex
                type: patch
                options: {peer=int-br-ex}
        Port br-ex
            Interface br-ex
                type: internal
        Port "eth2"
            Interface "eth2"
    ovs_version: "2.4.0"
```

## qr-XXX netnamespace

```
[root@network ~]# ip netns
snat-dd6481d3-c3c6-4cf8-877c-877c94b2edd3
qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3
qdhcp-728d4816-d2ea-4f7d-b005-13a1437eb6d6
[root@network ~]# ip netns exec qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN 
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
22: qr-e97ad7a4-64: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN 
    link/ether fa:16:3e:ec:a7:5e brd ff:ff:ff:ff:ff:ff
    inet 192.168.10.1/24 brd 192.168.10.255 scope global qr-e97ad7a4-64
       valid_lft forever preferred_lft forever
    inet6 fe80::f816:3eff:feec:a75e/64 scope link 
       valid_lft forever preferred_lft forever

[root@network ~]# ip netns exec qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 ip route show
192.168.10.0/24 dev qr-e97ad7a4-64  proto kernel  scope link  src 192.168.10.1 

[root@network ~]# ip netns exec qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 ip rule
0:      from all lookup local 
32766:  from all lookup main 
32767:  from all lookup default 
3232238081:     from 192.168.10.1/24 lookup 3232238081 

[root@network ~]# ip netns exec qrouter-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 ip route show table 3232238081
default via 192.168.10.3 dev qr-e97ad7a4-64 
```

可以看到，在network node和compute node都有一个qr-XXX namespace完全一样，包括route, interface的MAC/IP都完全一样。那么问题来了，当VM访问内部网关(192.168.10.1)时，到底与谁通信？

实际上，br-tun上有如下规则：

```
[root@compute1 ~]# ovs-ofctl dump-flows br-tun|grep 192.168.10.1
 cookie=0x80b4bbb92a1bc489, duration=11707.837s, table=1, n_packets=6, n_bytes=252, idle_age=528, priority=3,arp,dl_vlan=1,arp_tpa=192.168.10.1 actions=drop
```

所以，compute node上的VM只会与本地的qr-XXX通信。

除此之外，network node还多了一个snat-XXX的netnamespace.

## snat-XXX netnamespace

```
[root@network ~]# ip netns exec snat-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 ip a                 
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN 
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
23: sg-f69e692f-93: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN 
    link/ether fa:16:3e:78:a9:ba brd ff:ff:ff:ff:ff:ff
    inet 192.168.10.3/24 brd 192.168.10.255 scope global sg-f69e692f-93
       valid_lft forever preferred_lft forever
    inet6 fe80::f816:3eff:fe78:a9ba/64 scope link 
       valid_lft forever preferred_lft forever
24: qg-2d0bd3e5-d1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN 
    link/ether fa:16:3e:85:92:62 brd ff:ff:ff:ff:ff:ff
    inet 203.0.113.10/24 brd 203.0.113.255 scope global qg-2d0bd3e5-d1
       valid_lft forever preferred_lft forever
    inet6 fe80::f816:3eff:fe85:9262/64 scope link 
       valid_lft forever preferred_lft forever

[root@network ~]# ip netns exec snat-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 ip route show
default via 203.0.113.1 dev qg-2d0bd3e5-d1 
192.168.10.0/24 dev sg-f69e692f-93  proto kernel  scope link  src 192.168.10.3 
203.0.113.0/24 dev qg-2d0bd3e5-d1  proto kernel  scope link  src 203.0.113.10 

[root@network ~]# ip netns exec snat-dd6481d3-c3c6-4cf8-877c-877c94b2edd3 iptables -t nat  -S
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
-A neutron-l3-agent-POSTROUTING ! -i qg-2d0bd3e5-d1 ! -o qg-2d0bd3e5-d1 -m conntrack ! --ctstate DNAT -j ACCEPT
-A neutron-l3-agent-snat -o qg-2d0bd3e5-d1 -j SNAT --to-source 203.0.113.10
-A neutron-l3-agent-snat -m mark ! --mark 0x2/0xffff -m conntrack --ctstate DNAT -j SNAT --to-source 203.0.113.10
-A neutron-postrouting-bottom -m comment --comment "Perform source NAT on outgoing traffic." -j neutron-l3-agent-snat
```

# 数据流分析

## North/south for instances with a fixed IP address

考虑demo-vm1(192.168.10.4)访问外部203.0.113.250:

数据从compute node的br-int转给本地的qr-e97ad7a4-64（192.168.10.1)，qr-e97ad7a4-64查找路由，再传给network node的sg-f69e692f-93(192.168.10.3)，再由sg-f69e692f-93 SNAT出去，详细过程如下：

![](/assets/2016-02-03-openstack-neutron-dvr-internal-1.png)

![](/assets/2016-02-03-openstack-neutron-dvr-internal-2.png)

从这里也可以看到，DVR的SNAT还是集中式的。主要是为了节省external network address。

# 相关资料

* [Distributed Virtual Routing – SNAT](http://assafmuller.com/2015/04/15/distributed-virtual-routing-snat/)
* [理解 OpenStack 高可用（HA）（3）：Neutron 分布式虚拟路由（Neutron Distributed Virtual Routing）](http://www.cnblogs.com/sammyliu/p/4713562.html)
* [Architectural Overview of Distributed Virtual Routers in OpenStack Neutron](https://www.openstack.org/assets/presentation-media/Openstack-kilo-summit-DVR-Architecture-20141030-Master-submitted-to-openstack.pdf)
