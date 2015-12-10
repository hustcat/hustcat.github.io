---
layout: post
title: fork systemcall hanged cause by gprof 
date: 2015-12-10 18:16:30
categories: Linux
tags: 
excerpt: fork systemcall hanged cause by gprof 
---

# 问题
某业务反应程序执行fork时一直卡住，并导致CPU 100%：

![](/assets/2015-12-10-fork-problem-1.png)

使用gdb，发现调用栈处于一直处于fork：

```
(gdb) bt
#0  0x00007f754d6bdb1d in fork () from /lib64/libc.so.6
#1  0x00000000010c52ad in main (argc=2, argv=0x7fff79777398) at ../src/main/writesth.cpp:1465
```

# 原因分析

使用perf来看，内核一直在执行copy_pte_range：
 
![](/assets/2015-12-10-fork-problem-2.png)

使用strace，发现fork一直返回ERESTARTNOINTR：

```sh
#strace -p 25214
Process 25214 attached - interrupt to quit
--- SIGPROF (Profiling timer expired) @ 0 (0) ---
rt_sigreturn(0x1b)                      = 56
clone(child_stack=0, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, child_tidptr=0x7f754ef0daf0) = ? ERESTARTNOINTR (To be restarted)
--- SIGPROF (Profiling timer expired) @ 0 (0) ---
rt_sigreturn(0x1b)                      = 56
clone(child_stack=0, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, child_tidptr=0x7f754ef0daf0) = ? ERESTARTNOINTR (To be restarted)
--- SIGPROF (Profiling timer expired) @ 0 (0) ---
rt_sigreturn(0x1b)                      = 56
clone(child_stack=0, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, child_tidptr=0x7f754ef0daf0) = ? ERESTARTNOINTR (To be restarted)
--- SIGPROF (Profiling timer expired) @ 0 (0) ---
```

从[这里](https://bugzilla.redhat.com/show_bug.cgi?id=645528)来看，初步断定是由于程序在编译时使用了-pg所致。

如果程序使用-pg，每隔10ms就会触发SIGPROF，如果父进程申请了大量内存，在fork时，就会导致fork执行时间相对较长，SIGPROF就会中断fork系统调用，导致fork返回ERESTARTNOINTR:

```c
static struct task_struct *copy_process(unsigned long clone_flags,
					unsigned long stack_start,
					struct pt_regs *regs,
					unsigned long stack_size,
					int __user *child_tidptr,
					struct pid *pid,
					int trace)
...
	if ((retval = copy_mm(clone_flags, p)))
		goto bad_fork_cleanup_signal;
...
	/*
	 * Process group and session signals need to be delivered to just the
	 * parent before the fork or both the parent and the child after the
	 * fork. Restart if a signal comes in before we add the new process to
	 * it's process group.
	 * A fatal signal pending means that current will exit, so the new
	 * thread can't slip out of an OOM kill (or normal SIGKILL).
 	 */
	recalc_sigpending();
	if (signal_pending(current)) {
		spin_unlock(&current->sighand->siglock);
		write_unlock_irq(&tasklist_lock);
		retval = -ERESTARTNOINTR;
		goto bad_fork_free_pid;
	}
```

内核在处理信号时，发现返回值为ERESTARTNOINTR，就会重新调用fork：

do_signal -> handle_signal:

```c
static int
handle_signal(unsigned long sig, siginfo_t *info, struct k_sigaction *ka,
	      sigset_t *oldset, struct pt_regs *regs)
{
	int ret;

	/* Are we from a system call? */
	if (syscall_get_nr(current, regs) >= 0) {
		/* If so, check system call restarting.. */
		switch (syscall_get_error(current, regs)) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->ax = -EINTR;
			break;

		case -ERESTARTSYS:
			if (!(ka->sa.sa_flags & SA_RESTART)) {
				regs->ax = -EINTR;
				break;
			}
		/* fallthrough */
		case -ERESTARTNOINTR:
			regs->ax = regs->orig_ax;
			regs->ip -= 2;
			break;
		}
	}
...
```

注意到，对于ERESTARTNOINTR，内核会将ip减去2，由于int $0x80和sysenter都是2个字节，使得epi重新指向系统调用的指令。这样，当内核执行完信号处理函数后，又会重新执行被中断的系统调用。

# 测试程序
该问题可以通过下面的程序复现：

```
//gcc -o test_fork -pg test_fork.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main()
{
long sz = 1*1024*1024*1024;
char *p = (char*)malloc(sz);
memset(p, 0, sz);
fork();
return 0;
}
```

# SIGGRPOF

gprof是通过[setitmer](http://man7.org/linux/man-pages/man2/setitimer.2.html)设置定时器，每隔10000us(10ms)就会向进程发送SIGPROF，并在信号处理函数完成计数。

```
rt_sigaction(SIGPROF, {0x7f507f23ebd0, ~[], SA_RESTORER|SA_RESTART, 0x7f507f1859a0}, {SIG_DFL, [], 0}, 8) = 0
setitimer(ITIMER_PROF, {it_interval={0, 10000}, it_value={0, 10000}}, {it_interval={0, 0}, it_value={0, 0}}) = 0
```

# 主要参考

* [SIGPROF keeps a large task from ever completing a fork()](https://bugzilla.redhat.com/show_bug.cgi?id=645528)
* [GNU gprof](http://www.delorie.com/gnu/docs/binutils/gprof_25.html)
* [gprof Profiling Tools](https://www.alcf.anl.gov/user-guides/gprof-profiling-tools)
