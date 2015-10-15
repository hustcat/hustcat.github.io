---
layout: post
title: Docker native overlay network practice
date: 2015-10-10 20:07:30
categories: Linux
tags: docker network
excerpt: Docker native overlay network practice
---

从1.8开始，Docker通过[libnetwork](https://github.com/docker/libnetwork)，已经实现了原生的overlay network，不过该特性还处于[experimental](https://github.com/docker/docker/tree/master/experimental)。

机器环境

```
yy2 10.193.6.36
yy3 10.193.6.37
```

内核

```
4.1.10
```

目前在3.16以下的内核，会有一些[问题](https://github.com/docker/libnetwork/issues/329)。原因是因为3.16以下的内核在创建vxlan设备时会设置NETIF_F_NETNS_LOCAL。

# 启动docker

```sh
[root@yy2 ~]#consul agent -server -bootstrap -data-dir /tmp/consul -bind=10.193.6.36

[root@yy3 ~]#consul agent -data-dir /tmp/consul -bind 10.193.6.37

[root@yy3 ~]#consul join 10.193.6.36
Successfully joined cluster by contacting 1 nodes.

[root@yy2 ~]# docker daemon -D  --cluster-store=consul://localhost:8500 --label=com.docker.network.driver.overlay.bind_interface=eth0

[root@yy3 ~]#docker daemon -D  --cluster-store=consul://localhost:8500 --label=com.docker.network.driver.overlay.bind_interface=eth0 --label=com.docker.network.driver.overlay.neighbor_ip=10.193.6.36

[root@yy2 ~]# docker network ls
NETWORK ID          NAME                DRIVER
be9c27064942        none                null                
828067e92423        host                host                
124f2d8309a2        bridge              bridge   
```

docker daemon在启动时会默认创建3个network。

# docker network command

```sh
# docker network --help

Usage:  docker network [OPTIONS] COMMAND [OPTIONS]

Commands:
  disconnect               Disconnect container from a network
  inspect                  Display detailed network information
  ls                       List all networks
  rm                       Remove a network
  create                   Create a network
  connect                  Connect container to a network

Run 'docker network COMMAND --help' for more information on a command.

  --help=false       Print usage
```

# Create network

创建网络

```sh
[root@yy2 ~]# docker network create -d overlay prod
14ed035dc75d45ac5ff65850c0d0ed9ac93e43c681a62fb60160af58601eb4af

[root@yy2 ~]# docker network ls
NETWORK ID          NAME                DRIVER
2f1a63988135        bridge              bridge              
14ed035dc75d        prod                overlay             
3d8f144ecc4a        none                null                
641aa737bf98        host                host

[root@yy2 ~]# docker network inspect prod
{
    "name": "prod",
    "id": "14ed035dc75d45ac5ff65850c0d0ed9ac93e43c681a62fb60160af58601eb4af",
    "driver": "overlay",
    "containers": {}
}
```

# Start container

创建容器

```sh
[root@yy2 ~]# docker run -itd --name=vm1 busybox
a068964b53f4192c7d62fa9eaae1c03d674246448da8cc1fff97143c49a2ff54
[root@yy2 ~]# docker run -itd --name=vm2 busybox
b36033667c13b80e4a54b50c52bc6634bde5e24a5b89f10134cf36eb8a64157f
```

默认情况下，容器会加到bridge network：

```sh
[root@yy2 ~]# docker network inspect bridge     
{
    "name": "bridge",
    "id": "2f1a639881358fe98bbb5adbc81fdf69ca115f00063ba83b845bdc52b18d5386",
    "driver": "bridge",
    "containers": {
        "a068964b53f4192c7d62fa9eaae1c03d674246448da8cc1fff97143c49a2ff54": {
            "endpoint": "a1e36c8d2ff77c3d829044c3f7639d50423c54ea8fc5307b3ff94087eea96950",
            "mac_address": "02:42:ac:11:00:01",
            "ipv4_address": "172.17.0.1/16",
            "ipv6_address": ""
        },
        "b36033667c13b80e4a54b50c52bc6634bde5e24a5b89f10134cf36eb8a64157f": {
            "endpoint": "b9ce68da40df5c27f87f6f2af7f0daafe090e581e1cec2a9b72f5c2e8a6e6fbd",
            "mac_address": "02:42:ac:11:00:02",
            "ipv4_address": "172.17.0.2/16",
            "ipv6_address": ""
        }
    }
}

[root@yy2 ~]# docker attach vm1

/ # ifconfig
eth0      Link encap:Ethernet  HWaddr 02:42:AC:11:00:01  
          inet addr:172.17.0.1  Bcast:0.0.0.0  Mask:255.255.0.0
          inet6 addr: fe80::42:acff:fe11:1/64 Scope:Link
          UP BROADCAST RUNNING MULTICAST  MTU:1500  Metric:1
          RX packets:24 errors:0 dropped:0 overruns:0 frame:0
          TX packets:8 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:0 
          RX bytes:1944 (1.8 KiB)  TX bytes:648 (648.0 B)

lo        Link encap:Local Loopback  
          inet addr:127.0.0.1  Mask:255.0.0.0
          inet6 addr: ::1/128 Scope:Host
          UP LOOPBACK RUNNING  MTU:65536  Metric:1
          RX packets:0 errors:0 dropped:0 overruns:0 frame:0
          TX packets:0 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:0 
          RX bytes:0 (0.0 B)  TX bytes:0 (0.0 B)

/ # ping vm2
PING vm2 (172.17.0.2): 56 data bytes
64 bytes from 172.17.0.2: seq=0 ttl=64 time=0.134 ms
64 bytes from 172.17.0.2: seq=1 ttl=64 time=0.068 ms

/ # cat /etc/hosts
172.17.0.3      a068964b53f4
127.0.0.1       localhost
::1     localhost ip6-localhost ip6-loopback
fe00::0 ip6-localnet
ff00::0 ip6-mcastprefix
ff02::1 ip6-allnodes
ff02::2 ip6-allrouters
172.17.0.2      vm2
172.17.0.2      vm2.bridge
172.17.0.3      vm1
172.17.0.3      vm1.bridge
```

容器vm1和vm2可以相互通信。

# Use overlay network

```sh
[root@yy2 ~]# docker run -itd --net=prod --name=vm3 busybox
b40ccf102a0efe4d9607cd00856cc258d5f9d8b674082747e41361ec225bc16d
# docker network ls
NETWORK ID          NAME                DRIVER
14ed035dc75d        prod                overlay             
cb1cbe12dd0b        none                null                
77e1ab40cd08        host                host                
be0c67d2f221        bridge              bridge              
b49507e90aa1        docker_gwbridge     bridge
[root@yy2 ~]# docker network inspect prod
{
    "name": "prod",
    "id": "14ed035dc75d45ac5ff65850c0d0ed9ac93e43c681a62fb60160af58601eb4af",
    "driver": "overlay",
    "containers": {
        "b40ccf102a0efe4d9607cd00856cc258d5f9d8b674082747e41361ec225bc16d": {
            "endpoint": "63d6ecb1bdb16666f23e1ee742e48e94973dc85e29e8ba4e9baa45d6e4dc1e71",
            "mac_address": "02:42:ac:15:00:01",
            "ipv4_address": "172.21.0.1/16",
            "ipv6_address": ""
        }
    }
}

[root@yy2 ~]# brctl show
bridge name     bridge id               STP enabled     interfaces
docker0         8000.02426727fae4       no              veth00dec53
                                                        veth41cb953
docker_gwbridge         8000.02422dfb20a7       no              veth8ef8229
[root@yy2 ~]# ifconfig docker_gwbridge
docker_gwbridge: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500
        inet 172.18.42.1  netmask 255.255.0.0  broadcast 0.0.0.0
```

对于 ** prod ** 网络，docker创建了一个单独的用于NAT通信的bridge docker_gwbridge。

## 查看容器内部的网络

```sh
# docker attach vm3

/ # ifconfig
eth0      Link encap:Ethernet  HWaddr 02:42:AC:15:00:01  
          inet addr:172.21.0.1  Bcast:0.0.0.0  Mask:255.255.0.0
          inet6 addr: fe80::42:acff:fe15:1/64 Scope:Link
          UP BROADCAST RUNNING MULTICAST  MTU:1450  Metric:1
          RX packets:14 errors:0 dropped:0 overruns:0 frame:0
          TX packets:8 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:0 
          RX bytes:1128 (1.1 KiB)  TX bytes:648 (648.0 B)

eth1      Link encap:Ethernet  HWaddr 02:42:AC:12:00:01  
          inet addr:172.18.0.1  Bcast:0.0.0.0  Mask:255.255.0.0
          inet6 addr: fe80::42:acff:fe12:1/64 Scope:Link
          UP BROADCAST RUNNING MULTICAST  MTU:1500  Metric:1
          RX packets:15 errors:0 dropped:0 overruns:0 frame:0
          TX packets:8 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:0 
          RX bytes:1206 (1.1 KiB)  TX bytes:648 (648.0 B)

lo        Link encap:Local Loopback  
          inet addr:127.0.0.1  Mask:255.0.0.0
          inet6 addr: ::1/128 Scope:Host
          UP LOOPBACK RUNNING  MTU:65536  Metric:1
          RX packets:0 errors:0 dropped:0 overruns:0 frame:0
          TX packets:0 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:0 
          RX bytes:0 (0.0 B)  TX bytes:0 (0.0 B)
```

可以看到容器vm3内部有两个网络接口eth0、eth1。实际上，eth1连接到docker_gwbridge，这可以从ip就能看出。eth0即为overlay network的接口。

*** 如何查看overlay network的信息？ ***

libnetwork会为overlay network创建单独的net namespace。

```sh
[root@yy2 ~]# ls /var/run/docker/netns
1-14ed035dc7  89a69449cb27  a1833405369e  afe0d073844f
```

其中，1-14ed035dc7为overlay driver sandbox对应的net namespace。

```sh
[root@yy2 ~]# ln -s /var/run/docker/netns/1-14ed035dc7 /var/run/netns/1-14ed035dc7
[root@yy2 ~]# ip net list
1-14ed035dc7
[root@yy2 ~]# ip netns exec 1-14ed035dc7 ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN 
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host 
       valid_lft forever preferred_lft forever
2: br0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1450 qdisc noqueue state UP 
    link/ether 2e:66:d7:87:4f:ad brd ff:ff:ff:ff:ff:ff
    inet 172.21.255.254/16 scope global br0
       valid_lft forever preferred_lft forever
    inet6 fe80::7c94:4eff:fe28:f3da/64 scope link 
       valid_lft forever preferred_lft forever
8: vxlan1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue master br0 state UNKNOWN 
    link/ether aa:b7:94:75:35:ce brd ff:ff:ff:ff:ff:ff
    inet6 fe80::a8b7:94ff:fe75:35ce/64 scope link 
       valid_lft forever preferred_lft forever
10: veth2@if9: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1450 qdisc noqueue master br0 state UP 
    link/ether 2e:66:d7:87:4f:ad brd ff:ff:ff:ff:ff:ff
    inet6 fe80::2c66:d7ff:fe87:4fad/64 scope link 
       valid_lft forever preferred_lft forever
[root@yy2 ~]# ip netns exec 1-14ed035dc7 brctl show
bridge name     bridge id               STP enabled     interfaces
br0             8000.2e66d7874fad       no              veth2
                                                        vxlan1
```

在yy2创建的overy network会通过KV store自动同步到yy3：

```sh
[root@yy3 ~]# docker network ls
NETWORK ID          NAME                DRIVER
14ed035dc75d        prod                overlay             
fbf365fef5e8        none                null                
05940796ef54        host                host                
ce86e0f95fd3        bridge              bridge 

[root@yy3 ~]# docker network inspect prod
{
    "name": "prod",
    "id": "14ed035dc75d45ac5ff65850c0d0ed9ac93e43c681a62fb60160af58601eb4af",
    "driver": "overlay",
    "containers": {}
}
```

可以看到yy3上的prod与yy2的prod的Network ID相同。

启动容器

```sh
[root@yy3 ~]# docker run -itd --net=prod --name=vm33 busybox
58c990587b691c7aec9fc449b799986ae01339b24152e527a73bbb99235c8142

[root@yy3 ~]# brctl show              
bridge name     bridge id               STP enabled     interfaces
docker0         8000.02424f9905f5       no
docker_gwbridge         8000.024206b7e02a       no              vethdf2af97

[root@yy3 ~]# docker attach vm33
/ # ifconfig
eth0      Link encap:Ethernet  HWaddr 02:42:AC:15:00:02  
          inet addr:172.21.0.2  Bcast:0.0.0.0  Mask:255.255.0.0
          inet6 addr: fe80::42:acff:fe15:2/64 Scope:Link
          UP BROADCAST RUNNING MULTICAST  MTU:1450  Metric:1
          RX packets:13 errors:0 dropped:0 overruns:0 frame:0
          TX packets:8 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:0 
          RX bytes:1038 (1.0 KiB)  TX bytes:648 (648.0 B)

eth1      Link encap:Ethernet  HWaddr 02:42:AC:12:00:01  
          inet addr:172.18.0.1  Bcast:0.0.0.0  Mask:255.255.0.0
          inet6 addr: fe80::42:acff:fe12:1/64 Scope:Link
          UP BROADCAST RUNNING MULTICAST  MTU:1500  Metric:1
          RX packets:15 errors:0 dropped:0 overruns:0 frame:0
          TX packets:8 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:0 
          RX bytes:1206 (1.1 KiB)  TX bytes:648 (648.0 B)

lo        Link encap:Local Loopback  
          inet addr:127.0.0.1  Mask:255.0.0.0
          inet6 addr: ::1/128 Scope:Host
          UP LOOPBACK RUNNING  MTU:65536  Metric:1
          RX packets:0 errors:0 dropped:0 overruns:0 frame:0
          TX packets:0 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:0 
          RX bytes:0 (0.0 B)  TX bytes:0 (0.0 B)
```

可以看到，容器vm33内部也有2个网络接口。注意eth0的IP。

这时，如果底层的网络不支持组播（vxlan需要通过三层组播来实现overlay network的二层(ARP)广播），从vm33并不能ping通vm3。我们可以为bridge配置静态mac转发表项，来实现达到目的。

```sh
[root@yy3 ~]# ip netns exec 1-14ed035dc7 ip -d link show vxlan1
4: vxlan1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue master br0 state UNKNOWN mode DEFAULT 
    link/ether 02:b0:0c:20:d0:54 brd ff:ff:ff:ff:ff:ff promiscuity 1 
vxlan id 256 srcport 0 0 dstport 4789 proxy l2miss l3miss ageing 300
[root@yy3 ~]# ip netns exec 1-14ed035dc7 bridge fdb show dev vxlan1
02:b0:0c:20:d0:54 vlan 1 permanent
02:b0:0c:20:d0:54 permanent
[root@yy3 ~]# ip netns exec 1-14ed035dc7 bridge fdb add 02:42:AC:15:00:01 dev vxlan1 dst 10.193.6.36
```

其中02:42:AC:15:00:01为vm3的MAC address。

```sh
[root@yy3 ~]# ip netns exec 1-14ed035dc7 bridge fdb show dev vxlan1
02:b0:0c:20:d0:54 vlan 1 permanent
02:b0:0c:20:d0:54 permanent
02:42:ac:15:00:01 dst 10.193.6.36 self permanent
```

为vm33配置vm3的静态MAC

```sh
[root@yy3 ~]# ip netns exec fe8daa8d5438 arp -s 172.21.0.1  02:42:ac:15:00:01
```

在yy2上为vm3配置vm33的静态MAC转发表：

```sh
[root@yy2 ~]# ip netns exec 1-14ed035dc7 bridge fdb add 02:42:AC:15:00:02 dev vxlan1 dst 10.193.6.37
[root@yy2 ~]# ip netns exec a1833405369e arp -s 172.21.0.2  02:42:AC:15:00:02
```

其中02:42:AC:15:00:02为vm33的MAC address。

然后，vm3和vm33就可以相互ping通了，In vm33：

```sh
/ # ping vm3
PING vm3 (172.21.0.1): 56 data bytes
64 bytes from 172.21.0.1: seq=0 ttl=64 time=0.593 ms
64 bytes from 172.21.0.1: seq=1 ttl=64 time=0.667 ms
64 bytes from 172.21.0.1: seq=2 ttl=64 time=0.541 ms
```

# 网络结构

![](/assets/2015-10-10-docker-overlay-network-practice-network.jpg)

# vxlan协议

![](/assets/2015-10-10-docker-overlay-network-practice-protocal.png)

# 相关资料

* [libnetwork](https://github.com/docker/libnetwork)
* [libnetwork design doc](https://github.com/docker/libnetwork/blob/master/docs/design.md)
* [Docker container networking](https://github.com/docker/docker/blob/246072e8c5184c78edc9f40435987a7ba26baaa8/docs/userguide/dockernetworks.md)
* [libnetwork overlay driver](https://github.com/docker/libnetwork/blob/master/docs/overlay.md)
* [kernel vxlan doc](https://www.kernel.org/doc/Documentation/networking/vxlan.txt)
