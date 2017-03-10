---
layout: post
title: Mount namespace and mount propagation
date: 2017-03-10 15:00:30
categories: Linux
tags: namespace
excerpt: Mount namespace and mount propagation
---

## Mount namespace and problems

When a new mount namespace is created, it receives a copy of the mount point list replicated from the
namespace of the caller of clone() or unshare().

`create_new_namespaces` -> `copy_mnt_ns` -> `dup_mnt_ns`:

```c
/*
 * Allocate a new namespace structure and populate it with contents
 * copied from the namespace of the passed in task structure.
 */
static struct mnt_namespace *dup_mnt_ns(struct mnt_namespace *mnt_ns,
		struct fs_struct *fs)
{
	struct mnt_namespace *new_ns;
	struct vfsmount *rootmnt = NULL, *pwdmnt = NULL;
	struct vfsmount *p, *q;

	new_ns = alloc_mnt_ns();
	if (IS_ERR(new_ns))
		return new_ns;

	down_write(&namespace_sem);
	/* First pass: copy the tree topology */
	new_ns->root = copy_tree(mnt_ns->root, mnt_ns->root->mnt_root,
					CL_COPY_ALL | CL_EXPIRE); ///拷贝原来namespace所有的文件系统
 ///...
 ```
 
 每个Mount namespace有自己独立的文件系统视图，但是这种隔离性同时也带来一些问题：比如，当系统加载一块新的磁盘时，在最初的实现中，每个namespace必须单独挂载磁盘，才能见到。很多时候，我们希望挂载一次，就能在所有的mount namespace可见。为此，内核在2.6.15引入了[shared subtrees feature](https://lwn.net/Articles/159077/)。
 
> The key benefit of shared subtrees is to allow automatic, controlled propagation of mount and unmount events between namespaces. This means, for example, that mounting an optical disk in one mount namespace can trigger a mount of that disk in all
other namespaces.

为了支持`shared subtrees feature`，每个挂载点都会标记`propagation type`，用于决定在当前挂载点下创建/删除（子）挂载点时，是否传播到别的挂载点。

## propagation type

内核有以下几种传播类型：

- MS_SHARED

This mount point shares mount and unmount events with other mount points that are members of its "peer group". When
a mount point is added or removed under this mount point, this change will propagate to the peer group, so that the mount or unmount will also take place under each of the peer mount points. Propagation also occurs in the reverse direction, so that mount and unmount events on a peer mount will also propagate to this mount point.

- MS_PRIVATE

This is the converse of a shared mount point. The mount point does not propagate events to any peers, and does not receive propagation events from any peers.

- MS_SLAVE

This propagation type sits midway between shared and private. A slave mount has a master—a shared peer group whose members propagate mount and unmount events to the slave mount. However, the slave mount does not propagate events to the master peer group.

- MS_UNBINDABLE

This mount point is unbindable. Like a private mount point, this mount point does not propagate events to or from peers. In addition, this mount point can't be the source for a bind mount operation.

几点注意事项：

(1) `propagation type`是对每个挂载点的设置.

(2) `propagation type`决定挂载点的直属(immediately under)子挂载点mount/umount事件的传播.比如，挂载点X下创建新的挂载点Y，Y会扩展到X的`peer group`，但是X不会影响Y下面的子挂载点。

(3) 

## Peer groups

`peer group`就是一些可以相互传播mount/umount事件的挂载点集合. 对于`shared`挂载点，当创建新的mount namspace或者作为bind mount的源目标时，就会创建新的成员。这两种情况都会创建新的挂载点，新的挂载点与原来的挂载点构成`peer group`。同理，当mount namespace释放时，或者挂载点umount时，会从对应的`peer group`删除。

> A peer group is a set of mount points that propagate mount and unmount events to one another. A peer group acquires new members when a mount point whose propagation type is shared is either replicated during the creation of a new namespace or is used as the source for a bind mount. In both cases, the new mount point is made a member of the same peer group as the existing mount point. Conversely, a mount point ceases to be a member of a peer group when it is unmounted, either explicitly, or implicitly when a mount namespace is torn down because the last member process terminates or moves to another namespace.

* 示例

在sh1执行：将`/`设置为`private`，并创建2个`shared`的挂载点:

```
sh1# mount --make-private / 
sh1# mount --make-shared /dev/sda3 /X 
sh1# mount --make-shared /dev/sda5 /Y 
```

然后在sh2执行:创建新的mount namespace:

```
sh2# unshare -m --propagation unchanged sh 
```

然后再在sh1执行：`X` bind mount to `Z`:

```
sh1# mkdir /Z 
sh1# mount --bind /X /Z 
```

这会创建2个`peer group`:

![](/assets/namespace/mount-namespace-01.png)

- 第一个`peer group`包含X, X', 和 Z。其中，X和X'是因为namespace的创建，X和Z是因为bind mount产生的。
- 第二个`peer group`只包含Y, Y'。

注意，因为`/`是`private`的，所以，bind mount Z并不会传播到第二个namespace。

来看Docker使用`private`的代码：

```go
// InitializeMountNamespace sets up the devices, mount points, and filesystems for use inside a
// new mount namespace.
func InitializeMountNamespace(rootfs, console string, sysReadonly bool, mountConfig *MountConfig) error {
	var (
		err  error
		flag = syscall.MS_PRIVATE
	)

	if mountConfig.NoPivotRoot {
		flag = syscall.MS_SLAVE   ///容器中的mount事件不会传播到init ns
	}

	if err := syscall.Mount("", "/", "", uintptr(flag|syscall.MS_REC), ""); err != nil { ///将/设置为private，与init ns完全隔离
		return fmt.Errorf("mounting / with flags %X %s", (flag | syscall.MS_REC), err)
	}

	if err := syscall.Mount(rootfs, rootfs, "bind", syscall.MS_BIND|syscall.MS_REC, ""); err != nil {
		return fmt.Errorf("mouting %s as bind %s", rootfs, err)
	}
///...
```

## Reference
 
 - [Mount namespaces and shared subtrees](https://lwn.net/Articles/689856/)
 - [Mount namespaces, mount propagation, and unbindable mounts](https://lwn.net/Articles/690679/)