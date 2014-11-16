---
layout: post
title:  Docker内部存储结构（devicemapper）解析（续）
date: 2014-11-16 23:10:30
categories: Linux
tags: docker
excerpt: Devicemapper的DISCARD可以让Docker回收已经删除的镜像的空间，这对于Dokcer是非常重要的，Docker提供了一些参数，让用户可以控制该行为。
---

dm.fs
------
参数dm.fs可以指定容器的rootfs的文件系统，但只支持ext4/xfs：

```go
func NewDeviceSet(root string, doInit bool, options []string) (*DeviceSet, error) {
...
		case "dm.fs":
			if val != "ext4" && val != "xfs" {
				return nil, fmt.Errorf("Unsupported filesystem %s\n", val)
			}
			devices.filesystem = va
```
这是为什么呢？下面是docker dm的维护者Alexander Larsson的描述：

> Additionally we ensure that DISCARD support is enabled in the filesystem so that any files removed 
> in the conttainer filters down to the loopback file making it sparse again.

参考[这里](http://blogs.gnome.org/alexl/2013/10/15/adventures-in-docker-land/)

一句话，因为ext4/xfs支持DISCARD。这样，如果容器中删除了文件，空间就会马上还给Thin pool，因为Thin provisioning是支持DISCARD操作的。但是，默认情况下Thin pool是底层是稀疏文件/var/lib/docker/devicemapper/devicemapper/data，所以，只有Host的文件系统支持DISCARD，才能保证稀疏文件空间释放。

Host为ext3
------

```sh
# docker images
REPOSITORY                          TAG                 IMAGE ID            CREATED             VIRTUAL SIZE
dbyin/tlinux1.2   latest              8297f05d459f        41 hours ago        399.6 MB
dbyin/httpd       latest              93e711fab1c1        7 weeks ago         412.7 MB
centos                              latest              61038e6e3195        3 months ago        236.4 MB
```

我们可以查看稀疏文件的真正大小：

```sh
# ls -lsh /var/lib/docker/devicemapper/devicemapper/data 
1.6G -rw------- 1 root root 200G Nov 12 11:52 /var/lib/docker/devicemapper/devicemapper/data

# dmsetup status
yy_pool: 0 409600 thin-pool 0 13/65536 0/3200 - rw no_discard_passdown
docker-8:1-696417-base: 0 41943040 thin 928768 41943039
docker-8:1-696417-pool: 0 419430400 thin-pool 73 633/524288 26115/3276800 - rw no_discard_passdown
```

我们删除一个image

```sh
# docker rmi dbyin/httpd
```

可以看到稀疏文件并没有变小：

```sh
# ls -lsh /var/lib/docker/devicemapper/devicemapper/data 
1.6G -rw------- 1 root root 200G Nov 12 11:52 /var/lib/docker/devicemapper/devicemapper/data

# dmsetup status
docker-8:1-696417-base: 0 41943040 thin 928768 41943039
docker-8:1-696417-pool: 0 419430400 thin-pool 73 490/524288 18758/3276800 - rw no_discard_passdown
```
no_discard_passdown表示dm层不会将DISCARD传给底层的设备(loopback device)，只删除映射关系。

Host为ext4
------

```sh
[root@yy3 ~]# dmsetup status
docker-253:1-8790943-pool: 0 209715200 thin-pool 339 876/524288 32432/1638400 - rw discard_passdown queue_if_no_space
```
可以看到这里为discard_passdown，表示dm会将DISCARD传给底层设备(loopback device)，queue_if_no_space表示如果thin pool没有空闲空间后，IO请求会被排队。另外，error_if_no_space表示如果thin pool没有空闲空间后，直接报错。

```sh
[root@yy3 ~]# ls -slh /var/lib/docker/devicemapper/devicemapper/data 
2.5G -rw-------. 1 root root 100G Nov 12 06:14 /var/lib/docker/devicemapper/devicemapper/data
[root@yy3 ~]# docker rmi dbyin/httpd    
[root@yy3 ~]# ls -slh /var/lib/docker/devicemapper/devicemapper/data 
2.0G -rw-------. 1 root root 100G Nov 12 06:15 /var/lib/docker/devicemapper/devicemapper/data
```
可以看到删除image前后，稀疏文件大小的变化。

dm.blkdiscard
------
docker还提供这个参数，默认值为true，即删除image后，会调用DISCARD，真正释放HOST上空间。

```go
func (devices *DeviceSet) deleteDevice(info *DevInfo) error {
	if devices.doBlkDiscard {
		// This is a workaround for the kernel not discarding block so
		// on the thin pool when we remove a thinp device, so we do it
		// manually
		if err := devices.activateDeviceIfNeeded(info); err == nil {
			if err := BlockDeviceDiscard(info.DevName()); err != nil {
				log.Debugf("Error discarding block on device: %s (ignoring)", err)
			}
		}
	}
...
}


func BlockDeviceDiscard(path string) error {
...
	if err := ioctlBlkDiscard(file.Fd(), 0, size); err != nil {
		return err
	}
...
}

func ioctlBlkDiscard(fd uintptr, offset, length uint64) error {
	var r [2]uint64
	r[0] = offset
	r[1] = length

	if _, _, err := syscall.Syscall(syscall.SYS_IOCTL, fd, BlkDiscard, uintptr(unsafe.Pointer(&r[0]))); err != 0 {
		return err
	}
	return nil
}
```

主要参考
------
https://www.kernel.org/doc/Documentation/device-mapper/thin-provisioning.txt
https://github.com/docker/docker/tree/master/daemon/graphdriver/devmapper

