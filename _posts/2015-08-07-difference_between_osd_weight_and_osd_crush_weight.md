---
layout: post
title: Ceph osd weight与osd crush weight之间的区别
date: 2015-08-07 16:27:30
categories: Linux
tags: ceph
excerpt: Difference Between 'ceph osd weight' and 'ceph osd crush weight'。
---

这两者的区别实际上是osd weight和osd crush weight的区别：

```sh
# ceph osd tree
# id    weight  type name       up/down reweight
-1      4.08    root default
-2      1.36            host osd-node1
0       1.36                    osd.0   up      1
-3      1.36            host osd-node2
1       1.36                    osd.1   up      1
-4      1.36            host mon
2       1.36                    osd.2   up      1
```

其中，第二列对应osd crush weight，最后一列对应osd weight。

Crush weight实际上为bucket item weight，[下面](http://ceph.com/docs/master/rados/operations/crush-map/#crush-map-bucket-types)是关于bucket item weight的描述：

> Weighting Bucket Items
> 
> Ceph expresses bucket weights as doubles, which allows for fine weighting. A weight is the 
> relative difference between device capacities. We recommend using 1.00 as the relative weight for 
> a 1TB storage device. In such a scenario, a weight of 0.5 would represent approximately 500GB, and
> a weight of 3.00 would represent approximately 3TB. Higher level buckets have a weight that is 
> the sum total of the leaf items aggregated by the bucket.
> 
> A bucket item weight is one dimensional, but you may also calculate your item weights to reflect 
> the performance of the storage drive. For example, if you have many 1TB drives where some have 
> relatively low data transfer rate and the others have a relatively high data transfer rate, you 
> may weight them differently, even though they have the same capacity (e.g., a weight of 0.80 for
>  the first set of drives with lower total throughput, and 1.20 for the second set of drives with 
> higher total throughput).

简单来说，bucket weight表示设备(device)的容量，1TB对应1.00。bucket weight是所有item weight之和，item weight的变化会影响bucket weight的变化，也就是osd.X会影响host。

如果通过ceph-deploy来部署osd，defaultweight的计算：
参考/etc/init.d/ceph

```sh
if [ "$type" = "osd" ]; then
get_conf update_crush "" "osd crush update on start"
if [ "${update_crush:-1}" = "1" -o "{$update_crush:-1}" = "true" ]; then
    # update location in crush
    get_conf osd_location_hook "$BINDIR/ceph-crush-location" "osd crush location hook"
    osd_location=`$osd_location_hook --cluster ceph --id $id --type osd`
    get_conf osd_weight "" "osd crush initial weight"
    defaultweight="$(do_cmd "df $osd_data/. | tail -1 | awk '{ d= \$2/1073741824 ; r = sprintf(\"%.2f\", d); print r }'")"
    get_conf osd_keyring "$osd_data/keyring" "keyring"
    do_cmd "timeout 10 $BINDIR/ceph \
    --name=osd.$id \
    --keyring=$osd_keyring \
    osd crush create-or-move \
    -- \
    $id \
    ${osd_weight:-${defaultweight:-1}} \
    $osd_location"
fi
fi
```

注意

```
defaultweight="$(do_cmd "df $osd_data/. | tail -1 | awk '{ d= \$2/1073741824 ; r = sprintf(\"%.2f\", d); print r }'")"
```

osd weight的取值为0~1。osd reweight并不会影响host。当osd被踢出集群时，osd weight被设置0，加入集群时，设置为1。

> Note that 'ceph osd reweight' is not a persistent setting.  When an OSD
> gets marked out, the osd weight will be set to 0.  When it gets marked in
> again, the weight will be changed to 1.

这篇[文章](http://cephnotes.ksperis.com/blog/2014/12/23/difference-between-ceph-osd-reweight-and-ceph-osd-crush-reweight)做了详细介绍：


下面主要从代码的实现角度来看看两者的区别：

* （1）osd weight

对应OSDMap->osd_weight，它保存了所有OSD的weight：

```c
  unsigned get_weight(int o) const {
    assert(o < max_osd);
    return osd_weight[o];
  }
  float get_weightf(int o) const {
    return (float)get_weight(o) / (float)CEPH_OSD_IN;
  }
```

在pg->OSD的映射过程中，会将osd_weight数组传给CRUSH算法：

```c
//pg --> osds[]
int OSDMap::_pg_to_osds(const pg_pool_t& pool, pg_t pg, vector<int>& osds) const
{
  // map to osds[]
  ps_t pps = pool.raw_pg_to_pps(pg);  // placement ps
  unsigned size = pool.get_size(); //pool size

  // what crush rule?
  int ruleno = crush->find_rule(pool.get_crush_ruleset(), pool.get_type(), size);
  if (ruleno >= 0)
    crush->do_rule(ruleno, pps, osds, size, osd_weight); ///CrushWrapper->do_rule

  _remove_nonexistent_osds(osds);

  return osds.size();
}
```

*** osd weight对CRUSH算法的影响 ***

CRUSH在选择OSD时，如果发现weight为0，就跳过该OSD。

```c
/*
 * true if device is marked "out" (failed, fully offloaded)
 * of the cluster
 */
static int is_out(const struct crush_map *map,
		  const __u32 *weight, int weight_max,
		  int item, int x)
{
	if (item >= weight_max) ///out
		return 1;
	if (weight[item] >= 0x10000) ///in
		return 0;
	if (weight[item] == 0)  ///out
		return 1;
	if ((crush_hash32_2(CRUSH_HASH_RJENKINS1, x, item) & 0xffff)
	    < weight[item])
		return 0;
	return 1;
}
```

*** osd reweight的实现 ***

```c
bool OSDMonitor::prepare_command(MMonCommand *m)
{
...
else if (prefix == "osd reweight") {
    int64_t id;
    cmd_getval(g_ceph_context, cmdmap, "id", id);
    double w;
    cmd_getval(g_ceph_context, cmdmap, "weight", w);
    long ww = (int)((double)CEPH_OSD_IN*w);
    if (ww < 0L) {
      ss << "weight must be > 0";
      err = -EINVAL;
      goto reply;
    }
    if (osdmap.exists(id)) {
      pending_inc.new_weight[id] = ww;
      ss << "reweighted osd." << id << " to " << w << " (" << ios::hex << ww << ios::dec << ")";
      getline(ss, rs);
      wait_for_finished_proposal(new Monitor::C_Command(mon, m, 0, rs, get_last_committed()));
      return true;
    }

  } 
```

* （2）osd crush weight

每个bucket包含若干item，每个item有一个weight，bucket的weight为所有item的weight之和。注意osd也是一种bucket(type=0)。
ceph osd tree直接返回bucket->weight：

```c
  int get_bucket_weight(int id) const {
    const crush_bucket *b = get_bucket(id);
    if (IS_ERR(b)) return PTR_ERR(b);
    return b->weight;
  }
```

bucket->weight为所有item weight之和。以straw bucket为例：

```c
struct crush_bucket_straw {
	struct crush_bucket h;
	__u32 *item_weights;   /* 16-bit fixed point, item weight */
	__u32 *straws;         /* 16-bit fixed point,如果item weight=0,则staw为0 */
};

struct crush_bucket_straw *
crush_make_straw_bucket(int hash, 
			int type,
			int size,
			int *items,
			int *weights)
{

	bucket->h.alg = CRUSH_BUCKET_STRAW;
	bucket->h.hash = hash;
	bucket->h.type = type;
	bucket->h.size = size;
...
    bucket->h.weight = 0;
	for (i=0; i<size; i++) {
		bucket->h.items[i] = items[i];
		bucket->h.weight += weights[i]; ///item weight sum
		bucket->item_weights[i] = weights[i]; ///item weight
	}
...
```

*** osd crush weight对于CRUSH算法的影响 ***

对于straw bucket，如果item weight为0，则item straw也为0，当CRUSH算法在bucket选择item时，也就不太可能选中该item。

```c
/* straw */

static int bucket_straw_choose(struct crush_bucket_straw *bucket,
			       int x, int r)
{
	__u32 i;
	int high = 0;
	__u64 high_draw = 0;
	__u64 draw;
    ///返回draw最大的item
	for (i = 0; i < bucket->h.size; i++) {
		draw = crush_hash32_3(bucket->h.hash, x, bucket->h.items[i], r);
		draw &= 0xffff;
		draw *= bucket->straws[i];
		if (i == 0 || draw > high_draw) {
			high = i;
			high_draw = draw;
		}
	}
	return bucket->h.items[high];
}
```

*** crush osd reweight的实现 ***

整体流程：

![](/assets/2015-08-07-difference-between-osd-weight-and-osd-cruash-weight.png)

具体实现：

```c
///crush osd reweight
int crush_adjust_straw_bucket_item_weight(struct crush_bucket_straw *bucket, int item, int weight)
{
	unsigned idx;
	int diff;
        int r;

	for (idx = 0; idx < bucket->h.size; idx++)
		if (bucket->h.items[idx] == item)
			break;
	if (idx == bucket->h.size)
		return 0;

	diff = weight - bucket->item_weights[idx];
	bucket->item_weights[idx] = weight; ///修改item weight
	bucket->h.weight += diff; ///bucket item

	r = crush_calc_straw(bucket); ///重新计算item straw
    if (r < 0)
            return r;

	return diff;
}
```

* 相关资料

* [Difference Between 'Ceph Osd Reweight' and 'Ceph Osd Crush Reweight'](http://cephnotes.ksperis.com/blog/2014/12/23/difference-between-ceph-osd-reweight-and-ceph-osd-crush-reweight)
* [CRUSH map](http://ceph.com/docs/master/rados/operations/crush-map/#crush-map-bucket-types)
* [[ceph-users] Difference between "ceph osd reweight" and "ceph osd crush reweight"](http://lists.ceph.com/pipermail/ceph-users-ceph.com/2014-June/040967.html)