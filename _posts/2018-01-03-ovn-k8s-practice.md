---
layout: post
title: OVN-kubernetes practice
date: 2018-01-03 23:00:30
categories: Network
tags: OVN SDN OVS kubernetes
excerpt: OVN-kubernetes practice
---

环境:

* kube-master: 172.17.42.30 192.168.1.0/24
* kube-node1: 172.17.42.31  192.168.2.0/24
* kube-node2: 172.17.42.32  192.168.3.0/24
* kube-node3: 172.17.42.33


## Start OVN daemon

### Central node

* Start OVS and controller

```
CENTRAL_IP=172.17.42.30
LOCAL_IP=172.17.42.30
ENCAP_TYPE=geneve

## start ovs
/usr/share/openvswitch/scripts/ovs-ctl start

## set ovn-remote and ovn-nb
ovs-vsctl set Open_vSwitch . external_ids:ovn-remote="tcp:$CENTRAL_IP:6642" external_ids:ovn-nb="tcp:$CENTRAL_IP:6641" external_ids:ovn-encap-ip=$LOCAL_IP external_ids:ovn-encap-type="$ENCAP_TYPE"


## set system_id
id_file=/etc/openvswitch/system-id.conf
test -e $id_file || uuidgen > $id_file
ovs-vsctl set Open_vSwitch . external_ids:system-id=$(cat $id_file)


## start ovn-controller and vtep
/usr/share/openvswitch/scripts/ovn-ctl start_controller
/usr/share/openvswitch/scripts/ovn-ctl start_controller_vtep
```

* start ovn-northd

```
# /usr/share/openvswitch/scripts/ovn-ctl start_northd
Starting ovn-northd                                        [  OK  ]
```

Open up TCP ports to access the OVN databases:

```
[root@kube-master ~]# ovn-nbctl set-connection ptcp:6641
[root@kube-master ~]# ovn-sbctl set-connection ptcp:6642
```

### Compute node

```
CENTRAL_IP=172.17.42.30
LOCAL_IP=172.17.42.31
ENCAP_TYPE=geneve

## start ovs
/usr/share/openvswitch/scripts/ovs-ctl start

## set ovn-remote and ovn-nb
ovs-vsctl set Open_vSwitch . external_ids:ovn-remote="tcp:$CENTRAL_IP:6642" external_ids:ovn-nb="tcp:$CENTRAL_IP:6641" external_ids:ovn-encap-ip=$LOCAL_IP external_ids:ovn-encap-type="$ENCAP_TYPE"


## set system_id
id_file=/etc/openvswitch/system-id.conf
test -e $id_file || uuidgen > $id_file
ovs-vsctl set Open_vSwitch . external_ids:system-id=$(cat $id_file)


## start ovn-controller and vtep
/usr/share/openvswitch/scripts/ovn-ctl start_controller
/usr/share/openvswitch/scripts/ovn-ctl start_controller_vtep
```

## OVN K8S配置

### k8s master node

* master node initialization

Set the k8s API server address in the Open vSwitch database for the initialization scripts (and later daemons) to pick from.

```
# ovs-vsctl set Open_vSwitch . external_ids:k8s-api-server="127.0.0.1:8080"
```

```
git clone https://github.com/openvswitch/ovn-kubernetes
cd ovn-kubernetes
pip install .
```

* master init

```
ovn-k8s-overlay master-init \
   --cluster-ip-subnet="192.168.0.0/16" \
   --master-switch-subnet="192.168.1.0/24" \
   --node-name="kube-master"
```

这会创建`logical switch/router`:

