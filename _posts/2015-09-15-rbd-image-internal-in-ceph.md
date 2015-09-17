---
layout: post
title: RBD image internal in Ceph
date: 2015-09-15 18:27:30
categories: Linux
tags: ceph
excerpt: RBD image internal in Ceph
---

# RBD镜像的存储

当我们在一个空的pool创建一个image：

```sh
# rbd create -p pool100 user1_image1 --size 102400 --image-format 2
```

会看到pool100中多出下面几个object：

```sh
# rados -p pool100 ls
rbd_header.134d2ae8944a
rbd_directory
rbd_id.user1_image1
```

其中rbd_directory保存当前pool的所有image的信息：

```sh
# rados -p pool100 listomapvals  rbd_directory          
id_134d2ae8944a
value: (16 bytes) :
0000 : 0c 00 00 00 75 73 65 72 31 5f 69 6d 61 67 65 31 : ....user1_image1

name_user1_image1
value: (16 bytes) :
0000 : 0c 00 00 00 31 33 34 64 32 61 65 38 39 34 34 61 : ....134d2ae8944a
```

rbd_id.${image_name}(rbd_id.user1_image1)保存rbd image的id，16个字节：

```sh
# rados -p pool100 stat rbd_id.user1_image1               
pool100/rbd_id.user1_image1 mtime 1442282704, size 16
# rados -p pool100 get rbd_id.user1_image1 /tmp/f1.txt
# hexdump -C /tmp/f1.txt 
00000000  0c 00 00 00 31 33 34 64  32 61 65 38 39 34 34 61  |....134d2ae8944a|
```

rbd_header.${image_id}(rbd_header.134d2ae8944a)保存image的元数据信息：

```sh
# rados -p pool100 listomapvals  rbd_header.134d2ae8944a    
features
value: (8 bytes) :
0000 : 01 00 00 00 00 00 00 00                         : ........

object_prefix
value: (25 bytes) :
0000 : 15 00 00 00 72 62 64 5f 64 61 74 61 2e 31 33 34 : ....rbd_data.134
0010 : 64 32 61 65 38 39 34 34 61                      : d2ae8944a

order
value: (1 bytes) :
0000 : 16                                              : .

size
value: (8 bytes) :
0000 : 00 00 00 00 19 00 00 00                         : ........

snap_seq
value: (8 bytes) :
0000 : 00 00 00 00 00 00 00 00                         : ........
```

即为rbd info的信息:

```sh
# rbd -p pool100 info user1_image1
rbd image 'user1_image1':
        size 102400 MB in 25600 objects
        order 22 (4096 kB objects)
        block_name_prefix: rbd_data.134d2ae8944a
        format: 2
        features: layering
```

可以看到，user1_image1的数据对象前缀为rbd_data.134d2ae8944a。

写入8MB的数据：

```sh
# rbd map pool100/user1_image1 
/dev/rbd1
# dd if=/dev/zero of=/dev/rbd1 bs=1048576 count=8

# rados -p pool100 ls
rbd_data.134d2ae8944a.0000000000000000
rbd_data.134d2ae8944a.0000000000000001
rbd_header.134d2ae8944a
rbd_directory
rbd_id.user1_image1
```

可以看到user1_image1多了2个4M的object。

# create image的实现

client

