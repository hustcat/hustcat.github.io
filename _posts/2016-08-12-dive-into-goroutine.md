---
layout: post
title: Dive into goroutine internal
date: 2016-08-12 17:00:30
categories: 编程语言
tags: golang
excerpt: Dive into goroutine internal
---

Goroutine的[实现](http://morsmachine.dk/go-scheduler)中有一些简单的概念。

M:对应一个OS线程，执行goroutine封装成的任务(这个任务有自己的代码逻辑、栈、程序计数器)

G:对应一个goroutine，相当于一个个的任务

P:调度器，维护调度上下文信息(context)和任务(goroutine)的运行队列(runqueues)，并交给M执行。更确切的说，是M从调度器取任务执行。

M/G/P的关系：

![](/assets/goroutine/MGP1.jpg)

简单来说，goroutine的内部实现就是一个线程池。一般来说，有M和G就行了，这里为什么还要搞一个P？

这也是goroutine内部实现一个精妙的地方，当一个M执行G发生阻塞时(比如goroutine中有系统调用)，可以将P转到别的M调度执行其它的G。

![](/assets/goroutine/MGP2.jpg)

go关键字创建一个goroutine，实际上会转化为`runtime.newproc`的调用：

```
go f(args)
```

可以看作

```
runtime.newproc(size, f, args)
```

每个goroutine都需要有一个自己的栈，G结构的sched字段维护了栈地址以及程序计数器等代码运行所需的基本信息。也就是说这个goroutine放弃cpu的时候需要保存这些信息，待下次重新获得cpu的时候，需要将这些信息装载到对应的cpu寄存器中。

函数到G的转换：

```go
struct	Gobuf
{
	// The offsets of sp, pc, and g are known to (hard-coded in) libmach.
	uintptr	sp;
	uintptr	pc;
	G*	g;
	void*	ctxt;
	uintreg	ret;
	uintptr	lr;
};

// adjust Gobuf as it if executed a call to fn with context ctxt
// and then did an immediate gosave.
void
runtime·gostartcall(Gobuf *gobuf, void (*fn)(void), void *ctxt)
{
	uintptr *sp;
	
	sp = (uintptr*)gobuf->sp;
	if(sizeof(uintreg) > sizeof(uintptr))
		*--sp = 0;
	*--sp = (uintptr)gobuf->pc;
	gobuf->sp = (uintptr)sp;
	gobuf->pc = (uintptr)fn;
	gobuf->ctxt = ctxt;
}
```

## 创建G

runtime·newproc创建一个G，然后放到P的运行队列。

```go
void
runtime·newproc(int32 siz, FuncVal* fn, ...)
{
	byte *argp;

	if(thechar == '5')
		argp = (byte*)(&fn+2);  // skip caller's saved LR
	else
		argp = (byte*)(&fn+1);
	runtime·newproc1(fn, argp, siz, 0, runtime·getcallerpc(&siz));
}

G*
runtime·newproc1(FuncVal *fn, byte *argp, int32 narg, int32 nret, void *callerpc)
{
	/// get G
...
	p = m->p;
	if((newg = gfget(p)) != nil) {
		if(newg->stackguard - StackGuard != newg->stack0)
			runtime·throw("invalid stack in newg");
	} else {
		newg = runtime·malg(StackMin);
		allgadd(newg);
	}
...
	///设置相关字段
	newg->sched.sp = (uintptr)sp;
	newg->sched.pc = (uintptr)runtime·goexit;
	newg->sched.g = newg;
	runtime·gostartcallfn(&newg->sched, fn);
	newg->gopc = (uintptr)callerpc;
	newg->status = Grunnable;
...
	///Try to put g on local runnable queue
	runqput(p, newg);
...
```

## 创建M

当G太多，M太少，还有空闲的P的时候，就创建一些新的M，去执行G：

```
// Create a new m.  It will start off with a call to fn, or else the scheduler.
static void
newm(void(*fn)(void), P *p){
	M *mp;

	mp = runtime·allocm(p);
	mp->nextp = p;
	mp->mstartfn = fn;	
...
	runtime·newosproc(mp, (byte*)mp->g0->stackbase);
}

//os_linux.c
void
runtime·newosproc(M *mp, void *stk)
{
	int32 ret;
	int32 flags;
	Sigset oset;

	/*
	 * note: strace gets confused if we use CLONE_PTRACE here.
	 */
	flags = CLONE_VM	/* share memory */
		| CLONE_FS	/* share cwd, etc */
		| CLONE_FILES	/* share fd table */
		| CLONE_SIGHAND	/* share sig handler table */
		| CLONE_THREAD	/* revisit - okay for now */
		;

	mp->tls[0] = mp->id;	// so 386 asm can find it
	if(0){
		runtime·printf("newosproc stk=%p m=%p g=%p clone=%p id=%d/%d ostk=%p\n",
			stk, mp, mp->g0, runtime·clone, mp->id, (int32)mp->tls[0], &mp);
	}

	// Disable signals during clone, so that the new thread starts
	// with signals disabled.  It will enable them in minit.
	runtime·rtsigprocmask(SIG_SETMASK, &sigset_all, &oset, sizeof oset);
	ret = runtime·clone(flags, stk, mp, mp->g0, runtime·mstart);
	runtime·rtsigprocmask(SIG_SETMASK, &oset, nil, sizeof oset);

	if(ret < 0) {
		runtime·printf("runtime: failed to create new OS thread (have %d already; errno=%d)\n", runtime·mcount(), -ret);
		runtime·throw("runtime.newosproc");
	}
}
```

可以看到，OS线程实际上是执行的runtime·mstart函数：

```go
// Called to start an M.
void
runtime·mstart(void)
{
	// Install signal handlers; after minit so that minit can
	// prepare the thread to be able to handle the signals.
	if(m == &runtime·m0)
		runtime·initsig();
	
	if(m->mstartfn)
		m->mstartfn();
	if(m->helpgc) {
		m->helpgc = 0;
		stopm();
	} else if(m != &runtime·m0) {
		acquirep(m->nextp);
		m->nextp = nil;
	}
	schedule(); ///调度
}
```	

schedule()进行任务(goroutine)调度。

### 调度M

```go
// One round of scheduler: find a runnable goroutine and execute it.
// Never returns.
static void
schedule(void)
{
	G *gp;
	uint32 tick;
...
	// by constantly respawning each other.
	tick = m->p->schedtick;
	// This is a fancy way to say tick%61==0,
	// it uses 2 MUL instructions instead of a single DIV and so is faster on modern processors.
	if(tick - (((uint64)tick*0x4325c53fu)>>36)*61 == 0 && runtime·sched.runqsize > 0) {
		runtime·lock(&runtime·sched);
		gp = globrunqget(m->p, 1);
		runtime·unlock(&runtime·sched);
		if(gp)
			resetspinning();
	}
	if(gp == nil) {
		gp = runqget(m->p);
		if(gp && m->spinning)
			runtime·throw("schedule: spinning with local work");
	}
	if(gp == nil) {
		gp = findrunnable();  // blocks until work is available
		resetspinning();
	}

...
	execute(gp);  ///运行G
}
```

### 运行G

```go
// Schedules gp to run on the current M.
// Never returns.
static void
execute(G *gp)
{
	int32 hz;

	if(gp->status != Grunnable) {
		runtime·printf("execute: bad g status %d\n", gp->status);
		runtime·throw("execute: bad g status");
	}
	gp->status = Grunning;
	gp->waitsince = 0;
	gp->preempt = false;
	gp->stackguard0 = gp->stackguard;
	m->p->schedtick++;
	m->curg = gp;
	gp->m = m;

	// Check whether the profiler needs to be turned on or off.
	hz = runtime·sched.profilehz;
	if(m->profilehz != hz)
		runtime·resetcpuprofiler(hz);

	runtime·gogo(&gp->sched);
}
```

```
///asm_amd64.s

// void gogo(Gobuf*)
// restore state from Gobuf; longjmp
TEXT runtime·gogo(SB), NOSPLIT, $0-8
	MOVQ	8(SP), BX		// gobuf
	MOVQ	gobuf_g(BX), DX
	MOVQ	0(DX), CX		// make sure g != nil
	get_tls(CX)
	MOVQ	DX, g(CX)
	MOVQ	gobuf_sp(BX), SP	// restore SP
	MOVQ	gobuf_ret(BX), AX
	MOVQ	gobuf_ctxt(BX), DX
	MOVQ	$0, gobuf_sp(BX)	// clear to help garbage collector
	MOVQ	$0, gobuf_ret(BX)
	MOVQ	$0, gobuf_ctxt(BX)
	MOVQ	gobuf_pc(BX), BX
	JMP	BX
```

## 调度时机

* goroutine结束

很显然，当M运行完一个G后，会调用`schedule`运行一个新的G:

```c
void
runtime·goexit(void)
{
	if(g->status != Grunning)
		runtime·throw("bad g status");
	if(raceenabled)
		runtime·racegoend();
	runtime·mcall(goexit0);
}

// runtime·goexit continuation on g0.
static void
goexit0(G *gp)
{
...
	schedule();
}
```

* runtime·park

goroutine可以执行`runtime·park`挂起自己，M会运行一个新的G:

```
// Puts the current goroutine into a waiting state and calls unlockf.
// If unlockf returns false, the goroutine is resumed.
void
runtime·park(bool(*unlockf)(G*, void*), void *lock, int8 *reason)
{
	if(g->status != Grunning)
		runtime·throw("bad g status");
	m->waitlock = lock;
	m->waitunlockf = unlockf;
	g->waitreason = reason;
	runtime·mcall(park0);
}

// runtime·park continuation on g0.
static void
park0(G *gp)
{
	bool ok;

	gp->status = Gwaiting;
	gp->m = nil;
	m->curg = nil;
	if(m->waitunlockf) {
		ok = m->waitunlockf(gp, m->waitlock);
		m->waitunlockf = nil;
		m->waitlock = nil;
		if(!ok) {
			gp->status = Grunnable;
			execute(gp);  // Schedule it back, never returns.
		}
	}
	if(m->lockedg) {
		stoplockedm();
		execute(gp);  // Never returns.
	}
	schedule(); ///调度
}
```

* 系统调用

## LockOSThread

因为goroutine和OS线程并不是一一对应的关系。在一些场景下，我们希望goroutine在同一个OS线程中运行，比如goroutine使用了[线程局部存储](https://github.com/golang/go/wiki/LockOSThread)，[LockOSThread](https://golang.org/pkg/runtime/#LockOSThread)能够保证goroutine在同一个OS线程中运行：

> LockOSThread wires the calling goroutine to its current operating system thread. Until the calling
> goroutine exits or calls UnlockOSThread, it will always execute in that thread, and no other
> goroutine can.

```
struct	G
{
...
	M*	lockedm;
...
}

struct	M
{
...
	uint32	locked;		// tracking for LockOSThread
	G*	lockedg;
...
}
```


```c
// lockOSThread is called by runtime.LockOSThread and runtime.lockOSThread below
// after they modify m->locked. Do not allow preemption during this call,
// or else the m might be different in this function than in the caller.
#pragma textflag NOSPLIT
static void
lockOSThread(void)
{
	m->lockedg = g;
	g->lockedm = m;
}

void
runtime·LockOSThread(void)
{
	m->locked |= LockExternal;
	lockOSThread();
}

void
runtime·lockOSThread(void)
{
	m->locked += LockInternal;
	lockOSThread();
}
```

在调度G的时候，如果发现G->lockedm被设置，则会将G(实际上是关联的P)转给对应的M:
```c
// One round of scheduler: find a runnable goroutine and execute it.
// Never returns.
static void
schedule(void)
{
...
	if(gp->lockedm) {
		// Hands off own p to the locked m,
		// then blocks waiting for a new p.
		startlockedm(gp);
		goto top;
	}
...
}


// Schedules the locked m to run the locked gp.
static void
startlockedm(G *gp)
{
	M *mp;
	P *p;
	//锁定的M
	mp = gp->lockedm;
	if(mp == m)
		runtime·throw("startlockedm: locked to me");
	if(mp->nextp)
		runtime·throw("startlockedm: m has p");
	// directly handoff current P to the locked m
	incidlelocked(-1);
	//当前M与P解除绑定
	p = releasep();

	//目标M与P绑定
	mp->nextp = p;

	//wakeup目标M
	runtime·notewakeup(&mp->park);

	//休眠当前M
	stopm();
}
```

当goroutine结束时，会解除M和G的这种锁定关系：

```
// runtime·goexit continuation on g0.
static void
goexit0(G *gp)
{
	gp->lockedm = nil;
	m->lockedg = nil;
	m->locked = 0;
...
}
```

考虑如下程序：

```go
func main{
	var wg sync.WaitGroup
	wg.Add(1)
	go func(){
		defer wg.Done()
		runtime.LockOSThread()
		// sleep
		time.Sleep(10 * time.Second)
	}()
	wg.Wait()
}
```

当主goroutine G0对应的M创建一个G1，并放到M->P的运行队列。G0等待，执行G1中的`runtime.LockOSThread`，G1被挂起。如果此时，M有锁定的G(G1)，M不会调度新的G运行，而将P转给别的M，然后陷入休眠状态。唤醒后重新执行G1。

```c
// runtime·park continuation on g0.
static void
park0(G *gp)
{
	bool ok;

	gp->status = Gwaiting;
	gp->m = nil;
	m->curg = nil;
...
	/// 有锁定的G
	if(m->lockedg) {
		stoplockedm(); ///休眠当前M
		execute(gp);  // Never returns.
	}
	schedule();
}


// Stops execution of the current m that is locked to a g until the g is runnable again.
// Returns with acquired P.
static void
stoplockedm(void)
{
	P *p;

	///将P转到其它M
	if(m->p) {
		// Schedule another M to run this p.
		p = releasep();
		handoffp(p);
	}
	incidlelocked(1);

	/// 陷入休眠
	// Wait until another thread schedules lockedg again.
	runtime·notesleep(&m->park);
	runtime·noteclear(&m->park);
	if(m->lockedg->status != Grunnable)
		runtime·throw("stoplockedm: not runnable");
	acquirep(m->nextp);
	m->nextp = nil;
}
```

从上面的分析可以看到，当goroutine执行`runtime.LockOSThread`后，该goroutine只会在锁定的M执行，同时，M也只会执行锁定的G，直到锁定的G执行结束。由于go语言中，goroutine与OS线程不是一一对应的，我们必须小心处理一些依赖于底层OS线程的场景，比如线程局部存储，再比如[net namespace](https://github.com/hustcat/sriov-cni/blob/master/vendor/github.com/containernetworking/cni/pkg/ns/ns.go#L286)，而`runtime.LockOSThread`保证了goroutine与OS线程的一一对应。

* time.Sleep

调用`time.Sleep`后，会让当前goroutine挂起，详细参考[这里](http://skoo.me/go/2013/09/12/go-runtime-timer/)。

```
// Sleep puts the current goroutine to sleep for at least ns nanoseconds.
func Sleep(ns int64) {
	runtime·tsleep(ns, "sleep");
}

// Put the current goroutine to sleep for ns nanoseconds.
void
runtime·tsleep(int64 ns, int8 *reason)
{
	Timer t;

	if(ns <= 0)
		return;

	t.when = runtime·nanotime() + ns;
	t.period = 0;
	t.fv = &readyv;
	t.arg.data = g;
	runtime·lock(&timers);
	addtimer(&t);
	runtime·parkunlock(&timers, reason);
}
```

## Reference

* [The Go scheduler](http://morsmachine.dk/go-scheduler)
* [Analysis of the Go runtime scheduler](http://www.cs.columbia.edu/~aho/cs6998/reports/12-12-11_DeshpandeSponslerWeiss_GO.pdf)
* [goroutine与调度器](http://skoo.me/go/2013/11/29/golang-schedule/)
* [以goroutine为例看协程的相关概念](http://wangzhezhe.github.io/blog/2016/02/17/golang-scheduler/)
* [3.2 go关键字](https://tiancaiamao.gitbooks.io/go-internals/content/zh/03.3.html)