```
# ovn-nbctl show
switch d034f42f-6dd5-4ba9-bfdd-114ce17c9235 (kube-master)
    port k8s-kube-master
        addresses: ["ae:31:fa:c7:81:fc 192.168.1.2"]
    port stor-kube-master
        type: router
        addresses: ["00:00:00:B5:F1:57"]
        router-port: rtos-kube-master
switch 2680f36b-85c2-4064-b811-5c0bd91debdd (join)
    port jtor-kube-master
        type: router
        addresses: ["00:00:00:1A:E4:98"]
        router-port: rtoj-kube-master
router ce75b330-dbd3-43d2-aa4f-4e17af898532 (kube-master)
    port rtos-kube-master
        mac: "00:00:00:B5:F1:57"
        networks: ["192.168.1.1/24"]
    port rtoj-kube-master
        mac: "00:00:00:1A:E4:98"
        networks: ["100.64.1.1/24"]
```

### k8s node

kube-node1:

```
K8S_API_SERVER_IP=172.17.42.30
ovs-vsctl set Open_vSwitch . \
  external_ids:k8s-api-server="$K8S_API_SERVER_IP:8080"

ovn-k8s-overlay minion-init \
  --cluster-ip-subnet="192.168.0.0/16" \
  --minion-switch-subnet="192.168.2.0/24" \
  --node-name="kube-node1"

## 对于https需要指定CA和token
ovs-vsctl set Open_vSwitch . \
  external_ids:k8s-api-server="https://$K8S_API_SERVER_IP" \
  external_ids:k8s-ca-certificate="/etc/kubernetes/certs/ca.crt" \
  external_ids:k8s-api-token="YMMFKeD4XqLDakZKQbTCvueGlcdcdgBx"
```

这会创建对应的`logical switch`，并连接到`logical router (kube-master)`:

```
# ovn-nbctl show
switch 0147b986-1dab-49a5-9c4e-57d9feae8416 (kube-node1)
    port k8s-kube-node1
        addresses: ["ba:2c:06:32:14:78 192.168.2.2"]
    port stor-kube-node1
        type: router
        addresses: ["00:00:00:C0:2E:C7"]
        router-port: rtos-kube-node1
...
router ce75b330-dbd3-43d2-aa4f-4e17af898532 (kube-master)
    port rtos-kube-node2
        mac: "00:00:00:D3:4B:AA"
        networks: ["192.168.3.1/24"]
    port rtos-kube-node1
        mac: "00:00:00:C0:2E:C7"
        networks: ["192.168.2.1/24"]
    port rtos-kube-master
        mac: "00:00:00:B5:F1:57"
        networks: ["192.168.1.1/24"]
    port rtoj-kube-master
        mac: "00:00:00:1A:E4:98"
        networks: ["100.64.1.1/24"]
```

kube-node2:

```
K8S_API_SERVER_IP=172.17.42.30
ovs-vsctl set Open_vSwitch . \
  external_ids:k8s-api-server="$K8S_API_SERVER_IP:8080"

ovn-k8s-overlay minion-init \
  --cluster-ip-subnet="192.168.0.0/16" \
  --minion-switch-subnet="192.168.3.0/24" \
  --node-name="kube-node2"
```

### Gateway node

```
## attach eth0 to bridge breth0 and move IP/routes
ovn-k8s-util nics-to-bridge eth0

## initialize gateway

ovs-vsctl set Open_vSwitch . \
  external_ids:k8s-api-server="$K8S_API_SERVER_IP:8080"

ovn-k8s-overlay gateway-init \
  --cluster-ip-subnet="$CLUSTER_IP_SUBNET" \
  --bridge-interface breth0 \
  --physical-ip "$PHYSICAL_IP" \
  --node-name="$NODE_NAME" \
  --default-gw "$EXTERNAL_GATEWAY"

# Since you share a NIC for both mgmt and North-South connectivity, you will 
# have to start a separate daemon to de-multiplex the traffic.
ovn-k8s-gateway-helper --physical-bridge=breth0 --physical-interface=eth0 \
    --pidfile --detach
```


### Watchers on master node

