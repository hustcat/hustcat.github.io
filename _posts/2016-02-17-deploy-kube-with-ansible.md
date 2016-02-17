---
layout: post
title: Deploy Kubernetes with ansible
date: 2016-02-17 15:00:30
categories: Linux
tags: Kubernetes 
excerpt: Deploy Kubernetes with ansible
---

# Environment

```
172.17.42.30 kube-master
172.17.42.31 kube-node1
172.17.42.32 kube-node2
```

# Compile kube

```sh
# export PATH=/usr/local/go_1.5.2/bin:$PATH
# export GOROOT=/usr/local/go_1.5.2/
[root@yy /data/go/src/github.com/kubernetes/kubernetes]# make

[root@yy /data/go/src/github.com/kubernetes/kubernetes]# ls _output/local/go/bin   
e2e.test       gendeepcopy  genman              integration              kube-proxy      kubelet    mungedocs
genbashcomp    gendocs      genswaggertypedocs  kube-apiserver           kube-scheduler  kubemark   src
genconversion  genkubedocs  hyperkube           kube-controller-manager  kubectl         linkcheck
```

# Config ansible

```
[root@kube-master ansible]# yum install ansible

# vi /etc/ansible/ansible.cfg
remote_port    = 36000
```

```sh
[root@kube-master ~]# git clone https://github.com/hustcat/contrib
# cd contrib/ansible
```

* inventory file

```sh
# cat inventory
[masters]
kube-master ansible_ssh_pass=xxx

[etcd]
kube-master ansible_ssh_pass=xxx

[nodes]
kube-node1 ansible_ssh_pass=xxx
kube-node2 ansible_ssh_pass=xxx
```

* group_vars/all.yml

```
http_proxy: "http:xxx"
https_proxy: "http:xxx"
cluster_logging: false
cluster_monitoring: false
```

# Install kube

Put kubernetes binary files to contrib/ansible/roles/_output/local/go/bin/:

```sh
# cp   _output/local/go/bin/*  contrib/ansible/roles/_output/local/go/bin/
[root@kube-master ansible]# ls roles/_output/local/go/bin/
e2e.test       gendeepcopy  genman              integration              kube-proxy      kubelet    mungedocs
genbashcomp    gendocs      genswaggertypedocs  kube-apiserver           kube-scheduler  kubemark   src
genconversion  genkubedocs  hyperkube           kube-controller-manager  kubectl         linkcheck
```

## etcd

```sh
[root@kube-master ansible]# ./setup.sh --tags=etcd
…
PLAY RECAP ******************************************************************** 
kube-master                : ok=19   changed=4    unreachable=0    failed=0


[root@kube-master ansible]# systemctl status etcd
etcd.service - Etcd Server
   Loaded: loaded (/usr/lib/systemd/system/etcd.service; enabled)
   Active: active (running) since Wed 2016-02-17 06:49:27 UTC; 45s ago
 Main PID: 2999 (etcd)
   CGroup: /system.slice/etcd.service
           `-2999 /usr/bin/etcd
…
```

## Kube-master

```sh
[root@kube-master ansible]# ./setup.sh --tags=masters
…
PLAY RECAP ******************************************************************** 
kube-master                : ok=51   changed=32   unreachable=0    failed=0   
```

Check services:

```sh
# systemctl status kube-apiserver
# systemctl status kube-scheduler
# systemctl status kube-controller-manager
```

## Kube-minion

```sh
[root@kube-master ansible]# ./setup.sh --tags=nodes
…
PLAY RECAP ******************************************************************** 
kube-node1                 : ok=43   changed=23   unreachable=0    failed=0   
kube-node2                 : ok=43   changed=23   unreachable=0    failed=0
```

check services:

```
# systemctl status kubelet
# systemctl status kube-proxy
```

## Check result

```sh
[root@kube-master ansible]# kubectl get nodes
NAME         LABELS                              STATUS     AGE
kube-node1   kubernetes.io/hostname=kube-node1   Ready      1m
kube-node2   kubernetes.io/hostname=kube-node2   Ready      1m
```

done!

# Related

* [Kubernetes Ansible](https://github.com/kubernetes/contrib/tree/master/ansible)
