---
layout: post
title: Build gateway for LVS-TUN base on IPIP tunnel
date: 2016-02-16 18:00:30
categories: Linux
tags: LVS 
excerpt: Build gateway for LVS-TUN base on IPIP tunnel
---

# Introduce

By default, real server return packet to client directly for LVS-TUN, and so real server need an external IP. We can build a gateway for real server base on IPIP tunnel without external IP.

# Topology

```
                        ________
                       |        |
                       | client |
                       |________|
                       CIP=172.17.42.110/16
                           |
             CIP->VIP |    |   ^  
                      v    |   | VIP->CIP
                           |
       VIP=172.17.42.200   |
       (eth0:1, arps)      |
         __________        |       _________
        |          |       |      |         |
        |  LD-IN   |--------------|  LD-OUT |
        |__________|       |      |_________|
       DIP=172.17.42.111   |       GIP=172.17.42.112
       (eth0)              |
                           |
   DIP->RIP(CIP->VIP) |    |    ^
                      v    |    | RIP->GIP(VIP->CIP)
                           |  
                     RIP=172.17.42.120
                     VIP=172.17.42.200        192.168.10.1          VMIP: 192.168.10.10
                      _____________        ________________          _________
                     |             |      |             ___|___     |   VM    |
                     | rs: eth1    |------|   rs: br1  |_vethA_|----|   vethB |
                     |_____________|      |________________|        |_________|
                            CIP->VIP   =========DNAT===========>        CIP->VMIP
                            VMIP->CIP  <========================

[root@rs ~]# ip tunnel add tunl1 mode ipip remote 172.17.42.112 local 172.17.42.120 dev eth1
[root@rs ~]# ip route add default dev tunl1 table 1                                         
[root@rs ~]# ip rule add from $VIP table 1

[root@rs ~]# iptables -t nat -A PREROUTING -d $VIP/32 -p tcp --dport $PORT -j DNAT --to-destination $CONTAINER:$CONTAINER_PORT

当RS的br1收到VM的reply packet(VMIP->CIP)时，交给上层IP层，然后根据路由选择出口设备eth1（因为这时的源地址为VMIP，所以不会选择tunl1），最后在POST_ROUTING，完成转换（VIP->CIP）。内核只会在PRE_ROUTING之后和LOCAL_OUT之前进行路由，POST_ROUTING之后不会再进行路由了。

这与packet直接从rs(VIP)发出是有很大区别的。
```

# LD-IN config

Refer to [here](http://hustcat.github.io/lvs-pracice).

# RS config

```sh
[root@lvs-rs1 ~]# ip tunnel add tunl1 mode ipip remote 172.17.42.112 local 172.17.42.120 dev eth1
[root@lvs-rs1 ~]# ip link set tunl1 up
[root@lvs-rs1 ~]# ip route add default dev tunl1 table 1                                         
[root@lvs-rs1 ~]# ip rule add from 172.17.42.200 table 1


[root@lvs-rs1 ~]# ip a
4: tunl0: <NOARP,UP,LOWER_UP> mtu 0 qdisc noqueue state UNKNOWN 
    link/ipip 172.17.42.120 brd 172.17.42.112
    inet 172.17.42.200/32 brd 172.17.42.200 scope global tunl0
       valid_lft forever preferred_lft forever
7: tunl1@eth0: <POINTOPOINT,NOARP,UP,LOWER_UP> mtu 1480 qdisc noqueue state UNKNOWN 
    link/ipip 172.17.42.120 peer 172.17.42.112
```

# LD-OUT config

```sh
[root@lvs-director2 ~]# ip tunnel add tunl1 mode ipip remote 172.17.42.120 local 172.17.42.112 dev eth0
[root@lvs-director2 ~]# ip link set tunl1 up
[root@lvs-director2 ~]# ip a
4: tunl0: <NOARP> mtu 0 qdisc noop state DOWN 
    link/ipip 172.17.42.112 brd 172.17.42.120
7: tunl1@eth0: <POINTOPOINT,NOARP,UP,LOWER_UP> mtu 1480 qdisc noqueue state UNKNOWN 
    link/ipip 172.17.42.112 peer 172.17.42.120


[root@lvs-director2 ~]# echo 1 > /proc/sys/net/ipv4/ip_forward
```

/etc/sysctl.conf

```
net.ipv4.conf.all.rp_filter = 0
net.ipv4.conf.default.rp_filter = 0
net.ipv4.conf.lo.rp_filter = 0
net.ipv4.conf.eth0.rp_filter = 0
net.ipv4.conf.tunl0.rp_filter = 0
net.ipv4.conf.tunl1.rp_filter = 0
```

# packet flow

*** RS ***

![](/assets/lvs/2016-02-16-lvs-proxy-rs.png)

[more](/assets/lvs/2016-02-16-lvs-proxy-rs.cap).

*** LD-OUT ***

![](/assets/lvs/2016-02-16-lvs-proxy-ld-out.png)

# Related posts

* [13.1. Reverse Path Filtering](http://tldp.org/HOWTO/Adv-Routing-HOWTO/lartc.kernel.rpf.html)
* [在LVS上实现SNAT网关](http://tech.uc.cn/?p=2274)