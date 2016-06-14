---
layout: post
title: Ceph - pool has too few pgs
date: 2016-01-08 18:00:30
categories: Linux
tags: ceph
excerpt: Ceph - pool has too few pgs
---

## 问题

Ceph集群出现下面的warning：

```sh
# ceph -s
    cluster bb1b2753-e46d-48e9-8259-cb5fddcff989
     health HEALTH_WARN
            pool .cn-sh.rgw.buckets has too few pgs
...
     osdmap e391: 36 osds: 36 up, 36 in
      pgmap v3896204: 2848 pgs, 19 pools, 60860 MB data, 25157 objects
            180 GB used, 66675 GB / 66855 GB avail
                2848 active+clean
 
# ceph osd dump|grep .cn-sh.rgw.buckets
pool 41 '.cn-sh.rgw.buckets' replicated size 3 min_size 2 crush_ruleset 0 object_hash rjenkins pg_num 128 pgp_num 128 last_change 355 flags hashpspool stripe_width 0
```

可以通过下面的命令查看ceph集群的详细信息：

```sh
# ceph health detail
HEALTH_WARN pool .cn-sh.rgw.buckets has too few pgs
pool .cn-sh.rgw.buckets objects per pg (194) is more than 24.25 times cluster average (8)
```

从上面可以看到，pool '.cn-sh.rgw.buckets'的object数量太多，平均每个pg存储194个object，超过整个集群的平均水平(8)。

```sh
# ceph df detail
GLOBAL:
    SIZE       AVAIL      RAW USED     %RAW USED     OBJECTS 
    66855G     66675G         180G          0.27       25157 
POOLS:
    NAME                         ID     CATEGORY     USED       %USED     MAX AVAIL     OBJECTS     DIRTY     READ       WRITE 
    rbd                          2      -             1024M         0        22179G         258       258          3      2050 
    .cn-sh.users.uid             33     -               325         0        22179G           2         2        962       589 
    .cn-sh.users                 34     -                 9         0        22179G           1         1         14         1 
    .cn.rgw.root                 35     -               278         0        22179G           1         1         27         2 
    .cn-sh.domain.rgw            36     -               658         0        22179G           4         4        110        33 
    .cn-sh.rgw.root              37     -               931         0        22179G           1         1         21         2 
    .cn-sh.rgw.control           38     -                 0         0        22179G           8         8          0         0 
    .cn-sh.rgw.gc                39     -                 0         0        22179G          32        32       183k      123k 
    .cn-sh.rgw.buckets.index     40     -                 0         0        22179G           4         4     19874k      153k 
    .cn-sh.rgw.buckets           41     -            59836M      0.09        22179G       24836     24836       176M      287k 
    .cn-sh.log                   42     -                 0         0        22179G           7         7        757      1514 
    .cn-sh.intent-log            43     -                 0         0        22179G           0         0          0         0 
    .cn-sh.cnage                 44     -                 0         0        22179G           0         0          0         0 
    .cn-sh.cners                 45     -                 0         0        22179G           0         0          0         0 
    .cn-sh.cners.email           46     -                 0         0        22179G           0         0          0         0 
    .cn-sh.cners.swift           47     -                 0         0        22179G           0         0          0         0 
    .cn-sh.cners.uid             48     -                 0         0        22179G           0         0          0         0 
    .rgw.root                    58     -               848         0        22179G           3         3          9         3 
    .rgw.control                 59     -                 0         0        22179G           0         0          0         0
```

可以算一下，

```
.cn-sh.rgw.buckets objects_per_pg = 24836/128 = 194
cluster objects_per_pg = 25157/2848 = 8.8
```

这主要是由于其它pool（比如.cn-sh.users）的object数量太少，导致.cn-sh.rgw.buckets与其它pool的偏差太大。


```c
//mon/PGMonitor.cc
void PGMonitor::get_health(list<pair<health_status_t,string> >& summary,
			   list<pair<health_status_t,string> > *detail) const
{
...
  if (!pg_map.pg_stat.empty()) {
    for (ceph::unordered_map<int,pool_stat_t>::const_iterator p = pg_map.pg_pool_sum.begin();
	 p != pg_map.pg_pool_sum.end();
	 ++p) {
...
		///整个集群objects_per_pg
    	int average_objects_per_pg = pg_map.pg_sum.stats.sum.num_objects / pg_map.pg_stat.size();
      	if (average_objects_per_pg > 0 &&
	  	pg_map.pg_sum.stats.sum.num_objects >= g_conf->mon_pg_warn_min_objects &&
	  	p->second.stats.sum.num_objects >= g_conf->mon_pg_warn_min_pool_objects) {

	  		///pool的objects_per_pg
			int objects_per_pg = p->second.stats.sum.num_objects / pi->get_pg_num();

			///两者的比值
			float ratio = (float)objects_per_pg / (float)average_objects_per_pg;
			if (g_conf->mon_pg_warn_max_object_skew > 0 &&
	    		ratio > g_conf->mon_pg_warn_max_object_skew) {
	  			ostringstream ss;
	  			ss << "pool " << mon->osdmon()->osdmap.get_pool_name(p->first) << " has too few pgs";
	  			summary.push_back(make_pair(HEALTH_WARN, ss.str()));
	  			if (detail) {
	    			ostringstream ss;
	    			ss << "pool " << mon->osdmon()->osdmap.get_pool_name(p->first) << " objects per pg ("
	       			<< objects_per_pg << ") is more than " << ratio << " times cluster average ("
	       			<< average_objects_per_pg << ")";
	    			detail->push_back(make_pair(HEALTH_WARN, ss.str()));
	  			}
		}
    } //end for
...
```

*** mon_pg_warn_max_object_skew ***

Ceph中，mon_pg_warn_max_object_skew的默认值为10。

```c
//src/common/config_opts.h
OPTION(mon_pg_warn_max_object_skew, OPT_FLOAT, 10.0) // max skew few average in objects per pg
```

*** 偏差太大的影响？ ***

TODO:


## 解决方法

为了解决偏差，一方面可以减小其它object数量少的pool的pg数量；不过，ceph当前只支持增加pg，不支持减少pg数量：

```sh
# ceph osd pool set .cn-sh.rgw.control pg_num 32
Error EEXIST: specified pg_num 32 <= current 64
```

*** 为什么不支持？ ***

TODO:


另外可以增加当前pool的pg数量。不过，pg太大，也就失去pg的意义了。pg数量的选择，参考[CHOOSING THE NUMBER OF PLACEMENT GROUPS](http://docs.ceph.com/docs/master/rados/operations/placement-groups/#choosing-the-number-of-placement-groups)。


## 相关资料

* [HEALTH_WARN pool .rgw.buckets has too few pgs](http://lists.ceph.com/pipermail/ceph-users-ceph.com/2013-December/036054.html)
* [PLACEMENT GROUPS](http://docs.ceph.com/docs/master/rados/operations/placement-groups/)