```c
int create_v2(IoCtx& io_ctx, const char *imgname, uint64_t bid, uint64_t size,
		int order, uint64_t features, uint64_t stripe_unit,
		uint64_t stripe_count)
  {
  	///(1)创建rbd_id.<image_name>对象
    id_obj = id_obj_name(imgname); ///rbd_id.<image_name>, object id

    int r = io_ctx.create(id_obj, true); ///create rbd_id.<image_name> object

    ///(2)将image id写到rbd_id.<image_name>
    extra = rand() % 0xFFFFFFFF;
    bid_ss << std::hex << bid << std::hex << extra;
    id = bid_ss.str();
    r = cls_client::set_id(&io_ctx, id_obj, id); ///rbd set_id

    ///(3)exec rbd dir_add_image
    r = cls_client::dir_add_image(&io_ctx, RBD_DIRECTORY, imgname, id); ///rbd dir_add_image

    ///(4)exec rbd create
    oss << RBD_DATA_PREFIX << id; ///"rbd_data."
    header_oid = header_name(id); ///rbd_header.<image_id>
    r = cls_client::create_image(&io_ctx, header_oid, size, order, ///rbd create
				 features, oss.str());

	///(5)exec rbd set_stripe_unit_count
    if ((stripe_unit || stripe_count) &&
	(stripe_count != 1 || stripe_unit != (1ull << order))) {
      r = cls_client::set_stripe_unit_count(&io_ctx, header_oid, ///rbd set_stripe_unit_count
					    stripe_unit, stripe_count);
}
```

*** exec rbd create ***

```c
///csl_rbd_client.cc
int create_image(librados::IoCtx *ioctx, const std::string &oid,
		 uint64_t size, uint8_t order, uint64_t features,
		 const std::string &object_prefix)
{
  bufferlist bl, bl2;
  ::encode(size, bl);
  ::encode(order, bl);
  ::encode(features, bl);
  ::encode(object_prefix, (bl));

  return ioctx->exec(oid, "rbd", "create", bl, bl2);
}

///cls_rbd.cc
/**
 * Initialize the header with basic metadata.
 * Extra features may initialize more fields in the future.
 * Everything is stored as key/value pairs as omaps in the header object.
 *
 * If features the OSD does not understand are requested, -ENOSYS is
 * returned.
 *
 * Input:
 * @param size number of bytes in the image (uint64_t)
 * @param order bits to shift to determine the size of data objects (uint8_t)
 * @param features what optional things this image will use (uint64_t)
 * @param object_prefix a prefix for all the data objects
 *
 * Output:
 * @return 0 on success, negative error code on failure
 */
int create(cls_method_context_t hctx, bufferlist *in, bufferlist *out)
{
  string object_prefix;
  uint64_t features, size;
  uint8_t order;

  try {
    bufferlist::iterator iter = in->begin();
    ::decode(size, iter);
    ::decode(order, iter);
    ::decode(features, iter);
    ::decode(object_prefix, iter);
  } catch (const buffer::error &err) {
    return -EINVAL;
  }

  CLS_LOG(20, "create object_prefix=%s size=%llu order=%u features=%llu",
	  object_prefix.c_str(), (unsigned long long)size, order,
	  (unsigned long long)features);

  if (features & ~RBD_FEATURES_ALL) {
    return -ENOSYS;
  }

  if (!object_prefix.size()) {
    return -EINVAL;
  }

  bufferlist stored_prefixbl;
  int r = cls_cxx_map_get_val(hctx, "object_prefix", &stored_prefixbl);
  if (r != -ENOENT) {
    CLS_ERR("reading object_prefix returned %d", r);
    return -EEXIST;
  }

  bufferlist sizebl;
  ::encode(size, sizebl);
  r = cls_cxx_map_set_val(hctx, "size", &sizebl);
  if (r < 0)
    return r;

  bufferlist orderbl;
  ::encode(order, orderbl);
  r = cls_cxx_map_set_val(hctx, "order", &orderbl);
  if (r < 0)
    return r;

  bufferlist featuresbl;
  ::encode(features, featuresbl);
  r = cls_cxx_map_set_val(hctx, "features", &featuresbl);
  if (r < 0)
    return r;

  bufferlist object_prefixbl;
  ::encode(object_prefix, object_prefixbl);
  r = cls_cxx_map_set_val(hctx, "object_prefix", &object_prefixbl);
  if (r < 0)
    return r;

  bufferlist snap_seqbl;
  uint64_t snap_seq = 0;
  ::encode(snap_seq, snap_seqbl);
  r = cls_cxx_map_set_val(hctx, "snap_seq", &snap_seqbl);
  if (r < 0)
    return r;

  return 0;
}
```
# RBD snapshot的存储