```
ovn-k8s-watcher \
  --overlay \
  --pidfile \
  --log-file \
  -vfile:info \
  -vconsole:emer \
  --detach

# ps -ef | grep ovn-k8s
root     28151     1  1 12:57 ?        00:00:00 /usr/bin/python /usr/bin/ovn-k8s-watcher --overlay --pidfile --log-file -vfile:info -vconsole:emer --detach
```

对应的日志位于`/var/log/openvswitch/ovn-k8s-watcher.log`.

## 测试

创建Pod:

```
apiVersion: v1
kind: Pod
metadata:
  name: sshd-2
spec:
  containers:
  - name: sshd-2
    image: dbyin/sshd:1.0
```

CNI执行程序为`/opt/cni/bin/ovn_cni`，创建容器的日志：

```
# tail -f /var/log/openvswitch/ovn-k8s-cni-overlay.log
2018-01-03T08:42:39.609Z |  0  | ovn-k8s-cni-overlay | DBG | plugin invoked with cni_command = ADD cni_container_id = a2f5796e82e9286d7f56540585b6040b3a743093c46ea34364212cf1afd42a32 cni_ifname = eth0 cni_netns = /proc/31180/ns/net cni_args = IgnoreUnknown=1;K8S_POD_NAMESPACE=default;K8S_POD_NAME=sshd-2;K8S_POD_INFRA_CONTAINER_ID=a2f5796e82e9286d7f56540585b6040b3a743093c46ea34364212cf1afd42a32
2018-01-03T08:42:39.633Z |  1  | kubernetes | DBG | Annotations for pod sshd-2: {u'ovn': u'{"gateway_ip": "192.168.2.1", "ip_address": "192.168.2.3/24", "mac_address": "0a:00:00:00:00:01"}'}
2018-01-03T08:42:39.635Z |  2  | ovn-k8s-cni-overlay | DBG | Creating veth pair for container a2f5796e82e9286d7f56540585b6040b3a743093c46ea34364212cf1afd42a32
2018-01-03T08:42:39.662Z |  3  | ovn-k8s-cni-overlay | DBG | Bringing up veth outer interface a2f5796e82e9286
2018-01-03T08:42:39.769Z |  4  | ovn-k8s-cni-overlay | DBG | Create a link for container namespace
2018-01-03T08:42:39.781Z |  5  | ovn-k8s-cni-overlay | DBG | Adding veth inner interface to namespace for container a2f5796e82e9286d7f56540585b6040b3a743093c46ea34364212cf1afd42a32
2018-01-03T08:42:39.887Z |  6  | ovn-k8s-cni-overlay | DBG | Configuring and bringing up veth inner interface a2f5796e82e92_c. New name:'eth0',MAC address:'0a:00:00:00:00:01', MTU:'1400', IP:192.168.2.3/24
2018-01-03T08:42:44.960Z |  7  | ovn-k8s-cni-overlay | DBG | Setting gateway_ip 192.168.2.1 for container:a2f5796e82e9286d7f56540585b6040b3a743093c46ea34364212cf1afd42a32
2018-01-03T08:42:44.983Z |  8  | ovn-k8s-cni-overlay | DBG | output is {"gateway_ip": "192.168.2.1", "ip_address": "192.168.2.3/24", "mac_address": "0a:00:00:00:00:01"}
```

从`kube-node2`可以访问sshd-2`:

```
[root@kube-node2 ~]# ping -c 2 192.168.2.3
PING 192.168.2.3 (192.168.2.3) 56(84) bytes of data.
64 bytes from 192.168.2.3: icmp_seq=1 ttl=63 time=0.281 ms
64 bytes from 192.168.2.3: icmp_seq=2 ttl=63 time=0.304 ms

