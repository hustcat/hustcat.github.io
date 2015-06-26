---
layout: post
title: Ceph practice：RBD snapshot
date: 2015-06-26 20:00:30
categories: Linux
tags: ceph
excerpt: Ceph RBD snapshot操作。
---

# Preparation

* 创建pool

```sh
[root@mon ~]# ceph osd pool create pool1 64
pool 'pool1' created
```

* 删除pool

```sh
[root@mon ~]# ceph osd pool delete pool1 pool1 --yes-i-really-really-mean-it
pool 'pool1' removed
```

* 创建用户

```sh
[root@mon ~]# ceph auth get-or-create client.user1 mon 'allow *' mds 'allow' osd 'allow * pool=pool1' -o /etc/ceph/ceph.client.user1.keyring
```

* 检查用户权限

```sh
[root@mon ~]# ceph auth list
installed auth entries:
client.user1
        key: AQDSw4NVODPjARAAbPIDQMHKuaN39d2EJO7Tbg==
        caps: [mds] allow
        caps: [mon] allow *
        caps: [osd] allow * pool=pool1
```

# RBD block device

* 创建rbd image

```sh
# rbd create -p pool1 user1_image1 --size 102400 --image-format 2 -c /etc/ceph/ceph.conf --id user1 --keyring /etc/ceph/ceph.client.user1.keyring
```

* 查看image信息

```sh
# rbd info pool1/user1_image1 -c /etc/ceph/ceph.conf --id user1 --keyring /etc/ceph/ceph.client.user1.keyring
rbd image 'user1_image1':
        size 102400 MB in 25600 objects
        order 22 (4096 kB objects)
        block_name_prefix: rbd_data.10b82ae8944a
        format: 2
        features: layering
```

* 映射为块设备

```sh
# rbd map pool1/user1_image1 -c /etc/ceph/ceph.conf --id user1 --keyring /etc/ceph/ceph.client.user1.keyring
/dev/rbd1

# ls -lh /dev/rbd1 
brw-rw---- 1 root disk 252, 0 Jun 25 11:59 /dev/rbd1
```

* 使用block device

```sh
# mkfs.xfs /dev/rbd1
log stripe unit (4194304 bytes) is too large (maximum is 256KiB)
log stripe unit adjusted to 32KiB
meta-data=/dev/rbd1              isize=256    agcount=17, agsize=1637376 blks
         =                       sectsz=512   attr=2, projid32bit=0
data     =                       bsize=4096   blocks=26214400, imaxpct=25
         =                       sunit=1024   swidth=1024 blks
naming   =version 2              bsize=4096   ascii-ci=0
log      =internal log           bsize=4096   blocks=12800, version=2
         =                       sectsz=512   sunit=8 blks, lazy-count=1
realtime =none                   extsz=4096   blocks=0, rtextents=0

# mount -o noatime /dev/rbd1 /mnt/rbd1
# df -lh
/dev/rbd1             100G   33M  100G   1% /mnt/rbd1
#cd /mnt/rbd1/
# echo "rbd1" > f1.txt
# cat f1.txt 
rbd1
```

# Snapshot

* 创建快照

```sh
# rbd snap create pool1/user1_image1@user1_image1_snap -c /etc/ceph/ceph.conf --id user1 --keyring /etc/ceph/ceph.client.user1.keyring

# rbd snap ls pool1/user1_image1 -c /etc/ceph/ceph.conf --id user1 --keyring /etc/ceph/ceph.client.user1.keyring
SNAPID NAME                   SIZE 
     6 user1_image1_snap 102400 MB
```

Ceph RBD快照是只读的，可以对快照clone一个新的image（可读写）。

* Clone snapshot

在clone snaptshot之前，需要先protect snapshot，为了防止删除snapshot。

```sh
# rbd snap protect pool1/user1_image1@user1_image1_snap -c /etc/ceph/ceph.conf --id user1 --keyring /etc/ceph/ceph.client.user1.keyring

# rbd clone pool1/user1_image1@user1_image1_snap pool1/user1_image2 -c /etc/ceph/ceph.conf --id user1 --keyring /etc/ceph/ceph.client.user1.keyring
# rbd ls pool1 -c /etc/ceph/ceph.conf --id user1 --keyring /etc/ceph/ceph.client.user1.keyring            user1_image1
user1_image2
```

注意，只有format 2才支持clone。

* 使用cloned image

将cloned image映射成本地block device：

```sh
# rbd map pool1/user1_image2 -c /etc/ceph/ceph.conf --id user1 --keyring /etc/ceph/ceph.client.user1.keyring
/dev/rbd2
```

在mount的时候报错，因为XFS UUID重复：

```sh
# mount -o noatime /dev/rbd2 /mnt/rbd2
mount: wrong fs type, bad option, bad superblock on /dev/rbd2,
       missing codepage or helper program, or other error
       In some cases useful info is found in syslog - try
       dmesg | tail  or so
#dmesg|tail
XFS (rbd2): Filesystem has duplicate UUID 359fa82b-a536-4068-adf0-3d9950d03d80 - can't mount
```

```sh
# umount /mnt/rbd1
# mount -o noatime /dev/rbd2 /mnt/rbd2
# ls /mnt/rbd2
f1.txt
# cat /mnt/rbd2/f1.txt 
rbd1

echo "rbd2" > /mnt/rbd2/f2.txt
# ls /mnt/rbd2/
f1.txt  f2.txt
# umount /mnt/rbd2
# mount -o noatime /dev/rbd1 /mnt/rbd1/
# ls /mnt/rbd1
f1.txt
```

可以看到，在user1_image1中，仍然只有f1.txt一个文件。

# 总结
相对于Device mapper thin provisioning volume，RBD快照只能读，而dm-thin snapshot快照是可以读写的。另外，如果RBD snapshot层次太深，性能如何？？有待确认。

# 主要参考

* http://docs.ceph.com/docs/master/man/8/rbd/
* http://ceph.com/docs/master/rbd/rbd-snapshot/
* http://ceph.com/docs/master/dev/rbd-layering/
