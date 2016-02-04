---
layout: post
title: cgroup pgpgin与系统的pgpgin的区别
date: 2016-02-04 17:00:30
categories: Linux
tags: cgroup
excerpt: cgroup pgpgin与系统的pgpgin的区别
---

# 前言

VM有一个pgpgin统计参数，cgroup也有一个pgpgin统计参数，两者虽然都叫“pgpgin”，却TMD不是同一个东西。之前一直忽略了这里的区别。

# VM pgpgin

/proc/vmstat有一个pgpgin/pgpgout统计参数，用于统计系统层面的换进/换出的page数量：

```
//block/block-core.c
void submit_bio(int rw, struct bio *bio)
{
 int count = bio_sectors(bio);
 bio->bi_rw |= rw;
 /*
  * If it's a regular read/write or a barrier with data attached,
  * go through the normal accounting stuff before submission.
  */
 if (bio_has_data(bio)) {
  if (rw & WRITE) {
   count_vm_events(PGPGOUT, count);
  } else {
   task_io_account_read(bio->bi_size);
   count_vm_events(PGPGIN, count);
  }
  if (unlikely(block_dump)) {
   char b[BDEVNAME_SIZE];
   printk(KERN_DEBUG "%s(%d): %s block %Lu on %s\n",
   current->comm, task_pid_nr(current),
    (rw & WRITE) ? "WRITE" : "READ",
    (unsigned long long)bio->bi_sector,
    bdevname(bio->bi_bdev, b));
  }
 }
 generic_make_request(bio);
}
```

可以看到，内核在将bio下发给块层的入口submit_bio完成pgpgin/pgpgout的累加的。由于submit_bio是数据从内存到磁盘的唯一入口，所以pgpgin/pgpgout是准确的。


# cgroup pgpgin/pgpgout

cgroup也有一个pgpgin/pgpgout的统计参数：

```
pgpgin		- # of charging events to the memory cgroup. The charging
		event happens each time a page is accounted as either mapped
		anon page(RSS) or cache page(Page Cache) to the cgroup.
pgpgout		- # of uncharging events to the memory cgroup. The uncharging
		event happens each time a page is unaccounted from the cgroup.
```

虽然名字都是pgpgin/pgpgout，但是实际上不是同一回事。首先，两者的单位都不一样，系统层面的是page的数量，而cgroup是in/out的次数。另外，统计的实现也不一样。

```
/* mm/memcontrol.c
 * Statistics for memory cgroup.
 */
enum mem_cgroup_stat_index {
	/*
	 * For MEM_CONTAINER_TYPE_ALL, usage = pagecache + rss.
	 */
	MEM_CGROUP_STAT_CACHE, 	   /* # of pages charged as cache */
	MEM_CGROUP_STAT_RSS,	   /* # of pages charged as anon rss */
	MEM_CGROUP_STAT_FILE_MAPPED,  /* # of pages charged as file rss */
	MEM_CGROUP_STAT_PGPGIN_COUNT,	/* # of pages paged in */
	MEM_CGROUP_STAT_PGPGOUT_COUNT,	/* # of pages paged out */
	MEM_CGROUP_STAT_EVENTS,	/* sum of pagein + pageout for internal use */
	MEM_CGROUP_STAT_SWAPOUT, /* # of pages, swapped out */

	MEM_CGROUP_STAT_NSTATS,
};

static void mem_cgroup_charge_statistics(struct mem_cgroup *mem,
      struct page_cgroup *pc,
      long size)
{
 struct mem_cgroup_stat *stat = &mem->stat;
 struct mem_cgroup_stat_cpu *cpustat;
 long numpages = size >> PAGE_SHIFT;
 int cpu = get_cpu();
 cpustat = &stat->cpustat[cpu];
 if (PageCgroupCache(pc))
  __mem_cgroup_stat_add_safe(cpustat,
   MEM_CGROUP_STAT_CACHE, numpages);
 else
  __mem_cgroup_stat_add_safe(cpustat, MEM_CGROUP_STAT_RSS,
   numpages);
 if (numpages > 0)
  __mem_cgroup_stat_add_safe(cpustat,
    MEM_CGROUP_STAT_PGPGIN_COUNT, 1);
 else
  __mem_cgroup_stat_add_safe(cpustat,
    MEM_CGROUP_STAT_PGPGOUT_COUNT, 1);
 __mem_cgroup_stat_add_safe(cpustat, MEM_CGROUP_STAT_EVENTS, 1);
 put_cpu();
}
```

可以看到，cgroup是在函数mem_cgroup_charge_statistics完成MEM_CGROUP_STAT_PGPGIN_COUNT/MEM_CGROUP_STAT_PGPGOUT_COUNT加1操作。而上层却有多个地方调用该函数：

![](/assets/2016-02-04-cgroup-pgpgin-stat.jpg)

从这里可以得到一个很重要的结论，即使对于anon page，也会影响pgpgin/pgpgout。下面进行验证。

# 测试

## 程序

程序逻辑很简单，分配102400个page：

```c
/*gcc -o test test.c*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BUF_SIZE (4096)
#define TIMES 102400
char * p[TIMES];
int main(){
        int i;
        for (i = 0; i < TIMES ; i++){
                p[i] = malloc(BUF_SIZE);
                memset(p[i], 0, BUF_SIZE); 
        }
        printf("sleep....\n");
        sleep(10);
}
```

## 结果

来看看memory.stat的变化。

* 程序运行前

```
cache 25632768
rss 475136
mapped_file 1601536
pgpgin 38347
pgpgout 31973
swap 0
inactive_anon 0
active_anon 475136
inactive_file 15310848
active_file 10321920
```

* 内存分配完成

```
cache 25632768
rss 422449152
mapped_file 1609728
pgpgin 141393
pgpgout 31998
swap 0
inactive_anon 0
active_anon 422416384
inactive_file 15306752
active_file 10326016
```

* 程序退出

```
cache 25632768
rss 475136
mapped_file 1601536
pgpgin 141393
pgpgout 135019
swap 0
inactive_anon 0
active_anon 475136
inactive_file 15306752
active_file 10326016
```

rss/pgpgin/active_anon 3个参数都发生变化，其中pgpgin基本接近102400：

```
pgpgin = 141393 - 38347 = 103046
```

# 总结

看来，cgroup的pgpgin/pgpgout与系统的pgpgin/pgpgout不是同一个玩意儿，不能简单的划等号。

# 相关资料

* [Memory Resource Controller](https://www.kernel.org/doc/Documentation/cgroup-v1/memory.txt)