--- 192.168.2.3 ping statistics ---
2 packets transmitted, 2 received, 0% packet loss, time 1009ms
rtt min/avg/max/mdev = 0.281/0.292/0.304/0.020 ms
```

OVS on `kube-node1`:

```
# ovs-vsctl show                                      
9b92e4fb-fc59-47ae-afa4-a95d1842e2bd
    Bridge br-int
        fail_mode: secure
        Port "ovn-069367-0"
            Interface "ovn-069367-0"
                type: vxlan
                options: {csum="true", key=flow, remote_ip="172.17.42.33"}
        Port br-int
            Interface br-int
                type: internal
        Port "k8s-kube-node1"
            Interface "k8s-kube-node1"
                type: internal
        Port "a2f5796e82e9286"
            Interface "a2f5796e82e9286"
        Port "ovn-7f9937-0"
            Interface "ovn-7f9937-0"
                type: geneve
                options: {csum="true", key=flow, remote_ip="172.17.42.32"}
        Port "ovn-0696ca-0"
            Interface "ovn-0696ca-0"
                type: geneve
                options: {csum="true", key=flow, remote_ip="172.17.42.30"}
    ovs_version: "2.8.1"
```

`a2f5796e82e9286`为网络容器的前16位。

## Tracing

来看看从`192.168.3.2`到`192.168.2.3`的数据包的`Open Flow`处理过程：

```
[root@kube-node2 ~]# ovs-appctl ofproto/trace br-int in_port=9,ip,dl_src=82:ff:e7:83:99:a9,dl_dst=00:00:00:d3:4b:aa,nw_src=192.168.3.2,nw_dst=192.168.2.3,nw_ttl=32
bridge("br-int")
----------------
 0. in_port=9, priority 100
    set_field:0x1->reg13
    set_field:0xa->reg11
    set_field:0x6->reg12
    set_field:0x5->metadata
    set_field:0x2->reg14
    resubmit(,8)
...
42. ip,reg0=0x1/0x1,metadata=0x5, priority 100, cookie 0x88177e0
    ct(table=43,zone=NXM_NX_REG13[0..15])
    drop
     -> A clone of the packet is forked to recirculate. The forked pipeline will be resumed at table 43.

Final flow: ip,reg0=0x1,reg11=0xa,reg12=0x6,reg13=0x1,reg14=0x2,reg15=0x1,metadata=0x5,in_port=9,vlan_tci=0x0000,dl_src=82:ff:e7:83:99:a9,dl_dst=00:00:00:d3:4b:aa,nw_src=192.168.3.2,nw_dst=192.168.2.3,nw_proto=0,nw_tos=0,nw_ecn=0,nw_ttl=32
Megaflow: recirc_id=0,ct_state=-new-est-rel-inv-trk,eth,ip,in_port=9,vlan_tci=0x0000/0x1000,dl_src=00:00:00:00:00:00/01:00:00:00:00:00,dl_dst=00:00:00:d3:4b:aa,nw_dst=128.0.0.0/1,nw_frag=no
Datapath actions: ct(zone=1),recirc(0x24)

===============================================================================
recirc(0x24) - resume conntrack with default ct_state=trk|new (use --ct-next to customize)
===============================================================================

Flow: recirc_id=0x24,ct_state=new|trk,eth,ip,reg0=0x1,reg11=0xa,reg12=0x6,reg13=0x1,reg14=0x2,reg15=0x1,metadata=0x5,in_port=9,vlan_tci=0x0000,dl_src=82:ff:e7:83:99:a9,dl_dst=00:00:00:d3:4b:aa,nw_src=192.168.3.2,nw_dst=192.168.2.3,nw_proto=0,nw_tos=0,nw_ecn=0,nw_ttl=32

bridge("br-int")
----------------
    thaw
        Resuming from table 43
