---
layout: post
title: Debug with log in Ceph
date: 2015-09-14 18:27:30
categories: Linux
tags: ceph
excerpt: Debug with log in Ceph
---

在线调整log级别：

```sh
##### find primary osd of object
# ceph osd map pool100 greeting
osdmap e435 pool 'pool100' (91) object 'greeting' -> pg 91.e59e945d (91.1d) -> up ([1,0,2], p1) acting ([1,0,2], p1)

# ceph --admin-daemon /var/run/ceph/ceph-osd.1.asok config show|grep debug_filestore
  "debug_filestore": "1\/3",

# ceph daemon osd.1 config set debug_filestore 15/15
{ "success": ""}
```

读取object

```sh
# rados -p pool100 get greeting /tmp/f1.txt
```

观察osd.1日志输出

```sh
2015-09-14 17:24:53.066365 7ffb0431f700 15 filestore(/var/lib/ceph/osd/ceph-1) getattr 91.1d_head/e59e945d/greeting/head//91 '_'
2015-09-14 17:24:53.066387 7ffb0431f700 10 filestore(/var/lib/ceph/osd/ceph-1) getattr 91.1d_head/e59e945d/greeting/head//91 '_' = 235
2015-09-14 17:24:53.066399 7ffb0431f700 15 filestore(/var/lib/ceph/osd/ceph-1) getattr 91.1d_head/e59e945d/greeting/head//91 'snapset'
2015-09-14 17:24:53.066405 7ffb0431f700 10 filestore(/var/lib/ceph/osd/ceph-1) getattr 91.1d_head/e59e945d/greeting/head//91 'snapset' = 31
2015-09-14 17:24:53.066435 7ffb0431f700 15 filestore(/var/lib/ceph/osd/ceph-1) read 91.1d_head/e59e945d/greeting/head//91 0~5
2015-09-14 17:24:53.066445 7ffb0431f700 10 filestore(/var/lib/ceph/osd/ceph-1) FileStore::read 91.1d_head/e59e945d/greeting/head//91 0~5/5
```

其中倒数第二行对应下面的输出：

```c
int FileStore::read(
  coll_t cid,
  const ghobject_t& oid,
  uint64_t offset,
  size_t len,
  bufferlist& bl,
  uint32_t op_flags,
  bool allow_eio)
{
...
  dout(15) << "read " << cid << "/" << oid << " " << offset << "~" << len << dendl;
```

其中，e59e945d/greeting/head//91为oid(ghobject_t)，91.1d_head为cid(coll_t)，0~5为offset~len。91.1d为pg_t，head为snapid_t。

*** ghobject_t: ***

```c
struct ghobject_t {
  hobject_t hobj;
  gen_t generation;
  shard_t shard_id;
}

ostream& operator<<(ostream& out, const ghobject_t& o)
{
  out << o.hobj;
  if (o.generation != ghobject_t::NO_GEN) {
    assert(o.shard_id != ghobject_t::NO_SHARD);
    out << "/" << o.generation << "/" << o.shard_id;
  }
  return out;
}

struct hobject_t {
  object_t oid;
  snapid_t snap;
  uint32_t hash;
private:
  bool max;
public:
  int64_t pool;
  string nspace;

private:
  string key;
}

ostream& operator<<(ostream& out, const hobject_t& o)
{
  if (o.is_max())
    return out << "MAX";
  out << std::hex << o.hash << std::dec;
  if (o.get_key().length())
    out << "." << o.get_key();
  out << "/" << o.oid << "/" << o.snap;
  out << "/" << o.nspace << "/" << o.pool;
  return out;
}
```

以e59e945d/greeting/head//91为例：

>  e59e945d = hobject_t.hash
>  greeting = hobject_t.oid(即user object id)
>  head = hobject_t.snap
>  91 = hobject_t.pool

*** pg_t: ***

```c
// placement group id
struct pg_t {
  uint64_t m_pool;
  uint32_t m_seed;
  int32_t m_preferred;
}

ostream& operator<<(ostream& out, const pg_t &pg)
{
  out << pg.pool() << '.';      ///m_pool
  out << hex << pg.ps() << dec; ///m_seed

  if (pg.preferred() >= 0)
    out << 'p' << pg.preferred();

  return out;
}
```

*** snapid_t: ***

```c
struct snapid_t {
  uint64_t val;
}

inline ostream& operator<<(ostream& out, snapid_t s) {
  if (s == CEPH_NOSNAP)
    return out << "head";
  else if (s == CEPH_SNAPDIR)
    return out << "snapdir";
  else
    return out << hex << s.val << dec;
}
```

参考

http://ceph.com/docs/master/rados/troubleshooting/log-and-debug/