---
layout: post
title: Introduction to the UIO
date: 2017-10-10 23:00:30
categories: Linux
tags: driver
excerpt: Introduction to the UIO
---

## UIO

每个UIO设备可以通过设备文件（`/dev/uioX`）和sysfs的属性文件来访问。

可以通过mmap映射`/dev/uioX`来访问UIO设备的寄存器或者RAM。

直接read `/dev/uioX`来获取UIO设备的中断，read()会被阻塞，发生中断时就会返回。

> Each UIO device is accessed through a device file and several sysfs attribute files. The device file will be called /dev/uio0 for the first device, and /dev/uio1, /dev/uio2 and so on for subsequent devices.
> 
> /dev/uioX is used to access the address space of the card. Just use mmap() to access registers or RAM locations of your card.
>
> Interrupts are handled by reading from /dev/uioX. A blocking read() from /dev/uioX will return as soon as an interrupt occurs. You can also use select() on /dev/uioX to wait for an interrupt. The integer value read from /dev/uioX represents the total interrupt count. You can use this number to figure out if you missed some interrupts.

## uio driver

### uio_pci_generic

UIO设备需要UIO内核驱动的支持，[uio_pci_generic](https://www.kernel.org/doc/html/v4.12/driver-api/uio-howto.html#generic-pci-uio-driver)是一个通用的PCI UIO设备的内核驱动。

> UIO does not completely eliminate the need for kernel-space code. A small module is required to set up the device, perhaps interface to the PCI bus, and register an interrupt handler. The last function (interrupt handling) is particularly important; much can be done in user space, but there needs to be an in-kernel interrupt handler which knows how to tell the device to stop crying for attention.

```
///drivers/uio/uio_pci_generic.c
static struct pci_driver driver = {
	.name = "uio_pci_generic",
	.id_table = NULL, /* only dynamic id's */
	.probe = probe,
	.remove = remove,
};
```

### interrupt

`uio_register_device` 注册UIO驱动时时，会注册中断处理函数`uio_interrupt`:

```
/**
 * uio_register_device - register a new userspace IO device
 * @owner:	module that creates the new device
 * @parent:	parent device
 * @info:	UIO device capabilities
 *
 * returns zero on success or a negative error code.
 */
int __uio_register_device(struct module *owner,
			  struct device *parent,
			  struct uio_info *info)
{
///...
	if (info->irq && (info->irq != UIO_IRQ_CUSTOM)) {
		ret = request_irq(info->irq, uio_interrupt,
				  info->irq_flags, info->name, idev);
		if (ret)
			goto err_request_irq;
	}
}

/**
 * uio_interrupt - hardware interrupt handler
 * @irq: IRQ number, can be UIO_IRQ_CYCLIC for cyclic timer
 * @dev_id: Pointer to the devices uio_device structure
 */
static irqreturn_t uio_interrupt(int irq, void *dev_id)
{
	struct uio_device *idev = (struct uio_device *)dev_id;
	irqreturn_t ret = idev->info->handler(irq, idev->info); ///irqhandler

	if (ret == IRQ_HANDLED)
		uio_event_notify(idev->info); ///notify userspace

	return ret;
}
```

* uio_event_notify

UIO设备每次发生中断时，都会增加`uio_device->event`，然后唤醒等待进程
```
/** notify userspace application
 * uio_event_notify - trigger an interrupt event
 * @info: UIO device capabilities
 */
void uio_event_notify(struct uio_info *info)
{
	struct uio_device *idev = info->uio_dev;

	atomic_inc(&idev->event);
	wake_up_interruptible(&idev->wait); ///wake up waiting process
	kill_fasync(&idev->async_queue, SIGIO, POLL_IN);
}
```

* read /dev/uioX

每次调用open `/dev/uioX`时，内核都会创建一个`uio_listener`，关联到`struct file->private_data`:

```
static int uio_open(struct inode *inode, struct file *filep)
{
	struct uio_device *idev;
	struct uio_listener *listener;
	int ret = 0;

	mutex_lock(&minor_lock);
	idev = idr_find(&uio_idr, iminor(inode));
	mutex_unlock(&minor_lock);
///...
	listener = kmalloc(sizeof(*listener), GFP_KERNEL);
	if (!listener) {
		ret = -ENOMEM;
		goto err_alloc_listener;
	}

	listener->dev = idev;
	listener->event_count = atomic_read(&idev->event);
	filep->private_data = listener;
```

然后每次read()时，发现`uio_listener->event_count`与`uio_device->event`不相同时，就意味着发生中断，会返回对应的值；否则，将当前进程加入到`uio_device->wait`队列，然后挂起当前进程：

```
static ssize_t uio_read(struct file *filep, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct uio_listener *listener = filep->private_data;
	struct uio_device *idev = listener->dev;
	DECLARE_WAITQUEUE(wait, current);
///...
	add_wait_queue(&idev->wait, &wait);

	do {
		set_current_state(TASK_INTERRUPTIBLE);

		event_count = atomic_read(&idev->event);
		if (event_count != listener->event_count) { ///irq happened
			if (copy_to_user(buf, &event_count, count))
				retval = -EFAULT;
			else {
				listener->event_count = event_count;
				retval = count;
			}
			break;
		}

		if (filep->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		schedule();
	} while (1);

	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&idev->wait, &wait);

	return retval;
}
```

* uio_mmap

当用户态driver调用mmap映射`/dev/uioX`时，内核会调用`uio_mmap`映射UIO设备的RAM：

```
static int uio_mmap(struct file *filep, struct vm_area_struct *vma)
{
	struct uio_listener *listener = filep->private_data;
	struct uio_device *idev = listener->dev;
	int mi;
	unsigned long requested_pages, actual_pages;
	int ret = 0;

	if (vma->vm_end < vma->vm_start)
		return -EINVAL;

	vma->vm_private_data = idev;

	mi = uio_find_mem_index(vma);
	if (mi < 0)
		return -EINVAL;

	requested_pages = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	actual_pages = ((idev->info->mem[mi].addr & ~PAGE_MASK)
			+ idev->info->mem[mi].size + PAGE_SIZE -1) >> PAGE_SHIFT;
	if (requested_pages > actual_pages)
		return -EINVAL;

	if (idev->info->mmap) {
		ret = idev->info->mmap(idev->info, vma);
		return ret;
	}

	switch (idev->info->mem[mi].memtype) {
		case UIO_MEM_PHYS:
			return uio_mmap_physical(vma);
		case UIO_MEM_LOGICAL:
		case UIO_MEM_VIRTUAL:
			return uio_mmap_logical(vma);
		default:
			return -EINVAL;
	}
}
```

更多关于mmap参考[Linux MMAP & Ioremap introduction](https://www.slideshare.net/gene7299/linux-mmap-ioremap-introduction).

## userspace driver

```c
fd = open(“/dev/uio0”, O_RDWR|O_SYNC);
/* Map device's registers into user memory */
/* fitting the memory area on pages */
offset = addr & ~PAGE_MASK;
addr = 0 /* region 0 */ * PAGE_SIZE;
size = (size + PAGE_SIZE ­ 1) / PAGE_SIZE * PAGE_SIZE;
iomem = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, 
fd, addr);
iomem += offset;
/* Stop the counting */
*(u_char *)SH_TMU_TSTR(iomem) |= ~(TSTR_TSTR2);
...
/* Wait for an interrupt */;
read(fd, &n_pending, sizeof(u_long));
val = *(u_int *)SH_TMU2_TCNT(iomem);
...
/* Stop the TMU */
*(u_char *)SH_TMU_TSTR(iomem) &= ~(TSTR_TSTR2);
munmap(iomem, size);
close(fd);
```

详细参考 [Using UIO in an embedded platform](http://elinux.org/images/b/b0/Uio080417celfelc08.pdf).

## 参考

* [The Userspace I/O HOWTO](https://www.kernel.org/doc/html/v4.12/driver-api/uio-howto.html)
* [UIO: user-space drivers](https://lwn.net/Articles/232575/)
* [Using UIO in an embedded platform](http://elinux.org/images/b/b0/Uio080417celfelc08.pdf)