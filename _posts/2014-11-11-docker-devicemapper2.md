---
layout: post
title:  Docker内部存储结构（devicemapper）解析（下篇）
date: 2014-11-11 12:10:30
categories: Linux
tags: docker
excerpt: 自从第一次接触Docker，就有一个问题困扰着我——如何查看stop状态的容器的文件系统。今天再一次看了一下Docker的代码，终于找到了这个问题的答案。
---

Docker启动后，会创建thin pool和base image，如下：

```sh
# dmsetup info
Name:              docker-8:1-696417-base
State:             ACTIVE
Read Ahead:        256
Tables present:    LIVE
Open count:        0
Event number:      0
Major, minor:      253, 1
Number of targets: 1

Name:              docker-8:1-696417-pool
State:             ACTIVE
Read Ahead:        256
Tables present:    LIVE
Open count:        1
Event number:      0
Major, minor:      253, 0
Number of targets: 1
```

/dev/dm-0对应docker-8:1-696417-pool，大小为200G。

```sh
# ls /dev/dm-0 -lh
brw-rw---- 1 root disk 253, 0 Nov  7 13:01 /dev/dm-0
# blockdev --report /dev/dm-0
RO    RA   SSZ   BSZ   StartSec            Size   Device
rw   256   512  4096          0    214748364800   /dev/dm-0
```
/dev/dm-1对应docker-8:1-696417-base，大小为20G。

```sh
# ls /dev/dm-1 -lh 
brw-rw---- 1 root disk 253, 1 Nov  7 13:04 /dev/dm-1
# blockdev --report /dev/dm-1
RO    RA   SSZ   BSZ   StartSec            Size   Device
rw   256   512  4096          0     21474836480   /dev/dm-1
```

Devicemapper的初始化
------
整个流程大体如下：

![](/assets/2014-11-11-devicemapper2-1.png)

###创建thin pool

```go
// This is the programmatic example of "dmsetup create"
func createPool(poolName string, dataFile, metadataFile *os.File, poolBlockSize uint32) error {
...
	params := fmt.Sprintf("%s %s %d 32768 1 skip_block_zeroing", metadataFile.Name(), dataFile.Name(), poolBlockSize)
	if err := task.AddTarget(0, size/512, "thin-pool", params); err != nil {
		return fmt.Errorf("Can't add target %s", err)
	}
...
}
```

相当于执行下面的操作：

```sh
# dmsetup create docker-8:1-696417-pool --table ‘0 419430400 thin-pool 7:1 7:0 128 32768 1 skip_block_zeroing’
# dmsetup table docker-8:1-696417-pool
0 419430400 thin-pool 7:1 7:0 128 32768 1 skip_block_zeroing
```

###创建BaseImage
实际上，thin-provisioned volume分两步，首先是发送一个消息给pool，创建一个volume。然后激活volume。只有activated的volume，才能在dmsetup info的输出中看到。

（1）Creating a new thinly-provisioned volume

```go
func createDevice(poolName string, deviceId *int) error {
…
		if err := task.SetMessage(fmt.Sprintf("create_thin %d", *deviceId)); err != nil {
			return fmt.Errorf("Can't set message %s", err)
		}
```
相当于执行下面的操作：

```sh
#dmsetup message /dev/mapper/ docker-8:1-696417-pool 0 "create_thin 0"

# hexdump -C /var/lib/docker/devicemapper/metadata/base
00000000  7b 22 64 65 76 69 63 65  5f 69 64 22 3a 30 2c 22  |{"device_id":0,"|
00000010  73 69 7a 65 22 3a 32 31  34 37 34 38 33 36 34 38  |size":2147483648|
00000020  30 2c 22 74 72 61 6e 73  61 63 74 69 6f 6e 5f 69  |0,"transaction_i|
00000030  64 22 3a 31 2c 22 69 6e  69 74 69 61 6c 69 7a 65  |d":1,"initialize|
00000040  64 22 3a 74 72 75 65 7d                           |d":true}|
```

可以看到base的device_id为0。

（2）activated thinly-provisioned volumes

```go
func activateDevice(poolName string, name string, deviceId int, size uint64) error {
...
	params := fmt.Sprintf("%s %d", poolName, deviceId)
	if err := task.AddTarget(0, size/512, "thin", params); err != nil {
		return fmt.Errorf("Can't add target %s", err)
	}
```
相当于执行下面的操作：

```sh
#dmsetup create docker-8:1-696417-base --table "0 41943040 thin /dev/mapper/ docker-8:1-696417-pool 0"
#dmsetup table docker-8:1-696417-base
0 41943040 thin 253:0 0
```

