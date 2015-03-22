---
layout: post
title: Linux Namespace分析——mnt namespace的实现与应用
date: 2015-03-22 23:19:30
categories: Linux
tags: namespace
excerpt: mnt namespace的实现与应用。前段时间简单研究了一下namespace技术，简单总结一下吧。
---

# 基本介绍

Linux使用namespace来表示从不同进程角度所见的视图。
不同的namespace的进程看到的资源或者进程表是不一样的，相同namespace中的不同进程看到的是同样的资源。

每个task都有一个指针指向一个namespace，存放在tsk->nsproxy中，同一个container的所有进程指向的namespace是相同的，不同container的进程所指向的namepsace是不同的。

```c
struct nsproxy {
	atomic_t count;               /* 被引用的次数 */
	struct uts_namespace *uts_ns;
	struct ipc_namespace *ipc_ns;
	struct mnt_namespace *mnt_ns;
	struct pid_namespace *pid_ns;
	struct net 	     *net_ns;
};
```

nsproxy用来管理不同类型的namespace，nsproxy可以被多个进程共享，目前linux实现了6种类型的namespace，各个namespace实现对应的内核版本：

| namespace | kernel version | capability |
| ---- | ---- | ---- |
| CLONE_NEWNS | 2.4.19 | CAP_SYS_ADMIN |
| CLONE_NEWUTS | 2.6.19 | CAP_SYS_ADMIN |
| CLONE_NEWIPC | 2.6.19 | CAP_SYS_ADMIN |
| CLONE_NEWPID | 2.6.24 | CAP_SYS_ADMIN |
| CLONE_NEWNET | 2.6.29 | CAP_SYS_ADMIN |
| CLONE_NEWUSER | 3.8 | No capability is required |

