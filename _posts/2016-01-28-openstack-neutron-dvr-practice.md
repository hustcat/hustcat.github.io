---
layout: post
title: openstack neutron dvr practice
date: 2016-01-28 17:00:30
categories: Openstack
tags: neutron
excerpt: openstack neutron dvr practice
---

# environment

```
172.17.42.50 controller
172.17.42.51 network
172.17.42.52 compute1
```

# 配置

## controller node

* /etc/neutron/neutron.conf

```
[DEFAULT]
verbose = True
router_distributed = True
core_plugin = ml2
service_plugins = router
allow_overlapping_ips = True
```

* /etc/neutron/plugins/ml2/ml2_conf.ini

```
[ml2]
type_drivers = flat,vlan,gre,vxlan
tenant_network_types = vlan,gre,vxlan
mechanism_drivers = openvswitch,l2population

[ml2_type_flat]
flat_networks = external

[ml2_type_vlan]
network_vlan_ranges = external,vlan:1:1000

[ml2_type_gre]
tunnel_id_ranges = 1:1000

[ml2_type_vxlan]
vni_ranges = 1:1000
vxlan_group = 239.1.1.1

[securitygroup]
firewall_driver = neutron.agent.linux.iptables_firewall.OVSHybridIptablesFirewallDriver
enable_security_group = True
enable_ipset = True
```

* start neturon service

```
systemctl start neutron-server.service
```

## network node

* packages

```sh
# yum install openstack-neutron openstack-neutron-ml2 \
  openstack-neutron-openvswitch python-neutronclient ebtables ipset
```

* /etc/sysctl.conf

```
net.ipv4.ip_forward=1
net.ipv4.conf.default.rp_filter=0
net.ipv4.conf.all.rp_filter=0
```
* /etc/neutron/neutron.conf

```
[DEFAULT]
verbose = True

rpc_backend = rabbit
auth_strategy = keystone

[oslo_messaging_rabbit]
rabbit_host = controller
rabbit_userid = openstack
rabbit_password = 123456

[keystone_authtoken]
auth_uri = http://controller:5000
auth_url = http://controller:35357
auth_plugin = password
project_domain_id = default
user_domain_id = default
project_name = service
username = neutron
password = 123456

[oslo_concurrency]
lock_path = /var/lib/neutron/tmp
```

* /etc/neutron/plugins/ml2/ml2_conf.ini

```
[ovs]
local_ip = 172.18.42.51
bridge_mappings = vlan:br-vlan,external:br-ex

[agent]
l2_population = True
tunnel_types = gre,vxlan
enable_distributed_routing = True
arp_responder = True

[securitygroup]
firewall_driver = neutron.agent.linux.iptables_firewall.OVSHybridIptablesFirewallDriver
enable_security_group = True
enable_ipset = True
```

```
#ln -s /etc/neutron/plugins/ml2/ml2_conf.ini /etc/neutron/plugin.ini
```

* /etc/neutron/l3_agent.ini

```
[DEFAULT]
verbose = True
interface_driver = neutron.agent.linux.interface.OVSInterfaceDriver
use_namespaces = True
external_network_bridge = br-ex
router_delete_namespaces = True
agent_mode = dvr_snat
```

* /etc/neutron/dhcp_agent.ini

```
[DEFAULT]
verbose = True
interface_driver = neutron.agent.linux.interface.OVSInterfaceDriver
dhcp_driver = neutron.agent.linux.dhcp.Dnsmasq
use_namespaces = True
dhcp_delete_namespaces = True

dnsmasq_config_file = /etc/neutron/dnsmasq-neutron.conf
```

* /etc/neutron/metadata_agent.ini

```
[DEFAULT]
verbose = True
nova_metadata_ip = controller
metadata_proxy_shared_secret = 123456

auth_uri = http://controller:5000
auth_url = http://controller:35357
auth_region = RegionOne
auth_plugin = password
project_domain_id = default
user_domain_id = default
project_name = service
username = neutron
password = 123456
```

* start service

```
# systemctl start openvswitch 
# ovs-vsctl add-br br-ex
# ovs-vsctl add-port br-ex eth2


# systemctl start neutron-openvswitch-agent.service neutron-dhcp-agent.service \
  neutron-metadata-agent.service neutron-l3-agent.service
```

## compute node