只有activated的volume，才能在dmsetup info的输出中看到。

Devicemapper的基本操作
------
###Driver的基本操作

```go
///清除thin pool  
func (d *Driver) Cleanup()

///当加载新镜像时,添加一个新thin volume,id为containerid或imageid  
func (d *Driver) Create(id, parent string)

///挂载thin volume到/var/lib/docker/devicemapper/mnt/$id目录下(docker start)
func (d *Driver) Get(id, mountLabel string)

///从/var/lib/docker/devicemapper/mnt/$id目录umount thin volume(docker stop)
func (d *Driver) Put(id string)

///删除volume(真正删除)
func (d *Driver) Remove(id string)
```

###Thin pool的基本操作

```go
///在thin pool中创建一个新的snapshot volume
func (devices *DeviceSet) AddDevice(hash, baseHash string)

///删除thin volume(释放空间,删除(remove+delete)thin volume)
func (devices *DeviceSet) DeleteDevice(hash string) 

///将thin volume从/var/lib/docker/devicemapper/mnt/$id umount, deactivate(remove )thin volume(don't delete)
func (devices *DeviceSet) UnmountDevice(hash string)

///activate thin volume ，然后mount到/var/lib/docker/devicemapper/mnt/$id
func (devices *DeviceSet) MountDevice(hash, path, mountLabel string)

///thin pool的统计信息(docker info)
func (devices *DeviceSet) Status() *Status

///thin pool初始化
func NewDeviceSet(root string, doInit bool, options []string)
```

###Devmapper接口
devmapper/devmapper.go封装了OS层的thin volume的基本操作。

```go
///dmsetup suspend
func suspendDevice(name string)

///dmsetup resume
func resumeDevice(name string)

///message create_thin
func createDevice(poolName string, deviceId *int)

///message delete
func deleteDevice(poolName string, deviceId int)

///dmsetup remove
func removeDevice(name string)

///dmsetup create
func activateDevice(poolName string, name string, deviceId int, size uint64)

///message 'create_snap'
func createSnapDevice(poolName string, deviceId *int, baseName string, baseDeviceId int)
```

三者之间的调用关系如下：

![](/assets/2014-11-11-devicemapper2-2.png)

查看stop的容器的文件系统
-----

stop的容器的thin volume都处于未激活（deactivate）状态，我们可以将其激活（activate），然后查看文件系统中的内容。

我们创建一个容器，不启动：

```sh
# docker create --name="yy1" centos /bin/bash 
93f595ea79a0420cc263d054d3e63b5ad1e4cc3da128167984a6ac01ad89f8e9
```

metadata下面新增两个目录：

```sh
# ls metadata/
93f595ea79a0420cc263d054d3e63b5ad1e4cc3da128167984a6ac01ad89f8e9
93f595ea79a0420cc263d054d3e63b5ad1e4cc3da128167984a6ac01ad89f8e9-init
```
我们可以查看thin volume的信息

```sh
#cat metadata/93f595ea79a0420cc263d054d3e63b5ad1e4cc3da128167984a6ac01ad89f8e9
{"device_id":5,"size":21474836480,"transaction_id":8,"initialized":false}
# cat metadata/93f595ea79a0420cc263d054d3e63b5ad1e4cc3da128167984a6ac01ad89f8e9-init 
{"device_id":4,"size":21474836480,"transaction_id":7,"initialized":false}
```

我们来尝试手动挂载thin volume，首先activate thin volume：

```sh
# dmsetup create  93f595ea79a0420cc263d054d3e63b5ad1e4cc3da128167984a6ac01ad89f8e9-init  --table "0 41943040 thin 253:0 4"
```

然后就可以挂载该thin volume了：

```sh
# mount /dev/mapper/93f595ea79a0420cc263d054d3e63b5ad1e4cc3da128167984a6ac01ad89f8e9-init mnt/93f595ea79a0420cc263d054d3e63b5ad1e4cc3da128167984a6ac01ad89f8e9-init
# ls mnt/93f595ea79a0420cc263d054d3e63b5ad1e4cc3da128167984a6ac01ad89f8e9-init/   
id  lost+found  rootfs

deactivate thin volume
# umount mnt/93f595ea79a0420cc263d054d3e63b5ad1e4cc3da128167984a6ac01ad89f8e9-init
# dmsetup remove 93f595ea79a0420cc263d054d3e63b5ad1e4cc3da128167984a6ac01
```

Docker的devicemapper的维护者[Alexander Larsson]()在[这篇文章](http://blogs.gnome.org/alexl/2013/10/15/adventures-in-docker-land/)中详细讨论了devicemapper最初的一些想法。