...
65. reg15=0x1,metadata=0x5, priority 100
    clone(ct_clear,set_field:0->reg11,set_field:0->reg12,set_field:0->reg13,set_field:0x4->reg11,set_field:0xb->reg12,set_field:0x1->metadata,set_field:0x4->reg14,set_field:0->reg10,set_field:0->reg15,set_field:0->reg0,set_field:0->reg1,set_field:0->reg2,set_field:0->reg3,set_field:0->reg4,set_field:0->reg5,set_field:0->reg6,set_field:0->reg7,set_field:0->reg8,set_field:0->reg9,set_field:0->in_port,resubmit(,8))
    ct_clear
    set_field:0->reg11
    set_field:0->reg12
    set_field:0->reg13
    set_field:0x4->reg11
    set_field:0xb->reg12
    set_field:0x1->metadata
    set_field:0x4->reg14
    set_field:0->reg10
    set_field:0->reg15
    set_field:0->reg0
    set_field:0->reg1
    set_field:0->reg2
    set_field:0->reg3
    set_field:0->reg4
    set_field:0->reg5
    set_field:0->reg6
    set_field:0->reg7
    set_field:0->reg8
    set_field:0->reg9
    set_field:0->in_port
    resubmit(,8)
...
13. ip,metadata=0x1,nw_dst=192.168.2.0/24, priority 49, cookie 0xc6501434
    dec_ttl()
    move:NXM_OF_IP_DST[]->NXM_NX_XXREG0[96..127]
     -> NXM_NX_XXREG0[96..127] is now 0xc0a80203
    load:0xc0a80201->NXM_NX_XXREG0[64..95]
    set_field:00:00:00:c0:2e:c7->eth_src
    set_field:0x3->reg15
    load:0x1->NXM_NX_REG10[0]
    resubmit(,14)
14. reg0=0xc0a80203,reg15=0x3,metadata=0x1, priority 100, cookie 0x3b957bac
    set_field:0a:00:00:00:00:01->eth_dst
    resubmit(,15)
...
64. reg10=0x1/0x1,reg15=0x3,metadata=0x1, priority 100
    push:NXM_OF_IN_PORT[]
    set_field:0->in_port
    resubmit(,65)
    65. reg15=0x3,metadata=0x1, priority 100
            clone(ct_clear,set_field:0->reg11,set_field:0->reg12,set_field:0->reg13,set_field:0x5->reg11,set_field:0x9->reg12,set_field:0x4->metadata,set_field:0x1->reg14,set_field:0->reg10,set_field:0->reg15,set_field:0->reg0,set_field:0->reg1,set_field:0->reg2,set_field:0->reg3,set_field:0->reg4,set_field:0->reg5,set_field:0->reg6,set_field:0->reg7,set_field:0->reg8,set_field:0->reg9,set_field:0->in_port,resubmit(,8))
            ct_clear
            set_field:0->reg11
            set_field:0->reg12
            set_field:0->reg13
            set_field:0x5->reg11
            set_field:0x9->reg12
            set_field:0x4->metadata
            set_field:0x1->reg14
...
        23. metadata=0x4,dl_dst=0a:00:00:00:00:01, priority 50, cookie 0x6c2597ec
            set_field:0x3->reg15
            resubmit(,32)
        32. reg15=0x3,metadata=0x4, priority 100
            load:0x4->NXM_NX_TUN_ID[0..23]
            set_field:0x3->tun_metadata0
            move:NXM_NX_REG14[0..14]->NXM_NX_TUN_METADATA0[16..30]
             -> NXM_NX_TUN_METADATA0[16..30] is now 0x1
            output:7
             -> output to kernel tunnel
    pop:NXM_OF_IN_PORT[]
     -> NXM_OF_IN_PORT[] is now 0

