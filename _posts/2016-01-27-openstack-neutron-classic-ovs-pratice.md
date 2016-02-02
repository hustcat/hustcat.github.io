---
layout: post
title: openstack neutron classic ovs practice
date: 2016-01-27 18:00:30
categories: Openstack
tags: neutron
excerpt: openstack neutron classic ovs practice
---

# environment

```
172.17.42.20 controller
172.17.42.21 network
172.17.42.22 compute1
172.17.42.23 compute2
```

# create the external network

* Create the network

```sh
# neutron net-create ext-net --shared --router:external=True
Created a new network:
+---------------------------+--------------------------------------+
| Field                     | Value                                |
+---------------------------+--------------------------------------+
| admin_state_up            | True                                 |
| id                        | 43caec7c-8749-4cab-be5f-b3c4f831563f |
| name                      | ext-net                              |
| provider:network_type     | gre                                  |
| provider:physical_network |                                      |
| provider:segmentation_id  | 2                                    |
| router:external           | True                                 |
| shared                    | True                                 |
| status                    | ACTIVE                               |
| subnets                   |                                      |
| tenant_id                 | 527328d60127441285933c1011c00869     |
+---------------------------+--------------------------------------+
```

* Create the subnet

```sh
# neutron subnet-create ext-net --name ext-subnet \
>   --allocation-pool start=203.0.113.101,end=203.0.113.200 \
>   --disable-dhcp --gateway 203.0.113.1 203.0.113.0/24
Created a new subnet:
+------------------+----------------------------------------------------+
| Field            | Value                                              |
+------------------+----------------------------------------------------+
| allocation_pools | {"start": "203.0.113.101", "end": "203.0.113.200"} |
| cidr             | 203.0.113.0/24                                     |
| dns_nameservers  |                                                    |
| enable_dhcp      | False                                              |
| gateway_ip       | 203.0.113.1                                        |
| host_routes      |                                                    |
| id               | 0f9dd86f-8440-4193-8ac1-f1a0e4cec815               |
| ip_version       | 4                                                  |
| name             | ext-subnet                                         |
| network_id       | 43caec7c-8749-4cab-be5f-b3c4f831563f               |
| tenant_id        | 527328d60127441285933c1011c00869                   |
+------------------+----------------------------------------------------+
```

# create the tenant network

* Create the network

```sh
# neutron net-create demo-net
Created a new network:
+---------------------------+--------------------------------------+
| Field                     | Value                                |
+---------------------------+--------------------------------------+
| admin_state_up            | True                                 |
| id                        | 162678a1-0fd8-433f-b52d-717ea6045a5a |
| name                      | demo-net                             |
| provider:network_type     | gre                                  |
| provider:physical_network |                                      |
| provider:segmentation_id  | 1                                    |
| shared                    | False                                |
| status                    | ACTIVE                               |
| subnets                   |                                      |
| tenant_id                 | 527328d60127441285933c1011c00869     |
+---------------------------+--------------------------------------+
```

* Create the subnet

```sh
# neutron subnet-create demo-net --name demo-subnet \
>   --gateway 192.168.1.1 192.168.1.0/24
Created a new subnet:
+------------------+--------------------------------------------------+
| Field            | Value                                            |
+------------------+--------------------------------------------------+
| allocation_pools | {"start": "192.168.1.2", "end": "192.168.1.254"} |
| cidr             | 192.168.1.0/24                                   |
| dns_nameservers  |                                                  |
| enable_dhcp      | True                                             |
| gateway_ip       | 192.168.1.1                                      |
| host_routes      |                                                  |
| id               | a6cc2ed0-6f89-4843-a517-9b33729abc4e             |
| ip_version       | 4                                                |
| name             | demo-subnet                                      |
| network_id       | 162678a1-0fd8-433f-b52d-717ea6045a5a             |
| tenant_id        | 527328d60127441285933c1011c00869                 |
+------------------+--------------------------------------------------+
```

#  create a router on the tenant network and attach the external and tenant networks to it

* Create the router

```sh
# neutron router-create demo-router
Created a new router:
+-----------------------+--------------------------------------+
| Field                 | Value                                |
+-----------------------+--------------------------------------+
| admin_state_up        | True                                 |
| external_gateway_info |                                      |
| id                    | 90cef600-27e2-48a4-b447-35291e1a68aa |
| name                  | demo-router                          |
| status                | ACTIVE                               |
| tenant_id             | 527328d60127441285933c1011c00869     |
+-----------------------+--------------------------------------+
```

