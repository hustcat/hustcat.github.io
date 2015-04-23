---
layout: post
title: EXT4/EXT3文件系统目录的一点区别
date: 2015-04-23 20:56:30
categories: Linux
tags: 存储
excerpt: EXT4/EXT3文件系统目录的一点区别。
---

# 问题描述

业务同学在测试TDocker发现一个奇怪的问题，对一个目录调用lseek(fd,0,SEEK_END)时返回一个很大的数字9223372036854775807(0x 7FFFFFFFFFFFFFFF)
 
而在其它非TDocker机器返回的为4096。

```sh
#./calc_dir_size /tmp
file: cron_log, size: 9223372036854775807
```

为什么会这个差别呢？

# 原因分析

用strace跟踪一下，的确lseek返回9223372036854775807：

```sh
open("cron_log", O_RDONLY)              = 3
fstat(3, {st_mode=S_IFDIR|0755, st_size=4096, ...}) = 0
mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7f338b3a9000
fstat(3, {st_mode=S_IFDIR|0755, st_size=4096, ...}) = 0
lseek(3, 0, SEEK_END)                   = 9223372036854775807
lseek(3, 0, SEEK_SET)                   = 0
close(3)                                = 0
```

由于Docker对rootfs默认使用ext4，而其它非Docker机器一般为ext3。初步断定是由于系统差别引起的。

Trace kernel，发现对于ext4，lseek会调用ext4_dir_llseek：

```
  1)               |  vfs_llseek() {
  1)               |    ext4_dir_llseek() {
  1)               |      mutex_lock() {
  1)   0.185 us    |        _cond_resched();
  1)   0.922 us    |      }
  1)   0.182 us    |      mutex_unlock();
  1)   1.648 us    |    }
  1)   2.159 us    |  }
```

```c
const struct file_operations ext4_dir_operations = {
	.llseek		= ext4_dir_llseek,
	.read		= generic_read_dir,
	.readdir	= ext4_readdir,
	.unlocked_ioctl = ext4_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ext4_compat_ioctl,
#endif
	.fsync		= ext4_sync_file,
	.release	= ext4_release_dir,
};
```

来看ext4_dir_llseek的实现：

```c
loff_t ext4_dir_llseek(struct file *file, loff_t offset, int origin)
{
 struct inode *inode = file->f_mapping->host;
 loff_t ret = -EINVAL;
 int dx_dir = is_dx_dir(inode);
 mutex_lock(&inode->i_mutex);
 /* NOTE: relative offsets with dx directories might not work
  *       as expected, as it is difficult to figure out the
  *       correct offset between dx hashes */
 switch (origin) {
 case SEEK_END:
  if (unlikely(offset > 0))
   goto out_err; /* not supported for directories */
  /* so only negative offsets are left, does that have a
   * meaning for directories at all? */
  if (dx_dir)
   offset += ext4_get_htree_eof(file);
  else
   offset += inode->i_size;
...
 
/*
 * Return 32- or 64-bit end-of-file for dx directories
 */
static inline loff_t ext4_get_htree_eof(struct file *filp)
{
 if ((filp->f_mode & FMODE_32BITHASH) ||
     (!(filp->f_mode & FMODE_64BITHASH) && is_32bit_api()))
  return EXT4_HTREE_EOF_32BIT;
 else
  return EXT4_HTREE_EOF_64BIT;
}
 
 
/* 32 and 64 bit signed EOF for dx directories */
#define EXT4_HTREE_EOF_32BIT   ((1UL  << (32 - 1)) - 1)
#define EXT4_HTREE_EOF_64BIT   ((1ULL << (64 - 1)) - 1)
```