当我们对image创建一个snapshot：

```sh
# rbd snap create pool100/user1_image1@user1_image1_snap
```
pool100并不会多出一个新的object。实际上，ceph将image的信息保存到rbd_header.${image_id}中：

```sh
# rados -p pool100 listomapvals rbd_header.134d2ae8944a
features
value: (8 bytes) :
0000 : 01 00 00 00 00 00 00 00                         : ........

object_prefix
value: (25 bytes) :
0000 : 15 00 00 00 72 62 64 5f 64 61 74 61 2e 31 33 34 : ....rbd_data.134
0010 : 64 32 61 65 38 39 34 34 61                      : d2ae8944a

order
value: (1 bytes) :
0000 : 16                                              : .

size
value: (8 bytes) :
0000 : 00 00 00 00 19 00 00 00                         : ........

snap_seq
value: (8 bytes) :
0000 : 02 00 00 00 00 00 00 00                         : ........

snapshot_0000000000000002
value: (86 bytes) :
0000 : 03 01 50 00 00 00 02 00 00 00 00 00 00 00 11 00 : ..P.............
0010 : 00 00 75 73 65 72 31 5f 69 6d 61 67 65 31 5f 73 : ..user1_image1_s
0020 : 6e 61 70 00 00 00 00 19 00 00 00 01 00 00 00 00 : nap.............
0030 : 00 00 00 01 01 1c 00 00 00 ff ff ff ff ff ff ff : ................
0040 : ff 00 00 00 00 fe ff ff ff ff ff ff ff 00 00 00 : ................
0050 : 00 00 00 00 00 00                               : ......
```

snapshot_0000000000000002(snapshot_$SNAPID)包含user1_image1_snap的信息：

```c
struct cls_rbd_snap {
  snapid_t id;
  string name;
  uint64_t image_size;
  uint64_t features;
  uint8_t protection_status;
  cls_rbd_parent parent;
  uint64_t flags;
}
```

snap_seq为image当前最新的snapshot的snapid。

写入4M数据

```sh
# dd if=/dev/sda1 of=/dev/rbd1 bs=1048576 count=4
# ceph osd map pool100 rbd_data.134d2ae8944a.0000000000000000
osdmap e436 pool 'pool100' (91) object 'rbd_data.134d2ae8944a.0000000000000000' -> pg 91.8cd8ecc2 (91.2) -> up ([0,2,1], p0) acting ([0,2,1], p0)

# ls 91.2_head/
rbd\udata.134d2ae8944a.0000000000000000__2_8CD8ECC2__5b  rbd\udata.134d2ae8944a.0000000000000000__head_8CD8ECC2__5b
```

可以看到rbd_data.134d2ae8944a.0000000000000000多个了一个snap_seq为2的文件。

Ceph使用COW实现snapshot。Image的object为head version，当更新image时，ceph会针对image的snapshot（snap_seq version）拷贝一份数据，即object_${snap_seq}，然后再更新head version。

我们再创建一个snapshot：

```sh
# rbd snap create pool100/user1_image1@user1_image1_snap2
# dd if=/dev/sda1 of=/dev/rbd1 bs=1048576 count=4
# ls 91.2_head/
rbd\udata.134d2ae8944a.0000000000000000__2_8CD8ECC2__5b  rbd\udata.134d2ae8944a.0000000000000000__head_8CD8ECC2__5b
rbd\udata.134d2ae8944a.0000000000000000__3_8CD8ECC2__5b
```

可以看到，rbd_data.134d2ae8944a.0000000000000000多了一份snap_seq为3的副本。

