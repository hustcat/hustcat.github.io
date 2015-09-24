---
layout: post
title: The reason of Intel X540 SR-IOV initialize failed
date: 2015-09-23 18:27:30
categories: Linux
tags: network
excerpt: Intel X540 SR-IOV initialize failed:PF still in reset state.Is the PF interface up?
---

# 问题

最近在使用intel X540 10G网卡的SR-IOV时，出现下面的错误：

```
[   56.126379] ixgbevf 0000:01:10.0: enabling device (0000 -> 0002)
[   56.263462] ixgbevf 0000:01:10.0: PF still in reset state.  Is the PF interface up?
[   56.355415] ixgbevf 0000:01:10.0: Assigning random MAC address
[   56.425444] ixgbevf: probe of 0000:01:10.0 failed with error -15
```

简单来说，VF在初始化，会检查PF是否处于reset状态，如果是，就会返回错误IXGBE_ERR_RESET_FAILED。

![](/assets/2015-09-24-intel-x540-sriov-failed.png)

```c
///ixgbe_type.h
#define IXGBE_ERR_RESET_FAILED          -15
 
///Initialize struct ixgbevf_adapter
static int __devinit ixgbevf_sw_init(struct ixgbevf_adapter *adapter)
{
	///
	ixgbe_init_ops_vf(hw);
	hw->mbx.ops.init_params(hw);
	
	///...
	err = hw->mac.ops.reset_hw(hw); ///ixgbe_reset_hw_vf
	if (err) {
		dev_info(pci_dev_to_dev(pdev),
			 "PF still in reset state.  Is the PF interface up?\n");
	}

	///...

	set_bit(__IXGBEVF_DOWN, &adapter->state);

out:
	return err;
}
	
s32 ixgbe_reset_hw_vf(struct ixgbe_hw *hw)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	u32 timeout = IXGBE_VF_INIT_TIMEOUT;///200

	///...
	
	/* we cannot reset while the RSTI / RSTD bits are asserted */
	while (!mbx->ops.check_for_rst(hw, 0) && timeout) { ///ixgbe_check_for_rst_vf
		timeout--;
		udelay(5);
	}

	if (!timeout)
		return IXGBE_ERR_RESET_FAILED; ///-15
```

VF会尝试200次(每次间隔5us)，即1ms，如果还是reset状态，则ixgbe_reset_hw_vf向上层返回IXGBE_ERR_RESET_FAILED。

** 那么VF如何检测PF是否处于reset状态呢？ **

```c
static s32 ixgbe_check_for_rst_vf(struct ixgbe_hw *hw, u16 mbx_id)
{
	s32 ret_val = IXGBE_ERR_MBX;

	UNREFERENCED_1PARAMETER(mbx_id);
	if (!ixgbe_check_for_bit_vf(hw, (IXGBE_VFMAILBOX_RSTD |
	    IXGBE_VFMAILBOX_RSTI))) {
		ret_val = 0;
		hw->mbx.stats.rsts++;
	}

	return ret_val;
}
```
即检测RSTD和RSTI位是否为0。

## RSTI/RSTD

This mechanism is provided specifically for a PF software reset but can be used in other reset cases.The procedure is as follows:

(1)One of the following reset cases takes place: — LAN Power Good

- PCIe Reset (PERST and in-band)
- D3hot --> D0
- FLR
- Software reset by the PF

(2)The 82599 sets the RSTI bits in all the VFMailbox registers. Once the reset completes, each VF can read its VFMailbox register to identify a reset in progress.

- The VF might poll the RSTI bit to detect if the PF is in the process of configuring the device.

(3)Once the PF completes configuring the device,it sets the CTRL_EXT.PFRSTD bit.As a result, the 82599 clears the RSTI bits in all the VFMailbox registers and sets the Reset Done (RSTD) bits in all the VFMailbox registers.

- The VF might read the RSTD bit to detect that a reset has occurred. The RSTD bit is cleared on read.

PF/VF通过VFMailbox寄存器（IXGBE_VFMAILBOX）来实现reset过程同步，PF在初始化时，设置所有VF的VFMailbox寄存器RSTI标志，一旦reset完成，VF可以读取自己的VFMailbox寄存器，来确认PF处于reset状态。

