---
layout: post
title: Docker与Ceph的结合：让Docker容器跑在网络存储之上
date: 2015-07-24 16:44:30
categories: Linux
tags: ceph
excerpt: Run docker on ceph cluster with rbd driver。
---

# 概述

Ceph的RBD支持快照，我们可以通过RBD，让Docker的rootfs也跑在网络存储之上，从而实现OS、数据全部跑在网络上，从而打造无本地磁盘的容器服务。

逻辑示意图：
![](/assets/2015-07-24-docker-on-ceph.jpg)

# Image共享

在host1启动docker

```sh
 [root@host1 ~]#docker -d -D -s rbd
```

在host1拉取镜像

```sh
[root@host1 ~]# docker pull centos:latest
Pulling repository centos
7322fbe74aa5: Download complete 
f1b10cd84249: Download complete 
c852f6d61e65: Download complete 
Status: Downloaded newer image for centos:latest
[root@host1 ~]# docker images
REPOSITORY          TAG                 IMAGE ID            CREATED             VIRTUAL SIZE
centos              centos7             7322fbe74aa5        5 weeks ago         178.2 MB
centos              latest              7322fbe74aa5        5 weeks ago         178.2 MB
centos              7    
```

查看所有rbd image

```sh
[root@host1 ~]# rbd list
docker_image_7322fbe74aa5632b33a400959867c8ac4290e9c5112877a7754be70cfe5d66e9
docker_image_base_image
docker_image_c852f6d61e65cddf1e8af1f6cd7db78543bfb83cdcd36845541cf6d9dfef20a0
docker_image_f1b10cd842498c23d206ee0cbeaa9de8d2ae09ff3c7af2723a9e337a6965d639
```

可以看到，有4个image，其中3个image对应centos:latest的3个layer。

在host2上看不到image信息。

```sh
[root@host2 ~]# docker images
REPOSITORY          TAG                 IMAGE ID            CREATED             VIRTUAL SIZE
[root@host2 ~]# ls /var/lib/docker/graph/
```

我们需要将host1上的graph的元数据信息拷贝到host2：

```sh
[root@host1 ~]# ls /var/lib/docker/graph/
7322fbe74aa5632b33a400959867c8ac4290e9c5112877a7754be70cfe5d66e9  c852f6d61e65cddf1e8af1f6cd7db78543bfb83cdcd36845541cf6d9dfef20a0
_tmp
```

还有/var/lib/docker/repositories-rbd文件。

这时，在host2上就可以看到image信息了：

```sh
[root@host2 ~]# docker -d -D -s rbd
[root@host2 ~]# docker images
REPOSITORY          TAG                 IMAGE ID            CREATED             VIRTUAL SIZE
centos              7                   7322fbe74aa5        5 weeks ago         178.2 MB
centos              centos7             7322fbe74aa5        5 weeks ago         178.2 MB
centos              latest              7322fbe74aa5        5 weeks ago         178.2 MB
```

# 容器存储共享

在host1上启动容器

```sh
[root@host1 ~]# docker run -it centos:latest               
[root@8a437ea74af1 /]# ls /root
anaconda-ks.cfg
[root@8a437ea74af1 /]# echo "hello ceph" > /root/hello.txt
[root@8a437ea74af1 /]# ls /root
anaconda-ks.cfg  hello.txt
``` 

查看rbd map信息：

```sh
[root@host1 ~]# rbd showmapped     
id pool image                                                                         snap device    
1  rbd  docker_image_8a437ea74af139bc9ae6b08218076df35083acd1dfbc4b204e7edb8417aeb225 -    /dev/rbd1
```

停止host1容器

```sh
[root@host1 ~]# docker ps  -a
CONTAINER ID        IMAGE               COMMAND             CREATED             STATUS                     PORTS               NAMES
8a437ea74af1        centos:7            "/bin/bash"         2 minutes ago       Exited (0) 9 seconds ago                       insane_turing   
```

将容器8a437ea74af1的元数据信息拷贝到host2

```sh
[root@host1 ~]# ls /var/lib/docker/containers/       
8a437ea74af139bc9ae6b08218076df35083acd1dfbc4b204e7edb8417aeb225
```

这时，就可以在host2上看到容器的信息了：

```sh
[root@host2 ~]# docker ps  -a
CONTAINER ID        IMAGE               COMMAND             CREATED             STATUS                     PORTS               NAMES
8a437ea74af1        centos:7            "/bin/bash"         5 minutes ago       Exited (0) 2 minutes ago                       desperate_poincare   
```

让我们启动容器8a437ea74af1

```sh
[root@host2 ~]# docker start -ia 8a437ea74af1
[root@8a437ea74af1 /]# ls /root/
anaconda-ks.cfg  hello.txt
[root@8a437ea74af1 /]# cat /root/hello.txt 
hello ceph
```

看到了吧，数据都过来了：）great!

# 下一步

从上面的示例中，可以看到，虽然image数据的存储到了ceph集群中，但是image的元数据、container的元数据仍然存储在本地，这会影响故障迁移，为了实现故障无数据迁移，需要进一步改造Docker，将image的元数据和container的元数据都进行集中存储。

# 其它

你可以从[这里](https://github.com/hustcat/docker-1.3.2/tree/rbd)获取Docker rbd storage driver的代码，更多信息请参考[#14800](https://github.com/docker/docker/pull/14800/)。