由于ext4默认开启htree-indexed directory，参考[这里](http://oldblog.donghao.org/2011/03/ext3adir-indexioo.html):

```sh
# tune2fs -l /dev/mapper/docker-8:4-xxx|grep "Filesystem features"
Filesystem features:      has_journal ext_attr resize_inode dir_index filetype needs_recovery extent flex_bg sparse_super large_file huge_file uninit_bg dir_nlink extra_isize
```

所以，ext4_dir_llseek会返回EXT4_HTREE_EOF_64BIT，即0x 7FFFFFFFFFFFFFFF。

为什么ext3会返回4096呢？

```c
const struct file_operations ext3_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= ext3_readdir,		/* we take BKL. needed?*/
	.unlocked_ioctl	= ext3_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ext3_compat_ioctl,
#endif
	.fsync		= ext3_sync_file,	/* BKL held */
	.release	= ext3_release_dir,
};

loff_t generic_file_llseek(struct file *file, loff_t offset, int origin)
{
	loff_t rval;

	mutex_lock(&file->f_dentry->d_inode->i_mutex);
	rval = generic_file_llseek_unlocked(file, offset, origin);
	mutex_unlock(&file->f_dentry->d_inode->i_mutex);

	return rval;
}

loff_t
generic_file_llseek_unlocked(struct file *file, loff_t offset, int origin)
{
	struct inode *inode = file->f_mapping->host;

	switch (origin) {
	case SEEK_END:
		offset += inode->i_size;
		break;
...
```

从代码可以看到，与ext4不同的是，ext3调用的是generic_file_llseek，而后者直接返回inode->i_size。（至于为是4096，这里不再讨论）

问题就到这里结束了吗？

# tlinux

在分析原因的过程中，发现在tlinux内核的机器上，对于ext4目录，lseek仍然返回4096。

```
const struct file_operations ext4_dir_operations = {
    .llseek     = generic_file_llseek,
    .read       = generic_read_dir,
    .readdir    = ext4_readdir,     /* we take BKL. needed?*/
    .unlocked_ioctl = ext4_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl   = ext4_compat_ioctl,
#endif
    .fsync      = ext4_sync_file,
    .release    = ext4_release_dir,
};
```

原来tlinux内核下的ext4也是调用generic_file_llseek。

# XFS

对于XFS，lseek返回0，与stat的结果不同：

```sh
#./calc_dir_size /data/dir1 
file: /data/dir1, size: 0
#stat /data/dir1
  File: `/data/dir1'
  Size: 6               Blocks: 0          IO Block: 4096   directory
Device: 804h/2052d      Inode: 1879049233  Links: 2
```

这是因为在XFS中fstat与lseek取的值不同造成的。

## fstat

vfs_fstat -> vfs_getattr -> xfs_vn_getattr:

```c
STATIC int
xfs_vn_getattr(
  struct vfsmount   *mnt,
  struct dentry   *dentry,
  struct kstat    *stat)
{
  struct inode    *inode = dentry->d_inode;
  struct xfs_inode  *ip = XFS_I(inode);
  struct xfs_mount  *mp = ip->i_mount;

  trace_xfs_getattr(ip);

  if (XFS_FORCED_SHUTDOWN(mp))
    return -XFS_ERROR(EIO);

  stat->size = XFS_ISIZE(ip);
...

static inline xfs_fsize_t XFS_ISIZE(struct xfs_inode *ip)
{
  if (S_ISREG(ip->i_d.di_mode))
    return i_size_read(VFS_I(ip));
  return ip->i_d.di_size;
}
```

## lseek

```c
const struct file_operations xfs_dir_file_operations = {
  .open   = xfs_dir_open,
  .read   = generic_read_dir,
  .readdir  = xfs_file_readdir,
  .llseek   = generic_file_llseek,
  .unlocked_ioctl = xfs_file_ioctl,
#ifdef CONFIG_COMPAT
  .compat_ioctl = xfs_file_compat_ioctl,
#endif
  .fsync    = xfs_dir_fsync,
};
```

可以看到，XFS的lseek，也是调用的generic_file_llseek，返回inode->i_size。

# 总结

对目录调用lseek，不同的文件系统返回的值可能并不一样，这由文件系统自身的实现决定。如果业务希望保持ext3的传统，ext4下可以关闭dir_index特性：

```sh
# tune2fs -O ^dir_index /dev/mapper/docker-8:4-xxx
```


＃ 附测试程序

```c
#include <iostream>
using namespace std;

long GetFileSize(const char* szFileName) 
{
  FILE* pFile = fopen(szFileName, "r");
  if (NULL == pFile)
  {
    cout << "fopen failed. file: " << szFileName << endl;
    return -1;
  }

  long lLength = 0;
  fseek(pFile, 0, SEEK_END);
  lLength = ftell(pFile);
  fseek(pFile, 0, SEEK_SET);

  fclose(pFile);

  return lLength;
}

int main(int argc, char* argv[]) 
{
  if (argc < 2)
  {
    cout << "Usage: " << argv[0] << " filepath" << endl;
    return 1;
  }


  cout << "file: " << argv[1] << ", size: " << GetFileSize(argv[1]) << endl;

  return 0;
}
```

