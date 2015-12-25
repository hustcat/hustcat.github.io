---
layout: post
title: The reason cause SRIOV VF not be freed from netnamespace
date: 2015-11-20 18:16:30
categories: Linux
tags: netnamespace sriov
excerpt: The reason cause SRIOV VF not be freed from netnamespace
---

# 错误

最近遇到几起由于容器异常停掉，导致容器内的VF设备没有及时归还给给host的情况。

内核错误日志：

```
[2588629.076977] igb 0000:01:00.1: Setting VLAN 13, QOS 0x0 on VF 2
[2588629.093820] igb 0000:01:00.1: VF 2 attempted to override administratively set VLAN tag
[2588629.093820] Reload the VF driver to resume operations
[2588629.094303] igbvf 0000:01:11.1: Failed to remove vlan id 0
[2588629.094306] failed to kill vid 0081/0 for device eth1
```

相当于在容器内部执行下面的命令：

```sh
#ip link set eth1 down
```

# 原因

内核在关闭网络设备时，会调用dev_close，该函数会尝试清除VLAN id，如果网络设备设置了NETIF_F_HW_VLAN_CTAG_FILTER：

```c
static int vlan_device_event(struct notifier_block *unused, unsigned long event,
			     void *ptr)
{
...
	case NETDEV_DOWN:
		if (dev->features & NETIF_F_HW_VLAN_CTAG_FILTER)
			vlan_vid_del(dev, htons(ETH_P_8021Q), 0);
```

对于SRIOV设备（至少对于igb驱动是这样），如果通过PF设置了VF的VLAN ID（比如#ip link set eth1 vf 1 vlan $VLAN），就不允许VF直接设置VLAN ID（而必须通过PF进行设置）：

```c
//igb/igb_main.c
static void igb_rcv_msg_from_vf(struct igb_adapter *adapter, u32 vf)
{
...
retval = igb_read_mbx(hw, msgbuf, E1000_VFMAILBOX_SIZE, vf);

    switch ((msgbuf[0] & 0xFFFF)) {
...
    case E1000_VF_SET_VLAN:
        retval = -1;
        if (vf_data->pf_vlan) ///如果从PF设置VF
            dev_warn(&pdev->dev,
                 "VF %d attempted to override administratively set VLAN tag\nReload the VF driver to resume operations\n",
                 vf);
        else
            retval = igb_set_vf_vlan(adapter, msgbuf, vf);
        break;
//…
    msgbuf[0] |= E1000_VT_MSGTYPE_CTS;
out:
    /* notify the VF of the results of what it sent us */
    if (retval) ///设置失败
        msgbuf[0] |= E1000_VT_MSGTYPE_NACK;
    else
        msgbuf[0] |= E1000_VT_MSGTYPE_ACK;

    igb_write_mbx(hw, msgbuf, 1, vf);
}
```

VF设置VLAN ID

```c
//igbvf/vf.c
/**
 *  e1000_set_vfta_vf - Set/Unset vlan filter table address
 *  @hw: pointer to the HW structure
 *  @vid: determines the vfta register and bit to set/unset
 *  @set: if true then set bit, else clear bit
 **/
static s32 e1000_set_vfta_vf(struct e1000_hw *hw, u16 vid, bool set)
{
    struct e1000_mbx_info *mbx = &hw->mbx;
    u32 msgbuf[2];
    s32 err;

    msgbuf[0] = E1000_VF_SET_VLAN;
    msgbuf[1] = vid;
    /* Setting the 8 bit field MSG INFO to true indicates "add" */
    if (set)
        msgbuf[0] |= 1 << E1000_VT_MSGINFO_SHIFT;
    //发送消息给PF
    mbx->ops.write_posted(hw, msgbuf, 2);
    //读取结果
    err = mbx->ops.read_posted(hw, msgbuf, 2);

    msgbuf[0] &= ~E1000_VT_MSGTYPE_CTS;

    /* if nacked the vlan was rejected */
    if (!err && (msgbuf[0] == (E1000_VF_SET_VLAN | E1000_VT_MSGTYPE_NACK)))
        err = -E1000_ERR_MAC_INIT;

    return err;
}
```

![](/assets/2015-11-20-sriov-netnamespace-problem-2.jpg)

另外，值得一提的是，当VF的net namespace发生变化时，内核也会调用dev_close，也会导致上面的过程发生。

# net device与netnamespace

## netnamespace cleanup work

net namespace模块在初始化时，会创建一个netns的workqueue内核线程：

```c
static struct workqueue_struct *netns_wq;
static int __init net_ns_init(void)
{
...
    /* Create workqueue for cleanup */
    netns_wq = create_singlethread_workqueue("netns");
...
```

这个内核线程专门负责net namespace的清除操作。
当内核销毁net namespace时，就会将net namespace加到全局的cleanup_list，然后由netns内核线程负责net namespace的清除工作：

