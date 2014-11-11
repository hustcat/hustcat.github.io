---
layout: post
title:  Docker内部存储结构（devicemapper）解析
date: 2014-11-07 12:10:30
categories: Linux
tags: docker
excerpt: "关于Docker的内部存储结构，之前写过一篇文章，这里再补充一些内容。Docker的rootfs的大小默认为10G，我们可以通过一些参数来调整"
---

关于Docker的内部存储结构，之前写过一篇文章，参考[这里](http://www.cnblogs.com/hustcat/p/3908985.html)。这里再补充一些内容。

Docker在初始化过程中，会创建一个100G的用于存储数据，和一个2G的用于存储元数据的稀疏文件，然后分别附加到回环块设备/dev/loop0和/dev/loop1。然后基于回环块设备创建thin pool。

```sh
[root@yy ~]# ls -lh /var/lib/docker/devicemapper/devicemapper/* -lh        
-rw------- 1 root root 100G Oct 28 17:33 /var/lib/docker/devicemapper/devicemapper/data
-rw------- 1 root root 2.0G Oct 30 17:44 /var/lib/docker/devicemapper/devicemapper/metadata
```

查看thin pool信息

```sh
[root@yy ~]#dmsetup info
Name:              docker-8:1-701074-pool
State:             ACTIVE
Read Ahead:        256
Tables present:    LIVE
Open count:        2
Event number:      0
Major, minor:      253, 0
Number of targets: 1
```

701074为/var/lib/docker/devicemapper的inode number:

```sh
[root@yy ~]# stat /var/lib/docker/devicemapper             
  File: `/var/lib/docker/devicemapper'
  Size: 4096            Blocks: 8          IO Block: 4096   directory
Device: 801h/2049d      Inode: 701074      Links: 5
```

8:1为设备分区的主、次设备号：

```sh
[root@yy ~]# ls -l /dev/sda1
brw-rw---- 1 root disk 8, 1 Sep 17 19:28 /dev/sda1
```

回环块设备信息

```sh
[root@yy ~]# losetup -a
/dev/loop0: [0801]:701077 (/dev/loop0)
/dev/loop1: [0801]:701078 (/dev/loop1)
```

701077为/var/lib/docker/devicemapper/devicemapper/data的inode number：
701078为/var/lib/docker/devicemapper/devicemapper/metadata的inode number：

```sh
[root@yy ~]# stat /var/lib/docker/devicemapper/devicemapper/data 
  File: `/var/lib/docker/devicemapper/devicemapper/data'
  Size: 107374182400    Blocks: 6199168    IO Block: 4096   regular file
Device: 801h/2049d      Inode: 701077      Links: 1
[root@yy ~]# stat /var/lib/docker/devicemapper/devicemapper/metadata
  File: `/var/lib/docker/devicemapper/devicemapper/metadata'
  Size: 2147483648      Blocks: 9776       IO Block: 4096   regular file
Device: 801h/2049d      Inode: 701078      Links: 1
```

docker在创建image，会将image的信息(struct DevInfo)写到文件/var/lib/docker/devicemapper/metadata/${container-id}，参考函数(devices *DeviceSet) registerDevice。

```sh
[root@yy ~] # docker ps -q
d2b11baafccc
[root@yy ~] # dmsetup table docker-8:1-701074-d2b11baafcccb6785e5a28ce463447053c28bd81031b76c1247499e025ba5412
0 20971520 thin 253:0 18
[root@yy ~] # hexdump -C /var/lib/docker/devicemapper/metadata/d2b11baafcccb6785e5a28ce463447053c28bd81031b76c1247499e025ba5412     
00000000  7b 22 64 65 76 69 63 65  5f 69 64 22 3a 31 38 2c  |{"device_id":18,|
00000010  22 73 69 7a 65 22 3a 31  30 37 33 37 34 31 38 32  |"size":107374182|
00000020  34 30 2c 22 74 72 61 6e  73 61 63 74 69 6f 6e 5f  |40,"transaction_|
00000030  69 64 22 3a 36 30 39 2c  22 69 6e 69 74 69 61 6c  |id":609,"initial|
00000040  69 7a 65 64 22 3a 66 61  6c 73 65 7d              |ized":false}|
```
可以看到镜像的大小为10G，卷id为18

我们可以调整回环设备文件和镜像的大小，例如，我们将回环境设备文件大小设置为200G，元数据文件大小为4G，基础镜像大小为20G：

```sh
docker -d --storage-opt dm.basesize=20G --storage-opt dm.loopdatasize=200G --storage-opt dm.loopmetadatasize=4G
```
另外，--storage-opt还有其它一些参数，比较dm.fs指定文件系统（默认为ext4）等。

```sh
bash-4.2# df -h
Filesystem                                                                                      Size  Used Avail Use% Mounted on
/dev/mapper/docker-8:1-696417-751562f7368504d35ae19a1bfc47ea324470f3d624938303d664c1fd6086a34c   20G  429M   19G   3% /
```

更多内容请参考
https://github.com/snitm/docker/tree/master/daemon/graphdriver/devmapper