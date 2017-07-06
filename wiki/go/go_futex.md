## Semaphore in runtime

```
// runtime/sema.goc
typedef struct SemaWaiter SemaWaiter;
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
	SemaWaiter*	head;
	SemaWaiter*	tail;
	// Number of waiters. Read w/o the lock.
	uint32 volatile	nwait;
};

// Prime to not correlate with any user patterns.
#define SEMTABLESZ 251

struct semtable
{
	SemaRoot;
	uint8 pad[CacheLineSize-sizeof(SemaRoot)]; ///cache line
};
#pragma dataflag NOPTR /* mark semtable as 'no pointers', hiding from garbage collector */
static struct semtable semtable[SEMTABLESZ]; ///最多251个sema ???

void
runtime·semacquire(uint32 volatile *addr, bool profile)
{
	SemaWaiter s;	// Needs to be allocated on stack, otherwise garbage collector could deallocate it
	SemaRoot *root;
	int64 t0;
	
	// Easy case.
	if(cansemacquire(addr))
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
		runtime·lock(root); ///lock for change root
		// Add ourselves to nwait to disable "easy case" in semrelease.
		runtime·xadd(&root->nwait, 1);
		// Check cansemacquire to avoid missed wakeup.
		if(cansemacquire(addr)) {
			runtime·xadd(&root->nwait, -1);
			runtime·unlock(root);
			return;
		}
		// Any semrelease after the cansemacquire knows we're waiting
		// (we set nwait above), so go to sleep.
		semqueue(root, addr, &s);
		runtime·parkunlock(root, "semacquire"); ///unlock
		if(cansemacquire(addr)) {
			if(t0)
				runtime·blockevent(s.releasetime - t0, 3);
			return;
		}
	}
}

func runtime_Semacquire(addr *uint32) {
	runtime·semacquire(addr, true);
}

func runtime_Semrelease(addr *uint32) {
	runtime·semrelease(addr);
}
```

## futex in runtime

```
// runtime.h
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
```

* runtime·lock

Linux下的`runtime·lock`是在`runtime/lock_futex.c`中实现的。

```
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
	v = runtime·xchg((uint32*)&l->key, MUTEX_LOCKED);
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
			runtime·procyield(ACTIVE_SPIN_CNT);
		}

		// Try for lock, rescheduling.
		for(i=0; i < PASSIVE_SPIN; i++) {
			while(l->key == MUTEX_UNLOCKED)
				if(runtime·cas((uint32*)&l->key, MUTEX_UNLOCKED, wait))
					return;
			runtime·osyield();
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

* runtime·futexsleep

```
// os_linux.c
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
```

* runtime·futex

```
// sys_linux_amd64.s
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

## futex system call

```
//kernel/futex.c
SYSCALL_DEFINE6(futex, u32 __user *, uaddr, int, op, u32, val,
		struct timespec __user *, utime, u32 __user *, uaddr2,
		u32, val3)
{
	struct timespec ts;
	ktime_t t, *tp = NULL;
	u32 val2 = 0;
	int cmd = op & FUTEX_CMD_MASK;

	if (utime && (cmd == FUTEX_WAIT || cmd == FUTEX_LOCK_PI ||
		      cmd == FUTEX_WAIT_BITSET ||
		      cmd == FUTEX_WAIT_REQUEUE_PI)) {
		if (copy_from_user(&ts, utime, sizeof(ts)) != 0)
			return -EFAULT;
		if (!timespec_valid(&ts))
			return -EINVAL;

		t = timespec_to_ktime(ts);
		if (cmd == FUTEX_WAIT)
			t = ktime_add_safe(ktime_get(), t);
		tp = &t;
	}
	/*
	 * requeue parameter in 'utime' if cmd == FUTEX_*_REQUEUE_*.
	 * number of waiters to wake in 'utime' if cmd == FUTEX_WAKE_OP.
	 */
	if (cmd == FUTEX_REQUEUE || cmd == FUTEX_CMP_REQUEUE ||
	    cmd == FUTEX_CMP_REQUEUE_PI || cmd == FUTEX_WAKE_OP)
		val2 = (u32) (unsigned long) utime;

	return do_futex(uaddr, op, val, tp, uaddr2, val2, val3);
}
```

process stack:

```
[<ffffffff8109c324>] futex_wait_queue_me+0xc4/0x100
[<ffffffff8109e5bc>] futex_wait+0x18c/0x270
[<ffffffff8109f2bf>] do_futex+0xaf/0x1b0
[<ffffffff8109f43d>] SyS_futex+0x7d/0x170
[<ffffffff81ac755b>] tracesys+0xdd/0xe2
[<ffffffffffffffff>] 0xffffffffffffffff
```