一旦PF完成配置，会写CTRL_EXT.PFRSTD标志。网卡（X540）会清除所有VFMailbox Registers的RSTI标志位，并设置RSTD标志。VF在检测到RSTD标志之前，不应该激活中断（Until a RSTD condition is detected, the VFs should access only the VFMailbox register and should not attempt to activate the interrupt mechanism or the transmit and receive process.）。

```c
static void ixgbe_up_complete(struct ixgbe_adapter *adapter)
{
///...
    /* Set PF Reset Done bit so PF/VF Mail Ops can work */
    ctrl_ext = IXGBE_READ_REG(hw, IXGBE_CTRL_EXT);
    ctrl_ext |= IXGBE_CTRL_EXT_PFRSTD;
    IXGBE_WRITE_REG(hw, IXGBE_CTRL_EXT, ctrl_ext);
}
```

# 解决方法

将PF(ixgbe)和VF(ixgbevf)的加载分开，系统启动时，只加载ixgbe，启动后，再手动加载ixgbevf。这样就能保证VF在初始化的时候，PF已经[UP](https://www.mail-archive.com/e1000-devel@lists.sourceforge.net/msg02163.html)且ready。

# igb driver

为什么igb也会有类似的问题，但是VF初始化并没有报这个错误（仍然创建了VF对应的网络设备）：

```
[   63.974721] igbvf 0000:01:12.1: enabling device (0000 -> 0002)
[   64.044339] igbvf 0000:01:12.1: irq 151 for MSI/MSI-X
[   64.044350] igbvf 0000:01:12.1: irq 152 for MSI/MSI-X
[   64.044360] igbvf 0000:01:12.1: irq 153 for MSI/MSI-X
[   64.045505] igbvf 0000:01:12.1: PF still in reset state. Is the PF interface up?
[   64.133980] igbvf 0000:01:12.1: Assigning random MAC address.
[   64.203651] igbvf 0000:01:12.1: PF still resetting
[   64.260990] igbvf 0000:01:12.1: Intel(R) I350 Virtual Function
[   64.330564] igbvf 0000:01:12.1: Address: 0e:ce:51:a0:4b:9f
```

从igb代码来看，实际上，如果e1000_reset_hw_vf返回的错误，igbvf_probe只是打印一行错误日志，而并没有向上层返回该错误，这是igb驱动的BUG，还是故意为之呢？？？

```c
static int igbvf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	///...
	/* setup adapter struct */
	err = igbvf_sw_init(adapter);
	if (err)
		goto err_sw_init;
		
	/*reset the controller to put the device in a known good state */
	err = hw->mac.ops.reset_hw(hw);///e1000_reset_hw_vf
	if (err) {
		dev_info(&pdev->dev,
			 "PF still in reset state. Is the PF interface up?\n");
	}	
}
```

我把这个问题反馈给[e1000-devel maillist](e1000-devel@lists.sourceforge.net)，
Alexander Duyck的[回答](http://sourceforge.net/p/e1000/mailman/message/34485880/)解答了我的困惑。

简单来说，这是两者实现的区别，igbvf由于忽略了PF的状态，最后VF会分配一个随机的MAC地址。而ixgbevf会让VF从PF获取一个一致的MAC地址（This forced the interface to wait until the PF was up and ready to assign MAC addresses so that the VF should have a consistent address），这需要PF已经UP。所以，ixgbevf发现PF没有UP，就会返回错误。

但是，内核的实现确有些区别，从[这里](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/ixgbevf/ixgbevf_main.c#L2673)可以看出，内核主线代码选择了与igb相同的方式。

```c
/**
 * ixgbevf_sw_init - Initialize general software structures
 * @adapter: board private structure to initialize
 *
 * ixgbevf_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 **/
static int ixgbevf_sw_init(struct ixgbevf_adapter *adapter)
{
	///...

	set_bit(__IXGBEVF_DOWN, &adapter->state);
	return 0;

out:
	return err;

}
```

# VF MAC address

VF在初始化过程中，会尝试从PF获取MAC地址：

```c
///ixgbevf/ixgbe_vf.c
///Performs VF reset
s32 ixgbe_reset_hw_vf(struct ixgbe_hw *hw)
{

	/* 从PF读取MAC
	 * set our "perm_addr" based on info provided by PF
	 * also set up the mc_filter_type which is piggy backed
	 * on the mac address in word 3
	 */
	ret_val = mbx->ops.read_posted(hw, msgbuf,
			IXGBE_VF_PERMADDR_MSG_LEN, 0);

	memcpy(hw->mac.perm_addr, addr, IXGBE_ETH_LENGTH_OF_ADDRESS);
	hw->mac.mc_filter_type = msgbuf[IXGBE_VF_MC_TYPE_WORD];
}
```

ixgbevf_probe -> ixgbevf_sw_init -> ixgbe_reset_hw_vf

但实际上，对于ixgbevf，即使PF已经UP，如果没有对VF设置MAC，在手动reload ixgbevf时，ixgbevf会分配的一个随机地址:

```
[   55.633644] ixgbevf 0000:03:10.0: enabling device (0000 -> 0002)
[   55.697229] ixgbe 0000:03:00.0 eth1: VF Reset msg received from vf 0
[   55.697494] ixgbe 0000:03:00.0: VF 0 has no MAC address assigned, you may have to assign one manually
[   55.713717] ixgbevf 0000:03:10.0: MAC address not assigned by administrator.
[   55.713719] ixgbevf 0000:03:10.0: Assigning random MAC address
[   55.714279] ixgbevf 0000:03:10.0: irq 102 for MSI/MSI-X
[   55.714289] ixgbevf 0000:03:10.0: irq 103 for MSI/MSI-X
[   55.714297] ixgbevf 0000:03:10.0: irq 104 for MSI/MSI-X
[   55.714313] ixgbevf 0000:03:10.0: Multiqueue Enabled: Rx Queue count = 2, Tx Queue count = 2
[   55.714587] ixgbevf: eth2: ixgbevf_probe: Intel(R) X540 Virtual Function
[   55.714589] 52:06:ed:37:7d:c8
[   55.714592] ixgbevf: eth2: ixgbevf_probe: GRO is enabled
[   55.714593] ixgbevf: eth2: ixgbevf_probe: Intel(R) 10 Gigabit PCI Express Virtual Function Network Driver
```

```c
///Initialize struct ixgbevf_adapter
static int __devinit ixgbevf_sw_init(struct ixgbevf_adapter *adapter)
{
	err = hw->mac.ops.reset_hw(hw); ///ixgbe_reset_hw_vf
	if (err) {
		dev_info(pci_dev_to_dev(pdev),
			 "PF still in reset state.  Is the PF interface up?\n");
	} else {
		err = hw->mac.ops.init_hw(hw); ////ixgbe_init_hw_vf
		if (err) {
			dev_err(pci_dev_to_dev(pdev),
				"init_shared_code failed: %d\n", err);
			goto out;
		}
		ixgbevf_negotiate_api(adapter);
		err = hw->mac.ops.get_mac_addr(hw, hw->mac.addr); ///ixgbe_get_mac_addr_vf
		if (err)
			dev_info(&pdev->dev, "Error reading MAC address\n");
		else if (is_zero_ether_addr(adapter->hw.mac.addr))
			dev_info(&pdev->dev,
				 "MAC address not assigned by administrator.\n");
		memcpy(netdev->dev_addr, hw->mac.addr, netdev->addr_len);
	}

	if (!is_valid_ether_addr(netdev->dev_addr)) {
		dev_info(&pdev->dev, "Assigning random MAC address\n");
		eth_hw_addr_random(netdev);
		memcpy(hw->mac.addr, netdev->dev_addr, netdev->addr_len);
	}
}

/**
 *  ixgbe_get_mac_addr_vf - Read device MAC address
 *  @hw: pointer to the HW structure
 **/
s32 ixgbe_get_mac_addr_vf(struct ixgbe_hw *hw, u8 *mac_addr)
{
	int i;

	for (i = 0; i < IXGBE_ETH_LENGTH_OF_ADDRESS; i++)
		mac_addr[i] = hw->mac.perm_addr[i];

	return 0;
}
```

# 相关资料

* 《Intel® Ethernet Controller X540 Datasheet》
* [[E1000-devel] question about ixgbevf and RSTD bit](http://sourceforge.net/p/e1000/mailman/message/29616529/)
