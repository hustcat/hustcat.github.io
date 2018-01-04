---
layout: post
title: An introduction to OVN architecture
date: 2018-01-04 23:10:30
categories: Network
tags: OVN SDN OVS
excerpt: An introduction to OVN architecture
---

## OVN的特性 

OVN是OVS社区在2015年1月份才宣布的一个子项目，但是到目前为止OVN已经支持了很多功能：

* Logical switches：逻辑交换机，用来做二层转发。

* L2/L3/L4 ACLs：二到四层的 ACL，可以根据报文的 MAC 地址，IP 地址，端口号来做访问控制。

* Logical routers：逻辑路由器，分布式的，用来做三层转发。

* Multiple tunnel overlays：支持多种隧道封装技术，有 Geneve，STT 和 VXLAN。

* TOR switch or software logical switch gateways：支持使用硬件 TOR switch 或者软件逻辑 switch 当作网关来连接物理网络和虚拟网络。

## OVN架构

```
                                         CMS
                                          |
                                          |
                              +-----------|-----------+
                              |           |           |
                              |     OVN/CMS Plugin    |
                              |           |           |
                              |           |           |
                              |   OVN Northbound DB   |
                              |           |           |
                              |           |           |
                              |       ovn-northd      |
                              |           |           |
                              +-----------|-----------+
                                          |
                                          |
                                +-------------------+
                                | OVN Southbound DB |
                                +-------------------+
                                          |
                                          |
                       +------------------+------------------+
                       |                  |                  |
         HV 1          |                  |    HV n          |
       +---------------|---------------+  .  +---------------|---------------+
       |               |               |  .  |               |               |
       |        ovn-controller         |  .  |        ovn-controller         |
       |         |          |          |  .  |         |          |          |
       |         |          |          |     |         |          |          |
       |  ovs-vswitchd   ovsdb-server  |     |  ovs-vswitchd   ovsdb-server  |
       |                               |     |                               |
       +-------------------------------+     +-------------------------------+
```

上图是`OVN`的整体架构，最上面 `Openstack/CMS plugin` 是 CMS（Cloud Management System） 和 OVN 的接口，它把 CMS 的配置转化成 OVN 的格式写到 `Nnorthbound DB` 里面。

