---
layout: post
title: Getting started with Calico
date: 2016-06-16 20:00:30
categories: Network
tags: calico kubernetes
excerpt: Getting started with Calico
---

### Environment

```
172.17.42.30 kube-master
172.17.42.31 kube-node1
172.17.42.32 kube-node2
```

### Install Calico

```sh
# wget https://github.com/projectcalico/calico-containers/releases/download/v0.15.0/calicoctl
# chmod +x calicoctl
# mv calicoctl /usr/bin

# docker pull calico/node:v0.15.0
```

* Start calico on master and node:

```sh
[root@kube-node1 ~]# export ETCD_AUTHORITY=172.17.42.30:2379
[root@kube-node1 ~]# calicoctl node --ip=172.17.42.31               
Calico node is running with id: e234b4e9dce7ea62f81bc28327456766295ed2d7acc7b52a91d6b0ccc6dfee30


[root@kube-node2 ~]# export ETCD_AUTHORITY=172.17.42.30:2379
[root@kube-node2 ~]# calicoctl node --ip=172.17.42.32
Calico node is running with id: 256c9a45374ab44a67689aa55275523013b5b97ee7ac1f0af3cc51cba9d6fd24


[root@kube-master ~]# calicoctl status
calico-node container is running. Status: Up 13 minutes
Running felix version 1.3.0rc6

IPv4 BGP status
IP: 172.17.42.30    AS Number: 64511 (inherited)
+--------------+-------------------+-------+----------+-------------+
| Peer address |     Peer type     | State |  Since   |     Info    |
+--------------+-------------------+-------+----------+-------------+
| 172.17.42.31 | node-to-node mesh |   up  | 09:18:55 | Established |
| 172.17.42.32 | node-to-node mesh |   up  | 09:31:59 | Established |
+--------------+-------------------+-------+----------+-------------+

IPv6 BGP status
No IPv6 address configured.
```

### Install Calico CNI plugin

```sh
# mkdir -p /opt/cni/bin/
# wget -N -P /opt/cni/bin/ https://github.com/projectcalico/calico-cni/releases/download/v1.0.0/calico
# wget -N -P /opt/cni/bin/ https://github.com/projectcalico/calico-cni/releases/download/v1.0.0/calico-ipam


# mkdir -p /etc/cni/net.d
# cat >/etc/cni/net.d/10-calico.conf <<EOF
{
    "name": "calico-k8s-network",
    "type": "calico",
    "etcd_authority": "172.17.42.30:2379",
    "log_level": "info",
    "ipam": {
        "type": "calico-ipam"
    }
}
EOF
```

* Start kubelet with Calico plugin

```sh
# kubelet --register-node=false --logtostderr=true --v=0 --api-servers=https://kube-master:443 --address=0.0.0.0 --hostname-override=kube-node2 --allow-privileged=true --kubeconfig=/etc/kubernetes/kubelet.kubeconfig --config=/etc/kubernetes/manifests --cluster-dns=10.254.0.10 --cluster-domain=cluster.local --network-plugin=cni --network-plugin-dir=/etc/cni/net.d &>>/var/log/kubelet &
```

### Create pod

```sh
# cat sshd-1.yml 
apiVersion: v1
kind: Pod
metadata:
  name: sshd-1
spec:
  containers:
  - name: sshd-1
    image: dbyin/sshd:1.0

# kubectl create -f ./sshd-1.yml 
pod "sshd-1" created

# kubectl describe pods sshd-1
Name:           sshd-1
Namespace:      default
Image(s):       dbyin/sshd:1.0
Node:           kube-node2/172.17.42.32
Start Time:     Thu, 16 Jun 2016 09:59:28 +0000
Labels:         <none>
Status:         Running
Reason:
Message:
IP:             192.168.0.64
Controllers:    <none>
Containers:
  sshd-1:
    Container ID:       docker://8c07c4b166b4dd42424cc60291bba07dee865823191b9a30240bb43e3249d87e
    Image:              dbyin/sshd:1.0
    Image ID:           docker://sha256:d17dc00a1f8e36be500750f59b21ecca43f3403ecbbf58188f087bfb7db2c8ef
    QoS Tier:
      memory:           BestEffort
      cpu:              BestEffort
    State:              Running
      Started:          Thu, 16 Jun 2016 09:59:29 +0000
    Ready:              True
    Restart Count:      0
    Environment Variables:
Conditions:
  Type          Status
  Ready         True 
Volumes:
  default-token-ekfrr:
    Type:       Secret (a secret that should populate this volume)
    SecretName: default-token-ekfrr
No events.
```

* On `node2`

```sh
[root@kube-node2 ~]# ip route show
default via 172.17.42.1 dev eth0  
172.17.0.0/16 dev eth0  proto kernel  scope link  src 172.17.42.32 
192.168.0.0/26 via 172.17.42.31 dev eth0  proto bird 
192.168.0.64 dev cali03adc9f233a  scope link 
blackhole 192.168.0.64/26  proto bird

[root@kube-node2 ~]# ip a
40: cali03adc9f233a: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP qlen 1000
    link/ether 96:fb:9e:a5:2f:88 brd ff:ff:ff:ff:ff:ff
    inet6 fe80::94fb:9eff:fea5:2f88/64 scope link 
       valid_lft forever preferred_lft forever
```

`cali03adc9f233a` is veth pair of container `sshd-1(192.168.0.64)`.

* ping from `node2` to `sshd-1`

```sh
[root@kube-node2 ~]# ping -c 3 192.168.0.64 
PING 192.168.0.64 (192.168.0.64) 56(84) bytes of data.
64 bytes from 192.168.0.64: icmp_seq=1 ttl=64 time=0.055 ms
64 bytes from 192.168.0.64: icmp_seq=2 ttl=64 time=0.070 ms
64 bytes from 192.168.0.64: icmp_seq=3 ttl=64 time=0.065 ms
```

* On `node1`

```sh
[root@kube-node1 ~]# ip route show
default via 172.17.42.1 dev eth0 
172.17.0.0/16 dev eth0  proto kernel  scope link  src 172.17.42.31 
blackhole 192.168.0.0/26  proto bird 
192.168.0.64/26 via 172.17.42.32 dev eth0  proto bird
```

* ping from `node1` to `sshd-1`

```sh
[root@kube-node1 ~]# ping -c 3 192.168.0.64
PING 192.168.0.64 (192.168.0.64) 56(84) bytes of data.
64 bytes from 192.168.0.64: icmp_seq=1 ttl=63 time=0.233 ms
64 bytes from 192.168.0.64: icmp_seq=2 ttl=63 time=0.195 ms
64 bytes from 192.168.0.64: icmp_seq=3 ttl=63 time=0.213 ms
```

### Reference

* [Ubuntu Nodes with Calico](http://kubernetes.io/docs/getting-started-guides/ubuntu-calico/)
* [calicoctl command line interface user reference](https://github.com/projectcalico/calico-containers/blob/master/docs/calicoctl.md)
* [Calico for containers](https://github.com/projectcalico/calico-containers)
