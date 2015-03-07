---
layout: post
title: 浅析Linux的共享内存与tmpfs文件系统
date: 2015-03-05 23:58:30
categories: Linux
tags: memory
excerpt: 浅析Linux的共享内存与tmpfs文件系统
---

前言
------
共享内存主要用于进程间通信，Linux有两种共享内存(Shared Memory)机制：

*** System V shared memory(shmget/shmat/shmdt) ***

* Original shared memory mechanism, still widely used
* Sharing between unrelated processes

*** POSIX shared memory(shm_open/shm_unlink) ***

* Sharing between unrelated processes, without overhead of filesystem I/O
* Intended to be simpler and better than older APIs

另外，在Linux中不得不提一下内存映射（也可用于进程间通信）：

*** Shared mappings – mmap(2) ***

* Shared anonymous mappings：Sharing between related processes only (related via fork())
* Shared file mappings：Sharing between unrelated processes, backed by file in filesystem

System V共享内存历史悠久，使用也很广范，很多类Unix系统都支持。一般来说，我们在写程序时也通常使用第一种。这里不再讨论如何使用它们，关于POSIX共享内存的详细介绍可以参考[这里1](http://man7.org/linux/man-pages/man7/shm_overview.7.html),[这里2](http://man7.org/training/download/posix_shm_slides.pdf)。

*** 讲到那么多，那么问题来了，共享内存与tmpfs有什么关系？ ***

> The POSIX shared memory object implementation on Linux 2.4 makes use of a dedicated filesystem, which is normally mounted under /dev/shm.

从这里可以看到，POSIX共享内存是基于tmpfs来实现的。实际上，更进一步，不仅PSM(POSIX shared memory)，而且SSM(System V shared memory)在内核也是基于tmpfs实现的。

tmpfs介绍
------
下面是内核文档中关于tmpfs的介绍：

> tmpfs has the following uses:
> 
> 1) There is always a kernel internal mount which you will not see at all. This is used for shared anonymous mappings and SYSV shared memory. 
>
>    This mount does not depend on CONFIG_TMPFS. If CONFIG_TMPFS is not set, the user visible part of tmpfs is not build. But the internal mechanisms are always present.
>
> 2) glibc 2.2 and above expects tmpfs to be mounted at /dev/shm for POSIX shared memory (shm_open, shm_unlink). Adding the following line to /etc/fstab should take care of this:
>
>	tmpfs	/dev/shm	tmpfs	defaults	0 0
>
>   Remember to create the directory that you intend to mount tmpfs on if necessary.
>
>   This mount is _not_ needed for SYSV shared memory. The internal mount is used for that. (In the 2.3 kernel versions it was necessary to mount the predecessor of tmpfs (shm fs) to use SYSV shared memory)

从这里可以看到tmpfs主要有两个作用：

* （1）用于SYSV共享内存，还有匿名内存映射；这部分由内核管理，用户不可见；
* （2）用于POSIX共享内存，由用户负责mount，而且一般mount到/dev/shm；依赖于CONFIG_TMPFS;

到这里，我们可以了解，SSM与PSM之间的区别，也明白了/dev/shm的作用。

下面我们来做一些测试：

测试
------
我们将/dev/shm的tmpfs设置为64M：

```sh
# mount -size=64M -o remount /dev/shm
# df -lh
Filesystem                  Size  Used Avail Use% Mounted on
tmpfs                          64M     0   64M   0% /dev/shm
```

SYSV共享内存的最大大小为32M：

```sh
# cat /proc/sys/kernel/shmmax
33554432
```

(1)创建65M的system V共享内存失败：
---

```sh
# ipcmk -M 68157440                   
ipcmk: create share memory failed: Invalid argument
```

这是正常的。

(2)将shmmax调整为65M
---

```sh
# echo 68157440 > /proc/sys/kernel/shmmax
# cat /proc/sys/kernel/shmmax             
68157440
# ipcmk -M 68157440                       
Shared memory id: 0
# ipcs -m

------ Shared Memory Segments --------
key        shmid      owner      perms      bytes      nattch     status      
0xef46b249 0          root       644        68157440   0                       
```

可以看到system v共享内存的大小并不受/dev/shm的影响。

(3)创建POSIX共享内存
---

