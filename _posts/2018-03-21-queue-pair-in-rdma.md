---
layout: post
title: Queue Pair in RDMA
date: 2018-03-21 16:28:30
categories: Network
tags: RDMA
excerpt: Queue Pair in RDMA
---

一个CA(Channel Adapter)可以包含多个QP，QP相当于socket。通信的两端都需要进行QP的初始化，`Communication Manager (CM)`
在双方真正建立连接前交换QP信息。每个QP包含一个`Send Queue(SQ)`和`Receive Queue(RQ)`.

## QP type

* RC (Reliable Connected) QP

QP Setup. When it is set up by software, a RC QP is initialized with:

  * (1) The port number on the local CA through which it will send and receive all messages.
  * (2) The QP Number (QPN) that identifies the RC QP that it is married to in a remote CA.
  * (3) The port address of the remote CA port behind which the remote RC QP resides.

## 数据结构

* QP in userspace

```
struct ibv_qp {
	struct ibv_context     *context;
	void		       *qp_context;
	struct ibv_pd	       *pd;
	struct ibv_cq	       *send_cq;
	struct ibv_cq	       *recv_cq;
	struct ibv_srq	       *srq;
	uint32_t		handle;
	uint32_t		qp_num;///QPN
	enum ibv_qp_state       state; /// stat
	enum ibv_qp_type	qp_type; ///type

	pthread_mutex_t		mutex;
	pthread_cond_t		cond;
	uint32_t		events_completed;
};
```

