---
layout: post
title: Overlayfs - delete file failed because project id in XFS
date: 2016-05-27 11:00:30
categories: Linux
tags: xfs
excerpt: Overlayfs - delete file failed because project id in XFS
---

## 问题

在使用overlayfs的过程中，删除文件时报下面的错误：

```sh
# rm /data/ovl/vm2/merged/hello.txt 
rm: cannot remove `/data/ovl/vm2/merged/hello.txt': Invalid cross-device link
```

```sh
# mount -t overlay overlay lowerdir=/data/ovl/vm1,upperdir=/data/ovl/vm2/upper,workdir=/data/ovl/vm2/work /data/ovl/vm2/merged

# tree /data/ovl/vm2/
/data/ovl/vm2/
|-- merged
|   |-- f1.data
|   `-- hello.txt
|-- upper
`-- work
`-- work
```

重新创建一个目录，又没有问题，由于underlying文件系统是XFS，之前在/data/ovl/vm2目录做过很多XFS quota设置，初步与XFS quota相关。

## unlink过程

overlayfs unlink的实现：

![](/assets/overlayfs/unlink_function.jpg)

[More detailes](/assets/overlayfs/ovl_unlink.txt)。


从实现来看，问题出现在xfs_rename：

```c
int
xfs_rename(
	xfs_inode_t	*src_dp, ///src parent inode
	struct xfs_name	*src_name,
	xfs_inode_t	*src_ip, ///src inode
	xfs_inode_t	*target_dp, ///dst parent inode
	struct xfs_name	*target_name,
	xfs_inode_t	*target_ip, ///dst inode
	unsigned int flags)
{
...
	/*
	 * If we are using project inheritance, we only allow renames
	 * into our tree when the project IDs are the same; else the
	 * tree quota mechanism would be circumvented.
	 */
	if (unlikely((target_dp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT) &&
		     (xfs_get_projid(target_dp) != xfs_get_projid(src_ip)))) {
		error = XFS_ERROR(EXDEV);
		goto error_return;
	}
...
}
```

删除(lowerdir)文件时，overlayfs会先在workdir创建一个whiteout文件，然后rename到upperdir。在rename时，下层的XFS会比较project ID，如果不同，就会返回EXDEV（Cross-device link）错误。

```sh
# stat /data/ovl/vm2/upper 
  File: `/data/ovl/vm2/upper'
  Size: 10              Blocks: 0          IO Block: 4096   directory
Device: 804h/2052d      Inode: 12886713161  Links: 2
Access: (0755/drwxr-xr-x)  Uid: (    0/    root)   Gid: (    0/    root)
Access: 2016-05-25 20:12:48.750480603 +0800
Modify: 2016-05-27 11:17:00.641059377 +0800
Change: 2016-05-27 11:17:00.641059377 +0800

# xfs_db -xr -c 'inode 12886713161' -c p /dev/sda4                                                          
core.magic = 0x494e
core.mode = 040755
core.version = 2
core.format = 1 (local)
core.nlinkv2 = 2
core.onlink = 0
core.projid_lo = 4001
core.projid_hi = 0
…


# stat /data/ovl/vm2/work   
  File: `/data/ovl/vm2/work'
  Size: 26              Blocks: 0          IO Block: 4096   directory
Device: 804h/2052d      Inode: 31474       Links: 3
Access: (0755/drwxr-xr-x)  Uid: (    0/    root)   Gid: (    0/    root)
Access: 2016-05-25 20:12:51.602480565 +0800
Modify: 2016-05-25 21:08:56.434388661 +0800
Change: 2016-05-25 21:09:59.550387800 +0800

# xfs_db -xr -c 'inode 31474' -c p /dev/sda4        
core.magic = 0x494e
core.mode = 040755
core.version = 2
core.format = 1 (local)
core.nlinkv2 = 3
core.onlink = 0
core.projid_lo = 3001
core.projid_hi = 0
…
```

## 总结

当overlayfs与XFS quota结合时，不能对upperdir单独设置quota，否则会导致workdir与upperdir的project ID不一致。在删除lowerdir文件时，就会触发EXDEV错误。