```c
/*gcc -o shmopen shmopen.c -lrt*/
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>

#define MAP_SIZE 68157440

int main(int argc, char *argv[])
{
    int fd;
    void* result;
    fd = shm_open("/shm1", O_RDWR|O_CREAT, 0644);
    if(fd < 0){
        printf("shm_open failed\n");
        exit(1);
}
return 0;
}
```

```sh
# ./shmopen
# ls -lh /dev/shm/shm1 
-rw-r--r-- 1 root root 65M Mar  3 06:19 /dev/shm/shm1
```

仅管/dev/shm只有64M,但创建65M的POSIX SM也可以成功。


(4)向POSIX SM写数据
---

```c
/*gcc -o shmwrite shmwrite.c -lrt*/
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>

#define MAP_SIZE 68157440

int main(int argc, char *argv[])
{
    int fd;
    void* result;
    fd = shm_open("/shm1", O_RDWR|O_CREAT, 0644);
    if(fd < 0){
                printf("shm_open failed\n");
                exit(1);
        }
        if (ftruncate(fd, MAP_SIZE) < 0){
                printf("ftruncate failed\n");
                exit(1);
        }
        result = mmap(NULL, MAP_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if(result == MAP_FAILED){
                printf("mapped failed\n");
                exit(1);
        }
    /* ... operate result pointer */
        printf("memset\n");
        memset(result, 0, MAP_SIZE);
    //shm_unlink("/shm1");
    return 0;
}
```

```sh
# ./shmwrite
memset
Bus error
```

可以看到，写65M的数据会报Bus error错误。

但是，却可以在/dev/shm创建新的文件：

```sh
# ls -lh /dev/shm/ -lh
总用量 64M
-rw-r--r-- 1 root root 65M 3月   3 15:23 shm1
-rw-r--r-- 1 root root 65M 3月   3 15:24 shm2
```

这很正常，ls显示的是inode->size。

```sh
# stat /dev/shm/shm2
  File: "/dev/shm/shm2"
  Size: 68157440        Blocks: 0          IO Block: 4096   普通文件
Device: 10h/16d Inode: 217177      Links: 1
Access: (0644/-rw-r--r--)  Uid: (    0/    root)   Gid: (    0/    root)
Access: 2015-03-03 15:24:28.025985167 +0800
Modify: 2015-03-03 15:24:28.025985167 +0800
Change: 2015-03-03 15:24:28.025985167 +0800
```

(5)向SYS V共享内存写数据
---
将System V共享内存的最大值调整为65M(/dev/shm仍然为64M)。

```sh
# cat /proc/sys/kernel/shmmax
68157440
```

```c
/*gcc -o shmv shmv.c*/
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>
#define MAP_SIZE 68157440

int main(int argc, char** argv)
{
        int shm_id,i;
        key_t key;
        char temp;
        char *p_map;
        char* name = "/dev/shm/shm3";
        key = ftok(name,0);
        if(key==-1)
                perror("ftok error");
        shm_id=shmget(key,MAP_SIZE,IPC_CREAT);
        if(shm_id==-1)
        {
                perror("shmget error");
                return;
        }
        p_map=(char*)shmat(shm_id,NULL,0);
        memset(p_map, 0, MAP_SIZE);
        if(shmdt(p_map)==-1)
                perror(" detach error ");
}
```

```sh
#./shmv
```

却可以正常执行。

(7)结论
---
虽然System V与POSIX共享内存都是通过tmpfs实现，但是受的限制却不相同。也就是说/proc/sys/kernel/shmmax只会影响SYS V共享内存，/dev/shm只会影响Posix共享内存。实际上，System V与Posix共享内存本来就是使用的两个不同的tmpfs实例(instance)。

内核分析
------
内核在初始化时，会自动mount一个tmpfs文件系统，挂载为shm_mnt：

```c
//mm/shmem.c
static struct file_system_type shmem_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "tmpfs",
	.get_sb		= shmem_get_sb,
	.kill_sb	= kill_litter_super,
};

int __init shmem_init(void)
{
...
	error = register_filesystem(&shmem_fs_type);
	if (error) {
		printk(KERN_ERR "Could not register tmpfs\n");
		goto out2;
	}
	///挂载tmpfs(用于SYS V)
	shm_mnt = vfs_kern_mount(&shmem_fs_type, MS_NOUSER,
				 shmem_fs_type.name, NULL);
```

/dev/shm的mount与普通文件mount的流程类似，不再讨论。但是，值得注意的是，/dev/shm默认的大小为当前物理内存的1/2：

shmem_get_sb --> shmem_fill_super

