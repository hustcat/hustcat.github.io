---
layout: post
title: LVS practice
date: 2016-02-11 18:00:30
categories: Linux
tags: LVS
excerpt: LVS practice
---

# Installation

```
[root@lvs-director1 ~]# yum install ipvsadm keepalived -y

[root@lvs-director1 ~]# systemctl start ipvsadm

### clear tables
[root@lvs-director1 ~]# ipvsadm -C    
[root@lvs-director1 ~]# ipvsadm -l
IP Virtual Server version 1.2.1 (size=4096)
Prot LocalAddress:Port Scheduler Flags
  -> RemoteAddress:Port           Forward Weight ActiveConn InActConn

```

# NAT mode

## Environment

```
client 172.17.42.110/16
director1 172.17.42.111/16(eth0) 172.18.42.111/16(eth1)
rs2 172.18.42.121/16
```

如下:

```
        ____________
       |            |
       |  client    |
       |____________|                     
     CIP=172.17.42.110 (eth0)             
              |                           
              |                           
     VIP=172.17.42.200 
        ____________
       |            |
       |  director  |
       |____________|
     DIP=172.18.42.200
              |
              |
     RIP=1172.18.42.121/16 (eth0)
        _____________
       |             |
       | realserver  |
       |_____________|
```

## director配置

* /etc/sysctl.conf

```
net.ipv4.ip_forward=1
```

sysctl -p

* keepalived

/etc/keepalived/keepalived.conf

```
global_defs {
   notification_email {
     acassen@firewall.loc
     failover@firewall.loc
     sysadmin@firewall.loc
   }
   notification_email_from Alexandre.Cassen@firewall.loc
   smtp_server 192.168.200.1
   smtp_connect_timeout 30
   router_id LVS_DEVEL
}

vrrp_sync_group VG1 { 
   group { 
      VI_1 
      VI_GATEWAY 
   } 
} 

vrrp_instance VI_1 {
    state MASTER
    interface eth0
    virtual_router_id 51
    priority 100
    advert_int 1
    authentication {
        auth_type PASS
        auth_pass 1111
    }
    virtual_ipaddress {
        172.17.42.200
    }
}

vrrp_instance VI_GATEWAY { 
        state MASTER 
        interface eth1 
        lvs_sync_daemon_inteface eth1 
        virtual_router_id 52 
        priority 150 
        advert_int 1 
        smtp_alert 
        authentication { 
                auth_type PASS 
                auth_pass 1111
        } 
        virtual_ipaddress { 
                172.18.42.200
        } 
} 

virtual_server 172.17.42.200 3306 {
    delay_loop 6
    lb_algo rr
    lb_kind NAT
    nat_mask 255.255.0.0
    persistence_timeout 50
    protocol TCP

    real_server 172.18.42.121 3306 {
        weight 1
        TCP_CHECK {
                connect_timeout 3
                nb_get_retry 3
                delay_before_retry 3
                connect_port 22
            }
        }
    }
}
```



```
[root@lvs-director1 ~]# systemctl start keepalived
[root@lvs-director1 ~]# ipvsadm -l
IP Virtual Server version 1.2.1 (size=4096)
Prot LocalAddress:Port Scheduler Flags
  -> RemoteAddress:Port           Forward Weight ActiveConn InActConn
TCP  172.17.42.200:mysql rr persistent 50
  -> 172.18.42.121:mysql          Masq    1      1          0

[root@lvs-director1 ~]# ip a
2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP qlen 1000
    link/ether 52:54:60:12:71:d5 brd ff:ff:ff:ff:ff:ff
    inet 172.17.42.111/16 brd 172.17.255.255 scope global eth0
       valid_lft forever preferred_lft forever
    inet 172.17.42.200/32 scope global eth0
       valid_lft forever preferred_lft forever
    inet6 fe80::5054:60ff:fe12:71d5/64 scope link 
       valid_lft forever preferred_lft forever
3: eth1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP qlen 1000
    link/ether 52:54:61:12:72:d5 brd ff:ff:ff:ff:ff:ff
    inet 172.18.42.111/16 brd 172.18.255.255 scope global eth1
       valid_lft forever preferred_lft forever
    inet 172.18.42.200/32 scope global eth1
       valid_lft forever preferred_lft forever
    inet6 fe80::5054:61ff:fe12:72d5/64 scope link 
       valid_lft forever preferred_lft forever
```

## RS配置

设置rs的网关为DIP(172.18.42.200):

```
ip route add default via 172.18.42.200 dev eth1
```

## packet

*** rs ***

![](/assets/2016-02-11-lvs-practice-nat-rs.png)

[more...](/assets/lvs/2016-02-11-lvs-practice-nat-rs.cap)

*** director ***

![](/assets/2016-02-11-lvs-practice-nat-ld.png)


