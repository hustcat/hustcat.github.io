---
layout: post
title:  Kubernetes解析：Minion的添加与删除
date: 2014-11-05 23:40:30
categories: Linux
tags: docker kubernetes
excerpt: "当你第一次使用Kubernetes时，一定会问题：如何向集群添加一个新的Minion？目前，关于Minion API，github上还没有文档介绍，这里简单总结一下。"
---

集群中的Minion动态的增加、删除是实际运营的一个基本要求，API server已经提供了相关RESTful接口。0.4以前，Minion的信息是保存在API server本地内存的，从0.4开始，将Minion的信息移到etcd，同时将Minion与Cloud Provider的交互从API server独立出来，参考[Separate minion controller from master. #1997](https://github.com/GoogleCloudPlatform/kubernetes/issues/2164)。今天，看到社区有人提出改进Minion的接口，参考[Improve minion interface #2164](https://github.com/GoogleCloudPlatform/kubernetes/issues/2164)。相信这块后面还会有变化。

增加Minion
------
我们考虑增加一个新的minion yy3(10.193.6.37)。

向apiserver添加minion，直接通过curl:

```sh
# curl -s -L http://10.193.6.35:8080/api/v1beta1/minions -XPOST -d '{"id":"10.193.6.37","Kind":"Minion"}'  
{"kind":"Status","creationTimestamp":null,"apiVersion":"v1beta1","status":"working","reason":"working","details":{"id":"3","kind":"operation"}}

# kubecfg list minions
Minion identifier
----------
10.193.6.36
10.193.6.37
```

也可以通过kubecfg增加minion

```sh
[yy@yy1 ~]$ kubecfg list minions
Minion identifier
----------
172.16.213.140
[yy@yy1 ~]$ kubecfg -c minion.json create minions
Minion identifier
----------
172.16.213.141

[yy@yy1 ~]$ kubecfg list minions
Minion identifier
----------
172.16.213.140
172.16.213.141
```

Minion描述文件：

```sh
[yy@yy1 ~]$ cat minion.json 
{
 "id":"172.16.213.141",
 "kind":"Minion",
 "apiVersion":"v1beta1",
 "hostIP": "172.16.213.141",
 "resources":{
    "capacity":{
        "cpu":1000,
        "memory":536870912
    }
 },
 "labels":{
    "type": "A5"
 }
}

[yy@yy1 ~]$ ./etcdctl get /registry/minions/172.16.213.141
{"kind":"Minion","id":"172.16.213.141","creationTimestamp":"2014-11-05T16:42:36Z","apiVersion":"v1beta1","hostIP":"172.16.213.141","resources":{"capacity":{"cpu":1000,"memory":536870912}},"labels":{"type":"A5"}}
```
最近，Minion增加了资源描述，以及Labels字段，这样，让业务可以指定Label，给调度器更多的提示。

删除minion
------
这里只演示用kubecfg来完成。

```sh
[yy@yy1 ~]$ kubecfg list minions
Minion identifier
----------
172.16.213.140
172.16.213.141

[yy@yy1 ~]$ kubecfg delete  minions/172.16.213.141
Status
----------
Success

[yy@yy1 ~]$ kubecfg list minions
Minion identifier
----------
172.16.213.140
```
