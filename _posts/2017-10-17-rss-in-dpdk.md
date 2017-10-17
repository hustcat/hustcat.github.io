---
layout: post
title: Multiple queue and RSS in DPDK
date: 2017-10-17 11:00:30
categories: Network
tags: DPDK 
excerpt: Multiple queue and RSS in DPDK
---

## RX queue

`rte_eth_dev->data`（对应结构体`rte_eth_dev_data`）保存设备的（接收／发送）队列信息：

```
struct rte_eth_dev_data {
	char name[RTE_ETH_NAME_MAX_LEN]; /**< Unique identifier name */

	void **rx_queues; /**< Array of pointers to RX queues. */
	void **tx_queues; /**< Array of pointers to TX queues. */
	uint16_t nb_rx_queues; /**< Number of RX queues. */
	uint16_t nb_tx_queues; /**< Number of TX queues. */
///...
```

`rx_queues`为接收队列指针数组，每个指针指向和一个具体的接收队列，以`igb`驱动(`drivers/net/e1000`)为例：

```
/**
 * Structure associated with each RX queue.
 */
struct igb_rx_queue {
	struct rte_mempool  *mb_pool;   /**< mbuf pool to populate RX ring. */
	volatile union e1000_adv_rx_desc *rx_ring; /**< RX ring virtual address. */
	uint64_t            rx_ring_phys_addr; /**< RX ring DMA address. */
	volatile uint32_t   *rdt_reg_addr; /**< RDT register address. */
	volatile uint32_t   *rdh_reg_addr; /**< RDH register address. */
	struct igb_rx_entry *sw_ring;   /**< address of RX software ring. */
	struct rte_mbuf *pkt_first_seg; /**< First segment of current packet. */
	struct rte_mbuf *pkt_last_seg;  /**< Last segment of current packet. */
	uint16_t            nb_rx_desc; /**< number of RX descriptors. */
	uint16_t            rx_tail;    /**< current value of RDT register. */
	uint16_t            nb_rx_hold; /**< number of held free RX desc. */
	uint16_t            rx_free_thresh; /**< max free RX desc to hold. */
	uint16_t            queue_id;   /**< RX queue index. */
	uint16_t            reg_idx;    /**< RX queue register index. */
	uint8_t             port_id;    /**< Device port identifier. */
	uint8_t             pthresh;    /**< Prefetch threshold register. */
	uint8_t             hthresh;    /**< Host threshold register. */
	uint8_t             wthresh;    /**< Write-back threshold register. */
	uint8_t             crc_len;    /**< 0 if CRC stripped, 4 otherwise. */
	uint8_t             drop_en;  /**< If not 0, set SRRCTL.Drop_En. */
};

```

每个队列包含一个硬件描述符ring(`rx_ring`)和一个软件描述符ring(`sw_ring`)，`rx_ring`主要由驱动与硬件使用，`sw_ring`实际上是是一个mbuf指针，主要由DPDK应用程序使用。

* e1000_adv_rx_desc

硬件描述符，所有的`e1000_adv_rx_desc`构成一个环形DMA缓冲区。对于接收数据时，`pkt_addr`指向`rte_mbuf->buf_physaddr`，从而使得网卡收到数据时，将数据写到mbuf对应的数据缓冲区。

```
/* Receive Descriptor - Advanced */
union e1000_adv_rx_desc {
	struct {
		__le64 pkt_addr; /* Packet buffer address */
		__le64 hdr_addr; /* Header buffer address */
	} read; ///for receive
	struct {
		struct {
			union {
				__le32 data;
				struct {
					__le16 pkt_info; /*RSS type, Pkt type*/
					/* Split Header, header buffer len */
					__le16 hdr_info;
				} hs_rss;
			} lo_dword;
			union {
				__le32 rss; /* RSS Hash */
				struct {
					__le16 ip_id; /* IP id */
					__le16 csum; /* Packet Checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			__le32 status_error; /* ext status/error */
			__le16 length; /* Packet length */
			__le16 vlan; /* VLAN tag */
		} upper;
	} wb;  /* writeback */
};
```

