---
layout: post
title: Linux BPF introduction
date: 2016-10-19 16:00:30
categories: Linux
tags: BPF
excerpt: Linux BPF introduction
---

## BPF samples

### Compiling

```
# git clone https://github.com/torvalds/linux.git
# cd linux

# make headers_install ##This will creates a local "usr/include" directory in the git/build top level directory, that the make system automatically pickup first.

# make samples/bpf/
```

值得注意的是，对于kprobe，需要保证源代码的内核版本(LINUX_VERSION_CODE)与当前运行的内核版本一致，内核会[检查](https://github.com/torvalds/linux/blob/master/kernel/bpf/syscall.c#L744)：

```c
static int bpf_prog_load(union bpf_attr *attr)
{
...
	if (type == BPF_PROG_TYPE_KPROBE &&
	    attr->kern_version != LINUX_VERSION_CODE)
		return -EINVAL;
```


```
# ./sockex1
TCP 0 UDP 0 ICMP 0 bytes
TCP 0 UDP 0 ICMP 196 bytes
TCP 0 UDP 0 ICMP 392 bytes
TCP 0 UDP 0 ICMP 588 bytes
TCP 0 UDP 0 ICMP 784 bytes
```

参考[eBPF sample programs](https://github.com/torvalds/linux/tree/master/samples/bpf)。

## BCC

[BCC](https://github.com/iovisor/bcc) is a toolkit for creating efficient kernel tracing and manipulation programs based on [BPF](https://www.kernel.org/doc/Documentation/networking/filter.txt)。

### Install

```
# echo "deb [trusted=yes] https://repo.iovisor.org/apt/xenial xenial-nightly main" | sudo tee /etc/apt/sources.list.d/iovisor.list
# apt-get update
# apt-get install bcc-tools
```

会安装下面几个包:

```
bcc-tools libbcc python-bcc
```

可以通过命令`dpkg -L bcc-tools`查看`bcc-tools`的安装目录位于`/usr/share/bcc/tools`。


参考[Installing BCC](https://github.com/iovisor/bcc/blob/master/INSTALL.md)


### Test

* bitesize

`bitesize` show I/O distribution for requested block sizes, by process name:

```sh
# dd if=/dev/zero of=f1.data oflag=direct bs=4k count=1024 
1024+0 records in
1024+0 records out
4194304 bytes (4.2 MB, 4.0 MiB) copied, 0.11078 s, 37.9 MB/s
```

```
# ./bitesize
Tracing... Hit Ctrl-C to end.
^C
Process Name = 'dd'
     Kbytes              : count     distribution
         0 -> 1          : 0        |                                        |
         2 -> 3          : 0        |                                        |
         4 -> 7          : 1024     |****************************************|
```


## Internal

* eBPF programs

BPF程序分两部分，一部分是用户态程序，另外一部分是由交给内核执行的`restricted C`代码(*_kern.c)。内核部分通过llvm将C代码编译成eBPF字节码。然后通过系统调用[bpf](http://man7.org/linux/man-pages/man2/bpf.2.html)将eBPF字节码传给内核，内核再将字节码JIT编译成机器码并执行：

```c
char bpf_log_buf[LOG_BUF_SIZE];

int
bpf_prog_load(enum bpf_prog_type type,
             const struct bpf_insn *insns, int insn_cnt,
             const char *license)
{
   union bpf_attr attr = {
       .prog_type = type,
       .insns     = ptr_to_u64(insns),
       .insn_cnt  = insn_cnt,
       .license   = ptr_to_u64(license),
       .log_buf   = ptr_to_u64(bpf_log_buf),
       .log_size  = LOG_BUF_SIZE,
       .log_level = 1,
   };

   return bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
}
```

基本过程如图：

![](/assets/bpf/bfp-intro-1.png)

* eBPF maps

`eBPF maps`是内核与用户态程序传递数据的数据结构：

```
int
bpf_create_map(enum bpf_map_type map_type,
             unsigned int key_size,
             unsigned int value_size,
             unsigned int max_entries)
{
  union bpf_attr attr = {
      .map_type    = map_type,
      .key_size    = key_size,
      .value_size  = value_size,
      .max_entries = max_entries
  };

  return bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}
```

[BPF Internals - I](https://github.com/iovisor/bpf-docs/blob/master/bpf-internals-1.md)、[BPF Internals - II](https://github.com/iovisor/bpf-docs/blob/master/bpf-internals-2.md)详细介绍了BPF的原理与实现。

## Others

* [Dive into BPF: a list of reading material](https://qmonnet.github.io/whirl-offload/2016/09/01/dive-into-bpf/)
* [eBPF: One Small Step](http://www.brendangregg.com/blog/2015-05-15/ebpf-one-small-step.html)

cilium:

* [Cilium - BPF & XDP for containers](https://github.com/cilium/cilium)


kprobe:

* [Kprobe-based Event Tracing](https://www.kernel.org/doc/Documentation/trace/kprobetrace.txt)
kprobe usage example:
* [kprobe example](https://github.com/hustcat/perf-tools/blob/master/kernel/kprobe)



## Reference

* [Berkeley Packet Filter](https://en.wikipedia.org/wiki/Berkeley_Packet_Filter)
* [A JIT for packet filters](https://lwn.net/Articles/437981/)
* [BPF tracing filters](https://lwn.net/Articles/575531/)
* [Linux Socket Filtering aka Berkeley Packet Filter (BPF)](https://www.kernel.org/doc/Documentation/networking/filter.txt)