目前，还有一些新的namespace还在实现中，比如[device namespace](http://lwn.net/Articles/564854).

# mnt namespace

mnt namespace为进程提供独立的文件系统视图。当clone（）函数中带有CLONE_NEWNS标志时，新的mnt ns在子进程中被创建，新的mnt ns是一份父mnt ns的拷贝， 但是在子进程中调用mount安装的文件系统，将独立于父进程的mnt ns，只出现在新的mnt ns上（如果考虑到[shared subtree](https://www.kernel.org/doc/Documentation/filesystems/sharedsubtree.txt)，情况会复杂许多）。


## 数据结构：

```c
struct mnt_namespace {
	atomic_t		count;
	struct vfsmount *	root;///当前namespace下的根文件系统
	struct list_head	list; ///当前namespace下的文件系统链表（vfsmount list）
	wait_queue_head_t poll;
	int event;
};
```

由于mnt_namespace的核心是提供文件系统视图，所以与struct vfsmount（代表一个安装的文件系统）紧密相关。而理解struct vfsmount，也是理解mnt_namespace的关键。

```c
///安装的文件系统描述符
struct vfsmount {
	struct list_head mnt_hash;
	struct vfsmount *mnt_parent;	/* fs we are mounted on */
	struct dentry *mnt_mountpoint;	/* dentry of mountpoint,挂载点目录 */
	struct dentry *mnt_root;	/* root of the mounted tree,文件系统根目录 */
	struct super_block *mnt_sb;	/* pointer to superblock */
	struct list_head mnt_mounts;	/* list of children, anchored here,子文件系统链表 */
	struct list_head mnt_child;	/* and going through their mnt_child,构成mnt_mounts list*/
	int mnt_flags;
	__u32 rh_reserved;		/* for use with fanotify */
	struct hlist_head rh_reserved2;	/* for use with fanotify */
	const char *mnt_devname;	/* Name of device e.g. /dev/dsk/hda1 */
	struct list_head mnt_list;  ///构成mnt_namespace->list
	struct list_head mnt_expire;	/* link in fs-specific expiry list */
	struct list_head mnt_share;	/* circular list of shared mounts,构成shared mount list */
	struct list_head mnt_slave_list;/* list of slave mounts, 所有slave mount组成的链表 */
	struct list_head mnt_slave;	/* slave list entry,构成slave mount list */
	struct vfsmount *mnt_master;	/* slave is on master->mnt_slave_list */
	struct mnt_namespace *mnt_ns;	/* containing namespace,所属的namespace */
	int mnt_id;			/* mount identifier */
	int mnt_group_id;		/* peer group identifier */
	/*
	 * We put mnt_count & mnt_expiry_mark at the end of struct vfsmount
	 * to let these frequently modified fields in a separate cache line
	 * (so that reads of mnt_flags wont ping-pong on SMP machines)
	 */
	atomic_t mnt_count;
	int mnt_expiry_mark;		/* true if marked for expiry */
	int mnt_pinned;
	int mnt_ghosts;
#ifdef CONFIG_SMP
	int *mnt_writers;
#else
	int mnt_writers;
#endif
};
```

# mnt namespace example

来看一个mnt namespace的示例：

```sh
# dd if=/dev/zero of=block0.img bs=1024k count=256
# losetup /dev/loop7 block0.img
# mkfs.ext3 /dev/loop7


# unshare -m /bin/bash
# mount /dev/loop7 /tmpmnt
# cat /proc/mounts |grep loop7
/dev/loop7 /tmpmnt ext3 rw,relatime,data=ordered 0 0
# readlink /proc/$$/ns/mnt
mnt:[4026532526]
```

在另一个终端：

```sh
# cat /proc/mounts |grep loop7
show nothing
# readlink /proc/$$/ns/mnt
mnt:[4026531840]
```

注意，如果在另一个终端执行mount，可以看到mount信息，

```sh
# mount |grep loop7
/dev/loop7 on /tmpmnt type ext3 (rw)
```

这是因为mount命令是从/etc/mtab中读取的信息，而不是直接从内核读取的。/proc/mounts显示的是内核的数据。实际上，/proc/mounts是/proc/self/mounts的符号链接。

# bind mount

bind mount是一项非常有用的技术，它可以实现跨文件系统共享数据，这是容器实现自身文件系统的基础，来看个示例：

```c
# mount -o bind /root/tmp /tmpmnt
# cat /proc/mounts
/dev/mapper/vg_yy1-lv_root / ext4 rw,relatime,barrier=1,data=ordered 0 0
/dev/mapper/vg_yy1-lv_root /tmpmnt ext4 rw,relatime,barrier=1,data=ordered 0 0
```

对于bind mount，将创建一个新的vfsmount，并将其device name为source的device name：

```c
///入参为source对应的vfsmount、dentry，返回新的vfsmount
static struct vfsmount *
clone_mnt(struct vfsmount *old, struct dentry *root)
{
	struct super_block *sb = old->mnt_sb;
	struct vfsmount *mnt = alloc_vfsmnt(old->mnt_devname); ///source的device name

	if (mnt) {
		mnt->mnt_flags = old->mnt_flags;
		atomic_inc(&sb->s_active);
		mnt->mnt_sb = sb;
		mnt->mnt_root = dget(root); ///本文件系统的根目录
		mnt->mnt_mountpoint = mnt->mnt_root; ///挂载点暂时指向本文件系统的根目录,后面再修改指向target的挂载点目录
		mnt->mnt_parent = mnt; ///暂时指向自己
		mnt->mnt_namespace = old->mnt_namespace;
	}
	return mnt;
}
```

/proc/mounts显示当前mnt namespace的mount信息，参考内核函数fs/namespace.c/show_vfsmnt。另外，我们可以通过/proc/self/mountinfo得到更详细的信息：

```sh
# cat /proc/self/mountinfo
21 1 253:0 / / rw,relatime - ext4 /dev/mapper/vg_yy1-lv_root rw,barrier=1,data=ordered
36 21 253:0 /root/temp /tmpmnt rw,relatime - ext4 /dev/mapper/vg_yy1-lv_root rw,barrier=1,data=ordered
```

各个字段的含义：

> 36 35 98:0 /mnt1 /mnt2 rw,noatime master:1 - ext3 /dev/root rw,errors=continue
>
> (1)(2)(3)   (4)   (5)      (6)      (7)   (8) (9)   (10)         (11)
> 
> * (1) mount ID:  unique identifier of the mount (may be reused after umount)
> * (2) parent ID:  ID of parent (or of self for the top of the mount tree)
> * (3) major:minor:  value of st_dev for files on filesystem
> * (4) root:  root of the mount within the filesystem
> * (5) mount point:  mount point relative to the process's root
> * (6) mount options:  per mount options
> * (7) optional fields:  zero or more fields of the form "tag[:value]"
> * (8) separator:  marks the end of the optional fields
> * (9) filesystem type:  name of filesystem of the form "type[.subtype]"
> * (10) mount source:  filesystem specific information or "none"
> * (11) super options:  per super block options
>
> Parsers should ignore all unrecognised optional fields.  Currently the possible optional fields are:
> 
> shared:X  mount is shared in peer group X
> master:X  mount is slave to peer group X
> propagate_from:X  mount is slave and receives propagation from peer group X (*)
> unbindable  mount is unbindable
> 
> (*) X is the closest dominant peer group under the process's root.  If
> X is the immediate master of the mount, or if there's no dominant peer
> group under the same root, then only the "master:X" field is present
> and not the "propagate_from:X" field.
> 
> For more information on mount propagation see:
>
>  Documentation/filesystems/sharedsubtree.txt

参考内核函数fs/namespace.c/show_mountinfo

mountinfo的中显示的root不能跨越vfsmount：

```sh
# mount /dev/loop7  /mnt/vol1
# mkdir /mnt/vol1/dir1
# mount -o bind /mnt/vol1/dir1 /tmpmnt
# cat /proc/mounts
/dev/loop7 /mnt/vol1 ext3 rw,relatime,errors=continue,barrier=1,data=ordered 0 0
/dev/loop7 /tmpmnt ext3 rw,relatime,errors=continue,barrier=1,data=ordered 0 0

# cat /proc/self/mountinfo
36 21 7:7 / /mnt/vol1 rw,relatime - ext3 /dev/loop7 rw,errors=continue,barrier=1,data=ordered
37 21 7:7 /dir1 /tmpmnt rw,relatime - ext3 /dev/loop7 rw,errors=continue,barrier=1,data=ordered
```

可以看到/dir1前面并没有/mnt/vol1。

# bind mount的应用

我们可以通过bind mount给Docker容器在线增加data volume：

```sh
#mknod --mode 0600 /dev/sda4 b 8 4
#mkdir /tmpmnt
#mount -t ext3 /dev/sda4 /tmpmnt/
#mount -o bind /tmpmnt/vm11 /data1
#umount /tmpmnt
```

这里的关键在于即使在不同的mount namespace，但是块设备的major、minor都是一样的，所以，可以通过major、minor得到相同的块设备。显然，对于非块设备的文件系统，很难实现这一点，但实际上，我们还是可以通过shared mount/slave mount之类的技术达到目的。

对于块设备文件，最终都要通过major、minor得到具体的block_device。

```c
void init_special_inode(struct inode *inode, umode_t mode, dev_t rdev)
{
	inode->i_mode = mode;
	if (S_ISCHR(mode)) {
		inode->i_fop = &def_chr_fops;
		inode->i_rdev = rdev;
	} else if (S_ISBLK(mode)) {
		inode->i_fop = &def_blk_fops;
		inode->i_rdev = rdev;
	} else if (S_ISFIFO(mode))
		inode->i_fop = &def_fifo_fops;
	else if (S_ISSOCK(mode))
		inode->i_fop = &bad_sock_fops;
	else
		printk(KERN_DEBUG "init_special_inode: bogus i_mode (%o)\n",
		       mode);
}
```

更多请参考[这里](http://jpetazzo.github.io/2015/01/13/docker-mount-dynamic-volumes/)

下面我们来看看两个相关的系统调用。

# chroot
修改当前进程的根目录，该调用与mnt namespace没有太大关系，但是对于在容器技术中经常使用：

```c
///open.c
SYSCALL_DEFINE1(chroot, const char __user *, filename)
{
///…
	set_fs_root(current->fs, &path);
}
void set_fs_root(struct fs_struct *fs, struct path *path)
{
	struct path old_root;

	write_lock(&fs->lock);
	old_root = fs->root;
	fs->root = *path; ///指向新的vfsmount/dentry
	path_get(path);
	write_unlock(&fs->lock);
	if (old_root.dentry)
		path_put(&old_root);
}
```

只有具有CAP_SYS_CHROOT的进程才能执行该操作，chroot不会修改进程的工作目录：

> This call does not change the current working directory, so that after the call '.' can be outside the tree rooted at '/'.  In particular, the superuser can escape from a "chroot jail" by doing:
> 
>           mkdir foo; chroot foo; cd ..
>
> This call does not close open file descriptors, and such file descriptors may allow access to files outside the chroot tree.

# pivot_root

系统调用pivot_root将当前进程的根文件系统挂载到put_old，同时使用new_root所对应的文件系统作为进程的新的根文件系统（即挂载到/）。

> pivot_root() moves the root filesystem of the calling process to the directory put_old and makes new_root the new root filesystem of the calling process.

主要逻辑：

```c
//namespace.c
SYSCALL_DEFINE2(pivot_root, const char __user *, new_root,
		const char __user *, put_old)
{
	error = user_path_dir(new_root, &new);
	error = user_path_dir(put_old, &old);

	root = current->fs->root; ///当前根目录

	detach_mnt(new.mnt, &parent_path);  ///将new_root对应的文件系统从其挂载目录分离
	detach_mnt(root.mnt, &root_parent); ///将当前的根文件系统(root file system)从挂载目录(/)分离
	/* mount old root on put_old */
	attach_mnt(root.mnt, &old); ///将当前的根文件系统root挂载到put_old目录
	/* mount new_root on / */
	attach_mnt(new.mnt, &root_parent); ///将new_root对应的文件系统挂载到/
	touch_mnt_namespace(current->nsproxy->mnt_ns);
	spin_unlock(&vfsmount_lock);
	chroot_fs_refs(&root, &new); ///修改进程的root/pwd信息

…
}
```

在调用pivot_root时，需要注意：

The following restrictions apply to new_root and put_old:

* （1）They must be directories.
必须都是目录，也就是说不能是文件；

* （2）new_root and put_old must not be on the same filesystem as the current root.
new_root与put_old不能与当前进程的根目录在同一个文件系统；也就是说new_root与put_old与root必须是两个不同的vfsmount； 这从代码可以看出：

* （3）put_old must be underneath new_root, that is, adding a nonzero number of /.. to the string pointed to by put_old must yield the same directory as new_root.
目录put_old必须在new_root目录下，也就是说put_old/..与new_root是同一个目录；

* （4）No other filesystem may be mounted on put_old.


疑问：

为什么pivot_root没有修改mnt_namespace->root?

pivot_root与chroot的一点区别：

* （1）pivot_root会修改进程的根文件系统，chroot不会；
* （2）pivot_root会修改进程的根目录、工作目录，chroot只会修改工作；

来看看pivot_root在lxc中的使用：

```c
static int setup_rootfs_pivot_root(const char *rootfs, const char *pivotdir)
{
	/* change into new root fs */
  ///ex: /usr/lib/x86_64-linux-gnu/lxc
	if (chdir(rootfs)) {
		SYSERROR("can't chdir to new rootfs '%s'", rootfs);
		return -1;
	}

	/// /usr/lib/x86_64-linux-gnu/lxc/lxc_putold
	if (!pivotdir)
		pivotdir = "lxc_putold";

	/* compute the full path to pivotdir under rootfs */
	rc = snprintf(path, sizeof(path), "%s/%s", rootfs, pivotdir);

	/* pivot_root into our new root fs */
  ///以/usr/lib/x86_64-linux-gnu/lxc作为根文件系统，该文件系统一般通过mount -o bind /var/lib/lxc/xxx/rootfs /usr/lib/x86_64-linux-gnu/lxc生成
	if (pivot_root(".", path)) {
		SYSERROR("pivot_root syscall failed");
		return -1;
	}

	///转到根目录
	if (chdir("/")) {
		SYSERROR("can't chdir to / after pivot_root");
		return -1;
	}

	///umount旧的根文件系统下，挂载的其它文件系统；这通过读取pivotdir/proc/mounts得到
	/* we switch from absolute path to relative path */
	if (umount_oldrootfs(pivotdir))

}
```

在调用setup_rootfs_pivot_root之前，lxc会将容器的rootfs目录，比如（lxc.rootfs = /var/lib/lxc/vm1/rootfs）bind mount到LXCROOTFSMOUNT（比如/usr/lib/x86_64-linux-gnu/lxc）。


我们通过一些命令来模拟该过程：

```sh
# unshare -m /bin/bash
# mount -o bind /var/lib/lxc/vm1/rootfs /usr/lib/x86_64-linux-gnu/lxc
# mount -t proc proc /usr/lib/x86_64-linux-gnu/lxc/proc
# mount -t sysfs sysfs /usr/lib/x86_64-linux-gnu/lxc/sys

# mkdir /usr/lib/x86_64-linux-gnu/lxc/lxc_putold
# cd /usr/lib/x86_64-linux-gnu/lxc
# pivot_root . /usr/lib/x86_64-linux-gnu/lxc/lxc_putold

###umount /lxc_putold下面所有之前安装的文件系统：
# umount /lxc_putold/xxx

# cat /proc/self/mountinfo
97 71 252:0 /var/lib/lxc/vm1/rootfs / rw,relatime - ext4 /dev/mapper/ubuntu--vg-root rw,errors=remount-ro,data=ordered
98 97 0:3 / /proc rw,relatime - proc proc rw
99 97 0:15 / /sys rw,relatime - sysfs sysfs rw
```

这样，就看不到之前的所有安装的文件系统了。


主要参考:

http://man7.org/linux/man-pages/man2/pivot_root.2.html


