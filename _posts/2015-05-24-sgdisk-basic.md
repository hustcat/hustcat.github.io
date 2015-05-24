---
layout: post
title: sgdisk常用操作
date: 2015-05-24 16:24:30
categories: Linux
tags: 存储
excerpt: sgdisk常用操作。
---

与fdisk创建MBR分区一样，sgdisk是一个创建GPT分区的工具，如果你还不了解GPT分区，请参考[The difference between booting MBR and GPT with GRUB](http://www.anchor.com.au/blog/2012/10/the-difference-between-booting-mbr-and-gpt-with-grub/)。

# 查看所有GPT分区

```sh
# sgdisk -p /dev/sdb
Disk /dev/sdb: 16780288 sectors, 8.0 GiB
Logical sector size: 512 bytes
Disk identifier (GUID): 4D5B29E8-6E0B-45DA-8E52-A21910E74479
Partition table holds up to 128 entries
First usable sector is 34, last usable sector is 16780254
Partitions will be aligned on 2048-sector boundaries
Total free space is 4061 sectors (2.0 MiB)

Number  Start (sector)    End (sector)  Size       Code  Name
   1        10487808        16780254   3.0 GiB     FFFF  ceph data
   2            2048        10485760   5.0 GiB     FFFF  ceph journal
```

# 查看某个分区的详细的信息

```sh
#/usr/sbin/sgdisk --info=1 /dev/sdb
Partition GUID code: 89C57F98-2FE5-4DC0-89C1-F3AD0CEFF2BE (Unknown)
Partition unique GUID: C8D04950-18E6-4102-A867-B874CF94EA74
First sector: 10487808 (at 5.0 GiB)
Last sector: 16780254 (at 8.0 GiB)
Partition size: 6292447 sectors (3.0 GiB)
Attribute flags: 0000000000000000
Partition name: 'ceph data'
```

# 删除所有分区

```sh
# sgdisk --zap-all --clear --mbrtogpt /dev/sdb
GPT data structures destroyed! You may now partition the disk using fdisk or
other utilities.
The operation has completed successfully.
```

# 创建分区

创建分区2，扇区从2048到10485760，type code为8300。

```sh
# sgdisk -n 2:2048:10485760 -t 2:8300 -p /dev/sdb
Disk /dev/sdb: 16780288 sectors, 8.0 GiB
Logical sector size: 512 bytes
Disk identifier (GUID): 5888A491-1245-4B40-8AEA-A6AEB2C302BB
Partition table holds up to 128 entries
First usable sector is 34, last usable sector is 16780254
Partitions will be aligned on 2048-sector boundaries
Total free space is 6296508 sectors (3.0 GiB)

Number  Start (sector)    End (sector)  Size       Code  Name
   2            2048        10485760   5.0 GiB     8300  
Warning: The kernel is still using the old partition table.
The new table will be used at the next reboot.
The operation has completed successfully.

# sgdisk -n 1:2048 -t 1:8300 -p /dev/sdb
Disk /dev/sdb: 7813837232 sectors, 3.6 TiB
Logical sector size: 512 bytes
Disk identifier (GUID): 361860D7-33F5-45E6-9A86-406FE19B1C36
Partition table holds up to 128 entries
First usable sector is 34, last usable sector is 7813837198
Partitions will be aligned on 2048-sector boundaries
Total free space is 4294969310 sectors (2.0 TiB)

Number  Start (sector)    End (sector)  Size       Code  Name
   1            2048      3518869902   1.6 TiB     8300  
The operation has completed successfully.
```

# 删除指定的分区

删除分区2。

```sh
# sgdisk --delete=2 /dev/sdb
Warning: The kernel is still using the old partition table.
The new table will be used at the next reboot.
The operation has completed successfully.
```

# 主要参考

* [An sgdisk Walkthrough](http://www.rodsbooks.com/gdisk/sgdisk-walkthrough.html)