```sh
# rados -p pool100 listomapvals rbd_header.134d2ae8944a
…
snap_seq
value: (8 bytes) :
0000 : 03 00 00 00 00 00 00 00                         : ........

snapshot_0000000000000002
value: (86 bytes) :
0000 : 03 01 50 00 00 00 02 00 00 00 00 00 00 00 11 00 : ..P.............
0010 : 00 00 75 73 65 72 31 5f 69 6d 61 67 65 31 5f 73 : ..user1_image1_s
0020 : 6e 61 70 00 00 00 00 19 00 00 00 01 00 00 00 00 : nap.............
0030 : 00 00 00 01 01 1c 00 00 00 ff ff ff ff ff ff ff : ................
0040 : ff 00 00 00 00 fe ff ff ff ff ff ff ff 00 00 00 : ................
0050 : 00 00 00 00 00 00                               : ......

snapshot_0000000000000003
value: (87 bytes) :
0000 : 03 01 51 00 00 00 03 00 00 00 00 00 00 00 12 00 : ..Q.............
0010 : 00 00 75 73 65 72 31 5f 69 6d 61 67 65 31 5f 73 : ..user1_image1_s
0020 : 6e 61 70 32 00 00 00 00 19 00 00 00 01 00 00 00 : nap2............
0030 : 00 00 00 00 01 01 1c 00 00 00 ff ff ff ff ff ff : ................
0040 : ff ff 00 00 00 00 fe ff ff ff ff ff ff ff 00 00 : ................
0050 : 00 00 00 00 00 00 00                            : .......
```

可以看到rbd_header.${image_id}的变化。

写[4M,8M)的object1

```sh
# dd if=/dev/sda1 of=/dev/rbd1 bs=1048576 seek=4 count=4
# ceph osd map pool100 rbd_data.134d2ae8944a.0000000000000001
osdmap e437 pool 'pool100' (91) object 'rbd_data.134d2ae8944a.0000000000000001' -> pg 91.4a392b12 (91.12) -> up ([0,1,2], p0) acting ([0,1,2], p0)

# ls 91.12_head/
rbd\udata.134d2ae8944a.0000000000000001__3_4A392B12__5b  rbd\udata.134d2ae8944a.0000000000000001__head_4A392B12__5b
```
Ceph会为object1拷贝snap_seq为3的数据。

写[8M,12M)的object2

```sh
# dd if=/dev/sda1 of=/dev/rbd1 bs=1048576 seek=8 count=4
# ceph osd map pool100 rbd_data.134d2ae8944a.0000000000000002          
osdmap e437 pool 'pool100' (91) object 'rbd_data.134d2ae8944a.0000000000000002' -> pg 91.a7513028 (91.28) -> up ([0,1,2], p0) acting ([0,1,2], p0)

# ls 91.28_head/
rbd\udata.134d2ae8944a.0000000000000002__head_A7513028__5b
```

# create snapshot

```c
int add_snap(ImageCtx *ictx, const char *snap_name)
{
uint64_t snap_id;
///(1)alloc snap_id
int r = ictx->md_ctx.selfmanaged_snap_create(&snap_id);
if (r < 0) {
  lderr(ictx->cct) << "failed to create snap id: " << cpp_strerror(-r)
		   << dendl;
  return r;
}

///(2)exec rbd snapshot_add
if (ictx->old_format) {
  r = cls_client::old_snapshot_add(&ictx->md_ctx, ictx->header_oid,
				   snap_id, snap_name);
} else {
  r = cls_client::snapshot_add(&ictx->md_ctx, ictx->header_oid,
			   snap_id, snap_name);
}
//...
return 0;
}
```

*** exec rbd snapshot_add ***

