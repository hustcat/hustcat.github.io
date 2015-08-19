---
layout: post
title: Ceph性能调优——Journal与tcmalloc
date: 2015-08-19 18:00:00
categories: Linux
tags: ceph
excerpt: Ceph性能调优——Journal与tcmalloc
---
最近对Ceph做了一下简单的性能测试，发现Journal的性能与tcmalloc的版本对性能影响很大。

# 测试结果

```sh
# rados -p tmppool -b 4096  bench 120 write  -t 32 --run-name test1
```

| object size | bw(MB/s)  | lantency(s) | pool size | journal | tcmalloc version | max thread cache |
|-------------|-----------|-------------|-----------|---------|------------------|------------------|
| 4K | 2.676 | 0.0466848 | 3 | SATA | 2.0 |
| 4K | 3.669 | 0.0340574 | 2 | SATA | 2.0 |
| 4K | 10.169 | 0.0122452 | 2 | SSD | 2.0 |
| 4K | 5.34 | 0.0234077 | 3 | SSD | 2.0 | 
| 4K | 7.62 | 0.0164019 | 3 | SSD | 2.1 |
| 4K | 8.838 | 0.0141392 | 3 | SSD | 2.1 | 2048M |

可以看到：

* （1)SSD journal带来了一倍的性能提升；
* （2)使用tcmalloc 2.1，并调整max thread cache参数后，也带来了将近一倍的性能提升；
* （3)副本数量对性能的影响也很大。

# tcmalloc的问题

Ceph自带的tcmalloc为2.0，测试过程中发现CPU利用率很高，几乎90%：

```
Samples: 265K of event 'cycles', Event count (approx.): 104385445900
+  27.58%  libtcmalloc.so.4.1.0    [.] 
tcmalloc::CentralFreeList::FetchFromSpans()
+  15.25%  libtcmalloc.so.4.1.0    [.] 
tcmalloc::ThreadCache::ReleaseToCentralCache(tcmalloc::ThreadCache::FreeList*, 
unsigned long,
+  12.20%  libtcmalloc.so.4.1.0    [.] 
tcmalloc::CentralFreeList::ReleaseToSpans(void*)
+   1.63%  perf                    [.] append_chain
+   1.39%  libtcmalloc.so.4.1.0    [.] 
tcmalloc::CentralFreeList::ReleaseListToSpans(void*)
+   1.02%  libtcmalloc.so.4.1.0    [.] 
tcmalloc::CentralFreeList::RemoveRange(void**, void**, int)
+   0.85%  libtcmalloc.so.4.1.0    [.] 0x0000000000017e6f
+   0.75%  libtcmalloc.so.4.1.0    [.] 
tcmalloc::ThreadCache::IncreaseCacheLimitLocked()
+   0.67%  libc-2.12.so            [.] memcpy
+   0.53%  libtcmalloc.so.4.1.0    [.] operator delete(void*)
```

这是因为tcmalloc的TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES默认值太小，导致线程竞争引起的。邮件列表已经很多讨论这个问题：

