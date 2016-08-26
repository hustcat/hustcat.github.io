---
layout: post
title: Mutex in Go
date: 2016-08-25 16:00:30
categories: 编程语言
tags: golang
excerpt: Mutex in Go
---

[sync.Mutex](https://golang.org/pkg/sync/#Mutex.Lock)是Go提供给用户的互斥量接口。关于Mutex需要注意一点:

> A locked Mutex is not associated with a particular goroutine. It is allowed for one goroutine to lock a Mutex and then arrange for another goroutine to unlock it. 

更多参考[这里](https://golang.org/ref/mem#tmp_8)。

## Mutex.Lock()

```
func (m *Mutex) Lock()
```

Lock locks m. If the lock is already in use, the calling goroutine blocks until the mutex is available. 

```go
//sync/mutex.go

// A Mutex is a mutual exclusion lock.
// Mutexes can be created as part of other structures;
// the zero value for a Mutex is an unlocked mutex.
type Mutex struct {
	state int32
	sema  uint32
}

const (
	mutexLocked = 1 << iota // mutex is locked
	mutexWoken
	mutexWaiterShift = iota
)

// Lock locks m.
// If the lock is already in use, the calling goroutine
// blocks until the mutex is available.
func (m *Mutex) Lock() {
	// Fast path: grab unlocked mutex.
	if atomic.CompareAndSwapInt32(&m.state, 0, mutexLocked) {
		if raceenabled {
			raceAcquire(unsafe.Pointer(m))
		}
		return ///如果m.state==0, 则将m.state=1, 然后返回;表明Lock成功
	}

	///走到这里表明m.state != 0
	awoke := false
	for {
		old := m.state
		new := old | mutexLocked
		if old&mutexLocked != 0 {
			new = old + 1<<mutexWaiterShift //old == 1, new = 2
		}
		if awoke {
			// The goroutine has been woken from sleep,
			// so we need to reset the flag in either case.
			new &^= mutexWoken
		}
		if atomic.CompareAndSwapInt32(&m.state, old, new) { 
			if old&mutexLocked == 0 {
				break
			}
			runtime_Semacquire(&m.sema) ///block goroutine(调用runtime.park)
			awoke = true
		}
	}

	if raceenabled {
		raceAcquire(unsafe.Pointer(m))
	}
}
```

## runtime的信号量与自旋锁机制

`Mutex`依赖于runtime实现的信号量机制:

```c
//runtime/sema.goc

struct SemaWaiter
{
	uint32 volatile*	addr;
	G*	g;
	int64	releasetime;
	int32	nrelease;	// -1 for acquire
	SemaWaiter*	prev;
	SemaWaiter*	next;
};

typedef struct SemaRoot SemaRoot;
struct SemaRoot
{
	Lock;
	SemaWaiter*	head; ///goroutine等待队列
	SemaWaiter*	tail;
	// Number of waiters. Read w/o the lock.
	uint32 volatile	nwait; ///等待数量
};
```

* runtime_Semrelease

`runtime_Semrelease`相当于信号量的P操作:

```c
func runtime_Semrelease(addr *uint32) {
	runtime·semrelease(addr);
}


static int32
cansemacquire(uint32 *addr)
{
	uint32 v;

	while((v = runtime·atomicload(addr)) > 0)
		if(runtime·cas(addr, v, v-1)) ///compare and set, v=v-1
			return 1;
	return 0;
}

void
runtime·semacquire(uint32 volatile *addr, bool profile)
{
	SemaWaiter s;	// Needs to be allocated on stack, otherwise garbage collector could deallocate it
	SemaRoot *root;
	int64 t0;
	
	// Easy case.
	if(cansemacquire(addr)) /// P操作，获取信号量成功，则直接返回
		return;

	// Harder case:
	//	increment waiter count
	//	try cansemacquire one more time, return if succeeded
	//	enqueue itself as a waiter
	//	sleep
	//	(waiter descriptor is dequeued by signaler)
	root = semroot(addr);
	t0 = 0;
	s.releasetime = 0;
	if(profile && runtime·blockprofilerate > 0) {
		t0 = runtime·cputicks();
		s.releasetime = -1;
	}
	for(;;) {
		runtime·lock(root);
		// Add ourselves to nwait to disable "easy case" in semrelease.
		runtime·xadd(&root->nwait, 1); /// 等待数量加1
		// Check cansemacquire to avoid missed wakeup.
		if(cansemacquire(addr)) {
			runtime·xadd(&root->nwait, -1);
			runtime·unlock(root);
			return;
		}
		// Any semrelease after the cansemacquire knows we're waiting
		// (we set nwait above), so go to sleep.
		semqueue(root, addr, &s); ///将当前G加入等待队列
		runtime·parkunlock(root, "semacquire"); ///block当前G
		if(cansemacquire(addr)) {
			if(t0)
				runtime·blockevent(s.releasetime - t0, 3);
			return;
		}
	}
}
```

* runtime·cas

```
// runtime/asm_amd64.s
// bool cas(int32 *val, int32 old, int32 new)
// Atomically:
//	if(*val == old){
//		*val = new;
//		return 1;
//	} else
//		return 0;
TEXT runtime·cas(SB), NOSPLIT, $0-16
	MOVQ	8(SP), BX   # val -> BX
	MOVL	16(SP), AX  # old -> AX
	MOVL	20(SP), CX  # new -> CX
	LOCK
	CMPXCHGL	CX, 0(BX) # if (BX)==AX then (BX)=CX
	JZ 3(PC)
	MOVL	$0, AX
	RET
	MOVL	$1, AX
	RET
```

* runtime·lock

信号量机制依赖于自旋锁:

```
// runtime/runtime.h
/*
 * structures
 */
struct	Lock
{
	// Futex-based impl treats it as uint32 key,
	// while sema-based impl as M* waitm.
	// Used to be a union, but unions break precise GC.
	uintptr	key;
};

// runtime/lock_futex.c
// Possible lock states are MUTEX_UNLOCKED, MUTEX_LOCKED and MUTEX_SLEEPING.
// MUTEX_SLEEPING means that there is presumably at least one sleeping thread.
// Note that there can be spinning threads during all states - they do not
// affect mutex's state.
void
runtime·lock(Lock *l)
{
	uint32 i, v, wait, spin;

	if(m->locks++ < 0)
		runtime·throw("runtime·lock: lock count");

	// Speculative grab for lock.
	v = runtime·xchg((uint32*)&l->key, MUTEX_LOCKED); ///v=l->key, l->key=MUTEX_LOCKED, 
	if(v == MUTEX_UNLOCKED)
		return;

	// wait is either MUTEX_LOCKED or MUTEX_SLEEPING
	// depending on whether there is a thread sleeping
	// on this mutex.  If we ever change l->key from
	// MUTEX_SLEEPING to some other value, we must be
	// careful to change it back to MUTEX_SLEEPING before
	// returning, to ensure that the sleeping thread gets
	// its wakeup call.
	wait = v;

	// On uniprocessor's, no point spinning.
	// On multiprocessors, spin for ACTIVE_SPIN attempts.
	spin = 0;
	if(runtime·ncpu > 1)
		spin = ACTIVE_SPIN;

	for(;;) {
		// Try for lock, spinning.
		for(i = 0; i < spin; i++) {
			while(l->key == MUTEX_UNLOCKED)
				if(runtime·cas((uint32*)&l->key, MUTEX_UNLOCKED, wait))
					return;
			runtime·procyield(ACTIVE_SPIN_CNT); ///CPU执行PAUSE指令
		}

		// Try for lock, rescheduling.
		for(i=0; i < PASSIVE_SPIN; i++) {
			while(l->key == MUTEX_UNLOCKED)
				if(runtime·cas((uint32*)&l->key, MUTEX_UNLOCKED, wait))
					return;
			runtime·osyield(); ///执行G的M让出CPU
		}

		// Sleep.
		v = runtime·xchg((uint32*)&l->key, MUTEX_SLEEPING);
		if(v == MUTEX_UNLOCKED)
			return;
		wait = MUTEX_SLEEPING;
		runtime·futexsleep((uint32*)&l->key, MUTEX_SLEEPING, -1);
	}
}
```

`runtime·xchg`的实现:

```
TEXT runtime·xchg(SB), NOSPLIT, $0-12
	MOVQ	8(SP), BX  # val -> BX
	MOVL	16(SP), AX # new -> AX
	XCHGL	AX, 0(BX)
	RET
```

* runtime·procyield

`runtime·procyield`执行`PAUSE`指令:

```
TEXT runtime·procyield(SB),NOSPLIT,$0-0
	MOVL	8(SP), AX
again:
	PAUSE
	SUBL	$1, AX
	JNZ	again
	RET
```

* runtime·osyield

`runtime·osyield`执行系统调用，让系统线程让出CPU:

```
// runtime/sys_linux_asm64.s
TEXT runtime·osyield(SB),NOSPLIT,$0
	MOVL	$24, AX
	SYSCALL
	RET
```

* runtime·futexsleep

```
// runtime/os_linux.c
// Atomically,
//	if(*addr == val) sleep
// Might be woken up spuriously; that's allowed.
// Don't sleep longer than ns; ns < 0 means forever.
#pragma textflag NOSPLIT
void
runtime·futexsleep(uint32 *addr, uint32 val, int64 ns)
{
	Timespec ts;

	// Some Linux kernels have a bug where futex of
	// FUTEX_WAIT returns an internal error code
	// as an errno.  Libpthread ignores the return value
	// here, and so can we: as it says a few lines up,
	// spurious wakeups are allowed.

	if(ns < 0) {
		runtime·futex(addr, FUTEX_WAIT, val, nil, nil, 0);
		return;
	}
	// NOTE: tv_nsec is int64 on amd64, so this assumes a little-endian system.
	ts.tv_nsec = 0;
	ts.tv_sec = runtime·timediv(ns, 1000000000LL, (int32*)&ts.tv_nsec);
	runtime·futex(addr, FUTEX_WAIT, val, &ts, nil, 0);
}


// int64 futex(int32 *uaddr, int32 op, int32 val,
//	struct timespec *timeout, int32 *uaddr2, int32 val2);
TEXT runtime·futex(SB),NOSPLIT,$0
	MOVQ	8(SP), DI
	MOVL	16(SP), SI
	MOVL	20(SP), DX
	MOVQ	24(SP), R10
	MOVQ	32(SP), R8
	MOVL	40(SP), R9
	MOVL	$202, AX
	SYSCALL
	RET
```