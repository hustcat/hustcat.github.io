---
layout: post
title: Two aways to set mac address of SR-IOV VF
date: 2016-11-25 17:00:30
categories: Linux
tags: network sriov
excerpt: Two aways to set mac address of SR-IOV VF
---

## 1 问题

```sh
# ls /sys/class/net/eth1/device/virtfn2/net/
dev8

# ip link show eth1                         
2: eth1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP qlen 1000
    link/ether 8c:dc:d4:b1:60:c0 brd ff:ff:ff:ff:ff:ff
    vf 0 MAC 14:05:0a:f5:ac:36, vlan 3
    vf 1 MAC 14:05:0a:f5:ac:3a, vlan 3
    vf 2 MAC 14:05:0a:f5:ac:3e, vlan 3
    vf 3 MAC 14:05:0a:f5:ac:42, vlan 3
    vf 4 MAC 14:05:0a:f5:ac:46, vlan 3
    vf 5 MAC 00:00:00:00:00:00
    vf 6 MAC 00:00:00:00:00:00

# ip link show dev8
8: dev8: <BROADCAST,MULTICAST> mtu 1500 qdisc pfifo_fast state DOWN qlen 1000
    link/ether 14:05:0a:f5:ac:3e brd ff:ff:ff:ff:ff:ff
```


直接设置VF设备dev8的MAC返回错误：

```sh
# ip link set dev8 address 14:05:00:f5:ac:3e
RTNETLINK answers: Cannot assign requested address

# dmesg
[682286.034307] igb 0000:03:00.0: VF 2 attempted to override administratively set MAC address
[682286.034307] Reload the VF driver to resume operations
```

通过PF设置VF的MAC没有返回错误：

```sh
# ip link set eth1 vf 2 mac 14:05:00:f5:ac:3e
# dmesg
[682350.583348] igb 0000:03:00.0: setting MAC 14:05:00:f5:ac:3e on VF 2
[682350.583351] igb 0000:03:00.0: Reload the VF driver to make this change effective.

# ip link show eth1
2: eth1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP qlen 1000
    link/ether 8c:dc:d4:b1:60:c0 brd ff:ff:ff:ff:ff:ff
    vf 0 MAC 14:05:0a:f5:ac:36, vlan 3
    vf 1 MAC 14:05:0a:f5:ac:3a, vlan 3
    vf 2 MAC 14:05:00:f5:ac:3e, vlan 3
...

# ip link show dev8
8: dev8: <BROADCAST,MULTICAST> mtu 1500 qdisc pfifo_fast state DOWN qlen 1000
    link/ether 14:05:0a:f5:ac:3e brd ff:ff:ff:ff:ff:ff
```

但是，新的MAC地址的确写到了PF的配置，但没有写到VF网络设备。

这里有2个问题：

(1)为什么不能通过第一种方式直接设置VF网络设备的MAC地址？

(2)通过第二种方式设置VF的MAC地址后，为什么不能反映到VF网络设备？

## 2 原因

先看看两者的区别与实现:

![](/assets/network/two_way_set_vf.jpg)

###  2.1 ip link set dev $VFDEV address $MAC

* VF端

最终会到VF的驱动igb/igbvf/netdev.c

```c
/**
 * igbvf_set_mac - Change the Ethernet Address of the NIC
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/
static int igbvf_set_mac(struct net_device *netdev, void *p)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(hw->mac.addr, addr->sa_data, netdev->addr_len);

	hw->mac.ops.rar_set(hw, hw->mac.addr, 0); ///e1000_rar_set_vf

	if (memcmp(addr->sa_data, hw->mac.addr, 6))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len); 

	return 0;
}
```

在到MAC地址拷贝到net_device->dev_addr之前，会调用`e1000_rar_set_vf`，向PF发送`E1000_VF_SET_MAC_ADDR`消息

```c
/**
 *  e1000_rar_set_vf - set device MAC address
 *  @hw: pointer to the HW structure
 *  @addr: pointer to the receive address
 *  @index: receive address array register
 **/
static void e1000_rar_set_vf(struct e1000_hw *hw, u8 * addr, u32 index)
{
	struct e1000_mbx_info *mbx = &hw->mbx;
	u32 msgbuf[3];
	u8 *msg_addr = (u8 *)(&msgbuf[1]);
	s32 ret_val;

	memset(msgbuf, 0, 12);
	msgbuf[0] = E1000_VF_SET_MAC_ADDR;
	memcpy(msg_addr, addr, 6);
	ret_val = mbx->ops.write_posted(hw, msgbuf, 3);

	if (!ret_val)
		ret_val = mbx->ops.read_posted(hw, msgbuf, 3); ///e1000_read_posted_mbx

	msgbuf[0] &= ~E1000_VT_MSGTYPE_CTS;

	/* if nacked the address was rejected, use "perm_addr" */
	if (!ret_val &&
	    (msgbuf[0] == (E1000_VF_SET_MAC_ADDR | E1000_VT_MSGTYPE_NACK)))
		e1000_read_mac_addr_vf(hw);
}
```

如果PF返回NACK(E1000_VF_SET_MAC_ADDR | E1000_VT_MSGTYPE_NACK)，则使用`perm_addr`:

```c
/**
 *  e1000_read_mac_addr_vf - Read device MAC address
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_read_mac_addr_vf(struct e1000_hw *hw)
{
	memcpy(hw->mac.addr, hw->mac.perm_addr, ETH_ALEN);

	return E1000_SUCCESS;
}
```

* PF端
当PF收到VF的`E1000_VF_SET_MAC_ADDR`消息时，如果没有设置过`IGB_VF_FLAG_PF_SET_MAC`标志，则更新PF驱动保存的有关VF的MAC信息；

```c
static void igb_rcv_msg_from_vf(struct igb_adapter *adapter, u32 vf)
{
///...
	retval = igb_read_mbx(hw, msgbuf, E1000_VFMAILBOX_SIZE, vf);

	switch ((msgbuf[0] & 0xFFFF)) {
	case E1000_VF_SET_MAC_ADDR:
		retval = -EINVAL;
		if (!(vf_data->flags & IGB_VF_FLAG_PF_SET_MAC))
			retval = igb_set_vf_mac_addr(adapter, msgbuf, vf);
		else
			dev_warn(&pdev->dev,
				 "VF %d attempted to override administratively set MAC address\nReload the VF driver to resume operations\n",
				 vf);
		break;

	msgbuf[0] |= E1000_VT_MSGTYPE_CTS;
out:
	/* notify the VF of the results of what it sent us */
	if (retval)
		msgbuf[0] |= E1000_VT_MSGTYPE_NACK; ///PF更新MAC失败
	else
		msgbuf[0] |= E1000_VT_MSGTYPE_ACK;

	igb_write_mbx(hw, msgbuf, 1, vf);
}
```

当PF更新MAC失败或者标志位`IGB_VF_FLAG_PF_SET_MAC`设置时，会给VF返回`E1000_VT_MSGTYPE_NACK`消息。

`igb_set_vf_mac_addr`直接调用`igb_set_vf_mac`:

```c
static int igb_set_vf_mac(struct igb_adapter *adapter,
			  int vf, unsigned char *mac_addr)
{
	struct e1000_hw *hw = &adapter->hw;
	/* VF MAC addresses start at end of receive addresses and moves
	 * towards the first, as a result a collision should not be possible
	 */
	int rar_entry = hw->mac.rar_entry_count - (vf + 1);

	memcpy(adapter->vf_data[vf].vf_mac_addresses, mac_addr, ETH_ALEN);

	igb_rar_set_qsel(adapter, mac_addr, rar_entry, vf);

	return 0;
}
```



### 2.2 ip link set dev eth1 vf 2 mac $MAC

当通过PF去设置VF的MAC地址时，内核会通过PF的驱动函数`igb_ndo_set_vf_mac`更新PF驱动中文保存的VF的MAC信息`igb_adapter->vf_data[vf]`，并设置`IGB_VF_FLAG_PF_SET_MAC`标志，然后直接调用`igb_set_vf_mac`。

```c
static int igb_ndo_set_vf_mac(struct net_device *netdev, int vf, u8 *mac)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	if (!is_valid_ether_addr(mac) || (vf >= adapter->vfs_allocated_count))
		return -EINVAL;
	adapter->vf_data[vf].flags |= IGB_VF_FLAG_PF_SET_MAC;
	dev_info(&adapter->pdev->dev, "setting MAC %pM on VF %d\n", mac, vf);
	dev_info(&adapter->pdev->dev,
		 "Reload the VF driver to make this change effective.");
	if (test_bit(__IGB_DOWN, &adapter->state)) {
		dev_warn(&adapter->pdev->dev,
			 "The VF MAC address has been set, but the PF device is not up.\n");
		dev_warn(&adapter->pdev->dev,
			 "Bring the PF device up before attempting to use the VF device.\n");
	}
	return igb_set_vf_mac(adapter, vf, mac);
}
```

到这里基本上明白了第一方式设置mac地址失败的原因了，因为一旦通过第二种方式设置了VF的MAC地址，就会设置` IGB_VF_FLAG_PF_SET_MAC`标示位，就能再使用第一种方式了。

下面继续讨论第二个问题。从`igb_ndo_set_vf_mac`的提示可以看到，当我们通过PF去设置VF的MAC的时候，需要`Reload the VF driver to make this change effective.`。

难道要得重新加载VF驱动，如果是这样的话，会对所有的VF都有影响。实际上，VF驱动在加载的时候，的确会从PF的配置读取VF的MAC信息，然后设置VF:

```c
static int igbvf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
///...
	/*reset the controller to put the device in a known good state */
	err = hw->mac.ops.reset_hw(hw);
	if (err) {
		dev_info(&pdev->dev,
			 "PF still in reset state. Is the PF interface up?\n");
	} else {
		err = hw->mac.ops.read_mac_addr(hw); ///read MAC from PF
		if (err)
			dev_info(&pdev->dev, "Error reading MAC address.\n");
		else if (is_zero_ether_addr(adapter->hw.mac.addr))
			dev_info(&pdev->dev, "MAC address not assigned by administrator.\n");
		memcpy(netdev->dev_addr, adapter->hw.mac.addr, ///set MAC address
		       netdev->addr_len);
	}

///...
}
```

