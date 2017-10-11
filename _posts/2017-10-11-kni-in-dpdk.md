---
layout: post
title: KNI in DPDK
date: 2017-10-11 23:00:30
categories: Network
tags: DPDK 
excerpt: KNI in DPDK
---

## 介绍

The Kernel NIC Interface (KNI) is a DPDK control plane solution that allows userspace applications to exchange packets with the kernel networking stack. To accomplish this, DPDK userspace applications use an IOCTL call to request the creation of a KNI virtual device in the Linux* kernel. The IOCTL call provides interface information and the DPDK’s physical address space, which is re-mapped into the kernel address space by the KNI kernel loadable module that saves the information to a virtual device context. The DPDK creates FIFO queues for packet ingress and egress to the kernel module for each device allocated.

The KNI kernel loadable module is a standard net driver, which upon receiving the IOCTL call access the DPDK’s FIFO queue to receive/transmit packets from/to the DPDK userspace application. The FIFO queues contain pointers to data packets in the DPDK. This:

> * Provides a faster mechanism to interface with the kernel net stack and eliminates system calls
>
> * Facilitates the DPDK using standard Linux* userspace net tools (tcpdump, ftp, and so on)
>
> * Eliminate the copy_to_user and copy_from_user operations on packets.


## 测试

Load KNI kernel module:

```
# insmod /root/dpdk/x86/lib/modules/3.10.0-514.el7.x86_64/extra/dpdk/rte_kni.ko
```

Build KNI application:

```
# export RTE_SDK=/root/dpdk/x86/share/dpdk
# cd examples/kni
# make
  CC main.o
  LD kni
  INSTALL-APP kni
  INSTALL-MAP kni.map
```

Run KNI application:

```
# build/kni -c 0x0f -n 2 -- -P -p 0x3 --config="(0,0,1),(1,2,3)" 
EAL: Detected 4 lcore(s)
EAL: No free hugepages reported in hugepages-1048576kB
EAL: Probing VFIO support...
EAL: WARNING: cpu flags constant_tsc=yes nonstop_tsc=no -> using unreliable clock cycles !
EAL: PCI device 0000:00:05.0 on NUMA socket -1
EAL:   probe driver: 8086:100e net_e1000_em
EAL: PCI device 0000:00:06.0 on NUMA socket -1
EAL:   probe driver: 8086:100e net_e1000_em
EAL: PCI device 0000:00:07.0 on NUMA socket -1
EAL:   probe driver: 8086:100e net_e1000_em
APP: Initialising port 0 ...
KNI: pci: 00:06:00       8086:100e
APP: Initialising port 1 ...
KNI: pci: 00:07:00       8086:100e

Checking link status
.....done
Port 0 Link Up - speed 1000 Mbps - full-duplex
Port 1 Link Up - speed 1000 Mbps - full-duplex
APP: Lcore 1 is writing to port 0
APP: Lcore 2 is reading from port 1
APP: Lcore 0 is reading from port 0
APP: Lcore 3 is writing to port 1
...
```

其中，

> * -c = core bitmask
>
> * -P = promiscuous mode
>
> * -p = port hex bitmask
>
> * –config="(port, lcore_rx, lcore_tx [,lcore_kthread, …]) …"

Note that each core can do either TX or RX for one port only.

```
[root@vm01 ~]# ip a
...
7: vEth0: <BROADCAST,MULTICAST> mtu 1500 qdisc noop state DOWN qlen 1000
    link/ether ba:92:66:e5:2f:35 brd ff:ff:ff:ff:ff:ff
8: vEth1: <BROADCAST,MULTICAST> mtu 1500 qdisc noop state DOWN qlen 1000
    link/ether b2:64:67:2f:32:4a brd ff:ff:ff:ff:ff:ff
[root@vm01 ~]# ip addr add 10.0.10.30/24 dev vEth0
[root@vm01 ~]# ip link set vEth0 up
```


```
[root@vm03 ~]# ping -c 3 10.0.10.30 
PING 10.0.10.30 (10.0.10.30) 56(84) bytes of data.
64 bytes from 10.0.10.30: icmp_seq=1 ttl=64 time=14.2 ms
64 bytes from 10.0.10.30: icmp_seq=2 ttl=64 time=2.96 ms
64 bytes from 10.0.10.30: icmp_seq=3 ttl=64 time=1.89 ms
```

给kni应用进程发送`SIGUSR1`信号，kni应用进程会输出统计信息：

```
[root@vm01 ~]# pkill -10 kni

...
**KNI example application statistics**
======  ==============  ============  ============  ============  ============
 Port    Lcore(RX/TX)    rx_packets    rx_dropped    tx_packets    tx_dropped
------  --------------  ------------  ------------  ------------  ------------
      0          0/ 1            23             0             5             0
      1          2/ 3             1             0             0             0
======  ==============  ============  ============  ============  ============
```
## 实现

相关代码：

KNI示例程序位于`example/kni`，KNI内核模块位于`lib/librte_eal/linuxapp/kni`，KNI library位于`lib/librte_kni`。

整体实现如下：

![](/assets/dpdk/kni01.png)

### 数据接收

先调用`rte_eth_rx_burst`从网络接口读取数据，然后调用`rte_kni_tx_burst`通过`FIFO`传给内核模块。

![](/assets/dpdk/kni02.png)

```
		/* Burst rx from eth */
		nb_rx = rte_eth_rx_burst(port_id, 0, pkts_burst, PKT_BURST_SZ);

		/* Burst tx to kni */
		num = rte_kni_tx_burst(p->kni[i], pkts_burst, nb_rx);
```

rte_kni_tx_burst:

```
///librte_kni
unsigned
rte_kni_tx_burst(struct rte_kni *kni, struct rte_mbuf **mbufs, unsigned num)
{
	void *phy_mbufs[num];
	unsigned int ret;
	unsigned int i;

	for (i = 0; i < num; i++)
		phy_mbufs[i] = va2pa(mbufs[i]);

	ret = kni_fifo_put(kni->rx_q, phy_mbufs, num);

	/* Get mbufs from free_q and then free them */
	kni_free_mbufs(kni);

	return ret;
}

/**
 * Adds num elements into the fifo. Return the number actually written
 */
static inline unsigned
kni_fifo_put(struct rte_kni_fifo *fifo, void **data, unsigned num)
{
	unsigned i = 0;
	unsigned fifo_write = fifo->write;
	unsigned fifo_read = fifo->read;
	unsigned new_write = fifo_write;

	for (i = 0; i < num; i++) {
		new_write = (new_write + 1) & (fifo->len - 1);

		if (new_write == fifo_read)
			break;
		fifo->buffer[fifo_write] = data[i];
		fifo_write = new_write;
	}
	fifo->write = fifo_write;
	return i;
}
```

* fifo

DPDK应用通过`fifo`与内核模块交换数据，`fifo`实际上是一块环形共享内存：

```
/*
 * Fifo struct mapped in a shared memory. It describes a circular buffer FIFO
 * Write and read should wrap around. Fifo is empty when write == read
 * Writing should never overwrite the read position
 */
struct rte_kni_fifo {
	volatile unsigned write;     /**< Next position to be written*/
	volatile unsigned read;      /**< Next position to be read */
	unsigned len;                /**< Circular buffer length */
	unsigned elem_size;          /**< Pointer size - for 32/64 bit OS */
	void *volatile buffer[];     /**< The buffer contains mbuf pointers */
};
```

DPDK应用程序在初始化时，需要将`fifo`共享内存的地址告诉KNI内核模块：

```
struct rte_kni *
rte_kni_alloc(struct rte_mempool *pktmbuf_pool,
	      const struct rte_kni_conf *conf,
	      struct rte_kni_ops *ops)
{
///...
	/* TX RING */
	mz = slot->m_tx_q;
	ctx->tx_q = mz->addr;
	kni_fifo_init(ctx->tx_q, KNI_FIFO_COUNT_MAX);
	dev_info.tx_phys = mz->phys_addr;

	/* RX RING */
	mz = slot->m_rx_q;
	ctx->rx_q = mz->addr;
	kni_fifo_init(ctx->rx_q, KNI_FIFO_COUNT_MAX);
	dev_info.rx_phys = mz->phys_addr;
///...
	ret = ioctl(kni_fd, RTE_KNI_IOCTL_CREATE, &dev_info); ///内核模块
    
}
```

### KNI kernel module

```
static int
kni_ioctl(struct inode *inode, uint32_t ioctl_num, unsigned long ioctl_param)
{
///..
	case _IOC_NR(RTE_KNI_IOCTL_CREATE):
		ret = kni_ioctl_create(net, ioctl_num, ioctl_param);
```

`kni_ioctl_create`会创建对应的网络设备`vEthX`，然后设置对应的`fifo`共享内存，并启动对应的内核线程：

```
static int
kni_ioctl_create(struct net *net, uint32_t ioctl_num,
		unsigned long ioctl_param)
{
///...
	net_dev = alloc_netdev(sizeof(struct kni_dev), dev_info.name,
#ifdef NET_NAME_USER
							NET_NAME_USER,
#endif
							kni_net_init);
///...
	/* Translate user space info into kernel space info */
	kni->tx_q = phys_to_virt(dev_info.tx_phys);
	kni->rx_q = phys_to_virt(dev_info.rx_phys);
	kni->alloc_q = phys_to_virt(dev_info.alloc_phys);
	kni->free_q = phys_to_virt(dev_info.free_phys);
///...
	ret = kni_run_thread(knet, kni, dev_info.force_bind);
///...
}
```

kernel thread:

KNI内核线程不断从`fifo`共享内存读取数据，然后交给内核协议栈继续处理：

```
static int
kni_thread_single(void *data)
{
	struct kni_net *knet = data;
	int j;
	struct kni_dev *dev;

	while (!kthread_should_stop()) {
		down_read(&knet->kni_list_lock);
		for (j = 0; j < KNI_RX_LOOP_NUM; j++) {
			list_for_each_entry(dev, &knet->kni_list_head, list) {
				kni_net_rx(dev);
				kni_net_poll_resp(dev);
			}
		}
		up_read(&knet->kni_list_lock);
///...
}

/* rx interface */
void
kni_net_rx(struct kni_dev *kni)
{
	/**
	 * It doesn't need to check if it is NULL pointer,
	 * as it has a default value
	 */
	(*kni_net_rx_func)(kni); ///kni_net_rx_normal
}
```

* kni_net_rx_func

`kni_net_rx_func`从`fifo`共享内存读取数据，然后分配skb，拷贝数据，调用`netif_rx_ni`进入内核协议栈：

```
static void kni_net_rx_normal(struct kni_dev *kni);

/* kni rx function pointer, with default to normal rx */
static kni_net_rx_t kni_net_rx_func = kni_net_rx_normal;
```

## Refs

* [Learning DPDK : KNI interface](https://haryachyy.wordpress.com/2016/02/07/learning-dpdk-kni-sample-overview/)
* [11. Kernel NIC Interface Sample Application](http://dpdk.org/doc/guides/sample_app_ug/kernel_nic_interface.html)
* [24. Kernel NIC Interface](http://dpdk.org/doc/guides/prog_guide/kernel_nic_interface.html)