* /etc/sysctl.conf

```
net.ipv4.ip_forward=1
net.ipv4.conf.default.rp_filter=0
net.ipv4.conf.all.rp_filter=0
net.bridge.bridge-nf-call-iptables=1
net.bridge.bridge-nf-call-ip6tables=1
```

* /etc/neutron/neutron.conf

```
[DEFAULT]
verbose = True

rpc_backend = rabbit
auth_strategy = keystone

[oslo_messaging_rabbit]
rabbit_host = controller
rabbit_userid = openstack
rabbit_password = 123456

[keystone_authtoken]
auth_uri = http://controller:5000
auth_url = http://controller:35357
auth_plugin = password
project_domain_id = default
user_domain_id = default
project_name = service
username = neutron
password = 123456

[oslo_concurrency]
lock_path = /var/lib/neutron/tmp
```

* /etc/neutron/plugins/ml2/ml2_conf.ini

```
[ovs]
local_ip = 172.18.42.52
bridge_mappings = vlan:br-vlan,external:br-ex

[agent]
l2_population = True
tunnel_types = gre,vxlan
enable_distributed_routing = True
arp_responder = True

[securitygroup]
firewall_driver = neutron.agent.linux.iptables_firewall.OVSHybridIptablesFirewallDriver
enable_security_group = True
enable_ipset = True
```

```
#ln -s /etc/neutron/plugins/ml2/ml2_conf.ini /etc/neutron/plugin.ini
```

* /etc/neutron/l3_agent.ini

```
[DEFAULT]
verbose = True
interface_driver = neutron.agent.linux.interface.OVSInterfaceDriver
use_namespaces = True
external_network_bridge = br-ex
router_delete_namespaces = True
agent_mode = dvr
```

* /etc/neutron/metadata_agent.ini

```
[DEFAULT]
verbose = True
nova_metadata_ip = controller
metadata_proxy_shared_secret = 123456

auth_uri = http://controller:5000
auth_url = http://controller:35357
auth_region = RegionOne
auth_plugin = password
project_domain_id = default
user_domain_id = default
project_name = service
username = neutron
password = 123456

```

* /etc/nova/nova.conf

```
[neutron]
url = http://controller:9696
auth_url = http://controller:35357
auth_plugin = password
project_domain_id = default
user_domain_id = default
region_name = RegionOne
project_name = service
username = neutron
password = 123456
```

* start service

```
# systemctl start openvswitch 
# ovs-vsctl add-br br-ex
# ovs-vsctl add-port br-ex eth2

# systemctl start neutron-openvswitch-agent.service \
  neutron-metadata-agent.service neutron-l3-agent.service
```


## verify

```
[root@controller ~]# neutron agent-list
+--------------------------------------+--------------------+----------+-------+----------------+---------------------------+
| id                                   | agent_type         | host     | alive | admin_state_up | binary                    |
+--------------------------------------+--------------------+----------+-------+----------------+---------------------------+
| 29d52888-1268-4351-97e2-36dcc8466c32 | Open vSwitch agent | compute1 | :-)   | True           | neutron-openvswitch-agent |
| 406a8a5e-209a-43c9-aeb1-94109a0c2ba4 | Metadata agent     | compute1 | :-)   | True           | neutron-metadata-agent    |
| 5dc96667-7f5f-4e3d-85aa-22759c7a4c5a | L3 agent           | compute1 | :-)   | True           | neutron-l3-agent          |
| 77c78491-d4e6-491f-8ac6-e6348fbc87c9 | L3 agent           | network  | :-)   | True           | neutron-l3-agent          |
| 7945ac8b-42a7-4a6f-8af0-a825633abfe0 | Metadata agent     | network  | :-)   | True           | neutron-metadata-agent    |
| 9b2f273e-9f7a-468c-a47a-f794a66f5c76 | DHCP agent         | network  | :-)   | True           | neutron-dhcp-agent        |
| b0ebab6e-501a-40df-abc7-f870a3ff994d | Open vSwitch agent | network  | :-)   | True           | neutron-openvswitch-agent |
+--------------------------------------+--------------------+----------+-------+----------------+---------------------------+
```

# 测试

Creates a flat external network and a VXLAN project network.

## Create initial networks

### external network

* Create the external network

