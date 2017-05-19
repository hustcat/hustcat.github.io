## procfs

```c
/* fs/proc/root.c
 * This is the root "inode" in the /proc tree..
 */
struct proc_dir_entry proc_root = {
	.low_ino	= PROC_ROOT_INO, 
	.namelen	= 5, 
	.mode		= S_IFDIR | S_IRUGO | S_IXUGO, 
	.nlink		= 2, 
	.count		= ATOMIC_INIT(1),
	.proc_iops	= &proc_root_inode_operations, 
	.proc_fops	= &proc_root_operations,
	.parent		= &proc_root,
	.name		= "/proc",
};


int proc_fill_super(struct super_block *s)
{
	struct inode *root_inode;

	s->s_flags |= MS_NODIRATIME | MS_NOSUID | MS_NOEXEC;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = PROC_SUPER_MAGIC;
	s->s_op = &proc_sops;
	s->s_time_gran = 1;
	
	pde_get(&proc_root);
	root_inode = proc_get_inode(s, &proc_root); ///alloc inode for proc dentry
	if (!root_inode) {
		pr_err("proc_fill_super: get root inode failed\n");
		return -ENOMEM;
	}

	s->s_root = d_make_root(root_inode);
	if (!s->s_root) {
		pr_err("proc_fill_super: allocate dentry failed\n");
		return -ENOMEM;
	}

	return proc_setup_self(s); ///proc/self
}

```

* lookup

```c
/*
 * Call i_op->lookup on the dentry.  The dentry must be negative but may be
 * hashed if it was pouplated with DCACHE_NEED_LOOKUP.
 *
 * dir->d_inode->i_mutex must be held
 */
static struct dentry *lookup_real(struct inode *dir, struct dentry *dentry,
				  unsigned int flags)
{
	struct dentry *old;

	/* Don't create child dentry for a dead directory. */
	if (unlikely(IS_DEADDIR(dir))) {
		dput(dentry);
		return ERR_PTR(-ENOENT);
	}
	///dir is parent inode, dentry is child dentry object
	old = dir->i_op->lookup(dir, dentry, flags); ///proc_root_lookup
	if (unlikely(old)) {
		dput(dentry); ///目录项已经存在，释放新分配的dentry，使用旧的dentry
		dentry = old;
	}
	return dentry;
}


static struct dentry *proc_root_lookup(struct inode * dir, struct dentry * dentry, unsigned int flags)
{
	if (!proc_lookup(dir, dentry, flags))
		return NULL; ///old dentry is exist
	
	return proc_pid_lookup(dir, dentry, flags); ///alloc inode for dentry
}
```

* mount

```
# unshare -m /bin/bash
# mount --make-rslave /
# nsenter --target=14124 --pid  -- /bin/bash
# umount -l /proc
# cat /proc/mounts
...
none /var/run/docker/netns/default proc rw,relatime 0 0
none /var/run/docker/netns/96761cbf926b proc rw,relatime 0 0
proc /proc proc rw,relatime 0 0
# umount -l /proc
# cat /proc/mounts 
cat: /proc/mounts: No such file or directory
# mount -t proc proc /proc
# ps -ef
UID        PID  PPID  C STIME TTY          TIME CMD
root         1     0  0 16:32 ?        00:00:00 /usr/sbin/sshd -D
root       129     0  0 17:40 pts/14   00:00:00 /bin/bash
root       148   129  0 17:41 pts/14   00:00:00 ps -ef

# stat /proc/1
  File: `/proc/1'
  Size: 0               Blocks: 0          IO Block: 1024   directory
Device: 20h/32d Inode: 1040776094  Links: 7
Access: (0555/dr-xr-xr-x)  Uid: (    0/    root)   Gid: (    0/    root)
Access: 2017-05-19 16:33:00.829295372 +0800
Modify: 2017-05-19 16:33:00.829295372 +0800
Change: 2017-05-19 16:33:00.829295372 +0800

# stat /proc              
  File: `/proc'
  Size: 0               Blocks: 0          IO Block: 1024   directory
Device: 20h/32d Inode: 1           Links: 260
Access: (0555/dr-xr-xr-x)  Uid: (    0/    root)   Gid: (    0/    root)
Access: 2017-05-19 16:33:00.148955354 +0800
Modify: 2017-05-19 16:33:00.148955354 +0800
Change: 2017-05-19 16:33:00.148955354 +0800

# exit
# exit
# stat /proc/14124
  File: `/proc/14124'
  Size: 0               Blocks: 0          IO Block: 1024   directory
