---
layout: post
title:  Kubernetes解析：services
categories: Linux
tags: docker kubernetes
---

Kubernetes从0.4开始，开发很快，变化也很大。这里谈谈services的一些变化。
之前的kubernetes services，kube-proxy在host上监听service指定的端口，然后由kube-proxy将请求转到后端具体的pods。这有一些问题：

（1）最大的问题，由于kube-proxy在host上监听端口，这样pod内的容器在expose时，就不能使用该host的该端口；而且service之间可能冲突；

（2）环境变量不能动态更新，所以pods不能知道在它之后启动的service的信息。

为此，kubernetes重新设计了services，让每个service都有一个自己的IP，但这并不是一个真正IP，而是通过iptables实现一个虚拟的IP。每个节点都生成这样一条类似的iptables规则：

```sh
-A KUBE-PROXY -d ${service-ip}/32 -p tcp -m comment --comment apache-service -m tcp --dport ${service-port} -j REDIRECT --to-ports ${kube-proxy-port}
```

由于kube-proxy-port是随机生成，大大减少了host port冲突的机会。另外，由于每个service都有一个自己的IP，所以service之间冲突也没有了，不同的service可以使用相同的port。

考虑3个节点：

yy1: 172.16.213.138 (master)

yy2: 172.16.213.140 (minion)

yy3: 172.16.213.141 (minion)

apiserver运行参数

```sh
＃kube-apiserver --logtostderr=true --v=0 --etcd_servers=http://172.16.213.138:4001 --address=0.0.0.0 --port=8080 --machines=172.16.213.140,172.16.213.141 --minion_port=10250 --allow_privileged=true --portal_net=10.11.0.0/16
```

定义services

```sh
$ cat apache-service.json
{
  "id": "apache-service",
  "kind": "Service",
  "apiVersion": "v1beta1",
  "port": 12345,
  "containerPort": 80,
  "selector": {
    "name": "apache"
  }
}
```

port为services的端口，kubernetes会为该service选择一个portal_net子网内的一个IP。
如下：

```sh
$ kubecfg -c apache-service.json create services
ID                  Labels              Selector            IP                  Port
----------          ----------          ----------          ----------          ----------
apache-service                          name=apache         10.11.0.1           12345
```

可以看看yy2，yy3上iptables的变化。

yy2

```sh
[yy@yy2 ~]$ sudo iptables -nvL -t nat
Chain KUBE-PROXY (2 references)
 pkts bytes target     prot opt in     out     source               destination         
    1    60 REDIRECT   tcp  --  *      *       0.0.0.0/0            10.11.0.1            /* apache-service */ tcp dpt:12345 redir ports 33945

[yy@yy2 ~]$ sudo netstat -ltnp|grep 33945
tcp6       0      0 :::33945                :::*                    LISTEN      247/kube-proxy
```

yy3

```sh
[yy@yy3 ~]$ sudo iptables -nvL -t nat
Chain KUBE-PROXY (2 references)
 pkts bytes target     prot opt in     out     source               destination         
0     0 REDIRECT   tcp  --  *      *       0.0.0.0/0            10.11.0.1            /* apache-service */ tcp dpt:12345 redir ports 47163

[yy@yy3 ~]$ sudo netstat -ltnp|grep 47163
tcp6       0      0 :::47163                :::*                    LISTEN      255/kube-proxy
```

整体结构大致如下：
![](/assets/2014-11-03-kubernetes-services.png)

更多内容请参考
https://github.com/GoogleCloudPlatform/kubernetes/blob/master/docs/services.md
