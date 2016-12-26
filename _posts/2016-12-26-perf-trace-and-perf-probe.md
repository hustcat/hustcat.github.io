---
layout: post
title: perf-trace and perf-probe and uprobe
date: 2016-12-26 12:00:30
categories: Linux
tags: perf-trace perf-probe
excerpt: perf-trace and perf-probe and uprobe
---

## perf-trace

[perf-trace](http://man7.org/linux/man-pages/man1/perf-trace.1.html)是内核[tools/perf](https://github.com/torvalds/linux/tree/master/tools/perf)提供一个子工具，相对于传统的`strace`，它不用stop目标进程，开销更小。


```sh
# dd if=/dev/zero of=/dev/null bs=1 count=500k
512000+0 records in
512000+0 records out
512000 bytes (512 kB) copied, 0.421561 s, 1.2 MB/s

# strace -c dd if=/dev/zero of=/dev/null bs=1 count=500k       
512000+0 records in
512000+0 records out
512000 bytes (512 kB) copied, 19.8597 s, 25.8 kB/s

# perf stat -e 'syscalls:sys_enter_*' dd if=/dev/zero of=/dev/null bs=1 count=500k   
512000+0 records in
512000+0 records out
512000 bytes (512 kB) copied, 0.510631 s, 1.0 MB/s
```

可以看到`perf`的性能比`strace`好很多。但是遗憾的是3.10.x内核带的`perf-trace`不能指定event。


## perf-probe

[perf-probe](http://man7.org/linux/man-pages/man1/perf-probe.1.html)可以创建[dynamic tracepoints](https://lwn.net/Articles/343766/)，在kernel debuginfo的帮助下，可以实现[内核代码行级](http://www.brendangregg.com/blog/2014-09-11/perf-kernel-line-tracing.html)动态跟踪。

我们可以通过`-nv`查看`perf probe`命令的效果(不会执行)：

```sh
# perf probe -nv 'dump_write:0 file addr nr'           
probe-definition(0): dump_write:0 file addr nr 
symbol:dump_write file:(null) line:0 offset:0 return:0 lazy:(null)
parsing arg: file into file
parsing arg: addr into addr
parsing arg: nr into nr
3 arguments
Looking at the vmlinux_path (6 entries long)
Using /boot/vmlinux-3.10.102-1-tlinux2-0040.tl1 for symbols
Probe point found: dump_write+0
Searching 'file' variable in context.
Converting variable file into trace event.
file type is (null).
Searching 'addr' variable in context.
Converting variable addr into trace event.
addr type is (null).
Searching 'nr' variable in context.
Converting variable nr into trace event.
nr type is int.
find 1 probe_trace_events.
Opening /sys/kernel/debug//tracing/kprobe_events write=1
Added new event:
Writing event: p:probe/dump_write dump_write+0 file=%di:u64 addr=%si:u64 nr=%dx:s32
  probe:dump_write     (on dump_write with file addr nr)

You can now use it in all perf tools, such as:

        perf record -e probe:dump_write -aR sleep 1
```

然后，我们可以在没有kernel debuginfo的内核上执行下面的命令:

```sh
# perf probe 'dump_write+0 file=%di:u64 addr=%si:u64 nr=%dx:s32'
Failed to find path of kernel module.
Added new event:
  probe:dump_write     (on dump_write with file=%di:u64 addr=%si:u64 nr=%dx:s32)

You can now use it in all perf tools, such as:

        perf record -e probe:dump_write -aR sleep 1
# perf probe --list
  probe:dump_write     (on dump_write with file addr nr)

# ls /sys/kernel/debug/tracing/events/probe/
dump_write  enable  filter

# perf record -e probe:dump_write -aR sleep 5

# perf script 
...
     test_signal 25262 [005] 10874780.803504: probe:dump_write: (ffffffff811d472f) file=ffff881e95dbf100 addr=ffff880fcb8a7840 nr=64
     test_signal 25262 [005] 10874780.803522: probe:dump_write: (ffffffff811d472f) file=ffff881e95dbf100 addr=ffff880fcb8a7e00 nr=56
     test_signal 25262 [005] 10874780.803523: probe:dump_write: (ffffffff811d472f) file=ffff881e95dbf100 addr=ffff880fc949bc38 nr=56
     test_signal 25262 [005] 10874780.803525: probe:dump_write: (ffffffff811d472f) file=ffff881e95dbf100 addr=ffff880fc949bc38 nr=56
     test_signal 25262 [005] 10874780.803526: probe:dump_write: (ffffffff811d472f) file=ffff881e95dbf100 addr=ffff880fc949bc38 nr=56
     test_signal 25262 [005] 10874780.803527: probe:dump_write: (ffffffff811d472f) file=ffff881e95dbf100 addr=ffff880fc949bc38 nr=56
....
```

也可以使用`perf-tools`中的`kprobe`:

```sh
# ./kprobe 'p:dump_write nr=%dx:s32'
dump_write
Tracing kprobe dump_write. Ctrl-C to end.
     test_signal-31031 [005] d... 10875856.829642: dump_write: (dump_write+0x0/0x70) nr=64
     test_signal-31031 [005] d... 10875856.829658: dump_write: (dump_write+0x0/0x70) nr=56
     test_signal-31031 [005] d... 10875856.829660: dump_write: (dump_write+0x0/0x70) nr=56
     test_signal-31031 [005] d... 10875856.829660: dump_write: (dump_write+0x0/0x70) nr=56
     test_signal-31031 [005] d... 10875856.829661: dump_write: (dump_write+0x0/0x70) nr=56
...
```


## uprobe

[uprobe](https://www.kernel.org/doc/Documentation/trace/uprobetracer.txt)是在[3.5](https://lwn.net/Articles/499190/)加到内核的，它可以实现对用户进程动态trace。

```sh
# perf probe -x /lib64/libc.so.6 malloc
Added new event:
  probe_libc:malloc    (on 0x7a640)

You can now use it in all perf tools, such as:

        perf record -e probe_libc:malloc -aR sleep 1

# cat uprobe_events  
p:probe_libc/malloc /lib64/libc.so.6:0x000000000007a640
# perf probe --list
  probe_libc:malloc    (on 0x000000000007a640)

# perf record -e probe_libc:malloc -aR sleep 3 
[ perf record: Woken up 1 times to write data ]
[ perf record: Captured and wrote 0.092 MB perf.data (~4016 samples) ]


# perf script
...
           sleep  5924 [002] 256797.635705: probe_libc:malloc: (7fd7e092a640)
           sleep  5924 [002] 256797.635740: probe_libc:malloc: (7fd7e092a640)
           sleep  5924 [002] 256797.635752: probe_libc:malloc: (7fd7e092a640)

# perf probe --del probe_libc:malloc
Removed event: probe_libc:malloc
```

`uprobe`在[3.14](https://kernelnewbies.org/Linux_3.14#head-ca18fd90b3cee1181d74251909e0dda6934b5add)做了很多改进。根据[Brendan Gregg](http://www.brendangregg.com/blog/2015-06-28/linux-ftrace-uprobe.html)的经验，最好在4.0以上的内核使用`uprobe`。

> I was hoping to use uprobes on the Linux 3.13 kernels I'm now debugging, but have frequently hit issues where the target process either crashes or enters an endless spin loop. 
> These bugs seem to have been fixed by Linux 4.0 (maybe earlier). For that reason, uprobe won't run on kernels older than 4.0 (without -F to force). Maybe that's pessimistic, and it should be 3.18 or something.

## Reference

* [perf Examples](http://www.brendangregg.com/perf.html)
* [Dynamic probes with ftrace](https://lwn.net/Articles/343766/)
* [Linux Perf Tools: Probe & Trace](http://events.linuxfoundation.org/sites/events/files/slides/perf-collabsummit-2015.pdf)
* [Kernel Line Tracing: Linux perf Rides the Rocket](http://www.brendangregg.com/blog/2014-09-11/perf-kernel-line-tracing.html)

* [Uprobe-tracer: Uprobe-based Event Tracing](https://www.kernel.org/doc/Documentation/trace/uprobetracer.txt)
* [Linux uprobe: User-Level Dynamic Tracing](http://www.brendangregg.com/blog/2015-06-28/linux-ftrace-uprobe.html)