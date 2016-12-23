---
layout: post
title: Introduction to the perf-tools
date: 2016-12-23 18:00:30
categories: Linux
tags: tracing
excerpt: Introduction to the perf-tools
---

[ftrace](https://www.kernel.org/doc/Documentation/trace/ftrace.txt)是内核在2.6.27提供的trace机制。使用`ftrace`可以在不影响进程运行的情况下，跟踪分析内核中发生的事情。`ftrace`提供了不同的跟踪器，以用于不同的场合，比如跟踪内核函数调用、对上下文切换进行跟踪、查看中断被关闭的时长、跟踪内核态中的延迟以及性能问题等。

[perf-tools](https://github.com/brendangregg/perf-tools)是[Brendan Gregg](http://www.brendangregg.com/blog/index.html)基于[perf_events (aka perf)](https://perf.wiki.kernel.org/index.php/Main_Page)和[ftrace](https://www.kernel.org/doc/Documentation/trace/ftrace.txt)写的一套脚本工具。它包装并简化了`perf`、`ftrace`以及`kprobe`的使用，相比于直接去操作`ftrace`的内核接口，使用这些成熟的脚本工具更加方便安全。

个人已经使用`perf-tools`定位解决了无数生产环境线上问题(当然，还需熟悉内核的代码)。下面介绍几个常用工具。


## funccount

`funccount`可以用来统计内核函数的调用次数：

```sh
# ./funccount "schedule"
Tracing "schedule"... Ctrl-C to end.
^C
FUNC                              COUNT
schedule                          64261

Ending tracing...
```

`funccount`会设置`/sys/kernel/debug/tracing/set_ftrace_filter`:

```sh
# cat /sys/kernel/debug/tracing/set_ftrace_filter 
schedule
```

`trace_stat/*`目录下的文件为每个CPU执行函数的次数：

```sh
# ls /sys/kernel/debug/tracing/trace_stat/
function0   function13  function18  function22  function27  function31  function36  function40  function45  function7
function1   function14  function19  function23  function28  function32  function37  function41  function46  function8
function10  function15  function2   function24  function29  function33  function38  function42  function47  function9
function11  function16  function20  function25  function3   function34  function39  function43  function5
function12  function17  function21  function26  function30  function35  function4   function44  function6

# cat /sys/kernel/debug/tracing/trace_stat/function0   
  Function                               Hit    Time            Avg             s^2
  --------                               ---    ----            ---             ---
  schedule                               821    495097294 us     603041.7 us     8175448486 us 
```


## funcgraph

`funcgraph`可以用来trace内核某个函数的调用栈，例如：

```sh
# ./funcgraph "xfs_dir_open"
Tracing "xfs_dir_open"... Ctrl-C to end.
  2)               |  xfs_dir_open [xfs]() {
  2)   0.160 us    |    xfs_file_open [xfs]();
  2)               |    xfs_ilock_map_shared [xfs]() {
  2)               |      xfs_ilock [xfs]() {
  2)   0.164 us    |        down_read();
  2)   0.642 us    |      }
  2)   0.922 us    |    }
...
```

脚本主要设置下面几个trace参数：

```sh
# cat current_tracer 
function_graph
# cat set_graph_function 
xfs_dir_open [xfs]
```

另外，还可以通过`-p`指定进程ID，`-m`指定栈的深度。

```
-m maxdepth     # max stack depth to show
 -p PID          # trace when this pid is on-CPU
```

```sh
# ./funcgraph -m 3 "xfs_dir_open"  
Tracing "xfs_dir_open"... Ctrl-C to end.
 18)               |  xfs_dir_open [xfs]() {
 18)   0.103 us    |    xfs_file_open [xfs]();
 18)               |    xfs_ilock_map_shared [xfs]() {
 18)   0.264 us    |      xfs_ilock [xfs]();
 18)   0.609 us    |    }
 18)               |    xfs_dir3_data_readahead [xfs]() {
 18)   6.987 us    |      xfs_da_reada_buf [xfs]();
 18)   7.683 us    |    }
 18)               |    xfs_iunlock [xfs]() {
 18)   0.034 us    |      up_read();
 18)   0.446 us    |    }
 18) + 10.990 us   |  }
^C
Ending tracing...
```

这两个参数分别对应`tracing/set_ftrace_pid`和`tracing/max_graph_depth`。

## tpoint

tpoint - trace a given tracepoint. Static tracing

跟踪内核的静态tracepoint，tracepoint可以输出相关的内核变量的值，这一点是非常有用的。

内核所有的tracepoint都在`/sys/kernel/debug/tracing/events`目录下，例如，与信号相关的有2个signal：

```sh
# ls /sys/kernel/debug/tracing/events/signal/
enable  filter  signal_deliver  signal_generate
```

`signal_generate`跟踪进程发送信号，在内核函数`__send_signal`中创建，所有信号的发送都会调用该函数：

```sh
# ./tpoint -p 32309 signal:signal_generate
Tracing signal:signal_generate. Ctrl-C to end.
     test_signal-32309 [011] 51151417.094942: signal_generate: sig=27 errno=0 code=128 comm=test_signal pid=32309 grp=1 res=0
     test_signal-32309 [011] 51151418.097461: signal_generate: sig=27 errno=0 code=128 comm=test_signal pid=32309 grp=1 res=0
           <...>-32309 [011] 51151419.099980: signal_generate: sig=27 errno=0 code=128 comm=test_signal pid=32309 grp=1 res=0
           <...>-32309 [011] 51151419.642213: signal_generate: sig=17 errno=0 code=262146 comm=bash pid=26674 grp=1 res=0
^C
Ending tracing...
```

另外，还可以通过参数`-s`显示函数的调用栈(实际上是ftrace提供的)：

```
-s              # show kernel stack traces
```

一般来说，一个函数可能会被多个父函数调用，这可以方便分析当前调用产生的路径，结合相关变量的值可以帮助我们分析问题。

```sh
# ./tpoint -s -p 10213 signal:signal_generate     
Tracing signal:signal_generate. Ctrl-C to end.
     test_signal-10213 [020] 51151620.771581: signal_generate: sig=27 errno=0 code=128 comm=test_signal pid=10213 grp=1 res=0
     test_signal-10213 [020] 51151620.771582: <stack trace>
 => send_signal
 => __group_send_sig_info
 => check_cpu_itimer
 => run_posix_cpu_timers
 => update_process_times
 => tick_sched_timer
 => __run_hrtimer
 => hrtimer_interrupt
```

设置的主要tracing参数：

```sh
# cat options/stacktrace 
1
# cat events/signal/signal_generate/enable 
1
# cat events/signal/signal_generate/filter 
common_pid == 10213
```

## kprobe

kprobe - trace a given kprobe definition. Kernel dynamic tracing.

`kprobe`基于内核的[kprobe](https://www.kernel.org/doc/Documentation/trace/kprobetrace.txt)，非常强大，可以做一些动态跟踪的事情，`tpoint`只能分析内核静态的tracepoint，而`kprobe`可以分析内核所有函数。

比如，查看`xfs_dir_open`的调用栈和返回值：

```sh
# ./kprobe -s 'r:xfs_dir_open $retval' 
xfs_dir_open [xfs]
Tracing kprobe xfs_dir_open. Ctrl-C to end.
              ls-32222 [002] d... 1561178.430757: xfs_dir_open: (do_dentry_open+0x20e/0x290 <- xfs_dir_open) arg1=0
              ls-32222 [002] d... 1561178.430761: <stack trace>
 => vfs_open
 => do_last
 => path_openat
 => do_filp_open
 => do_sys_open
 => SyS_open
 => system_call_fastpath
^C
Ending tracing...
```

动态跟踪的内核接口为`kprobe_events`:

```sh
# cat kprobe_events 
r:kprobes/xfs_dir_open xfs_dir_open arg1=$retval
# ls events/kprobes/
enable  filter  xfs_dir_open
```

kprobe定义event的格式如下：

```
Synopsis of kprobe_events
-------------------------
  p[:[GRP/]EVENT] [MOD:]SYM[+offs]|MEMADDR [FETCHARGS]	: Set a probe
  r[:[GRP/]EVENT] [MOD:]SYM[+0] [FETCHARGS]		: Set a return probe
  -:[GRP/]EVENT						: Clear a probe

 GRP		: Group name. If omitted, use "kprobes" for it.
 EVENT		: Event name. If omitted, the event name is generated
		  based on SYM+offs or MEMADDR.
 MOD		: Module name which has given SYM.
 SYM[+offs]	: Symbol+offset where the probe is inserted.
 MEMADDR	: Address where the probe is inserted.

 FETCHARGS	: Arguments. Each probe can have up to 128 args.
  %REG		: Fetch register REG
  @ADDR		: Fetch memory at ADDR (ADDR should be in kernel)
  @SYM[+|-offs]	: Fetch memory at SYM +|- offs (SYM should be a data symbol)
  $stackN	: Fetch Nth entry of stack (N >= 0)
  $stack	: Fetch stack address.
  $retval	: Fetch return value.(*)
  $comm		: Fetch current task comm.
  +|-offs(FETCHARG) : Fetch memory at FETCHARG +|- offs address.(**)
  NAME=FETCHARG : Set NAME as the argument name of FETCHARG.
  FETCHARG:TYPE : Set TYPE as the type of FETCHARG. Currently, basic types
		  (u8/u16/u32/u64/s8/s16/s32/s64), hexadecimal types
		  (x8/x16/x32/x64), "string" and bitfield are supported.

  (*) only for return probe.
  (**) this is useful for fetching a field of data structures.
```

参考[Kprobe-based Event Tracing](https://www.kernel.org/doc/Documentation/trace/kprobetrace.txt)。直接使用内核的接口`/sys/kernel/debug/tracing/kprobe_events`来定义高级格式的event需要对熟悉内核二进制程序，不太方便，我们可以更加方便的内核自带的perf工具提供的`perf probe`定义一些更加复杂的kprobe event。


## perf probe

内核自带`tools/perf`工具，我们可以使用`perf probe`命令动态添加trace event。

首先，确认定义trace event的源代码，假设我们想trace内核函数`dump_write`，使用`perf probe --line dump_write`可以显示源代码以及显示源代码的哪一行可以插入event:

```sh
# perf probe --line dump_write 
<dump_write@/usr/src/kernels/kernel-tlinux2-3.10.102//fs/coredump.c:0>
      0  int dump_write(struct file *file, const void *addr, int nr)
      1  {
      2         return !dump_interrupted() &&
      3                 access_ok(VERIFY_READ, addr, nr) &&
      4                 file->f_op->write(file, addr, nr, &file->f_pos) == nr;
      5  }
         EXPORT_SYMBOL(dump_write);
         
         int dump_seek(struct file *file, loff_t off)
```

`perf probe --vars`可以查看指定函数某一行可以访问的变量：

```sh
# perf probe --vars dump_write:0                                            
Available variables at dump_write
        @<dump_write+0>
                (unknown_type)  addr
                int     nr
                struct file*    file
```

使用`perf probe --add`可以定义trace event:

```sh
# perf probe --add 'dump_write:0 nr file->f_path.dentry->d_name.name:string'   
Added new event:
  probe:dump_write     (on dump_write with nr name=file->f_path.dentry->d_name.name:string)

You can now use it in all perf tools, such as:

        perf record -e probe:dump_write -aR sleep 1

# perf probe --list
  probe:dump_write     (on dump_write@fs/coredump.c with nr name)
# ls events/probe/  
dump_write  enable  filter
```

定义trace event的格式如下:

(1)根据函数名定义时:

> [事件名=]函数[@文件][:从函数开头的行数 | +偏移量 | %return |;模式]

(2)根据文件名和行数定义时:

> [事件名=]文件[: 从文件开头的行数|; 模式]

开始trace:

```sh
# echo probe:dump_write > set_event

# head trace -n 20
# tracer: nop
#
# entries-in-buffer/entries-written: 22249/526438   #P:48
#
#                              _-----=> irqs-off
#                             / _----=> need-resched
#                            | / _---=> hardirq/softirq
#                            || / _--=> preempt-depth
#                            ||| /     delay
#           TASK-PID   CPU#  ||||    TIMESTAMP  FUNCTION
#              | |       |   ||||       |         |
           <...>-41773 [002] d...  7171.883693: dump_write: (dump_write+0x0/0x70) nr=4096 name="core_test_signal_1482493290.41773"
           <...>-41773 [002] d...  7171.883695: dump_write: (dump_write+0x0/0x70) nr=4096 name="core_test_signal_1482493290.41773"
           <...>-41773 [002] d...  7171.883698: dump_write: (dump_write+0x0/0x70) nr=4096 name="core_test_signal_1482493290.41773"
           <...>-41773 [002] d...  7171.883700: dump_write: (dump_write+0x0/0x70) nr=4096 name="core_test_signal_1482493290.41773"
           <...>-41773 [002] d...  7171.883703: dump_write: (dump_write+0x0/0x70) nr=4096 name="core_test_signal_1482493290.41773"
```

可以看到`dump_write`写的文件名称以及nr的值。

delete probe event:

```sh
# perf probe --list
  probe:dump_write     (on dump_write@fs/coredump.c with nr name)
# echo > set_event
# perf probe --del probe:dump_write
Removed event: probe:dump_write
```

## Summary

从4.x开始，内核社区开始转向使用[BPF](http://thenewstack.io/long-last-linux-gets-dynamic-tracing/)实现内核的动态跟踪。但4.x的内核要在生产环境推广还需要一段时间。对于3.X的内核，`perf-tools`以及内核自带的`perf`是我们定位内核问题的最佳选择。

## Reference

* [ftrace - Function Tracer](https://www.kernel.org/doc/Documentation/trace/ftrace.txt)
* [A look at ftrace](https://lwn.net/Articles/322666/)
* [ftrace 简介](https://www.ibm.com/developerworks/cn/linux/l-cn-ftrace/)
* [使用 ftrace 调试 Linux 内核，第 1 部分](http://www.ibm.com/developerworks/cn/linux/l-cn-ftrace1/)