```c
///cls_rbd_client.cc
int old_snapshot_add(librados::IoCtx *ioctx, const std::string &oid,
		 snapid_t snap_id, const std::string &snap_name)
{
  bufferlist bl, bl2;
  ::encode(snap_name, bl); ///snap name
  ::encode(snap_id, bl);   ///snap id

  return ioctx->exec(oid, "rbd", "snap_add", bl, bl2);
}

/**
 * Adds a snapshot to an rbd header. Ensures the id and name are unique.
 *
 * Input:
 * @param snap_name name of the snapshot (string)
 * @param snap_id id of the snapshot (uint64_t)
 *
 * Output:
 * @returns 0 on success, negative error code on failure.
 * @returns -ESTALE if the input snap_id is less than the image's snap_seq
 * @returns -EEXIST if the id or name are already used by another snapshot
 */
int snapshot_add(cls_method_context_t hctx, bufferlist *in, bufferlist *out)
{
  bufferlist snap_namebl, snap_idbl;
  cls_rbd_snap snap_meta;

  try {
    bufferlist::iterator iter = in->begin();
    ::decode(snap_meta.name, iter);
    ::decode(snap_meta.id, iter);
  } catch (const buffer::error &err) {
    return -EINVAL;
  }

  ///....
  bufferlist snap_metabl, snap_seqbl;
  ::encode(snap_meta, snap_metabl);
  ::encode(snap_meta.id, snap_seqbl); ///snapshot的 snap id

  string snapshot_key;
  key_from_snap_id(snap_meta.id, &snapshot_key);
  map<string, bufferlist> vals;
  vals["snap_seq"] = snap_seqbl; ///更新snap_seq field
  vals[snapshot_key] = snap_metabl; ///snapshot_$ID = struct cls_rbd_snap
  r = cls_cxx_map_set_vals(hctx, &vals);

}
```

# clone snapshot

```sh
# rbd clone pool100/user1_image1@user1_image1_snap pool100/user1_image2
# rados -p pool100 ls
rbd_data.134d2ae8944a.0000000000000000
rbd_children
rbd_data.134d2ae8944a.0000000000000001
rbd_id.user1_image2
rbd_header.134d2ae8944a
rbd_directory
rbd_id.user1_image1
rbd_header.1368238e1f29
```
可以看到，pool100中多了3个object：rbd_children，rbd_header.1368238e1f29，rbd_id.user1_image2

*** rbd_children ***

```sh
# rados -p pool100 listomapvals  rbd_children           
key: (32 bytes):
0000 : 5b 00 00 00 00 00 00 00 0c 00 00 00 31 33 34 64 : [...........134d
0010 : 32 61 65 38 39 34 34 61 02 00 00 00 00 00 00 00 : 2ae8944a........

value: (20 bytes) :
0000 : 01 00 00 00 0c 00 00 00 31 33 36 38 32 33 38 65 : ........1368238e
0010 : 31 66 32 39                                     : 1f29
```

*** rbd_header.1368238e1f29 ***

相比user1_image1，多了parent字段：

```sh
# rados -p pool100 listomapvals  rbd_header.1368238e1f29               
features
value: (8 bytes) :
0000 : 01 00 00 00 00 00 00 00                         : ........

object_prefix
value: (25 bytes) :
0000 : 15 00 00 00 72 62 64 5f 64 61 74 61 2e 31 33 36 : ....rbd_data.136
0010 : 38 32 33 38 65 31 66 32 39                      : 8238e1f29

order
value: (1 bytes) :
0000 : 16                                              : .

parent
value: (46 bytes) :
0000 : 01 01 28 00 00 00 5b 00 00 00 00 00 00 00 0c 00 : ..(...[.........
0010 : 00 00 31 33 34 64 32 61 65 38 39 34 34 61 02 00 : ..134d2ae8944a..
0020 : 00 00 00 00 00 00 00 00 00 00 19 00 00 00       : ..............

size
value: (8 bytes) :
0000 : 00 00 00 00 19 00 00 00                         : ........

snap_seq
value: (8 bytes) :
0000 : 00 00 00 00 00 00 00 00                         : ........
```

# read snapshot

在深入讨论之前，我们先整理下之前的流程：

```
create image1
write image1@[object0,object1]

create image1@snap
write image1@object0

create image1@snap2
write image1@object1
write image1@ojbect2

clone image1@snap image2
```

我们已经知道，当我们基于user1_image1@user1_image1_snap创建新的user1_image2时，pool100并没有对应的rbd_data.1368238e1f29.*的object，如果我们读取user1_image2，ceph如何处理呢？比如，我们读取[4M,8M)，即user1_image2@object1：

