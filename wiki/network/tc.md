## TC相关操作

* 概念

每个网络接口都有一个`egress ’root qdisc’`，默认是`pfifo_fast`。Each `qdisc` and `class` is assigned a `handle, `handle`用于配置流控策略.`handle`用`<major>:<minor>`来标识，`qdisc`的`minor`总是0.


> The `handles` of these `qdiscs` consist of two parts, a major number and a minor number : `<major>:<minor>`. It is customary to name the root qdisc `1:`, which is equal to `1:0`. The minor number of a qdisc is always 0.
> 
> `Classes` need to have the same major number as their parent. This major number must be unique within a egress or ingress setup. The minor number must be unique within a qdisc and his classes.

                     1:   root qdisc
                      |
                      1:1 child classes
                     /|\ 
                    / | \ 
                   /  |  \ 
                 1:10 1:11 1:12 child classes
                 |    |     |
                 |    11:   | leaf class 
                 |          |
                10:         12: qdisc 
               /  \        / \
           10:1  10:2   12:1  12:2  leaf classes

详细参考[Linux Advanced Routing & Traffic Control HOWTO](http://lartc.org/).

* 基本操作

删除/创建`htb qdisc`队列

```
# tc qdisc del dev cbr0  root
# tc qdisc add dev cbr0 root handle 1: htb default 30
```


进行流控只需3步操作：(1)创建qdisc；(2)创建class；(3)添加过滤规则；例如：

```
export DOWNLOAD_RATE=1mbit
export UPLOAD_RATE=1mbit
export INTERFACE=eth0

tc qdisc add dev ${INTERFACE} root handle 1: htb default 30
tc class add dev ${INTERFACE} parent 1: classid 1:1 htb rate ${DOWNLOAD_RATE}
tc class add dev ${INTERFACE} parent 1: classid 1:2 htb rate ${UPLOAD_RATE}
tc filter add dev ${INTERFACE} protocol ip parent 1:0 prio 1 u32 match ip dst `hostname -i`/32 flowid 1:1
tc filter add dev ${INTERFACE} protocol ip parent 1:0 prio 1 u32 match ip src `hostname -i`/32 flowid 1:2
```

参考[这里](https://github.com/kubernetes/kubernetes/issues/11965).

## QDisc的实现

### 数据结构

* Qdisc

```
struct Qdisc {
	int 			(*enqueue)(struct sk_buff *skb, struct Qdisc *dev); ///skb -> queue
	struct sk_buff *	(*dequeue)(struct Qdisc *dev);
///...
  
	const struct Qdisc_ops	*ops; ///pfifo_fast_ops  
	u32			handle; ///handle ID
	u32			parent; /// parent ID
```
 
每个网卡队列有一个`Qdisc`对象：

```
struct netdev_queue {
/*
 * read mostly part
 */
	struct net_device	*dev;
	struct Qdisc		*qdisc; ///see transition_one_qdisc
	struct Qdisc		*qdisc_sleeping;
```

* netlink命令初始化

```c
///sched/sch_api.c
static int __init pktsched_init(void)
{
	int err;

	err = register_pernet_subsys(&psched_net_ops);
	if (err) {
		pr_err("pktsched_init: "
		       "cannot initialize per netns operations\n");
		return err;
	}

	register_qdisc(&pfifo_qdisc_ops);
	register_qdisc(&bfifo_qdisc_ops);
	register_qdisc(&pfifo_head_drop_qdisc_ops);
	register_qdisc(&mq_qdisc_ops);

	rtnl_register(PF_UNSPEC, RTM_NEWQDISC, tc_modify_qdisc, NULL, NULL); /// new qdisc
	rtnl_register(PF_UNSPEC, RTM_DELQDISC, tc_get_qdisc, NULL, NULL);
	rtnl_register(PF_UNSPEC, RTM_GETQDISC, tc_get_qdisc, tc_dump_qdisc, NULL);
	rtnl_register(PF_UNSPEC, RTM_NEWTCLASS, tc_ctl_tclass, NULL, NULL);
	rtnl_register(PF_UNSPEC, RTM_DELTCLASS, tc_ctl_tclass, NULL, NULL);
	rtnl_register(PF_UNSPEC, RTM_GETTCLASS, tc_ctl_tclass, tc_dump_tclass, NULL);

	return 0;
}
```


* create qdisc

`tc_modify_qdisc` -> `qdisc_create` -> `qdisc_graft`.

`qdisc_create`会分配一个`Qdisc`对象，
 
```c
/* Graft qdisc "new" to class "classid" of qdisc "parent" or
 * to device "dev".
 *
 * When appropriate send a netlink notification using 'skb'
 * and "n".
 *
 * On success, destroy old qdisc.
 */

static int qdisc_graft(struct net_device *dev, struct Qdisc *parent,
		       struct sk_buff *skb, struct nlmsghdr *n, u32 classid,
		       struct Qdisc *new, struct Qdisc *old)
{
	struct Qdisc *q = old;
	struct net *net = dev_net(dev);
	int err = 0;

	if (parent == NULL) {
		unsigned int i, num_q, ingress;

		ingress = 0;
		num_q = dev->num_tx_queues;
		if ((q && q->flags & TCQ_F_INGRESS) ||
		    (new && new->flags & TCQ_F_INGRESS)) {
			num_q = 1;
			ingress = 1;
			if (!dev_ingress_queue(dev))
				return -ENOENT;
		}

		if (dev->flags & IFF_UP)
			dev_deactivate(dev);

		if (new && new->ops->attach)
			goto skip;

		for (i = 0; i < num_q; i++) { ///设置所有队列
			struct netdev_queue *dev_queue = dev_ingress_queue(dev);

			if (!ingress)
				dev_queue = netdev_get_tx_queue(dev, i);

			old = dev_graft_qdisc(dev_queue, new);
			if (new && i > 0)
				atomic_inc(&new->refcnt);

			if (!ingress)
				qdisc_destroy(old);
		}

skip:
		if (!ingress) {
			notify_and_destroy(net, skb, n, classid,
					   dev->qdisc, new);
			if (new && !new->ops->attach)
				atomic_inc(&new->refcnt);
			dev->qdisc = new ? : &noop_qdisc;

			if (new && new->ops->attach)
				new->ops->attach(new);
		} else {
			notify_and_destroy(net, skb, n, classid, old, new);
		}

		if (dev->flags & IFF_UP)
			dev_activate(dev); ///active
	}
	
```

`dev_activate`比较关键，它会设置`net_device->qdisc`和`netdev_queue->qdisc`.

```
void dev_activate(struct net_device *dev)
{
	int need_watchdog;

	/* No queueing discipline is attached to device;
	   create default one i.e. pfifo_fast for devices,
	   which need queueing and noqueue_qdisc for
	   virtual interfaces
	 */

	if (dev->qdisc == &noop_qdisc) /// see dev_graft_qdisc
		attach_default_qdiscs(dev); ///net_device

	if (!netif_carrier_ok(dev))
		/* Delay activation until next carrier-on event */
		return;

	need_watchdog = 0;
	netdev_for_each_tx_queue(dev, transition_one_qdisc, &need_watchdog); ///netdev_queue
	if (dev_ingress_queue(dev))
		transition_one_qdisc(dev, dev_ingress_queue(dev), NULL);

	if (need_watchdog) {
		dev->trans_start = jiffies;
		dev_watchdog_up(dev);
	}
}
```