LVS-NAT更多信息参考[5. LVS: LVS-NAT](http://www.austintek.com/LVS/LVS-HOWTO/HOWTO/LVS-HOWTO.LVS-NAT.html)

# TUNNAL

## Environment

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
         __________        |
        |          |       |
        | director |-------
        |__________|       |
       DIP=172.17.42.111   |
       (eth0)              |
                           |
   DIP->RIP(CIP->VIP) |    |
                      v    
          -------------------------------------
          |                |                  |
          |                |                  |
  RIP1=172.17.42.120 RIP2=172.17.42.121  RIP3=172.17.42.122 (eth0)
   VIP=172.17.42.200 VIP=172.17.42.200   VIP=172.17.42.200 (all tunl0,non-arping)
    _____________     _____________      _____________
   |             |   |             |    |             |
   | realserver  |   | realserver  |    | realserver  |
   |_____________|   |_____________|    |_____________|

```

## director配置

* VIP

* /etc/sysctl.conf

```
net.ipv4.ip_forward=1
net.ipv4.ip_nonlocal_bind = 1
```

sysctl -p

* keepalived

/etc/keepalived/keepalived.conf

```
...

vrrp_instance VI_1 {
    state MASTER
    interface eth0
    virtual_router_id 51
    priority 100
    advert_int 1
    authentication {
        auth_type PASS
        auth_pass 1111
    }
    virtual_ipaddress {
        172.17.42.200
    }
}

virtual_server 172.17.42.200 3306 {
    delay_loop 6
    lb_algo rr
    lb_kind TUN
    nat_mask 255.255.0.0
    persistence_timeout 50
    protocol TCP

    real_server 172.17.42.120 3306 {
        weight 1
        TCP_CHECK {
                connect_timeout 3
                nb_get_retry 3
                delay_before_retry 3
                connect_port 3306
            }
        }
    }
}
```

## RS配置

* /etc/sysctl.conf

```
net.ipv4.conf.tunl0.arp_ignore = 1
net.ipv4.conf.tunl0.arp_announce = 2

net.ipv4.conf.all.rp_filter = 0
net.ipv4.conf.default.rp_filter = 0
net.ipv4.conf.lo.rp_filter = 0
net.ipv4.conf.eth0.rp_filter = 0
net.ipv4.conf.tunl0.rp_filter = 0
```

由于director与rs在同一个网段，需要处理[ARP问题](http://www.austintek.com/LVS/LVS-HOWTO/HOWTO/LVS-HOWTO.LVS-Tun.html#arp_problem_lvs-tun)。arp_ignore/arp_announce参考[这里](http://www.austintek.com/LVS/LVS-HOWTO/HOWTO/LVS-HOWTO.arp_problem.html#the_problem)。rp_filter参数参考[这里](http://blog.hellosa.org/2011/02/23/kernel-2-6-32-lvs-rs.html).

* 创建tunnel设备

```
modprobe ipip
```

/etc/sysconfig/network-scripts/tunl0

```
DEVICE=tunl0
TYPE=ipip
IPADDR=172.17.42.200
NETMASK=255.255.255.255
ONBOOT=yes 
```

```
# ifup tunl0

# ip a
4: tunl0: <NOARP,UP,LOWER_UP> mtu 0 qdisc noqueue state UNKNOWN 
    link/ipip 0.0.0.0 brd 0.0.0.0
    inet 172.17.42.200/32 brd 172.17.42.200 scope global tunl0
       valid_lft forever preferred_lft forever
```

# 测试

启动keepalived

```
[root@lvs-director1 ~]# systemctl start keepalived
[root@lvs-director1 ~]# ipvsadm -l
IP Virtual Server version 1.2.1 (size=4096)
Prot LocalAddress:Port Scheduler Flags
  -> RemoteAddress:Port           Forward Weight ActiveConn InActConn
TCP  172.17.42.200:mysql rr persistent 50
  -> 172.17.42.120:mysql          Tunnel  1      0          0
```

```
[root@lvs-client ~]# telnet 172.17.42.200 3306
Trying 172.17.42.200...
Connected to 172.17.42.200.
Escape character is '^]'.
^]
telnet> quit
```

* packet

*** director ***

![](/assets/2016-02-11-lvs-practice-ld.jpg)

*** rs ***

![](/assets/2016-02-11-lvs-practice-rs.jpg)

从上面可以看到，从client发到rs的packet都会经过director，而从rs返回给client的包，没有经过director。

关于LVS-TUN更多的信息参考[这里](http://www.austintek.com/LVS/LVS-HOWTO/HOWTO/LVS-HOWTO.LVS-Tun.html).


# LVS与Docker

很多时候，我们希望用LVS来对Docker容器进行负载均衡，那么面临一个问题，tunnel隧道如何创建呢？

有两种选择，一种是在容器内部建立。这种方式有两个问题：一是需要容器必须使用flat network；另外，在容器内部动态创建隧道也不方便，也会影响镜像的发布。

第二种选择就是在Host创建，然后转发给容器。这种方式，容器本身不需要做任何配置，更加方便，而且不也需要flat network。

对于第二种方式，我们假设Host为172.17.42.40/16，容器使用bridge方式，容器的IP为172.18.0.2/16。我们只需要配置iptables就可以了：

```sh
# iptables -t nat -A PREROUTING -d $VIP/32 -p tcp --dport $VPORT -j DNAT --to-destination $CONTAINER_IP:$CONTAINER_PORT 
```

比如:

```sh
# iptables -t nat -A PREROUTING -d 172.17.42.200/32 -p tcp --dport 36000 -j DNAT --to-destination 172.18.0.2:22
```

***  /etc/keepalived/keepalived.conf ***

```
virtual_server 172.17.42.200 36000 {
    delay_loop 6
    lb_algo rr
    lb_kind TUN
    nat_mask 255.255.0.0
    persistence_timeout 50
    protocol TCP

    real_server 172.17.42.40 36000 {
        weight 1
        TCP_CHECK {
                connect_timeout 3
                nb_get_retry 3
                delay_before_retry 3
                connect_port 22
            }
        }
    }
}
```


# 其它相关资料

* [6. LVS: The ARP Problem](http://www.austintek.com/LVS/LVS-HOWTO/HOWTO/LVS-HOWTO.arp_problem.html#the_problem)
* [ipvsadm(8) - Linux man page](http://linux.die.net/man/8/ipvsadm)
* [Configure LVS (Linux Virtual Server)](http://www.server-world.info/en/note?os=CentOS_7&p=lvs)
* [LVS NAT + Keepalived HOWTO](http://keepalived.org/LVS-NAT-Keepalived-HOWTO.html)