* Attach the router to the demo tenant subnet:

```sh
# neutron router-interface-add demo-router demo-subnet
Added interface b13b21d8-972a-43e1-9ae0-4644e7d200dd to router demo-router.
```

* Attach the router to the external network by setting it as the gateway:

```sh
# neutron router-gateway-set demo-router ext-net
Set gateway for router demo-router
```

# launch an instance

* boot vm

```sh
# nova boot --flavor  1 --image d5e40bdf-4b89-49ef-b082-0af4763f3224 --nic net-id=162678a1-0fd8-433f-b52d-717ea6045a5a demo-vm1
+--------------------------------------+------------------------------------------------------------+
| Property                             | Value                                                      |
+--------------------------------------+------------------------------------------------------------+
| OS-DCF:diskConfig                    | MANUAL                                                     |
| OS-EXT-AZ:availability_zone          | nova                                                       |
| OS-EXT-SRV-ATTR:host                 | -                                                          |
| OS-EXT-SRV-ATTR:hypervisor_hostname  | -                                                          |
| OS-EXT-SRV-ATTR:instance_name        | instance-00000009                                          |
| OS-EXT-STS:power_state               | 0                                                          |
| OS-EXT-STS:task_state                | scheduling                                                 |
| OS-EXT-STS:vm_state                  | building                                                   |
| OS-SRV-USG:launched_at               | -                                                          |
| OS-SRV-USG:terminated_at             | -                                                          |
| accessIPv4                           |                                                            |
| accessIPv6                           |                                                            |
| adminPass                            | Xu5X4akAcEse                                               |
| config_drive                         |                                                            |
| created                              | 2016-01-27T07:54:58Z                                       |
| flavor                               | m1.tiny (1)                                                |
| hostId                               |                                                            |
| id                                   | a10146ef-eebb-45ab-abef-ba20f15e51d1                       |
| image                                | cirros-0.3.2-x86_64 (d5e40bdf-4b89-49ef-b082-0af4763f3224) |
| key_name                             | -                                                          |
| metadata                             | {}                                                         |
| name                                 | demo-vm1                                                   |
| os-extended-volumes:volumes_attached | []                                                         |
| progress                             | 0                                                          |
| security_groups                      | default                                                    |
| status                               | BUILD                                                      |
| tenant_id                            | 527328d60127441285933c1011c00869                           |
| updated                              | 2016-01-27T07:54:58Z                                       |
| user_id                              | fe624c6c89464ba1b61279858a42a6de                           |
+--------------------------------------+------------------------------------------------------------+
```

* Create a `floating IP address` on the ext-net external network

```sh
[root@controller ~]# neutron floatingip-create ext-net
Created a new floatingip:
+---------------------+--------------------------------------+
| Field               | Value                                |
+---------------------+--------------------------------------+
| fixed_ip_address    |                                      |
| floating_ip_address | 203.0.113.102                        |
| floating_network_id | 43caec7c-8749-4cab-be5f-b3c4f831563f |
| id                  | ff107a8d-0216-4a8c-8864-57fd125076e6 |
| port_id             |                                      |
| router_id           |                                      |
| status              | DOWN                                 |
| tenant_id           | 527328d60127441285933c1011c00869     |
+---------------------+--------------------------------------+
```

* Associate the floating IP address with instance

```sh
[root@controller ~]# nova floating-ip-associate demo-vm1 203.0.113.102
[root@controller ~]# nova list
+--------------------------------------+----------+--------+------------+-------------+-------------------------------------+
| ID                                   | Name     | Status | Task State | Power State | Networks                            |
+--------------------------------------+----------+--------+------------+-------------+-------------------------------------+
| a10146ef-eebb-45ab-abef-ba20f15e51d1 | demo-vm1 | ACTIVE | -          | Running     | demo-net=192.168.1.7, 203.0.113.102 |
+--------------------------------------+----------+--------+------------+-------------+-------------------------------------+
```

# Neutron L3 agent principle

