---
layout: post
title: Cgroup namespace
date: 2016-11-21 20:00:30
categories: Linux
tags: cgroup namespace
excerpt: Cgroup namespace
---

## 简介

内核从[4.6](https://www.phoronix.com/scan.php?page=news_item&px=CGroup-Namespaces-Linux-4.6)开始，支持[cgroup namespace](https://lwn.net/Articles/618873/)。在这之前，在一个容器查看`/proc/$PID/cgroup`，或者在容器挂载cgroup时，会看到整个系统的cgroup信息：

```sh
# docker run -it --rm busybox /bin/sh
/ # cat /proc/self/cgroup 
8:blkio:/docker/a33bb329884b34cfbea83cdcdac8292976c1e2ed40b9089eb6cf4f257c464560
7:net_cls:/docker/a33bb329884b34cfbea83cdcdac8292976c1e2ed40b9089eb6cf4f257c464560
6:freezer:/docker/a33bb329884b34cfbea83cdcdac8292976c1e2ed40b9089eb6cf4f257c464560
5:devices:/docker/a33bb329884b34cfbea83cdcdac8292976c1e2ed40b9089eb6cf4f257c464560
4:memory:/docker/a33bb329884b34cfbea83cdcdac8292976c1e2ed40b9089eb6cf4f257c464560
3:cpuacct:/docker/a33bb329884b34cfbea83cdcdac8292976c1e2ed40b9089eb6cf4f257c464560
2:cpu:/docker/a33bb329884b34cfbea83cdcdac8292976c1e2ed40b9089eb6cf4f257c464560
1:cpuset:/docker/a33bb329884b34cfbea83cdcdac8292976c1e2ed40b9089eb6cf4f257c464560
```

另一方面，也会存在安全隐患，比如，容器中一旦挂载cgroup filesystem，可以修改整全局的cgroup配置。最后，如果想在容器内部运行systemd，我们希望每个容器都自己的cgroup结构，[lxcfs](https://insights.ubuntu.com/2015/03/02/introducing-lxcfs/)也是为了解决这个问题，但是cgroup namespace更加安全。

有了cgroup namespace后，每个namespace中的进程都有自己`cgroupns root`和`cgroup filesystem`视图。

## 测试

```sh
# uname -a
4.8.10-1.el6.elrepo.x86_64

# cat /proc/self/cgroup 
4:memory:/
```

当我们查看`/proc/[pid]/cgroup`时，第3个字段是进程所属的`group directory`相对于`cgroup root directory`的路径，`/`表示当前进程位于cgroup root directory。

> If the cgroup directory of the target process lies outside the root directory of the reading 
> process's cgroup namespace, then the pathname will show ../ entries for each ancestor level 
> in the cgroup hierarchy.

```
# mkdir -p /cgroup/memory/docker/container1
# echo $$
12234

# echo $$ > /cgroup/memory/docker/container1/tasks
# cat /cgroup/memory/docker/container1/tasks
12234
22411
```

其中`22411`为`cat`命令对应的进程。


```sh
# cat /proc/self/cgroup
4:memory:/docker/container1
```

使用unshare创建新的cgroup namespace:

```sh
# unshare -Cm bash
# cat /proc/self/cgroup
4:memory:/
```

可以看到，新的进程（及子进程）的cgroup root信息发生了变化。

我们可以查看init cgroupns进程的cgroup信息：

```sh
# cat /proc/1/cgroup
4:memory:/../..
```

但是，cgroup filesystem的挂载点仍然是init namespace中挂载点。所以，在新的cgroupns仍然能够看到全局的cgroup视图：

```sh
# cat /proc/self/mountinfo
51 38 0:27 /../.. /cgroup/memory rw,relatime - cgroup cgroup rw,memory

# ls /cgroup/memory/
docker ...
```

我们希望在新的cgroupns中，进程看到的cgroup挂载点显示为`/`（即有自己的cgroup root），我们需要重新挂载一下cgroup filesystem:

```sh
# mount --make-rslave / # Don't propagate mount events to other namespaces
# umount /cgroup/memory 
# mount -t cgroup -o memory cgroup /cgroup/memory
# cat /proc/self/mountinfo 
51 38 0:27 / /cgroup/memory rw,relatime - cgroup cgroup rw,memory

# ls /cgroup/memory/ 
看不到docker目录
```

这样，我们就只能看到当前cgroupns中的视图了。

从init cgroupns中，仍然能够看到子cgroupns中的进程的完整路径：

```sh
# cat /proc/12234/cgroup
4:memory:/docker/container1
```

## 总结

Cgroup namespace带来以下一些好处：

(1)可以限制容器的cgroup filesytem视图，使得在容器中也可以安全的使用cgroup；

(2)此外，会使容器迁移更加容易；在迁移时，`/proc/self/cgroup`需要复制到目标机器，这要求容器的cgroup路径是唯一的，否则可能会与目标机器冲突。有了cgroupns，每个容器都有自己的cgroup filesystem视图，不用担心这种冲突。

## 应用层

[Docker](https://github.com/docker/docker/blob/master/oci/defaults_linux.go#L96)本身还没有使用这一特性，[runc](https://github.com/opencontainers/runc)已经有相关PR在实现中，参考[#774](https://github.com/opencontainers/runc/pull/774)，[#1184](https://github.com/opencontainers/runc/pull/1184)。

## 主要参考

* [cgroup_namespaces - overview of Linux cgroup namespaces](http://man7.org/linux/man-pages/man7/cgroup_namespaces.7.html)
* [Control group namespaces](https://lwn.net/Articles/621006/)
* [CGroup Namespaces](https://lwn.net/Articles/618873/)
* [Control Group v2](https://www.kernel.org/doc/Documentation/cgroup-v2.txt)
* [CGroup Namespaces Support Set For Linux 4.6 Kernel](https://www.phoronix.com/scan.php?page=news_item&px=CGroup-Namespaces-Linux-4.6)