```c
#define IMAGE_BUF_SIZE 4194304
err = rados_ioctx_create(cluster, poolname, &io);
if (err < 0) {
    fprintf(stderr, "%s: cannot open rados pool %s: %s\n", argv[0], poolname, strerror(-err));
    rados_shutdown(cluster);
    exit(1);
}

err = rbd_open(io, "user1_image2", &image, NULL);
if (err < 0){
    fprintf(stderr, "open image failed: %s\n", strerror(-err));
    goto out;
}

err = rbd_read(image, IMAGE_BUF_SIZE, IMAGE_BUF_SIZE, buf);
if (err < 0) {
    fprintf(stderr, "%s: cannot read image: %s\n",  poolname, strerror(-err));
}else{
    fprintf(stderr, "read image return :%d\n", err);
}
```

实际上，Ceph会先尝试读取rbd_data.1368238e1f29.0000000000000001，必然返回ENOENT。这时，client再尝试从parent(user1_image1@snap)读取object1，而并不存在object1-snap，而是返回object1-snap2。对于object1，snap和snap2都对应object1-snap2。

> FileStore::read 91.12_head/4a392b12/rbd_data.134d2ae8944a.0000000000000001/3//91 0~4194304/4194304

参考[log](/assets/rbd_read.log)。


如果我们读取rbd_data.1368238e1f29.0000000000000002，parent(user1_image1@snap)也会返回ENOENT。这时librbd会构造一个4M的zero block：

> error opening file /var/lib/ceph/osd/ceph-0/current/91.2a_head/rbd\udata.1368238e1f29.0000000000000002__head_EB9D38AA__5b with flags=2: (2) No such file or directory

参考[log2](/assets/rbd_read2.log)。

如下：

![](/assets/2015-09-15-ceph-rbd-internal-read.jpg)

```c
class AioRequest
{   
  void complete(int r)
  {
    if (should_complete(r)) {
      if (m_hide_enoent && r == -ENOENT)
          r = 0;
      m_completion->complete(r);
      delete this;
    }
  }

void C_AioRead::finish(int r)
{
  ldout(m_cct, 10) << "C_AioRead::finish() " << this << " r = " << r << dendl;
  if (r >= 0 || r == -ENOENT) { // this was a sparse_read operation

      m_completion->destriper.add_partial_sparse_result(
    m_cct, m_req->data(), m_req->m_ext_map, m_req->m_object_off,
    m_req->m_buffer_extents);
}

void Striper::StripedReadResult::assemble_result(CephContext *cct, bufferlist& bl, bool zero_tail)
{
  ldout(cct, 10) << "assemble_result(" << this << ") zero_tail=" << zero_tail << dendl;

  // go backwards, so that we can efficiently discard zeros
  map<uint64_t,pair<bufferlist,uint64_t> >::reverse_iterator p = partial.rbegin();
  if (p == partial.rend())
    return;

  uint64_t end = p->first + p->second.second;
  while (p != partial.rend()) {
    // sanity check
    ldout(cct, 20) << "assemble_result(" << this << ") " << p->first << "~" << p->second.second
       << " " << p->second.first.length() << " bytes"
       << dendl;
    assert(p->first == end - p->second.second);
    end = p->first;

    size_t len = p->second.first.length(); ///return data len = 0
    if (len < p->second.second) {
      if (zero_tail || bl.length()) {
        bufferptr bp(p->second.second - len); ///intended len(4M) - data len(0)
        bp.zero();
        bl.push_front(bp); ///zero block
        bl.claim_prepend(p->second.first);
      }
```

## rbd_read

整体流程：

![](/assets/2015-09-15-ceph-rbd-internal-read-2.jpg)

![](/assets/2015-09-15-ceph-rbd-internal-read-3.png)


# 参考

* [Ceph doc: SNAPS](https://ceph.com/docs/master/dev/osd_internals/snaps/)
* [解析Ceph: Snapshot](http://www.wzxue.com/%E8%A7%A3%E6%9E%90ceph-snapshot/)

