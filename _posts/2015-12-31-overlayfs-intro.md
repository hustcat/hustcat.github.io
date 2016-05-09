---
layout: post
title: Docker storage driver history and overlayfs
date: 2015-12-31 18:00:30
categories: Linux
tags: docker
excerpt: Docker storage driver history and overlayfs
---

#storage driver history

Docker目前支持很多[graph driver](http://jpetazzo.github.io/assets/2015-03-03-not-so-deep-dive-into-docker-storage-drivers.html#1)，最开始使用AUFS，但AUFS一直没有进入内核主线。但RHEL/Fedora等发行版本并不支持AUFS，所以，Redhat的[Alexander Larsson](https://blogs.gnome.org/alexl/)实现了device-mapper的driver，现在dm driver由[Vincent Batts](https://github.com/vbatts)在维护。在[这篇文章](https://blogs.gnome.org/alexl/2013/10/15/adventures-in-docker-land/)中，Alexander详细介绍了他当时的想法。由于btrfs不成熟、overlayfs也没有进入内核主线，所以，他选择了device-mapper作为RHEL/Fedora下的解决方案。

## device mapper

在相当长一段时间，dm几乎成为生产环境的使用docker的唯一选择，但在实际中，经常会遇到很多问题。比如，你一定经常遇到下面的问题：

```
Driver devicemapper failed to remove root filesystem ... : Device is Busy
```
另外，想让DM工作稳定，需要udev的支持，而udev没有静态库。最后，Docker希望通过容器之间共享pagecache，试想，如果一台机器上有几百个容器，如果每个容器都打开一份glibc，这会浪费许多内存。由于DM工作在块层，很难实现pagecache的共享。

因此，很多人都不建议在生产环境使用DM（[1](http://batmat.net/2015/08/26/docker-storage-driver-dont-use-devicemapper/)、[2](http://www.projectatomic.io/blog/2015/06/notes-on-fedora-centos-and-docker-storage-drivers/)）。

个人在使用DM的过程中也遇到一些问题，包括导致内核crash的问题（[1](https://github.com/docker/docker/issues/9862)，[2](https://www.redhat.com/archives/dm-devel/2015-May/msg00113.html)）。但总的来说，还是比较稳定的，这可能是因为我们单机支持的容器数量不会太多的缘故吧。

## btrfs

再后来社区实现了btrfs driver。但btrfs在稳定性、性能上都存在一些[问题](https://lwn.net/Articles/627232/)。


## overlayfs

在内核[3.18](http://kernelnewbies.org/Linux_3.18#head-f514a511bf32b818dbde50c24a51fb095e81cc8e)中，[overlayfs](https://www.kernel.org/doc/Documentation/filesystems/overlayfs.txt)终于正式进入主线。相比AUFS，overlayfs设计简单，代码也很少。而且可以实现pagecache共享。似乎是一个非常好的选择。于是，在这之后，docker社区开始转向将overlayfs作为第一选择（[1](https://github.com/docker/docker/pull/12354)）。


# docker overlay

## intro

Docker使用overlayfs的lowerdir指向image layer，使用upperdir指向container layer，merged将lowerdir与upperdir整合起来提供统一视图给容器，作为根文件系统。如下：

![](/assets/2015-12-31-docker-overlayfs-intro-1.jpg)

lowerdir与upperdir可以包含相同的文件，upperdir会隐藏lowerdir的文件。

*** read file ***

在容器内读文件时，如果upperdir（container layer）存在，就从container layer读取；如果不存在，就从lowerlay（image layer）读取。


*** write file ***

写容器内文件时，如果upperdir不存在，overlay则会发起copy_up操作，从lowerdir拷贝文件到upperdir。由于拷贝发生文件系统层面，而不是块层，会拷贝整个文件，即使只修改文件很小一部分。如果文件很大，会导致效率低下。但好在拷贝只会在第一次打开时发生。另外，由于overlay只有2层，所以性能影响也很小。

*** deleting files and directories ***

删除容器内文件时，upperdir会创建一个whiteout文件，它会隐藏lowerdir的文件（不会删除）。同样，删除目录时，upperdir会创建一个opaque directory，隐藏lowerdir的目录。

## practice

下载docker[最新的版本](https://docs.docker.com/engine/installation/binaries/)。

```sh
#docker daemon --storage-driver=overlay
# docker info
Containers: 0
Images: 0
Server Version: 1.9.1
Storage Driver: overlay
 Backing Filesystem: extfs
...

# docker images  -a
REPOSITORY          TAG                 IMAGE ID            CREATED             VIRTUAL SIZE
centos              centos6             1a895dd3954a        11 weeks ago        190.6 MB
<none>              <none>              366219586e86        11 weeks ago        190.6 MB
<none>              <none>              501f51238f9e        11 weeks ago        190.6 MB
<none>              <none>              ebdbe10e9b33        11 weeks ago        190.6 MB
<none>              <none>              fa5be2806d4c        3 months ago        0 B

# ls /var/lib/docker/overlay/
1a895dd3954aede5ea9e6bc23d23e8b1f6040df94647d83e71f96d60131d3235  ebdbe10e9b3379125ce3c105cb711f80afdc22a5adac56f0045bc2c19f08887c
366219586e86f21918abb0571e668eb702b506d825702856539515ba2ac4be52  fa5be2806d4c9aa0f75001687087876e47bb45dc8afb61f0c0e46315500ee144
501f51238f9ef52bcb6aecb6e2c1c04b3f8607c855d9b2cf7da780946ce02ec2

# ls /var/lib/docker/overlay/1a895dd3954aede5ea9e6bc23d23e8b1f6040df94647d83e71f96d60131d3235/root/
bin  dev  etc  home  lib  lib64  lost+found  media  mnt  opt  proc  root  sbin  selinux  srv  sys  tmp  usr  var
```

可以看到，每个layer对应一个目录。

创建一个容器

```sh
# docker run -it centos:centos6 /bin/bash
[root@b90c75273b11 /]#


# ls /var/lib/docker/overlay/b90c75273b116a4dac754f425380012bbdf90d098cdbc829de3691f857137435
lower-id  merged  upper  work
# cat /var/lib/docker/overlay/b90c75273b116a4dac754f425380012bbdf90d098cdbc829de3691f857137435/lower-id 
1a895dd3954aede5ea9e6bc23d23e8b1f6040df94647d83e71f96d60131d3235

# cat /proc/mounts
overlay /var/lib/docker/overlay/b90c75273b116a4dac754f425380012bbdf90d098cdbc829de3691f857137435/merged overlay rw,relatime,lowerdir=/var/lib/docker/overlay/1a895dd3954aede5ea9e6bc23d23e8b1f6040df94647d83e71f96d60131d3235/root,upperdir=/var/lib/docker/overlay/b90c75273b116a4dac754f425380012bbdf90d098cdbc829de3691f857137435/upper,workdir=/var/lib/docker/overlay/b90c75273b116a4dac754f425380012bbdf90d098cdbc829de3691f857137435/work 0 0
```

可以看到，容器对应的目录有3个目录（merged，upper，work），work目录用于overlayfs实现copy_up操作，lower-id保存image ID。

*** 创建文件 ***

在容器创建一个文件：

```sh
[root@b90c75273b11 ~]# echo "hello" > /root/f1.txt
[root@b90c75273b11 ~]# ls /root/ 
anaconda-ks.cfg  f1.txt  install.log  install.log.syslog
```

overlay目录变化：

```sh
[root@yy1 ~]# ls /var/lib/docker/overlay/b90c75273b116a4dac754f425380012bbdf90d098cdbc829de3691f857137435/merged/root/
anaconda-ks.cfg  f1.txt  install.log  install.log.syslog
[root@yy1 ~]# ls /var/lib/docker/overlay/b90c75273b116a4dac754f425380012bbdf90d098cdbc829de3691f857137435/upper/root/
f1.txt
[root@yy1 ~]# ls /var/lib/docker/overlay/1a895dd3954aede5ea9e6bc23d23e8b1f6040df94647d83e71f96d60131d3235/root/root/
anaconda-ks.cfg  install.log  install.log.syslog
```

可以看到文件出现在upper目录。


*** 删除文件 ***

在容器删除一个文件：

```sh
[root@b90c75273b11 ~]# rm /root/install.log
[root@b90c75273b11 ~]# ls /root/
anaconda-ks.cfg  f1.txt  install.log.syslog
```

```sh
[root@yy1 ~]# ls /var/lib/docker/overlay/b90c75273b116a4dac754f425380012bbdf90d098cdbc829de3691f857137435/merged/root/
anaconda-ks.cfg  f1.txt  install.log.syslog

[root@yy1 ~]# ls /var/lib/docker/overlay/b90c75273b116a4dac754f425380012bbdf90d098cdbc829de3691f857137435/upper/root/* -l
-rw-r--r-- 1 root root    6 12月 31 17:55 /var/lib/docker/overlay/b90c75273b116a4dac754f425380012bbdf90d098cdbc829de3691f857137435/upper/root/f1.txt
c--------- 1 root root 0, 0 12月 31 18:01 /var/lib/docker/overlay/b90c75273b116a4dac754f425380012bbdf90d098cdbc829de3691f857137435/upper/root/install.log

[root@yy1 ~]# ls /var/lib/docker/overlay/1a895dd3954aede5ea9e6bc23d23e8b1f6040df94647d83e71f96d60131d3235/root/root/
anaconda-ks.cfg  install.log  install.log.syslog
```
可以看到upper目录多了一个“install.log”文件。

# 总结

总的来说，overlayfs的原理和实现都非常简单，逐渐成为Docker在生产环境，特别是在高密度容器场景下的最好的选择。

# 相关资料

* [CoreOS looks to move from Btrfs to overlayfs](https://lwn.net/Articles/627232/)
* [Overlayfs issues and experiences](http://lwn.net/Articles/636943/)
* [Docker and OverlayFS in practice](https://docs.docker.com/engine/userguide/storagedriver/overlayfs-driver/)
* [Comprehensive Overview of Storage Scalability in Docker](https://developerblog.redhat.com/2014/09/30/overview-storage-scalability-docker/)
* [Docker with OverlayFS first impressions](http://blog.cloud66.com/docker-with-overlayfs-first-impression/)