```sh
[root@controller ~]# neutron net-create ext-net --router:external \
>   --provider:physical_network external --provider:network_type flat
Created a new network:
+---------------------------+--------------------------------------+
| Field                     | Value                                |
+---------------------------+--------------------------------------+
| admin_state_up            | True                                 |
| id                        | 8441b8e8-1dfb-484b-bdae-02a61936db9b |
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
```

* Create a subnet on the external network

```sh
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
| id                | ececc303-22d8-4804-a121-d98a4984be11             |
| ip_version        | 4                                                |
| ipv6_address_mode |                                                  |
| ipv6_ra_mode      |                                                  |
| name              |                                                  |
| network_id        | 8441b8e8-1dfb-484b-bdae-02a61936db9b             |
| subnetpool_id     |                                                  |
| tenant_id         | 0b48811a7e614b1ba752a6544d4eba02                 |
+-------------------+--------------------------------------------------+
```

### project network

* Create the project network

```sh
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
| id                        | 2ffdf0df-54cb-46e8-81d8-5b40d7cfd45e |
| mtu                       | 0                                    |
| name                      | demo-net                             |
| provider:network_type     | vxlan                                |
| provider:physical_network |                                      |
| provider:segmentation_id  | 25                                   |
| router:external           | False                                |
| shared                    | False                                |
| status                    | ACTIVE                               |
| subnets                   |                                      |
| tenant_id                 | 54dda1538bf64ba986f8672884326761     |
+---------------------------+--------------------------------------+
```

* Create a subnet on the project network

```sh
[root@controller ~]# source demo-openrc.sh
[root@controller ~]# neutron subnet-create demo-net --name demo-subnet --gateway 192.168.10.1   192.168.10.0/24
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
| id                | 6fceea95-4c1c-4631-84a8-ebe93d515f0a               |
| ip_version        | 4                                                  |
| ipv6_address_mode |                                                    |
| ipv6_ra_mode      |                                                    |
| name              | demo-subnet                                        |
| network_id        | 2ffdf0df-54cb-46e8-81d8-5b40d7cfd45e               |
| subnetpool_id     |                                                    |
| tenant_id         | 54dda1538bf64ba986f8672884326761                   |
+-------------------+----------------------------------------------------+
```

* Create a distributed project router

```sh
[root@controller ~]# neutron router-create demo-router
Created a new router:
+-----------------------+--------------------------------------+
| Field                 | Value                                |
+-----------------------+--------------------------------------+
| admin_state_up        | True                                 |
| external_gateway_info |                                      |
| id                    | 3d4857d4-50db-412c-ab09-da1b48222b46 |
| name                  | demo-router                          |
| routes                |                                      |
| status                | ACTIVE                               |
| tenant_id             | 54dda1538bf64ba986f8672884326761     |
+-----------------------+--------------------------------------+
```

* Attach the project network to the router

```sh
[root@controller ~]# neutron router-interface-add demo-router demo-subnet
Added interface 26693640-ee09-4b7b-a44d-a6f89c2c7125 to router demo-router.
```

* Add a gateway to the external network for the project network on the router

```sh
[root@controller ~]# neutron router-gateway-set demo-router ext-net                             
Set gateway for router demo-router
```

## verify

在网络节点，会看到snat, qrouter 和 qdhcp namespaces:

