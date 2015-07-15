---
layout: post
title: Ceph practice：rados object tool
date: 2015-07-15 18:00:30
categories: Linux
tags: ceph
excerpt: Ceph rados object operation。
---

rados对象操作工具，包含很多pool和object的操作命令，该工具相当MySQL的客户端工具mysql，非常强大，可以做很多事情。这些命令基本上都是[librados API](http://docs.ceph.com/docs/master/rados/api/librados/)的封装，-h可以详细查看。

```sh
# rados --help
usage: rados [options] [commands]
POOL COMMANDS
   lspools                          list pools
   mkpool <pool-name> [123[ 4]]     create pool <pool-name>'
                                    [with auid 123[and using crush rule 4]]
   cppool <pool-name> <dest-pool>   copy content of a pool
   rmpool <pool-name> [<pool-name> --yes-i-really-really-mean-it]
                                    remove pool <pool-name>'
   df                               show per-pool and total usage
   ls                               list objects in pool

   chown 123                        change the pool owner to auid 123

OBJECT COMMANDS
   get <obj-name> [outfile]         fetch object
   put <obj-name> [infile]          write object
   truncate <obj-name> length       truncate object
   create <obj-name> [category]     create object
   rm <obj-name> ...                remove object(s)
   cp <obj-name> [target-obj]       copy object
   clonedata <src-obj> <dst-obj>    clone object data
   listxattr <obj-name>
   getxattr <obj-name> attr
   setxattr <obj-name> attr val
   rmxattr <obj-name> attr
   stat objname                     stat the named object
   mapext <obj-name>
   lssnap                           list snaps
   mksnap <snap-name>               create snap <snap-name>
   rmsnap <snap-name>               remove snap <snap-name>
   rollback <obj-name> <snap-name>  roll back object to snap <snap-name>

   listsnaps <obj-name>             list the snapshots of this object
   bench <seconds> write|seq|rand [-t concurrent_operations] [--no-cleanup] [--run-name run_name]
                                    default is 16 concurrent IOs and 4 MB ops
                                    default is to clean up after write benchmark
                                    default run-name is 'benchmark_last_metadata'
   cleanup [--run-name run_name] [--prefix prefix]
                                    clean up a previous benchmark operation
                                    default run-name is 'benchmark_last_metadata'
   load-gen [options]               generate load on the cluster
   listomapkeys <obj-name>          list the keys in the object map
   listomapvals <obj-name>          list the keys and vals in the object map 
   getomapval <obj-name> <key> [file] show the value for the specified key
                                    in the object's object map
   setomapval <obj-name> <key> <val>
   rmomapkey <obj-name> <key>
   getomapheader <obj-name> [file]
   setomapheader <obj-name> <val>
   tmap-to-omap <obj-name>          convert tmap keys/values to omap
   listwatchers <obj-name>          list the watchers of this object
   set-alloc-hint <obj-name> <expected-object-size> <expected-write-size>
                                    set allocation hint for an object
…
```

# Pool相关

## 列出所有pool

```sh
# rados lspools
rbd
pool1
tmppool
```

## 创建/删除pool

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

## 查看每个pool的空间使用及IO情况 

```sh
# rados df    
pool name       category                 KB      objects       clones     degraded      unfound           rd        rd KB           wr        wr KB
pool1           -                     128635           59           19            0           0         2698        85365       108208       686811
rbd             -                          1            2            0            0           0           10            6            5            4
tmppool         -                          1            1            0            0           0           60           39           40           30
  total used          550864           62
  total avail     4367907776
  total space     4368458640
```

## 列出pool中的object

```sh
# rados ls -p pool1
rbd_data.10b82ae8944a.0000000000003200
rbd_data.10b82ae8944a.00000000000031fe
rbd_data.10b82ae8944a.000000000000257a
rbd_data.10b82ae8944a.0000000000005db1
rbd_data.10b1238e1f29.00000000000031f8
rbd_children
rbd_data.10b82ae8944a.00000000000063ff
rbd_data.10b82ae8944a.0000000000005772
rbd_id.user1_image2
```

# Object相关

## 查看object状态

```sh
# rados -p pool1 stat rbd_id.user1_image1
pool1/rbd_id.user1_image1 mtime 1435204702, size 16
```

## 查看object的分布

```sh
# ceph osd map pool1 rbd_id.user1_image1
osdmap e34 pool 'pool1' (3) object 'rbd_id.user1_image1' -> pg 3.dfe58b73 (3.33) -> up ([0,2,1], p0) acting ([0,2,1], p0)
```

## 查看object在osd的存储

```sh
[root@osd-node2 /var/lib/ceph/osd/ceph-1/current/3.33_head/DIR_3]# ls -lh
total 4.0K
-rw-r--r-- 1 root root 16 Jun 25 11:58 rbd\uid.user1\uimage1__head_DFE58B73__3
[root@osd-node2 /var/lib/ceph/osd/ceph-1/current/3.33_head/DIR_3]# hexdump -C rbd\\uid.user1\\uimage1__head_DFE58B73__3 
00000000  0c 00 00 00 31 30 62 38  32 61 65 38 39 34 34 61  |....10b82ae8944a|
```

## 读取object

```sh
# rados -p pool1 get rbd_id.user1_image1 /root/rbd_id.user1_image1
# hexdump -C /root/rbd_id.user1_image1 
00000000  0c 00 00 00 31 30 62 38  32 61 65 38 39 34 34 61  |....10b82ae8944a|
```

## 创建/删除object

```sh
   create <obj-name> [category]     create object
   rm <obj-name> ...                remove object(s)
```

# 参考资料

* [RADOS – RADOS OBJECT STORAGE UTILITY](http://docs.ceph.com/docs/master/man/8/rados/)
* [librados API](http://docs.ceph.com/docs/master/rados/api/librados/)