* igb_rx_entry

每个硬件描述符都有一个对应的软件描述符，它是DPDK应用程序与DPDK驱动之间进行数据传递的桥梁，它实际上是一个`rte_mbuf`的指针，`rte_mbuf->buf_physaddr`为DMA的物理地址，由网卡硬件使用，`rte_mbuf->buf_addr`为`buffer`的虚拟地址，由DPDK应用程序使用。

```
/**
 * Structure associated with each descriptor of the RX ring of a RX queue.
 */
struct igb_rx_entry {
	struct rte_mbuf *mbuf; /**< mbuf associated with RX descriptor. */
};

/**
 * The generic rte_mbuf, containing a packet mbuf.
 */
struct rte_mbuf {
	MARKER cacheline0;

	void *buf_addr;           /**< Virtual address of segment buffer. */
	/**
	 * Physical address of segment buffer.
	 * Force alignment to 8-bytes, so as to ensure we have the exact
	 * same mbuf cacheline0 layout for 32-bit and 64-bit. This makes
	 * working on vector drivers easier.
	 */
	phys_addr_t buf_physaddr __rte_aligned(sizeof(phys_addr_t));
///...
```

## Config queue

DPDK应用程序可以调用`rte_eth_dev_configure`设置Port的队列数量：

```
		ret = rte_eth_dev_configure(portid, nb_rx_queue,
					(uint16_t)n_tx_queue, &port_conf);
```

`rte_eth_dev_configure`会调用`rte_eth_dev_rx_queue_config`和`rte_eth_dev_tx_queue_config`设置接收队列和发送队列：

```
rte_eth_dev_configure
|---rte_eth_dev_rx_queue_config
|---rte_eth_dev_tx_queue_config
```

* config rx queue

```
static int
rte_eth_dev_rx_queue_config(struct rte_eth_dev *dev, uint16_t nb_queues)
{
	uint16_t old_nb_queues = dev->data->nb_rx_queues;
	void **rxq;
	unsigned i;

	if (dev->data->rx_queues == NULL && nb_queues != 0) { /* first time configuration */
		dev->data->rx_queues = rte_zmalloc("ethdev->rx_queues",
				sizeof(dev->data->rx_queues[0]) * nb_queues,
				RTE_CACHE_LINE_SIZE);
		if (dev->data->rx_queues == NULL) {
			dev->data->nb_rx_queues = 0;
			return -(ENOMEM);
		}
	}
///...
```


## Setup queue

* rte_eth_rx_queue_setup