详细参考[ovn-architecture - Open Virtual Network architecture](http://openvswitch.org/support/dist-docs/ovn-architecture.7.html)。

* Northbound DB

`Northbound DB`里面存的都是一些逻辑的数据，大部分和物理网络没有关系，比如`logical switch`，`logical router`，`ACL`，`logical port`，和传统网络设备概念一致。

`Northbound DB`进程：

```
root     29981     1  0 Dec27 ?        00:00:00 ovsdb-server: monitoring pid 29982 (healthy)
root     29982 29981  2 Dec27 ?        00:35:53 ovsdb-server --detach --monitor -vconsole:off --log-file=/var/log/openvswitch/ovsdb-server-nb.log --remote=punix:/var/run/openvswitch/ovnnb_db.sock --pidfile=/var/run/openvswitch/ovnnb_db.pid --remote=db:OVN_Northbound,NB_Global,connections --unixctl=ovnnb_db.ctl --private-key=db:OVN_Northbound,SSL,private_key --certificate=db:OVN_Northbound,SSL,certificate --ca-cert=db:OVN_Northbound,SSL,ca_cert --ssl-protocols=db:OVN_Northbound,SSL,ssl_protocols --ssl-ciphers=db:OVN_Northbound,SSL,ssl_ciphers /etc/openvswitch/ovnnb_db.db
```

查看`ovn-northd`的逻辑数据（`logical switch`,`logical port`, `logical router`）：
```
# ovn-nbctl show
switch 707dcb98-baa0-4ac5-8955-1ce4de2f780f (kube-master)
    port stor-kube-master
        type: router
        addresses: ["00:00:00:BF:CC:B1"]
        router-port: rtos-kube-master
    port k8s-kube-master
        addresses: ["66:ce:60:08:9e:cd 192.168.1.2"]
...
```

* OVN-northd

`OVN-northd`类似于一个集中的控制器，它把 `Northbound DB` 里面的数据翻译一下，写到 `Southbound DB` 里面。

```
# start ovn northd
/usr/share/openvswitch/scripts/ovn-ctl start_northd
```

`ovn-northd`进程：

```
root     29996     1  0 Dec27 ?        00:00:00 ovn-northd: monitoring pid 29997 (healthy)
root     29997 29996 25 Dec27 ?        06:52:54 ovn-northd -vconsole:emer -vsyslog:err -vfile:info --ovnnb-db=unix:/var/run/openvswitch/ovnnb_db.sock --ovnsb-db=unix:/var/run/openvswitch/ovnsb_db.sock --no-chdir --log-file=/var/log/openvswitch/ovn-northd.log --pidfile=/var/run/openvswitch/ovn-northd.pid --detach --monitor
```

* Southbound DB

`Southbound DB`进程：
```
root     32441     1  0 Dec28 ?        00:00:00 ovsdb-server: monitoring pid 32442 (healthy)
root     32442 32441  0 Dec28 ?        00:01:45 ovsdb-server --detach --monitor -vconsole:off --log-file=/var/log/openvswitch/ovsdb-server-sb.log --remote=punix:/var/run/openvswitch/ovnsb_db.sock --pidfile=/var/run/openvswitch/ovnsb_db.pid --remote=db:OVN_Southbound,SB_Global,connections --unixctl=ovnsb_db.ctl --private-key=db:OVN_Southbound,SSL,private_key --certificate=db:OVN_Southbound,SSL,certificate --ca-cert=db:OVN_Southbound,SSL,ca_cert --ssl-protocols=db:OVN_Southbound,SSL,ssl_protocols --ssl-ciphers=db:OVN_Southbound,SSL,ssl_ciphers /etc/openvswitch/ovnsb_db.db
```

`Southbound DB` 里面存的数据和 `Northbound DB` 语义完全不一样，主要包含 3 类数据，一是物理网络数据，比如 HV（hypervisor）的 IP 地址，HV 的 tunnel 封装格式；二是逻辑网络数据，比如报文如何在逻辑网络里面转发；三是物理网络和逻辑网络的绑定关系，比如逻辑端口关联到哪个 HV 上面。

```
# ovn-sbctl show
Chassis "7f99371a-d51c-478c-8de2-facd70e2f739"
    hostname: "kube-node2"
    Encap vxlan
        ip: "172.17.42.32"
        options: {csum="true"}
Chassis "069367d8-8e07-4b81-b057-38cf6b21b2b7"
    hostname: "kube-node3"
    Encap vxlan
        ip: "172.17.42.33"
        options: {csum="true"}
```

* OVN-controller

`ovn-controller` 是 OVN 里面的 agent，类似于 neutron 里面的 ovs-agent，它也是运行在每个 HV 上面。北向，`ovn-controller` 会把物理网络的信息写到 `Southbound DB` 里面；南向，它会把 `Southbound DB` 里面存的一些数据转化成 `Openflow flow` 配到本地的 `OVS table` 里面，来实现报文的转发。

```
# start ovs
/usr/share/openvswitch/scripts/ovs-ctl start --system-id=random
# start ovn-controller
/usr/share/openvswitch/scripts/ovn-ctl start_controller
```

`ovn-controller`进程：

```
root     13423     1  0 Dec26 ?        00:00:00 ovn-controller: monitoring pid 13424 (healthy)
root     13424 13423 82 Dec26 ?        1-15:53:23 ovn-controller unix:/var/run/openvswitch/db.sock -vconsole:emer -vsyslog:err -vfile:info --no-chdir --log-file=/var/log/openvswitch/ovn-controller.log --pidfile=/var/run/openvswitch/ovn-controller.pid --detach --monitor
```

ovs-vswitchd 和 ovsdb-server 是 OVS 的两个进程。


## OVN Northbound DB

`Northbound DB` 是 OVN 和 CMS 之间的接口，Northbound DB 里面的几乎所有的内容都是由 CMS 产生的，ovn-northd 监听这个数据库的内容变化，然后翻译，保存到 Southbound DB 里面。

Northbound DB 里面主要有如下几张表：

* Logical_Switch

每一行代表一个逻辑交换机，逻辑交换机有两种，一种是 `overlay logical switches`，对应于 neutron network，每创建一个 neutron network，networking-ovn 会在这张表里增加一行；另一种是 `bridged logical switch`，连接物理网络和逻辑网络，被 `VTEP gateway` 使用。`Logical_Switch` 里面保存了它包含的 `logical port`（指向 `Logical_Port table`）和应用在它上面的 ACL（指向 ACL table）。

`ovn-nbctl list Logical_Switch`可以查看`Logical_Switch`表中的数据：
```
# ovn-nbctl --db tcp:172.17.42.30:6641 list Logical_Switch
_uuid               : 707dcb98-baa0-4ac5-8955-1ce4de2f780f
acls                : []
dns_records         : []
external_ids        : {gateway_ip="192.168.1.1/24"}
load_balancer       : [4522d0fa-9d46-4165-9524-51d20a35ea0a, 5842a5a9-6c8e-4a87-be3c-c8a0bc271626]
name                : kube-master
other_config        : {subnet="192.168.1.0/24"}
ports               : [44222421-c811-4f38-8ea6-5504a35df703, ee5a5e97-c41d-4656-bd2a-8bc8ad180188]
qos_rules           : []
...
```

* Logical_Switch_Port

每一行代表一个逻辑端口，每创建一个 `neutron port`，`networking-ovn` 会在这张表里增加一行，每行保存的信息有端口的类型，比如 `patch port`，`localnet port`，端口的 IP 和 MAC 地址，端口的状态 UP/Down。

```
# ovn-nbctl --db tcp:172.17.42.30:6641 list Logical_Switch_Port
_uuid               : 44222421-c811-4f38-8ea6-5504a35df703
addresses           : ["00:00:00:BF:CC:B1"]
dhcpv4_options      : []
dhcpv6_options      : []
dynamic_addresses   : []
enabled             : []
external_ids        : {}
name                : stor-kube-master
options             : {router-port=rtos-kube-master}
parent_name         : []
port_security       : []
tag                 : []
tag_request         : []
type                : router
up                  : false
...
```

* ACL

每一行代表一个应用到逻辑交换机上的 ACL 规则，如果逻辑交换机上面的所有端口都没有配置 security group，那么这个逻辑交换机上不应用 ACL。每条 ACL 规则包含匹配的内容，方向，还有动作。


* Logical_Router

每一行代表一个逻辑路由器，每创建一个 neutron router，networking-ovn 会在这张表里增加一行，每行保存了它包含的逻辑的路由器端口。

```
# ovn-nbctl --db tcp:172.17.42.30:6641 list Logical_Router
_uuid               : e12293ba-e61e-40bf-babc-8580d1121641
enabled             : []
external_ids        : {"k8s-cluster-router"=yes}
load_balancer       : []
name                : kube-master
nat                 : []
options             : {}
ports               : [2cbbbb8e-6b5d-44d5-9693-b4069ca9e12a, 3a046f60-161a-4fee-a1b3-d9d3043509d2, 40d3d95d-906b-483b-9b71-1fa6970de6e8, 840ab648-6436-4597-aeff-f84fbc44e3a9, b08758e5-7017-413f-b3db-ff68f49460a4]
static_routes       : []
```

* Logical_Router_Port

每一行代表一个逻辑路由器端口，每创建一个 router interface，networking-ovn 会在这张表里加一行，它主要保存了路由器端口的 IP 和 MAC。

```
# ovn-nbctl --db tcp:172.17.42.30:6641 list Logical_Router_Port
_uuid               : 840ab648-6436-4597-aeff-f84fbc44e3a9
enabled             : []
external_ids        : {}
gateway_chassis     : []
mac                 : "00:00:00:BF:CC:B1"
name                : rtos-kube-master
networks            : ["192.168.1.1/24"]
options             : {}
peer                : []
```

更多请参考[ovn-nb - OVN_Northbound database schema](http://openvswitch.org/support/dist-docs/ovn-nb.5.html).

## Southbound DB

`Southbound DB` 处在 OVN 架构的中心，它是 OVN 中非常重要的一部分，它跟 OVN 的其他组件都有交互。

Southbound DB 里面有如下几张表：

* Chassis

`Chassis`是`OVN`新增的概念，OVS里面没有这个概念，`Chassis`可以是 HV，也可以是 VTEP 网关。

每一行表示一个 HV 或者 VTEP 网关，由 `ovn-controller/ovn-controller-vtep` 填写，包含 chassis 的名字和 chassis 支持的封装的配置（指向表 Encap），如果 chassis 是 VTEP 网关，VTEP 网关上和 OVN 关联的逻辑交换机也保存在这张表里。

```
# ovn-sbctl list Chassis
_uuid               : 3dec4aa7-8f15-493d-89f4-4a260b510bbd
encaps              : [bc324cd4-56f2-4f73-af9e-149b7401e0d2]
external_ids        : {datapath-type="", iface-types="geneve,gre,internal,lisp,patch,stt,system,tap,vxlan", ovn-bridge-mappings=""}
hostname            : "kube-node1"
name                : "c7889c47-2d18-4dd4-a3b7-446d42b79f79"
nb_cfg              : 34
vtep_logical_switches: []
...
```

* Encap

保存着`tunnel`的类型和 `tunnel endpoint IP` 地址。

```
# ovn-sbctl list Encap
_uuid               : bc324cd4-56f2-4f73-af9e-149b7401e0d2
chassis_name        : "c7889c47-2d18-4dd4-a3b7-446d42b79f79"
ip                  : "172.17.42.31"
options             : {csum="true"}
type                : vxlan
...
```

* Logical_Flow

每一行表示一个逻辑的流表，这张表是 `ovn-northd` 根据 `Nourthbound DB` 里面二三层拓扑信息和 ACL 信息转换而来的，`ovn-controller` 把这个表里面的流表转换成 OVS 流表，配到 HV 上的 OVS table。流表主要包含匹配的规则，匹配的方向，优先级，table ID 和执行的动作。

```
# ovn-sbctl lflow-list
Datapath: "kube-node1" (2c3caa57-6a58-4416-9bd2-3e2982d83cf1)  Pipeline: ingress
  table=0 (ls_in_port_sec_l2  ), priority=100  , match=(eth.src[40]), action=(drop;)
  table=0 (ls_in_port_sec_l2  ), priority=100  , match=(vlan.present), action=(drop;)
  table=0 (ls_in_port_sec_l2  ), priority=50   , match=(inport == "default_sshd-2"), action=(next;)
  table=0 (ls_in_port_sec_l2  ), priority=50   , match=(inport == "k8s-kube-node1"), action=(next;)
  table=0 (ls_in_port_sec_l2  ), priority=50   , match=(inport == "stor-kube-node1"), action=(next;)
....
```

* Multicast_Group

每一行代表一个组播组，组播报文和广播报文的转发由这张表决定，它保存了组播组所属的 `datapath`，组播组包含的端口，还有代表 `logical egress port` 的 `tunnel_key`。

* Datapath_Binding

每一行代表一个` datapath` 和物理网络的绑定关系，每个 `logical switch` 和 `logical router` 对应一行。它主要保存了 OVN 给 datapath 分配的代表 `logical datapath identifier` 的 `tunnel_key`。

示例：

```
# ovn-sbctl list Datapath_Binding
_uuid               : 4cfe0e4c-1bbb-406a-9d85-e7bc24c818d0
external_ids        : {logical-router="e12293ba-e61e-40bf-babc-8580d1121641", name=kube-master}
tunnel_key          : 1

_uuid               : 5ec4f962-77a8-44e8-ae01-5b7e46b6a286
external_ids        : {logical-switch="e865aa50-7510-4b7f-9df4-b82801a8e92b", name=join}
tunnel_key          : 2

_uuid               : 2c3caa57-6a58-4416-9bd2-3e2982d83cf1
external_ids        : {logical-switch="7c41601a-dcd5-4e77-b0e8-ca8692d7462b", name="kube-node1"}
tunnel_key          : 4
```

`Port_Binding`

这张表主要用来确定 `logical port` 处在哪个 chassis 上面。每一行包含的内容主要有 `logical port` 的 MAC 和 IP 地址，端口类型，端口属于哪个 `datapath binding`，代表 `logical input/output port identifier` 的 `tunnel_key`, 以及端口处在哪个 chassis。端口所处的 chassis 由 `ovn-controller/ovn-controller` 设置，其余的值由 ovn-northd 设置。

示例：

```
# ovn-sbctl list Port_Binding    
_uuid               : 5e5746d8-3533-45a8-8abe-5a7028c97afa
chassis             : []
datapath            : 2c3caa57-6a58-4416-9bd2-3e2982d83cf1
external_ids        : {}
gateway_chassis     : []
logical_port        : "stor-kube-node1"
mac                 : ["00:00:00:18:22:18"]
nat_addresses       : []
options             : {peer="rtos-kube-node1"}
parent_port         : []
tag                 : []
tunnel_key          : 2
type                : patch
```

* 小结

表 `Chassis` 和表 `Encap` 包含的是物理网络的数据，表 `Logical_Flow` 和表` Multicast_Group` 包含的是逻辑网络的数据，表 `Datapath_Binding` 和表 `Port_Binding`包含的是逻辑网络和物理网络绑定关系的数据。


## OVN tunnel

OVN 支持的 tunnel 类型有三种，分别是 Geneve，STT 和 VXLAN。HV 与 HV 之间的流量，只能用 Geneve 和 STT 两种，HV 和 VTEP 网关之间的流量除了用 Geneve 和 STT 外，还能用 VXLAN，这是为了兼容硬件 VTEP 网关，因为大部分硬件 VTEP 网关只支持 VXLAN。

虽然 VXLAN 是数据中心常用的 tunnel 技术，但是 VXLAN header 是固定的，只能传递一个 VNID（VXLAN network identifier），如果想在 tunnel 里面传递更多的信息，VXLAN 实现不了。所以 OVN 选择了 Geneve 和 STT，Geneve 的头部有个 option 字段，支持 TLV 格式，用户可以根据自己的需要进行扩展，而 STT 的头部可以传递 64-bit 的数据，比 VXLAN 的 24-bit 大很多。


OVN tunnel 封装时使用了三种数据:

* Logical datapath identifier（逻辑的数据通道标识符）

`datapath` 是 OVS 里面的概念，报文需要送到 datapath 进行处理，一个 datapath 对应一个 OVN 里面的逻辑交换机或者逻辑路由器，类似于 `tunnel ID`。这个标识符有 24-bit，由 ovn-northd 分配的，全局唯一，保存在 `Southbound DB` 里面的表 `Datapath_Binding` 的列 `tunnel_key` 里（参考前面的示例）。

* Logical input port identifier（逻辑的入端口标识符）：进入 `logical datapath` 的端口标识符，15-bit 长，由 ovn-northd 分配的，在每个 datapath 里面唯一。它可用范围是 1-32767，0 预留给内部使用。保存在 `Southbound DB` 里面的表 `Port_Binding` 的列 `tunnel_key` 里。

* Logical output port identifier（逻辑的出端口标识符）

离开 `logical datapath` 的端口标识符，16-bit 长，范围 0-32767 和 `logical input port identifier` 含义一样，范围 32768-65535 给组播组使用。对于每个 `logical port`，`input port identifier `和 `output port identifier` 相同。

如果 tunnel 类型是 Geneve，`Geneve header` 里面的 VNI 字段填 `logical datapath identifier`，Option 字段填 `logical input port identifier` 和 `logical output port identifier`，TLV 的 class 为 0xffff，type 为 0，value 为 0 (1-bit) + `logical input port identifier (15-bit)` + `logical output port identifier (16-bit)`。详细参考[Geneve: Generic Network Virtualization Encapsulation](https://tools.ietf.org/html/draft-gross-geneve-00)。

Geneve Option:

```
0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Option Class         |      Type     |R|R|R| Length  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      Variable Option Data                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

OVS 的 tunnel 封装是由 Openflow 流表来做的，所以 ovn-controller 需要把这三个标识符写到本地 HV 的 `Openflow flow table` 里面，对于每个进入 br-int 的报文，都会有这三个属性，`logical datapath identifier` 和 `logical input port identifier` 在入口方向被赋值，分别存在 `openflow metadata` 字段和 Nicira 扩展寄存器 reg6 里面。报文经过 OVS 的 pipeline 处理后，如果需要从指定端口发出去，只需要把 `Logical output port identifier` 写在 Nicira 扩展寄存器 reg7 里面。

OVN tunnel 里面所携带的 `logical input port identifier` 和 `logical output port identifier` 可以提高流表的查找效率，OVS 流表可以通过这两个值来处理报文，不需要解析报文的字段。

## Refs

* [如何借助 OVN 来提高 OVS 在云计算环境中的性能](https://www.ibm.com/developerworks/cn/cloud/library/1603-ovn-ovs-openvswitch/index.html)
* [ovn-architecture - Open Virtual Network architecture](http://openvswitch.org/support/dist-docs/ovn-architecture.7.html)
* [ovn-nb - OVN_Northbound database schema](http://openvswitch.org/support/dist-docs/ovn-nb.5.html)
* [Geneve: Generic Network Virtualization Encapsulation](https://tools.ietf.org/html/draft-gross-geneve-00)