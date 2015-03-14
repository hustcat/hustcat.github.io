---
layout: post
title: Cgroup分析——基本介绍
date: 2015-03-14 18:02:30
categories: Linux
tags: cgroup
excerpt: cgroup是容器虚拟化在内核的核心技术之一，有必要做一些深入研究
---

# 基本概念
## cgroup：

A *cgroup* associates a set of tasks with a set of parameters for one or more subsystems.

Cgroup主要用于将一组进程与一个或者多个子系统关联起来，它有一些控制参数；在内核对应数据结构struct cgroup。

## Subsystem：

A subsystem is typically a "resource controller" that schedules a resource or applies per-cgroup limits。

Cgroup通过子系统实现具体的资源限制，每个子系统负责一种类型的资源的控制。在内核对应的数据结构为struct cgroup_subsys.

## Hierarchy:

A *hierarchy* is a set of cgroups arranged in a tree。

由一些cgroup构成树状结构的层次。一个hierarchy至少拥有一个top cgroup。通过mount cgroup fs可以创建一个hierarchy实例，并随之创建一个cgroup virtual filesystem。在内核对应的数据结构为struct cgroupfs_root​​​.


## 三者之间的关系：

（1）Linux系统中可以mount多个hierarchy，但是每个hierarchy的子系统不能重复（即一个子系统只能出现在一个hierarchy）。一般情况，我们为每个hierarchy指定一个子系统：

```sh
#mount -t cgroup -o memory memtest /cgroup/memtest/
```

这样我们创建了一个为memtest的hierarchy，它只有memory一个子系统

此外，还需要注意：

> If an active hierarchy with exactly the same set of subsystems already exists, it will be reused for the new mount.
>
> If no existing hierarchy matches, and any of the requested subsystems are in use in an existing hierarchy, the mount will fail with -EBUSY.
> 
> Otherwise, a new hierarchy is activated, associated with the requested subsystems.

（2）我们可以在hierarchy中创建任意个cgroup，但一个hierarchy至少拥有一个top cgroup（由cgroup自己默认创建）。我们手动创建的cgroup都是top_cgroup的子cgroup：

```sh
# mkdir /cgroup/memtest/group1
# mkdir /cgroup/memtest/group2
```

（3）每次在系统中创建新的hierarchy时，该系统中的所有task都是那个层级的top cgroup（默认 cgroup）的初始成员。
task不能同时位于同一层级的不同 cgroup 中。例如：

一个task（比如httpd）不能同时存在于group1和group2。如果httpd开始位于group1，如果将它加到group2，则httpd会自动从group1删除。

（4）每个子进程自动成为其父进程所在 cgroup 的成员。然后可根据需要将该子进程移动到不同的 cgroup 中，但开始时它总是继承其父任务的 cgroup。

此后，父任务和子任务就彼此完全独立：更改某个任务所属 cgroup 不会影响到另一个。同样更改父任务的 cgroup 也不会以任何方式影响其子任务。总之：所有子任务总是可继承其父任务的同一 cgroup 的成员关系，但之后可更改或者删除那些成员关系。

# 基本操作

## mount cgroup

创建一个包含memory子系统的hierachy：

```sh
#mount -t cgroup -o memory memtest /cgroup/memtest
# cat /cgroup/memtest/tasks 
1
2
3
…
```

task中包含系统中的所有进程

## create cgroup

```sh
# mkdir /cgroup/memtest/g1
```

内核自动创建的cgroup的控制文件：

```sh
# ls /cgroup/memtest/g1/
cgroup.event_control …

# cat /cgroup/memtest/g1/tasks
show nothing
```

这样，就创建了一个g1的cgroup，此时，它还不包含任何进程。

## attach task

```sh
# ps -ef|grep nginx
root      2002     1  0 10:55 ?        00:00:00 nginx: master process /usr/sbin/nginx -c /etc/nginx/nginx.conf
nginx     2003  2002  0 10:55 ?        00:00:00 nginx: worker process  

# cat /cgroup/memtest/tasks |grep 2002
2002
# cat /cgroup/memtest/tasks |grep 2003
2003
# echo 2002 > /cgroup/memtest/g1/tasks
# cat /cgroup/memtest/g1/tasks 
2002
```

可以看到2003并没有包含在g1中。

```sh
# mkdir /cgroup/memtest/g2
# echo 2002 > /cgroup/memtest/g2/tasks
# cat /cgroup/memtest/g2/tasks 
2002
# cat /cgroup/memtest/g1/tasks
```

进程2002自动从g1删除。

将当前shell加入g2：

```sh
#echo $$ > /cgroup/memtest/g2/tasks
```

或者

```sh
#echo 0 > /cgroup/memtest/g2/tasks
```

这样，从shell启动的其它的进程都会自动attach到g2。

## remove cgroup

删除cgroup时，必须保证该cgroup没有进程，而且没有子cgroup，否则返回-EBUSY。

```sh
# rmdir /cgroup/memtest/g2
rmdir: 删除 "/cgroup/memtest/g2" 失败: 设备或资源忙

# echo 2002 > /cgroup/memtest/tasks
# rmdir /cgroup/memtest/g2
```

＃ 主要参考

https://www.kernel.org/doc/Documentation/cgroups/cgroups.txt