```c
//mem/shmem.c
int shmem_fill_super(struct super_block *sb, void *data, int silent)
{
...
#ifdef CONFIG_TMPFS
	/*
	 * Per default we only allow half of the physical ram per
	 * tmpfs instance, limiting inodes to one per page of lowmem;
	 * but the internal instance is left unlimited.
	 */
	if (!(sb->s_flags & MS_NOUSER)) {///内核会设置MS_NOUSER
		sbinfo->max_blocks = shmem_default_max_blocks();
		sbinfo->max_inodes = shmem_default_max_inodes();
		if (shmem_parse_options(data, sbinfo, false)) {
			err = -EINVAL;
			goto failed;
		}
	}
	sb->s_export_op = &shmem_export_ops;
#else
...

#ifdef CONFIG_TMPFS
static unsigned long shmem_default_max_blocks(void)
{
	return totalram_pages / 2;
}
```

可以看到：由于内核在mount tmpfs时，指定了MS_NOUSER，所以该tmpfs没有大小限制，因此，SYS V共享内存能够使用的内存空间只受/proc/sys/kernel/shmmax限制；而用户通过挂载的/dev/shm，默认为物理内存的1/2。

注意CONFIG_TMPFS.

另外，在/dev/shm创建文件走VFS接口，而SYS V与匿名映射却是通过shmem_file_setup实现：
 
SIGBUS
------
当应用访问共享内存对应的地址空间，如果对应的物理PAGE还没有分配，就会调用fault方法，分配失败，就会返回OOM或者BIGBUS错误：

```c
static const struct vm_operations_struct shmem_vm_ops = {
	.fault		= shmem_fault,
#ifdef CONFIG_NUMA
	.set_policy     = shmem_set_policy,
	.get_policy     = shmem_get_policy,
#endif
};
static int shmem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct inode *inode = vma->vm_file->f_path.dentry->d_inode;
	int error;
	int ret = VM_FAULT_LOCKED;

	error = shmem_getpage(inode, vmf->pgoff, &vmf->page, SGP_CACHE, &ret);
	if (error)
		return ((error == -ENOMEM) ? VM_FAULT_OOM : VM_FAULT_SIGBUS);

	return ret;
}
```

shmem_getpage --> shmem_getpage_gfp：

```c
/*
 * shmem_getpage_gfp - find page in cache, or get from swap, or allocate
 *
 * If we allocate a new one we do not mark it dirty. That's up to the
 * vm. If we swap it in we mark it dirty since we also free the swap
 * entry since a page cannot live in both the swap and page cache
 */
static int shmem_getpage_gfp(struct inode *inode, pgoff_t index,
	struct page **pagep, enum sgp_type sgp, gfp_t gfp, int *fault_type)
{
...
		if (sbinfo->max_blocks) { ///dev/shm会有该值
			if (percpu_counter_compare(&sbinfo->used_blocks,
						sbinfo->max_blocks) >= 0) {
				error = -ENOSPC;
				goto unacct;
			}
			percpu_counter_inc(&sbinfo->used_blocks);
		}
		//分配一个物理PAGE
		page = shmem_alloc_page(gfp, info, index);
		if (!page) {
			error = -ENOMEM;
			goto decused;
		}

		SetPageSwapBacked(page);
		__set_page_locked(page);
		error = mem_cgroup_cache_charge(page, current->mm,
						gfp & GFP_RECLAIM_MASK); ///mem_cgroup检查
		if (!error)
			error = shmem_add_to_page_cache(page, mapping, index,
						gfp, NULL);
```

共享内存与CGROUP
------
目前，共享内存的空间计算在第一个访问共享内存的group，参考：

* http://lwn.net/Articles/516541/
* https://www.kernel.org/doc/Documentation/cgroups/memory.txt

POSIX共享内存与Docker
------
目前Docker将/dev/shm限制为64M，却没有提供参数，这种做法比较糟糕。如果应用使用大内存的POSIX共享内存，必然会导致问题。
参考:

* https://github.com/docker/docker/issues/2606
* https://github.com/docker/docker/pull/4981

总结
------
(1)POSIX共享内存与SYS V共享内存在内核都是通过tmpfs实现，但对应两个不同的tmpfs实例，相互独立。

(2)通过/proc/sys/kernel/shmmax可以限制SYS V共享内存(单个)的最大值，通过/dev/shm可以限制POSIX共享内存的最大值(所有之和)。