L3 agent运行在network节点，负责路由（routing）、浮动 IP 分配（floatingip allocation）， 地址转换（SNAT/DNAT）和 Security Group 管理。L3 agent是neutron实现overlay network与external network互相通信的桥梁。更多介绍参考[这里](http://www.cnblogs.com/sammyliu/p/4636091.html)。

L3 Agent会为每个overlay network创建一个单独的network namespace，每个namespace命名为qrouter-<router-UUID>命名，比如对于上面创建的demo-router，则为qrouter-90cef600-27e2-48a4-b447-35291e1a68aa。这个network namespace有一个更专业的称呼——Virtual Router.

```sh
[root@network ~]# ip netns
qrouter-90cef600-27e2-48a4-b447-35291e1a68aa
qdhcp-162678a1-0fd8-433f-b52d-717ea6045a5a
```

网络节点如果不支持 Linux namespace 的话，只能运行一个 Virtual Router。也可以通过设置配置项 use_namespaces = True/False 来使用或者不使用 namespace。

* Virtual Router

Virtual Router会为连接的subnet创建一个virtual interface（qr-XXX）。每个interface的地址是该 subnet 的 gateway 地址，比如demo-net对应的192.168.1.1/24。这样，所有从overlay到external的数据包都会发到该interface。

同时，VR还有一个qg-YYY的interface，它的IP为所有的floating IP，其中第一个IP(203.0.113.101)不与具体的VM instance绑定，作为所有instance的gateway的public IP（更确切的说，是所有没有分配floating IP的实例）。

```sh
[root@network ~]# ip netns exec qrouter-90cef600-27e2-48a4-b447-35291e1a68aa ip a         
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN 
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
11: qr-b13b21d8-97: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN 
    link/ether fa:16:3e:e3:5a:1c brd ff:ff:ff:ff:ff:ff
    inet 192.168.1.1/24 brd 192.168.1.255 scope global qr-b13b21d8-97
       valid_lft forever preferred_lft forever
    inet6 fe80::f816:3eff:fee3:5a1c/64 scope link 
       valid_lft forever preferred_lft forever
12: qg-12fd2ded-6d: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN 
    link/ether fa:16:3e:98:d5:85 brd ff:ff:ff:ff:ff:ff
    inet 203.0.113.101/24 brd 203.0.113.255 scope global qg-12fd2ded-6d
       valid_lft forever preferred_lft forever
    inet 203.0.113.102/32 brd 203.0.113.102 scope global qg-12fd2ded-6d
       valid_lft forever preferred_lft forever
    inet6 fe80::f816:3eff:fe98:d585/64 scope link 
       valid_lft forever preferred_lft forever
```

内部路由规则：

```sh
[root@network ~]# ip netns exec qrouter-90cef600-27e2-48a4-b447-35291e1a68aa ip route show
default via 203.0.113.1 dev qg-12fd2ded-6d 
192.168.1.0/24 dev qr-b13b21d8-97  proto kernel  scope link  src 192.168.1.1 
203.0.113.0/24 dev qg-12fd2ded-6d  proto kernel  scope link  src 203.0.113.101 
```

* ovs interface on network node

```sh
[root@network ~]# ovs-vsctl show
ed9e1c43-bc19-4d00-85bb-d2787d0c9130
    Bridge br-int
        fail_mode: secure
        Port br-int
            Interface br-int
                type: internal
        Port patch-tun
            Interface patch-tun
                type: patch
                options: {peer=patch-int}
        Port "tap67d3187a-d7"
            tag: 1
            Interface "tap67d3187a-d7"
                type: internal
        Port "qr-b13b21d8-97"
            tag: 1
            Interface "qr-b13b21d8-97"
                type: internal
    Bridge br-tun
        Port br-tun
            Interface br-tun
                type: internal
        Port "gre-ac122a16"
            Interface "gre-ac122a16"
                type: gre
                options: {in_key=flow, local_ip="172.18.42.21", out_key=flow, remote_ip="172.18.42.22"}
        Port patch-int
            Interface patch-int
                type: patch
                options: {peer=patch-tun}
        Port "gre-ac122a17"
            Interface "gre-ac122a17"
                type: gre
                options: {in_key=flow, local_ip="172.18.42.21", out_key=flow, remote_ip="172.18.42.23"}
    Bridge br-ex
        Port "qg-12fd2ded-6d"
            Interface "qg-12fd2ded-6d"
                type: internal
        Port "eth2"
            Interface "eth2"
        Port br-ex
            Interface br-ex
                type: internal
    ovs_version: "2.1.3"
```

* floating IP ARP proxy

VM实例的floating IP并不是绑定在VM内部的interface上面，而是绑定在VR的public interface上面。这样，当external netowrk arp floating IP时，VR返回public interface的MAC，VR在这里充当了ARP proxy的角色。

更多详细介绍参考[Configuring Floating IP addresses for Networking in OpenStack Public and Private Clouds](https://www.mirantis.com/blog/configuring-floating-ip-addresses-networking-openstack-public-private-clouds/).

# Verify network connectivity

* packet flow

详细数据流分析可以参考[这里](http://docs.openstack.org/liberty/networking-guide/scenario-classic-ovs.html#packet-flow)和[这里](http://www.cnblogs.com/sammyliu/p/4204190.html)

* vm -> external (SNAT)

demo-vm1(192.168.1.7) ping  external(203.0.113.10)，在external抓包：

![](/assets/2016-01-27-openstack-neutron-classic-ovs-practice-1.png)

可以看到，从external来看，所有请求都来自203.0.113.101.

after run 'nova add-floating-ip demo-vm1 203.0.113.102':

![](/assets/2016-01-27-openstack-neutron-classic-ovs-practice-2.png)

给demo-vm1增加floating ip后，从external来看，所有请求都变成来自203.0.113.102.

通过[iptables-save](http://www.faqs.org/docs/iptables/iptables-save.html)查看相关iptables rule:

```sh
[root@network ~]# ip netns exec qrouter-90cef600-27e2-48a4-b447-35291e1a68aa iptables-save  
...
-A neutron-l3-agent-float-snat -s 192.168.1.7/32 -j SNAT --to-source 203.0.113.102 #影响有floating ip的实例
-A neutron-l3-agent-snat -j neutron-l3-agent-float-snat
-A neutron-l3-agent-snat -s 192.168.1.0/24 -j SNAT --to-source 203.0.113.101 #影响没有floating ip的实例
```

* external -> vm (DNAT)

external(203.0.113.10) ping demo-vm1(203.0.113.102,192.168.1.7) 


相关iptables rule:

```sh
[root@network ~]# ip netns exec qrouter-90cef600-27e2-48a4-b447-35291e1a68aa iptables-save  
...
-A neutron-l3-agent-OUTPUT -d 203.0.113.102/32 -j DNAT --to-destination 192.168.1.7 #影响network node -> instance
-A neutron-l3-agent-PREROUTING -d 203.0.113.102/32 -j DNAT --to-destination 192.168.1.7 #影响external -> instance
```

# enable access instance remotely

默认情况下，neutron的security groups机制会为VM实例增加防火墙规则，阻止外部对实例的访问：

```sh
[root@external ~]# ping  203.0.113.102
PING 203.0.113.102 (203.0.113.102) 56(84) bytes of data.
^C
--- 203.0.113.102 ping statistics ---
6 packets transmitted, 0 received, 100% packet loss, time 4999ms
```

我们需要手动增加[security group rule](http://docs.openstack.org/icehouse/install-guide/install/yum/content/launch-instance-neutron.html#launch-instance-neutron-remoteaccess):

* Permit ICMP (ping)

```sh
[root@controller ~]# nova secgroup-list
+--------------------------------------+---------+-------------+
| Id                                   | Name    | Description |
+--------------------------------------+---------+-------------+
| de3f2bd5-1abf-47e2-9852-438e4797d344 | default | default     |
+--------------------------------------+---------+-------------+

[root@controller ~]# nova secgroup-add-rule default icmp -1 -1 0.0.0.0/0
+-------------+-----------+---------+-----------+--------------+
| IP Protocol | From Port | To Port | IP Range  | Source Group |
+-------------+-----------+---------+-----------+--------------+
| icmp        | -1        | -1      | 0.0.0.0/0 |              |
+-------------+-----------+---------+-----------+--------------+

[root@external ~]# ping  203.0.113.102
PING 203.0.113.102 (203.0.113.102) 56(84) bytes of data.
64 bytes from 203.0.113.102: icmp_seq=1 ttl=63 time=2.07 ms
64 bytes from 203.0.113.102: icmp_seq=2 ttl=63 time=0.729 ms
64 bytes from 203.0.113.102: icmp_seq=3 ttl=63 time=0.736 ms
```

* Permit secure shell (SSH) access

```sh
[root@controller ~]# nova secgroup-add-rule default tcp 22 22 0.0.0.0/0
+-------------+-----------+---------+-----------+--------------+
| IP Protocol | From Port | To Port | IP Range  | Source Group |
+-------------+-----------+---------+-----------+--------------+
| tcp         | 22        | 22      | 0.0.0.0/0 |              |
+-------------+-----------+---------+-----------+--------------+
```

# 主要参考

* [Scenario: Classic with Open vSwitch](http://docs.openstack.org/liberty/networking-guide/scenario-classic-ovs.html)
* [Configuring Floating IP addresses for Networking in OpenStack Public and Private Clouds](https://www.mirantis.com/blog/configuring-floating-ip-addresses-networking-openstack-public-private-clouds/)