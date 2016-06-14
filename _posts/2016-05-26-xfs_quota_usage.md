---
layout: post
title: xfs quota usage
date: 2016-05-26 17:00:30
categories: Linux
tags: xfs
excerpt: xfs quota usage
---

## set project quota

Enabling project quota on an XFS filesystem (restrict files in `vm1` directories to only using 1 gigabyte of space).

```sh
# rm -f /etc/projects /etc/projid
# mount -o prjquota /dev/sda4 /data
# xfs_quota -x -c 'project -s -p /data/vm1 1000' /data
# xfs_quota -x -c 'limit -p bhard=1g 1000' /data
```

## project command

```
project [ -cCs [ -d depth ] [ -p path ] id | name ]
      The -c, -C, and -s options allow the directory tree quota
      mechanism to be maintained.  -d allows to limit recursion
      level when processing project directories and -p allows to
      specify project paths at command line ( instead of
      /etc/projects ). All options are discussed in detail below.
```

### setup project

* Q1: implementation

当我们对某个目录调用xfs_quota设置project quota时，会遍历该目录下的所有文件（目录），然后对每个文件设置project id：

```c
///xfsprogs
static int
setup_project(
	const char		*path,
	const struct stat	*stat,
	int			flag,
	struct FTW		*data)
{
...
	if ((fd = open(path, O_RDONLY|O_NOCTTY)) == -1) {
		exitcode = 1;
		fprintf(stderr, _("%s: cannot open %s: %s\n"),
			progname, path, strerror(errno));
		return 0;
	} else if (xfsctl(path, fd, XFS_IOC_FSGETXATTR, &fsx) < 0) {
		exitcode = 1;
		fprintf(stderr, _("%s: cannot get flags on %s: %s\n"),
			progname, path, strerror(errno));
		close(fd);
		return 0;
	}

	fsx.fsx_projid = prid;
	fsx.fsx_xflags |= XFS_XFLAG_PROJINHERIT;
	if (xfsctl(path, fd, XFS_IOC_FSSETXATTR, &fsx) < 0) { ///see xfs_file_ioctl
		exitcode = 1;
		fprintf(stderr, _("%s: cannot set project on %s: %s\n"),
			progname, path, strerror(errno));
	}
	close(fd);
```

In kernel:

xfs_file_ioctl -> xfs_ioc_fssetxattr -> xfs_ioctl_setattr

* Q2: skipping special file

```sh
# xfs_quota -x -c 'project -s vm44' /data        
Setting up project vm44 (path /data/docker/overlay/a698018e8a59c5c440e72a11958eb6aebdbd1f1872e657b8da91a799e8f2ea5a/)...
xfs_quota: skipping special file /data/docker/overlay/a698018e8a59c5c440e72a11958eb6aebdbd1f1872e657b8da91a799e8f2ea5a/upper/etc/mtab
```

XFS quota在setup时，会略过一些特殊文件，比如符号链接、块设备、字符设备、FIFO、socket：

```c
//xfsprogs
static int
setup_project(
	const char		*path,
	const struct stat	*stat,
	int			flag,
	struct FTW		*data)
{
...
	if (EXCLUDED_FILE_TYPES(stat->st_mode)) {
		fprintf(stderr, _("%s: skipping special file %s\n"), progname, path);
		return 0;
	}
```

* Q3: Don't cross mount points

xfs_quota不会跨文件系统设置quota:

```c
///xfsprogs
static void
project_operations(
	char		*project,
	char		*dir,
	int		type)
{
	switch (type) {
	case CHECK_PROJECT:
		printf(_("Checking project %s (path %s)...\n"), project, dir);
		nftw(dir, check_project, 100, FTW_PHYS|FTW_MOUNT);
		break;
	case SETUP_PROJECT:
		printf(_("Setting up project %s (path %s)...\n"), project, dir);
		nftw(dir, setup_project, 100, FTW_PHYS|FTW_MOUNT);
		break;
	case CLEAR_PROJECT:
		printf(_("Clearing project %s (path %s)...\n"), project, dir);
		nftw(dir, clear_project, 100, FTW_PHYS|FTW_MOUNT);
		break;
	}
}
```

FTW_MOUNT and FTW_PHYS:

```
       FTW_MOUNT
              If set, stay within the same filesystem (i.e., do not cross
              mount points).

       FTW_PHYS
              If set, do not follow symbolic links.  (This is what you
              want.)  If not set, symbolic links are followed, but no file
              is reported twice.
```

## limit command

```
limit [ -g | -p | -u ] bsoft=N | bhard=N | isoft=N | ihard=N |
      rtbsoft=N | rtbhard=N -d | id | name
      Set quota block limits (bhard/bsoft), inode count limits
      (ihard/isoft) and/or realtime block limits (rtbhard/rtbsoft).
      The -d option (defaults) can be used to set the default value
      that will be used, otherwise a specific user/group/project
      name or numeric identifier must be specified.
```

* implementation

```c
//xfsprogs
static void
set_limits(
	__uint32_t	id,
	uint		type,
	uint		mask,
	char		*dev,
	__uint64_t	*bsoft,
	__uint64_t	*bhard,
	__uint64_t	*isoft,
	__uint64_t	*ihard, 
	__uint64_t	*rtbsoft,
	__uint64_t	*rtbhard)
{
	fs_disk_quota_t	d;

	memset(&d, 0, sizeof(d));
	d.d_version = FS_DQUOT_VERSION;
	d.d_id = id;
	d.d_flags = type;
	d.d_fieldmask = mask;
	d.d_blk_hardlimit = *bhard;
	d.d_blk_softlimit = *bsoft;
	d.d_ino_hardlimit = *ihard;
	d.d_ino_softlimit = *isoft;
	d.d_rtb_hardlimit = *rtbhard;
	d.d_rtb_softlimit = *rtbsoft;

	if (xfsquotactl(XFS_SETQLIM, dev, type, id, (void *)&d) < 0) {
		exitcode = 1;
		fprintf(stderr, _("%s: cannot set limits: %s\n"),
				progname, strerror(errno));
	}
}
```

In kernel:

`quotactl -> do_quotactl -> xfs_fs_set_xquota`

```c
/* Copy parameters and call proper function */
static int do_quotactl(struct super_block *sb, int type, int cmd, qid_t id,
		       void __user *addr)
{
...
		case Q_XSETQLIM: {
			struct fs_disk_quota fdq;

			if (copy_from_user(&fdq, addr, sizeof(fdq)))
				return -EFAULT;
		       return sb->s_qcop->set_xquota(sb, type, id, &fdq);///xfs_fs_set_xquota
		}
```

## Reference
* [xfs_quota - manage use of quota on XFS filesystems](http://man7.org/linux/man-pages/man8/xfs_quota.8.html)
* [ftw, nftw - file tree walk](http://man7.org/linux/man-pages/man3/nftw.3.html)