Device: 3h/3d   Inode: 1040776053  Links: 7
Access: (0555/dr-xr-xr-x)  Uid: (    0/    root)   Gid: (    0/    root)
Access: 2017-05-19 16:33:00.148955354 +0800
Modify: 2017-05-19 16:33:00.148955354 +0800
Change: 2017-05-19 16:33:00.148955354 +0800



# docker exec -it vm101 /bin/bash
[root@7f5a636ff840 /]# stat /proc/1
  File: `/proc/1'
  Size: 0               Blocks: 0          IO Block: 1024   directory
Device: 20h/32d Inode: 1040776094  Links: 7
Access: (0555/dr-xr-xr-x)  Uid: (    0/    root)   Gid: (    0/    root)
Access: 2017-05-19 08:33:00.829295372 +0000
Modify: 2017-05-19 08:33:00.829295372 +0000
Change: 2017-05-19 08:33:00.829295372 +0000

# stat /proc
  File: `/proc'
  Size: 0               Blocks: 0          IO Block: 1024   directory
Device: 20h/32d Inode: 1           Links: 260
Access: (0555/dr-xr-xr-x)  Uid: (    0/    root)   Gid: (    0/    root)
Access: 2017-05-19 08:33:00.148955354 +0000
Modify: 2017-05-19 08:33:00.148955354 +0000
Change: 2017-05-19 08:33:00.148955354 +0000
```

相同pid namespace看到的/proc/$pid 对应的inode是相同的，而且/proc对应的inode也是相同的。

实际上，每个pid namespace只会对应一个procfs实例：

```c
static struct dentry *proc_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	int err;
	struct super_block *sb;
	struct pid_namespace *ns;
	char *options;

	if (flags & MS_KERNMOUNT) {
		ns = (struct pid_namespace *)data;
		options = NULL;
	} else {
		ns = task_active_pid_ns(current); ///pid namespace
		options = data;

		if (!current_user_ns()->may_mount_proc ||
		    !ns_capable(ns->user_ns, CAP_SYS_ADMIN))
			return ERR_PTR(-EPERM);
	}

	sb = sget(fs_type, proc_test_super, proc_set_super, flags, ns);
}

struct super_block *sget(struct file_system_type *type,
			int (*test)(struct super_block *,void *),
			int (*set)(struct super_block *,void *),
			int flags,
			void *data)
{
	struct super_block *s = NULL;
	struct super_block *old;
	int err;

retry:
	spin_lock(&sb_lock);
	if (test) {
		hlist_for_each_entry(old, &type->fs_supers, s_instances) {
			if (!test(old, data))///proc_test_super
				continue;
			if (!grab_super(old))  ///old pidname == data
				goto retry;
			if (s) {
				up_write(&s->s_umount);
				destroy_super(s);
				s = NULL;
			}
			return old;
		}
	}
///...
}

static int proc_test_super(struct super_block *sb, void *data)
{
	return sb->s_fs_info == data;
}
```

如果pid namespace已经存在一个procfs，直接使用已经存在的procfs实例。

```
# ./funcgraph proc_mount          
Tracing "proc_mount"... Ctrl-C to end.
 4)               |  proc_mount() {
 4)   0.102 us    |    task_active_pid_ns();
 4)               |    ns_capable() {
 4)   0.050 us    |      cap_capable();
 4)   0.466 us    |    }
 4)               |    sget() {
 4)   0.056 us    |      _raw_spin_lock();
 4)   0.140 us    |      proc_test_super();
 4)               |      grab_super() {
 4)   0.116 us    |        down_write();
 4)               |        put_super() {
 4)   0.052 us    |          _raw_spin_lock();
 4)   0.065 us    |          __put_super();
 4)   0.819 us    |        }
 4)   1.910 us    |      }
 4)   3.375 us    |    }
 4)   0.124 us    |    proc_parse_options();
 4)   0.084 us    |    _raw_spin_lock();
 4)   6.729 us    |  }
```

## drop cache

```
/// proc_sys_call_handler call this
int drop_caches_sysctl_handler(ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (ret)
		return ret;
	if (write) {
		if (sysctl_drop_caches & 1) ///drop page cache
			iterate_supers(drop_pagecache_sb, NULL);
		if (sysctl_drop_caches & 2)///drop  slab
			drop_slab();
	}
	return 0;
}
```

`drop_slab` -> `shrink_slab` -> `do_shrinker_shrink` -> `prune_super` -> `prune_dcache_sb` / `prune_icache_sb`.