```sh
[root@network ~]# ip netns
snat-3d4857d4-50db-412c-ab09-da1b48222b46
qrouter-3d4857d4-50db-412c-ab09-da1b48222b46
qdhcp-2ffdf0df-54cb-46e8-81d8-5b40d7cfd45e

[root@network ~]# ip netns exec snat-3d4857d4-50db-412c-ab09-da1b48222b46 ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN 
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
18: sg-ee7fdd84-0e: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN 
    link/ether fa:16:3e:f1:96:8b brd ff:ff:ff:ff:ff:ff
    inet 192.168.10.6/24 brd 192.168.10.255 scope global sg-ee7fdd84-0e
       valid_lft forever preferred_lft forever
    inet6 fe80::f816:3eff:fef1:968b/64 scope link 
       valid_lft forever preferred_lft forever
19: qg-6176d55e-ee: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN 
    link/ether fa:16:3e:16:0b:9c brd ff:ff:ff:ff:ff:ff
    inet 203.0.113.13/24 brd 203.0.113.255 scope global qg-6176d55e-ee
       valid_lft forever preferred_lft forever
    inet6 fe80::f816:3eff:fe16:b9c/64 scope link 
       valid_lft forever preferred_lft forever

[root@network ~]# ip netns exec qrouter-3d4857d4-50db-412c-ab09-da1b48222b46 ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN 
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
17: qr-3e60aab4-57: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UNKNOWN 
    link/ether fa:16:3e:ef:bc:7e brd ff:ff:ff:ff:ff:ff
    inet 192.168.10.1/24 brd 192.168.10.255 scope global qr-3e60aab4-57
       valid_lft forever preferred_lft forever
    inet6 fe80::f816:3eff:feef:bc7e/64 scope link 
       valid_lft forever preferred_lft forever

[root@network ~]# ovs-vsctl show
899935c8-0797-47c0-9290-8b718396519b
    Bridge br-int
        fail_mode: secure
        Port br-int
            Interface br-int
                type: internal
        Port "sg-ee7fdd84-0e"
            tag: 4095
            Interface "sg-ee7fdd84-0e"
                type: internal
        Port "qr-3e60aab4-57"
            tag: 4095
            Interface "qr-3e60aab4-57"
                type: internal
        Port "qg-7cac7e10-cc"
            tag: 4095
            Interface "qg-7cac7e10-cc"
                type: internal
        Port "tap8311fca4-65"
            tag: 4095
            Interface "tap8311fca4-65"
                type: internal
    Bridge br-ex
        Port "qg-6176d55e-ee"
            Interface "qg-6176d55e-ee"
                type: internal
        Port br-ex
            Interface br-ex
                type: internal
        Port "eth2"
            Interface "eth2"
    ovs_version: "2.4.0"
```

external network gateway IP address for the project network on the router

```sh
[root@controller ~]# neutron router-port-list demo-router
+--------------------------------------+------+-------------------+-------------------------------------------------------------------------------------+
| id                                   | name | mac_address       | fixed_ips                                                                           |
+--------------------------------------+------+-------------------+-------------------------------------------------------------------------------------+
| 3e60aab4-57d4-445d-ae94-890136ad2839 |      | fa:16:3e:ef:bc:7e | {"subnet_id": "6fceea95-4c1c-4631-84a8-ebe93d515f0a", "ip_address": "192.168.10.1"} |
| 6176d55e-ee84-4e56-a8e3-93ce3cc640ef |      | fa:16:3e:16:0b:9c | {"subnet_id": "ececc303-22d8-4804-a121-d98a4984be11", "ip_address": "203.0.113.13"} |
| ee7fdd84-0eb8-417e-98f3-66ecacf1fb81 |      | fa:16:3e:f1:96:8b | {"subnet_id": "6fceea95-4c1c-4631-84a8-ebe93d515f0a", "ip_address": "192.168.10.6"} |
+--------------------------------------+------+-------------------+-------------------------------------------------------------------------------------+
```

ping external gateway IP from external network

```sh
[root@external ~]# ping -c 3 203.0.113.13  
PING 203.0.113.13 (203.0.113.13) 56(84) bytes of data.
64 bytes from 203.0.113.13: icmp_seq=1 ttl=64 time=0.450 ms
64 bytes from 203.0.113.13: icmp_seq=2 ttl=64 time=0.315 ms
64 bytes from 203.0.113.13: icmp_seq=3 ttl=64 time=0.221 ms

--- 203.0.113.13 ping statistics ---
3 packets transmitted, 3 received, 0% packet loss, time 2000ms
rtt min/avg/max/mdev = 0.221/0.328/0.450/0.096 ms
```

# neutron command

```sh
# neutron router-gateway-clear demo-router ext-net
Removed gateway from router demo-router

# neutron router-interface-delete demo-router demo-subnet           
Removed interface from router demo-router.

# neutron router-delete demo-router                                    
Deleted router: demo-router

# neutron subnet-delete demo-subnet    
Deleted subnet: demo-subnet

# neutron net-delete demo-net      
Deleted network: demo-net
```

# 主要参考

* [Scenario: High Availability using Distributed Virtual Routing (DVR)](http://docs.openstack.org/liberty/networking-guide/scenario-dvr-ovs.html)
* [Networking service command-line client](http://docs.openstack.org/cli-reference/neutron.html)
