---
layout: post
title: Linux Soft-RoCE implementation
date: 2017-11-08 23:20:30
categories: Network
tags: RDMA RoCE Linux-RDMA 
excerpt: Linux Soft-RoCE implementation
---

内核在[`4.9`](https://github.com/hustcat/linux/tree/v4.9/drivers/infiniband/sw/rxe)实现的[Soft-RoCE](http://www.roceinitiative.org/wp-content/uploads/2016/11/SoftRoCE_Paper_FINAL.pdf)实现了[`RoCEv2`](https://en.wikipedia.org/wiki/RDMA_over_Converged_Ethernet).

## 队列初始化

### libRXE (user space library)

```
ibv_create_qp
|--- rxe_create_qp
    |--- ibv_cmd_create_qp
```

* ibv_create_qp

```
LATEST_SYMVER_FUNC(ibv_create_qp, 1_1, "IBVERBS_1.1",
		   struct ibv_qp *,
		   struct ibv_pd *pd,
		   struct ibv_qp_init_attr *qp_init_attr)
{
	struct ibv_qp *qp = pd->context->ops.create_qp(pd, qp_init_attr); ///rxe_ctx_ops
///..
}
```

* rxe_create_qp

```
static struct ibv_qp *rxe_create_qp(struct ibv_pd *pd,
				    struct ibv_qp_init_attr *attr)
{
	struct ibv_create_qp cmd;
	struct rxe_create_qp_resp resp;
	struct rxe_qp *qp;
	int ret;
////..
	ret = ibv_cmd_create_qp(pd, &qp->ibv_qp, attr, &cmd, sizeof cmd,
				&resp.ibv_resp, sizeof resp); /// ibv_create_qp CMD, to kernel
///...
	qp->sq.max_sge = attr->cap.max_send_sge;
	qp->sq.max_inline = attr->cap.max_inline_data;
	qp->sq.queue = mmap(NULL, resp.sq_mi.size, PROT_READ | PROT_WRITE,
			    MAP_SHARED,
			    pd->context->cmd_fd, resp.sq_mi.offset); ///mmap，参考rxe_mmap
```

`ibv_context->cmd_fd`指向对应的`ibv_device`，由`ibv_open_device`返回。

`ibv_cmd_create_qp`会通过`ibv_context->cmd_fd`给内核发送`IB_USER_VERBS_CMD_CREATE_QP`命令，参考[libiverbs@ibv_cmd_create_qp](https://github.com/hustcat/rdma-core/blob/v15/libibverbs/cmd.c#L1063).

对应的内核`write`函数为[`ib_uverbs_write`](https://github.com/torvalds/linux/blob/v4.9/drivers/infiniband/core/uverbs_main.c#L738):

``` kernel
///drivers/infiniband/core/uverbs_main.c
static const struct file_operations uverbs_fops = {
	.owner	 = THIS_MODULE,
	.write	 = ib_uverbs_write,
	.open	 = ib_uverbs_open,
	.release = ib_uverbs_close,
	.llseek	 = no_llseek,
};
```

* ibv_open_device

```
///libibverbs/device.c
LATEST_SYMVER_FUNC(ibv_open_device, 1_1, "IBVERBS_1.1",
		   struct ibv_context *,
		   struct ibv_device *device)
{
	struct verbs_device *verbs_device = verbs_get_device(device);
	char *devpath;
	int cmd_fd, ret;
	struct ibv_context *context;
	struct verbs_context *context_ex;

	if (asprintf(&devpath, "/dev/infiniband/%s", device->dev_name) < 0)
		return NULL;

	/*
	 * We'll only be doing writes, but we need O_RDWR in case the
	 * provider needs to mmap() the file.
	 */
	cmd_fd = open(devpath, O_RDWR | O_CLOEXEC); /// /dev/infiniband/uverbs0
	free(devpath);

	if (cmd_fd < 0)
		return NULL;

	if (!verbs_device->ops->init_context) {
		context = verbs_device->ops->alloc_context(device, cmd_fd); ///rxe_alloc_context, rxe_dev_ops
		if (!context)
			goto err;
	}
///...
	context->device = device;
	context->cmd_fd = cmd_fd;
	pthread_mutex_init(&context->mutex, NULL);

	ibverbs_device_hold(device);

	return context;
///...
}
```

### kernel (rdma_rxe module)

* ib_uverbs_create_qp

`IB_USER_VERBS_CMD_CREATE_QP`的处理函数为函数[ib_uverbs_create_qp](https://github.com/hustcat/linux/blob/master/drivers/infiniband/core/uverbs_main.c#L95).

```
ib_uverbs_write
|--- ib_uverbs_create_qp
     |--- create_qp
	      |--- ib_device->create_qp
		       |--- rxe_create_qp
```

`create_qp`调用[`ib_device->create_qp`](https://github.com/hustcat/linux/blob/master/drivers/infiniband/core/uverbs_cmd.c#L1529),对于RXE，
为函数[`rxe_create_qp`](https://github.com/hustcat/linux/blob/v4.9/drivers/infiniband/sw/rxe/rxe_verbs.c#L545)，
参考[`rxe_register_device`](https://github.com/hustcat/linux/blob/v4.9/drivers/infiniband/sw/rxe/rxe_verbs.c#L1290).

* rxe_create_qp

```
rxe_create_qp
|--- rxe_qp_from_init
     |--- rxe_qp_init_req
```

[`rxe_qp_from_init`](https://github.com/hustcat/linux/blob/v4.9/drivers/infiniband/sw/rxe/rxe_qp.c#L337)完成发送队列和接收队列的初始化。

* rxe_qp_init_req

[`rxe_qp_init_req`](https://github.com/hustcat/linux/blob/v4.9/drivers/infiniband/sw/rxe/rxe_qp.c#L226)主要做以下一些事情：

> 创建对应的UDP socket
> 
> 调用[`rxe_queue_init`](https://github.com/hustcat/linux/blob/v4.9/drivers/infiniband/sw/rxe/rxe_queue.c#L96)完成发送队列的初始化.
>
> 初始化对应的tasklet


* rxe_queue_init

[`rxe_queue_init`](https://github.com/hustcat/linux/blob/v4.9/drivers/infiniband/sw/rxe/rxe_queue.c#L96)给队列分配内存空间：

```
struct rxe_queue *rxe_queue_init(struct rxe_dev *rxe,
				 int *num_elem,
				 unsigned int elem_size)
{
	struct rxe_queue *q;
	size_t buf_size;
	unsigned int num_slots;
///...
	buf_size = sizeof(struct rxe_queue_buf) + num_slots * elem_size;

	q->buf = vmalloc_user(buf_size);
///...
}
```

`rxe_queue->buf`指向的内存缓冲区，由`rxe_mmap`映射到用户空间，队列的`element`对应数据结构`struct rxe_send_wqe`.

`libiverbs API`调用`ibv_post_send`时，会将对应的`struct rxe_send_wqe`加入到该队列，参考[`rdma-core@post_one_send`](https://github.com/hustcat/rdma-core/blob/v15/providers/rxe/rxe.c#L649).


* rxe_mmap

```
/**
 * rxe_mmap - create a new mmap region
 * @context: the IB user context of the process making the mmap() call
 * @vma: the VMA to be initialized
 * Return zero if the mmap is OK. Otherwise, return an errno.
 */
int rxe_mmap(struct ib_ucontext *context, struct vm_area_struct *vma)
{
	struct rxe_dev *rxe = to_rdev(context->device);
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long size = vma->vm_end - vma->vm_start;
	struct rxe_mmap_info *ip, *pp;
///...
found_it:
	list_del_init(&ip->pending_mmaps);
	spin_unlock_bh(&rxe->pending_lock);

	ret = remap_vmalloc_range(vma, ip->obj, 0);
	if (ret) {
		pr_err("rxe: err %d from remap_vmalloc_range\n", ret);
		goto done;
	}

	vma->vm_ops = &rxe_vm_ops;
	vma->vm_private_data = ip;
	rxe_vma_open(vma);
///...
}
```

## 发送数据

### libRXE

`rxe_post_send`会将`struct ibv_send_wr`转成`struct rxe_send_wqe`，并加入到发送队列`rxe_qp->rq`，然后通过`cmd_fd`给RXE内核模块发送`IB_USER_VERBS_CMD_POST_SEND`命令：

```
///providers/rxe/rxe.c
/* this API does not make a distinction between
   restartable and non-restartable errors */
static int rxe_post_send(struct ibv_qp *ibqp,
			 struct ibv_send_wr *wr_list,
			 struct ibv_send_wr **bad_wr)
{
	int rc = 0;
	int err;
	struct rxe_qp *qp = to_rqp(ibqp);/// ibv_qp -> rxe_qp
	struct rxe_wq *sq = &qp->sq;

	if (!bad_wr)
		return EINVAL;

	*bad_wr = NULL;

	if (!sq || !wr_list || !sq->queue)
	 	return EINVAL;

	pthread_spin_lock(&sq->lock);

	while (wr_list) {
		rc = post_one_send(qp, sq, wr_list); /// ibv_send_wr -> rxe_send_wqe, enqueue
		if (rc) {
			*bad_wr = wr_list;
			break;
		}

		wr_list = wr_list->next;
	}

	pthread_spin_unlock(&sq->lock);

	err =  post_send_db(ibqp); /// IB_USER_VERBS_CMD_POST_SEND cmd
	return err ? err : rc;
}
```

### kernel 

处理的`IB_USER_VERBS_CMD_POST_SEND`的函数为`ib_uverbs_post_send`:

`ib_uverbs_post_send` -> `ib_device->post_send` -> `rxe_post_send` -> `rxe_requester` -> `ip_local_out`。

* rxe_post_send

```
static int rxe_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
			 struct ib_send_wr **bad_wr)
{
	int err = 0;
	struct rxe_qp *qp = to_rqp(ibqp); ///ib_qp -> rxe_qp
///...
	/*
	 * Must sched in case of GSI QP because ib_send_mad() hold irq lock,
	 * and the requester call ip_local_out_sk() that takes spin_lock_bh.
	 */
	must_sched = (qp_type(qp) == IB_QPT_GSI) ||
			(queue_count(qp->sq.queue) > 1);

	rxe_run_task(&qp->req.task, must_sched); /// to rxe_requester

	return err;
}
```

* rxe_requester

`rxe_requester`从`rxe_qp`队列取出`rxe_send_wqe`，生成对应的`skb_buff`，然后下发给对应的`rxe_dev`设备：

```
///sw/rxe/rxe_req.c
int rxe_requester(void *arg)
{
	struct rxe_qp *qp = (struct rxe_qp *)arg;
	struct rxe_pkt_info pkt;
	struct sk_buff *skb;
	struct rxe_send_wqe *wqe;
///...
	wqe = req_next_wqe(qp); /// get rxe_send_wqe
///...
	/// rxe_send_wqe -> skb
	skb = init_req_packet(qp, wqe, opcode, payload, &pkt);
///...
	ret = rxe_xmit_packet(to_rdev(qp->ibqp.device), qp, &pkt, skb);
///...
}

static inline int rxe_xmit_packet(struct rxe_dev *rxe, struct rxe_qp *qp,
				  struct rxe_pkt_info *pkt, struct sk_buff *skb)
{
///...
	if (pkt->mask & RXE_LOOPBACK_MASK) {
		memcpy(SKB_TO_PKT(skb), pkt, sizeof(*pkt));
		err = rxe->ifc_ops->loopback(skb);
	} else {
		err = rxe->ifc_ops->send(rxe, pkt, skb);/// ifc_ops->send, send
	}
///...
}
```

`ifc_ops->send`最后会调用`ip_local_out`，从对应的物理NIC发送出去。


## Refs

* [SOFT-RoCE RDMA TRANSPORT IN A SOFTWARE IMPLEMENTATION](http://www.roceinitiative.org/wp-content/uploads/2016/11/SoftRoCE_Paper_FINAL.pdf)
* [THE LINUX SOFTROCE DRIVER](https://www.openfabrics.org/images/eventpresos/2017presentations/205_SoftRoCE_LLiss.pdf)