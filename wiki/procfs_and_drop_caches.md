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