此外，VF驱动函数`igbvf_reset`也会设置网络设备地址`net_device->dev_addr`:

```c
static void igbvf_reset(struct igbvf_adapter *adapter)
{
	struct e1000_mac_info *mac = &adapter->hw.mac;
	struct net_device *netdev = adapter->netdev;
	struct e1000_hw *hw = &adapter->hw;

	/* Allow time for pending master requests to run */
	if (mac->ops.reset_hw(hw)) ///e1000_reset_hw_vf
		dev_err(&adapter->pdev->dev, "PF still resetting\n");

	mac->ops.init_hw(hw);///e1000_init_hw_vf

	if (is_valid_ether_addr(adapter->hw.mac.addr)) {
		memcpy(netdev->dev_addr, adapter->hw.mac.addr, ///set net_device MAC
		       netdev->addr_len);
		memcpy(netdev->perm_addr, adapter->hw.mac.addr,
		       netdev->addr_len);
	}

	adapter->last_reset = jiffies;
}
```

可以，VF驱动会用`adapter->hw.mac.addr`的值，该值从哪里获取？

实际上`reset_hw`，即`e1000_reset_hw_vf`会向PF发送`E1000_VF_RESET`消息，PF会返回MAC信息，VF读取然后保存在`hw->mac.perm_addr`：

```c
static s32 e1000_reset_hw_vf(struct e1000_hw *hw)
{
	if (timeout) {
		/* mailbox timeout can now become active */
		mbx->timeout = E1000_VF_MBX_INIT_TIMEOUT;

		/* notify pf of vf reset completion */
		msgbuf[0] = E1000_VF_RESET;
		mbx->ops.write_posted(hw, msgbuf, 1);

		msleep(10);

		/* set our "perm_addr" based on info provided by PF */
		ret_val = mbx->ops.read_posted(hw, msgbuf, 3);
		if (!ret_val) {
			if (msgbuf[0] == (E1000_VF_RESET | E1000_VT_MSGTYPE_ACK))
				memcpy(hw->mac.perm_addr, addr, 6); ///保存MAC
			else
				ret_val = -E1000_ERR_MAC_INIT;
		}
	}
```

`init_hw`，即`e1000_init_hw_vf`会尝试直接使用发送`E1000_VF_SET_MAC_ADDR`，PF当然返回`E1000_VT_MSGTYPE_NACK`。

```c
static s32 e1000_init_hw_vf(struct e1000_hw *hw)
{
	/* attempt to set and restore our mac address */
	e1000_rar_set_vf(hw, hw->mac.addr, 0); ///上面已经分析

	return E1000_SUCCESS;
}
```

此时，VF就会使用前面的`hw->mac.perm_addr`覆盖`hw->mac.addr`，到这里，`hw->mac.addr`就保存从PF获取的VF的MAC信息。

最后，最重要的一点，`igbvf_reset`什么时候会被调用？

实际上上，`igbvf_down`会调用`igbvf_reset`:

```c
void igbvf_down(struct igbvf_adapter *adapter)
{

///...
	igbvf_reset(adapter);
	igbvf_clean_tx_ring(adapter->tx_ring);
	igbvf_clean_rx_ring(adapter->rx_ring);
}
```

这意味着，我们只需要将VF shutdown，我们通过PF给VF设置的MAC信息就会反映到VF网络设备：

```sh
# ip link set dev8 up  ##由于VF处于down状态，需要先将其UP
# ip link show dev8    
8: dev8: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP qlen 1000
    link/ether 14:05:0a:f5:ac:3e brd ff:ff:ff:ff:ff:ff

# ip link show eth1    
2: eth1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP qlen 1000
    link/ether 8c:dc:d4:b1:60:c0 brd ff:ff:ff:ff:ff:ff
    vf 0 MAC 14:05:0a:f5:ac:36, vlan 3
    vf 1 MAC 14:05:0a:f5:ac:3a, vlan 3
    vf 2 MAC 14:05:00:f5:ac:3e, vlan 3
...
# ip link set dev8 down
# ip link show dev8    
8: dev8: <BROADCAST,MULTICAST> mtu 1500 qdisc pfifo_fast state DOWN qlen 1000
    link/ether 14:05:00:f5:ac:3e brd ff:ff:ff:ff:ff:ff
```
可以看到`dev8`的地址从`14:05:0a:f5:ac:3e`变成了`14:05:00:f5:ac:3e`。

down对应的dmesg信息：

```
[699929.948823] igb 0000:03:00.0: VF 2 attempted to override administratively set MAC address
[699929.948823] Reload the VF driver to resume operations
[699929.950056] igb 0000:03:00.0: VF 2 attempted to override administratively set VLAN tag
[699929.950056] Reload the VF driver to resume operations
[699929.950539] igbvf 0000:03:11.0: Failed to remove vlan id 0
[699929.950543] failed to kill vid 0081/0 for device dev8
```

## 3 总结

通过PF设置VF的MAC后，需要重启VF网络设备，VF才能同步到PF的MAC信息。