```c
static DEFINE_SPINLOCK(cleanup_list_lock);
static LIST_HEAD(cleanup_list);  /* Must hold cleanup_list_lock to touch */

static DECLARE_WORK(net_cleanup_work, cleanup_net);

void __put_net(struct net *net)
{
    /* Cleanup the network namespace in process context */
    unsigned long flags;

    //将net加到cleanup_list
    spin_lock_irqsave(&cleanup_list_lock, flags);
    list_add(&net->cleanup_list, &cleanup_list);
    spin_unlock_irqrestore(&cleanup_list_lock, flags);

    queue_work(netns_wq, &net_cleanup_work);
}
```

函数cleanup_net主要执行各种注册的pernet_operations->exit操作：

```c
static void cleanup_net(struct work_struct *work)
{
    const struct pernet_operations *ops;
..
    /* Run all of the network namespace exit methods */
    list_for_each_entry_reverse(ops, &pernet_list, list)
        ops_exit_list(ops, &net_exit_list);
```

## pernet_operations

pernet_operations包含init和exit函数，init函数在创建netnamespace时调用，exit在销毁netnamespace时调用。各个涉及到netnamespace的网络模块，比如VXLAN，都有对应的pernet_operations对象，用于指定当netnamespace创建或者销毁时，内核通过调用其init/exit方法，进行初始化或者善后工作。

其中，对网络设备的处理由default_device_exit完成，它主要负责当netnamespace销毁时，将其中的网络设备移到init netnamespace：

```c
//net/core/dev.c
static struct pernet_operations __net_initdata default_device_ops = {
	.exit = default_device_exit,
	.exit_batch = default_device_exit_batch,
};


static void __net_exit default_device_exit(struct net *net)
{
...
    for_each_netdev_safe(net, dev, aux) {
        int err;
        char fb_name[IFNAMSIZ];

        /* Ignore unmoveable devices (i.e. loopback) */
        if (dev->features & NETIF_F_NETNS_LOCAL)
            continue;

        /* Leave virtual devices for the generic cleanup */
        if (dev->rtnl_link_ops)
            continue;
	      //将网络设备移到init netnamespace
        /* Push remaining network devices to init_net */
        snprintf(fb_name, IFNAMSIZ, "dev%d", dev->ifindex);
        err = dev_change_net_namespace(dev, &init_net, fb_name);
        if (err) {
            pr_emerg("%s: failed to move %s to init_net: %d\n",
                 __func__, dev->name, err);
            BUG();
        }
    }

}
```

# 问题

网络容器关闭之后，VF对应的网络设备并没有及时移到init netnamespace：

![](/assets/2015-11-20-sriov-netnamespace-problem-1.png)

那么问题来了，

*** 是因为内核线程没有及时处理cleanup任务，还是因为内核没有触发销毁netnamespace的逻辑而导致没有创建cleanup work呢？ ***

跟踪内核函数__put_net的调用，发现其调用时间与VF移动时间一致：

```sh
# ./kprobe -s 'p:__put_net'
__put_net
Tracing kprobe __put_net. Ctrl-C to end.
          <idle>-0     [013] dNs. 2611893.167041: __put_net: (__put_net+0x0/0x80)
          <idle>-0     [013] dNs. 2611893.167047: <stack trace>
 => sk_free
 => tcp_write_timer
 => call_timer_fn
 => run_timer_softirq
 => __do_softirq
 => call_softirq
 => do_softirq
 => irq_exit
 => smp_apic_timer_interrupt
 => apic_timer_interrupt
 => arch_cpu_idle
 => cpu_idle_loop
 => cpu_startup_entry
 => start_secondary
```

从这里可以看出，由于netnamespace还有socket没有释放，从而导致了netnamespace没有及时释放。

# socket与netnamespace

netnamespace对象有一个引用计数：

```c
//include/net/net_namespace.h
struct net {
    atomic_t        count;      /* To decided when the network
                         *  namespace should be shut down.
                         */
```

当我们在netnamespace创建一个socket，就会设置其netnamespace，并对netnamespace引用计数加1：

```c
#ifdef CONFIG_NET_NS

static inline struct net *get_net(struct net *net)
{
    atomic_inc(&net->count);
    return net;
}

struct sock *sk_alloc(struct net *net, int family, gfp_t priority,
              struct proto *prot)
{
    sk = sk_prot_alloc(prot, priority | __GFP_ZERO, family);
    if (sk) {
...
       //设置netnamespace，引用计数加1
        sock_net_set(sk, get_net(net)); 
```

当释放socket时，就会执行相反的过程：

```c
static void __sk_free(struct sock *sk)
{
...
    put_net(sock_net(sk));
}

static inline void put_net(struct net *net)
{
    if (atomic_dec_and_test(&net->count))
        __put_net(net);
}
```

# 进程与netnamespace

当进程结束时，也会尝试删除netnamespace：

![](/assets/2015-11-20-sriov-netnamespace-problem-3.jpg)

# 总结

当容器异常停掉之后，导致socket没有及时释放（socket中的数据没有确认完），从而引起netnamespace没有及时释放，再引起netnamespace中的VF设备没有回到init netnamespace。
