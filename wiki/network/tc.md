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
	struct Qdisc		*qdisc;
	struct Qdisc		*qdisc_sleeping;
```

 
* netlink命令初始化

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