[ibv_create_qp()](http://www.rdmamojo.com/2012/12/21/ibv_create_qp/)用于创建QP.

```
struct ibv_qp *ibv_create_qp(struct ibv_pd *pd,struct ibv_qp_init_attr *qp_init_attr);
```

* QP in ib_core(kernel)

```
/*
 * @max_write_sge: Maximum SGE elements per RDMA WRITE request.
 * @max_read_sge:  Maximum SGE elements per RDMA READ request.
 */
struct ib_qp {
	struct ib_device       *device;
	struct ib_pd	       *pd;
	struct ib_cq	       *send_cq;
	struct ib_cq	       *recv_cq;
///...
	void		       *qp_context;
	u32			qp_num; ///QP number(QPN)
	u32			max_write_sge;
	u32			max_read_sge;
	enum ib_qp_type		qp_type; ///QP type
///..
}
```

创建API为`ib_uverbs_create_qp`.

* QP in mlx4_ib

```
struct mlx4_ib_qp {
	union {
		struct ib_qp	ibqp; //QP in ib_core
		struct ib_wq	ibwq;
	};
	struct mlx4_qp		mqp; // QP in mlx4_core
	struct mlx4_buf		buf;

	struct mlx4_db		db;
	struct mlx4_ib_wq	rq;///RQ
///...
	struct mlx4_ib_wq	sq; ///SQ
///...
}
```

创建API为`mlx4_ib_create_qp`.

* QP in mlx4_core

```
struct mlx4_qp {
	void (*event)		(struct mlx4_qp *, enum mlx4_event);

	int			qpn; /// QP number

	atomic_t		refcount;
	struct completion	free;
	u8			usage;
};
```

创建的API为`mlx4_qp_alloc`.

## QP attributes

QP有很多属性，包括状态(state)等，具体参考[enum ibv_qp_attr_mask](https://github.com/hustcat/rdma-core/blob/v15/libibverbs/verbs.h#L842).这里主要讨论几个重要的属性.

* ibv_modify_qp

[`ibv_modify_qp`](https://github.com/hustcat/rdma-core/blob/v15/libibverbs/verbs.h#L2131)用于修改QP的属性，包括QP的状态等。

> ibv_modify_qp this verb changes QP attributes and one of those attributes may be the QP state.

```
/**
 * ibv_modify_qp - Modify a queue pair.
 */
int ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		  int attr_mask);
```

参考[这里](https://github.com/hustcat/rdma-core/blob/v15/libibverbs/examples/rc_pingpong.c#L459).

> A created QP still cannot be used until it is transitioned through several states, eventually getting to Ready To Send (RTS).
> 
> This provides needed information used by the QP to be able send / receive data.

### 状态(IBV_QP_STATE)

`QP`有如下一些状态：

```
RESET               Newly created, queues empty.
INIT                Basic information set. Ready for posting to receive queue.
RTR Ready to Receive. Remote address info set for connected QPs, QP may now receive packets.
RTS Ready to Send. Timeout and retry parameters set, QP may now send packets.
```

* RESET to INIT

当QP创建时，为`REST`状态，我们可以通过[调用ibv_modify_qp](https://github.com/hustcat/rdma-core/blob/v15/libibverbs/examples/rc_pingpong.c#L459)将其设置为`INIT`状态:

```
///...
	{
		struct ibv_qp_attr attr = {
			.qp_state        = IBV_QPS_INIT,
			.pkey_index      = 0,
			.port_num        = port,
			.qp_access_flags = 0
		};


		if (ibv_modify_qp(ctx->qp, &attr,
				  IBV_QP_STATE              |
				  IBV_QP_PKEY_INDEX         |
				  IBV_QP_PORT               |
				  IBV_QP_ACCESS_FLAGS)) {
			fprintf(stderr, "Failed to modify QP to INIT\n");
			goto clean_qp;
		}
	}
```

一旦QP处于`INIT`状态，我们就可以调用`ibv_post_recv` [post receive buffers to the receive queue](https://github.com/hustcat/rdma-core/blob/v15/libibverbs/examples/rc_pingpong.c#L539).

* INIT to RTR

Once a queue pair (QP) has receive buffers posted to it, it is now possible to transition the QP into the ready to receive (RTR) state.

例如，对于client/server，需要将`QP`设置为`RTS`状态，参考[rc_pingpong@pp_connect_ctx](https://github.com/hustcat/rdma-core/blob/v15/libibverbs/examples/rc_pingpong.c#L94).

在将QP的状态设置为`RTR`时，还需要填充其它一些属性，包括远端的地址信息`(LID, QPN, PSN, GID)`等。如果不使用`RDMA CM verb API`，则需要使用其它方式，比如基于TCP/IP的socket通信，在`client/server`间交换该信息，例如[rc_pingpong@pp_client_exch_dest](https://github.com/hustcat/rdma-core/blob/v15/libibverbs/examples/rc_pingpong.c#L197)。client先将自己的`(LID, QPN, PSN, GID)`发送到server，server端读取到这些信息，保存起来，同时将自己的`(LID, QPN, PSN, GID)`发给client。client收到这些信息后，就可以将QP设置为`RTR`状态了。

```
static int pp_connect_ctx(struct pingpong_context *ctx, int port, int my_psn,
			  enum ibv_mtu mtu, int sl,
			  struct pingpong_dest *dest, int sgid_idx)
{
	struct ibv_qp_attr attr = {
		.qp_state		= IBV_QPS_RTR,
		.path_mtu		= mtu,
		.dest_qp_num		= dest->qpn, /// remote QPN
		.rq_psn			= dest->psn, /// remote PSN
		.max_dest_rd_atomic	= 1,
		.min_rnr_timer		= 12,
		.ah_attr		= {
			.is_global	= 0,
			.dlid		= dest->lid, /// remote LID
			.sl		= sl, ///service level
			.src_path_bits	= 0,
			.port_num	= port
		}
	};

	if (dest->gid.global.interface_id) {
		attr.ah_attr.is_global = 1;
		attr.ah_attr.grh.hop_limit = 1;
		attr.ah_attr.grh.dgid = dest->gid;///remote GID
		attr.ah_attr.grh.sgid_index = sgid_idx;
	}
	if (ibv_modify_qp(ctx->qp, &attr,
			  IBV_QP_STATE              |
			  IBV_QP_AV                 |
			  IBV_QP_PATH_MTU           |
			  IBV_QP_DEST_QPN           |
			  IBV_QP_RQ_PSN             |
			  IBV_QP_MAX_DEST_RD_ATOMIC |
			  IBV_QP_MIN_RNR_TIMER)) {
		fprintf(stderr, "Failed to modify QP to RTR\n");
		return 1;
///...
```

```
ah_attr/IBV_QP_AV an address handle (AH) needs to be created and filled in as appropriate. Minimally, ah_attr.dlid needs to be filled in.
dest_qp_num/IBV_QP_DEST_QPN    QP number of remote QP.
rq_psn/IBV_QP_RQ_PSN    starting receive packet sequence number (should matchremote QP’s sq_psn)
```

这里值得注意是`IBV_QP_AV`，主要用来指示内核做地址解析，对于RoCE，则进行L3到MAC地址的转换。后面会详细介绍其实现。

另外，如果使用`RDMA CM verb API`，例如使用`rdma_connect`建立连接时，发送的`CM Connect Request`包含这些信息：

```
struct cm_req_msg {
	struct ib_mad_hdr hdr;

	__be32 local_comm_id;
	__be32 rsvd4;
	__be64 service_id;
	__be64 local_ca_guid;
	__be32 rsvd24;
	__be32 local_qkey;
	/* local QPN:24, responder resources:8 */
	__be32 offset32; ///QPN
	/* local EECN:24, initiator depth:8 */
	__be32 offset36;
	/*
	 * remote EECN:24, remote CM response timeout:5,
	 * transport service type:2, end-to-end flow control:1
	 */
	__be32 offset40;
	/* starting PSN:24, local CM response timeout:5, retry count:3 */
	__be32 offset44; ///PSN
	__be16 pkey;
	/* path MTU:4, RDC exists:1, RNR retry count:3. */
	u8 offset50;
	/* max CM Retries:4, SRQ:1, extended transport type:3 */
	u8 offset51;

	__be16 primary_local_lid;
	__be16 primary_remote_lid;
	union ib_gid primary_local_gid; /// local GID
	union ib_gid primary_remote_gid;
///...
```

server回复的`CM Connect Response`也包含相应的信息：

```
struct cm_rep_msg {
	struct ib_mad_hdr hdr;

	__be32 local_comm_id;
	__be32 remote_comm_id;
	__be32 local_qkey;
	/* local QPN:24, rsvd:8 */
	__be32 offset12;
	/* local EECN:24, rsvd:8 */
	__be32 offset16;
	/* starting PSN:24 rsvd:8 */
	__be32 offset20;
	u8 resp_resources;
	u8 initiator_depth;
	/* target ACK delay:5, failover accepted:2, end-to-end flow control:1 */
	u8 offset26;
	/* RNR retry count:3, SRQ:1, rsvd:5 */
	u8 offset27;
	__be64 local_ca_guid;

	u8 private_data[IB_CM_REP_PRIVATE_DATA_SIZE];

} __attribute__ ((packed));
```

* RTR to RTS

一旦QP为`RTR`状态后，就可以将其转为`RTS`状态了，参考[](https://github.com/hustcat/rdma-core/blob/v15/libibverbs/examples/rc_pingpong.c#L138).

```
	attr.qp_state	    = IBV_QPS_RTS;
	attr.timeout	    = 14;
	attr.retry_cnt	    = 7;
	attr.rnr_retry	    = 7;
	attr.sq_psn	    = my_psn;
	attr.max_rd_atomic  = 1;
	if (ibv_modify_qp(ctx->qp, &attr,
			  IBV_QP_STATE              |
			  IBV_QP_TIMEOUT            |
			  IBV_QP_RETRY_CNT          |
			  IBV_QP_RNR_RETRY          |
			  IBV_QP_SQ_PSN             |
			  IBV_QP_MAX_QP_RD_ATOMIC)) {
		fprintf(stderr, "Failed to modify QP to RTS\n");
		return 1;
	}
```

相关属性：

```
timeout/IBV_QP_TIMEOUT      local ack timeout (recommended value: 14)
retry_cnt/IBV_QP_RETRY_CNT   retry count (recommended value: 7)
rnr_retry/IBV_QP_RNR_RETRYRNR  retry count (recommended value: 7)
sq_psn/IBV_SQ_PSN   send queue starting packet sequence number (should match remote QP’s rq_psn)
```

## ibv_modify_qp的实现

### userspace

`ibv_modify_qp` -> `mlx4_modify_qp` -> `ibv_cmd_modify_qp`:

```
///libibverbs/cmd.c
int ibv_cmd_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		      int attr_mask,
		      struct ibv_modify_qp *cmd, size_t cmd_size)
{
	/*
	 * Masks over IBV_QP_DEST_QPN are only supported by
	 * ibv_cmd_modify_qp_ex.
	 */
	if (attr_mask & ~((IBV_QP_DEST_QPN << 1) - 1))
		return EOPNOTSUPP;

	IBV_INIT_CMD(cmd, cmd_size, MODIFY_QP);

	copy_modify_qp_fields(qp, attr, attr_mask, &cmd->base);

	if (write(qp->context->cmd_fd, cmd, cmd_size) != cmd_size)
		return errno;

	return 0;
}
```

### kernel

```
# ./funcgraph ib_uverbs_modify_qp
Tracing "ib_uverbs_modify_qp"... Ctrl-C to end.
  0)               |  ib_uverbs_modify_qp [ib_uverbs]() {
  0)               |    modify_qp.isra.24 [ib_uverbs]() {
  0)   0.090 us    |      kmem_cache_alloc_trace();
  0)               |      rdma_lookup_get_uobject [ib_uverbs]() {
  0)   0.711 us    |        lookup_get_idr_uobject [ib_uverbs]();
  0)   0.036 us    |        uverbs_try_lock_object [ib_uverbs]();
  0)   2.012 us    |      }
  0)   0.272 us    |      copy_ah_attr_from_uverbs.isra.23 [ib_uverbs]();
  0)               |      ib_modify_qp_with_udata [ib_core]() {
  0)               |        ib_resolve_eth_dmac [ib_core]() {
  0)               |          ib_query_gid [ib_core]() {
  0)               |            ib_get_cached_gid [ib_core]() {
  0)   0.159 us    |              _raw_read_lock_irqsave();
  0)   0.036 us    |              __ib_cache_gid_get [ib_core]();
  0)   0.041 us    |              _raw_read_unlock_irqrestore();
  0)   1.367 us    |            }
  0)   1.677 us    |          }
  0)   2.200 us    |        }
  0)   2.742 us    |      }
  0)               |      rdma_lookup_put_uobject [ib_uverbs]() {
  0)   0.023 us    |        lookup_put_idr_uobject [ib_uverbs]();
  0)   0.395 us    |      }
  0)   0.055 us    |      kfree();
  0)   7.688 us    |    }
  0)   8.331 us    |  }
```

* ib_modify_qp_with_udata

`ib_modify_qp_with_udata`中，会调用`ib_resolve_eth_dmac`解析`remote gid`对应的MAC地址：
```
int ib_modify_qp_with_udata(struct ib_qp *qp, struct ib_qp_attr *attr,
			    int attr_mask, struct ib_udata *udata)
{
	int ret;

	if (attr_mask & IB_QP_AV) {
		ret = ib_resolve_eth_dmac(qp->device, &attr->ah_attr); /// resolve remote mac address
		if (ret)
			return ret;
	}
	ret = ib_security_modify_qp(qp, attr, attr_mask, udata);
	if (!ret && (attr_mask & IB_QP_PORT))
		qp->port = attr->port_num;

	return ret;
}
```

`ib_resolve_eth_dmac` -> `rdma_addr_find_l2_eth_by_grh`:


```
int rdma_addr_find_l2_eth_by_grh(const union ib_gid *sgid,
				 const union ib_gid *dgid,
				 u8 *dmac, u16 *vlan_id, int *if_index,
				 int *hoplimit)
{
	int ret = 0;
	struct rdma_dev_addr dev_addr;
	struct resolve_cb_context ctx;
	struct net_device *dev;

	union {
		struct sockaddr     _sockaddr;
		struct sockaddr_in  _sockaddr_in;
		struct sockaddr_in6 _sockaddr_in6;
	} sgid_addr, dgid_addr;


	rdma_gid2ip(&sgid_addr._sockaddr, sgid);
	rdma_gid2ip(&dgid_addr._sockaddr, dgid);

	memset(&dev_addr, 0, sizeof(dev_addr));
	if (if_index)
		dev_addr.bound_dev_if = *if_index;
	dev_addr.net = &init_net; /// not support net namespace

	ctx.addr = &dev_addr;
	init_completion(&ctx.comp);
	ret = rdma_resolve_ip(&self, &sgid_addr._sockaddr, &dgid_addr._sockaddr,
			&dev_addr, 1000, resolve_cb, &ctx);
///..
	if (dmac)
		memcpy(dmac, dev_addr.dst_dev_addr, ETH_ALEN); ///set MAC address
```

从上面的代码可以看到，4.2版本还不支持`net namespace`.


* rdma_resolve_ip

```
rdma_resolve_ip
|- addr_resolve
   |- addr4_resolve  /// route
   |- addr_resolve_neigh /// ARP
```

## Refs

* [RDMA Aware Networks Programming User Manual](http://www.mellanox.com/related-docs/prod_software/RDMA_Aware_Programming_user_manual.pdf)