DPDK application都会调用[rte_eth_rx_queue_setup](https://bitbucket.org/hustcat/dpdk/src/3685bde7469df3660853acc78b29bfe0b78708eb/examples/l2fwd/main.c?at=master&fileviewer=file-view-default#main.c-673)初始化接收队列。

```
int
rte_eth_rx_queue_setup(uint8_t port_id, uint16_t rx_queue_id,
		       uint16_t nb_rx_desc, unsigned int socket_id,
		       const struct rte_eth_rxconf *rx_conf,
		       struct rte_mempool *mp)
{
///...
	ret = (*dev->dev_ops->rx_queue_setup)(dev, rx_queue_id, nb_rx_desc,
					      socket_id, rx_conf, mp); ///eth_igb_ops, eth_igb_rx_queue_setup
}
```

`eth_igb_rx_queue_setup`会创建接收队列`igb_rx_queue`，分配`RX ring hardware descriptors(e1000_adv_rx_desc)`和`software ring(igb_rx_entry)`:

```
int
eth_igb_rx_queue_setup(struct rte_eth_dev *dev,
			 uint16_t queue_idx,
			 uint16_t nb_desc,
			 unsigned int socket_id,
			 const struct rte_eth_rxconf *rx_conf,
			 struct rte_mempool *mp)
{
	const struct rte_memzone *rz;
	struct igb_rx_queue *rxq;
	struct e1000_hw     *hw;
	unsigned int size;

	hw = E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
///...
	/* First allocate the RX queue data structure. */
	rxq = rte_zmalloc("ethdev RX queue", sizeof(struct igb_rx_queue),
			  RTE_CACHE_LINE_SIZE);
///...
	/*
	 *  Allocate RX ring hardware descriptors. A memzone large enough to
	 *  handle the maximum ring size is allocated in order to allow for
	 *  resizing in later calls to the queue setup function.
	 */
	size = sizeof(union e1000_adv_rx_desc) * E1000_MAX_RING_DESC;
	rz = rte_eth_dma_zone_reserve(dev, "rx_ring", queue_idx, size,
				      E1000_ALIGN, socket_id);
///...
	rxq->rdt_reg_addr = E1000_PCI_REG_ADDR(hw, E1000_RDT(rxq->reg_idx));
	rxq->rdh_reg_addr = E1000_PCI_REG_ADDR(hw, E1000_RDH(rxq->reg_idx));
	rxq->rx_ring_phys_addr = rte_mem_phy2mch(rz->memseg_id, rz->phys_addr);
	rxq->rx_ring = (union e1000_adv_rx_desc *) rz->addr;

	/* Allocate software ring. */
	rxq->sw_ring = rte_zmalloc("rxq->sw_ring",
				   sizeof(struct igb_rx_entry) * nb_desc,
				   RTE_CACHE_LINE_SIZE);
}
```

`eth_igb_rx_queue_setup`主要完成DMA描述符环形队列的初始化。

## RSS

* Configure RSS with DPDK

通过`rx_mode.mq_mode = ETH_MQ_RX_RSS`（`rte_eth_dev_configure`）可以开启Port的RSS，以`l3fwd`为例：

```
static struct rte_eth_conf port_conf = {
	.rxmode = {
		.mq_mode = ETH_MQ_RX_RSS,
		.max_rx_pkt_len = ETHER_MAX_LEN,
		.split_hdr_size = 0,
		.header_split   = 0, /**< Header Split disabled */
		.hw_ip_checksum = 1, /**< IP checksum offload enabled */
		.hw_vlan_filter = 0, /**< VLAN filtering disabled */
		.jumbo_frame    = 0, /**< Jumbo Frame Support disabled */
		.hw_strip_crc   = 1, /**< CRC stripped by hardware */
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key = NULL,
			.rss_hf = ETH_RSS_IP,
		},
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
};
```

* Driver(igb) config RSS

`eth_igb_start` -> `eth_igb_rx_init` -> `igb_dev_mq_rx_configure`

```
//drivers/net/e1000/igb_rxtx.c
static int
igb_dev_mq_rx_configure(struct rte_eth_dev *dev)
{
	struct e1000_hw *hw =
		E1000_DEV_PRIVATE_TO_HW(dev->data->dev_private);
	uint32_t mrqc;

	if (RTE_ETH_DEV_SRIOV(dev).active == ETH_8_POOLS) {
		/*
		 * SRIOV active scheme
		 * FIXME if support RSS together with VMDq & SRIOV
		 */
		mrqc = E1000_MRQC_ENABLE_VMDQ;
		/* 011b Def_Q ignore, according to VT_CTL.DEF_PL */
		mrqc |= 0x3 << E1000_MRQC_DEF_Q_SHIFT;
		E1000_WRITE_REG(hw, E1000_MRQC, mrqc);
	} else if(RTE_ETH_DEV_SRIOV(dev).active == 0) { ///disable SRIOV
		/*
		 * SRIOV inactive scheme
		 */
		switch (dev->data->dev_conf.rxmode.mq_mode) {
			case ETH_MQ_RX_RSS:
				igb_rss_configure(dev); ///RSS
				break;
///...
}

static void
igb_rss_configure(struct rte_eth_dev *dev)
{
///...
	if (rss_conf.rss_key == NULL)
		rss_conf.rss_key = rss_intel_key; /* Default hash key */
	igb_hw_rss_hash_set(hw, &rss_conf);
}
```

## Refs

* [DPDK Design Tips (Part 1 - RSS)](http://galsagie.github.io/2015/02/26/dpdk-tips-1/)
* [intel dpdk的Poll Model Driver机制简介](https://my.oschina.net/u/2539854/blog/735332)