* [Hitting tcmalloc bug even with patch applied](http://comments.gmane.org/gmane.comp.file-systems.ceph.devel/24502)
* [tcmalloc issues](https://www.mail-archive.com/search?l=ceph-users%40lists.ceph.com&q=tcmalloc&start=0)


调整该参数后，性能大幅提升，同时CPU利用率大幅下降。

```
Samples: 280K of event 'cycles', Event count (approx.): 73401082082
  3.92%  libtcmalloc.so.4.1.2    [.] tcmalloc::CentralFreeList::FetchFromSpans()
  3.52%  libtcmalloc.so.4.1.2    [.] tcmalloc::ThreadCache::ReleaseToCentralCache(tcmalloc::ThreadCache::FreeList*, unsigned long, i
  2.41%  libtcmalloc.so.4.1.2    [.] 0x0000000000017dcf
  1.78%  libc-2.12.so            [.] memcpy
  1.37%  libtcmalloc.so.4.1.2    [.] operator delete(void*)
  1.32%  libtcmalloc.so.4.1.2    [.] tcmalloc::CentralFreeList::ReleaseToSpans(void*)
  1.00%  [kernel]                [k] _raw_spin_lock
```

# Journal相关

## Journal大小

Journal大小的选择尊循下面的规则：

> osd journal size = {2 * (expected throughput * filestore max sync interval)}

即osd journal的大小应该设置为(磁盘的带宽 * 同步时间) 的2倍。参考[这里](http://docs.ceph.com/docs/master/rados/configuration/osd-config-ref/#journal-settings)。

## Journal存储介质

由于OSD先写日志，然后异步写数据，所以写journal的速度至关重要。Journal的存储介质的选择参考[这里](http://www.sebastien-han.fr/blog/2014/10/10/ceph-how-to-test-if-your-ssd-is-suitable-as-a-journal-device/)

SSD: Intel s3500 240G的结果：

```sh
# fio --filename=/data/fio.dat --size=5G --direct=1 --sync=1 --bs=4k  --iodepth=1 --numjobs=32 --thread  --rw=write --runtime=120 --group_reporting --time_base --name=test_write
  write: io=3462.8MB, bw=29547KB/s, iops=7386 , runt=120005msec
    clat (usec): min=99 , max=51201 , avg=4328.97, stdev=382.90
     lat (usec): min=99 , max=51201 , avg=4329.26, stdev=382.86
```

## 在线调整journal
* (1)set noout

```sh
# ceph osd set noout
set noout
# ceph -s
    cluster 4a680a44-623f-4f5c-83b3-44483ba91872
     health HEALTH_WARN noout flag(s) set
…
```

* (2)stop all osd

```sh
# service ceph stop osd
```

* (3)flush journal

```sh
# cat flush.sh 
#!/bin/bash
i=12
num=12
end=`expr $i + $num`
while [ $i -lt $end ]
do
        ceph-osd -i $i --flush-journal
        i=$((i+1))
done
```

* (4)change ceph.conf

增加如下内容：

> [osd]
> osd journal = /data/ceph/osd$id/journal
> osd journal size = 5120

* (5)create new journal

```sh
# cat mkjournal.sh 
#!/bin/bash
i=12
num=12
end=`expr $i + $num`
while [ $i -lt $end ]
do
        mkdir -p /data/ceph/osd$i       
        ceph-osd -i $i --mkjournal
        #ceph-osd -i $i --mkjournal     
        i=$((i+1))
done
```

* (6)start ceph-osd deamon

```sh
# service ceph start osd
```

* (7)clear noout

```sh
# ceph osd unset noout
```

## 两个小问题

* 问题1 

在ext3文件系统上，mkjournal会报下面的错误：

> 2015-08-17 14:45:30.588136 7fc865b3a800 -1 journal FileJournal::_open: disabling aio for non-block 
> journal.  Use journal_force_aio to force use of aio anyway
> 2015-08-17 14:45:30.588160 7fc865b3a800 -1 journal FileJournal::_open_file : unable to 
> preallocation journal to 5368709120 bytes: (22) Invalid argument
> 2015-08-17 14:45:30.588171 7fc865b3a800 -1 filestore(/var/lib/ceph/osd/ceph-23) mkjournal error 
> creating journal on /data/ceph/osd23/journal: (22) Invalid argument
> 2015-08-17 14:45:30.588184 7fc865b3a800 -1  ** ERROR: error creating fresh journal 
> /data/ceph/osd23/journal for object store /var/lib/ceph/osd/ceph-23: (22) Invalid argument

这是因为ext3不支持fallocate：

```c++
int FileJournal::_open_file(int64_t oldsize, blksize_t blksize,
          bool create)
{
...
  if (create && (oldsize < conf_journal_sz)) {
    uint64_t newsize(g_conf->osd_journal_size);
    newsize <<= 20;
    dout(10) << "_open extending to " << newsize << " bytes" << dendl;
    ret = ::ftruncate(fd, newsize);
    if (ret < 0) {
      int err = errno;
      derr << "FileJournal::_open_file : unable to extend journal to "
     << newsize << " bytes: " << cpp_strerror(err) << dendl;
      return -err;
    }
    ret = ::posix_fallocate(fd, 0, newsize);
    if (ret) {
      derr << "FileJournal::_open_file : unable to preallocation journal to "
     << newsize << " bytes: " << cpp_strerror(ret) << dendl;
      return -ret;
    }
    max_size = newsize;
  }
```

* 问题2 

当journal为文件时，打开journal文件时，会输出下面的错误：

> 2015-08-19 17:27:48.900894 7f1302791800 -1 journal FileJournal::_open: disabling aio for non-block 
> journal.  Use journal_force_aio to force use of aio anyway

即ceph对于这种情况不会使用aio，为什么呢？？？

```c++
int FileJournal::_open(bool forwrite, bool create)
{
...
  if (S_ISBLK(st.st_mode)) {
    ret = _open_block_device();
  } else {
    if (aio && !force_aio) {
      derr << "FileJournal::_open: disabling aio for non-block journal.  Use "
     << "journal_force_aio to force use of aio anyway" << dendl;
      aio = false; ///不使用aio
    }
    ret = _open_file(st.st_size, st.st_blksize, create);
  }
...
```

# 其它资料

* [TCMalloc : Thread-Caching Malloc](http://gperftools.googlecode.com/svn/trunk/doc/tcmalloc.html)
* [How tcmalloc Works](http://jamesgolick.com/2013/5/19/how-tcmalloc-works.html)
* [Ceph性能优化总结(v0.94)](http://xiaoquqi.github.io/blog/2015/06/28/ceph-performance-optimization-summary/)