Final flow: unchanged
Megaflow: recirc_id=0x24,ct_state=+new-est-rel-inv+trk,eth,ip,tun_id=0/0xffffff,tun_metadata0=NP,in_port=9,vlan_tci=0x0000/0x1000,dl_src=82:ff:e7:83:99:a9,dl_dst=00:00:00:d3:4b:aa,nw_src=192.168.3.2/31,nw_dst=192.168.2.3,nw_ecn=0,nw_ttl=32,nw_frag=no
Datapath actions: set(tunnel(tun_id=0x4,dst=172.17.42.31,ttl=64,tp_dst=6081,geneve({class=0x102,type=0x80,len=4,0x10003}),flags(df|csum|key))),set(eth(src=00:00:00:c0:2e:c7,dst=0a:00:00:00:00:01)),2
```

几个注意点：

* (1) `ofproto/trace`中的`dl_dst=00:00:00:d3:4b:aa`为`kube-node2`对应的网关`192.168.2.1`的MAC地址（即`stor-kube-node2`的地址）.
* (2) 第65条规则将metadata从`0x5 (datapath/kube-node2)` 改成`0x1 (router/kube-master)`.
* (3) 第13条规则为路由规则，修改ttl，并修改`Source MAC`; 第14条规则修改`Dst MAC`地址.
* (4) 第64条规则将datapath改成`kube-node1 (0x4)`.
* (5) 将32条规则修改packet的tun_id为0x4，tun_metadata0为0x3，然后将packet转给`port 7`，即tunnel设备：

```
 7(ovn-c7889c-0): addr:76:c2:2f:bb:06:5b
     config:     0
     state:      0
     speed: 0 Mbps now, 0 Mbps max
...

        Port "ovn-c7889c-0"
            Interface "ovn-c7889c-0"
                type: geneve
                options: {csum="true", key=flow, remote_ip="172.17.42.31"}
```


节点`kube-node1`收到包后的处理过程：

```
[root@kube-node1 ~]# ovs-appctl ofproto/trace br-int in_port=6,tun_id=0x4,tun_metadata0=0x3,dl_src=00:00:00:c0:2e:c7,dl_dst=0a:00:00:00:00:01
Flow: tun_id=0x4,in_port=6,vlan_tci=0x0000,dl_src=00:00:00:c0:2e:c7,dl_dst=0a:00:00:00:00:01,dl_type=0x0000

bridge("br-int")
----------------
 0. in_port=6, priority 100
    move:NXM_NX_TUN_ID[0..23]->OXM_OF_METADATA[0..23]
     -> OXM_OF_METADATA[0..23] is now 0x4
    move:NXM_NX_TUN_METADATA0[16..30]->NXM_NX_REG14[0..14]
     -> NXM_NX_REG14[0..14] is now 0
    move:NXM_NX_TUN_METADATA0[0..15]->NXM_NX_REG15[0..15]
     -> NXM_NX_REG15[0..15] is now 0x3
    resubmit(,33)
...
48. reg15=0x3,metadata=0x4, priority 50, cookie 0x37e139d4
    resubmit(,64)
64. priority 0
    resubmit(,65)
65. reg15=0x3,metadata=0x4, priority 100
    output:10

Final flow: reg11=0x5,reg12=0x8,reg13=0xa,reg15=0x3,tun_id=0x4,metadata=0x4,in_port=6,vlan_tci=0x0000,dl_src=00:00:00:c0:2e:c7,dl_dst=0a:00:00:00:00:01,dl_type=0x0000
Megaflow: recirc_id=0,ct_state=-new-est-rel-inv-trk,eth,tun_id=0x4/0xffffff,tun_metadata0=0x3/0x7fffffff,in_port=6,dl_dst=00:00:00:00:00:00/01:00:00:00:00:00,dl_type=0x0000
Datapath actions: 5
```

最终将packet转给`port 10`，即容器对应的Port:

```
 10(a2f5796e82e9286): addr:de:d3:83:cf:22:7c
     config:     0
     state:      0
     current:    10GB-FD COPPER
     speed: 10000 Mbps now, 0 Mbps max
```

## Refs

* [How to Use Open Virtual Networking With Kubernetes](https://github.com/openvswitch/ovn-kubernetes)
* [OVN Kubernetes插件](https://feisky.gitbooks.io/sdn/ovs/ovn-kubernetes.html).